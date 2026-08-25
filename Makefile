# Custom Memory Allocators -- build file
#
#   make          build tests + benchmark
#   make test     build and run the correctness tests
#   make bench     build and run the benchmarks
#   make clean     remove build artifacts

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude

BIN      := bin
TESTS    := $(BIN)/tests
BENCH    := $(BIN)/benchmark

.PHONY: all test bench clean

all: $(TESTS) $(BENCH)

$(TESTS): src/tests.cpp include/mem/*.hpp | $(BIN)
	$(CXX) $(CXXFLAGS) src/tests.cpp -o $@

$(BENCH): src/benchmark.cpp include/mem/*.hpp | $(BIN)
	$(CXX) $(CXXFLAGS) src/benchmark.cpp -o $@

$(BIN):
	mkdir -p $(BIN)

test: $(TESTS)
	./$(TESTS)

bench: $(BENCH)
	./$(BENCH)

clean:
	rm -rf $(BIN)
