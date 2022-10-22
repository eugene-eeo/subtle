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
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_TIMES, TOKEN_SLASH,
    TOKEN_COMMA,
    TOKEN_LPAREN, TOKEN_RPAREN, // (, )
    TOKEN_LBRACE, TOKEN_RBRACE, // {, }
    // One-or-two characters
    TOKEN_EQ, TOKEN_EQ_EQ,       // =, ==
    TOKEN_BANG, TOKEN_BANG_EQ,   // !, !=
    TOKEN_LT, TOKEN_LEQ,         // <, <=
    TOKEN_GT, TOKEN_GEQ,         // >, >=
    TOKEN_AMP,  TOKEN_AMP_AMP,   // &, &&
    TOKEN_PIPE, TOKEN_PIPE_PIPE, // |, ||
    // One, two, or three characters
    TOKEN_DOT, TOKEN_DOTDOT, TOKEN_DOTDOTDOT, // ., .., ...
    // Literals
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_VARIABLE,
    // Keywords
    TOKEN_NIL,
    TOKEN_TRUE, TOKEN_FALSE,
    TOKEN_DONE,
    TOKEN_WHILE,
    TOKEN_SELF,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_LET,
    TOKEN_RETURN,
    TOKEN_ASSERT,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_FOR,

    TOKEN_SEMICOLON,
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
