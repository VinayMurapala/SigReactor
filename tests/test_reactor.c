
#include "sigreactor.h"
#include <stdio.h>
#include <unistd.h>

static int called = 0;

static void timer_callback(reactor_t *r, int fd,
                           uint32_t events, void *arg)
{
    (void)r;
    (void)events;
    (void)arg;

    unsigned long expirations;
    if (read(fd, &expirations, sizeof(expirations)) ==
        sizeof(expirations))
    {
        called = 1;
    }
    reactor_stop(r);
}

int main(void)
{
    reactor_t *r = reactor_create();
    if (!r)
    {
        perror("reactor_create");
        return 1;
    }

    int timer = reactor_timer_create(r, 100, 0,
                                     timer_callback, NULL);
    if (timer < 0)
    {
        perror("reactor_timer_create");
        reactor_destroy(r);
        return 1;
    }

    if (reactor_run(r) < 0)
    {
        perror("reactor_run");
        close(timer);
        reactor_destroy(r);
        return 1;
    }

    close(timer);
    reactor_destroy(r);

    if (!called)
    {
        fprintf(stderr, "FAIL: timer callback not called\n");
        return 1;
    }

    printf("PASS: reactor timer callback executed\n");
    return 0;
}
