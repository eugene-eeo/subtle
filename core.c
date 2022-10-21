#include "core.h"

#include "core.subtle.inc"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static inline void
define_on_table(VM* vm, Table* table, const char* name, Value value) {
    vm_push_root(vm, value);
    Value key = OBJ_TO_VAL(objstring_copy(vm, name, strlen(name)));
    vm_push_root(vm, key);

    table_set(table, vm, key, value);
    vm_pop_root(vm);
    vm_pop_root(vm);
}

#define DEFINE_NATIVE(name) \
    static bool name(VM* vm, Value* args, int num_args)

#define ARG_ERROR(arg_idx, msg) \
    do { \
        if ((arg_idx) == 0) \
            ERROR("%s expected 'this' to be %s.", __func__, msg); \
        else \
            ERROR("%s expected arg %d to be %s.", __func__, (arg_idx)-1, msg); \
    } while (false)

#define ARGSPEC(spec) do { \
        for (int i=0; i < strlen(spec); i++) \
            ARG_CHECK_SINGLE(vm, (spec)[i], i); \
    } while(false)

#define ARG_CHECK_SINGLE(vm, ch, idx) do { \
    if (num_args < (idx)) \
        ERROR("%s expected %d args, got %d instead.", __func__, idx, num_args); \
    Value arg = args[idx]; \
    switch (ch) { \
        case 'O': if (!IS_OBJECT(arg))  ARG_ERROR(idx, "an Object"); break; \
        case 'S': if (!IS_STRING(arg))  ARG_ERROR(idx, "a String"); break; \
        case 'N': if (!IS_NUMBER(arg))  ARG_ERROR(idx, "a Number"); break; \
        case 'n': if (!IS_NATIVE(arg))  ARG_ERROR(idx, "a Native"); break; \
        case 'F': if (!IS_CLOSURE(arg)) ARG_ERROR(idx, "an Fn"); break; \
        case 'f': if (!IS_FIBER(arg))   ARG_ERROR(idx, "a Fiber"); break; \
        case 'r': if (!IS_RANGE(arg))   ARG_ERROR(idx, "a Range"); break; \
        case 'L': if (!IS_LIST(arg))    ARG_ERROR(idx, "a List"); break; \
        case 'M': if (!IS_MAP(arg))     ARG_ERROR(idx, "a Map"); break; \
        case 'm': if (!IS_MESSAGE(arg)) ARG_ERROR(idx, "a Message"); break; \
        case '*': break; \
        default: UNREACHABLE(); \
    } \
    } while(false)

#define ERROR(...) \
    do { \
        vm_runtime_error(vm, __VA_ARGS__); \
        return false; \
    } while (false)

#define RETURN(expr) \
    do { \
        *(vm->fiber->stack_top - num_args - 1) = expr; \
        vm_drop(vm, num_args); \
        return true; \
    } while (false)

#define CONST_STRING(vm, s) objstring_copy(vm, (s), strlen(s))

static bool
value_to_index(Value num, uint32_t length, uint32_t* idx)
{
    ASSERT(IS_NUMBER(num), "!IS_NUMBER(num)");
    int32_t i = (int32_t) VAL_TO_NUMBER(num);
    if (VAL_TO_NUMBER(num) != i) return false;
    if (i < 0) i += length;
    if (i < 0 || i >= length)
        return false;
    *idx = (uint32_t)i;
    return true;
}

static bool
init_index(Value arg, uint32_t length, int32_t* rv)
{
    int32_t idx = -1;
    if (IS_NIL(arg)) {
        idx = 0;
    } else if (IS_NUMBER(arg)) {
        idx = (int32_t) VAL_TO_NUMBER(arg);
        if (VAL_TO_NUMBER(arg) != idx) return false;
        idx++;
    }
    if (idx < 0 || idx >= length)
        return false;
    *rv = idx;
    return true;
}

// A generic implementation of iterMore for sized sequences.
static Value
generic_iterMore(Value arg, uint32_t length)
{
    int32_t idx;
    if (!init_index(arg, length, &idx))
        return FALSE_VAL;
    return NUMBER_TO_VAL(idx);
}

// generic implementation to check if the table still has any
// valid entries after entry i.
static Value
generic_tableIterMore(Table* table, Value value)
{
    int32_t idx;
    if (!init_index(value, table->capacity, &idx))
        return FALSE_VAL;
    // Find a valid entry that is >= the given index.
    for (; idx < table->capacity; idx++)
        if (!IS_UNDEFINED(table->entries[idx].key))
            return NUMBER_TO_VAL(idx);
    return FALSE_VAL;
}

// generic implementation to get the i-th entry (if it's not deleted)
// from a table.
static bool
generic_tableIterEntry(Table* table, Value value, Entry* entry)
{
    uint32_t idx;
    if (!value_to_index(value, table->capacity, &idx))
        return false;
    if (IS_UNDEFINED(table->entries[idx].key))
        return false;
    *entry = table->entries[idx];
    return true;
}

// ============================= Object =============================

DEFINE_NATIVE(Object_proto) {
    Value proto = vm_get_prototype(vm, args[0]);
    RETURN(proto);
}

DEFINE_NATIVE(Object_setProto) {
    ARGSPEC("O*");
    ObjObject* object = VAL_TO_OBJECT(args[0]);
    object->proto = args[1];
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_hash) {
    RETURN(NUMBER_TO_VAL(value_hash(args[0])));
}

DEFINE_NATIVE(Object_getSlot) {
    ARGSPEC("**");
    Value slot;
    if (!vm_get_slot(vm, args[0], args[1], &slot))
        slot = (num_args > 1) ? args[2] : NIL_VAL;
    RETURN(slot);
}

DEFINE_NATIVE(Object_setSlot) {
    ARGSPEC("O**");
    objobject_set(VAL_TO_OBJECT(args[0]), vm, args[1], args[2]);
    RETURN(args[2]);
}

DEFINE_NATIVE(Object_hasSlot) {
    ARGSPEC("**");
    Value slot;
    bool has_slot = vm_get_slot(vm, args[0], args[1], &slot);
    RETURN(BOOL_TO_VAL(has_slot));
}

DEFINE_NATIVE(Object_perform) {
    ARGSPEC("*m");
    Value slot;
    Value self = args[0];
    ObjMessage* msg = VAL_TO_MESSAGE(args[1]);

    // search on protos.
    if (vm_get_slot(vm, self, OBJ_TO_VAL(msg->slot_name), &slot)) {
        // found it -- copy args onto stack.
        vm_push_root(vm, args[1]);
        vm_drop(vm, num_args); // pop all args, incl. msg
        vm_ensure_stack(vm, msg->args->size);
        vm_pop_root(vm); // msg
        for (uint32_t i = 0; i < msg->args->size; i++)
            vm_push(vm, msg->args->values[i]);
        num_args = msg->args->size;
    } else {
        slot = NIL_VAL;
        num_args = msg->args->size;
    }

    return vm_complete_call(vm, slot, num_args);
}

DEFINE_NATIVE(Object_getOwnSlot) {
    ARGSPEC("**");
    Value rv;
    if (!IS_OBJECT(args[0]) || !objobject_get(VAL_TO_OBJECT(args[0]), args[1], &rv))
        rv = (num_args > 1) ? args[2] : NIL_VAL;
    RETURN(rv);
}

DEFINE_NATIVE(Object_hasOwnSlot) {
    ARGSPEC("**");
    if (!IS_OBJECT(args[0]))
        RETURN(FALSE_VAL);
    RETURN(BOOL_TO_VAL(objobject_has(VAL_TO_OBJECT(args[0]), args[1])));
}

DEFINE_NATIVE(Object_deleteSlot) {
    ARGSPEC("O*");
    objobject_delete(VAL_TO_OBJECT(args[0]), vm, args[1]);
    RETURN(args[0]);
}

DEFINE_NATIVE(Object_same) {
    ARGSPEC("***");
    RETURN(BOOL_TO_VAL(value_equal(args[1], args[2])));
}

DEFINE_NATIVE(Object_eq) {
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(value_equal(args[0], args[1])));
}

DEFINE_NATIVE(Object_not) {
    RETURN(BOOL_TO_VAL(!value_truthy(args[0])));
}

DEFINE_NATIVE(Object_clone) {
    ObjObject* obj = objobject_new(vm);
    obj->proto = args[0];
    RETURN(OBJ_TO_VAL(obj));
}

static bool
has_ancestor(VM* vm, Value src, Value target)
{
    if (value_equal(src, target)) return true;
    if (IS_OBJ(src)) {
        Obj* obj = VAL_TO_OBJ(src);
        if (obj->visited) return false;
        obj->visited = true;
    }
    bool rv = has_ancestor(vm, vm_get_prototype(vm, src), target);
    if (IS_OBJ(src))
        VAL_TO_OBJ(src)->visited = false;
    return rv;
}

// obj.hasAncestor(x) returns true if the obj has x anywhere
// on obj's prototype chain (including obj itself, i.e.
// obj.hasAncestor(obj) is always true).
DEFINE_NATIVE(Object_hasAncestor) {
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(has_ancestor(vm, args[0], args[1])));
}

DEFINE_NATIVE(Object_rawType) {
    ARGSPEC("**");
    Value v = args[1];
    switch (v.type) {
    case VALUE_NIL:    RETURN(OBJ_TO_VAL(CONST_STRING(vm, "nil")));
    case VALUE_TRUE:   RETURN(OBJ_TO_VAL(CONST_STRING(vm, "true")));
    case VALUE_FALSE:  RETURN(OBJ_TO_VAL(CONST_STRING(vm, "false")));
    case VALUE_NUMBER: RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Number")));
    case VALUE_OBJ:
        switch (VAL_TO_OBJ(v)->type) {
        case OBJ_STRING:  RETURN(OBJ_TO_VAL(CONST_STRING(vm, "String")));
        case OBJ_CLOSURE: RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Fn")));
        case OBJ_OBJECT:  RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Object")));
        case OBJ_NATIVE:  RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Native")));
        case OBJ_FIBER:   RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Fiber")));
        case OBJ_RANGE:   RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Range")));
        case OBJ_LIST:    RETURN(OBJ_TO_VAL(CONST_STRING(vm, "List")));
        case OBJ_MAP:     RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Map")));
        case OBJ_MESSAGE: RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Message")));
        default: UNREACHABLE();
        }
    default: UNREACHABLE();
    }
}

static ObjString*
num_to_string(VM* vm, double num) {
    if (isnan(num)) return CONST_STRING(vm, "nan");
    if (isinf(num)) {
        if (num > 0) return CONST_STRING(vm, "+inf");
        else return CONST_STRING(vm, "-inf");
    }
    int length;
    char buffer[24];
    if (num >= INT32_MIN && num <= INT32_MAX && num == (int32_t)num) {
        length = sprintf(buffer, "%d", (int32_t)num);
    } else {
        length = sprintf(buffer, "%.14g", num);
    }
    return objstring_copy(vm, buffer, length);
}

DEFINE_NATIVE(Object_toString) {
    Value this = args[0];
    switch (this.type) {
    case VALUE_NIL:    RETURN(OBJ_TO_VAL(CONST_STRING(vm, "nil")));
    case VALUE_TRUE:   RETURN(OBJ_TO_VAL(CONST_STRING(vm, "true")));
    case VALUE_FALSE:  RETURN(OBJ_TO_VAL(CONST_STRING(vm, "false")));
    case VALUE_NUMBER: RETURN(OBJ_TO_VAL(num_to_string(vm, VAL_TO_NUMBER(this))));
    case VALUE_OBJ: {
        Obj* obj = VAL_TO_OBJ(this);
        int length;
        // Calculate the length (at compile-time) to store an
        // Object prefix plus the hex representation of the pointer:
        //   prefix + "_" + "0x" + hex ptr + NUL byte
        char buffer[7 + 1 + 2 + sizeof(void*) * 8 / 4 + 1];
        const char* prefix;

        switch (obj->type) {
        case OBJ_STRING:  RETURN(this);
        case OBJ_CLOSURE: prefix = "Fn"; break;
        case OBJ_OBJECT:  prefix = "Object"; break;
        case OBJ_NATIVE:  prefix = "Native"; break;
        case OBJ_FIBER:   prefix = "Fiber"; break;
        case OBJ_RANGE:   prefix = "Range"; break;
        case OBJ_LIST:    prefix = "List"; break;
        case OBJ_MAP:     prefix = "Map"; break;
        case OBJ_MESSAGE: prefix = "Message"; break;
        default:          UNREACHABLE();
        }
        length = sprintf(buffer, "%s_%p", prefix, (void*) obj);
        RETURN(OBJ_TO_VAL(objstring_copy(vm, buffer, length)));
    }
    default: UNREACHABLE();
    }
}

DEFINE_NATIVE(Object_print) {
    Value this = args[0];
    vm_ensure_stack(vm, 1);
    vm_push(vm, this);
    if (!vm_invoke(vm, this, CONST_STRING(vm, "toString"), 0))
        return false;

    Value slot = vm_pop(vm);
    char* str = IS_STRING(slot)
        ? VAL_TO_STRING(slot)->chars
        : "[invalid toString]";
    fputs(str, stdout);
    fflush(stdout);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_rawIterMore) {
    ARGSPEC("O*");
    ObjObject* obj = VAL_TO_OBJECT(args[0]);
    Value rv = generic_tableIterMore(&obj->slots, args[1]);
    RETURN(rv);
}

DEFINE_NATIVE(Object_rawIterSlotsNext) {
    ARGSPEC("ON");
    ObjObject* obj = VAL_TO_OBJECT(args[0]);
    Entry entry;
    if (generic_tableIterEntry(&obj->slots, args[1], &entry))
        RETURN(entry.key);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_rawIterValueNext) {
    ARGSPEC("ON");
    ObjObject* obj = VAL_TO_OBJECT(args[0]);
    Entry entry;
    if (generic_tableIterEntry(&obj->slots, args[1], &entry))
        RETURN(entry.value);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_new) {
    ObjObject* obj = objobject_new(vm);
    obj->proto = args[0];
    Value rv = OBJ_TO_VAL(obj);
    // setup a call for obj.init(...).
    // rather than copy the receiver and arguments we "replace"
    // the current call.
    // we have: | proto | arg1 | ... | argn |
    // we need: |  rv   | arg1 | ... | argn |
    args[0] = rv;
    if (!vm_invoke(vm, rv, vm->init_string, num_args))
        return false;
    // at this point, rv is guaranteed to not be GCed as it was
    // called as the receiver for the init slot.
    vm_pop(vm);
    vm_push(vm, rv);
    return true;
}

// ============================= Fn =============================

DEFINE_NATIVE(Fn_new) {
    ARGSPEC("*F");
    RETURN(args[1]);
}

DEFINE_NATIVE(Fn_call) {
    ARGSPEC("F");
    vm_push_frame(vm, VAL_TO_CLOSURE(args[0]), num_args);
    return true;
}

DEFINE_NATIVE(Fn_callWithThis) {
    ARGSPEC("F*");
    // Shift the arguments, so that we set up the stack properly.
    //            0    1         2      3            num_args
    // We have: | fn | newThis | arg1 | arg2 | ... | arg_{num_args} |
    // we want:      | newThis | arg1 | arg2 | ... | arg_{num_args} |
    //                 0         1      2            num_args-1
    ObjClosure* closure = VAL_TO_CLOSURE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm); // this will be a duplicate
    vm_push_frame(vm, closure, num_args - 1);
    return true;
}

// ============================= Native =============================

DEFINE_NATIVE(Native_call) {
    ARGSPEC("n");
    ObjNative* native = VAL_TO_NATIVE(args[0]);
    return native->fn(vm, args, num_args);
}

DEFINE_NATIVE(Native_callWithThis) {
    ARGSPEC("n*");
    ObjNative* native = VAL_TO_NATIVE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm);
    return native->fn(vm, args, num_args - 1);
}

// ============================= Number =============================

#define DEFINE_NUMBER_METHOD(name, cast_to, op, return_type) \
    DEFINE_NATIVE(name) {\
        ARGSPEC("NN"); \
        cast_to a = (cast_to) VAL_TO_NUMBER(args[0]); \
        cast_to b = (cast_to) VAL_TO_NUMBER(args[1]); \
        RETURN(return_type(a op b)); \
    }

DEFINE_NUMBER_METHOD(Number_add,  double, +,  NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(Number_sub,  double, -,  NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(Number_mul,  double, *,  NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(Number_div,  double, /,  NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(Number_lt,   double, <,  BOOL_TO_VAL)
DEFINE_NUMBER_METHOD(Number_gt,   double, >,  BOOL_TO_VAL)
DEFINE_NUMBER_METHOD(Number_leq,  double, <=, BOOL_TO_VAL)
DEFINE_NUMBER_METHOD(Number_geq,  double, >=, BOOL_TO_VAL)
DEFINE_NUMBER_METHOD(Number_lor,  int32_t, |, NUMBER_TO_VAL)
DEFINE_NUMBER_METHOD(Number_land, int32_t, &, NUMBER_TO_VAL)
#undef DEFINE_NUMBER_METHOD

DEFINE_NATIVE(Number_negate) {
    ARGSPEC("N");
    RETURN(NUMBER_TO_VAL(-VAL_TO_NUMBER(args[0])));
}

DEFINE_NATIVE(Number_inclusiveRange) {
    ARGSPEC("NN");
    double start = VAL_TO_NUMBER(args[0]);
    double end = VAL_TO_NUMBER(args[1]);
    RETURN(OBJ_TO_VAL(objrange_new(vm, start, end, true)));
}

DEFINE_NATIVE(Number_exclusiveRange) {
    ARGSPEC("NN");
    double start = VAL_TO_NUMBER(args[0]);
    double end = VAL_TO_NUMBER(args[1]);
    RETURN(OBJ_TO_VAL(objrange_new(vm, start, end, false)));
}

// ============================= String =============================

#define DEFINE_STRING_METHOD(name, op) \
    DEFINE_NATIVE(name) {\
        ARGSPEC("SS"); \
        const char* a = VAL_TO_STRING(args[0])->chars; \
        const char* b = VAL_TO_STRING(args[1])->chars; \
        RETURN(BOOL_TO_VAL(strcmp(a, b) op 0)); \
    }

DEFINE_STRING_METHOD(String_lt, <)
DEFINE_STRING_METHOD(String_gt, >)
DEFINE_STRING_METHOD(String_leq, <=)
DEFINE_STRING_METHOD(String_geq, >=)
#undef DEFINE_STRING_METHOD

DEFINE_NATIVE(String_add) {
    ARGSPEC("SS");
    RETURN(OBJ_TO_VAL(objstring_concat(vm,
        VAL_TO_STRING(args[0]),
        VAL_TO_STRING(args[1])
        )));
}

DEFINE_NATIVE(String_length) {
    ARGSPEC("S");
    RETURN(NUMBER_TO_VAL(VAL_TO_STRING(args[0])->length));
}

DEFINE_NATIVE(String_get) {
    ARGSPEC("SN");
    ObjString* s = VAL_TO_STRING(args[0]);
    uint32_t idx;
    if (value_to_index(args[1], s->length, &idx))
        RETURN(OBJ_TO_VAL(objstring_copy(vm, s->chars + idx, 1)));
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(String_iterMore) {
    ARGSPEC("S*");
    ObjString* s = VAL_TO_STRING(args[0]);
    Value rv = generic_iterMore(args[1], s->length);
    RETURN(rv);
}

// ============================= Fiber =============================

// Run the given `fiber`, transferring `value`.
static bool
run_fiber(VM* vm, ObjFiber* fiber,
          Value value, const char* verb)
{
    if (fiber->error != NULL)       ERROR("Cannot '%s' a fiber with an error.", verb);
    if (objfiber_is_done(fiber))    ERROR("Cannot '%s' a finished fiber.", verb);
    if (fiber->parent != NULL)      ERROR("Cannot '%s' a fiber with a parent.", verb);
    if (fiber->state == FIBER_ROOT) ERROR("Cannot '%s' a root fiber.", verb);

    if (fiber->frames_count == 1
        && fiber->frames[0].ip == fiber->frames[0].closure->function->chunk.code) {
        if (fiber->frames[0].closure->function->arity == 1) {
            // The fiber has not ran yet, and is expecting some
            // data to be sent.
            *fiber->stack_top = value;
            fiber->stack_top++;
        }
    } else {
        // We're resuming the `fiber`. In this case, `fiber`'s stack
        // will be like so, since it has already been suspended (e.g.
        // from a Fiber.call or Fiber.yield):
        //   +---+-----------+
        //   |...| Fiber_... |
        //   +---+-----------+
        //                   ^-- stack_top
        // Replacing stack_top[-1] gives Fiber.call or Fiber.yield
        // a return value.
        fiber->stack_top[-1] = value;
    }
    fiber->parent = vm->fiber;
    vm->fiber = fiber;
    return true;
}

DEFINE_NATIVE(Fiber_current) {
    RETURN(OBJ_TO_VAL(vm->fiber));
}

DEFINE_NATIVE(Fiber_yield) {
    if (!vm->can_yield)
        ERROR("Tried to yield from a VM call.");
    Value v = NIL_VAL;
    if (num_args >= 1) {
        vm_drop(vm, num_args - 1);
        v = vm_pop(vm);
    }
    ObjFiber* parent = vm->fiber->parent;
    vm->fiber->state = FIBER_OTHER;
    vm->fiber->parent = NULL;
    vm->fiber = parent;
    if (vm->fiber != NULL) {
        vm->fiber->stack_top[-1] = v;
    }
    return true;
}

DEFINE_NATIVE(Fiber_abort) {
    ARGSPEC("*S");
    vm->fiber->error = VAL_TO_STRING(vm_pop(vm));
    return false;
}

DEFINE_NATIVE(Fiber_new) {
    ARGSPEC("*F");
    ObjClosure* closure = VAL_TO_CLOSURE(args[1]);
    if (closure->function->arity != 0
        && closure->function->arity != 1) {
        ERROR("Cannot create fiber from function with arity %d.", closure->function->arity);
    }
    ObjFiber* fiber = objfiber_new(vm, closure);
    RETURN(OBJ_TO_VAL(fiber));
}

DEFINE_NATIVE(Fiber_parent) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    RETURN(fiber->parent != NULL
            ? OBJ_TO_VAL(fiber->parent)
            : NIL_VAL);
}

DEFINE_NATIVE(Fiber_call) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    Value v = NIL_VAL;
    if (num_args >= 1) {
        vm_drop(vm, num_args - 1);
        v = vm_pop(vm);
    }
    return run_fiber(vm, fiber, v, "call");
}

DEFINE_NATIVE(Fiber_try) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    Value v = NIL_VAL;
    if (num_args >= 1) {
        vm_drop(vm, num_args - 1);
        v = vm_pop(vm);
    }
    if (run_fiber(vm, fiber, v, "try")) {
        vm->fiber->state = FIBER_TRY;
        return true;
    }
    return false;
}

DEFINE_NATIVE(Fiber_error) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    RETURN(fiber->error != NULL
            ? OBJ_TO_VAL(fiber->error)
            : NIL_VAL);
}

DEFINE_NATIVE(Fiber_isDone) {
    ARGSPEC("f");
    ObjFiber* fiber = VAL_TO_FIBER(args[0]);
    RETURN(BOOL_TO_VAL(objfiber_is_done(fiber)));
}

// ============================= Range =============================

DEFINE_NATIVE(Range_start) {
    ARGSPEC("r");
    RETURN(NUMBER_TO_VAL(VAL_TO_RANGE(args[0])->start));
}

DEFINE_NATIVE(Range_end) {
    ARGSPEC("r");
    RETURN(NUMBER_TO_VAL(VAL_TO_RANGE(args[0])->end));
}

DEFINE_NATIVE(Range_iterMore) {
    ARGSPEC("r*");
    ObjRange* range = VAL_TO_RANGE(args[0]);
    // nothing to iterate?
    if (range->start == range->end && !range->inclusive)
        RETURN(FALSE_VAL);

    double v;
    if (IS_NIL(args[1])) {
        // start of the iteration.
        v = range->start;
    } else if (IS_NUMBER(args[1])) {
        v = VAL_TO_NUMBER(args[1]);
        if (range->start <= range->end) {
            // 0..5 or 0...5
            v = v + 1;
            if (v < range->start) RETURN(FALSE_VAL);
            if (range->inclusive && v > range->end) RETURN(FALSE_VAL);
            if (!range->inclusive && v >= range->end) RETURN(FALSE_VAL);
        } else {
            // 5..0 or 5...0
            v = v - 1;
            if (v > range->start) RETURN(FALSE_VAL);
            if (range->inclusive && v < range->end) RETURN(FALSE_VAL);
            if (!range->inclusive && v <= range->end) RETURN(FALSE_VAL);
        }
    } else {
        RETURN(FALSE_VAL);
    }
    RETURN(NUMBER_TO_VAL(v));
}

DEFINE_NATIVE(Range_iterNext) {
    ARGSPEC("rN");
    RETURN(args[1]);
}

// ============================= List =============================

DEFINE_NATIVE(List_new) {
    ObjList* list = objlist_new(vm, num_args);
    for (int i = 0; i < num_args; i++)
        list->values[i] = args[i+1];
    RETURN(OBJ_TO_VAL(list));
}

DEFINE_NATIVE(List_add) {
    ARGSPEC("L*");
    ObjList* list = VAL_TO_LIST(args[0]);
    objlist_insert(list, vm, list->size, args[1]);
    RETURN(OBJ_TO_VAL(list));
}

DEFINE_NATIVE(List_get) {
    ARGSPEC("LN");
    ObjList* list = VAL_TO_LIST(args[0]);
    uint32_t idx;
    if (value_to_index(args[1], list->size, &idx))
        RETURN(objlist_get(list, idx));
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(List_set) {
    ARGSPEC("LN*");
    ObjList* list = VAL_TO_LIST(args[0]);
    uint32_t idx;
    if (value_to_index(args[1], list->size, &idx))
        objlist_set(list, idx, args[2]);
    RETURN(OBJ_TO_VAL(list));
}

DEFINE_NATIVE(List_delete) {
    ARGSPEC("LN");
    ObjList* list = VAL_TO_LIST(args[0]);
    uint32_t idx;
    if (value_to_index(args[1], list->size, &idx))
        objlist_del(list, vm, idx);
    RETURN(OBJ_TO_VAL(list));
}

DEFINE_NATIVE(List_insert) {
    ARGSPEC("LN*");
    ObjList* list = VAL_TO_LIST(args[0]);
    uint32_t idx;
    if (value_to_index(args[1], list->size + 1, &idx))
        objlist_insert(list, vm, idx, args[2]);
    RETURN(OBJ_TO_VAL(list));
}

DEFINE_NATIVE(List_length) {
    ARGSPEC("L");
    ObjList* list = VAL_TO_LIST(args[0]);
    RETURN(NUMBER_TO_VAL((double) list->size));
}

DEFINE_NATIVE(List_iterMore) {
    ARGSPEC("L*");
    ObjList* list = VAL_TO_LIST(args[0]);
    Value rv = generic_iterMore(args[1], list->size);
    RETURN(rv);
}

// ============================= Map =============================

DEFINE_NATIVE(Map_new) {
    ObjMap* map = objmap_new(vm);
    vm_push_root(vm, OBJ_TO_VAL(map));
    for (int i = 1; i < num_args; i += 2)
        objmap_set(map, vm, args[i], args[i+1]);
    vm_pop_root(vm);
    RETURN(OBJ_TO_VAL(map));
}

DEFINE_NATIVE(Map_has) {
    ARGSPEC("M*");
    bool rv = objmap_has(VAL_TO_MAP(args[0]), args[1]);
    RETURN(BOOL_TO_VAL(rv));
}

DEFINE_NATIVE(Map_get) {
    ARGSPEC("M*");
    Value rv;
    if (!objmap_get(VAL_TO_MAP(args[0]), args[1], &rv))
        rv = (num_args > 1) ? args[2] : NIL_VAL;
    RETURN(rv);
}

DEFINE_NATIVE(Map_set) {
    ARGSPEC("M**");
    objmap_set(VAL_TO_MAP(args[0]), vm, args[1], args[2]);
    RETURN(args[0]);
}

DEFINE_NATIVE(Map_delete) {
    ARGSPEC("M*");
    objmap_delete(VAL_TO_MAP(args[0]), vm, args[1]);
    RETURN(args[0]);
}

DEFINE_NATIVE(Map_rawIterMore) {
    ARGSPEC("M*");
    ObjMap* map = VAL_TO_MAP(args[0]);
    Value rv = generic_tableIterMore(&map->tbl, args[1]);
    RETURN(rv);
}

DEFINE_NATIVE(Map_rawIterKeyNext) {
    ARGSPEC("MN");
    ObjMap* map = VAL_TO_MAP(args[0]);
    Entry entry;
    if (generic_tableIterEntry(&map->tbl, args[1], &entry))
        RETURN(entry.key);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Map_rawIterValueNext) {
    ARGSPEC("MN");
    ObjMap* map = VAL_TO_MAP(args[0]);
    Entry entry;
    if (generic_tableIterEntry(&map->tbl, args[1], &entry))
        RETURN(entry.value);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Map_length) {
    ARGSPEC("M");
    ObjMap* map = VAL_TO_MAP(args[0]);
    RETURN(NUMBER_TO_VAL((double) map->tbl.count));
}

// ============================= Message =============================

DEFINE_NATIVE(Message_slotName) {
    ARGSPEC("m");
    ObjMessage* msg = VAL_TO_MESSAGE(args[0]);
    RETURN(OBJ_TO_VAL(msg->slot_name));
}

DEFINE_NATIVE(Message_args) {
    ARGSPEC("m");
    ObjMessage* msg = VAL_TO_MESSAGE(args[0]);
    RETURN(OBJ_TO_VAL(msg->args));
}

void core_init_vm(VM* vm)
{
#define ADD_OBJECT(table, name, obj) (define_on_table(vm, table, name, OBJ_TO_VAL(obj)))
#define ADD_NATIVE(table, name, fn)  (ADD_OBJECT(table, name, objnative_new(vm, fn)))
#define ADD_METHOD(PROTO, name, fn)  (ADD_NATIVE(&vm->PROTO->slots, name, fn))
#define ADD_VALUE(PROTO, name, v)    (define_on_table(vm, &vm->PROTO->slots, name, v))

    vm->perform_string = CONST_STRING(vm, "perform");
    vm->setSlot_string = CONST_STRING(vm, "setSlot");
    vm->init_string = CONST_STRING(vm, "init");

    vm->ObjectProto = objobject_new(vm);
    vm->ObjectProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(ObjectProto, "proto",       Object_proto);
    ADD_METHOD(ObjectProto, "setProto",    Object_setProto);
    ADD_METHOD(ObjectProto, "hash",        Object_hash);
    ADD_METHOD(ObjectProto, "hasSlot",     Object_hasSlot);
    ADD_METHOD(ObjectProto, "getSlot",     Object_getSlot);
    ADD_METHOD(ObjectProto, "rawSetSlot",  Object_setSlot);
    ADD_METHOD(ObjectProto, "rawPerform",  Object_perform);
    ADD_METHOD(ObjectProto, "hasOwnSlot",  Object_hasOwnSlot);
    ADD_METHOD(ObjectProto, "getOwnSlot",  Object_getOwnSlot);
    ADD_METHOD(ObjectProto, "setOwnSlot",  Object_setSlot);
    ADD_METHOD(ObjectProto, "deleteSlot",  Object_deleteSlot);
    ADD_METHOD(ObjectProto, "same",        Object_same);
    ADD_METHOD(ObjectProto, "rawType",     Object_rawType);
    ADD_METHOD(ObjectProto, "==",          Object_eq);
    ADD_METHOD(ObjectProto, "!",           Object_not);
    ADD_METHOD(ObjectProto, "clone",       Object_clone);
    ADD_METHOD(ObjectProto, "hasAncestor", Object_hasAncestor);
    ADD_METHOD(ObjectProto, "toString",    Object_toString);
    ADD_METHOD(ObjectProto, "print",       Object_print);
    ADD_METHOD(ObjectProto, "new",         Object_new);
    ADD_METHOD(ObjectProto, "rawIterMore", Object_rawIterMore);
    ADD_METHOD(ObjectProto, "rawIterSlotsNext", Object_rawIterSlotsNext);
    ADD_METHOD(ObjectProto, "rawIterValueNext", Object_rawIterValueNext);

    vm->FnProto = objobject_new(vm);
    vm->FnProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(FnProto, "new",          Fn_new);
    ADD_METHOD(FnProto, "call",         Fn_call);
    ADD_METHOD(FnProto, "callWithThis", Fn_callWithThis);

    vm->NativeProto = objobject_new(vm);
    vm->NativeProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(NativeProto, "call",         Native_call);
    ADD_METHOD(NativeProto, "callWithThis", Native_callWithThis);

    vm->NumberProto = objobject_new(vm);
    vm->NumberProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(NumberProto, "+",   Number_add);
    ADD_METHOD(NumberProto, "-",   Number_sub);
    ADD_METHOD(NumberProto, "*",   Number_mul);
    ADD_METHOD(NumberProto, "/",   Number_div);
    ADD_METHOD(NumberProto, "<",   Number_lt);
    ADD_METHOD(NumberProto, ">",   Number_gt);
    ADD_METHOD(NumberProto, "<=",  Number_leq);
    ADD_METHOD(NumberProto, ">=",  Number_geq);
    ADD_METHOD(NumberProto, "neg", Number_negate);
    ADD_METHOD(NumberProto, "|",   Number_lor);
    ADD_METHOD(NumberProto, "&",   Number_land);
    ADD_METHOD(NumberProto, "..",  Number_inclusiveRange);
    ADD_METHOD(NumberProto, "...", Number_exclusiveRange);
    ADD_VALUE(NumberProto, "inf",  NUMBER_TO_VAL(INFINITY));
    ADD_VALUE(NumberProto, "nan",  NUMBER_TO_VAL(NAN));
    ADD_VALUE(NumberProto, "largest",  NUMBER_TO_VAL(DBL_MAX));
    ADD_VALUE(NumberProto, "smallest", NUMBER_TO_VAL(DBL_MIN));

    vm->StringProto = objobject_new(vm);
    vm->StringProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(StringProto, "+",      String_add);
    ADD_METHOD(StringProto, "length", String_length);
    ADD_METHOD(StringProto, "<",      String_lt);
    ADD_METHOD(StringProto, ">",      String_gt);
    ADD_METHOD(StringProto, "<=",     String_leq);
    ADD_METHOD(StringProto, ">=",     String_geq);
    ADD_METHOD(StringProto, "get",    String_get);
    ADD_METHOD(StringProto, "iterNext", String_get);
    ADD_METHOD(StringProto, "iterMore", String_iterMore);

    vm->FiberProto = objobject_new(vm);
    vm->FiberProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(FiberProto, "current", Fiber_current);
    ADD_METHOD(FiberProto, "yield",   Fiber_yield);
    ADD_METHOD(FiberProto, "abort",   Fiber_abort);
    ADD_METHOD(FiberProto, "new",     Fiber_new);
    ADD_METHOD(FiberProto, "parent",  Fiber_parent);
    ADD_METHOD(FiberProto, "call",    Fiber_call);
    ADD_METHOD(FiberProto, "try",     Fiber_try);
    ADD_METHOD(FiberProto, "isDone",  Fiber_isDone);
    ADD_METHOD(FiberProto, "error",   Fiber_error);

    vm->RangeProto = objobject_new(vm);
    vm->RangeProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(RangeProto, "start",    Range_start);
    ADD_METHOD(RangeProto, "end",      Range_end);
    ADD_METHOD(RangeProto, "iterNext", Range_iterNext);
    ADD_METHOD(RangeProto, "iterMore", Range_iterMore);

    vm->ListProto = objobject_new(vm);
    vm->ListProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(ListProto, "new", List_new);
    ADD_METHOD(ListProto, "add", List_add);
    ADD_METHOD(ListProto, "get", List_get);
    ADD_METHOD(ListProto, "set", List_set);
    ADD_METHOD(ListProto, "delete", List_delete);
    ADD_METHOD(ListProto, "length", List_length);
    ADD_METHOD(ListProto, "insert", List_insert);
    ADD_METHOD(ListProto, "iterNext", List_get);
    ADD_METHOD(ListProto, "iterMore", List_iterMore);

    vm->MapProto = objobject_new(vm);
    vm->MapProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(MapProto, "new", Map_new);
    ADD_METHOD(MapProto, "has", Map_has);
    ADD_METHOD(MapProto, "get", Map_get);
    ADD_METHOD(MapProto, "set", Map_set);
    ADD_METHOD(MapProto, "delete", Map_delete);
    ADD_METHOD(MapProto, "length", Map_length);
    ADD_METHOD(MapProto, "rawIterMore", Map_rawIterMore);
    ADD_METHOD(MapProto, "rawIterKeyNext", Map_rawIterKeyNext);
    ADD_METHOD(MapProto, "rawIterValueNext", Map_rawIterValueNext);

    vm->MessageProto = objobject_new(vm);
    vm->MessageProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(MessageProto, "slotName", Message_slotName);
    ADD_METHOD(MessageProto, "args",     Message_args);

    ADD_OBJECT(&vm->globals, "Object",  vm->ObjectProto);
    ADD_OBJECT(&vm->globals, "Fn",      vm->FnProto);
    ADD_OBJECT(&vm->globals, "Native",  vm->NativeProto);
    ADD_OBJECT(&vm->globals, "Number",  vm->NumberProto);
    ADD_OBJECT(&vm->globals, "String",  vm->StringProto);
    ADD_OBJECT(&vm->globals, "Fiber",   vm->FiberProto);
    ADD_OBJECT(&vm->globals, "Range",   vm->RangeProto);
    ADD_OBJECT(&vm->globals, "List",    vm->ListProto);
    ADD_OBJECT(&vm->globals, "Map",     vm->MapProto);
    ADD_OBJECT(&vm->globals, "Message", vm->MessageProto);

    if (vm_interpret(vm, CORE_SOURCE) != INTERPRET_OK) {
        fprintf(stderr, "vm_interpret(CORE_SOURCE) not ok.\n");
        exit(788);
    }

#undef ADD_OBJECT
#undef ADD_NATIVE
#undef ADD_METHOD
#undef ADD_VALUE
}
