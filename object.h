#ifndef SUBTLE_VALUE_H
#define SUBTLE_VALUE_H

#include "common.h"

// There are two kinds of `objects`, those that live on the stack
// (Values), and those that live on the heap -- these are pointed
// to by Values.

typedef enum {
    VALUE_NIL,
    VALUE_BOOL,
    VALUE_NUMBER,
    VALUE_OBJECT,
    VALUE_UNDEFINED, // hash table tombstone.
} ValueType;

typedef enum {
    OBJECT_STRING,
} ObjectType;

typedef struct Object {
    ObjectType type;
    // Link to the next allocated object.
    struct Object* next;
} Object;

typedef struct {
    Object obj; // header
    size_t length;
    char*  chars;
    uint32_t hash;
} ObjString;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Object* object;
    } as;
} Value;

#define IS_NIL(value)        ((value).type == VALUE_NIL)
#define IS_BOOL(value)       ((value).type == VALUE_BOOL)
#define IS_NUMBER(value)     ((value).type == VALUE_NUMBER)
#define IS_OBJECT(value)     ((value).type == VALUE_OBJECT)
#define IS_STRING(value)     (is_object_type(value, OBJECT_STRING))
#define IS_UNDEFINED(value)  ((value).type == VALUE_UNDEFINED)

#define OBJECT_TYPE(value) (VAL_TO_OBJECT(value)->type)

#define VAL_TO_BOOL(value)   ((value).as.boolean)
#define VAL_TO_NUMBER(value) ((value).as.number)
#define VAL_TO_OBJECT(value) ((value).as.object)
#define VAL_TO_STRING(value) ((ObjString*)VAL_TO_OBJECT(value))

#define UNDEFINED_VAL    ((Value){VALUE_UNDEFINED, {.number = 0}})
#define NIL_VAL          ((Value){VALUE_NIL,       {.number = 0}})
#define BOOL_TO_VAL(b)   ((Value){VALUE_BOOL,      {.boolean = b}})
#define NUMBER_TO_VAL(n) ((Value){VALUE_NUMBER,    {.number = n}})
#define OBJECT_TO_VAL(p) ((Value){VALUE_OBJECT,    {.object = (Object*)p}})

static inline bool is_object_type(Value value, ObjectType type) {
    return IS_OBJECT(value) && VAL_TO_OBJECT(value)->type == type;
}

typedef struct {
    Value* values;
    size_t length;
    size_t capacity;
} ValueArray;

void valuearray_init(ValueArray* va);
void valuearray_free(ValueArray* va);
void valuearray_write(ValueArray* va, Value v);

uint32_t value_hash(Value v);
bool value_equal(Value a, Value b);
bool value_truthy(Value a);

// We need the VM struct here, but vm.h needs object.h as well.
typedef struct VM VM;

// Object memory management
// ========================

Object* object_allocate(VM* vm, ObjectType type, size_t sz);
void object_free(Object* obj);

#define ALLOCATE_OBJECT(vm, obj_type, type) \
    (type*)object_allocate(vm, obj_type, sizeof(type))

// ObjString
// =========

ObjString* objstring_copy(VM* vm, const char* chars, size_t length);
ObjString* objstring_concat(VM* vm, ObjString* a, ObjString* b);

#endif
