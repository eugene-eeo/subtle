#include "core.h"
#include "value.h"
#include "object.h"
#include "table.h"
#include "vm.h"

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

#define ARG_ERROR(arg_idx, msg) \
    do { \
        if (arg_idx == 0) \
            ERROR("%s: expected 'this' to be an %s.", __func__, msg); \
        ERROR("%s: expected args[%d] to be %s.", __func__, arg_idx, msg); \
    } while (false)

#define ARGSPEC(spec) do { \
        int __arg_idx = 0; \
        for (int i=0; i < strlen(spec); i++) { \
            ARG_CHECK_SINGLE(spec[i], __arg_idx); \
            __arg_idx++; \
        } \
    } while(false)

#define ARG_CHECK_SINGLE(ch, idx) do { \
    Value arg = args[idx]; \
    switch (ch) { \
        case 'O': if (!IS_OBJECT(arg)) ARG_ERROR(idx, "an Object"); break; \
        case 'S': if (!IS_STRING(arg)) ARG_ERROR(idx, "a String"); break; \
        case 'N': if (!IS_NUMBER(arg)) ARG_ERROR(idx, "a Number"); break; \
        case 'B': if (!IS_BOOL(arg)) ARG_ERROR(idx, "a Boolean"); break; \
        case 'n': if (!IS_NATIVE(arg)) ARG_ERROR(idx, "a Native"); break; \
        case 'F': if (!IS_CLOSURE(arg)) ARG_ERROR(idx, "an Fn"); break; \
        case '*': if (num_args < idx) ERROR("%s expected %d args, got %d instead.", __func__, idx, num_args); break; \
        default: UNREACHABLE(); \
    } \
    } while(false)

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

// ============================= Object =============================

DEFINE_NATIVE(Object, proto) {
    Value proto = vm_get_prototype(vm, args[0]);
    RETURN(proto);
}

DEFINE_NATIVE(Object, setProto) {
    ARGSPEC("O*");

    ObjObject* object = VAL_TO_OBJECT(args[0]);
    object->proto = args[1];
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object, rawGetSlot) {
    ARGSPEC("**");

    Value this = args[0];
    Value slot;
    if (!vm_get_slot(vm, this, args[1], &slot))
        slot = NIL_VAL;
    RETURN(slot);
}

DEFINE_NATIVE(Object, rawSetSlot) {
    ARGSPEC("O**");

    objobject_set(VAL_TO_OBJECT(args[0]), vm, args[1], args[2]);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object, hasSlot) {
    ARGSPEC("**");

    Value slot;
    bool has_slot = vm_get_slot(vm, args[0], args[1], &slot);
    RETURN(BOOL_TO_VAL(has_slot));
}

DEFINE_NATIVE(Object, getOwnSlot) {
    ARGSPEC("**");

    if (!IS_OBJECT(args[0]))
        RETURN(NIL_VAL);

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    RETURN(BOOL_TO_VAL(objobject_has(this, args[1])));
}

DEFINE_NATIVE(Object, hasOwnSlot) {
    ARGSPEC("**");

    if (!IS_OBJECT(args[0]))
        return false;

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    RETURN(BOOL_TO_VAL(objobject_has(this, args[1])));
}

DEFINE_NATIVE(Object, deleteSlot) {
    ARGSPEC("O*");

    ObjObject* this = VAL_TO_OBJECT(args[0]);
    bool has_slot = objobject_delete(this, vm, args[1]);
    RETURN(BOOL_TO_VAL(has_slot));
}

DEFINE_NATIVE(Object, same) {
    ARGSPEC("***");
    RETURN(BOOL_TO_VAL(value_equal(args[1], args[2])));
}

DEFINE_NATIVE(Object, equal) {
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(value_equal(args[0], args[1])));
}

DEFINE_NATIVE(Object, notEqual) {
    ARGSPEC("**");
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
    ARGSPEC("**");
    RETURN(BOOL_TO_VAL(has_ancestor(vm, args[0], args[1])));
}

DEFINE_NATIVE(Object, print) {
    Value this = args[0];
    if (IS_NIL(this)) {
        fprintf(stdout, "nil");
        goto done;
    }

    Value name_slot;
    InterpretResult rv;
    vm_push(vm, this);
    if (!vm_invoke(vm, args[0], OBJ_TO_VAL(objstring_copy(vm, "name", 4)), 0, &name_slot, &rv))
        return rv;

    if (!IS_STRING(name_slot))
        ERROR("Object_print: expected .name to be a string.");
    fprintf(stdout, "%s_%p",
            VAL_TO_STRING(name_slot)->chars,
            VAL_TO_OBJ(this));
done:
    fflush(stdout);
    RETURN(NIL_VAL);
}

DEFINE_NATIVE(Object, println) {
    Value tmp;
    InterpretResult rv;
    vm_push(vm, args[0]);
    if (!vm_invoke(vm, args[0], OBJ_TO_VAL(objstring_copy(vm, "print", 5)), 0, &tmp, &rv))
        return rv;

    fprintf(stdout, "\n");
    fflush(stdout);
    RETURN(NIL_VAL);
}

// ============================= Fn =============================

DEFINE_NATIVE(Fn, new) {
    ARGSPEC("*F");
    RETURN(args[1]);
}

DEFINE_NATIVE(Fn, call) {
    ARGSPEC("F");
    return vm_push_frame(vm, VAL_TO_CLOSURE(args[0]), num_args);
}

DEFINE_NATIVE(Fn, callWithThis) {
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
    return vm_push_frame(vm, closure, num_args - 1);
}

// ============================= Native =============================

DEFINE_NATIVE(Native, call) {
    ARGSPEC("n");

    ObjNative* native = VAL_TO_NATIVE(args[0]);
    return native->fn(vm, args, num_args);
}

DEFINE_NATIVE(Native, callWithThis) {
    ARGSPEC("n*");

    ObjNative* native = VAL_TO_NATIVE(args[0]);
    for (int i = 0; i < num_args; i++)
        args[i] = args[i + 1];
    vm_pop(vm);
    return native->fn(vm, args, num_args - 1);
}

// ============================= Number =============================

#define DEFINE_ARITHMETIC_METHOD(name, op, return_type) \
    DEFINE_NATIVE(Number, name) {\
        ARGSPEC("NN"); \
        Value this = args[0]; \
        RETURN(return_type(VAL_TO_NUMBER(this) op VAL_TO_NUMBER(args[1]))); \
    }

#define DEFINE_BITWISE_METHOD(name, op) \
    DEFINE_NATIVE(Number, name) {\
        ARGSPEC("NN"); \
        Value this = args[0]; \
        int32_t left = (int32_t) VAL_TO_NUMBER(this); \
        int32_t right = (int32_t) VAL_TO_NUMBER(args[1]); \
        RETURN(NUMBER_TO_VAL(left op right)); \
    }

DEFINE_ARITHMETIC_METHOD(plus,     +,  NUMBER_TO_VAL);
DEFINE_ARITHMETIC_METHOD(minus,    -,  NUMBER_TO_VAL);
DEFINE_ARITHMETIC_METHOD(multiply, *,  NUMBER_TO_VAL);
DEFINE_ARITHMETIC_METHOD(divide,   /,  NUMBER_TO_VAL);
DEFINE_ARITHMETIC_METHOD(lt,       <,  BOOL_TO_VAL);
DEFINE_ARITHMETIC_METHOD(gt,       >,  BOOL_TO_VAL);
DEFINE_ARITHMETIC_METHOD(leq,      <=, BOOL_TO_VAL);
DEFINE_ARITHMETIC_METHOD(geq,      >=, BOOL_TO_VAL);
DEFINE_BITWISE_METHOD(lor,  |);
DEFINE_BITWISE_METHOD(land, &);

#undef DEFINE_ARITHMETIC_METHOD
#undef DEFINE_BITWISE_METHOD

DEFINE_NATIVE(Number, negate) {
    ARGSPEC("N");

    RETURN(NUMBER_TO_VAL(-VAL_TO_NUMBER(args[0])));
}

DEFINE_NATIVE(Number, print) {
    ARGSPEC("N");

    fprintf(stdout, "%g", VAL_TO_NUMBER(args[0]));
    fflush(stdout);
    RETURN(NIL_VAL);
}

// ============================= Boolean =============================

DEFINE_NATIVE(Boolean, print) {
    ARGSPEC("B");

    fprintf(stdout, VAL_TO_BOOL(args[0]) ? "true" : "false");
    fflush(stdout);
    RETURN(NIL_VAL);
}

// ============================= String =============================

DEFINE_NATIVE(String, plus) {
    ARGSPEC("SS");
    RETURN(OBJ_TO_VAL(objstring_concat(vm,
        VAL_TO_STRING(args[0]),
        VAL_TO_STRING(args[1])
        )));
}

DEFINE_NATIVE(String, print) {
    ARGSPEC("S");
    fprintf(stdout, "%s", VAL_TO_STRING(args[0])->chars);
    fflush(stdout);
    RETURN(NIL_VAL);
}

void core_init_vm(VM* vm)
{
#define ADD_OBJECT(table, name, obj) (define_on_table(vm, table, name, OBJ_TO_VAL(obj)))
#define ADD_NATIVE(table, name, fn)  (ADD_OBJECT(table, name, objnative_new(vm, fn)))
#define ADD_METHOD(PROTO, name, fn)  (ADD_NATIVE(&vm->PROTO->slots, name, fn))
#define ADD_NAME(PROTO, name)        (ADD_OBJECT(&vm->PROTO->slots, "name", objstring_copy(vm, name, strlen(name))))

    vm->getSlot_string = OBJ_TO_VAL(objstring_copy(vm, "getSlot", 7));
    vm->setSlot_string = OBJ_TO_VAL(objstring_copy(vm, "setSlot", 7));

    vm->ObjectProto = objobject_new(vm);
    ADD_NAME(ObjectProto, "Object");
    ADD_METHOD(ObjectProto, "proto",       Object_proto);
    ADD_METHOD(ObjectProto, "setProto",    Object_setProto);
    ADD_METHOD(ObjectProto, "rawGetSlot",  Object_rawGetSlot);
    ADD_METHOD(ObjectProto, "rawSetSlot",  Object_rawSetSlot);
    ADD_METHOD(ObjectProto, "hasSlot",     Object_hasSlot);
    ADD_METHOD(ObjectProto, "getOwnSlot",  Object_getOwnSlot);
    ADD_METHOD(ObjectProto, "setOwnSlot",  Object_rawSetSlot);
    ADD_METHOD(ObjectProto, "hasOwnSlot",  Object_hasOwnSlot);
    ADD_METHOD(ObjectProto, "deleteSlot",  Object_deleteSlot);
    ADD_METHOD(ObjectProto, "same",        Object_same);
    ADD_METHOD(ObjectProto, "==",          Object_equal);
    ADD_METHOD(ObjectProto, "!=",          Object_notEqual);
    ADD_METHOD(ObjectProto, "!",           Object_not);
    ADD_METHOD(ObjectProto, "clone",       Object_clone);
    ADD_METHOD(ObjectProto, "hasAncestor", Object_hasAncestor);
    ADD_METHOD(ObjectProto, "print",       Object_print);
    ADD_METHOD(ObjectProto, "println",     Object_println);

    // Note: allocating here is safe, because all *Protos are marked as
    // roots, and remaining *Protos are initialized to NULL. Thus we won't
    // potentially free ObjectProto.
    vm->FnProto = objobject_new(vm);
    vm->FnProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_NAME(FnProto, "Fn");
    ADD_METHOD(FnProto, "new",          Fn_new);
    ADD_METHOD(FnProto, "call",         Fn_call);
    ADD_METHOD(FnProto, "callWithThis", Fn_callWithThis);

    vm->NativeProto = objobject_new(vm);
    vm->NativeProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_NAME(NativeProto, "Native");
    ADD_METHOD(NativeProto, "call",         Native_call);
    ADD_METHOD(NativeProto, "callWithThis", Native_callWithThis);

    vm->NumberProto = objobject_new(vm);
    vm->NumberProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(NumberProto, "+",     Number_plus);
    ADD_METHOD(NumberProto, "-",     Number_minus);
    ADD_METHOD(NumberProto, "*",     Number_multiply);
    ADD_METHOD(NumberProto, "/",     Number_divide);
    ADD_METHOD(NumberProto, "<",     Number_lt);
    ADD_METHOD(NumberProto, ">",     Number_gt);
    ADD_METHOD(NumberProto, "<=",    Number_leq);
    ADD_METHOD(NumberProto, ">=",    Number_geq);
    ADD_METHOD(NumberProto, "neg",   Number_negate);
    ADD_METHOD(NumberProto, "print", Number_print);
    ADD_METHOD(NumberProto, "|",     Number_lor);
    ADD_METHOD(NumberProto, "&",     Number_land);

    vm->BooleanProto = objobject_new(vm);
    vm->BooleanProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(BooleanProto, "print", Boolean_print);

    vm->StringProto = objobject_new(vm);
    vm->StringProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_METHOD(StringProto, "+",     String_plus);
    ADD_METHOD(StringProto, "print", String_print);

    ADD_OBJECT(&vm->globals, "Object",  vm->ObjectProto);
    ADD_OBJECT(&vm->globals, "Fn",      vm->FnProto);
    ADD_OBJECT(&vm->globals, "Native",  vm->NativeProto);
    ADD_OBJECT(&vm->globals, "Number",  vm->NumberProto);
    ADD_OBJECT(&vm->globals, "Boolean", vm->BooleanProto);
    ADD_OBJECT(&vm->globals, "String",  vm->StringProto);

#undef ADD_OBJECT
#undef ADD_NATIVE
#undef ADD_METHOD
#undef ADD_NAME
}
