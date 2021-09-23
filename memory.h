#ifndef SUBTLE_MEMORY_H
#define SUBTLE_MEMORY_H

#include "common.h"

#define GROW_CAPACITY(n) ((n) < 8 ? 8 : (n) * 2)

#define ALLOCATE(type, size) (type*)memory_realloc(NULL, 0, sizeof(type) * size)
#define FREE(type, pointer) memory_realloc(pointer, sizeof(type), 0)

#define GROW_ARRAY(ptr, type, old_size, new_size) \
    memory_realloc(ptr, sizeof(type) * old_size, \
                   sizeof(type) * new_size)

#define FREE_ARRAY(ptr, type, size) \
    memory_realloc(ptr, sizeof(type) * size, 0)

void* memory_realloc(void* ptr, size_t old_size, size_t new_size);

#endif
