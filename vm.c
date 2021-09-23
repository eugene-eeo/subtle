#include "compiler.h"
#include "memory.h"
#include "vm.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef SUBTLE_DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

void vm_init(VM* vm) {
    vm->stack_top = vm->stack;
    vm->objects = NULL;
    vm->chunk = NULL;
    vm->ip = NULL;
    table_init(&vm->strings, vm);
    table_init(&vm->globals, vm);
}

void vm_free(VM* vm) {
    Object* obj = vm->objects;
    while (obj != NULL) {
        Object* next = obj->next;
        object_free(obj);
        obj = next;
    }
    table_free(&vm->strings);
    table_free(&vm->globals);
    vm_init(vm);
}

void vm_push(VM* vm, Value value) {
    *vm->stack_top = value;
    vm->stack_top++;
}

Value vm_pop(VM* vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

Value vm_peek(VM* vm, int distance) {
    return vm->stack_top[-1 - distance];
}

// Resets the VM's stack pointer
static void vm_reset_stack(VM* vm) {
    vm->stack_top = vm->stack;
}

static void runtime_error(VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vm->ip - vm->chunk->code;
    fprintf(stderr,
            "[line %zu] in script\n",
            chunk_get_line(vm->chunk, instruction));
}

static InterpretResult run(VM* vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_SHORT() \
    (vm->ip += 2, \
     (uint16_t)((vm->ip[-2] << 8) | vm->ip[-1]))
#define READ_CONSTANT() (vm->chunk->constants.values[READ_SHORT()])
#define BINARY_OP(value_type, op) \
    do { \
        if (!IS_NUMBER(vm_peek(vm, 0)) || !IS_NUMBER(vm_peek(vm, 1))) { \
            runtime_error(vm, "invalid types for " #op); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        Value b = vm_pop(vm); \
        Value a = vm_pop(vm); \
        vm_push(vm, value_type(VAL_TO_NUMBER(a) op VAL_TO_NUMBER(b))); \
    } while(false)

    for (;;) {
        uint8_t instruction;
#ifdef SUBTLE_DEBUG_TRACE_EXECUTION
        // Trace the stack.
        for (Value* vptr = vm->stack; vptr != vm->stack_top; vptr++) {
            printf("[ ");
            debug_print_value(*vptr);
            printf(" ]");
        }
        printf("\n");
        // Trace the about-to-be-executed instruction.
        debug_print_instruction(vm->chunk,
                                (int)(vm->ip - vm->chunk->code));
#endif
        switch (instruction = READ_BYTE()) {
            case OP_RETURN: {
                return INTERPRET_OK;
            }
            case OP_CONSTANT: {
                vm_push(vm, READ_CONSTANT());
                break;
            }
            case OP_POP: vm_pop(vm); break;
            case OP_ADD: {
                Value b = vm_peek(vm, 0);
                Value a = vm_peek(vm, 1);
                if (IS_NUMBER(a) && IS_NUMBER(b)) {
                    vm_pop(vm);
                    vm_pop(vm);
                    vm_push(vm, NUMBER_TO_VAL(VAL_TO_NUMBER(a) + VAL_TO_NUMBER(b)));
                } else if (IS_STRING(a) && IS_STRING(b)) {
                    ObjString* result = objstring_concat(vm,
                        VAL_TO_STRING(a),
                        VAL_TO_STRING(b)
                    );
                    vm_pop(vm);
                    vm_pop(vm);
                    vm_push(vm, OBJECT_TO_VAL(result));
                } else {
                    runtime_error(vm, "invalid types for +");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_TO_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_TO_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_TO_VAL, /); break;
            case OP_EQUAL: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                vm_push(vm, BOOL_TO_VAL(value_equal(a, b)));
                break;
            }
            case OP_NEGATE: {
                Value a = vm_pop(vm);
                if (!IS_NUMBER(a)) {
                    runtime_error(vm, "invalid type for negation");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_push(vm, NUMBER_TO_VAL(-VAL_TO_NUMBER(a)));
                break;
            }
            case OP_NOT: {
                Value a = vm_pop(vm);
                vm_push(vm, BOOL_TO_VAL(!value_truthy(a)));
                break;
            }
            case OP_LT:  BINARY_OP(BOOL_TO_VAL, <); break;
            case OP_GT:  BINARY_OP(BOOL_TO_VAL, >); break;
            case OP_LEQ: BINARY_OP(BOOL_TO_VAL, <=); break;
            case OP_GEQ: BINARY_OP(BOOL_TO_VAL, >=); break;
            case OP_TRUE:  vm_push(vm, BOOL_TO_VAL(true)); break;
            case OP_FALSE: vm_push(vm, BOOL_TO_VAL(false)); break;
            case OP_NIL:   vm_push(vm, NIL_VAL); break;
            case OP_DEF_GLOBAL: {
                Value name = READ_CONSTANT();
                table_set(&vm->globals, name, vm_peek(vm, 0));
                vm_pop(vm);
                break;
            }
            case OP_GET_GLOBAL: {
                Value name = READ_CONSTANT();
                Value value;
                if (!table_get(&vm->globals, name, &value)) {
                    runtime_error(vm, "Undefined variable '%s'.",
                                  VAL_TO_STRING(name)->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_push(vm, value);
                break;
            }
            case OP_SET_GLOBAL: {
                Value name = READ_CONSTANT();
                if (table_set(&vm->globals, name, vm_peek(vm, 0))) {
                    table_delete(&vm->globals, name);
                    runtime_error(vm, "Undefined variable '%s'.",
                                  VAL_TO_STRING(name)->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_ASSERT: {
                if (!value_truthy(vm_peek(vm, 0))) {
                    runtime_error(vm, "Assertion failed.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm_pop(vm);
                break;
            }
            case OP_GET_LOCAL: vm_push(vm, vm->stack[READ_BYTE()]); break;
            case OP_SET_LOCAL: {
                vm->stack[READ_BYTE()] = vm_peek(vm, 0);
                vm_pop(vm);
                break;
            }
            default: UNREACHABLE();
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult vm_interpret(VM* vm, const char* source) {
    InterpretResult result;
    Chunk chunk;
    chunk_init(&chunk);

    Compiler* compiler = ALLOCATE(Compiler, 1);
    compiler_init(compiler, vm, &chunk, source);

    if (compiler_compile(compiler)) {
        vm->chunk = &chunk;
        vm->ip = chunk.code;
        result = run(vm);
        vm_reset_stack(vm);
    } else {
        result = INTERPRET_COMPILE_ERROR;
    }

    FREE(Compiler, compiler);
    chunk_free(&chunk);
    return result;
}
