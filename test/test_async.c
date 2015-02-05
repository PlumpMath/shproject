#include <async.h>
#include <scheduler.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <sys/socket.h>

static const int CLIENTS = 500;

static const short PORT = 51234;

static const char* MESSAGES[] = {
    "Message 1",
    "Message 2",
    "Message 3",
    "Message 4",
    "Message 5"
};


int send_all(int sock, const void* buffer, size_t size) {
    const char* buf = (const char*)buffer;

    while (size > 0) {
        ssize_t sent = async_send(sock, buf, size, 0);
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


void* client_coro(void* unused) {
    static int client_counter = 1;
    int client_num = client_counter++;

    async_sleep_relative(client_num * 10L);

    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    struct sockaddr_in localhost = {0};
    localhost.sin_family = AF_INET;
    localhost.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    localhost.sin_port = htons(PORT);

    int result = async_connect(sock, (struct sockaddr*)&localhost, sizeof(struct sockaddr));
    if (result != 0) {
        perror("async_connect");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < sizeof(MESSAGES) / sizeof(MESSAGES[0]); i++) {
        const char* msg = MESSAGES[i];
        size_t size = strlen(msg);
        result = send_all(sock, msg, size);
        if (result == -1) {
            perror("async_send (client)");
            exit(EXIT_FAILURE);
        }
    }

    while (1) {
        char buf[64];
        ssize_t num_read = async_recv(sock, buf, sizeof(buf) - 1, 0);
        if (num_read == -1) {
            perror("async_recv (client)");
            exit(EXIT_FAILURE);
        }

        buf[num_read] = '\0';
        printf("Client %d recvd %s\n", client_num, buf);
    }

    return NULL;
}


void* server_handler_coro(void* sock_ptr) {
    int sock = (int)(intptr_t)sock_ptr;
    while (1) {
        char buf[64];
        ssize_t num_read = async_recv(sock, buf, sizeof(buf) - 1, 0);
        if (num_read == -1) {
            perror("async_recv (server)");
            exit(EXIT_FAILURE);
        }

        if (num_read == 0) {
            return NULL;
        }

        int result = send_all(sock, buf, num_read);
        if (result == -1) {
            perror("async_send (server)");
            exit(EXIT_FAILURE);
        }
    }

    return NULL;
}


void* spinner_coro(void* arg) {
    intptr_t spinner_num = (intptr_t)arg;
    for (long i = 0; ; i++) {
        if (i % 1000000000L == 0) {
            printf("Spinner %"PRIdPTR" still spinning\n", spinner_num);
        }
    }
}


int main() {
    struct coroutine* spinners[64];
    for (int i = 0; i < 64; i++) {
        spinners[i] = sched_new_coroutine(spinner_coro, (void*)(intptr_t)i);
        assert(spinners[i] != NULL);
    }

    struct sockaddr_in localhost = {0};
    localhost.sin_family = AF_INET;
    localhost.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    localhost.sin_port = htons(PORT);

    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock == -1) {
        perror("server socket()");
        exit(EXIT_FAILURE);
    }

    int result = bind(sock, (struct sockaddr*)&localhost, sizeof(localhost));
    if (result == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    result = listen(sock, CLIENTS);
    if (result == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    struct coroutine* clients[CLIENTS];
    for (int i = 0; i < CLIENTS; i++) {
        clients[i] = sched_new_coroutine(client_coro, NULL);
        assert(clients[i] != NULL);
    }

    struct coroutine* handlers[CLIENTS];

    for (int i = 0; 1; i++) {
        assert(i <= CLIENTS);
        int new_sock = async_accept(sock, NULL, NULL);
        if (new_sock == -1) {
            perror("async_accept");
            exit(EXIT_FAILURE);
        }

        handlers[i] = sched_new_coroutine(server_handler_coro, (void*)(intptr_t)new_sock);
        assert(handlers[i] != NULL);
    }
}
