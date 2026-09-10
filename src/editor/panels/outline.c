#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/model/registry.h"

#include <assert.h>
#include <ctype.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int ed_text_matches(const char *text, const char *filter)
{
    if (!filter || !filter[0]) {
        return 1;
    }
    if (!text) {
        return 0;
    }
    for (const char *start = text; *start; start++) {
        const char *a = start;
        const char *b = filter;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return 1;
        }
    }
    return 0;
}

static const char *ed_graph_display_name(const graph *g)
{
    if (g->graph_id == 0 && g->kind == GRAPH_EVENT) {
        if (g->owner && g->owner->domain == BEHAVIOR_DOMAIN_MATERIAL) {
            return "Material";
        }
        if (g->owner && g->owner->domain == BEHAVIOR_DOMAIN_SHAPE) {
            return "Shape";
        }
        return "Events";
    }
    return g->name && g->name[0] ? g->name : "Unnamed";
}

static const char *ed_variable_category(const behavior_variable *v)
{
    return v->category && v->category[0] ? v->category : "Default";
}

static int ed_variable_category_seen(const behavior_asset *asset, int index)
{
    const char *category = ed_variable_category(&asset->variables[index]);
    for (int i = 0; i < index; i++) {
        if (strcmp(category, ed_variable_category(&asset->variables[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ed_variable_category_matches(const behavior_asset *asset, int index, const char *filter)
{
    const char *category = ed_variable_category(&asset->variables[index]);
    if (ed_text_matches(category, filter)) {
        return 1;
    }
    for (int i = index; i < acount(asset->variables); i++) {
        if (strcmp(category, ed_variable_category(&asset->variables[i])) == 0 &&
            ed_text_matches(asset->variables[i].name, filter)) {
            return 1;
        }
    }
    return 0;
}

static void ed_tree_member_icon(const char *icon, ImVec4 color)
{
    ImGui_TextColored(color, "%s", icon);
    ImGui_SameLine();
}

static graph *ed_create_graph(editor *e, graph_kind kind, int pure)
{
    static char name[64];
    const char *base = kind == GRAPH_MACRO ? "Macro"
                     : kind == GRAPH_EVENT ? "EventGraph"
                                           : "Function";
    int first = kind == GRAPH_EVENT;
    for (int i = first; i < 10000; i++) {
        snprintf(name, sizeof name, i ? "%s_%d" : "%s", base, i);
        if (behavior_asset_find_graph_by_name(e->asset, name) == NULL) {
            break;
        }
    }
    ed_snapshot_before(e);
    graph *g = kind == GRAPH_MACRO ? behavior_asset_add_macro(e->asset, name)
             : kind == GRAPH_EVENT
                 ? behavior_asset_add_graph(e->asset, kind, name)
                 : behavior_asset_add_function(e->asset, name, pure);
    if (g) {
        editor_open_graph(e, g->graph_id);
    }
    return g;
}

node *ed_place_graph_call_at(editor *e, graph *target, ImVec2 world)
{
    if (!e->active_graph || e->active_graph->math_expression || !target) {
        return NULL;
    }
    ed_snapshot_before(e);
    node *call = target->kind == GRAPH_MACRO
                   ? behavior_asset_add_macro_call_node_to_graph(e->asset, e->active_graph, target)
                   : behavior_asset_add_function_call_node_to_graph(e->asset, e->active_graph, target);
    if (!call) {
        return NULL;
    }
    call->x = world.x;
    call->y = world.y;
    ed_select_node(e, call, 0);
    return call;
}

static node *ed_place_graph_call(editor *e, graph *target)
{
    return ed_place_graph_call_at(e, target, ed_view_center_world(e));
}

int editor_delete_graph(editor *e, uint64_t graph_id)
{
    graph *target = behavior_asset_find_graph(e->asset, graph_id);
    if (!target || target == e->asset->graph || !behavior_asset_mutable(e->asset)) {
        return -1;
    }
    ed_snapshot_before(e);
    if (target == e->active_graph &&
        editor_open_graph(e, e->asset->graph->graph_id) != 0) {
        return -1;
    }
    if (target->math_expression) {
        ed_reset_node_interaction(e);
        for (int i = 0; i < acount(e->asset->graphs); i++) {
            graph *g = e->asset->graphs[i];
            for (int j = acount(g->nodes) - 1; j >= 0; j--) {
                node *n = g->nodes[j];
                if ((n->desc == &NODE_CallPureFunction || n->desc == &NODE_CallFunction) &&
                    !n->reference_asset && n->reference_id == graph_id) {
                    int removed = graph_remove_node(g, n);
                    assert(removed == 0);
                    (void)removed;
                    j = acount(g->nodes);
                }
            }
        }
    }
    e->context_graph = NULL;
    return behavior_asset_delete_graph(e->asset, graph_id);
}

static int ed_draw_graph_member(editor *e, graph *g, int index, const char *filter)
{
    const char *kind = g->kind == GRAPH_MACRO ? "macro" : "function";
    if (!ed_text_matches(ed_graph_display_name(g), filter) &&
        !ed_text_matches(kind, filter)) {
        return 0;
    }
    int id = 5000 + index + (g->kind == GRAPH_MACRO ? 1000 : 0);
    ImGui_PushIDInt(id);
    char icon[4];
    ed_tree_member_icon(
        ed_node_prefix(g->kind == GRAPH_MACRO ? &NODE_CallMacro : &NODE_CallFunction, icon),
        g->kind == GRAPH_MACRO ? (ImVec4){ 0.75f, 0.55f, 1.0f, 1.0f }
                               : (ImVec4){ 0.45f, 0.70f, 1.0f, 1.0f }
    );
    char label[192];
    snprintf(label, sizeof label, "%s##member", ed_graph_display_name(g));
    if (ImGui_SelectableEx(label, e->active_graph == g, 0, (ImVec2){ 0.0f, 0.0f })) {
        editor_open_graph(e, g->graph_id);
    }
    char item_id[64];
    snprintf(
        item_id,
        sizeof item_id,
        "outline.graph.%llu",
        (unsigned long long)g->graph_id
    );
    editor_ui_track_item(item_id);
    if (ImGui_BeginDragDropSource(0)) {
        uint64_t graph_id = g->graph_id;
        (void)ImGui_SetDragDropPayload("CB_GRAPH_CALL", &graph_id, sizeof graph_id, 0);
        ImGui_Text("Place call: %s", ed_graph_display_name(g));
        ImGui_EndDragDropSource();
    }
    int delete_requested = 0;
    int rename_requested = 0;
    if (ImGui_BeginPopupContextItemEx("member_menu", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui_MenuItem("Open")) {
            editor_open_graph(e, g->graph_id);
            ImGui_CloseCurrentPopup();
        }
        if (ImGui_MenuItem("Rename")) {
            e->context_graph = g;
            snprintf(e->graph_rename, sizeof e->graph_rename, "%s", ed_graph_display_name(g));
            rename_requested = 1;
            ImGui_CloseCurrentPopup();
        }
        if (ImGui_MenuItemEx("Place Call Node", NULL, false, !e->active_graph->math_expression)) {
            (void)ed_place_graph_call(e, g);
            ImGui_CloseCurrentPopup();
        }
        ImGui_Separator();
        delete_requested = ImGui_MenuItemEx(
            g->kind == GRAPH_MACRO ? "Delete Macro" : "Delete Function",
            NULL,
            false,
            true
        );
        editor_ui_track_item("outline.graph.delete");
        if (delete_requested) {
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
    }
    ImGui_PopID();
    if (rename_requested) {
        ImGui_OpenPopup("rename_graph", 0);
    }
    return delete_requested && editor_delete_graph(e, g->graph_id) == 0;
}

static int ed_event_graph_has_match(const graph *g, const char *filter)
{
    if (ed_text_matches(ed_graph_display_name(g), filter)) {
        return 1;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->desc && n->desc->flow_kind == NODE_FLOW_EVENT &&
            ed_text_matches(n->reference_name ? n->reference_name : n->desc->name, filter)) {
            return 1;
        }
    }
    return 0;
}

static int ed_delete_event_node(editor *e, graph *g, node *n)
{
    if (!g || !n) {
        return -1;
    }
    ed_snapshot_before(e);
    if (graph_remove_node(g, n) != 0) {
        return -1;
    }
    ed_reset_node_interaction(e);
    return 0;
}

static int ed_draw_event_graph(editor *e, graph *g, int index, const char *filter)
{
    if (!ed_event_graph_has_match(g, filter)) {
        return 0;
    }
    int graph_match = ed_text_matches(ed_graph_display_name(g), filter);
    ImGui_PushIDInt(7000 + index);
    if (filter[0]) {
        ImGui_SetNextItemOpen(true, ImGuiCond_Always);
    }
    ed_tree_member_icon(ED_ICON_GRAPH, (ImVec4){ 0.45f, 0.80f, 0.55f, 1.0f });
    char graph_label[192];
    snprintf(graph_label, sizeof graph_label, "%s##event_graph", ed_graph_display_name(g));
    int open = ImGui_TreeNodeEx(graph_label, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui_IsItemClicked()) {
        editor_open_graph(e, g->graph_id);
    }
    int delete_requested = 0;
    int rename_requested = 0;
    if (ImGui_BeginPopupContextItemEx("event_graph_menu", 0)) {
        if (ImGui_MenuItem("Open")) {
            editor_open_graph(e, g->graph_id);
            ImGui_CloseCurrentPopup();
        }
        if (g != e->asset->graph) {
            if (ImGui_MenuItem("Rename")) {
                e->context_graph = g;
                snprintf(e->graph_rename, sizeof e->graph_rename, "%s", ed_graph_display_name(g));
                rename_requested = 1;
                ImGui_CloseCurrentPopup();
            }
            ImGui_Separator();
            delete_requested = ImGui_MenuItem("Delete Event Graph");
            if (delete_requested) {
                ImGui_CloseCurrentPopup();
            }
        }
        ImGui_EndPopup();
    }
    int event_deleted = 0;
    if (open) {
        for (int i = 0; i < acount(g->nodes); i++) {
            node *n = g->nodes[i];
            if (!n->desc || n->desc->flow_kind != NODE_FLOW_EVENT) {
                continue;
            }
            const char *name = n->reference_name && n->reference_name[0]
                                 ? n->reference_name
                                 : n->desc->name;
            if (!graph_match && !ed_text_matches(name, filter)) {
                continue;
            }
            char icon[4];
            ed_tree_member_icon(ed_node_prefix(n->desc, icon), (ImVec4){ 1.0f, 0.65f, 0.35f, 1.0f });
            char event_label[192];
            snprintf(event_label, sizeof event_label, "%s##event_%d", name, i);
            if (ImGui_SelectableEx(event_label, ed_node_selected(e, n), 0, (ImVec2){ 0.0f, 0.0f })) {
                if (editor_open_graph(e, g->graph_id) == 0) {
                    editor_reveal_node(e, n);
                }
            }
            int event_delete_requested = 0;
            if (ImGui_BeginPopupContextItemEx("event_menu", 0)) {
                event_delete_requested = ImGui_MenuItem("Delete Event");
                if (event_delete_requested) {
                    ImGui_CloseCurrentPopup();
                }
                ImGui_EndPopup();
            }
            if (event_delete_requested) {
                event_deleted = ed_delete_event_node(e, g, n) == 0;
                break;
            }
        }
        ImGui_TreePop();
    }
    ImGui_PopID();
    if (rename_requested) {
        ImGui_OpenPopup("rename_graph", 0);
    }
    if (event_deleted) {
        return 1;
    }
    return delete_requested && editor_delete_graph(e, g->graph_id) == 0;
}

static int ed_draw_variable_member(editor *e, behavior_variable *v, int index)
{
    char icon[4];
    ed_tree_member_icon(ed_node_prefix(&NODE_GetVariable, icon), (ImVec4){ 0.80f, 0.55f, 0.35f, 1.0f });
    char label[192];
    snprintf(label, sizeof label, "%s##variable_%d", v->name ? v->name : "?", index);
    if (ImGui_SelectableEx(label, e->selected_variable == v, 0, (ImVec2){ 0.0f, 0.0f })) {
        ed_select_variable(e, v);
    }
    int delete_requested = 0;
    if (ImGui_BeginPopupContextItemEx("variable_menu", 0)) {
        if (ImGui_MenuItem("Select")) {
            ed_select_variable(e, v);
            ImGui_CloseCurrentPopup();
        }
        ImGui_Separator();
        delete_requested = ImGui_MenuItem(
            e->asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION
                ? "Delete Parameter"
                : "Delete Variable"
        );
        if (delete_requested) {
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
    }
    if (!delete_requested) {
        return 0;
    }
    ed_snapshot_before(e);
    if (behavior_asset_delete_variable(e->asset, v->id) != 0) {
        return 0;
    }
    e->selected_variable = NULL;
    return 1;
}

static void ed_draw_graph_rename_popup(editor *e)
{
    if (!ImGui_BeginPopup("rename_graph", 0)) {
        return;
    }
    ImGui_TextUnformatted("Rename graph");
    if (ImGui_IsWindowAppearing()) {
        ImGui_SetKeyboardFocusHere();
    }
    ImGui_InputText("Name", e->graph_rename, sizeof e->graph_rename, 0);
    if (ImGui_Button("Rename") && e->context_graph && e->graph_rename[0]) {
        editor_command rename = {
            .kind = EDITOR_COMMAND_RENAME_GRAPH,
            .data.rename_graph = { e->context_graph->graph_id, e->graph_rename },
        };
        if (editor_command_apply(e, rename) == 0) {
            e->context_graph = NULL;
            ImGui_CloseCurrentPopup();
        } else {
            ed_show_error(e, "Graph name is empty or already in use");
        }
    }
    ImGui_SameLine();
    if (ImGui_Button("Cancel")) {
        e->context_graph = NULL;
        ImGui_CloseCurrentPopup();
    }
    ImGui_EndPopup();
}

static bool shader_asset(const editor *e)
{
    return e->asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
           e->asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
           e->asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
}

static bool library_asset(const editor *e)
{
    return e->asset->domain == BEHAVIOR_DOMAIN_LIBRARY ||
           e->asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
}

static void add_payload_parameter(editor *e)
{
    char name[64];
    int index = 1;
    do {
        snprintf(name, sizeof name, "Parameter%d", index++);
    } while (behavior_asset_find_variable_by_name(e->asset, name));
    ed_snapshot_before(e);
    behavior_variable *v = behavior_asset_add_variable(
        e->asset,
        name,
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        NULL,
        "Payload"
    );
    if (v) {
        ed_select_variable(e, v);
    }
}

static void ed_draw_add_popup(editor *e)
{
    if (!ImGui_BeginPopup("my_behavior_asset_add", 0)) {
        return;
    }
    if (e->asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
        if (ImGui_MenuItem("Parameter")) {
            add_payload_parameter(e);
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
        return;
    }
    if (!shader_asset(e) && !library_asset(e) && ImGui_MenuItem("Event Graph")) {
        (void)ed_create_graph(e, GRAPH_EVENT, 0);
        ImGui_CloseCurrentPopup();
    }
    if (ImGui_MenuItem("Function")) {
        (void)ed_create_graph(e, GRAPH_FUNCTION, shader_asset(e));
        ImGui_CloseCurrentPopup();
    }
    if (!shader_asset(e) && ImGui_MenuItem("Pure Function")) {
        (void)ed_create_graph(e, GRAPH_FUNCTION, 1);
        ImGui_CloseCurrentPopup();
    }
    if (!shader_asset(e) && !library_asset(e) && ImGui_MenuItem("Macro")) {
        (void)ed_create_graph(e, GRAPH_MACRO, 0);
        ImGui_CloseCurrentPopup();
    }
    if (!shader_asset(e) && !library_asset(e) && ImGui_MenuItem("Variable")) {
        ed_snapshot_before(e);
        behavior_variable *v = behavior_asset_add_variable(
            e->asset,
            ed_unique_variable_name(e),
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            NULL,
            "Default"
        );
        if (v) {
            ed_select_variable(e, v);
            ImGui_CloseCurrentPopup();
        }
    }
    ImGui_EndPopup();
}

void ed_finish_settings(editor *e)
{
    if (!e->settings_open) {
        return;
    }
    if (e->details_history_open) {
        if (e->edit_changed) {
            ed_commit_snapshot(e);
        } else {
            ed_cancel_snapshot(e);
        }
        e->details_history_open = 0;
        e->edit_changed = 0;
    }
    e->settings_open = false;
}

static void ed_draw_settings_popup(editor *e)
{
    e->settings_frame = ImGui_GetFrameCount();
    if (!ImGui_BeginPopup("behavior_settings", 0)) {
        ed_finish_settings(e);
        return;
    }
    e->settings_open = true;
    ed_draw_details_behavior_asset(e);
    bool close = ImGui_Button("Done");
    editor_ui_track_item("settings.close");
    if (close) {
        ImGui_CloseCurrentPopup();
    }
    ImGui_EndPopup();
    if (close) {
        ed_finish_settings(e);
    }
}

void editor_draw_outline(editor *e)
{
    if (ImGui_Button("+ Add")) {
        ImGui_OpenPopup("my_behavior_asset_add", 0);
    }
    ed_draw_add_popup(e);
    ImGui_SameLine();
    float search_width = ImGui_GetContentRegionAvail().x - 42.0f;
    if (search_width < 40.0f) {
        search_width = 40.0f;
    }
    ImGui_SetNextItemWidth(search_width);
    editor_ui_search_field(
        "my_behavior_asset_search",
        e->behavior_asset_search,
        sizeof e->behavior_asset_search,
        "Search"
    );
    ImGui_SameLine();
    if (editor_ui_icon_button("my_behavior_asset_settings", EDITOR_ICON_FA_COG, true)) {
        ImGui_OpenPopup("behavior_settings", 0);
    }
    editor_ui_track_item("outline.settings");
    ed_draw_settings_popup(e);

    const char *filter = e->behavior_asset_search;
    if (e->asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
        char label[64];
        snprintf(label, sizeof label, "Payload (%d)##payload", acount(e->asset->variables));
        bool open = ImGui_CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (editor_ui_right_icon_button("payload_add", EDITOR_ICON_FA_PLUS)) {
            add_payload_parameter(e);
        }
        if (open) {
            for (int i = 0; i < acount(e->asset->variables); i++) {
                behavior_variable *v = &e->asset->variables[i];
                if (ed_text_matches(v->name, filter) &&
                    ed_draw_variable_member(e, v, i)) {
                    break;
                }
            }
        }
        return;
    }

    if (filter[0]) {
        ImGui_SetNextItemOpen(true, ImGuiCond_Always);
    }
    if (!library_asset(e)) {
        bool graphs_open = ImGui_CollapsingHeader(
            shader_asset(e) ? "Shader##member_graphs" : "Events##member_graphs",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap
        );
        if (!shader_asset(e) &&
            editor_ui_right_icon_button("member_graphs_add", EDITOR_ICON_FA_PLUS)) {
            (void)ed_create_graph(e, GRAPH_EVENT, 0);
        }
        if (graphs_open) {
            for (int i = 0; i < acount(e->asset->graphs); i++) {
                graph *g = e->asset->graphs[i];
                if (g->kind == GRAPH_EVENT && ed_draw_event_graph(e, g, i, filter)) {
                    break;
                }
            }
        }
    }

    int function_count = 0;
    int macro_count = 0;
    for (int i = 0; i < acount(e->asset->graphs); i++) {
        function_count += e->asset->graphs[i]->kind == GRAPH_FUNCTION;
        macro_count += e->asset->graphs[i]->kind == GRAPH_MACRO;
    }
    char section[64];
    if (function_count > 0) {
        snprintf(section, sizeof section, "Functions (%d)##member_functions", function_count);
        if (filter[0]) {
            ImGui_SetNextItemOpen(true, ImGuiCond_Always);
        }
        bool functions_open = ImGui_CollapsingHeader(
            section,
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap
        );
        if (editor_ui_right_icon_button("member_functions_add", EDITOR_ICON_FA_PLUS)) {
            (void)ed_create_graph(e, GRAPH_FUNCTION, shader_asset(e));
        }
        if (functions_open) {
            for (int i = 0; i < acount(e->asset->graphs); i++) {
                graph *g = e->asset->graphs[i];
                if (g->kind == GRAPH_FUNCTION && ed_draw_graph_member(e, g, i, filter)) {
                    break;
                }
            }
        }
    }

    if (macro_count > 0) {
        snprintf(section, sizeof section, "Macros (%d)##member_macros", macro_count);
        if (filter[0]) {
            ImGui_SetNextItemOpen(true, ImGuiCond_Always);
        }
        bool macros_open = ImGui_CollapsingHeader(section, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (editor_ui_right_icon_button("member_macros_add", EDITOR_ICON_FA_PLUS)) {
            (void)ed_create_graph(e, GRAPH_MACRO, 0);
        }
        if (macros_open) {
            for (int i = 0; i < acount(e->asset->graphs); i++) {
                graph *g = e->asset->graphs[i];
                if (g->kind == GRAPH_MACRO && ed_draw_graph_member(e, g, i, filter)) {
                    break;
                }
            }
        }
    }

    if (acount(e->asset->variables) > 0) {
        snprintf(
            section,
            sizeof section,
            "Variables (%d)##member_variables",
            acount(e->asset->variables)
        );
        if (filter[0]) {
            ImGui_SetNextItemOpen(true, ImGuiCond_Always);
        }
        bool variables_open = ImGui_CollapsingHeader(
            section,
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap
        );
        if (editor_ui_right_icon_button("member_variables_add", EDITOR_ICON_FA_PLUS)) {
            ed_snapshot_before(e);
            behavior_variable *v = behavior_asset_add_variable(
                e->asset,
                ed_unique_variable_name(e),
                PIN_I32,
                "int",
                PIN_CONTAINER_SINGLE,
                NULL,
                "Default"
            );
            if (v) {
                ed_select_variable(e, v);
            }
        }
        if (variables_open) {
            for (int i = 0; i < acount(e->asset->variables); i++) {
                behavior_variable *v = &e->asset->variables[i];
                const char *category = ed_variable_category(v);
                if (ed_variable_category_seen(e->asset, i) ||
                    !ed_variable_category_matches(e->asset, i, filter)) {
                    continue;
                }
                int category_match = ed_text_matches(category, filter);
                ImGui_PushIDInt(9000 + i);
                if (filter[0]) {
                    ImGui_SetNextItemOpen(true, ImGuiCond_Always);
                }
                char category_label[192];
                snprintf(category_label, sizeof category_label, "%s##variable_category", category);
                bool category_open = ImGui_TreeNodeEx(
                    category_label,
                    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen
                );
                if (category_open) {
                    for (int j = i; j < acount(e->asset->variables); j++) {
                        behavior_variable *member = &e->asset->variables[j];
                        if (strcmp(category, ed_variable_category(member)) != 0 ||
                            (!category_match && !ed_text_matches(member->name, filter))) {
                            continue;
                        }
                        if (ed_draw_variable_member(e, member, j)) {
                            break;
                        }
                    }
                    ImGui_TreePop();
                }
                ImGui_PopID();
            }
        }
    }

    ed_draw_graph_rename_popup(e);
}

/* EVT-3 makes a duplicate name a compile error, so a name already taken by
 * another event node is offered as disabled rather than silently creating one. */
