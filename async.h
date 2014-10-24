#ifndef _ASYNC_H
#define _ASYNC_H

#include <coro.h>

#include <sys/socket.h>
#include <time.h>


/*
 * Schedule coroutine `coro` to be run when the current coroutine
 * either finishes or performs a blocking operation.
 */
extern void async_schedule(struct coroutine* coro, void* value);

/*
 * Sleep for a relative or absolute time period.
 */
extern void async_sleep_relative(long millisecs);
extern void async_sleep_absolute(const struct timespec* time);

/*
 * Asynchronous variants of blocking file/socket operations.
 */
extern ssize_t async_read(int fd, void* buf, size_t length);
extern ssize_t async_write(int fd, void* buf, size_t length);

extern int async_accept(int socket, struct sockaddr* address, socklen_t* length);
extern int async_connect(int socket, const struct sockaddr* address, socklen_t length);

extern ssize_t async_send(int socket, const void* buffer, size_t length, int flags);
extern ssize_t async_recv(int socket, void* buffer, size_t length, int flags);


#endif
