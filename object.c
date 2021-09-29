#include "object.h"
#include "memory.h"
#include "vm.h"
#include "table.h"

#include <stdint.h>
#include <string.h> // memcpy
#ifdef SUBTLE_DEBUG_TRACE_ALLOC
#include <stdio.h>
#endif

void valuearray_init(ValueArray* va) {
    va->values = NULL;
    va->length = 0;
    va->capacity = 0;
}

void valuearray_free(ValueArray* va, VM* vm) {
    FREE_ARRAY(vm, va->values, Value, va->capacity);
    valuearray_init(va);
}

void valuearray_write(ValueArray* va, VM* vm, Value v) {
    if (va->length + 1 > va->capacity) {
        size_t new_size = GROW_CAPACITY(va->capacity);
        va->values = GROW_ARRAY(vm, va->values, Value, va->capacity, new_size);
        va->capacity = new_size;
    }

    va->values[va->length] = v;
    va->length++;
}

// Hashing
// =======

static inline uint32_t hash_bits(uint64_t hash) {
    hash = ~hash + (hash << 18);
    hash = hash ^ (hash >> 31);
    hash = hash * 21;
    hash = hash ^ (hash >> 11);
    hash = hash + (hash << 6);
    hash = hash ^ (hash >> 22);
    return (uint32_t)(hash & 0x3fffffff);
}

typedef union {
    double   num;
    uint64_t bits64;
} double_bits;

uint64_t double_to_bits(double d) {
    double_bits data;
    data.num = d;
    return data.bits64;
}

static uint32_t object_hash(Obj* obj)
{
    switch (obj->type) {
        case OBJ_STRING:   return ((ObjString*)obj)->hash;
        case OBJ_FUNCTION: return hash_bits((uintptr_t)obj);
        case OBJ_CLOSURE:  return object_hash((Obj*) ((ObjClosure*)obj)->function);
        case OBJ_UPVALUE:  UNREACHABLE();
    }
}

uint32_t value_hash(Value v) {
    switch (v.type) {
        case VALUE_NIL:    return 0;
        case VALUE_BOOL:   return VAL_TO_BOOL(v) ? 1 : 2;
        case VALUE_NUMBER: return hash_bits(VAL_TO_NUMBER(v));
        case VALUE_OBJ:    return object_hash(VAL_TO_OBJ(v));
        default: UNREACHABLE();
    }
}

bool value_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VALUE_NIL:    return true;
        case VALUE_BOOL:   return VAL_TO_BOOL(a) == VAL_TO_BOOL(b);
        case VALUE_NUMBER: return VAL_TO_NUMBER(a) == VAL_TO_NUMBER(b);
        case VALUE_OBJ: return VAL_TO_OBJ(a) == VAL_TO_OBJ(b);
        default: UNREACHABLE();
    }
}

bool value_truthy(Value a) {
    if (IS_NIL(a)) return false;
    if (IS_BOOL(a)) return VAL_TO_BOOL(a);
    return true;
}

// Object memory management
// ========================

Obj* object_allocate(VM* vm, ObjType type, size_t sz) {
#ifdef SUBTLE_DEBUG_TRACE_ALLOC
    printf("allocate %zu for type %d\n", sz, type);
#endif
    Obj* object = memory_realloc(vm, NULL, 0, sz);
    object->type = type;
    object->next = vm->objects;
    vm->objects = object;
    return object;
}

static void objstring_free(VM*, Obj*);
static void objfunction_free(VM*, Obj*);
static void objupvalue_free(VM*, Obj*);
static void objclosure_free(VM*, Obj*);

void object_free(Obj* obj, VM* vm) {
    switch (obj->type) {
        case OBJ_STRING:   objstring_free(vm,   obj); break;
        case OBJ_FUNCTION: objfunction_free(vm, obj); break;
        case OBJ_UPVALUE:  objupvalue_free(vm,  obj); break;
        case OBJ_CLOSURE:  objclosure_free(vm,  obj); break;
    }
}

// ObjString
// =========

static uint32_t hash_string(const char* str, size_t length) {
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
    table_set(&vm->strings, vm, OBJ_TO_VAL(str), NIL_VAL);
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
    memcpy(chars,             a->chars, a->length);
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
