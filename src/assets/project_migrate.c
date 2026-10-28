#include "assets/project_migrate.h"

#include "assets/project.h"
#include "language/syntax.h"
#include "project_migrate26.h"
#include "project_migrate29.h"
#include "project_migrate30.h"
#include "project_migrate31.h"
#include "project_migrate32.h"
#include "project_migrate34.h"
#include "project_migrate36.h"
#include "runtime/input_action.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_edit.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <tree_sitter/parser.h>
#include <unistd.h>
#include <yyjson.h>

/* Historical migration steps parse their source contract, independently of
 * the current language accepted by compilation and authoring. */
const TSLanguage *tree_sitter_chibi_project23();
const TSLanguage *tree_sitter_chibi_project24();
const TSLanguage *tree_sitter_chibi_project25();
const TSLanguage *tree_sitter_chibi_project26();
const TSLanguage *tree_sitter_chibi_project27();
const TSLanguage *tree_sitter_chibi_project28();
const TSLanguage *tree_sitter_chibi_project29();
const TSLanguage *tree_sitter_chibi_project30();
const TSLanguage *tree_sitter_chibi_project31();
const TSLanguage *tree_sitter_chibi_project36();

void *tree_sitter_chibi_external_scanner_create();
void tree_sitter_chibi_external_scanner_destroy(void *);
bool tree_sitter_chibi_external_scanner_scan(void *, TSLexer *, const bool *);
unsigned tree_sitter_chibi_external_scanner_serialize(void *, char *);
void tree_sitter_chibi_external_scanner_deserialize(void *, const char *, unsigned);

void *tree_sitter_chibi_project24_external_scanner_create()
{
    return tree_sitter_chibi_external_scanner_create();
}
void tree_sitter_chibi_project24_external_scanner_destroy(void *scanner)
{
    tree_sitter_chibi_external_scanner_destroy(scanner);
}
bool tree_sitter_chibi_project24_external_scanner_scan(void *scanner, TSLexer *lexer, const bool *valid)
{
    return tree_sitter_chibi_external_scanner_scan(scanner, lexer, valid);
}
unsigned tree_sitter_chibi_project24_external_scanner_serialize(void *scanner, char *buffer)
{
    return tree_sitter_chibi_external_scanner_serialize(scanner, buffer);
}
void tree_sitter_chibi_project24_external_scanner_deserialize(void *scanner, const char *buffer, unsigned length)
{
    tree_sitter_chibi_external_scanner_deserialize(scanner, buffer, length);
}

script_syntax *chibi_migrate_parse(const char *text, int version)
{
    if (version >= CHIBI_PROJECT_VERSION) {
        return script_parse(text);
    }
    script_syntax *syntax = xcalloc(1, sizeof *syntax);
    syntax->text = xstrdup(text);
    TSParser *parser = ts_parser_new();
    bool compatible = ts_parser_set_language(
        parser,
        version == 23   ? tree_sitter_chibi_project23()
        : version == 24 ? tree_sitter_chibi_project24()
        : version == 25 ? tree_sitter_chibi_project25()
        : version == 26 ? tree_sitter_chibi_project26()
        : version == 27 ? tree_sitter_chibi_project27()
        : version == 28 ? tree_sitter_chibi_project28()
        : version == 29 ? tree_sitter_chibi_project29()
        : version == 30 ? tree_sitter_chibi_project30()
        /* Version 32 kept version 31's grammar, and versions 34 to 36 kept
         * version 33's. */
        : version <= 32 ? tree_sitter_chibi_project31()
                        : tree_sitter_chibi_project36()
    );
    assert(compatible);
    (void)compatible;
    syntax->tree = ts_parser_parse_string(parser, NULL, text, (uint32_t)strlen(text));
    ts_parser_delete(parser);
    assert(syntax->tree);
    TSNode problem = ts_tree_root_node(syntax->tree);
    if (ts_node_has_error(problem)) {
        for (;;) {
            TSNode child = { 0 };
            for (uint32_t i = 0; i < ts_node_child_count(problem); i++) {
                TSNode candidate = ts_node_child(problem, i);
                if (ts_node_has_error(candidate)) {
                    child = candidate;
                    break;
                }
            }
            if (ts_node_is_null(child) || ts_node_is_error(problem)) {
                break;
            }
            problem = child;
        }
        script_diagnostic diagnostic = {
            .range = script_node_range(problem),
            .message = "Invalid historical source syntax"
        };
        apush(syntax->diagnostics, diagnostic);
        syntax->diagnostic_count = 1;
    }
    return syntax;
}

static script_syntax *parse23(const char *text)
{
    return chibi_migrate_parse(text, 23);
}

static script_syntax *parse24(const char *text)
{
    return chibi_migrate_parse(text, 24);
}

static script_syntax *parse25(const char *text)
{
    return chibi_migrate_parse(text, 25);
}

static script_syntax *parse26(const char *text)
{
    return chibi_migrate_parse(text, 26);
}

static script_syntax *parse27(const char *text)
{
    return chibi_migrate_parse(text, 27);
}

static script_syntax *parse28(const char *text)
{
    return chibi_migrate_parse(text, 28);
}
/* Whether text parses with the grammar of project `version`, from 23 on. */
static bool valid_at(const char *text, int version)
{
    script_syntax *syntax = chibi_migrate_parse(text, version);
    bool valid = !syntax->diagnostic_count;
    script_syntax_free(syntax);
    return valid;
}
/* Whether later steps already migrated text past version `from + 1`. An
 * upgrade that fails after its steps wrote leaves sources ahead of the
 * manifest, and its rerun starts again at the manifest's version. */
static bool migrated_ahead(const char *text, int from)
{
    if (valid_at(text, from) || valid_at(text, from + 1)) {
        return false;
    }
    for (int version = from + 2; version <= CHIBI_PROJECT_VERSION; version++) {
        if (valid_at(text, version)) {
            return true;
        }
    }
    return false;
}

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
static void collect(TSNode node, int file, TSNode library, bool macros, function **functions)
{
    if (is(node, "library")) {
        library = ts_node_child_by_field_name(node, "name", 4);
    }
    /* An action has no body; it never waits and shadows functions. */
    if (is(node, "function_declaration") || is(node, "action_declaration") ||
        (macros && is(node, "macro_declaration"))) {
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
        collect(ts_node_named_child(node, i), file, library, macros, functions);
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
        syntax[i] = parse23(sources[i]);
        collect(ts_tree_root_node(syntax[i]->tree), i, (TSNode){ 0 }, false, &c.functions);
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
    script_syntax *syntax = parse23(stripped);
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
    script_syntax *syntax = parse23(text);
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
    script_syntax *syntax = parse23(text);
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
        script_syntax *check = parse23(result);
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
/* KDL names and prose may mention the removed feature without depending on
 * its reflected component or graph types. Only semantic references block. */
static bool motion_asset_use(const av_value *value, bool semantic)
{
    if (av_is_str(value)) {
        return semantic && strstr(av_get_str(value), "SdfMotion");
    }
    for (size_t i = 0; i < av_obj_size(value); i++) {
        const char *key = av_get_str(av_obj_key_at(value, i));
        if (!strcmp(key, "GameSdfMotion") || !strcmp(key, "GAME.GameSdfMotion")) {
            return true;
        }
        bool reference = !strcmp(key, "component") ||
                         !strcmp(key, "component_type") ||
                         !strcmp(key, "component_name") ||
                         !strcmp(key, "Component") || !strcmp(key, "type") ||
                         !strcmp(key, "type_name") ||
                         !strcmp(key, "split_type_name") ||
                         !strcmp(key, "owner_type");
        if (motion_asset_use(av_obj_value_at(value, i), semantic || reference)) {
            return true;
        }
    }
    for (size_t i = 0; i < av_arr_size(value); i++) {
        if (motion_asset_use(av_arr_get(value, i), semantic)) {
            return true;
        }
    }
    return false;
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
            char message[128];
            av_doc *document = asset_text_read(migrated[i] ? migrated[i] : sources[i], message, sizeof message);
            use = motion_asset_use(av_doc_root(document), false) ? words[1] : NULL;
            av_doc_free(document);
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
/* Writes `description` as `//` lines at `indent`, one per line, so an empty
 * line, a trailing one or an empty description reads back the same. */
static void comment(char **out, const char *indent, const char *description)
{
    for (const char *line = description; line;) {
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
        line = end ? end + 1 : NULL;
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
    script_syntax *syntax = parse23(migrated);
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
/* Version 20: each content/components/X.kdl becomes X.chibi. Every source is
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
/* The name of the `index`th object in a scene's `entities`, or NULL. */
static av_value *object_name(av_value *entities, size_t index)
{
    av_value *entity = av_arr_get(entities, index);
    av_value *name = av_is_obj(entity) ? av_obj_get(entity, "name") : NULL;
    return av_is_str(name) ? name : NULL;
}
/* Puts '_' in place of each '.' in a string value, returning whether it
 * had one. */
static bool undot(av_value *value)
{
    if (!av_is_str(value) || !strchr(av_get_str(value), '.')) {
        return false;
    }
    char *undotted = xstrdup(av_get_str(value));
    for (char *dot = strchr(undotted, '.'); dot; dot = strchr(dot, '.')) {
        *dot = '_';
    }
    av_set_str(value, undotted);
    free(undotted);
    return true;
}
/* A saved scenario names scene objects in its inputs' event targets, entity
 * payloads and UI targets. Before version 19 no name was a path, so a '.'
 * there named an object the scene renames the same way. */
static bool undot_targets(av_value *root)
{
    bool changed = false;
    av_value *inputs = av_obj_get(root, "inputs");
    for (size_t i = 0; i < av_arr_size(inputs); i++) {
        av_value *input = av_arr_get(inputs, i);
        av_value *events = av_obj_get(input, "events"), *ui = av_obj_get(input, "ui");
        for (size_t j = 0; j < av_arr_size(events); j++) {
            av_value *event = av_arr_get(events, j);
            changed |= undot(av_obj_get(event, "target"));
            av_value *values = av_obj_get(event, "values");
            for (size_t k = 0; k < av_arr_size(values); k++) {
                av_value *payload = av_arr_get(values, k);
                av_value *type = av_obj_get(payload, "type");
                if (av_is_str(type) && !strcmp(av_get_str(type), "entity")) {
                    changed |= undot(av_obj_get(payload, "value"));
                }
            }
        }
        for (size_t j = 0; j < av_arr_size(ui); j++) {
            changed |= undot(av_obj_get(av_arr_get(ui, j), "target"));
        }
    }
    return changed;
}
/* Version 19 names nested scene objects by '.'-separated paths, so an
 * object name holding '.' takes '_' in its place, as does a saved scenario's
 * reference to it. Earlier scenes are flat, so a renamed object that would
 * share a name with another stops the migration for a person to rename one
 * of them. Returns an owned copy of `asset` with the new names, or NULL when
 * no name changes. */
static char *undot_names(const char *asset, const char *path, char *error, size_t error_size)
{
    char message[128];
    av_doc *document = asset_text_read(asset, message, sizeof message);
    av_value *root = document ? av_doc_root(document) : NULL;
    av_value *entities = av_obj_get(root, "entities");
    size_t count = av_arr_size(entities);
    bool changed = undot_targets(root);
    bool renamed = false;
    for (size_t i = 0; i < count; i++) {
        renamed |= undot(object_name(entities, i));
    }
    for (size_t i = 0; renamed && i < count; i++) {
        av_value *a = object_name(entities, i);
        for (size_t j = i + 1; a && j < count; j++) {
            av_value *b = object_name(entities, j);
            if (b && !strcmp(av_get_str(a), av_get_str(b))) {
                snprintf(
                    error,
                    error_size,
                    "%s has two objects that version 19 names %s, because '.' "
                    "separates paths; rename one, then migrate",
                    path,
                    av_get_str(a)
                );
                av_doc_free(document);
                return NULL;
            }
        }
    }
    char *migrated = changed || renamed ? asset_text_write(root) : NULL;
    av_doc_free(document);
    return migrated;
}
/* Gives a Surface Output `node` explicit IDs, from `counter`, for the pins
 * version 21 appends. Saved pin IDs otherwise continue from the node's, which
 * are often the next node's. Returns whether it changed. */
static bool surface_pin_ids(av_doc *document, av_value *node, uint64_t *counter)
{
    static const char *const pins[] = { "Occlusion", "Alpha Mode", "Alpha Cutoff" };
    if (!av_is_obj(node) || !av_is_str(av_obj_get(node, "type")) ||
        strcmp(av_get_str(av_obj_get(node, "type")), "Shader Surface Output")) {
        return false;
    }
    av_value *inputs = av_obj_get(node, "inputs");
    if (!inputs) {
        inputs = av_obj_add_obj(document, node, "inputs");
    }
    if (!av_is_obj(inputs)) {
        return false;
    }
    for (size_t i = 0; i < sizeof pins / sizeof *pins; i++) {
        if (av_obj_get(inputs, pins[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < sizeof pins / sizeof *pins; i++) {
        av_value *pin = av_obj_add_obj(document, inputs, pins[i]);
        av_obj_add_uint(document, pin, "id", (*counter)++);
    }
    return true;
}
/* Returns an owned copy of a material `asset` whose Surface Outputs carry the
 * IDs of the pins version 21 appends, or NULL when it has none to change. The
 * IDs come from the asset's counter, which exceeds every ID it holds. */
static char *surface_outputs(const char *asset)
{
    char message[128];
    av_doc *document = asset_text_read(asset, message, sizeof message);
    av_value *root = document ? av_doc_root(document) : NULL;
    av_value *counter = av_is_obj(root) ? av_obj_get(root, "id_counter") : NULL;
    av_value *graphs = av_is_uint(counter) ? av_obj_get(root, "graphs") : NULL;
    uint64_t next = av_is_uint(counter) ? av_get_uint(counter) : 0;
    bool changed = false;
    for (size_t i = 0; av_is_arr(graphs) && i < av_arr_size(graphs); i++) {
        av_value *graph = av_arr_get(graphs, i);
        av_value *nodes = av_is_obj(graph) ? av_obj_get(graph, "nodes") : NULL;
        for (size_t j = 0; av_is_arr(nodes) && j < av_arr_size(nodes); j++) {
            changed |= surface_pin_ids(document, av_arr_get(nodes, j), &next);
        }
    }
    if (changed) {
        av_obj_set(document, root, "id_counter", av_uint(document, next));
    }
    char *migrated = changed ? asset_text_write(root) : NULL;
    av_doc_free(document);
    return migrated;
}
/* Version 23 removes the native Game type prefix and the component namespace.
 * Keep the historical names here: future catalog changes must not change what
 * this migration recognizes, and authored types may themselves start in Game. */
static const char *unprefixed_native(const char *text, size_t length)
{
    static const char *const names[] = {
        "GameAim",
        "GamePadControl",
        "GameGlobalIllumination",
        "GameAppearance",
        "GameBody",
        "GameBody3D",
        "GameBodyType",
        "GameCamera",
        "GameCharacter3D",
        "GameCollider",
        "GameCollider3D",
        "GameCollider3DShape",
        "GameColliderShape",
        "GameCombustion",
        "GameControl",
        "GameDash",
        "GameDestructible",
        "GameDirectionalLight",
        "GameDrive",
        "GameEncounter",
        "GameEncounterMember",
        "GameEnvironment",
        "GameExperience",
        "GameFluidKind",
        "GameFluidVolume",
        "GameFog",
        "GameFragmentGlow",
        "GameFuel",
        "GameGeometry3D",
        "GameGlobalBinding",
        "GameGlobalTransform2D",
        "GameGlobalTransform3D",
        "GameGravity",
        "GameGravity3D",
        "GameHRC2D",
        "GameHRCColor",
        "GameHRCDebugView",
        "GameHRCDepthEmitters",
        "GameHRCDepthQuality",
        "GameHRCDepthVisibility",
        "GameHRCGather",
        "GameHRCShadowMode",
        "GameHRCSurfaceMode",
        "GameHRCToneMapping",
        "GameHRCUpscale",
        "GameHealth",
        "GameIgniter",
        "GameInput",
        "GameJoint",
        "GameJoint3D",
        "GameJoint3DKind",
        "GameJointKind",
        "GameMaterial",
        "GameMaterialCavity",
        "GameMaterialFrame",
        "GameMeshAsset",
        "GameMeshParts",
        "GameMeshSurface2D",
        "GameMovement",
        "GameOcclusion",
        "GameOrientation",
        "GameParticleCurve",
        "GameParticleEmitter",
        "GameParticleShape",
        "GamePickup",
        "GamePointLight",
        "GameProjectile",
        "GameReflection",
        "GameRenderState",
        "GameRhythm",
        "GameSdfNormalMode",
        "GameSdfSurface3D",
        "GameSensorContact",
        "GameShader",
        "GameShape",
        "GameSpawnRequest",
        "GameSpotLight",
        "GameSprite",
        "GameStarfield2D",
        "GameSubstance",
        "GameSurfaceInfluence",
        "GameSurfaceQuality",
        "GameSurfaceResolution",
        "GameTilemap",
        "GameToneMapping",
        "GameTrail",
        "GameTransform",
        "GameTransform3D",
        "GameUi",
        "GameVelocity",
        "GameVision",
        "GameVisualMotion",
        "GameVisualTransform",
        "GameWeapon",
    };
    char name[128];
    if (length >= sizeof name) {
        return NULL;
    }
    memcpy(name, text, length);
    name[length] = '\0';
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        if (names_match(names[i], name)) {
            return names[i] + 4;
        }
    }
    return NULL;
}
static bool prefixed_native(const char *text, size_t length)
{
    return unprefixed_native(text, length) != NULL;
}
static bool native_source_call(const char *text)
{
    static const char *const native[] = { "game_move_transform",
                                          "game_transform_to_cf",
                                          "game_transform_from_cf",
                                          "game_transform_visual_offset",
                                          "game_visual_primitive_part" };
    for (size_t i = 0; i < sizeof native / sizeof *native; i++) {
        if (names_match(text, native[i])) {
            return true;
        }
    }
    static const char *const operations[] = {
        "getentity", "setentity", "hasentity", "get",   "set",  "has",
        "add",       "remove",    "make",      "break", "wrap", "unwrap"
    };
    char *normalized = xmalloc(strlen(text) + 1), *at = normalized;
    for (const char *p = text; *p; p++) {
        if (*p != '_' && *p != ' ' && *p != '-') {
            *at++ = (char)tolower((unsigned char)*p);
        }
    }
    *at = '\0';
    bool found = false;
    for (size_t i = 0; !found && i < sizeof operations / sizeof *operations; i++) {
        size_t length = strlen(operations[i]);
        if (strncmp(normalized, operations[i], length)) {
            continue;
        }
        const char *name = normalized + length;
        for (size_t end = strlen(name); end > 4; end--) {
            if (prefixed_native(name, end)) {
                found = true;
                break;
            }
        }
    }
    free(normalized);
    return found;
}
/* These are the version 22 reflected type categories, not the live catalog. */
static bool native_type_operation(const char *type, const char *operation)
{
    static const char *const enums[] = { "BodyType",
                                         "Collider3DShape",
                                         "ColliderShape",
                                         "Control",
                                         "FluidKind",
                                         "GlobalIllumination",
                                         "HRCColor",
                                         "HRCDebugView",
                                         "HRCDepthEmitters",
                                         "HRCDepthQuality",
                                         "HRCDepthVisibility",
                                         "HRCGather",
                                         "HRCShadowMode",
                                         "HRCSurfaceMode",
                                         "HRCToneMapping",
                                         "HRCUpscale",
                                         "Joint3DKind",
                                         "JointKind",
                                         "Occlusion",
                                         "PadControl",
                                         "ParticleShape",
                                         "SdfNormalMode",
                                         "Shape",
                                         "SurfaceResolution",
                                         "ToneMapping" };
    for (size_t i = 0; i < sizeof enums / sizeof *enums; i++) {
        if (!strcmp(type, enums[i])) {
            return false;
        }
    }
    if (!strcmp(operation, "Make_") || !strcmp(operation, "Break_")) {
        return true;
    }
    if (!strcmp(type, "ParticleCurve")) {
        return false;
    }
    if (!strcmp(operation, "Unwrap_")) {
        static const char *const wrappers[] = {
            "EncounterMember", "Experience", "Fuel",      "GlobalBinding",
            "Gravity",         "Gravity3D",  "Igniter",   "Input",
            "MaterialCavity",  "MeshAsset",  "MeshParts", "MeshSurface2D",
            "Movement",        "Orientation"
        };
        for (size_t i = 0; i < sizeof wrappers / sizeof *wrappers; i++) {
            if (!strcmp(type, wrappers[i])) {
                return true;
            }
        }
        return false;
    }
    if (!strcmp(type, "GlobalTransform2D") || !strcmp(type, "GlobalTransform3D")) {
        return !strcmp(operation, "Get_") || !strcmp(operation, "GetEntity_") ||
               !strcmp(operation, "Has_") || !strcmp(operation, "HasEntity_");
    }
    return true;
}
static bool native_canonical_call(const char *name)
{
    static const char *const calls[] = { "game_move_transform",
                                         "game_transform_to_cf",
                                         "game_transform_from_cf" };
    for (size_t i = 0; i < sizeof calls / sizeof *calls; i++) {
        if (!strcmp(name, calls[i])) {
            return true;
        }
    }
    static const char *const operations[] = {
        "GetEntity_", "SetEntity_", "HasEntity_", "Get_",   "Set_",   "Has_",
        "Add_",       "Remove_",    "Make_",      "Break_", "Unwrap_"
    };
    for (size_t i = 0; i < sizeof operations / sizeof *operations; i++) {
        size_t length = strlen(operations[i]);
        if (strncmp(name, operations[i], length) || strncmp(name + length, "Game", 4)) {
            continue;
        }
        const char *type = unprefixed_native(name + length, strlen(name + length));
        if (type && !strcmp(name + length + 4, type)) {
            return native_type_operation(type, operations[i]);
        }
    }
    return false;
}
static bool name_byte(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}
/* Rewrites reflected names inside type and capability spellings, including
 * Get_GameTransform and struct<GameTransform>. The caller selects semantic
 * names, so descriptions, comments, and ordinary string data stay untouched. */
static char *unprefix_name(const char *text)
{
    char *out = xmalloc(strlen(text) + 1), *next = out;
    const char *literal = text + (*text == '"');
    char *call = xstrdup(literal);
    size_t length = strlen(call);
    if (length && call[length - 1] == '"') {
        call[length - 1] = '\0';
    }
    const char *type_name = !strncmp(call, "GAME.", 5) ? call + 5 : call;
    const char *type = unprefixed_native(type_name, strlen(type_name));
    if (type) {
        if (*text == '"') {
            snprintf(out, strlen(text) + 1, "\"%s\"", type);
        } else {
            strcpy(out, type);
        }
        free(call);
        return out;
    }
    bool native = native_source_call(call);
    free(call);
    for (const char *p = text; *p;) {
        bool boundary = p == text || !isalnum((unsigned char)p[-1]);
        if (boundary && !strncmp(p, "GAME.", 5) && name_byte(p[5])) {
            p += 5;
            continue;
        }
        if ((boundary || native) && !strncasecmp(p, "Game", 4)) {
            const char *end = p + 4;
            while (isalnum((unsigned char)*end)) {
                end++;
            }
            if (prefixed_native(p, (size_t)(end - p))) {
                p += 4;
                continue;
            }
        }
        if (native && !strncasecmp(p, "game", 4)) {
            p += 4;
            while (*p == '_' || *p == ' ' || *p == '-') {
                p++;
            }
            continue;
        }
        *next++ = *p++;
    }
    *next = '\0';
    if (!strcmp(out, text)) {
        free(out);
        return NULL;
    }
    return out;
}
static void prefix_edit(const char *text, size_t start, size_t end, replacement **out)
{
    assert(start <= end && end <= strlen(text));
    char *part = xmalloc(end - start + 1);
    memcpy(part, text + start, end - start);
    part[end - start] = '\0';
    char *renamed = unprefix_name(part);
    free(part);
    if (renamed) {
        apush(*out, ((replacement){ (int)start, (int)end, renamed, 0 }));
    }
}
static char *prefix_apply(const char *text, replacement *edits)
{
    if (!acount(edits)) {
        return NULL;
    }
    qsort(edits, (size_t)acount(edits), sizeof *edits, by_start);
    size_t size = strlen(text) + 1;
    for (int i = 0; i < acount(edits); i++) {
        size += strlen(edits[i].text);
        size -= (size_t)(edits[i].end - edits[i].start);
    }
    char *out = xmalloc(size), *next = out;
    int from = 0;
    for (int i = 0; i < acount(edits); i++) {
        assert(from <= edits[i].start);
        next = stpncpy(next, text + from, (size_t)(edits[i].start - from));
        next = stpcpy(next, edits[i].text);
        from = edits[i].end;
    }
    strcpy(next, text + from);
    return out;
}
static void prefix_edits_free(replacement *edits)
{
    for (int i = 0; i < acount(edits); i++) {
        free(edits[i].text);
    }
    afree(edits);
}
static bool component_call(const char *text, TSNode call)
{
    TSNode target = ts_node_child_by_field_name(call, "function", 8);
    if (!is_name(target)) {
        return false;
    }
    char *name = name_text(text, target);
    static const char *const calls[] = { "component",
                                         "get_component",
                                         "set_component",
                                         "has_component",
                                         "add_component",
                                         "remove_component",
                                         "find_entities_with_component",
                                         "has_entities_with_component",
                                         "make_entity_condition" };
    bool found = false;
    for (size_t i = 0; i < sizeof calls / sizeof *calls; i++) {
        found |= names_match(name, calls[i]);
    }
    free(name);
    return found;
}
static bool component_argument(const char *text, TSNode node)
{
    TSNode parent = ts_node_parent(node), argument = node;
    if (is(parent, "struct_field")) {
        TSNode field = ts_node_child_by_field_name(parent, "name", 4);
        char *name = name_text(text, field);
        bool component = names_match(name, "component");
        free(name);
        for (TSNode scope = ts_node_parent(parent);
             component && !ts_node_is_null(scope);
             scope = ts_node_parent(scope)) {
            if (!is(scope, "call_expression")) {
                continue;
            }
            TSNode target = ts_node_child_by_field_name(scope, "function", 8);
            if (!is_name(target)) {
                continue;
            }
            name = name_text(text, target);
            bool condition = names_match(name, "count_matching_entities");
            free(name);
            if (condition) {
                return true;
            }
        }
        return false;
    }
    if (is(parent, "named_argument") || is(parent, "typed_argument")) {
        TSNode name = ts_node_child_by_field_name(parent, "name", 4);
        char *value = name_text(text, name);
        bool component = names_match(value, "Component");
        free(value);
        if (!component) {
            return false;
        }
        argument = parent;
        parent = ts_node_parent(parent);
    }
    if (!is(parent, "argument_list") || !component_call(text, ts_node_parent(parent))) {
        return false;
    }
    return !ts_node_eq(argument, node) ||
           ts_node_eq(ts_node_named_child(parent, 0), node);
}
static bool prefix_callee(const corpus *context, int file, TSNode target)
{
    if (callee(context, file, target)) {
        return true;
    }
    if (is_name(target)) {
        char *name = name_text(context->sources[file], target);
        bool native = native_canonical_call(name);
        free(name);
        /* Exact native catalog keys win over imported short-name aliases. */
        if (native) {
            return false;
        }
    }
    /* The authored catalog also exposes library callables by their short
     * names. Local declarations keep callee's lexical scope rules. */
    for (int i = 0; is_name(target) && i < acount(context->functions); i++) {
        const function *f = &context->functions[i];
        if (!ts_node_is_null(f->library) && !ts_node_is_null(f->body) &&
            similar_name(context->sources[file], target, context->sources[f->file], f->name)) {
            return true;
        }
    }
    return false;
}
static void source_prefix_edits(const corpus *context, int file, TSNode node, replacement **out)
{
    const char *text = context->sources[file];
    TSNode parent = ts_node_parent(node);
    bool call_target = !ts_node_is_null(parent) && is(parent, "call_expression") &&
                       ts_node_eq(node, ts_node_child_by_field_name(parent, "function", 8));
    if (call_target && prefix_callee(context, file, node)) {
        return;
    }
    bool type = is(node, "type_name");
    bool target = is_name(node) && call_target;
    bool selector = is(node, "string") && component_argument(text, node);
    bool member = is_name(node) && is(parent, "member_expression") &&
                  ts_node_eq(node, ts_node_child_by_field_name(parent, "member", 6));
    if (type || target || selector || member) {
        prefix_edit(text, ts_node_start_byte(node), ts_node_end_byte(node), out);
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        source_prefix_edits(context, file, ts_node_named_child(node, i), out);
    }
}
static void collect_prefix_edits(const char *text, TSNode root, replacement **out)
{
    const char *sources[] = { text };
    corpus context = { .sources = sources };
    collect(root, 0, (TSNode){ 0 }, true, &context.functions);
    source_prefix_edits(&context, 0, root, out);
    afree(context.functions);
}
static bool prefix_field(const char *name)
{
    return !strcmp(name, "type") || !strcmp(name, "type_name") ||
           !strcmp(name, "split_type_name") || !strcmp(name, "owner_type") ||
           !strcmp(name, "component") || !strcmp(name, "component_type") ||
           !strcmp(name, "component_name") || !strcmp(name, "Component");
}
static void asset_prefix_edits(
    const char *text,
    const av_value *value,
    bool semantic,
    bool components,
    replacement **out
)
{
    if (av_is_str(value) && semantic) {
        av_source source = av_get_source(value);
        prefix_edit(text, source.start, source.end, out);
        return;
    }
    for (size_t i = 0; i < av_obj_size(value); i++) {
        const av_value *key = av_obj_key_at(value, i),
                       *child = av_obj_value_at(value, i);
        const char *name = av_get_str(key);
        av_source source = av_get_source(child);
        if (!strncmp(name, "GAME.", 5) ||
            (components && prefixed_native(name, strlen(name)))) {
            prefix_edit(text, source.name_start, source.name_end, out);
        }
        asset_prefix_edits(
            text,
            child,
            prefix_field(name) || semantic,
            !strcmp(name, "components") || !strcmp(name, "component_overrides") ||
                !strcmp(name, "retained_components") ||
                !strcmp(name, "detached_components") ||
                !strcmp(name, "inputs") || !strcmp(name, "outputs"),
            out
        );
    }
    for (size_t i = 0; i < av_arr_size(value); i++) {
        asset_prefix_edits(text, av_arr_get(value, i), semantic, false, out);
    }
}
char *chibi_migrate_prefixes(const char *text, bool source, char *error, size_t error_size)
{
    replacement *edits = NULL;
    if (source) {
        script_syntax *syntax = parse23(text);
        collect_prefix_edits(text, ts_tree_root_node(syntax->tree), &edits);
        script_syntax_free(syntax);
    } else {
        av_doc *document = asset_text_read(text, error, error_size);
        if (!document) {
            return NULL;
        }
        asset_prefix_edits(text, av_doc_root(document), false, false, &edits);
        av_doc_free(document);
    }
    char *out = prefix_apply(text, edits);
    prefix_edits_free(edits);
    if (out && !source) {
        av_doc *document = asset_text_read(out, error, error_size);
        if (!document) {
            free(out);
            return NULL;
        }
        av_doc_free(document);
    }
    return out;
}

static char **migrate_prefixes(char **sources, char **paths, int count, char *error, size_t error_size)
{
    corpus context = { .sources = (const char *const *)sources };
    script_syntax **syntax = xcalloc((size_t)count, sizeof *syntax);
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count; i++) {
        if (has_suffix(paths[i], ".chibi")) {
            syntax[i] = parse23(sources[i]);
            collect(
                ts_tree_root_node(syntax[i]->tree),
                i,
                (TSNode){ 0 },
                true,
                &context.functions
            );
        }
    }
    for (int i = 0; i < count && !error[0]; i++) {
        if (syntax[i]) {
            replacement *edits = NULL;
            source_prefix_edits(&context, i, ts_tree_root_node(syntax[i]->tree), &edits);
            migrated[i] = prefix_apply(sources[i], edits);
            prefix_edits_free(edits);
        } else {
            migrated[i] = chibi_migrate_prefixes(sources[i], false, error, error_size);
        }
    }
    for (int i = 0; i < count; i++) {
        if (syntax[i]) {
            script_syntax_free(syntax[i]);
        }
    }
    free(syntax);
    afree(context.functions);
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
/* Removes the GameHRC2D fields version 22 moves out of authoring from the
 * component under each of `components`' names, and from the field list of a
 * component presentation. Returns whether any were there. */
static bool drop_lab_fields(av_value *components)
{
    static const char *const names[] = { "GAME.GameHRC2D", "GameHRC2D" };
    static const char *const fields[] = {
        "color_resolution",       "gather_resolution",
        "min_resolution",         "upscale_mode",
        "upscale_threshold",      "debug_view",
        "particle_focal_length",  "trace_levels",
        "surface_depth_emitters", "surface_depth_near_detail",
        "horizon_fuse_relief",    "horizon_cache",
        "horizon_reproject",      "horizon_hierarchy",
    };
    bool changed = false;
    for (size_t i = 0; av_is_obj(components) && i < sizeof names / sizeof *names; i++) {
        av_value *component = av_obj_get(components, names[i]);
        av_value *presented = av_is_obj(component) ? av_obj_get(component, "fields")
                                                   : NULL;
        for (size_t j = 0; av_is_obj(component) && j < sizeof fields / sizeof *fields;
             j++) {
            changed |= av_obj_remove_key(component, fields[j]);
            changed |= av_is_obj(presented) &&
                       av_obj_remove_key(presented, fields[j]);
        }
    }
    return changed;
}
/* Scene children use the same component and retained-component schema. */
static bool drop_entity_lab_fields(av_value *entities)
{
    bool changed = false;
    for (size_t i = 0; av_is_arr(entities) && i < av_arr_size(entities); i++) {
        av_value *entity = av_arr_get(entities, i);
        if (!av_is_obj(entity)) {
            continue;
        }
        changed |= drop_lab_fields(av_obj_get(entity, "components"));
        changed |= drop_lab_fields(av_obj_get(entity, "retained_components"));
        changed |= drop_entity_lab_fields(av_obj_get(entity, "children"));
    }
    return changed;
}
/* Returns an owned copy of a scene, prototype or component presentation
 * `asset` without the GameHRC2D fields version 22 removes, or NULL when it
 * has none. */
static char *drop_lab_settings(const char *asset)
{
    char message[128];
    av_doc *document = asset_text_read(asset, message, sizeof message);
    av_value *root = document ? av_doc_root(document) : NULL;
    if (!av_is_obj(root)) {
        av_doc_free(document);
        return NULL;
    }
    bool changed = drop_lab_fields(av_obj_get(root, "components"));
    changed |= drop_lab_fields(av_obj_get(root, "detached_components"));
    changed |= drop_entity_lab_fields(av_obj_get(root, "entities"));
    char *migrated = changed ? asset_text_write(root) : NULL;
    av_doc_free(document);
    return migrated;
}
/* Replace only syntax tokens, preserving comments, strings and each name. */
static void declaration_edits(const char *text, TSNode node, replacement **edits)
{
    if (is(node, "call_expression")) {
        TSNode target = ts_node_child_by_field_name(node, "function", 8);
        /* Version 23 treated not as an ordinary callable name. Version 24
         * added its unary spelling: quote the target for that historical
         * parser, without assuming a native call rather than authored shadow.
         * The catalog-aware final migration chooses unary ! only for natives. */
        if (is(target, "identifier") && names(text, target, "not")) {
            apush(
                *edits,
                ((replacement){ .start = (int)ts_node_start_byte(target),
                                .end = (int)ts_node_end_byte(target),
                                .text = xstrdup("\"not\"") })
            );
        }
    }
    if (is(node, "function_declaration")) {
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        bool pure = false;
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (!is(child, "pure") && !is(child, "function")) {
                continue;
            }
            pure |= is(child, "pure");
            int end = (int)ts_node_end_byte(child);
            while (text[end] == ' ' || text[end] == '\t') {
                end++;
            }
            apush(
                *edits,
                ((replacement){ .start = (int)ts_node_start_byte(child),
                                .end = end,
                                .text = xstrdup("") })
            );
        }
        int end = (int)ts_node_end_byte(name);
        apush(
            *edits,
            ((replacement){ .start = end,
                            .end = end,
                            .text = xstrdup(pure ? " :: pure fn" : " :: fn") })
        );
    }
    if (is(node, "match_statement")) {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (is(child, "match") || is(child, "enum") || is(child, "name")) {
                apush(
                    *edits,
                    ((replacement){ .start = (int)ts_node_start_byte(child),
                                    .end = (int)ts_node_end_byte(child),
                                    .text = xstrdup(is(child, "match") ? "switch" : "") })
                );
            }
        }
    }
    if (is(node, "match_arm")) {
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        if (!ts_node_is_null(value)) {
            int end = (int)ts_node_end_byte(value);
            apush(*edits, ((replacement){ .start = end, .end = end, .text = xstrdup(":") }));
        } else {
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                if (is(child, "default")) {
                    apush(
                        *edits,
                        ((replacement){ .start = (int)ts_node_start_byte(child),
                                        .end = (int)ts_node_end_byte(child),
                                        .text = xstrdup("case:") })
                    );
                }
            }
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        declaration_edits(text, ts_node_named_child(node, i), edits);
    }
}

int chibi_migrate_declarations(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *current = script_parse(source);
    bool done = !current->diagnostic_count;
    script_syntax_free(current);
    if (done) {
        return 0;
    }
    current = parse24(source);
    done = !current->diagnostic_count;
    script_syntax_free(current);
    if (done) {
        return 0;
    }
    current = parse25(source);
    done = !current->diagnostic_count;
    script_syntax_free(current);
    if (done) {
        return 0;
    }
    current = parse26(source);
    done = !current->diagnostic_count;
    script_syntax_free(current);
    if (done) {
        return 0;
    }
    script_syntax *syntax = parse23(source);
    if (syntax->diagnostic_count) {
        snprintf(error, size, "Cannot migrate invalid version 23 source");
        script_syntax_free(syntax);
        return -1;
    }
    replacement *edits = NULL;
    declaration_edits(source, ts_tree_root_node(syntax->tree), &edits);
    char *result = prefix_apply(source, edits);
    prefix_edits_free(edits);
    script_syntax_free(syntax);
    script_syntax *check = parse24(result ? result : source);
    bool valid = !check->diagnostic_count;
    if (!valid) {
        script_diagnostic diagnostic = check->diagnostics[0];
        snprintf(
            error,
            size,
            "Declaration migration produced invalid source at %d:%d: %s",
            diagnostic.range.line,
            diagnostic.range.column,
            diagnostic.message
        );
        script_syntax_free(check);
        free(result);
        return -1;
    }
    script_syntax_free(check);
    *migrated = result;
    return 0;
}

/* Move storage to the declaration's trait position without consuming comments. */
static void retention_edits(const char *text, TSNode node, replacement **edits)
{
    TSNode storage = ts_node_child_by_field_name(node, "storage", 7);
    if (is(node, "state_declaration") && !ts_node_is_null(storage)) {
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        TSNode type = ts_node_child_by_field_name(node, "type", 4);
        const char *trait = is(storage, "shared") ? "retain(system)" : "retain";
        int end = (int)ts_node_end_byte(storage);
        while (text[end] == ' ' || text[end] == '\t') {
            end++;
        }
        apush(
            *edits,
            ((replacement){ .start = (int)ts_node_start_byte(storage),
                            .end = end,
                            .text = xstrdup("") })
        );
        if (!ts_node_is_null(type) &&
            ts_node_start_byte(type) < ts_node_start_byte(name)) {
            char *spelling = copy_text(text + ts_node_start_byte(type), text + ts_node_end_byte(type));
            size_t size = strlen(trait) + strlen(spelling) + 4;
            char *annotation = xmalloc(size);
            snprintf(annotation, size, ": %s %s", trait, spelling);
            free(spelling);
            end = (int)ts_node_end_byte(type);
            while (text[end] == ' ' || text[end] == '\t') {
                end++;
            }
            apush(
                *edits,
                ((replacement){ .start = (int)ts_node_start_byte(type),
                                .end = end,
                                .text = xstrdup("") })
            );
            end = (int)ts_node_end_byte(name);
            apush(*edits, ((replacement){ .start = end, .end = end, .text = annotation }));
        } else {
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                if (!is(child, ":") && !is(child, ":=")) {
                    continue;
                }
                bool inferred = is(child, ":=");
                size_t size = strlen(trait) + 6;
                char *annotation = xmalloc(size);
                snprintf(annotation, size, inferred ? ": %s =" : ": %s", trait);
                apush(
                    *edits,
                    ((replacement){ .start = (int)ts_node_start_byte(child),
                                    .end = (int)ts_node_end_byte(child),
                                    .text = annotation })
                );
                break;
            }
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        retention_edits(text, ts_node_named_child(node, i), edits);
    }
}

int chibi_migrate_retention(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = parse24(source);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        if (valid_at(source, 25)) {
            return 0;
        }
        snprintf(error, size, "Cannot migrate invalid version 24 source");
        return -1;
    }
    replacement *edits = NULL;
    retention_edits(source, ts_tree_root_node(syntax->tree), &edits);
    char *result = prefix_apply(source, edits);
    prefix_edits_free(edits);
    script_syntax_free(syntax);
    script_syntax *check = parse25(result ? result : source);
    bool valid = !check->diagnostic_count;
    if (!valid) {
        const script_diagnostic *diagnostic = &check->diagnostics[0];
        snprintf(
            error,
            size,
            "Retention migration produced invalid source at %d:%d: %s",
            diagnostic->range.line,
            diagnostic->range.column,
            diagnostic->message
        );
        script_syntax_free(check);
        free(result);
        return -1;
    }
    script_syntax_free(check);
    *migrated = result;
    return 0;
}

static void syntax_edit(replacement **edits, int start, int end, const char *text)
{
    apush(*edits, ((replacement){ .start = start, .end = end, .text = xstrdup(text) }));
}

static void remove_syntax_token(const char *text, TSNode node, replacement **edits)
{
    int end = (int)ts_node_end_byte(node);
    while (text[end] == ' ' || text[end] == '\t') {
        end++;
    }
    syntax_edit(edits, (int)ts_node_start_byte(node), end, "");
}

typedef struct
{
    TSNode declaration, name, scope;
    char *before, *after;
} cleanup_name;

static char *cleanup_identifier(const char *name, bool native)
{
    if (native && (!strcmp(name, "Branch") || !strcmp(name, "branch"))) {
        return xstrdup("branch_if");
    }
    char *out = xmalloc(strlen(name) + 3), *at = out;
    if (isdigit((unsigned char)*name)) {
        *at++ = '_';
    }
    bool separator = false, letter = false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        bool ascii = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9');
        if (ascii || *p == '_') {
            if (separator && at > out && at[-1] != '_') {
                *at++ = '_';
            }
            *at++ = native ? (char)tolower(*p) : (char)*p;
            separator = false;
            letter |= ascii;
        } else {
            separator = true;
        }
    }
    *at = 0;
    if (!letter) {
        free(out);
        return NULL;
    }
    static const char *const keywords[] = {
        "behavior",     "library",  "system",  "component", "prototype",
        "relationship", "retain",   "state",   "action",    "event",
        "disabled",     "on",       "after",   "as",        "bind",
        "events",       "union",    "struct",  "switch",    "is",
        "where",        "macro",    "entry",   "exit",      "description",
        "domain",       "dataflow", "query",   "if",        "else",
        "while",        "for",      "in",      "case",      "sequence",
        "branch",       "return",   "reduce",  "goto",      "break",
        "continue",     "await",    "yield",   "cancel",    "true",
        "false",        "nil",      "null",    "cast",      "plain",
        "ref",          "service",  "setting", "wildcard"
    };
    for (size_t i = 0; !native && i < sizeof keywords / sizeof *keywords; i++) {
        if (!strcmp(out, keywords[i])) {
            memmove(out + 1, out, strlen(out) + 1);
            out[0] = '_';
            break;
        }
    }
    return out;
}

static char *cleanup_qualified_identifier(const char *name)
{
    char *parts = xstrdup(name), *out = xmalloc(strlen(name) * 2 + 3), *at = out;
    for (char *part = parts, *next; part; part = next) {
        next = strchr(part, '.');
        if (next) {
            *next++ = 0;
        }
        char *identifier = cleanup_identifier(part, false);
        if (!identifier) {
            free(parts);
            free(out);
            return NULL;
        }
        if (at > out) {
            *at++ = '.';
        }
        at = stpcpy(at, identifier);
        free(identifier);
    }
    free(parts);
    return out;
}

static void cleanup_type_edits(const char *text, TSNode node, int offset, replacement **edits)
{
    if (is(node, "string") && is(ts_node_parent(node), "type_name")) {
        char *before = name_text(text, node),
             *after = cleanup_qualified_identifier(before);
        if (after) {
            syntax_edit(
                edits,
                (int)ts_node_start_byte(node) - offset,
                (int)ts_node_end_byte(node) - offset,
                after
            );
        }
        free(before);
        free(after);
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        cleanup_type_edits(text, ts_node_named_child(node, i), offset, edits);
    }
}

static char *cleanup_type_spelling(const char *text, TSNode node)
{
    char *before = copy_text(text + ts_node_start_byte(node), text + ts_node_end_byte(node));
    replacement *edits = NULL;
    cleanup_type_edits(text, node, (int)ts_node_start_byte(node), &edits);
    char *after = prefix_apply(before, edits);
    prefix_edits_free(edits);
    if (!after) {
        return before;
    }
    free(before);
    return after;
}

static bool cleanup_type_moves(TSNode node)
{
    for (TSNode parent = ts_node_parent(node); !ts_node_is_null(parent);
         parent = ts_node_parent(parent)) {
        if (!is(parent, "state_declaration") && !is(parent, "parameter")) {
            continue;
        }
        TSNode type = ts_node_child_by_field_name(parent, "type", 4);
        TSNode name = ts_node_child_by_field_name(parent, "name", 4);
        return !ts_node_is_null(type) &&
               ts_node_start_byte(type) < ts_node_start_byte(name) &&
               ts_node_start_byte(type) <= ts_node_start_byte(node) &&
               ts_node_end_byte(type) >= ts_node_end_byte(node);
    }
    return false;
}

static TSNode cleanup_scope(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    if (is(node, "parameter")) {
        parent = ts_node_parent(parent);
        TSNode body = ts_node_child_by_field_name(parent, "body", 4);
        if (!ts_node_is_null(body)) {
            return body;
        }
    }
    for (; !ts_node_is_null(parent); parent = ts_node_parent(parent)) {
        if (is(parent, "source_file") || is(parent, "declaration_body") ||
            is(parent, "system_body") || is(parent, "callable_body") ||
            is(parent, "block") || is(parent, "component_body") ||
            is(parent, "record_body")) {
            return parent;
        }
    }
    return node;
}

static bool cleanup_declaration(TSNode node)
{
    static const char *const kinds[] = { "behavior",
                                         "library",
                                         "system",
                                         "component",
                                         "state_declaration",
                                         "local_declaration",
                                         "function_declaration",
                                         "macro_declaration",
                                         "constant_declaration",
                                         "union_declaration",
                                         "record_declaration",
                                         "record_field",
                                         "component_field",
                                         "parameter",
                                         "type_parameter",
                                         "action_declaration",
                                         "event_declaration",
                                         "bind_declaration",
                                         "events_declaration",
                                         "entry_declaration",
                                         "exit_declaration",
                                         "label_statement",
                                         "for_statement",
                                         "foreach_statement",
                                         "query_generator" };
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        if (is(node, kinds[i])) {
            return true;
        }
    }
    return false;
}

static bool cleanup_collect_names(const char *text, TSNode node, cleanup_name **names, char *error, size_t size)
{
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    if (cleanup_declaration(node) && !ts_node_is_null(name)) {
        char *before = name_text(text, name);
        char *after = is(name, "string") ? cleanup_identifier(before, false)
                                         : xstrdup(before);
        if (!after) {
            snprintf(error, size, "Cannot migrate symbolic name '%s' to an identifier; rename it before migration", before);
            free(before);
            return false;
        }
        apush(*names, ((cleanup_name){ node, name, cleanup_scope(node), before, after }));
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!cleanup_collect_names(text, ts_node_named_child(node, i), names, error, size)) {
            return false;
        }
    }
    return true;
}

static bool cleanup_contains(TSNode scope, TSNode node)
{
    return ts_node_start_byte(scope) <= ts_node_start_byte(node) &&
           ts_node_end_byte(scope) >= ts_node_end_byte(node);
}

static cleanup_name *cleanup_binding(cleanup_name *names, TSNode node, const char *name)
{
    cleanup_name *found = NULL;
    for (int i = 0; i < acount(names); i++) {
        cleanup_name *candidate = &names[i];
        if (strcmp(candidate->before, name) ||
            !cleanup_contains(candidate->scope, node)) {
            continue;
        }
        if (!found ||
            ts_node_end_byte(candidate->scope) - ts_node_start_byte(candidate->scope) <
                ts_node_end_byte(found->scope) - ts_node_start_byte(found->scope)) {
            found = candidate;
        }
    }
    return found;
}

static bool cleanup_native_shadows(const char *text, TSNode node, cleanup_name *names, char *error, size_t size)
{
    if (is(node, "engine_reference")) {
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        char *before = name_text(text, name),
             *native = cleanup_identifier(before, true);
        bool simple = native &&
                      strspn(before, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_ -") ==
                          strlen(before);
        if (!simple) {
            snprintf(error, size, "Cannot migrate native capability '%s'; use its registered source identifier", before);
            free(before);
            free(native);
            return false;
        }
        for (int i = 0; i < acount(names); i++) {
            cleanup_name *binding = &names[i];
            if (!names_match(binding->after, native) ||
                !cleanup_contains(binding->scope, node)) {
                continue;
            }
            const char *suffix = is(binding->declaration, "function_declaration")
                                   ? "_function"
                                   : "_action";
            size_t length = strlen(binding->after) + strlen(suffix) + 1;
            char *renamed = xmalloc(length);
            snprintf(renamed, length, "%s%s", binding->after, suffix);
            free(binding->after);
            binding->after = renamed;
        }
        free(before);
        free(native);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!cleanup_native_shadows(text, ts_node_named_child(node, i), names, error, size)) {
            return false;
        }
    }
    return true;
}

static bool cleanup_symbol_edits(
    const char *text,
    TSNode node,
    cleanup_name *names,
    replacement **edits,
    char *error,
    size_t size
)
{
    if (is(node, "engine_reference")) {
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        char *before = name_text(text, name),
             *native = cleanup_identifier(before, true);
        assert(native);
        TSNode parent = ts_node_parent(node);
        bool self = !strcmp(native, "self") && is(parent, "call_expression");
        TSNode arguments = ts_node_child_by_field_name(parent, "arguments", 9);
        if (self && ts_node_named_child_count(arguments)) {
            snprintf(error, size, "Cannot migrate Self with arguments");
            free(before);
            free(native);
            return false;
        }
        syntax_edit(edits, (int)ts_node_start_byte(node), (int)ts_node_end_byte(self ? parent : node), native);
        free(before);
        free(native);
        return true;
    }
    if (is(node, "identifier") || is(node, "string")) {
        TSNode parent = ts_node_parent(node);
        char *before = name_text(text, node);
        const char *after = NULL;
        char *normalized = NULL;
        for (int i = 0; i < acount(names); i++) {
            if (ts_node_eq(names[i].name, node)) {
                after = names[i].after;
                break;
            }
        }
        bool type_name = is(parent, "type_name");
        if (type_name && cleanup_type_moves(node)) {
            free(before);
            return true;
        }
        bool symbolic = !ts_node_is_null(parent) &&
                        ((is(parent, "member_expression") &&
                          (ts_node_eq(node, ts_node_child_by_field_name(parent, "member", 6)) ||
                           is(ts_node_parent(parent), "call_expression"))) ||
                         is(parent, "argument_name") || type_name ||
                         (is(parent, "handler") &&
                          (ts_node_eq(node, ts_node_child_by_field_name(parent, "name", 4)) ||
                           ts_node_eq(node, ts_node_child_by_field_name(parent, "alias", 5)))) ||
                         ((is(parent, "call_expression") || is(parent, "goto_statement")) &&
                          ts_node_eq(
                              node,
                              ts_node_child_by_field_name(
                                  parent,
                                  is(parent, "call_expression") ? "function" : "name",
                                  is(parent, "call_expression") ? 8 : 4
                              )
                          )));
        /* A receiver's member, a callee's argument label and a type name do
         * not resolve against the caller's lexical value bindings. */
        bool member = is(parent, "member_expression") &&
                      ts_node_eq(node, ts_node_child_by_field_name(parent, "member", 6));
        char *object_name = member
                              ? name_text(text, ts_node_child_by_field_name(parent, "object", 6))
                              : NULL;
        bool lexical_namespace = object_name && (!strcmp(object_name, "state") ||
                                                 !strcmp(object_name, "entry"));
        free(object_name);
        cleanup_name *binding = (member && !lexical_namespace) ||
                                        is(parent, "argument_name") || type_name
                                  ? NULL
                                  : cleanup_binding(names, node, before);
        if (!after && binding && (is(node, "identifier") || symbolic)) {
            after = binding->after;
        }
        if (!after && is(node, "string") && symbolic) {
            normalized = type_name ? cleanup_qualified_identifier(before)
                                   : cleanup_identifier(before, false);
            after = normalized;
        }
        if (after && (is(node, "string") || strcmp(before, after))) {
            syntax_edit(edits, (int)ts_node_start_byte(node), (int)ts_node_end_byte(node), after);
        }
        free(normalized);
        free(before);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!cleanup_symbol_edits(text, ts_node_named_child(node, i), names, edits, error, size)) {
            return false;
        }
    }
    return true;
}

static void cleanup_names_free(cleanup_name *names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i].before);
        free(names[i].after);
    }
    afree(names);
}

static bool cleanup_edits(const char *text, TSNode root, replacement **edits, char *error, size_t size);

/* Old per-row system state has no equivalent registration-owned binding. */
static bool cleanup_syntax_edits(const char *text, TSNode node, replacement **edits, char *error, size_t size)
{
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    TSNode type = ts_node_child_by_field_name(node, "type", 4);
    if (is(node, "state_declaration")) {
        TSNode storage = ts_node_child_by_field_name(node, "storage", 7);
        TSNode owner = ts_node_child_by_field_name(node, "owner", 5);
        bool system = false;
        for (TSNode parent = ts_node_parent(node); !ts_node_is_null(parent);
             parent = ts_node_parent(parent)) {
            system |= is(parent, "system");
        }
        if (system && ts_node_is_null(owner) &&
            ((!ts_node_is_null(storage) && is(storage, "retain")) ||
             is(ts_node_parent(node), "system_body"))) {
            TSPoint point = ts_node_start_point(name);
            snprintf(
                error,
                size,
                "Per-row system state at %u:%u cannot become "
                "registration-owned retain; move durable entity state to a "
                "component, or use an ordinary coroutine local",
                point.row + 1,
                point.column + 1
            );
            return false;
        }
        if (!ts_node_is_null(owner)) {
            /* Preserve comments between the retired ownership tokens. */
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                if (is(child, "(") || is(child, ")") ||
                    (ts_node_start_byte(child) == ts_node_start_byte(owner) &&
                     ts_node_end_byte(child) == ts_node_end_byte(owner))) {
                    syntax_edit(edits, (int)ts_node_start_byte(child), (int)ts_node_end_byte(child), "");
                }
            }
        }
    }
    if (is(node, "behavior") || is(node, "library") || is(node, "system") ||
        is(node, "component")) {
        const char *kind = ts_node_type(node);
        TSNode parameters = ts_node_child_by_field_name(node, "parameters", 10);
        bool query = !ts_node_is_null(parameters) &&
                     is(parameters, "query_parameter_list");
        char suffix[48];
        snprintf(
            suffix,
            sizeof suffix,
            " :: %s%s",
            kind,
            is(node, "system") ? ts_node_is_null(parameters) ? "()"
                               : query                       ? "("
                                                             : ""
                               : ""
        );
        syntax_edit(edits, (int)ts_node_end_byte(name), (int)ts_node_end_byte(name), suffix);
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (is(child, kind) || (query && is(child, "query"))) {
                remove_syntax_token(text, child, edits);
            }
        }
        if (query) {
            syntax_edit(edits, (int)ts_node_end_byte(parameters), (int)ts_node_end_byte(parameters), ")");
        }
    }
    bool type_first = !ts_node_is_null(name) && !ts_node_is_null(type) &&
                      ts_node_start_byte(type) < ts_node_start_byte(name) &&
                      (is(node, "state_declaration") || is(node, "parameter"));
    if (type_first) {
        char *spelling = cleanup_type_spelling(text, type);
        size_t length = strlen(spelling) + 12;
        char *annotation = xmalloc(length);
        snprintf(annotation, length, ": %s", spelling);
        remove_syntax_token(text, type, edits);
        syntax_edit(edits, (int)ts_node_end_byte(name), (int)ts_node_end_byte(name), annotation);
        free(annotation);
        free(spelling);
    }
    if (is(node, "function_declaration") || is(node, "foreach_statement")) {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (is(child, "fn") || is(child, "pure")) {
                remove_syntax_token(text, child, edits);
            } else if (is(child, "foreach")) {
                syntax_edit(edits, (int)ts_node_start_byte(child), (int)ts_node_end_byte(child), "for");
            }
        }
    }
    if (is(node, "metadata_declaration")) {
        TSNode kind = ts_node_child_by_field_name(node, "kind", 4);
        TSNode value = ts_node_child_by_field_name(node, "value", 5);
        bool owner = is(kind, "owner");
        {
            char *spelling = copy_text(text + ts_node_start_byte(value), text + ts_node_end_byte(value));
            bool supported = !owner || !strcmp(spelling, "\"ecs.entity\"") ||
                             !strcmp(spelling, "ecs.entity");
            free(spelling);
            if (!supported) {
                snprintf(error, size, "Cannot migrate custom owner metadata; only ecs.entity preserves the current runtime contract");
                return false;
            }
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                if (!is(child, "comment")) {
                    remove_syntax_token(text, child, edits);
                }
            }
            return true;
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!cleanup_syntax_edits(text, ts_node_named_child(node, i), edits, error, size)) {
            return false;
        }
    }
    return true;
}

static bool cleanup_edits(const char *text, TSNode root, replacement **edits, char *error, size_t size)
{
    cleanup_name *names = NULL;
    bool valid = cleanup_collect_names(text, root, &names, error, size) &&
                 cleanup_native_shadows(text, root, names, error, size);
    for (int i = 0; valid && i < acount(names); i++) {
        for (int j = 0; j < i; j++) {
            if (ts_node_eq(names[i].scope, names[j].scope) &&
                !strcmp(names[i].after, names[j].after) &&
                strcmp(names[i].before, names[j].before)) {
                snprintf(
                    error,
                    size,
                    "Symbol migration would collide '%s' with '%s'; rename one "
                    "before migration",
                    names[i].before,
                    names[j].before
                );
                valid = false;
                break;
            }
        }
    }
    if (valid) {
        valid = cleanup_syntax_edits(text, root, edits, error, size) &&
                cleanup_symbol_edits(text, root, names, edits, error, size);
    }
    cleanup_names_free(names);
    return valid;
}

typedef struct
{
    TSNode callable, list;
    char *name, *bound;
} cleanup_legacy_type;

static void cleanup_legacy_comments(const char *text, TSNode node, char **out)
{
    if (is(node, "comment")) {
        size_t old = *out ? strlen(*out) : 0;
        size_t bytes = ts_node_end_byte(node) - ts_node_start_byte(node);
        *out = xrealloc(*out, old + bytes + 2);
        memcpy(*out + old, text + ts_node_start_byte(node), bytes);
        (*out)[old + bytes] = '\n';
        (*out)[old + bytes + 1] = 0;
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        cleanup_legacy_comments(text, ts_node_named_child(node, i), out);
    }
}

static bool cleanup_legacy_types(
    const char *text,
    TSNode node,
    cleanup_legacy_type **types,
    replacement **edits,
    char *error,
    size_t size
)
{
    TSNode list = ts_node_child_by_field_name(node, "type_parameters", 15);
    if (!ts_node_is_null(list)) {
        for (uint32_t i = 0; i < ts_node_named_child_count(list); i++) {
            TSNode parameter = ts_node_named_child(list, i);
            if (ts_node_is_extra(parameter)) {
                continue;
            }
            TSNode bound = ts_node_child_by_field_name(parameter, "type", 4);
            if (ts_node_is_null(bound)) {
                snprintf(error, size, "Cannot migrate unbound legacy <T> declarations; replace them with argument binders such as (value: $T) -> T");
                return false;
            }
            char *name = name_text(text, ts_node_child_by_field_name(parameter, "name", 4));
            char *spelling = copy_text(text + ts_node_start_byte(bound), text + ts_node_end_byte(bound));
            apush(*types, ((cleanup_legacy_type){ node, list, name, spelling }));
        }
        char *comments = NULL;
        cleanup_legacy_comments(text, list, &comments);
        syntax_edit(
            edits,
            (int)ts_node_start_byte(list),
            (int)ts_node_end_byte(list),
            comments ? comments : ""
        );
        free(comments);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!cleanup_legacy_types(text, ts_node_named_child(node, i), types, edits, error, size)) {
            return false;
        }
    }
    return true;
}

static bool cleanup_legacy_uses(
    const char *text,
    TSNode node,
    cleanup_legacy_type *types,
    replacement **edits,
    char *error,
    size_t size
)
{
    cleanup_legacy_type *selected = NULL;
    for (int i = 0; i < acount(types); i++) {
        if (ts_node_start_byte(node) >= ts_node_start_byte(types[i].list) &&
            ts_node_end_byte(node) <= ts_node_end_byte(types[i].list)) {
            return true;
        }
        if (ts_node_start_byte(node) < ts_node_start_byte(types[i].callable) ||
            ts_node_end_byte(node) > ts_node_end_byte(types[i].callable)) {
            continue;
        }
        TSNode declared = ts_node_child_by_field_name(node, "name", 4);
        if ((is(node, "record_declaration") || is(node, "union_declaration") ||
             is(node, "constant_declaration")) &&
            !ts_node_is_null(declared)) {
            char *name = name_text(text, declared);
            bool shadows = !strcmp(name, types[i].name);
            free(name);
            if (shadows) {
                snprintf(
                    error,
                    size,
                    "Legacy generic alias '%s' is shadowed; rename the inner "
                    "type or constant before migration",
                    types[i].name
                );
                return false;
            }
        }
        if (!is(node, "type_name")) {
            continue;
        }
        char *name = name_text(text, node);
        bool match = !strcmp(name, types[i].name);
        free(name);
        if (match && (!selected || ts_node_start_byte(types[i].callable) >
                                       ts_node_start_byte(selected->callable))) {
            selected = &types[i];
        }
    }
    if (selected) {
        syntax_edit(
            edits,
            (int)ts_node_start_byte(node),
            (int)ts_node_end_byte(node),
            selected->bound
        );
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!cleanup_legacy_uses(text, ts_node_named_child(node, i), types, edits, error, size)) {
            return false;
        }
    }
    return true;
}

static bool cleanup_legacy_bound(const char *text, TSNode node, cleanup_legacy_type *types)
{
    if (is(node, "type_name")) {
        char *name = name_text(text, node);
        for (int i = 0; i < acount(types); i++) {
            if (!strcmp(name, types[i].name)) {
                free(name);
                return false;
            }
        }
        free(name);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!cleanup_legacy_bound(text, ts_node_named_child(node, i), types)) {
            return false;
        }
    }
    return true;
}

static int migrate_syntax_spelling(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = parse25(source);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        syntax = parse26(source);
        bool valid = !syntax->diagnostic_count;
        script_syntax_free(syntax);
        if (valid) {
            return 0;
        }
        snprintf(error, size, "Cannot migrate invalid version 25 source");
        return -1;
    }
    replacement *edits = NULL;
    cleanup_legacy_type *types = NULL;
    bool valid = cleanup_legacy_types(source, ts_tree_root_node(syntax->tree), &types, &edits, error, size) &&
                 cleanup_legacy_uses(source, ts_tree_root_node(syntax->tree), types, &edits, error, size);
    for (int i = 0; valid && i < acount(types); i++) {
        for (uint32_t j = 0; j < ts_node_named_child_count(types[i].list); j++) {
            TSNode bound = ts_node_child_by_field_name(
                ts_node_named_child(types[i].list, j),
                "type",
                4
            );
            if (!ts_node_is_null(bound) && !cleanup_legacy_bound(source, bound, types)) {
                snprintf(error, size, "Legacy generic aliases require concrete independent bounds; expand dependent bounds before migration");
                valid = false;
            }
        }
    }
    char *legacy = valid ? prefix_apply(source, edits) : NULL;
    for (int i = 0; i < acount(types); i++) {
        free(types[i].name);
        free(types[i].bound);
    }
    afree(types);
    prefix_edits_free(edits);
    edits = NULL;
    if (!valid) {
        script_syntax_free(syntax);
        free(legacy);
        return -1;
    }
    if (legacy) {
        source = legacy;
        script_syntax_free(syntax);
        syntax = parse25(source);
        if (syntax->diagnostic_count) {
            snprintf(error, size, "Legacy generic aliases require concrete independent bounds; expand dependent bounds before migration");
            script_syntax_free(syntax);
            free(legacy);
            return -1;
        }
    }
    valid = cleanup_edits(source, ts_tree_root_node(syntax->tree), &edits, error, size);
    char *result = valid ? prefix_apply(source, edits) : NULL;
    if (valid && !result && legacy) {
        result = xstrdup(legacy);
    }
    free(legacy);
    prefix_edits_free(edits);
    script_syntax_free(syntax);
    if (!valid) {
        return -1;
    }
    syntax = parse26(result ? result : source);
    valid = !syntax->diagnostic_count;
    if (!valid) {
        script_diagnostic diagnostic = syntax->diagnostics[0];
        snprintf(
            error,
            size,
            "Syntax migration produced invalid source at %d:%d: %.80s",
            diagnostic.range.line,
            diagnostic.range.column,
            (result ? result : source) + diagnostic.range.start
        );
        script_syntax_free(syntax);
        free(result);
        return -1;
    }
    script_syntax_free(syntax);
    *migrated = result;
    return 0;
}

int chibi_migrate_syntax(const char *source, char **migrated, char *error, size_t size)
{
    if (migrate_syntax_spelling(source, migrated, error, size)) {
        return -1;
    }
    const char *input = *migrated ? *migrated : source;
    text_edit **edits = NULL;
    bool valid = chibi_migrate26_references(&input, 1, &edits, error, size);
    if (valid && acount(edits[0])) {
        char *output = text_edits_apply(input, edits[0], acount(edits[0]));
        if (strcmp(output, input)) {
            free(*migrated);
            *migrated = output;
        } else {
            free(output);
        }
    }
    chibi_migrate26_edits_free(edits, 1);
    if (!valid) {
        free(*migrated);
        *migrated = NULL;
        return -1;
    }
    return 0;
}

typedef struct
{
    char *before, *after, *owner;
    const char *kind;
} cleanup_asset_name;

static bool cleanup_asset_names(const char *text, cleanup_asset_name **names, char *error, size_t size)
{
    script_syntax *syntax = parse25(text);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        syntax = parse26(text);
    }
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        return true; /* The owning source migration reports the invalid syntax. */
    }
    TSNode source = ts_tree_root_node(syntax->tree), root = { 0 };
    for (uint32_t i = 0; i < ts_node_named_child_count(source); i++) {
        TSNode child = ts_node_named_child(source, i);
        if (!ts_node_is_extra(child)) {
            root = child;
            break;
        }
    }
    if (ts_node_is_null(root)) {
        script_syntax_free(syntax);
        return true;
    }
    TSNode name = ts_node_child_by_field_name(root, "name", 4);
    char *before = name_text(text, name);
    char *after = is(name, "string") ? cleanup_identifier(before, false)
                                     : xstrdup(before);
    if (!after) {
        snprintf(error, size, "Cannot migrate project symbolic name '%s'; rename it before migration", before);
        free(before);
        script_syntax_free(syntax);
        return false;
    }
    const char *kind = is(root, "component") ? "component"
                     : is(root, "system")    ? "system"
                     : is(root, "behavior")  ? "behavior"
                                             : "library";
    for (int i = 0; i < acount(*names); i++) {
        cleanup_asset_name *previous = &(*names)[i];
        if (!previous->owner && !strcmp(previous->after, after) &&
            strcmp(previous->before, before)) {
            snprintf(
                error,
                size,
                "Project symbol migration would collide '%s' with '%s'; rename "
                "one before migration",
                before,
                previous->before
            );
            free(before);
            free(after);
            script_syntax_free(syntax);
            return false;
        }
    }
    apush(*names, ((cleanup_asset_name){ before, after, NULL, kind }));
    if (is(root, "component")) {
        TSNode body = ts_node_child_by_field_name(root, "body", 4);
        for (uint32_t i = 0; i < ts_node_named_child_count(body); i++) {
            TSNode field = ts_node_named_child(body, i);
            if (!is(field, "component_field")) {
                continue;
            }
            TSNode field_name = ts_node_child_by_field_name(field, "name", 4);
            char *field_before = name_text(text, field_name);
            char *field_after = is(field_name, "string")
                                  ? cleanup_identifier(field_before, false)
                                  : xstrdup(field_before);
            if (field_after) {
                apush(*names, ((cleanup_asset_name){ field_before, field_after, xstrdup(before), "field" }));
            } else {
                free(field_before);
            }
        }
    }
    script_syntax_free(syntax);
    return true;
}

static const cleanup_asset_name *cleanup_asset_find(
    const cleanup_asset_name *names,
    const char *name,
    const char *kind,
    const char *owner
)
{
    for (int i = 0; name && i < acount(names); i++) {
        if (strcmp(names[i].kind, kind) || strcmp(names[i].before, name) ||
            (owner ? !names[i].owner || strcmp(names[i].owner, owner)
                   : names[i].owner != NULL)) {
            continue;
        }
        return &names[i];
    }
    return NULL;
}

static void cleanup_asset_edits(
    const av_value *value,
    const cleanup_asset_name *names,
    const char *reference,
    bool components,
    const char *owner,
    replacement **edits
)
{
    if (av_is_str(value) && reference) {
        const cleanup_asset_name *name = cleanup_asset_find(names, av_get_str(value), reference, NULL);
        if (name && strcmp(name->before, name->after)) {
            av_source source = av_get_source(value);
            size_t length = strlen(name->after) + 3;
            char *quoted = xmalloc(length);
            snprintf(quoted, length, "\"%s\"", name->after);
            syntax_edit(edits, source.start, source.end, quoted);
            free(quoted);
        }
        return;
    }
    for (size_t i = 0; i < av_obj_size(value); i++) {
        const av_value *key = av_obj_key_at(value, i),
                       *child = av_obj_value_at(value, i);
        const char *field = av_get_str(key);
        av_source source = av_get_source(child);
        const cleanup_asset_name *name = components
                                           ? cleanup_asset_find(names, field, "component", NULL)
                                       : owner
                                           ? cleanup_asset_find(names, field, "field", owner)
                                           : NULL;
        if (name && strcmp(name->before, name->after)) {
            syntax_edit(edits, source.name_start, source.name_end, name->after);
        }
        const char *child_reference = !strcmp(field, "behaviors") ? "behavior"
                                    : owner && !strcmp(owner, "EngineSystem") &&
                                              !strcmp(field, "asset")
                                        ? "system"
                                        : NULL;
        bool child_components = !strcmp(field, "components") ||
                                !strcmp(field, "retained_components") ||
                                !strcmp(field, "detached_components") ||
                                !strcmp(field, "component_overrides");
        cleanup_asset_edits(child, names, child_reference, child_components, components ? field : NULL, edits);
    }
    for (size_t i = 0; i < av_arr_size(value); i++) {
        cleanup_asset_edits(av_arr_get(value, i), names, reference, false, owner, edits);
    }
}

static char **migrate_syntax(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    cleanup_asset_name *names = NULL;
    for (int i = 0; i < count && !error[0]; i++) {
        if (has_suffix(paths[i], ".chibi") &&
            !cleanup_asset_names(sources[i], &names, error, size)) {
            break;
        }
    }
    for (int i = 0; i < count && !error[0]; i++) {
        if (!has_suffix(paths[i], ".chibi")) {
            continue;
        }
        if (migrate_syntax_spelling(sources[i], &migrated[i], error, size)) {
            char *detail = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], detail);
            free(detail);
            break;
        }
    }
    for (int i = 0; i < count && !error[0]; i++) {
        if (!has_suffix(paths[i], DECLARATIVE_ASSET_EXTENSION)) {
            continue;
        }
        av_doc *document = asset_text_read(sources[i], error, size);
        if (!document) {
            break;
        }
        replacement *edits = NULL;
        cleanup_asset_edits(av_doc_root(document), names, NULL, false, NULL, &edits);
        migrated[i] = prefix_apply(sources[i], edits);
        prefix_edits_free(edits);
        av_doc_free(document);
    }
    const char **inputs = xcalloc((size_t)count, sizeof *inputs);
    for (int i = 0; i < count; i++) {
        if (has_suffix(paths[i], ".chibi")) {
            inputs[i] = migrated[i] ? migrated[i] : sources[i];
        }
    }
    text_edit **references = NULL;
    if (!error[0] &&
        chibi_migrate26_references(inputs, count, &references, error, size)) {
        for (int i = 0; i < count; i++) {
            if (!inputs[i] || !acount(references[i])) {
                continue;
            }
            char *after = text_edits_apply(inputs[i], references[i], acount(references[i]));
            if (!strcmp(after, inputs[i])) {
                free(after);
                continue;
            }
            free(migrated[i]);
            migrated[i] = after;
        }
    }
    if (references) {
        chibi_migrate26_edits_free(references, count);
    }
    free(inputs);
    for (int i = 0; i < acount(names); i++) {
        free(names[i].before);
        free(names[i].after);
        free(names[i].owner);
    }
    afree(names);
    return migrated;
}

static char **migrate_retention(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (!has_suffix(paths[i], ".chibi")) {
            continue;
        }
        chibi_migrate_retention(sources[i], &migrated[i], error, size);
    }
    return migrated;
}

static char **migrate_declarations(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (!has_suffix(paths[i], ".chibi")) {
            continue;
        }
        if (chibi_migrate_declarations(sources[i], &migrated[i], error, size)) {
            char *detail = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], detail);
            free(detail);
        }
    }
    return migrated;
}

/* Publishes a step's result, or NULL for unchanged source, only when the
 * current grammar parses it. */
/* Accepts a step's result when the grammar of the version it produces
 * parses it: 27 for the conversion step, 28 for the entry step, the current
 * grammar after that. */
static int accept_migrated(
    const char *source,
    char *result,
    const char *step,
    int version,
    char **migrated,
    char *error,
    size_t size
)
{
    const char *text = result ? result : source;
    script_syntax *check = chibi_migrate_parse(text, version);
    if (check->diagnostic_count) {
        const script_diagnostic *diagnostic = &check->diagnostics[0];
        snprintf(
            error,
            size,
            "%s migration produced invalid source at %d:%d: %s",
            step,
            diagnostic->range.line,
            diagnostic->range.column,
            diagnostic->message
        );
        script_syntax_free(check);
        free(result);
        return -1;
    }
    script_syntax_free(check);
    *migrated = result;
    return 0;
}
/* The T(value) callee for a version 26 cast type, or NULL when the type has
 * no name spelling, such as an array. */
static char *conversion_callee(const char *text, TSNode type)
{
    int from = (int)ts_node_start_byte(type), to = (int)ts_node_end_byte(type);
    if (to - from == (int)strlen("handle<ecs.entity>") &&
        !strncmp(text + from, "handle<ecs.entity>", (size_t)(to - from))) {
        return xstrdup("entity");
    }
    for (int i = from; i < to; i++) {
        if (!isalnum((unsigned char)text[i]) && text[i] != '_' && text[i] != '.') {
            return NULL;
        }
    }
    char *callee = xmalloc((size_t)(to - from) + 1);
    memcpy(callee, text + from, (size_t)(to - from));
    callee[to - from] = '\0';
    return callee;
}
/* The first anonymous child token spelled `token`. */
static TSNode child_token(TSNode node, const char *token)
{
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        if (!ts_node_is_named(child) && is(child, token)) {
            return child;
        }
    }
    return (TSNode){ 0 };
}
static void push_edit(replacement **edits, int start, int end, char *text)
{
    apush(*edits, ((replacement){ .start = start, .end = end, .text = text }));
}
/* `cast<T>(v)` becomes `T(v)`; a constant `N :: cast<T>(v)` becomes `N: T : v`.
 * Edits replace only the tokens around each value, so comments survive and
 * nested casts compose. */
static void conversion_edits(const char *text, TSNode node, replacement **edits, TSNode *unsupported)
{
    TSNode value = ts_node_child_by_field_name(node, "value", 5);
    if (is(node, "constant_declaration") && is(value, "cast_expression")) {
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        TSNode declare = child_token(node, "::");
        TSNode type = ts_node_child_by_field_name(value, "type", 4);
        TSNode open = child_token(value, "(");
        int from = (int)ts_node_start_byte(type), to = (int)ts_node_end_byte(type);
        int gap = (int)ts_node_end_byte(name);
        while (gap < (int)ts_node_start_byte(declare) &&
               isspace((unsigned char)text[gap])) {
            gap++;
        }
        size_t size = (size_t)(to - from) + sizeof ":  :";
        char *declared = xmalloc(size);
        snprintf(declared, size, ": %.*s :", to - from, text + from);
        /* Whitespace before `::` goes; a comment there stays. */
        int start = gap == (int)ts_node_start_byte(declare)
                      ? (int)ts_node_end_byte(name)
                      : (int)ts_node_start_byte(declare);
        push_edit(edits, start, (int)ts_node_end_byte(declare), declared);
        push_edit(edits, (int)ts_node_start_byte(value), (int)ts_node_end_byte(open), xstrdup(""));
        int close = (int)ts_node_end_byte(value);
        push_edit(edits, close - 1, close, xstrdup(""));
        conversion_edits(text, ts_node_child_by_field_name(value, "value", 5), edits, unsupported);
        return;
    }
    if (is(node, "cast_expression")) {
        char *callee = conversion_callee(text, ts_node_child_by_field_name(node, "type", 4));
        if (!callee && ts_node_is_null(*unsupported)) {
            *unsupported = node;
        }
        if (callee) {
            size_t size = strlen(callee) + 2;
            char *call = xmalloc(size);
            snprintf(call, size, "%s(", callee);
            free(callee);
            push_edit(
                edits,
                (int)ts_node_start_byte(node),
                (int)ts_node_end_byte(child_token(node, "(")),
                call
            );
        }
        conversion_edits(text, value, edits, unsupported);
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        conversion_edits(text, ts_node_child(node, i), edits, unsupported);
    }
}
int chibi_migrate_conversions(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = parse26(source);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        /* A rerun after an interrupted step finds sources already converted,
         * which the version 27 grammar reads at version 27 or later. */
        syntax = parse27(source);
        bool valid = !syntax->diagnostic_count;
        script_syntax_free(syntax);
        if (valid) {
            return 0;
        }
        snprintf(error, size, "Cannot migrate invalid version 26 source");
        return -1;
    }
    replacement *edits = NULL;
    TSNode unsupported = { 0 };
    conversion_edits(source, ts_tree_root_node(syntax->tree), &edits, &unsupported);
    bool spelled = ts_node_is_null(unsupported);
    script_range range = spelled ? (script_range){ 0 }
                                 : script_node_range(unsupported);
    script_syntax_free(syntax);
    if (!spelled) {
        snprintf(
            error,
            size,
            "cast at %d:%d converts to a type with no T(value) spelling; "
            "declare a typed local or constant",
            range.line,
            range.column
        );
        prefix_edits_free(edits);
        return -1;
    }
    char *result = prefix_apply(source, edits);
    prefix_edits_free(edits);
    return accept_migrated(source, result, "Conversion", 27, migrated, error, size);
}
static char **migrate_conversions(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (!has_suffix(paths[i], ".chibi")) {
            continue;
        }
        if (chibi_migrate_conversions(sources[i], &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    return migrated;
}

/* The callable a node is inside, or a null node. */
static TSNode enclosing_callable(TSNode node)
{
    for (TSNode up = ts_node_parent(node); !ts_node_is_null(up);
         up = ts_node_parent(up)) {
        if (is(up, "function_declaration") || is(up, "macro_declaration")) {
            return up;
        }
    }
    return (TSNode){ 0 };
}
/* Whether two nodes are spelled alike. */
static bool spelled_alike(const char *text, TSNode a, TSNode b)
{
    if (ts_node_is_null(a) || ts_node_is_null(b)) {
        return false;
    }
    size_t length = ts_node_end_byte(a) - ts_node_start_byte(a);
    return length == ts_node_end_byte(b) - ts_node_start_byte(b) &&
           !strncmp(text + ts_node_start_byte(a), text + ts_node_start_byte(b), length);
}
/* Whether node itself binds a name spelled like name: a local or constant, a
 * handler parameter or alias, or a loop name or key. */
static bool binds_here(const char *text, TSNode node, TSNode name)
{
    if (is(node, "handler")) {
        return spelled_alike(text, ts_node_child_by_field_name(node, "alias", 5), name);
    }
    if (is(node, "for_statement")) {
        return spelled_alike(text, ts_node_child_by_field_name(node, "name", 4), name) ||
               spelled_alike(text, ts_node_child_by_field_name(node, "key", 3), name);
    }
    bool named = is(node, "local_declaration") ||
                 is(node, "constant_declaration") || is(node, "parameter");
    return named &&
           spelled_alike(text, ts_node_child_by_field_name(node, "name", 4), name);
}
/* Whether node binds a name spelled like name, outside the callables nested
 * in it. Such a binding would capture a rewritten read of the parameter. */
static bool binds_name(const char *text, TSNode node, TSNode name)
{
    if (binds_here(text, node, name)) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (is(child, "function_declaration") || is(child, "macro_declaration")) {
            continue;
        }
        if ((is(node, "pattern_bindings") && spelled_alike(text, child, name)) ||
            binds_name(text, child, name)) {
            return true;
        }
    }
    return false;
}
/* The parameter in parameters spelled like name, or a null node. */
static TSNode named_parameter(const char *text, TSNode parameters, TSNode name)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(parameters); i++) {
        TSNode parameter = ts_node_child_by_field_name(ts_node_named_child(parameters, i), "name", 4);
        if (spelled_alike(text, parameter, name)) {
            return parameter;
        }
    }
    return (TSNode){ 0 };
}
typedef struct
{
    replacement *edits;
    TSNode unnamed; /* an entry naming no parameter of its callable */
    TSNode shadowed; /* an entry whose parameter a binding in the callable hides */
} entry_rewrite;
/* Rewrites one read through entry: `entry.X` of a parameter X loses its
 * prefix, and otherwise entry names the callable's only parameter, so
 * `entry.x` becomes `p.x` and a bare `entry` becomes `p`. */
static void entry_read(const char *text, TSNode node, entry_rewrite *out)
{
    TSNode parent = ts_node_parent(node);
    TSNode callable = enclosing_callable(node);
    TSNode parameters = ts_node_is_null(callable)
                          ? (TSNode){ 0 }
                          : ts_node_child_by_field_name(callable, "parameters", 10);
    TSNode object = ts_node_child_by_field_name(parent, "object", 6);
    TSNode member = is(parent, "member_expression") && ts_node_eq(object, node)
                      ? ts_node_child_by_field_name(parent, "member", 6)
                      : (TSNode){ 0 };
    TSNode parameter = ts_node_is_null(member)
                         ? (TSNode){ 0 }
                         : named_parameter(text, parameters, member);
    bool only = !ts_node_is_null(parameters) &&
                ts_node_named_child_count(parameters) == 1;
    if (ts_node_is_null(parameter) && !only) {
        if (ts_node_is_null(out->unnamed)) {
            out->unnamed = node;
        }
        return;
    }
    if (ts_node_is_null(parameter)) {
        parameter = ts_node_child_by_field_name(ts_node_named_child(parameters, 0), "name", 4);
    }
    if (binds_name(text, ts_node_child_by_field_name(callable, "body", 4), parameter) &&
        ts_node_is_null(out->shadowed)) {
        out->shadowed = node;
    }
    if (!ts_node_is_null(member) && spelled_alike(text, member, parameter)) {
        push_edit(
            &out->edits,
            (int)ts_node_start_byte(node),
            (int)ts_node_start_byte(member),
            xstrdup("")
        );
        return;
    }
    int from = (int)ts_node_start_byte(parameter),
        to = (int)ts_node_end_byte(parameter);
    char *spelling = xmalloc((size_t)(to - from) + 1);
    memcpy(spelling, text + from, (size_t)(to - from));
    spelling[to - from] = '\0';
    push_edit(&out->edits, (int)ts_node_start_byte(node), (int)ts_node_end_byte(node), spelling);
}
/* Every read through entry is rewritten; `after entry` names a handler, not a
 * value, and stays. */
static void entry_edits(const char *text, TSNode node, entry_rewrite *out)
{
    if (is(node, "builtin_reference") &&
        ts_node_end_byte(node) - ts_node_start_byte(node) == 5 &&
        !strncmp(text + ts_node_start_byte(node), "entry", 5)) {
        if (!is(ts_node_parent(node), "handler")) {
            entry_read(text, node, out);
        }
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        entry_edits(text, ts_node_child(node, i), out);
    }
}
int chibi_migrate_entry(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = parse27(source);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        /* A rerun after an interrupted step finds sources already migrated,
         * which the version 28 grammar reads at version 28 or later. */
        syntax = parse28(source);
        bool valid = !syntax->diagnostic_count;
        script_syntax_free(syntax);
        if (valid) {
            return 0;
        }
        snprintf(error, size, "Cannot migrate invalid version 27 source");
        return -1;
    }
    entry_rewrite rewrite = { 0 };
    entry_edits(source, ts_tree_root_node(syntax->tree), &rewrite);
    bool named = ts_node_is_null(rewrite.unnamed),
         clear = ts_node_is_null(rewrite.shadowed);
    if (!named || !clear) {
        script_range range = script_node_range(
            named ? rewrite.shadowed : rewrite.unnamed
        );
        snprintf(
            error,
            size,
            "entry at %d:%d %s",
            range.line,
            range.column,
            !named ? "names no parameter of its callable; name the parameter" : "reads a parameter that a binding of the same name hides; rename the binding"
        );
        script_syntax_free(syntax);
        prefix_edits_free(rewrite.edits);
        return -1;
    }
    script_syntax_free(syntax);
    if (!acount(rewrite.edits)) {
        return 0;
    }
    char *result = prefix_apply(source, rewrite.edits);
    prefix_edits_free(rewrite.edits);
    return accept_migrated(source, result, "Entry", 28, migrated, error, size);
}
static char **migrate_entry(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (!has_suffix(paths[i], ".chibi")) {
            continue;
        }
        if (chibi_migrate_entry(sources[i], &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    return migrated;
}
/* Whether node is an `after entry` handler. */
static bool after_entry(const char *text, TSNode node)
{
    TSNode kind = ts_node_child_by_field_name(node, "kind", 4);
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    return is(node, "handler") && !ts_node_is_null(kind) &&
           ts_node_end_byte(kind) - ts_node_start_byte(kind) == 5 &&
           !strncmp(text + ts_node_start_byte(kind), "after", 5) &&
           is(name, "builtin_reference") &&
           ts_node_end_byte(name) - ts_node_start_byte(name) == 5 &&
           !strncmp(text + ts_node_start_byte(name), "entry", 5);
}
/* Whether node reads the handler payload through `event`, outside the
 * callables and handlers nested in it. */
static bool reads_event(const char *text, TSNode node)
{
    if (is(node, "builtin_reference") &&
        ts_node_end_byte(node) - ts_node_start_byte(node) == 5 &&
        !strncmp(text + ts_node_start_byte(node), "event", 5)) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        bool nested = is(child, "function_declaration") ||
                      is(child, "macro_declaration") || is(child, "handler");
        if (!nested && reads_event(text, child)) {
            return true;
        }
    }
    return false;
}
/* Whether node, outside the callables nested in it, holds an identifier
 * spelled like name. */
static bool mentions(const char *text, TSNode node, TSNode name)
{
    if (is(node, "identifier")) {
        return spelled_alike(text, node, name);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        bool nested = is(child, "function_declaration") ||
                      is(child, "macro_declaration");
        if (!nested && mentions(text, child, name)) {
            return true;
        }
    }
    return false;
}
/* The variable an assignment target or call target starts from. */
static TSNode expression_root(TSNode node)
{
    while (is(node, "member_expression") || is(node, "index_expression")) {
        node = ts_node_child_by_field_name(node, "object", 6);
    }
    return node;
}
/* The name of the library, behavior or system that holds node, or a null
 * node. An unqualified call names a callable of its own container. */
static TSNode container_name(TSNode node)
{
    for (TSNode at = ts_node_parent(node); !ts_node_is_null(at);
         at = ts_node_parent(at)) {
        if (is(at, "library") || is(at, "behavior") || is(at, "system")) {
            return ts_node_child_by_field_name(at, "name", 4);
        }
    }
    return (TSNode){ 0 };
}
/* A callable's identity across a project: "Container.Name". */
static char *callable_key(const char *text, TSNode container, TSNode name)
{
    char *outer = ts_node_is_null(container) ? xstrdup("")
                                             : name_text(text, container);
    char *inner = name_text(text, name);
    size_t size = strlen(outer) + strlen(inner) + 2;
    char *key = xmalloc(size);
    snprintf(key, size, "%s.%s", outer, inner);
    free(outer);
    free(inner);
    return key;
}
static bool declares_reference(const char *text, TSNode parameters)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(parameters); i++) {
        TSNode type = ts_node_child_by_field_name(ts_node_named_child(parameters, i), "type", 4);
        if (ts_node_is_null(type)) {
            continue;
        }
        const char *spelling = text + ts_node_start_byte(type);
        if (!strncmp(spelling, "ref", 3) &&
            !isalnum((unsigned char)spelling[3]) && spelling[3] != '_') {
            return true;
        }
    }
    return false;
}
/* Appends the identities of the callables in node that take a parameter by
 * reference. Natives take copies, so only calls to these can change a
 * variable passed to them. */
static void collect_reference_callables(const char *text, TSNode node, char ***keys)
{
    if ((is(node, "function_declaration") || is(node, "macro_declaration")) &&
        declares_reference(text, ts_node_child_by_field_name(node, "parameters", 10))) {
        apush(*keys, callable_key(text, container_name(node), ts_node_child_by_field_name(node, "name", 4)));
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        collect_reference_callables(text, ts_node_named_child(node, i), keys);
    }
}
static void collect_source_references(const char *source, char ***keys)
{
    script_syntax *syntax = parse28(source);
    collect_reference_callables(source, ts_tree_root_node(syntax->tree), keys);
    script_syntax_free(syntax);
}
static void reference_callables_free(char **keys)
{
    for (int i = 0; i < acount(keys); i++) {
        free(keys[i]);
    }
    afree(keys);
}
/* Whether call may take an argument by reference: `Library.Name(...)` names
 * Library's callable, and `Name(...)` one in the call's own container. */
static bool takes_reference(const char *text, TSNode call, char **references)
{
    TSNode function = ts_node_child_by_field_name(call, "function", 8);
    TSNode library = ts_node_child_by_field_name(function, "object", 6);
    bool qualified = is(function, "member_expression") && is(library, "identifier");
    if (!qualified && !is(function, "identifier")) {
        return false;
    }
    char *key = qualified
                  ? callable_key(text, library, ts_node_child_by_field_name(function, "member", 6))
                  : callable_key(text, container_name(call), function);
    bool found = false;
    for (int i = 0; !found && i < acount(references); i++) {
        found = !strcmp(references[i], key);
    }
    free(key);
    return found;
}
/* Whether node, outside nested callables and handlers, may change a variable
 * spelled like name: it assigns the variable, a field or an element of it,
 * or passes it to a callable that takes a parameter by reference. */
static bool may_change(const char *text, TSNode node, TSNode name, char **references)
{
    if ((is(node, "assignment") || is(node, "reduce_statement")) &&
        spelled_alike(text, expression_root(ts_node_child_by_field_name(node, "target", 6)), name)) {
        return true;
    }
    if (is(node, "call_expression") && takes_reference(text, node, references) &&
        mentions(text, ts_node_child_by_field_name(node, "arguments", 9), name)) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        bool nested = is(child, "function_declaration") ||
                      is(child, "macro_declaration") || is(child, "handler");
        if (!nested && may_change(text, child, name, references)) {
            return true;
        }
    }
    return false;
}
/* Whether a local or constant the handler declares at its top level is a
 * parameter's name or is spelled elsewhere in body. Moved out of the handler,
 * it would join the function's scope and clash with, or capture, that name. */
static bool declares_used_name(const char *text, TSNode handler, TSNode body, TSNode parameters)
{
    TSNode block = ts_node_child_by_field_name(handler, "body", 4);
    for (uint32_t i = 0; i < ts_node_named_child_count(block); i++) {
        TSNode declaration = ts_node_named_child(block, i);
        if (!is(declaration, "local_declaration") &&
            !is(declaration, "constant_declaration")) {
            continue;
        }
        TSNode name = ts_node_child_by_field_name(declaration, "name", 4);
        if (!ts_node_is_null(named_parameter(text, parameters, name))) {
            return true;
        }
        for (uint32_t j = 0; j < ts_node_named_child_count(body); j++) {
            TSNode other = ts_node_named_child(body, j);
            if (!ts_node_eq(other, handler) && mentions(text, other, name)) {
                return true;
            }
        }
    }
    return false;
}
/* Whether the handler reads a parameter that the function's own statements
 * may change. The handler read the argument as passed; moved, it would read
 * the changed value. */
static bool reads_changed_parameter(
    const char *text,
    TSNode handler,
    TSNode body,
    TSNode parameters,
    char **references
)
{
    TSNode block = ts_node_child_by_field_name(handler, "body", 4);
    for (uint32_t i = 0; i < ts_node_named_child_count(parameters); i++) {
        TSNode name = ts_node_child_by_field_name(ts_node_named_child(parameters, i), "name", 4);
        if (!mentions(text, block, name)) {
            continue;
        }
        for (uint32_t j = 0; j < ts_node_named_child_count(body); j++) {
            TSNode statement = ts_node_named_child(body, j);
            if (!is(statement, "handler") &&
                may_change(text, statement, name, references)) {
                return true;
            }
        }
    }
    return false;
}
/* The columns of spaces or tabs that begin the line holding offset. */
static int line_indent(const char *text, int offset, int *line_start)
{
    int start = offset;
    while (start > 0 && text[start - 1] != '\n') {
        start--;
    }
    int end = start;
    while (text[end] == ' ' || text[end] == '\t') {
        end++;
    }
    *line_start = start;
    return end - start;
}
typedef struct
{
    replacement *edits;
    char **references; /* Borrowed callables that take a parameter by reference. */
    char *moved;       /* The current function's moved statements. */
    TSNode refused;
    const char *reason;
} entry_body_rewrite;
static void move_literal(entry_body_rewrite *out, const char *text, int length)
{
    if (!length) {
        return;
    }
    int offset = acount(out->moved);
    asetlen(out->moved, offset + length);
    memcpy(out->moved + offset, text, (size_t)length);
}
/* Appends the statements of block, between its braces, to the moved text. A
 * block on one line moves trimmed; with lines, each line moves without the
 * indent its statements had beyond the handler's. */
static void move_statements(entry_body_rewrite *out, const char *text, TSNode block, bool inline_close)
{
    int from = (int)ts_node_start_byte(block) + 1,
        to = (int)ts_node_end_byte(block) - 1;
    while (from < to && (text[from] == ' ' || text[from] == '\t')) {
        from++;
    }
    while (to > from && isspace((unsigned char)text[to - 1])) {
        to--;
    }
    if (from == to) {
        return;
    }
    int line_start,
        indent = line_indent(text, (int)ts_node_start_byte(block), &line_start);
    if (inline_close || text[from] != '\n') {
        if (!inline_close) {
            move_literal(out, text + line_start, indent);
        }
        move_literal(out, text + from, to - from);
        move_literal(out, inline_close ? " " : "\n", 1);
        return;
    }
    int inner_start,
        inner = line_indent(text, (int)ts_node_start_byte(ts_node_named_child(block, 0)), &inner_start);
    for (int line = from + 1; line < to;) {
        int skip = 0;
        while (skip < inner - indent && line + skip < to &&
               (text[line + skip] == ' ' || text[line + skip] == '\t')) {
            skip++;
        }
        int end = line + skip;
        while (end < to && text[end] != '\n') {
            end++;
        }
        move_literal(out, text + line + skip, end - line - skip);
        move_literal(out, "\n", 1);
        line = end + 1;
    }
}
/* Why the statements of handler cannot move into body, or NULL. */
static const char *entry_refusal(const char *text, TSNode handler, TSNode body, TSNode parameters, char **references)
{
    if (!ts_node_is_null(ts_node_child_by_field_name(handler, "alias", 5)) ||
        !ts_node_is_null(ts_node_child_by_field_name(handler, "parameters", 10))) {
        return "names the payload of `after entry`; "
               "read the parameters by name";
    }
    if (reads_event(text, ts_node_child_by_field_name(handler, "body", 4))) {
        return "reads `event` inside `after entry`; "
               "read the parameters by name";
    }
    if (declares_used_name(text, handler, body, parameters)) {
        return "declares a name in `after entry` that "
               "the function also uses; rename it";
    }
    if (reads_changed_parameter(text, handler, body, parameters, references)) {
        return "reads a parameter in `after entry` that the "
               "function's statements assign or pass by "
               "reference; move the statements by hand";
    }
    return NULL;
}
/* Where the moved statements go: after the function's last statement, before
 * the handlers that follow it, or before the closing brace. */
static int entry_insertion(const char *text, TSNode body, int close_start)
{
    for (uint32_t i = ts_node_named_child_count(body); i-- > 0;) {
        TSNode child = ts_node_named_child(body, i);
        if (is(child, "handler") || is(child, "comment")) {
            continue;
        }
        /* An automatic terminator ends a statement after its newline and the
         * next line's indent. */
        int end = (int)ts_node_end_byte(child);
        while (text[end - 1] == ' ' || text[end - 1] == '\t') {
            end--;
        }
        if (text[end - 1] != '\n') {
            while (text[end] == ' ' || text[end] == '\t' || text[end] == ';') {
                end++;
            }
            if (text[end] != '\n') {
                return close_start;
            }
            end++;
        }
        return end < close_start ? end : close_start;
    }
    return close_start;
}
/* `after entry { B }` in a function runs B after the function's statements,
 * with the arguments as they were passed, so B's statements move after them,
 * before the handlers that follow. The refusals keep the moved statements
 * reading the same values and names. */
static void move_entry_bodies(const char *text, TSNode function, entry_body_rewrite *out)
{
    TSNode body = ts_node_child_by_field_name(function, "body", 4);
    assert(is(body, "callable_body"));
    bool any = false;
    for (uint32_t i = 0; i < ts_node_named_child_count(body); i++) {
        any |= after_entry(text, ts_node_named_child(body, i));
    }
    if (!any) {
        return;
    }
    TSNode parameters = ts_node_child_by_field_name(function, "parameters", 10);
    TSNode close = ts_node_child(body, ts_node_child_count(body) - 1);
    int close_start,
        close_indent = line_indent(text, (int)ts_node_start_byte(close), &close_start);
    bool inline_close = close_start + close_indent != (int)ts_node_start_byte(close);
    if (inline_close && ts_node_named_child_count(body) > 1 &&
        ts_node_is_null(out->refused)) {
        out->refused = function;
        out->reason = "is on one line with `after entry` and other statements; "
                      "put them on separate lines";
    }
    int at = inline_close ? (int)ts_node_start_byte(close)
                          : entry_insertion(text, body, close_start);
    int merged = -1;
    asetlen(out->moved, 0);
    for (uint32_t i = 0; i < ts_node_named_child_count(body); i++) {
        TSNode handler = ts_node_named_child(body, i);
        if (!after_entry(text, handler)) {
            continue;
        }
        const char *refusal = entry_refusal(text, handler, body, parameters, out->references);
        if (refusal && ts_node_is_null(out->refused)) {
            out->refused = handler;
            out->reason = refusal;
        }
        int start = (int)ts_node_start_byte(handler),
            end = (int)ts_node_end_byte(handler);
        int line_start, indent = line_indent(text, start, &line_start), after = end;
        while (text[after] == ' ' || text[after] == '\t') {
            after++;
        }
        bool own_line = line_start + indent == start && text[after] == '\n';
        int from = own_line ? line_start : start;
        /* Edits must not start at one offset, so a handler where the
         * statements go is replaced by them. */
        merged = from == at ? acount(out->edits) : merged;
        push_edit(&out->edits, from, own_line ? after + 1 : after, xstrdup(""));
        move_statements(out, text, ts_node_child_by_field_name(handler, "body", 4), inline_close);
    }
    apush(out->moved, '\0');
    if (merged < 0) {
        push_edit(&out->edits, at, at, xstrdup(out->moved));
        return;
    }
    free(out->edits[merged].text);
    out->edits[merged].text = xstrdup(out->moved);
}
static void after_entry_edits(const char *text, TSNode node, entry_body_rewrite *out)
{
    if (is(node, "function_declaration")) {
        move_entry_bodies(text, node, out);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        after_entry_edits(text, ts_node_named_child(node, i), out);
    }
}
/* Collects the edits that move the statements of every `after entry` in a
 * function; returns -1 with error set when one cannot move. */
static int after_entry_rewrite(const char *source, entry_body_rewrite *rewrite, char *error, size_t size)
{
    script_syntax *syntax = parse28(source);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        /* A rerun after an interrupted step finds sources already migrated,
         * which the version 29 grammar reads. */
        if (valid_at(source, 29)) {
            return 0;
        }
        snprintf(error, size, "Cannot migrate invalid version 28 source");
        return -1;
    }
    after_entry_edits(source, ts_tree_root_node(syntax->tree), rewrite);
    afree(rewrite->moved);
    rewrite->moved = NULL;
    if (!ts_node_is_null(rewrite->refused)) {
        script_range range = script_node_range(rewrite->refused);
        snprintf(
            error,
            size,
            "function at %d:%d %s",
            range.line,
            range.column,
            rewrite->reason
        );
        script_syntax_free(syntax);
        return -1;
    }
    script_syntax_free(syntax);
    return 0;
}
/* Moves the statements of each `after entry`; references are the callables
 * that take a parameter by reference. */
static int migrate_entry_bodies(const char *source, char **references, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    entry_body_rewrite rewrite = { .references = references };
    int status = after_entry_rewrite(source, &rewrite, error, size);
    if (!status && acount(rewrite.edits)) {
        char *result = prefix_apply(source, rewrite.edits);
        status = accept_migrated(source, result, "After entry", 29, migrated, error, size);
    }
    prefix_edits_free(rewrite.edits);
    return status;
}
int chibi_migrate_after_entry(const char *source, char **migrated, char *error, size_t size)
{
    char **references = NULL;
    collect_source_references(source, &references);
    int status = migrate_entry_bodies(source, references, migrated, error, size);
    reference_callables_free(references);
    return status;
}
static char **migrate_after_entry(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated),
         **references = NULL;
    for (int i = 0; i < count; i++) {
        if (has_suffix(paths[i], ".chibi")) {
            collect_source_references(sources[i], &references);
        }
    }
    for (int i = 0; i < count && !error[0]; i++) {
        if (!has_suffix(paths[i], ".chibi")) {
            continue;
        }
        if (migrate_entry_bodies(sources[i], references, &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    reference_callables_free(references);
    return migrated;
}

int chibi_migrate_handlers(
    const char *source,
    const char *const *declarations,
    int declaration_count,
    char **migrated,
    char *error,
    size_t size
)
{
    char *result = NULL;
    *migrated = NULL;
    if (chibi_migrate29_handlers(source, declarations, declaration_count, &result, error, size)) {
        return -1;
    }
    return result
             ? accept_migrated(source, result, "Handler", 30, migrated, error, size)
             : 0;
}
static char **migrate_handlers(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    const char **declarations = NULL;
    for (int i = 0; i < count; i++) {
        if (has_suffix(paths[i], DECLARATIVE_ASSET_EXTENSION)) {
            apush(declarations, sources[i]);
        }
    }
    for (int i = 0; i < count && !error[0]; i++) {
        if (has_suffix(paths[i], ".chibi") &&
            chibi_migrate_handlers(sources[i], declarations, acount(declarations), &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    afree(declarations);
    return migrated;
}

int chibi_migrate_step_globals(const char *source, char **migrated, char *error, size_t size)
{
    char *result = NULL;
    *migrated = NULL;
    if (chibi_migrate30_step_globals(source, &result, error, size)) {
        return -1;
    }
    return result ? accept_migrated(source, result, "Step", 31, migrated, error, size)
                  : 0;
}
static char **migrate_step_globals(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (has_suffix(paths[i], ".chibi") &&
            chibi_migrate_step_globals(sources[i], &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    return migrated;
}

int chibi_migrate_array_calls(const char *source, char **migrated, char *error, size_t size)
{
    char *result = NULL;
    *migrated = NULL;
    if (chibi_migrate31_array_calls(source, &result, error, size)) {
        return -1;
    }
    return result
             ? accept_migrated(source, result, "Array call", 32, migrated, error, size)
             : 0;
}
int chibi_migrate_pointers(const char *source, char **migrated, char *error, size_t size)
{
    char *result = NULL;
    *migrated = NULL;
    if (chibi_migrate36_pointers(source, &result, error, size)) {
        return -1;
    }
    return result
             ? accept_migrated(source, result, "Pointer", 37, migrated, error, size)
             : 0;
}
static char **migrate_pointers(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (has_suffix(paths[i], ".chibi") &&
            chibi_migrate_pointers(sources[i], &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    return migrated;
}
static char **migrate_array_calls(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (has_suffix(paths[i], ".chibi") &&
            chibi_migrate_array_calls(sources[i], &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    return migrated;
}

int chibi_migrate_angle_forms(const char *source, char **migrated, char *error, size_t size)
{
    char *result = NULL;
    *migrated = NULL;
    /* A rerun after an interrupted step finds sources already migrated, which
     * only the version 33 grammar reads. */
    if (!valid_at(source, 32) && valid_at(source, 33)) {
        return 0;
    }
    if (chibi_migrate32_angle_forms(source, &result, error, size)) {
        return -1;
    }
    return result
             ? accept_migrated(source, result, "Angle form", 33, migrated, error, size)
             : 0;
}
static char **migrate_angle_forms(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (has_suffix(paths[i], ".chibi") &&
            chibi_migrate_angle_forms(sources[i], &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    return migrated;
}

/* Version 34 spells Cute's vectors as scripts name them: CF_V2 is vec2 and
 * CF_V3 is vec3, and so are their make_, break_ and scalar_to_ functions. */
static const char *const VECTOR_NAMES[][2] = {
    { "CF_V2", "vec2" },
    { "CF_V3", "vec3" },
    { "make_cf_v2", "make_vec2" },
    { "make_cf_v3", "make_vec3" },
    { "break_cf_v2", "break_vec2" },
    { "break_cf_v3", "break_vec3" },
    { "scalar_to_cf_v2", "scalar_to_vec2" },
    { "scalar_to_cf_v3", "scalar_to_vec3" },
};
static const char *vector_name(const char *text, size_t length)
{
    for (size_t i = 0; i < sizeof VECTOR_NAMES / sizeof *VECTOR_NAMES; i++) {
        if (strlen(VECTOR_NAMES[i][0]) == length &&
            !memcmp(text, VECTOR_NAMES[i][0], length)) {
            return VECTOR_NAMES[i][1];
        }
    }
    return NULL;
}
static void vector_edits(const char *source, TSNode node, text_edit **edits)
{
    if (is(node, "identifier")) {
        uint32_t start = ts_node_start_byte(node);
        const char *name = vector_name(source + start, ts_node_end_byte(node) - start);
        if (name) {
            apush(*edits, ((text_edit){ (int)start, (int)ts_node_end_byte(node), name }));
        }
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        vector_edits(source, ts_node_child(node, i), edits);
    }
}
/* Renames the vectors where a KDL key names a type, such as an event
 * payload's `type_name "CF_V2"`. Other strings are authored data. */
static bool vector_types(av_value *value)
{
    bool changed = false;
    for (size_t i = 0; av_is_obj(value) && i < av_obj_size(value); i++) {
        const char *key = av_get_str(av_obj_key_at(value, i));
        av_value *item = av_obj_value_at(value, i);
        bool type = !strcmp(key, "type_name") || !strcmp(key, "split_type_name");
        const char *name = type && av_is_str(item)
                             ? vector_name(av_get_str(item), av_get_len(item))
                             : NULL;
        if (name) {
            av_set_str(item, name);
            changed = true;
        }
        changed |= vector_types(item);
    }
    for (size_t i = 0; av_is_arr(value) && i < av_arr_size(value); i++) {
        changed |= vector_types(av_arr_get(value, i));
    }
    return changed;
}
static char *vector_strings(const char *source)
{
    char error[128];
    av_doc *document = asset_text_read(source, error, sizeof error);
    av_value *root = document ? av_doc_root(document) : NULL;
    char *migrated = root && vector_types(root) ? asset_text_write(root) : NULL;
    av_doc_free(document);
    return migrated;
}
int chibi_migrate_vector_names(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = chibi_migrate_parse(source, 33);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        snprintf(error, size, "Cannot parse version 33 source");
        return -1;
    }
    text_edit *edits = NULL;
    vector_edits(source, ts_tree_root_node(syntax->tree), &edits);
    script_syntax_free(syntax);
    char *result = acount(edits) ? text_edits_apply(source, edits, acount(edits))
                                 : NULL;
    afree(edits);
    return result
             ? accept_migrated(source, result, "Vector name", 34, migrated, error, size)
             : 0;
}
static char **migrate_vector_names(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        if (!has_suffix(paths[i], ".chibi")) {
            migrated[i] = vector_strings(sources[i]);
        } else if (chibi_migrate_vector_names(sources[i], &migrated[i], error, size)) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    return migrated;
}

int chibi_migrate_number_names(const char *source, char **migrated, char *error, size_t size)
{
    char *result = NULL;
    *migrated = NULL;
    if (chibi_migrate34_number_names(source, &result, error, size)) {
        return -1;
    }
    return result
             ? accept_migrated(source, result, "Number name", 35, migrated, error, size)
             : 0;
}
/* This step owns graph/event shapes, not the data their defaults hold. Use
 * source spans so changing a type never rewrites an unrelated numeric value. */
#define NUMBER_DOCUMENT_DEPTH 64
#define NUMBER_DOCUMENT_VALUES 1000000

typedef struct
{
    const char *text;
    size_t length;
    text_edit *edits;
    char *error;
    size_t size;
    int values;
    bool failed;
} number_document;

enum
{
    NUMBER_VALUE,
    NUMBER_ARRAY,
    NUMBER_SET,
    NUMBER_MAP
};

static void number_error(number_document *s, const av_value *value, const char *message)
{
    if (s->failed) {
        return;
    }
    s->failed = true;
    size_t at = av_get_source(value).start;
    int line = 1, column = 1;
    for (size_t i = 0; i < s->length && i < at; i++) {
        if (s->text[i] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }
    snprintf(s->error, s->size, "line %d, column %d: %s", line, column, message);
}
static const char *number_string(const av_value *value)
{
    return av_is_str(value) && strlen(av_get_str(value)) == av_get_len(value)
             ? av_get_str(value)
             : NULL;
}
static const char *number_canonical(const char *name)
{
    const char *renamed = name ? chibi_migrate34_number_name(name) : NULL;
    return renamed ? renamed : name;
}
static bool number_primitive(const char *name)
{
    static const char *const names[] = { "bool",        "i8",     "u8",  "i16",
                                         "u16",         "int",    "u32", "i64",
                                         "u64",         "number", "f64", "name",
                                         "const char *" };
    for (size_t i = 0; name && i < sizeof names / sizeof *names; i++) {
        if (!strcmp(number_canonical(name), names[i])) {
            return true;
        }
    }
    return false;
}
static void number_type_field(number_document *s, av_value *value, const char *key)
{
    av_value *field = av_obj_get(value, key);
    const char *before = number_string(field),
               *after = before ? chibi_migrate34_number_name(before) : NULL;
    if (!after || s->failed) {
        return;
    }
    av_source span = av_get_source(field);
    if (span.start > span.end || span.end > s->length || span.end > INT_MAX) {
        number_error(s, field, "Numeric type metadata has no editable source span");
        return;
    }
    size_t length = strlen(after) + 3;
    char *quoted = xmalloc(length);
    snprintf(quoted, length, "\"%s\"", after);
    apush(s->edits, ((text_edit){ (int)span.start, (int)span.end, quoted }));
}
static int number_container(const av_value *value)
{
    static const char *const names[] = { "value", "array", "set", "map" };
    const char *name = number_string(value);
    for (int i = 0; name && i < 4; i++) {
        if (!strcmp(name, names[i])) {
            return i;
        }
    }
    return -1;
}
static bool number_budget(number_document *s, const av_value *value, int depth)
{
    if (s->failed) {
        return false;
    }
    if (depth >= NUMBER_DOCUMENT_DEPTH || ++s->values > NUMBER_DOCUMENT_VALUES) {
        number_error(s, value, "Collection metadata exceeds the migration traversal limit");
        return false;
    }
    return true;
}
static bool number_descriptor_valid(const av_value *shape)
{
    const av_value *name = av_obj_get(shape, "name"),
                   *id = av_obj_get(shape, "record_id"),
                   *layout = av_obj_get(shape, "record_layout");
    const char *kind = number_string(av_obj_get(shape, "storage")),
               *nominal = number_string(name);
    bool record = kind && !strcmp(kind, "struct");
    bool supported = number_primitive(kind) || record ||
                     (kind && (!strcmp(kind, "handle") ||
                               !strcmp(kind, "enum") || !strcmp(kind, "union")));
    return av_is_obj(shape) && supported &&
           number_container(av_obj_get(shape, "container")) >= 0 &&
           (!name || (nominal && *nominal)) &&
           av_obj_size(shape) == 2u + (name != NULL) + (id != NULL) + (layout != NULL) &&
           (!(id || layout) ||
            (record && id && layout && av_is_uint(id) && av_is_uint(layout)));
}
/* An omitted pin container inherits its node template. Read only possible
 * codec paths to detect needed edits; these shapes never establish ownership. */
static void number_collection_probe(number_document *, const av_value *, int, int);
static int number_descriptor_probe(number_document *s, const av_value *shape, int depth)
{
    if (!number_descriptor_valid(shape) || !number_budget(s, shape, depth)) {
        return -1;
    }
    const av_value *storage = av_obj_get(shape, "storage"),
                   *name = av_obj_get(shape, "name");
    const char *kind = number_string(storage), *nominal = number_string(name);
    if ((kind && chibi_migrate34_number_name(kind)) ||
        (number_primitive(kind) && nominal && chibi_migrate34_number_name(nominal))) {
        number_error(
            s,
            kind && chibi_migrate34_number_name(kind) ? storage : name,
            "Numeric collection metadata requires an explicit pin container"
        );
        return -1;
    }
    return number_container(av_obj_get(shape, "container"));
}
static void number_collection_probe(number_document *s, const av_value *value, int container, int depth)
{
    if (!av_is_obj(value) || container == NUMBER_VALUE ||
        !number_budget(s, value, depth)) {
        return;
    }
    if (container == NUMBER_ARRAY || container == NUMBER_SET) {
        const av_value *items = av_obj_get(value, "items");
        if (av_obj_size(value) != 2 || !av_is_arr(items)) {
            return;
        }
        int child = number_descriptor_probe(s, av_obj_get(value, "element_type"), depth + 1);
        for (size_t i = 0;
             !s->failed && child > NUMBER_VALUE && i < av_arr_size(items);
             i++) {
            number_collection_probe(s, av_arr_get(items, i), child, depth + 1);
        }
        return;
    }
    if (container == NUMBER_MAP) {
        const av_value *entries = av_obj_get(value, "entries");
        if (av_obj_size(value) != 3 || !av_is_arr(entries) ||
            !number_descriptor_valid(av_obj_get(value, "key_type")) ||
            !number_descriptor_valid(av_obj_get(value, "value_type"))) {
            return;
        }
        int key = number_descriptor_probe(s, av_obj_get(value, "key_type"), depth + 1),
            item = number_descriptor_probe(s, av_obj_get(value, "value_type"), depth + 1);
        for (size_t i = 0; !s->failed && i < av_arr_size(entries); i++) {
            const av_value *pair = av_arr_get(entries, i);
            number_collection_probe(s, av_arr_get(pair, 0), key, depth + 1);
            number_collection_probe(s, av_arr_get(pair, 1), item, depth + 1);
        }
    }
}
/* Validate only the descriptor this collection owns. Record identities and
 * nominal names are data; scalar payloads are never searched for type keys. */
static int number_descriptor(number_document *s, av_value *shape, const char *expected)
{
    av_value *storage = av_obj_get(shape, "storage"),
             *name = av_obj_get(shape, "name");
    const char *kind = number_string(storage), *nominal = number_string(name);
    int container = number_container(av_obj_get(shape, "container"));
    if (!number_descriptor_valid(shape)) {
        number_error(s, shape, "Invalid collection type descriptor");
        return -1;
    }
    if ((expected && strcmp(number_canonical(expected), number_canonical(kind))) ||
        (number_primitive(kind) && number_primitive(nominal) &&
         strcmp(number_canonical(kind), number_canonical(nominal)))) {
        number_error(s, storage, "Collection type descriptor differs from its declaration");
        return -1;
    }
    number_type_field(s, shape, "storage");
    if (number_primitive(kind)) {
        number_type_field(s, shape, "name");
    }
    return container;
}
static void number_collection(number_document *s, av_value *value, int container, const char *expected, int depth)
{
    if (container == NUMBER_VALUE || !number_budget(s, value, depth)) {
        return;
    }
    /* Graph defaults pass through serialize_read_array, whose outer storage
     * cannot represent the union-valued maps used by the collection codec. */
    if (container == NUMBER_MAP) {
        number_error(s, value, "Map collection defaults require a supported owning graph schema");
        return;
    }
    if (av_is_arr(value)) {
        return; /* Plain arrays contain scalar/record payloads, not descriptors. */
    }
    av_value *shape = av_obj_get(value, "element_type"),
             *items = av_obj_get(value, "items");
    if (!av_is_obj(value) || av_obj_size(value) != 2 || !shape || !av_is_arr(items)) {
        number_error(s, value, "Invalid nested array document");
        return;
    }
    int child = number_descriptor(s, shape, expected);
    if (child == NUMBER_VALUE) {
        number_error(s, shape, "Nested array descriptor must name a collection");
        return;
    }
    if (child == NUMBER_MAP) {
        number_error(s, shape, "Map collection defaults require a supported owning graph schema");
        return;
    }
    const char *storage = number_string(av_obj_get(shape, "storage"));
    for (size_t i = 0; !s->failed && child > NUMBER_VALUE && i < av_arr_size(items);
         i++) {
        number_collection(s, av_arr_get(items, i), child, storage, depth + 1);
    }
}
static void number_metadata(number_document *s, av_value *value, bool pin)
{
    number_type_field(s, value, "type");
    const char *storage = number_string(av_obj_get(value, "type")),
               *name = number_string(av_obj_get(value, "type_name"));
    /* A pin that omits its storage keeps the node's. A type_name spelling a
     * version 34 scalar still names that scalar: preflight refuses any
     * declaration of those names. */
    if (number_primitive(storage) ||
        (!storage && name && chibi_migrate34_number_name(name))) {
        number_type_field(s, value, "type_name");
    }
    av_value *def = av_obj_get(value, "default"),
             *container = av_obj_get(value, "container");
    if (!def) {
        return;
    }
    if (!container) {
        if (pin) {
            number_collection_probe(s, def, NUMBER_ARRAY, 0);
        }
        return; /* Omitted variable containers are explicitly scalar. */
    }
    if (!av_is_int(container) || av_get_sint(container) < NUMBER_VALUE ||
        av_get_sint(container) > NUMBER_MAP) {
        number_error(s, container, "Invalid graph default container");
        return;
    }
    number_collection(s, def, (int)av_get_sint(container), number_string(av_obj_get(value, "type")), 0);
}
static void number_pins(number_document *s, av_value *pins)
{
    for (size_t i = 0; !s->failed && av_is_arr(pins) && i < av_arr_size(pins); i++) {
        number_metadata(s, av_arr_get(pins, i), true);
    }
    for (size_t i = 0; !s->failed && av_is_obj(pins) && i < av_obj_size(pins); i++) {
        number_metadata(s, av_obj_value_at(pins, i), true);
    }
}
static void number_graph(number_document *s, av_value *graph)
{
    av_value *nodes = av_obj_get(graph, "nodes");
    for (size_t i = 0; !s->failed && av_is_arr(nodes) && i < av_arr_size(nodes); i++) {
        av_value *node = av_arr_get(nodes, i);
        number_pins(s, av_obj_get(node, "inputs"));
        number_pins(s, av_obj_get(node, "outputs"));
    }
}
int chibi_migrate_number_types(const char *asset, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    av_doc *document = asset_text_read(asset, error, size);
    if (!document) {
        return -1;
    }
    number_document s = { .text = asset, .length = strlen(asset), .error = error, .size = size };
    av_value *root = av_doc_root(document), *graphs = av_obj_get(root, "graphs");
    number_graph(&s, root);
    for (size_t i = 0; !s.failed && av_is_arr(graphs) && i < av_arr_size(graphs); i++) {
        number_graph(&s, av_arr_get(graphs, i));
    }
    const char *domain = number_string(av_obj_get(root, "domain"));
    if (av_is_arr(graphs) || (domain && !strcmp(domain, "event"))) {
        av_value *variables = av_obj_get(root, "variables");
        for (size_t i = 0;
             !s.failed && av_is_arr(variables) && i < av_arr_size(variables);
             i++) {
            number_metadata(&s, av_arr_get(variables, i), false);
        }
    }
    if (!s.failed && acount(s.edits)) {
        *migrated = text_edits_apply(asset, s.edits, acount(s.edits));
    }
    for (int i = 0; i < acount(s.edits); i++) {
        free((void *)s.edits[i].text);
    }
    afree(s.edits);
    av_doc_free(document);
    return s.failed ? -1 : 0;
}
static char **migrate_number_names(char **sources, char **paths, int count, char *error, size_t size)
{
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; i < count && !error[0]; i++) {
        int status = has_suffix(paths[i], ".chibi")
                       ? chibi_migrate_number_names(sources[i], &migrated[i], error, size)
                       : chibi_migrate_number_types(sources[i], &migrated[i], error, size);
        if (status) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    return migrated;
}

/* The declaration collector is shared with the task-call migration. Unlike
 * that older step, this step rejects ambiguous library matches and only exposes
 * declarations directly inside a library through its qualifier. */
static TSNode ref_call_field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? (TSNode){ 0 }
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static bool ref_qualifier_binding(const char *source, TSNode scope, TSNode name)
{
    if (ts_node_is_null(scope)) {
        return false;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(scope); i++) {
        TSNode node = ts_node_named_child(scope, i);
        if ((is(node, "parameter") || is(node, "local_declaration") ||
             is(node, "constant_declaration") || is(node, "state_declaration")) &&
            same_name(source, ref_call_field(node, "name"), source, name)) {
            return true;
        }
    }
    return false;
}
static bool ref_qualifier_shadowed(const char *source, TSNode target, TSNode name)
{
    for (TSNode scope = ts_node_parent(target); !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        if (binds_here(source, scope, name) ||
            ref_qualifier_binding(source, scope, name) ||
            ref_qualifier_binding(source, ref_call_field(scope, "parameters"), name)) {
            return true;
        }
    }
    return false;
}
static const function *ref_call_callee(const corpus *c, int file, TSNode target, bool *ambiguous)
{
    TSNode name = target, qualifier = { 0 };
    if (is(target, "member_expression")) {
        qualifier = ref_call_field(target, "object");
        name = ref_call_field(target, "member");
    }
    if (!is_name(name) || (!ts_node_is_null(qualifier) && !is_name(qualifier))) {
        return NULL;
    }
    const char *source = c->sources[file];
    bool local = ts_node_is_null(qualifier);
    if (!local && ref_qualifier_shadowed(source, target, qualifier)) {
        return NULL;
    }
    const function *found = NULL;
    bool found_exact = false;
    for (int i = 0; i < acount(c->functions); i++) {
        const function *f = &c->functions[i];
        const char *defined = c->sources[f->file];
        bool exact = same_name(source, name, defined, f->name);
        if (local) {
            if (f->file != file || !contains(f->scope, target) || !exact) {
                continue;
            }
            if (found && span(f->scope) > span(found->scope)) {
                continue;
            }
            if (found && span(f->scope) == span(found->scope)) {
                /* Actions occupy the value namespace and take precedence. */
                bool action = is(ts_node_parent(f->name), "action_declaration");
                bool previous_action = is(ts_node_parent(found->name), "action_declaration");
                if (action != previous_action) {
                    found = action ? f : found;
                    *ambiguous = false;
                    continue;
                }
                *ambiguous = true;
                continue;
            }
        } else {
            if (ts_node_is_null(f->library) ||
                !is(ts_node_parent(f->scope), "library") ||
                !similar_name(source, qualifier, defined, f->library) ||
                !similar_name(source, name, defined, f->name)) {
                continue;
            }
            exact &= same_name(source, qualifier, defined, f->library);
            if (found && found_exact && !exact) {
                continue;
            }
            if (found && found_exact == exact) {
                *ambiguous = true;
                continue;
            }
        }
        found = f;
        found_exact = exact;
        *ambiguous = false;
    }
    return found;
}
static bool ref_call_rewrite(const corpus *c, int file, TSNode node, text_edit **edits, char *error, size_t size)
{
    if (is(node, "call_expression")) {
        bool ambiguous = false;
        const function *f = ref_call_callee(c, file, ref_call_field(node, "function"), &ambiguous);
        if (ambiguous) {
            script_range range = script_node_range(node);
            snprintf(
                error,
                size,
                "Ambiguous callable at %d:%d; rename it before ref-call "
                "migration",
                range.line,
                range.column
            );
            return false;
        }
        TSNode declaration = f ? ts_node_parent(f->name) : (TSNode){ 0 };
        TSNode parameters = ref_call_field(declaration, "parameters");
        if (f && !is(declaration, "action_declaration")) {
            TSNode *params = NULL;
            for (uint32_t i = 0; !ts_node_is_null(parameters) &&
                                 i < ts_node_named_child_count(parameters);
                 i++) {
                TSNode p = ts_node_named_child(parameters, i);
                if (is(p, "parameter")) {
                    apush(params, p);
                }
            }
            TSNode arguments = ref_call_field(node, "arguments");
            int ordinal = 0;
            for (uint32_t i = 0; i < ts_node_named_child_count(arguments); i++) {
                TSNode arg = ts_node_named_child(arguments, i);
                if (ts_node_is_extra(arg)) {
                    continue;
                }
                TSNode label = ref_call_field(arg, "name");
                int parameter = ordinal++;
                if (!ts_node_is_null(label)) {
                    parameter = -1;
                    for (int j = 0; j < acount(params); j++) {
                        if (same_name(
                                c->sources[file],
                                label,
                                c->sources[f->file],
                                ref_call_field(params[j], "name")
                            )) {
                            parameter = j;
                            break;
                        }
                    }
                }
                if (parameter < 0 || parameter >= acount(params) ||
                    !ref_parameter(params[parameter])) {
                    continue;
                }
                TSNode value = ts_node_is_null(label)
                                 ? arg
                                 : ref_call_field(arg, "value");
                if (ts_node_is_null(value)) {
                    value = ref_call_field(arg, "source");
                }
                if (!ts_node_is_null(value) && !is(value, "reference_argument")) {
                    int start = (int)ts_node_start_byte(value);
                    apush(*edits, ((text_edit){ start, start, "ref " }));
                }
            }
            afree(params);
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (!ref_call_rewrite(c, file, ts_node_named_child(node, i), edits, error, size)) {
            return false;
        }
    }
    return true;
}
char **chibi_migrate_ref_calls(const char *const *sources, int count, char *error, size_t size)
{
    script_syntax **syntax = xcalloc((size_t)count, sizeof *syntax);
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    corpus c = { .sources = sources };
    bool valid = true;
    for (int i = 0; i < count && valid; i++) {
        syntax[i] = chibi_migrate_parse(sources[i], 35);
        if (syntax[i]->diagnostic_count) {
            snprintf(error, size, "Cannot migrate invalid version 35 source %d", i + 1);
            valid = false;
        } else {
            collect(ts_tree_root_node(syntax[i]->tree), i, (TSNode){ 0 }, true, &c.functions);
        }
    }
    for (int i = 0; i < count && valid; i++) {
        text_edit *edits = NULL;
        valid = ref_call_rewrite(&c, i, ts_tree_root_node(syntax[i]->tree), &edits, error, size);
        if (valid && acount(edits)) {
            char *result = text_edits_apply(sources[i], edits, acount(edits));
            valid = !accept_migrated(sources[i], result, "Ref call", 36, &migrated[i], error, size);
        }
        afree(edits);
    }
    for (int i = 0; i < count; i++) {
        if (syntax[i]) {
            script_syntax_free(syntax[i]);
        }
        if (!valid) {
            free(migrated[i]);
        }
    }
    free(syntax);
    afree(c.functions);
    if (!valid) {
        free(migrated);
        return NULL;
    }
    return migrated;
}

/* Removes the scripts later steps migrated past version `from + 1` from the
 * step's inputs; returns the remaining count. */
static int skip_migrated_ahead(char **paths, char **sources, int count, int from)
{
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (has_suffix(paths[i], ".chibi") && migrated_ahead(sources[i], from)) {
            free(paths[i]);
            free(sources[i]);
            continue;
        }
        paths[kept] = paths[i];
        sources[kept++] = sources[i];
    }
    asetlen(paths, kept);
    return kept;
}
int chibi_project_migrate_content(
    const char *content,
    const char *start_scene,
    int from,
    char *error,
    size_t error_size
)
{
    if (from == 19) {
        return migrate_components(content, error, error_size);
    }
    char **paths = NULL;
    /* Versions 15, 16, 18, 19, 22 and 35 change scenes and prototypes, and
     * version 21 changes materials, wherever the project keeps them; every
     * other step changes scripts. */
    bool declarative = from == 14 || from == 15 || from == 17 || from == 18 ||
                       from == 20 || from == 21 || from == 22 || from == 25 ||
                       from == 34;
    int status = collect_sources(
        content,
        declarative ? DECLARATIVE_ASSET_EXTENSION : ".chibi",
        &paths,
        error,
        error_size
    );
    /* Versions 16 and 18 also check scripts for names they remove, and
     * versions 23, 26 and 35 also rewrite scripts. */
    if (!status &&
        (from == 15 || from == 17 || from == 22 || from == 25 || from == 34)) {
        status = collect_sources(content, ".chibi", &paths, error, error_size);
    }
    /* Version 30 types handler payloads from the project's event declarations,
     * and version 34 renames vectors in them. */
    if (!status && (from == 29 || from == 33)) {
        status = collect_sources(content, DECLARATIVE_ASSET_EXTENSION, &paths, error, error_size);
    }
    /* The scan skips hidden entries, and the start scene may lie outside the
     * content, so add it unless the scan found it. */
    struct stat info;
    if (!status && declarative && start_scene && !collected(paths, start_scene) &&
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
    /* Include every declaration before discarding already-migrated sources:
     * a sibling can still bind a name this version is about to introduce. */
    for (int i = 0; !status && from == 34 && i < count; i++) {
        if (has_suffix(paths[i], ".chibi") &&
            chibi_migrate34_preflight(sources[i], error, error_size)) {
            char *message = xstrdup(error);
            snprintf(error, error_size, "%s: %s", paths[i], message);
            free(message);
            status = -1;
        }
    }
    /* Steps from version 23 on read scripts at their own version or the
     * next; they leave alone scripts that later steps migrated further. */
    if (!status && from >= 23) {
        count = skip_migrated_ahead(paths, sources, count, from);
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
            case 18:
                error[0] = '\0';
                migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
                for (int i = 0; i < count && !error[0]; i++) {
                    migrated[i] = undot_names(sources[i], paths[i], error, error_size);
                }
                status = error[0] ? -1 : 0;
                break;
            case 20:
                migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
                for (int i = 0; i < count; i++) {
                    migrated[i] = surface_outputs(sources[i]);
                }
                break;
            case 21:
                migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
                for (int i = 0; i < count; i++) {
                    migrated[i] = drop_lab_settings(sources[i]);
                }
                break;
            case 22:
                error[0] = '\0';
                migrated = migrate_prefixes(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 23:
                error[0] = '\0';
                migrated = migrate_declarations(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 24:
                error[0] = '\0';
                migrated = migrate_retention(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 25:
                error[0] = '\0';
                migrated = migrate_syntax(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 26:
                error[0] = '\0';
                migrated = migrate_conversions(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 27:
                error[0] = '\0';
                migrated = migrate_entry(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 28:
                error[0] = '\0';
                migrated = migrate_after_entry(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 29:
                error[0] = '\0';
                migrated = migrate_handlers(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 30:
                error[0] = '\0';
                migrated = migrate_step_globals(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 31:
                error[0] = '\0';
                migrated = migrate_array_calls(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 32:
                error[0] = '\0';
                migrated = migrate_angle_forms(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 33:
                error[0] = '\0';
                migrated = migrate_vector_names(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 34:
                error[0] = '\0';
                migrated = migrate_number_names(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            case 35:
                error[0] = '\0';
                migrated = chibi_migrate_ref_calls((const char *const *)sources, count, error, error_size);
                status = migrated ? 0 : -1;
                break;
            case 36:
                error[0] = '\0';
                migrated = migrate_pointers(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            default: assert(false);
        }
    }
    /* Every source is staged before any original is replaced, and a failed
     * step leaves the originals. Each step is idempotent, so a step
     * interrupted while replacing reruns cleanly, except version 31's: see
     * chibi_migrate31_array_calls. The manifest records each finished step,
     * so a later failure reruns none. */
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
