debug:
	gcc -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-DSUBTLE_DEBUG_TRACE_ALLOC \
		-g -Og *.c -o subtle

build:
	gcc -O2 *.c -o subtle

test: build
	./subtle ./tests/operations
	./subtle ./tests/globals
	./subtle ./tests/locals
	./subtle ./tests/jumps
