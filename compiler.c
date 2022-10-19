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
#define MAX_ARGS     16

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
    ObjFn* function;
    FunctionType type;

    // Keep track of the number of stack slots currently
    int slot_count;

    // Scoping
    Local locals[MAX_LOCALS];
    int local_count;
    Upvalue upvalues[MAX_UPVALUES];
    int scope_depth; // Current scope depth.

    VM* vm;
} Compiler;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OPERATOR,   // operators
    PREC_CALL,       // (), .
    PREC_LITERAL,    // literals
} Precedence;

// ExprType is the type of the parsed expression.
// Used for TCO support.
typedef enum {
    EXPR_INVOKE,
    EXPR_OTHER,
} ExprType;

typedef ExprType (*ParseFn)(Compiler* compiler, bool can_assign, bool allow_newlines);

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
    compiler->function = objfn_new(vm);
    if (type == FUNCTION_TYPE_SCRIPT)
        compiler->function->arity = -1;

    compiler->type = type;
    compiler->slot_count = 1; // the initial local
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->vm = vm;

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

    fprintf(stderr, "[line %d] Error at ", token->line);
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
            || check(compiler, TOKEN_TRUE)  || check(compiler, TOKEN_FALSE) || check(compiler, TOKEN_NIL)) {
        advance(compiler);
        return true;
    }
    return false;
}

static void consume_slot(Compiler* compiler, const char* message) {
    if (!match_slot(compiler))
        error_at_current(compiler, message);
}

static bool match_newlines(Compiler* compiler) {
    return match(compiler, TOKEN_NEWLINE);
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
    [OP_ETHER] = 1,
    [OP_DEF_GLOBAL] = 0,
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
    [OP_TAIL_INVOKE] = 0,
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
    if (compiler->parser->had_error)
        return -1;
    uint32_t offset = chunk_write_constant(current_chunk(compiler), compiler->vm, v);
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

static ObjFn*
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

// TCO
// ===

static void maybe_tco(Compiler* compiler, ExprType t)
{
    if (t == EXPR_INVOKE) {
        // Patch the previous OP_INVOKE to be an OP_TAIL_INVOKE instead,
        // and adjust the stack requirements accordingly.
        Chunk* chunk = &compiler->function->chunk;
        chunk->code[chunk->length - 4] = OP_TAIL_INVOKE;
        // We still keep the OP_RETURN around if it's a TCO-ed call, so
        // that the vm can bail-out if necessary.
    }
}

// Expression parsing
// ==================

static void block(Compiler*);
static void define_variable(Compiler*, uint16_t);
static ExprType expression(Compiler*, bool);
static ExprType parse_precedence(Compiler*, Precedence, bool);
static ParseRule* get_rule(TokenType);

static ExprType string(Compiler* compiler, bool can_assign, bool allow_newlines) {
    ObjString* str = objstring_copy(
        compiler->vm,
        compiler->parser->previous.start + 1,
        compiler->parser->previous.length - 2
        );
    emit_constant(compiler, OBJ_TO_VAL(str));
    return EXPR_OTHER;
}

static ExprType number(Compiler* compiler, bool can_assign, bool allow_newlines) {
    double value = strtod(compiler->parser->previous.start, NULL);
    emit_constant(compiler, NUMBER_TO_VAL(value));
    return EXPR_OTHER;
}

static ExprType literal(Compiler* compiler, bool can_assign, bool allow_newlines) {
    switch (compiler->parser->previous.type) {
        case TOKEN_TRUE:  emit_op(compiler, OP_TRUE); break;
        case TOKEN_FALSE: emit_op(compiler, OP_FALSE); break;
        case TOKEN_NIL:   emit_op(compiler, OP_NIL); break;
        default: UNREACHABLE();
    }
    return EXPR_OTHER;
}

//
// Variable declaration is split into two stages: _declaring_, which
// marks its place in the stack (if it's a local variable), and _defining_,
// which emits bytecode that gives it a value.
//

static void declare_variable(Compiler* compiler, Token* name) {
    int scope_depth = compiler->scope_depth;
    if (scope_depth == 0) return; // Nothing to do in global scope.

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

static void named_variable(Compiler* compiler, Token name, bool can_assign, bool allow_newlines) {
    if (allow_newlines)
        match_newlines(compiler);

    if (can_assign && match(compiler, TOKEN_COLONEQ)) {
        uint16_t global = 0;
        if (compiler->scope_depth == 0)
            global = identifier_constant(compiler, &name);
        declare_variable(compiler, &name);
        match_newlines(compiler);
        expression(compiler, allow_newlines);
        define_variable(compiler, global);
        return;
    }

    // Check if we can resolve to a local variable.
    int local = resolve_local(compiler, &name);
    if (local != -1) {
        if (can_assign && match(compiler, TOKEN_EQ)) {
            match_newlines(compiler);
            expression(compiler, allow_newlines);
            emit_op(compiler, OP_SET_LOCAL);
            emit_byte(compiler, (uint8_t) local);
        } else {
            emit_op(compiler, OP_GET_LOCAL);
            emit_byte(compiler, (uint8_t) local);
        }
        return;
    }

    // Check if we can resolve it as an upvalue.
    int upvalue = resolve_upvalue(compiler, &name);
    if (upvalue != -1) {
        if (can_assign && match(compiler, TOKEN_EQ)) {
            match_newlines(compiler);
            expression(compiler, allow_newlines);
            emit_op(compiler, OP_SET_UPVALUE);
            emit_byte(compiler, (uint8_t) upvalue);
        } else {
            emit_op(compiler, OP_GET_UPVALUE);
            emit_byte(compiler, (uint8_t) upvalue);
        }
        return;
    }

    // Otherwise, it's a global.
    uint16_t global = identifier_constant(compiler, &name);
    if (can_assign && match(compiler, TOKEN_EQ)) {
        match_newlines(compiler);
        expression(compiler, allow_newlines);
        emit_op(compiler, OP_SET_GLOBAL);
        emit_offset(compiler, global);
    } else {
        emit_op(compiler, OP_GET_GLOBAL);
        emit_offset(compiler, global);
    }
}

static ExprType variable(Compiler* compiler, bool can_assign, bool allow_newlines) {
    named_variable(compiler, compiler->parser->previous, can_assign, allow_newlines);
    return EXPR_OTHER;
}

static ExprType object(Compiler* compiler, bool can_assign, bool allow_newlines) {
    // Object literal.
    emit_op(compiler, OP_OBJECT);
    if (!check(compiler, TOKEN_RBRACE)) {
        do {
            match_newlines(compiler);
            consume_slot(compiler, "Expect a slot name.");
            uint16_t constant = identifier_constant(compiler, &compiler->parser->previous);
            match_newlines(compiler);
            consume(compiler, TOKEN_EQ, "Expect '=' after slot name.");
            match_newlines(compiler);
            expression(compiler, true);
            emit_op(compiler, OP_OBJLIT_SET);
            emit_offset(compiler, constant);
        } while (match(compiler, TOKEN_COMMA));
    }
    match_newlines(compiler);
    consume(compiler, TOKEN_RBRACE, "Expect '}' after items.");
    return EXPR_OTHER;
}

static void finalise_closure(Compiler* compiler, Compiler* closure_compiler) {
    ObjFn* fn = compiler_end(closure_compiler);
    uint16_t idx = make_constant(compiler, OBJ_TO_VAL(fn));
    emit_op(compiler, OP_CLOSURE);
    emit_offset(compiler, idx);

    for (int i = 0; i < fn->upvalue_count; i++) {
        emit_byte(compiler, closure_compiler->upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler, closure_compiler->upvalues[i].index);
    }
}

static ExprType def_expr(Compiler* compiler, bool can_assign, bool allow_newlines) {
    uint16_t name;
    Compiler c;
    compiler_init(&c, compiler, compiler->parser,
                  compiler->vm, FUNCTION_TYPE_FUNCTION);
    begin_block(&c);

    if (match(compiler, TOKEN_OPERATOR)) {
        // match exactly one argument.
        const Token token = compiler->parser->previous;
        name = identifier_constant(compiler, &token);

        // parameter list.
        c.function->arity = 1;
        consume(&c, TOKEN_VARIABLE, "Expect parameter name after operator.");
        declare_variable(&c, &compiler->parser->previous);
        define_variable(&c, 0);
        c.slot_count++;
    } else {
        size_t siglen = 0;
        char* sig = NULL;

        while (true) {
            consume(compiler, TOKEN_SIG, "Expect keyword after '::'.");
            // build the signature...
            Token arg = compiler->parser->previous;
            if (!c.parser->had_error) {
                sig = GROW_ARRAY(compiler->vm, sig, char, siglen, siglen + arg.length);
                memcpy(&sig[siglen], arg.start, arg.length);
                siglen += arg.length;
            }
            consume(&c, TOKEN_VARIABLE, "Expect parameter name after keyword.");
            c.function->arity++;
            if (c.function->arity > MAX_ARGS)
                error_at_current(compiler, "Too many parameters");
            declare_variable(&c, &compiler->parser->previous);
            define_variable(&c, 0);
            // see if there's any more arguments.
            if (!check(compiler, TOKEN_SIG))
                break;
        }

        sig = GROW_ARRAY(compiler->vm, sig, char, siglen, siglen + 1);
        const Token tok = {.start=sig, .length=siglen};
        name = identifier_constant(compiler, &tok);
        FREE_ARRAY(compiler->vm, sig, char, siglen + 1);
    }

    consume(compiler, TOKEN_LBOX, "Expect '[' after signature.");
    block(&c);
    finalise_closure(compiler, &c);

    emit_op(compiler, OP_OBJECT_SET);
    emit_offset(compiler, name);
    return EXPR_OTHER;
}

static ExprType closure(Compiler* compiler, bool can_assign, bool allow_newlines) {
    Compiler c;
    compiler_init(&c, compiler, compiler->parser,
                  compiler->vm, FUNCTION_TYPE_FUNCTION);
    begin_block(&c);
    block(&c);
    end_block(&c);
    finalise_closure(compiler, &c);
    return EXPR_OTHER;
}

static ExprType invoke(Compiler* compiler, bool can_assign, bool allow_newlines) {
    Token arg = compiler->parser->previous;
    if (allow_newlines)
        match_newlines(compiler);

    // create the signature.
    int num_args = 0;
    size_t siglen = 0;
    char* sig = NULL;

    while (true) {
        num_args++;
        if (num_args > MAX_ARGS)
            error_at_current(compiler, "too many arguments");
        if (!compiler->parser->had_error) {
            sig = GROW_ARRAY(compiler->vm, sig, char, siglen, siglen + arg.length);
            memcpy(&sig[siglen], arg.start, arg.length);
            siglen += arg.length;
        }
        parse_precedence(compiler, PREC_LITERAL, allow_newlines);
        // see if there's any more arguments.
        if (!match(compiler, TOKEN_SIG))
            break;
        arg = compiler->parser->previous;
    }

    sig = GROW_ARRAY(compiler->vm, sig, char, siglen, siglen + 1);
    sig[siglen] = '\0';
    invoke_string_method(compiler, sig, num_args);
    FREE_ARRAY(compiler->vm, sig, char, siglen + 1);
    return EXPR_INVOKE;
}

static ExprType invoke_ether(Compiler* compiler, bool can_assign, bool allow_newlines) {
    emit_op(compiler, OP_ETHER);
    return invoke(compiler, can_assign, allow_newlines);
}

static ExprType self(Compiler* compiler, bool can_assign, bool allow_newlines) {
    if (compiler->type == FUNCTION_TYPE_SCRIPT) {
        error(compiler, "Cannot use 'self' in top-level code.");
    }
    // The 0-th stack slot contains the target of the call.
    // Remember that we put this slot aside in compiler_init.
    emit_op(compiler, OP_GET_LOCAL);
    emit_byte(compiler, 0);
    return EXPR_OTHER;
}

static ExprType grouping(Compiler* compiler, bool can_assign, bool allow_newlines) {
    match_newlines(compiler);
    ExprType t = expression(compiler, true);
    consume(compiler, TOKEN_RPAREN, "Expect ')' after expression.");
    return t;
}

static ExprType binary(Compiler* compiler, bool can_assign, bool allow_newlines) {
    Token op_token = compiler->parser->previous;
    TokenType operator = op_token.type;
    ParseRule* rule = get_rule(operator);

    // allow a newline after the operator.
    match_newlines(compiler);
    parse_precedence(compiler, (Precedence)(rule->precedence + 1), allow_newlines);

    invoke_token_method(compiler, &op_token, 1);
    return EXPR_OTHER;
}

static ExprType unary(Compiler* compiler, bool can_assign, bool allow_newlines) {
    Token token = compiler->parser->previous;
    invoke_token_method(compiler, &token, 0);
    return EXPR_INVOKE;
}

static ExprType return_expr(Compiler* compiler, bool can_assign, bool allow_newlines) {
    if (compiler->type == FUNCTION_TYPE_SCRIPT)
        error(compiler, "Cannot return from top-level code.");

    if (check(compiler, TOKEN_NEWLINE)) {
        emit_return(compiler);
    } else {
        ExprType t = expression(compiler, false);
        maybe_tco(compiler, t);
        emit_op(compiler, OP_RETURN);
    }
    return EXPR_OTHER;
}

static ParseRule rules[] = {
    [TOKEN_COMMA]      = {NULL,         NULL,     PREC_NONE},
    [TOKEN_LPAREN]     = {grouping,     NULL,     PREC_NONE},
    [TOKEN_RPAREN]     = {NULL,         NULL,     PREC_NONE},
    [TOKEN_LBOX]       = {closure,      NULL,     PREC_NONE},
    [TOKEN_RBOX]       = {NULL,         NULL,     PREC_NONE},
    [TOKEN_LBRACE]     = {object,       NULL,     PREC_NONE},
    [TOKEN_RBRACE]     = {NULL,         NULL,     PREC_NONE},
    [TOKEN_EQ]         = {NULL,         NULL,     PREC_NONE},
    [TOKEN_COLONCOLON] = {NULL,         def_expr, PREC_OPERATOR},
    [TOKEN_COLONEQ]    = {NULL,         NULL,     PREC_NONE},
    [TOKEN_OPERATOR]   = {NULL,         binary,   PREC_OPERATOR},
    [TOKEN_NUMBER]     = {number,       NULL,     PREC_NONE},
    [TOKEN_STRING]     = {string,       NULL,     PREC_NONE},
    [TOKEN_VARIABLE]   = {variable,     unary,    PREC_CALL},
    [TOKEN_SIG]        = {invoke_ether, invoke,   PREC_CALL},
    [TOKEN_NIL]        = {literal,      unary,    PREC_CALL},
    [TOKEN_TRUE]       = {literal,      unary,    PREC_CALL},
    [TOKEN_FALSE]      = {literal,      unary,    PREC_CALL},
    [TOKEN_SELF]       = {self,         unary,    PREC_NONE},
    [TOKEN_RETURN]     = {return_expr,  unary,    PREC_NONE},
    [TOKEN_NEWLINE]    = {NULL,         NULL,     PREC_NONE},
    [TOKEN_ERROR]      = {NULL,         NULL,     PREC_NONE},
    [TOKEN_EOF]        = {NULL,         NULL,     PREC_NONE},
};

static ExprType expression(Compiler* compiler, bool allow_newlines) {
    return parse_precedence(compiler, PREC_ASSIGNMENT, allow_newlines);
}

static ExprType parse_precedence(Compiler* compiler, Precedence prec, bool allow_newlines) {
    advance(compiler);
    ParseFn prefix_rule = get_rule(compiler->parser->previous.type)->prefix;
    if (prefix_rule == NULL) {
        error(compiler, "Expected an expression.");
        return EXPR_OTHER;
    }

    ExprType t;
    bool can_assign = prec <= PREC_ASSIGNMENT;
    t = prefix_rule(compiler, can_assign, allow_newlines);
    if (allow_newlines)
        match_newlines(compiler);

    while (prec <= get_rule(compiler->parser->current.type)->precedence) {
        advance(compiler);
        ParseFn infix_rule = get_rule(compiler->parser->previous.type)->infix;
        t = infix_rule(compiler, can_assign, allow_newlines);
        if (allow_newlines)
            match_newlines(compiler);
    }
    return t;
}

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
}

// Statement parsing
// =================

static void block(Compiler* compiler) {
    match_newlines(compiler);
    begin_block(compiler);

    ExprType t = EXPR_OTHER;
    bool once = false;

    while (!check(compiler, TOKEN_EOF) && !check(compiler, TOKEN_RBOX)) {
        once = true;
        t = expression(compiler, false);
        if (!match_newlines(compiler))
            break;
    }

    if (once) {
        maybe_tco(compiler, t);
        emit_op(compiler, OP_RETURN);
    }

    consume(compiler, TOKEN_RBOX, "Expect ']' after block.");
    end_block(compiler);
}

ObjFn*
compile(VM* vm, const char* source)
{
    Parser parser;
    parser_init(&parser, source);

    Compiler compiler;
    compiler_init(&compiler, NULL, &parser, vm, FUNCTION_TYPE_SCRIPT);

    advance(&compiler);
    match(&compiler, TOKEN_NEWLINE);
    bool has_newline = true;
    while (!match(&compiler, TOKEN_EOF) && has_newline) {
        expression(&compiler, false);
        emit_op(&compiler, OP_POP);
        has_newline = match_newlines(&compiler);
    }

    ObjFn* function = compiler_end(&compiler);
    consume(&compiler, TOKEN_EOF, "Expect end of file.");
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
