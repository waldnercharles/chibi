#ifndef CHIBI_CODE_LEX_H
#define CHIBI_CODE_LEX_H

#include <stddef.h>

typedef enum
{
    CODE_TOKEN_END,
    CODE_TOKEN_NAME,
    CODE_TOKEN_NUMBER,
    CODE_TOKEN_STRING,
    CODE_TOKEN_PUNCTUATION,
    CODE_TOKEN_ERROR
} code_token_kind;
typedef struct
{
    code_token_kind kind;
    size_t start, end;
    int line, column;
    char punctuation;
} code_token;
typedef struct
{
    const char *text;
    size_t at;
    int line, column;
} code_lexer;

void code_lex_init(code_lexer *lexer, const char *text);
/* Comments are trivia. Byte spans refer to the supplied source, including
 * incomplete tokens. Unterminated strings/comments yield CODE_TOKEN_ERROR. */
code_token code_lex_next(code_lexer *lexer);
int code_token_is(const code_lexer *lexer, code_token token, const char *text);

#endif
