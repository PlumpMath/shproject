#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef WEBSERVER_COROUTINES
#include <async.h>
#include <scheduler.h>
#else
#include <pthread.h>
#endif

#include <http-parser/http_parser.h>


static const int PORT = 6789;
static const int BACKLOG = SOMAXCONN;


static inline void exit_error(const char* str) {
    perror(str);
    exit(EXIT_FAILURE);
}


int send_all(int sock, const void* buffer, size_t size) {
    const char* buf = (const char*)buffer;

    while (size > 0) {
#if WEBSERVER_COROUTINES
        ssize_t sent = async_send(sock, buf, size, MSG_NOSIGNAL);
#else
        ssize_t sent = send(sock, buf, size, MSG_NOSIGNAL);
#endif
        if (sent == -1 && errno == EINTR) {
            continue;
        } else if (sent == -1) {
            return -1;
        }

        buf += sent;
        size -= sent;
    }
    return 0;
}


struct web_request {
    char url[128];
    size_t url_length;

    bool done;
    bool keep_alive;
};


int on_url(http_parser* parser, const char* buffer, size_t length) {
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


int on_done(http_parser* parser) {
    struct web_request* request = (struct web_request*)parser->data;
    request->url[request->url_length] = '\0';
    request->done = true;
    request->keep_alive = http_should_keep_alive(parser);

    return 0;
}


static int sendf(int sock, const char* format, ...) {
    char buffer[256];

    va_list list;
    va_start(list, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, list);
    va_end(list);

    int length = written >= sizeof(buffer) ? sizeof(buffer) - 1 : written;
    return send_all(sock, buffer, length);
}


const char* HEADERS_CLOSE_FORMAT = "HTTP/1.1 %s\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %zd\r\nConnection: close\r\n\r\n";
const char* HEADERS_KEEP_ALIVE_FORMAT = "HTTP/1.1 %s\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %zd\r\n\r\n";


static void send_http_headers(int sock, const char* status, size_t content_length,
        const char* content_type, bool keep_alive) {
    char buffer[64];

    time_t now = time(NULL);

    struct tm timeval;
    struct tm* utctime = gmtime_r(&now, &timeval);
    assert(utctime != NULL);

    char* timestr = asctime_r(&timeval, buffer);
    assert(timestr != NULL);
    timestr[strlen(timestr) - 1] = '\0';  // Remove newline

    const char* format = keep_alive ? HEADERS_KEEP_ALIVE_FORMAT : HEADERS_CLOSE_FORMAT;
    int result = sendf(sock, format, status, timestr, content_type, content_length);

    if (result != 0) {
        exit_error("sendf");
    }
}


static void send_http_response(int sock, const char* status, const void* data,
        size_t length, const char* content_type, bool keep_alive) {
    send_http_headers(sock, status, length, content_type, keep_alive);

    int result = send_all(sock, data, length);
    if (result != 0) {
        exit_error("send_all");
    }
}


const char HTML[] = "<html><head><title>Server</title></head><body><h1>they see me epollin', they hatin</h1></body></html>";


static ssize_t read_and_parse(int sock, struct web_request* request,
        http_parser* parser, http_parser_settings* settings) {
    char buf[256];

    while (true) {
#ifdef WEBSERVER_COROUTINES
        ssize_t num_read = async_recv(sock, buf, sizeof(buf), 0);
#else
        ssize_t num_read = recv(sock, buf, sizeof(buf), 0);
#endif
        if (num_read == -1) {
            exit_error("async_recv (server)");
        }

        size_t parsed = http_parser_execute(parser, settings, buf, num_read);
        if (parsed != num_read) {
            const char* error = http_errno_name(parser->http_errno);
            fprintf(stderr, "Error parsing socket %d: %s\n", sock, error);
            return -1;
        }

        if (parsed == 0 || request->done) {
            return parsed;
        }
    }
}


void* server_handler_coro(void* sock_ptr) {
    int sock = (int)(intptr_t)sock_ptr;

    http_parser_settings settings;
    http_parser_settings_init(&settings);
    settings.on_url = on_url;
    settings.on_message_complete = on_done;

    http_parser parser;
    http_parser_init(&parser, HTTP_REQUEST);

    struct web_request* request = malloc(sizeof(struct web_request));
    assert(request != NULL);

    request->keep_alive = true;
    parser.data = request;

    while (request->keep_alive) {
        request->url[0] = '\0';
        request->url_length = 0;
        request->done = false;
        request->keep_alive = false;

        ssize_t result = read_and_parse(sock, request, &parser, &settings);
        if (result == -1 || (result == 0 && !request->done)) {
            // Some non-fatal error - or peer closure
            break;
        }

        // Should be the case now after read_and_parse
        assert(request->done);

        struct http_parser_url parsed_url = {0};
        result = http_parser_parse_url(request->url, request->url_length,
            false, &parsed_url);
        assert(result == 0);

        if (parser.method == HTTP_GET) {
            send_http_response(sock, "200 Seek", (void*)HTML, sizeof(HTML), "text/html", request->keep_alive);
        } else {
            send_http_response(sock, "405 Method Not Allowed", "", 0, "text/plain", request->keep_alive);
        }
    }

    free(request);
    close(sock);
    return NULL;
}


#ifdef WEBSERVER_THREADS
static void create_thread(void* (*func)(void*), void* arg) {
    pthread_t thread;
    pthread_attr_t attr;

    int result = pthread_attr_init(&attr);
    assert(result == 0);

    result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    assert(result == 0);

    result = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
    assert(result == 0);

    result = pthread_create(&thread, &attr, func, arg);
    if (result != 0) {
        perror("pthread_create");
    }
    assert(result == 0);
}
#endif


int main() {
    struct sockaddr_in localhost = {0};
    localhost.sin_family = AF_INET;
    localhost.sin_addr.s_addr = htonl(INADDR_ANY);
    localhost.sin_port = htons(PORT);

#ifdef WEBSERVER_COROUTINES
    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    if (sock == -1) {
        exit_error("server socket()");
    }

    int reuse = 1;

    int result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (result == -1) {
        exit_error("setsockopt SO_REUSEADDR");
    }

    result = bind(sock, (struct sockaddr*)&localhost, sizeof(localhost));
    if (result == -1) {
        exit_error("bind");
        exit(EXIT_FAILURE);
    }

    result = listen(sock, BACKLOG);
    if (result == -1) {
        exit_error("listen");
    }

    while (true) {
#ifdef WEBSERVER_COROUTINES
        int new_sock = async_accept(sock, NULL, NULL);
#else
        int new_sock = accept(sock, NULL, NULL);
#endif
        if (new_sock == -1) {
            exit_error("async_accept");
        }

#ifdef WEBSERVER_COROUTINES
        struct coroutine* coro = sched_new_coroutine(server_handler_coro,
            (void*)(intptr_t)new_sock);
        assert(coro != NULL);
#else
        create_thread(server_handler_coro, (void*)(intptr_t)new_sock);
#endif
    }

    return 0;
}
