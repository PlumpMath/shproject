#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
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
static const int BACKLOG = 10;


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

    return 0;
}


static int sendf(int sock, const char* format, ...) {
    char buffer[128];

    va_list list;
    va_start(list, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, list);
    va_end(list);

    int length = written >= sizeof(buffer) ? sizeof(buffer) - 1 : written;
    return send_all(sock, buffer, length);
}


static void send_http_headers(int sock, const char* status,
       size_t content_length, const char* content_type) {
    char buffer[64];

    time_t now = time(NULL);

    struct tm timeval;
    struct tm* utctime = gmtime_r(&now, &timeval);
    assert(utctime != NULL);

    char* timestr = asctime_r(&timeval, buffer);
    assert(timestr != NULL);
    timestr[strlen(timestr) - 1] = '\0';  // Remove newline

    sendf(sock, "HTTP/1.1 %s\r\n", status);
    sendf(sock, "Date: %s\r\n", timestr);
    sendf(sock, "Content-Type: %s\r\n", content_type);
    sendf(sock, "Content-Length: %zd\r\n", content_length);
    sendf(sock, "Connection: close\r\n\r\n");
}


static void send_http_response(int sock, const char* status, const void* data,
        size_t length, const char* content_type) {
    send_http_headers(sock, status, length, content_type);

    int result = send_all(sock, data, length);
    assert(result == 0);
}


static void send_http_random(int sock, size_t length, const char* content_type) {
    send_http_headers(sock, "200 OK", length, content_type);

#ifdef WEBSERVER_COROUTINES
    int random = open("/dev/random", O_RDONLY | O_NONBLOCK);
#else
    int random = open("/dev/random", O_RDONLY);
#endif

    unsigned char buffer[128];

    size_t sent = 0;
    while (sent < length) {
        size_t to_read = length - sent;
        to_read = to_read > sizeof(buffer) ? sizeof(buffer) : to_read;

#ifdef WEBSERVER_COROUTINES
        ssize_t nread = async_read(random, buffer, to_read);
#else
        ssize_t nread = read(random, buffer, to_read);
#endif
        if (nread < 0) {
            fprintf(stderr, "Error reading from random: %s.\n", strerror(errno));
            close(random);
            return;
        }

        for (size_t i = 0; i < nread; i++) {
            buffer[i] = '0' + (buffer[i] % ('Z' - '0' + 1));
        }
        int result = send_all(sock, buffer, nread);
        if (result != 0) {
            fprintf(stderr, "Write error on sock %d: %s\n", sock, strerror(errno));
            break;
        }
        assert(result == 0);
        sent += nread;
    }

    close(random);
}


const char HTML[] = "<html><head><title>Server</title></head><body><h1>they see me epollin', they hatin</h1></body></html>";



void* server_handler_coro(void* sock_ptr) {
    int sock = (int)(intptr_t)sock_ptr;

    http_parser_settings settings = {
        .on_url = on_url,
        .on_message_complete = on_done
    };

    http_parser parser;
    http_parser_init(&parser, HTTP_REQUEST);

    struct web_request* request = malloc(sizeof(struct web_request));
    assert(request != NULL);

    request->url[0] = '\0';
    request->url_length = 0;
    request->done = false;
    parser.data = request;

    while (true) {
        char buf[256];

#ifdef WEBSERVER_COROUTINES
        ssize_t num_read = async_recv(sock, buf, sizeof(buf), 0);
#else
        ssize_t num_read = recv(sock, buf, sizeof(buf), 0);
#endif
        if (num_read == -1) {
            exit_error("async_recv (server)");
        }

        size_t parsed = http_parser_execute(&parser, &settings, buf, num_read);
        if (parsed != num_read) {
            const char* error = http_errno_name(parser.http_errno);
            exit_error(error);  // TODO: might not want to exit
        }

        if (parsed == 0 || request->done) {
            break;
        }
    }

    struct http_parser_url parsed_url = {0};
    int result = http_parser_parse_url(request->url, request->url_length,
        false, &parsed_url);
    assert(result == 0);

    if (parser.method == HTTP_GET) {
        send_http_random(sock, 1024, "text/plain");
    } else {
        send_http_response(sock, "405 Method Not Allowed", "", 0, "text/plain");
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

    result = pthread_create(&thread, &attr, func, arg);
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
