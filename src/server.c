
#define _GNU_SOURCE
#include "sigreactor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>

#define PORT 9090
#define IDLE_MS 10000
#define OUT_SIZE 8192

typedef struct server server_t;

typedef struct client {
    int fd, timerfd;
    char out[OUT_SIZE];
    size_t len, off;
    server_t *srv;
    struct client *next;
} client_t;

struct server {
    reactor_t *r;
    int listenfd, heartbeatfd, signalfd;
    client_t *clients;
};

static void client_close(client_t *c)
{
    server_t *s = c->srv;
    client_t **p = &s->clients;

    while (*p && *p != c)
        p = &(*p)->next;

    if (*p)
        *p = c->next;

    reactor_del(s->r, c->fd);
    reactor_del(s->r, c->timerfd);
    close(c->fd);
    close(c->timerfd);
    free(c);

    puts("Client closed.");
}

static void client_timeout(reactor_t *r, int fd,
                           uint32_t events, void *arg)
{
    (void)r;
    (void)events;

    uint64_t expirations;
    if (read(fd, &expirations, sizeof(expirations)) ==
        (ssize_t)sizeof(expirations)) {
        puts("Client idle timeout.");
        client_close(arg);
    }
}

static void client_io(reactor_t *r, int fd,
                      uint32_t events, void *arg)
{
    client_t *c = arg;

    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        client_close(c);
        return;
    }

    if (events & EPOLLIN) {
        char buf[2048];

        for (;;) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);

            if (n > 0) {
                if (c->off > 0) {
                    memmove(c->out, c->out + c->off,
                            c->len - c->off);
                    c->len -= c->off;
                    c->off = 0;
                }

                if ((size_t)n > OUT_SIZE - c->len) {
                    client_close(c);
                    return;
                }

                memcpy(c->out + c->len, buf, (size_t)n);
                c->len += (size_t)n;

                if (reactor_timer_set(c->timerfd,
                                      IDLE_MS, 0) == -1) {
                    client_close(c);
                    return;
                }
            } else if (n == 0) {
                client_close(c);
                return;
            } else if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN ||
                       errno == EWOULDBLOCK) {
                break;
            } else {
                client_close(c);
                return;
            }
        }
    }

    if (events & EPOLLOUT) {
        while (c->off < c->len) {
            ssize_t n = send(fd, c->out + c->off,
                             c->len - c->off, MSG_NOSIGNAL);

            if (n > 0) {
                c->off += (size_t)n;
            } else if (n == -1 && errno == EINTR) {
                continue;
            } else if (n == -1 &&
                       (errno == EAGAIN ||
                        errno == EWOULDBLOCK)) {
                break;
            } else {
                client_close(c);
                return;
            }
        }

        if (c->off == c->len)
            c->off = c->len = 0;
    }

    uint32_t mask = EPOLLIN | EPOLLRDHUP;

    if (c->len > c->off)
        mask |= EPOLLOUT;

    if (reactor_mod(r, fd, mask, client_io, c) == -1)
        client_close(c);
}

static void accept_clients(reactor_t *r, int fd,
                           uint32_t events, void *arg)
{
    (void)events;
    server_t *s = arg;

    for (;;) {
        int cfd = accept4(fd, NULL, NULL,
                          SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (cfd == -1) {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            perror("accept4");
            break;
        }

        client_t *c = calloc(1, sizeof(*c));
        if (!c) {
            close(cfd);
            continue;
        }

        c->fd = cfd;
        c->timerfd = -1;
        c->srv = s;

        c->timerfd = reactor_timer_create(
            r, IDLE_MS, 0, client_timeout, c);

        if (c->timerfd == -1 ||
            reactor_add(r, cfd,
                        EPOLLIN | EPOLLRDHUP,
                        client_io, c) == -1) {
            if (c->timerfd >= 0) {
                reactor_del(r, c->timerfd);
                close(c->timerfd);
            }
            close(cfd);
            free(c);
            continue;
        }

        c->next = s->clients;
        s->clients = c;
        printf("Client connected: fd=%d\n", cfd);
    }
}

static void heartbeat(reactor_t *r, int fd,
                      uint32_t events, void *arg)
{
    (void)r;
    (void)events;
    (void)arg;

    uint64_t expirations;
    if (read(fd, &expirations, sizeof(expirations)) ==
        (ssize_t)sizeof(expirations))
        printf("Reactor heartbeat: %llu tick(s)\n",
               (unsigned long long)expirations);
}

static void handle_signal(reactor_t *r, int signo, void *arg)
{
    server_t *s = arg;

    if (signo == SIGINT || signo == SIGTERM) {
        puts("Shutdown requested.");
        reactor_stop(r);
    } else if (signo == SIGCHLD) {
        int status;
        pid_t pid;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
            printf("Reaped child PID %ld\n", (long)pid);
    }

    (void)s;
}

int main(void)
{
    server_t s = {0};
    s.listenfd = s.heartbeatfd = s.signalfd = -1;
    s.r = reactor_create();

    if (!s.r) {
        perror("reactor_create");
        return 1;
    }

    int signals[] = {SIGINT, SIGTERM, SIGCHLD};
    s.signalfd = reactor_signals(
        s.r, signals, 3, handle_signal, &s);

    if (s.signalfd == -1) {
        perror("reactor_signals");
        reactor_destroy(s.r);
        return 1;
    }

    s.listenfd = socket(AF_INET,
                        SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                        0);

    if (s.listenfd == -1) {
        perror("socket");
        goto cleanup;
    }

    int yes = 1;
    setsockopt(s.listenfd, SOL_SOCKET, SO_REUSEADDR,
               &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(s.listenfd, (struct sockaddr *)&addr,
             sizeof(addr)) == -1 ||
        listen(s.listenfd, SOMAXCONN) == -1) {
        perror("bind/listen");
        goto cleanup;
    }

    if (reactor_add(s.r, s.listenfd, EPOLLIN,
                    accept_clients, &s) == -1) {
        perror("reactor_add listener");
        goto cleanup;
    }

    s.heartbeatfd = reactor_timer_create(
        s.r, 5000, 5000, heartbeat, NULL);

    if (s.heartbeatfd == -1) {
        perror("heartbeat timer");
        goto cleanup;
    }

    /* Demonstration child: exits after two seconds. */
    pid_t child = fork();

    if (child == 0) {
        sleep(2);
        _exit(0);
    } else if (child == -1) {
        perror("fork");
        goto cleanup;
    }

    printf("SigReactor server listening on port %d\n", PORT);
    puts("Press Ctrl+C to stop.");

    if (reactor_run(s.r) == -1)
        goto cleanup;

cleanup:
    while (s.clients)
        client_close(s.clients);

    if (s.listenfd >= 0) {
        reactor_del(s.r, s.listenfd);
        close(s.listenfd);
    }

    if (s.heartbeatfd >= 0) {
        reactor_del(s.r, s.heartbeatfd);
        close(s.heartbeatfd);
    }

    if (s.signalfd >= 0) {
        /* reactor_destroy closes the signal FD. */
    }

    reactor_destroy(s.r);
    puts("SigReactor stopped.");
    return 0;
}
