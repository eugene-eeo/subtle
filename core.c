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
    Value proto = get_prototype(vm, args[0]);
    RETURN(proto);
}

DEFINE_NATIVE(Object, setProto) {
    if (num_args == 0)
        ERROR("Object_setProto called with 0 arguments");

    if (!IS_OBJECT(args[0]))
        ERROR("Object_setProto called on a non-object.");

    ObjObject* object = VAL_TO_OBJECT(args[0]);
    object->proto = args[1];
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object, getSlot) {
    if (num_args == 0)
        ERROR("Object_getSlot called with 0 arguments");

    Value this = args[0];
    Value slot;
    if (!get_slot(vm, this, args[1], &slot))
        slot = NIL_VAL;
    RETURN(slot);
}

DEFINE_NATIVE(Object, setSlot) {
    if (num_args == 0)
        ERROR("Object_setSlot called with 0 arguments.");

    if (!IS_OBJECT(args[0]))
        ERROR("Object_setProto called on a non-object.");

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    Value key   = num_args >= 1 ? args[1] : NIL_VAL;
    Value value = num_args >= 2 ? args[2] : NIL_VAL;
    objobject_set(this, vm, key, value);
    RETURN(NIL_VAL);
}

// Defines the == method.
DEFINE_NATIVE(Object, equal) {
    if (num_args == 0)
        ERROR("Object_equal called with 0 arguments.");
    RETURN(BOOL_TO_VAL(value_equal(args[0],
                                   args[1])));
}

// Defines the != method.
DEFINE_NATIVE(Object, notEqual) {
    if (num_args == 0)
        ERROR("Object_notEqual called with 0 arguments.");
    RETURN(BOOL_TO_VAL(!value_equal(args[0],
                                    args[1])));
}

// Defines the ! method.
DEFINE_NATIVE(Object, not) {
    RETURN(BOOL_TO_VAL(!value_truthy(args[0])));
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
    // Don't need to pop off the stack:
    // The stack is perfectly set up for the function call.
    return vm_call_closure(vm, args[0], VAL_TO_CLOSURE(args[0]), num_args);
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
    Value this = args[1];
    for (int i = 0; i < num_args - 1; i++)
        args[i + 1] = args[i + 2];
    POP_ARGS(1);
    return vm_call_closure(vm, this, VAL_TO_CLOSURE(args[0]), num_args - 1);
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
    POP_ARGS(1);
    return native->fn(vm, args, num_args - 1);
}

// Define the methods for Number

#define DEFINE_ARITHMETIC_METHOD(name, op, return_type) \
    DEFINE_NATIVE(Number, name) {\
        if (!IS_NUMBER(args[0])) \
            ERROR("Expected to be called on a number."); \
        if (num_args == 0 || !IS_NUMBER(args[1])) \
            ERROR("Expected a number."); \
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

void core_init_vm(VM* vm)
{
#define ADD_NATIVE(table, name, fn)  (define_on_table(vm, table, name, OBJ_TO_VAL(objnative_new(vm, fn))))
#define ADD_OBJECT(table, name, obj) (define_on_table(vm, table, name, OBJ_TO_VAL(obj)))

    vm->ObjectProto = objobject_new(vm);
    ADD_NATIVE(&vm->ObjectProto->slots, "proto",    Object_proto);
    ADD_NATIVE(&vm->ObjectProto->slots, "setProto", Object_setProto);
    ADD_NATIVE(&vm->ObjectProto->slots, "getSlot",  Object_getSlot);
    ADD_NATIVE(&vm->ObjectProto->slots, "setSlot",  Object_setSlot);
    ADD_NATIVE(&vm->ObjectProto->slots, "==",       Object_equal);
    ADD_NATIVE(&vm->ObjectProto->slots, "!=",       Object_notEqual);
    ADD_NATIVE(&vm->ObjectProto->slots, "!",        Object_not);

    vm->FnProto = objobject_new(vm);
    vm->FnProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_NATIVE(&vm->FnProto->slots, "new", Fn_new);
    ADD_NATIVE(&vm->FnProto->slots, "call", Fn_call);
    ADD_NATIVE(&vm->FnProto->slots, "callWithThis", Fn_callWithThis);

    vm->NativeProto = objobject_new(vm);
    vm->NativeProto->proto = OBJ_TO_VAL(vm->NativeProto);
    ADD_NATIVE(&vm->NativeProto->slots, "call", Native_call);
    ADD_NATIVE(&vm->NativeProto->slots, "callWithThis", Native_callWithThis);

    vm->NumberProto = objobject_new(vm);
    vm->NumberProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_NATIVE(&vm->NumberProto->slots, "+", Number_plus);
    ADD_NATIVE(&vm->NumberProto->slots, "-", Number_minus);
    ADD_NATIVE(&vm->NumberProto->slots, "*", Number_multiply);
    ADD_NATIVE(&vm->NumberProto->slots, "/", Number_divide);
    ADD_NATIVE(&vm->NumberProto->slots, "<", Number_lt);
    ADD_NATIVE(&vm->NumberProto->slots, ">", Number_gt);
    ADD_NATIVE(&vm->NumberProto->slots, "<=", Number_leq);
    ADD_NATIVE(&vm->NumberProto->slots, ">=", Number_geq);
    ADD_NATIVE(&vm->NumberProto->slots, "neg", Number_negate);

    vm->BooleanProto = objobject_new(vm);
    vm->BooleanProto->proto = OBJ_TO_VAL(vm->ObjectProto);

    ADD_OBJECT(&vm->globals, "Object", vm->ObjectProto);
    ADD_OBJECT(&vm->globals, "Fn", vm->FnProto);
    ADD_OBJECT(&vm->globals, "Native", vm->NativeProto);
    ADD_OBJECT(&vm->globals, "Number", vm->NumberProto);
    ADD_OBJECT(&vm->globals, "Boolean", vm->BooleanProto);

#undef ADD_NATIVE
#undef ADD_OBJECT
}
