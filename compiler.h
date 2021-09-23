#ifndef SUBTLE_COMPILER_H
#define SUBTLE_COMPILER_H

#include "chunk.h"
#include "common.h"
#include "lexer.h"
#include "vm.h"

#define MAX_LOCALS UINT8_MAX

typedef struct {
    Token name;
    int depth;
} Local;

typedef struct {
    Token previous; // the previously consumed token
    Token current;  // the token under inspection
    bool had_error;
    bool panic_mode;

    // Local variables
    Local locals[MAX_LOCALS];
    int local_count;
    int scope_depth; // Current scope depth.

    Chunk* chunk;
    Lexer lexer;
    VM* vm;
} Compiler;

// Note: compiler doesn't own the chunk.
void compiler_init(Compiler* compiler, VM* vm, Chunk* chunk, const char* source);
bool compiler_compile(Compiler* compiler);

#endif
