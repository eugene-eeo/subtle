cute:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-g -Og *.c -o subtle

debug:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-DSUBTLE_DEBUG_TRACE_ALLOC \
		-g -Og *.c -o subtle

release:
	gcc -O3 *.c -o subtle

stress:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_STRESS_GC \
		-g -Og *.c -o subtle

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
