#ifndef SUBTLE_LEXER_H
#define SUBTLE_LEXER_H

#include "common.h"

typedef struct {
    const char* start;
    const char* current;
    int line; // current line number
} Lexer;

typedef enum {
    // Single character tokens
    TOKEN_COMMA,  // ,
    TOKEN_LPAREN, // (
    TOKEN_RPAREN, // )
    TOKEN_LBOX,   // [
    TOKEN_RBOX,   // ]
    TOKEN_LBRACE, // {
    TOKEN_RBRACE, // }
    // One or two chars
    TOKEN_EQ,         // =
    TOKEN_COLONCOLON, // ::
    TOKEN_COLONEQ,    // :=
    // Operator
    TOKEN_OPERATOR,
    // Literals
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_VARIABLE, // [a-zA-Z_][a-zA-Z_0-9]+
    // Keywords
    TOKEN_SIG, // variable + ":"
    TOKEN_NIL,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_SELF,
    TOKEN_RETURN,

    TOKEN_NEWLINE,
    TOKEN_ERROR,
    TOKEN_EOF,
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    size_t length;
    int line;
} Token;

void lexer_init(Lexer* lexer, const char* source);
Token lexer_next(Lexer* lexer);

#endif
