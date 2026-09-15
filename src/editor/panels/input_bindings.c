#include "editor/graph/eval_view.h"
#include "editor/graph/search.h"
#include "editor/ui/color.h"
#include "editor/ui/labels.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/authoring/layout.h"
#include "graph/authoring/optimize.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/shader/shader_graph.h"
#include "runtime/generated.h"

#include <assert.h>
#include <ctype.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    pin_binding binding;
    ed_search_match match;
    int source_pin;
    bool generic_source;
    char label[256];
    char search[256];
    char component_field_path[256];
    const char *group;
} ed_binding_choice;

static bool ed_binding_text_match(const char *text, const char *filter)
{
    if (!filter || !filter[0]) {
        return true;
    }
    if (!text) {
        return false;
    }
    for (const char *at = text; *at; at++) {
        const char *a = at;
        const char *b = filter;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return true;
        }
    }
    return false;
}

static bool ed_binding_choice_match(const ed_binding_choice *choice, const char *filter)
{
    return ed_binding_text_match(choice->label, filter) ||
           ed_binding_text_match(choice->search, filter);
}

static int ed_binding_compatible(const pin_spec *target, pin_type type, const char *type_name)
{
    pin_spec source = {
        .type = type,
        .type_name = type_name,
        .kind = PIN_DATA,
        .container = PIN_CONTAINER_SINGLE,
    };
    return pin_compatible(&source, target);
}

bool ed_source_scope_available(const editor *e)
{
    if (!e || !e->active_graph || !e->asset || e->active_graph->math_expression ||
        (e->ui_context != EDITOR_UI_CONTEXT_BEHAVIOR &&
         e->ui_context != EDITOR_UI_CONTEXT_SYSTEM)) {
        return false;
    }
    switch (e->asset->domain) {
        case BEHAVIOR_DOMAIN_EVENT:
        case BEHAVIOR_DOMAIN_LIBRARY:
        case BEHAVIOR_DOMAIN_MATERIAL:
        case BEHAVIOR_DOMAIN_SHAPE:
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return true;
    }
    return false;
}

bool ed_binding_scope_available(const editor *e)
{
    return e && e->ui_context == EDITOR_UI_CONTEXT_BEHAVIOR && e->asset &&
           e->asset->domain == BEHAVIOR_DOMAIN_EVENT;
}

static const ecs_component_desc *ed_component_by_key(const char *key)
{
    if (!key) {
        return NULL;
    }
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component && component->key && !strcmp(component->key, key)) {
            return component;
        }
    }
    return NULL;
}

enum
{
    ED_COMPONENT_FIELD_PATH_MAX_DEPTH = 16,
};

static bool ed_field_path_append(const char *path, const char *name, char *out, size_t cap)
{
    int length = snprintf(out, cap, "%s%s%s", path && path[0] ? path : "", path && path[0] ? "." : "", name);
    return length >= 0 && (size_t)length < cap;
}

static bool ed_field_path_label(const char *path, char *out, size_t cap)
{
    if (!path || !path[0] || !cap) {
        return false;
    }
    size_t used = 0;
    for (const char *at = path; at && *at;) {
        const char *end = strchr(at, '.');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        char segment[128];
        if (!length || length >= sizeof segment) {
            return false;
        }
        memcpy(segment, at, length);
        segment[length] = '\0';
        char label[128];
        editor_identifier_label(segment, label, sizeof label);
        int written = snprintf(out + used, cap - used, "%s%s", used ? "." : "", label);
        if (written < 0 || (size_t)written >= cap - used) {
            return false;
        }
        used += (size_t)written;
        if (!end) {
            return true;
        }
        at = end + 1;
    }
    return false;
}

static void ed_add_component_field_choice(
    const ecs_component_desc *component,
    const char *component_label,
    const type_field *field,
    const char *path,
    const pin_spec *target,
    ed_binding_choice **out
)
{
    if (!field || !field->name ||
        !ed_binding_compatible(target, field->type, field->type_name)) {
        return;
    }
    char path_label[128];
    if (!ed_field_path_label(path, path_label, sizeof path_label)) {
        return;
    }
    ed_binding_choice choice = {
        .binding = { .kind = PIN_BINDING_COMPONENT_FIELD,
                     .component_key = component->key },
        .group = "Self Components",
    };
    int path_length = snprintf(choice.component_field_path, sizeof choice.component_field_path, "%s", path);
    if (path_length < 0 || (size_t)path_length >= sizeof choice.component_field_path) {
        return;
    }
    snprintf(choice.label, sizeof choice.label, "Self.%s.%s", component->name, path);
    if (strchr(path, '.')) {
        snprintf(
            choice.search,
            sizeof choice.search,
            "Get %s.%s Get %s %s Self.%s.%s",
            component_label,
            path_label,
            component_label,
            path_label,
            component->name,
            path
        );
    } else {
        snprintf(choice.search, sizeof choice.search, "Get %s %s", component_label, path_label);
    }
    apush(*out, choice);
}

static void ed_add_nested_component_field_choices(
    const ecs_component_desc *component,
    const char *component_label,
    const type_desc *type,
    const char *path,
    int depth,
    const pin_spec *target,
    ed_binding_choice **out
)
{
    if (!type || depth >= ED_COMPONENT_FIELD_PATH_MAX_DEPTH) {
        return;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (!field->name) {
            continue;
        }
        char child_path[256];
        if (!ed_field_path_append(path, field->name, child_path, sizeof child_path)) {
            continue;
        }
        ed_add_component_field_choice(component, component_label, field, child_path, target, out);
        if (field->type == PIN_STRUCT && field->type_name) {
            ed_add_nested_component_field_choices(
                component,
                component_label,
                registry_type_by_name(field->type_name),
                child_path,
                depth + 1,
                target,
                out
            );
        }
    }
}

static bool ed_source_pin_defaults(const ed_search_match *match)
{
    if (match->variable) {
        return true;
    }
    const pin_spec *pins = match->desc->inputs;
    int count = match->desc->input_count;
    node *entry = match->function
                    ? graph_node_by_id(match->function, match->function->entry_id)
                    : NULL;
    if (match->function) {
        if (!entry) {
            return false;
        }
        pins = entry->output_pins;
        count = entry->output_count;
    }
    for (int i = 0; i < count; i++) {
        const pin_spec *pin = &pins[i];
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

static bool ed_source_match_allowed(const ed_search_match *match)
{
    if (!match || !match->desc || match->desc == &NODE_CallMacro ||
        match->desc == &NODE_CallFunction || match->desc->flow_kind != NODE_FLOW_NONE ||
        !node_desc_is_pure(match->desc) || !ed_source_pin_defaults(match)) {
        return false;
    }
    return true;
}

static void ed_source_choices(
    const editor *e,
    const pin_spec *target,
    ed_binding_choice **out,
    ed_search_match **matches_out
)
{
    if (!ed_source_scope_available(e)) {
        return;
    }
    ed_search_match *matches = ed_search_matches(e, "");
    *matches_out = matches;
    for (int i = 0; i < acount(matches); i++) {
        const ed_search_match *match = &matches[i];
        if (!ed_source_match_allowed(match)) {
            continue;
        }
        if (match->desc == &NODE_DeltaTime && ed_binding_scope_available(e) &&
            target->container == PIN_CONTAINER_SINGLE &&
            (target->type == PIN_F32 || target->type == PIN_F64) &&
            !(target->flags & PIN_FLAG_WILDCARD)) {
            continue;
        }
        int output_count = ed_search_match_output_count(e, match);
        for (int p = 0; p < output_count; p++) {
            ed_search_output output;
            if (!ed_search_match_output(e, match, p, &output) ||
                output.pin.kind != PIN_DATA || output.pin.type == PIN_FLOW ||
                !ed_connection_specs_valid(&output.pin, target, PIN_DATA)) {
                continue;
            }
            if (match->variable && ed_binding_scope_available(e) &&
                match->variable->container == target->container &&
                ed_binding_compatible(
                    target,
                    match->variable->type,
                    match->variable->type_name
                )) {
                continue;
            }
            ed_binding_choice choice = { .match = *match,
                                         .source_pin = output.node_pin,
                                         .generic_source = true,
                                         .group = "Graph Sources" };
            if (output_count == 1) {
                snprintf(choice.label, sizeof choice.label, "%s", match->name);
            } else {
                char pin_label[128];
                editor_identifier_label(output.pin.name ? output.pin.name : "Value", pin_label, sizeof pin_label);
                snprintf(choice.label, sizeof choice.label, "%s / %s", match->name, pin_label);
            }
            snprintf(
                choice.search,
                sizeof choice.search,
                "%s %s %s",
                match->name,
                output.pin.name ? output.pin.name : "Value",
                output.pin.type_name ? output.pin.type_name
                                     : pin_type_name(output.pin.type)
            );
            apush(*out, choice);
        }
    }
}

static void ed_binding_choices(
    const editor *e,
    const pin_spec *target,
    ed_binding_choice **out,
    ed_search_match **matches_out
)
{
    *matches_out = NULL;
    if (ed_binding_scope_available(e)) {
        for (int i = 0; i < acount(e->asset->variables); i++) {
            const behavior_variable *variable = &e->asset->variables[i];
            if (variable->container != target->container ||
                !ed_binding_compatible(target, variable->type, variable->type_name)) {
                continue;
            }
            ed_binding_choice choice = {
                .binding = { .kind = PIN_BINDING_VARIABLE,
                             .variable_id = variable->id,
                             .source_asset = e->asset->name },
                .group = "Variables",
            };
            snprintf(
                choice.label,
                sizeof choice.label,
                "%s",
                variable->name ? variable->name : "Variable"
            );
            apush(*out, choice);
        }
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
            const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
            const type_desc *type = component ? component->type : NULL;
            if (!component || !component->key || !component->name || !type) {
                continue;
            }
            char component_label[128];
            editor_component_label(component->name, component_label, sizeof component_label);
            if (ed_binding_compatible(target, PIN_STRUCT, type->name)) {
                ed_binding_choice choice = {
                    .binding = { .kind = PIN_BINDING_COMPONENT,
                                 .component_key = component->key },
                    .group = "Self Components",
                };
                snprintf(choice.label, sizeof choice.label, "Self.%s", component_label);
                snprintf(
                    choice.search,
                    sizeof choice.search,
                    "Get %s %s",
                    component_label,
                    component->name
                );
                apush(*out, choice);
            }
            for (int field = 0; field < type->field_count; field++) {
                const type_field *member = &type->fields[field];
                ed_add_component_field_choice(
                    component,
                    component_label,
                    member,
                    member->name,
                    target,
                    out
                );
            }
        }
        if (target->container == PIN_CONTAINER_SINGLE &&
            (target->type == PIN_F32 || target->type == PIN_F64)) {
            ed_binding_choice choice = {
                .binding = { .kind = PIN_BINDING_DELTA_TIME },
                .group = "Context",
            };
            snprintf(choice.label, sizeof choice.label, "Delta Time");
            apush(*out, choice);
        }
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
            const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
            const type_desc *type = component ? component->type : NULL;
            if (!component || !component->key || !component->name || !type) {
                continue;
            }
            char component_label[128];
            editor_component_label(component->name, component_label, sizeof component_label);
            for (int field = 0; field < type->field_count; field++) {
                const type_field *member = &type->fields[field];
                if (!member->name || member->type != PIN_STRUCT || !member->type_name) {
                    continue;
                }
                ed_add_nested_component_field_choices(
                    component,
                    component_label,
                    registry_type_by_name(member->type_name),
                    member->name,
                    1,
                    target,
                    out
                );
            }
        }
    }
    ed_source_choices(e, target, out, matches_out);
}

const char *ed_binding_label(const editor *e, const pin_binding *binding, char *buf, size_t cap)
{
    if (!binding) {
        return "Literal";
    }
    if (binding->kind == PIN_BINDING_LOCAL) {
        const graph_local *local = graph_local_by_id(e->active_graph, binding->local_id);
        return local ? local->name : "Missing local";
    }
    if (binding->kind == PIN_BINDING_DELTA_TIME) {
        return "Delta Time";
    }
    if (binding->kind == PIN_BINDING_COMPONENT) {
        const ecs_component_desc *component = ed_component_by_key(binding->component_key);
        char label[128];
        editor_component_label(component ? component->name : binding->component_key, label, sizeof label);
        snprintf(buf, cap, "Self.%s", label);
        return buf;
    }
    if (binding->kind == PIN_BINDING_COMPONENT_FIELD) {
        const ecs_component_desc *component = ed_component_by_key(binding->component_key);
        snprintf(
            buf,
            cap,
            "Self.%s.%s",
            component && component->name
                ? component->name
                : (binding->component_key ? binding->component_key : "?"),
            binding->field_name ? binding->field_name : "?"
        );
        return buf;
    }
    if (binding->kind == PIN_BINDING_VARIABLE) {
        if (binding->source_asset && e->asset->name &&
            strcmp(binding->source_asset, e->asset->name) != 0) {
            return "Missing variable source";
        }
        const behavior_variable
            *variable = behavior_asset_find_variable_by_id(e->asset, binding->variable_id);
        if (!variable) {
            return "Missing variable";
        }
        snprintf(buf, cap, "Variable.%s", variable->name ? variable->name : "?");
        return buf;
    }
    return "Invalid source";
}

static const char *ed_binding_chip_label(
    const editor *e,
    const pin_binding *binding,
    const char *full_label,
    char *buf,
    size_t cap
)
{
    if (!binding || !e || !e->asset) {
        return full_label;
    }
    if (binding->kind == PIN_BINDING_COMPONENT_FIELD) {
        const ecs_component_desc *component = ed_component_by_key(binding->component_key);
        snprintf(
            buf,
            cap,
            "%s.%s",
            component && component->name
                ? component->name
                : (binding->component_key ? binding->component_key : "?"),
            binding->field_name ? binding->field_name : "?"
        );
        return buf;
    }
    if (binding->kind != PIN_BINDING_VARIABLE) {
        return full_label;
    }
    if (binding->source_asset && e->asset->name &&
        strcmp(binding->source_asset, e->asset->name) != 0) {
        return full_label;
    }
    const behavior_variable
        *variable = behavior_asset_find_variable_by_id(e->asset, binding->variable_id);
    if (!variable || !variable->name || !variable->name[0]) {
        return full_label;
    }
    snprintf(buf, cap, "%s", variable->name);
    return buf;
}

static const char *ed_binding_fit_chip_label(const char *label, float width, char *buf, size_t cap)
{
    float available = width - 2.0f * ImGui_GetStyle()->FramePadding.x;
    if (ImGui_CalcTextSize(label).x <= available || cap < 4) {
        return label;
    }
    size_t length = strlen(label);
    while (length > 0) {
        size_t copy = length;
        if (copy + 4 > cap) {
            copy = cap - 4;
        }
        memcpy(buf, label, copy);
        memcpy(buf + copy, "...", 4);
        if (ImGui_CalcTextSize(buf).x <= available) {
            return buf;
        }
        length--;
    }
    snprintf(buf, cap, "...");
    return buf;
}

void ed_open_binding_popup(editor *e, node *n, int index)
{
    e->binding_node = n;
    e->binding_node_id = n->id;
    e->binding_pin = index;
    e->binding_axis = -1;
    e->binding_search[0] = '\0';
    e->binding_selected = 0;
    e->binding_focus_search = true;
    ImGui_OpenPopup("pin_binding_popup", 0);
}

void ed_open_vector_binding_popup(editor *e, node *n, int index, int axis)
{
    ed_open_binding_popup(e, n, index);
    e->binding_axis = axis;
}

node *ed_vector_source(const editor *e, const node *n, int index)
{
    node *source = NULL;
    int pin = -1;
    return ed_inline_source_for(e, n, index, &source, &pin) &&
                   graph_node_is_vector_constructor(source)
             ? source
             : NULL;
}

static node *ed_make_inline_vector(editor *e, node *target, int index)
{
    node *source = ed_vector_source(e, target, index);
    if (source) {
        return source;
    }
    const pin_spec *pin = &target->input_pins[index];
    const node_desc *desc = graph_vector_constructor(pin);
    assert(desc);
    source = graph_add_node(e->active_graph, desc);
    const float *value = pin->default_value.ptr;
    for (int axis = 0; axis < source->input_count; axis++) {
        pin_value scalar = { .type = PIN_F32,
                             .type_name = "float",
                             .f = value ? value[axis] : 0 };
        pin_coerce(&scalar, &source->input_pins[axis]);
        int result = graph_set_pin_default(e->active_graph, source, false, axis, &scalar);
        assert(result == 0);
    }
    int result = graph_connect_data(e->active_graph, source, 0, target, index);
    assert(result == 0);
    (void)result;
    result = graph_set_node_inline(e->active_graph, source, true);
    assert(result == 0);
    source->x = target->x - 190;
    source->y = target->y;
    return source;
}

static node *ed_binding_target(editor *e)
{
    if (!e || !e->active_graph || !e->binding_node_id) {
        return NULL;
    }
    node *target = graph_node_by_id(e->active_graph, e->binding_node_id);
    e->binding_node = target;
    return target;
}

static const pin_binding *ed_current_binding(const editor *e)
{
    editor *mutable_editor = (editor *)e;
    node *target = ed_binding_target(mutable_editor);
    return target && e->binding_pin >= 0 && e->binding_pin < target->input_count
             ? target->input_pins[e->binding_pin].binding
             : NULL;
}

bool ed_inline_source_for(const editor *e, const node *target, int index, node **source, int *source_pin)
{
    node *found_source = NULL;
    int found_pin = -1;
    return e && e->active_graph && target &&
           graph_inline_source(
               e->active_graph,
               target,
               index,
               source ? source : &found_source,
               source_pin ? source_pin : &found_pin
           );
}

static void ed_forget_removed_node(editor *e, node *removed, uint64_t id)
{
    ed_remove_selected(e, removed);
    if (e->reveal_target == removed) {
        e->reveal_target = NULL;
    }
    if (e->drag_toggle_node == removed) {
        e->drag_toggle_node = NULL;
        e->drag_toggle_selected = 0;
    }
    if (e->edit_text_node == removed) {
        e->edit_text_node = NULL;
    }
    if (e->binding_node == removed || e->binding_node_id == id) {
        e->binding_node = NULL;
        e->binding_node_id = 0;
        e->binding_pin = -1;
    }
    if (e->context_node == removed) {
        e->context_node = NULL;
    }
    if (e->hovered_node == removed) {
        e->hovered_node = NULL;
    }
    if (e->front_node == removed) {
        e->front_node = NULL;
    }
    if (e->fields_node == id) {
        e->fields_node = 0;
        e->fields_pin = 0;
    }
    if (e->open_call_node == id) {
        e->open_call_node = 0;
    }
    if (e->math_node_id == id) {
        e->math_node_id = 0;
    }
    if (e->link_pin.n == removed) {
        e->link_pin = (ed_pin_view){ 0 };
    }
    if (e->spawn_link_pin.n == removed) {
        e->spawn_link_pin = (ed_pin_view){ 0 };
    }
    for (int i = acount(e->drag_nodes) - 1; i >= 0; i--) {
        if (e->drag_nodes[i].n == removed) {
            adel(e->drag_nodes, i);
        }
    }
    for (int i = acount(e->selected_links) - 1; i >= 0; i--) {
        ed_link_ref link = e->selected_links[i];
        if (link.from_id == id || link.to_id == id) {
            adel(e->selected_links, i);
        }
    }
    if (e->has_context_link &&
        (e->context_link.from_id == id || e->context_link.to_id == id)) {
        e->has_context_link = 0;
    }
    if (e->has_hovered_link &&
        (e->hovered_link.from_id == id || e->hovered_link.to_id == id)) {
        e->has_hovered_link = 0;
    }
}

bool ed_simplify_component_getter(
    editor *e,
    node *source,
    int source_pin,
    node *target,
    int target_pin,
    const node_desc *conversion
)
{
    if (!ed_binding_scope_available(e) || !e->active_graph || !source || !target) {
        return false;
    }
    node *connected = NULL;
    int connected_pin = -1;
    if (!graph_input_source(e->active_graph, target, target_pin, &connected, &connected_pin) ||
        connected != source || connected_pin != source_pin ||
        graph_input_conversion(e->active_graph, target, target_pin) != conversion) {
        return false;
    }
    uint64_t id = source->id;
    if (!graph_fold_component_getter(e->active_graph, source)) {
        return false;
    }
    ed_forget_removed_node(e, source, id);
    return true;
}

int ed_optimize_graph(editor *e)
{
    if (!e->active_graph || e->active_graph->math_expression ||
        e->active_graph->flow_pause_count) {
        return 0;
    }
    char *before = ed_snapshot(e);
    if (!before) {
        return -1;
    }
    typedef struct
    {
        node *pointer;
        uint64_t id;
    } original_node;
    original_node *original = NULL;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        apush(original, ((original_node){ n, n->id }));
    }
    graph_optimize_report report = { 0 };
    int result = graph_optimize(e->active_graph, NULL, &report);
    snprintf(
        e->optimize_summary,
        sizeof e->optimize_summary,
        "%d inputs folded, %d nodes removed, %d locals created. %d candidates "
        "skipped.",
        report.folded_inputs,
        report.removed_nodes,
        report.created_aliases,
        acount(report.skipped)
    );
    memset(e->optimize_skips, 0, sizeof e->optimize_skips);
    for (int i = 0; i < acount(report.skipped); i++) {
        e->optimize_skips[report.skipped[i].reason]++;
    }
    if (result > 0) {
        for (int i = 0; i < acount(original); i++) {
            if (!graph_node_by_id(e->active_graph, original[i].id)) {
                ed_forget_removed_node(e, original[i].pointer, original[i].id);
            }
        }
        asetlen(e->selected_links, 0);
        e->has_context_link = e->has_hovered_link = 0;
        if (ImGui_GetCurrentContext() && e->canvas_size.x > 0) {
            ed_arrange_in_transaction(e);
        } else {
            graph_layout(e->active_graph);
        }
        ed_push_string(&e->undo, before);
        ed_free_string_stack(&e->redo);
        e->edit_pending = 1;
    } else {
        free(before);
    }
    graph_optimize_report_free(&report);
    afree(original);
    return result;
}

static int ed_remove_inline_source(editor *e, node *target, int index)
{
    node *source = NULL;
    int source_pin = -1;
    if (!ed_inline_source_for(e, target, index, &source, &source_pin)) {
        return 0;
    }
    bool owned = graph_node_is_inline(e->active_graph, source);
    if (graph_remove_edge(e->active_graph, source, source_pin, target, index) != 0) {
        return -1;
    }
    if (owned && graph_remove_node(e->active_graph, source) != 0) {
        return -1;
    }
    return 0;
}

static const char *ed_inline_source_label(const editor *e, const node *target, int index, char *buf, size_t cap)
{
    node *source = NULL;
    int source_pin = -1;
    if (!ed_inline_source_for(e, target, index, &source, &source_pin)) {
        return "Literal";
    }
    char title[192];
    char pin[128];
    ed_node_title(e, source, title, sizeof title);
    ed_pin_label(source, &source->output_pins[source_pin], pin, sizeof pin);
    if (source->output_count == 1) {
        snprintf(buf, cap, "%s", title);
    } else {
        snprintf(buf, cap, "%s.%s", title, pin);
    }
    return buf;
}

static int ed_choose_source(editor *e, node *target, int target_pin, const ed_binding_choice *choice)
{
    node *old_source = NULL;
    int old_pin = -1;
    bool had_inline = ed_inline_source_for(e, target, target_pin, &old_source, &old_pin);
    node *source = ed_spawn_match(e, &choice->match);
    if (!source || choice->source_pin < 0 || choice->source_pin >= source->output_count) {
        if (source) {
            graph_remove_node(e->active_graph, source);
        }
        return -1;
    }
    bool shader = e->asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                  e->asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
                  e->asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    if (shader && !shader_graph_init_node(e->active_graph, source)) {
        graph_remove_node(e->active_graph, source);
        return -1;
    }
    if (choice->source_pin >= source->output_count) {
        if (ed_expose_spawned_output_fields(e, source) != 0 ||
            choice->source_pin >= source->output_count) {
            graph_remove_node(e->active_graph, source);
            return -1;
        }
    }
    const pin_spec *output = &source->output_pins[choice->source_pin];
    const pin_spec *input = &target->input_pins[target_pin];
    const node_desc *conversion = pin_types_compatible(output, input)
                                    ? NULL
                                    : conversion_for_pins(output, input);
    if (!ed_connection_specs_valid(output, input, PIN_DATA) ||
        graph_connect_data_conversion(e->active_graph, source, choice->source_pin, target, target_pin, conversion) !=
            0) {
        graph_remove_node(e->active_graph, source);
        return -1;
    }
    int cleared = graph_set_pin_binding(e->active_graph, target, 0, target_pin, NULL);
    int folded = graph_set_node_inline(e->active_graph, source, true);
    assert(cleared == 0 && folded == 0);
    (void)cleared;
    (void)folded;
    source->x = target->x - 190.0f;
    source->y = target->y;
    if (had_inline && old_source != source) {
        graph_remove_node(e->active_graph, old_source);
    }
    return 0;
}

static int ed_choose_binding(editor *e, node *target, int index, const ed_binding_choice *choice)
{
    char *before = ed_snapshot(e);
    node *owner = graph_inline_owner(e->active_graph, target);
    node *created = NULL;
    if (e->binding_axis >= 0) {
        bool existed = ed_vector_source(e, target, index) != NULL;
        target = ed_make_inline_vector(e, target, index);
        created = existed ? NULL : target;
        index = e->binding_axis;
    }
    pin_binding binding = choice->binding;
    if (binding.kind == PIN_BINDING_COMPONENT_FIELD) {
        binding.field_name = choice->component_field_path;
    }
    int result = choice->generic_source
                   ? ed_choose_source(e, target, index, choice)
                   : graph_set_pin_binding(e->active_graph, target, false, index, &binding);
    if (!choice->generic_source && result == 0) {
        result = ed_remove_inline_source(e, target, index);
    }
    if (result != 0) {
        if (created) {
            graph_remove_node(e->active_graph, created);
        }
        free(before);
        return result;
    }
    ed_push_string(&e->undo, before);
    ed_free_string_stack(&e->redo);
    e->edit_pending = 1;
    ed_select_node(e, owner, false);
    return 0;
}

int ed_expand_inline_source(editor *e, node *target, int index)
{
    node *source = NULL;
    int source_pin = -1;
    if (!ed_inline_source_for(e, target, index, &source, &source_pin)) {
        return -1;
    }
    ed_snapshot_before(e);
    if (graph_set_node_inline(e->active_graph, source, false) != 0) {
        return -1;
    }
    source->x = target->x - ed_node_dimensions(e, e->active_graph, source).x - 60.0f;
    source->y = target->y;
    ed_select_node(e, source, false);
    editor_reveal_node(e, source);
    return 0;
}

int ed_clear_input_source(editor *e, node *target, int index)
{
    node *vector = ed_vector_source(e, target, index);
    if (vector) {
        float components[4] = { 0 };
        for (int axis = 0; axis < vector->input_count; axis++) {
            pin_value value = vector->input_pins[axis].default_value;
            components[axis] = value.type == PIN_F64 ? (float)value.d : value.f;
        }
        ed_snapshot_before(e);
        pin_value value = target->input_pins[index].default_value;
        value.ptr = components;
        graph_set_pin_default(e->active_graph, target, false, index, &value);
    } else {
        ed_snapshot_before(e);
    }
    if (ed_remove_inline_source(e, target, index) != 0 ||
        graph_set_pin_binding(e->active_graph, target, 0, index, NULL) != 0) {
        return -1;
    }
    return 0;
}

static void ed_close_binding_popup(editor *e)
{
    e->binding_node = NULL;
    e->binding_node_id = 0;
    e->binding_pin = -1;
    e->binding_axis = -1;
    e->binding_search[0] = '\0';
    e->binding_selected = 0;
    e->binding_focus_search = false;
    ImGui_CloseCurrentPopup();
}

static const type_field *ed_binding_component_field(const pin_binding *binding)
{
    const ecs_component_desc *component = binding
                                            ? ed_component_by_key(binding->component_key)
                                            : NULL;
    if (!component || !component->type || !binding->field_name) {
        return NULL;
    }
    return pin_type_field_path(component->type, binding->field_name);
}

int ed_convert_binding(editor *e)
{
    const pin_binding *binding = ed_current_binding(e);
    node *target = ed_binding_target(e);
    if (!binding || !target) {
        return -1;
    }
    char *before = ed_snapshot(e);
    node *getter = NULL;
    node *break_node = NULL;
    int break_pin = -1;
    if (binding->kind == PIN_BINDING_VARIABLE) {
        if ((binding->source_asset && e->asset->name &&
             strcmp(binding->source_asset, e->asset->name) != 0)) {
            free(before);
            return -1;
        }
        behavior_variable
            *variable = behavior_asset_find_variable_by_id(e->asset, binding->variable_id);
        if (!variable) {
            free(before);
            return -1;
        }
        getter = graph_add_node(e->active_graph, &NODE_GetVariable);
        if (getter) {
            if (graph_set_node_reference(
                    e->active_graph,
                    getter,
                    variable->id,
                    variable->name
                ) != 0 ||
                graph_set_pin_type(
                    e->active_graph,
                    getter,
                    1,
                    0,
                    variable->type,
                    variable->type_name
                ) != 0 ||
                graph_set_pin_container(e->active_graph, getter, 1, 0, variable->container) !=
                    0) {
                graph_remove_node(e->active_graph, getter);
                getter = NULL;
            }
        }
    } else if (binding->kind == PIN_BINDING_COMPONENT) {
        const ecs_component_desc *component = ed_component_by_key(binding->component_key);
        char name[128];
        if (component) {
            snprintf(name, sizeof name, "Get_%s", component->name);
            const node_desc *desc = registry_node_by_name(name);
            if (desc) {
                getter = graph_add_node(e->active_graph, desc);
            }
        }
    } else if (binding->kind == PIN_BINDING_COMPONENT_FIELD) {
        const ecs_component_desc *component = ed_component_by_key(binding->component_key);
        const type_field *field = ed_binding_component_field(binding);
        if (!component || !field || strchr(binding->field_name, '.')) {
            free(before);
            return -1;
        }
        char get_name[128];
        char break_name[128];
        snprintf(get_name, sizeof get_name, "Get_%s", component->name);
        snprintf(break_name, sizeof break_name, "Break_%s", component->name);
        const node_desc *get_desc = registry_node_by_name(get_name);
        const node_desc *break_desc = registry_node_by_name(break_name);
        if (!get_desc || !break_desc) {
            free(before);
            return -1;
        }
        getter = graph_add_node(e->active_graph, get_desc);
        break_node = graph_add_node(e->active_graph, break_desc);
        if (getter && break_node &&
            graph_connect_data(e->active_graph, getter, 0, break_node, 0) == 0) {
            for (int i = 0; i < break_node->output_count; i++) {
                if (break_node->output_pins[i].name &&
                    !strcmp(break_node->output_pins[i].name, field->name)) {
                    break_pin = i;
                    break;
                }
            }
        }
    } else if (binding->kind == PIN_BINDING_DELTA_TIME) {
        getter = graph_add_node(e->active_graph, &NODE_DeltaTime);
    }
    if (!before || !getter ||
        (binding->kind == PIN_BINDING_COMPONENT_FIELD &&
         (!break_node || break_pin < 0 ||
          graph_connect_data(e->active_graph, break_node, break_pin, target, e->binding_pin) !=
              0)) ||
        (binding->kind != PIN_BINDING_COMPONENT_FIELD &&
         graph_connect_data(e->active_graph, getter, 0, target, e->binding_pin) != 0) ||
        graph_set_pin_binding(e->active_graph, target, 0, e->binding_pin, NULL) != 0) {
        if (break_node) {
            graph_remove_node(e->active_graph, break_node);
        }
        if (getter) {
            graph_remove_node(e->active_graph, getter);
        }
        free(before);
        return -1;
    }
    getter->x = target->x - (break_node ? 260.0f : 190.0f);
    getter->y = target->y;
    if (break_node) {
        break_node->x = target->x - 110.0f;
        break_node->y = target->y;
    }
    e->edit_pending = 1;
    ed_push_string(&e->undo, before);
    ed_free_string_stack(&e->redo);
    ed_select_node(e, getter, 0);
    return 0;
}

void ed_jump_binding_source(editor *e)
{
    const pin_binding *binding = ed_current_binding(e);
    if (!binding || binding->kind != PIN_BINDING_VARIABLE ||
        (binding->source_asset && e->asset->name &&
         strcmp(binding->source_asset, e->asset->name) != 0)) {
        return;
    }
    behavior_variable
        *variable = behavior_asset_find_variable_by_id(e->asset, binding->variable_id);
    if (variable) {
        ed_select_variable(e, variable);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
        editor_ui_request_focus(EDITOR_UI_FOCUS_INSPECTOR);
    }
}

bool ed_binding_can_convert(const editor *e)
{
    const pin_binding *binding = ed_current_binding(e);
    if (!binding) {
        return false;
    }
    if (binding->kind == PIN_BINDING_VARIABLE) {
        return (!binding->source_asset || !e->asset->name ||
                !strcmp(binding->source_asset, e->asset->name)) &&
               behavior_asset_find_variable_by_id(e->asset, binding->variable_id) != NULL;
    }
    if (binding->kind == PIN_BINDING_COMPONENT) {
        const ecs_component_desc *component = ed_component_by_key(binding->component_key);
        if (!component) {
            return false;
        }
        char name[128];
        snprintf(name, sizeof name, "Get_%s", component->name);
        return registry_node_by_name(name) != NULL;
    }
    if (binding->kind == PIN_BINDING_COMPONENT_FIELD) {
        const type_field *field = ed_binding_component_field(binding);
        const ecs_component_desc *component = ed_component_by_key(binding->component_key);
        if (!field || !component || !component->name ||
            strchr(binding->field_name, '.')) {
            return false;
        }
        char get_name[128];
        char break_name[128];
        snprintf(get_name, sizeof get_name, "Get_%s", component->name);
        snprintf(break_name, sizeof break_name, "Break_%s", component->name);
        return registry_node_by_name(get_name) && registry_node_by_name(break_name);
    }
    return binding->kind == PIN_BINDING_DELTA_TIME;
}

bool ed_binding_can_jump(const editor *e)
{
    const pin_binding *binding = ed_current_binding(e);
    return binding && binding->kind == PIN_BINDING_VARIABLE &&
           ed_binding_can_convert(e);
}

void ed_draw_binding_popup(editor *e)
{
    if (!ed_source_scope_available(e) || !e->active_graph ||
        e->active_graph->math_expression || !e->binding_node_id ||
        e->binding_pin < 0) {
        return;
    }
    node *n = ed_binding_target(e);
    if (!n || e->binding_pin >= n->input_count) {
        ed_close_binding_popup(e);
        return;
    }
    if (!ImGui_BeginPopup("pin_binding_popup", 0)) {
        return;
    }
    pin_spec component = { .name = "Component", .type = PIN_F32, .type_name = "float" };
    const char *axes[] = { "X", "Y", "Z", "W" };
    if (e->binding_axis >= 0) {
        component.name = axes[e->binding_axis];
    }
    pin_spec *target = e->binding_axis >= 0 ? &component
                                            : &n->input_pins[e->binding_pin];
    const graph_local *local = graph_input_local(e->active_graph, n, e->binding_pin);
    if (local) {
        ImGui_Text("%s", local->name);
        if (ImGui_ButtonEx("Go to source", (ImVec2){ 0, 0 })) {
            node *source = graph_node_by_id(e->active_graph, local->source_id);
            ed_clear_selection(e);
            ed_add_selected(e, source);
            e->reveal_target = source;
            ed_close_binding_popup(e);
        }
        editor_ui_track_item("graph.local.jump");
        ImGui_SameLine();
        if (ImGui_ButtonEx("Expand local", (ImVec2){ 0, 0 })) {
            ed_snapshot_before(e);
            int expand_local_result = graph_expand_local(e->active_graph, local->id);
            assert(expand_local_result == 0);
            (void)expand_local_result;
            ed_close_binding_popup(e);
        }
        editor_ui_track_item("graph.local.expand");
        ImGui_EndPopup();
        return;
    }
    ed_binding_choice *choices = NULL;
    ed_search_match *matches = NULL;
    ed_binding_choices(e, target, &choices, &matches);
    ImGui_Text("Bind %s", target->name ? target->name : "Input");
    ImGui_SetNextItemWidth(300.0f);
    if (e->binding_focus_search) {
        ImGui_SetKeyboardFocusHere();
        e->binding_focus_search = false;
    }
    if (ImGui_InputText("##binding.search", e->binding_search, sizeof e->binding_search, 0)) {
        e->binding_selected = 0;
    }
    editor_ui_track_item("graph.binding.search");
    int visible = 0;
    for (int i = 0; i < acount(choices); i++) {
        if (!ed_binding_choice_match(&choices[i], e->binding_search)) {
            continue;
        }
        visible++;
    }
    if (visible) {
        if (ImGui_IsKeyPressed(ImGuiKey_DownArrow)) {
            e->binding_selected = (e->binding_selected + 1) % visible;
        }
        if (ImGui_IsKeyPressed(ImGuiKey_UpArrow)) {
            e->binding_selected = (e->binding_selected + visible - 1) % visible;
        }
    }
    bool choose = ImGui_IsKeyPressed(ImGuiKey_Enter);
    const char *group = NULL;
    int row = 0;
    bool results_visible = ImGui_BeginChild("graph.binding.results", (ImVec2){ 0.0f, 260.0f }, 0, 0);
    for (int i = 0; results_visible && i < acount(choices); i++) {
        ed_binding_choice *choice = &choices[i];
        if (!ed_binding_choice_match(choice, e->binding_search)) {
            continue;
        }
        if (!group || strcmp(group, choice->group) != 0) {
            ImGui_SeparatorText(choice->group);
            group = choice->group;
        }
        char choice_id[112];
        snprintf(
            choice_id,
            sizeof choice_id,
            "%s###graph.binding.choice.%llu.%d.%d",
            choice->label,
            (unsigned long long)n->id,
            e->binding_pin,
            i
        );
        bool clicked = ImGui_SelectableEx(choice_id, row == e->binding_selected, 0, (ImVec2){ 0 });
        char track[112];
        snprintf(
            track,
            sizeof track,
            "graph.binding.choice.%llu.%d.%d",
            (unsigned long long)n->id,
            e->binding_pin,
            i
        );
        editor_ui_track_item(track);
        if (choice->generic_source) {
            snprintf(
                track,
                sizeof track,
                "graph.binding.source.%s.%d",
                choice->match.desc->name,
                choice->source_pin
            );
            editor_ui_track_item(track);
        }
        if (clicked || (choose && row == e->binding_selected)) {
            int result = ed_choose_binding(e, n, e->binding_pin, choice);
            if (result == 0) {
                ed_close_binding_popup(e);
            }
            choose = false;
        }
        row++;
    }
    ImGui_EndChild();
    if (!visible) {
        ImGui_TextDisabled("No compatible sources");
    }
    if (e->binding_axis >= 0) {
        afree(choices);
        afree(matches);
        ImGui_EndPopup();
        return;
    }
    node *inline_source = NULL;
    int inline_pin = -1;
    bool has_inline_source = ed_inline_source_for(e, n, e->binding_pin, &inline_source, &inline_pin);
    bool can_jump = ed_binding_can_jump(e);
    ImGui_BeginDisabled(!can_jump);
    if (ImGui_ButtonEx("Jump to source", (ImVec2){ 0, 0 })) {
        ed_jump_binding_source(e);
        ed_close_binding_popup(e);
    }
    ImGui_EndDisabled();
    editor_ui_track_item("graph.binding.jump");
    ImGui_SameLine();
    bool can_convert = ed_binding_can_convert(e);
    ImGui_BeginDisabled(!can_convert && !has_inline_source);
    if (ImGui_ButtonEx(has_inline_source ? "Expand source" : "Convert to getter", (ImVec2){ 0, 0 })) {
        int result = has_inline_source ? ed_expand_inline_source(e, n, e->binding_pin)
                                       : ed_convert_binding(e);
        if (result == 0) {
            ed_close_binding_popup(e);
        }
    }
    ImGui_EndDisabled();
    editor_ui_track_item("graph.binding.convert");
    ImGui_SameLine();
    bool clear = ImGui_ButtonEx(
        has_inline_source ? "Use literal" : "Clear binding",
        (ImVec2){ 0, 0 }
    );
    editor_ui_track_item("graph.binding.clear");
    if (clear) {
        if (ed_clear_input_source(e, n, e->binding_pin) == 0) {
            ed_close_binding_popup(e);
        }
    }
    afree(choices);
    afree(matches);
    ImGui_EndPopup();
}

int ed_draw_bound_input(editor *e, node *n, int index, ed_value_layout layout)
{
    ImVec2 pos = layout.pos;
    float width = layout.width;
    const char *axes[] = { "X", "Y", "Z", "W" };
    const char *axis = layout.label                        ? layout.label
                     : graph_node_is_vector_constructor(n) ? axes[index]
                                                           : NULL;
    node *literal = NULL;
    int literal_pin = -1;
    if (ed_inline_source_for(e, n, index, &literal, &literal_pin) &&
        (literal->desc->display_class == NODE_DISPLAY_LITERAL ||
         ed_pin_editable(e->active_graph, literal, ED_PIN_OUTPUT, literal_pin)) &&
        !ed_vector_width(&literal->output_pins[literal_pin])) {
        int kind = literal->input_count == 1 ? ED_PIN_INPUT : ED_PIN_OUTPUT;
        int pin = kind == ED_PIN_INPUT ? 0 : literal_pin;
        float button = ImGui_GetFrameHeight() * e->zoom;
        int changed = ed_draw_value_editor(
            e,
            literal,
            kind,
            pin,
            (ed_value_layout){ pos, width - button - 4 * e->zoom, axis }
        );
        ed_draw_binding_trigger(e, n, index, (ImVec2){ pos.x + width - button, pos.y }, button);
        return changed;
    }
    const pin_binding *binding = n->input_pins[index].binding;
    char source[256];
    char chip[128];
    char fitted[128];
    const char *full_label = binding
                               ? ed_binding_label(e, binding, source, sizeof source)
                               : ed_inline_source_label(e, n, index, source, sizeof source);
    const char *short_label = ed_binding_chip_label(e, binding, full_label, chip, sizeof chip);
    char component_label[160];
    if (axis) {
        snprintf(component_label, sizeof component_label, "%s: %s", axis, short_label);
        short_label = component_label;
    }
    ImGui_SetCursorScreenPos(pos);
    ImGui_PushFontFloat(NULL, ImGui_GetFontSize() * e->zoom);
    ImGui_PushStyleVarImVec2(
        ImGuiStyleVar_FramePadding,
        ed_scale(ImGui_GetStyle()->FramePadding, e->zoom)
    );
    const char *label = ed_binding_fit_chip_label(short_label, width, fitted, sizeof fitted);
    ImGui_SetNextItemWidth(width);
    char button[320];
    snprintf(
        button,
        sizeof button,
        "%s###graph.input.binding.%llu.%d",
        label,
        (unsigned long long)n->id,
        index
    );
    bool clicked = ImGui_ButtonEx(button, (ImVec2){ width, ImGui_GetFrameHeight() });
    char id[96];
    snprintf(id, sizeof id, "graph.input.binding.%llu.%d", (unsigned long long)n->id, index);
    editor_ui_track_item(id);
    if (clicked) {
        ed_open_binding_popup(e, n, index);
    }
    if (ImGui_IsItemHovered(0)) {
        ImGui_SetItemTooltip("Source: %s", full_label);
    }
    e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    ImGui_PopStyleVar();
    ImGui_PopFont();
    return 0;
}

bool ed_binding_trigger_available(editor *e, node *n, int index)
{
    if (!ed_source_scope_available(e) || !e->active_graph || !n || index < 0 ||
        index >= n->input_count || n->input_pins[index].kind != PIN_DATA ||
        n->input_pins[index].binding ||
        (graph_node_is_inline(e->active_graph, n) &&
         !graph_node_is_vector_constructor(n))) {
        return false;
    }
    node *source = NULL;
    int source_pin = -1;
    return !graph_input_source(e->active_graph, n, index, &source, &source_pin);
}

bool ed_draw_binding_trigger(editor *e, node *n, int index, ImVec2 pos, float size)
{
    char id[112];
    snprintf(
        id,
        sizeof id,
        "graph.input.binding.open.%llu.%d",
        (unsigned long long)n->id,
        index
    );
    ImGui_SetCursorScreenPos(pos);
    bool clicked = editor_ui_icon_button_sized(id, EDITOR_ICON_FA_CODE_FORK, true, size);
    editor_ui_track_item(id);
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_None)) {
        ImGui_SetTooltip("Choose source");
    }
    e->controls_hovered |= ImGui_IsItemHovered(ImGuiHoveredFlags_None) ||
                           ImGui_IsItemActive();
    if (clicked) {
        ed_open_binding_popup(e, n, index);
    }
    return clicked;
}
