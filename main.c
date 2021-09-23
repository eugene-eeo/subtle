#include "vm.h"

#include <stdlib.h>
#include <stdio.h>

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

int main(int argc, const char* argv[]) {
    VM vm;
    vm_init(&vm);
    repl(&vm);
    vm_free(&vm);
    return 0;
}
