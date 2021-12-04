ci: lint test release

CCFLAGS=-Wall -pedantic
DEPS=$(shell ls *.c vendor/*.c | grep -v main.c)
MAIN=$(DEPS) main.c

cute:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-g -Og $(MAIN) $(CCFLAGS) -o subtle

debug:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-DSUBTLE_DEBUG_TRACE_ALLOC \
		-g -Og $(MAIN) $(CCFLAGS) -o subtle

release:
	gcc -O3 $(MAIN) $(CCFLAGS) -o subtle

stress:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_STRESS_GC \
		-g -Og $(MAIN) -o subtle

benchmark:
	mkdir -p build
	gcc -DSUBTLE_DEBUG_TABLE_STATS \
		-O3 $(CCFLAGS) $(DEPS) \
		bench/benchmark_table.c -o build/table_benchmark
	./build/table_benchmark

lint:
	cppcheck *.c
	clang-tidy *.c -checks=performance-*,clang-analyzer-*,-clang-analyzer-cplusplus*

test: stress
	valgrind -q ./subtle ./tests/operations
	valgrind -q ./subtle ./tests/globals
	valgrind -q ./subtle ./tests/locals
	valgrind -q ./subtle ./tests/jumps
	valgrind -q ./subtle ./tests/functions
	valgrind -q ./subtle ./tests/closures
	valgrind -q ./subtle ./tests/this
	valgrind -q ./subtle ./tests/vm_call
	valgrind -q ./subtle ./tests/compact
	valgrind -q ./subtle ./tests/table

vendor_deps:
	mkdir -p vendor
	wget 'https://raw.githubusercontent.com/antirez/linenoise/master/linenoise.c' -O vendor/linenoise.c
	wget 'https://raw.githubusercontent.com/antirez/linenoise/master/linenoise.h' -O vendor/linenoise.h
