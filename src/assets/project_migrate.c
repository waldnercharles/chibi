#include "assets/project_migrate.h"

#include "language/syntax.h"
#include "support/array.h"
#include "support/asset_text.h"
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
    TSNode library, name, body, scope;
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
    /* An action has no body; it never waits and shadows functions. */
    if (is(node, "function_declaration") || is(node, "action_declaration")) {
        apush(
            *functions,
            ((function){ .file = file,
                         .library = library,
                         .name = ts_node_child_by_field_name(node, "name", 4),
                         .body = ts_node_child_by_field_name(node, "body", 4),
                         .scope = ts_node_parent(node) })
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
static bool contains(TSNode outer, TSNode node)
{
    return ts_node_start_byte(outer) <= ts_node_start_byte(node) &&
           ts_node_end_byte(node) <= ts_node_end_byte(outer);
}
static uint32_t span(TSNode node)
{
    return ts_node_end_byte(node) - ts_node_start_byte(node);
}
/* The function a call names: the innermost local function of that name whose
 * scope holds the call, or a library function as Library.Function in any
 * file. */
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
    bool local = ts_node_is_null(library);
    const function *found = NULL;
    for (int i = 0; is_name(name) && i < acount(c->functions); i++) {
        const function *f = &c->functions[i];
        const char *defined = c->sources[f->file];
        if (!local) {
            if (!ts_node_is_null(f->body) && !ts_node_is_null(f->library) &&
                similar_name(source, library, defined, f->library) &&
                similar_name(source, name, defined, f->name)) {
                return f;
            }
            continue;
        }
        if (f->file == file && contains(f->scope, target) &&
            same_name(source, name, defined, f->name) &&
            (!found || span(f->scope) < span(found->scope))) {
            found = f;
        }
    }
    return found;
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
            if (!f->waits && !ts_node_is_null(f->body) && waits(&c, f->file, f->body)) {
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
/* An open parenthesis. `start` holds until the current argument has a
 * token, and `arg` is where that argument begins. */
typedef struct
{
    bool fallback;
    bool wrap;
    bool start;
    const char *arg;
} paren;
static bool fallback_call(const char *source, const char *last)
{
    if (!last || last - source < 7 || strncmp(last - 7, "fallback", 8)) {
        return false;
    }
    return last - 7 == source || !word_char(last[-8]);
}
/* A positional `&x` at `p` in `fallback(...)`, also inside wrapping
 * parentheses, bound through the reference. Returns where `binding: `
 * goes, or NULL. */
static const char *fallback_binding(const paren *stack, const char *p)
{
    int top = acount(stack) - 1;
    if (top < 0 || !stack[top].start) {
        return NULL;
    }
    while (top > 0 && stack[top].wrap) {
        top--;
    }
    if (!stack[top].fallback) {
        return NULL;
    }
    return stack[top].arg ? stack[top].arg : p;
}
static void enter_token(paren *stack, const char *p)
{
    if (acount(stack) && stack[acount(stack) - 1].start) {
        stack[acount(stack) - 1].start = false;
        stack[acount(stack) - 1].arg = p;
    }
}
char *chibi_migrate_references(const char *source)
{
    insertion *inserts = NULL;
    paren *stack = NULL;
    char *result = NULL;
    const char *last = NULL;
    int removed = 0;
    for (const char *p = source; *p;) {
        const char *skip = skip_literal(p);
        if (skip != p) {
            if (*p == '"') {
                enter_token(stack, p);
                last = skip - 1;
            }
            p = skip;
            continue;
        }
        if (p[0] == '&' && (p[1] == '&' || p[1] == '=')) {
            enter_token(stack, p);
            last = p + 1;
            p += 2;
            continue;
        }
        if (*p == '&' && !after_operand(source, last)) {
            const char *binding = fallback_binding(stack, p);
            if (binding) {
                apush(inserts, ((insertion){ (int)(binding - source), acount(inserts), "binding: " }));
            }
            enter_token(stack, p);
            apush(inserts, ((insertion){ (int)(p - source), -1, NULL }));
            removed++;
            p++;
            continue;
        }
        if (*p == '(') {
            bool call = after_operand(source, last);
            bool wrap = !call && acount(stack) && stack[acount(stack) - 1].start;
            enter_token(stack, p);
            apush(stack, ((paren){ call && fallback_call(source, last), wrap, true, NULL }));
        } else if (*p == ')' && acount(stack)) {
            asetlen(stack, acount(stack) - 1);
            enter_token(stack, p);
        } else if (*p == ',' && acount(stack)) {
            stack[acount(stack) - 1].start = true;
            stack[acount(stack) - 1].arg = NULL;
        } else if (!isspace((unsigned char)*p)) {
            enter_token(stack, p);
        }
        if (!isspace((unsigned char)*p)) {
            last = p;
        }
        p++;
    }
    afree(stack);
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
/* Whether only blanks and block comments separate `p` from the start of a
 * statement. Block comments do not nest in code. */
static bool statement_start(const char *source, const char *p)
{
    for (;;) {
        while (p > source && (p[-1] == ' ' || p[-1] == '\t')) {
            p--;
        }
        if (p - source < 4 || p[-1] != '/' || p[-2] != '*') {
            break;
        }
        const char *open = p - 2;
        while (open > source && strncmp(open - 1, "/*", 2)) {
            open--;
        }
        if (open == source) {
            return false;
        }
        p = open - 1;
    }
    return p == source || p[-1] == '\n' || p[-1] == '{' || p[-1] == '}' || p[-1] == ';';
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
/* The innermost callable whose body holds `offset`. A stripped declaration
 * can sit exactly where a nested callable starts or just after one ends. */
static TSNode enclosing_function(TSNode root, int offset)
{
    TSNode node = ts_node_descendant_for_byte_range(root, (uint32_t)offset, (uint32_t)offset);
    for (; !ts_node_is_null(node); node = ts_node_parent(node)) {
        if (!is(node, "function_declaration") && !is(node, "macro_declaration")) {
            continue;
        }
        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (ts_node_start_byte(body) < (uint32_t)offset &&
            (uint32_t)offset < ts_node_end_byte(body)) {
            return node;
        }
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
static bool names(const char *source, TSNode node, const char *name)
{
    uint32_t start = ts_node_start_byte(node), length = ts_node_end_byte(node) - start;
    if (is(node, "string")) {
        start++, length -= 2;
    }
    return is_name(node) && strlen(name) == length &&
           !strncmp(name, source + start, length);
}
/* The variable a write lands in: `Values` for `Values[0].x = 1`. */
static TSNode written(TSNode target)
{
    while (is(target, "member_expression") || is(target, "index_expression")) {
        target = ts_node_child_by_field_name(target, "object", 6);
    }
    return target;
}
static bool ref_parameter(TSNode parameter)
{
    TSNode type = ts_node_child_by_field_name(parameter, "type", 4);
    for (uint32_t i = 0; !ts_node_is_null(type) && i < ts_node_child_count(type); i++) {
        if (is(ts_node_child(type, i), "ref")) {
            return true;
        }
    }
    return false;
}
/* The function, macro or action declared directly in `scope` and named like
 * `name`, if any. */
static TSNode callable_in(const char *text, TSNode scope, TSNode name)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(scope); i++) {
        TSNode child = ts_node_named_child(scope, i);
        if ((is(child, "function_declaration") ||
             is(child, "macro_declaration") || is(child, "action_declaration")) &&
            same_name(text, ts_node_child_by_field_name(child, "name", 4), text, name)) {
            return child;
        }
    }
    return (TSNode){ 0 };
}
/* Whether the callable `declaration` takes the `index`th or `label`ed
 * argument by `ref`. An action may write any argument. */
static bool takes_ref(const char *source, TSNode declaration, int index, TSNode label)
{
    if (is(declaration, "action_declaration")) {
        return true;
    }
    TSNode parameters = ts_node_child_by_field_name(declaration, "parameters", 10);
    for (uint32_t i = 0; i < ts_node_named_child_count(parameters); i++) {
        TSNode parameter = ts_node_named_child(parameters, i);
        bool matches = ts_node_is_null(label)
                         ? (int)i == index
                         : same_name(
                               source,
                               ts_node_child_by_field_name(parameter, "name", 4),
                               source,
                               label
                           );
        if (matches) {
            return ref_parameter(parameter);
        }
    }
    return false;
}
/* Whether `call` may write `argument`: the callee visible at the call takes
 * it by `ref`, or the callee is declared elsewhere and may. */
static bool by_reference(const char *source, TSNode call, TSNode argument, int index)
{
    TSNode callee = ts_node_child_by_field_name(call, "function", 8);
    if (!is_name(callee)) {
        return true;
    }
    TSNode label = (TSNode){ 0 };
    if (is(argument, "named_argument")) {
        label = ts_node_named_child(ts_node_child_by_field_name(argument, "name", 4), 0);
    }
    for (TSNode scope = ts_node_parent(call); !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        TSNode declaration = callable_in(source, scope, callee);
        if (!ts_node_is_null(declaration)) {
            return takes_ref(source, declaration, index, label);
        }
    }
    return true;
}
/* Whether the function's own statements write `name` before a local of the
 * same name shadows it. */
static bool assigns(const char *source, TSNode node, const char *name)
{
    if (is(node, "assignment") &&
        names(source, written(ts_node_child_by_field_name(node, "target", 6)), name)) {
        return true;
    }
    if (is(node, "argument_list")) {
        /* A `ref` parameter may write the argument it is given. */
        for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
            TSNode argument = ts_node_named_child(node, i), value = argument;
            if (is(value, "named_argument")) {
                value = ts_node_child_by_field_name(value, "value", 5);
            }
            if (names(source, written(value), name) &&
                by_reference(source, ts_node_parent(node), argument, (int)i)) {
                return true;
            }
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (is(child, "local_declaration") &&
            names(source, ts_node_child_by_field_name(child, "name", 4), name)) {
            return false; /* Later statements write the local. */
        }
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
/* A version 13 `value Name = e` declaration. */
typedef struct
{
    int offset; /* Of its name, in the rewritten source. */
    TSNode node;
    char *name, *expansion;
    bool expanding;
} lazy;
typedef struct
{
    int start, end;
    char *text;
} replacement;
/* Rewrites `value Name = e` as `Name := e` and `value Name: T = e` as
 * `Name: T = e`, which parse as states at the root and locals in callables. */
static char *strip_lazy(const char *source, lazy **out)
{
    char *text = xmalloc(strlen(source) + 1), *at = text;
    for (const char *p = source; *p;) {
        const char *skip = skip_literal(p);
        if (skip != p) {
            at = stpncpy(at, p, (size_t)(skip - p));
            p = skip;
            continue;
        }
        size_t keyword = strncmp(p, "value", 5) ? 0 : 5;
        const char *name = p + keyword;
        while (keyword && (*name == ' ' || *name == '\t')) {
            name++;
        }
        const char *after = *name == '"' ? skip_literal(name) : name;
        while (*after != '"' && word_char(*after)) {
            after++;
        }
        const char *sign = after;
        while (*sign == ' ' || *sign == '\t') {
            sign++;
        }
        if (!keyword || word_char(p[keyword]) || name == p + keyword ||
            after == name || (p > source && word_char(p[-1])) ||
            !statement_start(source, p) || (*sign != ':' && *sign != '=') ||
            sign[1] == '=' || (*sign == ':' && sign[1] == '=')) {
            *at++ = *p++;
            continue;
        }
        apush(*out, ((lazy){ .offset = (int)(at - text) }));
        at = stpncpy(at, name, (size_t)(after - name));
        if (*sign == '=') {
            at = stpcpy(at, " :=");
            p = sign + 1;
        } else {
            p = after;
        }
    }
    *at = '\0';
    return text;
}
static bool scope_node(TSNode node)
{
    return is(node, "declaration_body") || is(node, "system_body") ||
           is(node, "callable_body") || is(node, "block") ||
           is(node, "function_declaration") || is(node, "macro_declaration") ||
           is(node, "handler");
}
/* The lazy declaration directly in `scope` named like `name`, if any. */
static lazy *lazy_in(const char *text, lazy *all, TSNode scope, TSNode name)
{
    for (int i = 0; i < acount(all); i++) {
        if (ts_node_eq(ts_node_parent(all[i].node), scope) &&
            same_name(text, ts_node_child_by_field_name(all[i].node, "name", 4), text, name)) {
            return &all[i];
        }
    }
    return NULL;
}
/* Whether a local or parameter declared directly in `scope` is named like
 * `name`. */
static bool declares_name(const char *text, TSNode scope, TSNode name)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(scope); i++) {
        TSNode child = ts_node_named_child(scope, i);
        if (is(child, "parameter_list") && declares_name(text, child, name)) {
            return true;
        }
        if ((is(child, "local_declaration") ||
             is(child, "lazy_local_declaration") || is(child, "parameter")) &&
            same_name(text, ts_node_child_by_field_name(child, "name", 4), text, name)) {
            return true;
        }
    }
    return false;
}
/* The lazy declaration a read of `name` at `node` resolves to, if any. */
static lazy *resolve_lazy(const char *text, lazy *all, TSNode node, TSNode name)
{
    for (TSNode scope = ts_node_parent(node); !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        if (!scope_node(scope)) {
            continue;
        }
        lazy *found = lazy_in(text, all, scope, name);
        if (found || declares_name(text, scope, name)) {
            return found;
        }
    }
    return NULL;
}
static bool child_is(TSNode parent, const char *field, TSNode node)
{
    TSNode child = ts_node_child_by_field_name(parent, field, (uint32_t)strlen(field));
    return !ts_node_is_null(child) && ts_node_eq(child, node);
}
/* Whether `node` is an identifier in expression position. */
static bool read_name(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    return is(node, "identifier") && !ts_node_is_null(parent) &&
           !is(parent, "type") && !is(parent, "argument_name") &&
           !child_is(parent, "name", node) && !child_is(parent, "member", node) &&
           !child_is(parent, "alias", node) && !child_is(parent, "type", node) &&
           !child_is(parent, "function", node);
}
/* The lazy declaration `node` reads, as an identifier in expression position. */
static lazy *lazy_read(const char *text, lazy *all, TSNode node)
{
    return read_name(node) ? resolve_lazy(text, all, node, node) : NULL;
}
static bool encloses(TSNode outer, TSNode node)
{
    return ts_node_start_byte(outer) <= ts_node_start_byte(node) &&
           ts_node_end_byte(node) <= ts_node_end_byte(outer);
}
/* A name in `node`, an expression of the declaration `home`, that a
 * declaration between `site` and `home` would capture once expanded at `site`. */
static TSNode captured(const char *text, lazy *all, TSNode node, TSNode home, TSNode site)
{
    lazy *read = lazy_read(text, all, node);
    if (read) {
        return captured(text, all, ts_node_child_by_field_name(read->node, "value", 5), read->node, site);
    }
    TSNode parent = ts_node_parent(node);
    bool callee = is(node, "identifier") && is(parent, "call_expression") &&
                  child_is(parent, "function", node);
    if (read_name(node) || callee) {
        for (TSNode scope = ts_node_parent(site);
             !ts_node_is_null(scope) && !encloses(scope, home);
             scope = ts_node_parent(scope)) {
            bool shadows = callee ? !ts_node_is_null(callable_in(text, scope, node))
                                  : declares_name(text, scope, node) ||
                                        lazy_in(text, all, scope, node);
            if (scope_node(scope) && shadows) {
                return node;
            }
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode name = captured(text, all, ts_node_named_child(node, i), home, site);
        if (!ts_node_is_null(name)) {
            return name;
        }
    }
    return (TSNode){ 0 };
}
static bool primary(TSNode node)
{
    static const char *const kinds[] = {
        "identifier",
        "string",
        "number",
        "boolean",
        "nil",
        "call_expression",
        "member_expression",
        "index_expression",
        "parenthesized_expression",
        "cast_expression",
        "array_expression",
        "struct_expression",
        "builtin_reference",
        "engine_reference",
    };
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        if (is(node, kinds[i])) {
            return true;
        }
    }
    return false;
}
static char *expansion(const char *text, lazy *all, lazy *l);
/* The text of `node` with each lazy read replaced by its expansion. */
static void expand_into(const char *text, lazy *all, TSNode node, char **out)
{
    lazy *read = lazy_read(text, all, node);
    if (read) {
        const char *value = expansion(text, all, read);
        for (const char *c = value; *c; c++) {
            apush(*out, *c);
        }
        return;
    }
    uint32_t at = ts_node_start_byte(node);
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        for (; at < ts_node_start_byte(child); at++) {
            apush(*out, text[at]);
        }
        expand_into(text, all, child, out);
        at = ts_node_end_byte(child);
    }
    for (; at < ts_node_end_byte(node); at++) {
        apush(*out, text[at]);
    }
}
static char *expansion(const char *text, lazy *all, lazy *l)
{
    if (l->expansion) {
        return l->expansion;
    }
    assert(!l->expanding);
    l->expanding = true;
    TSNode value = ts_node_child_by_field_name(l->node, "value", 5);
    TSNode type = ts_node_child_by_field_name(l->node, "type", 4);
    while (!ts_node_is_null(type) && is(value, "parenthesized_expression")) {
        value = ts_node_child_by_field_name(value, "expression", 10);
    }
    char *out = NULL;
    const char *open = !ts_node_is_null(type) ? "cast<"
                     : primary(value)         ? ""
                                              : "(";
    for (const char *c = open; *c; c++) {
        apush(out, *c);
    }
    for (uint32_t i = ts_node_is_null(type) ? 0 : ts_node_start_byte(type);
         !ts_node_is_null(type) && i < ts_node_end_byte(type);
         i++) {
        apush(out, text[i]);
    }
    if (!ts_node_is_null(type)) {
        apush(out, '>');
        apush(out, '(');
    }
    expand_into(text, all, value, &out);
    if (*open) {
        apush(out, ')');
    }
    apush(out, '\0');
    l->expansion = xstrdup(out);
    afree(out);
    return l->expansion;
}
/* Replaces each lazy read outside lazy declarations. Returns a read whose
 * expansion another declaration would capture, which stops the migration. */
static TSNode lazy_reads(const char *text, lazy *all, TSNode node, replacement **out)
{
    for (int i = 0; i < acount(all); i++) {
        if (ts_node_eq(all[i].node, node)) {
            return (TSNode){ 0 };
        }
    }
    lazy *read = lazy_read(text, all, node);
    if (read) {
        TSNode name = captured(
            text,
            all,
            ts_node_child_by_field_name(read->node, "value", 5),
            read->node,
            node
        );
        if (!ts_node_is_null(name)) {
            return name;
        }
        apush(
            *out,
            ((replacement){ (int)ts_node_start_byte(node),
                            (int)ts_node_end_byte(node),
                            xstrdup(expansion(text, all, read)) })
        );
        return (TSNode){ 0 };
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode name = lazy_reads(text, all, ts_node_named_child(node, i), out);
        if (!ts_node_is_null(name)) {
            return name;
        }
    }
    return (TSNode){ 0 };
}
static int by_start(const void *a, const void *b)
{
    return ((const replacement *)a)->start - ((const replacement *)b)->start;
}
char *chibi_migrate_values(const char *source, char *error, size_t error_size)
{
    lazy *all = NULL;
    char *text = strip_lazy(source, &all);
    if (!acount(all)) {
        free(text);
        afree(all);
        return NULL;
    }
    script_syntax *syntax = script_parse(text);
    TSNode root = ts_tree_root_node(syntax->tree);
    for (int i = 0; i < acount(all); i++) {
        TSNode node = ts_node_descendant_for_byte_range(
            root,
            (uint32_t)all[i].offset,
            (uint32_t)all[i].offset
        );
        while (!ts_node_is_null(node) && !is(node, "state_declaration") &&
               !is(node, "local_declaration")) {
            node = ts_node_parent(node);
        }
        assert(
            !ts_node_is_null(node) &&
            ts_node_start_byte(node) == (uint32_t)all[i].offset
        );
        all[i].node = node;
    }
    replacement *edits = NULL;
    TSNode name = lazy_reads(text, all, root, &edits);
    if (!ts_node_is_null(name)) {
        uint32_t start = ts_node_start_byte(name);
        snprintf(
            error,
            error_size,
            "A value reads `%.*s`, which another declaration shadows where the "
            "value is read; rename one",
            (int)(ts_node_end_byte(name) - start),
            text + start
        );
        for (int i = 0; i < acount(edits); i++) {
            free(edits[i].text);
        }
        afree(edits);
        for (int i = 0; i < acount(all); i++) {
            free(all[i].expansion);
        }
        afree(all);
        script_syntax_free(syntax);
        free(text);
        return NULL;
    }
    for (int i = 0; i < acount(all); i++) {
        /* A declaration alone on its lines goes with them. */
        int start = all[i].offset, end = (int)ts_node_end_byte(all[i].node);
        while (start > 0 && (text[start - 1] == ' ' || text[start - 1] == '\t')) {
            start--;
        }
        while (text[end] == ' ' || text[end] == '\t' || text[end] == ';') {
            end++;
        }
        bool ended = text[end - 1] == '\n';
        bool alone = (start == 0 || text[start - 1] == '\n') &&
                     (ended || text[end] == '\n' || !text[end]);
        if (!alone) {
            start = all[i].offset;
            end -= ended;
        } else if (!ended && text[end] == '\n') {
            end++;
        }
        apush(edits, ((replacement){ start, end, xstrdup("") }));
    }
    qsort(edits, (size_t)acount(edits), sizeof *edits, by_start);
    char *result = NULL;
    int from = 0;
    for (int i = 0; i < acount(edits); i++) {
        for (; from < edits[i].start; from++) {
            apush(result, text[from]);
        }
        for (const char *c = edits[i].text; *c; c++) {
            apush(result, *c);
        }
        from = edits[i].end;
        free(edits[i].text);
    }
    for (; text[from]; from++) {
        apush(result, text[from]);
    }
    apush(result, '\0');
    char *migrated = xstrdup(result);
    afree(result);
    afree(edits);
    for (int i = 0; i < acount(all); i++) {
        free(all[i].expansion);
    }
    afree(all);
    script_syntax_free(syntax);
    free(text);
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
    /* Never write through a symbolic or hard link left at the staging path. */
    unlink(staged);
    int fd = open(staged, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    struct stat original;
    /* The replacement keeps the source's permissions. */
    if (fd >= 0 && (stat(path, &original) || fchmod(fd, original.st_mode & 07777))) {
        close(fd);
        fd = -1;
    }
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
char *chibi_migrate_scene_behaviors(const char *scene)
{
    char error[128];
    av_doc *document = asset_text_read(scene, error, sizeof error);
    av_value *root = document ? av_doc_root(document) : NULL;
    av_value *entities = av_is_obj(root) ? av_obj_get(root, "entities") : NULL;
    bool changed = false;
    for (size_t i = 0; av_is_arr(entities) && i < av_arr_size(entities); i++) {
        av_value *entity = av_arr_get(entities, i);
        /* Only scene entities name a prototype. */
        if (av_is_obj(entity) && av_is_str(av_obj_get(entity, "prototype")) &&
            !av_obj_get(entity, "behaviors")) {
            av_obj_add_arr(document, entity, "behaviors");
            changed = true;
        }
    }
    char *migrated = changed ? asset_text_write(root) : NULL;
    av_doc_free(document);
    return migrated;
}
static int collect_sources(const char *directory, const char *suffix, char ***paths, char *error, size_t error_size)
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
            status = collect_sources(path, suffix, paths, error, error_size);
        } else if (S_ISREG(info.st_mode) && has_suffix(entry->d_name, suffix)) {
            apush(*paths, path);
            continue;
        }
        free(path);
    }
    closedir(dir);
    return status;
}
static bool collected(char **paths, const char *path)
{
    for (int i = 0; i < acount(paths); i++) {
        if (!strcmp(paths[i], path)) {
            return true;
        }
    }
    return false;
}
int chibi_project_migrate_content(
    const char *content,
    const char *start_scene,
    int from,
    char *error,
    size_t error_size
)
{
    char **paths = NULL;
    /* Version 15 changes scenes, wherever the project keeps them; every
     * earlier step changes scripts. */
    bool scenes = from == 14;
    int status = collect_sources(
        content,
        scenes ? DECLARATIVE_ASSET_EXTENSION : ".chibi",
        &paths,
        error,
        error_size
    );
    /* The scan skips hidden entries, and the start scene may lie outside the
     * content, so add it unless the scan found it. */
    struct stat info;
    if (!status && scenes && start_scene && !collected(paths, start_scene) &&
        !stat(start_scene, &info) && S_ISREG(info.st_mode)) {
        apush(paths, xstrdup(start_scene));
    }
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
            case 13:
                migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
                for (int i = 0; i < count && !status; i++) {
                    error[0] = '\0';
                    migrated[i] = chibi_migrate_values(sources[i], error, error_size);
                    status = error[0] ? -1 : 0;
                }
                break;
            case 14:
                migrated = xmalloc((size_t)(count + 1) * sizeof *migrated);
                for (int i = 0; i < count; i++) {
                    migrated[i] = chibi_migrate_scene_behaviors(sources[i]);
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
