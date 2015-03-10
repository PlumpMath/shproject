#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <webserver/eventlib.h>


#define MAX_EVENTS 128


static void arm_waiter(struct waiter* waiter, uint32_t events);


static int socket_errno(int fd) {
    int error;
    socklen_t len = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&error, &len) == 0) {
        return -error;
    }
    return -errno;
}


static void handle_accept(struct epoll_event* event, struct waiter* waiter) {
    if ((event->events & EPOLLERR) != 0) {
        waiter->cb.on_accept(waiter, socket_errno(waiter->fd));
        return;
    }

    assert((event->events & EPOLLIN) != 0);

    int new_sock = accept4(waiter->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (new_sock == -1) {
        if (errno != EAGAIN || errno != EWOULDBLOCK) {
            waiter->cb.on_accept(waiter, -errno);
        }
        arm_waiter(waiter, EPOLLIN);
        return;
    }

    waiter->cb.on_accept(waiter, new_sock);
}


static void handle_read(struct epoll_event* event, struct waiter* waiter) {
    if ((event->events & EPOLLERR) != 0) {
        waiter->cb.on_read(waiter, waiter->buffer.buffer, socket_errno(waiter->fd));
        return;
    }

    assert((event->events & EPOLLIN) != 0);

    ssize_t num_read = recv(waiter->fd, waiter->buffer.buffer, waiter->buffer.length, waiter->flags);
    if (num_read == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            waiter->cb.on_read(waiter, waiter->buffer.buffer, -errno);
        }
        arm_waiter(waiter, EPOLLIN);
        return;
    }

    waiter->cb.on_read(waiter, waiter->buffer.buffer, num_read);
}


static void handle_write(struct epoll_event* event, struct waiter* waiter) {
    if ((event->events & EPOLLERR) != 0) {
        waiter->cb.on_write(waiter, waiter->buffer.buffer, socket_errno(waiter->fd));
        return;
    }

    assert((event->events & EPOLLOUT) != 0);

    char* write_at = (char*)waiter->buffer.buffer + waiter->buffer.num_written;
    size_t remaining = waiter->buffer.length - waiter->buffer.num_written;

    ssize_t num_written = send(waiter->fd, write_at, remaining, waiter->flags);

    if (num_written == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            waiter->cb.on_write(waiter, waiter->buffer.buffer, -errno);
        }
        arm_waiter(waiter, EPOLLOUT);
        return;
    }

    waiter->buffer.num_written += num_written;
    if (waiter->buffer.num_written == waiter->buffer.length) {
        waiter->cb.on_write(waiter, waiter->buffer.buffer, num_written);
        return;
    }
    arm_waiter(waiter, EPOLLOUT);
}


static void handle_event(struct epoll_event* event) {
    struct waiter* waiter = (struct waiter*)event->data.ptr;

    int result = epoll_ctl(waiter->loop->epoll_fd, EPOLL_CTL_DEL, waiter->fd, NULL);
    assert(result == 0);

    switch (waiter->wait_type) {
    case WAIT_ACCEPT:
        handle_accept(event, waiter);
        return;
    case WAIT_READ:
        handle_read(event, waiter);
        return;
    case WAIT_WRITE:
        handle_write(event, waiter);
        return;
    }
}


static void arm_waiter(struct waiter* waiter, uint32_t events) {
    events |= EPOLLET | EPOLLONESHOT;

    struct epoll_event event = {
        .events = events,
        .data = { .ptr = (void*)waiter }
    };

    int epoll_fd = waiter->loop->epoll_fd;

    int result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, waiter->fd, &event);
    assert(result == 0);
}


void async_init_waiter(struct waiter* waiter, struct loop* loop, int fd) {
    waiter->loop = loop;
    waiter->fd = fd;
}


int async_accept(struct waiter* waiter, accept_cb on_accept) {
    int new_sock = accept4(waiter->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (new_sock != -1) {
        on_accept(waiter, new_sock);
        return new_sock;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK)
        return -errno;

    waiter->wait_type = WAIT_ACCEPT;
    waiter->cb.on_accept = on_accept;

    arm_waiter(waiter, EPOLLIN);
    return 0;
}


ssize_t async_recv(struct waiter* waiter, void* buffer, size_t length, int flags, read_cb on_read) {
    ssize_t num_read = recv(waiter->fd, buffer, length, flags);
    if (num_read >= 0) {
        on_read(waiter, buffer, num_read);
        return num_read;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK)
        return -errno;

    waiter->wait_type = WAIT_READ;
    waiter->cb.on_read = on_read;
    waiter->flags = flags;
    waiter->buffer.buffer = buffer;
    waiter->buffer.length = length;

    arm_waiter(waiter, EPOLLIN);
    return 0;
}


ssize_t async_send(struct waiter* waiter, void* buffer, size_t length, int flags, write_cb on_write) {
    ssize_t num_written = send(waiter->fd, buffer, length, flags);
    if (num_written >= 0) {
        on_write(waiter, buffer, num_written);
        return num_written;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK)
        return -errno;

    waiter->wait_type = WAIT_WRITE;
    waiter->cb.on_write = on_write;
    waiter->flags = flags;
    waiter->buffer.buffer = buffer;
    waiter->buffer.length = length;
    waiter->buffer.num_written = 0;

    arm_waiter(waiter, EPOLLOUT);
    return 0;
}


int async_run_loop(struct loop* loop) {
    struct epoll_event events[MAX_EVENTS];

    for (;;) {
        int nfds = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1 && errno == EINTR) {
            continue;
        } else if (nfds == -1) {
            return -errno;
        }

        for (int i = 0; i < nfds; i++) {
            handle_event(&events[i]);
        }
    }
}


int async_init_loop(struct loop* loop) {
    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epoll_fd == -1)
        return -errno;

    return 0;
}
