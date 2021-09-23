#include "vm.h"

#include <stdio.h>

int main(int argc, const char* argv[]) {
    /* const char* source = "(1.5 + 3.4) + 10 * 1.5 != 19.9 == !true"; */
    const char* source = "\"abc\" + \"def\" == \"abcdef\"";

    VM vm;
    vm_init(&vm);
    vm_interpret(&vm, source);
    vm_free(&vm);
}
