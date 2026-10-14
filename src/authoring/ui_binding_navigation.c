#include "authoring/ui_binding_navigation.h"

#include "support/array.h"
#include "support/markup_expression.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_DEPTH = 64,
    MAX_TOKENS = 65536,
    MAX_REFERENCES = 65536,
    MAX_BINDINGS = 4096
};
typedef struct
{
    const char *name;
    script_type type;
    bool known, ambiguous;
} alias;
typedef struct
{
    script_type type;
    const char *path;
    bool known, ambiguous;
} value;
typedef struct
{
    int start, end, kind;
    const char *text;
} token;
enum
{
    END,
    WORD,
    STRING,
    NUMBER,
    PUNCT
};
typedef struct
{
    const ui_source *source;
    const script_catalog *catalog;
    const authoring_ui_binding *bindings;
    int binding_count;
    arena *storage;
    authoring_ui_binding_reference *references;
    alias **scopes;
    int *lines;
    bool failed;
    int tokens, aliases;
} query;
typedef struct
{
    query *query;
    const alias *aliases;
    char *text;
    int *starts, *ends, length, at;
    token token;
} parser;
static const char *copy(arena *storage, const char *text, int length)
{
    char *out = arena_alloc(storage, (size_t)length + 1);
    memcpy(out, text, (size_t)length);
    out[length] = 0;
    return out;
}
static bool word_start(unsigned char c)
{
    return isalpha(c) || c == '_' || c >= 128;
}
static bool word_char(unsigned char c)
{
    return word_start(c) || isdigit(c);
}
static void decoded(parser *p, int start, int end)
{
    markup_expression text;
    markup_expression_decode(p->query->source->text, start, end, &text);
    p->text = text.text;
    p->starts = text.starts;
    p->ends = text.ends;
    p->length = text.length;
}
static void next(parser *p)
{
    if (++p->query->tokens > MAX_TOKENS) {
        p->query->failed = true;
        p->token = (token){ .kind = END };
        return;
    }
    while (p->at < p->length && isspace((unsigned char)p->text[p->at])) {
        p->at++;
    }
    int start = p->at, end = start;
    int kind = END;
    if (start < p->length) {
        unsigned char c = (unsigned char)p->text[p->at++];
        end = p->at;
        kind = PUNCT;
        if (word_start(c)) {
            kind = WORD;
            while (p->at < p->length && word_char((unsigned char)p->text[p->at])) {
                p->at++;
            }
            end = p->at;
        } else if (isdigit(c)) {
            kind = NUMBER;
            while (p->at < p->length && (isalnum((unsigned char)p->text[p->at]) ||
                                         p->text[p->at] == '.')) {
                p->at++;
            }
            end = p->at;
        } else if (c == '\'' || c == '"') {
            kind = STRING;
            start = p->at;
            while (p->at < p->length && p->text[p->at] != c) {
                if (p->text[p->at] == '\\' && p->at + 1 < p->length) {
                    p->at++;
                }
                p->at++;
            }
            end = p->at;
            p->at += p->at < p->length;
        } else if (p->at < p->length) {
            char d = p->text[p->at];
            if ((c == '&' && d == '&') || (c == '|' && d == '|') ||
                (d == '=' && (c == '=' || c == '!' || c == '<' || c == '>'))) {
                p->at++;
                end = p->at;
            }
        }
    }
    p->token = (token){ start,
                        end,
                        kind,
                        copy(p->query->storage, p->text + start, end - start) };
}
static bool is(parser *p, const char *text)
{
    return p->token.kind == PUNCT && !strcmp(p->token.text, text);
}
static script_range range(parser *p, token t)
{
    int start = p->starts[t.start], end = p->ends[t.end - 1], lo = 0,
        hi = acount(p->query->lines);
    while (lo + 1 < hi) {
        int mid = (lo + hi) / 2;
        if (p->query->lines[mid] <= start) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (script_range){ start, end, lo + 1, start - p->query->lines[lo] + 1 };
}
static void reference(parser *p, value receiver, token member, uint64_t field, bool unresolved)
{
    query *q = p->query;
    if (acount(q->references) >= MAX_REFERENCES) {
        q->failed = true;
        return;
    }
    if (member.end <= member.start) {
        return;
    }
    apush(
        q->references,
        ((
            authoring_ui_binding_reference
        ){ member.text, receiver.type.nominal, field, range(p, member), unresolved })
    );
}
static value root(parser *p, const char *name)
{
    if (!strcmp(name, "self")) {
        return (value){ .path = "" };
    }
    for (int i = acount(p->aliases) - 1; i >= 0; i--) {
        if (!strcmp(p->aliases[i].name, name)) {
            return (value){ .type = p->aliases[i].type,
                            .known = p->aliases[i].known,
                            .ambiguous = p->aliases[i].ambiguous };
        }
    }
    return (value){ 0 };
}
static value member(parser *p, value receiver, token name)
{
    query *q = p->query;
    if (receiver.ambiguous) {
        reference(p, receiver, name, 0, true);
        return (value){ .ambiguous = true };
    }
    if (receiver.path && !receiver.known) {
        int size = (int)strlen(receiver.path) + (int)strlen(name.text) + 2;
        char *path = arena_alloc(q->storage, (size_t)size);
        snprintf(path, (size_t)size, "%s%s%s", receiver.path, *receiver.path ? "." : "", name.text);
        for (int i = 0; i < q->binding_count; i++) {
            if (!strcmp(path, q->bindings[i].name)) {
                return (value){ .type = q->bindings[i].type,
                                .known = !q->bindings[i].ambiguous,
                                .ambiguous = q->bindings[i].ambiguous,
                                .path = path };
            }
        }
        if (*receiver.path) {
            reference(p, receiver, name, 0, true);
        }
        return (value){ .path = path, .ambiguous = receiver.ambiguous };
    }
    const script_catalog_type
        *type = receiver.known
                  ? script_catalog_type_by_id(q->catalog, receiver.type.nominal)
                  : NULL;
    if (type && receiver.type.container == PIN_CONTAINER_SINGLE) {
        for (int i = 0; i < type->field_count; i++) {
            if (!strcmp(type->fields[i].name, name.text)) {
                reference(p, receiver, name, type->fields[i].id, false);
                return (value){ .type = type->fields[i].type, .known = true };
            }
        }
    }
    if (!receiver.known || receiver.ambiguous) {
        reference(p, receiver, name, 0, true);
    }
    return (value){ .ambiguous = true };
}
static value expression(parser *, int, int);
static value primary(parser *p, int depth)
{
    if (depth > MAX_DEPTH || p->query->failed) {
        p->query->failed = true;
        return (value){ 0 };
    }
    value v = { 0 };
    token t = p->token;
    if (is(p, "(")) {
        next(p);
        v = expression(p, 0, depth + 1);
        if (is(p, ")")) {
            next(p);
        }
    } else if (is(p, "!") || is(p, "-") || is(p, "+")) {
        bool logical = is(p, "!");
        next(p);
        v = primary(p, depth + 1);
        v.path = NULL;
        if (logical) {
            v = (value){ .known = true, .type = { .storage = PIN_BOOL } };
        } else if (v.type.storage == PIN_STRUCT) {
            v = (value){ 0 };
        }
    } else if (t.kind == WORD) {
        v = root(p, t.text);
        next(p);
    } else if (t.kind == STRING || t.kind == NUMBER) {
        v.known = true;
        v.type.storage = t.kind == STRING ? PIN_STR : PIN_F64;
        next(p);
    } else {
        if (t.kind != END) {
            next(p);
        }
        return v;
    }
    for (;;) {
        if (is(p, ".")) {
            next(p);
            token field = p->token;
            if (field.kind != WORD) {
                return (value){ 0 };
            }
            v = member(p, v, field);
            next(p);
        } else if (is(p, "[")) {
            next(p);
            token key = p->token;
            if (v.known && v.type.container != PIN_CONTAINER_SINGLE) {
                expression(p, 0, depth + 1);
                v.type.container = PIN_CONTAINER_SINGLE;
                v.path = NULL;
            } else if (key.kind == STRING) {
                next(p);
                if (strchr(key.text, '\\')) {
                    reference(p, v, key, 0, true);
                    v = (value){ .ambiguous = true };
                } else {
                    v = member(p, v, key);
                }
            } else {
                expression(p, 0, depth + 1);
                reference(p, v, key, 0, true);
                v = (value){ .ambiguous = true };
            }
            if (is(p, "]")) {
                next(p);
            }
        } else if (is(p, "(")) {
            next(p);
            while (p->token.kind != END && !is(p, ")") && !p->query->failed) {
                int at = p->at;
                expression(p, 0, depth + 1);
                if (is(p, ",")) {
                    next(p);
                }
                if (p->at == at) {
                    next(p);
                }
            }
            if (is(p, ")")) {
                next(p);
            }
            v = (value){ 0 };
        } else {
            return v;
        }
    }
}
static int precedence(parser *p)
{
    if (p->token.kind != PUNCT) {
        return -1;
    }
    const char *s = p->token.text;
    if (!strcmp(s, "||")) {
        return 1;
    }
    if (!strcmp(s, "&&")) {
        return 2;
    }
    if (!strcmp(s, "==") || !strcmp(s, "!=")) {
        return 3;
    }
    if (!strcmp(s, "<") || !strcmp(s, ">") || !strcmp(s, "<=") || !strcmp(s, ">=")) {
        return 4;
    }
    if (!strcmp(s, "+") || !strcmp(s, "-")) {
        return 5;
    }
    if (!strcmp(s, "*") || !strcmp(s, "/") || !strcmp(s, "%")) {
        return 6;
    }
    return -1;
}
static value expression(parser *p, int minimum, int depth)
{
    value left = primary(p, depth);
    while (!p->query->failed) {
        int level = precedence(p);
        if (level < minimum) {
            break;
        }
        next(p);
        value right = expression(p, level + 1, depth + 1);
        left = level <= 4 ? (value){ .known = true, .type = { .storage = PIN_BOOL } }
             : left.known && right.known && left.type.storage != PIN_STRUCT &&
                       script_type_equal(left.type, right.type)
                 ? left
                 : (value){ 0 };
        left.path = NULL;
    }
    if (minimum == 0 && is(p, "?")) {
        next(p);
        value yes = expression(p, 0, depth + 1);
        if (is(p, ":")) {
            next(p);
        }
        value no = expression(p, 0, depth + 1);
        left = yes.known && no.known && script_type_equal(yes.type, no.type)
                 ? yes
                 : (value){ 0 };
        left.path = NULL;
    }
    return left;
}
static value walk(query *q, const alias *aliases, int start, int end)
{
    if (start < 0 || end <= start || q->failed) {
        return (value){ 0 };
    }
    parser p = { .query = q, .aliases = aliases };
    decoded(&p, start, end);
    next(&p);
    value result = expression(&p, 0, 0);
    while (p.token.kind != END && !q->failed) {
        int at = p.at;
        expression(&p, 0, 0);
        if (at == p.at) {
            next(&p);
        }
    }
    free(p.text);
    free(p.starts);
    free(p.ends);
    return result;
}
static void alias_set(alias **rows, const char *name, value v)
{
    for (int i = 0; i < acount(*rows); i++) {
        if (!strcmp((*rows)[i].name, name)) {
            (*rows)[i] = (alias){ name, v.type, v.known, v.ambiguous };
            return;
        }
    }
    apush(*rows, ((alias){ name, v.type, v.known, v.ambiguous }));
}
static bool attr(const ui_source *s, const ui_source_attribute *a, const char *name)
{
    return a->name_end - a->name_start == (int)strlen(name) &&
           !strncmp(s->text + a->name_start, name, strlen(name));
}
static void scopes(query *q)
{
    const ui_source *s = q->source;
    asetlen(q->scopes, acount(s->elements));
    if (acount(q->scopes)) {
        memset(q->scopes, 0, (size_t)acount(q->scopes) * sizeof *q->scopes);
    }
    for (int i = 0; i < acount(s->elements) && !q->failed; i++) {
        const ui_source_element *e = &s->elements[i];
        alias *rows = NULL;
        assert(e->parent < i);
        if (e->parent >= 0) {
            const alias *parent = q->scopes[e->parent];
            for (int j = 0; j < acount(parent); j++) {
                apush(rows, parent[j]);
            }
        }
        for (int j = 0; j < acount(e->attributes); j++) {
            const ui_source_attribute *a = &e->attributes[j];
            if (a->value_start < 0) {
                continue;
            }
            int start = a->value_start, end = a->value_end;
            if (attr(s, a, "data-for")) {
                int colon = start;
                while (colon < end && s->text[colon] != ':') {
                    colon++;
                }
                if (colon == end) {
                    continue;
                }
                value list = walk(q, rows, colon + 1, end);
                if (list.known && list.type.container != PIN_CONTAINER_SINGLE) {
                    list.type.container = PIN_CONTAINER_SINGLE;
                } else {
                    list.known = false;
                }
                int alias_index = 0;
                for (int at = start; at < colon;) {
                    while (at < colon && !word_start((unsigned char)s->text[at])) {
                        at++;
                    }
                    int first = at;
                    while (at < colon && word_char((unsigned char)s->text[at])) {
                        at++;
                    }
                    if (at == first) {
                        break;
                    }
                    value v = alias_index++ == 0
                                ? list
                                : (value){ .known = true,
                                           .type = { .storage = PIN_I32 } };
                    alias_set(&rows, copy(q->storage, s->text + first, at - first), v);
                }
            } else if (a->name_end - a->name_start > 11 && !strncmp(s->text + a->name_start, "data-alias-", 11)) {
                value v = walk(q, rows, start, end);
                alias_set(
                    &rows,
                    copy(q->storage, s->text + a->name_start + 11, a->name_end - a->name_start - 11),
                    v
                );
            }
        }
        q->aliases += acount(rows);
        if (q->aliases > MAX_REFERENCES) {
            q->failed = true;
        }
        q->scopes[i] = rows;
    }
}
static int containing(const ui_source *s, int at)
{
    int lo = 0, hi = acount(s->elements);
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s->elements[mid].start <= at) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    int i = lo - 1;
    while (i >= 0 && s->elements[i].end <= at) {
        i = s->elements[i].parent;
    }
    return i;
}
bool authoring_ui_binding_references(
    const ui_source *source,
    const script_catalog *catalog,
    const authoring_ui_binding *bindings,
    int count,
    arena *storage,
    authoring_ui_binding_reference **rows,
    int *row_count,
    char *error,
    size_t size
)
{
    *rows = NULL;
    *row_count = 0;
    if (size) {
        *error = 0;
    }
    if (!source || source->language != UI_SOURCE_RML || !catalog || count < 0 ||
        count > MAX_BINDINGS || (count && !bindings)) {
        if (size) {
            snprintf(error, size, "Typed UI references require markup and at most4096bindings");
        }
        return false;
    }
    query q = { .source = source,
                .catalog = catalog,
                .bindings = bindings,
                .binding_count = count,
                .storage = storage };
    apush(q.lines, 0);
    for (int i = 0; source->text[i]; i++) {
        if (source->text[i] == '\n') {
            apush(q.lines, i + 1);
        }
    }
    scopes(&q);
    for (int i = 0; i < acount(source->elements); i++) {
        const ui_source_element *e = &source->elements[i];
        for (int j = 0; j < acount(e->attributes); j++) {
            const ui_source_attribute *a = &e->attributes[j];
            if (a->value_start >= 0 && !attr(source, a, "data-for") &&
                !(a->name_end - a->name_start > 11 &&
                  !strncmp(source->text + a->name_start, "data-alias-", 11)) &&
                ui_source_is_binding(source->text + a->name_start, a->name_end - a->name_start)) {
                walk(&q, q.scopes[i], a->value_start, a->value_end);
            }
        }
    }
    int length = (int)strlen(source->text), span = 0;
    for (int at = 0; at + 1 < length && !q.failed; at++) {
        while (span < acount(source->spans) && source->spans[span].end <= at) {
            span++;
        }
        if (span < acount(source->spans) && source->spans[span].start <= at &&
            source->spans[span].kind == UI_SOURCE_COMMENT) {
            at = source->spans[span].end - 1;
            continue;
        }
        if (source->text[at] != '{' || source->text[at + 1] != '{') {
            continue;
        }
        int element = containing(source, at);
        if (element >= 0 && (at < source->elements[element].open_end ||
                             !strcmp(source->elements[element].tag, "style") ||
                             !strcmp(source->elements[element].tag, "script"))) {
            continue;
        }
        int end = at + 2;
        char quote = 0;
        while (end + 1 < length) {
            char c = source->text[end];
            if (quote) {
                if (c == '\\' && end + 1 < length) {
                    end++;
                } else if (c == quote) {
                    quote = 0;
                }
            } else if (c == '\'' || c == '"') {
                quote = c;
            } else if (c == '}' && source->text[end + 1] == '}') {
                break;
            }
            end++;
        }
        if (end + 1 < length) {
            walk(&q, element >= 0 ? q.scopes[element] : NULL, at + 2, end);
            at = end + 1;
        }
    }
    if (!q.failed) {
        *row_count = acount(q.references);
        *rows = arena_alloc(storage, (size_t)*row_count * sizeof **rows);
        if (*row_count) {
            memcpy(*rows, q.references, (size_t)*row_count * sizeof **rows);
        }
    } else if (size) {
        snprintf(error, size, "UI binding analysis exceeded its bounded token/depth/reference budget");
    }
    for (int i = 0; i < acount(q.scopes); i++) {
        afree(q.scopes[i]);
    }
    afree(q.scopes);
    afree(q.references);
    afree(q.lines);
    return !q.failed;
}
