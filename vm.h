#ifndef SUBTLE_VM_H
#define SUBTLE_VM_H

#include "common.h"
#include "chunk.h"
#include "object.h"
#include "table.h"

#define STACK_MAX 256

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

typedef struct VM {
    Value stack[STACK_MAX];
    Value* stack_top;
    Chunk* chunk;
    uint8_t* ip;

    // GC
    Object* objects;

    // String interning
    Table strings;
} VM;

void vm_init(VM* vm);
void vm_free(VM* vm);
void vm_push(VM* vm, Value value);
Value vm_pop(VM* vm);
Value vm_peek(VM* vm, int distance);
InterpretResult vm_interpret(VM* vm, const char* source);

#endif
