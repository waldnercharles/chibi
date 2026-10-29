#include "graph/authoring/graph_edit.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/core_nodes.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/type.h"

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
    int binding; /* how tightly source binds, as in behavior_expression_spelling */
    bool visiting;
} expression_term;

enum
{
    ATOM_BINDING = 100,
};

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
    uint64_t *inlined; /* Unselected component reads spelled in place. */
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
        return numeric_struct(type_catalog_find(type->alias_of));
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
           numeric_struct(type_catalog_find(pin->type_name));
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

/* The shortest text that reads back as the same number, in plain decimals at
   everyday magnitudes: 700, not 7e+02. */
static char *number_text(const pin_value *value)
{
    bool single = value->type == PIN_F32;
    double number = single ? (double)value->f : value->d;
    const char *format = fabs(number) >= 1e-4 && fabs(number) < 1e9 ? "%.*f" : "%.*g";
    char text[64];
    for (int digits = 0; digits <= 24; digits++) {
        snprintf(text, sizeof text, format, digits, number);
        if (single ? strtof(text, NULL) == value->f : strtod(text, NULL) == number) {
            break;
        }
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

/* Use a local, lowercase label for the component. */
static void component_label(char *out, size_t size, const char *key)
{
    const char *name = key ? strrchr(key, '.') : NULL;
    name = name ? name + 1 : key ? key : "component";
    snprintf(out, size, "%s", name);
    out[0] = (char)tolower((unsigned char)out[0]);
}

static bool spellable(const node *n);

/* NULL for a computed value, which takes a placeholder letter. */
static char *input_label(
    const expression_build *build,
    const node *source,
    int source_pin,
    const pin_spec *target,
    const pin_binding *binding
)
{
    char label[256];
    label[0] = 0;
    if (source) {
        /* Named sources keep their name: an output's (Seconds), a
           parameter's or function's (Glow), a built-in's (Shader UV reads
           as UV). A computed value has none of its own. */
        const pin_spec *output = &source->output_pins[source_pin];
        bool computed = source->desc == &NODE_BehaviorExpression || spellable(source);
        const char *builtin = source->desc->name;
        builtin = !strncmp(builtin, "Shader ", 7) ? builtin + 7
                : !strncmp(builtin, "Field ", 6)  ? builtin + 6
                                                  : NULL;
        const char *name = !generic_name(output->name) ? output->name
                         : computed                    ? NULL
                         : source->reference_name && source->reference_name[0]
                             ? source->reference_name
                         : graph_node_name_literal(build->graph, source)
                             ? graph_node_name_literal(build->graph, source)
                             : builtin;
        if (generic_name(name)) {
            return NULL;
        }
        snprintf(label, sizeof label, "%s", name);
    } else if (binding && binding->kind == PIN_BINDING_COMPONENT_FIELD) {
        char component[128];
        component_label(component, sizeof component, binding->component_key);
        snprintf(
            label,
            sizeof label,
            "%s_%s",
            component,
            binding->field_name ? binding->field_name : "value"
        );
    } else if (binding && binding->kind == PIN_BINDING_COMPONENT) {
        component_label(label, sizeof label, binding->component_key);
    } else if (binding && binding->kind == PIN_BINDING_EVENT_OUTPUT) {
        snprintf(label, sizeof label, "event_value");
    } else if (binding && binding->kind == PIN_BINDING_VARIABLE) {
        const behavior_variable *variable = behavior_asset_find_variable_by_id(
            build->asset,
            binding->variable_id
        );
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
    char *base = input_label(build, source, source_pin, pin, binding);
    /* Placeholders run a, b, c, skipping the constant e. */
    for (char letter = 'a'; !base && letter <= 'z'; letter++) {
        char candidate[2] = { letter, 0 };
        bool taken = letter == 'e';
        for (int i = 0; i < acount(build->inputs); i++) {
            taken |= !strcmp(build->inputs[i].name, candidate);
        }
        base = taken ? NULL : xstrdup(candidate);
    }
    base = base ? base : xstrdup("value");
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

static char *term_node(expression_build *build, node *n, int *binding);
static char member_axis(const node_desc *desc);

/* A component read shared with other consumers still reads in place, UV.x,
   rather than as a letter; NULL when source is not one. */
static char *inline_member(expression_build *build, node *source, int source_pin)
{
    node *vector = NULL;
    int vector_pin = -1;
    if (!member_axis(source->desc) || source->disabled ||
        graph_output_local(build->graph, source, source_pin) ||
        !graph_input_source(build->graph, source, 0, &vector, &vector_pin) ||
        graph_input_conversion(build->graph, source, 0)) {
        return NULL;
    }
    int input = add_input(build, vector, vector_pin, NULL, &source->input_pins[0]);
    if (input < 0) {
        return NULL;
    }
    apush(build->inlined, source->id);
    size_t size = strlen(build->inputs[input].name) + 3;
    char *text = xmalloc(size);
    snprintf(text, size, "%s.%c", build->inputs[input].name, member_axis(source->desc));
    return text;
}

/* An operand's source text and how tightly it binds. */
static char *term_input(expression_build *build, node *n, int index, int *binding)
{
    *binding = ATOM_BINDING;
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
            return term_node(build, source, binding);
        }
        char *member = inline_member(build, source, source_pin);
        if (member || build->error) {
            return member;
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
    char *number = number_text(&pin->default_value);
    *binding = number[0] == '-' ? BEHAVIOR_EXPRESSION_PREFIX_BINDING : ATOM_BINDING;
    return number;
}

/* Appends an operand, parenthesized only when it binds looser than its
   operator needs. */
static void append_operand(expression_text *text, const char *source, int binding, int needed)
{
    append_text(text, binding < needed ? "(" : "");
    append_text(text, source);
    append_text(text, binding < needed ? ")" : "");
}

/* Shader X..W read one component, spelled as a member: v.x. */
static char member_axis(const node_desc *desc)
{
    static const char *const AXES[] = { "Shader X", "Shader Y", "Shader Z", "Shader W" };
    for (int i = 0; i < 4; i++) {
        if (!strcmp(desc->name, AXES[i])) {
            return "xyzw"[i];
        }
    }
    return 0;
}

static char *spell_member(expression_build *build, node *n, int *binding)
{
    int operand = 0;
    char *vector = term_input(build, n, 0, &operand);
    if (!vector) {
        return NULL;
    }
    expression_text text = { 0 };
    append_operand(&text, vector, operand, ATOM_BINDING);
    append_text(&text, (char[]){ '.', member_axis(n->desc), 0 });
    free(vector);
    *binding = ATOM_BINDING;
    return take_text(&text);
}

/* Spells n with the shared expression vocabulary: an infix or prefix
   operator, a function call with GLSL argument order, or a member. */
static char *spell_node(expression_build *build, node *n, int *binding)
{
    if (member_axis(n->desc)) {
        return spell_member(build, n, binding);
    }
    int power = 0;
    const char *spelling = behavior_expression_spelling(n->desc, &power);
    int arity = power == BEHAVIOR_EXPRESSION_PREFIX_BINDING ? 1
              : power ? 2
                      : behavior_expression_arity(n->desc);
    if (!spelling || arity < 0 || arity > 8 || n->input_count != arity) {
        fail(build, "That node is not representable by an expression yet.");
        return NULL;
    }
    char *arguments[8] = { 0 };
    int bindings[8] = { 0 };
    bool failed = false;
    for (int i = 0; i < arity && !failed; i++) {
        int input = power ? i : behavior_expression_argument_input(n->desc, i);
        arguments[i] = term_input(build, n, input, &bindings[i]);
        failed = !arguments[i];
    }
    expression_text text = { 0 };
    /* a * -1 reads as -a, which lowers back to the same multiply. */
    int negated = -1;
    if (!failed && power && !strcmp(spelling, "*")) {
        negated = !strcmp(arguments[1], "-1") ? 0
                : !strcmp(arguments[0], "-1") ? 1
                                              : -1;
    }
    if (negated >= 0) {
        power = BEHAVIOR_EXPRESSION_PREFIX_BINDING;
        append_text(&text, "-");
        append_operand(&text, arguments[negated], bindings[negated], power + 1);
    } else if (!failed && power == BEHAVIOR_EXPRESSION_PREFIX_BINDING) {
        append_text(&text, spelling);
        append_operand(&text, arguments[0], bindings[0], power + 1);
    } else if (!failed && power) {
        /* ^ groups to the right; the rest group to the left. a + -b reads
           as a - b, and a - -b as a + b. */
        bool right = !strcmp(spelling, "^");
        const char *operand = arguments[1];
        bool additive = !strcmp(spelling, "+") || !strcmp(spelling, "-");
        if (additive && operand[0] == '-' && bindings[1] > power) {
            spelling = !strcmp(spelling, "+") ? "-" : "+";
            operand++;
        }
        append_operand(&text, arguments[0], bindings[0], power + right);
        append_text(&text, " ");
        append_text(&text, spelling);
        append_text(&text, " ");
        append_operand(&text, operand, bindings[1], power + !right);
    } else if (!failed) {
        append_text(&text, spelling);
        append_text(&text, "(");
        for (int i = 0; i < arity; i++) {
            append_text(&text, i ? ", " : "");
            append_text(&text, arguments[i]);
        }
        append_text(&text, ")");
    }
    for (int i = 0; i < arity; i++) {
        free(arguments[i]);
    }
    *binding = power ? power : ATOM_BINDING;
    char *source = take_text(&text);
    if (failed) {
        free(source);
        return NULL;
    }
    return source;
}

/* Real and Field Constant keep their number on the input, Shader Constant on
   its output; NULL for other nodes. */
static const pin_value *constant_value(const node *n)
{
    const char *name = n->desc->name;
    if (!strcmp(name, "Real") || !strcmp(name, "Field Constant")) {
        return &n->input_pins[0].default_value;
    }
    return !strcmp(name, "Shader Constant") ? &n->output_pins[0].default_value : NULL;
}

static char *term_node(expression_build *build, node *n, int *binding)
{
    int cached = term_index(build, n);
    if (cached >= 0) {
        if (build->terms[cached].visiting) {
            fail(build, "The selected math graph contains a cycle.");
            return NULL;
        }
        *binding = build->terms[cached].binding;
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
    const pin_value *value = constant_value(n);
    if (value) {
        if (!finite_number(value)) {
            fail(build, "Constants must contain a finite number.");
            return NULL;
        }
        stored->source = number_text(value);
        stored->binding = stored->source[0] == '-' ? BEHAVIOR_EXPRESSION_PREFIX_BINDING
                                                   : ATOM_BINDING;
    } else {
        int spelled = 0;
        char *source = spell_node(build, n, &spelled);
        if (!source) {
            return NULL;
        }
        stored = &build->terms[term_index(build, n)];
        stored->source = source;
        stored->binding = spelled;
    }
    stored->visiting = false;
    *binding = stored->binding;
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
            n->desc->pure_kind == NODE_PURE_GET ||
            n->desc->pure_kind == NODE_PURE_SELF) {
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
    int binding = 0;
    char *source = term_node(build, build->result_node, &binding);
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
    afree(build->inlined);
}

static bool has_consumer(const graph *g, const node *n)
{
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].kind == GRAPH_EDGE_DATA && g->edges[i].from_id == n->id) {
            return true;
        }
    }
    return false;
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
    int binding = 0;
    char *source_text = term_node(&build, build.result_node, &binding);
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
        (void)target_spec;
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
    for (int i = 0; i < acount(build.inlined); i++) {
        node *n = graph_node_by_id(source, build.inlined[i]);
        if (n && !has_consumer(source, n)) {
            int result = graph_remove_node(source, n);
            assert(result == 0);
            (void)result;
        }
    }
    free_build(&build);
    return call;
}

/* A math node an expression can spell, as opposed to one that stays at an
   expression's boundary: state, time and random reads, calls and flow. */
static bool spellable(const node *n)
{
    const node_desc *desc = n->desc;
    if (n->disabled || n->output_count != 1 || !numeric_value(&n->output_pins[0]) ||
        !node_desc_is_pure(desc) || desc->flow_kind != NODE_FLOW_NONE || desc->latent ||
        desc->pure_kind == NODE_PURE_READ || desc->pure_kind == NODE_PURE_FUNCTION ||
        desc->pure_kind == NODE_PURE_GET || desc->pure_kind == NODE_PURE_SELF) {
        return false;
    }
    if (constant_value(n)) {
        return true;
    }
    if (member_axis(desc)) {
        return n->input_count == 1;
    }
    int binding = 0;
    const char *spelling = behavior_expression_spelling(desc, &binding);
    int arity = binding == BEHAVIOR_EXPRESSION_PREFIX_BINDING ? 1
              : binding ? 2
                        : behavior_expression_arity(desc);
    return spelling && n->input_count == arity;
}

/* The spellable node a node's only use feeds by a plain wire, or NULL when
   its value is used more than once or leaves the math. */
static node *sole_spellable_consumer(const graph *g, const node *n)
{
    node *consumer = NULL;
    int uses = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_DATA || e->from_id != n->id) {
            continue;
        }
        uses++;
        consumer = e->conversion ? NULL : graph_node_by_id(g, e->to_id);
        int output = 0, pin = -1;
        if (consumer && graph_pin_by_id(consumer, e->to_pin_id, &output, &pin) &&
            graph_input_local(g, consumer, pin)) {
            consumer = NULL;
        }
    }
    return uses == 1 && consumer && spellable(consumer) ? consumer : NULL;
}

static bool same_read(const pin_binding *a, const pin_binding *b)
{
    return a->kind == b->kind && a->variable_id == b->variable_id &&
           a->local_id == b->local_id && a->event_node_id == b->event_node_id &&
           a->event_pin_id == b->event_pin_id &&
           same_text(a->source_asset, b->source_asset) &&
           same_text(a->component_key, b->component_key) &&
           same_text(a->field_name, b->field_name);
}

/* Whether two inputs are fed alike: the same wire, read or value. */
static bool same_feed(const graph *g, const node *a, int pa, const node *b, int pb)
{
    node *from_a = NULL, *from_b = NULL;
    int pin_a = -1, pin_b = -1;
    bool wired_a = graph_input_source(g, a, pa, &from_a, &pin_a),
         wired_b = graph_input_source(g, b, pb, &from_b, &pin_b);
    const pin_binding *read_a = a->input_pins[pa].binding,
                      *read_b = b->input_pins[pb].binding;
    if (wired_a || wired_b) {
        return wired_a && wired_b && from_a == from_b && pin_a == pin_b &&
               graph_input_conversion(g, a, pa) == graph_input_conversion(g, b, pb);
    }
    if (read_a || read_b) {
        return read_a && read_b && same_read(read_a, read_b);
    }
    return pin_value_equal(&a->input_pins[pa].default_value, &b->input_pins[pb].default_value);
}

/* A value expression whose source and inputs repeat an earlier one computes
   the same value: its readers take the earlier result and it goes. */
static bool repeats(const behavior_asset *asset, const graph *g, const node *first, const node *later)
{
    const char *source = behavior_expression_source(asset, first);
    const graph *body = behavior_asset_find_graph(asset, first->reference_id);
    if (later == first || later->desc != &NODE_BehaviorExpression || !source ||
        body->expression_mode != GRAPH_EXPRESSION_VALUE ||
        !same_text(source, behavior_expression_source(asset, later)) ||
        first->input_count != later->input_count ||
        first->output_count != later->output_count) {
        return false;
    }
    for (int p = 0; p < later->output_count; p++) {
        if (graph_output_local(g, later, p)) {
            return false;
        }
    }
    for (int p = 0; p < first->input_count; p++) {
        if (first->input_pins[p].kind != PIN_DATA ||
            !same_feed(g, first, p, later, p)) {
            return false;
        }
    }
    return true;
}

static int merge_repeated_expressions(behavior_asset *asset, graph *g)
{
    int merged = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *first = g->nodes[i];
        for (int j = i + 1;
             first->desc == &NODE_BehaviorExpression && j < acount(g->nodes);
             j++) {
            node *later = g->nodes[j];
            if (!repeats(asset, g, first, later)) {
                continue;
            }
            for (int e = acount(g->edges) - 1; e >= 0; e--) {
                const edge ed = g->edges[e];
                int from_pin = -1, to_pin = -1;
                node *to = graph_node_by_id(g, ed.to_id);
                if (ed.kind != GRAPH_EDGE_DATA || ed.from_id != later->id ||
                    !graph_pin_by_id(later, ed.from_pin_id, NULL, &from_pin) ||
                    !graph_pin_by_id(to, ed.to_pin_id, NULL, &to_pin)) {
                    continue;
                }
                int rc = graph_connect_data_conversion(g, first, from_pin, to, to_pin, ed.conversion);
                assert(rc == 0);
                (void)rc;
            }
            int rc = graph_remove_node(g, later);
            assert(rc == 0);
            (void)rc;
            merged++;
            j--;
        }
    }
    return merged;
}

/* About a line of formula: long enough to read as one idea. */
#define COLLAPSE_OPERATIONS_MAX 12

int behavior_expression_collapse_math(behavior_asset *asset, graph *g, int min_nodes, behavior_expression_error *error)
{
    behavior_expression_error local;
    error = error ? error : &local;
    *error = (behavior_expression_error){ 0 };
    int created = 0;
    /* Each pass converts the first large tree it finds; converting replaces
       nodes, so trees are found again on the changed graph. */
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < acount(g->nodes) && !changed; i++) {
            node *root = g->nodes[i];
            if (!spellable(root) || constant_value(root) ||
                sole_spellable_consumer(g, root)) {
                continue;
            }
            /* Breadth first, so a formula keeps its top and a deep operand
               past the limit becomes a separate expression feeding it. */
            node **tree = NULL;
            apush(tree, root);
            int operations = 1;
            for (int t = 0; t < acount(tree) && operations < COLLAPSE_OPERATIONS_MAX;
                 t++) {
                for (int j = 0;
                     j < acount(g->nodes) && operations < COLLAPSE_OPERATIONS_MAX;
                     j++) {
                    node *n = g->nodes[j];
                    if (spellable(n) && sole_spellable_consumer(g, n) == tree[t]) {
                        apush(tree, n);
                        operations += !constant_value(n);
                    }
                }
            }
            if (operations >= min_nodes &&
                !behavior_expression_can_convert_selection(asset, g, tree, acount(tree))) {
                if (!behavior_expression_convert_selection(asset, g, tree, acount(tree), error)) {
                    afree(tree);
                    return -1;
                }
                created++;
                changed = true;
            }
            afree(tree);
        }
    }
    merge_repeated_expressions(asset, g);
    return created;
}
