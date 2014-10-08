#include <async.h>
#include <coro.h>
#include <platform/loop.h>
#include <util/list.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/socket.h>


static __thread event_loop_t* loop;


static event_loop_t* new_loop() {
    event_loop_t* loop = (event_loop_t*)malloc(sizeof(event_loop_t));
    assert(loop != NULL);

    int result = event_loop_init(loop);
    if (result != 0) {
        perror("event_loop_init");
        exit(EXIT_FAILURE);
    }

    return loop;
}


static uint32_t wait(int fd, int events) {
    if (loop == NULL) {
        loop = new_loop();
    }
    return event_loop_wait(loop, fd, events);
}


static int socket_errno(int fd) {
    int error;
    socklen_t len = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&error, &len) == 0) {
        return error;
    }
    return errno;
}


void async_sleep_relative(long millisecs) {
    struct timespec now;
    int result = clock_gettime(CLOCK_REALTIME, &now);
    assert(result == 0);

    now.tv_sec += millisecs / 1000L;
    now.tv_nsec += (millisecs % 1000L) * 1000000L;
    if (now.tv_nsec > 1000000000L) {
        now.tv_nsec -= 1000000000L;
        now.tv_sec++;
    }

    async_sleep_absolute(&now);
}


void async_sleep_absolute(const struct timespec* time) {
    if (loop == NULL) {
        loop = new_loop();
    }
    event_loop_sleep(loop, time);
}


void async_schedule(coroutine_t* coro, void* value) {
    if (loop == NULL) {
        loop = new_loop();
    }
    coro->value = value;  // bit of a hack
    event_loop_schedule(loop, coro);
}


#define WAIT_RETURN_SOCKET_ERROR(fd, events)                                  \
    do {                                                                      \
        int revents = wait(fd, events);                                       \
        if ((revents & WAITERR) != 0) {                                       \
            errno = socket_errno(fd);                                         \
            return -1;                                                        \
        }                                                                     \
    } while (0);                                                              \


ssize_t async_read(int fd, void* buffer, size_t length) {
    for (;;) {
        ssize_t num_read = read(fd, buffer, length);
        if (num_read >= 0) {
            return num_read;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            WAIT_RETURN_SOCKET_ERROR(fd, WAITIN);
        } else {
            return -1;
        }
    }
}


ssize_t async_write(int fd, void* buffer, size_t length) {
    for (;;) {
        ssize_t num_written = write(fd, buffer, length);
        if (num_written >= 0) {
            return num_written;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            WAIT_RETURN_SOCKET_ERROR(fd, WAITOUT);
        } else {
            return -1;
        }
    }
}


int async_accept(int socket, struct sockaddr* address, socklen_t* length) {
    for (;;) {
#ifdef __linux__
        int new_sock = accept4(socket, address, length, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
        int new_sock = accept(socket, address, length);
#endif

        if (new_sock != -1) {
#ifndef __linux__
            int flags = fcntl(new_sock, F_GETFL, 0);
            if (fcntl(new_sock, F_SETFL, flags | O_NONBLOCK | O_CLOEXEC) == -1) {
                close(new_sock);
                return -1;
            }
#endif
            return new_sock;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            WAIT_RETURN_SOCKET_ERROR(socket, WAITIN);
        } else {
            return -1;
        }
    }
}


int async_connect(int socket, const struct sockaddr* address, socklen_t length) {
    int result = connect(socket, address, length);

    if (result == 0) {
        return 0;
    }

    if (errno != EINPROGRESS) {
        return -1;
    }

    wait(socket, WAITOUT);

    int error = socket_errno(socket);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return 0;
}


ssize_t async_send(int socket, const void* buffer, size_t length, int flags) {
    for (;;) {
        ssize_t num_written = send(socket, buffer, length, flags);
        if (num_written >= 0) {
            return num_written;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            WAIT_RETURN_SOCKET_ERROR(socket, WAITOUT);
        } else {
            return -1;
        }
    }
}


ssize_t async_recv(int socket, void* buffer, size_t length, int flags) {
    for (;;) {
        ssize_t num_read = recv(socket, buffer, length, flags);
        if (num_read >= 0) {
            return num_read;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            WAIT_RETURN_SOCKET_ERROR(socket, WAITIN);
        } else {
            return -1;
        }
    }
}
