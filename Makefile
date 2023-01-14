ci: lint test

CCFLAGS=-Wall -pedantic
CC=gcc
DEPS=$(shell ls *.c vendor/*.c | grep -v main.c)
MAIN=$(DEPS) main.c

core.subtle.inc: core.subtle gen.py
	python3 gen.py

cute: core.subtle.inc
	$(CC) $(CCFLAGS) -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_EXECUTION \
		-DSUBTLE_DEBUG_PRINT_CODE \
		-DSUBTLE_DEBUG_STRESS_GC \
		-g -Og $(MAIN) -o subtle

debug: core.subtle.inc
	$(CC) $(CCFLAGS) -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_TRACE_ALLOC \
		-DSUBTLE_DEBUG_STRESS_GC \
		-g -Og $(MAIN) -o subtle

release: core.subtle.inc
	$(CC) $(CCFLAGS) -O2 -flto=auto $(MAIN) -o subtle

stress: core.subtle.inc
	$(CC) $(CCFLAGS) -DSUBTLE_DEBUG \
		-DSUBTLE_DEBUG_STRESS_GC \
		-g -Og $(MAIN) -o subtle

profile: core.subtle.inc
	$(CC) $(CCFLAGS) -Og $(MAIN) -pg -o subtle

lint:
	cppcheck *.c
	# clang-tidy *.c -checks=performance-*,clang-analyzer-*,-clang-analyzer-cplusplus*

run_test:
	$(RUNNER) ./subtle ./tests/README
	$(RUNNER) ./subtle ./tests/operations
	$(RUNNER) ./subtle ./tests/globals
	$(RUNNER) ./subtle ./tests/locals
	$(RUNNER) ./subtle ./tests/jumps
	$(RUNNER) ./subtle ./tests/functions
	$(RUNNER) ./subtle ./tests/closures
	$(RUNNER) ./subtle ./tests/this
	$(RUNNER) ./subtle ./tests/compact
	$(RUNNER) ./subtle ./tests/table
	$(RUNNER) ./subtle ./tests/x
	$(RUNNER) ./subtle ./tests/for-loops
	$(RUNNER) ./subtle ./tests/fiber
	$(RUNNER) ./subtle ./tests/fiber2
	$(RUNNER) ./subtle ./tests/list
	$(RUNNER) ./subtle ./tests/newlines
	$(RUNNER) ./subtle ./tests/perform

test:
	make stress
	make run_test RUNNER="valgrind -q"
	make release
	make run_test RUNNER="valgrind -q"
	make run_test

vendor_deps:
	mkdir -p vendor
	wget 'https://raw.githubusercontent.com/antirez/linenoise/master/linenoise.c' -O vendor/linenoise.c
	wget 'https://raw.githubusercontent.com/antirez/linenoise/master/linenoise.h' -O vendor/linenoise.h

clangd_index:
	bear -- make release
