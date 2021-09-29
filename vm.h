#ifndef SUBTLE_VM_H
#define SUBTLE_VM_H

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "object.h"
#include "table.h"
#include "compiler.h"

#define FRAMES_MAX 64
#define STACK_MAX (256 * FRAMES_MAX)

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

typedef struct VM {
    CallFrame frames[FRAMES_MAX];
    int frame_count;

    Value stack[STACK_MAX];
    Value* stack_top;

    // The _top_ of the open upvalues list is the upvalue associated
    // with the _topmost_ stack slot. That is, if we navigate the
    // ->next pointers of each upvalue, we go from upvalues capturing
    // stack_top to the 'bottom' of the stack.
    ObjUpvalue* open_upvalues;

    // GC
    Obj* objects;
    ssize_t bytes_allocated;

    Table strings; // String interning
    Table globals; // Globals

    // The compiler currently used to compile source, so that
    // if a GC happens during compilation, we can track roots.
    Compiler* compiler;
} VM;

void vm_init(VM* vm);
void vm_free(VM* vm);
void vm_push(VM* vm, Value value);
Value vm_pop(VM* vm);
Value vm_peek(VM* vm, int distance);
InterpretResult vm_interpret(VM* vm, const char* source);

#endif
