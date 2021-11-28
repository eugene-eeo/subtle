#ifndef SUBTLE_CHUNK_H
#define SUBTLE_CHUNK_H

#include "common.h"
#include "value.h"
#include "table.h"

enum OpCode {
    OP_RETURN,
    OP_CONSTANT,
    OP_POP,
    OP_TRUE,
    OP_FALSE,
    OP_NIL,
    OP_EQ,
    OP_NEQ,
    OP_NOT,
    OP_DEF_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_ASSERT,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_LOOP,
    OP_JUMP,
    OP_JUMP_IF_TRUE,
    OP_JUMP_IF_FALSE,
    OP_CLOSURE,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CLOSE_UPVALUE,
    OP_OBJECT,
    OP_OBJECT_SET,
    OP_OBJLIT_SET,
    OP_INVOKE,
};

typedef struct Chunk {
    uint8_t*   code;
    size_t*    lines;
    size_t     length;
    size_t     capacity;
    ValueArray constants;
    Table      constants_index;
} Chunk;

typedef struct VM VM;

void chunk_init(Chunk* chunk);
void chunk_done(Chunk* chunk, VM* vm);
void chunk_free(Chunk* chunk, VM* vm);
void chunk_write_byte(Chunk* chunk, VM* vm, uint8_t byte, size_t line);
void chunk_write_offset(Chunk* chunk, VM* vm, uint16_t offset, size_t line);
size_t chunk_get_line(Chunk* chunk, int offset);
size_t chunk_write_constant(Chunk* chunk, VM* vm, Value v);
void chunk_mark(Chunk* chunk, VM* vm);

#endif
