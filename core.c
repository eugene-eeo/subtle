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

bool Object_proto(VM* vm, Value* args, int num_args) {
    args[0] = get_prototype(vm, args[0]);
    return true;
}

bool Object_setProto(VM* vm, Value* args, int num_args) {
    if (num_args == 0) {
        vm_runtime_error(vm, "Object_setProto called with 0 arguments");
        return false;
    }
    if (!IS_OBJECT(args[0])) {
        vm_runtime_error(vm, "Object_setProto called on a non-object.");
        return false;
    }
    ObjObject* object = VAL_TO_OBJECT(args[0]);
    object->proto = args[1];
    args[0] = NIL_VAL;
    return true;
}

bool Object_getSlot(VM* vm, Value* args, int num_args) {
    if (num_args == 0) {
        vm_runtime_error(vm, "Object_getSlot called with 0 arguments");
        return false;
    }
    Value this = args[0];
    Value slot;
    if (!get_slot(vm, this, args[1], &slot))
        slot = NIL_VAL;
    args[0] = slot;
    return true;
}

bool Fn_new(VM* vm, Value* args, int num_args) {
    if (num_args == 0) {
        vm_runtime_error(vm, "Fn_new called with 0 arguments");
        return false;
    }
    if (!IS_CLOSURE(args[1])) {
        vm_runtime_error(vm, "Fn_new called with non function.");
        return false;
    }
    args[0] = args[1];
    return true;
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
#define DEFINE_NATIVE(table, name, fn)  (define_on_table(vm, table, name, OBJ_TO_VAL(objnative_new(vm, fn))))
#define DEFINE_OBJECT(table, name, obj) (define_on_table(vm, table, name, OBJ_TO_VAL(obj)))

    vm->ObjectProto = objobject_new(vm);
    DEFINE_NATIVE(&vm->ObjectProto->slots, "proto",    Object_proto);
    DEFINE_NATIVE(&vm->ObjectProto->slots, "setProto", Object_setProto);
    DEFINE_NATIVE(&vm->ObjectProto->slots, "getSlot",  Object_getSlot);

    vm->FnProto = objobject_new(vm);
    vm->FnProto->proto = OBJ_TO_VAL(vm->ObjectProto);
    DEFINE_NATIVE(&vm->FnProto->slots, "new", Fn_new);
    DEFINE_NATIVE(&vm->FnProto->slots, "call", Fn_call);

    DEFINE_OBJECT(&vm->globals, "Object", vm->ObjectProto);
    DEFINE_OBJECT(&vm->globals, "Fn", vm->FnProto);

#undef DEFINE_NATIVE
#undef DEFINE_OBJECT
}
