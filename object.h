#ifndef SUBTLE_OBJECT_H
#define SUBTLE_OBJECT_H

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

// Forward declarations
// --------------------
typedef struct VM VM;

// Macros
// ------
#define IS_STRING(value)      (is_object_type(value, OBJ_STRING))
#define IS_FN(value)          (is_object_type(value, OBJ_FN))
#define IS_UPVALUE(value)     (is_object_type(value, OBJ_UPVALUE))
#define IS_CLOSURE(value)     (is_object_type(value, OBJ_CLOSURE))
#define IS_OBJECT(value)      (is_object_type(value, OBJ_OBJECT))
#define IS_NATIVE(value)      (is_object_type(value, OBJ_NATIVE))
#define IS_FIBER(value)       (is_object_type(value, OBJ_FIBER))
#define IS_RANGE(value)       (is_object_type(value, OBJ_RANGE))
#define IS_LIST(value)        (is_object_type(value, OBJ_LIST))
#define IS_MAP(value)         (is_object_type(value, OBJ_MAP))
#define IS_MSG(value)         (is_object_type(value, OBJ_MSG))
#define IS_FOREIGN(value)     (is_object_type(value, OBJ_FOREIGN))

#define VAL_TO_STRING(value)  ((ObjString*)VAL_TO_OBJ(value))
#define VAL_TO_FN(value)      ((ObjFn*)VAL_TO_OBJ(value))
#define VAL_TO_UPVALUE(value) ((ObjUpvalue*)VAL_TO_OBJ(value))
#define VAL_TO_CLOSURE(value) ((ObjClosure*)VAL_TO_OBJ(value))
#define VAL_TO_OBJECT(value)  ((ObjObject*)VAL_TO_OBJ(value))
#define VAL_TO_NATIVE(value)  ((ObjNative*)VAL_TO_OBJ(value))
#define VAL_TO_FIBER(value)   ((ObjFiber*)VAL_TO_OBJ(value))
#define VAL_TO_RANGE(value)   ((ObjRange*)VAL_TO_OBJ(value))
#define VAL_TO_LIST(value)    ((ObjList*)VAL_TO_OBJ(value))
#define VAL_TO_MAP(value)     ((ObjMap*)VAL_TO_OBJ(value))
#define VAL_TO_MSG(value)     ((ObjMsg*)VAL_TO_OBJ(value))
#define VAL_TO_FOREIGN(value) ((ObjForeign*)VAL_TO_OBJ(value))

typedef enum {
    OBJ_STRING,
    OBJ_FN,
    OBJ_UPVALUE,
    OBJ_CLOSURE,
    OBJ_OBJECT,
    OBJ_NATIVE,
    OBJ_FIBER,
    OBJ_RANGE,
    OBJ_LIST,
    OBJ_MAP,
    OBJ_MSG,
    OBJ_FOREIGN,
} ObjType;

typedef struct Obj {
    ObjType type;
    bool visited;     // Have we already visited this object?
    bool marked;      // Does this object have a live reference?
    struct Obj* next; // Link to the next allocated object.
} Obj;

static inline bool
is_object_type(Value value, ObjType type)
{
    return IS_OBJ(value) && VAL_TO_OBJ(value)->type == type;
}

typedef struct ObjString {
    Obj obj;
    char* chars; // NUL-terminated string.
    uint32_t length;
    uint32_t hash;
} ObjString;

typedef struct ObjFn {
    Obj obj;
    int max_slots; // Max slots required by this function.
    int8_t arity;  // Arguments required by the function. -1 if it's a script.
    uint8_t upvalue_count;
    Chunk chunk;
    ObjString* name;
} ObjFn;

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
    ObjFn* fn;
    ObjUpvalue** upvalues;
    uint8_t upvalue_count;
} ObjClosure;

typedef struct {
    Obj obj;
    Value* protos;
    uint32_t protos_count;
    Table slots;
} ObjObject;

typedef bool (*NativeFn)(VM* vm, void* ctx, Value* args, int num_args);

typedef struct {
    Obj obj;
    void* ctx; // this should be owned by an ExtContext
    NativeFn fn;
} ObjNative;

typedef struct {
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;
} CallFrame;

typedef enum {
    // VM's root fiber -- this fiber cannot be switched to.
    // Internally we pretend that fibers with FIBER_ROOT have
    // a parent fiber, even though they don't.
    FIBER_ROOT,
    // This Fiber was ran with a .try(), indicating that the parent
    // fiber will handle the error.
    FIBER_TRY,
    FIBER_OTHER,
} FiberState;

typedef struct ObjFiber {
    Obj obj;

    FiberState state;

    Value* stack;
    Value* stack_top;
    int stack_capacity;

    CallFrame* frames;
    int frames_count;
    int frames_capacity;

    struct ObjFiber* parent;
    ObjUpvalue* open_upvalues;
    ObjString* error;
} ObjFiber;

typedef struct ObjRange {
    Obj obj;
    double start;
    double end;
    bool inclusive;
} ObjRange;

typedef struct ObjList {
    Obj obj;
    Value* values;
    uint32_t size;
    uint32_t capacity;
} ObjList;

typedef struct ObjMap {
    Obj obj;
    Table tbl;
} ObjMap;

// ObjMsg represents a (mutable) "call", for example
// a.b(c,d,e) <-> ObjMsg{slot_name=b, args=[c,d,e]}
typedef struct {
    Obj obj;
    ObjString* slot_name;
    ObjList* args;
} ObjMsg;

typedef void (*GCFn)(VM* vm, void* p);

typedef struct {
    Obj obj;
    uid_t uid;   // Type tag (this combined with p is the minimal object).
    void* p;     // Pointer to externally-managed data.
    Value proto; // Prototype.
    GCFn gc;     // Function called when the Foreign is GC'd.
} ObjForeign;

// Object memory management
// ========================

Obj* object_allocate(VM* vm, ObjType type, size_t sz);
void object_free(Obj* obj, VM* vm);

#define ALLOCATE_OBJECT(vm, obj_type, type) \
    (type*)object_allocate(vm, obj_type, sizeof(type))

// ObjString
// =========

// Takes ownership of a NUL-terminated string `chars`.
// This assumes the memory was _not_ allocated via memory_realloc.
// If `chars` happens to be interned, `free(chars)` is called.
ObjString* objstring_take(VM* vm, char* chars, size_t length);
ObjString* objstring_copy(VM* vm, const char* chars, size_t length);
ObjString* objstring_concat(VM* vm, ObjString* a, ObjString* b);

// ObjFn
// =====

ObjFn* objfn_new(VM* vm);

// ObjUpvalue
// ==========

ObjUpvalue* objupvalue_new(VM* vm, Value* slot);

// ObjClosure
// ==========

ObjClosure* objclosure_new(VM* vm, ObjFn* fn);

// ObjObject
// =========

ObjObject* objobject_new(VM* vm);
void objobject_set_proto(ObjObject* obj, VM* vm, Value proto);
void objobject_insert_proto(ObjObject* obj, VM* vm, uint32_t idx, Value proto);
void objobject_delete_proto(ObjObject* obj, VM* vm, Value proto);
void objobject_copy_protos(ObjObject* obj, VM* vm, Value* protos, uint32_t length);
bool objobject_has(ObjObject* obj, Value key);
bool objobject_get(ObjObject* obj, Value key, Value* result);
void objobject_set(ObjObject* obj, VM* vm, Value key, Value value);
bool objobject_delete(ObjObject* obj, VM* vm, Value key);

// ObjNative
// =========

ObjNative* objnative_new(VM* vm, NativeFn fn);
ObjNative* objnative_new_with_context(VM* vm, NativeFn fn, void* ctx);

// ObjFiber
// ========

ObjFiber* objfiber_new(VM* vm, ObjClosure* closure);
void objfiber_ensure_stack(ObjFiber* fiber, VM* vm, int n);
CallFrame* objfiber_push_frame(ObjFiber* fiber, VM* vm,
                               ObjClosure* closure, Value* stack_start);
bool objfiber_is_done(ObjFiber* fiber);

// ObjRange
// ========

ObjRange* objrange_new(VM* vm, double start, double end, bool inclusive);

// ObjList
// =======

ObjList* objlist_new(VM* vm, uint32_t size);
Value objlist_get(ObjList* list, uint32_t idx);
void objlist_set(ObjList* list, uint32_t idx, Value v);
void objlist_del(ObjList* list, VM* vm, uint32_t idx);
void objlist_insert(ObjList* list, VM* vm, uint32_t idx, Value v);

// ObjMap
// ======

ObjMap* objmap_new(VM* vm);
bool objmap_has(ObjMap* map, Value key);
bool objmap_get(ObjMap* map, Value key, Value* value);
bool objmap_set(ObjMap* map, VM* vm, Value key, Value value);
bool objmap_delete(ObjMap* map, VM* vm, Value key);

// ObjMsg
// ======

ObjMsg* objmsg_new(VM* vm, ObjString* slot_name, Value* args, uint32_t num_args);
ObjMsg* objmsg_from_list(VM* vm, ObjString* slot_name, ObjList* list);

// ObjForeign
// ==========

ObjForeign* objforeign_new(VM* vm, uid_t uid, void* p, Value proto, GCFn gc);
bool value_has_uid(Value v, uid_t uid);

#endif
