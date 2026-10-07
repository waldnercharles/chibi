#include "assets/code_query.h"

#include "assets/code_value.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    av_doc *doc;
    code_lexer *lexer;
    code_token *token;
    char *error;
    size_t size;
} query_parser;

static av_value *fail(query_parser *p, const char *message)
{
    snprintf(p->error, p->size, "%d:%d: %s", p->token->line, p->token->column, message);
    return NULL;
}

static bool take(query_parser *p, const char *text)
{
    if (!code_token_is(p->lexer, *p->token, text)) {
        return false;
    }
    *p->token = code_lex_next(p->lexer);
    return true;
}

static av_value *term(query_parser *p, int depth)
{
    if (depth == 128) {
        return fail(p, "Query nesting exceeds 128 levels");
    }
    const char *op = NULL;
    const char *operators[] = { "has", "all", "any", "not" };
    for (int i = 0; i < 4; i++) {
        if (take(p, operators[i])) {
            op = operators[i];
            break;
        }
    }
    if (!op) {
        return fail(p, "Expected has, all, any or not in query");
    }
    if (!take(p, "(")) {
        return fail(p, "Expected '(' after query operator");
    }
    av_value *value = av_obj(p->doc);
    av_obj_add_str(p->doc, value, "op", op);
    if (!strcmp(op, "has")) {
        if (p->token->kind != CODE_TOKEN_STRING) {
            return fail(p, "Expected quoted component name in has");
        }
        size_t length = p->token->end - p->token->start;
        char *quoted = xmalloc(length + 1);
        memcpy(quoted, p->lexer->text + p->token->start, length);
        quoted[length] = 0;
        av_doc *string = code_value_read(quoted, NULL, 0);
        free(quoted);
        const av_value *name = string ? av_doc_root(string) : NULL;
        if (!av_is_str(name) || !av_get_len(name) ||
            memchr(av_get_str(name), 0, av_get_len(name))) {
            av_doc_free(string);
            return fail(p, "Invalid component name in query");
        }
        av_obj_add_val(p->doc, value, "component", av_clone(p->doc, name));
        av_doc_free(string);
        *p->token = code_lex_next(p->lexer);
        if (!take(p, ")")) {
            return fail(p, "has requires exactly one component name");
        }
        return value;
    }
    av_value *terms = av_obj_add_arr(p->doc, value, "terms");
    if (!take(p, ")")) {
        for (;;) {
            av_value *child = term(p, depth + 1);
            if (!child) {
                return NULL;
            }
            av_arr_add_val(terms, child);
            if (take(p, ")")) {
                break;
            }
            if (!take(p, ",")) {
                return fail(p, "Expected ',' or ')' after query term");
            }
        }
    }
    if (!strcmp(op, "not") && av_arr_size(terms) != 1) {
        return fail(p, "not requires exactly one query term");
    }
    return value;
}

av_value *code_query_parse(av_doc *doc, code_lexer *lexer, code_token *current, char *error, size_t size)
{
    query_parser p = { doc, lexer, current, error, size };
    return term(&p, 0);
}

static void append(char **out, const char *text)
{
    for (; *text; text++) {
        apush(*out, *text);
    }
}

static bool write_term(char **out, const av_value *value)
{
    const av_value *op = av_obj_get(value, "op");
    if (!av_is_str(op)) {
        return false;
    }
    const char *name = av_get_str(op);
    if (strcmp(name, "has") && strcmp(name, "all") && strcmp(name, "any") &&
        strcmp(name, "not")) {
        return false;
    }
    append(out, name);
    append(out, "(");
    if (!strcmp(name, "has")) {
        const av_value *component = av_obj_get(value, "component");
        if (!av_is_str(component)) {
            return false;
        }
        char *quoted = code_value_write(component);
        append(out, quoted);
        free(quoted);
        append(out, ")");
        return true;
    }
    const av_value *terms = av_obj_get(value, "terms");
    if (!av_is_arr(terms) || (!strcmp(name, "not") && av_arr_size(terms) != 1)) {
        return false;
    }
    for (size_t i = 0; i < av_arr_size(terms); i++) {
        if (i) {
            append(out, ", ");
        }
        if (!write_term(out, av_arr_get(terms, i))) {
            return false;
        }
    }
    append(out, ")");
    return true;
}

char *code_query_write(const av_value *query)
{
    char *text = NULL;
    bool valid = write_term(&text, query);
    apush(text, 0);
    char *result = valid ? xstrdup(text) : NULL;
    afree(text);
    return result;
}
