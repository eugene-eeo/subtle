#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "lexer.h"
#include "vm.h"
#include "memory.h"

#include <stdio.h>  // fprintf
#include <stdlib.h>  // strtod
#include <string.h>

#ifdef SUBTLE_DEBUG_PRINT_CODE
#include "debug.h"
#endif

#define MAX_LOCALS   UINT8_MAX
#define MAX_UPVALUES UINT8_MAX

typedef enum {
    FUNCTION_TYPE_SCRIPT,
    FUNCTION_TYPE_FUNCTION,
} FunctionType;

typedef struct {
    Token previous;
    Token current;
    Lexer lexer;

    bool had_error;
    bool panic_mode;
} Parser;

typedef struct Loop {
    // Design:
    // 1. Inject a 'well-known' OP_JUMP where we can break from the loop.
    // 2. Skip over the OP_JUMP from (1).
    // 3. Do the actual looping.
    struct Loop* outer;
    int depth; // Initial depth of the loop.
    size_t break_jump; // Loop break OP_JUMP.
    size_t cond_jump;  // Loop condition OP_JUMP_IF_FALSE.
    size_t start; // Where should the loop jump back to?
} Loop;

typedef struct {
    Token name;
    int depth;
    // Is this local captured by any upvalues?
    // If it is captured, this means we cannot pop this local
    // off the stack -- otherwise, a closure that depends on
    // this upvalue may exhibit undefined behaviour.
    bool is_captured;
} Local;

typedef struct {
    uint8_t index;
    bool is_local;
} Upvalue;

typedef struct Compiler {
    Compiler* enclosing;
    Parser* parser;

    // Where are we compiling to?
    ObjFunction* function;
    FunctionType type;

    // Keep track of the number of stack slots currently
    int slot_count;

    // Scoping
    Local locals[MAX_LOCALS];
    int local_count;
    Upvalue upvalues[MAX_UPVALUES];
    int scope_depth; // Current scope depth.
    Loop* loop;

    VM* vm;
} Compiler;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // ||
    PREC_AND,        // &&
    PREC_BITWISE_OR, // |
    PREC_BITWISE_AND,// &
    PREC_EQ,         // ==, !=
    PREC_CMP,        // <, >, <=, >=
    PREC_RANGE,      // .., ...
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_PREFIX,     // ! -
    PREC_POSTFIX,    // method calls
    PREC_CALL,       // (), .
    PREC_LITERAL,    // literals
} Precedence;

typedef void (*ParseFn)(Compiler* compiler, bool can_assign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

static void
parser_init(Parser* parser, const char* source)
{
    lexer_init(&parser->lexer, source);
    parser->had_error = false;
    parser->panic_mode = false;
}

static void
compiler_init(Compiler* compiler, Compiler* enclosing,
              Parser* parser, VM* vm, FunctionType type)
{
    compiler->enclosing = enclosing;
    compiler->parser = parser;
    compiler->function = objfunction_new(vm);
    if (type == FUNCTION_TYPE_SCRIPT)
        compiler->function->arity = -1;

    compiler->type = type;
    compiler->slot_count = 1; // the initial local
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->vm = vm;
    compiler->loop = NULL;

    Local* local = &compiler->locals[compiler->local_count++];
    local->depth = 0;
    local->is_captured = false;
    local->name.start = "";
    local->name.length = 0;

    vm->compiler = compiler;
}

static void error_at(Compiler* compiler, Token* token, const char* message) {
    if (compiler->parser->panic_mode) return;
    compiler->parser->panic_mode = true;

    fprintf(stderr, "[line %zu] Error at ", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, "end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, "\"%.*s\"", (int)token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    compiler->parser->had_error = true;
}

static void error(Compiler* compiler, const char* message)            { error_at(compiler, &compiler->parser->previous, message); }
static void error_at_current(Compiler* compiler, const char* message) { error_at(compiler, &compiler->parser->current, message); }

static void advance(Compiler* compiler) {
    compiler->parser->previous = compiler->parser->current;
    for (;;) {
        compiler->parser->current = lexer_next(&compiler->parser->lexer);
        if (compiler->parser->current.type != TOKEN_ERROR) break;

        error_at_current(compiler, compiler->parser->current.start);
    }
}

static bool check(Compiler* compiler, TokenType type) {
    return compiler->parser->current.type == type;
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

static bool match_slot(Compiler* compiler) {
    if (check(compiler, TOKEN_VARIABLE)
            || check(compiler, TOKEN_TRUE)  || check(compiler, TOKEN_FALSE) || check(compiler, TOKEN_NIL)
            || check(compiler, TOKEN_THIS)
            || check(compiler, TOKEN_PLUS)  || check(compiler, TOKEN_MINUS)
            || check(compiler, TOKEN_TIMES) || check(compiler, TOKEN_SLASH)
            || check(compiler, TOKEN_PIPE)
            || check(compiler, TOKEN_AMP)
            || check(compiler, TOKEN_BANG)
            || check(compiler, TOKEN_EQ_EQ) || check(compiler, TOKEN_BANG_EQ)
            || check(compiler, TOKEN_LT)    || check(compiler, TOKEN_LEQ)
            || check(compiler, TOKEN_GT)    || check(compiler, TOKEN_GEQ)) {
        advance(compiler);
        return true;
    }
    return false;
}

static void consume_slot(Compiler* compiler, const char* message) {
    if (!match_slot(compiler))
        error_at_current(compiler, message);
}

// Bytecode utilities
// ==================

static Chunk* current_chunk(Compiler* compiler) {
    return &compiler->function->chunk;
}

static void emit_byte(Compiler* compiler, uint8_t b) {
    chunk_write_byte(current_chunk(compiler), compiler->vm, b, compiler->parser->previous.line);
}

static int stack_effects[] = {
    [OP_RETURN] = -1,
    [OP_CONSTANT] = 1,
    [OP_POP] = -1,
    [OP_TRUE] = 1,
    [OP_FALSE] = 1,
    [OP_NIL] = 1,
    [OP_DEF_GLOBAL] = -1,
    [OP_GET_GLOBAL] = 1,
    [OP_SET_GLOBAL] = 0,
    [OP_ASSERT] = -1,
    [OP_GET_LOCAL] = 1,
    [OP_SET_LOCAL] = 0,
    [OP_LOOP] = 0,
    [OP_JUMP] = 0,
    [OP_JUMP_IF_FALSE] = -1,
    [OP_OR] = -1,
    [OP_AND] = -1,
    [OP_CLOSURE] = 1,
    [OP_GET_UPVALUE] = 1,
    [OP_SET_UPVALUE] = 0,
    [OP_CLOSE_UPVALUE] = -1,
    [OP_OBJECT] = 1,
    [OP_OBJECT_SET] = -1,
    [OP_OBJLIT_SET] = -1,
    [OP_INVOKE] = 0,
};

static void emit_op(Compiler* compiler, uint8_t op) {
    emit_byte(compiler, op);
    compiler->slot_count += stack_effects[op];
    if (!compiler->parser->had_error)
        ASSERT(compiler->slot_count >= 1, "compiler->slot_count < 1");
    if (compiler->function->max_slots < compiler->slot_count)
        compiler->function->max_slots = compiler->slot_count;
}

static void emit_offset(Compiler* compiler, uint16_t offset) {
    chunk_write_offset(current_chunk(compiler), compiler->vm, offset, compiler->parser->previous.line);
}

static uint16_t make_constant(Compiler* compiler, Value v) {
    size_t offset = chunk_write_constant(current_chunk(compiler), compiler->vm, v);
    if (offset > UINT16_MAX) {
        error(compiler, "Too many constants in one chunk.");
        return 0;
    }
    return offset;
}

static uint16_t identifier_constant(Compiler* compiler, const Token* token) {
    return make_constant(compiler,
        OBJ_TO_VAL(objstring_copy(
            compiler->vm,
            token->start,
            token->length
        )));
}

static void emit_constant(Compiler* compiler, Value v) {
    // Subtle point: the make_constant call has to come _before_ the
    // call to emit_byte(), because v might be freed during emit_byte.
    // make_constant() calls chunk_write_constant, which saves the
    // constant to the VM's root stack.
    uint16_t constant = make_constant(compiler, v);
    emit_op(compiler, OP_CONSTANT);
    emit_offset(compiler, constant);
}

static void emit_return(Compiler* compiler) {
    emit_op(compiler, OP_NIL);
    emit_op(compiler, OP_RETURN);
}

static ObjFunction*
compiler_end(Compiler* compiler)
{
    emit_return(compiler);
    compiler->vm->compiler = compiler->enclosing;
#ifdef SUBTLE_DEBUG_PRINT_CODE
    if (!compiler->parser->had_error) {
        printf("== ");
        if (compiler->type == FUNCTION_TYPE_SCRIPT) {
            printf("script");
        } else {
            printf("fn_%p", (void*)compiler->function);
        }
        printf(" [c=%d]", compiler->slot_count);
        printf(" [m=%d]", compiler->function->max_slots);
        printf(" ==\n");
        debug_print_chunk(current_chunk(compiler));
    }
#endif
    chunk_done(current_chunk(compiler), compiler->vm);
    return compiler->function;
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
        if (compiler->locals[compiler->local_count - 1].is_captured) {
            emit_op(compiler, OP_CLOSE_UPVALUE);
        } else {
            emit_op(compiler, OP_POP);
        }
        compiler->local_count--;
    }
}

// Similar to end_block, but without actually modifying the compiler.
static void pop_to_scope(Compiler* compiler, int scope_depth) {
    int local_count = compiler->local_count;
    ASSERT(compiler->scope_depth >= scope_depth, "compiler->scope_depth < scope_depth");
    while (local_count > 0
           && compiler->locals[local_count - 1].depth > scope_depth) {
        // use emit_byte instead of emit_op here since
        // we don't want to change slot_count
        if (compiler->locals[local_count - 1].is_captured) {
            emit_byte(compiler, OP_CLOSE_UPVALUE);
        } else {
            emit_byte(compiler, OP_POP);
        }
        local_count--;
    }
}

static int add_local(Compiler* compiler, Token name) {
    Local* local = &compiler->locals[compiler->local_count];
    local->name = name;
    local->depth = -1;
    local->is_captured = false;
    return compiler->local_count++;
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

static int
add_upvalue(Compiler* compiler, uint8_t index, bool is_local)
{
    int count = compiler->function->upvalue_count;

    for (int i = 0; i < count; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local)
            return i;
    }

    if (count == MAX_UPVALUES) {
        error(compiler, "Too many upvalues.");
        return 0;
    }

    compiler->upvalues[count].is_local = is_local;
    compiler->upvalues[count].index = index;
    compiler->function->upvalue_count++;
    return count;
}

// Is there an upvalue with the given name? An upvalue is a local
// variable defined on the outer scope.
static int
resolve_upvalue(Compiler* compiler, Token* token)
{
    if (compiler->enclosing == NULL) return -1;

    int local = resolve_local(compiler->enclosing, token);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolve_upvalue(compiler->enclosing, token);
    if (upvalue != -1)
        return add_upvalue(compiler, (uint8_t)upvalue, false);

    return -1;
}

// Jumping helpers
// ===============

static size_t
emit_jump(Compiler* compiler, uint8_t op)
{
    emit_op(compiler, op);
    emit_offset(compiler, 0xFFFF);
    // Return the index to the start of the offset.
    return current_chunk(compiler)->length - 2;
}

static void
patch_jump(Compiler* compiler, size_t offset)
{
    // Compute how many bytes we need to jump over -- accounting
    // for the 2-byte offset after the jump instruction.
    //
    // | OP_JUMP | 0 | 0 | .... | OP_POP |  |
    //             ^-- offset              ^-- chunk->length
    size_t jump = current_chunk(compiler)->length - offset - 2;
    if (jump > UINT16_MAX)
        error(compiler, "Too much code to jump over.");

    current_chunk(compiler)->code[offset    ] = (jump >> 8) & 0xFF;
    current_chunk(compiler)->code[offset + 1] = (jump)      & 0xFF;
}

static void
emit_loop(Compiler* compiler, size_t start)
{
    // It's important that this line is here, otherwise we would jump
    // over the wrong number of bytes: the bottom line assumes that
    // we would skip over only the 2 byte argument.
    emit_op(compiler, OP_LOOP);

    // Have to +2 here to account for the VM reading the 2 byte argument.
    size_t jump = current_chunk(compiler)->length - start + 2;
    if (jump > UINT16_MAX)
        error(compiler, "Too much code to jump over.");

    emit_offset(compiler, (uint16_t)jump);
}

// Invoke helpers
// ==============

static void
invoke_token_method(Compiler* compiler, const Token* tok, int num_args)
{
    uint16_t method_constant = identifier_constant(compiler, tok);
    emit_op(compiler, OP_INVOKE);
    emit_offset(compiler, method_constant);
    emit_byte(compiler, (uint8_t) num_args);
    // OP_INVOKE would pop the arguments, and leave the result.
    // Decrement the slot count accordingly.
    ASSERT(compiler->slot_count > num_args, "compiler->slot_count <= num_args");
    compiler->slot_count -= num_args;
}

static void
invoke_string_method(Compiler* compiler, const char* method, int num_args)
{
    const Token tok = {.start=method, .length=strlen(method)};
    invoke_token_method(compiler, &tok, num_args);
}

// Expression parsing
// ==================

static void block(Compiler*);
static uint16_t parse_variable(Compiler*, const char* msg);
static void define_variable(Compiler*, uint16_t);
static void expression(Compiler*);
static void parse_precedence(Compiler*, Precedence);
static ParseRule* get_rule(TokenType);


static void string(Compiler* compiler, bool can_assign) {
    ObjString* str = objstring_copy(
        compiler->vm,
        compiler->parser->previous.start + 1,
        compiler->parser->previous.length - 2
        );
    emit_constant(compiler, OBJ_TO_VAL(str));
}

static void number(Compiler* compiler, bool can_assign) {
    double value = strtod(compiler->parser->previous.start, NULL);
    emit_constant(compiler, NUMBER_TO_VAL(value));
}

static void literal(Compiler* compiler, bool can_assign) {
    switch (compiler->parser->previous.type) {
        case TOKEN_TRUE:  emit_op(compiler, OP_TRUE); break;
        case TOKEN_FALSE: emit_op(compiler, OP_FALSE); break;
        case TOKEN_NIL:   emit_op(compiler, OP_NIL); break;
        default: UNREACHABLE();
    }
}

static void and_(Compiler* compiler, bool can_assign) {
    size_t else_jump = emit_jump(compiler, OP_AND);
    parse_precedence(compiler, PREC_AND);
    patch_jump(compiler, else_jump);
}

static void or_(Compiler* compiler, bool can_assign) {
    size_t else_jump = emit_jump(compiler, OP_OR);
    parse_precedence(compiler, PREC_OR);
    patch_jump(compiler, else_jump);
}

static void named_variable(Compiler* compiler, Token* name, bool can_assign) {
    // Check if we can resolve to a local variable.
    int local = resolve_local(compiler, name);
    if (local != -1) {
        if (can_assign && match(compiler, TOKEN_EQ)) {
            expression(compiler);
            emit_op(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t) local);
        } else {
            emit_op(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t) local);
        }
        return;
    }

    // Check if we can resolve it as an upvalue.
    int upvalue = resolve_upvalue(compiler, name);
    if (upvalue != -1) {
        if (can_assign && match(compiler, TOKEN_EQ)) {
            expression(compiler);
            emit_op(compiler, OP_SET_UPVALUE);
            emit_byte(compiler, (uint8_t) upvalue);
        } else {
            emit_op(compiler, OP_GET_UPVALUE);
            emit_byte(compiler, (uint8_t) upvalue);
        }
        return;
    }

    // Otherwise, it's a global.
    uint16_t global = identifier_constant(compiler, name);
    if (can_assign && match(compiler, TOKEN_EQ)) {
        expression(compiler);
        emit_op(compiler, OP_SET_GLOBAL);
        emit_offset(compiler, global);
    } else {
        emit_op(compiler, OP_GET_GLOBAL);
        emit_offset(compiler, global);
    }
}

static void variable(Compiler* compiler, bool can_assign) {
    named_variable(compiler, &compiler->parser->previous, can_assign);
}

static void object(Compiler* compiler, bool can_assign) {
    // Object literal.
    emit_op(compiler, OP_OBJECT);
    if (!check(compiler, TOKEN_RBRACE)) {
        do {
            consume_slot(compiler, "Expect a slot name.");
            uint16_t constant = identifier_constant(compiler, &compiler->parser->previous);
            consume(compiler, TOKEN_COLON, "Expect ':' after slot name.");
            expression(compiler);
            emit_op(compiler, OP_OBJLIT_SET);
            emit_offset(compiler, constant);
        } while (match(compiler, TOKEN_COMMA));
    }
    consume(compiler, TOKEN_RBRACE, "Expect '}' after items.");
}

static void block_argument(Compiler* compiler) {
    Compiler c;
    compiler_init(&c, compiler, compiler->parser,
                  compiler->vm, FUNCTION_TYPE_FUNCTION);
    begin_block(&c);

    // Parse the optional parameter list, if any.
    if (match(&c, TOKEN_PIPE)) {
        do {
            c.function->arity++;
            if (c.function->arity > 255) {
                error_at_current(&c, "Cannot have more than 255 parameters.");
            }
            uint8_t constant = parse_variable(&c, "Expect parameter name.");
            define_variable(&c, constant);
            c.slot_count++;
        } while (match(&c, TOKEN_COMMA));
        consume(&c, TOKEN_PIPE, "Expect '|' after parameters.");
    }
    block(&c);
    end_block(&c);

    ObjFunction* fn = compiler_end(&c);
    uint16_t idx = make_constant(compiler, OBJ_TO_VAL(fn));
    emit_op(compiler, OP_CLOSURE);
    emit_offset(compiler, idx);

    for (int i = 0; i < fn->upvalue_count; i++) {
        emit_byte(compiler, c.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler, c.upvalues[i].index);
    }
}

static void invoke(Compiler* compiler, bool can_assign) {
    const Token op_token = compiler->parser->previous;

    if (can_assign && match(compiler, TOKEN_EQ)) {
        expression(compiler);
        emit_op(compiler, OP_OBJECT_SET);
        emit_offset(compiler, identifier_constant(compiler, &op_token));
        return;
    }

    uint8_t num_args = 0;
    // Match the optional arguments.
    if (match(compiler, TOKEN_LPAREN)) {
        if (!check(compiler, TOKEN_RPAREN)) {
            do {
                if (num_args == 255)
                    error(compiler, "Cannot have more than 255 arguments.");
                expression(compiler);
                num_args++;
            } while (match(compiler, TOKEN_COMMA));
        }
        consume(compiler, TOKEN_RPAREN, "Expect ')' after arguments.");
    }
    // Match a function block at the end.
    if (match(compiler, TOKEN_LBRACE)) {
        if (num_args == 255)
            error(compiler, "Cannot have more than 255 arguments.");
        num_args++;
        block_argument(compiler);
    }

    invoke_token_method(compiler, &op_token, num_args);
}

static void dot(Compiler* compiler, bool can_assign) {
    consume_slot(compiler, "Expect slot name after '.'.");
    invoke(compiler, can_assign);
}

static void this(Compiler* compiler, bool can_assign) {
    if (compiler->type == FUNCTION_TYPE_SCRIPT) {
        error(compiler, "Cannot use 'this' in top-level code.");
    }
    // The 0-th stack slot contains the target of the call.
    // Remember that we put this slot aside in compiler_init.
    emit_op(compiler, OP_GET_LOCAL);
    emit_byte(compiler, 0);
}

static void grouping(Compiler* compiler, bool can_assign) {
    expression(compiler);
    consume(compiler, TOKEN_RPAREN, "Expect ')' after expression.");
}

static void unary(Compiler* compiler, bool can_assign) {
    Token op_token = compiler->parser->previous;
    TokenType operator = op_token.type;
    // Compile the operand.
    parse_precedence(compiler, PREC_PREFIX);
    switch (operator) {
        case TOKEN_MINUS:
            invoke_string_method(compiler, "neg", 0);
            return;
        case TOKEN_BANG:
            invoke_token_method(compiler, &op_token, 0);
            break;
        default: UNREACHABLE();
    }
}

static void binary(Compiler* compiler, bool can_assign) {
    Token op_token = compiler->parser->previous;
    TokenType operator = op_token.type;
    ParseRule* rule = get_rule(operator);
    parse_precedence(compiler, (Precedence)(rule->precedence + 1));

    switch (operator) {
        case TOKEN_DOTDOT:
        case TOKEN_DOTDOTDOT:
        case TOKEN_EQ_EQ:
        case TOKEN_BANG_EQ:
        case TOKEN_PLUS:
        case TOKEN_MINUS:
        case TOKEN_TIMES:
        case TOKEN_SLASH:
        case TOKEN_LT:
        case TOKEN_LEQ:
        case TOKEN_GT:
        case TOKEN_GEQ:
        case TOKEN_AMP:
        case TOKEN_PIPE:
            invoke_token_method(compiler, &op_token, 1);
            break;
        default: UNREACHABLE();
    }
}

static ParseRule rules[] = {
    [TOKEN_PLUS]      = {NULL,     binary, PREC_TERM},
    [TOKEN_MINUS]     = {unary,    binary, PREC_TERM},
    [TOKEN_TIMES]     = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SLASH]     = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SEMICOLON] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LPAREN]    = {grouping, NULL,   PREC_NONE},
    [TOKEN_RPAREN]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LBRACE]    = {object,   NULL,   PREC_NONE},
    [TOKEN_RBRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQ]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQ_EQ]     = {NULL,     binary, PREC_EQ},
    [TOKEN_BANG]      = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQ]   = {NULL,     binary, PREC_EQ},
    [TOKEN_LT]        = {NULL,     binary, PREC_CMP},
    [TOKEN_LEQ]       = {NULL,     binary, PREC_CMP},
    [TOKEN_GT]        = {NULL,     binary, PREC_CMP},
    [TOKEN_GEQ]       = {NULL,     binary, PREC_CMP},
    [TOKEN_AMP]       = {NULL,     binary, PREC_BITWISE_AND},
    [TOKEN_AMP_AMP]   = {NULL,     and_,   PREC_AND},
    [TOKEN_PIPE]      = {NULL,     binary, PREC_BITWISE_OR},
    [TOKEN_PIPE_PIPE] = {NULL,     or_,    PREC_OR},
    [TOKEN_DOT]       = {NULL,     dot,    PREC_CALL},
    [TOKEN_DOTDOT]    = {NULL,     binary, PREC_RANGE},
    [TOKEN_DOTDOTDOT] = {NULL,     binary, PREC_RANGE},
    [TOKEN_NUMBER]    = {number,   NULL,   PREC_NONE},
    [TOKEN_STRING]    = {string,   NULL,   PREC_NONE},
    [TOKEN_VARIABLE]  = {variable, invoke, PREC_POSTFIX},
    [TOKEN_NIL]       = {literal,  invoke, PREC_POSTFIX},
    [TOKEN_TRUE]      = {literal,  invoke, PREC_POSTFIX},
    [TOKEN_FALSE]     = {literal,  invoke, PREC_POSTFIX},
    [TOKEN_WHILE]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]      = {this,     invoke, PREC_POSTFIX},
    [TOKEN_IF]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]      = {NULL,     NULL,   PREC_NONE},
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
    ParseFn prefix_rule = get_rule(compiler->parser->previous.type)->prefix;
    if (prefix_rule == NULL) {
        error(compiler, "Expected an expression.");
        return;
    }

    bool can_assign = prec <= PREC_ASSIGNMENT;
    prefix_rule(compiler, can_assign);

    while (prec <= get_rule(compiler->parser->current.type)->precedence) {
        advance(compiler);
        ParseFn infix_rule = get_rule(compiler->parser->previous.type)->infix;
        infix_rule(compiler, can_assign);
    }
}

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
}

static void block_or_stmt(Compiler*);

// Loop helpers
// ============

static void
enter_loop(Compiler* compiler, Loop* loop)
{
    size_t skip_jump = emit_jump(compiler, OP_JUMP); // (2)
    size_t break_jump = emit_jump(compiler, OP_JUMP); // (1)
    patch_jump(compiler, skip_jump); // (2)

    loop->outer = compiler->loop;
    loop->depth = compiler->scope_depth;
    loop->break_jump = break_jump - 1;
    loop->start = current_chunk(compiler)->length;
    compiler->loop = loop;
}

static void
test_exit_loop(Compiler* compiler)
{
    compiler->loop->cond_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
}

static void
exit_loop(Compiler* compiler)
{
    emit_loop(compiler, compiler->loop->start);
    patch_jump(compiler, compiler->loop->cond_jump);
    patch_jump(compiler, compiler->loop->break_jump + 1);
    compiler->loop = compiler->loop->outer;
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

    Token* name = &compiler->parser->previous;
    // Disallow declaring another variable with the same name,
    // within a local block.
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

    if (compiler->local_count == MAX_LOCALS) {
        error_at(compiler, name, "Too many locals in one chunk.");
        return;
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

    emit_op(compiler, OP_DEF_GLOBAL);
    emit_offset(compiler, global);
}

static uint16_t parse_variable(Compiler* compiler, const char* msg) {
    consume(compiler, TOKEN_VARIABLE, msg);

    declare_variable(compiler);
    if (compiler->scope_depth > 0)
        return 0;

    return identifier_constant(compiler, &compiler->parser->previous);
}

static void let_decl(Compiler* compiler) {
    uint16_t global = parse_variable(compiler, "Expect variable name.");

    if (match(compiler, TOKEN_EQ)) {
        expression(compiler);
    } else {
        emit_op(compiler, OP_NIL);
    }
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    define_variable(compiler, global);
}

static void assert_stmt(Compiler* compiler) {
    expression(compiler);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after expression.");
    emit_op(compiler, OP_ASSERT);
}

static void block(Compiler* compiler) {
    begin_block(compiler);
    while (!check(compiler, TOKEN_EOF) && !check(compiler, TOKEN_RBRACE))
        declaration(compiler);
    consume(compiler, TOKEN_RBRACE, "Expect '}' after block.");
    end_block(compiler);
}

static void block_or_stmt(Compiler* compiler) {
    if (match(compiler, TOKEN_LBRACE)) {
        block(compiler);
    } else {
        statement(compiler);
    }
}

static void if_stmt(Compiler* compiler) {
    consume(compiler, TOKEN_LPAREN, "Expect '(' after if.");
    expression(compiler);
    consume(compiler, TOKEN_RPAREN, "Expect ')' after condition.");

    size_t else_jump = emit_jump(compiler, OP_JUMP_IF_FALSE);
    block_or_stmt(compiler);

    size_t exit_jump = emit_jump(compiler, OP_JUMP);

    patch_jump(compiler, else_jump);

    if (match(compiler, TOKEN_ELSE))
        block_or_stmt(compiler);
    patch_jump(compiler, exit_jump);
}

static void while_stmt(Compiler* compiler) {
    Loop loop;
    enter_loop(compiler, &loop);

    consume(compiler, TOKEN_LPAREN, "Expect '(' after while.");
    expression(compiler);
    consume(compiler, TOKEN_RPAREN, "Expect ')' after condition.");

    test_exit_loop(compiler);

    // Compile the body of the while loop.
    block_or_stmt(compiler);
    exit_loop(compiler);
}

static void load_local(Compiler* compiler, int offset) {
    emit_op(compiler, OP_GET_LOCAL);
    emit_byte(compiler, offset);
}

static void for_stmt(Compiler* compiler) {
    // Desugar the following for loop:
    // for (x in items) { | let _s = items;
    //    bar;            | let _i = nil;
    // }                  | while (_i = _s.iterMore(_i)) {
    //                    |     let x = _s.iterNext(_i);
    //                    |     bar;
    //                    | }
    Token seq_token  = {.start="seq ", .length=4};
    Token iter_token = {.start="iter ", .length=5};

    begin_block(compiler);
    consume(compiler, TOKEN_LPAREN, "Expect '(' after 'for'.");
    consume(compiler, TOKEN_VARIABLE, "Expect loop variable.");

    // Remember the loop variable.
    Token loop_var = compiler->parser->previous;

    consume(compiler, TOKEN_IN, "Expect 'in' after loop variable.");

    // Evaluate the sequence.
    expression(compiler);

    // Check that we have enough space to store the two locals.
    if (compiler->local_count + 2 > MAX_LOCALS) {
        error(compiler, "Not enough space for for-loop variables.");
        return;
    }

    int seq = add_local(compiler, seq_token);
    mark_local_initialized(compiler);

    // The iterator value.
    emit_op(compiler, OP_NIL);
    int iter = add_local(compiler, iter_token);
    mark_local_initialized(compiler);

    consume(compiler, TOKEN_RPAREN, "Expect ')' after loop expression.");

    Loop loop;
    enter_loop(compiler, &loop);

    // _i = _s.iterMore(_i)
    load_local(compiler, seq);
    load_local(compiler, iter);
    invoke_string_method(compiler, "iterMore", 1);
    emit_op(compiler, OP_SET_LOCAL); emit_byte(compiler, (uint8_t) iter);
    test_exit_loop(compiler);

    // loop_var = _s.iterNext(_i)
    load_local(compiler, seq);
    load_local(compiler, iter);
    invoke_string_method(compiler, "iterNext", 1);

    // push a fresh block for every iteration
    begin_block(compiler);
    add_local(compiler, loop_var);
    mark_local_initialized(compiler);
    block_or_stmt(compiler);
    end_block(compiler);

    exit_loop(compiler);

    end_block(compiler);
}

static void continue_stmt(Compiler* compiler) {
    if (compiler->loop == NULL) {
        error(compiler, "Cannot continue from outside a loop.");
        return;
    }
    pop_to_scope(compiler, compiler->loop->depth);
    emit_loop(compiler, compiler->loop->start);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
}

static void break_stmt(Compiler* compiler) {
    if (compiler->loop == NULL) {
        error(compiler, "Cannot break from outside a loop.");
        return;
    }
    pop_to_scope(compiler, compiler->loop->depth);
    emit_loop(compiler, compiler->loop->break_jump);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after 'break'.");
}

static void return_stmt(Compiler* compiler) {
    if (compiler->type == FUNCTION_TYPE_SCRIPT)
        error(compiler, "Cannot return from top-level code.");

    if (match(compiler, TOKEN_SEMICOLON)) {
        emit_return(compiler);
    } else {
        expression(compiler);
        consume(compiler, TOKEN_SEMICOLON, "Expected ';' after expression.");
        emit_op(compiler, OP_RETURN);
    }
}

static void synchronize(Compiler* compiler) {
    compiler->parser->panic_mode = false;
    while (compiler->parser->current.type != TOKEN_EOF) {
        if (compiler->parser->previous.type == TOKEN_SEMICOLON) return;
        switch (compiler->parser->current.type) {
            case TOKEN_LET:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_RETURN:
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
            case TOKEN_ASSERT:
            case TOKEN_IF:
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

    if (compiler->parser->panic_mode) synchronize(compiler);
}

static void statement(Compiler* compiler) {
    if (match(compiler, TOKEN_ASSERT)) {
        assert_stmt(compiler);
    } else if (match(compiler, TOKEN_IF)) {
        if_stmt(compiler);
    } else if (match(compiler, TOKEN_WHILE)) {
        while_stmt(compiler);
    } else if (match(compiler, TOKEN_FOR)) {
        for_stmt(compiler);
    } else if (match(compiler, TOKEN_RETURN)) {
        return_stmt(compiler);
    } else if (match(compiler, TOKEN_BREAK)) {
        break_stmt(compiler);
    } else if (match(compiler, TOKEN_CONTINUE)) {
        continue_stmt(compiler);
    } else {
        // Expression statement
        expression(compiler);
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after expression.");
        emit_op(compiler, OP_POP);
    }
}

ObjFunction*
compile(VM* vm, const char* source)
{
    Parser parser;
    parser_init(&parser, source);

    Compiler compiler;
    compiler_init(&compiler, NULL, &parser, vm, FUNCTION_TYPE_SCRIPT);

    advance(&compiler);
    while (!match(&compiler, TOKEN_EOF))
        declaration(&compiler);

    ObjFunction* function = compiler_end(&compiler);
    consume(&compiler, TOKEN_EOF, "Expect end of expression.");
    return parser.had_error ? NULL : function;
}

void
compiler_mark(Compiler* compiler, VM* vm)
{
    if (compiler == NULL) return;

    // mark the enclosing compiler.
    compiler_mark(compiler->enclosing, vm);
    mark_object(vm, (Obj*)compiler->function);
}
