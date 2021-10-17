#ifndef SUBTLE_MEMORY_H
#define SUBTLE_MEMORY_H

#include "common.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define GROW_CAPACITY(n)   ((n) < 8 ? 8 : (n) * 2)
#define SHRINK_CAPACITY(n) ((n) > 8 ? (n) / 2 : 8)

#define ALLOCATE(vm, type) (type*)memory_realloc(vm, NULL, 0, sizeof(type))
#define FREE(vm, type, pointer) memory_realloc(vm, pointer, sizeof(type), 0)

#define ALLOCATE_ARRAY(vm, type, size) (type*)memory_realloc(vm, NULL, 0, sizeof(type) * size)
#define GROW_ARRAY(vm, ptr, type, old_size, new_size) \
    memory_realloc(vm, ptr, sizeof(type) * old_size, \
                   sizeof(type) * new_size)

#define FREE_ARRAY(vm, ptr, type, size) \
    memory_realloc(vm, ptr, sizeof(type) * size, 0)

void* memory_realloc(VM* vm, void* ptr, size_t old_size, size_t new_size);
void mark_object(VM*, Obj*);
void mark_value(VM*, Value);
void memory_collect(VM* vm);

#endif
