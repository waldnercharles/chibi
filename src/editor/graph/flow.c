#include "editor/graph/flow_visual.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

static const flow_run *ed_flow_run_for_display(const editor *e);

#include <math.h>
#include <stdint.h>

bool ed_inspection_blocked(const editor *e)
{
    return e->inspection_blocked ||
           (e->inspection_instance && (e->edit_pending || e->pending_snapshot));
}

const behavior_instance *ed_display_instance(const editor *e)
{
    return ed_inspection_blocked(e)
             ? NULL
             : (e->inspection_instance ? e->inspection_instance : e->instance);
}

static int ed_flow_edge_matches(const flow_traversal *record, const edge *ed)
{
    return record->kind == ed->kind && record->from_id == ed->from_id &&
           record->from_pin_id == ed->from_pin_id &&
           record->to_id == ed->to_id && record->to_pin_id == ed->to_pin_id;
}

const flow_traversal *ed_latest_flow_record(const editor *e, const edge *ed)
{
    if (ed_inspection_blocked(e)) {
        return NULL;
    }
    const flow_traversal *latest = NULL;
    const flow_run *run = ed_flow_run_for_display(e);
    if (run) {
        int count = 0;
        const flow_traversal *records = flow_run_traversals(run, &count);
        for (int i = count - 1; i >= 0; i--) {
            if (ed_flow_edge_matches(&records[i], ed) &&
                (!latest || records[i].timestamp > latest->timestamp)) {
                latest = &records[i];
            }
        }
    }
    if (!e->inspection_instance) {
        return latest;
    }
    for (int i = e->inspection_trace_count - 1; i >= 0; i--) {
        const flow_traversal *record = &e->inspection_trace[i].traversal;
        if (ed_flow_edge_matches(record, ed) &&
            (!latest || record->timestamp > latest->timestamp)) {
            latest = record;
        }
    }
    return latest;
}

typedef struct ed_flow_style
{
    float thickness;
    float bubble_radius;
    float opacity;
    float bubble_opacity;
    ImVec4 wire_color;
    ImVec4 bubble_color;
} ed_flow_style;

static const flow_run *ed_flow_run_for_display(const editor *e)
{
    if (ed_inspection_blocked(e)) {
        return NULL;
    }
    return e->inspection_instance ? e->inspection_run : e->activation;
}

static int ed_execution_pulse_active(const editor *e)
{
    /* Whole-graph feedback is only for hosts without an execution trace. */
    return !ed_inspection_blocked(e) && !e->inspection_instance &&
           !ed_flow_run_for_display(e) && e->execution_pulse_active &&
           e->active_graph &&
           e->execution_pulse_graph_id == e->active_graph->graph_id;
}

static ed_flow_style ed_flow_style_for_edge(const editor *e, const edge *ed, ImVec4 base, int selected, int hovered)
{
    float resting = selected ? ED_FLOW_SELECTED_THICKNESS
                             : (hovered ? ED_FLOW_HOVERED_THICKNESS
                                        : (ed->kind == GRAPH_EDGE_EXEC
                                               ? ED_FLOW_RESTING_EXEC_THICKNESS
                                               : ED_FLOW_RESTING_DATA_THICKNESS));
    float scale = ed_max(ED_FLOW_MIN_ZOOM_SCALE, ed_min(ED_FLOW_MAX_ZOOM_SCALE, e->zoom));
    ed_flow_style style = {
        .thickness = ed_max(1.0f, resting * scale),
        .bubble_radius = ED_FLOW_DOT_RADIUS * scale,
        .opacity = 0.0f,
        .bubble_opacity = 0.0f,
        .wire_color = base,
        .bubble_color = ed_lerp4(base, (ImVec4){ 1.0f, 1.0f, 1.0f, 1.0f }, ED_FLOW_BUBBLE_COLOR_BLEND),
    };
    if (ed->kind != GRAPH_EDGE_EXEC) {
        return style;
    }
    const flow_traversal *latest = ed_latest_flow_record(e, ed);
    if (!latest && !ed_execution_pulse_active(e)) {
        return style;
    }
    double age = latest ? e->monotonic_time - latest->timestamp
                        : e->monotonic_time - e->execution_pulse_time;
    if (age < 0.0) {
        age = 0.0;
    }
    float hot_thickness;
    ImVec4 hot_color;
    style.opacity = ed_flow_heat(age, &hot_thickness, &hot_color, base, ed->kind == GRAPH_EDGE_EXEC);
    if (style.opacity <= 0.0f) {
        style.opacity = 0.0f;
        return style;
    }
    style.thickness = ed_max(1.0f, ed_max(resting, hot_thickness) * scale);
    style.wire_color = ed_lerp4(base, hot_color, style.opacity);
    style.bubble_color = hot_color;
    float size_ratio = hot_thickness / ED_FLOW_ATTACK_THICKNESS;
    style.bubble_radius = ED_FLOW_DOT_RADIUS * scale *
                          (ED_FLOW_BUBBLE_BASE_WEIGHT +
                           ED_FLOW_BUBBLE_HEAT_WEIGHT * size_ratio);
    style.bubble_radius = ed_min(
        ED_FLOW_MAX_BUBBLE_RADIUS * scale,
        ed_max(style.bubble_radius, style.thickness * ED_FLOW_BUBBLE_MIN_LINE_RATIO)
    );
    style.bubble_radius *= style.opacity;
    style.bubble_opacity = 1.0f;
    return style;
}

void editor_query_edge_activity(const editor *e, const edge *edge, editor_edge_activity *out)
{
    assert(e && edge && out);
    const flow_traversal *record = ed_latest_flow_record(e, edge);
    ed_flow_style style = ed_flow_style_for_edge(e, edge, (ImVec4){ 1, 1, 1, 1 }, false, false);
    *out = (editor_edge_activity){
        .traversed = record != NULL,
        .timestamp = record ? record->timestamp : 0,
        .opacity = style.opacity,
        .bubble_opacity = style.bubble_opacity,
    };
}

/* UE draws a time-phased bubble stream for the latest traced edge, rather
 * than spawning a particle for every execution. Keep that distinction here. */
static void ed_draw_flow_markers(
    const editor *e,
    ImDrawList *dl,
    ImVec2 p0,
    ImVec2 p1,
    ImVec2 p2,
    ImVec2 p3,
    ImVec4 color,
    float radius,
    float opacity,
    double age
)
{
    if (opacity <= 0.0f) {
        return;
    }
    float length = ed_curve_length(p0, p1, p2, p3);
    float spacing = ED_FLOW_DOT_SPACING * e->zoom;
    if (length <= 0.0f || spacing <= 0.0f) {
        return;
    }
    float canvas_length = length / e->zoom;
    float offset = (float)(flow_visual_marker(age, canvas_length) *
                           canvas_length * e->zoom);
    offset = fmodf(offset, spacing);
    if (offset < 0.0f) {
        offset += spacing;
    }
    int bubble_count = (int)ceilf(length / spacing);
    color.w *= opacity;
    for (int i = 0; i < bubble_count; i++) {
        float distance = (float)i * spacing + offset;
        if (distance >= length) {
            continue;
        }
        ImVec2 position = ed_curve_at_distance(p0, p1, p2, p3, distance, length);
        ImDrawList_AddCircleFilled(dl, position, radius, ed_color(color), 12);
    }
}

static void ed_draw_reroute_markers(
    const editor *e,
    ImDrawList *dl,
    const edge *ed,
    ImVec2 p0,
    ImVec2 p3,
    ImVec4 color,
    float radius,
    float opacity,
    double age
)
{
    if (opacity <= 0.0f) {
        return;
    }
    ImVec2 *points = NULL;
    int routes = ed_route_count(e, ed);
    for (int segment = 0; segment <= routes; segment++) {
        ImVec2 start, c1, c2, end;
        ed_route_segment(e, ed, segment, p0, p3, &start, &c1, &c2, &end);
        for (int sample = segment ? 1 : 0; sample <= 16; sample++) {
            apush(points, ed_bezier(start, c1, c2, end, (float)sample / 16.0f));
        }
    }
    int count = acount(points);
    float length = 0.0f;
    for (int i = 1; i < count; i++) {
        length += ed_distance(points[i - 1], points[i]);
    }
    float spacing = ED_FLOW_DOT_SPACING * e->zoom;
    if (length <= 0.0f || spacing <= 0.0f) {
        afree(points);
        return;
    }
    float offset = fmodf((float)(flow_visual_marker(age, length / e->zoom) * length), spacing);
    if (offset < 0.0f) {
        offset += spacing;
    }
    color.w *= opacity;
    for (float distance = offset; distance < length; distance += spacing) {
        float walked = 0.0f;
        for (int i = 1; i < count; i++) {
            float segment = ed_distance(points[i - 1], points[i]);
            if (walked + segment >= distance) {
                float t = segment > 0.0f ? (distance - walked) / segment : 0.0f;
                ImVec2 position = ed_add(
                    points[i - 1],
                    ed_scale(ed_sub(points[i], points[i - 1]), t)
                );
                ImDrawList_AddCircleFilled(dl, position, radius, ed_color(color), 12);
                break;
            }
            walked += segment;
        }
    }
    afree(points);
}

void ed_draw_link(editor *e, ImDrawList *dl, const edge *ed)
{
    ImVec2 p0, p1, p2, p3;
    if (!ed_link_curve(e, ed, &p0, &p1, &p2, &p3)) {
        return;
    }
    ed_link_ref ref = { ed->from_id, ed->from_pin_id, ed->to_id, ed->to_pin_id };
    int selected = ed_link_selected(e, &ref);
    node *from_node = graph_node_by_id(e->active_graph, ed->from_id);
    int from_pin = -1, fo = 0;
    const pin_spec *source = NULL;
    if (from_node && graph_pin_by_id(from_node, ed->from_pin_id, &fo, &from_pin) && fo) {
        source = &from_node->output_pins[from_pin];
    }
    ImVec4 base = ed->conversion && ed->conversion->output_count == 1
                    ? ed_spec_color(&ed->conversion->outputs[0])
                    : (source ? ed_spec_color(source) : ed_pin_color(PIN_FLOW));
    int hovered = e->has_hovered_link && ed_link_equal(&ref, &e->hovered_link);
    ImVec4 edge_color = base;
    if (selected) {
        edge_color = (ImVec4){ 1.0f, 0.82f, 0.39f, 1.0f };
    } else if (hovered) {
        edge_color = (ImVec4){ 0.85f, 0.92f, 1.0f, 1.0f };
    }
    ImU32 color = ed_color(edge_color);
    ed_flow_style style = ed_flow_style_for_edge(e, ed, base, selected, hovered);
    if (selected || hovered) {
        style.bubble_color = edge_color;
    } else if (style.opacity > 0.0f) {
        color = ed_color(style.wire_color);
    }
    int routes = ed_route_count(e, ed);
    if (!routes) {
        ImDrawList_AddBezierCubic(dl, p0, p1, p2, p3, color, style.thickness, 0);
    } else {
        for (int segment = 0; segment <= routes; segment++) {
            ImVec2 start, c1, c2, end;
            ed_route_segment(e, ed, segment, p0, p3, &start, &c1, &c2, &end);
            ImDrawList_AddBezierCubic(dl, start, c1, c2, end, color, style.thickness, 0);
        }
    }
    if (selected || hovered) {
        ImU32 guide = IM_COL32(190, 205, 230, 150);
        ImU32 knob = selected ? IM_COL32(255, 190, 70, 255)
                              : IM_COL32(180, 215, 255, 255);
        for (int point = 0; point < ed->reroute_count; point++) {
            ImVec2 at = ed_world_to_screen(
                e,
                ed->reroutes[point].x,
                ed->reroutes[point].y
            );
            ImDrawList_AddCircleFilled(dl, at, ed_max(4.0f, 5.0f * e->zoom), knob, 12);
        }
    }
    /* The record controls whether this edge is active, but not marker phase.
     * Held input replaces the latest timestamp every frame, which would reset
     * the bubbles to the same point. The editor clock keeps the stream moving. */
    double marker_age = !editor_ui_reduced_motion() &&
                                (ed_latest_flow_record(e, ed) ||
                                 ed_execution_pulse_active(e))
                          ? e->monotonic_time
                          : 0.0;
    if (routes) {
        ed_draw_reroute_markers(
            e,
            dl,
            ed,
            p0,
            p3,
            style.bubble_color,
            style.bubble_radius,
            style.bubble_opacity,
            marker_age
        );
    } else {
        ed_draw_flow_markers(
            e,
            dl,
            p0,
            p1,
            p2,
            p3,
            style.bubble_color,
            style.bubble_radius,
            style.bubble_opacity,
            marker_age
        );
    }
}

bool editor_query_node_ui(const editor *e, uint64_t node_id, editor_node_ui *out)
{
    *out = (editor_node_ui){ 0 };
    for (int i = 0; i < acount(e->views); i++) {
        const ed_node_view *v = &e->views[i];
        if (v->draw_frame == ImGui_GetFrameCount() && v->id == node_id) {
            *out = v->drawn;
            return out->exec_pins + out->data_pins > 0;
        }
    }
    return false;
}

bool editor_query_pin_ui(const editor *e, uint64_t pin_id, editor_pin_ui *out)
{
    for (int i = 0; i < acount(e->pins); i++) {
        const ed_pin_view *pin = &e->pins[i];
        if (!ed_pin_visible(e->active_graph, pin->n, pin->kind, pin->index)) {
            continue;
        }
        const pin_spec *spec = pin->kind == ED_PIN_OUTPUT
                                 ? &pin->n->output_pins[pin->index]
                                 : &pin->n->input_pins[pin->index];
        editor_node_ui drawn;
        if (spec->id == pin_id && editor_query_node_ui(e, pin->n->id, &drawn) &&
            ed_point_in_rect(pin->pos, e->canvas_min, ed_add(e->canvas_min, e->canvas_size))) {
            *out = (editor_pin_ui){ pin->pos.x, pin->pos.y };
            return true;
        }
    }
    return false;
}
