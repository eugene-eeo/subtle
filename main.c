#include "vm.h"

#include <stdlib.h>
#include <stdio.h>

#define FILE_ERROR    60
#define COMPILE_ERROR 65
#define RUNTIME_ERROR 70

// Takes an already-inited VM and runs a REPL.
static void repl(VM* vm) {
    FILE* stream = stdin;
    char* line = NULL;
    size_t len = 0;

    for (;;) {
        printf("> ");
        ssize_t n_read = getline(&line, &len, stream);
        if (n_read == -1) {
            printf("\n");
            if (line != NULL)
                free(line);
            return;
        }
        vm_interpret(vm, line);
    }
}

static char* read_file(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("fopen");
        return NULL;
    }

    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char* source = malloc(size + 1);
    if (source == NULL) {
        perror("malloc");
        return NULL;
    }

    if (fread((void*)source, sizeof(char), size, fp) < size) {
        perror("fread");
        free(source);
        return NULL;
    }

    source[size] = '\0';
    fclose(fp);
    return source;
}

static int run_file(VM* vm, const char* filename)
{
    const char* source = read_file(filename);
    if (source == NULL)
        return FILE_ERROR;

    InterpretResult res = vm_interpret(vm, source);
    free((void*)source);

    if (res == INTERPRET_RUNTIME_ERROR) return RUNTIME_ERROR;
    if (res == INTERPRET_COMPILE_ERROR) return COMPILE_ERROR;
    return 0;
}

int main(int argc, const char* argv[]) {
    int rv = 0;
    VM vm;
    vm_init(&vm);
    if (argc == 1) {
        repl(&vm);
    } else if (argc == 2) {
        rv = run_file(&vm, argv[1]);
    } else {
        fprintf(stderr, "usage: subtle [filename]");
    }
    vm_free(&vm);
    return rv;
}
