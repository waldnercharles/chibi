#include "graph/document/library.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/events.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/state_nodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *name;
    uint64_t offset;
    bool active, transient_function_ids;
} library_import;

typedef struct
{
    behavior_asset *result;
    const behavior_asset *const *catalog;
    int count;
    library_import *imports;
    char *error;
    size_t error_size;
} library_link;

static bool call_node(const node *n)
{
    return n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
           n->desc == &NODE_BehaviorExpression;
}

bool behavior_library_node_allowed(behavior_domain domain, const node_desc *desc)
{
    if (domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return desc == &NODE_FunctionEntry || desc == &NODE_Return ||
               desc == &NODE_CallPureFunction || desc == &NODE_BehaviorExpression ||
               (desc->category && !strncmp(desc->category, "Shader", 6) &&
                strcmp(desc->name, "Shader Surface Output") &&
                strcmp(desc->name, "Shader Shape Output") &&
                strcmp(desc->name, "Shader Parameter"));
    }
    if ((desc->access && (desc->access->flags & NODE_EFFECT_ENTITY_SCOPED)) ||
        desc == &NODE_Event || desc == &NODE_GetVariable ||
        desc == &NODE_SetVariable || desc == &NODE_Self || desc == &NODE_CallMacro) {
        return false;
    }
    for (int i = 0; i < STATE_NODE_COUNT; i++) {
        if (desc == STATE_NODES[i]) {
            return false;
        }
    }
    return true;
}

const char *behavior_asset_validate_library(const behavior_asset *asset)
{
    if (asset->domain != BEHAVIOR_DOMAIN_LIBRARY &&
        asset->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return "The target is not a function library.";
    }
    if (acount(asset->variables) || asset->owner_type_name) {
        return "Library functions use explicit parameters instead of instance "
               "state.";
    }
    if (acount(asset->graph->nodes)) {
        return "Library functions belong in function graphs.";
    }
    for (int g = 1; g < acount(asset->graphs); g++) {
        const graph *graph = asset->graphs[g];
        if (graph->kind != GRAPH_FUNCTION) {
            return "Libraries contain functions only.";
        }
        for (int i = 0; i < acount(graph->nodes); i++) {
            const node *n = graph->nodes[i];
            /* A component node is available when the node it lowers to is.
             * Get, Set and Has also need an entity, since the field setters a
             * Set lowers to would accept Self. */
            const node_desc *target = component_node_target(graph, n);
            if (!behavior_library_node_allowed(asset->domain, target ? target : n->desc) ||
                component_node_self(graph, n)) {
                return "This operation is unavailable in a function library.";
            }
            if (asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
                for (int p = 0; p < n->input_count; p++) {
                    if (n->input_pins[p].kind == PIN_EXEC) {
                        return "Shader library functions must be pure.";
                    }
                }
                for (int p = 0; p < n->output_count; p++) {
                    if (n->output_pins[p].kind == PIN_EXEC) {
                        return "Shader library functions must be pure.";
                    }
                }
            }
        }
    }
    return NULL;
}

static const pin_spec *signature_match(const pin_spec *pins, int count, const pin_spec *old)
{
    for (int i = 0; i < count; i++) {
        const pin_spec *p = &pins[i];
        if (p->kind != PIN_DATA || p->field_source) {
            continue;
        }
        if ((old->declaration_id && old->declaration_id == p->id) ||
            (!old->declaration_id && old->name && p->name &&
             strcmp(old->name, p->name) == 0)) {
            return p;
        }
    }
    return NULL;
}

static bool same_parameter_type(const pin_spec *a, const pin_spec *b)
{
    /* A number's type names it; only named types compare their names. */
    bool named = a->type == PIN_STRUCT || a->type == PIN_ENUM || a->type == PIN_HANDLE;
    return a->type == b->type && a->container == b->container &&
           (!named || (!a->type_name && !b->type_name) ||
            (a->type_name && b->type_name && strcmp(a->type_name, b->type_name) == 0));
}

static bool connected_pin(const graph *g, const node *call, const pin_spec *pin)
{
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if ((e->from_id == call->id && e->from_pin_id == pin->id) ||
            (e->to_id == call->id && e->to_pin_id == pin->id)) {
            return true;
        }
    }
    for (int i = 0; i < call->output_count; i++) {
        if (call->output_pins[i].field_source == pin->id &&
            connected_pin(g, call, &call->output_pins[i])) {
            return true;
        }
    }
    return false;
}

/* A copy's graph and node ids moved by the same offset (shift_graph). */
graph_node_source graph_node_authored(const graph *g, uint64_t node_id)
{
    if (!g->source_asset) {
        return (graph_node_source){ NULL, g->graph_id, node_id };
    }
    uint64_t offset = g->graph_id - g->source_graph_id;
    return (graph_node_source){ g->source_asset, g->source_graph_id, node_id - offset };
}

const char *graph_library_refresh_call(graph *caller, node *call, const graph *target)
{
    if (!caller || !call || !target || !call_node(call) ||
        target->kind != GRAPH_FUNCTION || caller->flow_pause_count) {
        return "Choose an editable function call.";
    }
    const node *entry = graph_node_by_id(target, target->entry_id);
    const node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return "The function has no valid signature.";
    }
    bool execution = false;
    for (int i = 0; i < entry->output_count; i++) {
        execution |= entry->output_pins[i].kind == PIN_EXEC;
    }
    bool expression_action = call->desc == &NODE_BehaviorExpression &&
                             target->expression_mode == GRAPH_EXPRESSION_ACTION;
    bool expression_branch = call->desc == &NODE_BehaviorExpression &&
                             target->expression_mode == GRAPH_EXPRESSION_BRANCH;
    if (execution != (call->desc == &NODE_CallFunction ||
                      call->desc == &NODE_CallMacro || expression_action) &&
        !expression_branch) {
        return "The function execution kind changed; replace this call.";
    }
    for (int side = 0; side < 2; side++) {
        const pin_spec *expected = side ? ret->input_pins : entry->output_pins;
        int count = side ? ret->input_count : entry->output_count;
        const pin_spec *pins = side ? call->output_pins : call->input_pins;
        int total = side ? call->output_count : call->input_count;
        for (int p = 0; p < total; p++) {
            if (pins[p].kind != PIN_DATA || pins[p].field_source) {
                continue;
            }
            const pin_spec *match = signature_match(expected, count, &pins[p]);
            if ((!match || !same_parameter_type(match, &pins[p])) &&
                connected_pin(caller, call, &pins[p])) {
                return "Disconnect removed or incompatible parameter wires "
                       "before refreshing the signature.";
            }
        }
    }
    for (int side = 0; side < 2; side++) {
        const pin_spec *expected = side ? ret->input_pins : entry->output_pins;
        int count = side ? ret->input_count : entry->output_count;
        int total = side ? call->output_count : call->input_count;
        for (int p = total - 1; p >= 0; p--) {
            const pin_spec *pins = side ? call->output_pins : call->input_pins;
            if (pins[p].kind == PIN_DATA && !pins[p].field_source &&
                !signature_match(expected, count, &pins[p])) {
                graph_remove_pin(caller, call, side, p);
            }
        }
        int position = execution ? 1 : expression_branch ? (side ? 2 : 1) : 0;
        for (int p = 0; p < count; p++) {
            const pin_spec *source = &expected[p];
            if (source->kind != PIN_DATA || source->field_source) {
                continue;
            }
            pin_spec *pins = side ? call->output_pins : call->input_pins;
            total = side ? call->output_count : call->input_count;
            int at = -1;
            for (int i = 0; i < total; i++) {
                if (pins[i].kind == PIN_DATA && !pins[i].field_source &&
                    signature_match(source, 1, &pins[i])) {
                    at = i;
                    break;
                }
            }
            if (at < 0) {
                pin_spec added = *source;
                added.id = 0;
                added.declaration_id = source->id;
                added.binding = NULL; /* Signature bindings read inside the function. */
                added.type_name = source->type_name ? graph_own_str(
                                                          caller,
                                                          source->type_name,
                                                          strlen(source->type_name)
                                                      )
                                                    : NULL;
                added.default_value = (pin_value){ .type = source->type,
                                                   .container = source->container };
                at = graph_add_pin(caller, call, side, &added);
                graph_set_pin_default(caller, call, side, at, &source->default_value);
            } else {
                if (!same_parameter_type(source, &pins[at])) {
                    graph_set_pin_type(
                        caller,
                        call,
                        side,
                        at,
                        source->type,
                        source->type_name
                    );
                    graph_set_pin_container(caller, call, side, at, source->container);
                    graph_set_pin_default(caller, call, side, at, &source->default_value);
                }
                pins = side ? call->output_pins : call->input_pins;
                pins[at]
                    .name = graph_own_str(caller, source->name, strlen(source->name));
                pins[at].declaration_id = source->id;
            }
            graph_move_pin(caller, call, side, at, position++);
        }
    }
    graph_set_node_reference(caller, call, target->graph_id, target->name);
    return NULL;
}

static int fail(library_link *link, const char *message, const char *name)
{
    if (link->error && link->error_size) {
        snprintf(link->error, link->error_size, "%s: %s", name, message);
    }
    return -1;
}

static uint64_t shifted(uint64_t id, uint64_t offset)
{
    return id ? id + offset : 0;
}

static void shift_graph(graph *g, uint64_t offset, bool local_calls)
{
    g->graph_id = shifted(g->graph_id, offset);
    g->entry_id = shifted(g->entry_id, offset);
    g->return_id = shifted(g->return_id, offset);
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        n->id += offset;
        if (local_calls && !n->reference_asset && call_node(n)) {
            n->reference_id = shifted(n->reference_id, offset);
        }
        for (int side = 0; side < 2; side++) {
            pin_spec *pins = side ? n->output_pins : n->input_pins;
            int count = side ? n->output_count : n->input_count;
            for (int p = 0; p < count; p++) {
                pins[p].id = shifted(pins[p].id, offset);
                pins[p].split_parent = shifted(pins[p].split_parent, offset);
                pins[p].field_source = shifted(pins[p].field_source, offset);
                if (pins[p].binding && pins[p].binding->kind == PIN_BINDING_LOCAL) {
                    pins[p].binding->local_id = shifted(pins[p].binding->local_id, offset);
                }
            }
        }
    }
    for (int i = 0; i < acount(g->edges); i++) {
        edge *e = &g->edges[i];
        e->from_id += offset;
        e->to_id += offset;
        e->from_pin_id += offset;
        e->to_pin_id += offset;
    }
    for (int i = 0; i < acount(g->comment_boxes); i++) {
        g->comment_boxes[i].id += offset;
    }
    /* A library function's named values move with its nodes. */
    for (int i = 0; i < acount(g->locals); i++) {
        g->locals[i].id = shifted(g->locals[i].id, offset);
        g->locals[i].source_id = shifted(g->locals[i].source_id, offset);
        g->locals[i].source_pin_id = shifted(g->locals[i].source_pin_id, offset);
    }
    graph_reindex(g);
}

static int link_graph(library_link *link, graph *g);

static int import_library(library_link *link, const char *name, uint64_t *offset, bool *transient)
{
    for (int i = 0; i < acount(link->imports); i++) {
        if (strcmp(link->imports[i].name, name)) {
            continue;
        }
        if (link->imports[i].active) {
            return fail(link, "cyclic library dependency", name);
        }
        *offset = link->imports[i].offset;
        *transient = link->imports[i].transient_function_ids;
        return 0;
    }
    const behavior_asset *source = NULL;
    for (int i = 0; i < link->count; i++) {
        if (strcmp(link->catalog[i]->name, name) == 0) {
            source = link->catalog[i];
        }
    }
    if (!source) {
        return fail(link, "missing function library", name);
    }
    bool shader = link->result->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                  link->result->domain == BEHAVIOR_DOMAIN_SHAPE ||
                  link->result->domain == BEHAVIOR_DOMAIN_MODEL ||
                  link->result->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    if (shader != (source->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY)) {
        return fail(link, "library belongs to a different execution domain", name);
    }
    const char *error = behavior_asset_validate_library(source);
    if (error) {
        return fail(link, error, name);
    }
    behavior_asset *copy = behavior_asset_clone(source);
    if (!copy) {
        return fail(link, "could not copy library", name);
    }
    *offset = link->result->graph->id_counter;
    link->result->graph->id_counter += copy->graph->id_counter;
    int index = acount(link->imports);
    *transient = source->transient_function_ids;
    apush(link->imports, ((library_import){ source->name, *offset, true, *transient }));
    int first = acount(link->result->graphs);
    for (int i = 1; i < acount(copy->graphs); i++) {
        graph *g = copy->graphs[i];
        g->source_graph_id = g->graph_id;
        g->source_asset = graph_own_str(g, name, strlen(name));
        shift_graph(g, *offset, true);
        g->owner = link->result;
        g->shared_id_counter = &link->result->graph->id_counter;
        apush(link->result->graphs, g);
    }
    asetlen(copy->graphs, 1);
    behavior_asset_free(copy);
    int last = acount(link->result->graphs);
    for (int i = first; i < last; i++) {
        if (link_graph(link, link->result->graphs[i])) {
            return -1;
        }
    }
    link->imports[index].active = false;
    return 0;
}

/* A source library's functions have no stable graph ids, so a call into one
 * names its function. */
static graph *imported_function(const behavior_asset *result, const char *library, const char *name)
{
    for (int i = 1; name && i < acount(result->graphs); i++) {
        graph *g = result->graphs[i];
        if (g->source_asset && !strcmp(g->source_asset, library) &&
            !strcmp(g->name, name)) {
            return g;
        }
    }
    return NULL;
}

static int link_graph(library_link *link, graph *g)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (n->disabled) {
            continue;
        }
        bool event_call = n->desc == &NODE_CallEvent ||
                          strcmp(n->desc->name, "Send Event") == 0;
        if (event_call || n->desc == &NODE_Event) {
            const behavior_asset *event = NULL;
            for (int d = 0; d < link->count; d++) {
                if (link->catalog[d]->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION &&
                    n->reference_name &&
                    strcmp(link->catalog[d]->name, n->reference_name) == 0) {
                    event = link->catalog[d];
                }
            }
            if (event_call && !event) {
                return fail(link, "missing event definition", n->reference_name ? n->reference_name : "Event");
            }
            if (event && !graph_event_matches(n, event)) {
                return fail(link, "event payload signature changed", event->name);
            }
        }
        /* Whole-model references are resolved by the model compiler. */
        if (!n->reference_asset || (link->result->domain == BEHAVIOR_DOMAIN_MODEL &&
                                    (!strcmp(n->desc->name, "Model Instance") ||
                                     !strcmp(n->desc->name, "Model Material Slot")))) {
            continue;
        }
        if (!call_node(n)) {
            return fail(link, "external reference is not a function call", n->reference_asset);
        }
        uint64_t offset;
        bool transient;
        if (import_library(link, n->reference_asset, &offset, &transient)) {
            return -1;
        }
        graph *target = n->reference_id && !transient
                          ? behavior_asset_find_graph(link->result, n->reference_id + offset)
                          : imported_function(link->result, n->reference_asset, n->reference_name);
        if (!target || target->kind != GRAPH_FUNCTION) {
            return fail(link, "missing library function", n->reference_asset);
        }
        if (transient) {
            /* Generated pin IDs change too; names retain defaults and wires. */
            for (int side = 0; side < 2; side++) {
                pin_spec *pins = side ? n->output_pins : n->input_pins;
                int count = side ? n->output_count : n->input_count;
                for (int p = 0; p < count; p++) {
                    pins[p].declaration_id = 0;
                }
            }
            const char *error = graph_library_refresh_call(g, n, target);
            if (error) {
                return fail(link, error, n->reference_asset);
            }
        }
        n->reference_id = target->graph_id;
        n->reference_name = graph_own_str(g, target->name, strlen(target->name));
        n->reference_asset = NULL;
    }
    return 0;
}

static int expand_macros(library_link *link, graph *g, uint64_t *ancestors)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        node *call = g->nodes[i];
        if (call->disabled || call->desc != &NODE_CallMacro) {
            continue;
        }
        graph *source = behavior_asset_find_graph(link->result, call->reference_id);
        if (!source || source->kind != GRAPH_MACRO) {
            return fail(link, "missing macro", g->name);
        }
        for (int j = 0; j < acount(ancestors); j++) {
            if (ancestors[j] == source->graph_id) {
                return fail(link, "recursive macro expansion", source->name);
            }
        }
        if (acount(ancestors) >= 64) {
            return fail(link, "macro expansion depth exceeds 64", source->name);
        }
        av_doc *doc = av_doc_new();
        graph *copy = graph_from_value(graph_to_value(doc, source));
        av_doc_free(doc);
        if (!copy) {
            return fail(link, "could not expand macro", source->name);
        }
        copy->graph_id = source->graph_id;
        copy->entry_id = source->entry_id;
        copy->return_id = source->return_id;
        copy->kind = GRAPH_MACRO;
        copy->source_asset = graph_own_str(
            copy,
            link->result->name,
            strlen(link->result->name)
        );
        copy->source_graph_id = source->graph_id;
        uint64_t offset = link->result->graph->id_counter;
        link->result->graph->id_counter += copy->id_counter;
        shift_graph(copy, offset, false);
        copy->owner = link->result;
        copy->shared_id_counter = &link->result->graph->id_counter;
        apush(link->result->graphs, copy);
        call->reference_id = copy->graph_id;
        uint64_t *path = NULL;
        for (int j = 0; j < acount(ancestors); j++) {
            apush(path, ancestors[j]);
        }
        apush(path, source->graph_id);
        int result = expand_macros(link, copy, path);
        afree(path);
        if (result) {
            return result;
        }
    }
    return 0;
}

behavior_asset *behavior_asset_link_libraries(
    const behavior_asset *source,
    const behavior_asset *const *catalog,
    int count,
    char *error,
    size_t error_size
)
{
    if (error && error_size) {
        error[0] = '\0';
    }
    behavior_asset *result = behavior_asset_clone(source);
    if (!result) {
        if (error && error_size) {
            snprintf(error, error_size, "the asset does not load back: %s", graph_last_error());
        }
        return NULL;
    }
    library_link link = { .result = result,
                          .catalog = catalog,
                          .count = count,
                          .error = error,
                          .error_size = error_size };
    int graphs = acount(result->graphs);
    for (int i = 0; i < graphs; i++) {
        if (link_graph(&link, result->graphs[i])) {
            behavior_asset_free(result);
            result = NULL;
            break;
        }
    }
    int expanded_graphs = result ? acount(result->graphs) : 0;
    for (int i = 0; i < expanded_graphs; i++) {
        if (result->graphs[i]->kind == GRAPH_MACRO) {
            continue;
        }
        if (expand_macros(&link, result->graphs[i], NULL)) {
            behavior_asset_free(result);
            result = NULL;
            break;
        }
    }
    afree(link.imports);
    if (result && behavior_asset_lower_component_nodes(result, error, error_size)) {
        behavior_asset_free(result);
        result = NULL;
    }
    return result;
}
