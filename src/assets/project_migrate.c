#include "assets/project_migrate.h"

#include "language/syntax.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    int file;
    TSNode library, name, body;
    bool waits;
} function;
typedef struct
{
    const char *const *sources;
    function *functions;
} corpus;
typedef struct
{
    int offset, order;
    const char *text;
} insertion;

static bool is(TSNode node, const char *type)
{
    return !strcmp(ts_node_type(node), type);
}
/* A name is an identifier or a quoted string; compare them without quotes. */
static bool same_name(const char *a_source, TSNode a, const char *b_source, TSNode b)
{
    uint32_t start_a = ts_node_start_byte(a), length_a = ts_node_end_byte(a) - start_a;
    uint32_t start_b = ts_node_start_byte(b), length_b = ts_node_end_byte(b) - start_b;
    if (is(a, "string")) {
        start_a++, length_a -= 2;
    }
    if (is(b, "string")) {
        start_b++, length_b -= 2;
    }
    return length_a == length_b &&
           !memcmp(a_source + start_a, b_source + start_b, length_a);
}
static char *name_text(const char *source, TSNode node)
{
    uint32_t start = ts_node_start_byte(node), length = ts_node_end_byte(node) - start;
    if (is(node, "string")) {
        start++, length -= 2;
    }
    char *text = xmalloc(length + 1);
    memcpy(text, source + start, length);
    text[length] = '\0';
    return text;
}
static int name_char(const char **p)
{
    *p += strspn(*p, "_ -");
    return tolower((unsigned char)(**p ? *(*p)++ : 0));
}
/* Library calls resolve through the catalog, which ignores case and the
 * separators `_`, ` ` and `-` (script_name_matches). */
static bool similar_name(const char *a_source, TSNode a, const char *b_source, TSNode b)
{
    char *x = name_text(a_source, a), *y = name_text(b_source, b);
    const char *p = x, *q = y;
    int c, d;
    do {
        c = name_char(&p), d = name_char(&q);
    } while (c == d && c);
    bool matches = c == d;
    free(x);
    free(y);
    return matches;
}
static bool is_name(TSNode node)
{
    return is(node, "identifier") || is(node, "string");
}
static void collect(TSNode node, int file, TSNode library, function **functions)
{
    if (is(node, "library")) {
        library = ts_node_child_by_field_name(node, "name", 4);
    }
    if (is(node, "function_declaration")) {
        apush(
            *functions,
            ((function){ .file = file,
                         .library = library,
                         .name = ts_node_child_by_field_name(node, "name", 4),
                         .body = ts_node_child_by_field_name(node, "body", 4) })
        );
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        collect(ts_node_named_child(node, i), file, library, functions);
    }
}
static TSNode outer(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    while (is(parent, "parenthesized_expression")) {
        node = parent;
        parent = ts_node_parent(parent);
    }
    return parent;
}
/* The function a call names: a local function by name, or a library function
 * as Library.Function in any file. */
static const function *callee(const corpus *c, int file, TSNode target)
{
    const char *source = c->sources[file];
    TSNode library = { 0 }, name = target;
    if (is(target, "member_expression")) {
        library = ts_node_child_by_field_name(target, "object", 6);
        name = ts_node_child_by_field_name(target, "member", 6);
        if (!is_name(library)) {
            return NULL;
        }
    }
    for (int i = 0; is_name(name) && i < acount(c->functions); i++) {
        const function *f = &c->functions[i];
        const char *defined = c->sources[f->file];
        bool local = ts_node_is_null(library);
        bool scope = local ? f->file == file
                           : !ts_node_is_null(f->library) &&
                                 similar_name(source, library, defined, f->library);
        if (scope && (local ? same_name(source, name, defined, f->name)
                            : similar_name(source, name, defined, f->name))) {
            return f;
        }
    }
    return NULL;
}
/* A call blocked under version 10 when it called a function that waits and
 * was not already awaited. An action's call runs when the action starts. */
static bool blocking(const corpus *c, int file, TSNode node)
{
    if (!is(node, "call_expression") || is(outer(node), "await_expression") ||
        is(outer(node), "action_declaration")) {
        return false;
    }
    const function *f = callee(c, file, ts_node_child_by_field_name(node, "function", 8));
    return f && f->waits;
}
static bool waits(const corpus *c, int file, TSNode node)
{
    if (is(node, "await_expression") || blocking(c, file, node)) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!is(child, "function_declaration") &&
            !is(child, "macro_declaration") && waits(c, file, child)) {
            return true;
        }
    }
    return false;
}
static void edits(const corpus *c, int file, TSNode node, insertion **out)
{
    if (blocking(c, file, node)) {
        TSNode parent = outer(node);
        bool wrap = (is(parent, "member_expression") || is(parent, "index_expression")) &&
                    ts_node_start_byte(
                        ts_node_child_by_field_name(parent, "object", 6)
                    ) == ts_node_start_byte(node);
        int order = acount(*out);
        apush(*out, ((insertion){ (int)ts_node_start_byte(node), order, wrap ? "(await " : "await " }));
        if (wrap) {
            apush(*out, ((insertion){ (int)ts_node_end_byte(node), order + 1, ")" }));
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        edits(c, file, ts_node_named_child(node, i), out);
    }
}
static int by_offset(const void *a, const void *b)
{
    const insertion *x = a, *y = b;
    return x->offset != y->offset ? x->offset - y->offset : x->order - y->order;
}
static char *apply(const char *source, insertion *inserts)
{
    if (!acount(inserts)) {
        return NULL;
    }
    qsort(inserts, (size_t)acount(inserts), sizeof *inserts, by_offset);
    size_t size = strlen(source) + 1;
    for (int i = 0; i < acount(inserts); i++) {
        size += strlen(inserts[i].text);
    }
    char *result = xmalloc(size), *at = result;
    int from = 0;
    for (int i = 0; i < acount(inserts); i++) {
        at = stpcpy(
            stpncpy(at, source + from, (size_t)(inserts[i].offset - from)),
            inserts[i].text
        );
        from = inserts[i].offset;
    }
    strcpy(at, source + from);
    return result;
}

char **chibi_migrate_task_calls(const char *const *sources, int count)
{
    corpus c = { .sources = sources };
    script_syntax **syntax = xmalloc((size_t)(count + 1) * sizeof *syntax);
    for (int i = 0; i < count; i++) {
        syntax[i] = script_parse(sources[i]);
        collect(ts_tree_root_node(syntax[i]->tree), i, (TSNode){ 0 }, &c.functions);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < acount(c.functions); i++) {
            function *f = &c.functions[i];
            if (!f->waits && waits(&c, f->file, f->body)) {
                f->waits = changed = true;
            }
        }
    }
    char **migrated = xmalloc((size_t)(count + 1) * sizeof *migrated);
    for (int i = 0; i < count; i++) {
        insertion *inserts = NULL;
        edits(&c, i, ts_tree_root_node(syntax[i]->tree), &inserts);
        migrated[i] = apply(sources[i], inserts);
        afree(inserts);
    }
    for (int i = 0; i < count; i++) {
        script_syntax_free(syntax[i]);
    }
    free(syntax);
    afree(c.functions);
    return migrated;
}

/* Skips a string or comment starting at `p`; returns `p` otherwise. */
static const char *skip_literal(const char *p)
{
    if (*p == '"') {
        for (p++; *p && *p != '"'; p++) {
            p += *p == '\\' && p[1];
        }
        return *p ? p + 1 : p;
    }
    if (p[0] == '/' && p[1] == '/') {
        return p + strcspn(p, "\n");
    }
    if (p[0] == '/' && p[1] == '*') {
        const char *close = strstr(p + 2, "*/");
        return close ? close + 2 : p + strlen(p);
    }
    return p;
}
static bool word_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}
/* An `&` after an operand is the binary operator; elsewhere it is unary. */
static bool after_operand(const char *source, const char *last)
{
    if (!last) {
        return false;
    }
    if (*last == ')' || *last == ']' || *last == '"') {
        return true;
    }
    if (!word_char(*last)) {
        return false;
    }
    const char *word = last;
    while (word > source && word_char(word[-1])) {
        word--;
    }
    /* Every version 11 keyword but the literals introduces an expression. */
    static const char *keywords[] = {
        "behavior", "library",     "system",   "without",     "state",
        "shared",   "action",      "value",    "local",       "event",
        "disabled", "on",          "after",    "as",          "bind",
        "events",   "pure",        "function", "macro",       "entry",
        "exit",     "result",      "owner",    "description", "domain",
        "dataflow", "depth_limit", "query",    "if",          "else",
        "while",    "for",         "foreach",  "in",          "match",
        "case",     "default",     "sequence", "branch",      "return",
        "goto",     "break",       "continue", "await",       "yield",
        "cancel",   "engine",      "cast",     "plain",       "ref",
        "service",  "setting",     "wildcard", "from",        "split"
    };
    for (size_t i = 0; i < sizeof keywords / sizeof *keywords; i++) {
        size_t n = strlen(keywords[i]);
        if ((size_t)(last - word + 1) == n && !strncmp(word, keywords[i], n)) {
            return false;
        }
    }
    return true;
}
/* `fallback(&x, ...)` and `fallback((&x), ...)` bound their first argument
 * through the reference. Returns where `binding: ` goes, or NULL. */
static const char *fallback_binding(const char *source, const char *last)
{
    const char *open = NULL;
    while (last && *last == '(') {
        open = last;
        do {
            last = last > source ? last - 1 : NULL;
        } while (last && isspace((unsigned char)*last));
    }
    if (!open || !last || last - source < 7) {
        return NULL;
    }
    const char *word = last - 7;
    if (strncmp(word, "fallback", 8) || (word > source && word_char(word[-1]))) {
        return NULL;
    }
    const char *after = strchr(word, '(') + 1;
    return after;
}
char *chibi_migrate_references(const char *source)
{
    insertion *inserts = NULL;
    char *result = NULL;
    const char *last = NULL;
    int removed = 0;
    for (const char *p = source; *p;) {
        const char *skip = skip_literal(p);
        if (skip != p) {
            last = *p == '"' ? skip - 1 : last;
            p = skip;
            continue;
        }
        if (p[0] == '&' && (p[1] == '&' || p[1] == '=')) {
            last = p + 1;
            p += 2;
            continue;
        }
        if (*p == '&' && !after_operand(source, last)) {
            const char *binding = fallback_binding(source, last);
            if (binding) {
                apush(inserts, ((insertion){ (int)(binding - source), acount(inserts), "binding: " }));
            }
            apush(inserts, ((insertion){ (int)(p - source), -1, NULL }));
            removed++;
            p++;
            continue;
        }
        if (!isspace((unsigned char)*p)) {
            last = p;
        }
        p++;
    }
    if (removed) {
        size_t size = strlen(source) + 1;
        for (int i = 0; i < acount(inserts); i++) {
            size += inserts[i].text ? strlen(inserts[i].text) : 0;
        }
        result = xmalloc(size);
        char *at = result;
        int from = 0;
        for (int i = 0; i < acount(inserts); i++) {
            at = stpncpy(at, source + from, (size_t)(inserts[i].offset - from));
            from = inserts[i].offset;
            if (inserts[i].text) {
                at = stpcpy(at, inserts[i].text);
            } else {
                from++;
            }
        }
        strcpy(at, source + from);
    }
    afree(inserts);
    return result;
}

/* A version 12 `result Name: T = e` declaration, removed from the source. */
typedef struct
{
    int offset;   /* Where it was, in the stripped source. */
    char *indent; /* Its line's indentation, or NULL when it shared a line. */
    char *name, *spelling, *value;
    bool assigned; /* The function also assigns the result. */
} declared;
static char *copy_text(const char *from, const char *to)
{
    while (to > from && isspace((unsigned char)to[-1])) {
        to--;
    }
    char *text = xmalloc((size_t)(to - from) + 1);
    memcpy(text, from, (size_t)(to - from));
    text[to - from] = '\0';
    return text;
}
/* The end of the expression at `p`: a newline, `;` or `}` outside brackets. */
static const char *expression_end(const char *p)
{
    int depth = 0;
    while (*p) {
        const char *skip = skip_literal(p);
        if (skip != p) {
            p = skip;
            continue;
        }
        if (!depth && (*p == '\n' || *p == ';' || *p == '}')) {
            return p;
        }
        depth += (*p == '(' || *p == '[' || *p == '{') -
                 (*p == ')' || *p == ']' || *p == '}');
        p++;
    }
    return p;
}
static bool statement_start(const char *source, const char *p)
{
    while (p > source && (p[-1] == ' ' || p[-1] == '\t')) {
        p--;
    }
    return p == source || p[-1] == '\n' || p[-1] == '{' || p[-1] == ';';
}
/* Parses `result Name: T = e` at `p`. Returns its end, or NULL. */
static const char *result_declaration(const char *source, const char *p, declared *out)
{
    if (strncmp(p, "result", 6) || word_char(p[6]) ||
        (p > source && word_char(p[-1])) || !statement_start(source, p)) {
        return NULL;
    }
    const char *name = p + 6;
    while (*name == ' ' || *name == '\t') {
        name++;
    }
    const char *colon = name;
    if (*colon == '"') {
        colon = skip_literal(colon);
    } else {
        while (word_char(*colon)) {
            colon++;
        }
    }
    if (colon == name) {
        return NULL;
    }
    const char *equals = colon;
    while (*equals && *equals != '=' && *equals != '\n') {
        const char *skip = skip_literal(equals);
        equals = skip != equals ? skip : equals + 1;
    }
    if (*equals != '=') {
        return NULL;
    }
    const char *value = equals + 1;
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    const char *end = expression_end(value);
    *out = (declared){ .name = *name == '"' ? copy_text(name + 1, colon - 1)
                                            : copy_text(name, colon),
                       .spelling = copy_text(name, colon),
                       .value = copy_text(value, end) };
    return end + (*end == ';');
}
/* Removes every result declaration, with its line when it stands alone. */
static char *strip_results(const char *source, declared **out)
{
    char *stripped = xmalloc(strlen(source) + 1), *at = stripped;
    for (const char *p = source; *p;) {
        const char *skip = skip_literal(p);
        if (skip != p) {
            at = stpncpy(at, p, (size_t)(skip - p));
            p = skip;
            continue;
        }
        declared d;
        const char *end = result_declaration(source, p, &d);
        if (!end) {
            *at++ = *p++;
            continue;
        }
        while (*end == ' ' || *end == '\t') {
            end++;
        }
        char *line = at;
        while (line > stripped && (line[-1] == ' ' || line[-1] == '\t')) {
            line--;
        }
        if ((line == stripped || line[-1] == '\n') && (*end == '\n' || !*end)) {
            d.indent = xmalloc((size_t)(at - line) + 1);
            memcpy(d.indent, line, (size_t)(at - line));
            d.indent[at - line] = '\0';
            at = line;
            end += *end == '\n';
        }
        d.offset = (int)(at - stripped);
        apush(*out, d);
        p = end;
    }
    *at = '\0';
    return stripped;
}
static TSNode enclosing_function(TSNode root, int offset)
{
    TSNode node = ts_node_descendant_for_byte_range(root, (uint32_t)offset, (uint32_t)offset);
    while (!ts_node_is_null(node) && !is(node, "function_declaration") &&
           !is(node, "macro_declaration")) {
        node = ts_node_parent(node);
    }
    return node;
}
static void returns(TSNode node, TSNode **out)
{
    if (is(node, "return_statement")) {
        apush(*out, node);
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!is(child, "function_declaration") && !is(child, "macro_declaration")) {
            returns(child, out);
        }
    }
}
/* Whether the function's own statements assign `name`. */
static bool assigns(const char *source, TSNode node, const char *name)
{
    if (is(node, "assignment")) {
        TSNode target = ts_node_child_by_field_name(node, "target", 6);
        uint32_t start = ts_node_start_byte(target),
                 length = ts_node_end_byte(target) - start;
        if (is(target, "string")) {
            start++, length -= 2;
        }
        if (is_name(target) && strlen(name) == length &&
            !strncmp(name, source + start, length)) {
            return true;
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!is(child, "function_declaration") &&
            !is(child, "macro_declaration") && assigns(source, child, name)) {
            return true;
        }
    }
    return false;
}
static const declared *declaration_of(const declared *all, const char *source, TSNode name)
{
    uint32_t start = ts_node_start_byte(name), length = ts_node_end_byte(name) - start;
    if (is(name, "string")) {
        start++, length -= 2;
    }
    for (int i = 0; i < acount(all); i++) {
        if (!all[i].assigned && strlen(all[i].name) == length &&
            !strncmp(all[i].name, source + start, length)) {
            return &all[i];
        }
    }
    return NULL;
}
static void append_value(char **text, const char *value, size_t length)
{
    if (acount(*text)) {
        apush(*text, ',');
        apush(*text, ' ');
    }
    for (size_t i = 0; i < length; i++) {
        apush(*text, value[i]);
    }
}
/* The value `return` gives: the declared expressions, or the result itself
 * for a result without one, in signature order. */
static char *return_value(const char *source, TSNode function, const declared *own)
{
    TSNode results = ts_node_child_by_field_name(function, "results", 7);
    char *text = NULL;
    int count = 0;
    for (uint32_t i = 0;
         !ts_node_is_null(results) && i < ts_node_named_child_count(results);
         i++, count++) {
        TSNode name = ts_node_child_by_field_name(ts_node_named_child(results, i), "name", 4);
        const declared *d = declaration_of(own, source, name);
        uint32_t start = ts_node_start_byte(name), end = ts_node_end_byte(name);
        append_value(&text, d ? d->value : source + start, d ? strlen(d->value) : end - start);
    }
    for (int i = 0; ts_node_is_null(results) && i < acount(own); i++, count++) {
        const char *value = own[i].assigned ? own[i].spelling : own[i].value;
        append_value(&text, value, strlen(value));
    }
    apush(text, '\0');
    size_t size = strlen(text) + 4;
    char *value = xmalloc(size);
    snprintf(value, size, count > 1 || *text == '(' ? "(%s)" : " %s", text);
    afree(text);
    return value;
}
/* Whether the body's own statements can reach its closing brace. A body
 * with `on` or `after` handlers finishes from them instead. */
static bool falls_through(TSNode body)
{
    TSNode last = { 0 };
    for (uint32_t i = 0; i < ts_node_named_child_count(body); i++) {
        TSNode child = ts_node_named_child(body, i);
        if (is(child, "handler")) {
            return false;
        }
        last = is(child, "comment") ? last : child;
    }
    return ts_node_is_null(last) || !is(last, "return_statement");
}
/* Appends `return` before the closing brace of a body that can reach it. */
static void append_return(
    const char *source,
    TSNode body,
    const char *value,
    insertion **out,
    char ***texts
)
{
    int close = (int)ts_node_end_byte(body) - 1;
    int line = close;
    while (line > 0 && (source[line - 1] == ' ' || source[line - 1] == '\t')) {
        line--;
    }
    size_t size = (size_t)(close - line) + strlen(value) + 16;
    char *text = xmalloc(size);
    if (line > 0 && source[line - 1] == '\n') {
        snprintf(text, size, "%.*s    return%s\n", close - line, source + line, value);
        apush(*out, ((insertion){ line, acount(*out), text }));
    } else {
        snprintf(text, size, "return%s ", value);
        apush(*out, ((insertion){ close, acount(*out), text }));
    }
    apush(*texts, text);
}
char *chibi_migrate_results(const char *source)
{
    declared *all = NULL;
    char *stripped = strip_results(source, &all);
    if (!acount(all)) {
        free(stripped);
        afree(all);
        return NULL;
    }
    script_syntax *syntax = script_parse(stripped);
    TSNode root = ts_tree_root_node(syntax->tree);
    insertion *inserts = NULL;
    char **texts = NULL;
    for (int i = 0; i < acount(all); i++) {
        TSNode function = enclosing_function(root, all[i].offset);
        assert(!ts_node_is_null(function));
        bool first = true;
        for (int j = 0; j < i; j++) {
            first &= !ts_node_eq(function, enclosing_function(root, all[j].offset));
        }
        if (!first) {
            continue;
        }
        TSNode body = ts_node_child_by_field_name(function, "body", 4);
        declared *own = NULL;
        for (int j = i; j < acount(all); j++) {
            if (!ts_node_eq(function, enclosing_function(root, all[j].offset))) {
                continue;
            }
            declared d = all[j];
            d.assigned = assigns(stripped, body, d.name);
            apush(own, d);
            if (d.assigned) {
                /* Version 12 returned the declared value until an assignment
                 * replaced it; set it where it was declared instead. */
                size_t size = strlen(d.spelling) + strlen(d.value) +
                              (d.indent ? strlen(d.indent) : 0) + 8;
                char *text = xmalloc(size);
                snprintf(
                    text,
                    size,
                    d.indent ? "%s%s = %s\n" : "%s%s = %s; ",
                    d.indent ? d.indent : "",
                    d.spelling,
                    d.value
                );
                apush(inserts, ((insertion){ d.offset, acount(inserts), text }));
                apush(texts, text);
            }
        }
        char *value = return_value(stripped, function, own);
        apush(texts, value);
        TSNode *found = NULL;
        returns(body, &found);
        for (int j = 0; j < acount(found); j++) {
            if (!ts_node_is_null(ts_node_child_by_field_name(found[j], "value", 5))) {
                continue;
            }
            TSNode outcome = ts_node_child_by_field_name(found[j], "outcome", 7);
            TSNode end = ts_node_is_null(outcome) ? ts_node_child(found[j], 0) : outcome;
            apush(inserts, ((insertion){ (int)ts_node_end_byte(end), acount(inserts), value }));
        }
        if (!acount(found) || falls_through(body)) {
            append_return(stripped, body, value, &inserts, &texts);
        }
        afree(found);
        afree(own);
    }
    char *migrated = apply(stripped, inserts);
    for (int i = 0; i < acount(texts); i++) {
        free(texts[i]);
    }
    for (int i = 0; i < acount(all); i++) {
        free(all[i].indent);
        free(all[i].name);
        free(all[i].spelling);
        free(all[i].value);
    }
    afree(texts);
    afree(inserts);
    afree(all);
    script_syntax_free(syntax);
    if (!migrated) {
        return stripped;
    }
    free(stripped);
    return migrated;
}
static char *read_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    char *text = NULL;
    size_t size = 0;
    if (!fseek(file, 0, SEEK_END)) {
        long end = ftell(file);
        if (end >= 0 && !fseek(file, 0, SEEK_SET)) {
            size = (size_t)end;
            text = xmalloc(size + 1);
            if (fread(text, 1, size, file) != size) {
                free(text);
                text = NULL;
            }
        }
    }
    fclose(file);
    if (text) {
        text[size] = '\0';
    }
    return text;
}
#define STAGED_SUFFIX ".migrating"
static char *staged_path(const char *path)
{
    size_t size = strlen(path) + sizeof STAGED_SUFFIX;
    char *staged = xmalloc(size);
    snprintf(staged, size, "%s" STAGED_SUFFIX, path);
    return staged;
}
/* Writes the migrated source beside the original, which stays untouched. */
static int stage_text(const char *path, const char *text, char *error, size_t error_size)
{
    char *staged = staged_path(path);
    /* Never write through a link someone left at the staging path. */
    int fd = open(staged, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !file) {
        close(fd);
    }
    size_t size = strlen(text);
    bool written = file && fwrite(text, 1, size, file) == size;
    written = file && !fclose(file) && written;
    if (!written) {
        snprintf(error, error_size, "Cannot write migrated source %s", staged);
        remove(staged);
    }
    free(staged);
    return written ? 0 : -1;
}
static bool has_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name), suffix_length = strlen(suffix);
    return length > suffix_length && !strcmp(name + length - suffix_length, suffix);
}
static int collect_sources(const char *directory, char ***paths, char *error, size_t error_size)
{
    DIR *dir = opendir(directory);
    if (!dir) {
        snprintf(error, error_size, "Cannot open %s: %s", directory, strerror(errno));
        return -1;
    }
    int status = 0;
    for (struct dirent *entry; !status && (entry = readdir(dir));) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        size_t size = strlen(directory) + strlen(entry->d_name) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s/%s", directory, entry->d_name);
        /* Like the source catalog, symlinks are not project sources. */
        struct stat info;
        if (lstat(path, &info)) {
            snprintf(error, error_size, "Cannot inspect %s", path);
            status = -1;
        } else if (S_ISDIR(info.st_mode)) {
            status = collect_sources(path, paths, error, error_size);
        } else if (S_ISREG(info.st_mode) && has_suffix(entry->d_name, ".chibi")) {
            apush(*paths, path);
            continue;
        }
        free(path);
    }
    closedir(dir);
    return status;
}
int chibi_project_migrate_content(const char *content, int from, char *error, size_t error_size)
{
    char **paths = NULL;
    int status = collect_sources(content, &paths, error, error_size);
    int count = acount(paths);
    char **sources = xmalloc((size_t)(count + 1) * sizeof *sources);
    for (int i = 0; i < count; i++) {
        sources[i] = status ? NULL : read_text(paths[i]);
        if (!status && !sources[i]) {
            snprintf(error, error_size, "Cannot read source %s", paths[i]);
            status = -1;
        }
    }
    char **migrated = NULL;
    if (!status) {
        switch (from) {
            case 10:
                migrated = chibi_migrate_task_calls((const char *const *)sources, count);
                break;
            case 11:
                migrated = xmalloc((size_t)(count + 1) * sizeof *migrated);
                for (int i = 0; i < count; i++) {
                    migrated[i] = chibi_migrate_references(sources[i]);
                }
                break;
            case 12:
                migrated = xmalloc((size_t)(count + 1) * sizeof *migrated);
                for (int i = 0; i < count; i++) {
                    migrated[i] = chibi_migrate_results(sources[i]);
                }
                break;
            default: assert(false);
        }
    }
    /* Every source is staged before any original is replaced, and a failed
     * step leaves the originals. Each step is idempotent, so a step
     * interrupted while replacing reruns cleanly. */
    int staged = 0;
    for (; !status && staged < count; staged++) {
        if (migrated[staged]) {
            status = stage_text(paths[staged], migrated[staged], error, error_size);
        }
    }
    for (int i = 0; i < staged; i++) {
        char *from = migrated[i] ? staged_path(paths[i]) : NULL;
        if (from && status) {
            remove(from);
        } else if (from && rename(from, paths[i])) {
            snprintf(error, error_size, "Cannot replace source %s", paths[i]);
            status = -1;
        }
        free(from);
    }
    for (int i = 0; i < count; i++) {
        free(migrated ? migrated[i] : NULL);
        free(sources[i]);
        free(paths[i]);
    }
    free(migrated);
    free(sources);
    afree(paths);
    return status;
}
