#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lthread.h>
#include <lthread_int.h>
#include <arpa/inet.h>

struct cli_info {
    /* other stuff if needed*/
    struct sockaddr_in peer_addr;
    int fd;
};

typedef struct cli_info cli_info_t;

char *reply = "HTTP/1.0 200 OK\r\nContent-length: 11\r\n\r\nHello Kannan\n\n";

void
http_serv(void *arg)
{
    cli_info_t *cli_info = arg;
    char *buf = NULL;
    char ipstr[INET6_ADDRSTRLEN];

    inet_ntop(AF_INET, &cli_info->peer_addr.sin_addr, ipstr, INET_ADDRSTRLEN);
    //printf("Accepted connection on IP %s\n", ipstr);

    if ((buf = malloc(1024)) == NULL)
        return;

    while (lthread_recv(cli_info->fd, buf, 1024, 0) > 0)
        lthread_send(cli_info->fd, reply, strlen(reply), 0);
    lthread_close(cli_info->fd);
    free(buf);
    free(arg);
}

void
listener(void *arg)
{
    int cli_fd = 0;
    int lsn_fd = 0;
    int opt = 1;
    int ret = 0;
    struct sockaddr_in peer_addr = {};
    struct   sockaddr_in sin = {};
    socklen_t addrlen = sizeof(peer_addr);
    cli_info_t *cli_info = NULL;

    /* create listening socket */
    lsn_fd = lthread_socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (lsn_fd == -1)
        return;

    if (setsockopt(lsn_fd, SOL_SOCKET, SO_REUSEADDR, &opt,sizeof(int)) == -1)
        perror("failed to set SOREUSEADDR on socket");

    sin.sin_family = PF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(3128);

    /* bind to the listening port */
    ret = bind(lsn_fd, (struct sockaddr *)&sin, sizeof(sin));
    if (ret == -1) {
        perror("Failed to bind on port 3128");
        return;
    }

    printf("Starting listener on 3128\n");

    listen(lsn_fd, 128);

    uint64_t start_time = _lthread_usec_now();

    for (size_t i = 0;;++i)
    {
        cli_fd = lthread_accept(lsn_fd, (struct sockaddr*)&peer_addr, &addrlen);
        if (cli_fd == -1) {
            perror("Failed to accept connection");
            return;
        }

        if ((cli_info = malloc(sizeof(cli_info_t))) == NULL) {
            close(cli_fd);
            continue;
        }
        cli_info->peer_addr = peer_addr;
        cli_info->fd = cli_fd;
        /* launch a new lthread that takes care of this client */
        lthread_spawn(http_serv, cli_info);
        if (i > 0 && i % 1000 == 0)
        {
            uint64_t now = _lthread_usec_now();
            fprintf(stderr, "req/s: %llu\n", 1000000000 /  (now - start_time));
            start_time = now;
        }
    }
}

int
main(int argc, char **argv)
{
    lthread_run(listener, 0, 0, 10);

    return 0;
}
