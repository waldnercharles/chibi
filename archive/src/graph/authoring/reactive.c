#include "graph/model/reactive.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "graph/nodes/vector_nodes.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int behavior_reactive_output_index(const node *n, uint64_t id)
{
    for (int i = 0; n && i < n->output_count; i++) {
        if (n->output_pins[i].id == id) {
            return i;
        }
    }
    return -1;
}
static bool depends(const graph *, const node *, int, const node *, int, int);
static bool bind_depends(const graph *, const behavior_reactive *, const node *, int);

static bool compile_binding(const pin_binding *binding, behavior_reactive_path *path)
{
    if (binding->kind == PIN_BINDING_VARIABLE) {
        path->variable_id = binding->variable_id;
        path->field = (type_field){ .type = PIN_HANDLE,
                                    .type_name = "ecs.entity",
                                    .size = sizeof(uint64_t) };
        return true;
    }
    if (binding->kind != PIN_BINDING_COMPONENT &&
        binding->kind != PIN_BINDING_COMPONENT_FIELD) {
        return false;
    }
    const ecs_component_desc *component = component_by_key(binding->component_key);
    if (!component) {
        return false;
    }
    path->self = true;
    path->field = (type_field){ .type = PIN_STRUCT,
                                .type_name = component->type->name,
                                .size = component->type->size };
    size_t offset = 0;
    if (binding->kind == PIN_BINDING_COMPONENT_FIELD) {
        const type_desc *type = component->type;
        const char *at = binding->field_name;
        while (at && *at) {
            const char *end = strchr(at, '.');
            size_t length = end ? (size_t)(end - at) : strlen(at);
            const type_field *field = NULL;
            for (int i = 0; type && i < type->field_count; i++) {
                if (strlen(type->fields[i].name) == length &&
                    !strncmp(type->fields[i].name, at, length)) {
                    field = &type->fields[i];
                    break;
                }
            }
            if (!field) {
                return false;
            }
            path->field = *field;
            offset += field->offset;
            if (!end) {
                break;
            }
            type = field->type == PIN_STRUCT ? type_catalog_find(field->type_name)
                                             : NULL;
            at = end + 1;
        }
    }
    apush(path->steps, ((behavior_reactive_step){ .component = component->id, .offset = offset }));
    return true;
}

bool behavior_reactive_path_compile(const graph *g, const node *n, int pin, behavior_reactive_path *path, int depth)
{
    if (!n || pin < 0 || depth > 128) {
        return false;
    }
    const pin_spec *output = &n->output_pins[pin];
    if (output->field_source) {
        int parent;
        const type_field *field = graph_output_field(n, pin, &parent);
        if (!field || !behavior_reactive_path_compile(g, n, parent, path, depth + 1) ||
            !acount(path->steps)) {
            return false;
        }
        path->steps[acount(path->steps) - 1].offset += field->offset;
        path->field = *field;
        return true;
    }
    if (n->desc == &NODE_Self) {
        path->self = true;
        path->field = (type_field){ .type = PIN_HANDLE,
                                    .type_name = "ecs.entity",
                                    .size = sizeof(uint64_t) };
        return true;
    }
    if (n->desc && n->desc->pure_kind == NODE_PURE_GET) {
        path->variable_id = n->reference_id;
        path->field = (type_field){ .type = output->type,
                                    .type_name = output->type_name,
                                    .size = sizeof(uint64_t) };
        return output->type == PIN_HANDLE;
    }
    const ecs_component_desc *component = component_node_component(n);
    bool get = component_node_kind_of(n->desc) == COMPONENT_NODE_GET ||
               (n->desc && (n->desc->component_kind == NODE_COMPONENT_GET_SELF ||
                            n->desc->component_kind == NODE_COMPONENT_GET_ENTITY));
    if (!component || !get) {
        return false;
    }
    bool explicit_owner = false;
    for (int i = 0; i < n->input_count; i++) {
        if (strcmp(n->input_pins[i].name, "Entity")) {
            continue;
        }
        node *source;
        int source_pin;
        if (graph_input_source(g, n, i, &source, &source_pin)) {
            if (!behavior_reactive_path_compile(g, source, source_pin, path, depth + 1) ||
                path->field.type != PIN_HANDLE) {
                return false;
            }
            explicit_owner = true;
        } else if (n->input_pins[i].binding) {
            if (!compile_binding(n->input_pins[i].binding, path) ||
                path->field.type != PIN_HANDLE) {
                return false;
            }
            explicit_owner = true;
        } else if (n->input_pins[i].default_value.u64) {
            path->owner = n->input_pins[i].default_value.u64;
            explicit_owner = true;
        }
        break;
    }
    if (!explicit_owner) {
        path->self = true;
    }
    apush(path->steps, ((behavior_reactive_step){ .component = component->id }));
    path->field = (type_field){ .type = PIN_STRUCT,
                                .type_name = component->type->name,
                                .size = component->type->size };
    return true;
}

bool behavior_reactive_dependencies(
    const behavior_asset *asset,
    const graph *g,
    const node *n,
    behavior_reactive_path **paths,
    int depth
)
{
    if (!n || depth > 128) {
        return false;
    }
    if (component_node_component(n) &&
        (component_node_kind_of(n->desc) == COMPONENT_NODE_GET ||
         n->desc->component_kind == NODE_COMPONENT_GET_SELF ||
         n->desc->component_kind == NODE_COMPONENT_GET_ENTITY)) {
        behavior_reactive_path path = { 0 };
        int value = 0;
        while (value < n->output_count && n->output_pins[value].kind != PIN_DATA) {
            value++;
        }
        path.valid = value < n->output_count &&
                     behavior_reactive_path_compile(g, n, value, &path, 0);
        apush(*paths, path);
        return path.valid;
    }
    for (int i = 0; i < n->input_count; i++) {
        const pin_binding *binding = n->input_pins[i].binding;
        if (binding && (binding->kind == PIN_BINDING_COMPONENT ||
                        binding->kind == PIN_BINDING_COMPONENT_FIELD)) {
            behavior_reactive_path path = { 0 };
            path.valid = compile_binding(binding, &path);
            apush(*paths, path);
            if (!path.valid) {
                return false;
            }
        }
        node *source;
        int pin;
        if (n->input_pins[i].kind == PIN_DATA &&
            graph_input_source(g, n, i, &source, &pin) &&
            !behavior_reactive_dependencies(asset, g, source, paths, depth + 1)) {
            return false;
        }
    }
    const graph *body = n->reference_id
                          ? behavior_asset_find_graph(asset, n->reference_id)
                          : NULL;
    for (int i = 0; body && i < acount(body->nodes); i++) {
        const node *child = body->nodes[i];
        if (child->id == body->entry_id || child->id == body->return_id) {
            continue;
        }
        if (!behavior_reactive_dependencies(asset, body, child, paths, depth + 1)) {
            return false;
        }
    }
    return true;
}

static bool same_output(const graph *g, const node *a, int ap, const node *b, int bp, int depth);
static bool same_binding(const pin_binding *a, const pin_binding *b);

static bool same_owner(const graph *g, const node *a, const node *b, int depth)
{
    node *as = NULL, *bs = NULL;
    int ap = -1, bp = -1;
    uint64_t av = 0, bv = 0;
    const pin_binding *ab = NULL, *bb = NULL;
    for (int i = 0; i < a->input_count; i++) {
        if (!strcmp(a->input_pins[i].name, "Entity")) {
            graph_input_source(g, a, i, &as, &ap);
            av = a->input_pins[i].default_value.u64;
            ab = a->input_pins[i].binding;
        }
    }
    for (int i = 0; i < b->input_count; i++) {
        if (!strcmp(b->input_pins[i].name, "Entity")) {
            graph_input_source(g, b, i, &bs, &bp);
            bv = b->input_pins[i].default_value.u64;
            bb = b->input_pins[i].binding;
        }
    }
    if (as && as->desc == &NODE_Self) {
        as = NULL;
        av = 0;
        ab = NULL;
    }
    if (bs && bs->desc == &NODE_Self) {
        bs = NULL;
        bv = 0;
        bb = NULL;
    }
    if (!as || !bs) {
        return !as && !bs && av == bv && same_binding(ab, bb);
    }
    return same_output(g, as, ap, bs, bp, depth + 1);
}

static bool same_output(const graph *g, const node *a, int ap, const node *b, int bp, int depth)
{
    if (!a || !b || ap < 0 || bp < 0 || depth > 128) {
        return false;
    }
    if (a == b && ap == bp) {
        return true;
    }
    const pin_spec *af = &a->output_pins[ap], *bf = &b->output_pins[bp];
    if (af->field_source || bf->field_source) {
        return af->field_source && bf->field_source && af->field_name &&
               bf->field_name && !strcmp(af->field_name, bf->field_name) &&
               same_output(
                   g,
                   a,
                   behavior_reactive_output_index(a, af->field_source),
                   b,
                   behavior_reactive_output_index(b, bf->field_source),
                   depth + 1
               );
    }
    const ecs_component_desc *ac = component_node_component(a),
                             *bc = component_node_component(b);
    if (ac && bc) {
        return ac->id == bc->id && same_owner(g, a, b, depth + 1);
    }
    return a->desc == &NODE_Self && b->desc == &NODE_Self;
}

static bool overlapping_output(const graph *g, const node *a, int ap, const node *b, int bp)
{
    behavior_reactive_path left = { 0 }, right = { 0 };
    bool same = behavior_reactive_path_compile(g, a, ap, &left, 0) &&
                behavior_reactive_path_compile(g, b, bp, &right, 0) &&
                left.self == right.self && left.owner == right.owner &&
                left.variable_id == right.variable_id && acount(left.steps) &&
                acount(left.steps) == acount(right.steps);
    for (int i = 0; same && i < acount(left.steps); i++) {
        same = left.steps[i].component == right.steps[i].component;
        if (i + 1 < acount(left.steps)) {
            same &= left.steps[i].offset == right.steps[i].offset;
        } else {
            same &= left.steps[i].offset < right.steps[i].offset + right.field.size &&
                    right.steps[i].offset < left.steps[i].offset + left.field.size;
        }
    }
    afree(left.steps);
    afree(right.steps);
    return same;
}

static bool depends(const graph *g, const node *value, int pin, const node *target, int target_pin, int depth)
{
    if (!value || pin < 0 || depth > 128) {
        return false;
    }
    if (same_output(g, value, pin, target, target_pin, 0) ||
        overlapping_output(g, value, pin, target, target_pin)) {
        return true;
    }
    for (int i = 0; i < value->input_count; i++) {
        node *source = NULL;
        int output;
        if (value->input_pins[i].kind == PIN_DATA &&
            graph_input_source(g, value, i, &source, &output) &&
            depends(g, source, output, target, target_pin, depth + 1)) {
            return true;
        }
    }
    return false;
}

static bool bind_depends(const graph *g, const behavior_reactive *r, const node *target, int pin)
{
    const node *value = graph_node_by_id(g, r->value_node_id);
    if (value) {
        return depends(g, value, behavior_reactive_output_index(value, r->value_pin_id), target, pin, 0);
    }
    const node *set = graph_node_by_id(g, r->statement_node_id);
    behavior_reactive_path destination = { 0 };
    bool tracked = behavior_reactive_path_compile(g, target, pin, &destination, 0);
    bool overlap = false;
    for (int i = 0; tracked && set && i < set->input_count; i++) {
        const pin_spec *input = &set->input_pins[i];
        if (!strcmp(input->name, "Entity") || !strcmp(input->name, "Target")) {
            continue;
        }
        behavior_reactive_path source = { 0 };
        if (!input->binding || !compile_binding(input->binding, &source)) {
            afree(source.steps);
            continue;
        }
        bool same = source.self == destination.self &&
                    source.owner == destination.owner &&
                    source.variable_id == destination.variable_id &&
                    acount(source.steps) &&
                    acount(source.steps) == acount(destination.steps);
        for (int k = 0; same && k < acount(source.steps); k++) {
            same = source.steps[k].component == destination.steps[k].component;
            if (k + 1 < acount(source.steps)) {
                same &= source.steps[k].offset == destination.steps[k].offset;
            } else {
                same &= source.steps[k].offset < destination.steps[k].offset +
                                                     destination.field.size &&
                        destination.steps[k].offset <
                            source.steps[k].offset + source.field.size;
            }
        }
        overlap |= same;
        afree(source.steps);
    }
    afree(destination.steps);
    return overlap;
}

static bool cycle(const unsigned char *edges, int count, int at, unsigned char *state)
{
    if (state[at] == 1) {
        return true;
    }
    if (state[at] == 2) {
        return false;
    }
    state[at] = 1;
    for (int i = 0; i < count; i++) {
        if (edges[at * count + i] && cycle(edges, count, i, state)) {
            return true;
        }
    }
    state[at] = 2;
    return false;
}

static bool pure_value(const behavior_asset *asset, const graph *g, const node *n, int depth)
{
    if (!n || !n->desc || depth > 128) {
        return false;
    }
    const node_desc *desc = n->desc;
    if (n->id == g->entry_id && desc == &NODE_FunctionEntry) {
        return true;
    }
    bool getter = component_node_kind_of(desc) == COMPONENT_NODE_GET ||
                  desc->component_kind == NODE_COMPONENT_GET_SELF ||
                  desc->component_kind == NODE_COMPONENT_GET_ENTITY;
    bool expression = n->reference_id &&
                      behavior_asset_find_graph(asset, n->reference_id);
    if (desc == &NODE_RandomReal || desc == &NODE_RandomRangeReal ||
        desc == &NODE_Random_Range_Vector ||
        (!getter && !expression && !node_desc_is_pure(desc)) ||
        (desc->access &&
         (desc->access->write_count ||
          (desc->access->flags & (NODE_EFFECT_HOST_SINK | NODE_EFFECT_STRUCTURAL))))) {
        return false;
    }
    for (int i = 0; i < n->input_count; i++) {
        node *source;
        int pin;
        if (n->input_pins[i].kind == PIN_DATA &&
            graph_input_source(g, n, i, &source, &pin) &&
            !pure_value(asset, g, source, depth + 1)) {
            return false;
        }
    }
    const graph *body = expression ? behavior_asset_find_graph(asset, n->reference_id)
                                   : NULL;
    for (int i = 0; body && i < acount(body->nodes); i++) {
        const node *child = body->nodes[i];
        if (child->id == body->entry_id || child->id == body->return_id) {
            continue;
        }
        if (!pure_value(asset, body, child, depth + 1)) {
            return false;
        }
    }
    return true;
}

bool behavior_reactive_validate(const behavior_asset *asset, char *error, size_t size)
{
    int count = acount(asset->reactives);
    if (count) {
        behavior_asset *derived = behavior_asset_clone(asset);
        bool consistent = derived &&
                          behavior_reactive_refresh(derived, error, size) &&
                          behavior_asset_equal(asset, derived);
        behavior_asset_free(derived);
        if (!consistent) {
            if (size && !error[0]) {
                snprintf(error, size, "Binding derived guard does not match its authored Set inputs");
            }
            return false;
        }
    }
    unsigned char *edges = xcalloc((size_t)count * count, 1);
    unsigned char *state = xcalloc((size_t)count, 1);
    bool valid = true;
    for (int i = 0; i < count && valid; i++) {
        const behavior_reactive *a = &asset->reactives[i];
        const graph *g = behavior_asset_find_graph(asset, a->graph_id);
        const node *event = g ? graph_node_by_id(g, a->event_id) : NULL;
        if (!g || g != asset->graph || !event || event->desc != &NODE_Event) {
            snprintf(error, size, "Reactive relationship has an invalid root event");
            valid = false;
            break;
        }
        if (a->kind != BEHAVIOR_REACTIVE_BIND) {
            const node *watched = graph_node_by_id(g, a->source_node_id);
            behavior_reactive_path path = { 0 };
            bool tracked = behavior_reactive_path_compile(
                               g,
                               watched,
                               behavior_reactive_output_index(watched, a->source_pin_id),
                               &path,
                               0
                           ) &&
                           path.field.type == PIN_BOOL && acount(path.steps);
            afree(path.steps);
            if (!tracked) {
                snprintf(error, size, "Change events require a static boolean ECS field path");
                valid = false;
            }
            continue;
        }
        const node *target = g ? graph_node_by_id(g, a->source_node_id) : NULL;
        int pin = behavior_reactive_output_index(target, a->source_pin_id);
        const node *rhs = g ? graph_node_by_id(g, a->value_node_id) : NULL;
        behavior_reactive_path source_path = { 0 };
        bool path_valid = target &&
                          behavior_reactive_path_compile(g, target, pin, &source_path, 0);
        afree(source_path.steps);
        behavior_reactive_path *dependencies = NULL;
        const node *dependency_root = rhs ? rhs : graph_node_by_id(g, a->statement_node_id);
        bool tracked = dependency_root &&
                       behavior_reactive_dependencies(asset, g, dependency_root, &dependencies, 0);
        for (int k = 0; k < acount(dependencies); k++) {
            afree(dependencies[k].steps);
        }
        afree(dependencies);
        if (!path_valid || !tracked) {
            snprintf(error, size, "Binding dependencies must use statically resolvable ECS paths");
            valid = false;
            break;
        }
        if (rhs && !pure_value(asset, g, rhs, 0)) {
            snprintf(error, size, "Binding value must be deterministic and have no side effects");
            valid = false;
            break;
        }
        for (int j = 0; j < count; j++) {
            const behavior_reactive *b = &asset->reactives[j];
            if (b->kind != BEHAVIOR_REACTIVE_BIND || a->graph_id != b->graph_id) {
                continue;
            }
            const node *other = graph_node_by_id(g, b->source_node_id);
            int other_pin = behavior_reactive_output_index(other, b->source_pin_id);
            if (i != j && (same_output(g, target, pin, other, other_pin, 0) ||
                           overlapping_output(g, target, pin, other, other_pin))) {
                snprintf(error, size, "Two bindings write the same component field");
                valid = false;
                break;
            }
            if (bind_depends(g, b, target, pin)) {
                edges[i * count + j] = 1;
            }
        }
    }
    for (int i = 0; i < count && valid; i++) {
        if (cycle(edges, count, i, state)) {
            snprintf(error, size, "Binding dependency cycle");
            valid = false;
        }
    }
    free(edges);
    free(state);
    return valid;
}

/* The Set node is authored. Its read-back and comparison tree are compiled
 * implementation, refreshed when Graph changes the Set's value or owner. */
typedef struct
{
    node *node;
    int pin;
    const pin_spec *literal;
} reactive_operand;

static int named_pin(const node *n, bool output, const char *name)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    for (int i = 0; i < count; i++) {
        if (!strcmp(pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static reactive_operand input_operand(const graph *g, const node *n, int pin)
{
    reactive_operand operand = { .literal = &n->input_pins[pin] };
    graph_input_source(g, n, pin, &operand.node, &operand.pin);
    return operand;
}

static bool same_text(const char *a, const char *b)
{
    return !strcmp(a ? a : "", b ? b : "");
}

static bool same_binding(const pin_binding *a, const pin_binding *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->kind == b->kind && a->variable_id == b->variable_id &&
           a->local_id == b->local_id && a->event_node_id == b->event_node_id &&
           a->event_pin_id == b->event_pin_id &&
           same_text(a->source_asset, b->source_asset) &&
           same_text(a->component_key, b->component_key) &&
           same_text(a->field_name, b->field_name);
}

static bool put_operand(graph *g, node *n, int pin, reactive_operand value)
{
    reactive_operand old = input_operand(g, n, pin);
    bool same_source = value.node && old.node == value.node && old.pin == value.pin;
    bool same_literal = !value.literal ||
                        (same_binding(old.literal->binding, value.literal->binding) &&
                         pin_value_equal(
                             &old.literal->default_value,
                             &value.literal->default_value
                         ));
    if ((same_source || (!value.node && !old.node)) && same_literal) {
        return true;
    }
    if (old.node && !same_source) {
        graph_remove_edge(g, old.node, old.pin, n, pin);
    }
    if (value.literal) {
        if (graph_set_pin_binding(g, n, 0, pin, value.literal->binding) != 0) {
            return false;
        }
        if (graph_set_pin_default(g, n, 0, pin, &value.literal->default_value) != 0) {
            return false;
        }
    } else {
        if (graph_set_pin_binding(g, n, 0, pin, NULL) != 0) {
            return false;
        }
    }
    if (value.node && !same_source) {
        return graph_connect_data(g, value.node, value.pin, n, pin) == 0;
    }
    return true;
}

static node *refresh_comparison(
    graph *g,
    node *old,
    reactive_operand left,
    reactive_operand right,
    const pin_spec *type,
    int depth
)
{
    if (depth > 16 || type->container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    if (type->type != PIN_STRUCT) {
        node *result = old && old->desc == &NODE_Not_Equal
                         ? old
                         : graph_add_node(g, &NODE_Not_Equal);
        for (int i = 0; i < 2; i++) {
            if (result->input_pins[i].type != type->type ||
                !same_text(result->input_pins[i].type_name, type->type_name)) {
                graph_set_pin_type(g, result, 0, i, type->type, type->type_name);
            }
        }
        return put_operand(g, result, 0, left) && put_operand(g, result, 1, right)
                 ? result
                 : NULL;
    }
    const type_desc *aggregate = type_catalog_find(type->type_name);
    if (!aggregate || !type_is_plain(aggregate) || !left.node || !right.node ||
        !aggregate->field_count) {
        return NULL;
    }
    /* Parser uses a left-associated OR over reflected leaves. Reuse that
     * exact tree, including every native ID, when the shape is unchanged. */
    node **leaves = NULL, **joins = NULL;
    node *cursor = old;
    for (int i = aggregate->field_count - 1; i > 0; i--) {
        node *a = NULL, *b = NULL;
        int pin;
        if (cursor && cursor->desc == &NODE_Or) {
            graph_input_source(g, cursor, 0, &a, &pin);
            graph_input_source(g, cursor, 1, &b, &pin);
            apush(joins, cursor);
        } else {
            apush(joins, NULL);
        }
        apush(leaves, b);
        cursor = a;
    }
    apush(leaves, cursor);
    node *result = NULL;
    for (int i = 0; i < aggregate->field_count; i++) {
        const type_field *field = &aggregate->fields[i];
        int a = graph_expose_output_field(g, left.node, left.pin, field->name);
        int b = graph_expose_output_field(g, right.node, right.pin, field->name);
        if (a < 0 || b < 0) {
            result = NULL;
            break;
        }
        pin_spec shape = { .type = field->type, .type_name = field->type_name };
        node *next = refresh_comparison(
            g,
            leaves[aggregate->field_count - 1 - i],
            (reactive_operand){ .node = left.node, .pin = a },
            (reactive_operand){ .node = right.node, .pin = b },
            &shape,
            depth + 1
        );
        if (!next) {
            result = NULL;
            break;
        }
        if (!result) {
            result = next;
            continue;
        }
        node *join = joins[aggregate->field_count - 1 - i];
        if (!join) {
            join = graph_add_node(g, &NODE_Or);
        }
        put_operand(g, join, 0, (reactive_operand){ .node = result });
        put_operand(g, join, 1, (reactive_operand){ .node = next });
        result = join;
    }
    afree(leaves);
    afree(joins);
    return result;
}

static void discard_guard(graph *g, node *n)
{
    if (!n || (n->desc != &NODE_Not_Equal && n->desc != &NODE_Or)) {
        return;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].from_id == n->id) {
            return;
        }
    }
    node *a = NULL, *b = NULL;
    int pin;
    graph_input_source(g, n, 0, &a, &pin);
    graph_input_source(g, n, 1, &b, &pin);
    graph_remove_node(g, n);
    discard_guard(g, a);
    if (a != b) {
        discard_guard(g, b);
    }
}

static void collect_guard_reads(const graph *g, const node *n, uint64_t **reads)
{
    if (!n || (n->desc != &NODE_Not_Equal && n->desc != &NODE_Or)) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        node *source = NULL;
        int pin;
        if (!graph_input_source(g, n, i, &source, &pin)) {
            continue;
        }
        if (n->desc == &NODE_Or) {
            collect_guard_reads(g, source, reads);
        } else if (
            component_node_component(source) &&
            (component_node_kind_of(source->desc) == COMPONENT_NODE_GET ||
             source->desc->component_kind == NODE_COMPONENT_GET_SELF ||
             source->desc->component_kind == NODE_COMPONENT_GET_ENTITY)
        ) {
            apush(*reads, source->id);
        }
    }
}

static void discard_unused_read(behavior_asset *asset, graph *g, uint64_t id)
{
    node *n = graph_node_by_id(g, id);
    if (!n) {
        return;
    }
    for (int i = 0; i < acount(asset->reactives); i++) {
        if (asset->reactives[i].source_node_id == id ||
            asset->reactives[i].value_node_id == id) {
            return;
        }
    }
    for (int i = 0; i < acount(g->locals); i++) {
        if (g->locals[i].source_id == id) {
            return;
        }
    }
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].from_id == id) {
            return;
        }
    }
    graph_remove_node(g, n);
}

static int exec_count(const graph *g, const node *n, int pin)
{
    const edge *first;
    return graph_exec_output_edges(g, n, pin, &first);
}

static bool read_has_authored_users(const graph *g, const behavior_reactive *r)
{
    behavior_reactive_anchor *anchors = behavior_reactive_anchors(g, r);
    bool authored = false;
    for (int i = 0; i < acount(g->locals); i++) {
        authored |= g->locals[i].source_id == r->source_node_id;
    }
    for (int i = 0; i < acount(g->edges) && !authored; i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != r->source_node_id) {
            continue;
        }
        bool guard = false;
        for (int k = 0; k < acount(anchors); k++) {
            guard |= anchors[k].id == e->to_id &&
                     !strncmp(anchors[k].kind, "reactive_guard_", 15);
        }
        authored = !guard;
    }
    afree(anchors);
    return authored;
}

static node *copy_component_read(graph *g, node *read)
{
    node *copy = graph_add_node(g, read->desc);
    if (component_node_kind_of(read->desc) == COMPONENT_NODE_GET &&
        component_node_configure(g, copy, component_node_key(read), NULL) != 0) {
        return NULL;
    }
    for (int i = 0; i < read->input_count; i++) {
        if (read->input_pins[i].kind != PIN_DATA ||
            (read->input_pins[i].flags & PIN_FLAG_SETTING)) {
            continue;
        }
        int destination = named_pin(copy, false, read->input_pins[i].name);
        if (destination < 0 ||
            !put_operand(g, copy, destination, input_operand(g, read, i))) {
            return NULL;
        }
    }
    return copy;
}

bool behavior_reactive_refresh(behavior_asset *asset, char *error, size_t size)
{
    for (int i = 0; i < acount(asset->reactives); i++) {
        behavior_reactive *r = &asset->reactives[i];
        if (r->kind != BEHAVIOR_REACTIVE_BIND) {
            continue;
        }
        graph *g = behavior_asset_find_graph(asset, r->graph_id);
        node *set = g ? graph_node_by_id(g, r->statement_node_id) : NULL;
        node *get = g ? graph_node_by_id(g, r->source_node_id) : NULL;
        node *event = g ? graph_node_by_id(g, r->event_id) : NULL;
        node *branch = NULL;
        int pin;
        const ecs_component_desc *component = set ? component_node_component(set)
                                                  : NULL;
        int input = -1, owner = -1;
        const char *field = NULL;
        bool generic = set && component_node_kind_of(set->desc) == COMPONENT_NODE_SET;
        if (component && generic) {
            owner = named_pin(set, false, "Entity");
            for (int k = 0; k < set->input_count; k++) {
                const pin_spec *p = &set->input_pins[k];
                if (p->kind != PIN_DATA || k == owner || (p->flags & PIN_FLAG_SETTING)) {
                    continue;
                }
                if (input >= 0) {
                    input = -1;
                    break;
                }
                input = k;
                field = strcmp(p->name, "Value") ? p->name : NULL;
            }
        } else if (component && set->desc->component_kind == NODE_COMPONENT_SET_FIELD) {
            field = component->type->fields[set->desc->component_field].name;
            input = named_pin(set, false, "Value");
            owner = named_pin(set, false, "Target");
        } else if (
            component && (set->desc->component_kind == NODE_COMPONENT_SET_ENTITY ||
                          set->desc->component_kind == NODE_COMPONENT_SET_SELF)
        ) {
            input = named_pin(set, false, "Value");
            owner = named_pin(set, false, "Target");
        }
        node *write = NULL;
        int write_pin;
        if (!get || input < 0 || !event || exec_count(g, event, 0) != 1 ||
            !graph_exec_output_target(g, event, 0, 0, &branch, &pin) ||
            branch->desc != &NODE_Branch || exec_count(g, branch, 0) != 1 ||
            exec_count(g, branch, 1) != 0 ||
            !graph_exec_output_target(g, branch, 0, 0, &write, &write_pin) ||
            write != set || exec_count(g, set, 0) != 0) {
            snprintf(error, size, "Binding requires one typed component Set and its owned guard");
            return false;
        }
        bool native_read = !component_node_kind_of(get->desc);
        int target = behavior_reactive_output_index(get, r->source_pin_id);
        int entity = named_pin(get, false, "Entity");
        reactive_operand owner_value = { 0 };
        bool self_owner = owner < 0;
        if (owner >= 0) {
            owner_value = input_operand(g, set, owner);
            self_owner = owner_value.node
                           ? owner_value.node->desc == &NODE_Self
                           : !owner_value.literal->binding &&
                                 !owner_value.literal->default_value.u64;
        }
        reactive_operand previous_owner = { 0 };
        bool previous_self = entity < 0;
        if (entity >= 0) {
            previous_owner = input_operand(g, get, entity);
            previous_self = previous_owner.node
                              ? previous_owner.node->desc == &NODE_Self
                              : !previous_owner.literal->binding &&
                                    !previous_owner.literal->default_value.u64;
        }
        bool same_entity = self_owner && previous_self;
        if (!self_owner && !previous_self) {
            same_entity = owner_value.node && previous_owner.node
                            ? same_output(
                                  g,
                                  owner_value.node,
                                  owner_value.pin,
                                  previous_owner.node,
                                  previous_owner.pin,
                                  0
                              )
                            : !owner_value.node && !previous_owner.node &&
                                  same_binding(
                                      owner_value.literal->binding,
                                      previous_owner.literal->binding
                                  ) &&
                                  pin_value_equal(
                                      &owner_value.literal->default_value,
                                      &previous_owner.literal->default_value
                                  );
        }
        if ((!same_entity || component_node_component(get) != component) &&
            read_has_authored_users(g, r)) {
            get = copy_component_read(g, get);
            if (!get) {
                snprintf(error, size, "Binding target read cannot be isolated from authored consumers");
                return false;
            }
            r->source_node_id = get->id;
            entity = named_pin(get, false, "Entity");
        }
        if (component_node_component(get) != component || (!self_owner && entity < 0)) {
            if (component_node_kind_of(get->desc) != COMPONENT_NODE_GET &&
                !component_node_raise(g, get)) {
                snprintf(error, size, "Binding target cannot be represented by a component read");
                return false;
            }
            if (component_node_component(get) != component &&
                component_node_retarget(g, get, component->key) != 0) {
                return false;
            }
            target = named_pin(get, true, "Value");
            entity = named_pin(get, false, "Entity");
        }
        if (!self_owner && !put_operand(g, get, entity, owner_value)) {
            snprintf(error, size, "Binding owner must have a compatible entity path");
            return false;
        }
        if (self_owner && entity >= 0) {
            reactive_operand previous = input_operand(g, get, entity);
            if (!previous.node || previous.node->desc != &NODE_Self) {
                pin_spec self = {
                    .type = PIN_HANDLE,
                    .type_name = "ecs.entity",
                    .default_value = { .type = PIN_HANDLE, .type_name = "ecs.entity" }
                };
                if (!put_operand(g, get, entity, (reactive_operand){ .literal = &self })) {
                    return false;
                }
            }
        }
        if (native_read && component_node_lower(g, get, error, size) != 0) {
            return false;
        }
        if (component_node_kind_of(get->desc) == COMPONENT_NODE_GET) {
            target = named_pin(get, true, "Value");
        } else {
            for (int k = 0; k < get->output_count; k++) {
                if (get->output_pins[k].kind == PIN_DATA &&
                    !get->output_pins[k].field_source) {
                    target = k;
                    break;
                }
            }
        }
        if (field) {
            target = graph_expose_output_field(g, get, target, field);
        }
        node *old = NULL;
        int old_pin, condition = named_pin(branch, false, "Condition");
        if (target < 0 || condition < 0) {
            snprintf(error, size, "Binding target field is not reflected");
            return false;
        }
        graph_input_source(g, branch, condition, &old, &old_pin);
        uint64_t *obsolete_reads = NULL;
        collect_guard_reads(g, old, &obsolete_reads);
        reactive_operand rhs = input_operand(g, set, input);
        node *comparison = refresh_comparison(
            g,
            old,
            (reactive_operand){ .node = get, .pin = target },
            rhs,
            &get->output_pins[target],
            0
        );
        if (!comparison ||
            !put_operand(g, branch, condition, (reactive_operand){ .node = comparison })) {
            snprintf(error, size, "Binding target must have a reflected inline comparable value");
            afree(obsolete_reads);
            return false;
        }
        if (comparison != old) {
            discard_guard(g, old);
        }
        r->source_pin_id = get->output_pins[target].id;
        r->value_node_id = rhs.node ? rhs.node->id : 0;
        r->value_pin_id = rhs.node ? rhs.node->output_pins[rhs.pin].id : 0;
        for (int k = 0; k < acount(obsolete_reads); k++) {
            discard_unused_read(asset, g, obsolete_reads[k]);
        }
        afree(obsolete_reads);
    }
    return true;
}

static void reactive_anchor_add(behavior_reactive_anchor **anchors, uint64_t id, const char *kind)
{
    if (!id) {
        return;
    }
    behavior_reactive_anchor anchor = { .id = id };
    snprintf(anchor.kind, sizeof anchor.kind, "%s", kind);
    apush(*anchors, anchor);
}

static void reactive_guard_anchors(
    const graph *g,
    const node *n,
    behavior_reactive_anchor **anchors,
    int *ordinal
)
{
    if (!n || (n->desc != &NODE_Not_Equal && n->desc != &NODE_Or)) {
        return;
    }
    char kind[48];
    snprintf(kind, sizeof kind, "reactive_guard_%d", (*ordinal)++);
    reactive_anchor_add(anchors, n->id, kind);
    if (n->desc == &NODE_Or) {
        for (int i = 0; i < 2; i++) {
            node *child = NULL;
            int pin;
            graph_input_source(g, n, i, &child, &pin);
            reactive_guard_anchors(g, child, anchors, ordinal);
        }
    }
}

behavior_reactive_anchor *behavior_reactive_anchors(const graph *g, const behavior_reactive *r)
{
    behavior_reactive_anchor *anchors = NULL;
    reactive_anchor_add(&anchors, r->event_id, "node");
    reactive_anchor_add(&anchors, r->source_node_id, "reactive_target");
    if (r->kind != BEHAVIOR_REACTIVE_BIND) {
        return anchors;
    }
    reactive_anchor_add(&anchors, r->statement_node_id, "reactive_write");
    reactive_anchor_add(&anchors, r->value_node_id, "reactive_value");
    const node *event = graph_node_by_id(g, r->event_id);
    node *branch = NULL, *guard = NULL;
    int pin;
    if (event && graph_exec_output_target(g, event, 0, 0, &branch, &pin) &&
        branch->desc == &NODE_Branch) {
        reactive_anchor_add(&anchors, branch->id, "reactive_branch");
        int condition = named_pin(branch, false, "Condition");
        if (condition >= 0) {
            graph_input_source(g, branch, condition, &guard, &pin);
            int ordinal = 0;
            reactive_guard_anchors(g, guard, &anchors, &ordinal);
        }
    }
    return anchors;
}
