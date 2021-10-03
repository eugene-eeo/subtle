#include "core.h"
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

DEFINE_NATIVE(Fn, new) {
    if (num_args == 0)
        ERROR("Fn_new called with 0 arguments");

    if (!IS_CLOSURE(args[1]))
        ERROR("Fn_new called with non function.");

    RETURN(args[1]);
}

bool Fn_call(VM* vm, Value* args, int num_args) {
    if (!IS_CLOSURE(args[0])) {
        vm_runtime_error(vm, "Fn_new called on a non-closure.");
        return false;
    }
    return vm_call_closure(vm, args[0], VAL_TO_CLOSURE(args[0]), num_args);
}

void core_init_vm(VM* vm)
{
#define ADD_NATIVE(table, name, fn)  (define_on_table(vm, table, name, OBJ_TO_VAL(objnative_new(vm, fn))))
#define ADD_OBJECT(table, name, obj) (define_on_table(vm, table, name, OBJ_TO_VAL(obj)))

    vm->ObjectProto = objobject_new(vm);
    ADD_NATIVE(&vm->ObjectProto->slots, "proto",    Object_proto);
    ADD_NATIVE(&vm->ObjectProto->slots, "setProto", Object_setProto);
    ADD_NATIVE(&vm->ObjectProto->slots, "getSlot",  Object_getSlot);

    vm->FnProto = objobject_new(vm);
    vm->FnProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    ADD_NATIVE(&vm->FnProto->slots, "new", Fn_new);
    ADD_NATIVE(&vm->FnProto->slots, "call", Fn_call);

    ADD_OBJECT(&vm->globals, "Object", vm->ObjectProto);
    ADD_OBJECT(&vm->globals, "Fn", vm->FnProto);

#undef ADD_NATIVE
#undef ADD_OBJECT
}
