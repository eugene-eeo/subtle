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
#define MAX_ROOTS 8

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

    // ---- init'ed by core ----
    // Constants needed by the VM
    Value getSlot_string;
    Value setSlot_string;
    Value equal_string;
    Value notEqual_string;
    Value not_string;

    // Core Protos
    ObjObject* ObjectProto;
    ObjObject* FnProto;
    ObjObject* NativeProto;
    ObjObject* NumberProto;
    ObjObject* BooleanProto;
    ObjObject* StringProto;
    // -------------------------

    // ---- GC ----
    Obj* objects;
    size_t bytes_allocated;
    size_t next_gc;
    // The gray_* information encodes the gray stack used by the GC.
    // The mark-sweep GC uses a tricolour abstraction:
    //   1. Black objects are marked, and already processed.
    //   2. Gray objects are marked but not processed (their links
    //      still need to be traversed).
    //   3. White objects are everything else.
    // We put gray objects in gray_stack, and pop it off one-by-one.
    int gray_capacity;
    int gray_count;
    Obj** gray_stack;
    // Stack to temporarily treat values as roots.
    Value roots[MAX_ROOTS];
    int roots_count;
    // ------------

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
// vm_drop is similar to vm_pop, but doesn't return the value, and
// also can drop multiple items at once.
void vm_drop(VM* vm, int count);
void vm_push_root(VM* vm, Value value);
void vm_pop_root(VM* vm);
void vm_runtime_error(VM* vm, const char* format, ...);
InterpretResult vm_interpret(VM* vm, const char* source);

// Object system
// =============
Value vm_get_prototype(VM* vm, Value value);
bool vm_get_slot(VM* vm, Value src, Value slot_name, Value* slot_value);

// Pushes the given closure onto the call stack. Note that the
// num_args argument should be the number of _actual_ arguments.
// The stack should look like this:
//
//                | nargs |
//   +-----+------+-------+
//   | ... | this |  ...  |
//   +-----+------+-------+
//                        ^-- stack_top
bool vm_push_frame(VM* vm, ObjClosure* closure, int num_args);

// Runs the given slot, returning true if the call succeeded
// and false otherwise. Note that unlike vm_push_frame(),
// this will run the slot until completion.
bool vm_call(VM* vm, Value callable, int num_args,
             Value* return_value, InterpretResult* rv);

// Runs the usual invoke path. This uses vm_call internally.
bool vm_invoke(VM* vm,
               Value obj, Value slot_name, int num_args,
               Value* return_value, InterpretResult* rv);
#endif
