/* core/compile.c -- the CMPL compile and validation pass. One explicit,
 * deterministic, read-only pass over a blueprint's default event graph and
 * variable list (CMPL-1). CMPL-17 (editor navigation) and CMPL-19 (operator
 * ambiguity) are deferred: navigation is editor surface, and operator
 * signatures arrive with the reflection v2 widening ladder. */
#include "compile.h"

#include "arena.h"
#include "pin_conv.h"
#include "registry.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *node_label(const node *n)
{ return n && n->desc ? n->desc->name : "?"; }

static const char *pin_name(const pin_spec *p)
{ return p && p->name ? p->name : "?"; }

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
        .graph_id = CMPL_GRAPH_DEFAULT,
        .node_id = node_id,
        .pin_id = pin_id,
        .rule = rule,
    };
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d.message, sizeof d.message, fmt, ap);
    va_end(ap);
    apush(out->items, d);
    if (severity == CMPL_ERROR) out->error_count++;
    else out->warning_count++;
}

void compile_report_init(compile_report *out)
{
    out->items = NULL;
    out->error_count = 0;
    out->warning_count = 0;
}

void compile_report_free(compile_report *out)
{
    if (!out) return;
    afree(out->items);
    out->error_count = 0;
    out->warning_count = 0;
}

int compile_report_blocked(const compile_report *out)
{ return out && out->error_count > 0; }

const compile_diagnostic *compile_report_first_error(const compile_report *out)
{
    if (!out) return NULL;
    for (int i = 0; i < acount(out->items); i++)
        if (out->items[i].severity == CMPL_ERROR) return &out->items[i];
    return NULL;
}

const char *cmpl_rule_name(int rule)
{
    switch (rule) {
        case CMPL_RULE_UNCONNECTED_EXEC: return "CMPL-4";
        case CMPL_RULE_INCOMPATIBLE_DATA_EDGE: return "CMPL-5";
        case CMPL_RULE_DATA_CYCLE: return "CMPL-6";
        case CMPL_RULE_MISSING_REFERENCE: return "CMPL-7";
        case CMPL_RULE_DUPLICATE_NAME: return "CMPL-8";
        case CMPL_RULE_UNRESOLVED_WILDCARD: return "CMPL-12";
        case CMPL_RULE_DISABLED: return "CMPL-18";
        case CMPL_RULE_UNKNOWN_NOMINAL_TYPE: return "CMPL-20";
        default: return "CMPL-?";
    }
}

/* CMPL-18: one warning per disabled node; the node is then excluded from
 * every other rule below. */
static void pass_disabled(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (!n->disabled) continue;
        emit(out, CMPL_WARNING, n->id, 0, CMPL_RULE_DISABLED, "node is disabled");
    }
}

/* CMPL-4: a required exec input with no incoming edge on a node that can
 * only run from an entry path (executable, non-entry under NODE-13). Entry
 * nodes have no exec input and are exempt. */
static void pass_unconnected_exec(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled || node_behavior_for(n) != NODE_BEHAVIOR_EXEC) continue;
        for (int p = 0; p < n->input_count; p++) {
            if (n->input_pins[p].kind != PIN_EXEC) continue;
            if (graph_exec_input_count(g, n, p) == 0)
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    n->input_pins[p].id,
                    CMPL_RULE_UNCONNECTED_EXEC,
                    "exec input '%s' has no incoming edge",
                    pin_name(&n->input_pins[p])
                );
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
        if (e->kind != GRAPH_EDGE_DATA) continue;
        const node *from = graph_node_by_id(g, e->from_id);
        const node *to = graph_node_by_id(g, e->to_id);
        if (!from || !to || from->disabled || to->disabled) continue;
        int fp = -1, tp = -1, is_out = 0;
        if (!graph_pin_by_id(from, e->from_pin_id, &is_out, &fp) || !is_out)
            continue;
        if (!graph_pin_by_id(to, e->to_pin_id, &is_out, &tp) || is_out)
            continue;
        /* An unresolved wildcard adopts the connected type (NODE-10), so the
         * edge is not "incompatible": CMPL-12 owns that case. */
        if ((from->output_pins[fp].flags & PIN_FLAG_WILDCARD) &&
            from->output_pins[fp].type == PIN_FLOW)
            continue;
        if ((to->input_pins[tp].flags & PIN_FLAG_WILDCARD) &&
            to->input_pins[tp].type == PIN_FLOW)
            continue;
        if (pin_compatible(&from->output_pins[fp], &to->input_pins[tp]))
            continue;
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
    if (!n) return;
    state[id] = 1;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_DATA || e->from_id != id) continue;
        const node *to = graph_node_by_id(g, e->to_id);
        if (!to || to->disabled) continue;
        if (state[e->to_id] == 0) cycle_dfs(g, e->to_id, state, out);
        else if (state[e->to_id] == 1)
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
    state[id] = 2;
}

static void pass_data_cycles(compile_report *out, const graph *g, arena *tmp)
{
    size_t n_ids = acount(g->id_table);
    uint8_t *state = (uint8_t *)arena_alloc(tmp, n_ids);
    memset(state, 0, n_ids);
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (!n->disabled && state[n->id] == 0) cycle_dfs(g, n->id, state, out);
    }
}

/* CMPL-7: a reference to a retired or missing variable, event, function, or
 * macro. Get/Set reference variables; Call Event references an event node.
 * Function and macro references arrive with Phase 4. */
static void pass_references(compile_report *out, const blueprint *bp)
{
    const graph *g = bp->graph;
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled) continue;
        if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
            if (!blueprint_find_variable_by_id(bp, n->reference_id))
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    0,
                    CMPL_RULE_MISSING_REFERENCE,
                    "references a missing variable (id %llu)",
                    (unsigned long long)n->reference_id
                );
        } else if (n->desc == &NODE_CallEvent) {
            const node *target = graph_node_by_id(g, n->reference_id);
            if (!target || target->desc->flow_kind != NODE_FLOW_EVENT)
                emit(
                    out,
                    CMPL_ERROR,
                    n->id,
                    0,
                    CMPL_RULE_MISSING_REFERENCE,
                    "references a missing event (id %llu)",
                    (unsigned long long)n->reference_id
                );
        }
    }
}

/* CMPL-8: a duplicate event name, a duplicate name within one parameter
 * list, or a duplicate variable name in one scope. Each duplicate pair
 * reports once, on the later element. */
static void pass_duplicates(compile_report *out, const blueprint *bp)
{
    const graph *g = bp->graph;
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled || n->desc->flow_kind != NODE_FLOW_EVENT) continue;
        const char *name = n->reference_name;
        if (!name || !name[0]) continue;
        for (int j = 0; j < i; j++) {
            const node *other = g->nodes[j];
            if (other->disabled || other->desc->flow_kind != NODE_FLOW_EVENT ||
                !other->reference_name || strcmp(other->reference_name, name) != 0)
                continue;
            emit(out, CMPL_ERROR, n->id, 0, CMPL_RULE_DUPLICATE_NAME, "duplicate event name '%s'", name);
            break;
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled || n->desc->flow_kind != NODE_FLOW_EVENT) continue;
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *pin = &n->output_pins[p];
            if (pin->kind != PIN_DATA || !pin->name || !pin->name[0]) continue;
            for (int q = 0; q < p; q++) {
                const pin_spec *other = &n->output_pins[q];
                if (other->kind != PIN_DATA || !other->name ||
                    strcmp(other->name, pin->name) != 0)
                    continue;
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
    for (int i = 0; i < acount(bp->variables); i++) {
        const char *name = bp->variables[i].name;
        if (!name) continue;
        for (int j = 0; j < i; j++)
            if (bp->variables[j].name && strcmp(bp->variables[j].name, name) == 0) {
                emit(out, CMPL_ERROR, 0, 0, CMPL_RULE_DUPLICATE_NAME, "duplicate variable name '%s'", name);
                break;
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
        if (e->from_pin_id != pin_id && e->to_pin_id != pin_id) continue;
        const node *other = graph_node_by_id(g, e->from_pin_id == pin_id ? e->to_id : e->from_id);
        if (other && other->disabled) continue;
        return 1;
    }
    return 0;
}

static void pass_wildcards(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled) continue;
        for (int p = 0; p < n->input_count; p++) {
            const pin_spec *pin = &n->input_pins[p];
            if ((pin->flags & PIN_FLAG_WILDCARD) && pin->type == PIN_FLOW &&
                edge_touches_pin(g, pin->id))
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
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *pin = &n->output_pins[p];
            if ((pin->flags & PIN_FLAG_WILDCARD) && pin->type == PIN_FLOW &&
                edge_touches_pin(g, pin->id))
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

/* CMPL-20: an authored nominal type name (PIN_STRUCT, PIN_ENUM, PIN_HANDLE)
 * that is unavailable in the active registry (REGISTRY_TYPES). The message
 * preserves the unresolved name. The owner handle type is covered through
 * the Self node's output pin; a declared owner type with no Self node is
 * not exercised and is deferred with the editor's owner-type surface. */
static int nominal_in_registry(const char *type_name)
{
    if (!type_name || !type_name[0]) return 1;
    for (int i = 0; i < REGISTRY_TYPE_COUNT; i++)
        if (strcmp(REGISTRY_TYPES[i]->name, type_name) == 0) return 1;
    return 0;
}

static int nominal_pin(const pin_spec *p)
{
    return p->type == PIN_STRUCT || p->type == PIN_ENUM || p->type == PIN_HANDLE;
}

static void pass_nominal_types(compile_report *out, const graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->disabled) continue;
        for (int p = 0; p < n->input_count; p++) {
            const pin_spec *pin = &n->input_pins[p];
            if (nominal_pin(pin) && !nominal_in_registry(pin->type_name))
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
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *pin = &n->output_pins[p];
            if (nominal_pin(pin) && !nominal_in_registry(pin->type_name))
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
    if (ay != by) return ay < by;
    if (ax != bx) return ax < bx;
    return id_a < id_b;
}

static int diag_less(const graph *g, const compile_diagnostic *a, const compile_diagnostic *b)
{
    if (a->graph_id != b->graph_id) return a->graph_id < b->graph_id;
    if (a->node_id != b->node_id)
        return node_pos_less(g, a->node_id, b->node_id);
    if (a->rule != b->rule) return a->rule < b->rule;
    if (a->pin_id != b->pin_id) return a->pin_id < b->pin_id;
    return 0;
}

static void sort_diagnostics(compile_report *out, const graph *g)
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

int blueprint_compile(const blueprint *bp, compile_report *out)
{
    assert(bp && bp->graph && out);
    compile_report_free(out);
    compile_report_init(out);
    const graph *g = bp->graph;
    arena tmp;
    arena_init(&tmp, 8192);
    pass_disabled(out, g);
    pass_unconnected_exec(out, g);
    pass_incompatible_edges(out, g);
    pass_data_cycles(out, g, &tmp);
    pass_references(out, bp);
    pass_duplicates(out, bp);
    pass_wildcards(out, g);
    pass_nominal_types(out, g);
    sort_diagnostics(out, g);
    arena_free(&tmp);
    return out->error_count;
}
