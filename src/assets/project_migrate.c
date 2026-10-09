#include "assets/project_migrate.h"

#include "language/syntax.h"
#include "runtime/input_action.h"
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
#include <yyjson.h>

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
/* Names resolve through the catalog, which ignores case and the separators
 * `_`, ` ` and `-` (script_name_matches). */
static bool names_match(const char *a, const char *b)
{
    int c, d;
    do {
        c = name_char(&a), d = name_char(&b);
    } while (c == d && c);
    return c == d;
}
static bool similar_name(const char *a_source, TSNode a, const char *b_source, TSNode b)
{
    char *x = name_text(a_source, a), *y = name_text(b_source, b);
    bool matches = names_match(x, y);
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
    bool alias; /* Names storage, which each read and write reaches. */
} lazy;
typedef struct
{
    int start, end;
    char *text;
    int order; /* Breaks ties between edits at one offset. */
} replacement;
/* Skips the whitespace and comments the grammar allows between tokens. */
static const char *gap(const char *p)
{
    for (;;) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        const char *skip = *p == '/' ? skip_literal(p) : p;
        if (skip == p) {
            return p;
        }
        p = skip;
    }
}
/* Rewrites `value Name = e` as `Name := e` and `value Name: T = e` as
 * `Name: T = e`, which parse as states at the root and locals in callables.
 * `keyword` is `value` or `local`, both five letters. A multi-result type
 * `(a: T, b: U)` is dropped, since such a local is inferred. */
static char *strip_lazy(const char *source, const char *keyword_text, lazy **out)
{
    char *text = xmalloc(strlen(source) + 1), *at = text;
    for (const char *p = source; *p;) {
        const char *skip = skip_literal(p);
        if (skip != p) {
            at = stpncpy(at, p, (size_t)(skip - p));
            p = skip;
            continue;
        }
        size_t keyword = strncmp(p, keyword_text, 5) ? 0 : 5;
        const char *name = p + keyword;
        name = keyword ? gap(name) : name;
        const char *after = *name == '"' ? skip_literal(name) : name;
        while (*after != '"' && word_char(*after)) {
            after++;
        }
        const char *sign = gap(after);
        if (!keyword || word_char(p[keyword]) || name == p + keyword ||
            after == name || (p > source && word_char(p[-1])) ||
            !statement_start(source, p) || (*sign != ':' && *sign != '=') ||
            sign[1] == '=' || (*sign == ':' && sign[1] == '=')) {
            *at++ = *p++;
            continue;
        }
        apush(*out, ((lazy){ .offset = (int)(at - text) }));
        at = stpncpy(at, name, (size_t)(after - name));
        const char *tuple = *sign == ':' ? gap(sign + 1) : sign + 1;
        if (*sign == ':' && *tuple == '(') {
            int depth = 0;
            do {
                const char *skip = skip_literal(tuple);
                if (skip != tuple) {
                    tuple = skip;
                    continue;
                }
                depth += (*tuple == '(') - (*tuple == ')');
                tuple++;
            } while (depth && *tuple);
            tuple = gap(tuple);
            sign = *tuple == '=' ? tuple : sign;
        }
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
           is(node, "handler") || is(node, "system");
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
        if ((is(child, "local_declaration") || is(child, "parameter")) &&
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
static lazy *local_read(const char *text, lazy *all, TSNode node);
static void expand_into(const char *text, lazy *all, TSNode node, char **out)
{
    lazy *read = local_read(text, all, node);
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
    TSNode type = l->alias ? (TSNode){ 0 }
                           : ts_node_child_by_field_name(l->node, "type", 4);
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
                            xstrdup(expansion(text, all, read)),
                            0 })
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
int chibi_migrate_values(const char *source, char **migrated, char *error, size_t error_size)
{
    lazy *all = NULL;
    char *text = strip_lazy(source, "value", &all);
    *migrated = NULL;
    if (!acount(all)) {
        free(text);
        afree(all);
        return 0;
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
        return -1;
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
        apush(edits, ((replacement){ start, end, xstrdup(""), 0 }));
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
    *migrated = xstrdup(result);
    afree(result);
    afree(edits);
    for (int i = 0; i < acount(all); i++) {
        free(all[i].expansion);
    }
    afree(all);
    script_syntax_free(syntax);
    free(text);
    return 0;
}

/* A read of a version 16 local: `X`, or `local.X`, which `node` spans. */
typedef struct
{
    int local;
    TSNode node, unit;
} local_use;
/* `local.X` names the root local despite any nearer `X`. */
static lazy *qualified(const char *text, lazy *all, TSNode node, TSNode name)
{
    for (TSNode scope = ts_node_parent(node); !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        lazy *found = scope_node(scope) ? lazy_in(text, all, scope, name) : NULL;
        if (found) {
            return found;
        }
    }
    return NULL;
}
/* The local `node` reads, if any. */
static lazy *local_read(const char *text, lazy *all, TSNode node)
{
    if (is(node, "member_expression")) {
        TSNode object = ts_node_child_by_field_name(node, "object", 6);
        TSNode member = ts_node_child_by_field_name(node, "member", 6);
        uint32_t start = ts_node_start_byte(object);
        return is(object, "identifier") && ts_node_end_byte(object) - start == 5 &&
                       !strncmp(text + start, "local", 5)
                 ? qualified(text, all, node, member)
                 : NULL;
    }
    return lazy_read(text, all, node);
}
/* The handler, callable or system whose activation evaluates `node`, or
 * null for an action argument or binding, which is evaluated on its own. */
static TSNode unit_of(TSNode node)
{
    for (; !ts_node_is_null(node); node = ts_node_parent(node)) {
        if (is(node, "action_declaration") || is(node, "event_declaration") ||
            is(node, "bind_declaration")) {
            return (TSNode){ 0 };
        }
        if (is(node, "handler") || is(node, "function_declaration") ||
            is(node, "macro_declaration") || is(node, "entry_declaration") ||
            is(node, "system_body")) {
            return node;
        }
    }
    return node;
}
static bool container(TSNode node)
{
    return is(node, "block") || is(node, "callable_body") || is(node, "system_body");
}
static int lazy_index(const lazy *all, const lazy *l)
{
    return l ? (int)(l - all) : -1;
}
/* Collects reads outside local declarations, and rewrites `local.X` as `X`. */
static void local_uses(const char *text, lazy *all, TSNode node, local_use **uses, replacement **out)
{
    for (int i = 0; i < acount(all); i++) {
        if (ts_node_eq(all[i].node, node)) {
            return;
        }
    }
    lazy *read = local_read(text, all, node);
    if (read && ts_node_is_null(unit_of(node))) {
        /* An action argument or binding reads it where it is evaluated. */
        apush(
            *out,
            ((replacement){ (int)ts_node_start_byte(node),
                            (int)ts_node_end_byte(node),
                            xstrdup(expansion(text, all, read)),
                            0 })
        );
        return;
    }
    if (read) {
        apush(*uses, ((local_use){ lazy_index(all, read), node, unit_of(node) }));
        if (is(node, "member_expression")) {
            apush(
                *out,
                ((replacement){ (int)ts_node_start_byte(node),
                                (int)ts_node_end_byte(node),
                                xstrdup(read->name),
                                0 })
            );
        }
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        local_uses(text, all, ts_node_named_child(node, i), uses, out);
    }
}
/* The declaration text of `l` with `local.X` reads spelled `X`, and the
 * locals it reads in `depends`. */
static void local_text(const char *text, lazy *all, TSNode node, char **out, int **depends)
{
    lazy *read = local_read(text, all, node);
    if (read) {
        apush(*depends, lazy_index(all, read));
        if (read->alias || is(node, "member_expression")) {
            for (const char *c = read->alias ? expansion(text, all, read) : read->name;
                 *c;
                 c++) {
                apush(*out, *c);
            }
            return;
        }
    }
    uint32_t at = ts_node_start_byte(node);
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        for (; at < ts_node_start_byte(child); at++) {
            apush(*out, text[at]);
        }
        if (read) {
            for (; at < ts_node_end_byte(child); at++) {
                apush(*out, text[at]);
            }
            continue;
        }
        local_text(text, all, child, out, depends);
        at = ts_node_end_byte(child);
    }
    for (; at < ts_node_end_byte(node); at++) {
        apush(*out, text[at]);
    }
}
/* The local's name as an identifier: `"Remaining Health"` reads as
 * `RemainingHealth`, since a quoted name in an expression is a string. */
static char *local_name(const char *text, TSNode name)
{
    const char *from = text + ts_node_start_byte(name),
               *to = text + ts_node_end_byte(name);
    if (*from != '"') {
        return copy_text(from, to);
    }
    char *out = xmalloc((size_t)(to - from) + 2), *at = out;
    if (isdigit((unsigned char)from[1])) {
        *at++ = '_';
    }
    for (const char *c = from + 1; c < to - 1; c++) {
        if (isalnum((unsigned char)*c) || *c == '_') {
            *at++ = *c;
        }
    }
    *at = '\0';
    return out;
}
/* Whether `node` holds an identifier spelled `name`. */
static bool spells(const char *text, TSNode node, const char *name)
{
    if (is(node, "identifier")) {
        size_t length = ts_node_end_byte(node) - ts_node_start_byte(node);
        return length == strlen(name) &&
               !strncmp(text + ts_node_start_byte(node), name, length);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (spells(text, ts_node_named_child(node, i), name)) {
            return true;
        }
    }
    return false;
}
static int by_start_order(const void *a, const void *b)
{
    const replacement *x = a, *y = b;
    return x->start != y->start ? x->start - y->start : x->order - y->order;
}
static int local_fail(char *error, size_t error_size, const char *text, TSNode node, const char *format)
{
    uint32_t start = ts_node_start_byte(node);
    if (error_size) {
        snprintf(error, error_size, format, (int)(ts_node_end_byte(node) - start), text + start);
    }
    return -1;
}
/* Whether a parameter or local other than `own` in `scope` is named `name`. */
static bool redeclares(const char *text, TSNode scope, TSNode name, TSNode own)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(scope); i++) {
        TSNode child = ts_node_named_child(scope, i);
        if (is(child, "parameter_list") && redeclares(text, child, name, own)) {
            return true;
        }
        if ((is(child, "local_declaration") || is(child, "parameter")) &&
            !ts_node_eq(child, own) &&
            same_name(text, ts_node_child_by_field_name(child, "name", 4), text, name)) {
            return true;
        }
    }
    return false;
}
/* Whether `node` evaluates to the same value wherever and however often it
 * runs: it reads no names, calls nothing and builds no fresh array or
 * struct. */
static bool constant(TSNode node)
{
    if (is(node, "identifier") || is(node, "call_expression") ||
        is(node, "await_expression") || is(node, "array_expression") ||
        is(node, "struct_expression")) {
        return false;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!constant(ts_node_named_child(node, i))) {
            return false;
        }
    }
    return true;
}
static bool in_loop(TSNode node, TSNode unit)
{
    for (; !ts_node_eq(node, unit); node = ts_node_parent(node)) {
        if (is(node, "while_statement") || is(node, "for_statement") ||
            is(node, "foreach_statement")) {
            return true;
        }
    }
    return false;
}
/* Replaces each read of local `index` in `unit` with its expression, where a
 * declaration of its name would clash with another. */
static int inline_local(
    const char *text,
    lazy *all,
    int index,
    TSNode unit,
    local_use **uses,
    replacement **out,
    char *error,
    size_t error_size
)
{
    TSNode value = ts_node_child_by_field_name(all[index].node, "value", 5);
    int reads = 0;
    for (int i = 0; i < acount(*uses); i++) {
        local_use u = (*uses)[i];
        if (u.local == index && ts_node_eq(u.unit, unit)) {
            reads += in_loop(u.node, unit) ? 2 : 1;
        }
    }
    /* Each run evaluated the local once; an expansion runs at every read. */
    if (reads > 1 && !constant(value) && !all[index].alias) {
        return local_fail(
            error,
            error_size,
            text,
            ts_node_child_by_field_name(all[index].node, "name", 4),
            "Local `%.*s` is read more than once where another declaration "
            "has its name; rename one before migrating"
        );
    }
    for (int i = 0; i < acount(*uses); i++) {
        local_use u = (*uses)[i];
        if (u.local != index || !ts_node_eq(u.unit, unit)) {
            continue;
        }
        TSNode shadowed = captured(text, all, value, all[index].node, u.node);
        if (!ts_node_is_null(shadowed)) {
            return local_fail(error, error_size, text, shadowed, "A local reads `%.*s`, which another declaration shadows where the local is read; rename one");
        }
        /* Another local placed here reads this one by its name. */
        if (!is(u.node, "identifier") && !is(u.node, "member_expression")) {
            return local_fail(
                error,
                error_size,
                text,
                ts_node_child_by_field_name(all[index].node, "name", 4),
                "Another declaration where local `%.*s` is read has its name; "
                "rename one"
            );
        }
        for (int j = 0; j < acount(*out); j++) {
            if ((*out)[j].start == (int)ts_node_start_byte(u.node) &&
                (*out)[j].end == (int)ts_node_end_byte(u.node)) {
                free((*out)[j].text);
                (*out)[j] = (*out)[acount(*out) - 1];
                asetlen(*out, acount(*out) - 1);
                break;
            }
        }
        apush(
            *out,
            ((replacement){ (int)ts_node_start_byte(u.node),
                            (int)ts_node_end_byte(u.node),
                            xstrdup(expansion(text, all, &all[index])),
                            0 })
        );
    }
    return 0;
}
/* Whether reaching the statement of `block` that holds `read` always reaches
 * `read`: a condition or loop header is always read, a branch or loop body
 * or the right side of `&&` and `||` is not. */
static bool always_reads(const char *text, TSNode read, TSNode block)
{
    static const char *const branching[] = {
        "if_statement",      "while_statement", "for_statement",
        "foreach_statement", "match_statement", "branch_statement",
        "label_statement"
    };
    static const char *const headers[] = { "condition", "value", "first", "last" };
    for (TSNode child = read, at = ts_node_parent(read); !ts_node_eq(at, block);
         child = at, at = ts_node_parent(at)) {
        bool header = false;
        for (size_t i = 0; i < sizeof headers / sizeof *headers; i++) {
            header |= child_is(at, headers[i], child);
        }
        for (size_t i = 0; !header && i < sizeof branching / sizeof *branching; i++) {
            if (is(at, branching[i])) {
                return false;
            }
        }
        TSNode op = ts_node_child_by_field_name(at, "operator", 8);
        if (is(at, "binary_expression") && child_is(at, "right", child) &&
            !ts_node_is_null(op) &&
            (!strncmp(text + ts_node_start_byte(op), "&&", 2) ||
             !strncmp(text + ts_node_start_byte(op), "||", 2))) {
            return false;
        }
    }
    return true;
}
/* Whether a state, parameter or local declared around `name` has its name. */
static bool names_storage(const char *text, TSNode name)
{
    for (TSNode scope = ts_node_parent(name); !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        if (!scope_node(scope)) {
            continue;
        }
        if (redeclares(text, scope, name, (TSNode){ 0 })) {
            return true;
        }
        for (uint32_t i = 0; i < ts_node_named_child_count(scope); i++) {
            TSNode child = ts_node_named_child(scope, i);
            if (is(child, "state_declaration") &&
                same_name(text, ts_node_child_by_field_name(child, "name", 4), text, name)) {
                return true;
            }
        }
    }
    return false;
}
/* Whether `call` reads a component, as `get_component` or its engine name. */
static bool reads_component(const char *text, TSNode call)
{
    TSNode callee = ts_node_child_by_field_name(call, "function", 8);
    TSNode name = is(callee, "engine_reference")
                    ? ts_node_child_by_field_name(callee, "name", 4)
                    : callee;
    const char *spelled = is(callee, "engine_reference") ? "\"Get Component\""
                                                         : "get_component";
    uint32_t start = ts_node_start_byte(name);
    return ts_node_end_byte(name) - start == strlen(spelled) &&
           !strncmp(text + start, spelled, strlen(spelled));
}
/* Whether `node` names storage, as a state, component, parameter or local
 * path, which version 16 read and wrote through a local initialized with it.
 * `self` alone is the owner, not storage. */
static bool storage_path(const char *text, lazy *all, TSNode node)
{
    lazy *read = local_read(text, all, node);
    if (read) {
        return read->alias;
    }
    if (is(node, "member_expression") || is(node, "index_expression")) {
        TSNode object = ts_node_child_by_field_name(node, "object", 6);
        uint32_t start = ts_node_start_byte(object);
        return (is(object, "identifier") && ts_node_end_byte(object) - start == 4 &&
                !strncmp(text + start, "self", 4)) ||
               storage_path(text, all, object);
    }
    if (is(node, "parenthesized_expression")) {
        return storage_path(text, all, ts_node_child_by_field_name(node, "expression", 10));
    }
    if (is(node, "call_expression")) {
        return reads_component(text, node);
    }
    uint32_t start = ts_node_start_byte(node);
    return (is(node, "builtin_reference") && !strncmp(text + start, "state", 5)) ||
           (is(node, "identifier") && names_storage(text, node));
}
/* Whether a storage path reads a local evaluated once or calls something,
 * which expanding it at each use would evaluate again. */
static bool computed(const char *text, lazy *all, TSNode node)
{
    lazy *read = local_read(text, all, node);
    if (read) {
        return !read->alias;
    }
    if (is(node, "await_expression")) {
        return true;
    }
    if (is(node, "call_expression")) {
        if (!reads_component(text, node)) {
            return true;
        }
        node = ts_node_child_by_field_name(node, "arguments", 9);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (computed(text, all, ts_node_named_child(node, i))) {
            return true;
        }
    }
    return false;
}
/* Whether evaluating `node` calls something, directly or through the first
 * read of a local whose initializer does. */
static bool calls(const char *text, lazy *all, TSNode node)
{
    lazy *read = local_read(text, all, node);
    if (read) {
        return !read->alias &&
               calls(text, all, ts_node_child_by_field_name(read->node, "value", 5));
    }
    if (is(node, "call_expression") || is(node, "await_expression")) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (calls(text, all, ts_node_named_child(node, i))) {
            return true;
        }
    }
    return false;
}
/* Counts the reads of each local `node` evaluates, through other locals. */
static void count_reads(const char *text, lazy *all, TSNode node, int *reads)
{
    lazy *read = local_read(text, all, node);
    if (read) {
        reads[lazy_index(all, read)]++;
        if (!read->alias) {
            count_reads(text, all, ts_node_child_by_field_name(read->node, "value", 5), reads);
        }
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        count_reads(text, all, ts_node_named_child(node, i), reads);
    }
}
/* A local an action, event or binding reads more than once, which its
 * expansion would evaluate at every read, or null. */
static lazy *repeated_argument(const char *text, lazy *all, TSNode node)
{
    if (is(node, "action_declaration") || is(node, "event_declaration") ||
        is(node, "bind_declaration")) {
        int *reads = xcalloc((size_t)acount(all), sizeof *reads);
        count_reads(text, all, node, reads);
        lazy *found = NULL;
        for (int i = 0; !found && i < acount(all); i++) {
            if (reads[i] > 1 && !all[i].alias &&
                !constant(ts_node_child_by_field_name(all[i].node, "value", 5))) {
                found = &all[i];
            }
        }
        free(reads);
        return found;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        lazy *found = repeated_argument(text, all, ts_node_named_child(node, i));
        if (found) {
            return found;
        }
    }
    return NULL;
}
/* Whether `node` calls something that runs before `read` is evaluated. */
static bool calls_before(const char *text, lazy *all, TSNode node, TSNode read)
{
    if (ts_node_start_byte(node) >= ts_node_start_byte(read)) {
        return false;
    }
    if (!encloses(node, read)) {
        return calls(text, all, node);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (calls_before(text, all, ts_node_named_child(node, i), read)) {
            return true;
        }
    }
    return false;
}
/* Declares `l` in `unit` before the first statement that reads it. */
static int place_local(
    const char *text,
    lazy *all,
    int index,
    TSNode unit,
    local_use **uses,
    replacement **out,
    char *error,
    size_t error_size
)
{
    lazy *l = &all[index];
    if (l->alias) {
        return inline_local(text, all, index, unit, uses, out, error, error_size);
    }
    TSNode first = { 0 }, block = { 0 };
    for (int i = 0; i < acount(*uses); i++) {
        local_use *u = &(*uses)[i];
        if (u->local != index || !ts_node_eq(u->unit, unit)) {
            continue;
        }
        if (ts_node_is_null(first) ||
            ts_node_start_byte(u->node) < ts_node_start_byte(first)) {
            first = u->node;
        }
        if (ts_node_is_null(block)) {
            block = ts_node_parent(u->node);
        }
        while (!container(block) || !encloses(block, u->node)) {
            block = ts_node_parent(block);
        }
    }
    TSNode statement = first;
    while (!ts_node_eq(ts_node_parent(statement), block)) {
        statement = ts_node_parent(statement);
    }
    /* Each run evaluates it once, so it goes before the outermost loop. */
    for (TSNode at = block; !ts_node_eq(at, unit); at = ts_node_parent(at)) {
        if (is(at, "while_statement") || is(at, "for_statement") ||
            is(at, "foreach_statement")) {
            statement = at;
        }
    }
    block = ts_node_parent(statement);
    while (!container(block)) {
        statement = block;
        block = ts_node_parent(block);
    }
    TSNode value = ts_node_child_by_field_name(l->node, "value", 5);
    TSNode name = ts_node_child_by_field_name(l->node, "name", 4);
    for (TSNode scope = block; !ts_node_eq(scope, ts_node_parent(unit));
         scope = ts_node_parent(scope)) {
        if (scope_node(scope) && redeclares(text, scope, name, l->node)) {
            return inline_local(text, all, index, unit, uses, out, error, error_size);
        }
    }
    /* Assigning a local before reading it skipped its initializer. */
    TSNode target = ts_node_parent(first);
    TSNode op = ts_node_child_by_field_name(target, "operator", 8);
    if (!constant(value) && is(target, "assignment") &&
        child_is(target, "target", first) &&
        ts_node_end_byte(op) - ts_node_start_byte(op) == 1) {
        return local_fail(
            error,
            error_size,
            text,
            ts_node_child_by_field_name(l->node, "name", 4),
            "Local `%.*s` is assigned before it is read; declare it there "
            "before migrating"
        );
    }
    /* A local was evaluated at its first read, which a branch or loop
     * body might skip or reach after the state it reads changed. */
    if (!constant(value) && (!always_reads(text, first, block) ||
                             calls_before(text, all, statement, first))) {
        return local_fail(
            error,
            error_size,
            text,
            ts_node_child_by_field_name(l->node, "name", 4),
            "Local `%.*s` is first read in a branch or loop, or after a call; "
            "declare it where it should run before migrating"
        );
    }
    TSNode shadowed = captured(text, all, value, l->node, statement);
    if (!ts_node_is_null(shadowed)) {
        return local_fail(error, error_size, text, shadowed, "A local reads `%.*s`, which another declaration shadows where the local is read; rename one");
    }
    char *declaration = NULL;
    int *depends = NULL;
    for (const char *c = l->name; *c; c++) {
        apush(declaration, *c);
    }
    local_text(text, all, l->node, &declaration, &depends);
    /* The spelling of `name` gives way to the identifier. */
    uint32_t spelled = ts_node_end_byte(name) - ts_node_start_byte(name);
    memmove(
        declaration + strlen(l->name),
        declaration + strlen(l->name) + spelled,
        acount(declaration) - strlen(l->name) - spelled
    );
    asetlen(declaration, acount(declaration) - (int)spelled);
    while (acount(declaration) &&
           (declaration[acount(declaration) - 1] == ';' ||
            isspace((unsigned char)declaration[acount(declaration) - 1]))) {
        asetlen(declaration, acount(declaration) - 1);
    }
    uint32_t at = ts_node_start_byte(statement), line = at;
    while (line > 0 && (text[line - 1] == ' ' || text[line - 1] == '\t')) {
        line--;
    }
    bool own_line = line == 0 || text[line - 1] == '\n';
    if (own_line) {
        apush(declaration, '\n');
        for (uint32_t i = line; i < at; i++) {
            apush(declaration, text[i]);
        }
    } else {
        apush(declaration, ';');
        apush(declaration, ' ');
    }
    apush(declaration, '\0');
    apush(*out, ((replacement){ (int)at, (int)at, xstrdup(declaration), -acount(*out) - 1 }));
    afree(declaration);
    /* What it reads is placed later, so a later edit at this offset sorts
     * first and declares it before this one. */
    for (int i = 0; i < acount(depends); i++) {
        if (!all[depends[i]].alias) {
            apush(*uses, ((local_use){ depends[i], statement, unit }));
        }
    }
    afree(depends);
    return 0;
}
enum
{
    LOCAL_UNSEEN,
    LOCAL_VISITING,
    LOCAL_ORDERED
};
/* Orders the locals `index` reads before it; false when one reads itself. */
static bool depends_first(const char *text, lazy *all, int index, int *seen, int **order)
{
    if (seen[index] != LOCAL_UNSEEN) {
        return seen[index] == LOCAL_ORDERED;
    }
    seen[index] = LOCAL_VISITING;
    char *ignored = NULL;
    int *depends = NULL;
    local_text(text, all, ts_node_child_by_field_name(all[index].node, "value", 5), &ignored, &depends);
    afree(ignored);
    bool ordered = true;
    for (int i = 0; ordered && i < acount(depends); i++) {
        ordered = depends_first(text, all, depends[i], seen, order);
    }
    afree(depends);
    seen[index] = LOCAL_ORDERED;
    apush(*order, index);
    return ordered;
}
int chibi_migrate_locals(const char *source, char **migrated, char *error, size_t error_size)
{
    *migrated = NULL;
    for (const char *p = source; *p;) {
        const char *skip = skip_literal(p);
        if (skip != p) {
            p = skip;
            continue;
        }
        if (strncmp(p, "disabled", 8)) {
            p++;
            continue;
        }
        const char *word = gap(p + 8);
        if (word > p + 8 && !strncmp(word, "local", 5) && !word_char(word[5]) &&
            (p == source || !word_char(p[-1])) && statement_start(source, p)) {
            if (error_size) {
                snprintf(error, error_size, "%s", "A local is disabled; enable or remove it before migrating");
            }
            return -1;
        }
        p++;
    }
    lazy *all = NULL;
    char *text = strip_lazy(source, "local", &all);
    if (!acount(all)) {
        free(text);
        afree(all);
        return 0;
    }
    script_syntax *syntax = script_parse(text);
    TSNode root = ts_tree_root_node(syntax->tree);
    int status = 0;
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
        all[i].name = local_name(text, ts_node_child_by_field_name(node, "name", 4));
        TSNode quoted = ts_node_child_by_field_name(node, "name", 4);
        bool unique = *all[i].name && !spells(text, root, all[i].name);
        for (int j = 0; unique && j < i; j++) {
            unique = strcmp(all[j].name, all[i].name) != 0;
        }
        if (!status && text[ts_node_start_byte(quoted)] == '"' && !unique) {
            status = local_fail(
                error,
                error_size,
                text,
                quoted,
                "Local %.*s has no identifier of its own; rename it before "
                "migrating"
            );
        }
    }
    /* Each local is placed after the locals that read it, so their
     * declarations count as its reads. */
    int *seen = xcalloc((size_t)acount(all), sizeof *seen);
    int *order = NULL;
    for (int i = 0; i < acount(all); i++) {
        if (!depends_first(text, all, i, seen, &order) && !status) {
            status = local_fail(
                error,
                error_size,
                text,
                ts_node_child_by_field_name(all[i].node, "name", 4),
                "Local `%.*s` reads itself; break the cycle before migrating"
            );
        }
    }
    free(seen);
    for (int k = 0; !status && k < acount(order); k++) {
        lazy *l = &all[order[k]];
        TSNode value = ts_node_child_by_field_name(l->node, "value", 5);
        l->alias = storage_path(text, all, value);
        if (l->alias && computed(text, all, value)) {
            status = local_fail(
                error,
                error_size,
                text,
                ts_node_child_by_field_name(l->node, "name", 4),
                "Local `%.*s` names storage through a computed value; declare "
                "that value where it should run before migrating"
            );
        }
    }
    lazy *repeated = status ? NULL : repeated_argument(text, all, root);
    if (repeated) {
        status = local_fail(
            error,
            error_size,
            text,
            ts_node_child_by_field_name(repeated->node, "name", 4),
            "Local `%.*s` is read more than once by an action, event or "
            "binding; declare it where it should run before migrating"
        );
    }
    replacement *edits = NULL;
    local_use *uses = NULL;
    if (!status) {
        local_uses(text, all, root, &uses, &edits);
    }
    for (int k = acount(order) - 1; !status && k >= 0; k--) {
        int index = order[k];
        TSNode *units = NULL;
        for (int i = 0; i < acount(uses); i++) {
            bool seen = uses[i].local != index;
            for (int j = 0; !seen && j < acount(units); j++) {
                seen = ts_node_eq(units[j], uses[i].unit);
            }
            if (!seen) {
                apush(units, uses[i].unit);
            }
        }
        for (int j = 0; !status && j < acount(units); j++) {
            status = place_local(text, all, index, units[j], &uses, &edits, error, error_size);
        }
        afree(units);
    }
    afree(order);
    afree(uses);
    for (int i = 0; !status && i < acount(all); i++) {
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
        apush(edits, ((replacement){ start, end, xstrdup(""), 0 }));
    }
    char *result = NULL;
    if (!status) {
        qsort(edits, (size_t)acount(edits), sizeof *edits, by_start_order);
        int from = 0;
        for (int i = 0; i < acount(edits); i++) {
            for (; from < edits[i].start; from++) {
                apush(result, text[from]);
            }
            for (const char *c = edits[i].text; *c; c++) {
                apush(result, *c);
            }
            from = edits[i].end;
        }
        for (; text[from]; from++) {
            apush(result, text[from]);
        }
        apush(result, '\0');
        script_syntax *check = script_parse(result);
        if (ts_node_has_error(ts_tree_root_node(check->tree))) {
            status = -1;
            if (error_size) {
                snprintf(error, error_size, "%s", "A migrated local does not parse, such as a quoted name that is a keyword; rename it before migrating");
            }
        } else {
            *migrated = xstrdup(result);
        }
        script_syntax_free(check);
    }
    afree(result);
    for (int i = 0; i < acount(edits); i++) {
        free(edits[i].text);
    }
    afree(edits);
    for (int i = 0; i < acount(all); i++) {
        free(all[i].name);
        free(all[i].expansion);
    }
    afree(all);
    script_syntax_free(syntax);
    free(text);
    return status;
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
    /* The replacement keeps the source's permissions; a new file gets the
     * usual ones. */
    struct stat original = { .st_mode = 0644 };
    if (fd >= 0 && ((stat(path, &original) && errno != ENOENT) ||
                    fchmod(fd, original.st_mode & 07777))) {
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
/* Adds one binding's control label to its action in the input map `map`. A
 * binding without an action or a control, or a disabled one, never held
 * anything. Reading the map checks the labels. */
static void move_binding(const av_value *binding, av_doc *map)
{
    const av_value *action = av_obj_get(binding, "action");
    const av_value *source = av_obj_get(binding, "source");
    const av_value *disabled = av_obj_get(binding, "disabled");
    const av_value *firing = av_obj_get(binding, "firing");
    if (!av_is_str(action) || !av_get_str(action)[0] || !av_is_str(source) ||
        !strcmp(av_get_str(source), "None") ||
        (av_is_bool(disabled) && av_get_bool(disabled))) {
        return;
    }
    av_value *root = av_doc_root(map);
    av_value *entry = av_obj_get(root, av_get_str(action));
    if (!av_is_obj(entry)) {
        entry = av_obj_add_obj(map, root, av_get_str(action));
    }
    av_value *sources = av_obj_get(entry, "sources");
    if (!av_is_arr(sources)) {
        sources = av_obj_add_arr(map, entry, "sources");
    }
    bool listed = false;
    for (size_t i = 0; i < av_arr_size(sources) && !listed; i++) {
        const av_value *label = av_arr_get(sources, i);
        listed = av_is_str(label) && !strcmp(av_get_str(label), av_get_str(source));
    }
    if (!listed) {
        av_arr_add_str(map, sources, av_get_str(source));
    }
    if (av_is_bool(firing) && av_get_bool(firing)) {
        av_obj_add_bool(map, entry, "firing", true);
    }
}
/* Removes the bindings from `owner`'s components object `key`, adding active
 * ones to `map`, and drops the object when nothing else is left in it unless
 * `keep` is set. Returns whether any were removed. */
static bool move_bindings(av_value *owner, const char *key, bool active, bool keep, av_doc *map)
{
    av_value *components = av_obj_get(owner, key);
    bool changed = false;
    for (size_t i = av_is_obj(components) ? av_obj_size(components) : 0; i-- > 0;) {
        const char *name = av_get_str(av_obj_key_at(components, i));
        if (strcmp(name, "GAME.GameActionBinding") &&
            strcmp(name, "GameActionBinding")) {
            continue;
        }
        if (active) {
            move_binding(av_obj_value_at(components, i), map);
        }
        av_obj_remove_key(components, name);
        changed = true;
    }
    if (changed && !keep && !av_obj_size(components)) {
        av_obj_remove_key(owner, key);
    }
    return changed;
}
char *chibi_migrate_action_bindings(const char *asset, av_doc *map)
{
    char message[128];
    av_doc *document = asset_text_read(asset, message, sizeof message);
    av_value *root = document ? av_doc_root(document) : NULL;
    if (!av_is_obj(root)) {
        av_doc_free(document);
        return NULL;
    }
    /* A prototype holds components and detached ones; a scene's entities hold
     * components and retained ones, which are detached too. */
    bool changed = move_bindings(root, "components", true, true, map);
    changed |= move_bindings(root, "detached_components", false, false, map);
    av_value *entities = av_obj_get(root, "entities");
    for (size_t i = 0; av_is_arr(entities) && i < av_arr_size(entities); i++) {
        av_value *entity = av_arr_get(entities, i);
        if (av_is_obj(entity)) {
            changed |= move_bindings(entity, "components", true, false, map);
            changed |= move_bindings(entity, "retained_components", false, false, map);
        }
    }
    char *migrated = changed ? asset_text_write(root) : NULL;
    av_doc_free(document);
    return migrated;
}
/* The first of `words` that a word in `source` spells, outside strings and
 * comments, or NULL. */
static const char *word_use(const char *source, const char *const *words, size_t count)
{
    for (const char *p = source; *p;) {
        const char *skipped = skip_literal(p);
        if (skipped != p || !word_char(*p)) {
            p = skipped != p ? skipped : p + 1;
            continue;
        }
        const char *end = p;
        while (word_char(*end)) {
            end++;
        }
        char word[32];
        size_t length = (size_t)(end - p);
        for (size_t i = 0; length < sizeof word && i < count; i++) {
            memcpy(word, p, length);
            word[length] = '\0';
            if (names_match(word, words[i])) {
                return words[i];
            }
        }
        p = end;
    }
    return NULL;
}
static const char *binding_use(const char *source)
{
    static const char *const words[] = { "ActionBinding",
                                         "GameActionBinding",
                                         "PressedChanged" };
    return word_use(source, words, sizeof words / sizeof *words);
}
static av_doc *empty_map()
{
    av_doc *map = av_doc_new();
    av_doc_set_root(map, av_obj(map));
    return map;
}
static bool action_shaped(const av_value *action)
{
    const av_value *sources = av_obj_get(action, "sources");
    const av_value *firing = av_obj_get(action, "firing");
    if (av_obj_size(action) != 1 + (firing != NULL) ||
        (firing && !av_is_bool(firing)) || !av_arr_size(sources)) {
        return false;
    }
    for (size_t i = 0; i < av_arr_size(sources); i++) {
        if (!av_is_str(av_arr_get(sources, i))) {
            return false;
        }
    }
    return true;
}
/* Whether `map` has the shape assets/input_map.c reads. Its labels are left
 * to that reader: naming GameControls needs the type catalog, which the
 * launcher that links this library does not provide. */
static bool input_map_shaped(const av_value *map)
{
    if (!av_is_obj(map) || av_obj_size(map) > INPUT_ACTION_MAX) {
        return false;
    }
    for (size_t i = 0; i < av_obj_size(map); i++) {
        if (!av_get_str(av_obj_key_at(map, i))[0] ||
            !action_shaped(av_obj_value_at(map, i))) {
            return false;
        }
    }
    return true;
}
/* Reads the input map `source` at `path`, which is empty when the project has
 * none. A map the input map reader would reject stops the migration before
 * any asset changes. */
static av_doc *read_map(const char *source, const char *path, char *error, size_t error_size)
{
    char message[128];
    av_doc *map = source[0] ? asset_text_read(source, message, sizeof message)
                            : empty_map();
    if (!map) {
        snprintf(error, error_size, "Cannot read %s: %s; fix or move it, then migrate", path, message);
        return NULL;
    }
    if (input_map_shaped(av_doc_root(map))) {
        return map;
    }
    snprintf(
        error,
        error_size,
        "%s is not an input map: it holds at most %d actions, each with "
        "(array)sources of GameControl labels and an optional firing flag; "
        "fix or move it, then migrate",
        path,
        INPUT_ACTION_MAX
    );
    av_doc_free(map);
    return NULL;
}
static bool code_path(const char *path)
{
    size_t length = strlen(path);
    return length > strlen(".chibi") &&
           !strcmp(path + length - strlen(".chibi"), ".chibi");
}
/* Moves the bindings of every scene and prototype into the input map at
 * paths[target], whose source is empty when the project has none. Which
 * action a script's binding read meant depends on its entity, so a script
 * that reads one stops the migration for a person to rewrite it as
 * `input.<Action>`. */
static char **migrate_bindings(char **sources, char **paths, int count, int target, char *error, size_t error_size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count; i++) {
        const char *word = code_path(paths[i]) ? binding_use(sources[i]) : NULL;
        if (word) {
            snprintf(
                error,
                error_size,
                "%s reads %s, which version 16 replaces with input.<Action>; "
                "rewrite it, then migrate",
                paths[i],
                word
            );
            return migrated;
        }
    }
    av_doc *map = read_map(sources[target], paths[target], error, error_size);
    if (!map) {
        return migrated;
    }
    bool moved = false;
    for (int i = 0; i < count; i++) {
        if (i != target && !code_path(paths[i])) {
            migrated[i] = chibi_migrate_action_bindings(sources[i], map);
            moved |= migrated[i] != NULL;
        }
    }
    if (av_obj_size(av_doc_root(map)) > INPUT_ACTION_MAX) {
        snprintf(
            error,
            error_size,
            "Moving the bindings into %s gives it more than %d actions; "
            "remove some bindings or actions, then migrate",
            paths[target],
            INPUT_ACTION_MAX
        );
    } else if (moved && av_obj_size(av_doc_root(map))) {
        migrated[target] = asset_text_write(av_doc_root(map));
    }
    av_doc_free(map);
    return migrated;
}
/* Removes GameSdfMotion from `owner`'s components object `key`, and drops the
 * object when nothing else is left in it unless `keep` is set. Returns
 * whether it was there. */
static bool drop_motion(av_value *owner, const char *key, bool keep)
{
    av_value *components = av_obj_get(owner, key);
    if (!av_is_obj(components)) {
        return false;
    }
    bool changed = av_obj_remove_key(components, "GAME.GameSdfMotion");
    changed |= av_obj_remove_key(components, "GameSdfMotion");
    if (changed && !keep && !av_obj_size(components)) {
        av_obj_remove_key(owner, key);
    }
    return changed;
}
/* Returns an owned copy of a scene or prototype `asset` without
 * GameSdfMotion, or NULL when it has none. */
static char *drop_motions(const char *asset)
{
    char message[128];
    av_doc *document = asset_text_read(asset, message, sizeof message);
    av_value *root = document ? av_doc_root(document) : NULL;
    if (!av_is_obj(root)) {
        av_doc_free(document);
        return NULL;
    }
    bool changed = drop_motion(root, "components", true);
    changed |= drop_motion(root, "detached_components", false);
    av_value *entities = av_obj_get(root, "entities");
    for (size_t i = 0; av_is_arr(entities) && i < av_arr_size(entities); i++) {
        av_value *entity = av_arr_get(entities, i);
        if (av_is_obj(entity)) {
            changed |= drop_motion(entity, "components", false);
            changed |= drop_motion(entity, "retained_components", false);
        }
    }
    char *migrated = changed ? asset_text_write(root) : NULL;
    av_doc_free(document);
    return migrated;
}
/* The first of `words` that a string literal in `source` holds whole, decoded
 * as the language decodes strings, or NULL. */
static const char *quoted_use(const char *source, const char *const *words, size_t count)
{
    const char *use = NULL;
    for (const char *p = source; *p && !use;) {
        const char *end = skip_literal(p);
        if (end == p) {
            p++;
            continue;
        }
        yyjson_doc *document = *p == '"' ? yyjson_read(p, (size_t)(end - p), 0) : NULL;
        const char *text = yyjson_get_str(yyjson_doc_get_root(document));
        for (size_t i = 0; text && !use && i < count; i++) {
            use = names_match(text, words[i]) ? words[i] : NULL;
        }
        yyjson_doc_free(document);
        p = end;
    }
    return use;
}
/* Version 18 removes GameSdfMotion from every scene and prototype. Any other
 * use, by name or in a string in a script or as text left in another asset,
 * stops the migration for a person to remove it. */
static char **migrate_motions(char **sources, char **paths, int count, char *error, size_t error_size)
{
    static const char *const words[] = { "SdfMotion", "GameSdfMotion", "GAME.GameSdfMotion" };
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count; i++) {
        const char *use;
        if (code_path(paths[i])) {
            use = word_use(sources[i], words, 2);
            use = use ? use : quoted_use(sources[i], words, 3);
        } else {
            migrated[i] = drop_motions(sources[i]);
            use = strstr(migrated[i] ? migrated[i] : sources[i], "SdfMotion")
                    ? words[1]
                    : NULL;
        }
        if (use) {
            snprintf(error, error_size, "%s uses %s, which version 18 removes; remove that use, then migrate", paths[i], use);
            return migrated;
        }
    }
    return migrated;
}
static bool spelled(const char *text, bool dotted)
{
    if (!text || !(isalpha((unsigned char)*text) || *text == '_')) {
        return false;
    }
    for (const char *c = text; *c; c++) {
        bool dot = dotted && *c == '.' && c[1] && c[-1] != '.';
        if (!(isalnum((unsigned char)*c) || *c == '_' || dot)) {
            return false;
        }
    }
    return true;
}
static void append(char **out, const char *text)
{
    for (; *text; text++) {
        apush(*out, *text);
    }
}
/* Writes `description` as `//` lines at `indent`. */
static void comment(char **out, const char *indent, const char *description)
{
    for (const char *line = description; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        append(out, indent);
        append(out, "//");
        if (length) {
            apush(*out, ' ');
        }
        for (size_t i = 0; i < length; i++) {
            apush(*out, line[i]);
        }
        apush(*out, '\n');
        line = end ? end + 1 : line + length;
    }
}
static const char *text_of(const av_value *object, const char *key)
{
    const av_value *value = av_obj_get(object, key);
    return av_is_str(value) ? av_get_str(value) : NULL;
}
char *chibi_migrate_component(const char *kdl, char *error, size_t error_size)
{
    av_doc *document = asset_text_read(kdl, error, error_size);
    av_value *root = document ? av_doc_root(document) : NULL;
    av_value *fields = av_is_obj(root) ? av_obj_get(root, "fields") : NULL;
    const char *name = av_is_obj(root) ? text_of(root, "name") : NULL;
    bool valid = spelled(name, false) && av_is_arr(fields);
    char *out = NULL;
    if (valid) {
        comment(&out, "", text_of(root, "description"));
        append(&out, "component ");
        append(&out, name);
        append(&out, " {\n");
    }
    for (size_t i = 0; valid && i < av_arr_size(fields); i++) {
        av_value *field = av_arr_get(fields, i);
        const char *field_name = av_is_obj(field) ? text_of(field, "name") : NULL;
        const char *type = av_is_obj(field) ? text_of(field, "type") : NULL;
        valid = spelled(field_name, false) && spelled(type, true);
        if (valid) {
            comment(&out, "    ", text_of(field, "description"));
            append(&out, "    ");
            append(&out, field_name);
            append(&out, ": ");
            append(&out, type);
            apush(out, '\n');
        }
    }
    if (document && !valid) {
        snprintf(error, error_size, "A component needs a name and fields with names and types");
    }
    av_doc_free(document);
    if (!valid) {
        afree(out);
        return NULL;
    }
    append(&out, "}\n");
    apush(out, 0);
    char *migrated = xstrdup(out);
    afree(out);
    /* A name KDL allowed may be a reserved word in .chibi. */
    script_syntax *syntax = script_parse(migrated);
    if (syntax->diagnostic_count) {
        const char *line = migrated;
        for (int i = 1; i < syntax->diagnostics[0].range.line; i++) {
            line = strchr(line, '\n') + 1;
        }
        int length = (int)strcspn(line, "\n");
        while (*line == ' ') {
            line++;
            length--;
        }
        snprintf(error, error_size, "'%.*s' is not valid .chibi; a name may be a reserved word such as state; rename it, then migrate", length, line);
        free(migrated);
        migrated = NULL;
    }
    script_syntax_free(syntax);
    return migrated;
}
/* Version 19: each content/components/X.kdl becomes X.chibi. Every source is
 * staged before any is replaced, and rerunning after an interruption writes
 * the same sources again. */
static int migrate_components(const char *content, char *error, size_t error_size)
{
    size_t size = strlen(content) + sizeof "/components";
    char *folder = xmalloc(size);
    snprintf(folder, size, "%s/components", content);
    DIR *dir = opendir(folder);
    if (!dir) {
        int status = errno == ENOENT ? 0 : -1;
        if (status) {
            snprintf(error, error_size, "Cannot open %s: %s", folder, strerror(errno));
        }
        free(folder);
        return status;
    }
    char **paths = NULL, **targets = NULL;
    for (struct dirent *entry; (entry = readdir(dir));) {
        if (entry->d_name[0] != '.' &&
            has_suffix(entry->d_name, DECLARATIVE_ASSET_EXTENSION)) {
            size_t length = strlen(folder) + strlen(entry->d_name) + sizeof "/.chibi";
            char *path = xmalloc(length), *target = xmalloc(length);
            snprintf(path, length, "%s/%s", folder, entry->d_name);
            int stem = (int)(strlen(entry->d_name) - strlen(DECLARATIVE_ASSET_EXTENSION));
            snprintf(target, length, "%s/%.*s.chibi", folder, stem, entry->d_name);
            apush(paths, path);
            apush(targets, target);
        }
    }
    closedir(dir);
    free(folder);
    int status = 0, staged = 0;
    for (; !status && staged < acount(paths); staged++) {
        char *kdl = read_text(paths[staged]);
        char message[256] = { 0 };
        char *source = kdl ? chibi_migrate_component(kdl, message, sizeof message)
                           : NULL;
        if (!source) {
            snprintf(error, error_size, "%s: %s", paths[staged], kdl ? message : "cannot read the file");
            status = -1;
        }
        /* A rerun finds its own earlier output; anything else is kept. */
        char *existing = status ? NULL : read_text(targets[staged]);
        if (existing && strcmp(existing, source)) {
            snprintf(error, error_size, "%s already exists; move it, then migrate", targets[staged]);
            status = -1;
        }
        free(existing);
        status = status ? status
                        : stage_text(targets[staged], source, error, error_size);
        free(kdl);
        free(source);
    }
    for (int i = 0; i < staged; i++) {
        char *from = staged_path(targets[i]);
        if (status) {
            remove(from);
        } else if (rename(from, targets[i]) || remove(paths[i])) {
            snprintf(error, error_size, "Cannot replace %s with %s", paths[i], targets[i]);
            status = -1;
        }
        free(from);
    }
    for (int i = 0; i < acount(paths); i++) {
        free(paths[i]);
        free(targets[i]);
    }
    afree(paths);
    afree(targets);
    return status;
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
    if (from == 18) {
        return migrate_components(content, error, error_size);
    }
    char **paths = NULL;
    /* Versions 15, 16 and 18 change scenes and prototypes, wherever the
     * project keeps them; every other step changes scripts. */
    bool scenes = from == 14 || from == 15 || from == 17;
    int status = collect_sources(
        content,
        scenes ? DECLARATIVE_ASSET_EXTENSION : ".chibi",
        &paths,
        error,
        error_size
    );
    /* Versions 16 and 18 also check scripts for names they remove. */
    if (!status && (from == 15 || from == 17)) {
        status = collect_sources(content, ".chibi", &paths, error, error_size);
    }
    /* The scan skips hidden entries, and the start scene may lie outside the
     * content, so add it unless the scan found it. */
    struct stat info;
    if (!status && scenes && start_scene && !collected(paths, start_scene) &&
        !stat(start_scene, &info) && S_ISREG(info.st_mode)) {
        apush(paths, xstrdup(start_scene));
    }
    /* Version 16 writes the input map, which may not exist yet. */
    int map = -1;
    bool created = false;
    if (!status && from == 15) {
        size_t size = strlen(content) + sizeof "/input.kdl";
        char *path = xmalloc(size);
        snprintf(path, size, "%s/input.kdl", content);
        for (int i = 0; i < acount(paths) && map < 0; i++) {
            map = strcmp(paths[i], path) ? -1 : i;
        }
        created = map < 0;
        if (created) {
            map = acount(paths);
            apush(paths, path);
        } else {
            free(path);
        }
    }
    int count = acount(paths);
    char **sources = xmalloc((size_t)(count + 1) * sizeof *sources);
    for (int i = 0; i < count; i++) {
        sources[i] = status              ? NULL
                   : i == map && created ? xstrdup("")
                                         : read_text(paths[i]);
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
                    status = chibi_migrate_values(sources[i], &migrated[i], error, error_size);
                }
                break;
            case 14:
                migrated = xmalloc((size_t)(count + 1) * sizeof *migrated);
                for (int i = 0; i < count; i++) {
                    migrated[i] = chibi_migrate_scene_behaviors(sources[i]);
                }
                break;
            case 15:
                error[0] = '\0';
                migrated = migrate_bindings(sources, paths, count, map, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 16:
                migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
                for (int i = 0; i < count && !status; i++) {
                    status = chibi_migrate_locals(sources[i], &migrated[i], error, error_size);
                }
                break;
            case 17:
                error[0] = '\0';
                migrated = migrate_motions(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
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
