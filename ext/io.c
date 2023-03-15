#include "io.h"

#include "../object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ExtIOContext {
    uid_t file_uid;
    Value file_proto;
} ExtIOContext;

bool
File_new(VM* vm, void* ctx, Value* args, int num_args)
{
    ExtIOContext* io_ctx = (ExtIOContext*)ctx;

    vm_drop(vm, num_args);
    args[0] = OBJ_TO_VAL(objforeign_new(
        vm,
        io_ctx->file_uid, NULL,
        io_ctx->file_proto,
        NULL));
    return true;
}

bool
File_hello(VM* vm, void* ctx, Value* args, int num_args)
{
    ExtIOContext* io_ctx = (ExtIOContext*)ctx;
    Value self = args[0];

    if (!value_has_uid(self, io_ctx->file_uid)) {
        vm_runtime_error(vm, "expected a File object");
        return false;
    }
    vm_drop(vm, num_args);
    args[0] = TRUE_VAL;
    return true;
}

static void
add_native(VM* vm, ExtIOContext* ctx, ObjObject* obj, char* s, NativeFn f)
{
    Value k = OBJ_TO_VAL(objstring_copy(vm, s, strlen(s)));
    vm_push_root(vm, k);
    Value n = OBJ_TO_VAL(objnative_new_with_context(vm, f, ctx));
    vm_push_root(vm, n);
    objobject_set(obj, vm, k, n);
    vm_pop_root(vm); // n
    vm_pop_root(vm); // k
}

void
free_context(VM* vm, void* ctx)
{
    free(ctx);
}

void
ext_io_init_vm(VM* vm)
{
    ExtIOContext* ctx = (ExtIOContext*)malloc(sizeof(ExtIOContext));
    if (ctx == NULL) {
        fprintf(stderr, "%s failed to allocate context", __func__);
        exit(1);
    }

    ctx->file_uid = vm_get_uid(vm);

    vm_add_extension(vm, (void*)ctx, free_context);

    ObjObject* file_proto = objobject_new(vm);
    vm_add_global(vm, "File", OBJ_TO_VAL(file_proto));
    objobject_set_proto(file_proto, vm, OBJ_TO_VAL(vm->ObjectProto));

    ctx->file_proto = OBJ_TO_VAL(file_proto);

    add_native(vm, ctx, file_proto, "new", File_new);
    add_native(vm, ctx, file_proto, "hello", File_hello);
}
