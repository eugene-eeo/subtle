#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> // free

#ifdef SUBTLE_DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#define MAX_LOOKUPS 64

void vm_init(VM* vm) {
    vm->frame_count = 0;
    vm->stack_top = vm->stack;
    vm->open_upvalues = NULL;

    vm->ObjectProto = NULL;
    vm->FnProto = NULL;
    vm->NativeProto = NULL;
    vm->NumberProto = NULL;
    vm->BooleanProto = NULL;
    vm->StringProto = NULL;

    vm->objects = NULL;
    vm->bytes_allocated = 0;
    vm->next_gc = 1024 * 1024;
    vm->gray_capacity = 0;
    vm->gray_count = 0;
    vm->gray_stack = NULL;
    vm->roots_count = 0;

    table_init(&vm->strings);
    table_init(&vm->globals);

    vm->compiler = NULL;
}

void vm_free(VM* vm) {
    // Note: the loop below will free the *Proto fields.
    Obj* obj = vm->objects;
    while (obj != NULL) {
        Obj* next = obj->next;
        object_free(obj, vm);
        obj = next;
    }
    table_free(&vm->strings, vm);
    table_free(&vm->globals, vm);
    free(vm->gray_stack);
    vm_init(vm);
}

void vm_push(VM* vm, Value value) {
    *vm->stack_top = value;
    vm->stack_top++;
}

Value vm_pop(VM* vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

Value vm_peek(VM* vm, int distance) {
    return vm->stack_top[-1 - distance];
}

void vm_push_root(VM* vm, Value value) {
    ASSERT(vm->roots_count < MAX_ROOTS, "vm->roots_count == MAX_ROOTS");
    vm->roots[vm->roots_count] = value;
    vm->roots_count++;
}

void vm_pop_root(VM* vm) {
    vm->roots_count--;
}

static void vm_reset_stack(VM* vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
}

void vm_runtime_error(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm->frame_count - 1; i >= 0; i--) {
        CallFrame* frame = &vm->frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk.code;
        fprintf(stderr, "[line %zu] in %s\n",
                chunk_get_line(&function->chunk, instruction),
                function->arity == -1 ? "script" : "fn");
    }
}

bool
vm_push_frame(VM* vm, ObjClosure* closure, int args)
{
    if (vm->frame_count == FRAMES_MAX) {
        vm_runtime_error(vm, "Stack overflow.");
        return false;
    }
    ObjFunction* function = closure->function;
    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = function->chunk.code;
    frame->slots = vm->stack_top - args - 1;
    // Fix the number of arguments.
    // Since -1 arity means a script, we ignore that here.
    if (function->arity != -1) {
        for (int i = 0; i < function->arity - args; i++) vm_push(vm, NIL_VAL);
        for (int i = 0; i < args - function->arity; i++) vm_pop(vm);
    }
    return true;
}

static bool
complete_call(VM* vm, Value callee, int args)
{
    if (IS_OBJ(callee)) {
        switch(VAL_TO_OBJ(callee)->type) {
            case OBJ_CLOSURE:
                return vm_push_frame(vm, VAL_TO_CLOSURE(callee), args);
            case OBJ_NATIVE: {
                ObjNative* native = VAL_TO_NATIVE(callee);
                Value* args_start = &vm->stack_top[-args - 1];
                return native->fn(vm, args_start, args);
            }
            default: ; // It's an error.
        }
    }
    vm_runtime_error(vm, "Tried to call non-callable value.");
    return false;
}

static ObjUpvalue*
capture_upvalue(VM* vm, Value* local)
{
    // Before creating a new ObjUpvalue, we search in the list.
    ObjUpvalue* prev = NULL;
    ObjUpvalue* upvalue = vm->open_upvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* created = objupvalue_new(vm, local);
    created->next = upvalue;

    if (prev == NULL) {
        vm->open_upvalues = created;
    } else {
        prev->next = created;
    }
    return created;
}

static void
close_upvalues(VM* vm, Value* last)
{
    while (vm->open_upvalues != NULL
           && vm->open_upvalues->location >= last) {
        ObjUpvalue* upvalue = vm->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->open_upvalues = upvalue->next;
    }
}

Value
vm_get_prototype(VM* vm, Value value)
{
    switch (value.type) {
        case VALUE_UNDEFINED: UNREACHABLE();
        case VALUE_NIL: return OBJ_TO_VAL(vm->ObjectProto);
        case VALUE_BOOL: return OBJ_TO_VAL(vm->BooleanProto);
        case VALUE_NUMBER: return OBJ_TO_VAL(vm->NumberProto);
        case VALUE_OBJ: {
            Obj* object = VAL_TO_OBJ(value);
            switch (object->type) {
                case OBJ_FUNCTION:
                case OBJ_UPVALUE:
                    UNREACHABLE();
                case OBJ_STRING:
                    return OBJ_TO_VAL(vm->StringProto);
                case OBJ_NATIVE:
                    return OBJ_TO_VAL(vm->NativeProto);
                case OBJ_OBJECT:
                    return ((ObjObject*)object)->proto;
                case OBJ_CLOSURE:
                    return OBJ_TO_VAL(vm->FnProto);
            }
        }
    }
}

bool
vm_get_slot(VM* vm, Value src, Value slot_name, Value* slot_value)
{
    int lookups = 0;
    do {
        if (IS_OBJECT(src) && objobject_get(VAL_TO_OBJECT(src), slot_name, slot_value))
            return true;
        src = vm_get_prototype(vm, src);
        lookups++;
    } while (!IS_NIL(src) && lookups < MAX_LOOKUPS);
    return false;
}

static inline bool
is_activatable(Value value)
{
    return IS_CLOSURE(value) || IS_NATIVE(value);
}

static InterpretResult run(VM* vm, ObjClosure* top_level) {
    CallFrame* frame;

#define REFRESH_FRAME() (frame = &vm->frames[vm->frame_count - 1])
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, \
     (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_SHORT()])

    // Actually start running the code here.
    REFRESH_FRAME();

    for (;;) {
        uint8_t instruction;
#ifdef SUBTLE_DEBUG_TRACE_EXECUTION
        // Trace the stack.
        for (Value* vptr = vm->stack; vptr != vm->stack_top; vptr++) {
            printf("[ ");
            debug_print_value(*vptr);
            printf(" ]");
        }
        printf("\n");
        // Trace the about-to-be-executed instruction.
        debug_print_instruction(&frame->closure->function->chunk,
                                (int)(frame->ip - frame->closure->function->chunk.code));
#endif
        switch (instruction = READ_BYTE()) {
            case OP_RETURN: {
                Value result = vm_pop(vm);
                close_upvalues(vm, frame->slots);
                vm->frame_count--;
                vm->stack_top = frame->slots;
                vm_push(vm, result);
                if (frame->closure == top_level)
                    return INTERPRET_OK;
                ASSERT(vm->frame_count > 0, "vm->frame_count == 0");
                REFRESH_FRAME();
                break;
            }
            case OP_CONSTANT: {
                vm_push(vm, READ_CONSTANT());
                break;
            }
            case OP_POP: vm_pop(vm); break;
            case OP_TRUE:  vm_push(vm, BOOL_TO_VAL(true)); break;
            case OP_FALSE: vm_push(vm, BOOL_TO_VAL(false)); break;
            case OP_NIL:   vm_push(vm, NIL_VAL); break;
            case OP_DEF_GLOBAL: {
                Value name = READ_CONSTANT();
                table_set(&vm->globals, vm, name, vm_peek(vm, 0));
                vm_pop(vm);
                break;
            }
            case OP_GET_GLOBAL: {
                Value name = READ_CONSTANT();
                Value value;
                if (!table_get(&vm->globals, name, &value)) {
                    vm_runtime_error(vm, "Undefined variable '%s'.",
                                  VAL_TO_STRING(name)->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_push(vm, value);
                break;
            }
            case OP_SET_GLOBAL: {
                Value name = READ_CONSTANT();
                if (table_set(&vm->globals, vm, name, vm_peek(vm, 0))) {
                    table_delete(&vm->globals, name);
                    vm_runtime_error(vm, "Undefined variable '%s'.",
                                  VAL_TO_STRING(name)->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_ASSERT: {
                if (!value_truthy(vm_peek(vm, 0))) {
                    vm_runtime_error(vm, "Assertion failed.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_pop(vm);
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm_push(vm, frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = vm_peek(vm, 0);
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_TRUE: {
                uint16_t offset = READ_SHORT();
                if (value_truthy(vm_peek(vm, 0)))
                    frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (!value_truthy(vm_peek(vm, 0)))
                    frame->ip += offset;
                break;
            }
            case OP_CLOSURE: {
                ObjFunction* fn = VAL_TO_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = objclosure_new(vm, fn);
                vm_push(vm, OBJ_TO_VAL(closure));
                for (int i = 0; i < closure->upvalue_count; i++) {
                    uint8_t is_local = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (is_local) {
                        // If it's a local upvalue, then the captured value
                        // can be found in the current frame.
                        closure->upvalues[i] = capture_upvalue(vm, frame->slots + index);
                    } else {
                        // Otherwise, the non-local upvalue should be
                        // captured by this frame's upvalues (the compiler
                        // should add one upvalue to this function).
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                vm_push(vm, *frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = vm_peek(vm, 0);
                break;
            }
            case OP_CLOSE_UPVALUE: {
                close_upvalues(vm, vm->stack_top - 1);
                vm_pop(vm);
                break;
            }
            case OP_OBJECT: {
                ObjObject* object = objobject_new(vm);
                object->proto = OBJ_TO_VAL(vm->ObjectProto);
                vm_push(vm, OBJ_TO_VAL(object));
                break;
            }
            case OP_OBJLIT_SET: {
                Value key = READ_CONSTANT();
                Value obj = vm_peek(vm, 1);
                Value value = vm_peek(vm, 0);
                ObjObject* object = VAL_TO_OBJECT(obj);
                objobject_set(object, vm, key, value);
                vm_pop(vm); // value
                break;
            }
            case OP_OBJECT_SET: {
                Value key = READ_CONSTANT();
                Value obj = vm_peek(vm, 1);
                Value value = vm_peek(vm, 0);
                if (!IS_OBJECT(obj)) {
                    vm_runtime_error(vm, "Trying to set slot on non-object.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjObject* object = VAL_TO_OBJECT(obj);

                // Check if there is a custom setslot method
                Value setSlot_slot;
                if (vm_get_slot(vm, obj, OBJ_TO_VAL(objstring_copy(vm, "setSlot", 7)), &setSlot_slot)) {
                    InterpretResult rv;
                    vm_push(vm, obj);
                    vm_push(vm, key);
                    vm_push(vm, value);
                    Value return_value;
                    if (!vm_call(vm, setSlot_slot, 2, &return_value, &rv))
                        return rv;
                    value = return_value;
                } else {
                    objobject_set(object, vm, key, value);
                }

                vm_pop(vm); // value
                vm_pop(vm); // object
                vm_push(vm, value);
                break;
            }
            case OP_INVOKE: {
                Value key = READ_CONSTANT();
                uint8_t num_args = READ_BYTE();
                Value obj = vm_peek(vm, num_args);
                Value slot;
                Value getSlot_slot;
                // Check if there is a custom getSlot method
                if (vm_get_slot(vm, obj, OBJ_TO_VAL(objstring_copy(vm, "getSlot", 7)), &getSlot_slot)) {
                    InterpretResult rv;
                    vm_push(vm, obj);
                    vm_push(vm, key);
                    if (!vm_call(vm, getSlot_slot, 1, &slot, &rv))
                        return rv;
                } else if (!vm_get_slot(vm, obj, key, &slot)) {
                    slot = NIL_VAL;
                }
                if (!is_activatable(slot)) {
                    if (num_args != 0) {
                        vm_runtime_error(vm, "Non-activatable slot called with %d arguments", num_args);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    vm_pop(vm); // The object.
                    vm_push(vm, slot);
                    break;
                }
                // The stack is already in the correct form for a method call.
                // The object is above `num_args` values.
                if (!complete_call(vm, slot, num_args))
                    return INTERPRET_RUNTIME_ERROR;
                REFRESH_FRAME();
                break;
            }
            default: UNREACHABLE();
        }
    }

#undef REFRESH_FRAME
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
}

bool
vm_call(VM* vm, Value slot, int num_args,
        Value* return_value, InterpretResult* rv)
{
    if (!IS_OBJ(slot)) {
        vm_runtime_error(vm, "Tried to call a non-activatable slot.");
        *rv = INTERPRET_RUNTIME_ERROR;
        return false;
    }

    switch (VAL_TO_OBJ(slot)->type) {
        case OBJ_CLOSURE: {
            ObjClosure* closure = VAL_TO_CLOSURE(slot);
            vm_push_frame(vm, closure, num_args);
            *rv = run(vm, closure);
            break;
        }
        case OBJ_NATIVE: {
            ObjNative* native = VAL_TO_NATIVE(slot);
            if (native->fn(vm, &vm->stack_top[-num_args - 1], num_args)) {
                *rv = INTERPRET_OK;
            } else {
                *rv = INTERPRET_RUNTIME_ERROR;
            }
            break;
        }
        default: UNREACHABLE();
    }

    if (*rv == INTERPRET_OK) {
        *return_value = vm_pop(vm);
        return true;
    }
    return false;
}

InterpretResult vm_interpret(VM* vm, const char* source) {
    ObjFunction* fn = compile(vm, source);
    if (fn == NULL) return INTERPRET_COMPILE_ERROR;

    vm_push(vm, OBJ_TO_VAL(fn));
    ObjClosure* closure = objclosure_new(vm, fn);
    vm_pop(vm);
    vm_push(vm, OBJ_TO_VAL(closure));
    vm_push_frame(vm, closure, 0);

    InterpretResult result = run(vm, closure);
    vm_reset_stack(vm);
    return result;
}
