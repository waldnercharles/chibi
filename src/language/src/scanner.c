#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <wctype.h>

enum
{
    AUTOMATIC_TERMINATOR
};

void *tree_sitter_chibi_external_scanner_create(void)
{
    return NULL;
}
void tree_sitter_chibi_external_scanner_destroy(void *payload)
{
    (void)payload;
}
unsigned tree_sitter_chibi_external_scanner_serialize(void *payload, char *buffer)
{
    (void)payload;
    (void)buffer;
    return 0;
}
void tree_sitter_chibi_external_scanner_deserialize(void *payload, const char *buffer, unsigned length)
{
    (void)payload;
    (void)buffer;
    (void)length;
}

bool tree_sitter_chibi_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid)
{
    (void)payload;
    if (!valid[AUTOMATIC_TERMINATOR]) {
        return false;
    }
    bool newline = false, comment_boundary = false;
    for (;;) {
        while (iswspace((wint_t)lexer->lookahead)) {
            newline |= lexer->lookahead == '\n' || lexer->lookahead == '\r';
            lexer->advance(lexer, true);
        }
        if (!comment_boundary) {
            lexer->mark_end(lexer);
        }
        if (lexer->lookahead != '/') {
            break;
        }
        /* Comments can contain the statement-separating newline. Keep the
         * token boundary before them so Tree-sitter still retains comments. */
        comment_boundary = true;
        lexer->advance(lexer, false);
        if (lexer->lookahead == '/') {
            while (lexer->lookahead && lexer->lookahead != '\n') {
                lexer->advance(lexer, false);
            }
            newline |= lexer->lookahead == '\n';
        } else if (lexer->lookahead == '*') {
            lexer->advance(lexer, false);
            int previous = 0;
            while (lexer->lookahead) {
                int current = lexer->lookahead;
                newline |= current == '\n' || current == '\r';
                lexer->advance(lexer, false);
                if (previous == '*' && current == '/') {
                    break;
                }
                previous = current;
            }
        } else {
            return false; /* Division continues an expression. */
        }
    }
    if (lexer->lookahead == '}' || lexer->eof(lexer)) {
        lexer->result_symbol = AUTOMATIC_TERMINATOR;
        return true;
    }
    if (!newline) {
        return false;
    }
    switch (lexer->lookahead) {
        case '+':
        case '-':
        case '*':
        case '/':
        case '%':
        case '&':
        case '|':
        case '^':
        case '!':
        case '<':
        case '>':
        case '=':
        case '.':
        case ',':
        case '?':
        case ':':
        case '(':
        case '[': return false;
    }
    lexer->result_symbol = AUTOMATIC_TERMINATOR;
    return true;
}
