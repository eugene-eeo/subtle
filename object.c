#include "object.h"

#include "memory.h"
#include "table.h"
#include "vm.h"

#include <stdint.h>
#include <string.h> // memcpy
#ifdef SUBTLE_DEBUG_TRACE_ALLOC
#include <stdio.h>
#endif

// Object memory management
// ========================

Obj*
object_allocate(VM* vm, ObjType type, size_t sz)
{
    Obj* object = memory_realloc(vm, NULL, 0, sz);
    object->type = type;
    object->next = vm->objects;
    object->visited = false;
    object->marked = false;
    vm->objects = object;
#ifdef SUBTLE_DEBUG_TRACE_ALLOC
    printf("%p allocate %zu for type %d\n", object, sz, type);
#endif
    return object;
}

static void objstring_free(VM*, Obj*);
static void objfunction_free(VM*, Obj*);
static void objupvalue_free(VM*, Obj*);
static void objclosure_free(VM*, Obj*);
static void objobject_free(VM*, Obj*);
static void objnative_free(VM*, Obj*);
static void objfiber_free(VM*, Obj*);

void
object_free(Obj* obj, VM* vm)
{
#ifdef SUBTLE_DEBUG_TRACE_ALLOC
    printf("%p free type %d\n", (void*)obj, obj->type);
#endif
    switch (obj->type) {
    case OBJ_STRING: objstring_free(vm, obj); break;
    case OBJ_FUNCTION: objfunction_free(vm, obj); break;
    case OBJ_UPVALUE: objupvalue_free(vm, obj); break;
    case OBJ_CLOSURE: objclosure_free(vm, obj); break;
    case OBJ_OBJECT: objobject_free(vm, obj); break;
    case OBJ_NATIVE: objnative_free(vm, obj); break;
    case OBJ_FIBER: objfiber_free(vm, obj); break;
    }
}

// ObjString
// =========

static uint32_t
hash_string(const char* str, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString*
objstring_new(VM* vm, char* chars, size_t length, uint32_t hash)
{
    ObjString* str = ALLOCATE_OBJECT(vm, OBJ_STRING, ObjString);
    str->chars = chars;
    str->length = length;
    str->hash = hash;

    // intern the string here.
    vm_push_root(vm, OBJ_TO_VAL(str));
    table_set(&vm->strings, vm, OBJ_TO_VAL(str), NIL_VAL);
    vm_pop_root(vm);
    return str;
}

static void
objstring_free(VM* vm, Obj* obj)
{
    ObjString* str = (ObjString*)obj;
    FREE_ARRAY(vm, str->chars, char, str->length + 1);
    FREE(vm, ObjString, str);
}

ObjString*
objstring_copy(VM* vm, const char* src, size_t length)
{
    uint32_t hash = hash_string(src, length);
    ObjString* interned = table_find_string(&vm->strings, src, length, hash);
    if (interned != NULL)
        return interned;

    char* chars = ALLOCATE_ARRAY(vm, char, length + 1);
    memcpy(chars, src, length);
    chars[length] = '\0';

    return objstring_new(vm, chars, length, hash);
}

ObjString*
objstring_concat(VM* vm, ObjString* a, ObjString* b)
{
    size_t length = a->length + b->length;
    char* chars = ALLOCATE_ARRAY(vm, char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    uint32_t hash = hash_string(chars, length);

    ObjString* interned = table_find_string(&vm->strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(vm, chars, char, length + 1);
        return interned;
    }

    return objstring_new(vm, chars, length, hash);
}

// ObjFunction
// ===========

ObjFunction*
objfunction_new(VM* vm)
{
    ObjFunction* fn = ALLOCATE_OBJECT(vm, OBJ_FUNCTION, ObjFunction);
    fn->arity = 0;
    fn->upvalue_count = 0;
    chunk_init(&fn->chunk);
    return fn;
}

static void
objfunction_free(VM* vm, Obj* obj)
{
    ObjFunction* fn = (ObjFunction*)obj;
    chunk_free(&fn->chunk, vm);
    FREE(vm, ObjFunction, fn);
}

// ObjUpvalue
// ==========

ObjUpvalue*
objupvalue_new(VM* vm, Value* slot)
{
    ObjUpvalue* upvalue = ALLOCATE_OBJECT(vm, OBJ_UPVALUE, ObjUpvalue);
    upvalue->location = slot;
    upvalue->closed = NIL_VAL;
    upvalue->next = NULL;
    return upvalue;
}

static void
objupvalue_free(VM* vm, Obj* obj)
{
    ObjUpvalue* upvalue = (ObjUpvalue*)obj;
    FREE(vm, ObjUpvalue, upvalue);
}

// ObjClosure
// ==========

ObjClosure*
objclosure_new(VM* vm, ObjFunction* fn)
{
    ObjUpvalue** upvalues = ALLOCATE_ARRAY(vm, ObjUpvalue*, fn->upvalue_count);
    for (int i = 0; i < fn->upvalue_count; i++)
        upvalues[i] = NULL;

    ObjClosure* closure = ALLOCATE_OBJECT(vm, OBJ_CLOSURE, ObjClosure);
    closure->function = fn;
    closure->upvalues = upvalues;
    closure->upvalue_count = fn->upvalue_count;
    return closure;
}

static void
objclosure_free(VM* vm, Obj* obj)
{
    ObjClosure* closure = (ObjClosure*)obj;
    FREE_ARRAY(vm, closure->upvalues, ObjUpvalue*, closure->upvalue_count);
    FREE(vm, ObjClosure, closure);
}

// ObjObject
// =========

ObjObject*
objobject_new(VM* vm)
{
    ObjObject* object = ALLOCATE_OBJECT(vm, OBJ_OBJECT, ObjObject);
    object->proto = NIL_VAL;
    table_init(&object->slots);
    return object;
}

bool
objobject_has(ObjObject* obj, Value key)
{
    Value res;
    return objobject_get(obj, key, &res);
}

bool
objobject_get(ObjObject* obj, Value key, Value* result)
{
    return table_get(&obj->slots, key, result);
}

void
objobject_set(ObjObject* obj, VM* vm, Value key, Value value)
{
    table_set(&obj->slots, vm, key, value);
}

bool
objobject_delete(ObjObject* obj, VM* vm, Value key)
{
    return table_delete(&obj->slots, vm, key);
}

static void
objobject_free(VM* vm, Obj* obj)
{
    ObjObject* object = (ObjObject*)obj;
    table_free(&object->slots, vm);
    FREE(vm, ObjObject, object);
}

// ObjNative
// =========

ObjNative*
objnative_new(VM* vm, NativeFn fn)
{
    ObjNative* native = ALLOCATE_OBJECT(vm, OBJ_NATIVE, ObjNative);
    native->fn = fn;
    return native;
}

static void
objnative_free(VM* vm, Obj* obj)
{
    FREE(vm, ObjNative, obj);
}

// ObjFiber
// ========

ObjFiber*
objfiber_new(VM* vm, ObjClosure* closure)
{
    // Allocate arrays first in case of GC
    size_t stack_capacity = 8;
    Value* stack = ALLOCATE_ARRAY(vm, Value, stack_capacity);

    size_t frames_capacity = 4;
    CallFrame* frames = ALLOCATE_ARRAY(vm, CallFrame, frames_capacity);

    ObjFiber* fiber = ALLOCATE_OBJECT(vm, OBJ_FIBER, ObjFiber);
    fiber->open_upvalues = NULL;
    fiber->error = NIL_VAL;
    fiber->stack = stack;
    fiber->stack_top = stack;
    fiber->stack_capacity = stack_capacity;
    fiber->frames = frames;
    fiber->frames_count = 0;
    fiber->frames_capacity = frames_capacity;

    fiber->stack[0] = OBJ_TO_VAL(closure);
    fiber->stack_top++;
    objfiber_push_frame(fiber, vm, closure, fiber->stack_top - 1);

    return fiber;
}

static size_t
next_power_of_two(size_t n)
{
    size_t m = 1;
    while (m < n)
        m *= 2;
    return m;
}

void
objfiber_ensure_stack(ObjFiber* fiber, VM* vm, size_t sz)
{
    size_t stack_count = fiber->stack_top - fiber->stack;
    if (stack_count + sz <= fiber->stack_capacity)
        return;

    size_t new_capacity = next_power_of_two(stack_count + sz);
    Value* new_stack = GROW_ARRAY(vm, fiber->stack, Value,
                                  fiber->stack_capacity,
                                  new_capacity);

    // If the stack has moved, then we must also move pointers
    // referencing values on the stack.
    if (new_stack != fiber->stack) {
        // Callframes
        for (size_t i = 0; i < fiber->frames_capacity; i++) {
            CallFrame* frame = &fiber->frames[i];
            frame->slots = new_stack + (frame->slots - fiber->stack);
        }

        // Upvalues
        for (ObjUpvalue* upvalue = fiber->open_upvalues;
             upvalue != NULL;
             upvalue = upvalue->next) {
            upvalue->location = new_stack + (upvalue->location - fiber->stack);
        }
    }

    fiber->stack = new_stack;
    fiber->stack_top = new_stack + stack_count;
    fiber->stack_capacity = new_capacity;
}

CallFrame*
objfiber_push_frame(ObjFiber* fiber, VM* vm,
                    ObjClosure* closure, Value* stack_start)
{
    if (fiber->frames_count + 1 >= fiber->frames_capacity) {
        size_t new_capacity = GROW_CAPACITY(fiber->frames_capacity);
        fiber->frames = GROW_ARRAY(vm, fiber->frames, CallFrame, fiber->frames_capacity, new_capacity);
        fiber->frames_capacity = new_capacity;
    }
    CallFrame* frame = &fiber->frames[fiber->frames_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = stack_start;
    return frame;
}

static void
objfiber_free(VM* vm, Obj* obj)
{
    ObjFiber* fiber = (ObjFiber*)obj;
    FREE_ARRAY(vm, fiber->stack, Value, fiber->stack_capacity);
    FREE_ARRAY(vm, fiber->frames, CallFrame, fiber->frames_capacity);
    FREE(vm, ObjFiber, obj);
}
