#include "editor/graph/eval_view.h"
#include "editor/panels/shader_inline.h"
#include "editor/ui/labels.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/math_nodes.h"
#include "graph/shader/shader_graph.h"
#include "support/math.h"

#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

static int ed_link_in_rect(const editor *e, const edge *ed, ImVec2 min, ImVec2 max);

#include <math.h>
#include <stdint.h>
#include <stdio.h>

const char *ed_pin_type_name(const pin_spec *spec)
{
    if (spec->type_name && spec->type_name[0]) {
        return spec->type_name;
    }
    if (spec->kind == PIN_EXEC) {
        return "exec";
    }
    const char *name = pin_type_name(spec->type);
    return name ? name : "unknown";
}

ImVec2 ed_world_to_screen(const editor *e, float x, float y)
{
    return ed_add(e->canvas_min, ed_add(e->pan, ed_scale((ImVec2){ x, y }, e->zoom)));
}

ImVec2 ed_screen_to_world(const editor *e, ImVec2 p)
{
    return ed_scale(ed_sub(ed_sub(p, e->canvas_min), e->pan), 1.0f / e->zoom);
}

static ed_node_view *ed_view_for(const editor *e, const node *n)
{
    for (int i = 0; i < acount(e->views); i++) {
        if (e->views[i].n == n) {
            return &e->views[i];
        }
    }
    return NULL;
}

static ed_pin_view *ed_pin_for(const editor *e, const node *n, int kind, int index)
{
    for (int i = 0; i < acount(e->pins); i++) {
        ed_pin_view *p = &e->pins[i];
        if (p->n == n && p->kind == kind && p->index == index) {
            return p;
        }
    }
    return NULL;
}

int ed_edge_index(const editor *e, const ed_link_ref *link)
{
    for (int i = 0; i < acount(e->active_graph->edges); i++) {
        const edge *ed = &e->active_graph->edges[i];
        if (ed->from_id == link->from_id && ed->from_pin_id == link->from_pin_id &&
            ed->to_id == link->to_id && ed->to_pin_id == link->to_pin_id) {
            return i;
        }
    }
    return -1;
}

static void ed_grow_views(editor *e, int nodes, int pins)
{
    asetcap(e->views, nodes);
    asetcap(e->pins, pins);
}

static int ed_input_connected(const graph *g, const node *n, int pin)
{
    node *src = NULL;
    int src_pin = 0;
    return graph_input_source(g, n, pin, &src, &src_pin);
}

bool ed_pin_visible(const graph *g, const node *n, int kind, int pin)
{
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    if (spec->flags & PIN_FLAG_SETTING) {
        return false;
    }
    return kind != ED_PIN_INPUT || n->show_advanced ||
           (n->desc->category && !strncmp(n->desc->category, "Shader", 6)) ||
           !(n->input_pins[pin].flags & PIN_FLAG_ADVANCED) || spec->binding ||
           ed_input_connected(g, n, pin);
}

static bool editable_output(const node *n)
{
    if (n->desc->category && !strncmp(n->desc->category, "Shader", 6)) {
        return !strcmp(n->desc->name, "Shader Constant") ||
               !strcmp(n->desc->name, "Shader Parameter");
    }
    return n->desc->pure_kind == NODE_PURE_CONST &&
           n->desc->display_class == NODE_DISPLAY_LITERAL;
}

int ed_pin_editable(const graph *g, const node *n, int kind, int pin)
{
    if (g->math_expression || !ed_pin_visible(g, n, kind, pin)) {
        return 0;
    }
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    node *source = NULL;
    int source_pin = -1;
    if (kind == ED_PIN_INPUT &&
        (spec->binding || graph_inline_source(g, n, pin, &source, &source_pin))) {
        return 1;
    }
    if (spec->kind != PIN_DATA || spec->field_source) {
        return 0;
    }
    return kind == ED_PIN_INPUT ? !ed_input_connected(g, n, pin)
                                : editable_output(n);
}

int ed_pin_rows(const editor *e, const graph *g, const node *n, int kind, int pin)
{
    if (!ed_pin_visible(g, n, kind, pin)) {
        return 0;
    }
    if (!ed_pin_editable(g, n, kind, pin)) {
        return 1;
    }
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    const char *context = NULL;
    node *source = NULL;
    int source_pin = -1;
    bool bound = kind == ED_PIN_INPUT &&
                 (spec->binding ||
                  graph_inline_source(g, n, pin, &source, &source_pin));
    int vector = ed_vector_width(spec);
    if (vector && !(spec->flags & PIN_FLAG_COLOR) &&
        (!bound || graph_node_is_vector_constructor(source))) {
        bool contextual = kind == ED_PIN_INPUT &&
                          shader_node_input_context(n, pin, &context) >= 0 && !source;
        return contextual && !spec->default_value.ptr ? 2 : 1 + vector + contextual;
    }
    bool choices = kind == ED_PIN_INPUT && spec->type == PIN_I32 &&
                   e->choices_resolver &&
                   e->choices_resolver(n, false, pin, e->choices_user);
    return choices || kind == ED_PIN_OUTPUT || ed_vector_width(spec) ||
                   spec->type == PIN_U32 || spec->type == PIN_ENUM ||
                   spec->type == PIN_STR || spec->type == PIN_NAME || spec->binding ||
                   graph_inline_source(g, n, pin, &source, &source_pin)
             ? 2
             : 1;
}

bool ed_node_has_advanced_inputs(const node *n)
{
    if (n->desc->category && !strncmp(n->desc->category, "Shader", 6)) {
        return false;
    }
    for (int p = 0; p < n->input_count; p++) {
        if (n->input_pins[p].flags & PIN_FLAG_ADVANCED) {
            return true;
        }
    }
    return false;
}

float ed_node_options_height(const node *n)
{
    return ed_shader_inline_height(n) +
           (ed_node_has_advanced_inputs(n) ? ed_row_height(n) : 0);
}

static int ed_exec_rows(const node *n)
{
    int inputs = 0, outputs = 0;
    for (int p = 0; p < n->input_count; p++) {
        inputs += n->input_pins[p].kind == PIN_EXEC;
    }
    for (int p = 0; p < n->output_count; p++) {
        outputs += n->output_pins[p].kind == PIN_EXEC;
    }
    return inputs > outputs ? inputs : outputs;
}

static int ed_data_rows(const editor *e, const graph *g, const node *n, int kind)
{
    const pin_spec *pins = kind == ED_PIN_INPUT ? n->input_pins : n->output_pins;
    int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
    int rows = 0;
    for (int p = 0; p < count; p++) {
        if (pins[p].kind != PIN_EXEC) {
            rows += ed_pin_rows(e, g, n, kind, p);
        }
    }
    return rows;
}

void ed_pin_label(const node *n, const pin_spec *pin, char *buf, size_t cap)
{
    editor_identifier_label(pin->name ? pin->name : "Pin", buf, cap);
    if (!pin->field_source) {
        return;
    }
    int aggregates = 0;
    const pin_spec *source = NULL;
    for (int i = 0; i < n->output_count; i++) {
        aggregates += graph_output_struct(n, i) != NULL;
        if (n->output_pins[i].id == pin->field_source) {
            source = &n->output_pins[i];
        }
    }
    if (aggregates > 1 && source) {
        char name[128], field[128];
        editor_identifier_label(source->name, name, sizeof name);
        editor_identifier_label(pin->name, field, sizeof field);
        snprintf(buf, cap, "%s %s", name, field);
    }
}

int ed_vector_width(const pin_spec *pin)
{
    return pin_vector_width(pin);
}

float ed_pin_value_width(const graph *g, const node *n, int kind, int pin)
{
    static const float binding_width = 128.0f;
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    bool control = ed_pin_editable(g, n, kind, pin) && spec->type != PIN_HANDLE;
    node *source = NULL;
    int source_pin = -1;
    if (kind == ED_PIN_INPUT &&
        (spec->binding || graph_inline_source(g, n, pin, &source, &source_pin))) {
        return binding_width;
    }
    int vector = ed_vector_width(spec);
    if (control && vector && !(spec->flags & PIN_FLAG_COLOR)) {
        float width = ED_VALUE_WIDTH;
        const char *context = NULL;
        if (kind == ED_PIN_INPUT) {
            shader_node_input_context(n, pin, &context);
        }
        return context ? ed_max(width, ImGui_CalcTextSize(context).x + 16) : width;
    }
    return control ? ED_VALUE_WIDTH : ED_PREVIEW_WIDTH;
}

static float ed_pin_text_width(const graph *g, const node *n, int kind, int pin)
{
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    char label[256];
    ed_pin_label(n, spec, label, sizeof label);
    return ImGui_CalcTextSize(label).x +
           (spec->kind == PIN_DATA ? ed_pin_value_width(g, n, kind, pin) + 16.0f : 0.0f);
}

int ed_field_output(const node *n)
{
    for (int i = 0; i < n->output_count; i++) {
        const type_desc *type = graph_output_struct(n, i);
        if (type && type->field_count > 1) {
            return i;
        }
    }
    return -1;
}

bool ed_fields_expanded(const node *n, int output)
{
    const pin_spec *source = &n->output_pins[output];
    const type_desc *type = registry_type_by_name(source->type_name);
    int count = 0;
    for (int i = 0; i < n->output_count; i++) {
        count += n->output_pins[i].field_source == source->id;
    }
    return type && count == type->field_count;
}

void ed_request_fields(editor *e, node *n, int output)
{
    e->fields_node = n->id;
    e->fields_pin = n->output_pins[output].id;
    e->fields_expand = !ed_fields_expanded(n, output);
}

void ed_draw_fields_menu(editor *e, node *n)
{
    for (int i = 0; i < n->output_count; i++) {
        const type_desc *type = graph_output_struct(n, i);
        if (!type || type->field_count < 2) {
            continue;
        }
        char name[128];
        char label[192];
        editor_identifier_label(n->output_pins[i].name, name, sizeof name);
        snprintf(
            label,
            sizeof label,
            "%s %s fields##%d",
            ed_fields_expanded(n, i) ? "Hide unused" : "Show",
            name,
            i
        );
        if (ImGui_MenuItem(label)) {
            ed_request_fields(e, n, i);
        }
        char item[96];
        snprintf(
            item,
            sizeof item,
            "fields.option.%llu.%d",
            (unsigned long long)n->id,
            i
        );
        editor_ui_track_item(item);
    }
}

float ed_row_height(const node *)
{
    /* Controls use the theme's full frame padding, not just the text baseline. */
    return ImGui_GetFrameHeight() + ED_NODE_ROW_EXTRA;
}

static ImVec2 ed_local_dimensions(const graph *g, const node *n, ImVec2 size)
{
    for (int p = 0; p < n->output_count; p++) {
        const graph_local *local = graph_output_local(g, n, p);
        if (local) {
            size.y += ImGui_GetTextLineHeight() + 4.0f;
            size.x = ed_max(size.x, ImGui_CalcTextSize(local->name).x + 60.0f);
        }
    }
    return size;
}

ImVec2 ed_node_dimensions(const editor *e, const graph *g, const node *n)
{
    float left = 0.0f;
    float right = 0.0f;
    for (int p = 0; p < n->input_count; p++) {
        if (!ed_pin_visible(g, n, ED_PIN_INPUT, p)) {
            continue;
        }
        left = ed_max(left, ed_pin_text_width(g, n, ED_PIN_INPUT, p));
    }
    for (int p = 0; p < n->output_count; p++) {
        right = ed_max(right, ed_pin_text_width(g, n, ED_PIN_OUTPUT, p));
    }

    char node_title[256];
    ImVec2 title_size = ImGui_CalcTextSize(ed_node_title(e, n, node_title, sizeof node_title));
    float width = ed_max(
                      title_size.x + ed_node_title_inset(n) +
                          (ed_field_output(n) >= 0 ? 24.0f : 0.0f) +
                          (ed_is_graph_call(n) ? 24.0f : 0.0f),
                      ed_max(left, right) + 28.0f
                  ) +
                  2.0f * ED_PAD;
    int rows = ed_exec_rows(n) + ed_data_rows(e, g, n, ED_PIN_OUTPUT) +
               ed_data_rows(e, g, n, ED_PIN_INPUT);
    if (rows > ed_exec_rows(n)) {
        width = ed_max(width, ed_max(ED_NODE_CONTROL_WIDTH, ed_shader_inline_width(n)) + 2.0f * ED_PAD);
    }
    float title_height = ImGui_GetTextLineHeight() + 2.0f * ed_node_title_padding(n);
    float height = title_height + 2.0f * ed_node_body_padding(n) +
                   rows * ed_row_height(n) + ed_node_options_height(n);
    return ed_local_dimensions(g, n, (ImVec2){ width, height });
}

void ed_build_layout(editor *e)
{
    int pin_total = 0;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        const node *n = e->active_graph->nodes[i];
        pin_total += n->input_count + n->output_count;
    }
    ed_grow_views(e, acount(e->active_graph->nodes), pin_total);

    const float line = ImGui_GetTextLineHeight();
    int pin_at = 0;
    int view_at = 0;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        if (graph_node_is_inline(e->active_graph, n)) {
            continue;
        }
        ImVec2 dimensions = ed_node_dimensions(e, e->active_graph, n);
        float title_height = line + 2.0f * ed_node_title_padding(n);
        float row_height = ed_row_height(n);
        ImVec2 min = ed_world_to_screen(e, n->x, n->y);

        ImVec2 max = ed_add(min, ed_scale(dimensions, e->zoom));

        ed_node_view *v = &e->views[view_at++];
        v->n = n;
        v->id = n->id;
        v->draw_frame = -1;
        v->drawn = (editor_node_ui){ 0 };
        v->min = min;
        v->max = max;
        v->title_height = title_height * e->zoom;
        v->input_first = pin_at;
        int exec_rows = ed_exec_rows(n);
        int input_row = exec_rows + ed_data_rows(e, e->active_graph, n, ED_PIN_OUTPUT);
        int exec_row = 0;
        for (int p = 0; p < n->input_count; p++) {
            bool exec = n->input_pins[p].kind == PIN_EXEC;
            int row = exec ? exec_row++ : input_row;
            float y = min.y + v->title_height +
                      (ed_node_body_padding(n) + (row + 0.5f) * row_height +
                       (exec ? 0 : ed_node_options_height(n))) *
                          e->zoom;
            if (!exec) {
                input_row += ed_pin_rows(e, e->active_graph, n, ED_PIN_INPUT, p);
            }
            e->pins[pin_at++] = (ed_pin_view){ n, ED_PIN_INPUT, p, { min.x, y } };
        }
        v->output_first = pin_at;
        int output_row = exec_rows;
        exec_row = 0;
        for (int p = 0; p < n->output_count; p++) {
            bool exec = n->output_pins[p].kind == PIN_EXEC;
            int row = exec ? exec_row++ : output_row;
            float y = min.y + v->title_height +
                      (ed_node_body_padding(n) + (row + 0.5f) * row_height) * e->zoom;
            if (!exec) {
                output_row += ed_pin_rows(e, e->active_graph, n, ED_PIN_OUTPUT, p);
            }
            e->pins[pin_at++] = (ed_pin_view){ n, ED_PIN_OUTPUT, p, { max.x, y } };
        }
    }
    asetlen(e->views, view_at);
    asetlen(e->pins, pin_at);
}

int ed_point_in_rect(ImVec2 p, ImVec2 min, ImVec2 max)
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

static int ed_rects_overlap(ImVec2 a_min, ImVec2 a_max, ImVec2 b_min, ImVec2 b_max)
{
    return a_min.x <= b_max.x && a_max.x >= b_min.x && a_min.y <= b_max.y &&
           a_max.y >= b_min.y;
}

void ed_select_marquee(editor *e)
{
    ImVec2 min = { e->select_start.x < e->select_end.x ? e->select_start.x
                                                       : e->select_end.x,
                   e->select_start.y < e->select_end.y ? e->select_start.y
                                                       : e->select_end.y };
    ImVec2 max = { e->select_start.x > e->select_end.x ? e->select_start.x
                                                       : e->select_end.x,
                   e->select_start.y > e->select_end.y ? e->select_start.y
                                                       : e->select_end.y };
    if (!e->select_additive) {
        aclear(e->selected);
        aclear(e->selected_links);
    }
    for (int i = 0; i < acount(e->views); i++) {
        if (ed_rects_overlap(min, max, e->views[i].min, e->views[i].max)) {
            ed_add_selected(e, e->views[i].n);
        }
    }
    for (int i = 0; i < acount(e->active_graph->edges); i++) {
        if (ed_link_in_rect(e, &e->active_graph->edges[i], min, max)) {
            ed_link_ref link = { e->active_graph->edges[i].from_id,
                                 e->active_graph->edges[i].from_pin_id,
                                 e->active_graph->edges[i].to_id,
                                 e->active_graph->edges[i].to_pin_id };
            ed_add_selected_link(e, &link);
        }
    }
}

ed_pin_view *ed_hit_pin(editor *e, ImVec2 p)
{
    float radius = ed_max(ED_PIN_RADIUS * e->zoom + 4.0f, 6.0f);
    for (int i = acount(e->pins) - 1; i >= 0; i--) {
        ed_pin_view *pin = &e->pins[i];
        if (graph_node_is_inline(e->active_graph, pin->n)) {
            continue;
        }
        if (!ed_pin_visible(e->active_graph, pin->n, pin->kind, pin->index)) {
            continue;
        }
        if (ed_abs(pin->pos.x - p.x) <= radius && ed_abs(pin->pos.y - p.y) <= radius) {
            return pin;
        }
    }
    return NULL;
}

ed_node_view *ed_hit_node(editor *e, ImVec2 p)
{
    for (int i = acount(e->views) - 1; i >= 0; i--) {
        ed_node_view *v = &e->views[i];
        if (graph_node_is_inline(e->active_graph, v->n)) {
            continue;
        }
        if (ed_point_in_rect(p, v->min, v->max)) {
            return v;
        }
    }
    return NULL;
}

static float ed_distance_to_segment(ImVec2 p, ImVec2 a, ImVec2 b)
{
    ImVec2 ab = ed_sub(b, a);
    ImVec2 ap = ed_sub(p, a);
    float denom = ab.x * ab.x + ab.y * ab.y;
    float t = denom > 0.0f ? (ap.x * ab.x + ap.y * ab.y) / denom : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    ImVec2 q = ed_add(a, ed_scale(ab, t));
    ImVec2 d = ed_sub(p, q);
    return sqrtf(d.x * d.x + d.y * d.y);
}

ImVec4 ed_lerp4(ImVec4 a, ImVec4 b, float t)
{
    return (ImVec4){ a.x + (b.x - a.x) * t,
                     a.y + (b.y - a.y) * t,
                     a.z + (b.z - a.z) * t,
                     a.w + (b.w - a.w) * t };
}

ImVec2 ed_bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t)
{
    float u = 1.0f - t;
    float a = u * u * u;
    float b = 3.0f * u * u * t;
    float c = 3.0f * u * t * t;
    float d = t * t * t;
    return (ImVec2){ a * p0.x + b * p1.x + c * p2.x + d * p3.x,
                     a * p0.y + b * p1.y + c * p2.y + d * p3.y };
}

float ed_distance(ImVec2 a, ImVec2 b)
{
    ImVec2 d = ed_sub(b, a);
    return sqrtf(d.x * d.x + d.y * d.y);
}

float ed_curve_length(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3)
{
    float length = 0.0f;
    ImVec2 prev = p0;
    for (int i = 1; i <= ED_FLOW_CURVE_SAMPLES; i++) {
        ImVec2 next = ed_bezier(p0, p1, p2, p3, (float)i / ED_FLOW_CURVE_SAMPLES);
        length += ed_distance(prev, next);
        prev = next;
    }
    return length;
}

ImVec2 ed_curve_at_distance(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float distance, float length)
{
    if (distance <= 0.0f) {
        return p0;
    }
    if (distance >= length) {
        return p3;
    }
    float travelled = 0.0f;
    ImVec2 prev = p0;
    for (int i = 1; i <= ED_FLOW_CURVE_SAMPLES; i++) {
        ImVec2 next = ed_bezier(p0, p1, p2, p3, (float)i / ED_FLOW_CURVE_SAMPLES);
        float segment = ed_distance(prev, next);
        if (segment > 0.0f && travelled + segment >= distance) {
            float t = (distance - travelled) / segment;
            return ed_add(prev, ed_scale(ed_sub(next, prev), t));
        }
        travelled += segment;
        prev = next;
    }
    return p3;
}

static float ed_clamp01(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float ed_flow_heat(double age, float *thickness, ImVec4 *color, ImVec4 base, int exec)
{
    static const ImVec4 highlight = { 1.0f, 1.0f, 1.0f, 1.0f };
    float delta = (float)age;
    float attack_ratio = ed_clamp01(1.0f - (delta - ED_FLOW_ATTACK_HOLD) / ED_FLOW_ATTACK_DECAY);
    float sustain_start = ED_FLOW_ATTACK_HOLD + ED_FLOW_ATTACK_DECAY + ED_FLOW_SUSTAIN_HOLD;
    float sustain_ratio = ed_clamp01(1.0f - (delta - sustain_start) / ED_FLOW_RELEASE);
    float attack_alpha = math_pow(attack_ratio, 1.8f);
    float sustain_alpha = math_pow(sustain_ratio, 1.4f);
    float attack_peak = exec ? ED_FLOW_ATTACK_THICKNESS : ED_FLOW_SUSTAIN_THICKNESS;
    float sustain_level = exec ? ED_FLOW_SUSTAIN_THICKNESS
                               : ED_FLOW_DATA_SUSTAIN_THICKNESS;
    float attack_thickness = sustain_level + (attack_peak - sustain_level) * attack_alpha;
    *thickness = ED_FLOW_RELEASE_THICKNESS +
                 (attack_thickness - ED_FLOW_RELEASE_THICKNESS) * sustain_alpha;
    float brightness = (exec ? 0.55f : 0.30f) * (0.5f + 0.5f * attack_alpha) *
                       sustain_alpha;
    *color = ed_lerp4(base, highlight, brightness);
    return sustain_alpha;
}

int ed_link_curve(const editor *e, const edge *ed, ImVec2 *p0, ImVec2 *p1, ImVec2 *p2, ImVec2 *p3)
{
    if (graph_edge_is_local(e->active_graph, ed)) {
        return 0;
    }
    node *from_node = graph_node_by_id(e->active_graph, ed->from_id);
    node *to_node = graph_node_by_id(e->active_graph, ed->to_id);
    if ((from_node && graph_node_is_inline(e->active_graph, from_node)) ||
        (to_node && graph_node_is_inline(e->active_graph, to_node))) {
        return 0;
    }
    int from_pin = -1, to_pin = -1, fo = 0, to_out = 0;
    if (!from_node ||
        !graph_pin_by_id(from_node, ed->from_pin_id, &fo, &from_pin) || !fo) {
        return 0;
    }
    if (!to_node || !graph_pin_by_id(to_node, ed->to_pin_id, &to_out, &to_pin) || to_out) {
        return 0;
    }
    ed_pin_view *from = ed_pin_for(e, from_node, ED_PIN_OUTPUT, from_pin);
    ed_pin_view *to = ed_pin_for(e, to_node, ED_PIN_INPUT, to_pin);
    if (!from || !to) {
        return 0;
    }
    *p0 = from->pos;
    *p3 = to->pos;
    float bend = ed_max(40.0f * e->zoom, ed_abs(to->pos.x - from->pos.x) * 0.5f);
    *p1 = ed_add(
        *p0,
        ed->custom_handles
            ? (ImVec2){ ed->from_handle_x * e->zoom, ed->from_handle_y * e->zoom }
            : (ImVec2){ bend, 0.0f }
    );
    *p2 = ed_add(
        *p3,
        ed->custom_handles
            ? (ImVec2){ ed->to_handle_x * e->zoom, ed->to_handle_y * e->zoom }
            : (ImVec2){ -bend, 0.0f }
    );
    return 1;
}

int ed_hit_handle(const editor *e, ImVec2 mouse, int *edge_index, int *side)
{
    for (int i = acount(e->active_graph->edges) - 1; i >= 0; i--) {
        const edge *ed = &e->active_graph->edges[i];
        ImVec2 p0, p1, p2, p3;
        if (!ed_link_curve(e, ed, &p0, &p1, &p2, &p3) || !ed->reroute_count) {
            continue;
        }
        float radius = ed_max(8.0f, 6.0f * e->zoom);
        for (int point = 0; point < ed->reroute_count; point++) {
            ImVec2 at = ed_world_to_screen(
                e,
                ed->reroutes[point].x,
                ed->reroutes[point].y
            );
            if (ed_distance(at, mouse) <= radius) {
                *edge_index = i;
                *side = point;
                return 1;
            }
        }
    }
    return 0;
}

static ImVec2 ed_route_point(const editor *e, const edge *ed, int index, ImVec2 start, ImVec2 end)
{
    if (index == 0) {
        return start;
    }
    if (index == ed->reroute_count + 1) {
        return end;
    }
    return ed_world_to_screen(
        e,
        ed->reroutes[index - 1].x,
        ed->reroutes[index - 1].y
    );
}

static float ed_route_tangent(const editor *e, const edge *ed, int index, ImVec2 start, ImVec2 end)
{
    float span;
    if (index == 0) {
        span = ed_abs(ed_route_point(e, ed, 1, start, end).x - start.x);
    } else if (index == ed->reroute_count + 1) {
        span = ed_abs(end.x - ed_route_point(e, ed, index - 1, start, end).x);
    } else {
        ImVec2 at = ed_route_point(e, ed, index, start, end);
        float before = ed_abs(
            at.x - ed_route_point(e, ed, index - 1, start, end).x
        );
        float after = ed_abs(ed_route_point(e, ed, index + 1, start, end).x - at.x);
        span = ed_min(before, after);
    }
    float direction = end.x >= start.x ? 1.0f : -1.0f;
    /* Keep control points inside short horizontal spans.  A fixed minimum
     * handle otherwise crosses the reroute knot and produces a visible
     * back-turn right at the attached pin. */
    return direction * ed_min(40.0f * e->zoom, span * 0.5f);
}

void ed_route_segment(
    const editor *e,
    const edge *ed,
    int segment,
    ImVec2 start,
    ImVec2 end,
    ImVec2 *p0,
    ImVec2 *p1,
    ImVec2 *p2,
    ImVec2 *p3
)
{
    *p0 = ed_route_point(e, ed, segment, start, end);
    *p3 = ed_route_point(e, ed, segment + 1, start, end);
    float from_tangent = ed_route_tangent(e, ed, segment, start, end);
    float to_tangent = ed_route_tangent(e, ed, segment + 1, start, end);
    *p1 = ed_add(*p0, (ImVec2){ from_tangent, 0.0f });
    *p2 = ed_sub(*p3, (ImVec2){ to_tangent, 0.0f });
}

int ed_hit_link_segment(const editor *e, ImVec2 p, ed_link_ref *out, int *route_segment)
{
    for (int i = acount(e->active_graph->edges) - 1; i >= 0; i--) {
        const edge *ed = &e->active_graph->edges[i];
        ImVec2 p0, p1, p2, p3;
        if (!ed_link_curve(e, ed, &p0, &p1, &p2, &p3)) {
            continue;
        }
        if (ed->reroute_count) {
            for (int segment = 0; segment <= ed->reroute_count; segment++) {
                ImVec2 start, c1, c2, end;
                ed_route_segment(e, ed, segment, p0, p3, &start, &c1, &c2, &end);
                ImVec2 prev = start;
                for (int s = 1; s <= 16; s++) {
                    ImVec2 next = ed_bezier(start, c1, c2, end, (float)s / 16.0f);
                    if (ed_distance_to_segment(p, prev, next) <=
                        ed_max(7.0f, 5.0f * e->zoom)) {
                        *out = (ed_link_ref){ ed->from_id,
                                              ed->from_pin_id,
                                              ed->to_id,
                                              ed->to_pin_id };
                        *route_segment = segment;
                        return 1;
                    }
                    prev = next;
                }
            }
            continue;
        }
        ImVec2 prev = p0;
        for (int s = 1; s <= 24; s++) {
            ImVec2 next = ed_bezier(p0, p1, p2, p3, (float)s / 24.0f);
            if (ed_distance_to_segment(p, prev, next) <= ed_max(7.0f, 5.0f * e->zoom)) {
                *out = (ed_link_ref){ ed->from_id, ed->from_pin_id, ed->to_id, ed->to_pin_id };
                *route_segment = 0;
                return 1;
            }
            prev = next;
        }
    }
    return 0;
}

int ed_hit_link(const editor *e, ImVec2 p, ed_link_ref *out)
{
    int segment;
    return ed_hit_link_segment(e, p, out, &segment);
}

static int ed_link_in_rect(const editor *e, const edge *ed, ImVec2 min, ImVec2 max)
{
    ImVec2 p0, p1, p2, p3;
    if (!ed_link_curve(e, ed, &p0, &p1, &p2, &p3)) {
        return 0;
    }
    int segment_count = ed->reroute_count ? ed->reroute_count + 1 : 1;
    for (int segment = 0; segment < segment_count; segment++) {
        ImVec2 start = p0, c1 = p1, c2 = p2, end = p3;
        if (ed->reroute_count) {
            ed_route_segment(e, ed, segment, p0, p3, &start, &c1, &c2, &end);
        }
        ImVec2 prev = start;
        for (int i = 1; i <= 24; i++) {
            ImVec2 next = ed_bezier(start, c1, c2, end, (float)i / 24.0f);
            ImVec2 a = { ed_min(prev.x, next.x), ed_min(prev.y, next.y) };
            ImVec2 b = { ed_max(prev.x, next.x), ed_max(prev.y, next.y) };
            if (ed_rects_overlap(min, max, a, b)) {
                return 1;
            }
            prev = next;
        }
    }
    return 0;
}

void ed_update_hover(editor *e, ImVec2 mouse)
{
    ed_node_view *view = ed_hit_node(e, mouse);
    e->hovered_node = view ? view->n : NULL;
    e->has_hovered_link = 0;
    if (!e->hovered_node) {
        ed_link_ref link;
        if (ed_hit_link(e, mouse, &link)) {
            e->hovered_link = link;
            e->has_hovered_link = 1;
        }
    }
}
