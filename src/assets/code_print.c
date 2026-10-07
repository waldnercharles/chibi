#include "assets/code_print.h"

#include "assets/code_calls.h"
#include "assets/code_components.h"
#include "assets/code_program.h"
#include "assets/code_query.h"
#include "assets/code_source.h"
#include "assets/code_value.h"
#include "graph/document/events.h"
#include "graph/document/graph_signature.h"
#include "graph/document/serialize.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/reactive.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/snapshot_nodes.h"
#include "support/asset_json.h"
#include "support/code_lex.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const node *node;
    char *name;
    bool declared, reachable, outlined, emitted, preserve_operation, loop;
    bool printing, printed, result_local;
} symbol;
typedef struct
{
    const node *source;
    int output;
    char *name;
    bool emitted;
} capture_alias;
typedef struct
{
    int group;
    uint64_t pin;
    pin_spec bound;
} generic;
typedef struct
{
    const behavior_asset *asset;
    const graph *graph;
    const node *system_tick;
    bool implicit_tick;
    const node *activation;
    symbol *symbols;
    capture_alias *aliases;
    char *text;
    size_t length, capacity;
    int indent;
    char *error;
    size_t error_size;
    bool failed;
    int depth;
    generic *generics;
    bool use_generics, expression_operand, system;
    av_doc *metadata;
    av_value *anchors;
} printer;

static void write_text(printer *p, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (count < 0) {
        va_end(args);
        p->failed = true;
        return;
    }
    size_t need = p->length + (size_t)count + 1;
    if (need > p->capacity) {
        p->capacity = need * 2;
        p->text = xrealloc(p->text, p->capacity);
    }
    vsnprintf(p->text + p->length, p->capacity - p->length, format, args);
    p->length += (size_t)count;
    va_end(args);
}
static void source_anchor(printer *p, uint64_t id, const char *kind)
{
    if (!p->anchors || !id) {
        return;
    }
    av_value *item = av_arr_add_obj(p->metadata, p->anchors);
    av_obj_add_uint(p->metadata, item, "start", p->length);
    av_obj_add_uint(p->metadata, item, "id", id);
    av_obj_add_str(p->metadata, item, "kind", kind);
}

static void line(printer *p)
{
    for (int i = 0; i < p->indent; i++) {
        write_text(p, "    ");
    }
}
static void quoted(printer *p, const char *value)
{
    av_doc *doc = av_doc_new();
    char *text = code_value_write(av_str(doc, value ? value : ""));
    write_text(p, "%s", text);
    free(text);
    av_doc_free(doc);
}
static void identifier(printer *p, const char *name)
{
    bool plain = name && (isalpha((unsigned char)name[0]) || name[0] == '_');
    for (const char *s = name ? name : ""; *s; s++) {
        plain &= isalnum((unsigned char)*s) || *s == '_';
    }
    if (plain && !code_program_reserved(name)) {
        write_text(p, "%s", name);
        return;
    }
    quoted(p, name);
}
static void symbol_reference(printer *p, const char *name)
{
    bool segment = true, valid = name && *name;
    for (const char *at = name ? name : ""; *at; at++) {
        if (*at == '.') {
            valid &= !segment;
            segment = true;
        } else {
            valid &= segment ? isalpha((unsigned char)*at) || *at == '_'
                             : isalnum((unsigned char)*at) || *at == '_';
            segment = false;
        }
    }
    if (valid && !segment && !code_program_reserved(name)) {
        write_text(p, "%s", name);
        return;
    }
    quoted(p, name);
}

static void argument_reference(printer *p, const char *name)
{
    if (name && strchr(name, '.')) {
        quoted(p, name);
        return;
    }
    bool shadowed = name && behavior_asset_find_variable_by_name(p->asset, name);
    for (int i = 0; name && i < acount(p->graph->locals); i++) {
        shadowed |= !strcmp(p->graph->locals[i].name, name);
    }
    for (int i = 0; name && i < acount(p->symbols); i++) {
        shadowed |= !strcmp(p->symbols[i].name, name);
    }
    const node *entry = graph_node_by_id(p->graph, p->graph->entry_id);
    for (int i = 0; name && entry && i < entry->output_count; i++) {
        shadowed |= !strcmp(entry->output_pins[i].name, name);
    }
    if (shadowed) {
        quoted(p, name);
        return;
    }
    symbol_reference(p, name);
}

static void literal(printer *p, const pin_value *value)
{
    pin_spec port = { .type = value->type,
                      .type_name = value->type_name,
                      .container = value->container };
    int width = pin_vector_width(&port);
    if (p->expression_operand && value->type == PIN_STRUCT && value->ptr &&
        (width == 2 || width == 3)) {
        const type_desc *vector = registry_type_by_name(value->type_name);
        if (vector && vector->field_count == width) {
            write_text(p, "vec%d(", width);
            for (int i = 0; i < width; i++) {
                pin_value field;
                pin_read_field(value->ptr, &vector->fields[i], &field);
                if (i) {
                    write_text(p, ", ");
                }
                literal(p, &field);
            }
            write_text(p, ")");
            return;
        }
    }
    av_doc *doc = av_doc_new();
    char *text = code_value_write(serialize_value(doc, value));
    if (!text) {
        p->failed = true;
        av_doc_free(doc);
        return;
    }
    const char *part = text;
    const char *newline;
    while ((newline = strchr(part, '\n'))) {
        write_text(p, "%.*s\n", (int)(newline - part), part);
        line(p);
        part = newline + 1;
    }
    write_text(p, "%s", part);
    free(text);
    av_doc_free(doc);
}
static void type(printer *p, const pin_spec *port)
{
    if (port->kind == PIN_EXEC) {
        write_text(p, "flow");
        return;
    }
    if (port->container == PIN_CONTAINER_SET) {
        write_text(p, "set<");
    }
    if (port->container == PIN_CONTAINER_MAP) {
        write_text(p, "map<");
    }
    int parameter = -1;
    if (p->use_generics && (port->flags & PIN_FLAG_WILDCARD)) {
        for (int i = 0; i < acount(p->generics); i++) {
            if ((port->group && p->generics[i].group == port->group) ||
                (!port->group && p->generics[i].pin == port->id)) {
                parameter = i;
                break;
            }
        }
    }
    const char *name = port->type == PIN_STR ? "string"
                     : port->type == PIN_F64 ? "number"
                                             : pin_type_name(port->type);
    if (parameter >= 0) {
        write_text(p, "T%d", parameter);
    } else {
        write_text(p, "%s", name ? name : "any");
    }
    if (parameter < 0 && (port->type == PIN_HANDLE ||
                          port->type == PIN_STRUCT || port->type == PIN_ENUM)) {
        write_text(p, "<");
        quoted(p, port->type_name);
        write_text(p, ">");
    }
    if (port->container == PIN_CONTAINER_SET || port->container == PIN_CONTAINER_MAP) {
        write_text(p, ">");
    }
    if (port->container == PIN_CONTAINER_ARRAY) {
        write_text(p, "[]");
    }
}
static void parameters(printer *p, const node *entry, const node *result)
{
    afree(p->generics);
    p->generics = NULL;
    const node *sides[] = { entry, result };
    for (int side = 0; side < 2; side++) {
        if (!sides[side]) {
            continue;
        }
        const pin_spec *ports = side ? sides[side]->input_pins : sides[side]->output_pins;
        int count = side ? sides[side]->input_count : sides[side]->output_count;
        for (int j = 0; j < count; j++) {
            const pin_spec *port = &ports[j];
            if (port->kind != PIN_DATA || port->field_source ||
                !(port->flags & PIN_FLAG_WILDCARD) ||
                !graph_signature_declared(p->graph, port)) {
                continue;
            }
            bool found = false;
            for (int k = 0; k < acount(p->generics); k++) {
                found |= port->group && p->generics[k].group == port->group;
            }
            if (!found) {
                apush(p->generics, ((generic){ .group = port->group, .pin = port->id, .bound = *port }));
            }
        }
    }
    if (!acount(p->generics)) {
        return;
    }
    write_text(p, "<");
    for (int i = 0; i < acount(p->generics); i++) {
        if (i) {
            write_text(p, ", ");
        }
        write_text(p, "T%d: ", i);
        pin_spec bound = p->generics[i].bound;
        bound.container = PIN_CONTAINER_SINGLE;
        type(p, &bound);
    }
    write_text(p, ">");
}
static char *semantic_name(const behavior_asset *asset, const node *n)
{
    const char *original = n->reference_name && n->reference_name[0]
                             ? n->reference_name
                             : n->desc->name;
    if (n->desc == &NODE_BehaviorExpression && strstr(original, "_code_input_")) {
        original = "expression";
    }
    if ((!n->reference_name || !n->reference_name[0]) &&
        (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable)) {
        const behavior_variable *variable = behavior_asset_find_variable_by_id(asset, n->reference_id);
        if (variable) {
            original = variable->name;
        }
    }
    char *name = xmalloc(strlen(original) + 16);
    int length = 0;
    bool separator = false;
    for (const char *s = original; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!isalnum(c) && c != '_') {
            separator = length > 0;
            continue;
        }
        if (separator) {
            name[length++] = '_';
            separator = false;
        }
        if (!length && isdigit(c)) {
            name[length++] = '_';
        }
        name[length++] = (char)tolower(c);
        if (length >= 48) {
            break;
        }
    }
    if (!length) {
        strcpy(name, "value");
        return name;
    }
    name[length] = 0;
    return name;
}
static const char *node_name(const printer *p, const node *n)
{
    if (n->id == p->graph->entry_id) {
        return "entry";
    }
    if (n->id == p->graph->return_id) {
        return "return";
    }
    for (int i = 0; i < acount(p->symbols); i++) {
        if (p->symbols[i].node == n) {
            return p->symbols[i].name;
        }
    }
    return "missing";
}
static symbol *node_symbol(printer *p, const node *n)
{
    for (int i = 0; i < acount(p->symbols); i++) {
        if (p->symbols[i].node == n) {
            return &p->symbols[i];
        }
    }
    return NULL;
}
static int data_uses(const graph *g, const node *n)
{
    int count = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        count += g->edges[i].from_id == n->id && g->edges[i].kind == GRAPH_EDGE_DATA;
    }
    for (int i = 0; i < acount(g->locals); i++) {
        count += g->locals[i].source_id == n->id;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *target = g->nodes[i];
        for (int j = 0; j < target->input_count; j++) {
            const pin_binding *b = target->input_pins[j].binding;
            count += b && b->kind == PIN_BINDING_EVENT_OUTPUT &&
                     b->event_node_id == n->id;
        }
    }
    return count;
}

static const behavior_reactive *reactive_event(const printer *p, const node *n)
{
    for (int i = 0; i < acount(p->asset->reactives); i++) {
        const behavior_reactive *reactive = &p->asset->reactives[i];
        if (reactive->graph_id == p->graph->graph_id && reactive->event_id == n->id) {
            return reactive;
        }
    }
    return NULL;
}

static void change_parameter(const printer *p, char *name, size_t size)
{
    for (int suffix = 0;; suffix++) {
        snprintf(name, size, suffix ? "pressed_%d" : "pressed", suffix);
        bool used = false;
        for (int i = 0; i < acount(p->asset->variables); i++) {
            used |= !strcmp(name, p->asset->variables[i].name);
        }
        for (int i = 0; i < acount(p->symbols); i++) {
            used |= p->symbols[i].declared && !strcmp(name, p->symbols[i].name);
        }
        for (int i = 0; i < acount(p->graph->locals); i++) {
            used |= !strcmp(name, p->graph->locals[i].name);
        }
        for (int i = 0; i < acount(p->asset->component_parameters); i++) {
            used |= !strcmp(name, p->asset->component_parameters[i].name);
        }
        if (!used) {
            return;
        }
    }
}
static bool named_expression(const graph *g, const node *n);
static bool ordinary_expression(const behavior_asset *asset, const graph *target)
{
    if (!target || !target->math_expression) {
        return false;
    }
    if (target->expression_mode == GRAPH_EXPRESSION_ACTION) {
        for (int i = 0; i < acount(asset->graphs); i++) {
            const graph *owner = asset->graphs[i];
            for (int j = 0; j < acount(owner->nodes); j++) {
                const node *call = owner->nodes[j];
                if (call->desc == &NODE_BehaviorExpression && !call->reference_asset &&
                    call->reference_id == target->graph_id && data_uses(owner, call)) {
                    return true;
                }
            }
        }
        return false;
    }
    if (target->expression_mode != GRAPH_EXPRESSION_VALUE ||
        !graph_signature_pure(target)) {
        return false;
    }
    const node *entry = graph_node_by_id(target, target->entry_id);
    for (int i = 0; entry && i < entry->output_count; i++) {
        const pin_spec *field = &entry->output_pins[i];
        if (!field->field_source) {
            continue;
        }
        for (int j = 0; j < entry->output_count; j++) {
            const pin_spec *parent = &entry->output_pins[j];
            if (parent->id == field->field_source && parent->kind == PIN_DATA &&
                parent->type == PIN_STRUCT) {
                return true;
            }
        }
    }
    const node *ret = graph_node_by_id(target, target->return_id);
    bool float_result = false;
    for (int i = 0; ret && i < ret->input_count; i++) {
        float_result |= ret->input_pins[i].kind == PIN_DATA &&
                        ret->input_pins[i].type == PIN_F32;
    }
    for (int i = 0; float_result && i < acount(target->nodes); i++) {
        const node *operation = target->nodes[i];
        if (operation->id == target->entry_id || operation->id == target->return_id) {
            continue;
        }
        for (int pin = 0; pin < operation->output_count; pin++) {
            if (operation->output_pins[pin].kind == PIN_DATA &&
                operation->output_pins[pin].type == PIN_F64 &&
                operation->output_pins[pin].container == PIN_CONTAINER_SINGLE) {
                return true;
            }
        }
    }
    int callers = 0;
    bool named_capture = false;
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *owner = asset->graphs[i];
        for (int j = 0; j < acount(owner->nodes); j++) {
            const node *n = owner->nodes[j];
            if (n->desc != &NODE_BehaviorExpression || n->reference_asset ||
                n->reference_id != target->graph_id) {
                continue;
            }
            callers++;
            named_capture |= named_expression(owner, n);
            for (int pin = 0; pin < n->input_count; pin++) {
                const pin_spec *capture = &n->input_pins[pin];
                const pin_binding *binding = capture->binding;
                if (capture->type != PIN_STRUCT || !capture->type_name ||
                    !binding || binding->kind != PIN_BINDING_COMPONENT) {
                    continue;
                }
                const ecs_component_desc *component = component_by_key(binding->component_key);
                if (!component || !component->type ||
                    !strcmp(capture->type_name, component->type->name)) {
                    continue;
                }
                pin_spec source = { .kind = PIN_DATA,
                                    .type = PIN_STRUCT,
                                    .type_name = component->type->name,
                                    .container = PIN_CONTAINER_SINGLE };
                if (pin_compatible(capture, &source)) {
                    return true;
                }
            }
        }
    }
    if (callers > 1) {
        return true;
    }
    const node *result = graph_node_by_id(target, target->return_id);
    const node *signature[] = { entry, result };
    for (int i = 0; i < acount(target->nodes); i++) {
        const node *constant = target->nodes[i];
        if ((constant->desc->pure_kind != NODE_PURE_CONST &&
             constant->desc->display_class != NODE_DISPLAY_LITERAL) ||
            constant->output_count != 1) {
            continue;
        }
        bool driven = false;
        for (int j = 0; j < constant->input_count; j++) {
            node *producer;
            int output;
            driven |= constant->input_pins[j].binding != NULL ||
                      graph_input_source(target, constant, j, &producer, &output);
        }
        if (driven) {
            continue;
        }
        pin_type width = constant->output_pins[0].type;
        if (width < PIN_I8 || width > PIN_F64) {
            continue;
        }
        for (int side = 0; side < 2; side++) {
            if (!signature[side]) {
                continue;
            }
            const pin_spec *pins = side ? signature[side]->input_pins
                                        : signature[side]->output_pins;
            int count = side ? signature[side]->input_count
                             : signature[side]->output_count;
            for (int j = 0; j < count; j++) {
                if (pins[j].kind != PIN_DATA || pins[j].type < PIN_I8 ||
                    pins[j].type > PIN_F64 || pins[j].type == width) {
                    continue;
                }
                /* A float capture may participate in double arithmetic without
                   changing its capture type or the generated operation types. */
                if (!side && !float_result && width == PIN_F64 &&
                    pins[j].type == PIN_F32) {
                    continue;
                }
                /* Named integer captures promote to a double literal without
                   changing the editable interface or the generated body. */
                if (named_capture && !float_result &&
                    ((!side && width == PIN_F64 && pins[j].type <= PIN_U64) ||
                     (side && width <= PIN_U64 && pins[j].type == PIN_F64))) {
                    continue;
                }
                return true;
            }
        }
    }
    return false;
}
static bool ordinary_expression_call(printer *p, const node *n)
{
    return n->desc == &NODE_BehaviorExpression && !n->reference_asset &&
           ordinary_expression(p->asset, behavior_asset_find_graph(p->asset, n->reference_id));
}
static const pin_spec *computed_type(const node *n)
{
    if (node_behavior_for(n) != NODE_BEHAVIOR_PURE ||
        n->desc == &NODE_BehaviorExpression) {
        return NULL;
    }
    const pin_spec *actual = NULL, *reflected = NULL;
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_DATA) {
            continue;
        }
        if (actual) {
            return NULL;
        }
        actual = &n->output_pins[i];
    }
    for (int i = 0; i < n->desc->output_count; i++) {
        if (n->desc->outputs[i].kind != PIN_DATA) {
            continue;
        }
        if (reflected) {
            return NULL;
        }
        reflected = &n->desc->outputs[i];
    }
    if (actual && reflected && reflected->group > 0 &&
        (reflected->flags & PIN_FLAG_WILDCARD)) {
        for (int i = 0; i < n->desc->input_count && i < n->input_count; i++) {
            const pin_spec *input = &n->desc->inputs[i];
            if (input->kind == PIN_DATA && (input->flags & PIN_FLAG_WILDCARD) &&
                input->group == reflected->group &&
                (n->input_pins[i].flags & PIN_FLAG_WILDCARD) &&
                n->input_pins[i].type == actual->type &&
                n->input_pins[i].container == actual->container) {
                return NULL;
            }
        }
    }
    return actual && reflected &&
                   (actual->type != PIN_FLOW ||
                    actual->container != PIN_CONTAINER_SINGLE) &&
                   (reflected->type == PIN_FLOW || (reflected->flags & PIN_FLAG_WILDCARD))
             ? actual
             : NULL;
}
static bool named_expression(const graph *g, const node *n)
{
    if (n->desc != &NODE_BehaviorExpression) {
        return false;
    }
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *input = &n->input_pins[i];
        bool literal_capture = input->type == PIN_BOOL ||
                               input->type == PIN_F32 || input->type == PIN_F64 ||
                               (input->default_value.ptr && pin_vector_width(input));
        if (input->kind == PIN_DATA &&
            (strncmp(input->name, "_code_input_", strlen("_code_input_")) ||
             (node_behavior_for(n) == NODE_BEHAVIOR_EXEC && !literal_capture &&
              !input->binding && !graph_input_source(g, n, i, NULL, NULL)))) {
            return true;
        }
    }
    return false;
}
static bool branch(const node *n)
{
    if (n->desc == &NODE_Branch) {
        return true;
    }
    if (n->desc != &NODE_BehaviorExpression) {
        return false;
    }
    int yes = 0, no = 0;
    for (int i = 0; i < n->output_count; i++) {
        yes |= n->output_pins[i].kind == PIN_EXEC &&
               !strcmp(n->output_pins[i].name, "True");
        no |= n->output_pins[i].kind == PIN_EXEC &&
              !strcmp(n->output_pins[i].name, "False");
    }
    return yes && no;
}
static bool selection(const node *n)
{
    return n->desc == &NODE_SwitchInt || n->desc == &NODE_SwitchString ||
           n->desc == &NODE_SwitchName || n->desc == &NODE_SwitchEnum;
}
static bool next(printer *p, const node *n, int pin, node **target, int *target_pin)
{
    const edge *edge = NULL;
    int count = graph_exec_output_edges(p->graph, n, pin, &edge);
    if (!count) {
        return false;
    }
    if (count != 1) {
        p->failed = true;
        return false;
    }
    *target = graph_node_by_id(p->graph, edge->to_id);
    if (!*target || !graph_pin_by_id(*target, edge->to_pin_id, NULL, target_pin)) {
        p->failed = true;
        return false;
    }
    return true;
}
static void mark_reachable(printer *p, const node *n)
{
    symbol *s = node_symbol(p, n);
    if (!s || s->reachable) {
        return;
    }
    s->reachable = true;
    for (int i = 0; i < n->output_count; i++) {
        node *target = NULL;
        int port = -1;
        if (n->output_pins[i].kind == PIN_EXEC && next(p, n, i, &target, &port)) {
            mark_reachable(p, target);
        }
    }
}
static bool numeric_boundary(const graph *g, const node *n)
{
    if (n->desc != &NODE_BehaviorExpression || n->output_count != 1 ||
        n->output_pins[0].container != PIN_CONTAINER_SINGLE ||
        (n->output_pins[0].type != PIN_F32 && n->output_pins[0].type != PIN_F64)) {
        return false;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_DATA || e->from_id != n->id) {
            continue;
        }
        const node *consumer = graph_node_by_id(g, e->to_id);
        const pin_spec *input = graph_pin_by_id(consumer, e->to_pin_id, NULL, NULL);
        if (input->container == PIN_CONTAINER_SINGLE &&
            (input->type == PIN_F32 || input->type == PIN_F64) &&
            input->type != n->output_pins[0].type) {
            return true;
        }
    }
    return false;
}
static bool single_call_result(const node *n)
{
    int results = 0, outcomes = 0;
    for (int i = 0; i < n->output_count; i++) {
        const pin_spec *pin = &n->output_pins[i];
        results += pin->kind == PIN_DATA && !pin->field_source;
        if (pin->kind == PIN_EXEC) {
            if (strcmp(pin->name, "Then") && strcmp(pin->name, "Completed")) {
                return false;
            }
            outcomes++;
        }
    }
    return results == 1 && outcomes == 1;
}
static bool linear_call_consumer(printer *p, const node *from, const node *target)
{
    for (int depth = 0; from && depth < acount(p->symbols); depth++) {
        if (from == target) {
            return true;
        }
        const node *following = NULL;
        int outcomes = 0;
        for (int i = 0; i < from->output_count; i++) {
            if (from->output_pins[i].kind != PIN_EXEC) {
                continue;
            }
            outcomes++;
            node *child = NULL;
            int port;
            if (next(p, from, i, &child, &port)) {
                following = child;
            }
        }
        if (outcomes != 1) {
            return false;
        }
        from = following;
    }
    return false;
}
static bool call_result_consumers(printer *p, const node *producer, const node *completion, int depth)
{
    if (depth >= acount(p->symbols)) {
        return false;
    }
    for (int i = 0; i < acount(p->graph->edges); i++) {
        const edge *wire = &p->graph->edges[i];
        if (wire->kind != GRAPH_EDGE_DATA || wire->from_id != producer->id) {
            continue;
        }
        const node *consumer = graph_node_by_id(p->graph, wire->to_id);
        if (!consumer || consumer->id == p->graph->return_id) {
            return false;
        }
        const symbol *consumer_symbol = node_symbol(p, consumer);
        if (consumer_symbol && consumer_symbol->declared) {
            return false;
        }
        if (node_behavior_for(consumer) == NODE_BEHAVIOR_PURE) {
            const symbol *value = node_symbol(p, consumer);
            if ((value && value->declared) ||
                !call_result_consumers(p, consumer, completion, depth + 1)) {
                return false;
            }
        } else if (!linear_call_consumer(p, completion, consumer)) {
            return false;
        }
    }
    return true;
}
static bool local_call_result(printer *p, const node *n)
{
    if (!single_call_result(n) ||
        (n->desc == &NODE_BehaviorExpression && !ordinary_expression_call(p, n))) {
        return false;
    }
    for (int i = 0; i < acount(p->graph->locals); i++) {
        if (p->graph->locals[i].source_id == n->id && n->desc != &NODE_Snapshot) {
            return false;
        }
    }
    for (int i = 0; i < acount(p->graph->nodes); i++) {
        const node *consumer = p->graph->nodes[i];
        for (int j = 0; j < consumer->input_count; j++) {
            const pin_binding *binding = consumer->input_pins[j].binding;
            if (binding && binding->kind == PIN_BINDING_EVENT_OUTPUT &&
                binding->event_node_id == n->id) {
                return false;
            }
        }
    }
    for (int i = 0; i < n->output_count; i++) {
        node *completion = NULL;
        int input;
        if (n->output_pins[i].kind == PIN_EXEC && next(p, n, i, &completion, &input)) {
            return call_result_consumers(p, n, completion, 0);
        }
    }
    return false;
}
static void plan(printer *p)
{
    for (int i = 0; i < acount(p->symbols); i++) {
        const node *n = p->symbols[i].node;
        if (node_behavior_for(n) == NODE_BEHAVIOR_ENTRY) {
            mark_reachable(p, n);
        }
    }
    for (int i = 0; i < acount(p->symbols); i++) {
        symbol *s = &p->symbols[i];
        const node *n = s->node;
        int incoming = 0, outgoing = 0, uses = data_uses(p->graph, n);
        for (int j = 0; j < n->input_count; j++) {
            if (n->input_pins[j].kind == PIN_EXEC) {
                incoming++;
            }
        }
        for (int j = 0; j < n->output_count; j++) {
            outgoing += n->output_pins[j].kind == PIN_EXEC;
        }
        int predecessors = 0;
        for (int j = 0; j < acount(p->graph->edges); j++) {
            predecessors += p->graph->edges[j].kind == GRAPH_EDGE_EXEC &&
                            p->graph->edges[j].to_id == n->id;
        }
        if (node_behavior_for(n) == NODE_BEHAVIOR_PURE) {
            int results = n->output_count;
            if (component_node_kind_of(n->desc) == COMPONENT_NODE_GET ||
                component_generated_kind(n->desc) == COMPONENT_NODE_GET) {
                results = 0;
                for (int j = 0; j < n->output_count; j++) {
                    results += n->output_pins[j].kind == PIN_DATA &&
                               !n->output_pins[j].field_source;
                }
            }
            bool component_get = component_node_component(n) &&
                                 (component_node_kind_of(n->desc) == COMPONENT_NODE_GET ||
                                  component_generated_kind(n->desc) == COMPONENT_NODE_GET);
            s->declared = uses != 1 || results != 1 || n->disabled ||
                          n->desc->instance_state ||
                          n->presentation != NODE_PRESENTATION_AUTO || n->breakpoint ||
                          n->comment || (!component_get && computed_type(n)) ||
                          numeric_boundary(p->graph, n);
            for (int j = 0; j < acount(p->graph->locals); j++) {
                s->declared |= p->graph->locals[j].source_id == n->id;
            }
            continue;
        }
        s->result_local = uses && s->reachable && incoming == 1 &&
                          predecessors == 1 && local_call_result(p, n) &&
                          !n->disabled;
        s->declared = !s->reachable || incoming != 1 || predecessors != 1 ||
                      (uses && !s->result_local) || n->disabled ||
                      (outgoing > 1 && !branch(n) && !selection(n) &&
                       n->desc != &NODE_Sequence);
        if (outgoing == 1) {
            for (int j = 0; j < n->output_count; j++) {
                const pin_spec *port = &n->output_pins[j];
                node *target = NULL;
                int target_port = -1;
                if (port->kind == PIN_EXEC && strcmp(port->name, "Then") &&
                    strcmp(port->name, "Completed") &&
                    next(p, n, j, &target, &target_port)) {
                    s->declared = true;
                }
            }
        }
        if (selection(n) && s->reachable && incoming == 1 && predecessors > 1 &&
            !uses && !n->disabled) {
            s->outlined = true;
            s->declared = false;
        }
    }
}
static void preserve_operations(printer *p, const node *n)
{
    symbol *s = node_symbol(p, n);
    if (!s || s->preserve_operation || n->desc == &NODE_BehaviorExpression ||
        component_node_component(n) || node_behavior_for(n) != NODE_BEHAVIOR_PURE) {
        return;
    }
    s->preserve_operation = true;
    s->declared = true;
    for (int i = 0; i < n->input_count; i++) {
        node *source = NULL;
        int port = -1;
        if (n->input_pins[i].kind == PIN_DATA &&
            graph_input_source(p->graph, n, i, &source, &port)) {
            preserve_operations(p, source);
        }
    }
}
static bool reachable_without(
    printer *p,
    const node *from,
    const node *target,
    const node *blocked,
    const node ***seen
)
{
    if (!from || from == blocked) {
        return false;
    }
    if (from == target) {
        return true;
    }
    for (int i = 0; i < acount(*seen); i++) {
        if ((*seen)[i] == from) {
            return false;
        }
    }
    apush(*seen, from);
    for (int i = 0; i < from->output_count; i++) {
        node *child = NULL;
        int port = -1;
        if (from->output_pins[i].kind == PIN_EXEC && next(p, from, i, &child, &port) &&
            reachable_without(p, child, target, blocked, seen)) {
            return true;
        }
    }
    return false;
}
static bool loop_consumer(
    printer *p,
    const node *loop,
    const node *consumer,
    const node *body,
    const node *completed,
    int depth
)
{
    symbol *s = node_symbol(p, consumer);
    if (depth >= acount(p->symbols) || !s || s->declared || consumer == loop) {
        return false;
    }
    if (node_behavior_for(consumer) != NODE_BEHAVIOR_PURE) {
        const node **seen = NULL;
        bool inside = reachable_without(p, body, consumer, loop, &seen);
        afree(seen);
        seen = NULL;
        bool outside = reachable_without(p, completed, consumer, loop, &seen);
        afree(seen);
        return inside && !outside;
    }
    for (int i = 0; i < acount(p->graph->edges); i++) {
        const edge *edge = &p->graph->edges[i];
        if (edge->kind == GRAPH_EDGE_DATA && edge->from_id == consumer->id &&
            !loop_consumer(p, loop, graph_node_by_id(p->graph, edge->to_id), body, completed, depth + 1)) {
            return false;
        }
    }
    return true;
}
static bool simple_loop(printer *p, const node *n)
{
    if (n->desc != &NODE_ForLoop && n->desc != &NODE_ForEach &&
        n->desc != &NODE_WhileLoop) {
        return false;
    }
    int predecessors = 0;
    for (int i = 0; i < acount(p->graph->edges); i++) {
        predecessors += p->graph->edges[i].kind == GRAPH_EDGE_EXEC &&
                        p->graph->edges[i].to_id == n->id;
    }
    if (predecessors != 1 || n->disabled) {
        return false;
    }
    if (n->desc == &NODE_ForEach && !n->input_pins[1].binding) {
        node *array = NULL;
        int output = -1;
        if (!graph_input_source(p->graph, n, 1, &array, &output)) {
            return false;
        }
    }
    node *body = NULL, *completed = NULL;
    int port = -1;
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_EXEC) {
            continue;
        }
        if (!strcmp(n->output_pins[i].name, "Body")) {
            next(p, n, i, &body, &port);
        } else if (!strcmp(n->output_pins[i].name, "Completed")) {
            next(p, n, i, &completed, &port);
        }
    }
    for (int i = 0; i < acount(p->graph->locals); i++) {
        if (p->graph->locals[i].source_id == n->id) {
            return false;
        }
    }
    for (int i = 0; i < acount(p->graph->edges); i++) {
        const edge *edge = &p->graph->edges[i];
        if (edge->kind != GRAPH_EDGE_DATA || edge->from_id != n->id) {
            continue;
        }
        if (n->desc == &NODE_ForEach && edge->from_pin_id == n->output_pins[2].id) {
            return false;
        }
        if (!loop_consumer(p, n, graph_node_by_id(p->graph, edge->to_id), body, completed, 0)) {
            return false;
        }
    }
    for (int i = 0; i < acount(p->graph->nodes); i++) {
        const node *consumer = p->graph->nodes[i];
        for (int j = 0; j < consumer->input_count; j++) {
            const pin_binding *binding = consumer->input_pins[j].binding;
            if (binding && binding->kind == PIN_BINDING_EVENT_OUTPUT &&
                binding->event_node_id == n->id &&
                ((n->desc == &NODE_ForEach &&
                  binding->event_pin_id == n->output_pins[2].id) ||
                 !loop_consumer(p, n, consumer, body, completed, 0))) {
                return false;
            }
        }
    }
    return true;
}
static int expression_uses(printer *p, const node *n, const pin_spec *port)
{
    const char *source = behavior_expression_source(p->asset, n);
    if (!source) {
        return 0;
    }
    code_lexer lex;
    code_lex_init(&lex, source);
    int count = 0;
    for (;;) {
        code_token token = code_lex_next(&lex);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            return count;
        }
        count += token.kind == CODE_TOKEN_NAME &&
                 strlen(port->name) == token.end - token.start &&
                 !strncmp(port->name, source + token.start, token.end - token.start);
    }
}
static bool explicit_component_owner(const graph *g, const node *n)
{
    if (n->desc != &NODE_Self) {
        return false;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *wire = &g->edges[i];
        if (wire->kind != GRAPH_EDGE_DATA || wire->from_id != n->id) {
            continue;
        }
        const node *target = graph_node_by_id(g, wire->to_id);
        const pin_spec *input = graph_pin_by_id(target, wire->to_pin_id, NULL, NULL);
        if (component_node_component(target) && !strcmp(input->name, "Entity")) {
            return true;
        }
    }
    return false;
}

static void symbols(printer *p)
{
    for (int i = 0; i < acount(p->aliases); i++) {
        free(p->aliases[i].name);
    }
    afree(p->aliases);
    for (int i = 0; i < acount(p->symbols); i++) {
        free(p->symbols[i].name);
    }
    afree(p->symbols);
    p->symbols = NULL;
    for (int i = 0; i < acount(p->graph->nodes); i++) {
        const node *n = p->graph->nodes[i];
        char *base = explicit_component_owner(p->graph, n) && !n->reference_name
                       ? xstrdup("owner")
                       : semantic_name(p->asset, n);
        if (n->desc == &NODE_Snapshot) {
            for (int j = 0; j < acount(p->graph->locals); j++) {
                if (p->graph->locals[j].source_id == n->id) {
                    free(base);
                    base = xstrdup(p->graph->locals[j].name);
                    break;
                }
            }
        }
        char *name = xstrdup(base);
        for (int suffix = 2;; suffix++) {
            bool used = code_program_reserved(name) || !strcmp(name, "entry") ||
                        !strcmp(name, "return") || !strcmp(name, "self") ||
                        !strcmp(name, "state") || !strcmp(name, "local") ||
                        !strcmp(name, "component") || !strcmp(name, "delta");
            for (int j = 0; j < acount(p->symbols); j++) {
                used |= !strcmp(name, p->symbols[j].name);
            }
            for (int j = 0; j < acount(p->asset->variables); j++) {
                used |= !strcmp(name, p->asset->variables[j].name);
            }
            for (int j = 0; j < acount(p->asset->graphs); j++) {
                const char *callable = p->asset->graphs[j]->name;
                used |= callable && !strcmp(name, callable);
            }
            for (int j = 0; j < acount(p->graph->locals); j++) {
                used |= !(n->desc == &NODE_Snapshot &&
                          p->graph->locals[j].source_id == n->id) &&
                        !strcmp(name, p->graph->locals[j].name);
            }
            if (!used) {
                break;
            }
            free(name);
            name = xmalloc(strlen(base) + 32);
            snprintf(name, strlen(base) + 32, "%s_%d", base, suffix);
        }
        apush(p->symbols, ((symbol){ .node = n, .name = name }));
        free(base);
    }
    plan(p);
    for (int i = 0; i < acount(p->symbols); i++) {
        const node *n = p->symbols[i].node;
        if (p->graph->kind == GRAPH_FUNCTION && graph_signature_pure(p->graph) &&
            node_behavior_for(n) == NODE_BEHAVIOR_PURE &&
            n->desc != &NODE_BehaviorExpression) {
            p->symbols[i].declared = true;
        }
        if (node_behavior_for(n) == NODE_BEHAVIOR_PURE &&
            n->desc != &NODE_BehaviorExpression) {
            for (int j = 0; j < acount(p->symbols); j++) {
                if (j != i && p->symbols[j].node->desc == n->desc &&
                    node_behavior_for(p->symbols[j].node) == NODE_BEHAVIOR_PURE) {
                    const ecs_component_desc *component = component_node_component(n);
                    if (component &&
                        (component != component_node_component(p->symbols[j].node) ||
                         (data_uses(p->graph, n) == 1 && !p->symbols[i].declared))) {
                        continue;
                    }
                    p->symbols[i].declared = true;
                    break;
                }
            }
        }
        if (n->desc != &NODE_BehaviorExpression) {
            continue;
        }
        for (int j = 0; j < n->input_count; j++) {
            node *source = NULL;
            int output = -1;
            if (n->input_pins[j].kind != PIN_DATA ||
                !graph_input_source(p->graph, n, j, &source, &output) ||
                node_behavior_for(source) != NODE_BEHAVIOR_PURE) {
                continue;
            }
            if (source->desc == &NODE_BehaviorExpression ||
                expression_uses(p, n, &n->input_pins[j]) > 1) {
                node_symbol(p, source)->declared = true;
            }
        }
    }
    for (int i = 0; i < acount(p->symbols); i++) {
        const node *n = p->symbols[i].node;
        node *condition = NULL;
        int port = -1;
        if (n->desc == &NODE_Branch &&
            graph_input_source(p->graph, n, 1, &condition, &port)) {
            symbol *s = node_symbol(p, condition);
            if (s && node_behavior_for(condition) == NODE_BEHAVIOR_PURE) {
                s->declared = true;
            }
        }
    }
    for (int i = 0; i < acount(p->symbols); i++) {
        if (p->symbols[i].reachable && simple_loop(p, p->symbols[i].node)) {
            p->symbols[i].loop = true;
            p->symbols[i].declared = false;
        }
    }
    bool labels = false;
    for (int i = 0; i < acount(p->symbols); i++) {
        labels |= p->symbols[i].outlined;
    }
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < acount(p->symbols); i++) {
            symbol *value = &p->symbols[i];
            if (value->result_local && (labels || !local_call_result(p, value->node))) {
                value->result_local = false;
                value->declared = true;
                changed = true;
            }
        }
    } while (changed);
    for (int i = 0; i < acount(p->symbols); i++) {
        if (p->symbols[i].declared) {
            preserve_operations(p, p->symbols[i].node);
        }
    }
    for (int i = 0; i < acount(p->graph->nodes); i++) {
        const node *n = p->graph->nodes[i];
        if (n->desc != &NODE_BehaviorExpression) {
            continue;
        }
        for (int j = 0; j < n->input_count; j++) {
            node *source = NULL;
            int output = -1;
            if (n->input_pins[j].binding ||
                !graph_input_source(p->graph, n, j, &source, &output) ||
                component_node_component(source) ||
                node_behavior_for(source) != NODE_BEHAVIOR_PURE ||
                !source->output_pins[output].field_source) {
                continue;
            }
            bool exists = false;
            for (int k = 0; k < acount(p->aliases); k++) {
                exists |= p->aliases[k].source == source &&
                          p->aliases[k].output == output;
            }
            if (exists) {
                continue;
            }
            const pin_spec *field = &source->output_pins[output];
            node hint = *source;
            hint.reference_name = xstrdup(
                field->field_name ? field->field_name : field->name
            );
            char *base = semantic_name(p->asset, &hint);
            free(hint.reference_name);
            char *name = xstrdup(base);
            for (int suffix = 2;; suffix++) {
                bool used = code_program_reserved(name) ||
                            !strcmp(name, "component") || !strcmp(name, "delta");
                for (int k = 0; k < acount(p->symbols); k++) {
                    used |= !strcmp(name, p->symbols[k].name);
                }
                for (int k = 0; k < acount(p->aliases); k++) {
                    used |= !strcmp(name, p->aliases[k].name);
                }
                for (int k = 0; k < acount(p->asset->variables); k++) {
                    used |= !strcmp(name, p->asset->variables[k].name);
                }
                for (int k = 0; k < acount(p->graph->locals); k++) {
                    used |= !strcmp(name, p->graph->locals[k].name);
                }
                if (!used) {
                    break;
                }
                free(name);
                name = xmalloc(strlen(base) + 32);
                snprintf(name, strlen(base) + 32, "%s_%d", base, suffix);
            }
            free(base);
            node_symbol(p, source)->declared = true;
            apush(p->aliases, ((capture_alias){ .source = source, .output = output, .name = name }));
        }
    }
}
static void member_name(printer *p, const node *n, int index)
{
    const pin_spec *port = &n->output_pins[index];
    if (port->field_source) {
        int parent = -1;
        if (!graph_pin_by_id(n, port->field_source, NULL, &parent) || parent == index) {
            p->failed = true;
            return;
        }
        member_name(p, n, parent);
        write_text(p, ".");
        identifier(p, port->field_name ? port->field_name : port->name);
        return;
    }
    identifier(p, port->name);
}
static void component_field_output(printer *p, const node *n, int index, const type_desc *root);
static void output(printer *p, const node *n, int index)
{
    const behavior_reactive *reactive = reactive_event(p, n);
    if (reactive && reactive->kind == BEHAVIOR_REACTIVE_PRESSED_CHANGED &&
        n->output_pins[index].kind == PIN_DATA && p->activation == n) {
        char name[64];
        change_parameter(p, name, sizeof name);
        write_text(p, "%s", name);
        return;
    }
    if (n->desc == &NODE_Event &&
        graph_host_event_is(n->reference_name, GRAPH_HOST_EVENT_TICK) &&
        !strcmp(n->output_pins[index].name, "DeltaSeconds") &&
        (p->activation == n ||
         (p->implicit_tick && n == p->system_tick && p->activation == NULL))) {
        write_text(p, "dt");
        return;
    }
    if (n->desc == &NODE_Event &&
        graph_host_event_is(n->reference_name, GRAPH_HOST_EVENT_TICK) &&
        !strcmp(n->output_pins[index].name, "DeltaSeconds")) {
        write_text(p, "%s.dt", node_name(p, n));
        return;
    }
    symbol *s = node_symbol(p, n);
    const ecs_component_desc *component = component_node_component(n);
    if (component && (component_node_kind_of(n->desc) == COMPONENT_NODE_GET ||
                      component_generated_kind(n->desc) == COMPONENT_NODE_GET)) {
        write_text(p, "%s", node_name(p, n));
        component_field_output(p, n, index, component->type);
        return;
    }
    if (s && (s->loop || s->result_local) && n->output_pins[index].kind == PIN_DATA) {
        const pin_spec *port = &n->output_pins[index];
        if (port->field_source) {
            int parent = -1;
            if (!graph_pin_by_id(n, port->field_source, NULL, &parent)) {
                p->failed = true;
                return;
            }
            output(p, n, parent);
            write_text(p, ".");
            identifier(p, port->field_name ? port->field_name : port->name);
            return;
        }
        write_text(p, "%s", node_name(p, n));
        return;
    }
    if (node_behavior_for(n) == NODE_BEHAVIOR_PURE && n->output_count == 1) {
        write_text(p, "%s", node_name(p, n));
        return;
    }
    write_text(p, "%s.", node_name(p, n));
    member_name(p, n, index);
}
static void field_path(printer *p, const type_desc *type, const char *path)
{
    while (path && *path) {
        const char *end = strchr(path, '.');
        size_t length = end ? (size_t)(end - path) : strlen(path);
        const type_field *field = NULL;
        for (int i = 0; type && i < type->field_count; i++) {
            if (strlen(type->fields[i].name) == length &&
                !strncmp(type->fields[i].name, path, length)) {
                field = &type->fields[i];
                break;
            }
        }
        if (!field) {
            p->failed = true;
            return;
        }
        char name[128];
        code_component_field_spelling(field, name, sizeof name);
        write_text(p, ".%s", name);
        type = field->type == PIN_STRUCT ? registry_type_by_name(field->type_name)
                                         : NULL;
        path = end ? end + 1 : NULL;
    }
}

static void binding(printer *p, const pin_binding *b)
{
    switch (b->kind) {
        case PIN_BINDING_DELTA_TIME: write_text(p, "&delta"); return;
        case PIN_BINDING_VARIABLE: {
            const behavior_variable *v = behavior_asset_find_variable_by_id(p->asset, b->variable_id);
            const char *name = v ? v->name : "missing";
            bool plain = isalpha((unsigned char)name[0]) || name[0] == '_';
            for (const char *at = name; *at; at++) {
                plain &= isalnum((unsigned char)*at) || *at == '_';
            }
            if (!plain || code_program_reserved(name)) {
                write_text(p, "&state.");
            }
            identifier(p, v ? v->name : "missing");
            return;
        }
        case PIN_BINDING_LOCAL: {
            const graph_local *local = graph_local_by_id(p->graph, b->local_id);
            write_text(p, "&local.");
            identifier(p, local ? local->name : "missing");
            return;
        }
        case PIN_BINDING_EVENT_OUTPUT: {
            const node *event = graph_node_by_id(p->graph, b->event_node_id);
            int index = 0;
            if (!event || !graph_pin_by_id(event, b->event_pin_id, NULL, &index)) {
                p->failed = true;
                return;
            }
            output(p, event, index);
            return;
        }
        case PIN_BINDING_COMPONENT:
        case PIN_BINDING_COMPONENT_FIELD: {
            const ecs_component_desc *component = component_by_key(b->component_key);
            if (!component) {
                p->failed = true;
                return;
            }
            write_text(p, "self.%s", code_component_spelling(component));
            if (b->field_name) {
                field_path(p, component->type, b->field_name);
            }
            return;
        }
    }
}
static void print_fallback_binding(printer *p, const pin_binding *b)
{
    switch (b->kind) {
        case PIN_BINDING_DELTA_TIME: write_text(p, "&delta"); return;
        case PIN_BINDING_VARIABLE: {
            const behavior_variable *v = behavior_asset_find_variable_by_id(p->asset, b->variable_id);
            write_text(p, "&state.");
            identifier(p, v ? v->name : "missing");
            return;
        }
        case PIN_BINDING_COMPONENT:
        case PIN_BINDING_COMPONENT_FIELD:
            write_text(p, "&component(");
            quoted(p, b->component_key);
            if (b->field_name) {
                write_text(p, ", ");
                quoted(p, b->field_name);
            }
            write_text(p, ")");
            return;
        case PIN_BINDING_EVENT_OUTPUT:
            write_text(p, "&");
            binding(p, b);
            return;
        case PIN_BINDING_LOCAL: binding(p, b); return;
    }
}
static void input(printer *p, const node *n, int index);
static void callable(printer *p, const node *n);
static void signature(printer *p, const node *n, bool result);
static void call_arguments(printer *p, const node *n)
{
    bool previous_operand = p->expression_operand;
    p->expression_operand = false;
    bool positional = false;
    bool authored_call = n->desc == &NODE_CallFunction ||
                         n->desc == &NODE_CallPureFunction ||
                         n->desc == &NODE_CallMacro ||
                         ordinary_expression_call(p, n);
    for (int i = 0; !authored_call && i < n->desc->input_count; i++) {
        const pin_spec *a = &n->desc->inputs[i];
        if (a->kind != PIN_DATA) {
            continue;
        }
        for (int j = i + 1; j < n->desc->input_count; j++) {
            const pin_spec *b = &n->desc->inputs[j];
            positional |= b->kind == PIN_DATA && !strcmp(a->name, b->name);
        }
    }
    write_text(p, "(");
    int emitted = 0;
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *port = &n->input_pins[i];
        if (port->kind == PIN_EXEC) {
            continue;
        }
        if (emitted++) {
            write_text(p, ", ");
        }
        const pin_spec *base = serialize_pin_template(n->desc, false, i);
        bool typed = (port->flags & PIN_FLAG_WILDCARD) ||
                     (!authored_call &&
                      ((base->flags & PIN_FLAG_WILDCARD) || base->type == PIN_FLOW ||
                       port->type != base->type || port->container != base->container ||
                       ((port->type == PIN_STRUCT || port->type == PIN_ENUM ||
                         port->type == PIN_HANDLE) &&
                        strcmp(
                            port->type_name ? port->type_name : "",
                            base->type_name ? base->type_name : ""
                        ))));
        if (positional) {
            bool scalar_cast = typed && port->container == PIN_CONTAINER_SINGLE &&
                               port->type >= PIN_I8 && port->type <= PIN_F64;
            if (scalar_cast) {
                write_text(p, "cast<");
                type(p, port);
                write_text(p, ">(");
            }
            input(p, n, i);
            if (scalar_cast) {
                write_text(p, ")");
            }
            continue;
        }
        bool split_field = component_node_kind_of(n->desc) == COMPONENT_NODE_SET &&
                           component_node_split(n) && i >= n->desc->input_count - 1;
        if (split_field) {
            write_text(p, "Value.");
        }
        bool template_input = !authored_call && !split_field &&
                              i < n->desc->input_count && port->kind == base->kind &&
                              !((port->flags ^ base->flags) & PIN_FLAG_SETTING) &&
                              (!component_node_kind_of(n->desc) ||
                               !strcmp(port->name, base->name));
        identifier(p, template_input ? base->name : port->name);
        write_text(p, ": ");
        if (typed) {
            if (!authored_call && !port->flags && (base->flags & PIN_FLAG_WILDCARD)) {
                write_text(p, "plain ");
            }
            type(p, port);
            write_text(p, " = ");
        }
        input(p, n, i);
    }
    write_text(p, ")");
    p->expression_operand = previous_operand;
}
static pin_type expression_source_type(printer *p, const node *n, int index)
{
    node *source = NULL;
    int output = -1;
    if (graph_input_source(p->graph, n, index, &source, &output)) {
        const node_desc *conversion = graph_input_conversion(p->graph, n, index);
        if (conversion) {
            const pin_spec *result = NULL;
            for (int i = 0; i < conversion->output_count; i++) {
                if (conversion->outputs[i].kind != PIN_DATA) {
                    continue;
                }
                if (result) {
                    return PIN_FLOW;
                }
                result = &conversion->outputs[i];
            }
            return result && result->container == PIN_CONTAINER_SINGLE ? result->type
                                                                       : PIN_FLOW;
        }
        return source->output_pins[output].container == PIN_CONTAINER_SINGLE
                 ? source->output_pins[output].type
                 : PIN_FLOW;
    }
    const pin_binding *b = n->input_pins[index].binding;
    if (!b) {
        return PIN_FLOW;
    }
    switch (b->kind) {
        case PIN_BINDING_VARIABLE: {
            const behavior_variable *v = behavior_asset_find_variable_by_id(p->asset, b->variable_id);
            return v && v->container == PIN_CONTAINER_SINGLE ? v->type : PIN_FLOW;
        }
        case PIN_BINDING_DELTA_TIME: return PIN_F32;
        case PIN_BINDING_LOCAL: {
            const graph_local *local = graph_local_by_id(p->graph, b->local_id);
            source = local ? graph_node_by_id(p->graph, local->source_id) : NULL;
            const pin_spec *port = source
                                     ? graph_pin_by_id(source, local->source_pin_id, NULL, NULL)
                                     : NULL;
            return port && port->container == PIN_CONTAINER_SINGLE ? port->type
                                                                   : PIN_FLOW;
        }
        case PIN_BINDING_EVENT_OUTPUT: {
            source = graph_node_by_id(p->graph, b->event_node_id);
            const pin_spec *port = source
                                     ? graph_pin_by_id(source, b->event_pin_id, NULL, NULL)
                                     : NULL;
            return port && port->container == PIN_CONTAINER_SINGLE ? port->type
                                                                   : PIN_FLOW;
        }
        case PIN_BINDING_COMPONENT_FIELD: {
            const ecs_component_desc *component = component_by_key(b->component_key);
            const type_field *field = component
                                        ? pin_type_field_path(component->type, b->field_name)
                                        : NULL;
            return field ? field->type : PIN_FLOW;
        }
        case PIN_BINDING_COMPONENT: return PIN_STRUCT;
    }
    return PIN_FLOW;
}
static void expression(printer *p, const node *n)
{
    const char *source = behavior_expression_source(p->asset, n);
    if (!source) {
        p->failed = true;
        return;
    }
    code_lexer lex;
    code_lex_init(&lex, source);
    size_t copied = 0;
    for (;;) {
        code_token token = code_lex_next(&lex);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        int index = -1;
        if (token.kind == CODE_TOKEN_NAME) {
            for (int i = 0; i < n->input_count; i++) {
                const pin_spec *port = &n->input_pins[i];
                if (port->kind == PIN_DATA &&
                    strlen(port->name) == token.end - token.start &&
                    !strncmp(port->name, source + token.start, token.end - token.start)) {
                    index = i;
                    break;
                }
            }
        }
        if (index < 0) {
            continue;
        }
        write_text(p, "%.*s", (int)(token.start - copied), source + copied);
        const pin_spec *port = &n->input_pins[index];
        pin_type source_type = expression_source_type(p, n, index);
        bool capture_cast = source_type >= PIN_I8 && source_type <= PIN_F64 &&
                            port->type >= PIN_I8 && port->type <= PIN_F64 &&
                            source_type != port->type &&
                            port->container == PIN_CONTAINER_SINGLE;
        if (capture_cast) {
            write_text(p, "cast<");
            type(p, port);
            write_text(p, ">(");
        }
        node *producer = NULL;
        int projection = -1;
        capture_alias *alias = NULL;
        if (graph_input_source(p->graph, n, index, &producer, &projection)) {
            for (int i = 0; i < acount(p->aliases); i++) {
                if (p->aliases[i].source == producer &&
                    p->aliases[i].output == projection) {
                    alias = &p->aliases[i];
                    break;
                }
            }
        }
        if (alias && !n->input_pins[index].binding) {
            const node_desc *conversion = graph_input_conversion(p->graph, n, index);
            if (conversion) {
                write_text(p, "convert<");
                quoted(p, conversion->name);
                write_text(p, ">(");
            }
            write_text(p, "%s", alias->name);
            if (conversion) {
                write_text(p, ")");
            }
        } else {
            bool previous_operand = p->expression_operand;
            p->expression_operand = !capture_cast;
            input(p, n, index);
            p->expression_operand = previous_operand;
        }
        if (capture_cast) {
            write_text(p, ")");
        }
        copied = token.end;
    }
    write_text(p, "%s", source + copied);
}
static bool grouped_expression(const char *source)
{
    if (!source) {
        return false;
    }
    code_lexer lexer;
    code_lex_init(&lexer, source);
    code_token token = code_lex_next(&lexer);
    if (!code_token_is(&lexer, token, "(")) {
        return false;
    }
    int depth = 1;
    while (depth) {
        token = code_lex_next(&lexer);
        if (token.kind == CODE_TOKEN_END || token.kind == CODE_TOKEN_ERROR) {
            return false;
        }
        if (code_token_is(&lexer, token, "(")) {
            depth++;
        } else if (code_token_is(&lexer, token, ")")) {
            depth--;
        }
    }
    return code_lex_next(&lexer).kind == CODE_TOKEN_END;
}

static int entity_input(const node *n)
{
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind == PIN_DATA &&
            !strcmp(n->input_pins[i].name, "Entity")) {
            return i;
        }
    }
    return -1;
}

static void component_path(printer *p, const node *n, const ecs_component_desc *component)
{
    int entity = entity_input(n);
    node *owner = NULL;
    int owner_port = -1;
    bool explicit_owner = entity >= 0 &&
                          graph_input_source(p->graph, n, entity, &owner, &owner_port) &&
                          owner->desc == &NODE_Self &&
                          node_symbol(p, owner)->declared;
    if (entity < 0 || (component_node_self(p->graph, n) && !explicit_owner)) {
        write_text(p, "self");
    } else {
        input(p, n, entity);
    }
    write_text(p, ".%s", code_component_spelling(component));
}

static void component_field_output(printer *p, const node *n, int index, const type_desc *root)
{
    const pin_spec *port = &n->output_pins[index];
    if (!port->field_source) {
        return;
    }
    int parent = -1;
    if (!graph_pin_by_id(n, port->field_source, NULL, &parent) || parent == index) {
        p->failed = true;
        return;
    }
    component_field_output(p, n, parent, root);
    const pin_spec *owner = &n->output_pins[parent];
    const type_desc *type = owner->field_source
                              ? registry_type_by_name(owner->type_name)
                              : root;
    field_path(p, type, port->field_name ? port->field_name : port->name);
}

static bool component_read(printer *p, const node *n, int index)
{
    const ecs_component_desc *component = component_node_component(n);
    if (!component || (component_node_kind_of(n->desc) != COMPONENT_NODE_GET &&
                       component_generated_kind(n->desc) != COMPONENT_NODE_GET)) {
        return false;
    }
    component_path(p, n, component);
    component_field_output(p, n, index, component->type);
    return true;
}

static bool component_write(printer *p, const node *n)
{
    const ecs_component_desc *component = component_node_component(n);
    if (!component || data_uses(p->graph, n) ||
        (component_node_kind_of(n->desc) != COMPONENT_NODE_SET &&
         component_generated_kind(n->desc) != COMPONENT_NODE_SET)) {
        return false;
    }
    int value = -1, outcomes = 0;
    const char *split = NULL;
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *pin = &n->input_pins[i];
        bool ambiguous = false;
        bool field = component_node_split(n) &&
                     code_component_field(component->type, pin->name, &ambiguous) &&
                     !ambiguous;
        if (pin->kind != PIN_DATA || (strcmp(pin->name, "Value") &&
                                      strncmp(pin->name, "Value.", 6) && !field)) {
            continue;
        }
        if (value >= 0) {
            return false;
        }
        value = i;
        split = field                            ? pin->name
              : !strncmp(pin->name, "Value.", 6) ? pin->name + 6
                                                 : NULL;
    }
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind == PIN_EXEC) {
            if (strcmp(n->output_pins[i].name, "Then")) {
                return false;
            }
            outcomes++;
        }
    }
    if (value < 0 || outcomes != 1) {
        return false;
    }
    component_path(p, n, component);
    if (split) {
        field_path(p, component->type, split);
    } else if (n->desc->component_kind == NODE_COMPONENT_SET_FIELD) {
        int field = n->desc->component_field;
        if (field < 0 || field >= component->type->field_count) {
            p->failed = true;
            return true;
        }
        field_path(p, component->type, component->type->fields[field].name);
    }
    node *operation = NULL, *target = NULL;
    int result = -1, target_port = -1;
    bool compound = !split && n->desc->component_kind != NODE_COMPONENT_SET_FIELD &&
                    !n->input_pins[value].binding &&
                    !graph_input_conversion(p->graph, n, value) &&
                    graph_input_source(p->graph, n, value, &operation, &result) &&
                    (operation->desc == &NODE_Add || operation->desc == &NODE_Subtract) &&
                    !node_symbol(p, operation)->declared &&
                    operation->input_count == 2 && result == 0 &&
                    !operation->input_pins[0].binding &&
                    !graph_input_conversion(p->graph, operation, 0) &&
                    graph_input_source(p->graph, operation, 0, &target, &target_port) &&
                    component_node_component(target) == component &&
                    !node_symbol(p, target)->declared && target_port == 0 &&
                    component_node_self(p->graph, target) &&
                    component_node_self(p->graph, n);
    if (compound) {
        write_text(p, operation->desc == &NODE_Add ? " += " : " -= ");
        input(p, operation, 1);
    } else {
        write_text(p, " = ");
        input(p, n, value);
    }
    write_text(p, ";\n");
    return true;
}

static void value_source(printer *p, const node *source, int source_port)
{
    symbol *s = node_symbol(p, source);
    if (s && s->loop) {
        output(p, source, source_port);
        return;
    }
    if (!s || s->declared || s->result_local ||
        source->id == p->graph->entry_id || p->depth >= acount(p->symbols)) {
        output(p, source, source_port);
        return;
    }
    p->depth++;
    if (component_read(p, source, source_port)) {
        p->depth--;
        return;
    }
    if (source->desc == &NODE_Self && source->output_count == 1) {
        write_text(p, "self");
        p->depth--;
        return;
    }
    if (source->desc->pure_kind == NODE_PURE_CONST && !source->input_count) {
        literal(p, &source->output_pins[source_port].default_value);
        p->depth--;
        return;
    }
    if (source->desc == &NODE_BehaviorExpression &&
        !ordinary_expression_call(p, source) && !named_expression(p->graph, source)) {
        bool wrap = p->expression_operand &&
                    !grouped_expression(behavior_expression_source(p->asset, source));
        if (wrap) {
            write_text(p, "(");
        }
        expression(p, source);
        if (wrap) {
            write_text(p, ")");
        }
        p->depth--;
        return;
    }
    callable(p, source);
    call_arguments(p, source);
    if (source->output_count != 1) {
        write_text(p, ".");
        member_name(p, source, source_port);
    }
    p->depth--;
}
static void input(printer *p, const node *n, int index)
{
    const pin_spec *port = &n->input_pins[index];
    node *source = NULL;
    int source_port = -1;
    bool wired = graph_input_source(p->graph, n, index, &source, &source_port);
    bool local_wire = false;
    if (wired && port->binding && port->binding->kind == PIN_BINDING_LOCAL) {
        const graph_local *local = graph_local_by_id(p->graph, port->binding->local_id);
        local_wire = local && local->source_id == source->id &&
                     local->source_pin_id == source->output_pins[source_port].id;
    }
    pin_value zero = serialize_zero_value(port->type, port->container, port->type_name);
    bool fallback_default = (wired || port->binding) &&
                            port->default_value.type == port->type &&
                            port->default_value.container == port->container &&
                            !serialize_same_value(&port->default_value, &zero);
    bool fallback_binding = wired && port->binding && !local_wire;
    if (fallback_binding || fallback_default) {
        write_text(p, "fallback(");
    }
    if (wired) {
        const node_desc *conversion = graph_input_conversion(p->graph, n, index);
        if (conversion) {
            write_text(p, "convert<");
            quoted(p, conversion->name);
            write_text(p, ">(");
        }
        if (local_wire) {
            binding(p, port->binding);
        } else {
            bool previous_operand = p->expression_operand;
            if (conversion) {
                p->expression_operand = false;
            }
            value_source(p, source, source_port);
            p->expression_operand = previous_operand;
        }
        if (conversion) {
            write_text(p, ")");
        }
    } else if (port->binding) {
        binding(p, port->binding);
    } else if (port->type == PIN_NAME && code_call_reference_kind(n->desc, port->name)) {
        argument_reference(p, port->default_value.str);
    } else {
        literal(p, &port->default_value);
    }
    if (fallback_binding) {
        write_text(p, ", binding: ");
        print_fallback_binding(p, port->binding);
    }
    if (fallback_default) {
        write_text(p, ", default: ");
        literal(p, &port->default_value);
    }
    if (fallback_binding || fallback_default) {
        write_text(p, ")");
    }
}
static void signature(printer *p, const node *n, bool result)
{
    const pin_spec *ports = result ? n->output_pins : n->input_pins;
    int count = result ? n->output_count : n->input_count;
    write_text(p, "(");
    int emitted = 0;
    for (int i = 0; i < count; i++) {
        const pin_spec *port = &ports[i];
        if (port->kind == PIN_EXEC || port->field_source) {
            continue;
        }
        if (emitted++) {
            write_text(p, ", ");
        }
        identifier(p, port->name);
        write_text(p, ": ");
        if (port->flags & PIN_FLAG_REFERENCE) {
            write_text(p, "ref ");
        }
        if (port->flags & PIN_FLAG_SERVICE) {
            write_text(p, "service ");
        }
        if (port->flags & PIN_FLAG_DEFAULT_SELF) {
            write_text(p, "self ");
        }
        if (port->flags & PIN_FLAG_SETTING) {
            write_text(p, "setting ");
        }
        type(p, port);
        if (port->kind != PIN_DATA) {
            continue;
        }
        write_text(p, " = ");
        literal(p, &port->default_value);
    }
    write_text(p, ")");
}
static bool local_callable(printer *p, const char *name)
{
    const graph *g = behavior_asset_find_graph_by_name(p->asset, name);
    return g && (g->kind == GRAPH_FUNCTION || g->kind == GRAPH_MACRO);
}
static bool action_callable(printer *p, const char *name)
{
    for (int i = 0; i < acount(p->symbols); i++) {
        const symbol *s = &p->symbols[i];
        if ((s->declared || s->result_local) &&
            node_behavior_for(s->node) != NODE_BEHAVIOR_PURE &&
            !strcmp(s->name, name)) {
            return true;
        }
    }
    return false;
}
static void callable(printer *p, const node *n)
{
    if (n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
        n->desc == &NODE_CallMacro || ordinary_expression_call(p, n)) {
        const graph *target = behavior_asset_find_graph(p->asset, n->reference_id);
        if (n->reference_asset) {
            quoted(p, n->reference_asset);
            write_text(p, ".");
        }
        identifier(
            p,
            ordinary_expression_call(p, n) && target ? target->name
            : n->reference_name                      ? n->reference_name
            : target                                 ? target->name
                                                     : "missing"
        );
        return;
    }
    char name[256];
    if (code_call_name(n->desc, name, sizeof name) &&
        !code_program_reserved(name) && !local_callable(p, name) &&
        !local_callable(p, n->desc->name) && !action_callable(p, name)) {
        write_text(p, "%s", name);
    } else {
        write_text(p, "engine[");
        quoted(p, n->desc->name);
        write_text(p, "]");
    }
    const char *reference = n->reference_name;
    if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
        const behavior_variable *variable = behavior_asset_find_variable_by_id(p->asset, n->reference_id);
        if (variable) {
            reference = variable->name;
        }
    }
    if (n->desc == &NODE_BehaviorExpression) {
        reference = behavior_expression_source(p->asset, n);
    }
    if (reference || n->reference_asset || n->reference_id) {
        write_text(p, "<");
        if (!reference && n->reference_id) {
            const graph *target = behavior_asset_find_graph(p->asset, n->reference_id);
            const behavior_variable *v = behavior_asset_find_variable_by_id(p->asset, n->reference_id);
            reference = target ? target->name : v ? v->name : NULL;
        }
        if (component_node_kind_of(n->desc)) {
            symbol_reference(p, reference);
        } else {
            quoted(p, reference);
        }
        if (n->reference_asset) {
            write_text(p, ", ");
            quoted(p, n->reference_asset);
        }
        write_text(p, ">");
    }
}
static void declaration(printer *p, const node *n)
{
    if (n->desc == &NODE_Event || n->id == p->graph->entry_id ||
        n->id == p->graph->return_id) {
        return;
    }
    if (!node_symbol(p, n)->declared) {
        return;
    }
    line(p);
    if (n->disabled) {
        write_text(p, "disabled ");
    }
    bool constant = node_behavior_for(n) == NODE_BEHAVIOR_PURE &&
                    n->desc->pure_kind == NODE_PURE_CONST && !n->input_count &&
                    n->output_count == 1;
    const char *kind = node_behavior_for(n) == NODE_BEHAVIOR_PURE ? "value" : "action";
    write_text(p, "%s ", kind);
    source_anchor(p, n->id, "node");
    write_text(p, "%s", node_name(p, n));
    if (n->desc == &NODE_Self) {
        write_text(p, " = self;\n");
        return;
    }
    if (constant) {
        write_text(p, ": ");
        type(p, &n->output_pins[0]);
        write_text(p, " = ");
        literal(p, &n->output_pins[0].default_value);
        write_text(p, ";\n");
        return;
    }
    const pin_spec *result_type = computed_type(n);
    if (component_node_component(n) &&
        (component_node_kind_of(n->desc) == COMPONENT_NODE_GET ||
         component_generated_kind(n->desc) == COMPONENT_NODE_GET)) {
        result_type = NULL;
    }
    if (n->desc == &NODE_BehaviorExpression && !ordinary_expression_call(p, n) &&
        n->output_count == 1 && n->output_pins[0].kind == PIN_DATA &&
        n->output_pins[0].container == PIN_CONTAINER_SINGLE &&
        n->output_pins[0].type >= PIN_I8 && n->output_pins[0].type <= PIN_F64) {
        result_type = &n->output_pins[0];
    }
    if (result_type) {
        write_text(p, ": ");
        type(p, result_type);
    }
    write_text(p, " = ");
    if (n->desc == &NODE_BehaviorExpression && node_behavior_for(n) == NODE_BEHAVIOR_PURE &&
        !ordinary_expression_call(p, n) && !named_expression(p->graph, n)) {
        write_text(p, "(");
        expression(p, n);
        write_text(p, ");\n");
        return;
    }
    if (component_read(p, n, 0)) {
        write_text(p, ";\n");
        return;
    }
    callable(p, n);
    call_arguments(p, n);
    write_text(p, ";\n");
}
static void declarations(printer *p, const node *n);
static void capture_declaration(printer *p, capture_alias *alias)
{
    if (alias->emitted) {
        return;
    }
    declarations(p, alias->source);
    line(p);
    write_text(p, "value %s: ", alias->name);
    type(p, &alias->source->output_pins[alias->output]);
    write_text(p, " = ");
    output(p, alias->source, alias->output);
    write_text(p, ";\n");
    alias->emitted = true;
}
static void declarations(printer *p, const node *n)
{
    symbol *s = node_symbol(p, n);
    if (!s || !s->declared || s->printing || s->printed) {
        return;
    }
    s->printing = true;
    for (int i = 0; i < n->input_count; i++) {
        node *source = NULL;
        int port = -1;
        if (n->input_pins[i].kind == PIN_DATA &&
            graph_input_source(p->graph, n, i, &source, &port) &&
            node_behavior_for(source) == NODE_BEHAVIOR_PURE) {
            declarations(p, source);
        }
    }
    if (n->desc == &NODE_BehaviorExpression) {
        for (int i = 0; i < n->input_count; i++) {
            node *source = NULL;
            int output = -1;
            if (!graph_input_source(p->graph, n, i, &source, &output)) {
                continue;
            }
            for (int j = 0; j < acount(p->aliases); j++) {
                if (p->aliases[j].source == source && p->aliases[j].output == output) {
                    capture_declaration(p, &p->aliases[j]);
                }
            }
        }
    }
    declaration(p, n);
    s->printing = false;
    s->printed = true;
}
static void statements(printer *p, const node *n, int entry, int depth)
{
    if (depth > acount(p->symbols)) {
        p->failed = true;
        return;
    }
    symbol *s = node_symbol(p, n);
    if (s && s->outlined) {
        line(p);
        if (s->emitted) {
            write_text(p, "goto %s;\n", node_name(p, n));
            return;
        }
        write_text(p, "%s:\n", node_name(p, n));
        s->emitted = true;
    }
    if (n->id == p->graph->return_id || !s || s->declared) {
        line(p);
        source_anchor(p, n->id, "node");
        write_text(p, "%s", node_name(p, n));
        int methods = 0;
        for (int i = 0; i < n->input_count; i++) {
            methods += n->input_pins[i].kind == PIN_EXEC;
        }
        if (entry != 0 || (methods > 1 && strcmp(n->input_pins[entry].name, "Exec")) ||
            (n->id == p->graph->return_id && p->graph->kind == GRAPH_MACRO)) {
            write_text(p, ".");
            identifier(p, n->input_pins[entry].name);
        }
        write_text(p, "();\n");
        return;
    }
    line(p);
    source_anchor(p, n->id, "node");
    if (n->desc == &NODE_Return) {
        write_text(p, "return");
        if (p->graph->kind == GRAPH_MACRO) {
            write_text(p, ".");
            identifier(p, n->input_pins[entry].name);
        }
        write_text(p, "(");
        int emitted = 0;
        for (int i = 0; i < n->input_count; i++) {
            if (n->input_pins[i].kind != PIN_DATA) {
                continue;
            }
            if (emitted++) {
                write_text(p, ", ");
            }
            identifier(p, n->input_pins[i].name);
            write_text(p, ": ");
            input(p, n, i);
        }
        write_text(p, ");\n");
        return;
    }
    if (s->loop) {
        if (n->desc == &NODE_WhileLoop) {
            write_text(p, "while (");
            input(p, n, 1);
        } else {
            write_text(p, "%s (%s in ", n->desc == &NODE_ForEach ? "foreach" : "for", node_name(p, n));
            input(p, n, 1);
            if (n->desc == &NODE_ForLoop) {
                write_text(p, "..");
                input(p, n, 2);
            }
        }
        write_text(p, ") {\n");
        p->indent++;
        node *target = NULL;
        int target_port = -1;
        if (next(p, n, 0, &target, &target_port)) {
            statements(p, target, target_port, depth + 1);
        }
        p->indent--;
        line(p);
        write_text(p, "}\n");
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].kind == PIN_EXEC &&
                !strcmp(n->output_pins[i].name, "Completed") &&
                next(p, n, i, &target, &target_port)) {
                statements(p, target, target_port, depth + 1);
            }
        }
        return;
    }
    if (selection(n)) {
        write_text(p, "match ");
        if (n->desc == &NODE_SwitchEnum) {
            write_text(p, "enum ");
        } else if (n->desc == &NODE_SwitchName) {
            write_text(p, "name ");
        }
        write_text(p, "(");
        input(p, n, 1);
        write_text(p, ") {\n");
        p->indent++;
        for (int i = 0; i < n->output_count; i++) {
            const char *name = n->output_pins[i].name;
            bool last = i == n->output_count - 1;
            bool text = n->desc == &NODE_SwitchString || n->desc == &NODE_SwitchName;
            if (!last && text && strncmp(name, "Case:", 5)) {
                continue;
            }
            line(p);
            if (last) {
                write_text(p, "default");
            } else {
                write_text(p, "case ");
                if (text) {
                    quoted(p, name + 5);
                } else {
                    write_text(p, "%d", i);
                }
            }
            write_text(p, " {\n");
            p->indent++;
            node *target = NULL;
            int target_port = -1;
            if (next(p, n, i, &target, &target_port)) {
                statements(p, target, target_port, depth + 1);
            }
            p->indent--;
            line(p);
            write_text(p, "}\n");
        }
        p->indent--;
        line(p);
        write_text(p, "}\n");
        return;
    }
    if (n->desc == &NODE_Sequence) {
        write_text(p, "sequence {\n");
        p->indent++;
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].kind != PIN_EXEC) {
                continue;
            }
            line(p);
            write_text(p, "branch {\n");
            p->indent++;
            node *target = NULL;
            int target_port = -1;
            if (next(p, n, i, &target, &target_port)) {
                statements(p, target, target_port, depth + 1);
            }
            p->indent--;
            line(p);
            write_text(p, "}\n");
        }
        p->indent--;
        line(p);
        write_text(p, "}\n");
        return;
    }
    if (branch(n)) {
        write_text(p, "if (");
        if (n->desc == &NODE_BehaviorExpression) {
            expression(p, n);
        } else {
            input(p, n, 1);
        }
        write_text(p, ") {\n");
        for (int side = 0; side < 2; side++) {
            int port = -1;
            for (int i = 0; i < n->output_count; i++) {
                if (n->output_pins[i].kind == PIN_EXEC &&
                    !strcmp(n->output_pins[i].name, side ? "False" : "True")) {
                    port = i;
                }
            }
            node *target = NULL;
            int target_port = -1;
            p->indent++;
            if (port >= 0 && next(p, n, port, &target, &target_port)) {
                statements(p, target, target_port, depth + 1);
            }
            p->indent--;
            line(p);
            write_text(p, "%s", side ? "}\n" : "} else {\n");
        }
        return;
    }
    if (n->desc == &NODE_Snapshot && s->result_local) {
        write_text(p, "%s := ", node_name(p, n));
        for (int i = 0; i < n->input_count; i++) {
            if (n->input_pins[i].kind == PIN_DATA) {
                input(p, n, i);
                break;
            }
        }
        write_text(p, ";\n");
    } else if (component_write(p, n)) {
        /* Its conventional completion continues below. */
    } else if (n->desc == &NODE_BehaviorExpression && !ordinary_expression_call(p, n) && !named_expression(p->graph, n)) {
        expression(p, n);
        write_text(p, ";\n");
    } else {
        if (s->result_local) {
            write_text(p, "%s := await ", node_name(p, n));
        } else {
            for (int i = 0; i < n->output_count; i++) {
                const pin_spec *port = &n->output_pins[i];
                node *target = NULL;
                int target_port = -1;
                if (port->kind == PIN_EXEC &&
                    (!strcmp(port->name, "Then") || !strcmp(port->name, "Completed")) &&
                    next(p, n, i, &target, &target_port)) {
                    write_text(p, "await ");
                    break;
                }
            }
        }
        callable(p, n);
        call_arguments(p, n);
        write_text(p, ";\n");
    }
    for (int i = 0; i < n->output_count; i++) {
        node *target = NULL;
        int target_port = -1;
        if (n->output_pins[i].kind == PIN_EXEC && next(p, n, i, &target, &target_port)) {
            statements(p, target, target_port, depth + 1);
        }
    }
}
static bool deferred_activation(printer *p, const node *n, int depth)
{
    if (depth >= acount(p->symbols)) {
        return true;
    }
    for (int i = 0; i < acount(p->graph->edges); i++) {
        const edge *wire = &p->graph->edges[i];
        if (wire->kind != GRAPH_EDGE_EXEC || wire->to_id != n->id) {
            continue;
        }
        const node *parent = graph_node_by_id(p->graph, wire->from_id);
        if (parent->desc == &NODE_Event) {
            continue;
        }
        const symbol *s = node_symbol(p, parent);
        if ((s && s->declared) || deferred_activation(p, parent, depth + 1)) {
            return true;
        }
    }
    return false;
}

static bool external_data_use(printer *p, const node *n, int depth)
{
    if (depth >= acount(p->symbols)) {
        return true;
    }
    for (int i = 0; i < acount(p->graph->edges); i++) {
        const edge *wire = &p->graph->edges[i];
        if (wire->kind != GRAPH_EDGE_DATA || wire->from_id != n->id) {
            continue;
        }
        const node *target = graph_node_by_id(p->graph, wire->to_id);
        const symbol *s = node_symbol(p, target);
        if (s && s->declared) {
            return true;
        }
        if (node_behavior_for(target) == NODE_BEHAVIOR_PURE &&
            external_data_use(p, target, depth + 1)) {
            return true;
        }
        if (node_behavior_for(target) != NODE_BEHAVIOR_PURE &&
            deferred_activation(p, target, 0)) {
            return true;
        }
    }
    return false;
}

static bool uses_delta_binding(const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        for (int j = 0; j < n->input_count; j++) {
            const pin_binding *binding = n->input_pins[j].binding;
            if (binding && binding->kind == PIN_BINDING_DELTA_TIME) {
                return true;
            }
        }
    }
    return false;
}

static bool inferred_event(const node *n)
{
    for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
        const graph_host_event_desc *event = &GRAPH_HOST_EVENTS[i];
        if (strcmp(n->reference_name, event->name)) {
            continue;
        }
        int count = 0;
        for (int j = 0; j < n->output_count; j++) {
            const pin_spec *port = &n->output_pins[j];
            if (port->kind != PIN_DATA || port->field_source) {
                continue;
            }
            if (count >= event->payload_count ||
                strcmp(port->name, event->payload[count].name) ||
                !pin_compatible(port, &event->payload[count])) {
                return false;
            }
            pin_value zero = serialize_zero_value(port->type, port->container, port->type_name);
            if (!serialize_same_value(&port->default_value, &zero)) {
                return false;
            }
            count++;
        }
        return count == event->payload_count;
    }
    return false;
}

static void callbacks(printer *p, const node *n)
{
    symbol *s = node_symbol(p, n);
    if (s && !s->declared && n->id != p->graph->entry_id) {
        return;
    }
    const behavior_reactive *reactive = reactive_event(p, n);
    if (reactive && reactive->kind == BEHAVIOR_REACTIVE_BIND) {
        const node *assignment = graph_node_by_id(p->graph, reactive->statement_node_id);
        if (!assignment) {
            p->failed = true;
            return;
        }
        line(p);
        write_text(p, "bind ");
        behavior_reactive_anchor *anchors = behavior_reactive_anchors(p->graph, reactive);
        for (int i = 0; i < acount(anchors); i++) {
            source_anchor(p, anchors[i].id, anchors[i].kind);
        }
        afree(anchors);
        if (component_write(p, assignment)) {
            return;
        }
        expression(p, assignment);
        write_text(p, ";\n");
        return;
    }
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_EXEC) {
            continue;
        }
        node *target = NULL;
        int target_port = -1;
        bool connected = next(p, n, i, &target, &target_port);
        if (!connected && n->desc != &NODE_Event &&
            !(n->id == p->graph->entry_id && p->graph->kind == GRAPH_MACRO)) {
            continue;
        }
        if (n == p->system_tick && inferred_event(n) && p->implicit_tick) {
            source_anchor(p, n->id, "node");
            if (connected) {
                statements(p, target, target_port, 0);
            }
            continue;
        }
        line(p);
        if (n->id == p->graph->entry_id && p->graph->kind == GRAPH_MACRO) {
            write_text(p, "entry ");
            identifier(p, n->output_pins[i].name);
        } else {
            write_text(p, "%s ", n->desc == &NODE_Event ? "on" : "after");
            if (n->desc == &NODE_Event) {
                if (reactive && reactive->kind == BEHAVIOR_REACTIVE_PRESSED_CHANGED) {
                    const node *source = graph_node_by_id(p->graph, reactive->source_node_id);
                    int pin = -1;
                    if (!source ||
                        !graph_pin_by_id(source, reactive->source_pin_id, NULL, &pin)) {
                        p->failed = true;
                        return;
                    }
                    char parameter[64];
                    change_parameter(p, parameter, sizeof parameter);
                    behavior_reactive_anchor *anchors = behavior_reactive_anchors(p->graph, reactive);
                    for (int j = 0; j < acount(anchors); j++) {
                        source_anchor(p, anchors[j].id, anchors[j].kind);
                    }
                    afree(anchors);
                    value_source(p, source, pin);
                    write_text(p, "Changed(%s)", parameter);
                } else {
                    bool tick = graph_host_event_is(n->reference_name, GRAPH_HOST_EVENT_TICK);
                    bool alias = data_uses(p->graph, n) &&
                                 (!tick || external_data_use(p, n, 0));
                    source_anchor(p, n->id, "node");
                    identifier(p, n->reference_name);
                    if (alias) {
                        write_text(p, " as %s", node_name(p, n));
                    }
                    if (inferred_event(n) &&
                        graph_host_event_is(n->reference_name, GRAPH_HOST_EVENT_TICK) &&
                        (data_uses(p->graph, n) || uses_delta_binding(p->graph))) {
                        write_text(p, "(dt)");
                    } else if (!inferred_event(n)) {
                        signature(p, n, true);
                    }
                }
            } else {
                int outcomes = 0;
                for (int j = 0; j < n->output_count; j++) {
                    outcomes += n->output_pins[j].kind == PIN_EXEC;
                }
                if (outcomes == 1) {
                    write_text(p, "%s", node_name(p, n));
                } else {
                    output(p, n, i);
                }
            }
        }
        write_text(p, " {\n");
        p->indent++;
        const node *previous = p->activation;
        p->activation = n;
        if (connected) {
            statements(p, target, target_port, 0);
        }
        p->activation = previous;
        p->indent--;
        line(p);
        write_text(p, "}\n");
    }
}
static void guard_values(printer *p, const node *n, const node ***values)
{
    if (!n || node_behavior_for(n) != NODE_BEHAVIOR_PURE) {
        return;
    }
    for (int i = 0; i < acount(*values); i++) {
        if ((*values)[i] == n) {
            return;
        }
    }
    apush(*values, n);
    for (int i = 0; i < n->input_count; i++) {
        node *source = NULL;
        int port = -1;
        if (graph_input_source(p->graph, n, i, &source, &port)) {
            guard_values(p, source, values);
        }
    }
}

static void suppress_reactive_guard(printer *p, const behavior_reactive *reactive)
{
    if (reactive->kind != BEHAVIOR_REACTIVE_BIND) {
        return;
    }
    const node *event = graph_node_by_id(p->graph, reactive->event_id);
    node *branch = NULL;
    int port = -1;
    if (!event || !next(p, event, 0, &branch, &port) || branch->desc != &NODE_Branch) {
        return;
    }
    node *condition = NULL;
    if (!graph_input_source(p->graph, branch, 1, &condition, &port)) {
        return;
    }
    const node **values = NULL;
    guard_values(p, condition, &values);
    for (int i = 0; i < acount(values); i++) {
        bool external = false;
        for (int j = 0; j < acount(p->graph->edges); j++) {
            const edge *wire = &p->graph->edges[j];
            if (wire->kind != GRAPH_EDGE_DATA || wire->from_id != values[i]->id) {
                continue;
            }
            bool guard = wire->to_id == branch->id;
            for (int k = 0; k < acount(values); k++) {
                guard |= wire->to_id == values[k]->id;
            }
            external |= !guard;
        }
        if (!external) {
            node_symbol(p, values[i])->declared = false;
        }
    }
    const node *value = graph_node_by_id(p->graph, reactive->value_node_id);
    bool component_get = value && component_node_component(value) &&
                         (component_node_kind_of(value->desc) == COMPONENT_NODE_GET ||
                          component_generated_kind(value->desc) == COMPONENT_NODE_GET);
    if (component_get && value->id != reactive->source_node_id &&
        !value->disabled && !value->comment && !value->breakpoint &&
        !value->reference_name && !value->desc->instance_state &&
        value->presentation == NODE_PRESENTATION_AUTO) {
        bool named = false;
        for (int i = 0; i < acount(p->graph->locals); i++) {
            named |= p->graph->locals[i].source_id == value->id;
        }
        int uses = 0;
        bool assignment_only = true;
        for (int i = 0; i < acount(p->graph->edges); i++) {
            const edge *wire = &p->graph->edges[i];
            if (wire->kind != GRAPH_EDGE_DATA || wire->from_id != value->id) {
                continue;
            }
            bool guard = wire->to_id == branch->id;
            for (int j = 0; j < acount(values); j++) {
                guard |= wire->to_id == values[j]->id;
            }
            if (!guard) {
                uses++;
                assignment_only &= wire->to_id == reactive->statement_node_id;
            }
        }
        if (!named && uses == 1 && assignment_only) {
            node_symbol(p, value)->declared = false;
        }
    }
    afree(values);
}

static void graph_body(printer *p, const graph *g)
{
    p->graph = g;
    p->system_tick = NULL;
    if (p->system && g == p->asset->graph) {
        int ticks = 0;
        for (int i = 0; i < acount(g->nodes); i++) {
            const node *n = g->nodes[i];
            if (n->desc == &NODE_Event &&
                graph_host_event_is(n->reference_name, GRAPH_HOST_EVENT_TICK)) {
                p->system_tick = n;
                ticks++;
            }
        }
        if (ticks != 1) {
            p->system_tick = NULL;
        }
    }
    symbols(p);
    for (int i = 0; i < acount(p->asset->reactives); i++) {
        if (p->asset->reactives[i].graph_id == g->graph_id) {
            suppress_reactive_guard(p, &p->asset->reactives[i]);
        }
    }
    p->implicit_tick = p->system_tick != NULL;
    if (p->implicit_tick && external_data_use(p, p->system_tick, 0)) {
        p->implicit_tick = false;
    }
    for (int i = 0; p->implicit_tick && i < acount(p->symbols); i++) {
        const symbol *s = &p->symbols[i];
        if (s->declared && s->reachable && s->node->desc != &NODE_Event &&
            node_behavior_for(s->node) != NODE_BEHAVIOR_PURE) {
            p->implicit_tick = false;
        }
    }
    for (int i = 0; i < acount(p->symbols); i++) {
        symbol *s = &p->symbols[i];
        if (s->node->desc == &NODE_Self && !s->node->disabled) {
            s->declared = explicit_component_owner(g, s->node);
        }
        for (int j = 0; j < acount(p->asset->reactives); j++) {
            const behavior_reactive *reactive = &p->asset->reactives[j];
            if (reactive->graph_id == g->graph_id &&
                reactive->source_node_id == s->node->id && !data_uses(g, s->node)) {
                s->declared = false;
            }
        }
    }
    if (g->dataflow) {
        line(p);
        write_text(p, "dataflow;\n");
    }
    if (g->call_depth_limit || g->kind == GRAPH_FUNCTION) {
        line(p);
        write_text(p, "depth_limit %d;\n", g->call_depth_limit);
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        declarations(p, g->nodes[i]);
    }
    for (int i = 0; i < acount(p->aliases); i++) {
        capture_declaration(p, &p->aliases[i]);
    }
    for (int i = 0; i < acount(g->locals); i++) {
        const graph_local *local = &g->locals[i];
        const node *n = graph_node_by_id(g, local->source_id);
        int port = -1;
        if (!n || !graph_pin_by_id(n, local->source_pin_id, NULL, &port)) {
            p->failed = true;
            continue;
        }
        const symbol *producer = node_symbol(p, n);
        if (n->desc == &NODE_Snapshot && producer && producer->result_local) {
            continue;
        }
        line(p);
        write_text(p, "local ");
        source_anchor(p, local->id, "value");
        identifier(p, local->name);
        write_text(p, " = ");
        output(p, n, port);
        write_text(p, ";\n");
    }
    const node *result = graph_node_by_id(g, g->return_id);
    for (int i = 0; result && i < result->input_count; i++) {
        if (result->input_pins[i].kind == PIN_EXEC) {
            if (g->kind == GRAPH_MACRO) {
                line(p);
                write_text(p, "exit ");
                identifier(p, result->input_pins[i].name);
                write_text(p, ";\n");
            }
            continue;
        }
        line(p);
        write_text(p, "result ");
        identifier(p, result->input_pins[i].name);
        write_text(p, ": ");
        type(p, &result->input_pins[i]);
        write_text(p, " = ");
        input(p, result, i);
        write_text(p, ";\n");
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        callbacks(p, g->nodes[i]);
    }
}
static bool generated(const behavior_asset *asset, const graph *g)
{
    if (ordinary_expression(asset, g)) {
        return false;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *owner = asset->graphs[i];
        for (int j = 0; j < acount(owner->nodes); j++) {
            const node *n = owner->nodes[j];
            if (n->desc == &NODE_BehaviorExpression && n->reference_id == g->graph_id) {
                return true;
            }
        }
    }
    return false;
}
static void system_query_terms(
    const behavior_asset *asset,
    av_doc *doc,
    av_value *terms,
    const av_value *query,
    const char ***excluded
)
{
    if (!query) {
        return;
    }
    const char *op = av_get_str(av_obj_get(query, "op"));
    const av_value *children = av_obj_get(query, "terms");
    if (op && !strcmp(op, "all")) {
        for (size_t i = 0; i < av_arr_size(children); i++) {
            system_query_terms(asset, doc, terms, av_arr_get(children, i), excluded);
        }
        return;
    }
    const char *key = av_get_str(av_obj_get(query, "component"));
    if (op && !strcmp(op, "has") && key) {
        for (int i = 0; i < acount(asset->component_parameters); i++) {
            const behavior_component_parameter *parameter = &asset->component_parameters[i];
            if (!parameter->optional && !strcmp(parameter->key, key)) {
                return;
            }
        }
    }
    if (op && !strcmp(op, "not") && av_arr_size(children) == 1) {
        const av_value *child = av_arr_get(children, 0);
        const char *child_op = av_get_str(av_obj_get(child, "op"));
        const char *child_key = av_get_str(av_obj_get(child, "component"));
        if (child_op && !strcmp(child_op, "has") && child_key &&
            component_by_key(child_key)) {
            apush(*excluded, child_key);
            return;
        }
    }
    av_arr_add_val(terms, av_clone(doc, query));
}

static char *print_asset(
    const behavior_asset *asset,
    bool system,
    const av_value *query,
    char **metadata,
    char *error,
    size_t size
)
{
    if (metadata) {
        *metadata = NULL;
    }
    printer p = { .asset = asset, .system = system, .error = error, .error_size = size };
    if (size) {
        error[0] = 0;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        if (g->kind != GRAPH_FUNCTION && g->kind != GRAPH_MACRO) {
            continue;
        }
        const node *entry = graph_node_by_id(g, g->entry_id);
        const node *result = graph_node_by_id(g, g->return_id);
        if (!entry || entry->desc != &NODE_FunctionEntry || !result ||
            result->desc != &NODE_Return) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "Callable '%s' requires Function Entry and Return nodes",
                    g->name ? g->name : ""
                );
            }
            return NULL;
        }
    }
    if (metadata) {
        char *json = code_source_metadata_export(asset);
        p.metadata = av_json_read(json);
        free(json);
        p.anchors = av_obj_add_arr(p.metadata, av_doc_root(p.metadata), "anchors");
    }
    write_text(
        &p,
        "%s ",
        system                                     ? "system"
        : asset->domain == BEHAVIOR_DOMAIN_LIBRARY ? "library"
                                                   : "behavior"
    );
    identifier(&p, asset->name);
    av_doc *query_doc = av_doc_new();
    av_value *remaining = av_obj(query_doc);
    av_obj_add_str(query_doc, remaining, "op", "all");
    av_value *terms = av_obj_add_arr(query_doc, remaining, "terms");
    const char **excluded = NULL;
    if (system) {
        if (acount(asset->component_parameters)) {
            write_text(&p, "(");
            for (int i = 0; i < acount(asset->component_parameters); i++) {
                const behavior_component_parameter *parameter = &asset->component_parameters[i];
                const ecs_component_desc *component = component_by_key(parameter->key);
                if (!component) {
                    p.failed = true;
                    continue;
                }
                if (i) {
                    write_text(&p, ", ");
                }
                identifier(&p, parameter->name);
                write_text(&p, ": ");
                identifier(&p, code_component_spelling(component));
                if (parameter->optional) {
                    write_text(&p, "?");
                }
            }
            write_text(&p, ")");
        }
        system_query_terms(asset, query_doc, terms, query, &excluded);
        if (acount(excluded)) {
            write_text(&p, " without ");
            for (int i = 0; i < acount(excluded); i++) {
                if (i) {
                    write_text(&p, ", ");
                }
                const ecs_component_desc *component = component_by_key(excluded[i]);
                assert(component);
                identifier(&p, code_component_spelling(component));
            }
        }
    }
    afree(excluded);
    write_text(&p, " {\n");
    p.indent = 1;
    if (asset->owner_type_name && strcmp(asset->owner_type_name, "ecs.entity")) {
        line(&p);
        write_text(&p, "owner ");
        quoted(&p, asset->owner_type_name);
        write_text(&p, ";\n");
    }
    if (asset->domain != BEHAVIOR_DOMAIN_EVENT &&
        asset->domain != BEHAVIOR_DOMAIN_LIBRARY) {
        line(&p);
        write_text(&p, "domain %s;\n", behavior_domain_name(asset->domain));
    }
    if (system && av_arr_size(terms)) {
        char *expression = code_query_write(remaining);
        if (!expression) {
            p.failed = true;
        } else {
            line(&p);
            write_text(&p, "query %s;\n", expression);
        }
        free(expression);
    }
    av_doc_free(query_doc);
    for (int i = 0; i < acount(asset->variables); i++) {
        const behavior_variable *v = &asset->variables[i];
        line(&p);
        write_text(&p, "%s ", v->shared ? "shared" : "state");
        source_anchor(&p, v->id, "value");
        identifier(&p, v->name);
        pin_spec port = { .type = v->type,
                          .type_name = v->type_name,
                          .container = v->container };
        if (v->container == PIN_CONTAINER_SINGLE &&
            (v->type == PIN_F64 || v->type == PIN_BOOL || v->type == PIN_STR)) {
            write_text(&p, " := ");
        } else {
            write_text(&p, ": ");
            type(&p, &port);
            write_text(&p, " = ");
        }
        literal(&p, &v->default_value);
        write_text(&p, ";\n");
    }
    graph_body(&p, asset->graph);
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        if (g == asset->graph || generated(asset, g)) {
            continue;
        }
        p.graph = g;
        const node *entry = graph_node_by_id(g, g->entry_id);
        const node *result = graph_node_by_id(g, g->return_id);
        line(&p);
        if (g->kind == GRAPH_EVENT) {
            write_text(&p, "events ");
            quoted(&p, g->name);
            write_text(&p, " ");
            source_anchor(&p, g->graph_id, "graph");
            write_text(&p, "{\n");
            p.indent++;
            graph_body(&p, g);
            p.indent--;
            line(&p);
            write_text(&p, "}\n");
            continue;
        }
        if (g->kind == GRAPH_FUNCTION && graph_signature_pure(g)) {
            write_text(&p, "pure ");
        }
        write_text(&p, "%s ", g->kind == GRAPH_MACRO ? "macro" : "function");
        quoted(&p, g->name);
        parameters(&p, entry, result);
        p.use_generics = true;
        if (entry) {
            signature(&p, entry, true);
        } else {
            write_text(&p, "()");
        }
        write_text(&p, " -> ");
        if (result) {
            signature(&p, result, false);
        } else {
            write_text(&p, "()");
        }
        p.use_generics = false;
        write_text(&p, " ");
        source_anchor(&p, g->graph_id, "graph");
        write_text(&p, "{\n");
        p.indent++;
        graph_body(&p, g);
        p.indent--;
        line(&p);
        write_text(&p, "}\n");
    }
    write_text(&p, "}\n");
    for (int i = 0; i < acount(p.symbols); i++) {
        free(p.symbols[i].name);
    }
    afree(p.symbols);
    for (int i = 0; i < acount(p.aliases); i++) {
        free(p.aliases[i].name);
    }
    afree(p.aliases);
    afree(p.generics);
    if (p.failed) {
        if (size) {
            snprintf(error, size, "Behavior contains an unresolved executable source reference");
        }
        free(p.text);
        av_doc_free(p.metadata);
        return NULL;
    }
    if (metadata) {
        *metadata = av_json_write(av_doc_root(p.metadata));
    }
    av_doc_free(p.metadata);
    return p.text;
}

bool code_print_reactive_event(
    const behavior_asset *asset,
    const graph *graph,
    const node *event,
    char *out,
    size_t size
)
{
    printer p = { .asset = asset, .graph = graph };
    const behavior_reactive *reactive = reactive_event(&p, event);
    if (!reactive) {
        return false;
    }
    const node *source = graph_node_by_id(graph, reactive->source_node_id);
    int pin = -1;
    if (!source || !graph_pin_by_id(source, reactive->source_pin_id, NULL, &pin)) {
        snprintf(out, size, "%s", reactive->kind == BEHAVIOR_REACTIVE_BIND ? "bind" : "PressedChanged");
        return true;
    }
    symbols(&p);
    for (int i = 0; i < acount(p.symbols); i++) {
        p.symbols[i].declared = false;
    }
    if (reactive->kind == BEHAVIOR_REACTIVE_BIND) {
        write_text(&p, "bind ");
    }
    value_source(&p, source, pin);
    if (reactive->kind == BEHAVIOR_REACTIVE_PRESSED_CHANGED) {
        write_text(&p, "Changed");
    }
    snprintf(out, size, "%s", p.failed ? "Reactive relationship" : p.text);
    for (int i = 0; i < acount(p.symbols); i++) {
        free(p.symbols[i].name);
    }
    afree(p.symbols);
    for (int i = 0; i < acount(p.aliases); i++) {
        free(p.aliases[i].name);
    }
    afree(p.aliases);
    afree(p.generics);
    free(p.text);
    return true;
}

char *code_print_behavior_with_metadata(const behavior_asset *asset, char **metadata, char *error, size_t size)
{
    return print_asset(asset, false, NULL, metadata, error, size);
}

char *code_print_system_with_metadata(
    const behavior_asset *asset,
    const av_value *query,
    char **metadata,
    char *error,
    size_t size
)
{
    return print_asset(asset, true, query, metadata, error, size);
}

char *code_print_behavior(const behavior_asset *asset, char *error, size_t size)
{
    return code_print_behavior_with_metadata(asset, NULL, error, size);
}
