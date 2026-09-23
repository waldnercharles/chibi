#include "graph/model/registry.h"
#include "graph/model/type.h"
#include "graph/nodes/behavior_expression.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct expression_input
{
    node *source;
    int source_pin;
    const pin_binding *binding;
    pin_type type;
    const char *type_name;
    pin_container container;
    char *name;
} expression_input;

typedef struct expression_term
{
    node *node;
    char *source;
    bool visiting;
} expression_term;

typedef struct expression_output
{
    uint64_t target_id;
    uint64_t target_pin_id;
} expression_output;

typedef struct expression_text
{
    char *text;
    size_t length;
    size_t capacity;
} expression_text;

typedef struct expression_build
{
    const behavior_asset *asset;
    const graph *graph;
    node *const *selection;
    int selection_count;
    expression_input *inputs;
    expression_term *terms;
    expression_output *outputs;
    node *result_node;
    int result_pin;
    const char *error;
} expression_build;

static bool selected(const expression_build *build, const node *candidate)
{
    for (int i = 0; i < build->selection_count; i++) {
        if (build->selection[i] == candidate) {
            return true;
        }
    }
    return false;
}

static bool numeric_struct(const type_desc *type)
{
    if (!type) {
        return false;
    }
    if (!type->field_count && type->alias_of && strcmp(type->alias_of, type->name)) {
        return numeric_struct(registry_type_by_name(type->alias_of));
    }
    if (type->field_count <= 0) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type != PIN_F32 && field->type != PIN_F64) {
            return false;
        }
    }
    return true;
}

static bool numeric_value(const pin_spec *pin)
{
    if (!pin || pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (pin->type == PIN_F32 || pin->type == PIN_F64) {
        return true;
    }
    return pin->type == PIN_STRUCT &&
           numeric_struct(registry_type_by_name(pin->type_name));
}

static const char *fail(expression_build *build, const char *message)
{
    if (!build->error) {
        build->error = message;
    }
    return build->error;
}

static bool same_text(const char *left, const char *right)
{
    return !strcmp(left ? left : "", right ? right : "");
}

static bool same_binding(
    const pin_binding *left,
    const pin_binding *right,
    const expression_input *input,
    const pin_spec *pin
)
{
    return left && right && left->kind == right->kind &&
           left->variable_id == right->variable_id &&
           left->local_id == right->local_id &&
           left->event_node_id == right->event_node_id &&
           left->event_pin_id == right->event_pin_id &&
           same_text(left->source_asset, right->source_asset) &&
           same_text(left->component_key, right->component_key) &&
           same_text(left->field_name, right->field_name) &&
           input->type == pin->type && input->container == pin->container &&
           same_text(input->type_name, pin->type_name);
}

static bool generic_name(const char *name)
{
    return !name || !name[0] || !strcmp(name, "A") || !strcmp(name, "B") ||
           !strcmp(name, "Value") || !strcmp(name, "Result");
}

static void append_text(expression_text *text, const char *value)
{
    size_t length = strlen(value);
    size_t required = text->length + length + 1;
    if (required > text->capacity) {
        size_t capacity = text->capacity ? text->capacity : 64;
        while (capacity < required) {
            capacity *= 2;
        }
        text->text = xrealloc(text->text, capacity);
        text->capacity = capacity;
    }
    memcpy(text->text + text->length, value, length + 1);
    text->length += length;
}

static char *take_text(expression_text *text)
{
    if (!text->text) {
        return xstrdup("");
    }
    char *result = text->text;
    text->text = NULL;
    text->length = 0;
    text->capacity = 0;
    return result;
}

static bool finite_number(const pin_value *value)
{
    return value && (value->type == PIN_F32 || value->type == PIN_F64) &&
           (value->type == PIN_F32 ? isfinite(value->f) : isfinite(value->d));
}

static char *number_text(const pin_value *value)
{
    char text[64];
    if (value->type == PIN_F32) {
        snprintf(text, sizeof text, "%.9g", (double)value->f);
    } else {
        snprintf(text, sizeof text, "%.17g", value->d);
    }
    return xstrdup(text);
}

static char *identifier(const char *source)
{
    const char *fallback = source && source[0] ? source : "input";
    size_t length = strlen(fallback);
    char *result = xmalloc(length * 2 + 16);
    size_t written = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)fallback[i];
        if (written == 0 && !(isalpha(c) || c == '_')) {
            memcpy(result + written, "input_", 6);
            written += 6;
        }
        result[written++] = (isalnum(c) || c == '_') ? (char)c : '_';
    }
    if (!written) {
        memcpy(result, "input", 6);
        written = 5;
    }
    result[written] = 0;
    return result;
}

static char *input_label(
    const behavior_asset *asset,
    const node *source,
    int source_pin,
    const pin_spec *target,
    const pin_binding *binding
)
{
    char label[256];
    label[0] = 0;
    if (source) {
        const pin_spec *output = &source->output_pins[source_pin];
        const char *name = !generic_name(output->name) ? output->name : target->name;
        snprintf(label, sizeof label, "%s", name && name[0] ? name : "input");
    } else if (binding && binding->kind == PIN_BINDING_COMPONENT_FIELD) {
        snprintf(
            label,
            sizeof label,
            "%s_%s",
            binding->component_key ? binding->component_key : "component",
            binding->field_name ? binding->field_name : "value"
        );
    } else if (binding && binding->kind == PIN_BINDING_COMPONENT) {
        snprintf(label, sizeof label, "%s", binding->component_key ? binding->component_key : "component");
    } else if (binding && binding->kind == PIN_BINDING_DELTA_TIME) {
        snprintf(label, sizeof label, "delta_seconds");
    } else if (binding && binding->kind == PIN_BINDING_EVENT_OUTPUT) {
        snprintf(label, sizeof label, "event_value");
    } else if (binding && binding->kind == PIN_BINDING_VARIABLE) {
        const behavior_variable
            *variable = behavior_asset_find_variable_by_id(asset, binding->variable_id);
        snprintf(label, sizeof label, "%s", variable ? variable->name : "variable");
    } else {
        snprintf(label, sizeof label, "%s", target->name ? target->name : "input");
    }
    return identifier(label);
}

static bool input_matches(
    const expression_input *input,
    const node *source,
    int source_pin,
    const pin_binding *binding,
    const pin_spec *pin
)
{
    if (source) {
        return input->source == source && input->source_pin == source_pin &&
               input->type == source->output_pins[source_pin].type &&
               input->container == source->output_pins[source_pin].container;
    }
    return !input->source && same_binding(input->binding, binding, input, pin);
}

static int add_input(
    expression_build *build,
    node *source,
    int source_pin,
    const pin_binding *binding,
    const pin_spec *pin
)
{
    for (int i = 0; i < acount(build->inputs); i++) {
        expression_input *input = &build->inputs[i];
        if (!input_matches(input, source, source_pin, binding, pin)) {
            continue;
        }
        return i;
    }
    const pin_spec *type_pin = source ? &source->output_pins[source_pin] : pin;
    if (!numeric_value(type_pin)) {
        fail(build, "Convert only numeric calculations to a Behavior Expression.");
        return -1;
    }
    if (binding && binding->kind == PIN_BINDING_LOCAL) {
        fail(build, "Local bindings cannot cross a Behavior Expression boundary.");
        return -1;
    }
    char *base = input_label(build->asset, source, source_pin, pin, binding);
    char *name = base;
    int suffix = 2;
    for (;;) {
        bool duplicate = false;
        for (int i = 0; i < acount(build->inputs); i++) {
            duplicate |= !strcmp(build->inputs[i].name, name);
        }
        if (!duplicate) {
            break;
        }
        if (name != base) {
            free(name);
        }
        name = xmalloc(strlen(base) + 24);
        snprintf(name, strlen(base) + 24, "%s_%d", base, suffix++);
    }
    if (name != base) {
        free(base);
    }
    apush(
        build->inputs,
        ((expression_input){ .source = source,
                             .source_pin = source_pin,
                             .binding = binding,
                             .type = type_pin->type,
                             .type_name = type_pin->type_name,
                             .container = type_pin->container,
                             .name = name })
    );
    return acount(build->inputs) - 1;
}

static int term_index(const expression_build *build, const node *n)
{
    for (int i = 0; i < acount(build->terms); i++) {
        if (build->terms[i].node == n) {
            return i;
        }
    }
    return -1;
}

static const char *binary_operator(const char *name)
{
    if (!strcmp(name, "Add")) {
        return "+";
    }
    if (!strcmp(name, "Subtract")) {
        return "-";
    }
    if (!strcmp(name, "Multiply")) {
        return "*";
    }
    if (!strcmp(name, "Divide")) {
        return "/";
    }
    if (!strcmp(name, "Modulo")) {
        return "%";
    }
    if (!strcmp(name, "Power")) {
        return "^";
    }
    return NULL;
}

static const char *function_name(const char *name)
{
    if (!strcmp(name, "Min")) {
        return "min";
    }
    if (!strcmp(name, "Max")) {
        return "max";
    }
    if (!strcmp(name, "Clamp")) {
        return "clamp";
    }
    if (!strcmp(name, "Negate")) {
        return "negate";
    }
    if (!strcmp(name, "Abs")) {
        return "abs";
    }
    if (!strcmp(name, "Sine")) {
        return "sin";
    }
    if (!strcmp(name, "Cosine")) {
        return "cos";
    }
    if (!strcmp(name, "Tangent")) {
        return "tan";
    }
    if (!strcmp(name, "Arc Sine")) {
        return "asin";
    }
    if (!strcmp(name, "Arc Cosine")) {
        return "acos";
    }
    if (!strcmp(name, "Arc Tangent")) {
        return "atan";
    }
    if (!strcmp(name, "Square Root")) {
        return "sqrt";
    }
    if (!strcmp(name, "Exponential")) {
        return "exp";
    }
    if (!strcmp(name, "Logarithm") || !strcmp(name, "Logarithm 10") ||
        !strcmp(name, "Floor") || !strcmp(name, "Ceiling") || !strcmp(name, "Round") ||
        !strcmp(name, "Power") || !strcmp(name, "Arc Tangent 2")) {
        if (!strcmp(name, "Logarithm")) {
            return "log";
        }
        if (!strcmp(name, "Logarithm 10")) {
            return "log10";
        }
        if (!strcmp(name, "Floor")) {
            return "floor";
        }
        if (!strcmp(name, "Ceiling")) {
            return "ceil";
        }
        if (!strcmp(name, "Round")) {
            return "round";
        }
        if (!strcmp(name, "Power")) {
            return "pow";
        }
        return "atan2";
    }
    return NULL;
}

static char *term_node(expression_build *build, node *n);

static char *term_input(expression_build *build, node *n, int index)
{
    const pin_spec *pin = &n->input_pins[index];
    if (pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE) {
        fail(build, "Select only numeric math inputs.");
        return NULL;
    }
    node *source = NULL;
    int source_pin = -1;
    if (graph_input_source(build->graph, n, index, &source, &source_pin)) {
        if (graph_input_conversion(build->graph, n, index)) {
            fail(build, "Behavior Expression conversion does not preserve converted wires yet.");
            return NULL;
        }
        if (selected(build, source)) {
            if (source_pin != 0) {
                fail(build, "Selected math nodes must expose their first result pin.");
                return NULL;
            }
            return term_node(build, source);
        }
        int input = add_input(build, source, source_pin, NULL, pin);
        return input < 0 ? NULL : xstrdup(build->inputs[input].name);
    }
    if (pin->binding) {
        int input = add_input(build, NULL, -1, pin->binding, pin);
        return input < 0 ? NULL : xstrdup(build->inputs[input].name);
    }
    if (!numeric_value(pin) || !finite_number(&pin->default_value)) {
        fail(build, "Every selected math input needs a numeric value, wire, or binding.");
        return NULL;
    }
    return number_text(&pin->default_value);
}

static char *term_node(expression_build *build, node *n)
{
    int cached = term_index(build, n);
    if (cached >= 0) {
        if (build->terms[cached].visiting) {
            fail(build, "The selected math graph contains a cycle.");
            return NULL;
        }
        return xstrdup(build->terms[cached].source);
    }
    expression_term term = { .node = n, .visiting = true };
    apush(build->terms, term);
    expression_term *stored = &alast(build->terms);
    if (!n || n->disabled || n->output_count != 1 ||
        n->output_pins[0].kind != PIN_DATA || n->output_pins[0].field_source ||
        !numeric_value(&n->output_pins[0]) || !node_desc_is_pure(n->desc)) {
        fail(build, "Select only enabled pure numeric math nodes.");
        return NULL;
    }
    if (!strcmp(n->desc->name, "Real")) {
        if (n->input_count != 1 || !finite_number(&n->input_pins[0].default_value)) {
            fail(build, "Real nodes must contain a finite numeric value.");
            return NULL;
        }
        stored->source = number_text(&n->input_pins[0].default_value);
    } else {
        const char *op = binary_operator(n->desc->name);
        const char *fn = function_name(n->desc->name);
        int expected = op ? 2
                     : fn ? (!strcmp(fn, "clamp") ? 3
                             : !strcmp(fn, "min") || !strcmp(fn, "max") ||
                                     !strcmp(fn, "pow") || !strcmp(fn, "atan2")
                                 ? 2
                                 : 1)
                          : -1;
        if (expected < 0 || n->input_count != expected) {
            fail(build, "That node is not representable by a Behavior Expression yet.");
            return NULL;
        }
        char *arguments[3] = { 0 };
        for (int i = 0; i < expected; i++) {
            arguments[i] = term_input(build, n, i);
            if (!arguments[i]) {
                for (int j = 0; j < expected; j++) {
                    free(arguments[j]);
                }
                return NULL;
            }
        }
        expression_text text = { 0 };
        if (op) {
            append_text(&text, "(");
            append_text(&text, arguments[0]);
            append_text(&text, op);
            append_text(&text, arguments[1]);
            append_text(&text, ")");
        } else {
            append_text(&text, fn);
            append_text(&text, "(");
            for (int i = 0; i < expected; i++) {
                if (i) {
                    append_text(&text, ",");
                }
                append_text(&text, arguments[i]);
            }
            append_text(&text, ")");
        }
        for (int i = 0; i < expected; i++) {
            free(arguments[i]);
        }
        stored->source = take_text(&text);
    }
    stored->visiting = false;
    return xstrdup(stored->source);
}

static bool output_pin_for_edge(const graph *graph, const edge *edge, node **source, int *source_pin)
{
    int output = 0;
    *source = graph_node_by_id(graph, edge->from_id);
    const pin_spec *pin = *source
                            ? graph_pin_by_id(*source, edge->from_pin_id, &output, source_pin)
                            : NULL;
    return pin && output && pin->kind == PIN_DATA;
}

static const char *build_expression(expression_build *build)
{
    if (!build->asset || !build->graph || build->graph->owner != build->asset ||
        build->graph->math_expression) {
        return fail(build, "Choose nodes in an editable behavior graph.");
    }
    if (build->selection_count <= 0 || !build->selection) {
        return fail(build, "Select pure numeric math nodes first.");
    }
    for (int i = 0; i < build->selection_count; i++) {
        node *n = build->selection[i];
        if (!n || graph_node_by_id(build->graph, n->id) != n) {
            return fail(build, "The selection contains a node from another graph.");
        }
        for (int j = 0; j < i; j++) {
            if (build->selection[j] == n) {
                return fail(build, "The selection contains the same node twice.");
            }
        }
        if (n->desc->flow_kind != NODE_FLOW_NONE || n->desc->latent ||
            n->desc->pure_kind == NODE_PURE_READ ||
            n->desc->pure_kind == NODE_PURE_FUNCTION ||
            n->desc->pure_kind == NODE_PURE_GET || n->desc->pure_kind == NODE_PURE_SELF ||
            n->desc->pure_kind == NODE_PURE_DELTA) {
            return fail(build, "Select pure numeric math nodes; state and time reads stay outside.");
        }
    }
    for (int i = 0; i < acount(build->graph->edges); i++) {
        const edge *edge = &build->graph->edges[i];
        if (edge->kind != GRAPH_EDGE_DATA ||
            !selected(build, graph_node_by_id(build->graph, edge->from_id)) ||
            selected(build, graph_node_by_id(build->graph, edge->to_id))) {
            continue;
        }
        node *source = NULL;
        int source_pin = -1;
        if (!output_pin_for_edge(build->graph, edge, &source, &source_pin) ||
            edge->conversion || !numeric_value(&source->output_pins[source_pin])) {
            return fail(build, "Selected math outputs must be numeric, direct data wires.");
        }
        if (build->result_node &&
            (build->result_node != source || build->result_pin != source_pin)) {
            return fail(build, "Select one math result; convert each scalar result separately.");
        }
        node *target = graph_node_by_id(build->graph, edge->to_id);
        int target_output = 0, target_pin = -1;
        const pin_spec *target_spec = target
                                        ? graph_pin_by_id(target, edge->to_pin_id, &target_output, &target_pin)
                                        : NULL;
        if (!target_spec || target_output || target_spec->kind != PIN_DATA ||
            !numeric_value(target_spec)) {
            return fail(build, "The converted result must feed a numeric data input.");
        }
        build->result_node = source;
        build->result_pin = source_pin;
        apush(build->outputs, ((expression_output){ edge->to_id, edge->to_pin_id }));
    }
    if (!build->result_node) {
        node *terminal = NULL;
        for (int i = 0; i < build->selection_count; i++) {
            node *candidate = build->selection[i];
            bool has_selected_consumer = false;
            for (int j = 0; j < acount(build->graph->edges); j++) {
                const edge *edge = &build->graph->edges[j];
                if (edge->kind != GRAPH_EDGE_DATA || edge->from_id != candidate->id) {
                    continue;
                }
                node *target = graph_node_by_id(build->graph, edge->to_id);
                if (selected(build, target)) {
                    has_selected_consumer = true;
                    break;
                }
            }
            if (has_selected_consumer) {
                continue;
            }
            if (terminal) {
                return fail(build, "Select one terminal math result.");
            }
            terminal = candidate;
        }
        if (!terminal) {
            return fail(build, "Select a connected math result.");
        }
        build->result_node = terminal;
        build->result_pin = 0;
    }
    char *source = term_node(build, build->result_node);
    free(source);
    if (build->error) {
        return build->error;
    }
    for (int i = 0; i < build->selection_count; i++) {
        if (term_index(build, build->selection[i]) < 0) {
            return fail(build, "Include every calculation between the inputs and result.");
        }
    }
    return NULL;
}

static void free_build(expression_build *build)
{
    for (int i = 0; i < acount(build->inputs); i++) {
        free(build->inputs[i].name);
    }
    for (int i = 0; i < acount(build->terms); i++) {
        free(build->terms[i].source);
    }
    afree(build->inputs);
    afree(build->terms);
    afree(build->outputs);
}

const char *behavior_expression_can_convert_selection(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count
)
{
    expression_build build = { .asset = asset,
                               .graph = source,
                               .selection = selection,
                               .selection_count = count,
                               .result_pin = -1 };
    const char *error = build_expression(&build);
    free_build(&build);
    return error;
}

static void set_error(behavior_expression_error *error, const char *message)
{
    if (!error) {
        return;
    }
    *error = (behavior_expression_error){ 0 };
    snprintf(error->message, sizeof error->message, "%s", message);
}

static int call_input(const node *call, const char *name)
{
    for (int i = 0; i < call->input_count; i++) {
        if (!strcmp(call->input_pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}

node *behavior_expression_convert_selection(
    behavior_asset *asset,
    graph *source,
    node *const *selection,
    int count,
    behavior_expression_error *error
)
{
    behavior_expression_error local;
    error = error ? error : &local;
    expression_build build = { .asset = asset,
                               .graph = source,
                               .selection = selection,
                               .selection_count = count,
                               .result_pin = -1 };
    if (build_expression(&build)) {
        set_error(error, build.error);
        free_build(&build);
        return NULL;
    }
    char *source_text = term_node(&build, build.result_node);
    node *call = behavior_expression_add(asset, source, source_text, error);
    free(source_text);
    if (!call) {
        free_build(&build);
        return NULL;
    }
    float x = 0.0f, y = 0.0f;
    for (int i = 0; i < build.selection_count; i++) {
        x += build.selection[i]->x;
        y += build.selection[i]->y;
    }
    call->x = x / build.selection_count;
    call->y = y / build.selection_count;
    for (int i = 0; i < acount(build.inputs); i++) {
        expression_input *input = &build.inputs[i];
        int pin = call_input(call, input->name);
        assert(pin >= 0);
        int result = graph_set_pin_type(source, call, 0, pin, input->type, input->type_name);
        assert(result == 0);
        (void)result;
        if (input->binding) {
            result = graph_set_pin_binding(source, call, 0, pin, input->binding);
        } else {
            result = graph_connect_data(source, input->source, input->source_pin, call, pin);
        }
        assert(result == 0);
    }
    if (behavior_expression_refresh(asset, call, error) != 0) {
        free_build(&build);
        return NULL;
    }
    for (int i = 0; i < acount(build.outputs); i++) {
        expression_output *output = &build.outputs[i];
        node *target = graph_node_by_id(source, output->target_id);
        int target_output = 0, target_pin = -1;
        const pin_spec *target_spec = graph_pin_by_id(target, output->target_pin_id, &target_output, &target_pin);
        assert(target_spec && !target_output);
        int result = graph_connect_data(source, call, 0, target, target_pin);
        assert(result == 0);
        (void)result;
    }
    for (int i = 0; i < build.selection_count; i++) {
        node *n = graph_node_by_id(source, build.selection[i]->id);
        if (n) {
            int result = graph_remove_node(source, n);
            assert(result == 0);
            (void)result;
        }
    }
    free_build(&build);
    return call;
}
