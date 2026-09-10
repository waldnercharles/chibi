#include "editor/panels/shader_details.h"

#include "editor/workspace/editor_internal.h"
#include "graph/document/library.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "runtime/engine.h"

#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void render_preview(void *user)
{
    editor *e = user;
    int width, height;
    engine_viewport_pixel_size(e->shader_viewport, &width, &height);
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(
        e->asset,
        e->asset_catalog,
        acount(e->asset_catalog),
        error,
        sizeof error
    );
    app_scene_renderer_preview(
        e->shader_preview,
        linked ? linked : e->asset,
        app_scene_renderer_time(e->shader_preview),
        width,
        height
    );
    behavior_asset_free(linked);
}

static void draw_preview(editor *e)
{
    if (!cf_app_is_running()) {
        return;
    }
    if (!e->shader_preview) {
        e->shader_preview = app_scene_renderer_new();
    }
    if (!e->shader_viewport) {
        e->shader_viewport = engine_viewport_new(256, 256);
        engine_viewport_set_render_callback(e->shader_viewport, render_preview, e);
    }
    engine_viewport_set_perspective(e->shader_viewport, e->asset->domain == BEHAVIOR_DOMAIN_MATERIAL);
    float x = e->asset->domain == BEHAVIOR_DOMAIN_SHAPE ? e->asset->bounds[0] : 50;
    float y = e->asset->domain == BEHAVIOR_DOMAIN_SHAPE ? e->asset->bounds[1] : 50;
    engine_viewport_fit(e->shader_viewport, -x * 1.2f, -y * 1.2f, x * 1.2f, y * 1.2f);
    ImGui_SeparatorText("Preview");
    float time = app_scene_renderer_time(e->shader_preview);
    if (!e->shader_preview_paused) {
        time += ImGui_GetIO()->DeltaTime;
    }
    ImGui_Checkbox("Pause", &e->shader_preview_paused);
    ImGui_SameLine();
    ImGui_SetNextItemWidth(ImGui_GetContentRegionAvail().x);
    if (ImGui_DragFloatEx("##shader_time", &time, 0.01f, 0, 10000, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
        e->shader_preview_paused = true;
    }
    app_scene_renderer_set_time(e->shader_preview, time);
    float size = ImGui_GetContentRegionAvail().x;
    if (size > 320) {
        size = 320;
    }
    if (size > 0) {
        engine_viewport_image(e->shader_viewport, size, size);
    }
    editor_ui_track_item("shader.preview");
    const char *problem = app_scene_renderer_error(e->shader_preview);
    if (problem[0]) {
        ImGui_TextWrapped("%s", problem);
        editor_ui_track_item("shader.backend_error");
    }
}

static void parameter_type(editor *e, node *n)
{
    const char *names[] = { "Scalar",   "Vector 2", "Vector 3",
                            "Vector 4", "Color",    "Boolean" };
    const char *types[] = { NULL,         "ShaderVec2", "ShaderVec3",
                            "ShaderVec4", "ShaderVec3", NULL };
    pin_spec *pin = &n->output_pins[0];
    int current = pin->type == PIN_BOOL ? 5 : 0;
    for (int i = 1; i < 4; i++) {
        if (pin->type_name && !strcmp(pin->type_name, types[i])) {
            current = i;
        }
    }
    if ((pin->flags & PIN_FLAG_COLOR) && current >= 2) {
        current = 4;
    }
    bool open = ImGui_BeginCombo("Type", names[current], 0);
    editor_ui_track_item("shader.parameter.type");
    if (!open) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        bool selected = ImGui_SelectableEx(names[i], i == current, 0, (ImVec2){ 0 });
        char id[64];
        snprintf(id, sizeof id, "shader.parameter.type.%d", i);
        editor_ui_track_item(id);
        if (!selected || i == current) {
            continue;
        }
        float components[4] = { 0 };
        if (!current) {
            components[0] = pin->default_value.f;
        } else if (current == 5) {
            components[0] = pin->default_value.b ? 1 : 0;
        } else if (pin->default_value.ptr) {
            memcpy(components, pin->default_value.ptr, (size_t)ed_vector_width(pin) * sizeof(float));
        }
        ed_snapshot_before(e);
        pin_type type = i == 5 ? PIN_BOOL : i ? PIN_STRUCT : PIN_F32;
        graph_set_pin_type(e->active_graph, n, 1, 0, type, types[i]);
        pin->flags = (pin->flags & ~PIN_FLAG_COLOR) | (i == 4 ? PIN_FLAG_COLOR : 0);
        pin_value value = { .type = type, .type_name = types[i] };
        if (i == 5) {
            value.b = components[0] != 0;
        } else if (i) {
            value.ptr = components;
        } else {
            value.f = components[0];
        }
        graph_set_pin_default(e->active_graph, n, 1, 0, &value);
    }
    ImGui_EndCombo();
}
void ed_draw_shader_details(editor *e, node *n)
{
    if (n) {
        ed_draw_shader_controls(e, n);
    }
    if (e->asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        if (!n) {
            ImGui_TextUnformatted("Shader Function Library");
        }
        const char *problem = behavior_asset_validate_library(e->asset);
        if (problem) {
            ImGui_TextWrapped("%s", problem);
        }
        return;
    }
    if (e->asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
        e->asset->domain != BEHAVIOR_DOMAIN_SHAPE) {
        return;
    }
    if (!n) {
        ImGui_TextUnformatted(e->asset->domain == BEHAVIOR_DOMAIN_SHAPE ? "Shape" : "Material");
        if (e->asset->domain == BEHAVIOR_DOMAIN_SHAPE) {
            float bounds[2] = { e->asset->bounds[0], e->asset->bounds[1] };
            if (ImGui_DragFloat2Ex("Half extents", bounds, 0.1f, 0.01f, 100000, "%.3g", ImGuiSliderFlags_AlwaysClamp)) {
                ed_details_begin_mutation(e);
                e->asset->bounds[0] = bounds[0];
                e->asset->bounds[1] = bounds[1];
            }
            editor_ui_track_item("shader.bounds");
            ed_details_finish_mutation(e, ImGui_IsItemEdited());
        }
    }
    if (n && (!strcmp(n->desc->name, "Shader Parameter") ||
              !strcmp(n->desc->name, "Shader Constant"))) {
        if (!strcmp(n->desc->name, "Shader Parameter")) {
            char name[128];
            snprintf(name, sizeof name, "%s", n->reference_name ? n->reference_name : "");
            if (ImGui_InputText("Name", name, sizeof name, 0)) {
                ed_details_begin_mutation(e);
                graph_set_node_reference(e->active_graph, n, n->reference_id, name);
            }
            editor_ui_track_item("shader.parameter.name");
            ed_details_finish_mutation(e, ImGui_IsItemEdited());
        }
        parameter_type(e, n);
        ed_draw_details_pin(e, n, 1, 0);
    }
    shader_graph ir;
    shader_graph_init(&ir);
    shader_error error = { 0 };
    char link_error[256] = { 0 };
    behavior_asset *linked = behavior_asset_link_libraries(
        e->asset,
        e->asset_catalog,
        acount(e->asset_catalog),
        link_error,
        sizeof link_error
    );
    if (!linked || !shader_graph_from_graph(linked->graph, &ir, &error)) {
        ImGui_SeparatorText("Shader error");
        ImGui_TextWrapped("%s", linked ? error.message : link_error);
        if (error.node && linked) {
            const graph *source = NULL;
            for (int i = 0; i < acount(linked->graphs); i++) {
                graph *g = linked->graphs[i];
                if (g->source_asset && graph_node_by_id(g, error.node)) {
                    source = g;
                }
            }
            if (source) {
                uint64_t original = error.node -
                                    (source->graph_id - source->source_graph_id);
                ImGui_TextDisabled("%s / %s", source->source_asset, source->name);
                if (e->open_asset && ImGui_Button("Open source")) {
                    if (e->open_asset(
                            e->open_asset_user,
                            source->source_asset,
                            source->source_graph_id
                        ) == 0) {
                        editor_reveal_node_by_id(e, source->source_graph_id, original);
                    }
                    shader_graph_free(&ir);
                    behavior_asset_free(linked);
                    return;
                }
            } else {
                ImGui_TextDisabled("Node %llu", (unsigned long long)error.node);
            }
        }
        editor_ui_track_item("shader.error");
    }
    shader_graph_free(&ir);
    behavior_asset_free(linked);
    draw_preview(e);
}
