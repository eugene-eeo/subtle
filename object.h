#ifndef SUBTLE_OBJECT_H
#define SUBTLE_OBJECT_H

#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

// Forward declarations
// --------------------
typedef struct VM VM;

// Macros
// ------
#define IS_STRING(value)     (is_object_type(value, OBJ_STRING))
#define IS_FUNCTION(value)   (is_object_type(value, OBJ_FUNCTION))
#define IS_UPVALUE(value)    (is_object_type(value, OBJ_UPVALUE))
#define IS_CLOSURE(value)    (is_object_type(value, OBJ_CLOSURE))
#define IS_OBJECT(value)     (is_object_type(value, OBJ_OBJECT))
#define IS_NATIVE(value)     (is_object_type(value, OBJ_NATIVE))

#define OBJ_TYPE(value)      (VAL_TO_OBJ(value)->type)

#define VAL_TO_STRING(value)   ((ObjString*)VAL_TO_OBJ(value))
#define VAL_TO_FUNCTION(value) ((ObjFunction*)VAL_TO_OBJ(value))
#define VAL_TO_UPVALUE(value)  ((ObjUpvalue*)VAL_TO_OBJ(value))
#define VAL_TO_CLOSURE(value)  ((ObjClosure*)VAL_TO_OBJ(value))
#define VAL_TO_OBJECT(value)   ((ObjObject*)VAL_TO_OBJ(value))
#define VAL_TO_NATIVE(value)   ((ObjNative*)VAL_TO_OBJ(value))

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_UPVALUE,
    OBJ_CLOSURE,
    OBJ_OBJECT,
    OBJ_NATIVE,
} ObjType;

typedef struct Obj {
    ObjType type;
    // Does this object have a live reference?
    bool marked;
    // Link to the next allocated object.
    struct Obj* next;
} Obj;

static inline bool is_object_type(Value value, ObjType type) {
    return IS_OBJ(value) && VAL_TO_OBJ(value)->type == type;
}

typedef struct ObjString {
    Obj obj;
    size_t length;
    char*  chars;
    uint32_t hash;
} ObjString;

typedef struct {
    Obj obj;
    // If the arity is -1, then this is a script.
    int arity;
    int upvalue_count;
    Chunk chunk;
} ObjFunction;

typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    // This is where a closed-over value lives on the heap.
    // An upvalue is _closed_ by having its ->location point
    // to its ->closed.
    Value closed;
    // Pointer to the next upvalue.
    // Upvalues are stored in a linked-list in stack order.
    struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalue_count;
} ObjClosure;

typedef struct {
    Obj obj;
    Value proto;
    Table slots;
} ObjObject;

typedef bool (*NativeFn)(VM* vm, Value* args, int num_args);

typedef struct {
    Obj obj;
    NativeFn fn;
} ObjNative;

// Object memory management
// ========================

Obj* object_allocate(VM* vm, ObjType type, size_t sz);
void object_free(Obj* obj, VM* vm);

#define ALLOCATE_OBJECT(vm, obj_type, type) \
    (type*)object_allocate(vm, obj_type, sizeof(type))

// ObjString
// =========

ObjString* objstring_copy(VM* vm, const char* chars, size_t length);
ObjString* objstring_concat(VM* vm, ObjString* a, ObjString* b);

// ObjFunction
// ===========

ObjFunction* objfunction_new(VM* vm);

// ObjUpvalue
// ==========

ObjUpvalue* objupvalue_new(VM* vm, Value* slot);

// ObjClosure
// ==========

ObjClosure* objclosure_new(VM* vm, ObjFunction* fn);

// ObjObject
// =========

ObjObject* objobject_new(VM* vm);
bool objobject_has(ObjObject* obj, Value key);
bool objobject_get(ObjObject* obj, Value key, Value* result);
void objobject_set(ObjObject* obj, VM* vm, Value key, Value value);

// ObjNative
// =========

ObjNative* objnative_new(VM* vm, NativeFn fn);

#endif
