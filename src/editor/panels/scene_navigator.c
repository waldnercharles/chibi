#include "editor/panels/scene_properties.h"
#include "editor/ui/labels.h"
#include "editor/ui/search.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"
#include "graph/model/pin_fmt.h"
#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"
#include "runtime/generated.h"

#include <assert.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

bool scene_search_matches(const char *query, const char *text)
{
    return editor_search_score(query, text, NULL) >= 0;
}

typedef struct
{
    ImDrawList *draw;
    ImVec2 min;
    float width;
    ImU32 selected;
    ImU32 hovered;
} sidebar_row;

static sidebar_row sidebar_row_begin()
{
    sidebar_row row = { ImGui_GetWindowDrawList(),
                        ImGui_GetCursorScreenPos(),
                        ImGui_GetContentRegionAvail().x,
                        ImGui_GetColorU32(ImGuiCol_Header),
                        ImGui_GetColorU32(ImGuiCol_HeaderHovered) };
    ImDrawList_ChannelsSplit(row.draw, 2);
    ImDrawList_ChannelsSetCurrent(row.draw, 1);
    ImGui_PushStyleColorImVec4(ImGuiCol_Header, (ImVec4){ 0 });
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderHovered, (ImVec4){ 0 });
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderActive, (ImVec4){ 0 });
    return row;
}

static void sidebar_row_end(sidebar_row row, bool selected, const char *icon, const char *label, float inset)
{
    ImGui_PopStyleColorEx(3);
    ImVec2 min = ImGui_GetItemRectMin(), max = ImGui_GetItemRectMax();
    bool hovered = ImGui_IsItemHovered(0);
    ImDrawList_ChannelsSetCurrent(row.draw, 0);
    if (selected || hovered) {
        ImDrawList_AddRectFilledEx(
            row.draw,
            (ImVec2){ row.min.x + 2, min.y + 1 },
            (ImVec2){ row.min.x + row.width - 2, max.y - 1 },
            selected ? row.selected : row.hovered,
            5.0f,
            ImDrawFlags_RoundCornersAll
        );
    }
    ImDrawList_ChannelsSetCurrent(row.draw, 1);
    float y = (min.y + max.y - ImGui_GetFontSize()) * 0.5f;
    ImDrawList_PushClipRect(
        row.draw,
        (ImVec2){ row.min.x, min.y },
        (ImVec2){ row.min.x + row.width - 4, max.y },
        true
    );
    ImDrawList_AddText(row.draw, (ImVec2){ row.min.x + inset, y }, ImGui_GetColorU32(ImGuiCol_TextDisabled), icon);
    ImVec2 text_min = { row.min.x + inset + 24, y };
    ImVec2 text_max = { fmaxf(text_min.x, row.min.x + row.width - 8), max.y };
    ImGui_RenderTextEllipsis(row.draw, text_min, text_max, text_max.x, label, NULL, NULL);
    ImDrawList_PopClipRect(row.draw);
    ImDrawList_ChannelsMerge(row.draw);
}

static bool scene_actor_row(const char *name, bool selected)
{
    sidebar_row row = sidebar_row_begin();
    bool pressed = ImGui_SelectableEx("##actor_row", selected, 0, (ImVec2){ 0.0f, 28.0f });
    sidebar_row_end(row, selected, "\xef\x86\xb2", name, 10.0f);
    return pressed;
}

static void draw_scene_outline(app_scene_authoring *scene)
{
    if (ImGui_Button("Add")) {
        ImGui_OpenPopup("scene_add", 0);
    }
    editor_ui_track_item("scene.add");
    if (ImGui_BeginPopup("scene_add", 0)) {
        for (int i = 0; i < acount(scene->prototypes); i++) {
            const char *name = scene->prototypes[i].prototype->name;
            if (ImGui_MenuItem(name)) {
                app_scene_authoring_add_instance(scene, name);
            }
            char id[192];
            snprintf(id, sizeof id, "scene.add.%s", name);
            editor_ui_track_item(id);
        }
        ImGui_EndPopup();
    }
    ImGui_SetNextItemWidth(fmaxf(1, ImGui_GetContentRegionAvail().x));
    editor_ui_search_field("scene.search", scene->scene_search, sizeof scene->scene_search, "Search scene");
    int visible = 0;
    int delete_index = -1;
    for (int i = 0; i < acount(scene->actors); i++) {
        if (!scene_search_matches(scene->scene_search, scene->actors[i].name) &&
            !scene_search_matches(scene->scene_search, scene->actors[i].prototype_name)) {
            continue;
        }
        visible++;
        ImGui_PushIDInt(i);
        if (scene_actor_row(
                scene->actors[i].name,
                scene->actors[i].marked || i == scene->selected
            )) {
            select_instance(scene, i);
            scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
        }
        char item_id[192];
        snprintf(item_id, sizeof item_id, "scene.actor.%s", scene->actors[i].name);
        editor_ui_track_item(item_id);
        if (ImGui_IsItemHovered(0) && ImGui_IsMouseDoubleClicked(0)) {
            scene->reveal_actor = scene->actors[i].entity;
        }
        if (ImGui_BeginPopupContextItemEx("actor_actions", 0)) {
            if (ImGui_MenuItem("Delete")) {
                delete_index = i;
            }
            editor_ui_track_item("scene.delete");
            ImGui_EndPopup();
        }
        ImGui_PopID();
    }
    if (delete_index >= 0) {
        select_instance(scene, delete_index);
        app_scene_authoring_delete_instance(scene);
    }
    game_entities_copy_live(scene->live->entities, &scene->runtime_entities);
    bool spawned_section = false;
    for (int i = 0; i < acount(scene->runtime_entities); i++) {
        ecs_entity entity = scene->runtime_entities[i];
        char id[96], label[192];
        snprintf(id, sizeof id, "scene.spawned.%u.%u", entity.slot, entity.generation);
        snprintf(
            label,
            sizeof label,
            "%s %u",
            runtime_entity_label(scene, entity),
            entity.slot
        );
        if (!scene_search_matches(scene->scene_search, label)) {
            continue;
        }
        visible++;
        if (!spawned_section) {
            ImGui_SeparatorText("Spawned during Play");
            editor_ui_track_item("scene.spawned.section");
            spawned_section = true;
        }
        ImGui_PushID(id);
        if (scene_actor_row(
                label,
                ecs_entity_to_u64(entity) == ecs_entity_to_u64(scene->runtime_selected)
            )) {
            app_scene_authoring_select_runtime_entity(scene, entity);
        }
        editor_ui_track_item(id);
        if (ImGui_IsItemHovered(0) && ImGui_IsMouseDoubleClicked(0)) {
            scene->reveal_actor = entity;
        }
        ImGui_PopID();
    }
    if (!visible) {
        ImGui_TextDisabled(scene->scene_search[0] ? "No Results" : "No Instances");
        if (scene->scene_search[0]) {
            editor_ui_track_item("scene.search.empty");
        }
    }
}

static bool draw_content_tree(app_scene_authoring *scene, const char *parent)
{
    bool searching = scene->asset_search[0] != '\0';
    editor_search_result *results = NULL;
    if (searching) {
        for (int i = 0; i < acount(scene->content); i++) {
            scene_content_entry *entry = &scene->content[i];
            if (entry->directory) {
                continue;
            }
            int score = editor_search_score(
                scene->asset_search,
                content_basename(entry->path),
                entry->path
            );
            if (score >= 0) {
                apush(results, ((editor_search_result){ .index = i, .score = score }));
            }
        }
        if (acount(results) > 1) {
            qsort(results, (size_t)acount(results), sizeof *results, editor_search_compare);
        }
    }
    bool any = false;
    int count = searching ? acount(results) : acount(scene->content);
    for (int row = 0; row < count; row++) {
        int i = searching ? results[row].index : row;
        scene_content_entry *entry = &scene->content[i];
        if (!searching && !content_path_is_child(parent, entry->path)) {
            continue;
        }
        any = true;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_FramePadding;
        if (!entry->directory) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (scene->content_selected_path &&
            strcmp(scene->content_selected_path, entry->path) == 0) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        ImGui_PushID(entry->path);
        int prototype_index = content_prototype_index(scene, entry->path);
        editor_ui_asset_kind kind = EDITOR_UI_ASSET_FILE;
        if (entry->directory) {
            kind = EDITOR_UI_ASSET_FOLDER;
        } else if (prototype_index >= 0) {
            kind = EDITOR_UI_ASSET_PROTOTYPE;
        } else if (content_path_in_folder(scene, entry->path, "behaviors")) {
            kind = EDITOR_UI_ASSET_BEHAVIOR;
        } else if (content_path_in_folder(scene, entry->path, "scenes")) {
            kind = EDITOR_UI_ASSET_SCENE;
        } else if (content_path_in_folder(scene, entry->path, "systems")) {
            kind = EDITOR_UI_ASSET_SYSTEM;
        }
        const char *icon = editor_ui_asset_icon(kind);
        sidebar_row row = sidebar_row_begin();
        float icon_inset = ImGui_GetTreeNodeToLabelSpacing();
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, (ImVec2){ 6.0f, 6.0f });
        bool open = ImGui_TreeNodeEx("##file_row", flags);
        ImGui_PopStyleVar();
        sidebar_row_end(
            row,
            (flags & ImGuiTreeNodeFlags_Selected) != 0,
            icon,
            content_basename(entry->path),
            icon_inset
        );
        editor_ui_track_item(entry->path);
        if (!entry->directory && ImGui_IsItemHovered(0)) {
            ImGui_SetTooltip("%s", entry->path);
        }
        if (ImGui_IsItemClicked()) {
            select_content_entry(scene, entry);
        }
        if (!entry->directory && !entry->image && ImGui_IsItemHovered(0) &&
            ImGui_IsMouseDoubleClicked(0)) {
            open_content_entry(scene, entry);
        }
        if (prototype_index >= 0 && ImGui_BeginDragDropSource(0)) {
            const char *name = scene->prototypes[prototype_index].prototype->name;
            ImGui_SetDragDropPayload("CHIBI_PROTOTYPE", name, strlen(name) + 1, 0);
            ImGui_Text("Place %s in Scene", name);
            ImGui_EndDragDropSource();
        }
        if (entry->image && ImGui_BeginDragDropSource(0)) {
            ImGui_SetDragDropPayload("CHIBI_IMAGE", entry->path, strlen(entry->path) + 1, 0);
            ImGui_TextUnformatted(content_basename(entry->path));
            ImGui_EndDragDropSource();
        }
        if (!entry->directory && ImGui_BeginPopupContextItemEx("asset_actions", 1)) {
            ImGui_BeginDisabled(entry->image);
            if (ImGui_MenuItem("Open")) {
                open_content_entry(scene, entry);
            }
            ImGui_EndDisabled();
            if (prototype_index >= 0 && ImGui_MenuItem("Add to Scene")) {
                app_scene_authoring_add_instance(
                    scene,
                    scene->prototypes[prototype_index].prototype->name
                );
            }
            ImGui_EndPopup();
        }
        if (entry->directory && open) {
            draw_content_tree(scene, entry->path);
            ImGui_TreePop();
        }
        ImGui_PopID();
    }
    afree(results);
    return any;
}

static void draw_files(app_scene_authoring *scene)
{
    double now = ImGui_GetTime();
    if (now - scene->content_scan_time > 1.0) {
        content_refresh(scene);
        scene->content_scan_time = now;
    }
    if (ImGui_Button("+ New")) {
        ImGui_OpenPopup("create_asset", 0);
    }
    editor_ui_track_item("content.create");
    if (ImGui_BeginPopup("create_asset", 0)) {
        if (ImGui_MenuItem("Behavior")) {
            create_behavior_asset(scene);
        }
        editor_ui_track_item("content.new_behavior");
        if (ImGui_MenuItem("Function Library")) {
            create_graph_asset(scene, BEHAVIOR_DOMAIN_LIBRARY);
        }
        if (ImGui_MenuItem("Shader Function Library")) {
            create_graph_asset(scene, BEHAVIOR_DOMAIN_SHADER_LIBRARY);
        }
        if (ImGui_MenuItem("Event Definition")) {
            create_graph_asset(scene, BEHAVIOR_DOMAIN_EVENT_DEFINITION);
        }
        if (ImGui_MenuItem("Material")) {
            create_graph_asset(scene, BEHAVIOR_DOMAIN_MATERIAL);
        }
        if (ImGui_MenuItem("Shape")) {
            create_graph_asset(scene, BEHAVIOR_DOMAIN_SHAPE);
        }
        if (ImGui_MenuItem("Prototype")) {
            create_new_prototype(scene);
        }
        editor_ui_track_item("content.new_prototype");
        ImGui_EndPopup();
    }
    ImGui_SameLine();
    ImGui_SetNextItemWidth(fmaxf(1.0f, ImGui_GetContentRegionAvail().x));
    if (ImGui_BeginCombo("##project_view", scene->content_catalog ? "Asset catalog" : "Files", 0)) {
        if (ImGui_SelectableEx("Files", !scene->content_catalog, 0, (ImVec2){ 0 })) {
            scene->content_catalog = false;
        }
        editor_ui_track_item("content.view.files");
        if (ImGui_SelectableEx("Asset catalog", scene->content_catalog, 0, (ImVec2){ 0 })) {
            scene->content_catalog = true;
        }
        editor_ui_track_item("content.view.catalog");
        ImGui_EndCombo();
    }
    editor_ui_track_item("content.view");
    ImGui_SetNextItemWidth(fmaxf(1.0f, ImGui_GetContentRegionAvail().x));
    editor_ui_search_field(
        "content.search",
        scene->asset_search,
        sizeof scene->asset_search,
        scene->content_catalog ? "Search assets" : "Search files"
    );
    if (scene->content_catalog) {
        draw_asset_catalog(scene);
        return;
    }
    if (ImGui_BeginChild("content_tree", (ImVec2){ 0.0f, 0.0f }, 0, 0)) {
        if (!draw_content_tree(scene, scene->content_root)) {
            ImGui_TextDisabled(scene->asset_search[0] ? "No matching files" : "No files");
            editor_ui_track_item("content.empty");
        }
    }
    ImGui_EndChild();
}

static void draw_prototype_outline(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    if (!asset) {
        ImGui_TextDisabled("No Selection");
        return;
    }
    ImGui_TextUnformatted(asset->prototype->name);
    ImGui_SeparatorText("Components");
    for (int i = 0; i < acount(asset->prototype->components); i++) {
        const ecs_component_desc *desc = asset->prototype->components[i].desc;
        ImGui_TextUnformatted(desc->key ? desc->key : desc->name);
    }
    ImGui_SeparatorText("Behaviors");
    for (int i = 0; i < acount(asset->prototype->graphs); i++) {
        const char *name = asset->prototype->graphs[i];
        if (ImGui_SelectableEx(name, false, 0, (ImVec2){ 0.0f, 0.0f })) {
            focus_prototype_behavior(scene, name);
        }
    }
    const asset_index *index = app_scene_authoring_asset_index(scene);
    int position = index ? asset_index_find_name(
                               index,
                               ASSET_INDEX_PROTOTYPE,
                               asset->prototype->name
                           )
                         : -1;
    draw_asset_references(scene, position);
}

void draw_navigator(app_scene_authoring *scene)
{
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_WORLD_OUTLINER)) {
        scene->navigator_scene = true;
        editor_ui_request_focus(EDITOR_UI_FOCUS_WORLD_OUTLINER);
    }
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET)) {
        scene->navigator_scene = false;
        editor_ui_request_focus(EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET);
    }
    bool has_document = scene->document.kind != APP_EDITOR_DOCUMENT_SCENE ||
                        scene->graph_document_id != NULL;
    editor_ui_pane outline = scene->navigator_scene || !has_document
                               ? EDITOR_UI_PANE_WORLD_OUTLINER
                               : EDITOR_UI_PANE_MY_BEHAVIOR_ASSET;
    editor_ui_pane selected;
    if (!editor_ui_begin_navigator(&outline, has_document, &selected)) {
        return;
    }
    scene->navigator_scene = outline == EDITOR_UI_PANE_WORLD_OUTLINER;
    if (scene->navigation_error[0]) {
        ImGui_TextWrapped("%s", scene->navigation_error);
        editor_ui_track_item("navigator.history.error");
    }
    if (selected == EDITOR_UI_PANE_CONTENT) {
        draw_files(scene);
        editor_ui_end_navigator();
        return;
    }
    if (scene->navigator_scene || !has_document) {
        draw_scene_outline(scene);
    } else if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        draw_prototype_outline(scene);

    } else {
        editor_draw_outline(scene->graph_editor);
        const behavior_asset *asset = editor_behavior_asset(scene->graph_editor);
        if (asset && (asset->domain == BEHAVIOR_DOMAIN_EVENT ||
                      asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                      asset->domain == BEHAVIOR_DOMAIN_SHAPE)) {
            asset_index_kind kind = asset->domain == BEHAVIOR_DOMAIN_MATERIAL
                                      ? ASSET_INDEX_MATERIAL
                                  : asset->domain == BEHAVIOR_DOMAIN_SHAPE
                                      ? ASSET_INDEX_SHAPE
                                      : ASSET_INDEX_BEHAVIOR;
            const asset_index *index = app_scene_authoring_asset_index(scene);
            draw_asset_references(
                scene,
                index ? asset_index_find_name(index, kind, asset->name) : -1
            );
            if (asset->domain == BEHAVIOR_DOMAIN_EVENT) {
                draw_asset_binding_usages(
                    scene,
                    index ? asset_index_find_name(
                                index,
                                ASSET_INDEX_BEHAVIOR,
                                asset->name
                            )
                          : -1
                );
            }
        }
    }
    editor_ui_end_navigator();
}
