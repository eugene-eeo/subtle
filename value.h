#ifndef SUBTLE_VALUE_H
#define SUBTLE_VALUE_H

#include "common.h"

// Forward declarations
// --------------------
typedef struct VM VM;
typedef struct Obj Obj;

// Macros
// ------
#define IS_UNDEFINED(value)  ((value).type == VALUE_UNDEFINED)
#define IS_NIL(value)        ((value).type == VALUE_NIL)
#define IS_TRUE(value)       ((value).type == VALUE_TRUE)
#define IS_FALSE(value)      ((value).type == VALUE_FALSE)
#define IS_NUMBER(value)     ((value).type == VALUE_NUMBER)
#define IS_OBJ(value)        ((value).type == VALUE_OBJ)

#define VAL_TO_BOOL(value)   ((value).type == VALUE_TRUE ? true : false)
#define VAL_TO_NUMBER(value) ((value).as.number)
#define VAL_TO_OBJ(value)    ((value).as.obj)

#define UNDEFINED_VAL    ((Value){VALUE_UNDEFINED, {.number = 0}})
#define NIL_VAL          ((Value){VALUE_NIL,       {.number = 0}})
#define TRUE_VAL         ((Value){VALUE_TRUE,      {.number = 0}})
#define FALSE_VAL        ((Value){VALUE_FALSE,     {.number = 0}})
#define BOOL_TO_VAL(b)   ((b) ? TRUE_VAL : FALSE_VAL)
#define NUMBER_TO_VAL(n) ((Value){VALUE_NUMBER,    {.number = n}})
#define OBJ_TO_VAL(p)    ((Value){VALUE_OBJ,       {.obj = (Obj*)p}})

// There are two kinds of `objects`, those that live on the stack
// (Values), and those that live on the heap (Objects) -- these are
// pointed to by Values.

typedef enum {
    VALUE_UNDEFINED,
    VALUE_NIL,
    VALUE_TRUE,
    VALUE_FALSE,
    VALUE_NUMBER,
    VALUE_OBJ,
} ValueType;

typedef struct {
    ValueType type;
    union {
        double number;
        Obj* obj;
    } as;
} Value;

typedef struct {
    Value* values;
    size_t length;
    size_t capacity;
} ValueArray;

void valuearray_init(ValueArray* va);
void valuearray_free(ValueArray* va, VM* vm);
void valuearray_write(ValueArray* va, VM* vm, Value v);
void valuearray_mark(ValueArray* va, VM* vm);

uint32_t value_hash(Value v);
bool value_equal(Value a, Value b);
bool value_truthy(Value a);

#endif
