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
 * lthread.h
 */


#ifndef LTHREAD_H
#define LTHREAD_H

#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <poll.h>

#ifndef LTHREAD_INT_H
struct lthread;
typedef struct lthread lthread_t;
#endif

typedef void (*lthread_func)(void *);
#ifdef __cplusplus
extern "C" {
#endif

// lthreads high
lthread_t* lthread_spawn(lthread_func func, void* arg);
void    lthread_yield();
void    lthread_sleep(uint64_t msecs);
void    lthread_exit();
lthread_t* lthread_current();
int     lthread_init(size_t size);
void    lthread_run(lthread_func main_func, void* main_arg, size_t stack_size, size_t num_aux_threads);

// util
void    lthread_print_timestamp(char *);

// async network io
int     lthread_socket(int domain, int type, int protocol);
int     lthread_pipe(int fildes[2]);
int     lthread_accept(int fd, struct sockaddr *, socklen_t *);
int     lthread_close(int fd);
int     lthread_connect(int fd, struct sockaddr *, socklen_t);

ssize_t lthread_recv(
    int fd,
    void *buf,
    size_t buf_len,
    int flags
);
ssize_t lthread_read(int fd, void *buf, size_t length);
ssize_t lthread_readline(int fd, char **buf, size_t max);
ssize_t lthread_recvmsg(
    int fd,
    struct msghdr *message,
    int flags
);
ssize_t lthread_recvfrom(
    int fd,
    void *buf,
    size_t length,
    int flags,
    struct sockaddr *address,
    socklen_t *address_len
);

ssize_t lthread_send(int fd, const void *buf, size_t buf_len, int flags);
ssize_t lthread_write(int fd, const void *buf, size_t buf_len);
ssize_t lthread_sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t lthread_sendto(
    int fd,
    const void *buf,
    size_t length,
    int flags,
    const struct sockaddr *dest_addr,
    socklen_t dest_len
);

#ifdef __cplusplus
}
#endif

#endif
