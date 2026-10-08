#include "authoring/source_minimize.h"

#include "support/array.h"
#include "support/text_edit.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MINIMIZE_ROUNDS = 8,
};

typedef enum
{
    PASS_SHAPE, /* Everything except labels; dropped arguments decide ordinals. */
    PASS_LABELS, /* Labels on arguments already in parameter order. */
} pass;

/* One candidate rewrite: its edits apply together. Texts are owned. */
typedef struct
{
    text_edit *edits;
} rewrite;

typedef struct
{
    const char *text;
    const script_analysis *analysis;
    const script_catalog *catalog;
    rewrite *rewrites;
} collector;

static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static int start(TSNode node)
{
    return (int)ts_node_start_byte(node);
}
static int end(TSNode node)
{
    return (int)ts_node_end_byte(node);
}

/* Equivalence ignores source positions, the spelling of payload aliases and
 * reference markers, which lower to the same reads. */
static bool same_expression(const script_analysis *, int, const script_analysis *, int);
static bool same_statement(const script_analysis *, int, const script_analysis *, int);

static bool payload(const script_symbol *s)
{
    return s->kind == SCRIPT_PARAMETER && s->type.storage == PIN_STRUCT &&
           (s->type.flags & SCRIPT_TYPE_RESULTS);
}
static bool same_type(script_type x, script_type y)
{
    return x.storage == y.storage && x.container == y.container &&
           x.nominal == y.nominal && x.parameter == y.parameter && x.flags == y.flags;
}
/* A reference marker on an operand propagates into derived value types
 * without changing how they lower. */
static bool same_value_type(script_type x, script_type y)
{
    x.flags &= ~SCRIPT_TYPE_REFERENCE;
    y.flags &= ~SCRIPT_TYPE_REFERENCE;
    return same_type(x, y);
}
static bool same_text(const char *x, const char *y)
{
    return !strcmp(x ? x : "", y ? y : "");
}
static bool same_symbol(const script_analysis *a, int i, const script_analysis *b, int j)
{
    if (i < 0 || j < 0) {
        return i == j;
    }
    const script_symbol *x = &a->symbols[i], *y = &b->symbols[j];
    /* Implicit payload parameters record their handler; declared ones rely on
     * the handler's parameter list, which is compared separately. */
    bool parameter = x->kind == SCRIPT_PARAMETER;
    return x->kind == y->kind && same_type(x->type, y->type) &&
           x->capability == y->capability &&
           (parameter || x->function == y->function) &&
           x->writable == y->writable && x->disabled == y->disabled &&
           x->outcome == y->outcome && x->lazy_local == y->lazy_local &&
           ((payload(x) && payload(y)) || same_text(x->name, y->name));
}
static bool same_arguments(
    const script_expression *x,
    const script_analysis *a,
    const script_expression *y,
    const script_analysis *b
)
{
    if (x->argument_count != y->argument_count) {
        return false;
    }
    bool *used = xcalloc((size_t)(y->argument_count ? y->argument_count : 1), sizeof *used);
    bool same = true;
    for (int i = 0; same && i < x->argument_count; i++) {
        const script_argument *left = &x->arguments[i];
        int match = -1;
        for (int j = 0; match < 0 && j < y->argument_count; j++) {
            const script_argument *right = &y->arguments[j];
            if (!used[j] && right->parameter == left->parameter &&
                right->member == left->member) {
                match = j;
            }
        }
        same = match >= 0 &&
               same_expression(a, left->value, b, y->arguments[match].value);
        if (same) {
            used[match] = true;
        }
    }
    free(used);
    return same;
}
/* Component paths record their root symbol only for navigation; the
 * receiver expression carries their meaning. */
static bool component_path(const script_expression *e)
{
    return e->kind == SCRIPT_EXPR_COMPONENT || e->kind == SCRIPT_EXPR_MEMBER;
}
static bool same_expression(const script_analysis *a, int i, const script_analysis *b, int j)
{
    if (i < 0 || j < 0) {
        return i == j;
    }
    const script_expression *x = &a->expressions[i], *y = &b->expressions[j];
    if (x->kind != y->kind || x->op != y->op ||
        !same_value_type(x->type, y->type) || x->capability != y->capability ||
        x->member != y->member || x->read != y->read || x->write != y->write ||
        x->instance != y->instance || x->entry != y->entry || x->offset != y->offset ||
        x->effects != y->effects || x->writable != y->writable ||
        x->awaited != y->awaited || x->constant_snapshot != y->constant_snapshot ||
        x->partial != y->partial || x->result_count != y->result_count ||
        (!component_path(x) && !same_symbol(a, x->symbol, b, y->symbol)) ||
        !same_expression(a, x->left, b, y->left)) {
        return false;
    }
    if (x->kind == SCRIPT_EXPR_CONSTANT &&
        !pin_value_equal(&x->constant, &y->constant)) {
        return false;
    }
    for (int r = 0; r < x->result_count; r++) {
        if (!same_value_type(x->results[r], y->results[r])) {
            return false;
        }
    }
    bool right = x->kind == SCRIPT_EXPR_CALL
                   ? x->right == y->right
                   : same_expression(a, x->right, b, y->right);
    return right && same_arguments(x, a, y, b);
}
/* An empty else block and an absent one run the same statements. */
static int alternative(const script_analysis *a, int i)
{
    if (i >= 0 && a->statements[i].kind == SCRIPT_STMT_BLOCK &&
        !a->statements[i].child_count) {
        return -1;
    }
    return i;
}
static bool same_statement(const script_analysis *a, int i, const script_analysis *b, int j)
{
    if (i < 0 || j < 0) {
        return i == j;
    }
    const script_statement *x = &a->statements[i], *y = &b->statements[j];
    if (x->kind != y->kind || x->op != y->op || x->effects != y->effects ||
        x->child_count != y->child_count || !same_symbol(a, x->symbol, b, y->symbol) ||
        !same_expression(a, x->expression, b, y->expression) ||
        !same_expression(a, x->second, b, y->second) ||
        !same_expression(a, x->third, b, y->third) ||
        !same_statement(a, x->body, b, y->body) ||
        !same_statement(a, alternative(a, x->alternative), b, alternative(b, y->alternative))) {
        return false;
    }
    for (int c = 0; c < x->child_count; c++) {
        if (!same_statement(a, x->children[c], b, y->children[c])) {
            return false;
        }
    }
    return true;
}
static bool same_symbols(
    const script_analysis *a,
    const int *x,
    const script_analysis *b,
    const int *y,
    int count
)
{
    for (int i = 0; i < count; i++) {
        if (!same_symbol(a, x[i], b, y[i])) {
            return false;
        }
    }
    return true;
}
static bool same_function(
    const script_analysis *a,
    const script_function *x,
    const script_analysis *b,
    const script_function *y
)
{
    return x->kind == y->kind && x->receiver == y->receiver &&
           x->parent == y->parent && x->parameter_count == y->parameter_count &&
           x->result_count == y->result_count &&
           x->type_parameter_count == y->type_parameter_count &&
           x->depth_limit == y->depth_limit && x->event == y->event &&
           x->action == y->action && same_text(x->entry, y->entry) &&
           x->effects == y->effects && x->pure == y->pure &&
           x->disabled == y->disabled && same_symbol(a, x->symbol, b, y->symbol) &&
           same_symbols(a, x->parameters, b, y->parameters, x->parameter_count) &&
           same_symbols(a, x->results, b, y->results, x->result_count) &&
           same_symbols(a, x->type_parameters, b, y->type_parameters, x->type_parameter_count) &&
           same_statement(a, x->body, b, y->body);
}
/* Declared symbols in order, without payload aggregates and their aliases. */
static int next_declared(const script_analysis *a, int i)
{
    while (i < a->symbol_count && payload(&a->symbols[i])) {
        i++;
    }
    return i;
}
static bool same_declarations(const script_analysis *a, const script_analysis *b)
{
    int i = next_declared(a, 0), j = next_declared(b, 0);
    for (; i < a->symbol_count && j < b->symbol_count;
         i = next_declared(a, i + 1), j = next_declared(b, j + 1)) {
        if (!same_symbol(a, i, b, j) ||
            !same_expression(a, a->symbols[i].initializer, b, b->symbols[j].initializer)) {
            return false;
        }
    }
    return i == a->symbol_count && j == b->symbol_count;
}
static bool same_query(const script_analysis *a, int i, const script_analysis *b, int j)
{
    if (i < 0 || j < 0) {
        return i == j;
    }
    const script_query *x = &a->queries[i], *y = &b->queries[j];
    if (x->kind != y->kind || x->component != y->component ||
        x->child_count != y->child_count) {
        return false;
    }
    for (int c = 0; c < x->child_count; c++) {
        if (!same_query(a, x->children[c], b, y->children[c])) {
            return false;
        }
    }
    return true;
}
static bool same_ids(const uint64_t *x, int n, const uint64_t *y, int m)
{
    return n == m && (!n || !memcmp(x, y, sizeof *x * (size_t)n));
}
static bool same_program(const script_analysis *a, const script_analysis *b)
{
    if (!a->valid || !b->valid || a->diagnostic_count || b->diagnostic_count ||
        a->kind != b->kind || a->owner_type != b->owner_type ||
        !same_text(a->name, b->name) || a->function_count != b->function_count ||
        a->binding_count != b->binding_count || !same_query(a, a->query, b, b->query) ||
        !same_symbol(a, a->owner_symbol, b, b->owner_symbol) ||
        !same_declarations(a, b)) {
        return false;
    }
    for (int i = 0; i < a->function_count; i++) {
        if (!same_function(a, &a->functions[i], b, &b->functions[i])) {
            return false;
        }
    }
    for (int i = 0; i < a->binding_count; i++) {
        const script_binding *x = &a->bindings[i], *y = &b->bindings[i];
        if (!same_expression(a, x->target, b, y->target) ||
            !same_expression(a, x->value, b, y->value) ||
            !same_ids(x->reads, x->read_count, y->reads, y->read_count) ||
            !same_ids(x->writes, x->write_count, y->writes, y->write_count)) {
            return false;
        }
    }
    return true;
}

static void edit(rewrite *r, int from, int to, const char *text)
{
    apush(r->edits, ((text_edit){ from, to, xstrdup(text) }));
}
static void add(collector *c, int from, int to, const char *text)
{
    rewrite r = { 0 };
    edit(&r, from, to, text);
    apush(c->rewrites, r);
}
static void rewrite_free(rewrite *r)
{
    for (int i = 0; i < acount(r->edits); i++) {
        free((char *)r->edits[i].text);
    }
    afree(r->edits);
}
static char *slice(const char *text, int from, int to)
{
    char *out = xmalloc((size_t)(to - from) + 1);
    memcpy(out, text + from, (size_t)(to - from));
    out[to - from] = 0;
    return out;
}
/* Replaces a node with the text of one of its parts. */
/* Replaces `node` with `part`, keeping a word before it apart: `return(x)`
 * becomes `return x`. */
static void unwrap_node(collector *c, TSNode node, TSNode part)
{
    int from = start(node);
    bool word = from > 0 && (isalnum((unsigned char)c->text[from - 1]) ||
                             c->text[from - 1] == '_');
    size_t size = (size_t)(end(part) - start(part)) + 2;
    char *inner = xmalloc(size);
    snprintf(inner, size, "%s%.*s", word ? " " : "", end(part) - start(part), c->text + start(part));
    add(c, from, end(node), inner);
    free(inner);
}
/* Removes a node with its line when nothing else shares that line. */
static void delete_line(collector *c, TSNode node)
{
    int from = start(node), to = end(node);
    while (from > 0 && (c->text[from - 1] == ' ' || c->text[from - 1] == '\t')) {
        from--;
    }
    while (c->text[to] == ' ' || c->text[to] == '\t') {
        to++;
    }
    if ((from == 0 || c->text[from - 1] == '\n') && c->text[to] == '\n') {
        to++;
    }
    add(c, from, to, "");
}
static bool literal(TSNode value)
{
    return is(value, "number") || is(value, "string") || is(value, "boolean") ||
           is(value, "nil") || is(value, "struct_expression") ||
           is(value, "array_expression") ||
           (is(value, "unary_expression") && is(field(value, "operand"), "number"));
}
static TSNode argument_value(TSNode argument)
{
    TSNode value = field(argument, "value");
    return ts_node_is_null(value) ? argument : value;
}
/* Arguments equal to their parameter's default; later ones keep labels. */
static void default_arguments(collector *c, TSNode list)
{
    uint32_t count = ts_node_named_child_count(list);
    TSNode *arguments = NULL;
    for (uint32_t i = 0; i < count; i++) {
        TSNode argument = ts_node_named_child(list, i);
        if (!ts_node_is_extra(argument)) {
            apush(arguments, argument);
        }
    }
    for (int i = 0; i < acount(arguments); i++) {
        if (!literal(argument_value(arguments[i])) ||
            !ts_node_is_null(field(arguments[i], "source"))) {
            continue;
        }
        int from = start(arguments[i]), to = end(arguments[i]);
        if (i + 1 < acount(arguments)) {
            to = start(arguments[i + 1]);
        } else if (i) {
            from = end(arguments[i - 1]);
        }
        add(c, from, to, "");
    }
    afree(arguments);
}
static void labels(collector *c, TSNode list)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(list); i++) {
        TSNode argument = ts_node_named_child(list, i);
        TSNode name = field(argument, "name");
        if (is(argument, "named_argument") && ts_node_named_child_count(name) == 1) {
            add(c, start(argument), start(field(argument, "value")), "");
        }
    }
}
static int symbol_declared_at(const script_analysis *a, TSNode node)
{
    for (int i = 0; i < a->symbol_count; i++) {
        if (a->symbols[i].declaration.start == start(node) &&
            a->symbols[i].declaration.end == end(node)) {
            return i;
        }
    }
    return -1;
}
/* `on Event as alias` reads the payload as `event`. */
static void handler_alias(collector *c, TSNode handler)
{
    TSNode name = field(handler, "name"), alias = field(handler, "alias");
    int symbol = symbol_declared_at(c->analysis, alias);
    if (symbol < 0) {
        return;
    }
    rewrite r = { 0 };
    edit(&r, end(name), end(alias), "");
    for (int i = 0; i < c->analysis->use_count; i++) {
        const script_use *use = &c->analysis->uses[i];
        if (use->symbol == symbol && use->range.start != start(alias)) {
            edit(&r, use->range.start, use->range.end, "event");
        }
    }
    apush(c->rewrites, r);
}
static void handler(collector *c, TSNode node)
{
    TSNode parameters = field(node, "parameters");
    if (!ts_node_is_null(field(node, "alias"))) {
        handler_alias(c, node);
    }
    if (ts_node_is_null(parameters)) {
        return;
    }
    TSNode before = field(node, "alias");
    if (ts_node_is_null(before)) {
        before = field(node, "name");
    }
    add(c, end(before), end(parameters), "");
    /* Payload parameters take their registered types and defaults. */
    for (uint32_t i = 0; i < ts_node_named_child_count(parameters); i++) {
        TSNode parameter = ts_node_named_child(parameters, i);
        TSNode name = field(parameter, "name");
        if (!is(parameter, "parameter") ||
            !ts_node_is_null(field(parameter, "source"))) {
            continue;
        }
        if (start(name) > start(parameter)) {
            unwrap_node(c, parameter, name);
        } else if (end(name) < end(parameter)) {
            add(c, end(name), end(parameter), "");
        }
    }
}
/* `name: type = value` and `type name = value` become `name := value`. */
static void inferred_declaration(collector *c, TSNode node)
{
    TSNode name = field(node, "name"), type = field(node, "type"),
           value = field(node, "value");
    if (ts_node_is_null(type) || ts_node_is_null(value)) {
        return;
    }
    if (start(type) > start(name)) {
        add(c, end(name), start(value), " := ");
        return;
    }
    char *spelled = slice(c->text, start(name), end(name));
    size_t size = strlen(spelled) + sizeof " := ";
    char *replacement = xmalloc(size);
    snprintf(replacement, size, "%s := ", spelled);
    add(c, start(type), start(value), replacement);
    free(replacement);
    free(spelled);
}
static char *string_argument(const collector *c, TSNode list, uint32_t index)
{
    TSNode value = ts_node_named_child(list, index);
    if (!is(value, "string") || end(value) - start(value) < 2) {
        return NULL;
    }
    return slice(c->text, start(value) + 1, end(value) - 1);
}
/* Reflected source spelling: `GameHealth` is `Health`, `cooldown_remaining`
 * is `CooldownRemaining`. */
static void append_pascal(char **out, const char *name)
{
    bool upper = true;
    for (const char *p = name; *p; p++) {
        if (*p == '_') {
            upper = true;
            continue;
        }
        apush(*out, upper ? (char)toupper((unsigned char)*p) : *p);
        upper = false;
    }
}
/* `component("GAME.GameHealth", "current")` reads `self.Health.Current`. */
static char *component_path_text(const collector *c, TSNode call)
{
    TSNode function = field(call, "function"), list = field(call, "arguments");
    uint32_t count = ts_node_named_child_count(list);
    if (!is(function, "identifier") || end(function) - start(function) != 9 ||
        strncmp(c->text + start(function), "component", 9) || count < 1 || count > 2) {
        return NULL;
    }
    char *key = string_argument(c, list, 0),
         *member = count == 2 ? string_argument(c, list, 1) : NULL;
    int found = key ? script_catalog_find_type(c->catalog, key) : -1;
    char *out = NULL;
    if (found >= 0 && (count == 1 || member)) {
        const script_catalog_type *type = &c->catalog->types[found];
        const char *name = strrchr(type->name, '.');
        name = name ? name + 1 : type->name;
        if (!strncmp(name, "Game", 4) && isupper((unsigned char)name[4])) {
            name += 4;
        }
        for (const char *p = "self."; *p; p++) {
            apush(out, *p);
        }
        append_pascal(&out, name);
        if (member) {
            apush(out, '.');
            append_pascal(&out, member);
        }
        apush(out, 0);
    }
    free(key);
    free(member);
    char *text = out ? xstrdup(out) : NULL;
    afree(out);
    return text;
}
static void component_call(collector *c, TSNode node, TSNode call)
{
    char *path = component_path_text(c, call);
    if (path) {
        add(c, start(node), end(node), path);
    }
    free(path);
}
/* `state.x` and `local.x` name their symbol directly. */
static void storage_path(collector *c, TSNode node)
{
    TSNode object = field(node, "object");
    if (is(object, "builtin_reference") &&
        (!strncmp(c->text + start(object), "state", 5) ||
         !strncmp(c->text + start(object), "local", 5))) {
        unwrap_node(c, node, field(node, "member"));
    }
}

/* `return(x)` returns its single value as `return x`. */
static void bare_return(collector *c, TSNode list)
{
    TSNode value = ts_node_named_child(list, 0);
    size_t size = (size_t)(end(value) - start(value)) + 2;
    char *text = xmalloc(size);
    snprintf(text, size, " %.*s", end(value) - start(value), c->text + start(value));
    add(c, start(list), end(list), text);
    free(text);
}
static void collect(collector *c, TSNode node, pass phase)
{
    const char *type = ts_node_type(node);
    if (phase == PASS_LABELS) {
        if (!strcmp(type, "argument_list")) {
            labels(c, node);
        }
    } else if (!strcmp(type, ";")) {
        add(c, start(node), end(node), "");
    } else if (!strcmp(type, "metadata_declaration")) {
        delete_line(c, node);
    } else if (!strcmp(type, "if_statement")) {
        TSNode other = field(node, "alternative");
        if (is(other, "block") && !ts_node_named_child_count(other)) {
            add(c, end(field(node, "consequence")), end(other), "");
        }
    } else if (!strcmp(type, "function_declaration") || !strcmp(type, "macro_declaration")) {
        TSNode results = field(node, "results");
        if (!ts_node_is_null(results) && !ts_node_named_child_count(results)) {
            add(c, end(field(node, "parameters")), end(results), "");
        }
    } else if (!strcmp(type, "value_declaration") || !strcmp(type, "action_declaration") || !strcmp(type, "event_declaration")) {
        TSNode name = field(node, "name"), declared = field(node, "type");
        if (!ts_node_is_null(declared)) {
            add(c, end(name), end(declared), "");
        }
    } else if (!strcmp(type, "state_declaration") || !strcmp(type, "local_declaration")) {
        inferred_declaration(c, node);
    } else if (!strcmp(type, "typed_argument")) {
        TSNode value = field(node, "value");
        if (!ts_node_is_null(value) && ts_node_is_null(field(node, "source"))) {
            add(c, start(field(node, "type")), start(value), "");
        }
    } else if (!strcmp(type, "handler")) {
        handler(c, node);
    } else if (!strcmp(type, "cast_expression")) {
        unwrap_node(c, node, field(node, "value"));
    } else if (!strcmp(type, "parenthesized_expression")) {
        unwrap_node(c, node, field(node, "expression"));
    } else if (!strcmp(type, "member_expression")) {
        storage_path(c, node);
    } else if (!strcmp(type, "call_expression")) {
        component_call(c, node, node);
    }
    if (phase == PASS_SHAPE && !strcmp(type, "argument_list")) {
        TSNode parent = ts_node_parent(node);
        if (is(parent, "return_statement") && !ts_node_named_child_count(node)) {
            add(c, start(node), end(node), "");
        } else if (
            is(parent, "return_statement") &&
            ts_node_is_null(field(parent, "outcome")) &&
            ts_node_named_child_count(node) == 1 &&
            !is(ts_node_named_child(node, 0), "named_argument")
        ) {
            bare_return(c, node);
        } else {
            default_arguments(c, node);
        }
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        collect(c, ts_node_child(node, i), phase);
    }
}

typedef struct
{
    const script_catalog *catalog;
    const char *identity;
    const script_analysis *original;
} oracle;

/* Text with the accepted rewrites and one more applied, or NULL on overlap. */
static char *apply(const char *text, const rewrite *rewrites, const bool *accepted, int extra)
{
    text_edit *edits = NULL;
    for (int i = 0; i < acount(rewrites); i++) {
        if (!accepted[i] && i != extra) {
            continue;
        }
        for (int e = 0; e < acount(rewrites[i].edits); e++) {
            apush(edits, rewrites[i].edits[e]);
        }
    }
    for (int i = 0; i < acount(edits); i++) {
        for (int j = i + 1; j < acount(edits); j++) {
            if (edits[i].start < edits[j].end && edits[j].start < edits[i].end) {
                afree(edits);
                return NULL;
            }
            /* Two insertions at one point have no defined order. */
            if (edits[i].start == edits[j].start &&
                (edits[i].start == edits[i].end || edits[j].start == edits[j].end)) {
                afree(edits);
                return NULL;
            }
        }
    }
    char *out = text_edits_apply(text, edits, acount(edits));
    afree(edits);
    return out;
}
static bool accepts(const oracle *o, const char *text)
{
    script_syntax *syntax = script_parse(text);
    bool same = !syntax->diagnostic_count &&
                !ts_node_has_error(ts_tree_root_node(syntax->tree));
    if (same) {
        script_analysis *analysis = script_analyze_document(syntax, o->catalog, o->identity);
        same = same_program(o->original, analysis);
        script_analysis_free(analysis);
    }
    script_syntax_free(syntax);
    return same;
}
/* One round of a pass; returns the new text, or NULL when nothing applied. */
static char *round_text(const oracle *o, const char *text, pass phase)
{
    script_syntax *syntax = script_parse(text);
    script_analysis *analysis = script_analyze_document(syntax, o->catalog, o->identity);
    collector c = { .text = text, .analysis = analysis, .catalog = o->catalog };
    collect(&c, ts_tree_root_node(syntax->tree), phase);
    int count = acount(c.rewrites);
    bool *accepted = xcalloc((size_t)(count ? count : 1), sizeof *accepted);
    char *result = NULL;
    /* Most rewrites are independent: try them together first. */
    bool *all = xcalloc((size_t)(count ? count : 1), sizeof *all);
    for (int i = 0; i < count; i++) {
        char *trial = apply(text, c.rewrites, all, i);
        all[i] = trial != NULL;
        free(trial);
    }
    char *together = count ? apply(text, c.rewrites, all, -1) : NULL;
    if (together && accepts(o, together)) {
        result = together;
    } else {
        free(together);
        int kept = 0;
        for (int i = 0; i < count; i++) {
            char *trial = apply(text, c.rewrites, accepted, i);
            if (trial && accepts(o, trial)) {
                accepted[i] = true;
                kept++;
            }
            free(trial);
        }
        result = kept ? apply(text, c.rewrites, accepted, -1) : NULL;
    }
    for (int i = 0; i < count; i++) {
        rewrite_free(&c.rewrites[i]);
    }
    afree(c.rewrites);
    free(all);
    free(accepted);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return result;
}

char *authoring_source_minimize(const char *text, const script_catalog *catalog, const char *identity)
{
    script_syntax *syntax = script_parse(text);
    script_analysis *original = script_analyze_document(syntax, catalog, identity);
    char *current = xstrdup(text);
    if (original->valid && !original->diagnostic_count && !syntax->diagnostic_count) {
        oracle o = { catalog, identity, original };
        for (pass phase = PASS_SHAPE; phase <= PASS_LABELS; phase++) {
            for (int round = 0; round < MINIMIZE_ROUNDS; round++) {
                char *next = round_text(&o, current, phase);
                if (!next) {
                    break;
                }
                free(current);
                current = next;
            }
        }
    }
    script_analysis_free(original);
    script_syntax_free(syntax);
    return current;
}
