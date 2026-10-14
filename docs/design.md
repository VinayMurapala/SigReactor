# SigReactor Design Document

## 1. Overview

SigReactor is a Linux event-driven framework implemented in C11. It provides a unified event loop for monitoring I/O readiness, timer events, and operating-system signals.

## 2. Architecture

The framework uses the following Linux interfaces:

- **epoll:** Monitors multiple file descriptors and reports ready events.
- **signalfd:** Exposes configured signals through a file descriptor.
- **timerfd:** Exposes timer expirations through a file descriptor.
- **TCP sockets:** Provide client-server communication.
- **Callbacks:** Execute application logic when registered events become ready.

## 3. File-Descriptor Unification

The reactor registers relevant descriptors with a single epoll instance. These descriptors may represent sockets, timers, or signals.

The event loop waits for ready descriptors using epoll_wait(). When an event occurs, the reactor identifies its registered callback and dispatches the event to the appropriate handler.

This design centralizes event monitoring and avoids requiring a separate polling loop for each event source.

## 4. Asynchronous-Signal Safety

Traditional signal handlers have strict restrictions on the operations they may safely perform. Calling ordinary library functions or manipulating complex shared state inside a handler can introduce unsafe behaviour.

SigReactor uses signalfd to receive configured signals as readable file-descriptor events. The relevant signals must be blocked appropriately before they are consumed through signalfd.

This allows signal processing to take place in the normal reactor event loop rather than performing complex operations inside an asynchronous signal handler.

## 5. Timer Management

The framework uses timerfd to represent timer expirations as file-descriptor events. Timer callbacks can process expiration counts and implement periodic activities such as server heartbeats.

CLOCK_MONOTONIC is suitable for measuring elapsed time because it is not affected by ordinary changes to the system wall clock.

Timer precision and behaviour under load must be measured experimentally.

## 6. Interrupted System Calls

System calls can fail with EINTR when interrupted by a signal. The application must handle this condition according to the particular system call and its documented behaviour.

For example, an interrupted epoll_wait() may be retried when appropriate. Read and write operations must also account for partial transfers, errors, and whether retrying is safe.

The test suite should verify that interruptions do not cause silent event loss or incorrect termination.

## 7. Process Management

The demo server includes child-process management and SIGCHLD handling. The parent process should collect terminated child status using waitpid() and an appropriate nonblocking strategy to prevent zombie processes.

Correctness should be verified through repeated child creation and termination tests.

## 8. Testing Strategy

The test suite should cover:

1. Signal delivery and safe processing.
2. Timer accuracy and periodic expirations.
3. I/O readiness and callback dispatch.
4. EINTR handling and recovery.

## 9. Benchmarking

The benchmark should report the number of processed events, elapsed time, throughput in events per second, and timer deviation under load.

Results should be measured on the target machine. Expected values must not be substituted for actual measurements.

## 10. Limitations

Benchmark results depend on system load, kernel scheduling, hardware, and test configuration. Passing individual tests does not prove the absence of all race conditions or guarantee real-time timer behaviour.

The implementation and its measured results must be reviewed before claiming that every acceptance criterion has been satisfied.
