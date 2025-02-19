/*
 * Lthread
 * Copyright (C) 2012, Hasan Alayli <halayli@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * lthread_socket.c
 */


#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <assert.h>

#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>

#include "lthread_int.h"

#if defined(__FreeBSD__) || defined(__APPLE__)
    #define FLAG
#else
    #define FLAG | MSG_NOSIGNAL
#endif

static inline int _lthread_unblock_fd(int fd);

#define LTHREAD_RECV(x, y)                                  \
x {                                                         \
    ssize_t ret = -1;                                       \
    while (ret < 0) {                                       \
        ret = y;                                            \
        if (ret == -1)                                      \
        {                                                   \
            if (errno == EAGAIN) {                          \
                if (_lthread_wait_fd(fd, LT_EV_READ) == -1) \
                    return -1;                              \
            } else {                                        \
                return -1;                                  \
            }                                               \
        }                                                   \
    }                                                       \
    return (ret);                                           \
}                                                           \


#define LTHREAD_SEND(x, y)                                  \
x {                                                         \
    ssize_t ret = -1;                                       \
    while (ret < 0) {                                       \
        ret = y;                                            \
        if (ret == -1) {                                    \
            if (errno == EAGAIN) {                          \
                if (_lthread_wait_fd(fd, LT_EV_WRITE) == -1)\
                    return -1;                              \
            } else {                                        \
                return -1;                                  \
            }                                               \
        }                                                   \
    }                                                       \
    return ret;                                             \
}                                                           \

int lthread_accept(int fd, struct sockaddr *addr, socklen_t *len)
{
    int ret = -1;
    while (ret <= 0)
    {
        ret = accept(fd, addr, len);
        if (ret == -1)
        {
            switch (errno)
            {
                case ENFILE:
                case EWOULDBLOCK:
                case EMFILE:
                    _lthread_wait_fd(fd, LT_EV_READ);
                    break;
                case ECONNABORTED:
                    perror("connection accept aborted");
                    break;
                default:
                    perror("Cannot accept connection");
                    return -1;
            }
        }
    }
#ifndef __FreeBSD__
    if (_lthread_unblock_fd(ret) == -1)
        return -1;
#endif
    return ret;
}

int lthread_close(int fd)
{
    return (close(fd));
}

int lthread_socket(int domain, int type, int protocol)
{
    int fd;
#if defined(__FreeBSD__) || defined(__APPLE__)
    int set = 1;
#endif

    if ((fd = socket(domain, type, protocol)) == -1) {
        perror("Failed to create a new socket");
        return (-1);
    }

    if (_lthread_unblock_fd(fd) == -1)
        return -1;

#if defined(__FreeBSD__) || defined(__APPLE__)
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(int)) == -1) {
        close(fd);
        perror("Failed to set socket properties");
        return (-1);
    }
#endif

    return (fd);
}

/* forward declare lthread_recv for use in readline */
ssize_t lthread_recv(int fd, void *buf, size_t buf_len, int flags,
    uint64_t timeout);

ssize_t
lthread_readline(int fd, char **buf, size_t max, uint64_t timeout)
{
    size_t cur = 0;
    ssize_t r = 0;
    size_t total_read = 0;
    char *data = NULL;

    data = calloc(1, max + 1);
    if (data == NULL)
        return (-1);

    while (total_read < max) {
        r = lthread_recv(fd, data + total_read, 1, 0, timeout);

        if (r == 0 || r == -2 || r == -1) {
            free(data);
            return (r);
        }

        total_read += 1;
        if (data[cur++] == '\n')
            break;
    }

    *buf = data;

    return (total_read);
}

int lthread_pipe(int fildes[2])
{
    int ret = 0;

    ret = pipe(fildes);
    if (ret != 0)
        return (ret);

    ret = fcntl(fildes[0], F_SETFL, O_NONBLOCK);
    if (ret != 0)
        goto err;

    ret = fcntl(fildes[1], F_SETFL, O_NONBLOCK);
    if (ret != 0)
        goto err;

    return (0);

err:
    close(fildes[0]);
    close(fildes[1]);
    return (ret);
}

LTHREAD_RECV(
    ssize_t lthread_recv(int fd, void *buf, size_t length, int flags,
        uint64_t timeout),
    recv(fd, buf, length, flags FLAG)
)

LTHREAD_RECV(
    ssize_t lthread_read(int fd, void *buf, size_t length, uint64_t timeout),
    read(fd, buf, length)
)

LTHREAD_RECV(
    ssize_t lthread_recvmsg(int fd, struct msghdr *message, int flags,
        uint64_t timeout),
    recvmsg(fd, message, flags FLAG)
)

LTHREAD_RECV(
    ssize_t lthread_recvfrom(int fd, void *buf, size_t length, int flags,
        struct sockaddr *address, socklen_t *address_len, uint64_t timeout),
    recvfrom(fd, buf, length, flags FLAG, address, address_len)
)

LTHREAD_SEND(
    ssize_t lthread_sendmsg(int fd, const struct msghdr *message, int flags),
    sendmsg(fd, message, flags FLAG)
)

LTHREAD_SEND(
    ssize_t lthread_sendto(int fd, const void *buf, size_t length, int flags,
        const struct sockaddr *dest_addr, socklen_t dest_len),
    sendto(fd, buf, length, flags FLAG, dest_addr, dest_len)
)

int lthread_connect(int fd, struct sockaddr *name, socklen_t namelen)
{

    int ret = 0;
    while (1) {
        ret = connect(fd, name, namelen);
        if (ret == 0)
            break;
        if (ret == -1 && (errno == EAGAIN || 
            errno == EWOULDBLOCK ||
            errno == EINPROGRESS)) {
            _lthread_wait_fd(fd, LT_EV_WRITE);
            continue;
        } else {
            break;
        }
    }

    return (ret);
}

ssize_t lthread_writev(int fd, struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    int iov_index = 0;
    do {
        ssize_t n = writev(fd, iov + iov_index, iovcnt - iov_index);
        if (n > 0) {
            int i = 0;
            total += n;
            for (i = iov_index; i < iovcnt && n > 0; i++) {
                if (n < iov[i].iov_len) {
                    iov[i].iov_base += n;
                    iov[i].iov_len -= n;
                    n = 0;
                } else {
                    n -= iov[i].iov_len;
                    iov_index++;
                }
            }
        } else if (-1 == n && EAGAIN == errno) {
            _lthread_wait_fd(fd, LT_EV_WRITE);
        } else {
            return (n);
        }
    } while (iov_index < iovcnt);

    return (total);
}

static inline int _lthread_unblock_fd(int fd)
{
    if ((fcntl(fd, F_SETFL, O_NONBLOCK)) == -1)
    {
        close(fd);
        perror("Failed to set socket properties");
        return (-1);
    }
    else
    {
        return 0;
    }

}