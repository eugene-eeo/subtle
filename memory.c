#include "chunk.h"
#include "memory.h"
#include "table.h"

#ifdef SUBTLE_DEBUG_TRACE_ALLOC
#include "debug.h"
#endif

#include <stdio.h>   // perror
#include <stdlib.h>  // realloc, free

#define GC_HEAP_GROW_FACTOR 2

void* memory_realloc(VM* vm, void* ptr, size_t old_size, size_t new_size) {
    vm->bytes_allocated += new_size - old_size;

    if (new_size > old_size) {
#ifdef SUBTLE_DEBUG_STRESS_GC
        memory_collect(vm);
#endif
        if (vm->bytes_allocated > vm->next_gc)
            memory_collect(vm);
    }

    if (new_size == 0) {
        // free() never fails
        free(ptr);
        return NULL;
    }

    void* result = realloc(ptr, new_size);
    if (result == NULL) {
        perror("memory_realloc");
        exit(1);
    }
    return result;
}

void mark_object(VM* vm, Obj* obj) {
    if (obj == NULL) return;
    if (obj->marked) return; // don't mark cycles forever

#ifdef SUBTLE_DEBUG_TRACE_ALLOC
    printf("%p mark ", (void*)obj);
    debug_print_value(OBJ_TO_VAL(obj));
    printf("\n");
#endif

    ASSERT(!obj->visited, "obj->visited");
    obj->marked = true;

    // Make space in the gray stack.
    if (vm->gray_count + 1 > vm->gray_capacity) {
        vm->gray_capacity = GROW_CAPACITY(vm->gray_capacity);
        vm->gray_stack = (Obj**) realloc(
            vm->gray_stack,
            sizeof(Obj*) * vm->gray_capacity);
        if (vm->gray_stack == NULL) {
            perror("mark_object: cannot allocate gray_stack");
            exit(1);
        }
    }
    // Put the object in the gray stack.
    vm->gray_stack[vm->gray_count++] = obj;
}

void mark_value(VM* vm, Value value) {
    if (IS_OBJ(value)) mark_object(vm, VAL_TO_OBJ(value));
}

static void mark_roots(VM* vm) {
    // Mark the stack.
    for (Value* slot = vm->stack; slot != vm->stack_top; slot++) {
        mark_value(vm, *slot);
    }

    // Mark each closure on the call stack.
    for (int i = 0; i < vm->frame_count; i++) {
        mark_object(vm, (Obj*)vm->frames[i].closure);
    }

    // Mark the open upvalues list.
    for (ObjUpvalue* upvalue = vm->open_upvalues;
            upvalue != NULL;
            upvalue = upvalue->next)
        mark_object(vm, (Obj*)upvalue);

    // Mark the roots stack.
    for (int i = 0; i < vm->roots_count; i++)
        mark_value(vm, vm->roots[i]);

    // Mark the constants
    mark_value(vm, vm->getSlot_string);
    mark_value(vm, vm->setSlot_string);

    // Mark the *Protos
    mark_object(vm, (Obj*)vm->ObjectProto);
    mark_object(vm, (Obj*)vm->FnProto);
    mark_object(vm, (Obj*)vm->NativeProto);
    mark_object(vm, (Obj*)vm->NumberProto);
    mark_object(vm, (Obj*)vm->BooleanProto);
    mark_object(vm, (Obj*)vm->StringProto);

    table_mark(&vm->globals, vm);
    compiler_mark(vm->compiler, vm);
}

static void blacken_object(VM* vm, Obj* obj) {
#ifdef SUBTLE_DEBUG_TRACE_ALLOC
    printf("%p blacken ", (void*)obj);
    debug_print_value(OBJ_TO_VAL(obj));
    printf("\n");
#endif

    switch (obj->type) {
        case OBJ_STRING: break; // Nothing to do here.
        case OBJ_NATIVE: break; // Nothing to do here.
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)obj;
            chunk_mark(&function->chunk, vm);
            break;
        }
        case OBJ_UPVALUE:
            mark_value(vm, ((ObjUpvalue*)obj)->closed);
            break;
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)obj;
            mark_object(vm, (Obj*)closure->function);
            for (int i = 0; i < closure->upvalue_count; i++)
                mark_object(vm, (Obj*)closure->upvalues[i]);
            break;
        }
        case OBJ_OBJECT: {
            ObjObject* object = (ObjObject*)obj;
            mark_value(vm, object->proto);
            table_mark(&object->slots, vm);
            break;
        }
    }
}

static void trace_references(VM* vm) {
    while (vm->gray_count > 0) {
        Obj* obj = vm->gray_stack[--vm->gray_count];
        blacken_object(vm, obj);
    }
}

static void sweep(VM* vm) {
    Obj* prev = NULL;
    Obj* curr = vm->objects;
    while (curr != NULL) {
        if (curr->marked) {
            curr->marked = false;
            prev = curr;
            curr = curr->next;
        } else {
            Obj* unreached = curr;
            curr = curr->next;
            if (prev == NULL) {
                vm->objects = curr;
            } else {
                prev->next = curr;
            }
            object_free(unreached, vm);
        }
    }
}

void memory_collect(VM* vm) {
#ifdef SUBTLE_DEBUG_TRACE_ALLOC
    size_t before = vm->bytes_allocated;
    printf("-- gc begin\n");
#endif

    mark_roots(vm);
    trace_references(vm);
    table_remove_white(&vm->strings, vm);
    sweep(vm);

    vm->next_gc = vm->bytes_allocated * GC_HEAP_GROW_FACTOR;

#ifdef SUBTLE_DEBUG_TRACE_ALLOC
    printf("-- gc end\n");
    printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm->bytes_allocated,
           before,
           vm->bytes_allocated,
           vm->next_gc);
#endif
}
