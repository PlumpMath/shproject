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
static const int BACKLOG = SOMAXCONN;


static void on_read(struct waiter* waiter, void* buffer, ssize_t num_read);
static void request_done(struct waiter* waiter, void* buffer, ssize_t result);


static inline void exit_error(const char* str) {
    perror(str);
    exit(EXIT_FAILURE);
}


static inline void exit_error_num(const char* str, int error) {
    fprintf(stderr, "%s: %s\n", str, strerror(-error));
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
    bool keep_alive;

    http_parser_settings settings;
    http_parser parser;

    char read_buffer[512];
    char sendf_buffer[512];

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
    request->keep_alive = http_should_keep_alive(parser);

    return 0;
}


static ssize_t sendf(struct web_request* request, write_cb on_done, const char* format, ...) {
    size_t maxlen = sizeof(request->sendf_buffer);

    va_list list;
    va_start(list, format);
    size_t written = vsnprintf(request->sendf_buffer, maxlen, format, list);
    va_end(list);

    size_t length = written >= maxlen ? maxlen : written;
    return async_send(WAITER(request), request->sendf_buffer, length, MSG_NOSIGNAL, on_done);
}


const char* HEADERS_CLOSE_FORMAT = "HTTP/1.1 %s\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %zd\r\nConnection: close\r\n\r\n";
const char* HEADERS_KEEP_ALIVE_FORMAT = "HTTP/1.1 %s\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %zd\r\n\r\n";


static ssize_t send_http_headers(struct web_request* request, const char* status,
        size_t content_length, const char* content_type, bool keep_alive, write_cb on_done) {
    char buffer[64];

    time_t now = time(NULL);

    struct tm timeval;
    struct tm* utctime = gmtime_r(&now, &timeval);
    assert(utctime != NULL);

    char* timestr = asctime_r(&timeval, buffer);
    assert(timestr != NULL);
    timestr[strlen(timestr) - 1] = '\0';  // Remove newline

    const char* format = keep_alive ? HEADERS_KEEP_ALIVE_FORMAT : HEADERS_CLOSE_FORMAT;
    return sendf(request, on_done, format, status, timestr, content_type, content_length);
}


static void send_http_response_helper(struct waiter* waiter, void* buffer, ssize_t result) {
    struct web_request* request = REQUEST(waiter);

    if (result < 0) {
        exit_error_num("send_http_headers cb", result);
    }

    result = async_send(waiter, request->response_data, request->response_length,
        MSG_NOSIGNAL, request->on_response_done);

    if (result < 0) {
        request_done(waiter, NULL, result);
    }
}


static void send_http_response(struct web_request* request, const char* status,
        void* data, size_t length, const char* content_type, bool keep_alive, write_cb on_done) {
    request->response_data = data;
    request->response_length = length;
    request->on_response_done = on_done;

    ssize_t result = send_http_headers(request, status, length, content_type, keep_alive, send_http_response_helper);
    if (result < 0) {
        request_done(WAITER(request), NULL, result);
    }
}


const char HTML[] = "<html><head><title>Server</title></head><body><h1>they see me epollin', they hatin</h1></body></html>";


static void next_request(struct waiter* waiter, void* buffer, ssize_t result) {
    if (result < 0) {
        exit_error_num("next_request", result);
    }

    // Reset the request fields for the next request.
    struct web_request* request = REQUEST(waiter);
    request->url_length = 0;
    request->done = false;

    ssize_t num_read = async_recv(waiter, request->read_buffer,
        sizeof(request->read_buffer), 0, on_read);

    if (num_read < 0) {
        request_done(waiter, NULL, num_read);
    }
}


static void request_done(struct waiter* waiter, void* buffer, ssize_t result) {
    if (result < 0 && result != -EPIPE && result != -ECONNRESET) {
        exit_error_num("request_done cb", result);
    }

    struct web_request* request = REQUEST(waiter);

    result = close(request->sock);
    if (result != 0) {
        exit_error("sock close");
    }

    free(request);
}


static void on_read(struct waiter* waiter, void* buffer, ssize_t num_read) {
    if (num_read == -EPIPE || num_read == -ECONNRESET) {
        // Generally means we're on a keep-alive connection and the client
        // doesn't want to make another request - but we should check.
        request_done(waiter, NULL, 0);
        return;
    }

    if (num_read < 0) {
        exit_error_num("on_read", num_read);
    }

    struct web_request* request = REQUEST(waiter);

    size_t parsed = http_parser_execute(&request->parser, &request->settings,
        request->read_buffer, num_read);

    if (parsed != num_read) {
        const char* error = http_errno_name(request->parser.http_errno);
        fprintf(stderr, "Error on socket %d: %s\n", waiter->fd, error);
        request_done(waiter, NULL, 0);
        return;
    }

    if (parsed == 0 && !request->done) {
        // Client closed the connection. If they've already made a request and
        // this is a keep-alive connection, that's fine. We should check.
        request_done(waiter, NULL, 0);
        return;
    }

    if (!request->done) {
        // Keep reading
        num_read = async_recv(waiter, request->read_buffer, sizeof(request->read_buffer), 0,
            on_read);

        if (num_read < 0) {
            request_done(waiter, NULL, num_read);
        }
        return;
    }

    // Now we're writing
    struct http_parser_url parsed_url = {0};
    int result = http_parser_parse_url(request->url, request->url_length,
        false, &parsed_url);
    assert(result == 0);

    write_cb on_done = request->keep_alive ? next_request : request_done;

    if (request->parser.method == HTTP_GET) {
        send_http_response(request, "200 Seek", (void*)HTML, sizeof(HTML),
            "text/html", request->keep_alive, on_done);
    } else {
        send_http_response(request, "405 Method Not Allowed", "", 0,
            "text/plain", request->keep_alive, on_done);
    }
}


static void on_accept(struct waiter* waiter, int sock) {
    if (sock < 0) {
        exit_error_num("on_accept", sock);
    }

    // Kick off the accept again straight away so it can be serviced
    // by another thread.
    int error = async_accept(waiter, on_accept);
    if (error < 0) {
        exit_error_num("async_accept on_accept", error);
    }

    int reuse = 1;
    int result = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
    if (result == -1) {
        exit_error("setsockopt SO_REUSEADDR");
    }

    struct web_request* request = malloc(sizeof(*request));
    assert(request != NULL);

    async_init_waiter(&request->waiter, waiter->loop, sock);
    request->sock = sock;
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

    int error = async_accept(&waiter, on_accept);
    if (error < 0) {
        exit_error_num("async_accept on_accept", error);
    }

    main_loop((void*)&server);
    return 0;
}
