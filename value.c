#include "value.h"
#include "memory.h"

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

void valuearray_mark(ValueArray* va, VM* vm)
{
    for (size_t i = 0; i < va->length; i++)
        mark_value(vm, va->values[i]);
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

static uint64_t double_to_bits(double d) {
    double_bits data;
    data.num = d;
    return data.bits64;
}

static uint32_t object_hash(Obj* obj)
{
    switch (obj->type) {
        case OBJ_STRING:
            return ((ObjString*)obj)->hash;
        case OBJ_CLOSURE:
        case OBJ_FUNCTION:
        case OBJ_OBJECT:
        case OBJ_NATIVE:
        case OBJ_FIBER:
        case OBJ_RANGE:
        case OBJ_LIST:
            return hash_bits((uintptr_t)obj);
        default:
            UNREACHABLE();
    }
}

uint32_t value_hash(Value v) {
    switch (v.type) {
        case VALUE_NIL:    return 0xa3b1799d;
        case VALUE_TRUE:   return 0x46685257;
        case VALUE_FALSE:  return 0x392456de;
        case VALUE_NUMBER: return hash_bits(double_to_bits(VAL_TO_NUMBER(v)));
        case VALUE_OBJ:    return object_hash(VAL_TO_OBJ(v));
        default: UNREACHABLE();
    }
}

bool value_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VALUE_NIL:
        case VALUE_TRUE:
        case VALUE_FALSE:
            return true;
        case VALUE_NUMBER: return VAL_TO_NUMBER(a) == VAL_TO_NUMBER(b);
        case VALUE_OBJ:    return VAL_TO_OBJ(a) == VAL_TO_OBJ(b);
        default: UNREACHABLE();
    }
}

bool value_truthy(Value a) {
    if (IS_NIL(a) || IS_FALSE(a)) return false;
    return true;
}
