#include "graph/authoring/graph_edit.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "support/array.h"
#include "types/value_format.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Anything that is not an operator: a literal, name or call. */
#define ATOM_BINDING 100

typedef struct text
{
    char *buf;
    size_t cap;
    size_t used;
    behavior_expression_read **reads; /* NULL when the caller wants text only */
} text;

/* Truncates at cap; used stays below cap, so room never underflows. */
static void put(text *t, const char *s)
{
    assert(t->used < t->cap);
    size_t room = t->cap - 1 - t->used;
    size_t length = strlen(s);
    if (length > room) {
        length = room;
    }
    memcpy(t->buf + t->used, s, length);
    t->used += length;
    t->buf[t->used] = '\0';
}

/* Lower snake case, so "Random Range" and "HasDash" read as calls. */
static void put_identifier(text *t, const char *name)
{
    char out[128];
    size_t used = 0;
    for (size_t i = 0; name[i] && used + 2 < sizeof out; i++) {
        unsigned char c = (unsigned char)name[i];
        bool boundary = !isalnum(c) ||
                        (i && isupper(c) && islower((unsigned char)name[i - 1]));
        if (boundary && used && out[used - 1] != '_') {
            out[used++] = '_';
        }
        if (isalnum(c)) {
            out[used++] = (char)tolower(c);
        }
    }
    while (used && out[used - 1] == '_') {
        used--;
    }
    out[used] = '\0';
    put(t, out);
}

/* Component keys are namespaced, as in `Movement`. */
static const char *component_name(const char *key)
{
    const char *dot = key ? strrchr(key, '.') : NULL;
    return dot ? dot + 1 : key ? key : "component";
}

static void put_binding(text *t, const graph *g, const pin_binding *binding);

/* Writes a value the expression reads, noting where its text lies. */
static void put_read(text *t, const graph *g, const pin_binding *read)
{
    int start = (int)t->used;
    put_binding(t, g, read);
    if (t->reads) {
        apush(*t->reads, ((behavior_expression_read){ start, (int)t->used - start, *read }));
    }
}

/* Typographic operators, as node titles draw them. */
const char *behavior_expression_symbol(const char *symbol)
{
    static const char *const symbols[][2] = {
        { "*", "×" },  { "/", "÷" },  { "-", "−" },  { "<=", "≤" },
        { ">=", "≥" }, { "!=", "≠" }, { "==", "=" },
    };
    for (size_t i = 0; i < sizeof symbols / sizeof *symbols; i++) {
        if (!strcmp(symbol, symbols[i][0])) {
            return symbols[i][1];
        }
    }
    return symbol;
}

static void put_binding(text *t, const graph *g, const pin_binding *binding)
{
    switch (binding->kind) {
        case PIN_BINDING_VARIABLE: {
            const behavior_variable *variable = g->owner
                                                  ? behavior_asset_find_variable_by_id(
                                                        g->owner,
                                                        binding->variable_id
                                                    )
                                                  : NULL;
            put(t, variable ? variable->name : "variable");
            return;
        }
        case PIN_BINDING_COMPONENT_FIELD:
        case PIN_BINDING_COMPONENT:
            put(t, "Self.");
            put(t, component_name(binding->component_key));
            if (binding->kind == PIN_BINDING_COMPONENT_FIELD && binding->field_name) {
                put(t, ".");
                put(t, binding->field_name);
            }
            return;
        case PIN_BINDING_LOCAL: {
            const graph_local *local = graph_local_by_id(g, binding->local_id);
            put(t, local ? local->name : "local");
            return;
        }
        case PIN_BINDING_EVENT_OUTPUT: {
            node *event = graph_node_by_id(g, binding->event_node_id);
            int output = 0;
            const pin_spec *pin = event
                                    ? graph_pin_by_id(event, binding->event_pin_id, &output, NULL)
                                    : NULL;
            put(t, event && event->reference_name ? event->reference_name : "event");
            put(t, ".");
            put(t, pin && output && pin->name ? pin->name : "value");
            return;
        }
    }
    assert(false);
}

static void put_value(text *t, const pin_spec *pin, const pin_value *value)
{
    int width = pin_vector_width(pin);
    if (width && value->ptr && value->container == PIN_CONTAINER_SINGLE) {
        char number[64];
        snprintf(number, sizeof number, "vec%d(", width);
        put(t, number);
        for (int axis = 0; axis < width; axis++) {
            snprintf(
                number,
                sizeof number,
                "%s%g",
                axis ? ", " : "",
                (double)((const float *)value->ptr)[axis]
            );
            put(t, number);
        }
        put(t, ")");
        return;
    }
    bool quoted = value->type == PIN_STR && value->container == PIN_CONTAINER_SINGLE;
    put(t, quoted ? "\"" : "");
    put(t, pin_value_to_string(value));
    put(t, quoted ? "\"" : "");
}

static int binding_of(const node *n)
{
    int binding = 0;
    const char *spelling = behavior_expression_spelling(n->desc, &binding);
    int arity = binding == BEHAVIOR_EXPRESSION_PREFIX_BINDING ? 1 : 2;
    return spelling && binding && n->input_count == arity ? binding : ATOM_BINDING;
}

static void put_node(text *t, const graph *g, const node *n, int output);

/* Literals read as their value. Shader and field nodes leave pure_kind at its
   zero default, so only their Constant nodes are literals; a built-in such as
   Shader UV reads as its name. */
static bool literal_node(const node_desc *desc)
{
    return desc->display_class == NODE_DISPLAY_LITERAL ||
           (desc->pure_kind == NODE_PURE_CONST && desc->flow_kind != NODE_FLOW_NONE) ||
           !strcmp(desc->name, "Shader Constant") ||
           !strcmp(desc->name, "Field Constant");
}

/* Writes the source wired into an input, calling its edge conversion by name
 * so a conversion such as truncation stays visible. */
static int put_source(text *t, const graph *g, const node *n, int pin, const node *source, int source_pin)
{
    const node_desc *conversion = graph_input_conversion(g, n, pin);
    if (!conversion) {
        put_node(t, g, source, source_pin);
        return binding_of(source);
    }
    put_identifier(t, conversion->name);
    put(t, "(");
    put_node(t, g, source, source_pin);
    put(t, ")");
    return ATOM_BINDING;
}

/* Writes an input's value and returns how tightly its text binds. */
static int put_operand(text *t, const graph *g, const node *n, int pin)
{
    const pin_spec *spec = &n->input_pins[pin];
    node *source = NULL;
    int source_pin = -1;
    if (graph_inline_source(g, n, pin, &source, &source_pin)) {
        return put_source(t, g, n, pin, source, source_pin);
    }
    if (spec->binding) {
        put_read(t, g, spec->binding);
        return ATOM_BINDING;
    }
    if ((spec->flags & PIN_FLAG_DEFAULT_SELF) && !spec->default_value.u64) {
        put(t, "Self");
        return ATOM_BINDING;
    }
    put_value(t, spec, &spec->default_value);
    return ATOM_BINDING;
}

/* Parenthesizes an operand that binds looser than `binding`; a negative
 * number binds like a prefix minus. */
static void put_input(text *t, const graph *g, const node *n, int pin, int binding)
{
    char operand[512];
    text value = { operand, sizeof operand, 0, t->reads };
    operand[0] = '\0';
    int first = t->reads ? acount(*t->reads) : 0;
    int operand_binding = put_operand(&value, g, n, pin);
    if (operand[0] == '-' && operand_binding > BEHAVIOR_EXPRESSION_PREFIX_BINDING) {
        operand_binding = BEHAVIOR_EXPRESSION_PREFIX_BINDING;
    }
    bool group = operand_binding < binding;
    put(t, group ? "(" : "");
    for (int i = first; t->reads && i < acount(*t->reads); i++) {
        (*t->reads)[i].start += (int)t->used;
    }
    put(t, operand);
    put(t, group ? ")" : "");
}

static void put_arguments(text *t, const graph *g, const node *n)
{
    put(t, "(");
    bool first = true;
    bool spelled = behavior_expression_arity(n->desc) == n->input_count;
    /* A component node's setting names the call rather than an argument. */
    int setting = component_node_kind_of(n->desc) ? component_node_setting(n) : -1;
    for (int a = 0; a < n->input_count; a++) {
        int p = spelled ? behavior_expression_argument_input(n->desc, a) : a;
        if ((n->input_pins[p].flags & PIN_FLAG_SERVICE) || p == setting) {
            continue;
        }
        put(t, first ? "" : ", ");
        put_input(t, g, n, p, 0);
        first = false;
    }
    put(t, ")");
}

/* An entity input left to its default, or fed by a folded Self. */
static bool reads_self(const graph *g, const node *n, int pin)
{
    const pin_spec *spec = &n->input_pins[pin];
    node *source = NULL;
    int source_pin = -1;
    if (graph_inline_source(g, n, pin, &source, &source_pin)) {
        return source->desc->pure_kind == NODE_PURE_SELF;
    }
    return !graph_input_source(g, n, pin, &source, &source_pin) && !spec->binding &&
           (spec->flags & PIN_FLAG_DEFAULT_SELF) && !spec->default_value.u64;
}

/* Component reads, generated or configured, read like their bindings:
 * `Self.Health.current`, and `has(Self.Dash)` for presence. A read
 * of Self is a read callers may name; another entity's is spelled out. */
static bool put_component(text *t, const graph *g, const node *n, int output)
{
    const ecs_component_desc *component = component_node_component(n);
    component_node_kind kind = component_node_kind_of(n->desc);
    node_component_kind access = n->desc->component_kind;
    bool has = kind == COMPONENT_NODE_HAS || access == NODE_COMPONENT_HAS_SELF ||
               access == NODE_COMPONENT_HAS_ENTITY;
    bool get = kind == COMPONENT_NODE_GET || access == NODE_COMPONENT_GET_SELF ||
               access == NODE_COMPONENT_GET_ENTITY;
    if (!component || !(get || has)) {
        return false;
    }
    int entity = -1;
    for (int p = 0; p < n->input_count && entity < 0; p++) {
        entity = n->input_pins[p].type == PIN_HANDLE ? p : -1;
    }
    const pin_spec *out = &n->output_pins[output];
    bool field = get && out->field_source && out->name;
    put(t, has ? "has(" : "");
    if (entity < 0 || reads_self(g, n, entity)) {
        pin_binding read = { .kind = field ? PIN_BINDING_COMPONENT_FIELD
                                           : PIN_BINDING_COMPONENT,
                             .component_key = component->key,
                             .field_name = field ? out->name : NULL };
        put_read(t, g, &read);
    } else {
        put_input(t, g, n, entity, ATOM_BINDING);
        put(t, ".");
        put(t, component->name);
        put(t, field ? "." : "");
        put(t, field ? out->name : "");
    }
    put(t, has ? ")" : "");
    return true;
}

static void put_node(text *t, const graph *g, const node *n, int output)
{
    const node_desc *desc = n->desc;
    int binding = binding_of(n);
    if (binding == BEHAVIOR_EXPRESSION_PREFIX_BINDING) {
        put(t, behavior_expression_symbol(behavior_expression_spelling(desc, &binding)));
        put_input(t, g, n, 0, binding);
        return;
    }
    if (binding != ATOM_BINDING) {
        const char *spelling = behavior_expression_spelling(desc, &binding);
        bool right = !strcmp(spelling, "^");
        put_input(t, g, n, 0, binding + right);
        put(t, " ");
        put(t, behavior_expression_symbol(spelling));
        put(t, " ");
        put_input(t, g, n, 1, binding + !right);
        return;
    }
    if (put_component(t, g, n, output)) {
        return;
    }
    if (desc->pure_kind == NODE_PURE_SELF) {
        put(t, "Self");
        return;
    }
    if (desc->pure_kind == NODE_PURE_GET) {
        pin_binding read = { .kind = PIN_BINDING_VARIABLE,
                             .variable_id = n->reference_id };
        if (g->owner && behavior_asset_find_variable_by_id(g->owner, n->reference_id)) {
            put_read(t, g, &read);
        } else {
            put(t, n->reference_name ? n->reference_name : "variable");
        }
        return;
    }
    if (literal_node(desc)) {
        if (n->input_count == 1) {
            put_input(t, g, n, 0, 0);
        } else {
            put_value(t, &n->output_pins[output], &n->output_pins[output].default_value);
        }
        return;
    }
    /* Named sources such as shader and field parameters read as their name. */
    if (!n->input_count && n->reference_name && n->reference_name[0]) {
        put(t, n->reference_name);
        return;
    }
    const char *named = n->input_count == 1 ? graph_node_name_literal(g, n) : NULL;
    if (named) {
        put(t, named);
        return;
    }
    /* Make and Break Struct read like the Make_<T> and Break_<T> they lower to. */
    const node_desc *made = component_node_struct(n);
    desc = made ? made : desc;
    const char *function = behavior_expression_spelling(desc, &binding);
    char vector[8];
    bool vector_struct = made && made->pure_kind == NODE_PURE_MAKE &&
                         graph_vector_constructor(&made->outputs[0]) &&
                         pin_vector_width(&made->outputs[0]) == made->input_count;
    if (graph_node_is_vector_constructor(n) || vector_struct) {
        snprintf(vector, sizeof vector, "vec%d", desc->input_count);
        function = vector;
    }
    if (function) {
        put(t, function);
    } else {
        put_identifier(
            t,
            !strncmp(desc->name, "Shader ", 7)  ? desc->name + 7
            : !strncmp(desc->name, "Field ", 6) ? desc->name + 6
                                                : desc->name
        );
    }
    if (n->input_count || function) {
        put_arguments(t, g, n);
    }
    if (n->output_count > 1 && n->output_pins[output].name) {
        put(t, ".");
        put(t, n->output_pins[output].name);
    }
}

void behavior_expression_format(
    const graph *g,
    const node *target,
    int pin,
    char *buf,
    size_t cap,
    behavior_expression_read **reads
)
{
    assert(cap && pin >= 0 && pin < target->input_count);
    text t = { buf, cap, 0, reads };
    buf[0] = '\0';
    int first = reads ? acount(*reads) : 0;
    node *source = NULL;
    int source_pin = -1;
    if (graph_input_source(g, target, pin, &source, &source_pin) &&
        !graph_input_local(g, target, pin)) {
        put_source(&t, g, target, pin, source, source_pin);
    } else {
        put_operand(&t, g, target, pin);
    }
    if (!reads) {
        return;
    }
    /* Truncation drops the reads past the end and cuts the last one short. */
    int kept = acount(*reads);
    while (kept > first && (*reads)[kept - 1].start >= (int)t.used) {
        kept--;
    }
    asetlen(*reads, kept);
    if (kept > first) {
        behavior_expression_read *last = &(*reads)[kept - 1];
        if (last->start + last->length > (int)t.used) {
            last->length = (int)t.used - last->start;
        }
    }
}
