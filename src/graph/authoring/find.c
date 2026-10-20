#include "graph/authoring/find.h"

#include "graph/model/behavior_asset.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/core_nodes.h"
#include "types/value_format.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool contains(const char *text, const char *query)
{
    size_t length = strlen(query);
    for (const char *t = text; t && *t; t++) {
        size_t i = 0;
        while (i < length && t[i] &&
               tolower((unsigned char)t[i]) == tolower((unsigned char)query[i])) {
            i++;
        }
        if (i == length) {
            return true;
        }
    }
    return false;
}

/* Records text, prefixed by the pin that holds it, when it contains query. */
static bool found(
    graph_find_match *out,
    graph_find_field field,
    const char *pin,
    const char *text,
    const char *query
)
{
    if (!contains(text, query)) {
        return false;
    }
    out->field = field;
    snprintf(out->text, sizeof out->text, "%s%s%s", pin ? pin : "", pin ? ": " : "", text);
    return true;
}

static bool find_pin_names(const node *n, const char *query, graph_find_match *out)
{
    for (int p = 0; p < n->input_count + n->output_count; p++) {
        const pin_spec *pin = p < n->input_count
                                ? &n->input_pins[p]
                                : &n->output_pins[p - n->input_count];
        if (pin->kind == PIN_DATA &&
            found(out, GRAPH_FIND_PIN, NULL, pin->name, query)) {
            return true;
        }
    }
    return false;
}

/* The value a data pin holds as text, or NULL for values without one. */
static const char *pin_literal(const pin_spec *pin)
{
    pin_type type = pin->default_value.type;
    if (pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE ||
        type == PIN_FLOW || type == PIN_HANDLE || type == PIN_STRUCT) {
        return NULL;
    }
    return pin_value_to_string(&pin->default_value);
}

/* Unwired, unbound inputs and a literal's outputs hold authored values. */
static bool find_values(const graph *g, const node *n, const char *query, graph_find_match *out)
{
    for (int p = 0; p < n->input_count; p++) {
        const pin_spec *pin = &n->input_pins[p];
        node *source = NULL;
        int source_pin = -1;
        if (!pin->binding && !graph_input_source(g, n, p, &source, &source_pin) &&
            found(out, GRAPH_FIND_VALUE, pin->name, pin_literal(pin), query)) {
            return true;
        }
    }
    for (int p = 0; n->desc->display_class == NODE_DISPLAY_LITERAL && p < n->output_count;
         p++) {
        const pin_spec *pin = &n->output_pins[p];
        if (found(out, GRAPH_FIND_VALUE, pin->name, pin_literal(pin), query)) {
            return true;
        }
    }
    return false;
}

static const char *binding_text(
    const behavior_asset *asset,
    const graph *g,
    const node *n,
    int p,
    char *buf,
    size_t size
)
{
    const graph_local *local = graph_input_local(g, n, p);
    const pin_binding *b = n->input_pins[p].binding;
    if (local || !b) {
        return local ? local->name : NULL;
    }
    switch (b->kind) {
        case PIN_BINDING_VARIABLE: {
            const behavior_variable *v = asset
                                           ? behavior_asset_find_variable_by_id(asset, b->variable_id)
                                           : NULL;
            return v ? v->name : NULL;
        }
        case PIN_BINDING_LOCAL: {
            const graph_local *named = graph_local_by_id(g, b->local_id);
            return named ? named->name : NULL;
        }
        case PIN_BINDING_COMPONENT:
        case PIN_BINDING_COMPONENT_FIELD:
            snprintf(
                buf,
                size,
                "%s%s%s",
                b->component_key ? b->component_key : "",
                b->field_name ? "." : "",
                b->field_name ? b->field_name : ""
            );
            return buf;
        case PIN_BINDING_EVENT_OUTPUT: return NULL;
    }
    return NULL;
}

static bool find_bindings(
    const behavior_asset *asset,
    const graph *g,
    const node *n,
    const char *query,
    graph_find_match *out
)
{
    char text[160];
    for (int p = 0; p < n->input_count; p++) {
        const char *bound = binding_text(asset, g, n, p, text, sizeof text);
        if (found(out, GRAPH_FIND_BINDING, n->input_pins[p].name, bound, query)) {
            return true;
        }
    }
    return false;
}

bool graph_find_node(
    const behavior_asset *asset,
    const graph *g,
    const node *n,
    const char *title,
    const char *query,
    graph_find_match *out
)
{
    if (!query || !query[0]) {
        return false;
    }
    const char *source = n->desc == &NODE_BehaviorExpression && asset
                           ? behavior_expression_source(asset, n)
                           : NULL;
    return found(out, GRAPH_FIND_TITLE, NULL, title, query) ||
           find_pin_names(n, query, out) || find_values(g, n, query, out) ||
           find_bindings(asset, g, n, query, out) ||
           found(out, GRAPH_FIND_EXPRESSION, NULL, source, query) ||
           found(out, GRAPH_FIND_COMMENT, NULL, n->comment, query);
}
