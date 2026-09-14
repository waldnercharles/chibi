#include "editor/ui/search.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"

#include <float.h>
#include <imgui/dcimgui.h>
#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *asset_kind_label(asset_index_kind kind)
{
    switch (kind) {
        case ASSET_INDEX_SCENE: return "Scene";
        case ASSET_INDEX_PROTOTYPE: return "Prototype";
        case ASSET_INDEX_BEHAVIOR: return "Behavior";
        case ASSET_INDEX_EVENT: return "Event";
        case ASSET_INDEX_MATERIAL: return "Material";
        case ASSET_INDEX_SHAPE: return "Shape";
        case ASSET_INDEX_LIBRARY: return "Library";
        case ASSET_INDEX_SYSTEM: return "System";
        case ASSET_INDEX_KIND_COUNT: return "All types";
    }
    return "Asset";
}

static bool asset_kind_matches(const asset_index_asset *asset, asset_index_kind kind)
{
    return kind == ASSET_INDEX_KIND_COUNT || asset->kind == kind;
}

void asset_browser_request_quick_open(app_scene_authoring *scene)
{
    app_scene_authoring_refresh_asset_index(scene);
    scene->quick_open = true;
    scene->quick_open_selected = 0;
    scene->quick_open_query[0] = '\0';
    scene->quick_open_kind = ASSET_INDEX_KIND_COUNT;
    scene->quick_open_focus = true;
}

static editor_search_result *ranked_assets(
    const asset_index *index,
    asset_index_kind kind,
    const char *query
)
{
    editor_search_result *results = NULL;
    for (int i = 0; i < asset_index_asset_count(index); i++) {
        const asset_index_asset *asset = asset_index_asset_at(index, i);
        if (!asset_kind_matches(asset, kind)) {
            continue;
        }
        int score = asset_index_score(asset, query);
        if (score >= 0) {
            apush(results, ((editor_search_result){ .index = i, .score = score }));
        }
    }
    if (acount(results) > 1) {
        qsort(results, (size_t)acount(results), sizeof *results, editor_search_compare);
    }
    return results;
}

static bool draw_asset_kind_combo(const char *id, asset_index_kind *kind)
{
    bool changed = false;
    ImGui_PushID(id);
    if (ImGui_BeginCombo("##asset_kind", asset_kind_label(*kind), 0)) {
        for (int i = 0; i <= ASSET_INDEX_KIND_COUNT; i++) {
            asset_index_kind candidate = (asset_index_kind)i;
            bool selected = candidate == *kind;
            if (ImGui_SelectableEx(asset_kind_label(candidate), selected, 0, (ImVec2){ 0, 0 })) {
                *kind = candidate;
                changed = true;
            }
            if (selected) {
                ImGui_SetItemDefaultFocus();
            }
            char track[96];
            snprintf(track, sizeof track, "%s.%s", id, asset_kind_label(candidate));
            editor_ui_track_item(track);
        }
        ImGui_EndCombo();
    }
    ImGui_PopID();
    return changed;
}

void draw_quick_open(app_scene_authoring *scene)
{
    ImGuiIO *io = ImGui_GetIO();
    bool modifier = io->KeyCtrl || io->KeySuper ||
                    ImGui_IsKeyDown(ImGuiKey_LeftCtrl) ||
                    ImGui_IsKeyDown(ImGuiKey_RightCtrl) ||
                    ImGui_IsKeyDown(ImGuiKey_LeftSuper) ||
                    ImGui_IsKeyDown(ImGuiKey_RightSuper);
    if (!scene->quick_open && modifier && ImGui_IsKeyPressed(ImGuiKey_P)) {
        asset_browser_request_quick_open(scene);
    }
    if (!scene->quick_open) {
        return;
    }
    ImVec2 work = ImGui_GetMainViewport()->WorkSize;
    ImGui_SetNextWindowPosEx(
        (ImVec2){ work.x * 0.5f, work.y * 0.5f },
        ImGuiCond_Always,
        (ImVec2){ 0.5f, 0.5f }
    );
    ImGui_SetNextWindowSize((ImVec2){ 1.0f, 1.0f }, ImGuiCond_Always);
    ImGui_Begin(
        "##asset_quick_open_host",
        NULL,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBackground
    );
    if (scene->quick_open_focus) {
        editor_ui_open_popup("asset_quick_open");
    }
    const ImGuiViewport *viewport = ImGui_GetMainViewport();
    float popup_width = fminf(720.0f, fmaxf(320.0f, viewport->WorkSize.x - 48.0f));
    float popup_height = fminf(460.0f, fmaxf(240.0f, viewport->WorkSize.y - 48.0f));
    ImGui_SetNextWindowPos(
        (ImVec2){ viewport->WorkPos.x + (viewport->WorkSize.x - popup_width) * 0.5f,
                  viewport->WorkPos.y + fmaxf(24.0f, viewport->WorkSize.y * 0.12f) },
        ImGuiCond_Always
    );
    ImGui_SetNextWindowSize((ImVec2){ popup_width, popup_height }, ImGuiCond_Always);
    if (!editor_ui_begin_popup("asset_quick_open")) {
        scene->quick_open = false;
        ImGui_End();
        return;
    }
    ImGui_SetNextItemWidth(fmaxf(320.0f, ImGui_GetContentRegionAvail().x));
    bool follow_selection = scene->quick_open_focus;
    if (scene->quick_open_focus) {
        ImGui_SetKeyboardFocusHere();
        scene->quick_open_focus = false;
    }
    bool changed = editor_ui_search_field(
        "quick.open.search",
        scene->quick_open_query,
        sizeof scene->quick_open_query,
        "Search assets"
    );
    editor_ui_track_item("quick.open.search");
    if (ImGui_IsItemActive()) {
        ImGui_GetCurrentContext()->ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Up) |
                                                              (1 << ImGuiDir_Down);
        ImGui_SetKeyOwner(ImGuiKey_UpArrow, ImGui_GetItemID(), 0);
        ImGui_SetKeyOwner(ImGuiKey_DownArrow, ImGui_GetItemID(), 0);
    }

    changed |= draw_asset_kind_combo("quick.open.kind", &scene->quick_open_kind);
    editor_ui_track_item("quick.open.kind");
    const asset_index *index = app_scene_authoring_asset_index(scene);
    editor_search_result
        *results = ranked_assets(index, scene->quick_open_kind, scene->quick_open_query);
    int count = acount(results);
    if (changed) {
        scene->quick_open_selected = 0;
        follow_selection = true;
    }
    if (count > 0) {
        if (scene->quick_open_selected >= count) {
            scene->quick_open_selected = count - 1;
        }
        if (ImGui_IsKeyPressed(ImGuiKey_DownArrow)) {
            scene->quick_open_selected = (scene->quick_open_selected + 1) % count;
            follow_selection = true;
        }
        if (ImGui_IsKeyPressed(ImGuiKey_UpArrow)) {
            scene->quick_open_selected = (scene->quick_open_selected + count - 1) % count;
            follow_selection = true;
        }
    }
    bool activate_enter = ImGui_IsKeyPressed(ImGuiKey_Enter);
    int activate = -1;
    int visible = 0;
    if (ImGui_BeginChild("quick.open.results", (ImVec2){ 0.0f, 300.0f }, 0, 0)) {
        for (int row = 0; row < count; row++) {
            int i = results[row].index;
            const asset_index_asset *asset = asset_index_asset_at(index, i);
            char label[256];
            snprintf(
                label,
                sizeof label,
                "%s  %s",
                asset->name,
                asset_kind_label(asset->kind)
            );
            ImGui_BeginDisabled(asset->unresolved);
            char row_id[64];
            snprintf(row_id, sizeof row_id, "quick.open.row.%d", i);
            ImGui_PushID(row_id);
            bool selected = visible == scene->quick_open_selected;
            bool pressed = ImGui_SelectableEx(label, selected, 0, (ImVec2){ 0, 0 });
            if (pressed) {
                scene->quick_open_selected = visible;
                if (!asset->unresolved) {
                    activate = i;
                }
            } else if (activate_enter && selected && !asset->unresolved) {
                activate = i;
            }
            if (selected && follow_selection) {
                ImGui_SetScrollHereY(0.5f);
            }
            ImGui_PopID();
            ImGui_EndDisabled();
            char item_id[96];
            snprintf(item_id, sizeof item_id, "quick.open.result.%d", visible);
            editor_ui_track_item(item_id);
            if (ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui_SetTooltip("%s", asset->path ? asset->path : "Unsaved asset");
            }
            if (asset->unresolved) {
                ImGui_SameLine();
                ImGui_TextDisabled("Missing");
            }
            visible++;
        }
    }
    ImGui_EndChild();
    afree(results);
    if (!visible) {
        ImGui_TextDisabled("No matching assets");
        editor_ui_track_item("quick.open.empty");
    }
    if (ImGui_IsKeyPressed(ImGuiKey_Escape)) {
        scene->quick_open = false;
        ImGui_CloseCurrentPopup();
    }
    editor_ui_end_popup();
    ImGui_End();
    if (activate >= 0) {
        if (app_scene_authoring_open_asset(scene, activate) == 0) {
            scene->quick_open = false;
            ImGui_CloseCurrentPopup();
        }
    }
}

static bool draw_index_asset_row(
    app_scene_authoring *scene,
    const asset_index_asset *asset,
    int position,
    const char *prefix
)
{
    char id[128];
    snprintf(id, sizeof id, "%s.%s.%d", prefix, asset_kind_label(asset->kind), position);
    ImGui_PushID(id);
    ImGui_BeginDisabled(asset->unresolved);
    char label[256];
    snprintf(label, sizeof label, "%s", asset->name);
    bool pressed = ImGui_SelectableEx(label, false, 0, (ImVec2){ 0, 0 });
    ImGui_EndDisabled();
    editor_ui_track_item(id);
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui_SetTooltip(
            "%s%s%s",
            asset_kind_label(asset->kind),
            asset->path ? "\n" : "",
            asset->path ? asset->path : "Unsaved asset"
        );
    }
    if (asset->unresolved) {
        ImGui_SameLine();
        ImGui_TextDisabled("Missing");
    }
    if (asset->kind == ASSET_INDEX_PROTOTYPE && !asset->unresolved &&
        ImGui_BeginDragDropSource(0)) {
        ImGui_SetDragDropPayload("CHIBI_PROTOTYPE", asset->name, strlen(asset->name) + 1, 0);
        ImGui_TextUnformatted(asset->name);
        ImGui_EndDragDropSource();
    }
    bool activate = pressed && ImGui_IsMouseDoubleClicked(0) && !asset->unresolved;
    if (activate) {
        app_scene_authoring_open_asset(scene, position);
    }
    ImGui_PopID();
    return pressed;
}

void draw_asset_catalog(app_scene_authoring *scene)
{
    ImGui_SetNextItemWidth(fmaxf(1, ImGui_GetContentRegionAvail().x));
    draw_asset_kind_combo("content.kind", &scene->content_kind);
    editor_ui_track_item("content.kind");
    const asset_index *index = app_scene_authoring_asset_index(scene);
    editor_search_result
        *results = ranked_assets(index, scene->content_kind, scene->asset_search);
    int assets = acount(results);
    if (ImGui_BeginChild("content_assets", (ImVec2){ 0.0f, 0.0f }, 0, 0)) {
        for (int row = 0; row < assets; row++) {
            int i = results[row].index;
            draw_index_asset_row(scene, asset_index_asset_at(index, i), i, "content.asset");
        }
        if (!assets) {
            ImGui_TextDisabled(scene->asset_search[0] ? "No matching assets" : "No authored assets");
            editor_ui_track_item("content.assets.empty");
        }
    }
    ImGui_EndChild();
    afree(results);
}
