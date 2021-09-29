debug:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-DSUBTLE_DEBUG_TRACE_ALLOC \
		-g -Og *.c -o subtle

build:
	gcc -Og -g *.c -o subtle

test: build
	valgrind -q ./subtle ./tests/operations
	valgrind -q ./subtle ./tests/globals
	valgrind -q ./subtle ./tests/locals
	valgrind -q ./subtle ./tests/jumps
	valgrind -q ./subtle ./tests/functions
	valgrind -q ./subtle ./tests/closures
