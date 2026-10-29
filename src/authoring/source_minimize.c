#include "authoring/source_minimize.h"

#include "authoring/source_edit_refactor_internal.h"
#include "authoring/source_format.h"
#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/text_edit.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    const TSTree *tree;
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

/* Equivalence ignores source positions and reference markers, which lower to
 * the same reads. */
/* An original analysis, a rewritten one and the catalog both resolve. */
typedef struct
{
    const script_analysis *a, *b;
    const script_catalog *catalog;
} pair;
static bool same_expression(const pair *, int, int);
static bool same_statement(const pair *, int, int);

static bool payload(const script_symbol *s)
{
    return s->kind == SCRIPT_PARAMETER && s->type.storage == PIN_STRUCT &&
           (s->type.flags & SCRIPT_TYPE_RESULTS);
}
static bool same_type(script_type x, script_type y)
{
    return x.storage == y.storage && x.container == y.container &&
           x.nominal == y.nominal && x.parameter == y.parameter &&
           x.flags == y.flags && x.collection == y.collection;
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
static bool same_symbol(const pair *p, int i, int j)
{
    const script_analysis *a = p->a, *b = p->b;
    if (i < 0 || j < 0) {
        return i == j;
    }
    const script_symbol *x = &a->symbols[i], *y = &b->symbols[j];
    /* Implicit payload parameters record their handler; declared ones rely on
     * the handler's parameter list, which is compared separately. */
    bool parameter = x->kind == SCRIPT_PARAMETER;
    return x->kind == y->kind && same_type(x->type, y->type) &&
           x->capability == y->capability &&
           (parameter || x->function == y->function) && x->writable == y->writable &&
           x->disabled == y->disabled && x->outcome == y->outcome &&
           x->source_relationship == y->source_relationship &&
           x->source_traversal == y->source_traversal &&
           x->source_variable == y->source_variable &&
           x->query_description == y->query_description &&
           x->query_field == y->query_field &&
           same_type(x->component_type, y->component_type) &&
           same_text(x->name, y->name);
}
/* Arguments evaluate in source order, so a rewrite must keep their order as
 * well as their parameters. */
static bool same_arguments(const pair *p, const script_expression *x, const script_expression *y)
{
    if (x->argument_count != y->argument_count) {
        return false;
    }
    for (int i = 0; i < x->argument_count; i++) {
        const script_argument *left = &x->arguments[i], *right = &y->arguments[i];
        if (right->parameter != left->parameter || right->member != left->member ||
            !same_expression(p, left->value, right->value)) {
            return false;
        }
    }
    return true;
}
/* Component paths record their root symbol only for navigation; the
 * receiver expression carries their meaning. */
static bool component_path(const script_expression *e)
{
    return e->kind == SCRIPT_EXPR_COMPONENT || e->kind == SCRIPT_EXPR_MEMBER;
}
static const script_catalog_capability *capability(const pair *p, const script_expression *e)
{
    return e->kind == SCRIPT_EXPR_CALL
             ? script_catalog_capability_by_id(p->catalog, e->capability)
             : NULL;
}
/* A call to a script function lowers its results from the callee, whatever
 * results the call site recorded. */
static bool script_call(const pair *p, const script_expression *e)
{
    return e->kind == SCRIPT_EXPR_CALL && e->right >= 0 && !capability(p, e);
}
/* The value bound to a capability parameter ordinal, or -1. */
static int argument(const script_expression *e, int parameter)
{
    for (int i = 0; parameter >= 0 && i < e->argument_count; i++) {
        if (e->arguments[i].parameter == parameter) {
            return e->arguments[i].value;
        }
    }
    return -1;
}
/* A whole-component accessor call such as `get_dash()`, `get_entity_dash(e)`
 * or `has_dash()`, read directly or through its single `.Value` result. */
static const script_catalog_capability *accessor(
    const pair *p,
    const script_analysis *a,
    int *i,
    script_component_access access
)
{
    const script_expression *e = &a->expressions[*i];
    if (e->kind == SCRIPT_EXPR_MEMBER && e->left >= 0) {
        const script_catalog_capability *c = capability(p, &a->expressions[e->left]);
        if (c && c->result_count == 1 &&
            e->member == script_catalog_id("port", c->results[0].name)) {
            *i = e->left;
            e = &a->expressions[*i];
        }
    }
    const script_catalog_capability *c = capability(p, e);
    return c && c->component_access == access ? c : NULL;
}
/* An entity argument and a path root name the same entity; an omitted
 * argument addresses the owner. */
static bool same_entity(const pair *p, int entity, int root)
{
    if (entity >= 0) {
        return same_expression(p, entity, root);
    }
    const script_expression *r = &p->b->expressions[root];
    return r->kind == SCRIPT_EXPR_SYMBOL && r->symbol == p->b->owner_symbol;
}
/* `get_dash()` reads `self.Dash`; `has_dash()` tests it as a condition. */
static bool same_component(const pair *p, int i, int j)
{
    const script_expression *y = &p->b->expressions[j];
    bool presence = false;
    if (y->kind == SCRIPT_EXPR_BINARY && y->op == SCRIPT_OP_NE &&
        y->right >= 0 && p->b->expressions[y->right].kind == SCRIPT_EXPR_NIL) {
        y = &p->b->expressions[y->left];
        presence = true;
    }
    const script_catalog_capability *c = accessor(
        p,
        p->a,
        &i,
        presence || (y->kind == SCRIPT_EXPR_COMPONENT &&
                     (y->type.flags & SCRIPT_TYPE_OPTIONAL))
            ? SCRIPT_COMPONENT_ACCESS_HAS
            : SCRIPT_COMPONENT_ACCESS_READ
    );
    if (c && c->component_field) {
        if (y->kind != SCRIPT_EXPR_MEMBER || y->member != c->component_field) {
            return false;
        }
        y = &p->b->expressions[y->left];
    }
    if (y->kind != SCRIPT_EXPR_COMPONENT) {
        return false;
    }
    return c && c->component == y->capability &&
           same_entity(p, argument(&p->a->expressions[i], c->component_entity), y->left);
}
static script_operator operator_of(const char *name)
{
    static const struct
    {
        const char *name;
        script_operator op;
    } operators[] = {
        { "+", SCRIPT_OP_ADD }, { "-", SCRIPT_OP_SUB }, { "*", SCRIPT_OP_MUL },
        { "/", SCRIPT_OP_DIV }, { "<", SCRIPT_OP_LT },  { "<=", SCRIPT_OP_LE },
        { ">", SCRIPT_OP_GT },  { ">=", SCRIPT_OP_GE }, { "==", SCRIPT_OP_EQ },
        { "!=", SCRIPT_OP_NE },
    };
    for (size_t i = 0; name && i < sizeof operators / sizeof *operators; i++) {
        if (!strcmp(operators[i].name, name)) {
            return operators[i].op;
        }
    }
    return SCRIPT_OP_NONE;
}
static bool real(script_type type)
{
    return (type.storage == PIN_F32 || type.storage == PIN_F64) &&
           type.container == PIN_CONTAINER_SINGLE;
}
static bool integer(script_type type)
{
    return type.storage == PIN_I32 && type.container == PIN_CONTAINER_SINGLE;
}
/* The operator a call to `add`, `less`, `not`, `and` or `or` spells, when
 * the operator lowers to the same operation on these operands. */
static script_operator operator_call(const pair *p, const script_expression *x)
{
    const script_catalog_capability *c = capability(p, x);
    if (!c || c->result_count != 1) {
        return SCRIPT_OP_NONE;
    }
    int first = argument(x, 0), second = argument(x, 1);
    if (c->parameter_count == 1 && first >= 0 && script_name_matches(c->name, "not")) {
        return SCRIPT_OP_NOT;
    }
    if (c->parameter_count != 2 || first < 0 || second < 0) {
        return SCRIPT_OP_NONE;
    }
    const script_expression *right = &p->a->expressions[second];
    /* `&&` and `||` skip their right operand, which must not act. */
    bool quiet = !(right->effects & ~(SCRIPT_EFFECT_READ | SCRIPT_EFFECT_INVOCATION_READ));
    if (quiet && script_name_matches(c->name, "and")) {
        return SCRIPT_OP_AND;
    }
    if (quiet && script_name_matches(c->name, "or")) {
        return SCRIPT_OP_OR;
    }
    script_type l = p->a->expressions[first].type, r = right->type;
    /* A typed parameter converts its operand first: `less` compares doubles
     * as floats, while `<` compares them as doubles. */
    script_type pa = c->parameters[0].type, pb = c->parameters[1].type;
    if ((!(pa.flags & SCRIPT_TYPE_UNTYPED) && pa.storage != l.storage) ||
        (!(pb.flags & SCRIPT_TYPE_UNTYPED) && pb.storage != r.storage)) {
        return SCRIPT_OP_NONE;
    }
    script_operator op = operator_of(c->operator_name);
    /* Integer comparisons agree; integer arithmetic may not. */
    bool ints = integer(l) && integer(r) && op >= SCRIPT_OP_EQ && op <= SCRIPT_OP_GE;
    return (real(l) && real(r)) || ints ? op : SCRIPT_OP_NONE;
}
static bool same_operator(const pair *p, int i, int j)
{
    const script_expression *x = &p->a->expressions[i], *y = &p->b->expressions[j];
    const script_catalog_capability *c = capability(p, x);
    /* An operator on other operands resolves to the capability's own call. */
    if (c && y->kind == SCRIPT_EXPR_CALL && y->capability == x->capability &&
        y->op != SCRIPT_OP_NONE && y->op == operator_of(c->operator_name)) {
        return same_value_type(x->type, y->type) && same_arguments(p, x, y);
    }
    script_operator op = operator_call(p, x);
    if (op == SCRIPT_OP_NONE || op != y->op ||
        (y->kind != SCRIPT_EXPR_BINARY && y->kind != SCRIPT_EXPR_UNARY) ||
        !same_value_type(x->type, y->type)) {
        return false;
    }
    return same_expression(p, argument(x, 0), y->left) &&
           (op == SCRIPT_OP_NOT || same_expression(p, argument(x, 1), y->right));
}
static bool same_expression(const pair *p, int i, int j)
{
    if (i >= 0 && j >= 0) {
        const script_expression *x = &p->a->expressions[i],
                                *y = &p->b->expressions[j];
        if (x->kind == SCRIPT_EXPR_MEMBER && x->left >= 0) {
            const script_expression *call = &p->a->expressions[x->left];
            const script_catalog_capability *cap = capability(p, call);
            if (cap && cap->id == script_catalog_id("capability", "Self") &&
                cap->result_count == 1 &&
                x->member == script_catalog_id("port", cap->results[0].name)) {
                x = call;
            }
        }
        if (x->kind == SCRIPT_EXPR_CALL && x->right < 0 &&
            x->capability == script_catalog_id("capability", "Self") &&
            !x->argument_count && y->kind == SCRIPT_EXPR_SYMBOL &&
            y->symbol == p->b->owner_symbol && same_value_type(x->type, y->type)) {
            return true;
        }
    }
    if (i >= 0 && j >= 0 && (same_component(p, i, j) || same_operator(p, i, j))) {
        return true;
    }
    const script_analysis *a = p->a, *b = p->b;
    if (i < 0 || j < 0) {
        return i == j;
    }
    const script_expression *x = &a->expressions[i], *y = &b->expressions[j];
    script_type left_type = x->type, right_type = y->type;
    /* A numeric literal's inference marker does not change its stored value.
     * Keep its storage and exact constant comparison: float/int declarations
     * must not turn into inferred doubles. */
    if (x->kind == SCRIPT_EXPR_CONSTANT && y->kind == SCRIPT_EXPR_CONSTANT &&
        script_type_numeric(left_type) && script_type_numeric(right_type)) {
        left_type.flags &= ~SCRIPT_TYPE_UNTYPED;
        right_type.flags &= ~SCRIPT_TYPE_UNTYPED;
    }
    /* A call records the operator that resolved it only for presentation. */
    if (x->kind != y->kind || (x->kind != SCRIPT_EXPR_CALL && x->op != y->op) ||
        !same_value_type(left_type, right_type) || x->capability != y->capability ||
        x->member != y->member || x->read != y->read || x->write != y->write ||
        x->instance != y->instance || x->entry != y->entry ||
        x->offset != y->offset || x->effects != y->effects ||
        (x->kind != SCRIPT_EXPR_MEMBER && x->writable != y->writable) ||
        x->awaited != y->awaited ||
        x->constant_snapshot != y->constant_snapshot || x->partial != y->partial ||
        (!script_call(p, x) && x->result_count != y->result_count) ||
        (!component_path(x) && !same_symbol(p, x->symbol, y->symbol)) ||
        !same_expression(p, x->left, y->left)) {
        return false;
    }
    if (x->kind == SCRIPT_EXPR_CONSTANT &&
        !pin_value_equal(&x->constant, &y->constant)) {
        return false;
    }
    for (int r = 0; !script_call(p, x) && r < x->result_count; r++) {
        if (!same_value_type(x->results[r], y->results[r])) {
            return false;
        }
    }
    bool right = x->kind == SCRIPT_EXPR_CALL
                   ? x->right == y->right
                   : same_expression(p, x->right, y->right);
    return right && same_arguments(p, x, y);
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
/* Statement and function effects only report what their expressions do; a
 * path assignment also reads the component it writes, which lowers to no
 * read. Expression effects stay exact: bindings depend on reading calls. */
static bool same_effects(unsigned x, unsigned y)
{
    return !((x ^ y) & ~(unsigned)SCRIPT_EFFECT_READ);
}
/* `set_dash(v)` assigns `self.Dash = v` and `set_dash_remaining(v)` assigns
 * `self.Dash.remaining = v`. A partial struct merges into the old value,
 * which an assignment does not. */
static bool same_write(const pair *p, const script_statement *x, const script_statement *y)
{
    if (x->kind != SCRIPT_STMT_EXPRESSION || y->kind != SCRIPT_STMT_ASSIGN ||
        y->op != SCRIPT_OP_NONE || x->expression < 0 || y->expression < 0) {
        return false;
    }
    const script_expression *call = &p->a->expressions[x->expression];
    const script_expression *target = &p->b->expressions[y->expression];
    const script_catalog_capability *c = capability(p, call);
    if (!c || c->component_access != SCRIPT_COMPONENT_ACCESS_WRITE) {
        return false;
    }
    int value = argument(call, c->component_value);
    if (c->component_field) {
        if (target->kind != SCRIPT_EXPR_MEMBER || target->member != c->component_field) {
            return false;
        }
        target = &p->b->expressions[target->left];
    }
    return value >= 0 && !call->instance && !p->a->expressions[value].partial &&
           target->kind == SCRIPT_EXPR_COMPONENT &&
           target->capability == c->component &&
           same_entity(p, argument(call, c->component_entity), target->left) &&
           same_expression(p, value, y->second);
}
static bool same_statement(const pair *p, int i, int j)
{
    const script_analysis *a = p->a, *b = p->b;
    if (i < 0 || j < 0) {
        return i == j;
    }
    const script_statement *x = &a->statements[i], *y = &b->statements[j];
    if (same_write(p, x, y)) {
        return true;
    }
    if (x->kind != y->kind || x->op != y->op || x->reduction != y->reduction ||
        !same_effects(x->effects, y->effects) ||
        x->child_count != y->child_count || !same_symbol(p, x->symbol, y->symbol) ||
        (x->kind == SCRIPT_STMT_FOREACH && !same_symbol(p, x->key_symbol, y->key_symbol)) ||
        !same_expression(p, x->expression, y->expression) ||
        !same_expression(p, x->second, y->second) ||
        !same_expression(p, x->third, y->third) ||
        !same_statement(p, x->body, y->body) ||
        !same_statement(p, alternative(a, x->alternative), alternative(b, y->alternative))) {
        return false;
    }
    for (int c = 0; c < x->child_count; c++) {
        if (!same_statement(p, x->children[c], y->children[c])) {
            return false;
        }
    }
    return true;
}
static bool same_symbols(const pair *p, const int *x, const int *y, int count)
{
    for (int i = 0; i < count; i++) {
        if (!same_symbol(p, x[i], y[i])) {
            return false;
        }
    }
    return true;
}
static bool same_function(const pair *p, const script_function *x, const script_function *y)
{
    return x->kind == y->kind && x->receiver == y->receiver &&
           x->parent == y->parent && x->parameter_count == y->parameter_count &&
           x->result_count == y->result_count &&
           x->depth_limit == y->depth_limit && x->event == y->event &&
           x->action == y->action && same_text(x->entry, y->entry) &&
           same_effects(x->effects, y->effects) && x->pure == y->pure &&
           x->disabled == y->disabled && same_symbol(p, x->symbol, y->symbol) &&
           same_symbols(p, x->parameters, y->parameters, x->parameter_count) &&
           same_symbols(p, x->results, y->results, x->result_count) &&
           same_statement(p, x->body, y->body);
}
/* Declared symbols in order, without payload aggregates. */
static int next_declared(const script_analysis *a, int i)
{
    while (i < a->symbol_count && payload(&a->symbols[i])) {
        i++;
    }
    return i;
}
static bool same_declarations(const pair *p)
{
    const script_analysis *a = p->a, *b = p->b;
    int i = next_declared(a, 0), j = next_declared(b, 0);
    for (; i < a->symbol_count && j < b->symbol_count;
         i = next_declared(a, i + 1), j = next_declared(b, j + 1)) {
        if (!same_symbol(p, i, j) ||
            !same_expression(p, a->symbols[i].initializer, b->symbols[j].initializer)) {
            return false;
        }
    }
    return i == a->symbol_count && j == b->symbol_count;
}
static bool same_query(const pair *p, int i, int j)
{
    const script_analysis *a = p->a, *b = p->b;
    if (i < 0 || j < 0) {
        return i == j;
    }
    const script_query *x = &a->queries[i], *y = &b->queries[j];
    if (x->kind != y->kind || x->required != y->required ||
        x->description != y->description || x->component != y->component ||
        x->source_relationship != y->source_relationship ||
        x->source_traversal != y->source_traversal ||
        x->source_variable != y->source_variable ||
        x->target_variable != y->target_variable || x->child_count != y->child_count) {
        return false;
    }
    if (x->kind == SCRIPT_QUERY_VALUE && !same_expression(p, x->value, y->value)) {
        return false;
    }
    for (int c = 0; c < x->child_count; c++) {
        if (!same_query(p, x->children[c], y->children[c])) {
            return false;
        }
    }
    return true;
}
static bool same_query_descriptions(const pair *p)
{
    if (p->a->description_count != p->b->description_count ||
        p->a->system_description != p->b->system_description) {
        return false;
    }
    for (int i = 0; i < p->a->description_count; i++) {
        const script_query_description *x = &p->a->descriptions[i],
                                       *y = &p->b->descriptions[i];
        if (x->system != y->system || x->query_variable_count != y->query_variable_count ||
            !same_type(x->row_type, y->row_type) || !same_query(p, x->query, y->query) ||
            acount(x->symbols) != acount(y->symbols) ||
            acount(x->captures) != acount(y->captures) ||
            acount(x->stages) != acount(y->stages)) {
            return false;
        }
        for (int j = 0; j < acount(x->symbols); j++) {
            if (!same_symbol(p, x->symbols[j], y->symbols[j])) {
                return false;
            }
        }
        for (int j = 0; j < acount(x->captures); j++) {
            if (!same_symbol(p, x->captures[j], y->captures[j])) {
                return false;
            }
        }
        for (int j = 0; j < acount(x->stages); j++) {
            const script_query_stage *a = &x->stages[j], *b = &y->stages[j];
            if (!same_symbol(p, a->symbol, b->symbol) ||
                (a->symbol < 0 ? !same_query(p, a->expression, b->expression)
                               : !same_expression(p, a->expression, b->expression))) {
                return false;
            }
        }
    }
    return true;
}
static bool same_ids(const uint64_t *x, int n, const uint64_t *y, int m)
{
    return n == m && (!n || !memcmp(x, y, sizeof *x * (size_t)n));
}
static bool same_program(const pair *p)
{
    const script_analysis *a = p->a, *b = p->b;
    if (!a->valid || !b->valid || a->diagnostic_count || b->diagnostic_count ||
        a->kind != b->kind || a->owner_type != b->owner_type ||
        !same_text(a->name, b->name) || a->function_count != b->function_count ||
        a->binding_count != b->binding_count || !same_query(p, a->query, b->query) ||
        !same_symbol(p, a->owner_symbol, b->owner_symbol) ||
        !same_declarations(p) || !same_query_descriptions(p)) {
        return false;
    }
    for (int i = 0; i < a->function_count; i++) {
        if (!same_function(p, &a->functions[i], &b->functions[i])) {
            return false;
        }
    }
    for (int i = 0; i < a->binding_count; i++) {
        const script_binding *x = &a->bindings[i], *y = &b->bindings[i];
        if (!same_expression(p, x->target, y->target) ||
            !same_expression(p, x->value, y->value) ||
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
/* The group of operators whose mix reads ambiguously, or 0. */
static int operator_group(const collector *c, TSNode binary)
{
    TSNode op = field(binary, "operator");
    static const struct
    {
        const char *text;
        int group;
    } groups[] = { { "||", 1 }, { "&&", 1 }, { "|", 2 }, { "^", 2 }, { "&", 2 } };
    for (size_t i = 0; i < sizeof groups / sizeof *groups; i++) {
        size_t n = strlen(groups[i].text);
        if ((size_t)(end(op) - start(op)) == n &&
            !strncmp(c->text + start(op), groups[i].text, n)) {
            return groups[i].group;
        }
    }
    return 0;
}
/* Parentheses that group `&&` under `||`, or one bitwise operator under
 * another, stay: precedence decides them, but the grouping reads better. */
static bool grouping(const collector *c, TSNode node)
{
    TSNode parent = ts_node_parent(node), inner = field(node, "expression");
    if (!is(parent, "binary_expression") || !is(inner, "binary_expression")) {
        return false;
    }
    int group = operator_group(c, parent);
    TSNode a = field(parent, "operator"), b = field(inner, "operator");
    bool same = end(a) - start(a) == end(b) - start(b) &&
                !strncmp(c->text + start(a), c->text + start(b), (size_t)(end(a) - start(a)));
    return group && group == operator_group(c, inner) && !same;
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
    return is(value, "number") || is(value, "string") ||
           is(value, "character") || is(value, "boolean") || is(value, "nil") ||
           is(value, "struct_expression") || is(value, "array_expression") ||
           is(value, "map_expression") ||
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
static void handler(collector *c, TSNode node)
{
    TSNode parameters = field(node, "parameters");
    /* `on X :: (a: T)` names the payload fields it reads, with their types. */
    if (!ts_node_is_null(field(node, "binding")) || ts_node_is_null(parameters)) {
        return;
    }
    add(c, end(field(node, "name")), end(parameters), "");
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
/* Behavior/library roots already retain per-instance storage. System roots
 * and callable locals require retain to preserve their private lifetime. */
static void root_storage(collector *c, TSNode node)
{
    TSNode storage = field(node, "storage"),
           program = ts_node_parent(ts_node_parent(node));
    if (!is(storage, "retain") ||
        (!is(program, "behavior") && !is(program, "library"))) {
        return;
    }
    int to = end(storage);
    while (c->text[to] == ' ' || c->text[to] == '\t') {
        to++;
    }
    rewrite r = { 0 };
    edit(&r, start(storage), to, "");
    if (ts_node_is_null(field(node, "type"))) {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (is(child, ":")) {
                edit(&r, start(child), end(child), ":=");
            } else if (is(child, "=")) {
                int after = end(child);
                while (c->text[after] == ' ' || c->text[after] == '\t') {
                    after++;
                }
                edit(&r, start(child), after, "");
            }
        }
    }
    apush(c->rewrites, r);
}
/* `name: type = value` and `type name = value` become `name := value`. */
static void inferred_declaration(collector *c, TSNode node)
{
    TSNode name = field(node, "name"), type = field(node, "type"),
           value = field(node, "value");
    if (ts_node_is_null(type) || ts_node_is_null(value)) {
        return;
    }
    if (!ts_node_is_null(field(node, "storage"))) {
        add(c, start(type), start(value), "= ");
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
/* Reflected source spelling: `Health` is `Health`, `cooldown_remaining`
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
/* `component("Health", "current")` reads `self.Health.Current`. */
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
/* The analyzed expression of this kind that a node spells, or NULL. */
static const script_expression *expression_at(const collector *c, TSNode node, script_expression_kind kind)
{
    for (int i = 0; i < c->analysis->expression_count; i++) {
        const script_expression *e = &c->analysis->expressions[i];
        if (e->kind == kind && e->range.start == start(node) &&
            e->range.end == end(node)) {
            return e;
        }
    }
    return NULL;
}
/* T(value) with one positional value, analyzed as a conversion. */
static bool conversion(const collector *c, TSNode call)
{
    TSNode arguments = field(call, "arguments");
    return ts_node_named_child_count(arguments) == 1 &&
           !is(ts_node_named_child(arguments, 0), "named_argument") &&
           expression_at(c, call, SCRIPT_EXPR_CAST);
}
/* An argument written in the call's own list. Defaults span the whole list;
 * an action's configured arguments lie at its declaration. */
static int written_argument(const collector *c, TSNode node, const script_expression *call, int parameter)
{
    int value = argument(call, parameter);
    TSNode list = field(node, "arguments");
    if (value < 0) {
        return -1;
    }
    script_range range = c->analysis->expressions[value].range;
    bool inside = range.start > start(list) && range.end < end(list);
    return inside ? value : -1;
}
static script_range value_range(const collector *c, int value)
{
    return c->analysis->expressions[value].range;
}
/* Operands that need no parentheses before a `.` or as an operand. */
static bool primary_at(const collector *c, script_range range)
{
    TSNode root = ts_tree_root_node(c->tree);
    TSNode node = ts_node_named_descendant_for_byte_range(
        root,
        (uint32_t)range.start,
        (uint32_t)range.end
    );
    if (start(node) != range.start || end(node) != range.end) {
        return false;
    }
    static const char *const primaries[] = { "identifier",
                                             "member_expression",
                                             "call_expression",
                                             "index_expression",
                                             "parenthesized_expression",
                                             "builtin_reference",
                                             "number",
                                             "character",
                                             "string",
                                             "boolean" };
    for (size_t i = 0; i < sizeof primaries / sizeof *primaries; i++) {
        if (is(node, primaries[i])) {
            return true;
        }
    }
    return false;
}
static void append_text(char **out, const char *text, int length)
{
    for (int i = 0; i < length; i++) {
        apush(*out, text[i]);
    }
}
/* `.Dash` or `.Dash.remaining`: the component's source name and the
 * field's reflected name, after a `)` that closes a grouped entity. */
static char *component_suffix(const collector *c, const script_catalog_capability *cap, bool close)
{
    const script_catalog_type *type = script_catalog_type_by_id(c->catalog, cap->component);
    if (!type) {
        return NULL;
    }
    char *name = authoring_source_identifier(type->source_name ? type->source_name : type->name, NULL, 0);
    const char *field_name = "";
    for (int i = 0; cap->component_field && i < type->field_count; i++) {
        if (type->fields[i].id == cap->component_field) {
            field_name = type->fields[i].name;
        }
    }
    char *member = *field_name ? authoring_source_identifier(field_name, NULL, 0)
                               : xstrdup("");
    if (!name || !member || (cap->component_field && !*field_name)) {
        free(name);
        free(member);
        return NULL;
    }
    size_t size = strlen(name) + strlen(member) + 4;
    char *text = xmalloc(size);
    snprintf(text, size, "%s.%s%s%s", close ? ")" : "", name, *member ? "." : "", member);
    free(name);
    free(member);
    return text;
}
/* Replaces [from, to) with a path to the component, keeping an entity
 * argument's own text in place so rewrites inside it still apply. */
static bool path_edits(rewrite *r, const collector *c, int from, int to, int entity, const script_catalog_capability *cap)
{
    if (entity < 0) {
        char *suffix = component_suffix(c, cap, false);
        if (!suffix) {
            return false;
        }
        size_t size = strlen(suffix) + sizeof "self";
        char *text = xmalloc(size);
        snprintf(text, size, "self%s", suffix);
        edit(r, from, to, text);
        free(text);
        free(suffix);
        return true;
    }
    script_range range = value_range(c, entity);
    bool bare = primary_at(c, range);
    char *suffix = component_suffix(c, cap, !bare);
    if (!suffix) {
        return false;
    }
    edit(r, from, range.start, bare ? "" : "(");
    edit(r, range.end, to, suffix);
    free(suffix);
    return true;
}
/* Whether a node's value is read as a condition, which tests presence. */
static bool condition(const collector *c, TSNode node)
{
    TSNode parent = ts_node_parent(node);
    if (is(parent, "if_statement") || is(parent, "while_statement")) {
        TSNode tested = field(parent, "condition");
        return start(tested) == start(node) && end(tested) == end(node);
    }
    if (is(parent, "unary_expression")) {
        return c->text[start(field(parent, "operator"))] == '!';
    }
    return is(parent, "binary_expression") && operator_group(c, parent) == 1;
}
/* `get_dash().Value` reads `self.Dash`; `has_dash()` tests `self.Dash`. */
static void accessor_read(collector *c, TSNode node, const script_expression *call, const script_catalog_capability *cap)
{
    TSNode parent = ts_node_parent(node), read = node;
    if (cap->component_access == SCRIPT_COMPONENT_ACCESS_READ &&
        is(parent, "member_expression") &&
        start(field(parent, "object")) == start(node) &&
        end(field(parent, "object")) == end(node)) {
        TSNode member = field(parent, "member");
        const char *result = cap->results[0].name;
        if ((size_t)(end(member) - start(member)) == strlen(result) &&
            !strncmp(c->text + start(member), result, strlen(result))) {
            read = parent;
        }
    }
    rewrite r = { 0 };
    if (!path_edits(&r, c, start(read), end(read), written_argument(c, node, call, cap->component_entity), cap)) {
        return;
    }
    if (cap->component_access == SCRIPT_COMPONENT_ACCESS_HAS && !condition(c, node)) {
        edit(&r, end(read), end(read), " != nil");
    }
    apush(c->rewrites, r);
}
static bool is_self(const collector *c, script_range range)
{
    return range.end - range.start == 4 && !strncmp(c->text + range.start, "self", 4);
}
/* `set_dash(v)` becomes `self.Dash = v`; the value keeps its text. */
static void accessor_write(
    collector *c,
    TSNode node,
    const script_expression *call,
    const script_catalog_capability *cap
)
{
    TSNode statement = ts_node_parent(node);
    if (is(statement, "await_expression")) {
        statement = ts_node_parent(statement);
    }
    int value = written_argument(c, node, call, cap->component_value),
        entity = written_argument(c, node, call, cap->component_entity);
    if (!is(statement, "expression_statement") || value < 0) {
        return;
    }
    script_range v = value_range(c, value);
    int from = start(ts_node_named_child(statement, 0)), to = end(node);
    rewrite r = { 0 };
    if (entity >= 0 && value_range(c, entity).start < v.start) {
        if (!path_edits(&r, c, from, v.start, entity, cap)) {
            return;
        }
        text_edit *last = &r.edits[acount(r.edits) - 1];
        size_t size = strlen(last->text) + sizeof " = ";
        char *text = xmalloc(size);
        snprintf(text, size, "%s = ", last->text);
        free((char *)last->text);
        last->text = text;
    } else {
        /* An entity written after the value would be evaluated first. */
        if (entity >= 0 && !is_self(c, value_range(c, entity))) {
            return;
        }
        char *suffix = component_suffix(c, cap, false), *root = NULL;
        if (!suffix) {
            return;
        }
        append_text(&root, "self", 4);
        append_text(&root, suffix, (int)strlen(suffix));
        append_text(&root, " = ", 3);
        apush(root, 0);
        edit(&r, from, v.start, root);
        free(suffix);
        afree(root);
    }
    edit(&r, v.end, to, "");
    apush(c->rewrites, r);
}
static const char *spelling(script_operator op)
{
    static const char *const spellings[] = {
        [SCRIPT_OP_ADD] = "+", [SCRIPT_OP_SUB] = "-",  [SCRIPT_OP_MUL] = "*",
        [SCRIPT_OP_DIV] = "/", [SCRIPT_OP_LT] = "<",   [SCRIPT_OP_LE] = "<=",
        [SCRIPT_OP_GT] = ">",  [SCRIPT_OP_GE] = ">=",  [SCRIPT_OP_EQ] = "==",
        [SCRIPT_OP_NE] = "!=", [SCRIPT_OP_AND] = "&&", [SCRIPT_OP_OR] = "||",
    };
    return spellings[op];
}
/* `add(a, b)` becomes `(a + b)` and `not(x)` becomes `!(x)`; a later round
 * drops the parentheses that precedence makes redundant. */
static void operator_rewrite(collector *c, TSNode node, const script_expression *call)
{
    script_operator op = operator_call(&(pair){ c->analysis, NULL, c->catalog }, call);
    const script_catalog_capability *cap = script_catalog_capability_by_id(c->catalog, call->capability);
    if (cap && call->right < 0 && cap->id == script_catalog_id("capability", "Self") &&
        !call->argument_count && c->analysis->owner_symbol >= 0) {
        TSNode parent = ts_node_parent(node);
        TSNode member = field(parent, "member");
        if (is(parent, "member_expression") && cap->result_count == 1 &&
            end(member) - start(member) == (int)strlen(cap->results[0].name) &&
            !memcmp(
                c->text + start(member),
                cap->results[0].name,
                strlen(cap->results[0].name)
            )) {
            node = parent;
        }
        add(c, start(node), end(node), "self");
        return;
    }
    /* Other operands resolve the operator through the catalog. */
    if (op == SCRIPT_OP_NONE && cap && cap->parameter_count == 2) {
        op = operator_of(cap->operator_name);
    }
    if (op == SCRIPT_OP_NONE) {
        return;
    }
    int a = written_argument(c, node, call, 0),
        b = written_argument(c, node, call, 1);
    if (a < 0 || (op != SCRIPT_OP_NOT && b < 0)) {
        return;
    }
    script_range first = value_range(c, a);
    rewrite r = { 0 };
    if (op == SCRIPT_OP_NOT) {
        edit(&r, start(node), first.start, "!(");
        edit(&r, first.end, end(node), ")");
        apush(c->rewrites, r);
        return;
    }
    script_range second = value_range(c, b);
    if (second.start < first.end) {
        return;
    }
    size_t size = strlen(spelling(op)) + 3;
    char *between = xmalloc(size);
    snprintf(between, size, " %s ", spelling(op));
    edit(&r, start(node), first.start, "(");
    edit(&r, first.end, second.start, between);
    edit(&r, second.end, end(node), ")");
    free(between);
    apush(c->rewrites, r);
}
static void call_rewrites(collector *c, TSNode node)
{
    const script_expression *call = expression_at(c, node, SCRIPT_EXPR_CALL);
    if (!call) {
        return;
    }
    const script_catalog_capability *cap = script_catalog_capability_by_id(c->catalog, call->capability);
    /* A generic call such as has_component("Heat") names no fixed component
     * to spell as a path, so it keeps its call. */
    bool accessor = cap && cap->component;
    if (accessor && (cap->component_access == SCRIPT_COMPONENT_ACCESS_READ ||
                     cap->component_access == SCRIPT_COMPONENT_ACCESS_HAS)) {
        accessor_read(c, node, call, cap);
    } else if (accessor && cap->component_access == SCRIPT_COMPONENT_ACCESS_WRITE) {
        accessor_write(c, node, call, cap);
    } else {
        operator_rewrite(c, node, call);
    }
}
/* `state.x` names its symbol directly. */
static void storage_path(collector *c, TSNode node)
{
    TSNode object = field(node, "object");
    if (is(object, "builtin_reference") &&
        !strncmp(c->text + start(object), "state", 5)) {
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
    } else if (!strcmp(type, "action_declaration") || !strcmp(type, "event_declaration")) {
        TSNode name = field(node, "name"), declared = field(node, "type");
        if (!ts_node_is_null(declared)) {
            add(c, end(name), end(declared), "");
        }
    } else if (!strcmp(type, "state_declaration") || !strcmp(type, "local_declaration")) {
        root_storage(c, node);
        inferred_declaration(c, node);
    } else if (!strcmp(type, "typed_argument")) {
        TSNode value = field(node, "value");
        if (!ts_node_is_null(value) && ts_node_is_null(field(node, "source"))) {
            add(c, start(field(node, "type")), start(value), "");
        }
    } else if (!strcmp(type, "handler")) {
        handler(c, node);
    } else if (!strcmp(type, "call_expression") && conversion(c, node)) {
        /* Equivalence keeps only conversions whose value already has the type. */
        unwrap_node(c, node, ts_node_named_child(field(node, "arguments"), 0));
    } else if (!strcmp(type, "await_expression")) {
        unwrap_node(c, node, field(node, "value"));
    } else if (!strcmp(type, "parenthesized_expression") && !grouping(c, node)) {
        unwrap_node(c, node, field(node, "expression"));
    } else if (!strcmp(type, "member_expression")) {
        storage_path(c, node);
    } else if (!strcmp(type, "call_expression")) {
        component_call(c, node, node);
        call_rewrites(c, node);
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
    const char *text;
    script_range *comments;
} oracle;

static void collect_comments(TSNode node, script_range **comments)
{
    if (is(node, "comment")) {
        apush(*comments, script_node_range(node));
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        collect_comments(ts_node_child(node, i), comments);
    }
}
/* Execution equivalence does not protect comments. Reject a rewrite that
 * changes their exact bytes or order, retaining the original attachment
 * rather than guessing where a deleted wrapper's comments should move. */
static bool same_comments(const oracle *o, const script_syntax *syntax)
{
    script_range *comments = NULL;
    collect_comments(ts_tree_root_node(syntax->tree), &comments);
    bool same = acount(o->comments) == acount(comments);
    for (int i = 0; same && i < acount(comments); i++) {
        script_range a = o->comments[i], b = comments[i];
        same = a.end - a.start == b.end - b.start &&
               !memcmp(o->text + a.start, syntax->text + b.start, (size_t)(a.end - a.start));
    }
    afree(comments);
    return same;
}

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
                !ts_node_has_error(ts_tree_root_node(syntax->tree)) &&
                same_comments(o, syntax);
    if (same) {
        script_analysis *analysis = script_analyze_document(syntax, o->catalog, o->identity);
        same = same_program(&(pair){ o->original, analysis, o->catalog });
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
    collector c = { .text = text,
                    .analysis = analysis,
                    .catalog = o->catalog,
                    .tree = syntax->tree };
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
        memcpy(accepted, all, (size_t)count * sizeof *accepted);
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
        oracle o = { .catalog = catalog,
                     .identity = identity,
                     .original = original,
                     .text = text };
        collect_comments(ts_tree_root_node(syntax->tree), &o.comments);
        for (pass phase = PASS_SHAPE; phase <= PASS_LABELS; phase++) {
            /* Each round accepts at least one rewrite, so this ends when
             * nothing is left to drop. */
            for (char *next; (next = round_text(&o, current, phase));) {
                free(current);
                current = next;
            }
        }
        afree(o.comments);
    }
    script_analysis_free(original);
    script_syntax_free(syntax);
    return current;
}

char *authoring_source_canonical(
    const char *text,
    const script_catalog *catalog,
    const char *identity,
    char *error,
    size_t size
)
{
    script_syntax *syntax = script_parse(text);
    char *layout = authoring_source_format(syntax, error, size);
    script_syntax_free(syntax);
    if (!layout) {
        return NULL;
    }
    char *minimal = authoring_source_minimize(layout, catalog, identity);
    free(layout);
    syntax = script_parse(minimal);
    free(minimal);
    char *result = authoring_source_format(syntax, error, size);
    script_syntax_free(syntax);
    return result;
}
