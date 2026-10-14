
#ifndef SIGREACTOR_H
#define SIGREACTOR_H

#include <stdint.h>
#include <signal.h>
#include <sys/epoll.h>

#define SR_MAX_FDS 65536

typedef struct reactor reactor_t;

typedef void (*sr_callback)(
    reactor_t *, int, uint32_t, void *);

typedef void (*sr_signal_callback)(
    reactor_t *, int, void *);

reactor_t *reactor_create(void);
void reactor_destroy(reactor_t *r);

int reactor_add(reactor_t *r, int fd,
                uint32_t events, sr_callback cb, void *arg);

int reactor_mod(reactor_t *r, int fd,
                uint32_t events, sr_callback cb, void *arg);

int reactor_del(reactor_t *r, int fd);

int reactor_run(reactor_t *r);
void reactor_stop(reactor_t *r);

int reactor_signals(reactor_t *r,
                    const int *signals, int count,
                    sr_signal_callback cb, void *arg);

int reactor_timer_create(reactor_t *r,
                         int initial_ms, int interval_ms,
                         sr_callback cb, void *arg);

int reactor_timer_set(int fd,
                      int initial_ms, int interval_ms);

#endif
