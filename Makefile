ci: test release

cute:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-g -Og *.c -Wall -o subtle

debug:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-DSUBTLE_DEBUG_TRACE_ALLOC \
		-g -Og *.c -o subtle

release:
	gcc -O3 *.c -Wall -o subtle

stress:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_STRESS_GC \
		-g -Og *.c -o subtle

DEPS=$(shell ls *.c | grep -v main.c)

benchmark:
	mkdir -p build
	gcc -DSUBTLE_DEBUG_TABLE_STATS \
		-O3 -Wall $(DEPS) \
		bench/benchmark_table.c -o build/table_benchmark
	./build/table_benchmark

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
