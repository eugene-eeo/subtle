#include "core.h"

#include "core.subtle.inc"
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
    static bool name(VM* vm, void* ctx, Value* args, int num_args)

#define RETURN(expr) \
    do { \
        *(vm->fiber->stack_top - num_args - 1) = expr; \
        vm_drop(vm, num_args); \
        return true; \
    } while (false)

#define ERROR(...) \
    do { \
        vm_runtime_error(vm, __VA_ARGS__); \
        return false; \
    } while (false)

#define ARGSPEC(spec) \
    do { \
        for (int i=0; i < strlen(spec); i++) \
            ARG_CHECK_SINGLE(vm, (spec)[i], i); \
    } while(false)

#define CHECK_TYPE(idx, val, check, msg) \
    do { \
        if (!check(val)) { \
            if ((idx) == 0) \
                ERROR("%s expected 'self' to be %s.", __func__, msg); \
            else \
                ERROR("%s expected arg %d to be %s.", __func__, (idx)-1, msg); \
        } \
    } while(false)

#define ARG_CHECK_SINGLE(vm, ch, idx) \
    do { \
        if (num_args < (idx)) \
            ERROR("%s expected %d args, got %d instead.", __func__, idx, num_args); \
        Value arg = args[idx]; \
        switch (ch) { \
        case 'O': CHECK_TYPE(idx, arg, IS_OBJECT, "an Object"); break; \
        case 'S': CHECK_TYPE(idx, arg, IS_STRING, "a String"); break; \
        case 'N': CHECK_TYPE(idx, arg, IS_NUMBER, "a Number"); break; \
        case 'n': CHECK_TYPE(idx, arg, IS_NATIVE, "a Native"); break; \
        case 'F': CHECK_TYPE(idx, arg, IS_CLOSURE, "an Fn"); break; \
        case 'f': CHECK_TYPE(idx, arg, IS_FIBER, "a Fiber"); break; \
        case 'r': CHECK_TYPE(idx, arg, IS_RANGE, "a Range"); break; \
        case 'L': CHECK_TYPE(idx, arg, IS_LIST, "a List"); break; \
        case 'M': CHECK_TYPE(idx, arg, IS_MAP, "a Map"); break; \
        case 'm': CHECK_TYPE(idx, arg, IS_MSG, "a Msg"); break; \
        case '*': break; \
        default: UNREACHABLE(); \
        } \
    } while(false)

#define CONST_STRING(vm, s) objstring_copy(vm, (s), strlen(s))

static inline bool
is_integer(double f)
{
    return trunc(f) == f;
}

static bool
value_to_index(Value v, uint32_t length, uint32_t* idx)
{
    ASSERT(IS_NUMBER(v), "!IS_NUMBER(v)");
    double i = VAL_TO_NUMBER(v);
    if (!is_integer(i)) return false;
    if (i < 0) i += length;
    if (i < 0 || i >= length)
        return false;
    // at this point we know:
    //  1) i is integral
    //  2) i >= 0 and i < UINT32_MAX
    // so i must be a valid uint32_t
    *idx = (uint32_t) i;
    return true;
}

static bool
next_index(Value arg, uint32_t length, uint32_t* rv)
{
    double idx = -1;
    if (IS_NIL(arg)) {
        idx = 0;
    } else if (IS_NUMBER(arg)) {
        idx = VAL_TO_NUMBER(arg);
        idx++;
        if (!is_integer(idx)) return false;
    }
    if (idx < 0 || idx >= length)
        return false;
    *rv = (uint32_t) idx;
    return true;
}

// A generic implementation of iterMore for sized sequences.
static Value
generic_iterMore(Value arg, uint32_t length)
{
    uint32_t idx;
    if (!next_index(arg, length, &idx))
        return FALSE_VAL;
    return NUMBER_TO_VAL(idx);
}

// generic implementation to check if the table still has any
// valid entries after entry i.
static Value
generic_tableIterMore(Table* table, Value value)
{
    uint32_t idx;
    if (!next_index(value, table->capacity, &idx))
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
    if (IS_OBJECT(args[0])) {
        ObjObject* obj = VAL_TO_OBJECT(args[0]);
        if (obj->protos_count > 0)
            RETURN(obj->protos[0]);
        RETURN(NIL_VAL);
    }
    Value proto = vm_get_prototype(vm, args[0]);
    RETURN(proto);
}

DEFINE_NATIVE(Object_setProto) {
    ARGSPEC("O*");
    ObjObject* object = VAL_TO_OBJECT(args[0]);
    objobject_set_proto(object, vm, args[1]);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_setProtos) {
    ARGSPEC("OL");
    ObjObject* object = VAL_TO_OBJECT(args[0]);
    ObjList* protos = VAL_TO_LIST(args[1]);
    objobject_copy_protos(object, vm, protos->values, protos->size);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_addProto) {
    ARGSPEC("O*");
    ObjObject* object = VAL_TO_OBJECT(args[0]);
    objobject_insert_proto(object, vm, object->protos_count, args[1]);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_prependProto) {
    ARGSPEC("O*");
    ObjObject* object = VAL_TO_OBJECT(args[0]);
    objobject_insert_proto(object, vm, 0, args[1]);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_deleteProto) {
    ARGSPEC("O*");
    ObjObject* object = VAL_TO_OBJECT(args[0]);
    objobject_delete_proto(object, vm, args[1]);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_protos) {
    ARGSPEC("*");
    ObjList* list;
    if (IS_OBJECT(args[0])) {
        ObjObject* object = VAL_TO_OBJECT(args[0]);
        list = objlist_new(vm, object->protos_count);
        for (int i = 0; i < object->protos_count; i++)
            list->values[i] = object->protos[i];
    } else {
        list = objlist_new(vm, 1);
        list->values[0] = vm_get_prototype(vm, args[0]);
    }
    RETURN(OBJ_TO_VAL(list));
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
    if (IS_STRING(args[1]) && IS_CLOSURE(args[2])) {
        ObjFn* fn = VAL_TO_CLOSURE(args[2])->fn;
        if (fn->name == NULL)
            fn->name = VAL_TO_STRING(args[1]);
    }
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
    Value slot = NIL_VAL;
    Value self = args[0];
    ObjMsg* msg = VAL_TO_MSG(args[1]);

    // search on protos. if we find it, then we need to
    // perform a call. we always check that the call is
    // valid.
    bool exists = vm_get_slot(vm, self, OBJ_TO_VAL(msg->slot_name), &slot);
    if (!vm_check_call(vm, slot, msg->args->size, msg->slot_name))
        return false;
    if (!exists) {
        vm_push_root(vm, OBJ_TO_VAL(msg->slot_name));
        vm_runtime_error(vm, "Object does not respond to '%s'", msg->slot_name->chars);
        vm_pop_root(vm);
        return false;
    }

    // copy args onto stack.
    vm_push_root(vm, args[1]);
    vm_drop(vm, num_args); // pop all args, incl. msg
    vm_ensure_stack(vm, msg->args->size);
    vm_pop_root(vm); // msg
    for (uint32_t i = 0; i < msg->args->size; i++)
        vm_push(vm, msg->args->values[i]);
    return vm_complete_call(vm, slot, msg->args->size);
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

DEFINE_NATIVE(Object_neq) {
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(!value_equal(args[0], args[1])));
}

DEFINE_NATIVE(Object_not) {
    RETURN(BOOL_TO_VAL(!value_truthy(args[0])));
}

DEFINE_NATIVE(Object_clone) {
    ObjObject* obj = objobject_new(vm);
    vm_push_root(vm, OBJ_TO_VAL(obj));
    objobject_set_proto(obj, vm, args[0]);
    vm_pop_root(vm);
    RETURN(OBJ_TO_VAL(obj));
}

DEFINE_NATIVE(Object_is) {
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(vm_has_ancestor(vm, args[0], args[1])));
}

DEFINE_NATIVE(Object_type) {
    Value v = args[0];
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
        case OBJ_MSG:     RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Msg")));
        case OBJ_FOREIGN: RETURN(OBJ_TO_VAL(CONST_STRING(vm, "Foreign")));
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
    Value self = args[0];
    switch (self.type) {
    case VALUE_NIL:    RETURN(OBJ_TO_VAL(CONST_STRING(vm, "nil")));
    case VALUE_TRUE:   RETURN(OBJ_TO_VAL(CONST_STRING(vm, "true")));
    case VALUE_FALSE:  RETURN(OBJ_TO_VAL(CONST_STRING(vm, "false")));
    case VALUE_NUMBER: RETURN(OBJ_TO_VAL(num_to_string(vm, VAL_TO_NUMBER(self))));
    case VALUE_OBJ: {
        Obj* obj = VAL_TO_OBJ(self);
        int length;
        // Calculate the length (at compile-time) to store an
        // Object prefix plus the hex representation of the pointer:
        //   prefix + "_" + "0x" + hex ptr + NUL byte
        char buffer[7 + 1 + 2 + sizeof(void*) * 8 / 4 + 1];
        const char* prefix;

        switch (obj->type) {
        case OBJ_STRING:  RETURN(self);
        case OBJ_CLOSURE: prefix = "Fn"; break;
        case OBJ_OBJECT:  prefix = "Object"; break;
        case OBJ_NATIVE:  prefix = "Native"; break;
        case OBJ_FIBER:   prefix = "Fiber"; break;
        case OBJ_RANGE:   prefix = "Range"; break;
        case OBJ_LIST:    prefix = "List"; break;
        case OBJ_MAP:     prefix = "Map"; break;
        case OBJ_MSG:     prefix = "Msg"; break;
        case OBJ_FOREIGN: prefix = "Foreign"; break;
        default:          UNREACHABLE();
        }
        length = sprintf(buffer, "%s_%p", prefix, (void*) obj);
        RETURN(OBJ_TO_VAL(objstring_copy(vm, buffer, length)));
    }
    default: UNREACHABLE();
    }
}

DEFINE_NATIVE(Object_print) {
    Value self = args[0];
    vm_ensure_stack(vm, 1);
    vm_push(vm, self);
    if (!vm_invoke(vm, self, CONST_STRING(vm, "toString"), 0))
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

DEFINE_NATIVE(Object_rawSlotAt) {
    ARGSPEC("ON");
    ObjObject* obj = VAL_TO_OBJECT(args[0]);
    Entry entry;
    if (generic_tableIterEntry(&obj->slots, args[1], &entry))
        RETURN(entry.key);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_rawValueAt) {
    ARGSPEC("ON");
    ObjObject* obj = VAL_TO_OBJECT(args[0]);
    Entry entry;
    if (generic_tableIterEntry(&obj->slots, args[1], &entry))
        RETURN(entry.value);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object_new) {
    ObjObject* obj = objobject_new(vm);
    vm_push_root(vm, OBJ_TO_VAL(obj));
    objobject_set_proto(obj, vm, args[0]);
    vm_pop_root(vm);
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
    return vm_push_frame(vm, VAL_TO_CLOSURE(args[0]), num_args);
}

DEFINE_NATIVE(Fn_callWith) {
    ARGSPEC("F*");
    // Shift the arguments, so that we set up the stack properly.
    //            0    1         2      3            num_args
    // We have: | fn | newSelf | arg1 | arg2 | ... | arg_{num_args} |
    // we want:      | newSelf | arg1 | arg2 | ... | arg_{num_args} |
    //                 0         1      2            num_args-1
    ObjClosure* closure = VAL_TO_CLOSURE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm); // this will be a duplicate
    return vm_push_frame(vm, closure, num_args - 1);
}

DEFINE_NATIVE(Fn_apply) {
    ARGSPEC("FL");
    ObjClosure* fn = VAL_TO_CLOSURE(args[0]);
    ObjList* arg_list = VAL_TO_LIST(args[1]);

    vm_ensure_stack(vm, arg_list->size - 1);
    vm_pop(vm); // list
    for (uint32_t i = 0; i < arg_list->size; i++)
        vm_push(vm, arg_list->values[i]);

    return vm_push_frame(vm, fn, arg_list->size);
}

DEFINE_NATIVE(Fn_applyWith) {
    ARGSPEC("F*L");
    ObjClosure* fn = VAL_TO_CLOSURE(args[0]);
    Value self = args[1];
    ObjList* arg_list = VAL_TO_LIST(args[2]);

    vm_ensure_stack(vm, arg_list->size - 2);
    vm_pop(vm); // list
    vm_pop(vm); // new_self
    vm_pop(vm); // fn
    vm_push(vm, self);
    for (uint32_t i = 0; i < arg_list->size; i++)
        vm_push(vm, arg_list->values[i]);

    return vm_push_frame(vm, fn, arg_list->size);
}

// ============================= Native =============================

DEFINE_NATIVE(Native_call) {
    ARGSPEC("n");
    ObjNative* native = VAL_TO_NATIVE(args[0]);
    return native->fn(vm, native->ctx, args, num_args);
}

DEFINE_NATIVE(Native_callWith) {
    ARGSPEC("n*");
    ObjNative* native = VAL_TO_NATIVE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm);
    return native->fn(vm, native->ctx, args, num_args - 1);
}

DEFINE_NATIVE(Native_apply) {
    ARGSPEC("nL");
    ObjNative* native = VAL_TO_NATIVE(args[0]);
    ObjList* arg_list = VAL_TO_LIST(args[1]);

    vm_ensure_stack(vm, arg_list->size - 1);
    vm_pop(vm); // list
    Value* args_start = &vm->fiber->stack_top[-1];
    for (uint32_t i = 0; i < arg_list->size; i++)
        vm_push(vm, arg_list->values[i]);

    return native->fn(vm, native->ctx, args_start, arg_list->size);
}

DEFINE_NATIVE(Native_applyWith) {
    ARGSPEC("n*L");
    ObjNative* native = VAL_TO_NATIVE(args[0]);
    Value self = args[1];
    ObjList* arg_list = VAL_TO_LIST(args[2]);

    vm_ensure_stack(vm, arg_list->size - 2);
    vm_pop(vm); // list
    vm_pop(vm); // new_self
    vm_pop(vm); // fn
    vm_push(vm, self);
    Value* args_start = &vm->fiber->stack_top[-1];
    for (uint32_t i = 0; i < arg_list->size; i++)
        vm_push(vm, arg_list->values[i]);

    return native->fn(vm, native->ctx, args_start, arg_list->size);
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

DEFINE_NATIVE(Number_truncate) {
    ARGSPEC("N");
    double n = VAL_TO_NUMBER(args[0]);
    RETURN(NUMBER_TO_VAL(trunc(n)));
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
        && fiber->frames[0].ip == fiber->frames[0].closure->fn->chunk.code) {
        if (fiber->frames[0].closure->fn->arity == 1) {
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
        ERROR("Cannot yield from a VM call.");
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
    if (closure->fn->arity != 0 && closure->fn->arity != 1)
        ERROR("Cannot create fiber from function with arity %d.",
              closure->fn->arity);
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

DEFINE_NATIVE(Map_rawKeyAt) {
    ARGSPEC("MN");
    ObjMap* map = VAL_TO_MAP(args[0]);
    Entry entry;
    if (generic_tableIterEntry(&map->tbl, args[1], &entry))
        RETURN(entry.key);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Map_rawValueAt) {
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

// ============================= Msg =============================

DEFINE_NATIVE(Msg_new) {
    ARGSPEC("*S");
    ObjString* slot_name = VAL_TO_STRING(args[1]);
    ObjMsg* msg = objmsg_new(vm, slot_name, &args[2], num_args - 1);
    RETURN(OBJ_TO_VAL(msg));
}

DEFINE_NATIVE(Msg_newFromList) {
    ARGSPEC("*SL");
    ObjString* slot_name = VAL_TO_STRING(args[1]);
    ObjList* list = VAL_TO_LIST(args[2]);
    ObjMsg* msg = objmsg_from_list(vm, slot_name, list);
    RETURN(OBJ_TO_VAL(msg));
}

DEFINE_NATIVE(Msg_slotName) {
    ARGSPEC("m");
    ObjMsg* msg = VAL_TO_MSG(args[0]);
    RETURN(OBJ_TO_VAL(msg->slot_name));
}

DEFINE_NATIVE(Msg_setSlotName) {
    ARGSPEC("mS");
    ObjMsg* msg = VAL_TO_MSG(args[0]);
    msg->slot_name = VAL_TO_STRING(args[1]);
    RETURN(OBJ_TO_VAL(msg));
}

DEFINE_NATIVE(Msg_args) {
    ARGSPEC("m");
    ObjMsg* msg = VAL_TO_MSG(args[0]);
    RETURN(OBJ_TO_VAL(msg->args));
}

DEFINE_NATIVE(Msg_setArgs) {
    ARGSPEC("mL");
    ObjMsg* msg = VAL_TO_MSG(args[0]);
    msg->args = VAL_TO_LIST(args[1]);
    RETURN(OBJ_TO_VAL(msg));
}

void core_init_vm(VM* vm)
{
#define ADD_OBJECT(table, name, obj) (define_on_table(vm, table, name, OBJ_TO_VAL(obj)))
#define ADD_NATIVE(table, name, fn)  (ADD_OBJECT(table, name, objnative_new(vm, fn)))
#define ADD_METHOD(PROTO, name, fn)  (ADD_NATIVE(&vm->PROTO->slots, name, fn))
#define ADD_VALUE(PROTO, name, v)    (define_on_table(vm, &vm->PROTO->slots, name, v))
#define SET_PROTO(TARGET, PROTO)     (objobject_set_proto(vm->TARGET, vm, OBJ_TO_VAL(vm->PROTO)))

    vm->forward_string = CONST_STRING(vm, "forward");
    vm->init_string = CONST_STRING(vm, "init");

    vm->ObjectProto = objobject_new(vm);
    ADD_METHOD(ObjectProto, "proto",       Object_proto);
    ADD_METHOD(ObjectProto, "setProto",    Object_setProto);
    ADD_METHOD(ObjectProto, "setProtos",   Object_setProtos);
    ADD_METHOD(ObjectProto, "addProto",    Object_addProto);
    ADD_METHOD(ObjectProto, "prependProto",Object_prependProto);
    ADD_METHOD(ObjectProto, "deleteProto", Object_deleteProto);
    ADD_METHOD(ObjectProto, "protos",      Object_protos);
    ADD_METHOD(ObjectProto, "hash",        Object_hash);
    ADD_METHOD(ObjectProto, "hasSlot",     Object_hasSlot);
    ADD_METHOD(ObjectProto, "getSlot",     Object_getSlot);
    ADD_METHOD(ObjectProto, "setSlot",     Object_setSlot);
    ADD_METHOD(ObjectProto, "perform",     Object_perform);
    ADD_METHOD(ObjectProto, "hasOwnSlot",  Object_hasOwnSlot);
    ADD_METHOD(ObjectProto, "getOwnSlot",  Object_getOwnSlot);
    ADD_METHOD(ObjectProto, "deleteSlot",  Object_deleteSlot);
    ADD_METHOD(ObjectProto, "same",        Object_same);
    ADD_METHOD(ObjectProto, "type",        Object_type);
    ADD_METHOD(ObjectProto, "==",          Object_eq);
    ADD_METHOD(ObjectProto, "!=",          Object_neq);
    ADD_METHOD(ObjectProto, "!",           Object_not);
    ADD_METHOD(ObjectProto, "clone",       Object_clone);
    ADD_METHOD(ObjectProto, "is",          Object_is);
    ADD_METHOD(ObjectProto, "toString",    Object_toString);
    ADD_METHOD(ObjectProto, "print",       Object_print);
    ADD_METHOD(ObjectProto, "new",         Object_new);
    ADD_METHOD(ObjectProto, "rawIterMore", Object_rawIterMore);
    ADD_METHOD(ObjectProto, "rawSlotAt",   Object_rawSlotAt);
    ADD_METHOD(ObjectProto, "rawValueAt",  Object_rawValueAt);

    vm->FnProto = objobject_new(vm);
    SET_PROTO(FnProto, ObjectProto);
    ADD_METHOD(FnProto, "new",       Fn_new);
    ADD_METHOD(FnProto, "call",      Fn_call);
    ADD_METHOD(FnProto, "callWith",  Fn_callWith);
    ADD_METHOD(FnProto, "apply",     Fn_apply);
    ADD_METHOD(FnProto, "applyWith", Fn_applyWith);

    vm->NativeProto = objobject_new(vm);
    SET_PROTO(NativeProto, ObjectProto);
    ADD_METHOD(NativeProto, "call",      Native_call);
    ADD_METHOD(NativeProto, "callWith",  Native_callWith);
    ADD_METHOD(NativeProto, "apply",     Native_apply);
    ADD_METHOD(NativeProto, "applyWith", Native_applyWith);

    vm->NumberProto = objobject_new(vm);
    SET_PROTO(NumberProto, ObjectProto);
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
    ADD_METHOD(NumberProto, "truncate", Number_truncate);
    ADD_VALUE(NumberProto, "inf",  NUMBER_TO_VAL(INFINITY));
    ADD_VALUE(NumberProto, "nan",  NUMBER_TO_VAL(NAN));
    ADD_VALUE(NumberProto, "largest",  NUMBER_TO_VAL(DBL_MAX));
    ADD_VALUE(NumberProto, "smallest", NUMBER_TO_VAL(DBL_MIN));

    vm->StringProto = objobject_new(vm);
    SET_PROTO(StringProto, ObjectProto);
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
    SET_PROTO(FiberProto, ObjectProto);
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
    SET_PROTO(RangeProto, ObjectProto);
    ADD_METHOD(RangeProto, "start",    Range_start);
    ADD_METHOD(RangeProto, "end",      Range_end);
    ADD_METHOD(RangeProto, "iterNext", Range_iterNext);
    ADD_METHOD(RangeProto, "iterMore", Range_iterMore);

    vm->ListProto = objobject_new(vm);
    SET_PROTO(ListProto, ObjectProto);
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
    SET_PROTO(MapProto, ObjectProto);
    ADD_METHOD(MapProto, "new", Map_new);
    ADD_METHOD(MapProto, "has", Map_has);
    ADD_METHOD(MapProto, "get", Map_get);
    ADD_METHOD(MapProto, "set", Map_set);
    ADD_METHOD(MapProto, "delete", Map_delete);
    ADD_METHOD(MapProto, "length", Map_length);
    ADD_METHOD(MapProto, "rawIterMore", Map_rawIterMore);
    ADD_METHOD(MapProto, "rawKeyAt",    Map_rawKeyAt);
    ADD_METHOD(MapProto, "rawValueAt",  Map_rawValueAt);

    vm->MsgProto = objobject_new(vm);
    SET_PROTO(MsgProto, ObjectProto);
    ADD_METHOD(MsgProto, "new",         Msg_new);
    ADD_METHOD(MsgProto, "newFromList", Msg_newFromList);
    ADD_METHOD(MsgProto, "slotName",    Msg_slotName);
    ADD_METHOD(MsgProto, "args",        Msg_args);
    ADD_METHOD(MsgProto, "setSlotName", Msg_setSlotName);
    ADD_METHOD(MsgProto, "setArgs",     Msg_setArgs);

    ADD_OBJECT(&vm->globals, "Object", vm->ObjectProto);
    ADD_OBJECT(&vm->globals, "Fn",     vm->FnProto);
    ADD_OBJECT(&vm->globals, "Native", vm->NativeProto);
    ADD_OBJECT(&vm->globals, "Number", vm->NumberProto);
    ADD_OBJECT(&vm->globals, "String", vm->StringProto);
    ADD_OBJECT(&vm->globals, "Fiber",  vm->FiberProto);
    ADD_OBJECT(&vm->globals, "Range",  vm->RangeProto);
    ADD_OBJECT(&vm->globals, "List",   vm->ListProto);
    ADD_OBJECT(&vm->globals, "Map",    vm->MapProto);
    ADD_OBJECT(&vm->globals, "Msg",    vm->MsgProto);

    if (vm_interpret(vm, CORE_SOURCE) != INTERPRET_OK) {
        fprintf(stderr, "vm_interpret(CORE_SOURCE) not ok.\n");
        exit(788);
    }

#undef ADD_OBJECT
#undef ADD_NATIVE
#undef ADD_METHOD
#undef ADD_VALUE
#undef SET_PROTO
}
