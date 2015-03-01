#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <unistd.h>


#include <http-parser/http_parser.h>
#include <webserver/eventlib.h>


static const int PORT = 6789;
static const int BACKLOG = 10;


static inline void exit_error(const char* str) {
    perror(str);
    exit(EXIT_FAILURE);
}


static inline void exit_error_num(const char* str, int error) {
    fprintf(stderr, "%s: %s\n", str, strerror(error));
    exit(EXIT_FAILURE);
}


struct web_server {
    struct loop loop;
    int sock;
};


struct web_request {
    struct waiter waiter;

    int sock;
    char url[128];
    size_t url_length;

    bool done;

    http_parser_settings settings;
    http_parser parser;

    char read_buffer[512];

    write_cb on_sendf_done;
    void* response_data;
    size_t response_length;
    write_cb on_response_done;
};


#define WAITER(r) (&(r)->waiter)
#define REQUEST(w) ((struct web_request*)(w))


int on_parse_url(http_parser* parser, const char* buffer, size_t length) {
    struct web_request* request = (struct web_request*)parser->data;

    // Out of space?
    if (request->url_length + length > sizeof(request->url)) {
        fprintf(stderr, "URL too long\n");
        return -1;
    }

    memcpy(&request->url[request->url_length], buffer, length);
    request->url_length += length;
    return 0;
}


int on_parse_done(http_parser* parser) {
    struct web_request* request = (struct web_request*)parser->data;
    request->url[request->url_length] = '\0';
    request->done = true;

    return 0;
}


static void sendf_done(struct waiter* waiter, void* buffer, ssize_t result) {
    if (result < 0) {
        exit_error_num("sendf cb", result);
    }

    free(buffer);
    struct web_request* request = REQUEST(waiter);
    request->on_sendf_done(waiter, NULL, 0);
}


#define SENDF_BUFFER 1024

static int sendf(struct web_request* request, write_cb on_done, const char* format, ...) {
    char* buffer = malloc(SENDF_BUFFER);
    assert(buffer);

    request->on_sendf_done = on_done;

    va_list list;
    va_start(list, format);
    size_t written = vsnprintf(buffer, SENDF_BUFFER, format, list);
    va_end(list);

    size_t length = written >= SENDF_BUFFER ? SENDF_BUFFER : written;
    return async_send(WAITER(request), buffer, length, MSG_NOSIGNAL, sendf_done);
}


static void send_http_headers(struct web_request* request, const char* status,
        size_t content_length, const char* content_type, write_cb on_done) {
    char buffer[64];

    time_t now = time(NULL);

    struct tm timeval;
    struct tm* utctime = gmtime_r(&now, &timeval);
    assert(utctime != NULL);

    char* timestr = asctime_r(&timeval, buffer);
    assert(timestr != NULL);
    timestr[strlen(timestr) - 1] = '\0';  // Remove newline

    sendf(request, on_done,
        "HTTP/1.1 %s\r\n"
        "Date: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zd\r\n"
        "Connection: close\r\n\r\n",
        status, timestr, content_type, content_length);
}


static void send_http_response_helper(struct waiter* waiter, void* buffer, ssize_t result) {
    struct web_request* request = REQUEST(waiter);

    if (result < 0) {
        exit_error_num("send_http_headers cb", result);
    }

    async_send(waiter, request->response_data, request->response_length, MSG_NOSIGNAL,
        request->on_response_done);
}


static void send_http_response(struct web_request* request, const char* status,
        void* data, size_t length, const char* content_type, write_cb on_done) {
    request->response_data = data;
    request->response_length = length;
    request->on_response_done = on_done;

    send_http_headers(request, status, length, content_type, send_http_response_helper);
}


static void send_random_done(struct waiter* waiter, void* buffer, ssize_t num_written) {
    struct web_request* request = REQUEST(waiter);

    if (num_written < 0) {
        exit_error_num("send_random cb", num_written);
    }

    free(buffer);

    request->on_response_done(waiter, NULL, 0);
}


static void send_http_random_helper(struct waiter* waiter, void* unused, ssize_t num_written) {
    struct web_request* request = REQUEST(waiter);

    if (num_written < 0) {
        exit_error_num("send_http_headers cb", num_written);
    }

    unsigned char* buffer = (unsigned char*)request->response_data;
    size_t random_size = request->response_length;

    int random_fd = open("/dev/urandom", O_RDONLY);
    if (random_fd < 0) {
        exit_error("open(/dev/urandom)");
    }

    // Don't bother with asynchronicity here to save on callback chaining
    size_t num_read = 0;
    while (num_read < random_size) {
        size_t to_read = random_size - num_read;
        ssize_t nread = read(random_fd, buffer + num_read, to_read);
        if (nread < 0) {
            fprintf(stderr, "Error reading from random: %s.\n", strerror(errno));
            close(random_fd);
            return;
        }

        for (size_t i = num_read; i < num_read + nread; i++) {
            buffer[i] = '0' + (buffer[i] % ('Z' - '0' + 1));
        }
        num_read += nread;
    }

    close(random_fd);

    async_send(waiter, buffer, random_size, MSG_NOSIGNAL, send_random_done);
}


static void send_http_random(struct web_request* request, size_t length, const char* content_type, write_cb on_done) {
    void* buffer = malloc(length);
    assert(buffer != NULL);

    request->response_data = buffer;
    request->response_length = length;
    request->on_response_done = on_done;

    send_http_headers(request, "200 OK", length, content_type, send_http_random_helper);
}


const char HTML[] = "<html><head><title>Server</title></head><body><h1>they see me epollin', they hatin</h1></body></html>";


static void request_done(struct waiter* waiter, void* buffer, ssize_t result) {
    if (result < 0) {
        exit_error_num("on_read", result);
    }

    struct web_request* request = REQUEST(waiter);
    close(request->sock);
    free(request);
}


static void on_read(struct waiter* waiter, void* buffer, ssize_t num_read) {
    if (num_read < 0) {
        exit_error_num("on_read", num_read);
    }

    struct web_request* request = REQUEST(waiter);

    size_t parsed = http_parser_execute(&request->parser, &request->settings,
        request->read_buffer, num_read);

    if (parsed != num_read) {
        const char* error = http_errno_name(request->parser.http_errno);
        exit_error(error);
    }

    if (parsed != 0 && !request->done) {
        // Keep reading
        async_recv(waiter, request->read_buffer, sizeof(request->read_buffer), 0,
            on_read);
        return;
    }

    // Now we're writing
    struct http_parser_url parsed_url = {0};
    int result = http_parser_parse_url(request->url, request->url_length,
        false, &parsed_url);
    assert(result == 0);

    if (request->parser.method == HTTP_GET) {
        //send_http_response(request, "200 Seek", (void*)HTML, sizeof(HTML), "text/html", request_done);
        send_http_random(request, 1024, "text/plain", request_done);
    } else {
        send_http_response(request, "405 Method Not Allowed", "", 0,
            "text/plain", request_done);
    }
}


static void on_accept(struct waiter* waiter, int sock) {
    if (sock < 0) {
        exit_error_num("on_accept", sock);
    }

    struct web_request* request = malloc(sizeof(*request));
    assert(request != NULL);

    async_init_waiter(&request->waiter, waiter->loop, sock);
    request->url_length = 0;
    request->done = false;

    http_parser_settings_init(&request->settings);
    request->settings.on_url = on_parse_url;
    request->settings.on_message_complete = on_parse_done;

    http_parser_init(&request->parser, HTTP_REQUEST);
    request->parser.data = request;

    ssize_t num_read = async_recv(WAITER(request), request->read_buffer,
        sizeof(request->read_buffer), 0, on_read);

    if (num_read < 0) {
        exit_error_num("async_recv", num_read);
    }

    // And kick off the accept again
    async_accept(waiter, on_accept);
}




void* main_loop(void* arg) {
    struct web_server* server = (struct web_server*)arg;

    int result = async_run_loop(&server->loop);
    if (result < 0) {
        exit_error_num("async_run_loop", result);
    }

    return NULL;
}


int main() {
    struct sockaddr_in localhost = {0};
    localhost.sin_family = AF_INET;
    localhost.sin_addr.s_addr = htonl(INADDR_ANY);
    localhost.sin_port = htons(PORT);

    struct web_server server;

    server.sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (server.sock == -1) {
        exit_error("server socket()");
    }

    int reuse = 1;

    int result = setsockopt(server.sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (result == -1) {
        exit_error("setsockopt SO_REUSEADDR");
    }

    result = bind(server.sock, (struct sockaddr*)&localhost, sizeof(localhost));
    if (result == -1) {
        exit_error("bind");
    }

    result = listen(server.sock, BACKLOG);
    if (result == -1) {
        exit_error("listen");
    }

    result = async_init_loop(&server.loop);
    if (result == -1) {
        exit_error("async_init_loop");
    }

    // Get CPU count
    cpu_set_t mask;
    result = sched_getaffinity(0, sizeof(mask), &mask);
    assert(result == 0);
    int cpu_count = CPU_COUNT(&mask);

    pthread_t threads[cpu_count - 1];

    for (int i = 0; i < cpu_count - 1; i++) {
        result = pthread_create(&threads[i], NULL, main_loop, (void*)&server);
        assert(result == 0);
    }

    // Init the accept waiter
    struct waiter waiter;
    async_init_waiter(&waiter, &server.loop, server.sock);

    async_accept(&waiter, on_accept);

    main_loop((void*)&server);
    return 0;
}
