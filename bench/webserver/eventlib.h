/*
 * Basic callback-driven asynchronous I/O routines.
 */
#ifndef _BENCH_WEBSERVER_EVENTLIB_H
#define _BENCH_WEBSERVER_EVENTLIB_H

#include <stdlib.h>


struct loop {
    int epoll_fd;
};


struct waiter;


typedef void (*accept_cb)(struct waiter* waiter, int result);
typedef void (*read_cb)(struct waiter* waiter, void* buffer, ssize_t result);
typedef void (*write_cb)(struct waiter* waiter, void* buffer, ssize_t result);



struct waiter {
    struct loop* loop;

    enum wait_type {
        WAIT_ACCEPT,
        WAIT_READ,
        WAIT_WRITE
    } wait_type;

    union {
        accept_cb on_accept;
        read_cb on_read;
        write_cb on_write;
    } cb;

    int fd;

    struct {
        void* buffer;
        size_t length;
        size_t num_written;
    } buffer;

    int flags;
};


extern int async_accept(struct waiter* waiter, accept_cb on_accept);

extern ssize_t async_read(struct waiter* waiter, void* buffer, size_t length,
    read_cb on_read);

extern ssize_t async_write(struct waiter* waiter, void* buffer, size_t length,
    write_cb on_write);

extern ssize_t async_recv(struct waiter* waiter, void* buffer, size_t length,
    int flags, read_cb on_read);

extern ssize_t async_send(struct waiter* waiter, void* buffer, size_t length,
    int flags, write_cb on_write);

extern int async_run_loop(struct loop* loop);

extern int async_init_loop(struct loop* loop);

extern void async_init_waiter(struct waiter* waiter, struct loop* loop, int fd);

#endif
