#include "support/code_lex.h"

#include <ctype.h>
#include <string.h>

void code_lex_init(code_lexer *lexer, const char *text)
{
    *lexer = (code_lexer){ .text = text ? text : "", .line = 1, .column = 1 };
}
static void advance(code_lexer *lexer)
{
    if (lexer->text[lexer->at++] == '\n') {
        lexer->line++;
        lexer->column = 1;
        return;
    }
    lexer->column++;
}
code_token code_lex_next(code_lexer *lexer)
{
    const char *text = lexer->text;
    for (;;) {
        while (isspace((unsigned char)text[lexer->at])) {
            advance(lexer);
        }
        if (text[lexer->at] == '/' && text[lexer->at + 1] == '/') {
            while (text[lexer->at] && text[lexer->at] != '\n') {
                advance(lexer);
            }
            continue;
        }
        if (text[lexer->at] != '/' || text[lexer->at + 1] != '*') {
            break;
        }
        code_token comment = { .kind = CODE_TOKEN_ERROR,
                               .start = lexer->at,
                               .line = lexer->line,
                               .column = lexer->column };
        advance(lexer);
        advance(lexer);
        int depth = 1;
        while (text[lexer->at] && depth) {
            if (text[lexer->at] == '/' && text[lexer->at + 1] == '*') {
                advance(lexer);
                advance(lexer);
                depth++;
            } else if (text[lexer->at] == '*' && text[lexer->at + 1] == '/') {
                advance(lexer);
                advance(lexer);
                depth--;
            } else {
                advance(lexer);
            }
        }
        if (depth) {
            comment.end = lexer->at;
            return comment;
        }
    }
    code_token token = { .start = lexer->at,
                         .line = lexer->line,
                         .column = lexer->column };
    char c = text[lexer->at];
    if (!c) {
        token.kind = CODE_TOKEN_END;
    } else if (c == '"') {
        token.kind = CODE_TOKEN_STRING;
        advance(lexer);
        while (text[lexer->at] && text[lexer->at] != '"') {
            if (text[lexer->at] == '\n' || text[lexer->at] == '\r') {
                token.kind = CODE_TOKEN_ERROR;
                break;
            }
            if (text[lexer->at] == '\\') {
                advance(lexer);
                if (!text[lexer->at]) {
                    break;
                }
            }
            advance(lexer);
        }
        if (text[lexer->at] == '"') {
            advance(lexer);
        } else {
            token.kind = CODE_TOKEN_ERROR;
        }
    } else if (isalpha((unsigned char)c) || c == '_') {
        token.kind = CODE_TOKEN_NAME;
        advance(lexer);
        while (isalnum((unsigned char)text[lexer->at]) || text[lexer->at] == '_') {
            advance(lexer);
        }
    } else if (
        isdigit((unsigned char)c) ||
        ((c == '+' || c == '-') && isdigit((unsigned char)text[lexer->at + 1]))
    ) {
        token.kind = CODE_TOKEN_NUMBER;
        advance(lexer);
        while (isalnum((unsigned char)text[lexer->at]) ||
               (text[lexer->at] && strchr("._", text[lexer->at])) ||
               ((text[lexer->at] == '+' || text[lexer->at] == '-') &&
                (text[lexer->at - 1] == 'e' || text[lexer->at - 1] == 'E'))) {
            advance(lexer);
        }
    } else {
        token.kind = CODE_TOKEN_PUNCTUATION;
        token.punctuation = c;
        advance(lexer);
    }
    token.end = lexer->at;
    return token;
}
int code_token_is(const code_lexer *lexer, code_token token, const char *text)
{
    return token.end - token.start == strlen(text) &&
           !memcmp(lexer->text + token.start, text, token.end - token.start);
}
