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
    OP_DEF_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_ASSERT,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_LOOP,
    OP_JUMP,
    OP_JUMP_IF_FALSE, // Pop, then jump
    OP_OR,  // Jump if the top is true, else pop
    OP_AND, // Jump if the top is false, else pop
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
    int*       lines;
    int        length;
    int        capacity;
    ValueArray constants;
    Table      constants_index;
} Chunk;

typedef struct VM VM;

void chunk_init(Chunk* chunk);
void chunk_done(Chunk* chunk, VM* vm);
void chunk_free(Chunk* chunk, VM* vm);
void chunk_write_byte(Chunk* chunk, VM* vm, uint8_t byte, int line);
void chunk_write_offset(Chunk* chunk, VM* vm, uint16_t offset, int line);
int chunk_get_line(Chunk* chunk, int offset);
int chunk_write_constant(Chunk* chunk, VM* vm, Value v);
void chunk_mark(Chunk* chunk, VM* vm);

#endif
