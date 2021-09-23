#include <stdio.h>

#include "debug.h"

void debug_print_value(Value value) {
    switch (value.type) {
        case VALUE_UNDEFINED: printf("undefined"); break;
        case VALUE_NIL: printf("nil"); break;
        case VALUE_BOOL: printf(VAL_TO_BOOL(value) ? "true" : "false"); break;
        case VALUE_NUMBER: printf("%g", VAL_TO_NUMBER(value)); break;
        case VALUE_OBJECT: {
            switch (OBJECT_TYPE(value)) {
                case OBJECT_STRING:
                    printf("%s", VAL_TO_STRING(value)->chars);
                    break;
            }
        }
    }
}

void debug_print_chunk(Chunk* chunk, const char* name) {
    printf("==== %s ====\n", name);
    for (size_t i = 0; i < chunk->length;) {
        i = debug_print_instruction(chunk, i);
    }
}

static int simple_instruction(int index, const char* name) {
    printf("%16s\n", name);
    return index + 1;
}

static int constant_instruction(Chunk* chunk, int index, const char* name) {
    uint16_t offset = (uint16_t)(chunk->code[index + 1] << 8);
    offset |= chunk->code[index + 2];
    printf("%16s %4d ", name, offset);
    debug_print_value(chunk->constants.values[offset]);
    printf("\n");
    return index + 3;
}

int debug_print_instruction(Chunk* chunk, int index) {
    if (index > 0 && chunk_get_line(chunk, index-1) == chunk_get_line(chunk, index)) {
        printf("   |");
    } else {
        printf("%4zu", chunk_get_line(chunk, index));
    }
    switch (chunk->code[index]) {
        case OP_RETURN:
            return simple_instruction(index, "OP_RETURN");
        case OP_CONSTANT:
            return constant_instruction(chunk, index, "OP_CONSTANT");
        case OP_POP:      return simple_instruction(index, "OP_POP");
        case OP_ADD:      return simple_instruction(index, "OP_ADD");
        case OP_SUBTRACT: return simple_instruction(index, "OP_SUBTRACT");
        case OP_MULTIPLY: return simple_instruction(index, "OP_MULTIPLY");
        case OP_DIVIDE:   return simple_instruction(index, "OP_DIVIDE");
        case OP_EQUAL:    return simple_instruction(index, "OP_EQUAL");
        case OP_NEGATE:   return simple_instruction(index, "OP_NEGATE");
        case OP_NOT:      return simple_instruction(index, "OP_NOT");
        case OP_LT:       return simple_instruction(index, "OP_LT");
        case OP_GT:       return simple_instruction(index, "OP_GT");
        case OP_LEQ:      return simple_instruction(index, "OP_LEQ");
        case OP_GEQ:      return simple_instruction(index, "OP_GEQ");
        case OP_TRUE:     return simple_instruction(index, "OP_TRUE");
        case OP_FALSE:    return simple_instruction(index, "OP_FALSE");
        case OP_NIL:      return simple_instruction(index, "OP_NIL");
        default:
            printf("Unknown instruction.\n");
            return index + 1;
    }
}
