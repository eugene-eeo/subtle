#ifndef SUBTLE_DEBUG_H
#define SUBTLE_DEBUG_H

#include "common.h"
#include "chunk.h"

void debug_print_object(Obj* obj);
void debug_print_value(Value value);
void debug_print_chunk(Chunk* chunk);
size_t debug_print_instruction(Chunk* chunk, size_t offset);

#endif
