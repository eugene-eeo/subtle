#include "chunk.h"
#include "memory.h"

void chunk_init(Chunk* chunk) {
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->length = 0;
    chunk->capacity = 0;
    valuearray_init(&chunk->constants);
}

void chunk_free(Chunk* chunk) {
    FREE_ARRAY(chunk->code, uint8_t, chunk->capacity);
    FREE_ARRAY(chunk->lines, size_t, chunk->capacity);
    valuearray_free(&chunk->constants);
    chunk_init(chunk);
}

void chunk_write_byte(Chunk* chunk, uint8_t byte, size_t line) {
    if (chunk->length + 1 > chunk->capacity) {
        size_t new_size = GROW_CAPACITY(chunk->capacity);
        chunk->code = GROW_ARRAY(chunk->code, uint8_t, chunk->capacity, new_size);
        chunk->lines = GROW_ARRAY(chunk->lines, size_t, chunk->capacity, new_size);
        chunk->capacity = new_size;
    }

    chunk->lines[chunk->length] = line;
    chunk->code[chunk->length] = byte;
    chunk->length++;
}

void chunk_write_offset(Chunk* chunk, uint16_t offset, size_t line) {
    chunk_write_byte(chunk, (offset >> 8) & 0xFF, line);
    chunk_write_byte(chunk, (offset)      & 0xFF, line);
}

size_t chunk_get_line(Chunk* chunk, int offset) {
    return chunk->lines[offset];
}

size_t chunk_write_constant(Chunk* chunk, Value value) {
    valuearray_write(&chunk->constants, value);
    return chunk->constants.length - 1;
}
