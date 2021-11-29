#include "core.h"
#include "value.h"
#include "object.h"
#include "table.h"
#include "vm.h"
#include <string.h>

static inline void
define_on_table(VM* vm, Table* table, const char* name, Value value) {
    vm_push_root(vm, value);
    Value key = OBJ_TO_VAL(objstring_copy(vm, name, strlen(name)));
    vm_push_root(vm, key);

    table_set(table, vm, key, value);
    vm_pop_root(vm);
    vm_pop_root(vm);
}

#define POP_ARGS(num_args) \
    do { \
        for (int i = 0; i < num_args; i++) \
            vm_pop(vm); \
    } while (false)


#define ERROR(...) \
    do { \
        vm_runtime_error(vm, __VA_ARGS__); \
        POP_ARGS(num_args); \
        return false; \
    } while (false)


#define RETURN(v) \
    do { \
        POP_ARGS(num_args); \
        args[0] = v; \
        return true; \
    } while (false)


#define DEFINE_NATIVE(proto, name) \
    static bool proto##_##name(VM* vm, Value* args, int num_args)


DEFINE_NATIVE(Object, proto) {
    Value proto = vm_get_prototype(vm, args[0]);
    RETURN(proto);
}

DEFINE_NATIVE(Object, setProto) {
    if (num_args == 0)
        ERROR("Object_setProto called with 0 arguments.");

    if (!IS_OBJECT(args[0]))
        ERROR("Object_setProto called on a non-object.");

    ObjObject* object = VAL_TO_OBJECT(args[0]);
    object->proto = args[1];
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object, rawGetSlot) {
    if (num_args == 0)
        ERROR("Object_rawGetSlot called with 0 arguments.");

    Value this = args[0];
    Value slot;
    if (!vm_get_slot(vm, this, args[1], &slot))
        slot = NIL_VAL;
    RETURN(slot);
}

DEFINE_NATIVE(Object, rawSetSlot) {
    if (num_args < 2)
        ERROR("Object_rawSetSlot called with %d arguments, need 2.", num_args);

    if (!IS_OBJECT(args[0]))
        ERROR("Object_rawSetSlot called on a non-object.");

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    objobject_set(this, vm, args[1], args[2]);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object, hasSlot) {
    if (num_args == 0)
        ERROR("Object_hasSlot called with 0 arguments.");
    Value slot;
    bool has_slot = vm_get_slot(vm, args[0], args[1], &slot);
    RETURN(BOOL_TO_VAL(has_slot));
}

DEFINE_NATIVE(Object, getOwnSlot) {
    if (num_args == 0)
        ERROR("Object_getOwnSlot called with 0 arguments.");

    if (!IS_OBJECT(args[0]))
        RETURN(NIL_VAL);

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    RETURN(BOOL_TO_VAL(objobject_has(this, args[1])));
}

DEFINE_NATIVE(Object, setOwnSlot) {
    if (num_args != 2)
        ERROR("Object_setOwnSlot requires 2 arguments.");

    if (!IS_OBJECT(args[0]))
        RETURN(NIL_VAL);

    objobject_set(VAL_TO_OBJECT(args[0]), vm, args[1], args[2]);
    RETURN(args[2]);
}

DEFINE_NATIVE(Object, hasOwnSlot) {
    if (num_args == 0)
        ERROR("Object_hasOwnSlot called with 0 arguments.");

    if (!IS_OBJECT(args[0]))
        return false;

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    RETURN(BOOL_TO_VAL(objobject_has(this, args[1])));
}

DEFINE_NATIVE(Object, deleteSlot) {
    if (num_args == 0)
        ERROR("Object_deleteSlot called with 0 arguments.");

    if (!IS_OBJECT(args[0]))
        ERROR("Object_deleteSlot called on a non-object.");

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    bool has_slot = objobject_delete(this, vm, args[1]);
    RETURN(BOOL_TO_VAL(has_slot));
}

DEFINE_NATIVE(Object, same) {
    if (num_args < 2)
        ERROR("Object_same requires 2 arguments.");
    RETURN(BOOL_TO_VAL(value_equal(args[1], args[2])));
}

DEFINE_NATIVE(Object, equal) {
    if (num_args == 0)
        ERROR("Object_== called with 0 arguments.");
    RETURN(BOOL_TO_VAL(value_equal(args[0], args[1])));
}

DEFINE_NATIVE(Object, notEqual) {
    if (num_args == 0)
        ERROR("Object_!= called with 0 arguments.");
    RETURN(BOOL_TO_VAL(!value_equal(args[0], args[1])));
}

DEFINE_NATIVE(Object, not) {
    RETURN(BOOL_TO_VAL(!value_truthy(args[0])));
}

DEFINE_NATIVE(Object, clone) {
    ObjObject* obj = objobject_new(vm);
    obj->proto = args[0];
    RETURN(OBJ_TO_VAL(obj));
}

static bool
has_ancestor(VM* vm, Value src, Value target)
{
    bool rv;
    if (value_equal(src, target)) return true;
    if (IS_OBJ(src)) {
        Obj* obj = VAL_TO_OBJ(src);
        if (obj->visited) return false;
        obj->visited = true;
    }
    rv = has_ancestor(vm, vm_get_prototype(vm, src), target);
    if (IS_OBJ(src))
        VAL_TO_OBJ(src)->visited = false;
    return rv;
}

// obj.hasAncestor(x) returns true if the obj has x anywhere
// on obj's prototype chain (including obj itself, i.e.
// obj.hasAncestor(obj) is always true).
DEFINE_NATIVE(Object, hasAncestor) {
    if (num_args == 0)
        ERROR("Object_hasAncestor called with 0 arguments.");
    RETURN(BOOL_TO_VAL(has_ancestor(vm, args[0], args[1])));
}

DEFINE_NATIVE(Fn, new) {
    if (num_args == 0)
        ERROR("Fn_new called with 0 arguments.");

    if (!IS_CLOSURE(args[1]))
        ERROR("Fn_new called with non function.");

    RETURN(args[1]);
}

DEFINE_NATIVE(Fn, call) {
    if (!IS_CLOSURE(args[0])) {
        vm_runtime_error(vm, "Fn_call called on a non-closure.");
        return false;
    }
    return vm_push_frame(vm, VAL_TO_CLOSURE(args[0]), num_args);
}

DEFINE_NATIVE(Fn, callWithThis) {
    if (!IS_CLOSURE(args[0])) {
        vm_runtime_error(vm, "Fn_callWithThis called on a non-closure.");
        return false;
    }
    if (num_args == 0) {
        vm_runtime_error(vm, "Fn_callWithThis called with no arguments.");
        return false;
    }
    // Shift the arguments, so that we set up the stack properly.
    //            0    1         2      3            num_args
    // We have: | fn | newThis | arg1 | arg2 | ... | arg_{num_args} |
    // we want:      | newThis | arg1 | arg2 | ... | arg_{num_args} |
    //                 0         1      2            num_args-1
    ObjClosure* closure = VAL_TO_CLOSURE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm); // this will be a duplicate
    return vm_push_frame(vm, closure, num_args - 1);
}

DEFINE_NATIVE(Native, call) {
    if (!IS_NATIVE(args[0])) {
        vm_runtime_error(vm, "Native_call called on a non-native.");
        return false;
    }
    ObjNative* native = VAL_TO_NATIVE(args[0]);
    return native->fn(vm, args, num_args);
}

DEFINE_NATIVE(Native, callWithThis) {
    if (!IS_NATIVE(args[0])) {
        vm_runtime_error(vm, "Native_callWithThis called on a non-native.");
        return false;
    }
    if (num_args == 0) {
        vm_runtime_error(vm, "Native_callWithThis called with no arguments.");
        return false;
    }
    ObjNative* native = VAL_TO_NATIVE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm);
    return native->fn(vm, args, num_args - 1);
}

// Define the methods for Number

#define DEFINE_ARITHMETIC_METHOD(name, op, return_type) \
    DEFINE_NATIVE(Number, name) {\
        if (!IS_NUMBER(args[0])) \
            ERROR("%s expected to be called on a number.", __func__); \
        if (num_args == 0 || !IS_NUMBER(args[1])) \
            ERROR("%s called with a non-number.", __func__); \
        Value this = args[0]; \
        RETURN(return_type(VAL_TO_NUMBER(this) op VAL_TO_NUMBER(args[1]))); \
    }

DEFINE_ARITHMETIC_METHOD(plus,     +,  NUMBER_TO_VAL);
DEFINE_ARITHMETIC_METHOD(minus,    -,  NUMBER_TO_VAL);
DEFINE_ARITHMETIC_METHOD(multiply, *,  NUMBER_TO_VAL);
DEFINE_ARITHMETIC_METHOD(divide,   /,  NUMBER_TO_VAL);
DEFINE_ARITHMETIC_METHOD(lt,       <,  BOOL_TO_VAL);
DEFINE_ARITHMETIC_METHOD(gt,       >,  BOOL_TO_VAL);
DEFINE_ARITHMETIC_METHOD(leq,      <=, BOOL_TO_VAL);
DEFINE_ARITHMETIC_METHOD(geq,      >=, BOOL_TO_VAL);

DEFINE_NATIVE(Number, negate) {
    if (!IS_NUMBER(args[0]))
        ERROR("Expected to be called on a number.");
    RETURN(NUMBER_TO_VAL(-VAL_TO_NUMBER(args[0])));
}

#undef DEFINE_ARITHMETIC_METHOD

DEFINE_NATIVE(String, plus) {
    Value this = args[0];
    if (!IS_STRING(this))
        ERROR("Expected to be called on a string.");
    if (num_args == 0 || !IS_STRING(args[1]))
        ERROR("Expected a string.");
    RETURN(OBJ_TO_VAL(objstring_concat(vm,
        VAL_TO_STRING(this),
        VAL_TO_STRING(args[1])
        )));
}

void core_init_vm(VM* vm)
{
#define ADD_OBJECT(table, name, obj) (define_on_table(vm, table, name, OBJ_TO_VAL(obj)))
#define ADD_NATIVE(table, name, fn)  (ADD_OBJECT(table, name, objnative_new(vm, fn)))
#define ADD_METHOD(PROTO, name, fn)  (ADD_NATIVE(&vm->PROTO->slots, name, fn))

    vm->getSlot_string  = OBJ_TO_VAL(objstring_copy(vm, "getSlot", 7));
    vm->setSlot_string  = OBJ_TO_VAL(objstring_copy(vm, "setSlot", 7));
    vm->equal_string    = OBJ_TO_VAL(objstring_copy(vm, "==", 2));
    vm->notEqual_string = OBJ_TO_VAL(objstring_copy(vm, "!=", 2));
    vm->not_string      = OBJ_TO_VAL(objstring_copy(vm, "!", 1));

    vm->ObjectProto = objobject_new(vm);
    ADD_METHOD(ObjectProto, "proto",       Object_proto);
    ADD_METHOD(ObjectProto, "setProto",    Object_setProto);
    ADD_METHOD(ObjectProto, "rawGetSlot",  Object_rawGetSlot);
    ADD_METHOD(ObjectProto, "rawSetSlot",  Object_rawSetSlot);
    ADD_METHOD(ObjectProto, "hasSlot",     Object_hasSlot);
    ADD_METHOD(ObjectProto, "getOwnSlot",  Object_getOwnSlot);
    ADD_METHOD(ObjectProto, "setOwnSlot",  Object_setOwnSlot);
    ADD_METHOD(ObjectProto, "hasOwnSlot",  Object_hasOwnSlot);
    ADD_METHOD(ObjectProto, "deleteSlot",  Object_deleteSlot);
    ADD_METHOD(ObjectProto, "same",        Object_same);
    ADD_METHOD(ObjectProto, "==",          Object_equal);
    ADD_METHOD(ObjectProto, "!=",          Object_notEqual);
    ADD_METHOD(ObjectProto, "!",           Object_not);
    ADD_METHOD(ObjectProto, "clone",       Object_clone);
    ADD_METHOD(ObjectProto, "hasAncestor", Object_hasAncestor);

    // Note: allocating here is safe, because all *Protos are marked as
    // roots, and remaining *Protos are initialized to NULL. Thus we won't
    // potentially free ObjectProto.
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
    ADD_METHOD(NumberProto, "+",   Number_plus);
    ADD_METHOD(NumberProto, "-",   Number_minus);
    ADD_METHOD(NumberProto, "*",   Number_multiply);
    ADD_METHOD(NumberProto, "/",   Number_divide);
    ADD_METHOD(NumberProto, "<",   Number_lt);
    ADD_METHOD(NumberProto, ">",   Number_gt);
    ADD_METHOD(NumberProto, "<=",  Number_leq);
    ADD_METHOD(NumberProto, ">=",  Number_geq);
    ADD_METHOD(NumberProto, "neg", Number_negate);

    vm->BooleanProto = objobject_new(vm);
    vm->BooleanProto->proto = OBJ_TO_VAL(vm->ObjectProto);

    vm->StringProto = objobject_new(vm);
    vm->StringProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(StringProto, "+", String_plus);

    ADD_OBJECT(&vm->globals, "Object",  vm->ObjectProto);
    ADD_OBJECT(&vm->globals, "Fn",      vm->FnProto);
    ADD_OBJECT(&vm->globals, "Native",  vm->NativeProto);
    ADD_OBJECT(&vm->globals, "Number",  vm->NumberProto);
    ADD_OBJECT(&vm->globals, "Boolean", vm->BooleanProto);
    ADD_OBJECT(&vm->globals, "String",  vm->StringProto);

#undef ADD_OBJECT
#undef ADD_NATIVE
#undef ADD_METHOD
}
