#include "graph/authoring/graph_edit.h"
#include "graph/authoring/local_edit.h"
#include "graph/nodes/core_nodes.h"
/* graph/compiler/compile.c -- the CMPL compile and validation pass. One explicit,
 * deterministic, read-only pass over a behavior_asset's default event graph and
 * variable list (CMPL-1). CMPL-17 (editor navigation) is deferred to the
 * editor surface. */
#include "graph/compiler/field_program.h"
#include "graph/compiler/structure_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/reactive.h"
#include "graph/model/registry.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/shader/shader_graph.h"
#include "support/arena.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint64_t compile_graph_id;

static const char *node_label(const node *n)
{
    return n && n->desc ? n->desc->name : "?";
}

static const char *pin_name(const pin_spec *p)
{
    return p && p->name ? p->name : "?";
}

static const type_desc *component_type_by_key(const char *key)
{
    const type_desc *type = type_catalog_find(key);
    if (type || !key) {
        return type;
    }
    const char *dot = strrchr(key, '.');
    return dot && dot[1] ? type_catalog_find(dot + 1) : NULL;
}

static void emit(
    compile_report *out,
    int severity,
    uint64_t node_id,
    uint64_t pin_id,
    int rule,
    const char *fmt,
    ...
)
{
    compile_diagnostic d = {
        .severity = severity,
        .graph_id = compile_graph_id,
        .node_id = node_id,
        .pin_id = pin_id,
        .rule = rule,
    };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d.message, sizeof d.message, fmt, ap);
    va_end(ap);
    apush(out->items, d);
    if (severity == CMPL_ERROR) {
        out->error_count++;
    } else if (severity == CMPL_WARNING) {
        out->warning_count++;
    } else {
        out->note_count++;
    }
}

static void effect_add(uint64_t **values, uint64_t id)
{
    for (int i = 0; i < acount(*values); i++) {
        if ((*values)[i] == id) {
            return;
        }
    }
    apush(*values, id);
}

static void build_effects(compile_report *out, const behavior_asset *asset)
{
    compile_effects *effects = &out->effects;
    effects->parallel_eligible = 1;
    effects->topology_revision = graph_topology_revision(asset->graph);
    for (int g = 0; g < acount(asset->graphs); g++) {
        const graph *graph = asset->graphs[g];
        for (int i = 0; i < acount(graph->nodes); i++) {
            const node *n = graph->nodes[i];
            const node_access *access = n->desc ? n->desc->access : NULL;
            for (int p = 0; p < n->input_count; p++) {
                node *source = NULL;
                int source_pin = -1;
                if (n->input_pins[p].binding &&
                    !graph_input_source(graph, n, p, &source, &source_pin) &&
                    (n->input_pins[p].binding->kind == PIN_BINDING_COMPONENT_FIELD ||
                     n->input_pins[p].binding->kind == PIN_BINDING_COMPONENT)) {
                    effects->flags |= NODE_EFFECT_WORLD_READ;
                    effects->parallel_eligible = 0;
                    if (!effects->blocking_node_id) {
                        effects->blocking_node_id = n->id;
                        effects->blocking_reason = "component input binding "
                                                   "reads world state";
                    }
                }
            }
            if (!access) {
                effects->all_writes = 1;
                effects->parallel_eligible = 0;
                if (!effects->blocking_node_id) {
                    effects->blocking_node_id = n->id;
                    effects->blocking_reason = "undeclared access writes every "
                                               "component";
                }
                continue;
            }
            for (int r = 0; r < access->read_count; r++) {
                effect_add(&effects->reads, access->reads[r]);
            }
            for (int r = 0; access->read_refs && access->read_refs[r]; r++) {
                effect_add(&effects->reads, *access->read_refs[r]);
            }
            for (int w = 0; w < access->write_count; w++) {
                effect_add(&effects->writes, access->writes[w]);
            }
            effects->flags |= access->flags;
            if ((access->flags & NODE_EFFECT_STRUCTURAL) ||
                (access->flags & NODE_EFFECT_WORLD_READ) ||
                (access->flags & NODE_EFFECT_HOST_SINK) ||
                (access->write_count && !(access->flags & NODE_EFFECT_ENTITY_SCOPED))) {
                effects->parallel_eligible = 0;
                if (!effects->blocking_node_id) {
                    effects->blocking_node_id = n->id;
                    effects->blocking_reason = access->flags & NODE_EFFECT_STRUCTURAL
                                                 ? "structural effect"
                                             : access->flags & NODE_EFFECT_WORLD_READ
                                                 ? "world read effect"
                                             : access->flags & NODE_EFFECT_HOST_SINK
                                                 ? "host sink effect"
                                                 : "non-entity-scoped write";
                }
            }
        }
    }
}

/* CMPL-18: one warning per disabled node; the node is then excluded from
 * every other rule below. */
static void pass_disabled(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (!n->disabled) {
            continue;
        }
        emit(out, CMPL_WARNING, n->id, 0, CMPL_RULE_DISABLED, "node is disabled");
    }
}

/* CMPL-4: a required exec input with no incoming edge on a node that can
 * only run from an entry path (executable, non-entry under NODE-13). Entry
 * nodes have no exec input and are exempt. This is a warning because the
 * disconnected node is unreachable, not invalid to apply. */
static void pass_unconnected_exec(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled || node_behavior_for(n) != NODE_BEHAVIOR_EXEC) {
            continue;
        }
        for (int p = 0; p < n->input_count; p++) {
            if (n->input_pins[p].kind != PIN_EXEC) {
                continue;
            }
            const char *name = n->input_pins[p].name;
            if (name && (strcmp(name, "Reset") == 0 || strcmp(name, "Open") == 0 ||
                         strcmp(name, "Close") == 0 || strcmp(name, "Toggle") == 0 ||
                         strcmp(name, "Break") == 0)) {
                continue;
            }
            if (graph_exec_input_count(g, n, p) == 0) {
                emit(
                    out,
                    CMPL_WARNING,
                    n->id,
                    n->input_pins[p].id,
                    CMPL_RULE_UNCONNECTED_EXEC,
                    "exec input '%s' has no incoming edge",
                    pin_name(&n->input_pins[p])
                );
            }
        }
    }
}

/* CMPL-5: a data edge between incompatible pins, which can arise from a
 * retype after the edge was made (VAR-6). The edge itself is excluded when
 * either endpoint node is disabled (CMPL-18). */
static void pass_incompatible_edges(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_DATA) {
            continue;
        }
        const node *from = graph_node_by_id(g, e->from_id);
        const node *to = graph_node_by_id(g, e->to_id);
        if (!from || !to || from->disabled || to->disabled) {
            continue;
        }
        int fp = -1, tp = -1, is_out = 0;
        if (!graph_pin_by_id(from, e->from_pin_id, &is_out, &fp) || !is_out) {
            continue;
        }
        if (!graph_pin_by_id(to, e->to_pin_id, &is_out, &tp) || is_out) {
            continue;
        }
        /* An unresolved wildcard adopts the connected type (NODE-10), so the
         * edge is not "incompatible": CMPL-12 owns that case. */
        if ((from->output_pins[fp].flags & PIN_FLAG_WILDCARD) &&
            from->output_pins[fp].type == PIN_FLOW) {
            continue;
        }
        if ((to->input_pins[tp].flags & PIN_FLAG_WILDCARD) &&
            to->input_pins[tp].type == PIN_FLOW) {
            continue;
        }
        int conversion_ok = conversion_matches_pins(
            &from->output_pins[fp],
            &to->input_pins[tp],
            e->conversion
        );
        if (conversion_ok) {
            continue;
        }
        if (pin_compatible(&from->output_pins[fp], &to->input_pins[tp])) {
            continue;
        }
        emit(
            out,
            CMPL_ERROR,
            to->id,
            to->input_pins[tp].id,
            CMPL_RULE_INCOMPATIBLE_DATA_EDGE,
            "data edge from '%s'.%s to '%s'.%s is type-incompatible",
            node_label(from),
            pin_name(&from->output_pins[fp]),
            node_label(to),
            pin_name(&to->input_pins[tp])
        );
    }
}

/* CMPL-6: a cycle among data edges. DFS with per-node colors (0 unvisited,
 * 1 in-progress, 2 done); a back edge onto an in-progress node closes one
 * cycle and produces exactly one diagnostic at the edge's source. Edges to
 * or from disabled nodes are excluded (CMPL-18). */
static void cycle_dfs(const graph *g, uint64_t id, uint8_t *state, compile_report *out)
{
    const node *n = graph_node_by_id(g, id);
    if (!n) {
        return;
    }
    state[id] = 1;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_DATA || e->from_id != id) {
            continue;
        }
        const node *to = graph_node_by_id(g, e->to_id);
        if (!to || to->disabled) {
            continue;
        }
        if (state[e->to_id] == 0) {
            cycle_dfs(g, e->to_id, state, out);
        } else if (state[e->to_id] == 1) {
            emit(
                out,
                CMPL_ERROR,
                id,
                e->from_pin_id,
                CMPL_RULE_DATA_CYCLE,
                "data cycle: '%s' feeds '%s', which reaches back",
                node_label(n),
                node_label(to)
            );
        }
    }
    state[id] = 2;
}

static void pass_data_cycles(compile_report *out, const graph *g, arena *tmp)
{
    size_t n_ids = acount(g->id_table);
    uint8_t *state = (uint8_t *)arena_alloc(tmp, n_ids);
    memset(state, 0, n_ids);
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (!n->disabled && state[n->id] == 0) {
            cycle_dfs(g, n->id, state, out);
        }
    }
}

static const pin_spec *next_parameter(const pin_spec *pins, int count, int *index)
{
    while (*index < count) {
        const pin_spec *pin = &pins[(*index)++];
        if (pin->kind == PIN_DATA && !pin->field_source) {
            return pin;
        }
    }
    return NULL;
}

static bool parameters_match(const pin_spec *left, int left_count, const pin_spec *right, int right_count)
{
    int l = 0, r = 0;
    for (;;) {
        const pin_spec *a = next_parameter(left, left_count, &l);
        const pin_spec *b = next_parameter(right, right_count, &r);
        if (!a || !b) {
            return a == b;
        }
        if (!(a->flags & PIN_FLAG_WILDCARD) &&
            !(b->flags & PIN_FLAG_WILDCARD) && !pin_compatible(a, b)) {
            return false;
        }
    }
}

static bool has_execution(const pin_spec *pins, int count)
{
    for (int i = 0; i < count; i++) {
        if (pins[i].kind == PIN_EXEC) {
            return true;
        }
    }
    return false;
}

static int execution_count(const pin_spec *pins, int count)
{
    int result = 0;
    for (int i = 0; i < count; i++) {
        result += pins[i].kind == PIN_EXEC;
    }
    return result;
}

static bool call_signature_matches(const node *call, const graph *target)
{
    const node *entry = graph_node_by_id(target, target->entry_id);
    const node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return false;
    }
    if (call->desc == &NODE_CallMacro &&
        (execution_count(entry->output_pins, entry->output_count) !=
             execution_count(call->input_pins, call->input_count) ||
         execution_count(ret->input_pins, ret->input_count) !=
             execution_count(call->output_pins, call->output_count))) {
        return false;
    }
    if (call->desc == &NODE_BehaviorExpression &&
        target->expression_mode == GRAPH_EXPRESSION_BRANCH) {
        return !has_execution(entry->output_pins, entry->output_count) &&
               !has_execution(ret->input_pins, ret->input_count) &&
               has_execution(call->input_pins, call->input_count) &&
               has_execution(call->output_pins, call->output_count) &&
               parameters_match(entry->output_pins, entry->output_count, call->input_pins, call->input_count) &&
               parameters_match(ret->input_pins, ret->input_count, call->output_pins, call->output_count);
    }
    bool execution = call->desc == &NODE_CallFunction ||
                     call->desc == &NODE_CallMacro ||
                     (call->desc == &NODE_BehaviorExpression &&
                      target->expression_mode == GRAPH_EXPRESSION_ACTION);
    return has_execution(entry->output_pins, entry->output_count) == execution &&
           has_execution(ret->input_pins, ret->input_count) == execution &&
           has_execution(call->input_pins, call->input_count) == execution &&
           has_execution(call->output_pins, call->output_count) == execution &&
           parameters_match(entry->output_pins, entry->output_count, call->input_pins, call->input_count) &&
           parameters_match(ret->input_pins, ret->input_count, call->output_pins, call->output_count);
}

/* CMPL-7: a reference to a retired or missing variable, function, or macro.
 * Get/Set reference variables; Function and macro references use graph IDs. */
static void pass_references(compile_report *out, const behavior_asset *asset)
{
    for (int graph_index = 0; graph_index < acount(asset->graphs); graph_index++) {
        const graph *g = asset->graphs[graph_index];
        compile_graph_id = g->graph_id;
        for (int i = 0; i < acount(g->nodes); i++) {
            const node *n = g->nodes[i];
            if (n->disabled) {
                continue;
            }
            if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
                if (!behavior_asset_find_variable_by_id(asset, n->reference_id)) {
                    emit(
                        out,
                        CMPL_ERROR,
                        n->id,
                        0,
                        CMPL_RULE_MISSING_REFERENCE,
                        "references a missing variable (id %llu)",
                        (unsigned long long)n->reference_id
                    );
                }
            } else if (
                n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
                n->desc == &NODE_BehaviorExpression || n->desc == &NODE_CallMacro
            ) {
                const graph *target = n->reference_asset
                                        ? NULL
                                        : behavior_asset_find_graph(asset, n->reference_id);
                int valid = target && ((n->desc == &NODE_CallMacro &&
                                        target->kind == GRAPH_MACRO) ||
                                       (n->desc != &NODE_CallMacro &&
                                        target->kind == GRAPH_FUNCTION));
                if (!valid) {
                    emit(
                        out,
                        CMPL_ERROR,
                        n->id,
                        0,
                        CMPL_RULE_MISSING_REFERENCE,
                        "references a missing graph (id %llu)",
                        (unsigned long long)n->reference_id
                    );
                } else if (!call_signature_matches(n, target)) {
                    emit(
                        out,
                        CMPL_ERROR,
                        n->id,
                        0,
                        CMPL_RULE_CALL_SIGNATURE,
                        "call parameters do not match the signature of '%s'",
                        target->name
                    );
                }
            }
            for (int p = 0; p < n->input_count; p++) {
                const pin_spec *pin = &n->input_pins[p];
                const pin_binding *binding = pin->binding;
                if (!binding) {
                    continue;
                }
                node *source = NULL;
                int source_pin = -1;
                if (graph_input_source(g, n, p, &source, &source_pin)) {
                    continue;
                }
                if (asset->domain == BEHAVIOR_DOMAIN_LIBRARY ||
                    asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
                    emit(out, CMPL_ERROR, n->id, pin->id, CMPL_RULE_INPUT_BINDING, "input bindings are unavailable in this asset domain");
                    continue;
                }
                if (binding->kind == PIN_BINDING_VARIABLE) {
                    behavior_variable *variable = behavior_asset_find_variable_by_id(
                        asset,
                        binding->variable_id
                    );
                    if (!variable ||
                        (binding->source_asset &&
                         (!asset->name ||
                          strcmp(binding->source_asset, asset->name) != 0))) {
                        emit(
                            out,
                            CMPL_ERROR,
                            n->id,
                            pin->id,
                            CMPL_RULE_INPUT_BINDING,
                            "input binding references a missing variable (id "
                            "%llu)",
                            (unsigned long long)binding->variable_id
                        );
                    } else {
                        pin_spec source = { .type = variable->type,
                                            .type_name = variable->type_name,
                                            .container = variable->container,
                                            .kind = PIN_DATA };
                        if (!pin_compatible(&source, pin) &&
                            !conversion_for_pins(&source, pin)) {
                            emit(
                                out,
                                CMPL_ERROR,
                                n->id,
                                pin->id,
                                CMPL_RULE_INPUT_BINDING,
                                "input binding variable '%s' is incompatible "
                                "with '%s'",
                                variable->name,
                                pin_name(pin)
                            );
                        }
                    }
                } else if (binding->kind == PIN_BINDING_COMPONENT_FIELD) {
                    const type_desc *type = component_type_by_key(binding->component_key);
                    const type_field
                        *field = type ? pin_type_field_path(type, binding->field_name)
                                      : NULL;
                    if (!field) {
                        emit(out, CMPL_ERROR, n->id, pin->id, CMPL_RULE_INPUT_BINDING, "input binding references a missing component field");
                    } else {
                        pin_spec source = { .type = field->type,
                                            .type_name = field->type_name,
                                            .container = PIN_CONTAINER_SINGLE,
                                            .kind = PIN_DATA };
                        if (!pin_compatible(&source, pin) &&
                            !conversion_for_pins(&source, pin)) {
                            emit(
                                out,
                                CMPL_ERROR,
                                n->id,
                                pin->id,
                                CMPL_RULE_INPUT_BINDING,
                                "component field '%s.%s' is incompatible with "
                                "'%s'",
                                binding->component_key,
                                binding->field_name,
                                pin_name(pin)
                            );
                        }
                    }
                } else if (binding->kind == PIN_BINDING_COMPONENT) {
                    const type_desc *type = component_type_by_key(binding->component_key);
                    if (!type) {
                        emit(out, CMPL_ERROR, n->id, pin->id, CMPL_RULE_INPUT_BINDING, "input binding references a missing component");
                    } else {
                        pin_spec source = { .type = PIN_STRUCT,
                                            .type_name = type->name,
                                            .container = PIN_CONTAINER_SINGLE,
                                            .kind = PIN_DATA };
                        if (!pin_compatible(&source, pin) &&
                            !conversion_for_pins(&source, pin)) {
                            emit(
                                out,
                                CMPL_ERROR,
                                n->id,
                                pin->id,
                                CMPL_RULE_INPUT_BINDING,
                                "component '%s' is incompatible with '%s'",
                                binding->component_key,
                                pin_name(pin)
                            );
                        }
                    }
                } else if (binding->kind == PIN_BINDING_EVENT_OUTPUT) {
                    node *event = graph_node_by_id(g, binding->event_node_id);
                    int output = 0;
                    const pin_spec *source = event
                                               ? graph_pin_by_id(event, binding->event_pin_id, &output, NULL)
                                               : NULL;
                    if (!event || event->desc->flow_kind != NODE_FLOW_EVENT ||
                        !output || !source || source->kind != PIN_DATA) {
                        emit(out, CMPL_ERROR, n->id, pin->id, CMPL_RULE_INPUT_BINDING, "event output binding references a missing event output");
                    } else if (!pin_compatible(source, pin) && !conversion_for_pins(source, pin)) {
                        emit(
                            out,
                            CMPL_ERROR,
                            n->id,
                            pin->id,
                            CMPL_RULE_INPUT_BINDING,
                            "event output '%s' is incompatible with '%s'",
                            pin_name(source),
                            pin_name(pin)
                        );
                    }
                } else {
                    emit(out, CMPL_ERROR, n->id, pin->id, CMPL_RULE_INPUT_BINDING, "input binding has an unknown source kind");
                }
            }
        }
    }
    compile_graph_id = asset->graph->graph_id;
}

/* CMPL-8: a duplicate event name, a duplicate name within one parameter
 * list, or a duplicate variable name in one scope. Each duplicate pair
 * reports once, on the later element. */
static void pass_duplicates(compile_report *out, const behavior_asset *asset)
{
    const graph *g = asset->graph;
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled || n->desc->flow_kind != NODE_FLOW_EVENT) {
            continue;
        }
        const char *name = n->reference_name;
        if (!name || !name[0]) {
            continue;
        }
        for (int j = 0; j < i; j++) {
            const node *other = g->nodes[j];
            if (other->disabled || other->desc->flow_kind != NODE_FLOW_EVENT ||
                !other->reference_name || strcmp(other->reference_name, name) != 0) {
                continue;
            }
            emit(out, CMPL_ERROR, n->id, 0, CMPL_RULE_DUPLICATE_NAME, "duplicate event name '%s'", name);
            break;
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled || n->desc->flow_kind != NODE_FLOW_EVENT) {
            continue;
        }
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *pin = &n->output_pins[p];
            if (pin->kind != PIN_DATA || !pin->name || !pin->name[0]) {
                continue;
            }
            for (int q = 0; q < p; q++) {
                const pin_spec *other = &n->output_pins[q];
                if (other->kind != PIN_DATA || !other->name ||
                    strcmp(other->name, pin->name) != 0) {
                    continue;
                }
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    pin->id,
                    CMPL_RULE_DUPLICATE_NAME,
                    "duplicate parameter name '%s'",
                    pin->name
                );
                break;
            }
        }
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        const char *name = asset->variables[i].name;
        if (!name) {
            continue;
        }
        for (int j = 0; j < i; j++) {
            if (asset->variables[j].name &&
                strcmp(asset->variables[j].name, name) == 0) {
                emit(out, CMPL_ERROR, 0, 0, CMPL_RULE_DUPLICATE_NAME, "duplicate variable name '%s'", name);
                break;
            }
        }
    }
}

static void pass_shader_library_functions(compile_report *out, const behavior_asset *asset)
{
    if (asset->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        if (g->kind != GRAPH_FUNCTION) {
            continue;
        }
        compile_graph_id = g->graph_id;
        shader_error error = { 0 };
        if (!shader_graph_validate_function(g, &error)) {
            emit(out, CMPL_ERROR, error.node, 0, CMPL_RULE_OPERATOR, "%s", error.message);
        }
    }
}

/* CMPL-12: an unresolved wildcard pin that carries an edge. Unresolved means
 * the pin still carries its wildcard marker with the flow/unknown type; a
 * resolved wildcard has adopted a concrete type (NODE-10). Edges whose other
 * endpoint is a disabled node are excluded (CMPL-18). */
static int edge_touches_pin(const graph *g, uint64_t pin_id)
{
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_pin_id != pin_id && e->to_pin_id != pin_id) {
            continue;
        }
        const node *other = graph_node_by_id(g, e->from_pin_id == pin_id ? e->to_id : e->from_id);
        if (other && other->disabled) {
            continue;
        }
        return 1;
    }
    return 0;
}

static void pass_wildcards(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled) {
            continue;
        }
        for (int p = 0; p < n->input_count; p++) {
            const pin_spec *pin = &n->input_pins[p];
            if ((pin->flags & PIN_FLAG_WILDCARD) && pin->type == PIN_FLOW &&
                edge_touches_pin(g, pin->id)) {
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    pin->id,
                    CMPL_RULE_UNRESOLVED_WILDCARD,
                    "unresolved wildcard pin '%s' carries an edge",
                    pin_name(pin)
                );
            }
        }
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *pin = &n->output_pins[p];
            if ((pin->flags & PIN_FLAG_WILDCARD) && pin->type == PIN_FLOW &&
                edge_touches_pin(g, pin->id)) {
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    pin->id,
                    CMPL_RULE_UNRESOLVED_WILDCARD,
                    "unresolved wildcard pin '%s' carries an edge",
                    pin_name(pin)
                );
            }
        }
    }
}

/* CMPL-20: an authored nominal type name (PIN_STRUCT, PIN_ENUM, PIN_HANDLE)
 * that is unavailable in the active registry. The message
 * preserves the unresolved name. The owner handle type is covered through
 * the Self node's output pin; a declared owner type with no Self node is
 * not exercised and is deferred with the editor's owner-type surface. */
static int nominal_in_registry(const pin_spec *pin)
{
    const char *type_name = pin->type_name;
    if (!type_name || !type_name[0]) {
        return 1;
    }
    return type_catalog_find(type_name) != NULL ||
           (pin->type == PIN_ENUM && type_catalog_enum_find(type_name) != NULL);
}

static int nominal_pin(const pin_spec *p)
{
    return p->type == PIN_STRUCT || p->type == PIN_ENUM || p->type == PIN_HANDLE;
}

static void pass_nominal_types(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled) {
            continue;
        }
        for (int p = 0; p < n->input_count; p++) {
            const pin_spec *pin = &n->input_pins[p];
            if (nominal_pin(pin) && !nominal_in_registry(pin)) {
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    pin->id,
                    CMPL_RULE_UNKNOWN_NOMINAL_TYPE,
                    "nominal type '%s' is not in the active registry",
                    pin->type_name
                );
            }
        }
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *pin = &n->output_pins[p];
            if (nominal_pin(pin) && !nominal_in_registry(pin)) {
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    pin->id,
                    CMPL_RULE_UNKNOWN_NOMINAL_TYPE,
                    "nominal type '%s' is not in the active registry",
                    pin->type_name
                );
            }
        }
    }
}

/* CMPL-9/10: function graphs have one entry, one or more return nodes, and
 * no latent nodes. */
static void pass_macro_recursion(compile_report *out, const graph *g)
{
    if (g->kind != GRAPH_MACRO) {
        return;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled || n->desc != &NODE_CallMacro || n->reference_id != g->graph_id) {
            continue;
        }
        emit(
            out,
            CMPL_ERROR,
            n->id,
            0,
            CMPL_RULE_MACRO_RECURSION,
            "macro '%s' recursively calls itself",
            g->name ? g->name : "?"
        );
    }
}

static void pass_function_shape(compile_report *out, const graph *g)
{
    if (g->kind != GRAPH_FUNCTION) {
        return;
    }
    const node *entry = graph_node_by_id(g, g->entry_id);
    bool pure = true;
    for (int i = 0; entry && i < entry->output_count; i++) {
        pure &= entry->output_pins[i].kind != PIN_EXEC;
    }
    int entries = 0;
    int returns = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled) {
            continue;
        }
        if (n->desc == &NODE_FunctionEntry) {
            entries++;
        }
        if (n->desc == &NODE_Return) {
            returns++;
        }
        if (pure && n->desc->latent) {
            emit(
                out,
                CMPL_ERROR,
                n->id,
                0,
                CMPL_RULE_FUNCTION_LATENT,
                "latent node '%s' is not allowed in a pure function graph",
                node_label(n)
            );
        }
    }
    if (entries != 1 || returns < 1) {
        emit(out, CMPL_ERROR, 0, 0, CMPL_RULE_FUNCTION_SHAPE, "function graph requires exactly one entry and at least one return (got %d/%d)", entries, returns);
    }
}

/* CMPL-16: order by graph id, then node position (canvas y, then x, then
 * node id), then rule code, then pin id. Diagnostics without a node sort as
 * if at (y=0, x=0, node id 0). Exact ties keep the rule-pass order because
 * the sort below is stable. */
static int node_pos_less(const graph *g, uint64_t id_a, uint64_t id_b)
{
    const node *a = graph_node_by_id(g, id_a);
    const node *b = graph_node_by_id(g, id_b);
    float ay = a ? a->y : 0.0f, ax = a ? a->x : 0.0f;
    float by = b ? b->y : 0.0f, bx = b ? b->x : 0.0f;
    if (ay != by) {
        return ay < by;
    }
    if (ax != bx) {
        return ax < bx;
    }
    return id_a < id_b;
}

static int diag_less(const graph *g, const compile_diagnostic *a, const compile_diagnostic *b)
{
    if (a->graph_id != b->graph_id) {
        return a->graph_id < b->graph_id;
    }
    if (a->node_id != b->node_id) {
        return node_pos_less(g, a->node_id, b->node_id);
    }
    if (a->rule != b->rule) {
        return a->rule < b->rule;
    }
    if (a->pin_id != b->pin_id) {
        return a->pin_id < b->pin_id;
    }
    return 0;
}

void graph_structure_sort(compile_report *out, const graph *g)
{
    int n = acount(out->items);
    for (int i = 1; i < n; i++) {
        compile_diagnostic key = out->items[i];
        int j = i - 1;
        while (j >= 0 && diag_less(g, &key, &out->items[j])) {
            out->items[j + 1] = out->items[j];
            j--;
        }
        out->items[j + 1] = key;
    }
}

static void pass_field_projections(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled) {
            continue;
        }
        for (int output = 0; output <= 1; output++) {
            const pin_spec *pins = output ? n->output_pins : n->input_pins;
            int count = output ? n->output_count : n->input_count;
            for (int p = 0; p < count; p++) {
                if (!pins[p].field_source && !pins[p].field_name) {
                    continue;
                }
                if (output && graph_output_field(n, p, NULL)) {
                    continue;
                }
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    pins[p].id,
                    CMPL_RULE_FIELD_PROJECTION,
                    "field output '%s' has an invalid aggregate source, field, "
                    "or type",
                    pin_name(&pins[p])
                );
            }
        }
    }
}

int graph_structure_compile(const behavior_asset *asset, compile_report *out)
{
    assert(asset && asset->graph && out);
    compile_report_free(out);
    compile_report_init(out);
    const graph *g = asset->graph;
    build_effects(out, asset);
    arena tmp;
    arena_init(&tmp, 8192);
    for (int i = 0; i < acount(asset->graphs); i++) {
        g = asset->graphs[i];
        compile_graph_id = g->graph_id;
        const char *local_error = graph_validate_locals(g);
        if (local_error) {
            emit(out, CMPL_ERROR, 0, 0, CMPL_RULE_INPUT_BINDING, "%s", local_error);
        }
        if (asset->domain != BEHAVIOR_DOMAIN_LIBRARY) {
            for (int j = 0; j < acount(g->nodes); j++) {
                const node *n = g->nodes[j];
                if (n->desc->category &&
                    !strcmp(n->desc->category, "Procedural Field")) {
                    emit(out, CMPL_ERROR, n->id, 0, CMPL_RULE_BACKEND, "Procedural field nodes require a field graph");
                }
            }
        }
        pass_disabled(out, g);
        pass_unconnected_exec(out, g);
        pass_incompatible_edges(out, g);
        pass_data_cycles(out, g, &tmp);
        pass_function_shape(out, g);
        pass_macro_recursion(out, g);
        pass_wildcards(out, g);
        pass_nominal_types(out, g);
        pass_field_projections(out, g);
    }
    compile_graph_id = asset->graph->graph_id;
    pass_references(out, asset);
    pass_duplicates(out, asset);
    pass_shader_library_functions(out, asset);
    if (!out->error_count && acount(asset->reactives)) {
        char error[CMPL_MESSAGE_MAX] = "";
        if (!behavior_reactive_validate(asset, error, sizeof error)) {
            emit(out, CMPL_ERROR, 0, 0, CMPL_RULE_OPERATOR, "%s", error);
        }
    }
    graph_structure_sort(out, asset->graph);
    arena_free(&tmp);
    return out->error_count;
}
