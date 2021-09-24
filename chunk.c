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

void chunk_free(Chunk* chunk, VM* vm) {
    FREE_ARRAY(vm, chunk->code, uint8_t, chunk->capacity);
    FREE_ARRAY(vm, chunk->lines, size_t, chunk->capacity);
    valuearray_free(&chunk->constants, vm);
    table_free(&chunk->constants_index, vm);
    chunk_init(chunk);
}

void chunk_write_byte(Chunk* chunk, VM* vm, uint8_t byte, size_t line) {
    if (chunk->length + 1 > chunk->capacity) {
        size_t new_size = GROW_CAPACITY(chunk->capacity);
        chunk->code = GROW_ARRAY(vm, chunk->code, uint8_t, chunk->capacity, new_size);
        chunk->lines = GROW_ARRAY(vm, chunk->lines, size_t, chunk->capacity, new_size);
        chunk->capacity = new_size;
    }

    chunk->lines[chunk->length] = line;
    chunk->code[chunk->length] = byte;
    chunk->length++;
}

void chunk_write_offset(Chunk* chunk, VM* vm, uint16_t offset, size_t line) {
    chunk_write_byte(chunk, vm, (offset >> 8) & 0xFF, line);
    chunk_write_byte(chunk, vm, (offset)      & 0xFF, line);
}

size_t chunk_get_line(Chunk* chunk, int offset) {
    return chunk->lines[offset];
}

size_t chunk_write_constant(Chunk* chunk, VM* vm, Value value) {
    // Check if it already exists.
    Value offset;
    if (table_get(&chunk->constants_index, value, &offset))
        return (size_t)VAL_TO_NUMBER(offset);

    valuearray_write(&chunk->constants, vm, value);
    size_t rv = chunk->constants.length - 1;
    offset = NUMBER_TO_VAL(rv);
    table_set(&chunk->constants_index, vm, value, offset);
    return rv;
}
