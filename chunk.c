#include "chunk.h"
#include "memory.h"

void chunk_init(Chunk* chunk) {
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->length = 0;
    chunk->capacity = 0;
    valuearray_init(&chunk->constants);
    table_init(&chunk->constants_index);
}

void chunk_done(Chunk* chunk, VM* vm) {
    // Save some memory by freeing the constants_index, because we
    // won't be writing to it any more.
    table_free(&chunk->constants_index, vm);
}

void chunk_free(Chunk* chunk, VM* vm) {
    FREE_ARRAY(vm, chunk->code, uint8_t, chunk->capacity);
    FREE_ARRAY(vm, chunk->lines, uint32_t, chunk->capacity);
    valuearray_free(&chunk->constants, vm);
    // Just in case we didn't call chunk_done()
    table_free(&chunk->constants_index, vm);
    chunk_init(chunk);
}

void chunk_write_byte(Chunk* chunk, VM* vm, uint8_t byte, int line) {
    if (chunk->length + 1 > chunk->capacity) {
        int new_size = GROW_CAPACITY(chunk->capacity);
        chunk->code = GROW_ARRAY(vm, chunk->code, uint8_t, chunk->capacity, new_size);
        chunk->lines = GROW_ARRAY(vm, chunk->lines, uint32_t, chunk->capacity, new_size);
        chunk->capacity = new_size;
    }

    chunk->lines[chunk->length] = line;
    chunk->code[chunk->length] = byte;
    chunk->length++;
}

void chunk_write_offset(Chunk* chunk, VM* vm, uint16_t offset, int line) {
    chunk_write_byte(chunk, vm, (offset >> 8) & 0xFF, line);
    chunk_write_byte(chunk, vm, (offset)      & 0xFF, line);
}

int chunk_get_line(Chunk* chunk, int offset) {
    return chunk->lines[offset];
}

int chunk_write_constant(Chunk* chunk, VM* vm, Value value) {
    // Check if it already exists.
    Value offset;
    if (table_get(&chunk->constants_index, value, &offset))
        return (int)VAL_TO_NUMBER(offset);

    vm_push_root(vm, value);
    valuearray_write(&chunk->constants, vm, value);

    int rv = chunk->constants.length - 1;
    offset = NUMBER_TO_VAL(rv);
    table_set(&chunk->constants_index, vm, value, offset);
    vm_pop_root(vm);
    return rv;
}

void chunk_mark(Chunk* chunk, VM* vm) {
    valuearray_mark(&chunk->constants, vm);
}
