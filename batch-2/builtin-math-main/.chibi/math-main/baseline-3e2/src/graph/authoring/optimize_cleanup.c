#include "graph/authoring/constant.h"
#include "graph/authoring/graph_edit.h"
#include "graph/authoring/optimize_internal.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "support/arena.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cleanup never discards debug state, even when the caller elects to rename
 * debug fanouts. Rewrites also leave authored routes and dormant bindings alone. */
static bool editable(const graph *g, const node *n)
{
    if (n->disabled || n->breakpoint || n->breakpoint_enabled ||
        node_behavior_for(n) != NODE_BEHAVIOR_PURE || n->id == g->entry_id ||
        n->id == g->return_id || n->desc->flow_kind != NODE_FLOW_NONE ||
        n->desc->latent) {
        return false;
    }
    const node_access *access = n->desc->access;
    if (access && (access->read_count || access->write_count ||
                   (access->flags & ~NODE_EFFECT_PURE))) {
        return false;
    }
    for (int p = 0; p < n->input_count; p++) {
        if (n->input_pins[p].binding || (n->input_pins[p].flags & PIN_FLAG_WATCH)) {
            return false;
        }
    }
    for (int p = 0; p < n->output_count; p++) {
        if ((n->output_pins[p].flags & PIN_FLAG_WATCH) || graph_output_local(g, n, p)) {
            return false;
        }
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != n->id && e->to_id != n->id) {
            continue;
        }
        if (e->kind != GRAPH_EDGE_DATA || e->custom_handles ||
            e->reroute_count || graph_edge_is_local(g, e)) {
            return false;
        }
        const node *peer = graph_node_by_id(g, e->from_id == n->id ? e->to_id : e->from_id);
        if (!peer || peer->disabled || peer->breakpoint || peer->breakpoint_enabled) {
            return false;
        }
        for (int p = 0; p < peer->input_count; p++) {
            if (peer->input_pins[p].flags & PIN_FLAG_WATCH) {
                return false;
            }
        }
        for (int p = 0; p < peer->output_count; p++) {
            if (peer->output_pins[p].flags & PIN_FLAG_WATCH) {
                return false;
            }
        }
    }
    return true;
}

static bool debug_state(const node *n)
{
    if (n->disabled || n->breakpoint || n->breakpoint_enabled) {
        return true;
    }
    for (int p = 0; p < n->input_count; p++) {
        if (n->input_pins[p].flags & PIN_FLAG_WATCH) {
            return true;
        }
    }
    for (int p = 0; p < n->output_count; p++) {
        if (n->output_pins[p].flags & PIN_FLAG_WATCH) {
            return true;
        }
    }
    return false;
}

static bool component_field_prefix(const graph *g, const node *source, int pin, char *path, size_t cap)
{
    if (!graph_optimize_component_getter(g, source) || pin < 0 ||
        pin >= source->output_count || !cap) {
        return false;
    }
    if (pin == 0) {
        path[0] = '\0';
        return true;
    }
    int aggregate = -1;
    const type_field *field = graph_output_field(source, pin, &aggregate);
    if (!field || aggregate != 0) {
        return false;
    }
    int length = snprintf(path, cap, "%s", field->name);
    return length >= 0 && (size_t)length < cap;
}

static bool append_field_path(const char *prefix, const char *field, char *path, size_t cap)
{
    int length = snprintf(
        path,
        cap,
        "%s%s%s",
        prefix && prefix[0] ? prefix : "",
        prefix && prefix[0] ? "." : "",
        field
    );
    return length >= 0 && (size_t)length < cap;
}

typedef struct replacement
{
    node *target;
    int input;
    int output;
    const node_desc *conversion;
    pin_spec expected;
    char component_field_path[256];
} replacement;

static bool transparent(graph *g, node *n, graph_optimize_report *report)
{
    /* A Break Struct node projects like the Break_<T> it lowers to. */
    const node_desc *d = component_node_target(g, n);
    bool projection = d && d->pure_kind == NODE_PURE_BREAK;
    if (!d || (!projection && d->pure_kind != NODE_PURE_COPY) ||
        n->input_count != 1 + (d != n->desc) || !editable(g, n)) {
        return false;
    }
    node *source = NULL;
    int pin = -1;
    if (!graph_input_source(g, n, 0, &source, &pin) || source == n) {
        return false;
    }
    const node_desc *incoming = graph_input_conversion(g, n, 0);
    const type_desc *type = projection ? graph_output_struct(source, pin) : NULL;
    char component_prefix[256];
    const node_desc *getter = graph_optimize_component_getter(g, source);
    bool component_projection = projection && getter;
    if (component_projection &&
        !component_field_prefix(g, source, pin, component_prefix, sizeof component_prefix)) {
        return false;
    }
    if (component_projection && pin != 0) {
        int aggregate = -1;
        const type_field *field = graph_output_field(source, pin, &aggregate);
        type = field && aggregate == 0 ? type_catalog_find(field->type_name) : NULL;
    }
    if (projection && (!type || type != d->type || incoming ||
                       n->output_count != type->field_count)) {
        return false;
    }
    if (!projection && n->output_count != 1) {
        return false;
    }
    replacement *replacements = NULL;
    bool valid = true;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != n->id) {
            continue;
        }
        node *target = graph_node_by_id(g, e->to_id);
        int input = -1, output = -1;
        const pin_spec *dest = graph_pin_by_id(target, e->to_pin_id, NULL, &input);
        graph_pin_by_id(n, e->from_pin_id, NULL, &output);
        if (!dest || dest->binding || output < 0 || (incoming && e->conversion)) {
            valid = false;
            break;
        }
        pin_spec from = projection
                          ? (pin_spec){ .kind = PIN_DATA,
                                        .type = type->fields[output].type,
                                        .type_name = type->fields[output].type_name }
                          : source->output_pins[pin];
        const node_desc *conversion = incoming ? incoming : e->conversion;
        if (conversion ? !conversion_matches_pins(&from, dest, conversion)
                       : !pin_types_compatible(&from, dest)) {
            valid = false;
            break;
        }
        replacement replacement_value = { .target = target,
                                          .input = input,
                                          .output = output,
                                          .conversion = conversion };
        if (component_projection &&
            (conversion || !append_field_path(
                               component_prefix,
                               type->fields[output].name,
                               replacement_value.component_field_path,
                               sizeof replacement_value.component_field_path
                           ))) {
            valid = false;
            break;
        }
        apush(replacements, replacement_value);
    }
    if (!valid || !acount(replacements)) {
        afree(replacements);
        return false;
    }
    if (projection && !component_projection &&
        graph_expose_output_fields(g, source, pin)) {
        afree(replacements);
        return false;
    }
    for (int i = 0; i < acount(replacements); i++) {
        replacement r = replacements[i];
        if (component_projection) {
            pin_binding binding = { .kind = PIN_BINDING_COMPONENT_FIELD,
                                    .component_key = getter->binding_component,
                                    .field_name = r.component_field_path };
            int set_pin_binding_result = graph_set_pin_binding(g, r.target, 0, r.input, &binding);
            assert(set_pin_binding_result == 0);
            (void)set_pin_binding_result;
            int remove_edge_result = graph_remove_edge(g, n, r.output, r.target, r.input);
            assert(remove_edge_result == 0);
            (void)remove_edge_result;
            report->folded_inputs++;
            report->changed_edges++;
            continue;
        }
        int source_pin = pin;
        if (projection) {
            source_pin = -1;
            for (int p = 0; p < source->output_count; p++) {
                int aggregate = -1;
                const type_field *field = graph_output_field(source, p, &aggregate);
                if (field == &type->fields[r.output] && aggregate == pin) {
                    source_pin = p;
                    break;
                }
            }
            assert(source_pin >= 0);
        }
        int connect_data_conversion_result = graph_connect_data_conversion(
            g,
            source,
            source_pin,
            r.target,
            r.input,
            r.conversion
        );
        assert(connect_data_conversion_result == 0);
        (void)connect_data_conversion_result;
        report->changed_edges++;
    }
    afree(replacements);
    int remove_node_result = graph_remove_node(g, n);
    assert(remove_node_result == 0);
    (void)remove_node_result;
    report->changed_edges++;
    report->removed_nodes++;
    if (component_projection) {
        int hide_output_fields_result = graph_hide_output_fields(g, source, pin);
        assert(hide_output_fields_result == 0);
        (void)hide_output_fields_result;
        bool has_edges = false;
        for (int i = 0; i < acount(g->edges); i++) {
            has_edges |= g->edges[i].from_id == source->id;
        }
        if (!has_edges && !debug_state(source)) {
            remove_node_result = graph_remove_node(g, source);
            assert(remove_node_result == 0);
            (void)remove_node_result;
            report->removed_nodes++;
        }
    }
    return true;
}

/* A component node has the effects of the node it lowers to. */
static bool side_effect_free(const graph *g, const node *n)
{
    const node_desc *d = component_node_target(g, n);
    if (!d || (d->access && (d->access->read_count || d->access->write_count ||
                             (d->access->flags & ~NODE_EFFECT_PURE)))) {
        return false;
    }
    switch (d->pure_kind) {
        case NODE_PURE_CONST:
        case NODE_PURE_COPY:
        case NODE_PURE_MAKE:
        case NODE_PURE_BREAK: return true;
        case NODE_PURE_CALL:
            return d->foldable || (d->access && d->access->flags == NODE_EFFECT_PURE);
        default: return false;
    }
}

static bool disconnected(const graph *g, const node *n)
{
    bool executable = false;
    for (int i = 0; i < acount(g->nodes); i++) {
        executable |= node_behavior_for(g->nodes[i]) != NODE_BEHAVIOR_PURE;
    }
    if (!executable || g->dataflow || !g->owner ||
        g->owner->domain != BEHAVIOR_DOMAIN_EVENT) {
        return false;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].from_id == n->id) {
            return false;
        }
    }
    return true;
}

static bool constant(graph *g, node *n, graph_optimize_report *report)
{
    const node_desc *d = n->desc;
    if (!d->foldable || d->pure_kind != NODE_PURE_CALL || d->read ||
        d->read_inputs || n->input_count != d->input_count || d->output_count != 1 ||
        n->output_count != 1 || !side_effect_free(g, n) || !editable(g, n)) {
        return false;
    }
    for (int p = 0; p < n->input_count; p++) {
        node *source = NULL;
        int pin = -1;
        const pin_spec *input = &n->input_pins[p];
        if (input->type != d->inputs[p].type ||
            !pin_types_compatible(input, &d->inputs[p]) || input->type == PIN_FLOW ||
            input->binding || (input->flags & PIN_FLAG_DEFAULT_SELF) ||
            graph_input_source(g, n, p, &source, &pin)) {
            return false;
        }
    }
    replacement *replacements = NULL;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != n->id) {
            continue;
        }
        node *target = graph_node_by_id(g, e->to_id);
        int pin = -1;
        const pin_spec *input = graph_pin_by_id(target, e->to_pin_id, NULL, &pin);
        if (!input || input->binding || e->conversion ||
            input->flags & PIN_FLAG_DEFAULT_SELF) {
            afree(replacements);
            return false;
        }
        apush(replacements, ((replacement){ .target = target, .input = pin, .expected = *input }));
    }
    if (!acount(replacements)) {
        afree(replacements);
        return false;
    }
    arena storage;
    arena_init(&storage, 4096);
    pin_value value = { 0 };
    bool valid = graph_constant_value(g, n, &storage, &value);
    // JSON cannot preserve non-finite numbers. Leave those calculations authored.
    valid &= value.type != PIN_FLOW && (value.type != PIN_F32 || isfinite(value.f)) &&
             (value.type != PIN_F64 || isfinite(value.d));
    if (valid) {
        int remove_node_result = graph_remove_node(g, n);
        assert(remove_node_result == 0);
        (void)remove_node_result;
        for (int i = 0; i < acount(replacements); i++) {
            replacement r = replacements[i];
            pin_value converted = value;
            pin_coerce(&converted, &r.expected);
            int set_pin_default_result = graph_set_pin_default(g, r.target, 0, r.input, &converted);
            assert(set_pin_default_result == 0);
            (void)set_pin_default_result;
            report->changed_edges++;
            report->folded_inputs++;
        }
        report->removed_nodes++;
    }
    arena_free(&storage);
    afree(replacements);
    return valid;
}

static int compare_ids(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

void graph_optimize_cleanup(graph *g, graph_optimize_report *report)
{
    uint64_t *ids = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        apush(ids, g->nodes[i]->id);
    }
    if (acount(ids)) {
        asort(ids, compare_ids);
    }
    for (int pass = 0; pass < 3; pass++) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < acount(ids); i++) {
                node *n = graph_node_by_id(g, ids[i]);
                if (!n) {
                    continue;
                }
                if (pass == 0) {
                    changed = transparent(g, n, report);
                }
                if (pass == 1 && editable(g, n) && side_effect_free(g, n) &&
                    disconnected(g, n)) {
                    int edges = acount(g->edges);
                    int remove_node_result = graph_remove_node(g, n);
                    assert(remove_node_result == 0);
                    (void)remove_node_result;
                    report->removed_nodes++;
                    report->changed_edges += edges - acount(g->edges);
                    changed = true;
                }
                if (pass == 2) {
                    changed = constant(g, n, report);
                }
                if (changed) {
                    break;
                }
            }
        }
    }
    afree(ids);
}
