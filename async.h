#ifndef _ASYNC_H
#define _ASYNC_H

#include <coro.h>

#include <sys/socket.h>


extern void async_schedule(coroutine_t* coro, void* value);

extern ssize_t async_read(int fd, void* buf, size_t length);
extern ssize_t async_write(int fd, void* buf, size_t length);

extern int async_accept(int socket, struct sockaddr* address, socklen_t* length);
extern int async_connect(int socket, const struct sockaddr* address, socklen_t length);

extern ssize_t async_send(int socket, const void* buffer, size_t length, int flags);
extern ssize_t async_recv(int socket, void* buffer, size_t length, int flags);

#endif
