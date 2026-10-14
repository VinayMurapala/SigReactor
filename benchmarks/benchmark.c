#define _GNU_SOURCE
#include "sigreactor.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define TARGET_EVENTS 100000
#define TIMER_INTERVAL_MS 10
#define TARGET_TICKS 100

static int pipefd[2];
static reactor_t *reactor;
static int timerfd_value;

static uint64_t events_processed;
static uint64_t timer_ticks;
static double start_time;
static double previous_tick;
static double total_error_ms;
static double max_error_ms;

static double monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void event_callback(reactor_t *r, int fd,
                           uint32_t events, void *arg)
{
    (void)events;
    (void)arg;

    char value;

    if (read(fd, &value, 1) != 1)
        return;

    events_processed++;

    if (events_processed < TARGET_EVENTS) {
        if (write(pipefd[1], "E", 1) != 1) {
            perror("pipe write");
            reactor_stop(r);
        }
    }

    if (events_processed >= TARGET_EVENTS &&
        timer_ticks >= TARGET_TICKS)
        reactor_stop(r);
}

static void timer_callback(reactor_t *r, int fd,
                           uint32_t events, void *arg)
{
    (void)events;
    (void)arg;

    uint64_t expirations;

    if (read(fd, &expirations, sizeof(expirations))
            != sizeof(expirations))
        return;

    double now = monotonic_seconds();

    for (uint64_t i = 0; i < expirations; i++) {
        timer_ticks++;

        if (previous_tick > 0.0) {
            double interval_ms = (now - previous_tick) * 1000.0;
            double error_ms = interval_ms - TIMER_INTERVAL_MS;

            if (error_ms < 0)
                error_ms = -error_ms;

            total_error_ms += error_ms;

            if (error_ms > max_error_ms)
                max_error_ms = error_ms;
        }

        previous_tick = now;
    }

    if (events_processed >= TARGET_EVENTS &&
        timer_ticks >= TARGET_TICKS)
        reactor_stop(r);
}

int main(void)
{
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    reactor = reactor_create();
    if (!reactor) {
        perror("reactor_create");
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    if (reactor_add(reactor, pipefd[0], EPOLLIN,
                    event_callback, NULL) == -1) {
        perror("reactor_add");
        reactor_destroy(reactor);
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    timerfd_value = reactor_timer_create(
        reactor, TIMER_INTERVAL_MS, TIMER_INTERVAL_MS,
        timer_callback, NULL);

    if (timerfd_value < 0) {
        perror("reactor_timer_create");
        reactor_del(reactor, pipefd[0]);
        reactor_destroy(reactor);
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    if (write(pipefd[1], "E", 1) != 1) {
        perror("initial pipe write");
        close(timerfd_value);
        reactor_del(reactor, pipefd[0]);
        reactor_destroy(reactor);
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    start_time = monotonic_seconds();

    if (reactor_run(reactor) == -1) {
        perror("reactor_run");
        close(timerfd_value);
        reactor_destroy(reactor);
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    double elapsed = monotonic_seconds() - start_time;

    printf("\nSigReactor Performance Benchmark\n");
    printf("--------------------------------\n");
    printf("Pipe callbacks processed : %llu\n",
           (unsigned long long)events_processed);
    printf("Elapsed time             : %.6f seconds\n", elapsed);

    if (elapsed > 0.0)
        printf("Callback throughput      : %.2f events/sec\n",
               (double)events_processed / elapsed);

    printf("Timer interval configured: %d ms\n",
           TIMER_INTERVAL_MS);
    printf("Timer ticks observed     : %llu\n",
           (unsigned long long)timer_ticks);

    if (timer_ticks > 1) {
        printf("Mean absolute interval error: %.3f ms\n",
               total_error_ms / (double)(timer_ticks - 1));
        printf("Maximum interval error      : %.3f ms\n",
               max_error_ms);
    }

    close(timerfd_value);
    reactor_del(reactor, pipefd[0]);
    reactor_destroy(reactor);
    close(pipefd[0]);
    close(pipefd[1]);

    if (events_processed != TARGET_EVENTS ||
        timer_ticks < TARGET_TICKS) {
        fprintf(stderr, "Benchmark incomplete.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
