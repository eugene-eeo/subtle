#include "debug.h"
#include "value.h"
#include "object.h"

#include <stdio.h>

void debug_print_object(Obj* obj) {
    switch (obj->type) {
        case OBJ_STRING:
            printf("\"%s\"", ((ObjString*) obj)->chars);
            break;
        case OBJ_FUNCTION:
            if (((ObjFunction*)obj)->arity == -1) {
                printf("script");
            } else {
                printf("fn_%p", (void*)obj);
            }
            break;
        case OBJ_NATIVE:
            printf("native_%p", (void*)obj);
            break;
        case OBJ_CLOSURE:
            debug_print_object((Obj*)(((ObjClosure*) obj)->function));
            break;
        case OBJ_UPVALUE:
            printf("upvalue");
            break;
        case OBJ_OBJECT:
            printf("object_%p", (void*)obj);
            break;
    }
}

void debug_print_value(Value value) {
    switch (value.type) {
        case VALUE_UNDEFINED: printf("undefined"); break;
        case VALUE_NIL: printf("nil"); break;
        case VALUE_BOOL: printf(VAL_TO_BOOL(value) ? "true" : "false"); break;
        case VALUE_NUMBER: printf("%g", VAL_TO_NUMBER(value)); break;
        case VALUE_OBJ: debug_print_object(VAL_TO_OBJ(value)); break;
    }
}

void debug_print_chunk(Chunk* chunk) {
    for (size_t i = 0; i < chunk->length;) {
        i = debug_print_instruction(chunk, i);
    }
}

static size_t simple_instruction(size_t index, const char* name) {
    printf("%-16s\n", name);
    return index + 1;
}

static size_t constant_instruction(Chunk* chunk, size_t index, const char* name) {
    uint16_t offset = (uint16_t)(chunk->code[index + 1] << 8);
    offset |= chunk->code[index + 2];
    printf("%-16s %4d ", name, offset);
    debug_print_value(chunk->constants.values[offset]);
    printf("\n");
    return index + 3;
}

static size_t byte_instruction(Chunk* chunk, size_t index, const char* name) {
    uint8_t byte = (uint8_t)(chunk->code[index + 1]);
    printf("%-16s %4d\n", name, byte);
    return index + 2;
}

static size_t
jump_instruction(Chunk* chunk, size_t index, int direction, const char* name)
{
    uint16_t jump = (uint16_t)(chunk->code[index + 1] << 8);
    jump |= chunk->code[index + 2];
    printf("%-16s %4zu -> %zu\n", name, index,
           index + 3 + (long) direction * jump);
    return index + 3;
}

size_t debug_print_instruction(Chunk* chunk, size_t index) {
    printf("%04zu ", index);
    if (index > 0 && chunk_get_line(chunk, index-1) == chunk_get_line(chunk, index)) {
        printf("   | ");
    } else {
        printf("%4zu ", chunk_get_line(chunk, index));
    }
    switch (chunk->code[index]) {
        case OP_RETURN:   return simple_instruction(index, "OP_RETURN");
        case OP_CONSTANT: return constant_instruction(chunk, index, "OP_CONSTANT");
        case OP_POP:      return simple_instruction(index, "OP_POP");
        case OP_TRUE:     return simple_instruction(index, "OP_TRUE");
        case OP_FALSE:    return simple_instruction(index, "OP_FALSE");
        case OP_NIL:      return simple_instruction(index, "OP_NIL");
        case OP_DEF_GLOBAL: return constant_instruction(chunk, index, "OP_DEF_GLOBAL");
        case OP_GET_GLOBAL: return constant_instruction(chunk, index, "OP_GET_GLOBAL");
        case OP_SET_GLOBAL: return constant_instruction(chunk, index, "OP_SET_GLOBAL");
        case OP_ASSERT: return simple_instruction(index, "OP_ASSERT");
        case OP_GET_LOCAL:  return byte_instruction(chunk, index, "OP_GET_LOCAL");
        case OP_SET_LOCAL:  return byte_instruction(chunk, index, "OP_SET_LOCAL");
        case OP_LOOP:          return jump_instruction(chunk, index, -1, "OP_LOOP");
        case OP_JUMP:          return jump_instruction(chunk, index, +1, "OP_JUMP");
        case OP_JUMP_IF_FALSE: return jump_instruction(chunk, index, +1, "OP_JUMP_IF_FALSE");
        case OP_JUMP_IF_TRUE:  return jump_instruction(chunk, index, +1, "OP_JUMP_IF_TRUE");
        case OP_CLOSURE: {
            index++;
            uint16_t offset = (uint16_t)(chunk->code[index++] << 8);
            offset |= chunk->code[index++];
            printf("%-16s %4u ", "OP_CLOSURE", offset);
            debug_print_value(chunk->constants.values[offset]);
            printf("\n");

            ObjFunction* fn = VAL_TO_FUNCTION(chunk->constants.values[offset]);
            for (int j = 0; j < fn->upvalue_count; j++) {
                uint8_t is_local = chunk->code[index++];
                uint8_t upvalue_idx = chunk->code[index++];
                printf("%04zu    |                     %s %u\n",
                       index - 2, (is_local == 1) ? "local" : "upvalue", upvalue_idx);
            }
            return index;
        }
        case OP_GET_UPVALUE: return byte_instruction(chunk, index, "OP_GET_UPVALUE");
        case OP_SET_UPVALUE: return byte_instruction(chunk, index, "OP_SET_UPVALUE");
        case OP_CLOSE_UPVALUE: return simple_instruction(index, "OP_CLOSE_UPVALUE");
        case OP_OBJECT: return simple_instruction(index, "OP_OBJECT");
        case OP_OBJECT_SET: return constant_instruction(chunk, index, "OP_OBJECT_SET");
        case OP_OBJLIT_SET: return constant_instruction(chunk, index, "OP_OBJLIT_SET");
        case OP_INVOKE: {
            index++;
            uint16_t constant = (uint16_t)(chunk->code[index++] << 8);
            constant |= chunk->code[index++];
            printf("%-16s %4d ", "OP_INVOKE", constant);
            debug_print_value(chunk->constants.values[constant]);
            uint8_t num_args = chunk->code[index++];
            printf(" (%u args)", num_args);
            printf("\n");
            return index;
        }
        default:
            printf("Unknown instruction.\n");
            return index + 1;
    }
}
