#include "editor/panels/shader_inline.h"

#include "editor/panels/shader_details.h"
#include "editor/ui/color.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/shader/shader_graph.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint64_t inline_node_id;

const char *ed_shader_control_id(const char *id, char *buffer, size_t size)
{
    if (!inline_node_id) {
        return id;
    }
    snprintf(buffer, size, "shader.node.%llu.%s", (unsigned long long)inline_node_id, id);
    return buffer;
}

void ed_shader_track_item(const char *id)
{
    char scoped[1280];
    editor_ui_track_item(ed_shader_control_id(id, scoped, sizeof scoped));
}

static bool is_ramp(const node *n)
{
    return n && !strcmp(n->desc->name, "Shader Color Ramp");
}

bool ed_shader_graph(const editor *e)
{
    return e->asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
           e->asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
           e->asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
}

float ed_shader_inline_height(const node *n)
{
    int rows = 0;
    if (is_ramp(n)) {
        rows = 4;
    } else if (!strcmp(n->desc->name, "Shader Image Texture")) {
        const char *path = n->input_pins[SHADER_IMAGE_INPUT_IMAGE].default_value.str;
        rows = path && path[0] ? 6 : 4;
    } else if (!strcmp(n->desc->name, "Shader Gradient") || !strcmp(n->desc->name, "Shader Constant")) {
        rows = 1;
    } else if (!strcmp(n->desc->name, "Shader Parameter")) {
        rows = 2;
    }
    return rows ? rows * ed_row_height(n) + (is_ramp(n) ? 14.0f : 8.0f) : 0;
}

float ed_shader_inline_width(const node *n)
{
    return is_ramp(n) ? 280.0f : 0;
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

static void ramp_add_stop(editor *e, node *n, shader_ramp_stop *stops, int *count, int interpolation, float position)
{
    if (*count == SHADER_RAMP_MAX) {
        return;
    }
    int at = 0;
    while (at < *count && stops[at].position < position) {
        at++;
    }
    if (at < *count && stops[at].position == position) {
        select_ramp_stop(e, n, at);
        return;
    }
    shader_ramp_stop added = { .position = position };
    ramp_color(stops, *count, interpolation, position, added.color);
    ed_snapshot_before(e);
    memmove(&stops[at + 1], &stops[at], (size_t)(*count - at) * sizeof *stops);
    stops[at] = added;
    set_ramp(e, n, stops, ++*count);
    select_ramp_stop(e, n, at);
}

static void ramp_draw_strip(
    editor *e,
    node *n,
    shader_ramp_stop *stops,
    int *stop_count,
    int interpolation,
    ImVec2 start,
    float width,
    float height,
    float scale,
    const char *item_id,
    const char *button_id
)
{
    int count = *stop_count;
    bool insert = ImGui_GetIO()->ConfigMacOSXBehaviors ? ImGui_GetIO()->KeySuper
                                                       : ImGui_GetIO()->KeyCtrl;
    float pad = 5.0f * scale;
    float range = fmaxf(1.0f, width - 2.0f * pad);
    float tile = fmaxf(1.0f, 8.0f * scale);
    ImGui_SetCursorScreenPos(start);
    ImGuiButtonFlags buttons = ImGuiButtonFlags_EnableNav |
                               ImGuiButtonFlags_MouseButtonLeft;
    // ImGui maps physical Control-click to the secondary button on macOS.
    if (insert && ImGui_GetIO()->ConfigMacOSXBehaviors) {
        buttons |= ImGuiButtonFlags_MouseButtonRight;
    }
    ImGui_InvisibleButton(button_id, (ImVec2){ width, height + 14.0f * scale }, buttons);
    ed_shader_track_item(item_id);
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
        if (insert) {
            ramp_add_stop(e, n, stops, stop_count, interpolation, fminf(1, fmaxf(0, (x - pad) / range)));
            count = *stop_count;
        }
    }
    bool changed = false;
    if (ImGui_IsItemActive() && !insert && ImGui_IsMouseDragging(0, -1)) {
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
    ImDrawList_AddRectEx(
        draw,
        start,
        (ImVec2){ start.x + width, start.y + height },
        IM_COL32(16, 17, 19, 255),
        0,
        0,
        scale
    );
    int selected = ramp_selected(e, n, count);
    float active_x = start.x + pad + stops[selected].position * range;
    for (float y = 0; y < height; y += 4 * scale) {
        ImDrawList_AddLineEx(
            draw,
            (ImVec2){ active_x, start.y + y },
            (ImVec2){ active_x, start.y + fminf(height, y + 2 * scale) },
            IM_COL32(245, 245, 245, 255),
            scale
        );
    }
    // Draw the selected handle last so closely spaced stops remain distinguishable.
    for (int pass = 0; pass < count; pass++) {
        int i = pass == count - 1 ? selected
              : pass < selected   ? pass
                                  : pass + 1;
        float x = start.x + pad + stops[i].position * range;
        float y = start.y + height - 4 * scale;
        ImVec2 points[] = {
            { x, y },
            { x + pad, y + 5 * scale },
            { x + pad, y + 16 * scale },
            { x - pad, y + 16 * scale },
            { x - pad, y + 5 * scale },
        };
        ImDrawList_AddConvexPolyFilled(draw, points, 5, IM_COL32(22, 23, 26, 255));
        ImDrawList_AddPolyline(
            draw,
            points,
            5,
            i == selected ? IM_COL32(245, 245, 245, 255) : IM_COL32(145, 147, 152, 255),
            ImDrawFlags_Closed,
            scale
        );
        ImVec4 color = { stops[i].color[0], stops[i].color[1], stops[i].color[2], 1 };
        ImDrawList_AddRectFilled(
            draw,
            (ImVec2){ x - pad + 2 * scale, y + 7 * scale },
            (ImVec2){ x + pad - 2 * scale, y + 14 * scale },
            ImGui_ColorConvertFloat4ToU32(color)
        );
    }
}

static void ramp_setting_choice(editor *e, node *n, const char *label, const char *const *names, int count)
{
    int selected = n->input_pins[2].default_value.i;
    const char *current = selected >= 0 && selected < count ? names[selected] : "Invalid";
    bool open = ImGui_BeginCombo(label, current, 0);
    ed_shader_track_item("shader.setting.2");
    if (!open) {
        return;
    }
    for (int i = 0; i < count; i++) {
        bool chosen = ImGui_SelectableEx(names[i], selected == i, 0, (ImVec2){ 0 });
        char id[80];
        snprintf(id, sizeof id, "shader.setting.2.%d", i);
        ed_shader_track_item(id);
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
    int *count,
    int interpolation,
    ImVec2 start,
    float width,
    float strip_height
)
{
    float scale = inline_node_id ? e->zoom : 1.0f;
    ramp_draw_strip(e, n, stops, count, interpolation, start, width, strip_height, scale, "shader.ramp.strip", "##ramp");
    int selected = ramp_selected(e, n, *count);
    ImGui_SetNextItemWidth(width * .28f);
    if (ImGui_DragIntEx("##stop", &selected, .1f, 0, *count - 1, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        select_ramp_stop(e, n, selected);
    }
    ed_shader_track_item("shader.ramp.stop");
    ImGui_SetItemTooltip("Active color stop");
    e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    ImGui_SameLine();
    float position = stops[selected].position;
    float min = selected ? nextafterf(stops[selected - 1].position, 1) : 0;
    float max = selected + 1 < *count ? nextafterf(stops[selected + 1].position, 0)
                                      : 1;
    ImGui_SetNextItemWidth(ImGui_GetContentRegionAvail().x);
    bool changed = editor_ui_slider_float("##position", "Pos", &position, min, max);
    ed_shader_track_item("shader.ramp.position");
    e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    if (changed) {
        ed_details_begin_mutation(e);
        select_ramp_stop(e, n, selected);
        stops[selected].position = fminf(max, fmaxf(min, position));
        set_ramp(e, n, stops, *count);
    }
    ed_details_finish_mutation(e, changed);
    bool first_change;
    char color_id[192];
    ImGui_SetNextItemWidth(width);
    if (editor_color_draw_float(
            &e->color_state,
            ed_shader_control_id("shader.ramp.color", color_id, sizeof color_id),
            stops[selected].color,
            4,
            &first_change
        )) {
        if (first_change) {
            ed_snapshot_before(e);
        }
        select_ramp_stop(e, n, selected);
        set_ramp(e, n, stops, *count);
    }
    e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
}

static bool ramp_state(node *n, shader_ramp_stop stops[SHADER_RAMP_MAX], int *count)
{
    *count = shader_node_ramp_get(n, stops);
    if (*count < 2 || !shader_ramp_valid(stops, *count)) {
        return false;
    }
    return true;
}

static void ramp_tools(editor *e, node *n, shader_ramp_stop *stops, int *count)
{
    if (!ImGui_BeginPopup("ramp.tools", 0)) {
        return;
    }
    const char *const labels[] = { "Flip Color Ramp",
                                   "Distribute Stops Evenly",
                                   "Distribute Stops from Left",
                                   "Reset Color Ramp" };
    const char *const ids[] = { "shader.ramp.flip",
                                "shader.ramp.even",
                                "shader.ramp.left",
                                "shader.ramp.reset" };
    for (int action = 0; action < 4; action++) {
        bool chosen = ImGui_MenuItem(labels[action]);
        ed_shader_track_item(ids[action]);
        if (!chosen) {
            continue;
        }
        shader_ramp_stop updated[SHADER_RAMP_MAX];
        memcpy(updated, stops, (size_t)*count * sizeof *stops);
        int selected = ramp_selected(e, n, *count);
        int updated_count = *count;
        switch (action) {
            case 0:
                for (int i = 0; i < *count; i++) {
                    updated[i] = stops[*count - i - 1];
                    updated[i].position = 1 - updated[i].position;
                }
                for (int i = *count - 2; i >= 0; i--) {
                    if (updated[i].position >= updated[i + 1].position) {
                        updated[i].position = nextafterf(updated[i + 1].position, 0);
                    }
                }
                selected = *count - selected - 1;
                break;
            case 1:
            case 2:
                for (int i = 0; i < *count; i++) {
                    updated[i].position = (float)i / (*count - (action == 1));
                }
                break;
            case 3:
                updated[0] = (shader_ramp_stop){ 0, { 0, 0, 0, 1 } };
                updated[1] = (shader_ramp_stop){ 1, { 1, 1, 1, 1 } };
                updated_count = 2;
                selected = 0;
                break;
        }
        bool reset_interpolation = action == 3 &&
                                   n->input_pins[2].default_value.i != 0;
        if (*count != updated_count ||
            memcmp(stops, updated, (size_t)*count * sizeof *stops) ||
            reset_interpolation) {
            ed_snapshot_before(e);
            *count = updated_count;
            memcpy(stops, updated, (size_t)*count * sizeof *stops);
            set_ramp(e, n, stops, *count);
            if (reset_interpolation) {
                graph_set_pin_default(e->active_graph, n, 0, 2, &(pin_value){ .type = PIN_I32, .i = 0 });
            }
        }
        select_ramp_stop(e, n, selected);
    }
    ImGui_EndPopup();
}

void ed_draw_shader_ramp_controls(editor *e, node *n)
{
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    int count;
    if (!ramp_state(n, stops, &count)) {
        return;
    }
    float width = fmaxf(1, ImGui_GetContentRegionAvail().x);
    float button_width = ImGui_GetFrameHeight();
    int selected = ramp_selected(e, n, count);
    int interpolation = n->input_pins[2].default_value.i;
    ImGui_PushStyleColorImVec4(ImGuiCol_Button, (ImVec4){ 0 });
    ImGui_BeginDisabled(count == SHADER_RAMP_MAX);
    if (ImGui_ButtonEx("+", (ImVec2){ button_width, button_width })) {
        int gap = selected ? selected - 1 : 0;
        ramp_add_stop(
            e,
            n,
            stops,
            &count,
            interpolation,
            (stops[gap].position + stops[gap + 1].position) * .5f
        );
    }
    ed_shader_track_item("shader.ramp.add");
    ImGui_SetItemTooltip("Add color stop");
    ImGui_EndDisabled();
    ImGui_SameLine();
    ImGui_BeginDisabled(count == 2);
    if (ImGui_ButtonEx("−", (ImVec2){ button_width, button_width })) {
        ed_snapshot_before(e);
        memmove(&stops[selected], &stops[selected + 1], (size_t)(count - selected - 1) * sizeof *stops);
        set_ramp(e, n, stops, --count);
        select_ramp_stop(e, n, selected < count ? selected : count - 1);
    }
    ed_shader_track_item("shader.ramp.remove");
    ImGui_SetItemTooltip("Remove color stop");
    ImGui_EndDisabled();
    ImGui_SameLine();
    if (ImGui_ArrowButton("##tools", ImGuiDir_Down)) {
        ImGui_OpenPopup("ramp.tools", 0);
    }
    ed_shader_track_item("shader.ramp.tools");
    ImGui_SetItemTooltip("Color ramp tools");
    ImGui_PopStyleColor();
    ramp_tools(e, n, stops, &count);
    ImGui_SameLine();
    ImGui_AlignTextToFramePadding();
    ImGui_TextUnformatted("RGB");
    ImGui_SetItemTooltip("RGB color interpolation");
    ImGui_SameLine();
    ImGui_SetNextItemWidth(ImGui_GetContentRegionAvail().x);
    const char *const names[] = { "Linear", "Constant", "Ease" };
    ramp_setting_choice(e, n, "##interpolation", names, 3);
    interpolation = n->input_pins[2].default_value.i;
    ramp_common_controls(e, n, stops, &count, interpolation, ImGui_GetCursorScreenPos(), width, ImGui_GetFrameHeight());
}

void ed_draw_shader_inline(editor *e, node *n, ImVec2 min, ImVec2 max)
{
    float height = ed_shader_inline_height(n);
    if (!height || !ed_shader_graph(e)) {
        return;
    }
    int rows = 0;
    for (int p = 0; p < n->output_count; p++) {
        rows += ed_pin_rows(e, e->active_graph, n, ED_PIN_OUTPUT, p);
    }
    if (n->output_count == 1 && ed_pin_editable(e->active_graph, n, ED_PIN_OUTPUT, 0)) {
        rows = 1;
    }
    float padding = ED_PAD * e->zoom;
    float top = min.y + (ImGui_GetTextLineHeight() + 2 * ed_node_title_padding(n) +
                         ed_node_body_padding(n) + rows * ed_row_height(n)) *
                            e->zoom;
    ImGui_SetCursorScreenPos((ImVec2){ min.x + padding, top });
    ImGui_PushIDPtr(n);
    ImGui_PushFontFloat(NULL, ImGui_GetFontSize() * e->zoom);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 0 });
    ImGui_PushStyleVarImVec2(
        ImGuiStyleVar_FramePadding,
        ed_scale(ImGui_GetStyle()->FramePadding, e->zoom)
    );
    ImGui_PushStyleVarImVec2(
        ImGuiStyleVar_ItemSpacing,
        ed_scale(ImGui_GetStyle()->ItemSpacing, e->zoom)
    );
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowMinSize, (ImVec2){ 1, 1 });
    if (ImGui_BeginChild(
            "shader.options",
            (ImVec2){ max.x - min.x - 2 * padding, height * e->zoom },
            0,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings
        )) {
        ImGui_PushItemWidth(-1);
        assert(!inline_node_id);
        inline_node_id = n->id;
        ed_draw_shader_controls(e, n);
        inline_node_id = 0;
        ImGui_PopItemWidth();
        e->controls_hovered |= ImGui_IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    }
    ImGui_EndChild();
    ImGui_PopStyleVarEx(4);
    ImGui_PopFont();
    ImGui_PopID();
}
