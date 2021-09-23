#include "lexer.h"

#include <string.h> // strlen

void lexer_init(Lexer* lexer, const char* source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
}

static Token make_token(Lexer* lexer, TokenType type) {
    Token tok;
    tok.type = type;
    tok.start = lexer->start;
    tok.length = (size_t)(lexer->current - lexer->start);
    tok.line = lexer->line;
    return tok;
}

static Token error_token(Lexer* lexer, const char* message) {
    Token tok;
    tok.type = TOKEN_ERROR;
    tok.start = message;
    tok.length = strlen(message);
    tok.line = lexer->line;
    return tok;
}

static char is_at_end(Lexer* lexer) {
    return *lexer->current == '\0';
}

static char peek(Lexer* lexer) {
    return lexer->current[0];
}

static char peek_next(Lexer* lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static char advance(Lexer* lexer) {
    lexer->current++;
    return lexer->current[-1];
}

static bool match(Lexer* lexer, char ch) {
    if (is_at_end(lexer)) return false;
    if (*lexer->current == ch) {
        lexer->current++;
        return true;
    }
    return false;
}

static bool is_alpha(char ch) {
    return ch == '_' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static bool is_numeric(char ch) {
    return ch >= '0' && ch <= '9';
}

static bool is_alphanumeric(char ch) {
    return is_alpha(ch) || is_numeric(ch);
}

static bool is_whitespace(char ch) {
    return ch == '\t' || ch == ' ' || ch == '\n' || ch == '\r';
}

static void skip_whitespace(Lexer* lexer) {
    for (;;) {
        char ch = peek(lexer);
        switch (ch) {
            case '\n':
                // Special handling for '\n': need to increment the
                // line count.
                lexer->line++;
            case ' ':
            case '\t':
            case '\r':
                advance(lexer);
                break;
            case '#':
                // Comments start with '#' and continue until
                // the end of the line.
                while (!is_at_end(lexer) && peek(lexer) != '\n')
                    advance(lexer);
                break;
            default:
                return;
        }
    }
}

static Token string(Lexer* lexer) {
    // we've already consumed the first '"'.
    while (!is_at_end(lexer) && peek(lexer) != '"') {
        char ch = advance(lexer);
        if (ch == '\n')
            lexer->line++;
    }
    // consume the '"'
    if (!match(lexer, '"')) {
        return error_token(lexer, "Unterminated string literal.");
    }
    return make_token(lexer, TOKEN_STRING);
}

static Token number(Lexer* lexer) {
    // Consume the integer part.
    while (is_numeric(peek(lexer)))
        advance(lexer);

    // Fractional part
    if (peek(lexer) == '.' && is_numeric(peek_next(lexer))) {
        advance(lexer); // consume the '.'
        while (is_numeric(peek(lexer)))
            advance(lexer);
    }

    return make_token(lexer, TOKEN_NUMBER);
}

static TokenType
match_rest(Lexer* lexer, TokenType type,
           size_t start, const char* rest,
           size_t length)
{
    if (lexer->current - lexer->start == start + length
            && memcmp(lexer->start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_VARIABLE;
}

static TokenType variable_type(Lexer* lexer) {
    switch (lexer->start[0]) {
        case 'a':
            if (lexer->current - lexer->start >= 2) {
                switch (lexer->start[1]) {
                    case 'n': return match_rest(lexer, TOKEN_AND, 2, "d", 1);
                    case 's': return match_rest(lexer, TOKEN_ASSERT, 2, "sert", 4);
                }
            }
            break;
        case 'e': return match_rest(lexer, TOKEN_ELSE, 1, "lse", 3);
        case 'f':
            if (lexer->current - lexer->start >= 2) {
                switch (lexer->start[1]) {
                    case 'a': return match_rest(lexer, TOKEN_FALSE, 2, "lse", 3);
                    case 'n': return match_rest(lexer, TOKEN_FN, 2, "", 0);
                }
            }
            break;
        case 'i': return match_rest(lexer, TOKEN_IF, 1, "f", 1);
        case 'l': return match_rest(lexer, TOKEN_LET, 1, "et", 2);
        case 'n': return match_rest(lexer, TOKEN_NIL, 1, "il", 2);
        case 'o': return match_rest(lexer, TOKEN_OR, 1, "r", 1);
        case 'r': return match_rest(lexer, TOKEN_RETURN, 1, "eturn", 5);
        case 's': return match_rest(lexer, TOKEN_SUPER, 1, "uper", 4);
        case 't':
            if (lexer->current - lexer->start >= 2) {
                switch (lexer->start[1]) {
                    case 'r': return match_rest(lexer, TOKEN_TRUE, 2, "ue", 2);
                    case 'h': return match_rest(lexer, TOKEN_THIS, 2, "is", 2);
                }
            }
            break;
        case 'w': return match_rest(lexer, TOKEN_WHILE, 1, "hile", 4);
    }
    return TOKEN_VARIABLE;
}

static Token variable(Lexer* lexer) {
    while (is_alphanumeric(peek(lexer)))
        advance(lexer);
    TokenType type = variable_type(lexer);
    return make_token(lexer, type);
}

Token lexer_next(Lexer* lexer) {
    skip_whitespace(lexer);
    lexer->start = lexer->current;
    if (is_at_end(lexer)) return make_token(lexer, TOKEN_EOF);

    char ch = advance(lexer);

    if (is_numeric(ch)) return number(lexer);
    if (is_alpha(ch)) return variable(lexer);

    switch (ch) {
        case '+': return make_token(lexer, TOKEN_PLUS);
        case '-': return make_token(lexer, TOKEN_MINUS);
        case '*': return make_token(lexer, TOKEN_TIMES);
        case '/': return make_token(lexer, TOKEN_SLASH);
        case ',': return make_token(lexer, TOKEN_COMMA);
        case ';': return make_token(lexer, TOKEN_SEMICOLON);
        case '.': return make_token(lexer, TOKEN_DOT);
        case '(': return make_token(lexer, TOKEN_LPAREN);
        case ')': return make_token(lexer, TOKEN_RPAREN);
        case '{': return make_token(lexer, TOKEN_LBRACE);
        case '}': return make_token(lexer, TOKEN_RBRACE);
        case '=': return make_token(lexer, match(lexer, '=') ? TOKEN_EQ_EQ : TOKEN_EQ);
        case '!': return make_token(lexer, match(lexer, '=') ? TOKEN_BANG_EQ : TOKEN_BANG);
        case '<': return make_token(lexer, match(lexer, '=') ? TOKEN_LEQ : TOKEN_LT);
        case '>': return make_token(lexer, match(lexer, '=') ? TOKEN_GEQ : TOKEN_GT);
        case '"': return string(lexer);
    }

    return error_token(lexer, "Unexpected character.");
}
