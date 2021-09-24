#ifndef SUBTLE_OBJECT_H
#define SUBTLE_OBJECT_H

#include "common.h"
#include "value.h"
#include "chunk.h"

// Forward declarations
// --------------------
typedef struct VM VM;

// Macros
// ------
#define IS_STRING(value)     (is_object_type(value, OBJECT_STRING))
#define IS_FUNCTION(value)   (is_object_type(value, OBJECT_FUNCTION))

#define OBJ_TYPE(value)      (VAL_TO_OBJ(value)->type)

#define VAL_TO_STRING(value)   ((ObjString*)VAL_TO_OBJ(value))
#define VAL_TO_FUNCTION(value) ((ObjFunction*)VAL_TO_OBJ(value))

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
} ObjType;

typedef struct Object {
    ObjType type;
    // Link to the next allocated object.
    struct Object* next;
} Obj;

typedef struct ObjString {
    Obj obj; // header
    size_t length;
    char*  chars;
    uint32_t hash;
} ObjString;

typedef struct {
    Obj obj;
    int arity;
    Chunk chunk;
} ObjFunction;

static inline bool is_object_type(Value value, ObjType type) {
    return IS_OBJ(value) && VAL_TO_OBJ(value)->type == type;
}

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

#endif
