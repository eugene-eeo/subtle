#include "io.h"

#include "../object.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_CHUNK_SIZE 1024

typedef struct ExtIOContext {
    uid_t file_uid;
    Value file_proto;
} ExtIOContext;

typedef struct ExtIOFile {
    FILE* f;
    bool closed;
} ExtIOFile;

void
extio_file_free(VM* vm, void* p)
{
    ExtIOFile* ef = (ExtIOFile*)p;
    if (!ef->closed)
        fclose(ef->f);
    free(ef);
}

static bool
File_new(VM* vm, void* ctx, Value* args, int num_args)
{
    ExtIOContext* io_ctx = (ExtIOContext*)ctx;

    if (num_args < 2) {
        vm_runtime_error(vm, "%s expected a filename and mode.", __func__);
        return false;
    }

    Value path = args[1];
    if (!IS_STRING(path)) {
        vm_runtime_error(vm, "%s expected path to be a string.", __func__);
        return false;
    }
    Value mode = args[2];
    if (!IS_STRING(mode)) {
        vm_runtime_error(vm, "%s expected mode to be a string.", __func__);
        return false;
    }

    FILE* f = fopen(VAL_TO_STRING(path)->chars, VAL_TO_STRING(mode)->chars);
    if (f == NULL) {
        vm_runtime_error(vm, "%s: %s: %s.", __func__, VAL_TO_STRING(path)->chars, strerror(errno));
        return false;
    }
    ExtIOFile* ef = malloc(sizeof(ExtIOFile));
    if (ef == NULL) {
        vm_runtime_error(vm, "blown up!");
        exit(1);
    }
    ef->closed = false;
    ef->f = f;

    vm_drop(vm, num_args);
    args[0] = OBJ_TO_VAL(objforeign_new(
        vm,
        io_ctx->file_uid, (void*)ef,
        io_ctx->file_proto,
        extio_file_free));
    return true;
}

static bool
File_read(VM* vm, void* ctx, Value* args, int num_args)
{
    ExtIOContext* io_ctx = (ExtIOContext*)ctx;
    Value self = args[0];

    if (!value_has_uid(self, io_ctx->file_uid)) {
        vm_runtime_error(vm, "%s expected a File object.", __func__);
        return false;
    }

    ExtIOFile* ef = VAL_TO_FOREIGN(self)->p;
    char* buffer = malloc(READ_CHUNK_SIZE);
    if (buffer == NULL) {
        vm_runtime_error(vm, "%s: malloc failed.", __func__);
        return false;
    }

    size_t r = 0;
    size_t n = 0;
    size_t bufsz = READ_CHUNK_SIZE;
    while ((r = fread(&buffer[n], 1, READ_CHUNK_SIZE, ef->f)) == READ_CHUNK_SIZE) {
        n += r;
        if (bufsz < n + READ_CHUNK_SIZE) {
            bufsz *= 2;
            char* new_buffer = realloc(buffer, bufsz);
            if (new_buffer == NULL) {
                free(buffer);
                vm_runtime_error(vm, "%s: realloc failed.", __func__);
                return false;
            }
            buffer = new_buffer;
        }
    }
    n += r;
    if (ferror(ef->f) && !feof(ef->f)) {
        free(buffer);
        vm_runtime_error(vm, "%s: error reading from file.", __func__);
        return false;
    }

    // trim the buffer.
    buffer = realloc(buffer, n + 1);
    if (buffer == NULL) {
        // surprise?
        vm_runtime_error(vm, "%s: error resizing buffer.", __func__);
        return false;
    }
    buffer[n] = '\0';

    vm_drop(vm, num_args);
    args[0] = OBJ_TO_VAL(objstring_take(vm, buffer, n));
    return true;
}

static bool
File_close(VM* vm, void* ctx, Value* args, int num_args)
{
    ExtIOContext* io_ctx = (ExtIOContext*)ctx;
    Value self = args[0];

    if (!value_has_uid(self, io_ctx->file_uid)) {
        vm_runtime_error(vm, "%s expected a File object.", __func__);
        return false;
    }

    vm_drop(vm, num_args);
    ExtIOFile* ef = VAL_TO_FOREIGN(self)->p;
    if (!ef->closed) {
        if (fclose(ef->f) != 0) {
            vm_runtime_error(vm, "%s: %s.", __func__, strerror(errno));
            return false;
        }
    }
    ef->closed = true;
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
    add_native(vm, ctx, file_proto, "read", File_read);
    add_native(vm, ctx, file_proto, "close", File_close);
}
