#ifndef _ASYNC_H
#define _ASYNC_H

#include <sys/socket.h>
#include <time.h>


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
