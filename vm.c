#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> // free
#include <string.h> // strlen

#ifdef SUBTLE_DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

#define SUBTLE_MAX_FRAMES 1024

void vm_init(VM* vm) {
    vm->fiber = NULL;
    vm->can_yield = true;

    vm->forward_string = NULL;
    vm->init_string = NULL;

    vm->ObjectProto = NULL;
    vm->FnProto = NULL;
    vm->NativeProto = NULL;
    vm->NumberProto = NULL;
    vm->StringProto = NULL;
    vm->FiberProto = NULL;
    vm->RangeProto = NULL;
    vm->ListProto = NULL;
    vm->MapProto = NULL;
    vm->MsgProto = NULL;

    vm->uid = 0;
    vm->handles = NULL;
    vm->extensions = NULL;
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

    ExtContext* ext = vm->extensions;
    while (ext != NULL) {
        ExtContext* next = ext->next;
        ext->free(vm, ext->ctx);
        FREE(vm, ExtContext, ext);
        ext = next;
    }

    // Check that our memory accounting is correct.
    ASSERT(vm->bytes_allocated == 0, "bytes_allocated != 0");
    vm_init(vm);
}

void vm_push(VM* vm, Value value) {
    ASSERT(vm->fiber->stack_capacity >= (vm->fiber->stack_top - vm->fiber->stack) + 1,
           "Stack size was not ensured.");
    *vm->fiber->stack_top = value;
    vm->fiber->stack_top++;
}

Value vm_pop(VM* vm) {
    vm->fiber->stack_top--;
    return *vm->fiber->stack_top;
}

Value vm_peek(VM* vm, int distance) {
    return vm->fiber->stack_top[-1 - distance];
}

void vm_drop(VM* vm, int count) {
    vm->fiber->stack_top -= count;
}

void vm_push_root(VM* vm, Value value) {
    ASSERT(vm->roots_count < MAX_ROOTS, "vm->roots_count == MAX_ROOTS");
    vm->roots[vm->roots_count] = value;
    vm->roots_count++;
}

void vm_pop_root(VM* vm) {
    vm->roots_count--;
}

void vm_runtime_error(VM* vm, const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    ssize_t len = vsnprintf(buffer, 256, format, args);
    va_end(args);

    len = len >= 255 ? 255 : len;
    vm->fiber->error = objstring_copy(vm, buffer, len);
}

static void
print_stack_trace(VM* vm)
{
    fprintf(stderr, "Error: %s\n", vm->fiber->error->chars);
    for (ObjFiber* fiber = vm->fiber;
         fiber != NULL;
         fiber = fiber->parent) {
        fprintf(stderr, "[Fiber %p]\n", (void*) fiber);
        int frames_count = fiber->frames_count;
        for (int i = frames_count - 1; i >= 0; i--) {
            CallFrame* frame = &fiber->frames[i];
            ObjFn* fn = frame->closure->fn;
            if (frames_count - i >= 10 && i > 10) {
                fprintf(stderr, "\t[...]\n");
                i = 10;
                continue;
            }
            // -1 as we increment frame->ip on each loop.
            int instruction = frame->ip - fn->chunk.code - 1;
            fprintf(stderr, "\t[line %u] in %s\n",
                    chunk_get_line(&fn->chunk, instruction),
                    fn->arity == -1 ? "script" :
                    fn->name == NULL ? "fn" : fn->name->chars);
        }
    }
}

static bool
handle_error(VM* vm, ObjFiber* until, int level)
{
    ASSERT(vm->fiber->error != NULL, "Should only be called after an error.");
    ObjFiber* fiber = vm->fiber;
    ObjString* error = fiber->error;

    // Unwind the fiber stack.
    // Find a fiber that's ran with FIBER_TRY, and transfer the
    // error value to the parent.
    while (fiber != NULL) {
        fiber->error = error;
        if (fiber == until && level != -1) return false;
        if (fiber->state == FIBER_TRY) {
            fiber->parent->stack_top[-1] = OBJ_TO_VAL(error);
            vm->fiber = fiber->parent;
            return true;
        }

        ObjFiber* parent = fiber->parent;
        fiber->parent = NULL;
        fiber = parent;
    }

    if (level == -1) {
        print_stack_trace(vm);
        vm->fiber = NULL;
    }
    return false;
}

bool
vm_push_frame(VM* vm, ObjClosure* closure, int num_args)
{
    if (vm->fiber->frames_count > SUBTLE_MAX_FRAMES) {
        vm_runtime_error(vm, "Hit max frame count: %d", SUBTLE_MAX_FRAMES);
        return false;
    }
    Value* stack_start = vm->fiber->stack_top - num_args - 1;

    ObjFn* fn = closure->fn;
    vm_push_root(vm, OBJ_TO_VAL(closure));
    objfiber_push_frame(vm->fiber, vm, closure, stack_start);
    vm_pop_root(vm);
    vm_ensure_stack(vm, fn->max_slots);

    // Fix the number of arguments.
    // Since -1 arity means a script, we ignore that here.
    if (fn->arity != -1) {
        for (int i = 0; i < fn->arity - num_args; i++) vm_push(vm, NIL_VAL);
        if (num_args > fn->arity)
            vm_drop(vm, num_args - fn->arity);
    }
    return true;
}

static inline bool
is_callable(Value v)
{
    return IS_CLOSURE(v) || IS_NATIVE(v);
}

bool
vm_check_call(VM* vm, Value v, int num_args, ObjString* slot)
{
    ASSERT(slot != NULL, "slot may not be NULL");
    if (is_callable(v) || num_args == 0)
        return true;
    vm_push_root(vm, OBJ_TO_VAL(slot));
    vm_runtime_error(vm, "Called a non-callable slot '%s' with %d args.", slot->chars, num_args);
    vm_pop_root(vm);
    return false;
}

bool
vm_complete_call(VM* vm, Value callee, int num_args)
{
    if (IS_CLOSURE(callee)) {
        return vm_push_frame(vm, VAL_TO_CLOSURE(callee), num_args);
    }
    if (IS_NATIVE(callee)) {
        ObjNative* native = VAL_TO_NATIVE(callee);
        Value* args_start = &vm->fiber->stack_top[-num_args - 1];
        return native->fn(vm, native->ctx, args_start, num_args);
    }
    ASSERT(num_args == 0, "num_args != 0");
    vm->fiber->stack_top[-1] = callee;
    return true;
}

static ObjUpvalue*
capture_upvalue(VM* vm, Value* local)
{
    // Before creating a new ObjUpvalue, we search in the list.
    ObjUpvalue* prev = NULL;
    ObjUpvalue* upvalue = vm->fiber->open_upvalues;
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
        vm->fiber->open_upvalues = created;
    } else {
        prev->next = created;
    }
    return created;
}

static void
close_upvalues(ObjFiber* fiber, Value* last)
{
    while (fiber->open_upvalues != NULL
           && fiber->open_upvalues->location >= last) {
        ObjUpvalue* upvalue = fiber->open_upvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        fiber->open_upvalues = upvalue->next;
    }
}

Value
vm_get_prototype(VM* vm, Value value)
{
    switch (value.type) {
        case VALUE_NIL:
        case VALUE_TRUE:
        case VALUE_FALSE:
            return OBJ_TO_VAL(vm->ObjectProto);
        case VALUE_NUMBER:
            return OBJ_TO_VAL(vm->NumberProto);
        case VALUE_OBJ: {
            Obj* object = VAL_TO_OBJ(value);
            switch (object->type) {
                case OBJ_STRING:  return OBJ_TO_VAL(vm->StringProto);
                case OBJ_CLOSURE: return OBJ_TO_VAL(vm->FnProto);
                case OBJ_NATIVE:  return OBJ_TO_VAL(vm->NativeProto);
                case OBJ_FIBER:   return OBJ_TO_VAL(vm->FiberProto);
                case OBJ_RANGE:   return OBJ_TO_VAL(vm->RangeProto);
                case OBJ_LIST:    return OBJ_TO_VAL(vm->ListProto);
                case OBJ_MAP:     return OBJ_TO_VAL(vm->MapProto);
                case OBJ_MSG:     return OBJ_TO_VAL(vm->MsgProto);
                case OBJ_FOREIGN: return VAL_TO_FOREIGN(value)->proto;
                default: UNREACHABLE();
            }
        }
        default: UNREACHABLE();
    }
}

bool
vm_get_slot(VM* vm, Value src, Value slot_name, Value* slot_value)
{
    // We don't mark non-ObjObject values as their prototypes
    // are well-known and can only be given by vm_get_prototype.
    if (IS_OBJECT(src)) {
        Obj* obj = VAL_TO_OBJ(src);
        if (obj->visited) return false;

        // First do a lookup on the object itself.
        ObjObject* object = (ObjObject*)obj;
        if (objobject_get(object, slot_name, slot_value))
            return true;

        // Then do the multiple inheritance.
        bool found = false;
        obj->visited = true;
        for (int i = 0; i < object->protos_count; i++)
            if ((found = vm_get_slot(vm, object->protos[i], slot_name, slot_value)))
                break;
        obj->visited = false;
        return found;
    }
    return vm_get_slot(vm, vm_get_prototype(vm, src), slot_name, slot_value);
}

bool
vm_has_ancestor(VM* vm, Value src, Value ancestor)
{
    if (value_equal(src, ancestor)) return true;
    if (IS_OBJECT(src)) {
        Obj* obj = VAL_TO_OBJ(src);
        if (obj->visited) return false;

        ObjObject* object = (ObjObject*)obj;
        bool found = false;
        obj->visited = true;
        for (int i = 0; i < object->protos_count; i++)
            if ((found = vm_has_ancestor(vm, object->protos[i], ancestor)))
                break;
        obj->visited = false;
        return found;
    }
    return vm_has_ancestor(vm, vm_get_prototype(vm, src), ancestor);
}

typedef bool (*CompleteCallFn)(VM* vm, Value slot, int num_args);

// generic method for invoking a message on an object, doing the
// work using the given `complete_call` callback. The steps are:
//
// 1. try to find the `slot_name` on the protos. if found, then
//    complete the call using `complete_call`.
// 2. try to find a "forward" slot on the protos. convert the call
//    into an equivalent ObjMsg and call the forward slot with
//    that instead.
// 3. if both lookups fail, error out.
//
// this function will check if the call is valid before calling
// the given complete_call callback.
static bool
generic_invoke(VM* vm, Value obj, ObjString* slot_name, int num_args,
               CompleteCallFn complete_call)
{
    // Try to search on the protos.
    Value callee;
    if (vm_get_slot(vm, obj, OBJ_TO_VAL(slot_name), &callee)) {
        if (!vm_check_call(vm, callee, num_args, slot_name))
            return false;
        return complete_call(vm, callee, num_args);
    }

    // Try to call the 'forward' slot with an ObjMsg.
    if (vm_get_slot(vm, obj, OBJ_TO_VAL(vm->forward_string), &callee)
            && is_callable(callee)) {
        vm_push_root(vm, OBJ_TO_VAL(slot_name));
        Value* args = &vm->fiber->stack_top[-num_args];
        ObjMsg* msg = objmsg_new(vm, slot_name, args, num_args);
        vm_drop(vm, num_args);
        vm_push_root(vm, OBJ_TO_VAL(msg));
        vm_ensure_stack(vm, 1);
        vm_push(vm, OBJ_TO_VAL(msg));
        vm_pop_root(vm); // msg
        vm_pop_root(vm); // str
        return complete_call(vm, callee, 1);
    }

    vm_push_root(vm, OBJ_TO_VAL(slot_name));
    vm_runtime_error(vm, "Object does not respond to '%s'.", slot_name->chars);
    vm_pop_root(vm);
    return false;
}

bool
vm_invoke(VM* vm, Value obj, ObjString* slot_name, int num_args)
{
    return generic_invoke(vm, obj, slot_name, num_args, vm_call);
}

// Run the given fiber until fiber->frames_count == top_level.
static InterpretResult
run(VM* vm, ObjFiber* fiber, int top_level)
{
    ObjFiber* original_fiber = fiber;
    CallFrame* frame;

#define REFRESH_FRAME() (frame = &vm->fiber->frames[vm->fiber->frames_count - 1])
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, \
     (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->fn->chunk.constants.values[READ_SHORT()])

    // Actually start running the code here.
    REFRESH_FRAME();

    for (;;) {
#ifdef SUBTLE_DEBUG_TRACE_EXECUTION
        // Trace the stack.
        for (Value* vptr = vm->fiber->stack; vptr != vm->fiber->stack_top; vptr++) {
            printf("[ ");
            debug_print_value(*vptr);
            printf(" ]");
        }
        printf("\n");
        // Trace the about-to-be-executed instruction.
        debug_print_instruction(&frame->closure->fn->chunk,
                                frame->ip - frame->closure->fn->chunk.code);
#endif
        switch (READ_BYTE()) {
            case OP_RETURN: {
                Value result = vm_pop(vm);
                close_upvalues(fiber, frame->slots);
                fiber->frames_count--;
                fiber->stack_top = frame->slots;
                if (fiber == original_fiber && fiber->frames_count == top_level) {
                    vm_push(vm, result);
                    return INTERPRET_OK;
                }
                if (objfiber_is_done(fiber)) {
                    // Transfer control to the parent fiber.
                    fiber = fiber->parent;
                    vm->fiber = fiber;
                    if (fiber == NULL)
                       return INTERPRET_OK; // Nothing to do?
                    fiber->stack_top[-1] = result;
                    REFRESH_FRAME();
                } else {
                    vm_push(vm, result);
                    REFRESH_FRAME();
                }
                break;
            }
            case OP_CONSTANT: vm_push(vm, READ_CONSTANT()); break;
            case OP_POP:      vm_pop(vm); break;
            case OP_TRUE:     vm_push(vm, TRUE_VAL); break;
            case OP_FALSE:    vm_push(vm, FALSE_VAL); break;
            case OP_NIL:      vm_push(vm, NIL_VAL); break;
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
                    vm_runtime_error(vm, "Undefined variable '%s'.", VAL_TO_STRING(name)->chars);
                    goto handle_fibers;
                }
                vm_push(vm, value);
                break;
            }
            case OP_SET_GLOBAL: {
                Value name = READ_CONSTANT();
                if (table_set(&vm->globals, vm, name, vm_peek(vm, 0))) {
                    table_delete(&vm->globals, vm, name);
                    vm_runtime_error(vm, "Undefined variable '%s'.", VAL_TO_STRING(name)->chars);
                    goto handle_fibers;
                }
                break;
            }
            case OP_ASSERT: {
                if (!value_truthy(vm_pop(vm))) {
                    vm_runtime_error(vm, "Assertion failed.");
                    goto handle_fibers;
                }
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
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (!value_truthy(vm_pop(vm)))
                    frame->ip += offset;
                break;
            }
            case OP_OR: {
                uint16_t offset = READ_SHORT();
                if (value_truthy(vm_peek(vm, 0)))
                    frame->ip += offset;
                else
                    vm_pop(vm);
                break;
            }
            case OP_AND: {
                uint16_t offset = READ_SHORT();
                if (!value_truthy(vm_peek(vm, 0)))
                    frame->ip += offset;
                else
                    vm_pop(vm);
                break;
            }
            case OP_CLOSURE: {
                ObjFn* fn = VAL_TO_FN(READ_CONSTANT());
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
                close_upvalues(fiber, fiber->stack_top - 1);
                vm_pop(vm);
                break;
            }
            case OP_OBJECT: {
                ObjObject* object = objobject_new(vm);
                vm_push(vm, OBJ_TO_VAL(object));
                objobject_set_proto(object, vm, OBJ_TO_VAL(vm->ObjectProto));
                break;
            }
            case OP_OBJLIT_SET: {
                Value key = READ_CONSTANT();
                Value obj = vm_peek(vm, 1);
                Value value = vm_peek(vm, 0);
                objobject_set(VAL_TO_OBJECT(obj), vm, key, value);
                vm_pop(vm); // value
                break;
            }
            case OP_INVOKE: {
                Value key = READ_CONSTANT();
                uint8_t num_args = READ_BYTE();
                Value obj = vm_peek(vm, num_args);
                // The stack is already in the correct form for a method call.
                // We have `obj` followed by `num_args`.
                generic_invoke(vm, obj,
                               VAL_TO_STRING(key), num_args,
                               vm_complete_call);
handle_fibers:
                fiber = vm->fiber;
                if (fiber == NULL) return INTERPRET_OK;
                if (fiber->error != NULL) {
                    if (!handle_error(vm, original_fiber, top_level))
                        return INTERPRET_RUNTIME_ERROR;
                    fiber = vm->fiber;
                }
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
vm_call(VM* vm, Value slot, int num_args)
{
    bool rv;
    bool can_yield = vm->can_yield;
    vm->can_yield = false;
    if (IS_CLOSURE(slot)) {
        ObjClosure* closure = VAL_TO_CLOSURE(slot);
        if ((rv = vm_push_frame(vm, closure, num_args)))
            rv = run(vm, vm->fiber, vm->fiber->frames_count - 1) == INTERPRET_OK;
    } else if (IS_NATIVE(slot)) {
        ObjNative* native = VAL_TO_NATIVE(slot);
        rv = native->fn(vm, native->ctx, &vm->fiber->stack_top[-num_args - 1], num_args);
    } else {
        ASSERT(num_args == 0, "num_args != 0");
        vm->fiber->stack_top[-1] = slot;
        rv = true;
    }
    vm->can_yield = can_yield;
    return rv;
}

InterpretResult
vm_interpret(VM* vm, const char* source)
{
    ObjFn* fn = compile(vm, source);
    if (fn == NULL) return INTERPRET_COMPILE_ERROR;

    vm_push_root(vm, OBJ_TO_VAL(fn));
    ObjClosure* closure = objclosure_new(vm, fn);
    vm_push_root(vm, OBJ_TO_VAL(closure));
    vm->fiber = objfiber_new(vm, closure);
    vm->fiber->state = FIBER_ROOT;
    vm_pop_root(vm); // closure
    vm_pop_root(vm); // fn

    InterpretResult result = run(vm, vm->fiber, -1);
    vm->fiber = NULL;
    return result;
}

// Extension API
// =============

uid_t
vm_get_uid(VM* vm)
{
    return ++vm->uid;
}

void
vm_add_global(VM* vm, char* name, Value v)
{
    vm_push_root(vm, v);
    ObjString* s = objstring_copy(vm, name, strlen(name));
    vm_push_root(vm, OBJ_TO_VAL(s));
    table_set(&vm->globals, vm, OBJ_TO_VAL(s), v);
    vm_pop_root(vm); // name
    vm_pop_root(vm); // v
}

void
vm_add_extension(VM *vm, void *p, GCFn free)
{
    ExtContext* ctx = ALLOCATE(vm, ExtContext);
    ctx->ctx = p;
    ctx->free = free;
    ctx->next = vm->extensions;
    vm->extensions = ctx;
}

Handle*
handle_new(VM* vm, Value v) {
    vm_push_root(vm, v);
    Handle* h = ALLOCATE(vm, Handle);
    vm_pop_root(vm); // v
    h->value = v;
    h->prev = vm->handles;
    if (vm->handles != NULL)
        vm->handles->next = h;
    h->next = NULL;
    vm->handles = h;
    return h;
}

void
handle_release(VM* vm, Handle* h)
{
    // head of the linked list?
    if (vm->handles == h) vm->handles = h->next;

    // unlink from the linked list
    if (h->prev != NULL) h->prev->next = h->next;
    if (h->next != NULL) h->next->prev = h->prev;

    FREE(vm, Handle, h);
}
