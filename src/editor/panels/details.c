#include "editor/panels/field_details.h"
#include "editor/panels/shader_details.h"
#include "editor/ui/choices.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/events.h"
#include "graph/model/local.h"
#include "support/xalloc.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ed_unique_variable_name(const editor *e)
{
    static char buf[64];
    for (int i = 1; i < 10000; i++) {
        snprintf(buf, sizeof buf, "NewVariable%d", i);
        if (!behavior_asset_find_variable_by_name(e->asset, buf)) {
            return buf;
        }
    }
    return "NewVariable";
}

ImVec2 ed_view_center_world(const editor *e)
{
    ImVec2 center = ed_add(e->canvas_min, ed_scale(e->canvas_size, 0.5f));
    return ed_screen_to_world(e, center);
}

void ed_show_error(editor *e, const char *message)
{
    ed_clear_connect_error(e);
    snprintf(e->connect_error, sizeof e->connect_error, "%s", message ? message : "error");
    e->connect_error_pos = ImGui_GetIO()->MousePos;
    e->connect_error_until = e->monotonic_time + ED_TOAST_DURATION;
}

static int ed_draw_variable_default(editor *e, behavior_variable *v)
{
    pin_value value = v->default_value;
    int changed = 0;
    char id[32];
    snprintf(id, sizeof id, "Value##var");
    if (value.type != PIN_STR) {
        ImGui_SetNextItemWidth(160.0f);
    }
    switch (value.type) {
        case PIN_BOOL: changed = ImGui_Checkbox(id, &value.b); break;
        case PIN_I8:
            changed = ImGui_InputScalar(id, ImGuiDataType_S8, &value.i8);
            break;
        case PIN_U8:
            changed = ImGui_InputScalar(id, ImGuiDataType_U8, &value.u8);
            break;
        case PIN_I16:
            changed = ImGui_InputScalar(id, ImGuiDataType_S16, &value.i16);
            break;
        case PIN_U16:
            changed = ImGui_InputScalar(id, ImGuiDataType_U16, &value.u16);
            break;
        case PIN_I32: changed = ImGui_InputInt(id, &value.i); break;
        case PIN_U32:
            changed = ImGui_InputScalar(id, ImGuiDataType_U32, &value.u);
            break;
        case PIN_I64:
            changed = ImGui_InputScalar(id, ImGuiDataType_S64, &value.i64);
            break;
        case PIN_U64:
            changed = ImGui_InputScalar(id, ImGuiDataType_U64, &value.u64);
            break;
        case PIN_F32:
            changed = ImGui_InputFloatEx(id, &value.f, 0.1f, 0.0f, "%.6g", 0);
            break;
        case PIN_F64:
            changed = ImGui_InputDoubleEx(id, &value.d, 0.1, 0.0, "%.12g", 0);
            break;
        case PIN_ENUM: {
            const editor_choices *choices = editor_choices_for_enum(value.type_name);
            changed = choices
                        ? editor_choices_draw("variable.enum", &value.e, choices)
                        : ImGui_InputInt(id, &value.e);
            break;
        }
        case PIN_STR: {
            snprintf(e->edit_text, sizeof e->edit_text, "%s", value.str ? value.str : "");
            changed = ImGui_InputTextMultilineEx(
                id,
                e->edit_text,
                sizeof e->edit_text,
                (ImVec2){ 0.0f, 0.0f },
                ImGuiInputTextFlags_WordWrap,
                NULL,
                NULL
            );
            value.str = e->edit_text;
            break;
        }
        case PIN_NAME: {
            snprintf(e->edit_text, sizeof e->edit_text, "%s", value.str ? value.str : "");
            changed = ImGui_InputText(id, e->edit_text, sizeof e->edit_text, 0);
            value.str = e->edit_text;
            break;
        }
        case PIN_FLOW:
        case PIN_STRUCT:
        case PIN_HANDLE:
        default: ImGui_TextUnformatted("(not editable)"); break;
    }
    if (changed) {
        ed_details_begin_mutation(e);
        if (behavior_asset_set_variable_default(e->asset, v->id, &value) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, changed);
    return changed;
}

static bool event_parameter_type(pin_type *type)
{
    bool changed = false;
    const char *label = *type == PIN_HANDLE ? "Entity" : pin_type_name(*type);
    if (!ImGui_BeginCombo("Type", label, 0)) {
        return false;
    }
    for (int candidate = PIN_BOOL; candidate <= PIN_HANDLE; candidate++) {
        const char *name = candidate == PIN_HANDLE ? "Entity"
                                                   : pin_type_name(candidate);
        if (ImGui_SelectableEx(name, candidate == (int)*type, 0, (ImVec2){ 0 })) {
            *type = candidate;
            changed = true;
        }
    }
    ImGui_EndCombo();
    return changed;
}

static void ed_draw_details_variable(editor *e, behavior_variable *v)
{
    bool payload = e->asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    ImGui_Text("%s: %s", payload ? "Parameter" : "Variable", v->name ? v->name : "?");
    ImGui_Separator();
    snprintf(e->details_title, sizeof e->details_title, "%s", v->name ? v->name : "");
    if (ImGui_InputText("Name", e->details_title, sizeof e->details_title, 0)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_rename_variable(e->asset, v->id, e->details_title) == 0) {
            e->edit_changed = 1;
        } else if (e->details_title[0]) {
            ed_show_error(e, "Variable name is empty or already in use");
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    pin_type type = v->type;
    if (payload ? event_parameter_type(&type) : ed_pin_type_combo("Type", &type)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_retype_variable(
                e->asset,
                v->id,
                type,
                payload && type == PIN_HANDLE ? "ecs.entity" : pin_type_name(type)
            ) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    pin_container container = v->container;
    if (!payload && ed_pin_container_combo("Container", &container)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_set_variable_container(e->asset, v->id, container) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    snprintf(e->details_comment, sizeof e->details_comment, "%s", v->category ? v->category : "");
    if (ImGui_InputText("Category", e->details_comment, sizeof e->details_comment, 0)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_set_variable_category(e->asset, v->id, e->details_comment) ==
            0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    ed_draw_variable_default(e, v);
    ImGui_Separator();
    if (!payload && ImGui_Button("Add Get node")) {
        ed_snapshot_before(e);
        node *n = behavior_asset_add_get_node(e->asset, v->id);
        if (n) {
            ImVec2 world = ed_view_center_world(e);
            n->x = world.x - 160.0f;
            n->y = world.y;
            ed_select_node(e, n, 0);
        }
    }
    ImGui_SameLine();
    if (!payload && ImGui_Button("Add Set node")) {
        ed_snapshot_before(e);
        node *n = behavior_asset_add_set_node(e->asset, v->id);
        if (n) {
            ImVec2 world = ed_view_center_world(e);
            n->x = world.x + 160.0f;
            n->y = world.y;
            ed_select_node(e, n, 0);
        }
    }
    ImGui_Separator();
    if (ImGui_Button(payload ? "Delete parameter" : "Delete variable")) {
        ed_snapshot_before(e);
        if (behavior_asset_delete_variable(e->asset, v->id) == 0) {
            e->selected_variable = NULL;
        }
    }
}

void ed_draw_details_behavior_asset(editor *e)
{
    ImGui_Text(
        "Behavior Asset: %s",
        e->asset->name && e->asset->name[0] ? e->asset->name : "Unnamed"
    );
    ImGui_Separator();
    snprintf(
        e->details_title,
        sizeof e->details_title,
        "%s",
        e->asset->name ? e->asset->name : ""
    );
    if (ImGui_InputText("Name", e->details_title, sizeof e->details_title, 0)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_set_name(e->asset, e->details_title) == 0) {
            e->edit_changed = 1;
        }
    }
    editor_ui_track_item("settings.name");
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
}

static int ed_event_name_taken(const editor *e, const node *self, const char *name)
{
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        const node *n = e->active_graph->nodes[i];
        if (n == self || n->desc->flow_kind != NODE_FLOW_EVENT || !n->reference_name) {
            continue;
        }
        if (strcmp(n->reference_name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static const editor_event_signature *ed_event_signature(const editor *e, const char *name)
{
    for (int i = 0; i < e->event_catalog_count; i++) {
        if (name && strcmp(e->event_catalog[i].name, name) == 0) {
            return &e->event_catalog[i];
        }
    }
    return NULL;
}

static int ed_event_param_count(const node *n)
{
    int count = 0;
    for (int i = 0; i < n->output_count; i++) {
        count += n->output_pins[i].kind == PIN_DATA;
    }
    return count;
}

static bool ed_event_payload_used(const graph *g, const node *n, int pin)
{
    uint64_t pin_id = n->output_pins[pin].id;
    if (graph_output_local(g, n, pin)) {
        return true;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].from_id == n->id && g->edges[i].from_pin_id == pin_id) {
            return true;
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *target = g->nodes[i];
        for (int j = 0; j < target->input_count; j++) {
            const pin_binding *binding = graph_pin_binding(g, target, 0, j);
            if (binding && binding->kind == PIN_BINDING_EVENT_OUTPUT &&
                binding->event_node_id == n->id && binding->event_pin_id == pin_id) {
                return true;
            }
        }
    }
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].field_source == pin_id) {
            return true;
        }
    }
    return false;
}

int ed_event_unused_payload_count(const editor *e, const node *n)
{
    if (!e || !e->active_graph || !n || n->desc->flow_kind != NODE_FLOW_EVENT ||
        !n->reference_name || !graph_event_host_name(n->reference_name)) {
        return 0;
    }
    int count = 0;
    for (int i = 1; i < n->output_count; i++) {
        count += n->output_pins[i].kind == PIN_DATA &&
                 !ed_event_payload_used(e->active_graph, n, i);
    }
    return count;
}

int ed_repair_event_payloads(editor *e, node *n)
{
    if (!e || !e->active_graph || !n || graph_node_by_id(e->active_graph, n->id) != n ||
        n->desc->flow_kind != NODE_FLOW_EVENT || !n->reference_name ||
        !graph_event_host_name(n->reference_name) || e->active_graph->flow_pause_count) {
        return -1;
    }
    int unused = ed_event_unused_payload_count(e, n);
    if (!unused) {
        return 0;
    }
    ed_snapshot_before(e);
    int removed = 0;
    for (int i = n->output_count - 1; i > 0; i--) {
        if (n->output_pins[i].kind != PIN_DATA ||
            ed_event_payload_used(e->active_graph, n, i) ||
            graph_remove_pin(e->active_graph, n, 1, i)) {
            continue;
        }
        removed++;
    }
    return removed;
}

/* Appends only the declared parameters this event is still missing, so no
 * existing pin is retyped or removed and no edge is invalidated under EVT-5. */
int ed_event_add_declared_params(editor *e, node *n, const editor_event_signature *signature)
{
    int added = 0;
    for (int i = ed_event_param_count(n); i < signature->param_count; i++) {
        const editor_event_param *parameter = &signature->params[i];
        pin_spec pin = { parameter->name, parameter->type, parameter->type_name, PIN_DATA };
        added += graph_add_pin(e->active_graph, n, 1, &pin) >= 0;
    }
    return added;
}

static void ed_details_project_event(editor *e, node *n)
{
    if (n->desc->flow_kind != NODE_FLOW_EVENT && n->desc != &NODE_CallEvent &&
        strcmp(n->desc->name, "Send Event")) {
        return;
    }
    if (!ImGui_BeginCombo("Event", n->reference_name ? n->reference_name : "Choose event", 0)) {
        return;
    }
    for (int i = 0; i < acount(e->asset_catalog); i++) {
        const behavior_asset *asset = e->asset_catalog[i];
        if (asset->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            continue;
        }
        if (ImGui_Selectable(asset->name)) {
            ed_snapshot_before(e);
            if (graph_event_apply(e->active_graph, n, asset) == 0) {
                e->edit_changed = 1;
            } else {
                ed_show_error(e, "Disconnect incompatible payload fields before changing this event signature.");
            }
        }
    }
    ImGui_EndCombo();
}

static int ed_event_apply_host(editor *e, node *n, const editor_event_signature *signature)
{
    graph *g = e->active_graph;
    if (g->flow_pause_count) {
        return -1;
    }
    for (int p = 1; p < n->output_count; p++) {
        const pin_spec *pin = &n->output_pins[p];
        const editor_event_param *param = p <= signature->param_count
                                            ? &signature->params[p - 1]
                                            : NULL;
        bool compatible = param && pin->type == param->type &&
                          pin->container == PIN_CONTAINER_SINGLE &&
                          ((!pin->type_name && !param->type_name) ||
                           (pin->type_name && param->type_name &&
                            !strcmp(pin->type_name, param->type_name)));
        if (compatible) {
            continue;
        }
        for (int i = 0; i < acount(g->edges); i++) {
            if (g->edges[i].from_id == n->id && g->edges[i].from_pin_id == pin->id) {
                return -1;
            }
        }
    }
    for (int p = n->output_count - 1; p > signature->param_count; p--) {
        graph_remove_pin(g, n, true, p);
    }
    ed_event_add_declared_params(e, n, signature);
    for (int p = 1; p <= signature->param_count; p++) {
        const editor_event_param *param = &signature->params[p - 1];
        graph_set_pin_type(g, n, true, p, param->type, param->type_name);
        graph_set_pin_container(g, n, true, p, PIN_CONTAINER_SINGLE);
        graph_set_pin_name(g, n, true, p, param->name);
        n->output_pins[p].declaration_id = 0;
    }
    return graph_set_node_reference(g, n, n->reference_id, signature->name);
}

static void ed_details_event_catalog(editor *e, node *n)
{
    if (!e->event_catalog_count) {
        return;
    }
    const char *current = n->reference_name && n->reference_name[0]
                            ? n->reference_name
                            : "(unnamed event)";
    if (ImGui_BeginCombo("Host events", current, 0)) {
        for (int i = 0; i < e->event_catalog_count; i++) {
            const editor_event_signature *signature = &e->event_catalog[i];
            int taken = ed_event_name_taken(e, n, signature->name);
            int selected = n->reference_name &&
                           strcmp(n->reference_name, signature->name) == 0;
            if (ImGui_SelectableEx(
                    signature->name,
                    selected,
                    taken ? ImGuiSelectableFlags_Disabled : 0,
                    (ImVec2){ 0.0f, 0.0f }
                )) {
                ed_snapshot_before(e);
                if (ed_event_apply_host(e, n, signature) == 0) {
                    e->edit_changed = 1;
                } else {
                    ed_show_error(e, "Disconnect incompatible payload fields before changing this event signature.");
                }
            }
            if (taken) {
                ImGui_SameLine();
                ImGui_TextUnformatted("(in use)");
            }
        }
        ImGui_EndCombo();
    }
    const editor_event_signature *signature = ed_event_signature(e, n->reference_name);
    if (!signature) {
        return;
    }
    for (int i = 0; i < signature->param_count; i++) {
        ImGui_Text(
            "  %d %s (%s)",
            i + 1,
            signature->params[i].name,
            signature->params[i].type_name
        );
    }
}

static int ed_math_resize(ImGuiInputTextCallbackData *data)
{
    editor *e = data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        e->math_capacity = (size_t)data->BufSize;
        e->math_text = xrealloc(e->math_text, e->math_capacity);
        data->Buf = e->math_text;
    }
    return 0;
}

static void ed_draw_math_graph_name(editor *e, graph *g)
{
    snprintf(e->details_title, sizeof e->details_title, "%s", g->name ? g->name : "");
    if (ImGui_InputText("Name", e->details_title, sizeof e->details_title, 0)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_rename_graph(e->asset, g->graph_id, e->details_title) == 0) {
            e->edit_changed = 1;
        } else if (e->details_title[0]) {
            ed_show_error(e, "Function name is empty or already in use");
        }
    }
    editor_ui_track_item("math.title");
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
}

static void ed_draw_math_source(editor *e, node *n)
{
    const char *source = behavior_expression_source(e->asset, n);
    if (!source) {
        return;
    }
    if (e->math_node_id != n->id || !e->math_saved || strcmp(e->math_saved, source)) {
        e->math_node_id = n->id;
        free(e->math_saved);
        e->math_saved = xstrdup(source);
        e->math_capacity = strlen(source) + 256;
        e->math_text = xrealloc(e->math_text, e->math_capacity);
        snprintf(e->math_text, e->math_capacity, "%s", source);
        e->expression_error = (behavior_expression_error){ 0 };
    }
    graph *generated = behavior_asset_find_graph(e->asset, n->reference_id);
    assert(generated && generated->math_expression);
    ed_draw_math_graph_name(e, generated);
    ImGui_SeparatorText("Expression");
    ImGui_SetNextItemWidth(ImGui_GetContentRegionAvail().x - 36.0f);
    bool apply = ImGui_InputTextEx(
        "##math_source",
        e->math_text,
        e->math_capacity,
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
        ed_math_resize,
        e
    );
    editor_ui_track_item("math.expression");
    if (ImGui_IsItemEdited()) {
        e->expression_error = (behavior_expression_error){ 0 };
    }
    bool changed = strcmp(e->math_text, source) != 0;
    ImGui_SameLine();
    if (editor_ui_icon_button_sized("math.inspect", ED_GRAPH_OPEN_GLYPH, true, 28.0f)) {
        e->open_call_node = n->id;
    }
    editor_ui_track_item("math.inspect");
    if (apply && changed) {
        editor_set_behavior_expression(e, n, e->math_text, &e->expression_error);
    }
    if (e->expression_error.message[0]) {
        ImGui_TextWrapped(
            "%s (column %d)",
            e->expression_error.message,
            e->expression_error.offset + 1
        );
        editor_ui_track_item("math.error");
    }
}

void ed_draw_details(editor *e)
{
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR)) {
        return;
    }
    ImGui_SetNextWindowSize((ImVec2){ 340.0f, 600.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_INSPECTOR)) {
        ImGui_SetNextWindowFocus();
    }
    if (!editor_ui_begin_pane(EDITOR_UI_PANE_INSPECTOR, "Inspector", ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui_End();
        return;
    }
    if (e->active_graph->math_expression) {
        ImGui_TextUnformatted("Generated Behavior Expression graph · Read-only");
        ed_draw_math_graph_name(e, e->active_graph);
        ImGui_TextWrapped("%s", e->active_graph->math_expression);
        ImGui_End();
        return;
    }
    if (!e->selected_variable && !acount(e->selected) && !acount(e->selected_links)) {
        ed_draw_shader_details(e, NULL);
        ed_draw_field_details(e);
        if (e->asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
            e->asset->domain != BEHAVIOR_DOMAIN_SHAPE &&
            e->asset->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
            ImGui_TextUnformatted(e->asset->name);
            editor_ui_track_item("inspector.document");
            if (e->active_graph->name && e->active_graph->name[0]) {
                ImGui_TextDisabled("Function: %s", e->active_graph->name);
            }
        }
        ImGui_End();
        return;
    }
    if (e->selected_variable) {
        ed_draw_details_variable(e, e->selected_variable);
        ImGui_End();
        return;
    }
    if (acount(e->selected) == 0 && acount(e->selected_links) == 1) {
        ed_draw_details_edge(e, &e->selected_links[0]);
        ImGui_End();
        return;
    }
    if (acount(e->selected) == 0 && acount(e->selected_links) > 1) {
        ImGui_Text("%d connections selected", acount(e->selected_links));
        ImGui_End();
        return;
    }
    if (acount(e->selected) != 1) {
        ImGui_TextUnformatted("Multiple nodes selected");
        ImGui_End();
        return;
    }
    node *n = e->selected[0];
    char node_title[256];
    ImGui_Text("%s", ed_node_title(e, n, node_title, sizeof node_title));
    editor_ui_track_item("inspector.graph");
    ed_draw_shader_details(e, n);
    ed_details_project_event(e, n);
    ed_draw_math_source(e, n);
    ImGui_Separator();
    for (int p = 0; p < n->input_count; p++) {
        ed_draw_details_pin(e, n, 0, p);
    }
    bool has_outputs = false;
    for (int p = 0; p < n->output_count; p++) {
        has_outputs |= n->output_pins[p].kind == PIN_DATA;
    }
    ImGui_Spacing();
    if (has_outputs) {
        bool outputs_open = ImGui_CollapsingHeader("Outputs", 0);
        editor_ui_track_item("inspector.outputs");
        if (outputs_open) {
            for (int p = 0; p < n->output_count; p++) {
                ed_draw_details_pin(e, n, 1, p);
            }
        }
    }
    bool advanced_open = ImGui_CollapsingHeader("Advanced", 0);
    editor_ui_track_item("inspector.advanced");
    if (advanced_open) {
        bool disabled = n->disabled != 0;
        if (ImGui_Checkbox("Disabled", &disabled)) {
            ed_details_begin_mutation(e);
            if (graph_set_node_disabled(e->active_graph, n, disabled) == 0) {
                e->edit_changed = 1;
            }
        }
        ed_details_finish_mutation(e, ImGui_IsItemEdited());
        if (n->desc && n->desc->flow_kind == NODE_FLOW_EVENT) {
            ImGui_SeparatorText("Event signature");
            ed_details_event_catalog(e, n);
        } else if (n->desc && n->desc->flow_kind == NODE_FLOW_SEQUENCE) {
            ImGui_SeparatorText("Sequence outputs");
            if (ImGui_Button("Add output pin")) {
                char name[32];
                snprintf(name, sizeof name, "Then%d", n->output_count);
                pin_spec pin = { name, PIN_FLOW, NULL, PIN_EXEC };
                ed_snapshot_before(e);
                (void)graph_add_pin(e->active_graph, n, 1, &pin);
            }
            ImGui_SameLine();
            ImGui_BeginDisabled(n->output_count <= 2);
            if (ImGui_Button("Remove last output pin")) {
                ed_snapshot_before(e);
                (void)graph_remove_pin(e->active_graph, n, 1, n->output_count - 1);
            }
            ImGui_EndDisabled();
        }
        if ((e->active_graph->kind == GRAPH_FUNCTION ||
             e->active_graph->kind == GRAPH_MACRO) &&
            (n->id == e->active_graph->entry_id || n->id == e->active_graph->return_id)) {
            bool entry = n->id == e->active_graph->entry_id;
            ImGui_SeparatorText(entry ? "Function parameters" : "Function results");
            static char parameter_name[64] = "Value";
            static pin_type parameter_type = PIN_F32;
            static char parameter_nominal[64] = "CF_V2";
            ImGui_InputText("Parameter name", parameter_name, sizeof parameter_name, 0);
            ed_pin_type_combo("Parameter type", &parameter_type);
            bool nominal = parameter_type == PIN_STRUCT ||
                           parameter_type == PIN_HANDLE || parameter_type == PIN_ENUM;
            if (nominal) {
                ImGui_InputText("Registered type", parameter_nominal, sizeof parameter_nominal, 0);
            }
            if (parameter_name[0] && parameter_type != PIN_FLOW &&
                ImGui_Button("Add parameter")) {
                ed_snapshot_before(e);
                pin_spec pin = { .name = parameter_name,
                                 .type = parameter_type,
                                 .type_name = nominal ? parameter_nominal
                                                      : pin_type_name(parameter_type),
                                 .kind = PIN_DATA };
                graph_add_pin(e->active_graph, n, entry, &pin);
                for (int g = 0; g < acount(e->asset->graphs); g++) {
                    graph *caller = e->asset->graphs[g];
                    for (int i = 0; i < acount(caller->nodes); i++) {
                        node *call = caller->nodes[i];
                        if (ed_is_graph_call(call) && !call->reference_asset &&
                            call->reference_id == e->active_graph->graph_id) {
                            graph_add_pin(caller, call, !entry, &pin);
                        }
                    }
                }
            }
        }
        if (e->active_graph->kind == GRAPH_MACRO &&
            (n->id == e->active_graph->entry_id || n->id == e->active_graph->return_id)) {
            bool entry = n->id == e->active_graph->entry_id;
            ImGui_SeparatorText(entry ? "Macro execution inputs" : "Macro execution outputs");
            static char port_name[64] = "Path";
            ImGui_InputText("Port name", port_name, sizeof port_name, 0);
            if (port_name[0] && ImGui_Button("Add execution port")) {
                ed_snapshot_before(e);
                pin_spec pin = { .name = port_name, .type = PIN_FLOW, .kind = PIN_EXEC };
                graph_add_pin(e->active_graph, n, entry, &pin);
                for (int g = 0; g < acount(e->asset->graphs); g++) {
                    graph *caller = e->asset->graphs[g];
                    for (int i = 0; i < acount(caller->nodes); i++) {
                        node *call = caller->nodes[i];
                        if (call->desc == &NODE_CallMacro &&
                            call->reference_id == e->active_graph->graph_id) {
                            graph_add_pin(caller, call, !entry, &pin);
                        }
                    }
                }
            }
        }
        if (ed_field_output(n) >= 0 && ImGui_BeginMenu("Output fields")) {
            ed_draw_fields_menu(e, n);
            ImGui_EndMenu();
        }
    }
    ImGui_End();
}
