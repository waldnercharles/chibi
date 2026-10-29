#include "graph/authoring/graph_edit.h"
#include "graph/authoring/local_edit.h"
#include "graph/authoring/optimize_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "support/arena.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

graph_optimize_options graph_optimize_defaults()
{
    return (graph_optimize_options){ .fanout_threshold = 3,
                                     .preserve_debug = true,
                                     .local_aliases = true };
}

void graph_optimize_report_free(graph_optimize_report *report)
{
    afree(report->skipped);
    *report = (graph_optimize_report){ 0 };
}

static bool same_string(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

static bool same_binding(const pin_binding *a, const pin_binding *b)
{
    return a && b && a->kind == b->kind && a->variable_id == b->variable_id &&
           a->local_id == b->local_id &&
           same_string(a->source_asset, b->source_asset) &&
           same_string(a->component_key, b->component_key) &&
           same_string(a->field_name, b->field_name);
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

const node_desc *graph_optimize_component_getter(const graph *g, const node *n)
{
    const node_desc *d = component_node_getter(g, n);
    bool valid = d && d->binding_component && d->name &&
                 registry_node_by_name(d->name) == d &&
                 d->pure_kind == NODE_PURE_READ && d->read && !d->read_inputs &&
                 d->input_count == 0 && (n->desc != d || n->input_count == 0) &&
                 d->output_count == 1 && d->type && d->access &&
                 d->access->read_count == 1 && d->access->reads &&
                 d->access->write_count == 0 &&
                 d->access->flags == NODE_EFFECT_ENTITY_SCOPED;
    return valid ? d : NULL;
}

static bool binding_domain(const graph *g)
{
    return g->owner && g->owner->domain == BEHAVIOR_DOMAIN_EVENT && !g->dataflow;
}

static int direct_binding(
    const graph *g,
    const node *source,
    int pin,
    const pin_spec *input,
    const node_desc *conversion,
    pin_binding *binding
)
{
    const pin_spec *output = &source->output_pins[pin];
    const node_desc *getter = graph_optimize_component_getter(g, source);
    if (getter) {
        int aggregate = pin;
        const type_field *field = graph_output_field(source, pin, &aggregate);
        if (aggregate != 0 || (pin != 0 && !field) ||
            source->output_pins[0].type != PIN_STRUCT ||
            !same_string(source->output_pins[0].type_name, getter->type->name)) {
            return GRAPH_OPTIMIZE_SEMANTICS;
        }
        pin_spec value = field ? (pin_spec){ .kind = PIN_DATA,
                                             .type = field->type,
                                             .type_name = field->type_name,
                                             .container = PIN_CONTAINER_SINGLE }
                               : source->output_pins[0];
        bool unwrap_conversion = false;
        if (conversion && field) {
            if (!conversion_matches_pins(&value, input, conversion)) {
                return GRAPH_OPTIMIZE_CONVERSION;
            }
        } else if (conversion) {
            char unwrap[160];
            snprintf(unwrap, sizeof unwrap, "Unwrap_%s", getter->type->name);
            if (field || getter->type->field_count != 1 ||
                conversion != registry_node_by_name(unwrap)) {
                return GRAPH_OPTIMIZE_CONVERSION;
            }
            field = &getter->type->fields[0];
            unwrap_conversion = true;
            value = (pin_spec){ .kind = PIN_DATA,
                                .type = field->type,
                                .type_name = field->type_name,
                                .container = PIN_CONTAINER_SINGLE };
        }
        if ((!conversion && !pin_types_compatible(&value, input)) ||
            (unwrap_conversion && !pin_types_compatible(&value, input))) {
            return GRAPH_OPTIMIZE_CONVERSION;
        }
        *binding = (pin_binding){ .kind = field ? PIN_BINDING_COMPONENT_FIELD
                                                : PIN_BINDING_COMPONENT,
                                  .component_key = getter->binding_component,
                                  .field_name = field ? field->name : NULL };
        return -1;
    }
    if ((conversion && !conversion_matches_pins(output, input, conversion)) ||
        (!conversion && !pin_types_compatible(output, input))) {
        return GRAPH_OPTIMIZE_CONVERSION;
    }
    bool setter = source->desc == &NODE_SetVariable;
    if ((!setter && (pin || source->output_count != 1 || source->input_count)) ||
        (setter && (pin != 1 || source->input_count != 2 || source->output_count != 2))) {
        return GRAPH_OPTIMIZE_SEMANTICS;
    }
    if (source->desc == &NODE_GetVariable || setter) {
        behavior_variable
            *variable = behavior_asset_find_variable_by_id(g->owner, source->reference_id);
        if (!variable || source->reference_asset) {
            return GRAPH_OPTIMIZE_SEMANTICS;
        }
        pin_spec value = { .kind = PIN_DATA,
                           .type = variable->type,
                           .type_name = variable->type_name,
                           .container = variable->container };
        if (!pin_types_compatible(&value, output)) {
            return GRAPH_OPTIMIZE_SEMANTICS;
        }
        *binding = (pin_binding){ .kind = PIN_BINDING_VARIABLE,
                                  .variable_id = variable->id,
                                  .source_asset = g->owner->name };
        return -1;
    }
    if (source->desc == &NODE_Self && (input->flags & PIN_FLAG_DEFAULT_SELF) &&
        !input->default_value.u64 && !input->binding) {
        return -1;
    }
    return GRAPH_OPTIMIZE_SEMANTICS;
}

typedef struct fold
{
    edge edge;
    node *target;
    int source_pin;
    int target_pin;
    pin_binding binding;
} fold;

static int collect_folds(graph *g, node *source, bool preserve_debug, fold **folds)
{
    if (!binding_domain(g)) {
        return GRAPH_OPTIMIZE_BACKEND;
    }
    if (source->disabled || (preserve_debug && debug_state(source))) {
        return GRAPH_OPTIMIZE_DEBUG;
    }
    bool setter = source->desc == &NODE_SetVariable;
    for (int p = 0; p < source->output_count; p++) {
        if (graph_output_local(g, source, p) && !setter) {
            return GRAPH_OPTIMIZE_SEMANTICS;
        }
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->to_id == source->id && !setter) {
            return GRAPH_OPTIMIZE_SEMANTICS;
        }
        if (e->from_id != source->id) {
            continue;
        }
        if (setter && e->kind == GRAPH_EDGE_EXEC) {
            continue;
        }
        if (e->kind != GRAPH_EDGE_DATA) {
            return GRAPH_OPTIMIZE_SEMANTICS;
        }
        if (e->reroute_count || e->custom_handles) {
            return GRAPH_OPTIMIZE_WIRE_ROUTE;
        }
        fold f = { .edge = *e, .target = graph_node_by_id(g, e->to_id) };
        int output = 0;
        const pin_spec *in = graph_pin_by_id(f.target, e->to_pin_id, &output, &f.target_pin);
        if (!in || output ||
            !graph_pin_by_id(source, e->from_pin_id, NULL, &f.source_pin) ||
            in->kind != PIN_DATA || in->type == PIN_FLOW ||
            (in->flags & PIN_FLAG_SETTING)) {
            return GRAPH_OPTIMIZE_PIN;
        }
        if (f.target->disabled || (preserve_debug && debug_state(f.target))) {
            return GRAPH_OPTIMIZE_DEBUG;
        }
        /* Keep a structured getter edge intact until Break cleanup can convert
         * its scalar fields to nested component bindings. */
        const node_desc *made = component_node_struct(f.target);
        if (graph_optimize_component_getter(g, source) && made &&
            made->pure_kind == NODE_PURE_BREAK &&
            source->output_pins[f.source_pin].type == PIN_STRUCT) {
            return GRAPH_OPTIMIZE_SEMANTICS;
        }
        int reason = direct_binding(g, source, f.source_pin, in, e->conversion, &f.binding);
        if (reason >= 0) {
            return reason;
        }
        const graph_local *local = graph_input_local(g, f.target, f.target_pin);
        bool own_local = setter && local && local->source_id == source->id &&
                         local->source_pin_id == e->from_pin_id;
        if (in->binding && !own_local && !same_binding(in->binding, &f.binding)) {
            return GRAPH_OPTIMIZE_DORMANT_BINDING;
        }
        if (setter && !graph_set_output_is_current(g, source, f.target)) {
            return GRAPH_OPTIMIZE_SEMANTICS;
        }
        apush(*folds, f);
    }
    return acount(*folds) ? -1 : GRAPH_OPTIMIZE_SEMANTICS;
}

static void apply_folds(graph *g, node *source, fold *folds, graph_optimize_report *report)
{
    bool setter = source->desc == &NODE_SetVariable;
    const graph_local *local = setter ? graph_output_local(g, source, 1) : NULL;
    if (local) {
        int result = graph_expand_local(g, local->id);
        assert(result == 0);
        (void)result;
    }
    for (int i = 0; i < acount(folds); i++) {
        const fold *f = &folds[i];
        const pin_spec input = f->target->input_pins[f->target_pin];
        // Store the binding before removing the edge so resolved wildcard types survive.
        if (f->binding.kind) {
            int set_pin_binding_result = graph_set_pin_binding(g, f->target, 0, f->target_pin, &f->binding);
            assert(set_pin_binding_result == 0);
            (void)set_pin_binding_result;
        }
        int remove_edge_result = graph_remove_edge(g, source, f->source_pin, f->target, f->target_pin);
        assert(remove_edge_result == 0);
        (void)remove_edge_result;
        if (!f->binding.kind) {
            pin_value self = { .type = input.type,
                               .type_name = input.type_name,
                               .container = input.container };
            int set_pin_default_result = graph_set_pin_default(g, f->target, 0, f->target_pin, &self);
            assert(set_pin_default_result == 0);
            (void)set_pin_default_result;
        }
        report->folded_inputs++;
        report->changed_edges++;
    }
    if (setter) {
        return;
    }
    int remove_node_result = graph_remove_node(g, source);
    assert(remove_node_result == 0);
    (void)remove_node_result;
    report->removed_nodes++;
}

/* Self is immutable for an instance. The existing inline-source representation
 * handles explicit entity inputs, conversions, and dormant defaults unchanged.
 * Each use owns one Self node, as required by graph_node_is_inline(). */
static bool inline_self(graph *g, node *source, bool preserve_debug, graph_optimize_report *report)
{
    if (source->desc != &NODE_Self || source->input_count ||
        source->output_count != 1 || !binding_domain(g) || source->disabled ||
        source->presentation == NODE_PRESENTATION_EXPANDED ||
        graph_node_is_inline(g, source) || (preserve_debug && debug_state(source))) {
        return false;
    }
    fold *uses = NULL;
    bool valid = true;
    for (int i = 0; valid && i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->to_id == source->id) {
            valid = false;
            break;
        }
        if (e->from_id != source->id) {
            continue;
        }
        fold f = { .edge = *e, .target = graph_node_by_id(g, e->to_id) };
        int output = 0;
        const pin_spec *input = graph_pin_by_id(f.target, e->to_pin_id, &output, &f.target_pin);
        const pin_spec *from = &source->output_pins[0];
        if (e->kind != GRAPH_EDGE_DATA || e->from_pin_id != from->id ||
            e->reroute_count || e->custom_handles || !input || output ||
            input->kind != PIN_DATA || input->type == PIN_FLOW ||
            ((input->flags | from->flags) & PIN_FLAG_SETTING) ||
            f.target->disabled || (preserve_debug && debug_state(f.target)) ||
            (e->conversion ? !conversion_matches_pins(from, input, e->conversion)
                           : !pin_types_compatible(from, input))) {
            valid = false;
            break;
        }
        apush(uses, f);
    }
    if (!valid || !acount(uses)) {
        afree(uses);
        return false;
    }
    const graph_local *local = graph_output_local(g, source, 0);
    if (local) {
        int result = graph_expand_local(g, local->id);
        assert(result == 0);
        (void)result;
    }
    node **selves = NULL;
    for (int i = 0; i < acount(uses); i++) {
        const fold *use = &uses[i];
        node *self = source;
        if (i) {
            self = graph_add_node(g, &NODE_Self);
            int typed = graph_set_pin_type(
                g,
                self,
                1,
                0,
                source->output_pins[0].type,
                source->output_pins[0].type_name
            );
            int connected = graph_connect_data_conversion(
                g,
                self,
                0,
                use->target,
                use->target_pin,
                use->edge.conversion
            );
            assert(typed == 0 && connected == 0);
            (void)typed;
            (void)connected;
        }
        apush(selves, self);
        report->folded_inputs++;
        report->changed_edges++;
    }
    for (int i = 0; i < acount(selves); i++) {
        int inlined = graph_inline_node(g, selves[i]);
        assert(inlined == 0);
        (void)inlined;
    }
    afree(selves);
    afree(uses);
    return true;
}

bool graph_fold_component_getter(graph *g, node *source)
{
    if (!g || g->flow_pause_count || !source ||
        !graph_optimize_component_getter(g, source)) {
        return false;
    }
    fold *folds = NULL;
    int reason = collect_folds(g, source, true, &folds);
    bool accepted = reason < 0 && acount(folds) == 1;
    if (accepted) {
        graph_optimize_report report = { 0 };
        apply_folds(g, source, folds, &report);
    }
    afree(folds);
    return accepted;
}

static void skip(graph_optimize_report *report, const node *n, uint64_t pin, int reason)
{
    apush(report->skipped, ((graph_optimize_skip){ n->id, pin, (graph_optimize_skip_reason)reason }));
}

void graph_local_name(const graph *g, const node *n, int pin, char *name, size_t size)
{
    const char *source = n->output_pins[pin].name;
    const char *verb = NULL, *addressed = NULL;
    bool component = component_node_kind_of(n->desc) &&
                     component_node_access(n, &verb, &addressed);
    /* An expression's reference name is its whole source; a name that long
       reads worse than a plain Value the author renames. */
    if (!source || !strcmp(source, "Value") || !strcmp(source, "Result")) {
        source = component                           ? addressed
               : n->desc == &NODE_BehaviorExpression ? "Value"
               : n->reference_name                   ? n->reference_name
               : graph_node_name_literal(g, n) ? graph_node_name_literal(g, n)
                                               : n->desc->name;
    }
    if (!strncmp(source, "Shader ", 7) || !strncmp(source, "Field ", 6)) {
        source = strchr(source, ' ') + 1;
    }
    if (!strcmp(n->desc->name, "Spawn Prototype")) {
        source = "SpawnedEntity";
    }
    if (!strncmp(source, "Get_", 4)) {
        source += 4;
    }
    char base[100];
    int at = 0;
    bool upper = true;
    for (; *source && at < (int)sizeof base - 1; source++) {
        unsigned char c = (unsigned char)*source;
        if (!isalnum(c)) {
            upper = true;
            continue;
        }
        base[at++] = (char)(upper ? toupper(c) : c);
        upper = false;
    }
    base[at] = 0;
    if (!at) {
        strcpy(base, "Value");
    }
    snprintf(name, size, "%s", base);
    for (int suffix = 2;; suffix++) {
        bool used = false;
        for (int i = 0; i < acount(g->locals); i++) {
            used |= !strcmp(g->locals[i].name, name);
        }
        if (!used) {
            return;
        }
        snprintf(name, size, "%s%d", base, suffix);
    }
}

static void alias_outputs(graph *g, node *source, const graph_optimize_options *options, graph_optimize_report *report)
{
    for (int p = 0; p < source->output_count; p++) {
        const pin_spec *out = &source->output_pins[p];
        if (out->kind != PIN_DATA || graph_output_local(g, source, p)) {
            continue;
        }
        int count = 0, reason = -1;
        if (source->disabled || (options->preserve_debug && debug_state(source))) {
            reason = GRAPH_OPTIMIZE_DEBUG;
        }
        if (out->type == PIN_FLOW || (out->flags & PIN_FLAG_SETTING)) {
            reason = GRAPH_OPTIMIZE_PIN;
        }
        for (int i = 0; i < acount(g->edges); i++) {
            const edge *e = &g->edges[i];
            if (e->from_id != source->id || e->from_pin_id != out->id) {
                continue;
            }
            count++;
            node *target = graph_node_by_id(g, e->to_id);
            const pin_spec *in = graph_pin_by_id(target, e->to_pin_id, NULL, NULL);
            if (!in || e->kind != GRAPH_EDGE_DATA || in->type == PIN_FLOW ||
                (in->flags & PIN_FLAG_SETTING)) {
                reason = GRAPH_OPTIMIZE_PIN;
            } else if (in->binding) {
                reason = GRAPH_OPTIMIZE_DORMANT_BINDING;
            } else if (e->conversion ? !conversion_matches_pins(out, in, e->conversion) : !pin_types_compatible(out, in)) {
                reason = GRAPH_OPTIMIZE_CONVERSION;
            }
            if (target && (target->disabled ||
                           (options->preserve_debug && debug_state(target)))) {
                reason = GRAPH_OPTIMIZE_DEBUG;
            }
            if (e->reroute_count || e->custom_handles) {
                reason = GRAPH_OPTIMIZE_WIRE_ROUTE;
            }
        }
        if (count < options->fanout_threshold) {
            continue;
        }
        if (reason >= 0) {
            skip(report, source, out->id, reason);
            continue;
        }
        char name[128];
        graph_local_name(g, source, p, name, sizeof name);
        uint64_t local_id = graph_create_local(g, source, p, name);
        assert(local_id);
        (void)local_id;
        report->created_aliases++;
        report->changed_edges += count;
    }
}

static int compare_ids(const void *a, const void *b)
{
    uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

int graph_optimize(graph *g, const graph_optimize_options *requested, graph_optimize_report *report)
{
    graph_optimize_options options = requested ? *requested
                                               : graph_optimize_defaults();
    if (!g || !report || g->flow_pause_count || options.fanout_threshold < 2 ||
        graph_validate_locals(g)) {
        return -1;
    }
    *report = (graph_optimize_report){ 0 };
    if (g->math_expression) {
        if (acount(g->nodes)) {
            skip(report, g->nodes[0], 0, GRAPH_OPTIMIZE_BACKEND);
        }
        return 0;
    }
    if (options.dry_run) {
        av_doc *doc = av_doc_new();
        graph *copy = graph_from_value(graph_to_value(doc, g));
        av_doc_free(doc);
        if (!copy) {
            return -1;
        }
        copy->owner = g->owner;
        copy->kind = g->kind;
        copy->entry_id = g->entry_id;
        copy->return_id = g->return_id;
        options.dry_run = false;
        int result = graph_optimize(copy, &options, report);
        graph_free(copy);
        free(copy);
        return result;
    }
    uint64_t *ids = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        apush(ids, g->nodes[i]->id);
    }
    if (acount(ids)) {
        asort(ids, compare_ids);
    }
    for (int i = 0; i < acount(ids); i++) {
        node *source = graph_node_by_id(g, ids[i]);
        if (!graph_optimize_component_getter(g, source) &&
            source->desc != &NODE_GetVariable && source->desc != &NODE_Self &&
            source->desc != &NODE_SetVariable) {
            continue;
        }
        fold *folds = NULL;
        int reason = collect_folds(g, source, options.preserve_debug, &folds);
        if (reason < 0) {
            apply_folds(g, source, folds, report);
        } else if (!inline_self(g, source, options.preserve_debug, report)) {
            skip(report, source, 0, reason);
        }
        afree(folds);
    }
    if (options.local_aliases) {
        for (int i = 0; i < acount(ids); i++) {
            node *source = graph_node_by_id(g, ids[i]);
            if (source) {
                alias_outputs(g, source, &options, report);
            }
        }
    }
    afree(ids);
    graph_optimize_cleanup(g, report);
    assert(!graph_validate_locals(g));
    int changes = report->removed_nodes + report->created_aliases;
    return changes ? changes : report->folded_inputs > 0;
}
