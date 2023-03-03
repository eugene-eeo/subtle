#ifndef SUBTLE_VM_H
#define SUBTLE_VM_H

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define MAX_ROOTS 8

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

typedef struct VM {
    ObjFiber* fiber;
    // Whether we allow the currently running fiber to yield.
    // At present, fibers cannot yield when they trigger a vm_call,
    // since we assume that we stay in the same fiber after vm_call
    // returns, and the only way to switch _out_ of the current fiber
    // is via Fiber.yield(); since Fiber.call() or Fiber.try() has
    // "stack" semantics.
    bool can_yield;
    bool did_error;

    // ---- init'ed by core ----
    // Constants needed by the VM or core
    ObjString* forward_string;
    ObjString* init_string;

    // Core Protos
    ObjObject* ObjectProto;
    ObjObject* FnProto;
    ObjObject* NativeProto;
    ObjObject* NumberProto;
    ObjObject* StringProto;
    ObjObject* FiberProto;
    ObjObject* RangeProto;
    ObjObject* ListProto;
    ObjObject* MapProto;
    ObjObject* MsgProto;
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

// Returns the prototype of a non-ObjObject value.
Value vm_get_prototype(VM* vm, Value value);
bool vm_get_slot(VM* vm, Value src, Value slot_name, Value* slot_value);
bool vm_has_ancestor(VM* vm, Value src, Value ancestor);

// Invocation
// ==========

// Ensures that we have at least n more slots on the stack.
static inline void
vm_ensure_stack(VM* vm, int n)
{
    objfiber_ensure_stack(vm->fiber, vm, n);
}

// Pushes the given closure onto the call stack. Note that the
// num_args argument should be the number of _actual_ arguments,
// excluding "self".
// The stack should look like this:
//
//                | num_args |
//   +-----+------+----------+
//   | ... | self |   ...    |
//   +-----+------+----------+
//                           ^-- stack_top
bool vm_push_frame(VM* vm, ObjClosure* closure, int num_args);

// Run the given callable, returning true if the call succeeded.
// `callable` MUST be a closure or native, or num_args MUST be 0.
// This should only be called if it's the last thing you do before
// returning from a native function, as it uses vm_push_frame to
// do the work.
bool vm_complete_call(VM* vm, Value callable, int num_args);

// Similar to vm_complete_call, but runs the slot until completion.
// Upon return, if the return value is true, the top of the stack
// will contain the callable's return value. Use this if you need
// to do some work with the return value.
bool vm_call(VM* vm, Value callable, int num_args);

// Runs the usual invoke path. This uses vm_call internally.
bool vm_invoke(VM* vm,
               Value obj, ObjString* slot_name, int num_args);

// Return true if the object `v` is callable.
bool vm_check_call(VM* vm, Value v, int num_args, const char* slot);
#endif
