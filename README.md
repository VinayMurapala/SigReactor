# SigReactor — Signal-Driven Event Framework

## 1. Project Overview

SigReactor is a Linux event-driven framework written in C11. It demonstrates how I/O readiness, timer events, and signals can be handled through a unified event loop.

The project uses Linux system interfaces to process events efficiently without continuously polling each individual file descriptor.

## 2. Technologies Used

- **Language:** C11
- **Operating System:** Linux
- **Compiler:** GCC
- **I/O multiplexing:** `epoll`
- **Signal handling:** `signalfd`
- **Timer management:** `timerfd`
- **Networking:** TCP sockets
- **Process management:** `fork()`, `waitpid()`, and `SIGCHLD`

## 3. Features

- Event-driven reactor loop
- File-descriptor event registration, modification, and removal
- Signal notification and graceful shutdown
- Timer callbacks and periodic heartbeat
- TCP echo server on port 9090
- Client idle timeout
- Child-process reaping
- Automated functional tests
- Event throughput and timer interval benchmark

## 4. Project Structure

```text
sigreactor/
├── include/
│   └── sigreactor.h
├── src/
│   ├── reactor.c
│   ├── server.c
│   ├── signal.c
│   ├── timer.c
│   └── process.c
├── tests/
│   ├── test_reactor.c
│   └── test_suite.c
├── benchmarks/
│   └── benchmark.c
├── docs/
│   └── design.md
├── Makefile
└── README.md
```

**Note:** The current build compiles `src/reactor.c` and `src/server.c`. The `signal.c`, `timer.c`, and `process.c` files are placeholders and are not separate implementation modules in the current build.

## 5. Build

Compile the application and test programs:

```bash
make clean
make
```

## 6. Run the Server

```bash
./sigreactor
```

The server listens on TCP port 9090. It also displays periodic heartbeat messages and handles shutdown requests.

## 7. Automated Tests

Run the tests:

```bash
make test
```

The extended test suite checks:

1. Timer expiration and callback execution
2. I/O readiness and callback execution
3. SIGUSR1 delivery through `signalfd`
4. EINTR recovery and timer processing

The previously executed test suite passed all four tests.

## 8. TCP Echo Test

Start the server in one terminal:

```bash
./sigreactor
```

Connect from another terminal:

```bash
nc 127.0.0.1 9090
```

Enter a message and press Enter. The server should echo the message back.

## 9. Performance Benchmark

Run:

```bash
make benchmark
```

The benchmark reports the number of pipe callbacks processed, elapsed time, callback throughput, and timer interval error. Results can vary with system load and hardware.

## 10. Graceful Shutdown

Press Ctrl+C in the server terminal. The application should process the shutdown request and terminate cleanly.

## 11. Operating System Concepts Demonstrated

- I/O multiplexing with `epoll`
- Signal notification using `signalfd`
- Timer events using `timerfd`
- TCP socket communication
- Process creation and child-process reaping
- Event-driven programming
- Interrupted system call handling
- Resource cleanup and graceful shutdown

## 12. Future Improvements

- More edge-case and stress tests
- Further validation of simultaneous-client handling
- Improved error-path testing
- Optional separation into dedicated signal, timer, and process modules
- More complete API documentation
