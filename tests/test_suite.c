
#define _GNU_SOURCE
#include "sigreactor.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

/* ---------- Timer test ---------- */
static int timer_ok;

static void timer_cb(reactor_t *r, int fd,
                     uint32_t events, void *arg)
{
    (void)events;
    (void)arg;

    uint64_t expirations;
    if (read(fd, &expirations, sizeof(expirations))
            == sizeof(expirations) && expirations > 0)
        timer_ok = 1;

    reactor_stop(r);
}

static int test_timer(void)
{
    reactor_t *r = reactor_create();
    if (!r) {
        perror("reactor_create");
        return 0;
    }

    int fd = reactor_timer_create(r, 100, 0, timer_cb, NULL);
    if (fd < 0) {
        perror("reactor_timer_create");
        reactor_destroy(r);
        return 0;
    }

    int rc = reactor_run(r);
    close(fd);
    reactor_destroy(r);

    return rc == 0 && timer_ok;
}

/* ---------- I/O readiness test ---------- */
static int io_ok;

static void io_cb(reactor_t *r, int fd,
                  uint32_t events, void *arg)
{
    (void)events;
    (void)arg;

    char ch;
    if (read(fd, &ch, 1) == 1 && ch == 'X')
        io_ok = 1;

    reactor_stop(r);
}

static int test_io(void)
{
    int p[2];
    if (pipe(p) == -1) {
        perror("pipe");
        return 0;
    }

    reactor_t *r = reactor_create();
    if (!r) {
        perror("reactor_create");
        close(p[0]);
        close(p[1]);
        return 0;
    }

    if (reactor_add(r, p[0], EPOLLIN, io_cb, NULL) == -1) {
        perror("reactor_add");
        reactor_destroy(r);
        close(p[0]);
        close(p[1]);
        return 0;
    }

    if (write(p[1], "X", 1) != 1) {
        perror("write");
        reactor_del(r, p[0]);
        reactor_destroy(r);
        close(p[0]);
        close(p[1]);
        return 0;
    }

    int rc = reactor_run(r);

    reactor_del(r, p[0]);
    reactor_destroy(r);
    close(p[0]);
    close(p[1]);

    return rc == 0 && io_ok;
}

/* ---------- Signal delivery test ---------- */
static volatile sig_atomic_t signal_ok;

static void signal_cb(reactor_t *r, int signo, void *arg)
{
    (void)arg;

    if (signo == SIGUSR1)
        signal_ok = 1;

    reactor_stop(r);
}

static int test_signal(void)
{
    reactor_t *r = reactor_create();
    if (!r) {
        perror("reactor_create");
        return 0;
    }

    int sig = SIGUSR1;
    if (reactor_signals(r, &sig, 1, signal_cb, NULL) < 0) {
        perror("reactor_signals");
        reactor_destroy(r);
        return 0;
    }

    if (kill(getpid(), SIGUSR1) == -1) {
        perror("kill");
        reactor_destroy(r);
        return 0;
    }

    int rc = reactor_run(r);
    reactor_destroy(r);

    return rc == 0 && signal_ok;
}

/* ---------- EINTR recovery test ---------- */
static volatile sig_atomic_t alarm_seen;
static int eintr_timer_ok;

static void alarm_handler(int signo)
{
    (void)signo;
    alarm_seen = 1;
}

static void eintr_timer_cb(reactor_t *r, int fd,
                           uint32_t events, void *arg)
{
    (void)events;
    (void)arg;

    uint64_t expirations;
    if (read(fd, &expirations, sizeof(expirations))
            == sizeof(expirations) && expirations > 0)
        eintr_timer_ok = 1;

    reactor_stop(r);
}

static int test_eintr(void)
{
    struct sigaction sa = {0};
    struct sigaction old_sa;

    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, &old_sa) == -1) {
        perror("sigaction");
        return 0;
    }

    reactor_t *r = reactor_create();
    if (!r) {
        perror("reactor_create");
        sigaction(SIGALRM, &old_sa, NULL);
        return 0;
    }

    int fd = reactor_timer_create(r, 300, 0,
                                  eintr_timer_cb, NULL);
    if (fd < 0) {
        perror("reactor_timer_create");
        reactor_destroy(r);
        sigaction(SIGALRM, &old_sa, NULL);
        return 0;
    }

    struct itimerval it = {0};
    it.it_value.tv_usec = 50000;

    if (setitimer(ITIMER_REAL, &it, NULL) == -1) {
        perror("setitimer");
        close(fd);
        reactor_destroy(r);
        sigaction(SIGALRM, &old_sa, NULL);
        return 0;
    }

    int rc = reactor_run(r);

    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &it, NULL);

    close(fd);
    reactor_destroy(r);
    sigaction(SIGALRM, &old_sa, NULL);

    return rc == 0 && alarm_seen && eintr_timer_ok;
}

/* ---------- Main test runner ---------- */
int main(void)
{
    int passed = 0;
    int total = 4;

    puts("SigReactor Extended Test Suite");
    puts("------------------------------");

    if (test_timer()) {
        puts("[PASS] Timer expiration and callback");
        passed++;
    } else {
        puts("[FAIL] Timer expiration and callback");
    }

    if (test_io()) {
        puts("[PASS] I/O readiness and callback");
        passed++;
    } else {
        puts("[FAIL] I/O readiness and callback");
    }

    if (test_signal()) {
        puts("[PASS] SIGUSR1 delivery through signalfd");
        passed++;
    } else {
        puts("[FAIL] SIGUSR1 delivery through signalfd");
    }

    if (test_eintr()) {
        puts("[PASS] EINTR recovery and timer processing");
        passed++;
    } else {
        puts("[FAIL] EINTR recovery and timer processing");
    }

    printf("------------------------------\n");
    printf("Result: %d/%d tests passed\n", passed, total);

    return passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
