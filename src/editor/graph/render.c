#include "editor/graph/eval_view.h"
#include "editor/panels/shader_inline.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <float.h>
#include <imgui/dcimgui_internal.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int ed_node_dragged(const editor *e, const node *n)
{
    for (int i = 0; i < acount(e->drag_nodes); i++) {
        if (e->drag_nodes[i].n == n) {
            return 1;
        }
    }
    return 0;
}

static int ed_node_on_top(const editor *e, const node *n)
{
    return ed_node_dragged(e, n) || ed_node_selected(e, n) || e->front_node == n;
}

static float ed_input_editor_column(const graph *g, const node *n)
{
    if (ed_node_appearance(n->desc).symbol) {
        return ED_PIN_RADIUS + 9.0f;
    }

    float width = 0.0f;
    for (int p = 0; p < n->input_count; p++) {
        if (!ed_pin_visible(g, n, ED_PIN_INPUT, p)) {
            continue;
        }
        if (!ed_pin_editable(g, n, ED_PIN_INPUT, p)) {
            continue;
        }
        char label[256];
        ed_pin_label(n, &n->input_pins[p], label, sizeof label);
        width = ed_max(width, ImGui_CalcTextSize(label).x);
    }
    /* ed_node_dimensions already reserves this widest label plus ED_VALUE_WIDTH. */
    return ED_PIN_RADIUS + 9.0f + width;
}

static void ed_draw_controls_for_node(editor *e, ed_node_view *view)
{
    node *n = view->n;
    if (graph_node_is_inline(e->active_graph, n)) {
        return;
    }
    float input_column = ed_input_editor_column(e->active_graph, n) * e->zoom;
    for (int p = 0; p < n->input_count; p++) {
        bool inline_source = ed_inline_source_for(e, n, p, NULL, NULL);
        if (!ed_pin_editable(e->active_graph, n, ED_PIN_INPUT, p) && !inline_source) {
            continue;
        }
        ed_pin_view *pin = &e->pins[view->input_first + p];
        ImVec2 pos = ed_add(
            pin->pos,
            (ImVec2){ input_column, -ImGui_GetFrameHeight() * e->zoom * 0.5f }
        );
        if (!inline_source) {
            ed_begin_value_edit(e, n, ED_PIN_INPUT, p, pos);
        }
        ed_draw_value_editor(e, n, ED_PIN_INPUT, p, pos);
        char id[96];
        snprintf(id, sizeof id, "graph.input.%llu.%d", (unsigned long long)n->id, p);
        editor_ui_track_item(id);
        if (ImGui_IsItemClicked()) {
            ed_select_node(e, n, ed_selection_modifier(ImGui_GetIO()));
        }
        e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    }
    for (int p = 0; p < n->output_count; p++) {
        if (!ed_pin_editable(e->active_graph, n, ED_PIN_OUTPUT, p)) {
            continue;
        }
        ed_pin_view *pin = &e->pins[view->output_first + p];
        char pin_label[256];
        ed_pin_label(n, &n->output_pins[p], pin_label, sizeof pin_label);
        ImVec2 label = ed_text_size(e, pin_label);
        ImVec2 pos = { pin->pos.x - (ED_PIN_RADIUS + 9.0f) * e->zoom - label.x -
                           ed_pin_value_width(e->active_graph, n, ED_PIN_OUTPUT, p) *
                               e->zoom,
                       pin->pos.y - ImGui_GetFrameHeight() * e->zoom * 0.5f };
        ed_begin_value_edit(e, n, ED_PIN_OUTPUT, p, pos);
        ed_draw_value_editor(e, n, ED_PIN_OUTPUT, p, pos);
        if (ImGui_IsItemClicked()) {
            ed_select_node(e, n, ed_selection_modifier(ImGui_GetIO()));
        }
        e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    }
    ed_draw_shader_inline(e, n, view->min, view->max);
}

void ed_draw_controls(editor *e, int on_top)
{
    ImGui_PushClipRect(e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    for (int i = 0; i < acount(e->views); i++) {
        if (ed_node_on_top(e, e->views[i].n) == on_top) {
            ed_draw_controls_for_node(e, &e->views[i]);
        }
    }
    ImGui_PopClipRect();
}

static void ed_draw_grid(editor *e, ImDrawList *dl)
{
    ImDrawList_AddRectFilled(
        dl,
        e->canvas_min,
        ed_add(e->canvas_min, e->canvas_size),
        IM_COL32(23, 24, 27, 255)
    );
    float spacing = ED_GRID_SPACING * e->zoom;
    if (spacing < 8.0f) {
        return;
    }
    float start_x = e->canvas_min.x + fmodf(e->pan.x, spacing);
    float start_y = e->canvas_min.y + fmodf(e->pan.y, spacing);
    for (float x = start_x; x <= e->canvas_min.x + e->canvas_size.x; x += spacing) {
        for (float y = start_y; y <= e->canvas_min.y + e->canvas_size.y; y += spacing) {
            ImDrawList_AddCircleFilled(dl, (ImVec2){ x, y }, 1.0f, IM_COL32(57, 59, 65, 255), 4);
        }
    }
}

static bool ed_pin_connected(const graph *g, const ed_pin_view *pin)
{
    const node *n = pin->n;
    bool output = pin->kind == ED_PIN_OUTPUT;
    uint64_t id = output ? n->output_pins[pin->index].id
                         : n->input_pins[pin->index].id;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *connection = &g->edges[i];
        if (output ? connection->from_id == n->id && connection->from_pin_id == id
                   : connection->to_id == n->id && connection->to_pin_id == id) {
            return true;
        }
    }
    return false;
}

static void ed_draw_pin(const editor *e, ImDrawList *dl, const ed_pin_view *pin, const pin_spec *spec)
{
    float radius = ed_max(2.0f, ED_PIN_RADIUS * e->zoom);
    float stroke = ed_max(1.0f, 1.8f * e->zoom);
    ImU32 color = ed_color(ed_spec_color(spec));
    bool connected = ed_pin_connected(e->active_graph, pin);
    ImU32 fill = connected ? color : IM_COL32(29, 32, 39, 255);
    if (spec->kind == PIN_EXEC) {
        ImVec2 points[] = {
            { pin->pos.x - radius * 0.7f, pin->pos.y - radius },
            { pin->pos.x + radius * 0.15f, pin->pos.y - radius },
            { pin->pos.x + radius, pin->pos.y },
            { pin->pos.x + radius * 0.15f, pin->pos.y + radius },
            { pin->pos.x - radius * 0.7f, pin->pos.y + radius },
        };
        ImDrawList_AddConvexPolyFilled(dl, points, 5, fill);
        ImDrawList_AddPolyline(dl, points, 5, color, ImDrawFlags_Closed, stroke);
        return;
    }
    ImDrawList_AddCircleFilled(dl, pin->pos, radius, fill, 0);
    ImDrawList_AddCircleEx(dl, pin->pos, radius, color, 0, stroke);
}

static void ed_count_drawn_pin(
    ed_node_view *view,
    const ed_pin_view *pin,
    const pin_spec *spec,
    ImDrawList *dl
)
{
    if (!ed_point_in_rect(pin->pos, ImDrawList_GetClipRectMin(dl), ImDrawList_GetClipRectMax(dl))) {
        return;
    }
    if (spec->kind == PIN_EXEC) {
        view->drawn.exec_pins++;
    } else {
        view->drawn.data_pins++;
    }
}

static void ed_draw_field_disclosure(editor *e, ImDrawList *dl, const ed_node_view *v)
{
    node *n = v->n;
    int output = ed_field_output(n);
    if (output < 0 || e->zoom < 0.5f) {
        return;
    }
    float size = 18.0f * e->zoom;
    ImVec2 center = { v->max.x - 13.0f * e->zoom, v->min.y + v->title_height * 0.5f };
    ImVec2 cursor = ImGui_GetCursorScreenPos();
    ImGui_SetCursorScreenPos((ImVec2){ center.x - size * 0.5f, center.y - size * 0.5f });
    char id[64];
    snprintf(id, sizeof id, "fields.%llu", (unsigned long long)n->id);
    int field_outputs = 0;
    for (int i = 0; i < n->output_count; i++) {
        const type_desc *type = graph_output_struct(n, i);
        field_outputs += type && type->field_count > 1;
    }
    bool expanded = ed_fields_expanded(n, output);
    if (ImGui_InvisibleButton(id, (ImVec2){ size, size }, 0)) {
        if (field_outputs > 1) {
            ImGui_OpenPopup(id, 0);
        } else {
            ed_request_fields(e, n, output);
        }
    }
    editor_ui_track_item(id);
    bool hovered = ImGui_IsItemHovered(0);
    e->controls_hovered |= hovered || ImGui_IsItemActive();
    if (ImGui_BeginPopup(id, 0)) {
        ed_draw_fields_menu(e, n);
        ImGui_EndPopup();
    }
    float r = 3.5f * e->zoom;
    ImU32 color = hovered ? IM_COL32(235, 240, 250, 255)
                          : IM_COL32(157, 165, 178, 255);
    ImVec2 a = expanded ? (ImVec2){ center.x - r, center.y - r * 0.5f }
                        : (ImVec2){ center.x - r * 0.5f, center.y - r };
    ImVec2 b = expanded ? (ImVec2){ center.x + r, center.y - r * 0.5f }
                        : (ImVec2){ center.x - r * 0.5f, center.y + r };
    ImVec2 c = expanded ? (ImVec2){ center.x, center.y + r }
                        : (ImVec2){ center.x + r, center.y };
    ImDrawList_AddTriangleFilled(dl, a, b, c, color);
    ImGui_SetCursorScreenPos(cursor);
}

static void ed_draw_call_open(editor *e, ImDrawList *dl, const ed_node_view *v)
{
    node *n = v->n;
    if (!ed_is_graph_call(n) || e->zoom < 0.5f) {
        return;
    }
    graph *target = behavior_asset_find_graph(e->asset, n->reference_id);
    bool available = target && target != e->active_graph;
    float size = 18.0f * e->zoom;
    float inset = ed_field_output(n) >= 0 ? 37.0f : 13.0f;
    ImVec2 center = { v->max.x - inset * e->zoom, v->min.y + v->title_height * 0.5f };
    ImVec2 cursor = ImGui_GetCursorScreenPos();
    ImGui_SetCursorScreenPos((ImVec2){ center.x - size * 0.5f, center.y - size * 0.5f });
    char id[64];
    snprintf(id, sizeof id, "graph.call.open.%llu", (unsigned long long)n->id);
    ImGui_BeginDisabled(!available);
    if (ImGui_InvisibleButton(id, (ImVec2){ size, size }, 0)) {
        e->open_call_node = n->id;
    }
    editor_ui_track_item(id);
    bool hovered = ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    e->controls_hovered |= hovered || ImGui_IsItemActive();
    ImGui_EndDisabled();
    ImU32 color = !available ? IM_COL32(100, 105, 115, 255)
                : hovered    ? IM_COL32(235, 240, 250, 255)
                             : IM_COL32(157, 165, 178, 255);
    ed_draw_centered_icon(
        dl,
        e->icon_font,
        13.0f * e->zoom,
        (ImVec2){ center.x - size * 0.5f, center.y - size * 0.5f },
        (ImVec2){ center.x + size * 0.5f, center.y + size * 0.5f },
        color,
        ED_GRAPH_OPEN_GLYPH
    );
    ImGui_SetCursorScreenPos(cursor);
}

typedef struct
{
    float min_x;
    float min_y;
    float max_x;
    float max_y;
} ed_symbol_bounds;

static unsigned ed_next_utf8(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    unsigned codepoint;
    if (s[0] < 0x80) {
        codepoint = s[0];
        *text = (const char *)(s + 1);
        return codepoint;
    }
    if ((s[0] & 0xe0) == 0xc0) {
        codepoint = ((unsigned)(s[0] & 0x1f) << 6) | (s[1] & 0x3f);
        *text = (const char *)(s + 2);
        return codepoint;
    }
    if ((s[0] & 0xf0) == 0xe0) {
        codepoint = ((unsigned)(s[0] & 0x0f) << 12) |
                    ((unsigned)(s[1] & 0x3f) << 6) | (s[2] & 0x3f);
        *text = (const char *)(s + 3);
        return codepoint;
    }
    assert((s[0] & 0xf8) == 0xf0);
    codepoint = ((unsigned)(s[0] & 0x07) << 18) | ((unsigned)(s[1] & 0x3f) << 12) |
                ((unsigned)(s[2] & 0x3f) << 6) | (s[3] & 0x3f);
    *text = (const char *)(s + 4);
    return codepoint;
}

static ed_symbol_bounds ed_math_symbol_bounds(ImFont *font, float size, const char *text)
{
    ImFontBaked *baked = ImFont_GetFontBaked(font, size);
    float scale = size / baked->Size;
    float cursor_x = 0.0f;
    ed_symbol_bounds bounds = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const char *at = text; *at;) {
        ImFontGlyph *glyph = ImFontBaked_FindGlyphNoFallback(baked, ed_next_utf8(&at));
        assert(glyph && glyph->Visible);
        bounds.min_x = ed_min(bounds.min_x, cursor_x + glyph->X0 * scale);
        bounds.min_y = ed_min(bounds.min_y, glyph->Y0 * scale);
        bounds.max_x = ed_max(bounds.max_x, cursor_x + glyph->X1 * scale);
        bounds.max_y = ed_max(bounds.max_y, glyph->Y1 * scale);
        cursor_x += glyph->AdvanceX * scale;
    }
    return bounds;
}

static void ed_draw_math_symbol(
    const editor *e,
    ImFont *font,
    ImDrawList *dl,
    ImVec2 min,
    ImVec2 max,
    const char *symbol
)
{
    assert(font);
    float size = (max.y - min.y) * 1.15f;
    float available_width = max.x - min.x - 16.0f * e->zoom;
    ed_symbol_bounds bounds = ed_math_symbol_bounds(font, size, symbol);
    float width = bounds.max_x - bounds.min_x;
    if (width > available_width) {
        size *= available_width / width;
        bounds = ed_math_symbol_bounds(font, size, symbol);
    }
    ImVec2 center = { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
    ImVec2 pos = { center.x - (bounds.min_x + bounds.max_x) * 0.5f,
                   center.y - (bounds.min_y + bounds.max_y) * 0.5f };
    ImU32 color = IM_COL32(210, 216, 228, 52);
    ImDrawList_AddTextImFontPtr(dl, font, size, pos, color, symbol);
}

static void ed_draw_node(editor *e, ImDrawList *dl, ed_node_view *v)
{
    v->draw_frame = ImGui_GetFrameCount();
    v->drawn = (editor_node_ui){ 0 };
    node *n = v->n;
    if (graph_node_is_inline(e->active_graph, n)) {
        return;
    }
    int selected = ed_node_selected(e, n);
    ed_node_presentation appearance = ed_node_appearance(n->desc);
    v->drawn.title_x = (v->min.x + v->max.x) * 0.5f;
    v->drawn.title_y = v->min.y + v->title_height * 0.5f;
    v->drawn.left = v->min.x;
    v->drawn.right = v->max.x;
    v->drawn.title_bottom = v->min.y + v->title_height;
    v->drawn.compact = appearance.compact;
    v->drawn.top = v->min.y;
    v->drawn.bottom = v->max.y;
    v->drawn.selected = selected;
    ImU32 bg = ed_color(ED_NODE_BODY);
    ImU32 border = ed_color(ED_NODE_BORDER);
    ImU32 title = ed_color(ed_node_title_color(n));
    int hovered = e->hovered_node == n;
    if (hovered) {
        bg = ed_color((ImVec4){ 0.25f, 0.28f, 0.34f, 1.0f });
    }
    if (selected) {
        bg = ed_color((ImVec4){ 0.24f, 0.31f, 0.43f, 1.0f });
        border = ed_color((ImVec4){ 0.35f, 0.75f, 1.0f, 1.0f });
    }
    if (n->disabled) {
        bg = ed_dim_color(bg, 0.55f);
        border = ed_dim_color(border, 0.65f);
        title = ed_dim_color(title, 0.60f);
    }
    ImDrawList_AddRectFilledEx(dl, v->min, v->max, bg, 5.0f * e->zoom, ImDrawFlags_RoundCornersAll);
    if (appearance.symbol) {
        ImDrawList_AddRectEx(
            dl,
            v->min,
            v->max,
            border,
            5.0f * e->zoom,
            ImDrawFlags_RoundCornersAll,
            (selected ? 2.5f : 1.0f) * e->zoom
        );
        ImVec2 center = { (v->min.x + v->max.x) * 0.5f, (v->min.y + v->max.y) * 0.5f };
        v->drawn.title_x = center.x;
        v->drawn.title_y = center.y;
        ed_draw_math_symbol(e, e->math_font, dl, v->min, v->max, appearance.symbol);
        for (int i = 0; i < n->input_count; i++) {
            ed_pin_view *pin = &e->pins[v->input_first + i];
            ed_count_drawn_pin(v, pin, &n->input_pins[i], dl);
            ed_draw_pin(e, dl, pin, &n->input_pins[i]);
        }
        for (int i = 0; i < n->output_count; i++) {
            ed_pin_view *pin = &e->pins[v->output_first + i];
            ed_count_drawn_pin(v, pin, &n->output_pins[i], dl);
            ed_draw_pin(e, dl, pin, &n->output_pins[i]);
        }
        return;
    }

    ImDrawList_AddRectFilledEx(
        dl,
        v->min,
        (ImVec2){ v->max.x, v->min.y + v->title_height },
        appearance.compact ? bg : IM_COL32(48, 50, 55, 255),
        5.0f * e->zoom,
        ImDrawFlags_RoundCornersTop
    );
    ImDrawList_AddLineEx(
        dl,
        (ImVec2){ v->min.x + 8.0f * e->zoom, v->min.y + v->title_height },
        (ImVec2){ v->max.x - 8.0f * e->zoom, v->min.y + v->title_height },
        title,
        2.0f * e->zoom
    );
    ImDrawList_AddRectEx(
        dl,
        v->min,
        v->max,
        border,
        5.0f * e->zoom,
        ImDrawFlags_RoundCornersAll,
        selected ? 2.5f * e->zoom : 1.0f
    );
    ImVec2 title_min = ed_add(v->min, (ImVec2){ ED_PAD * e->zoom, 0.0f });
    float inset = ed_node_title_inset(n) * e->zoom;
    ImVec2 icon_max = { title_min.x + inset - 6.0f * e->zoom,
                        title_min.y + v->title_height };
    if (appearance.symbol) {
        ed_draw_text(
            e,
            dl,
            ed_add(title_min, (ImVec2){ 0.0f, ed_node_title_padding(n) * e->zoom }),
            IM_COL32(235, 238, 245, 255),
            appearance.symbol
        );
    } else {
        ed_draw_centered_icon(
            dl,
            e->icon_font,
            12.0f * e->zoom,
            title_min,
            icon_max,
            IM_COL32(194, 203, 219, 255),
            appearance.icon
        );
    }
    char node_title[256];
    ed_draw_text(
        e,
        dl,
        ed_add(title_min, (ImVec2){ inset, ed_node_title_padding(n) * e->zoom }),
        IM_COL32(235, 238, 245, 255),
        ed_node_title(e, n, node_title, sizeof node_title)
    );

    ed_draw_field_disclosure(e, dl, v);
    ed_draw_call_open(e, dl, v);

    int rows = n->input_count > n->output_count ? n->input_count : n->output_count;
    for (int row = 0; row < rows; row++) {
        if (row < n->input_count &&
            ed_pin_visible(e->active_graph, n, ED_PIN_INPUT, row)) {
            const pin_spec *spec = &n->input_pins[row];
            char pin_label[256];
            ed_pin_label(n, spec, pin_label, sizeof pin_label);
            ed_pin_view *pin = &e->pins[v->input_first + row];
            ed_count_drawn_pin(v, pin, spec, dl);
            ed_draw_pin(e, dl, pin, spec);
            ed_draw_text(
                e,
                dl,
                ed_add(pin->pos, (ImVec2){ (ED_PIN_RADIUS + 5.0f) * e->zoom, -ed_font_size(e) * 0.5f }),
                IM_COL32(220, 224, 232, 255),
                pin_label
            );
            const char *value = eval_view_input(e->ev, e->active_graph, n, row);
            if (value && !ed_pin_editable(e->active_graph, n, ED_PIN_INPUT, row)) {
                ImVec2 label = ed_text_size(e, pin_label);
                ImVec2 at = ed_add(
                    pin->pos,
                    (ImVec2){ (ED_PIN_RADIUS + 9.0f) * e->zoom + label.x,
                              -ed_font_size(e) * 0.5f }
                );
                ImDrawList_PushClipRect(
                    dl,
                    at,
                    (ImVec2){ at.x + ed_pin_value_width(e->active_graph, n, ED_PIN_INPUT, row) *
                                         e->zoom,
                              at.y + ed_font_size(e) },
                    true
                );
                ed_draw_text(e, dl, at, IM_COL32(130, 205, 255, 255), value);
                ImDrawList_PopClipRect(dl);
                if (strcmp(value, "Self") == 0 &&
                    (spec->flags & PIN_FLAG_DEFAULT_SELF) &&
                    ed_point_in_rect(pin->pos, ImDrawList_GetClipRectMin(dl), ImDrawList_GetClipRectMax(dl))) {
                    v->drawn.self_labels++;
                }
            }
        }
        if (row < n->output_count) {
            const pin_spec *spec = &n->output_pins[row];
            char pin_label[256];
            ed_pin_label(n, spec, pin_label, sizeof pin_label);
            ed_pin_view *pin = &e->pins[v->output_first + row];
            ed_count_drawn_pin(v, pin, spec, dl);
            ImVec2 label = ed_text_size(e, pin_label);
            ed_draw_pin(e, dl, pin, spec);
            ed_draw_text(
                e,
                dl,
                (ImVec2){ pin->pos.x - (ED_PIN_RADIUS + 5.0f) * e->zoom - label.x,
                          pin->pos.y - ed_font_size(e) * 0.5f },
                IM_COL32(220, 224, 232, 255),
                pin_label
            );
            const char *value = eval_view_output(e->ev, n, row);
            if (value && value[0] &&
                !ed_pin_editable(e->active_graph, n, ED_PIN_OUTPUT, row)) {
                ImVec2 val = ed_text_size(e, value);
                float right = pin->pos.x - (ED_PIN_RADIUS + 9.0f) * e->zoom -
                              label.x;
                ImDrawList_PushClipRect(
                    dl,
                    (ImVec2){ right - ED_PREVIEW_WIDTH * e->zoom,
                              pin->pos.y - ed_font_size(e) * 0.5f },
                    (ImVec2){ right, pin->pos.y + ed_font_size(e) * 0.5f },
                    true
                );
                ed_draw_text(
                    e,
                    dl,
                    (ImVec2){ pin->pos.x - (ED_PIN_RADIUS + 9.0f) * e->zoom -
                                  label.x - ed_min(val.x, ED_PREVIEW_WIDTH * e->zoom),
                              pin->pos.y - ed_font_size(e) * 0.5f },
                    IM_COL32(130, 205, 255, 255),
                    value
                );
                ImDrawList_PopClipRect(dl);
            }
        }
    }
}

static void ed_draw_comment_boxes(editor *e, ImDrawList *dl)
{
    for (int i = 0; i < acount(e->active_graph->comment_boxes); i++) {
        const graph_comment_box *box = &e->active_graph->comment_boxes[i];
        ImVec2 min = ed_world_to_screen(e, box->x, box->y);
        ImVec2 max = ed_world_to_screen(e, box->x + box->width, box->y + box->height);
        ImU32 fill = (box->color & 0x00ffffffu) | 0x30000000u;
        ImDrawList_AddRectFilledEx(dl, min, max, fill, 4.0f, ImDrawFlags_RoundCornersAll);
        ImDrawList_AddRectEx(dl, min, max, box->color, 4.0f, ImDrawFlags_RoundCornersAll, 1.5f);
        ed_draw_text(e, dl, ed_add(min, (ImVec2){ 8.0f, 6.0f }), box->color, box->title ? box->title : "Comment");
    }
}

void ed_draw_canvas(editor *e)
{
    ImDrawList *dl = ImGui_GetWindowDrawList();
    ImDrawList_PushClipRect(dl, e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    ed_draw_grid(e, dl);
    ed_draw_comment_boxes(e, dl);
    for (int i = 0; i < acount(e->active_graph->edges); i++) {
        ed_draw_link(e, dl, &e->active_graph->edges[i]);
    }
    for (int i = 0; i < acount(e->views); i++) {
        if (!ed_node_on_top(e, e->views[i].n)) {
            ed_draw_node(e, dl, &e->views[i]);
        }
    }

    if (e->linking) {
        ImVec2 p0 = e->link_pin.pos;
        ImVec2 p3 = e->link_mouse;
        int output = e->link_pin.kind == ED_PIN_OUTPUT;
        float bend = ed_max(40.0f * e->zoom, ed_abs(p3.x - p0.x) * 0.5f);
        ImVec2 p1 = ed_add(p0, (ImVec2){ output ? bend : -bend, 0.0f });
        ImVec2 p2 = ed_add(p3, (ImVec2){ output ? -bend : bend, 0.0f });
        const pin_spec *spec = output
                                 ? &e->link_pin.n->output_pins[e->link_pin.index]
                                 : &e->link_pin.n->input_pins[e->link_pin.index];
        ImDrawList_AddBezierCubic(dl, p0, p1, p2, p3, ed_color(ed_spec_color(spec)), 3.0f, 0);
    }
    ImDrawList_PopClipRect(dl);
}

void ed_draw_front_nodes(editor *e)
{
    ImDrawList *dl = ImGui_GetWindowDrawList();
    ImDrawList_PushClipRect(dl, e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    for (int i = 0; i < acount(e->views); i++) {
        if (ed_node_on_top(e, e->views[i].n)) {
            ed_draw_node(e, dl, &e->views[i]);
        }
    }
    ImDrawList_PopClipRect(dl);
}

void ed_draw_selection_overlay(const editor *e)
{
    if (!e->selecting) {
        return;
    }
    ImVec2 min = { e->select_start.x < e->select_end.x ? e->select_start.x
                                                       : e->select_end.x,
                   e->select_start.y < e->select_end.y ? e->select_start.y
                                                       : e->select_end.y };
    ImVec2 max = { e->select_start.x > e->select_end.x ? e->select_start.x
                                                       : e->select_end.x,
                   e->select_start.y > e->select_end.y ? e->select_start.y
                                                       : e->select_end.y };
    ImDrawList *dl = ImGui_GetWindowDrawList();
    ImDrawList_PushClipRect(dl, e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    ImDrawList_AddRectFilled(dl, min, max, IM_COL32(100, 150, 230, 45));
    ImDrawList_AddRect(dl, min, max, IM_COL32(140, 190, 255, 220));
    ImDrawList_PopClipRect(dl);
}
