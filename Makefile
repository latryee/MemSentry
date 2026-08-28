CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude -pthread
LDFLAGS ?= -ldl -lpthread

SRCS = src/memsentry.cpp src/allocator_hooks.cpp src/stacktrace.cpp src/snapshot.cpp src/reporter.cpp
OBJS = $(SRCS:.cpp=.o)

BIN_DIR = bin

all: dirs examples tests

dirs:
	mkdir -p $(BIN_DIR)

libmemsentry.a: $(OBJS)
	ar rcs $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

examples: libmemsentry.a
	$(CXX) $(CXXFLAGS) examples/01_basic_leak.cpp libmemsentry.a -o $(BIN_DIR)/01_basic_leak $(LDFLAGS)
	$(CXX) $(CXXFLAGS) examples/02_scoped_profiling.cpp libmemsentry.a -o $(BIN_DIR)/02_scoped_profiling $(LDFLAGS)
	$(CXX) $(CXXFLAGS) examples/03_snapshot_diffing.cpp libmemsentry.a -o $(BIN_DIR)/03_snapshot_diffing $(LDFLAGS)
	$(CXX) $(CXXFLAGS) examples/04_buffer_overflow.cpp libmemsentry.a -o $(BIN_DIR)/04_buffer_overflow $(LDFLAGS)
	$(CXX) $(CXXFLAGS) examples/demo.cpp libmemsentry.a -o $(BIN_DIR)/demo $(LDFLAGS)

tests: libmemsentry.a
	$(CXX) $(CXXFLAGS) tests/test_tracker.cpp libmemsentry.a -o $(BIN_DIR)/test_tracker $(LDFLAGS)
	$(CXX) $(CXXFLAGS) tests/test_canary.cpp libmemsentry.a -o $(BIN_DIR)/test_canary $(LDFLAGS)
	$(CXX) $(CXXFLAGS) tests/test_suite.cpp libmemsentry.a -o $(BIN_DIR)/test_suite $(LDFLAGS)
	$(CXX) $(CXXFLAGS) tests/benchmark.cpp libmemsentry.a -o $(BIN_DIR)/benchmark $(LDFLAGS)

clean:
	rm -rf src/*.o libmemsentry.a $(BIN_DIR)

.PHONY: all dirs examples tests clean
