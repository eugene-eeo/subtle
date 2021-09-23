#include "memory.h"

#include <stdio.h>   // perror
#include <stdlib.h>  // realloc, free

void* memory_realloc(void* ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        // free() never fails
        free(ptr);
        return NULL;
    }

    void* result = realloc(ptr, new_size);
    if (result == NULL) {
        perror("memory_realloc");
        exit(1);
    }
    return result;
}
