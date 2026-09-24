#include "editor/graph/search.h"

#include "editor/ui/labels.h"
#include "editor/ui/search.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/events.h"
#include "graph/document/extract.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/shader/shader_graph.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pin_spec ed_math_inputs[] = { { .name = "a", .type = PIN_F64 },
                                     { .name = "b", .type = PIN_F64 } };
static pin_spec ed_math_outputs[] = { { .name = "Result", .type = PIN_F64 } };
static const node_desc ed_math_desc = {
    .name = "Behavior Expression",
    .category = "Math",
    .keywords = "formula expression equation calculate",
    .inputs = ed_math_inputs,
    .input_count = 2,
    .outputs = ed_math_outputs,
    .output_count = 1
};

static const node_desc ed_comment_box_desc = {
    .name = "Comment Box",
    .category = "Basic",
    .keywords = "comment note annotation",
    .display_class = NODE_DISPLAY_PURE
};

static int ed_desc_score(const node_desc *desc, const char *filter)
{
    char label[256], secondary[768];
    editor_node_label(desc, label, sizeof label);
    snprintf(
        secondary,
        sizeof secondary,
        "%s %s",
        desc->category ? desc->category : "",
        desc->keywords ? desc->keywords : ""
    );
    int score = editor_search_score(filter, label, secondary);
    int raw = editor_search_score(filter, desc->name, secondary);
    return score > raw ? score : raw;
}

static const char *ed_desc_category(const node_desc *d)
{
    return d && d->category && d->category[0] ? d->category : "Basic";
}

static int ed_search_match_cmp(const void *a, const void *b)
{
    const ed_search_match *ma = (const ed_search_match *)a;
    const ed_search_match *mb = (const ed_search_match *)b;
    const char *ca = ma->category;
    const char *cb = mb->category;
    int c = mb->score - ma->score;
    if (c) {
        return c;
    }
    c = strcmp(ca, cb);
    return c ? c : strcmp(ma->name, mb->name);
}

static bool ed_inline_inputs_default(const node *n)
{
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *pin = &n->input_pins[i];
        unsigned implicit_flags = PIN_FLAG_SETTING | PIN_FLAG_DEFAULT_SELF |
                                  PIN_FLAG_SERVICE;
        if (pin->kind != PIN_DATA || pin->type == PIN_FLOW ||
            (!(pin->flags & implicit_flags) && pin->type == PIN_STRUCT &&
             !pin->default_value.ptr) ||
            (!(pin->flags & implicit_flags) && pin->type == PIN_HANDLE &&
             !pin->default_value.u64)) {
            return false;
        }
    }
    return true;
}

static bool ed_node_can_inline(const editor *e, const node *n)
{
    graph *g = e ? e->active_graph : NULL;
    if (!ed_source_scope_available(e) || !g || !n || !n->desc || n->inline_source ||
        n->disabled || node_behavior_for(n) != NODE_BEHAVIOR_PURE ||
        n->desc->flow_kind != NODE_FLOW_NONE || n->desc == &NODE_CallFunction ||
        n->desc == &NODE_CallPureFunction || n->desc == &NODE_BehaviorExpression ||
        n->desc == &NODE_CallMacro || !ed_inline_inputs_default(n)) {
        return false;
    }
    for (int p = 0; p < n->output_count; p++) {
        if (graph_output_local(g, n, p)) {
            return false;
        }
    }
    const edge *outgoing = NULL;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *edge = &g->edges[i];
        if (edge->from_id != n->id) {
            continue;
        }
        if (edge->kind != GRAPH_EDGE_DATA || outgoing || edge->to_id == n->id) {
            return false;
        }
        outgoing = edge;
    }
    if (!outgoing) {
        return false;
    }
    node *target = graph_node_by_id(g, outgoing->to_id);
    int source_is_output = 0;
    int target_is_output = 0;
    const pin_spec *source_pin = graph_pin_by_id(n, outgoing->from_pin_id, &source_is_output, NULL);
    const pin_spec *target_pin = target
                                   ? graph_pin_by_id(target, outgoing->to_pin_id, &target_is_output, NULL)
                                   : NULL;
    if (!source_pin || !target_pin || !source_is_output || target_is_output ||
        source_pin->kind != PIN_DATA || target_pin->kind != PIN_DATA ||
        ((source_pin->flags | target_pin->flags) & PIN_FLAG_SETTING)) {
        return false;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *edge = &g->edges[i];
        if (edge->to_id != n->id) {
            continue;
        }
        node *source = graph_node_by_id(g, edge->from_id);
        if (edge->kind != GRAPH_EDGE_DATA || !graph_node_is_vector_constructor(n) ||
            !source || !graph_node_is_inline(g, source)) {
            return false;
        }
    }
    return true;
}

void ed_open_context_menu(editor *e, ImVec2 mouse)
{
    if (e->active_graph->math_expression) {
        return;
    }
    e->context_node = NULL;
    e->has_context_link = 0;
    ed_pin_view *pin = ed_hit_pin(e, mouse);
    ed_node_view *node_view = pin ? NULL : ed_hit_node(e, mouse);
    ed_link_ref link;
    if (node_view) {
        e->context_node = node_view->n;
        ImGui_OpenPopup("node_menu", 0);
    } else if (!pin && ed_hit_link(e, mouse, &link)) {
        e->context_link = link;
        e->has_context_link = 1;
        ImGui_OpenPopup("link_menu", 0);
    } else {
        ImVec2 world = ed_screen_to_world(e, mouse);
        e->popup_x = world.x;
        e->popup_y = world.y;
        ImGui_OpenPopup("spawn_menu", 0);
    }
}

/* PIN-11: a Generic (wildcard) data pin accepts any data peer; the type is
 * adopted in core on connect, so the editor treats it as compatible. */
static int ed_data_connectable(const pin_spec *source, const pin_spec *candidate)
{
    if ((source->flags | candidate->flags) & PIN_FLAG_SETTING) {
        return 0;
    }
    if ((candidate->flags & PIN_FLAG_REFERENCE) &&
        (!(source->flags & PIN_FLAG_REFERENCE) ||
         (!(candidate->flags & PIN_FLAG_WILDCARD) &&
          (source->type != candidate->type || source->container != candidate->container)))) {
        return 0;
    }
    if (source->kind == PIN_EXEC) {
        return 1;
    }
    if ((source->flags | candidate->flags) & PIN_FLAG_WILDCARD) {
        return 1;
    }
    return pin_types_compatible(source, candidate) ||
           conversion_for_pins(source, candidate) != NULL;
}

static const type_desc *ed_struct_output_type(const pin_spec *pin)
{
    if (!pin || pin->kind != PIN_DATA || pin->type != PIN_STRUCT ||
        pin->container != PIN_CONTAINER_SINGLE ||
        (pin->flags & PIN_FLAG_SETTING) || !pin->type_name) {
        return NULL;
    }
    return registry_type_by_name(pin->type_name);
}

static bool ed_struct_field_pin(const pin_spec *aggregate, int index, pin_spec *field_pin)
{
    const type_desc *type = ed_struct_output_type(aggregate);
    if (!type || index < 0 || index >= type->field_count || !field_pin) {
        return false;
    }
    const type_field *field = &type->fields[index];
    *field_pin = (pin_spec){ .name = field->name,
                             .type = field->type,
                             .type_name = field->type_name,
                             .kind = PIN_DATA,
                             .container = PIN_CONTAINER_SINGLE };
    return true;
}

static bool ed_struct_fields_connectable(const pin_spec *aggregate, const pin_spec *target)
{
    if (!target || target->kind != PIN_DATA) {
        return false;
    }
    const type_desc *type = ed_struct_output_type(aggregate);
    for (int i = 0; type && i < type->field_count; i++) {
        pin_spec field;
        if (ed_struct_field_pin(aggregate, i, &field) &&
            ed_data_connectable(&field, target)) {
            return true;
        }
    }
    return false;
}

static int ed_system_node_allowed(const node_desc *desc)
{
    if (desc == &ed_math_desc) {
        return 1;
    }
    if (desc && desc->type && type_is_plain(desc->type) &&
        (desc->component_kind == NODE_COMPONENT_GET_SELF ||
         desc->component_kind == NODE_COMPONENT_SET_SELF)) {
        return 1;
    }
    static const char *const names[] = {
        "Event", "Add",   "Multiply", "Subtract", "Divide", "Min",
        "Max",   "Clamp", "Modulo",   "Negate",   "Abs",    "Delta Time",
    };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        if (desc && desc->name && strcmp(desc->name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ed_compact_system_node(editor *e, node *n)
{
    if (e->ui_context != EDITOR_UI_CONTEXT_SYSTEM || !n || !n->desc) {
        return 0;
    }
    if (n->desc == &NODE_Event || !n->desc->type) {
        return 0;
    }
    int get = strncmp(n->desc->name, "Get_", 4) == 0;
    int set = strncmp(n->desc->name, "Set_", 4) == 0;
    if (!get && !set) {
        return 0;
    }
    if (get) {
        for (int i = n->input_count - 1; i >= 0; i--) {
            if (graph_remove_pin(e->active_graph, n, 0, i) != 0) {
                return -1;
            }
        }
        for (int i = n->output_count - 1; i >= 0; i--) {
            if (graph_remove_pin(e->active_graph, n, 1, i) != 0) {
                return -1;
            }
        }
    } else {
        for (int i = n->input_count - 1; i >= 0; i--) {
            if (graph_remove_pin(e->active_graph, n, 0, i) != 0) {
                return -1;
            }
        }
        pin_spec exec = { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC };
        if (graph_add_pin(e->active_graph, n, 0, &exec) < 0) {
            return -1;
        }
    }
    int output = get;
    if (n->desc->type->alias_of) {
        pin_spec pin = {
            .name = "Value",
            .type = PIN_STRUCT,
            .type_name = n->desc->type->alias_of,
            .kind = PIN_DATA,
        };
        return graph_add_pin(e->active_graph, n, output, &pin) < 0 ? -1 : 0;
    }
    for (int i = 0; i < n->desc->type->field_count; i++) {
        const type_field *field = &n->desc->type->fields[i];
        pin_spec pin = {
            .name = field->name,
            .type = field->type,
            .type_name = field->type_name,
            .kind = PIN_DATA,
        };
        if (graph_add_pin(e->active_graph, n, output, &pin) < 0) {
            return -1;
        }
    }
    return 0;
}

static int ed_spawn_desc_compatible(const editor *e, const node_desc *desc)
{
    if (desc == &ed_comment_box_desc) {
        return e->ui_context != EDITOR_UI_CONTEXT_SYSTEM && !e->spawn_link_pending;
    }
    if ((e->asset->domain == BEHAVIOR_DOMAIN_LIBRARY ||
         e->asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) &&
        !behavior_library_node_allowed(e->asset->domain, desc)) {
        return 0;
    }
    bool shader = e->asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                  e->asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
                  e->asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    bool field_node = desc->category && !strcmp(desc->category, "Procedural Field");
    if (e->asset->domain == BEHAVIOR_DOMAIN_FIELD && !field_node) {
        return 0;
    }
    if (field_node && e->asset->domain != BEHAVIOR_DOMAIN_FIELD &&
        e->asset->domain != BEHAVIOR_DOMAIN_LIBRARY) {
        return 0;
    }
    bool shader_node = desc->category && !strncmp(desc->category, "Shader", 6);
    if (shader != shader_node) {
        return 0;
    }
    if (e->ui_context == EDITOR_UI_CONTEXT_SYSTEM && !ed_system_node_allowed(desc)) {
        return 0;
    }
    if (desc == &NODE_CallFunction || desc == &NODE_CallPureFunction ||
        desc == &NODE_BehaviorExpression || desc == &NODE_CallMacro) {
        return 0;
    }
    if (!e->spawn_link_pending) {
        return 1;
    }
    const pin_spec
        *source = e->spawn_link_pin.kind == ED_PIN_OUTPUT
                    ? &e->spawn_link_pin.n->output_pins[e->spawn_link_pin.index]
                    : &e->spawn_link_pin.n->input_pins[e->spawn_link_pin.index];
    const pin_spec *pins = e->spawn_link_pin.kind == ED_PIN_OUTPUT ? desc->inputs
                                                                   : desc->outputs;
    int count = e->spawn_link_pin.kind == ED_PIN_OUTPUT ? desc->input_count
                                                        : desc->output_count;
    for (int i = 0; i < count; i++) {
        const pin_spec *candidate = &pins[i];
        if (source->kind != candidate->kind) {
            continue;
        }
        if (source->kind == PIN_EXEC ||
            (e->spawn_link_pin.kind == ED_PIN_OUTPUT
                 ? ed_data_connectable(source, candidate)
                 : ed_data_connectable(candidate, source) ||
                       ed_struct_fields_connectable(candidate, source))) {
            return 1;
        }
    }
    return 0;
}

static void ed_add_search_match(
    const editor *e,
    ed_search_match **matches,
    ed_search_match match,
    const node_desc *signature,
    const char *filter
)
{
    if (!ed_spawn_desc_compatible(e, signature)) {
        return;
    }
    node_desc named = *signature;
    named.name = match.name;
    match.score = ed_desc_score(&named, filter);
    if (match.score >= 0) {
        apush(*matches, match);
    }
}

static void ed_search_host_events(const editor *e, ed_search_match **matches, const char *filter)
{
    for (int i = 0; i < e->event_catalog_count; i++) {
        const editor_event_signature *event = &e->event_catalog[i];
        bool used = false;
        for (int g = 0; g < acount(e->asset->graphs); g++) {
            const graph *graph = e->asset->graphs[g];
            for (int n = 0; n < acount(graph->nodes); n++) {
                const node *node = graph->nodes[n];
                used |= node->desc->flow_kind == NODE_FLOW_EVENT &&
                        node->reference_name &&
                        !strcmp(node->reference_name, event->name);
            }
        }
        if (used) {
            continue;
        }
        pin_spec *outputs = NULL;
        apush(outputs, NODE_Event.outputs[0]);
        for (int p = 0; p < event->param_count; p++) {
            const editor_event_param *param = &event->params[p];
            apush(
                outputs,
                ((pin_spec){ .name = param->name,
                             .type = param->type,
                             .type_name = param->type_name })
            );
        }
        node_desc signature = NODE_Event;
        signature.outputs = outputs;
        signature.output_count = acount(outputs);
        ed_search_match match = { .desc = &NODE_Event,
                                  .host_event = i + 1,
                                  .category = "Events" };
        snprintf(match.name, sizeof match.name, "On %s", event->name);
        ed_add_search_match(e, matches, match, &signature, filter);
        afree(outputs);
    }
}

static void ed_search_events(const editor *e, ed_search_match **matches, const char *filter)
{
    const node_desc *descs[] = { &NODE_Event,
                                 &NODE_CallEvent,
                                 registry_node_by_name("Send Event") };
    const char *verbs[] = { "On", "Call", "Send" };
    for (int i = 0; i < acount(e->asset_catalog); i++) {
        const behavior_asset *event = e->asset_catalog[i];
        if (event->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            continue;
        }
        for (int action = 0; action < 3; action++) {
            const node_desc *desc = descs[action];
            if (!desc) {
                continue;
            }
            node_desc signature = *desc;
            pin_spec *pins = NULL;
            const pin_spec *base = action ? desc->inputs : desc->outputs;
            int count = action ? desc->input_count : desc->output_count;
            for (int p = 0; p < count; p++) {
                apush(pins, base[p]);
            }
            for (int p = 0; p < acount(event->variables); p++) {
                const behavior_variable *v = &event->variables[p];
                apush(
                    pins,
                    ((pin_spec){ .name = v->name,
                                 .type = v->type,
                                 .type_name = v->type_name,
                                 .container = v->container })
                );
            }
            if (action) {
                signature.inputs = pins;
                signature.input_count = acount(pins);
            } else {
                signature.outputs = pins;
                signature.output_count = acount(pins);
            }
            ed_search_match match = { .desc = desc, .event = event, .category = "Events" };
            snprintf(
                match.name,
                sizeof match.name,
                "%s %s",
                verbs[action],
                event->name
            );
            ed_add_search_match(e, matches, match, &signature, filter);
            afree(pins);
        }
    }
}

ed_search_match *ed_search_matches(const editor *e, const char *filter)
{
    ed_search_match *matches = NULL;
    for (int i = -1; i < registry_node_count(); i++) {
        const node_desc *desc = i < 0 ? &ed_math_desc : registry_node(i);
        if (desc == &NODE_GetVariable || desc == &NODE_SetVariable) {
            continue;
        }
        ed_search_match match = { .desc = desc, .category = ed_desc_category(desc) };
        editor_node_label(desc, match.name, sizeof match.name);
        if (desc == &NODE_Self) {
            if (!e->asset->owner_type_name) {
                continue;
            }
            pin_spec output = desc->outputs[0];
            output.type_name = e->asset->owner_type_name;
            node_desc signature = *desc;
            signature.outputs = &output;
            ed_add_search_match(e, &matches, match, &signature, filter);
            continue;
        }
        ed_add_search_match(e, &matches, match, desc, filter);
    }
    if (e->ui_context == EDITOR_UI_CONTEXT_SYSTEM) {
        return matches;
    }
    ed_search_match comment = { .name = "Comment Box",
                                .desc = &ed_comment_box_desc,
                                .category = ed_desc_category(&ed_comment_box_desc) };
    ed_add_search_match(e, &matches, comment, &ed_comment_box_desc, filter);
    bool shader = e->asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                  e->asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
                  e->asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    for (int a = -1; a < acount(e->asset_catalog); a++) {
        const behavior_asset *asset = a < 0 ? e->asset : e->asset_catalog[a];
        if (a >= 0 && (!strcmp(asset->name, e->asset->name) ||
                       asset->domain != (shader ? BEHAVIOR_DOMAIN_SHADER_LIBRARY
                                                : BEHAVIOR_DOMAIN_LIBRARY))) {
            continue;
        }
        for (int i = 1; i < acount(asset->graphs); i++) {
            graph *function = asset->graphs[i];
            if (function == e->active_graph ||
                (function->math_expression &&
                 !strcmp(function->name, function->math_expression)) ||
                (function->kind != GRAPH_FUNCTION && function->kind != GRAPH_MACRO)) {
                continue;
            }
            if (a >= 0 && function->kind != GRAPH_FUNCTION) {
                continue;
            }
            node *entry = graph_node_by_id(function, function->entry_id);
            node *ret = graph_node_by_id(function, function->return_id);
            if (!entry || !ret) {
                continue;
            }
            bool field = e->asset->domain == BEHAVIOR_DOMAIN_FIELD;
            if (field) {
                bool compatible = ret->input_count == 1;
                for (int side = 0; side < 2; side++) {
                    const pin_spec *pins = side ? ret->input_pins : entry->output_pins;
                    int count = side ? ret->input_count : entry->output_count;
                    for (int p = 0; p < count; p++) {
                        compatible &= pins[p].kind == PIN_DATA &&
                                      pins[p].type == PIN_F32 &&
                                      pins[p].container == PIN_CONTAINER_SINGLE;
                    }
                }
                for (int n = 0; n < acount(function->nodes); n++) {
                    const node_desc *desc = function->nodes[n]->desc;
                    compatible &= desc == &NODE_FunctionEntry ||
                                  desc == &NODE_Return ||
                                  desc == &NODE_CallPureFunction ||
                                  (desc->category &&
                                   !strcmp(desc->category, "Procedural Field"));
                }
                if (!compatible) {
                    continue;
                }
            }
            const char *category = field  ? "Procedural Field"
                                 : shader ? "Shader/Functions"
                                 : function->kind == GRAPH_MACRO ? "Macros"
                                                                 : "Functions";
            bool executable = false;
            for (int p = 0; p < entry->output_count; p++) {
                executable |= entry->output_pins[p].kind == PIN_EXEC;
            }
            for (int p = 0; p < ret->input_count; p++) {
                executable |= ret->input_pins[p].kind == PIN_EXEC;
            }
            ed_search_match match = { .function = function,
                                      .category = category,
                                      .desc = function->kind == GRAPH_MACRO ? &NODE_CallMacro
                                            : executable ? &NODE_CallFunction
                                                         : &NODE_CallPureFunction };
            if (a < 0) {
                editor_identifier_label(function->name, match.name, sizeof match.name);
            } else {
                snprintf(
                    match.name,
                    sizeof match.name,
                    "%s / %s",
                    asset->name,
                    function->name
                );
            }
            node_desc signature = { .name = match.name,
                                    .category = category,
                                    .inputs = entry->output_pins,
                                    .input_count = entry->output_count,
                                    .outputs = ret->input_pins,
                                    .output_count = ret->input_count };
            ed_add_search_match(e, &matches, match, &signature, filter);
        }
    }
    if (!shader && e->asset->domain == BEHAVIOR_DOMAIN_EVENT) {
        ed_search_host_events(e, &matches, filter);
        ed_search_events(e, &matches, filter);
        for (int i = 0; i < acount(e->asset->variables); i++) {
            const behavior_variable *v = &e->asset->variables[i];
            for (int set = 0; set < 2; set++) {
                const node_desc *desc = set ? &NODE_SetVariable : &NODE_GetVariable;
                pin_spec data = { .name = v->name,
                                  .type = v->type,
                                  .type_name = v->type_name,
                                  .container = v->container };
                pin_spec inputs[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
                                      data };
                pin_spec outputs[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
                                       data };
                node_desc signature = *desc;
                signature.inputs = set ? inputs : NULL;
                signature.input_count = set ? 2 : 0;
                signature.outputs = set ? outputs : &data;
                signature.output_count = set ? 2 : 1;
                ed_search_match match = { .desc = desc,
                                          .variable = v,
                                          .category = "Variables" };
                char name[256];
                editor_identifier_label(v->name, name, sizeof name);
                snprintf(match.name, sizeof match.name, "%s %s", set ? "Set" : "Get", name);
                ed_add_search_match(e, &matches, match, &signature, filter);
            }
        }
        const node_desc *spawn = registry_node_by_name("Spawn Prototype");
        for (int i = 0; spawn && i < acount(e->prototype_catalog); i++) {
            ed_search_match match = { .desc = spawn,
                                      .prototype = e->prototype_catalog[i],
                                      .category = "Prototypes" };
            snprintf(match.name, sizeof match.name, "Spawn %s", match.prototype);
            ed_add_search_match(e, &matches, match, spawn, filter);
        }
    }
    if (acount(matches) > 1) {
        qsort(matches, (size_t)acount(matches), sizeof *matches, ed_search_match_cmp);
    }
    return matches;
}

node *ed_spawn_match(editor *e, const ed_search_match *match)
{
    graph *g = e->active_graph;
    if (match->function) {
        return match->function->kind == GRAPH_MACRO
                 ? behavior_asset_add_macro_call_node_to_graph(
                       e->asset,
                       g,
                       match->function
                   )
                 : behavior_asset_add_function_call_node_to_graph(
                       e->asset,
                       g,
                       match->function
                   );
    }
    if (match->desc == &ed_math_desc) {
        return behavior_expression_add(e->asset, g, "a + b", NULL);
    }
    if (!strcmp(match->desc->name, "Shader Surface Output")) {
        return shader_graph_add_output(g, false);
    }
    if (!strcmp(match->desc->name, "Shader Shape Output")) {
        return shader_graph_add_output(g, true);
    }
    node *n = graph_add_node(g, match->desc);
    if (!n) {
        return NULL;
    }
    if (match->desc == &NODE_Self) {
        if (!e->asset->owner_type_name ||
            graph_set_pin_type(g, n, 1, 0, PIN_HANDLE, e->asset->owner_type_name) != 0) {
            graph_remove_node(g, n);
            return NULL;
        }
    }
    if (match->variable) {
        const behavior_variable *v = match->variable;
        bool set = match->desc == &NODE_SetVariable;
        graph_set_node_reference(g, n, v->id, v->name);
        if (set) {
            graph_set_pin_type(g, n, 0, 1, v->type, v->type_name);
            graph_set_pin_container(g, n, 0, 1, v->container);
        }
        graph_set_pin_type(g, n, 1, set ? 1 : 0, v->type, v->type_name);
        graph_set_pin_container(g, n, 1, set ? 1 : 0, v->container);
    }
    if (match->host_event) {
        const editor_event_signature *signature = &e->event_catalog[match->host_event - 1];
        graph_set_node_reference(g, n, 0, signature->name);
        ed_event_add_declared_params(e, n, signature);
    }
    if (match->event && graph_event_apply(g, n, match->event) != 0) {
        graph_remove_node(g, n);
        return NULL;
    }
    if (match->prototype) {
        pin_value value = { .type = PIN_NAME, .str = match->prototype };
        graph_set_pin_default(g, n, 0, 1, &value);
    }
    return n;
}

static int ed_search_match_base_output_count(const ed_search_match *match)
{
    if (!match) {
        return 0;
    }
    if (match->function) {
        node *ret = graph_node_by_id(match->function, match->function->return_id);
        return ret ? ret->input_count : 0;
    }
    if (match->variable) {
        return 1;
    }
    return match->desc ? match->desc->output_count : 0;
}

static bool ed_search_match_base_output(
    const editor *e,
    const ed_search_match *match,
    int index,
    pin_spec *output
)
{
    if (!e || !match || !output || index < 0) {
        return false;
    }
    if (match->function) {
        node *ret = graph_node_by_id(match->function, match->function->return_id);
        if (!ret || index >= ret->input_count) {
            return false;
        }
        *output = ret->input_pins[index];
        return true;
    }
    if (match->variable) {
        if (index != 0) {
            return false;
        }
        *output = (pin_spec){ .name = match->variable->name,
                              .type = match->variable->type,
                              .type_name = match->variable->type_name,
                              .kind = PIN_DATA,
                              .container = match->variable->container };
        return true;
    }
    if (!match->desc || index >= match->desc->output_count || !match->desc->outputs) {
        return false;
    }
    *output = match->desc->outputs[index];
    if (match->desc == &NODE_Self && index == 0) {
        if (!e->asset->owner_type_name) {
            return false;
        }
        output->type_name = e->asset->owner_type_name;
    }
    return true;
}

int ed_search_match_output_count(const editor *e, const ed_search_match *match)
{
    int base_count = ed_search_match_base_output_count(match);
    int count = base_count;
    for (int i = 0; i < base_count; i++) {
        pin_spec output;
        if (!ed_search_match_base_output(e, match, i, &output)) {
            return count;
        }
        const type_desc *type = ed_struct_output_type(&output);
        count += type ? type->field_count : 0;
    }
    return count;
}

bool ed_search_match_output(const editor *e, const ed_search_match *match, int index, ed_search_output *output)
{
    if (!output || index < 0) {
        return false;
    }
    int base_count = ed_search_match_base_output_count(match);
    int remaining = index;
    int projected = 0;
    for (int i = 0; i < base_count; i++) {
        pin_spec base;
        if (!ed_search_match_base_output(e, match, i, &base)) {
            return false;
        }
        if (remaining == 0) {
            output->pin = base;
            output->node_pin = i;
            return true;
        }
        remaining--;
        const type_desc *type = ed_struct_output_type(&base);
        if (!type) {
            continue;
        }
        if (remaining < type->field_count) {
            if (!ed_struct_field_pin(&base, remaining, &output->pin)) {
                return false;
            }
            output->node_pin = base_count + projected + remaining;
            return true;
        }
        remaining -= type->field_count;
        projected += type->field_count;
    }
    return false;
}

int ed_expose_spawned_output_fields(editor *e, node *n)
{
    if (!e || !e->active_graph || !n) {
        return -1;
    }
    int output_count = n->output_count;
    for (int i = 0; i < output_count; i++) {
        if (!graph_output_struct(n, i)) {
            continue;
        }
        if (graph_expose_output_fields(e->active_graph, n, i) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ed_autowire_spawn_input(editor *e, node *n, bool inline_source)
{
    int connected = 0;
    const pin_spec *input = &e->spawn_link_pin.n->input_pins[e->spawn_link_pin.index];
    for (int p = 0; !connected && p < n->output_count; p++) {
        ed_pin_view target = { n, ED_PIN_OUTPUT, p, { 0.0f, 0.0f } };
        const pin_spec *output = &n->output_pins[p];
        if (output->kind == input->kind && ed_data_connectable(output, input)) {
            connected = ed_connect_with_inline(e, &e->spawn_link_pin, &target, inline_source);
        }
    }
    return connected;
}

static int ed_autowire_spawn(editor *e, node *n)
{
    bool inline_source = editor_ui_inline_edge_nodes();
    if (e->spawn_link_pin.kind == ED_PIN_OUTPUT) {
        int connected = 0;
        for (int p = 0; p < n->input_count && !connected; p++) {
            ed_pin_view target = { n, ED_PIN_INPUT, p, { 0.0f, 0.0f } };
            const pin_spec *a = &e->spawn_link_pin.n
                                     ->output_pins[e->spawn_link_pin.index];
            const pin_spec *b = &n->input_pins[p];
            if (a->kind == b->kind && ed_data_connectable(a, b)) {
                connected = ed_connect_with_inline(e, &e->spawn_link_pin, &target, inline_source);
            }
        }
        return connected;
    }
    int connected = ed_autowire_spawn_input(e, n, inline_source);
    if (connected || ed_expose_spawned_output_fields(e, n) != 0) {
        return connected;
    }
    return ed_autowire_spawn_input(e, n, inline_source);
}

node *ed_spawn_match_at(editor *e, const ed_search_match *match, float x, float y)
{
    if (match->desc == &ed_comment_box_desc) {
        ed_snapshot_before(e);
        graph_add_comment_box(e->active_graph, "Comment", x, y, 320.0f, 160.0f);
        return NULL;
    }
    if (!e->spawn_link_pending) {
        ed_snapshot_before(e);
    }
    node *n = ed_spawn_match(e, match);
    if (n && !shader_graph_init_node(e->active_graph, n)) {
        graph_remove_node(e->active_graph, n);
        n = NULL;
        ed_show_error(e, "This shader already has 32 parameters.");
    }
    if (n && ed_compact_system_node(e, n) != 0) {
        graph_remove_node(e->active_graph, n);
        n = NULL;
    }
    if (n && !match->event && !match->host_event && ed_name_spawned_event(e, n) != 0) {
        graph_remove_node(e->active_graph, n);
        n = NULL;
    }
    if (!n) {
        return NULL;
    }
    n->x = x;
    n->y = y;
    if (match->desc == &ed_math_desc) {
        ed_select_node(e, n, 0);
    }
    if (e->spawn_link_pending) {
        ed_autowire_spawn(e, n);
        ed_commit_snapshot(e);
        e->spawn_link_pending = 0;
    }
    return n;
}

void ed_request_extraction(editor *e)
{
    if (e->active_graph->math_expression || e->ui_context == EDITOR_UI_CONTEXT_SYSTEM) {
        return;
    }
    for (int i = 1;; i++) {
        if (i == 1) {
            snprintf(e->extract_name, sizeof e->extract_name, "New Function");
        } else {
            snprintf(e->extract_name, sizeof e->extract_name, "New Function %d", i);
        }
        if (!behavior_asset_find_graph_by_name(e->asset, e->extract_name)) {
            break;
        }
    }
    e->extract_error[0] = '\0';
    e->extract_requested = true;
}

static const char *ed_extract_selection(editor *e)
{
    if (e->ui_context == EDITOR_UI_CONTEXT_SYSTEM) {
        return "Systems do not support extracted functions.";
    }
    if (e->active_graph->math_expression) {
        return "Generated math graphs are read-only";
    }
    behavior_asset *result = NULL;
    uint64_t call_id = 0;
    const char *error = behavior_asset_extract_function(
        e->asset,
        e->active_graph,
        e->selected,
        acount(e->selected),
        e->extract_name,
        &result,
        &call_id
    );
    if (error) {
        return error;
    }
    char *json = behavior_asset_save_json(result);
    behavior_asset_free(result);
    if (!json) {
        return "Could not save the extracted function.";
    }
    ed_snapshot_before(e);
    int adopted = ed_adopt_behavior_asset(e, json);
    free(json);
    assert(adopted == 0);
    node *call = graph_node_by_id(e->active_graph, call_id);
    assert(call);
    ed_select_node(e, call, 0);
    return NULL;
}

static node *ed_convert_behavior_expression_selection(
    editor *e,
    node *const *selection,
    int count,
    behavior_expression_error *error
)
{
    ed_snapshot_before(e);
    node *call = behavior_expression_convert_selection(e->asset, e->active_graph, selection, count, error);
    if (!call) {
        return NULL;
    }
    ed_select_node(e, call, 0);
    return call;
}

void ed_draw_extract_popup(editor *e)
{
    if (e->extract_requested) {
        ImGui_OpenPopup("extract_function", 0);
        e->extract_requested = false;
    }
    if (!ImGui_BeginPopup("extract_function", 0)) {
        return;
    }
    ImGui_TextUnformatted("Extract Function");
    if (ImGui_IsWindowAppearing()) {
        ImGui_SetKeyboardFocusHere();
    }
    bool enter = ImGui_InputText(
        "Name",
        e->extract_name,
        sizeof e->extract_name,
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
    );
    editor_ui_track_item("graph.extract.name");
    if (e->extract_error[0]) {
        ImGui_TextColored((ImVec4){ 1.0f, 0.55f, 0.45f, 1.0f }, "%s", e->extract_error);
    }
    bool extract = ImGui_Button("Extract");
    editor_ui_track_item("graph.extract.confirm");
    if (extract || enter) {
        const char *error = ed_extract_selection(e);
        if (error) {
            snprintf(e->extract_error, sizeof e->extract_error, "%s", error);
        } else {
            ImGui_CloseCurrentPopup();
        }
    }
    ImGui_SameLine();
    if (ImGui_Button("Cancel")) {
        ImGui_CloseCurrentPopup();
    }
    ImGui_EndPopup();
}

void ed_handle_popups(editor *e)
{
    if (e->active_graph->math_expression) {
        return;
    }
    if (ImGui_BeginPopup("node_menu", 0)) {
        node *n = e->context_node;
        bool advanced = false;
        for (int p = 0; n && p < n->input_count; p++) {
            advanced |= (n->input_pins[p].flags &
                         (PIN_FLAG_ADVANCED | PIN_FLAG_SETTING)) == PIN_FLAG_ADVANCED;
        }
        if (advanced &&
            ImGui_MenuItemEx("Show Advanced Inputs", NULL, n->show_advanced, true)) {
            ed_snapshot_before(e);
            n->show_advanced = !n->show_advanced;
            e->edit_changed = 1;
        }
        node *single[] = { n };
        node *const *selection = n && !ed_node_selected(e, n) ? single : e->selected;
        int selection_count = n && !ed_node_selected(e, n) ? 1 : acount(e->selected);
        const char *extract_reason = behavior_asset_can_extract_function(
            e->asset,
            e->active_graph,
            selection,
            selection_count
        );
        if (ImGui_MenuItemEx("Extract Function...", NULL, false, !extract_reason && e->ui_context != EDITOR_UI_CONTEXT_SYSTEM)) {
            if (selection == single) {
                ed_select_node(e, n, 0);
            }
            ed_request_extraction(e);
        }
        const char *expression_reason = behavior_expression_can_convert_selection(
            e->asset,
            e->active_graph,
            selection,
            selection_count
        );
        if (ImGui_MenuItemEx(
                "Convert to Behavior Expression",
                NULL,
                false,
                !expression_reason && e->ui_context != EDITOR_UI_CONTEXT_SYSTEM
            )) {
            behavior_expression_error error;
            node *call = ed_convert_behavior_expression_selection(e, selection, selection_count, &error);
            if (!call) {
                ed_show_error(e, error.message[0] ? error.message : "Could not convert the selection to a Behavior Expression.");
            }
            ImGui_CloseCurrentPopup();
            ImGui_EndPopup();
            return;
        }
        editor_ui_track_item("graph.node.convert_math");
        ImGui_Separator();
        if (ed_is_graph_call(n)) {
            graph *target = behavior_asset_find_graph(e->asset, n->reference_id);
            if (n->reference_asset) {
                target = NULL;
                for (int a = 0; a < acount(e->asset_catalog); a++) {
                    if (strcmp(e->asset_catalog[a]->name, n->reference_asset) == 0) {
                        target = behavior_asset_find_graph(e->asset_catalog[a], n->reference_id);
                    }
                }
                if (ImGui_MenuItemEx("Refresh Signature", NULL, false, target != NULL)) {
                    ed_snapshot_before(e);
                    const char *error = graph_library_refresh_call(e->active_graph, n, target);
                    if (error) {
                        ed_show_error(e, error);
                    } else {
                        ed_commit_snapshot(e);
                    }
                }
            }

            if (ImGui_MenuItemEx(
                    n->desc == &NODE_CallMacro ? "Open Macro" : "Open Function",
                    "Double-click",
                    false,
                    target && target != e->active_graph
                )) {
                e->open_call_node = n->id;
                ImGui_CloseCurrentPopup();
                ImGui_EndPopup();
                return;
            }
            ImGui_Separator();
        }
        if (n && n->desc && strcmp(n->desc->name, "Sequence") == 0) {
            if (ImGui_MenuItem("Add output pin")) {
                char name[32];
                snprintf(name, sizeof name, "Then%d", n->output_count);
                pin_spec pin = { name, PIN_FLOW, NULL, PIN_EXEC };
                ed_snapshot_before(e);
                graph_add_pin(e->active_graph, n, 1, &pin);
            }
            if (n->output_count > 2 && ImGui_MenuItem("Remove last output pin")) {
                ed_snapshot_before(e);
                graph_remove_pin(e->active_graph, n, 1, n->output_count - 1);
            }
            ImGui_Separator();
        }
        if (n && ImGui_MenuItem(n->disabled ? "Enable" : "Disable")) {
            ed_snapshot_before(e);
            graph_set_node_disabled(e->active_graph, n, !n->disabled);
        }
        if (n && node_behavior_for(n) != NODE_BEHAVIOR_PURE &&
            ImGui_MenuItem(n->breakpoint ? "Clear Breakpoint" : "Set Breakpoint")) {
            ed_snapshot_before(e);
            graph_set_breakpoint(e->active_graph, n, !n->breakpoint);
        }
        if (n && ImGui_MenuItem(
                     n->breakpoint_enabled ? "Disable All Breakpoints" : "Enable All Breakpoints"
                 )) {
            ed_snapshot_before(e);
            graph_set_all_breakpoints(e->active_graph, !e->active_graph->breakpoints_enabled);
        }
        if (n && ImGui_MenuItem("Toggle data watches")) {
            ed_snapshot_before(e);
            for (int i = 0; i < n->input_count; i++) {
                if (n->input_pins[i].kind == PIN_DATA) {
                    graph_set_pin_watch(
                        e->active_graph,
                        n,
                        0,
                        i,
                        !(n->input_pins[i].flags & PIN_FLAG_WATCH)
                    );
                }
            }
            for (int i = 0; i < n->output_count; i++) {
                if (n->output_pins[i].kind == PIN_DATA) {
                    graph_set_pin_watch(
                        e->active_graph,
                        n,
                        1,
                        i,
                        !(n->output_pins[i].flags & PIN_FLAG_WATCH)
                    );
                }
            }
        }
        bool can_inline = ed_node_can_inline(e, n);
        if (ImGui_MenuItemEx("Inline", NULL, false, can_inline)) {
            ed_snapshot_before(e);
            int result = graph_set_node_inline(e->active_graph, n, true);
            assert(result == 0);
            (void)result;
            ImGui_CloseCurrentPopup();
        }
        editor_ui_track_item("graph.node.inline");
        if (n && ed_field_output(n) >= 0 && ImGui_BeginMenu("Fields")) {
            ed_draw_fields_menu(e, n);
            ImGui_EndMenu();
        }
        if (ImGui_MenuItem("Delete")) {
            ed_select_node(e, e->context_node, 0);
            ed_delete_selected(e);
        }
        ImGui_EndPopup();
    }
    if (ImGui_BeginPopup("link_menu", 0)) {
        if (ImGui_MenuItem("Delete Link") && e->has_context_link) {
            aclear(e->selected);
            aclear(e->selected_links);
            ed_add_selected_link(e, &e->context_link);
            ed_delete_selected(e);
        }
        ImGui_EndPopup();
    }
    ImVec2 search_space = ImGui_GetMainViewport()->WorkSize;
    ImGui_SetNextWindowSize((ImVec2){ fminf(360.0f, search_space.x - 20.0f), 0.0f }, ImGuiCond_Always);
    if (ImGui_BeginPopup("spawn_menu", 0)) {
        bool reveal_result = ImGui_IsWindowAppearing();
        if (reveal_result) {
            ImGui_SetKeyboardFocusHere();
            e->search_selected = 0;
        }
        ImGui_SetNextItemWidth(-1.0f);
        if (editor_ui_search_field("graph.node_search", e->search, sizeof e->search, "Search nodes")) {
            e->search_selected = 0;
            reveal_result = true;
        }
        ed_search_match *matches = ed_search_matches(e, e->search);
        int match_count = acount(matches);
        if (match_count == 0) {
            e->search_selected = -1;
        } else {
            if (e->search_selected < 0) {
                e->search_selected = 0;
            }
            if (e->search_selected >= match_count) {
                e->search_selected = match_count - 1;
            }
            if (ImGui_IsKeyPressed(ImGuiKey_DownArrow)) {
                e->search_selected = (e->search_selected + 1) % match_count;
                reveal_result = true;
            }
            if (ImGui_IsKeyPressed(ImGuiKey_UpArrow)) {
                e->search_selected = (e->search_selected + match_count - 1) % match_count;
                reveal_result = true;
            }
        }
        int activate = ImGui_IsKeyPressed(ImGuiKey_Enter);
        ImGui_BeginChild(
            "node_results",
            (ImVec2){ 0.0f, fmaxf(1.0f, fminf(300.0f, search_space.y - 140.0f)) },
            0,
            0
        );
        const char *category = NULL;
        for (int m = 0; m < match_count; m++) {
            const node_desc *desc = matches[m].desc;
            const char *next_category = matches[m].category;
            if (!category || strcmp(category, next_category) != 0) {
                ImGui_TextDisabled("%s", next_category);
                category = next_category;
            }
            char label[416], icon[4];
            snprintf(
                label,
                sizeof label,
                "%s  %s",
                ed_node_prefix(desc, icon),
                matches[m].name
            );
            ImGui_PushIDInt(m);
            int clicked = ImGui_SelectableEx(label, m == e->search_selected, 0, (ImVec2){ 0.0f, 0.0f });
            if (reveal_result && m == e->search_selected) {
                ImGui_SetScrollHereY(0.5f);
            }
            if (m == e->search_selected) {
                editor_ui_track_item("graph.node_search.selected");
            }
            ImGui_PopID();
            if (clicked || (activate && m == e->search_selected)) {
                bool comment = desc == &ed_comment_box_desc;
                node *n = ed_spawn_match_at(e, &matches[m], e->popup_x, e->popup_y);
                if (n && desc == &ed_math_desc) {
                    e->inspector_active = true;
                    editor_ui_request_focus(EDITOR_UI_FOCUS_INSPECTOR);
                }
                if (n || comment) {
                    e->search[0] = '\0';
                    e->search_selected = 0;
                    ImGui_CloseCurrentPopup();
                }
            }
        }
        if (match_count == 0) {
            ImGui_TextUnformatted(
                e->spawn_link_pending ? "No compatible nodes" : "No matching nodes"
            );
        }
        afree(matches);
        ImGui_Dummy((ImVec2){ 0.0f, ImGui_GetStyle()->ItemSpacing.y });
        ImGui_EndChild();
        editor_ui_track_item("graph.node_results");
        /* Keep search text focused while our result selection handles these keys. */
        ImGuiID navigation = ImGui_GetID("node_search_navigation");
        ImGui_SetKeyOwner(ImGuiKey_UpArrow, navigation, 0);
        ImGui_SetKeyOwner(ImGuiKey_DownArrow, navigation, 0);
        ImGui_SetKeyOwner(ImGuiKey_Enter, navigation, 0);
        ImGui_EndPopup();
    }
    if (e->spawn_link_pending && !ImGui_IsPopupOpen("spawn_menu", 0)) {
        ed_cancel_snapshot(e);
        e->spawn_link_pending = 0;
    }
}
