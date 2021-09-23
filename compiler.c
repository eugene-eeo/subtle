#include "compiler.h"
#include "lexer.h"
#include <stdio.h>  // fprintf
#include <stdlib.h>  // strtod

#ifdef SUBTLE_DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQ,         // ==, !=
    PREC_CMP,        // <, >, <=, >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // ()
    PREC_LITERAL,    // literals
} Precedence;

typedef void (*ParseFn)(Compiler*);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

void compiler_init(Compiler* compiler, VM* vm, Chunk* chunk, const char* source) {
    compiler->chunk = chunk;
    compiler->had_error = false;
    compiler->panic_mode = false;
    compiler->vm = vm;
    lexer_init(&compiler->lexer, source);
}

static void error_at(Compiler* compiler, Token* token, const char* message) {
    if (compiler->panic_mode) return;
    compiler->panic_mode = true;

    fprintf(stderr, "[line %zu] Error at ", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, "end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, "\"%.*s\"", (int)token->length, token->start);
    }

    fprintf(stderr, ": %s", message);
    compiler->had_error = true;
}

static void error(Compiler* compiler, const char* message)            { error_at(compiler, &compiler->previous, message); }
static void error_at_current(Compiler* compiler, const char* message) { error_at(compiler, &compiler->current, message); }

static void advance(Compiler* compiler) {
    compiler->previous = compiler->current;
    for (;;) {
        compiler->current = lexer_next(&compiler->lexer);
        if (compiler->current.type != TOKEN_ERROR) break;

        error_at_current(compiler, compiler->current.start);
    }
}

static void consume(Compiler* compiler, TokenType type, const char* message) {
    if (compiler->current.type == type) {
        advance(compiler);
        return;
    }

    error_at_current(compiler, message);
}

// ==================
// Bytecode utilities
// ==================

static Chunk* current_chunk(Compiler* compiler) {
    return compiler->chunk;
}

static void emit_byte(Compiler* compiler, uint8_t b) {
    chunk_write_byte(current_chunk(compiler), b, compiler->previous.line);
}

static void emit_offset(Compiler* compiler, uint16_t offset) {
    chunk_write_offset(current_chunk(compiler), offset, compiler->previous.line);
}

static uint16_t make_constant(Compiler* compiler, Value v) {
    size_t offset = chunk_write_constant(current_chunk(compiler), v);
    if (offset > UINT16_MAX) {
        error(compiler, "Too many constants in one chunk.");
        return 0;
    }
    return offset;
}

static void emit_constant(Compiler* compiler, Value v) {
    emit_byte(compiler, OP_CONSTANT);
    emit_offset(compiler, make_constant(compiler, v));
}

static void emit_return(Compiler* compiler) {
    emit_byte(compiler, OP_RETURN);
}

// ==================
// Expression parsing
// ==================

static void expression(Compiler*);
static void parse_precedence(Compiler*, Precedence);
static ParseRule* get_rule(TokenType);


static void string(Compiler* compiler) {
    ObjString* str = objstring_copy(
        compiler->vm,
        compiler->previous.start + 1,
        compiler->previous.length - 2
        );
    emit_constant(compiler, OBJECT_TO_VAL(str));
}

static void number(Compiler* compiler) {
    double value = strtod(compiler->previous.start, NULL);
    emit_constant(compiler, NUMBER_TO_VAL(value));
}

static void literal(Compiler* compiler) {
    switch (compiler->previous.type) {
        case TOKEN_TRUE:  emit_byte(compiler, OP_TRUE); break;
        case TOKEN_FALSE: emit_byte(compiler, OP_FALSE); break;
        case TOKEN_NIL:   emit_byte(compiler, OP_NIL); break;
        default: return; // Unreachable
    }
}

static void grouping(Compiler* compiler) {
    expression(compiler);
    consume(compiler, TOKEN_RPAREN, "Expect ')' after expression.");
}

static void unary(Compiler* compiler) {
    TokenType operator = compiler->previous.type;
    // Compile the operand.
    parse_precedence(compiler, PREC_UNARY);
    switch (operator) {
        case TOKEN_MINUS:
            emit_byte(compiler, OP_NEGATE);
            break;
        case TOKEN_BANG:
            emit_byte(compiler, OP_NOT);
            break;
        default: return; // Unreachable.
    }
}

static void binary(Compiler* compiler) {
    TokenType operator = compiler->previous.type;
    ParseRule* rule = get_rule(operator);
    parse_precedence(compiler, (Precedence)(rule->precedence + 1));

    switch (operator) {
        case TOKEN_PLUS:  emit_byte(compiler, OP_ADD); break;
        case TOKEN_MINUS: emit_byte(compiler, OP_SUBTRACT); break;
        case TOKEN_TIMES: emit_byte(compiler, OP_MULTIPLY); break;
        case TOKEN_SLASH: emit_byte(compiler, OP_DIVIDE); break;
        case TOKEN_EQ_EQ: emit_byte(compiler, OP_EQUAL); break;
        case TOKEN_BANG_EQ:
            emit_byte(compiler, OP_EQUAL);
            emit_byte(compiler, OP_NOT);
            break;
        case TOKEN_LT:    emit_byte(compiler, OP_LT); break;
        case TOKEN_LEQ:   emit_byte(compiler, OP_LEQ); break;
        case TOKEN_GT:    emit_byte(compiler, OP_GT); break;
        case TOKEN_GEQ:   emit_byte(compiler, OP_GEQ); break;
        default: return; // Unreachable.
    }
}

static ParseRule rules[] = {
    [TOKEN_PLUS]      = {NULL,     binary, PREC_TERM},
    [TOKEN_MINUS]     = {unary,    binary, PREC_TERM},
    [TOKEN_TIMES]     = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SLASH]     = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SEMICOLON] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LPAREN]    = {grouping, NULL,   PREC_NONE},
    [TOKEN_RPAREN]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LBRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RBRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQ]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQ_EQ]     = {NULL,     binary, PREC_EQ},
    [TOKEN_BANG]      = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQ]   = {NULL,     binary, PREC_EQ},
    [TOKEN_LT]        = {NULL,     binary, PREC_CMP},
    [TOKEN_LEQ]       = {NULL,     binary, PREC_CMP},
    [TOKEN_GT]        = {NULL,     binary, PREC_CMP},
    [TOKEN_GEQ]       = {NULL,     binary, PREC_CMP},
    [TOKEN_NUMBER]    = {number,   NULL,   PREC_NONE},
    [TOKEN_STRING]    = {string,   NULL,   PREC_NONE},
    [TOKEN_VARIABLE]  = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]       = {literal,  NULL,   PREC_NONE},
    [TOKEN_TRUE]      = {literal,  NULL,   PREC_NONE},
    [TOKEN_FALSE]     = {literal,  NULL,   PREC_NONE},
    [TOKEN_FN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_AND]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_OR]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LET]       = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]       = {NULL,     NULL,   PREC_NONE},
};

static void expression(Compiler* compiler) {
    parse_precedence(compiler, PREC_ASSIGNMENT);
}

static void parse_precedence(Compiler* compiler, Precedence prec) {
    advance(compiler);
    ParseFn prefix_rule = get_rule(compiler->previous.type)->prefix;
    if (prefix_rule == NULL) {
        error(compiler, "Expected an expression.");
        return;
    }

    prefix_rule(compiler);

    while (prec <= get_rule(compiler->current.type)->precedence) {
        advance(compiler);
        ParseFn infix_rule = get_rule(compiler->previous.type)->infix;
        infix_rule(compiler);
    }
}

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
}

bool compiler_compile(Compiler* compiler) {
    advance(compiler);
    expression(compiler);
    consume(compiler, TOKEN_EOF, "Expect end of expression.");
    emit_return(compiler);

#ifdef SUBTLE_DEBUG_PRINT_CODE
    if (!compiler->had_error) {
        debug_print_chunk(current_chunk(compiler), "script");
    }
#endif

    return !compiler->had_error;
}
