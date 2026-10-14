CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude

TARGET = sigreactor
SOURCES = src/reactor.c src/server.c
TEST_REACTOR = test_reactor
TEST_SUITE = test_suite
BENCHMARK = benchmark_runner

all: $(TARGET)

$(TARGET): $(SOURCES) include/sigreactor.h
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

$(TEST_REACTOR): tests/test_reactor.c src/reactor.c include/sigreactor.h
	$(CC) $(CFLAGS) tests/test_reactor.c src/reactor.c -o $(TEST_REACTOR)

$(TEST_SUITE): tests/test_suite.c src/reactor.c include/sigreactor.h
	$(CC) $(CFLAGS) tests/test_suite.c src/reactor.c -o $(TEST_SUITE)

test: $(TEST_REACTOR) $(TEST_SUITE)
	./$(TEST_REACTOR)
	./$(TEST_SUITE)

$(BENCHMARK): benchmarks/benchmark.c src/reactor.c include/sigreactor.h
	$(CC) $(CFLAGS) benchmarks/benchmark.c src/reactor.c -o $(BENCHMARK)

benchmark: $(BENCHMARK)
	./$(BENCHMARK)

clean:
	rm -f $(TARGET) $(TEST_REACTOR) $(TEST_SUITE) $(BENCHMARK) benchmark

.PHONY: all test benchmark clean
