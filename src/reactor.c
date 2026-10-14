
#define _GNU_SOURCE
#include "sigreactor.h"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <time.h>

struct watch {
    sr_callback cb;
    void *arg;
};

struct reactor {
    int epfd;
    int running;
    struct watch *watches[SR_MAX_FDS];
    int signal_fd;
    sigset_t old_mask;
    int mask_saved;
    sr_signal_callback signal_cb;
    void *signal_arg;
};

static void signal_dispatch(reactor_t *r, int fd,
                            uint32_t events, void *arg)
{
    (void)events;
    (void)arg;
    struct signalfd_siginfo info;

    for (;;) {
        ssize_t n = read(fd, &info, sizeof(info));

        if (n == sizeof(info)) {
            if (r->signal_cb)
                r->signal_cb(r, (int)info.ssi_signo,
                             r->signal_arg);
        } else if (n == -1 && errno == EINTR) {
            continue;
        } else if (n == -1 &&
                   (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            reactor_stop(r);
            break;
        }
    }
}

reactor_t *reactor_create(void)
{
    reactor_t *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    r->epfd = epoll_create1(EPOLL_CLOEXEC);
    r->signal_fd = -1;

    if (r->epfd == -1) {
        free(r);
        return NULL;
    }

    return r;
}

int reactor_add(reactor_t *r, int fd, uint32_t events,
                sr_callback cb, void *arg)
{
    if (!r || fd < 0 || fd >= SR_MAX_FDS || !cb) {
        errno = EINVAL;
        return -1;
    }

    if (r->watches[fd]) {
        errno = EEXIST;
        return -1;
    }

    struct watch *w = malloc(sizeof(*w));
    if (!w)
        return -1;

    w->cb = cb;
    w->arg = arg;

    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        free(w);
        return -1;
    }

    r->watches[fd] = w;
    return 0;
}

int reactor_mod(reactor_t *r, int fd, uint32_t events,
                sr_callback cb, void *arg)
{
    if (!r || fd < 0 || fd >= SR_MAX_FDS ||
        !r->watches[fd] || !cb) {
        errno = EINVAL;
        return -1;
    }

    struct epoll_event ev = {0};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(r->epfd, EPOLL_CTL_MOD, fd, &ev) == -1)
        return -1;

    r->watches[fd]->cb = cb;
    r->watches[fd]->arg = arg;
    return 0;
}

int reactor_del(reactor_t *r, int fd)
{
    if (!r || fd < 0 || fd >= SR_MAX_FDS ||
        !r->watches[fd]) {
        errno = EINVAL;
        return -1;
    }

    int rc = epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, NULL);
    free(r->watches[fd]);
    r->watches[fd] = NULL;
    return rc;
}

int reactor_run(reactor_t *r)
{
    if (!r) {
        errno = EINVAL;
        return -1;
    }

    struct epoll_event events[64];
    r->running = 1;

    while (r->running) {
        int n = epoll_wait(r->epfd, events, 64, -1);

        if (n == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        for (int i = 0; i < n && r->running; i++) {
            int fd = events[i].data.fd;

            if (fd < 0 || fd >= SR_MAX_FDS)
                continue;

            struct watch *w = r->watches[fd];
            if (w && w->cb)
                w->cb(r, fd, events[i].events, w->arg);
        }
    }

    return 0;
}

void reactor_stop(reactor_t *r)
{
    if (r)
        r->running = 0;
}

int reactor_signals(reactor_t *r, const int *signals, int count,
                    sr_signal_callback cb, void *arg)
{
    if (!r || !signals || count <= 0 || !cb ||
        r->signal_fd >= 0) {
        errno = EINVAL;
        return -1;
    }

    sigset_t mask;
    sigemptyset(&mask);

    for (int i = 0; i < count; i++) {
        if (signals[i] <= 0 || signals[i] >= NSIG) {
            errno = EINVAL;
            return -1;
        }
        sigaddset(&mask, signals[i]);
    }

    if (sigprocmask(SIG_BLOCK, &mask, &r->old_mask) == -1)
        return -1;

    r->mask_saved = 1;
    r->signal_fd = signalfd(-1, &mask,
                            SFD_NONBLOCK | SFD_CLOEXEC);

    if (r->signal_fd == -1)
        goto fail;

    r->signal_cb = cb;
    r->signal_arg = arg;

    if (reactor_add(r, r->signal_fd, EPOLLIN,
                    signal_dispatch, NULL) == -1) {
        close(r->signal_fd);
        r->signal_fd = -1;
        goto fail;
    }

    return r->signal_fd;

fail:
    sigprocmask(SIG_SETMASK, &r->old_mask, NULL);
    r->mask_saved = 0;
    return -1;
}

int reactor_timer_set(int fd, int initial_ms, int interval_ms)
{
    if (fd < 0 || initial_ms < 0 || interval_ms < 0) {
        errno = EINVAL;
        return -1;
    }

    struct itimerspec ts = {0};
    ts.it_value.tv_sec = initial_ms / 1000;
    ts.it_value.tv_nsec = (initial_ms % 1000) * 1000000L;
    ts.it_interval.tv_sec = interval_ms / 1000;
    ts.it_interval.tv_nsec = (interval_ms % 1000) * 1000000L;

    return timerfd_settime(fd, 0, &ts, NULL);
}

int reactor_timer_create(reactor_t *r, int initial_ms,
                         int interval_ms, sr_callback cb, void *arg)
{
    if (!r || initial_ms < 0 || interval_ms < 0 || !cb) {
        errno = EINVAL;
        return -1;
    }

    int fd = timerfd_create(CLOCK_MONOTONIC,
                            TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd == -1)
        return -1;

    if (reactor_timer_set(fd, initial_ms, interval_ms) == -1) {
        close(fd);
        return -1;
    }

    if (reactor_add(r, fd, EPOLLIN, cb, arg) == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

void reactor_destroy(reactor_t *r)
{
    if (!r)
        return;

    sigset_t old_mask;
    int restore_mask = r->mask_saved;

    if (restore_mask)
        old_mask = r->old_mask;

    if (r->signal_fd >= 0) {
        reactor_del(r, r->signal_fd);
        close(r->signal_fd);
    }

    for (int fd = 0; fd < SR_MAX_FDS; fd++)
        free(r->watches[fd]);

    close(r->epfd);
    free(r);

    if (restore_mask)
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
}
