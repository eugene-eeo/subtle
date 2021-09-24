#include "compiler.h"
#include "lexer.h"
#include <stdio.h>  // fprintf
#include <stdlib.h>  // strtod
#include <string.h>

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

typedef void (*ParseFn)(Compiler* compiler, bool can_assign);

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
    compiler->local_count = 0;
    compiler->scope_depth = 0;
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

    fprintf(stderr, ": %s\n", message);
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

static bool check(Compiler* compiler, TokenType type) {
    return compiler->current.type == type;
}

static bool match(Compiler* compiler, TokenType type) {
    if (!check(compiler, type)) return false;
    advance(compiler);
    return true;
}

static void consume(Compiler* compiler, TokenType type, const char* message) {
    if (!match(compiler, type))
        error_at_current(compiler, message);
}

// Bytecode utilities
// ==================

static Chunk* current_chunk(Compiler* compiler) {
    return compiler->chunk;
}

static void emit_byte(Compiler* compiler, uint8_t b) {
    chunk_write_byte(current_chunk(compiler), compiler->vm, b, compiler->previous.line);
}

static void emit_offset(Compiler* compiler, uint16_t offset) {
    chunk_write_offset(current_chunk(compiler), compiler->vm, offset, compiler->previous.line);
}

static uint16_t make_constant(Compiler* compiler, Value v) {
    size_t offset = chunk_write_constant(current_chunk(compiler), compiler->vm, v);
    if (offset > UINT16_MAX) {
        error(compiler, "Too many constants in one chunk.");
        return 0;
    }
    return offset;
}

static uint16_t identifier_constant(Compiler* compiler, Token* token) {
    return make_constant(compiler,
        OBJECT_TO_VAL(objstring_copy(
            compiler->vm,
            token->start,
            token->length
        )));
}

static void emit_constant(Compiler* compiler, Value v) {
    emit_byte(compiler, OP_CONSTANT);
    emit_offset(compiler, make_constant(compiler, v));
}

static void emit_return(Compiler* compiler) {
    emit_byte(compiler, OP_RETURN);
}

// Scoping helpers
// ===============

static void begin_block(Compiler* compiler) {
    compiler->scope_depth++;
}

// Emits pop instructions to pop the current scope's locals off the stack.
static void end_block(Compiler* compiler) {
    compiler->scope_depth--;
    while (compiler->local_count > 0
           && compiler->locals[compiler->local_count - 1].depth
                > compiler->scope_depth) {
        emit_byte(compiler, OP_POP);
        compiler->local_count--;
    }
}

static void add_local(Compiler* compiler, Token name) {
    if (compiler->local_count == MAX_LOCALS) {
        error_at(compiler, &name, "Too many locals in one chunk.");
        return;
    }

    Local* local = &compiler->locals[compiler->local_count];
    local->name = name;
    local->depth = -1;
    compiler->local_count++;
}

static void mark_local_initialized(Compiler* compiler) {
    compiler->locals[compiler->local_count - 1].depth = compiler->scope_depth;
}

static bool
identifiers_equal(Token* a, Token* b)
{
    return a->length == b->length
        && memcmp(a->start, b->start, a->length) == 0;
}

// Is there a local with the given name?
// Returns -1 if there is no local, or the distance from the _top_ of
// the stack instead.
static int
resolve_local(Compiler* compiler, Token* token)
{
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiers_equal(&local->name, token)) {
            if (local->depth == -1)
                error_at(compiler, token, "Cannot read local variable in own initializer.");
            return i;
        }
    }
    return -1;
}

// Expression parsing
// ==================

static void expression(Compiler*);
static void parse_precedence(Compiler*, Precedence);
static ParseRule* get_rule(TokenType);


static void string(Compiler* compiler, bool can_assign) {
    ObjString* str = objstring_copy(
        compiler->vm,
        compiler->previous.start + 1,
        compiler->previous.length - 2
        );
    emit_constant(compiler, OBJECT_TO_VAL(str));
}

static void number(Compiler* compiler, bool can_assign) {
    double value = strtod(compiler->previous.start, NULL);
    emit_constant(compiler, NUMBER_TO_VAL(value));
}

static void literal(Compiler* compiler, bool can_assign) {
    switch (compiler->previous.type) {
        case TOKEN_TRUE:  emit_byte(compiler, OP_TRUE); break;
        case TOKEN_FALSE: emit_byte(compiler, OP_FALSE); break;
        case TOKEN_NIL:   emit_byte(compiler, OP_NIL); break;
        default: return; // Unreachable
    }
}

static void named_variable(Compiler* compiler, Token* name, bool can_assign) {
    // Check if we can resolve to a local variable.
    int local = resolve_local(compiler, name);
    if (local != -1) {
        if (can_assign && match(compiler, TOKEN_EQ)) {
            expression(compiler);
            emit_byte(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t) local);
        } else {
            emit_byte(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t) local);
        }
        return;
    }

    // Otherwise, it's a global.
    uint16_t global = identifier_constant(compiler, name);
    if (can_assign && match(compiler, TOKEN_EQ)) {
        expression(compiler);
        emit_byte(compiler, OP_SET_GLOBAL);
        emit_offset(compiler, global);
    } else {
        emit_byte(compiler, OP_GET_GLOBAL);
        emit_offset(compiler, global);
    }
}

static void variable(Compiler* compiler, bool can_assign) {
    named_variable(compiler, &compiler->previous, can_assign);
}

static void grouping(Compiler* compiler, bool can_assign) {
    expression(compiler);
    consume(compiler, TOKEN_RPAREN, "Expect ')' after expression.");
}

static void unary(Compiler* compiler, bool can_assign) {
    TokenType operator = compiler->previous.type;
    // Compile the operand.
    parse_precedence(compiler, PREC_UNARY);
    switch (operator) {
        case TOKEN_MINUS: emit_byte(compiler, OP_NEGATE); break;
        case TOKEN_BANG:  emit_byte(compiler, OP_NOT); break;
        default: return; // Unreachable.
    }
}

static void binary(Compiler* compiler, bool can_assign) {
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
    [TOKEN_VARIABLE]  = {variable, NULL,   PREC_NONE},
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

    bool can_assign = prec <= PREC_ASSIGNMENT;
    prefix_rule(compiler, can_assign);

    while (prec <= get_rule(compiler->current.type)->precedence) {
        advance(compiler);
        ParseFn infix_rule = get_rule(compiler->previous.type)->infix;
        infix_rule(compiler, can_assign);
    }
}

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
}

// Statement parsing
// =================
// There are two kinds of statements: declarations and `statements'.
// Declarations include the let statement; this allows us to make
// things like the following a syntactic error:
//
//     if (x) let u = 1; <--- why?

static void block(Compiler*);
static void declaration(Compiler*);
static void statement(Compiler*);

//
// Variable declaration is split into two stages: _declaring_, which
// marks its place in the stack (if it's a local variable), and _defining_,
// which emits bytecode that gives it a value.
//

static void declare_variable(Compiler* compiler) {
    int scope_depth = compiler->scope_depth;
    if (scope_depth == 0) return; // Nothing to do in global scope.

    Token* name = &compiler->previous;
    // Disallow the following in the same block:
    //    let a = 1;
    //    let a = 2;
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (local->depth != -1 && local->depth < scope_depth) break;
        if (identifiers_equal(&local->name, name)) {
            error_at(compiler, name, "Already a variable with this name in this scope.");
            break;
        }
    }
    add_local(compiler, *name);
}

static void define_variable(Compiler* compiler, uint16_t global) {
    // If there is no local scope at the moment, then there is
    // nothing to do: the local will be on the stack.
    if (compiler->scope_depth > 0) {
        mark_local_initialized(compiler);
        return;
    }

    emit_byte(compiler, OP_DEF_GLOBAL);
    emit_offset(compiler, global);
}

static uint16_t parse_variable(Compiler* compiler, const char* msg) {
    consume(compiler, TOKEN_VARIABLE, msg);

    declare_variable(compiler);
    if (compiler->scope_depth > 0)
        return 0;

    return identifier_constant(compiler, &compiler->previous);
}

static void let_decl(Compiler* compiler) {
    uint16_t global = parse_variable(compiler, "Expect variable name.");

    consume(compiler, TOKEN_EQ, "Expect '=' after variable name.");
    expression(compiler);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    define_variable(compiler, global);
}

static void assert_stmt(Compiler* compiler) {
    expression(compiler);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after expression.");
    emit_byte(compiler, OP_ASSERT);
}

static void block(Compiler* compiler) {
    begin_block(compiler);
    while (!check(compiler, TOKEN_EOF) && !check(compiler, TOKEN_RBRACE))
        declaration(compiler);
    end_block(compiler);
    consume(compiler, TOKEN_RBRACE, "Expect '}' after block.");
}

static void do_stmt(Compiler* compiler) {
    if (!match(compiler, TOKEN_LBRACE)) {
        error_at_current(compiler, "Expect '{' after do.");
        return;
    }
    block(compiler);
}

static void synchronize(Compiler* compiler) {
    compiler->panic_mode = false;
    while (compiler->current.type != TOKEN_EOF) {
        if (compiler->previous.type == TOKEN_SEMICOLON) return;
        switch (compiler->current.type) {
            case TOKEN_LET:
            case TOKEN_WHILE:
            case TOKEN_RETURN:
            case TOKEN_ASSERT:
            case TOKEN_IF:
            case TOKEN_DO:
                return;
            default:
                advance(compiler);
        }
    }
}

static void declaration(Compiler* compiler) {
    if (match(compiler, TOKEN_LET)) {
        let_decl(compiler);
    } else {
        statement(compiler);
    }

    if (compiler->panic_mode) synchronize(compiler);
}

static void statement(Compiler* compiler) {
    if (match(compiler, TOKEN_ASSERT)) {
        assert_stmt(compiler);
    } else if (match(compiler, TOKEN_DO)) {
        do_stmt(compiler);
    } else {
        // Expression statement
        expression(compiler);
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after expression.");
        emit_byte(compiler, OP_POP);
    }
}

bool compiler_compile(Compiler* compiler) {
    advance(compiler);
    while (!match(compiler, TOKEN_EOF))
        declaration(compiler);

    consume(compiler, TOKEN_EOF, "Expect end of expression.");
    emit_return(compiler);

#ifdef SUBTLE_DEBUG_PRINT_CODE
    if (!compiler->had_error) {
        debug_print_chunk(current_chunk(compiler), "script");
    }
#endif

    return !compiler->had_error;
}
