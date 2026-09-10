#include "editor/panels/shader_inline.h"

#include "editor/ui/color.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/shader/shader_graph.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool is_ramp(const node *n)
{
    return n && !strcmp(n->desc->name, "Shader Color Ramp");
}

float ed_shader_inline_height(const node *n)
{
    return is_ramp(n) ? 64.0f : 0.0f;
}

float ed_shader_inline_width(const node *n)
{
    return is_ramp(n) ? 240.0f : 0.0f;
}

static void set_ramp(editor *e, node *n, const shader_ramp_stop *stops, int count)
{
    bool valid = shader_node_ramp_set(e->active_graph, n, stops, count);
    assert(valid);
    (void)valid;
}

static int ramp_selected(const editor *e, const node *n, int count)
{
    if (e->shader_ramp_node != n->id || e->shader_ramp_stop < 0 ||
        e->shader_ramp_stop >= count) {
        return 0;
    }
    return e->shader_ramp_stop;
}

static void select_ramp_stop(editor *e, const node *n, int selected)
{
    e->shader_ramp_node = n->id;
    e->shader_ramp_stop = selected;
}

static void ramp_color(
    const shader_ramp_stop *stops,
    int count,
    int interpolation,
    float position,
    float out[4]
)
{
    int right = 1;
    while (right < count - 1 && position >= stops[right].position) {
        right++;
    }
    const shader_ramp_stop *a = &stops[right - 1], *b = &stops[right];
    float t = fminf(1, fmaxf(0, (position - a->position) / (b->position - a->position)));
    if (interpolation == 1) {
        t = t < 1 ? 0 : 1;
    } else if (interpolation == 2) {
        t = t * t * (3 - 2 * t);
    }
    for (int c = 0; c < 4; c++) {
        out[c] = a->color[c] + (b->color[c] - a->color[c]) * t;
    }
}

static void ramp_draw_strip(
    editor *e,
    node *n,
    shader_ramp_stop *stops,
    int count,
    int interpolation,
    ImVec2 start,
    float width,
    float height,
    float scale,
    const char *item_id,
    const char *button_id
)
{
    float pad = 5.0f * scale;
    float range = fmaxf(1.0f, width - 2.0f * pad);
    float tile = 8.0f * scale;
    ImGui_SetCursorScreenPos(start);
    ImGui_InvisibleButton(button_id, (ImVec2){ width, height + 12.0f * scale }, ImGuiButtonFlags_EnableNav);
    editor_ui_track_item(item_id);
    e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    if (ImGui_IsItemActivated()) {
        float x = ImGui_GetIO()->MousePos.x - start.x;
        int nearest = 0;
        for (int i = 1; i < count; i++) {
            if (fabsf(x - (pad + stops[i].position * range)) <
                fabsf(x - (pad + stops[nearest].position * range))) {
                nearest = i;
            }
        }
        select_ramp_stop(e, n, nearest);
    }
    bool changed = false;
    if (ImGui_IsItemActive() && ImGui_IsMouseDragging(0, -1)) {
        int selected = ramp_selected(e, n, count);
        float position = (ImGui_GetIO()->MousePos.x - start.x - pad) / range;
        float min = selected ? nextafterf(stops[selected - 1].position, 1) : 0;
        float max = selected + 1 < count
                      ? nextafterf(stops[selected + 1].position, 0)
                      : 1;
        position = fminf(max, fmaxf(min, position));
        changed = position != stops[selected].position;
        if (changed) {
            ed_details_begin_mutation(e);
            select_ramp_stop(e, n, selected);
            stops[selected].position = position;
            set_ramp(e, n, stops, count);
        }
    }
    ed_details_finish_mutation(e, changed);
    ImDrawList *draw = ImGui_GetWindowDrawList();
    for (int x = 0; x < (int)width; x++) {
        float color[4];
        float position = fminf(1, fmaxf(0, (x + 0.5f - pad) / range));
        ramp_color(stops, count, interpolation, position, color);
        for (float y = 0; y < height; y += tile) {
            float checker = ((x / (int)tile + (int)y / (int)tile) & 1) ? 0.25f : 0.4f;
            ImVec4 display = { color[0], color[1], color[2], 1 };
            display.x = checker + (display.x - checker) * color[3];
            display.y = checker + (display.y - checker) * color[3];
            display.z = checker + (display.z - checker) * color[3];
            ImDrawList_AddRectFilledEx(
                draw,
                (ImVec2){ start.x + x, start.y + y },
                (ImVec2){ start.x + x + 1, start.y + fminf(y + tile, height) },
                ImGui_ColorConvertFloat4ToU32(display),
                0,
                0
            );
        }
    }
    int selected = ramp_selected(e, n, count);
    for (int i = 0; i < count; i++) {
        float x = start.x + pad + stops[i].position * range;
        ImVec2 a = { x, start.y + height },
               b = { x - pad, start.y + height + 9.0f * scale },
               c = { x + pad, start.y + height + 9.0f * scale };
        ImDrawList_AddTriangleFilled(draw, a, b, c, IM_COL32(25, 28, 34, 255));
        ImDrawList_AddTriangleEx(
            draw,
            a,
            b,
            c,
            i == selected ? IM_COL32(110, 210, 255, 255) : IM_COL32(150, 150, 150, 255),
            1.5f * scale
        );
    }
}

static void ramp_setting_choice(editor *e, node *n, const char *label, const char *const *names, int count)
{
    int selected = n->input_pins[2].default_value.i;
    const char *current = selected >= 0 && selected < count ? names[selected] : "Invalid";
    bool open = ImGui_BeginCombo(label, current, 0);
    editor_ui_track_item("shader.setting.2");
    if (!open) {
        return;
    }
    for (int i = 0; i < count; i++) {
        bool chosen = ImGui_SelectableEx(names[i], selected == i, 0, (ImVec2){ 0 });
        char id[80];
        snprintf(id, sizeof id, "shader.setting.2.%d", i);
        editor_ui_track_item(id);
        if (chosen && i != selected) {
            ed_snapshot_before(e);
            pin_value value = { .type = PIN_I32, .i = i };
            graph_set_pin_default(e->active_graph, n, 0, 2, &value);
        }
    }
    ImGui_EndCombo();
}

static void ramp_common_controls(
    editor *e,
    node *n,
    shader_ramp_stop *stops,
    int count,
    int interpolation,
    ImVec2 start,
    float width,
    float strip_height,
    bool compact
)
{
    char strip_id[96];
    if (compact) {
        snprintf(
            strip_id,
            sizeof strip_id,
            "shader.ramp.inline.%llu.strip",
            (unsigned long long)n->id
        );
    } else {
        snprintf(strip_id, sizeof strip_id, "shader.ramp.strip");
    }
    float scale = compact ? e->zoom : 1.0f;
    ramp_draw_strip(
        e,
        n,
        stops,
        count,
        interpolation,
        start,
        width,
        strip_height,
        scale,
        strip_id,
        compact ? "##inline_ramp" : "##ramp"
    );
    int selected = ramp_selected(e, n, count);
    float position = stops[selected].position;
    float min = selected ? nextafterf(stops[selected - 1].position, 1) : 0;
    float max = selected + 1 < count ? nextafterf(stops[selected + 1].position, 0) : 1;
    if (compact) {
        ImVec2 controls = { start.x, start.y + strip_height + 12.0f * scale };
        ImGui_SetCursorScreenPos(controls);
        float color_width = 54.0f * scale;
        ImGui_PushFontFloat(NULL, ImGui_GetFontSize() * scale);
        ImGui_PushStyleVarImVec2(
            ImGuiStyleVar_FramePadding,
            ed_scale(ImGui_GetStyle()->FramePadding, scale)
        );
        ImGui_PushStyleVarImVec2(
            ImGuiStyleVar_ItemSpacing,
            ed_scale(ImGui_GetStyle()->ItemSpacing, scale)
        );
        ImGui_SetNextItemWidth(fmaxf(40.0f * scale, width - color_width - 8.0f * scale));
        bool changed = ImGui_SliderFloatEx(
            "##inline_position",
            &position,
            min,
            max,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat
        );
        editor_ui_track_item("shader.ramp.inline.position");
        e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
        if (changed) {
            ed_details_begin_mutation(e);
            select_ramp_stop(e, n, selected);
            stops[selected].position = fminf(max, fmaxf(min, position));
            set_ramp(e, n, stops, count);
        }
        ed_details_finish_mutation(e, changed);
        ImGui_SameLine();
        ImGui_SetNextItemWidth(color_width);
        bool first_change;
        if (editor_color_draw_float(
                &e->color_state,
                "shader.ramp.inline.color",
                stops[selected].color,
                4,
                &first_change
            )) {
            if (first_change) {
                ed_snapshot_before(e);
            }
            select_ramp_stop(e, n, selected);
            set_ramp(e, n, stops, count);
        }
        e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
        ImGui_PopStyleVarEx(2);
        ImGui_PopFont();
        return;
    }
    float label_width = ImGui_CalcTextSize("Position").x +
                        ImGui_GetStyle()->ItemInnerSpacing.x;
    ImGui_SetNextItemWidth(fmaxf(1.0f, width - label_width));
    bool changed = ImGui_SliderFloatEx(
        "Position",
        &position,
        min,
        max,
        "%.3f",
        ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat
    );
    editor_ui_track_item("shader.ramp.position");
    e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    if (changed) {
        ed_details_begin_mutation(e);
        select_ramp_stop(e, n, selected);
        stops[selected].position = fminf(max, fmaxf(min, position));
        set_ramp(e, n, stops, count);
    }
    ed_details_finish_mutation(e, changed);
    bool first_change;
    if (editor_color_draw_float(
            &e->color_state,
            "shader.ramp.color",
            stops[selected].color,
            4,
            &first_change
        )) {
        if (first_change) {
            ed_snapshot_before(e);
        }
        select_ramp_stop(e, n, selected);
        set_ramp(e, n, stops, count);
    }
    e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
}

static bool ramp_state(const editor *e, node *n, shader_ramp_stop stops[SHADER_RAMP_MAX], int *count)
{
    *count = shader_node_ramp_get(n, stops);
    if (*count < 2 || !shader_ramp_valid(stops, *count)) {
        return false;
    }
    return true;
}

void ed_draw_shader_ramp_controls(editor *e, node *n)
{
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    int count;
    if (!ramp_state(e, n, stops, &count)) {
        return;
    }
    const char *const names[] = { "Linear", "Constant", "Smooth" };
    ramp_setting_choice(e, n, "Interpolation", names, 3);
    int interpolation = n->input_pins[2].default_value.i;
    ImVec2 start = ImGui_GetCursorScreenPos();
    float width = fmaxf(1, ImGui_GetContentRegionAvail().x);
    ramp_common_controls(e, n, stops, count, interpolation, start, width, ImGui_GetFrameHeight(), false);
    int selected = ramp_selected(e, n, count);
    ImGui_BeginDisabled(count == SHADER_RAMP_MAX);
    if (ImGui_Button("Add stop")) {
        int gap = 0;
        for (int i = 1; i < count - 1; i++) {
            if (stops[i + 1].position - stops[i].position >
                stops[gap + 1].position - stops[gap].position) {
                gap = i;
            }
        }
        shader_ramp_stop added = {
            .position = (stops[gap].position + stops[gap + 1].position) * .5f
        };
        ramp_color(stops, count, interpolation, added.position, added.color);
        if (added.position > stops[gap].position &&
            added.position < stops[gap + 1].position) {
            ed_snapshot_before(e);
            memmove(&stops[gap + 2], &stops[gap + 1], (size_t)(count - gap - 1) * sizeof *stops);
            stops[gap + 1] = added;
            set_ramp(e, n, stops, ++count);
            select_ramp_stop(e, n, gap + 1);
        }
    }
    editor_ui_track_item("shader.ramp.add");
    ImGui_EndDisabled();
    ImGui_SameLine();
    ImGui_BeginDisabled(count == 2);
    if (ImGui_Button("Remove stop")) {
        ed_snapshot_before(e);
        memmove(&stops[selected], &stops[selected + 1], (size_t)(count - selected - 1) * sizeof *stops);
        set_ramp(e, n, stops, --count);
        select_ramp_stop(e, n, selected < count ? selected : count - 1);
    }
    editor_ui_track_item("shader.ramp.remove");
    ImGui_EndDisabled();
    ImGui_Text("Stop %d of %d", ramp_selected(e, n, count) + 1, count);
}

void ed_draw_shader_inline(editor *e, node *n, ImVec2 min, ImVec2 max)
{
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    int count;
    if (!ramp_state(e, n, stops, &count)) {
        return;
    }
    int interpolation = n->input_pins[2].default_value.i;
    float height = ed_shader_inline_height(n) * e->zoom;
    float padding = 8.0f * e->zoom;
    ImVec2 start = { min.x + padding, max.y - height + 4.0f * e->zoom };
    float width = fmaxf(1.0f, max.x - min.x - 2.0f * padding);
    ImGui_PushIDPtr(n);
    ramp_common_controls(e, n, stops, count, interpolation, start, width, 16.0f * e->zoom, true);
    ImGui_PopID();
    e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
}
