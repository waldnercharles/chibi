#include "editor.h"

#include "../core/nodes.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "eval_view.h"
#include "preview_gate.h"

#include <assert.h>
#include <dcimgui.h>

CIMGUI_API void ImGui_ClearActiveID(void);

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ED_HIST_MAX 64
#define ED_PAD 8.0f
#define ED_TITLE_PAD 6.0f
#define ED_PIN_RADIUS 4.0f
#define ED_GRID_SPACING 32.0f
#define ED_MIN_ZOOM 0.35f
#define ED_MAX_ZOOM 2.5f
#define ED_VALUE_WIDTH 86.0f
#define ED_TEXT_WIDTH 120.0f
#define ED_LOG_MAX 128
#define ED_LOG_MESSAGE 256

typedef enum ed_pin_kind
{
    ED_PIN_INPUT,
    ED_PIN_OUTPUT
} ed_pin_kind;

typedef struct ed_node_view
{
    node *n;
    ImVec2 min;
    ImVec2 max;
    float title_height;
    int input_first;
    int output_first;
} ed_node_view;

typedef struct ed_pin_view
{
    node *n;
    int kind;
    int index;
    ImVec2 pos;
} ed_pin_view;

typedef struct ed_drag_node
{
    node *n;
    float x;
    float y;
} ed_drag_node;

typedef struct ed_link_ref
{
    node *from;
    int from_pin;
    node *to;
    int to_pin;
} ed_link_ref;

typedef struct ed_log_entry
{
    const node *origin;
    double timestamp;
    unsigned long step;
    char message[ED_LOG_MESSAGE];
} ed_log_entry;

struct editor
{
    graph *g;
    eval_view *ev;
    flow_run *run;
    int entry_index;
    double monotonic_time;
    ed_log_entry log[ED_LOG_MAX];
    int log_count;
    double run_started_at;

    ImVec2 canvas_min;
    ImVec2 canvas_size;
    ImVec2 pan;
    float zoom;
    int view_initialized;
    node *reveal_target;

    ed_node_view *views;
    int view_count;
    int view_cap;
    ed_pin_view *pins;
    int pin_count;
    int pin_cap;

    node **selected;
    int selected_count;
    int selected_cap;
    ed_link_ref *selected_links;
    int selected_link_count;
    int selected_link_cap;

    int selecting;
    int select_additive;
    int panning;
    ImVec2 select_start;
    ImVec2 select_end;

    int dragging;
    int drag_toggle_selected;
    node *drag_toggle_node;
    ImVec2 drag_start_mouse;
    ed_drag_node *drag_nodes;
    int drag_count;
    int drag_cap;

    int linking;
    ed_pin_view link_pin;
    ImVec2 link_mouse;

    char *pending_snapshot;
    int edit_history_open;
    int edit_changed;
    char edit_text[256];
    node *edit_text_node;
    int edit_text_kind;
    int edit_text_index;
    char *clipboard;
    char search[64];
    int search_selected;
    float popup_x;
    float popup_y;
    node *context_node;
    int controls_hovered;
    int has_context_link;
    ed_link_ref context_link;
    node *hovered_node;
    node *front_node;
    ed_link_ref hovered_link;
    int has_hovered_link;
    int spawn_link_pending;
    ed_pin_view spawn_link_pin;
    char connect_error[ED_LOG_MESSAGE];
    ImVec2 connect_error_pos;
    node *details_node;
    int details_history_open;
    char details_title[256];
    char details_comment[512];
    char details_reference_name[256];
    char details_pin_name[256];

    char **undo;
    int undo_count;
    int undo_cap;
    char **redo;
    int redo_count;
    int redo_cap;
};

static ImVec2 ed_add(ImVec2 a, ImVec2 b)
{ return (ImVec2){ a.x + b.x, a.y + b.y }; }

static ImVec2 ed_sub(ImVec2 a, ImVec2 b)
{ return (ImVec2){ a.x - b.x, a.y - b.y }; }

static ImVec2 ed_scale(ImVec2 a, float s)
{ return (ImVec2){ a.x * s, a.y * s }; }

static float ed_max(float a, float b) { return a > b ? a : b; }

static float ed_abs(float a) { return a < 0.0f ? -a : a; }

/* Unreal-compatible palette. Flow uses one title color here even though Unreal
 * gives Branch a separate white title; variables will use their pin type in Phase 4. */
static const ImVec4 ED_PIN_COLORS[] = {
    [PIN_FLOW] = { 1.0f, 1.0f, 1.0f, 1.0f },
    [PIN_BOOL] = { 0.300f, 0.0f, 0.0f, 1.0f },
    [PIN_I8] = { 0.0f, 0.160f, 0.131270f, 1.0f },
    [PIN_U8] = { 0.0f, 0.160f, 0.131270f, 1.0f },
    [PIN_I16] = { 0.013575f, 0.770f, 0.429609f, 1.0f },
    [PIN_U16] = { 0.013575f, 0.770f, 0.429609f, 1.0f },
    [PIN_I32] = { 0.013575f, 0.770f, 0.429609f, 1.0f },
    [PIN_U32] = { 0.013575f, 0.770f, 0.429609f, 1.0f },
    [PIN_I64] = { 0.413575f, 0.770f, 0.429609f, 1.0f },
    [PIN_U64] = { 0.413575f, 0.770f, 0.429609f, 1.0f },
    [PIN_F32] = { 0.357667f, 1.0f, 0.060f, 1.0f },
    [PIN_F64] = { 0.357667f, 1.0f, 0.060f, 1.0f },
    [PIN_STR] = { 1.0f, 0.0f, 0.660537f, 1.0f },
    [PIN_STRUCT] = { 0.0f, 0.1f, 0.6f, 1.0f },
    [PIN_ENUM] = { 0.0f, 0.160f, 0.131270f, 1.0f },
};

static const ImVec4 ED_NODE_BODY = { 0.188f, 0.200f, 0.239f, 1.0f };
static const ImVec4 ED_NODE_BORDER = { 0.412f, 0.431f, 0.490f, 1.0f };
static const ImVec4 ED_NODE_TITLES[] = {
    [NODE_DISPLAY_PURE] = { 0.2f, 0.425f, 0.175f, 1.0f },
    [NODE_DISPLAY_EVENT] = { 0.8f, 0.1f, 0.1f, 1.0f },
    [NODE_DISPLAY_FUNCTION] = { 0.190525f, 0.583898f, 1.0f, 1.0f },
    [NODE_DISPLAY_FLOW] = { 0.8f, 0.4f, 0.4f, 1.0f },
    /* Unreal derives this from the variable's pin type; Phase 4 will do so too. */
    [NODE_DISPLAY_VARIABLE] = { 0.36f, 0.22f, 0.18f, 1.0f },
    [NODE_DISPLAY_LITERAL] = { 0.750f, 0.6f, 0.4f, 1.0f },
};

static ImVec4 ed_pin_color(pin_type t)
{
    return t >= PIN_FLOW && t <= PIN_ENUM ? ED_PIN_COLORS[t] : ED_PIN_COLORS[PIN_FLOW];
}

static ImVec4 ed_node_title_color(const node *n)
{
    node_display_class display = n && n->desc ? n->desc->display_class
                                              : NODE_DISPLAY_PURE;
    if (display < NODE_DISPLAY_PURE || display > NODE_DISPLAY_LITERAL)
        display = NODE_DISPLAY_PURE;
    return ED_NODE_TITLES[display];
}

static ImU32 ed_color(ImVec4 c)
{
    int r = (int)(c.x * 255.0f);
    int g = (int)(c.y * 255.0f);
    int b = (int)(c.z * 255.0f);
    int a = (int)(c.w * 255.0f);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return IM_COL32(r, g, b, a);
}

static ImU32 ed_dim_color(ImU32 c, float factor)
{
    int r = (int)((c & 255u) * factor);
    int g = (int)(((c >> 8) & 255u) * factor);
    int b = (int)(((c >> 16) & 255u) * factor);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return IM_COL32(r, g, b, (c >> 24) & 255u);
}

static void ed_free_string_stack(char **stack, int *count)
{
    for (int i = 0; i < *count; i++) free(stack[i]);
    *count = 0;
}

static void ed_push_string(char ***stack, int *count, int *cap, char *value)
{
    if (!value) return;
    if (*count == ED_HIST_MAX) {
        free((*stack)[0]);
        memmove(*stack, *stack + 1, (size_t)(ED_HIST_MAX - 1) * sizeof **stack);
        (*stack)[ED_HIST_MAX - 1] = value;
        return;
    }
    if (*count == *cap) {
        int next = *cap ? *cap * 2 : 16;
        if (next > ED_HIST_MAX) next = ED_HIST_MAX;
        char **grown = (char **)realloc(*stack, (size_t)next * sizeof *grown);
        if (!grown) {
            free(value);
            return;
        }
        *stack = grown;
        *cap = next;
    }
    (*stack)[(*count)++] = value;
}

static int ed_link_equal(const ed_link_ref *a, const ed_link_ref *b)
{
    return a->from == b->from && a->from_pin == b->from_pin && a->to == b->to &&
           a->to_pin == b->to_pin;
}

static void ed_clear_selection(editor *e)
{
    e->selected_count = 0;
    e->selected_link_count = 0;
}

static int ed_link_selected(const editor *e, const ed_link_ref *link)
{
    for (int i = 0; i < e->selected_link_count; i++)
        if (ed_link_equal(link, &e->selected_links[i])) return 1;
    return 0;
}

static int ed_add_selected_link(editor *e, const ed_link_ref *link)
{
    if (ed_link_selected(e, link)) return 0;
    if (e->selected_link_count == e->selected_link_cap) {
        int next = e->selected_link_cap ? e->selected_link_cap * 2 : 8;
        ed_link_ref *grown = (ed_link_ref *)
            realloc(e->selected_links, (size_t)next * sizeof *grown);
        if (!grown) return -1;
        e->selected_links = grown;
        e->selected_link_cap = next;
    }
    e->selected_links[e->selected_link_count++] = *link;
    return 0;
}

static void ed_remove_selected_link(editor *e, const ed_link_ref *link)
{
    for (int i = 0; i < e->selected_link_count; i++) {
        if (!ed_link_equal(link, &e->selected_links[i])) continue;
        e->selected_links[i] = e->selected_links[--e->selected_link_count];
        return;
    }
}

static int ed_node_selected(const editor *e, const node *n)
{
    for (int i = 0; i < e->selected_count; i++)
        if (e->selected[i] == n) return 1;
    return 0;
}

static int ed_add_selected(editor *e, node *n)
{
    if (ed_node_selected(e, n)) return 0;
    if (e->selected_count == e->selected_cap) {
        int next = e->selected_cap ? e->selected_cap * 2 : 8;
        node **grown = (node **)realloc(e->selected, (size_t)next * sizeof *grown);
        if (!grown) return -1;
        e->selected = grown;
        e->selected_cap = next;
    }
    e->selected[e->selected_count++] = n;
    return 0;
}

static void ed_remove_selected(editor *e, node *n)
{
    for (int i = 0; i < e->selected_count; i++) {
        if (e->selected[i] != n) continue;
        e->selected[i] = e->selected[--e->selected_count];
        return;
    }
}

static void ed_select_node(editor *e, node *n, int append)
{
    if (!append) {
        e->selected_count = 0;
        e->selected_link_count = 0;
    }
    if (n) {
        ed_add_selected(e, n);
        e->front_node = n;
    }
}

static void ed_center_initial_view(editor *e);

static void ed_log_sink(const node *origin, const char *message, void *user)
{
    editor *e = (editor *)user;
    if (!e) return;
    if (e->log_count == ED_LOG_MAX)
        memmove(e->log, e->log + 1, (size_t)(ED_LOG_MAX - 1) * sizeof *e->log);
    ed_log_entry *entry = &e->log[e->log_count < ED_LOG_MAX ? e->log_count++ : ED_LOG_MAX - 1];
    entry->origin = origin;
    entry->timestamp = e->monotonic_time - e->run_started_at;
    entry->step = e->run ? flow_run_steps(e->run) : 0;
    snprintf(entry->message, sizeof entry->message, "%s", message ? message : "");
}

void editor_select_node(editor *e, node *n)
{
    if (e) ed_select_node(e, n, 0);
}

void editor_reveal_node(editor *e, node *n)
{
    if (!e || !n) return;
    ed_select_node(e, n, 0);
    e->pan = ed_sub(ed_scale(e->canvas_size, 0.5f), ed_scale((ImVec2){ n->x, n->y }, e->zoom));
    e->reveal_target = n;
    e->view_initialized = 0;
}

static ImVec4 ed_spec_color(const pin_spec *spec)
{
    return spec->kind == PIN_EXEC ? (ImVec4){ 1.0f, 0.55f, 0.20f, 1.0f }
                                  : ed_pin_color(spec->type);
}

static ImVec2 ed_world_to_screen(const editor *e, float x, float y)
{
    return ed_add(e->canvas_min, ed_add(e->pan, ed_scale((ImVec2){ x, y }, e->zoom)));
}

static ImVec2 ed_screen_to_world(const editor *e, ImVec2 p)
{ return ed_scale(ed_sub(ed_sub(p, e->canvas_min), e->pan), 1.0f / e->zoom); }

static ed_node_view *ed_view_for(const editor *e, const node *n)
{
    for (int i = 0; i < e->view_count; i++)
        if (e->views[i].n == n) return &e->views[i];
    return NULL;
}

static ed_pin_view *ed_pin_for(const editor *e, const node *n, int kind, int index)
{
    for (int i = 0; i < e->pin_count; i++) {
        ed_pin_view *p = &e->pins[i];
        if (p->n == n && p->kind == kind && p->index == index) return p;
    }
    return NULL;
}

static int ed_edge_index(const editor *e, const ed_link_ref *link)
{
    for (int i = 0; i < e->g->edge_count; i++) {
        const edge *ed = &e->g->edges[i];
        if (ed->from == link->from && ed->from_pin == link->from_pin &&
            ed->to == link->to && ed->to_pin == link->to_pin)
            return i;
    }
    return -1;
}

static int ed_grow_views(editor *e, int nodes, int pins)
{
    if (nodes > e->view_cap) {
        int next = e->view_cap ? e->view_cap * 2 : 16;
        while (next < nodes) next *= 2;
        ed_node_view *grown = (ed_node_view *)realloc(e->views, (size_t)next * sizeof *grown);
        if (!grown) return -1;
        e->views = grown;
        e->view_cap = next;
    }
    if (pins > e->pin_cap) {
        int next = e->pin_cap ? e->pin_cap * 2 : 32;
        while (next < pins) next *= 2;
        ed_pin_view *grown = (ed_pin_view *)realloc(e->pins, (size_t)next * sizeof *grown);
        if (!grown) return -1;
        e->pins = grown;
        e->pin_cap = next;
    }
    return 0;
}

static int ed_input_connected(const graph *g, const node *n, int pin)
{
    node *src = NULL;
    int src_pin = 0;
    return graph_input_source(g, n, pin, &src, &src_pin);
}

static int ed_pin_editable(const graph *g, const node *n, int kind, int pin)
{
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    if (spec->kind != PIN_DATA) return 0;
    return kind == ED_PIN_INPUT ? !ed_input_connected(g, n, pin)
                                : n->desc->eval == node_eval_const;
}

static float ed_pin_text_width(const editor *e, const graph *g, const node *n, int kind, int pin)
{
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    ImVec2 label = ImGui_CalcTextSize(spec->name);
    if (ed_pin_editable(g, n, kind, pin))
        return label.x + ED_VALUE_WIDTH + 16.0f;

    const char *value = kind == ED_PIN_INPUT ? eval_view_input(e->ev, g, n, pin)
                                             : eval_view_output(e->ev, n, pin);
    if (value && value[0]) {
        ImVec2 val = ImGui_CalcTextSize(value);
        label.x += 4.0f + val.x;
    }
    return label.x;
}

static void ed_build_layout(editor *e)
{
    int pin_total = 0;
    for (int i = 0; i < e->g->node_count; i++) {
        const node *n = e->g->nodes[i];
        pin_total += n->input_count + n->output_count;
    }
    if (ed_grow_views(e, e->g->node_count, pin_total)) {
        e->view_count = 0;
        e->pin_count = 0;
        return;
    }

    const float line = ImGui_GetTextLineHeight();
    int pin_at = 0;
    for (int i = 0; i < e->g->node_count; i++) {
        node *n = e->g->nodes[i];
        const node_desc *d = n->desc;
        float left = 0.0f;
        float right = 0.0f;
        for (int p = 0; p < n->input_count; p++)
            left = ed_max(left, ed_pin_text_width(e, e->g, n, ED_PIN_INPUT, p));
        for (int p = 0; p < n->output_count; p++)
            right = ed_max(right, ed_pin_text_width(e, e->g, n, ED_PIN_OUTPUT, p));

        ImVec2 title = ImGui_CalcTextSize(n->title_override ? n->title_override : d->name);
        float width = ed_max(title.x, left + right + 28.0f) + 2.0f * ED_PAD;
        int rows = n->input_count > n->output_count ? n->input_count : n->output_count;
        float row_height = line + 6.0f;
        float title_height = line + 2.0f * ED_TITLE_PAD;
        float height = title_height + ED_PAD + rows * row_height + ED_PAD;
        ImVec2 min = ed_world_to_screen(e, n->x, n->y);

        ImVec2 max = ed_add(min, ed_scale((ImVec2){ width, height }, e->zoom));

        ed_node_view *v = &e->views[i];
        v->n = n;
        v->min = min;
        v->max = max;
        v->title_height = title_height * e->zoom;
        v->input_first = pin_at;
        for (int p = 0; p < n->input_count; p++) {
            float y = min.y + v->title_height +
                      (ED_PAD + (p + 0.5f) * row_height) * e->zoom;
            e->pins[pin_at++] = (ed_pin_view){ n, ED_PIN_INPUT, p, { min.x, y } };
        }
        v->output_first = pin_at;
        for (int p = 0; p < n->output_count; p++) {
            float y = min.y + v->title_height +
                      (ED_PAD + (p + 0.5f) * row_height) * e->zoom;
            e->pins[pin_at++] = (ed_pin_view){ n, ED_PIN_OUTPUT, p, { max.x, y } };
        }
    }
    e->view_count = e->g->node_count;
    e->pin_count = pin_at;
}

static int ed_point_in_rect(ImVec2 p, ImVec2 min, ImVec2 max)
{ return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y; }

static int ed_rects_overlap(ImVec2 a_min, ImVec2 a_max, ImVec2 b_min, ImVec2 b_max)
{
    return a_min.x <= b_max.x && a_max.x >= b_min.x && a_min.y <= b_max.y &&
           a_max.y >= b_min.y;
}

static int ed_link_in_rect(const editor *e, const edge *ed, ImVec2 min, ImVec2 max);

static void ed_select_marquee(editor *e)
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
        e->selected_count = 0;
        e->selected_link_count = 0;
    }
    for (int i = 0; i < e->view_count; i++)
        if (ed_rects_overlap(min, max, e->views[i].min, e->views[i].max))
            ed_add_selected(e, e->views[i].n);
    for (int i = 0; i < e->g->edge_count; i++)
        if (ed_link_in_rect(e, &e->g->edges[i], min, max)) {
            ed_link_ref link = { e->g->edges[i].from,
                                 e->g->edges[i].from_pin,
                                 e->g->edges[i].to,
                                 e->g->edges[i].to_pin };
            ed_add_selected_link(e, &link);
        }
}

static ed_pin_view *ed_hit_pin(editor *e, ImVec2 p)
{
    float radius = ed_max(ED_PIN_RADIUS * e->zoom + 4.0f, 6.0f);
    for (int i = e->pin_count - 1; i >= 0; i--) {
        ed_pin_view *pin = &e->pins[i];
        if (ed_abs(pin->pos.x - p.x) <= radius && ed_abs(pin->pos.y - p.y) <= radius)
            return pin;
    }
    return NULL;
}

static ed_node_view *ed_hit_node(editor *e, ImVec2 p)
{
    for (int i = e->view_count - 1; i >= 0; i--) {
        ed_node_view *v = &e->views[i];
        if (ed_point_in_rect(p, v->min, v->max)) return v;
    }
    return NULL;
}

static float ed_distance_to_segment(ImVec2 p, ImVec2 a, ImVec2 b)
{
    ImVec2 ab = ed_sub(b, a);
    ImVec2 ap = ed_sub(p, a);
    float denom = ab.x * ab.x + ab.y * ab.y;
    float t = denom > 0.0f ? (ap.x * ab.x + ap.y * ab.y) / denom : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    ImVec2 q = ed_add(a, ed_scale(ab, t));
    ImVec2 d = ed_sub(p, q);
    return sqrtf(d.x * d.x + d.y * d.y);
}

static ImVec2 ed_bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t)
{
    float u = 1.0f - t;
    float a = u * u * u;
    float b = 3.0f * u * u * t;
    float c = 3.0f * u * t * t;
    float d = t * t * t;
    return (ImVec2){ a * p0.x + b * p1.x + c * p2.x + d * p3.x,
                     a * p0.y + b * p1.y + c * p2.y + d * p3.y };
}

static int ed_link_curve(const editor *e, const edge *ed, ImVec2 *p0, ImVec2 *p1, ImVec2 *p2, ImVec2 *p3)
{
    ed_pin_view *from = ed_pin_for(e, ed->from, ED_PIN_OUTPUT, ed->from_pin);
    ed_pin_view *to = ed_pin_for(e, ed->to, ED_PIN_INPUT, ed->to_pin);
    if (!from || !to) return 0;
    *p0 = from->pos;
    *p3 = to->pos;
    float bend = ed_max(40.0f * e->zoom, ed_abs(to->pos.x - from->pos.x) * 0.5f);
    *p1 = ed_add(*p0, (ImVec2){ bend, 0.0f });
    *p2 = ed_add(*p3, (ImVec2){ -bend, 0.0f });
    return 1;
}

static int ed_hit_link(const editor *e, ImVec2 p, ed_link_ref *out)
{
    for (int i = e->g->edge_count - 1; i >= 0; i--) {
        const edge *ed = &e->g->edges[i];
        ImVec2 p0, p1, p2, p3;
        if (!ed_link_curve(e, ed, &p0, &p1, &p2, &p3)) continue;
        ImVec2 prev = p0;
        for (int s = 1; s <= 24; s++) {
            ImVec2 next = ed_bezier(p0, p1, p2, p3, (float)s / 24.0f);
            if (ed_distance_to_segment(p, prev, next) <= ed_max(7.0f, 5.0f * e->zoom)) {
                *out = (ed_link_ref){ ed->from, ed->from_pin, ed->to, ed->to_pin };
                return 1;
            }
            prev = next;
        }
    }
    return 0;
}

static int ed_link_in_rect(const editor *e, const edge *ed, ImVec2 min, ImVec2 max)
{
    ImVec2 p0, p1, p2, p3;
    if (!ed_link_curve(e, ed, &p0, &p1, &p2, &p3)) return 0;
    ImVec2 prev = p0;
    for (int i = 1; i <= 24; i++) {
        ImVec2 next = ed_bezier(p0, p1, p2, p3, (float)i / 24.0f);
        ImVec2 a = { prev.x < next.x ? prev.x : next.x,
                     prev.y < next.y ? prev.y : next.y };
        ImVec2 b = { prev.x > next.x ? prev.x : next.x,
                     prev.y > next.y ? prev.y : next.y };
        if (ed_rects_overlap(min, max, a, b)) return 1;
        prev = next;
    }
    return 0;
}

static void ed_update_hover(editor *e, ImVec2 mouse)
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

static void ed_begin_authoring_edit(editor *e);
static void ed_snapshot_before(editor *e);
static char *ed_snapshot(const editor *e);
static void ed_commit_snapshot(editor *e);
static void ed_cancel_snapshot(editor *e);
static void ed_draw_details(editor *e);

static float ed_font_size(const editor *e)
{ return ImGui_GetFontSize() * e->zoom; }

static ImVec2 ed_text_size(const editor *e, const char *text)
{ return ed_scale(ImGui_CalcTextSize(text), e->zoom); }

static void ed_draw_text(const editor *e, ImDrawList *dl, ImVec2 pos, ImU32 color, const char *text)
{
    float size = ed_font_size(e);
    ImDrawList_AddTextImFontPtr(
        dl,
        ImGui_GetFont(),
        size,
        ed_add(pos, (ImVec2){ 1.0f, 1.0f }),
        IM_COL32(0, 0, 0, 210),
        text
    );
    ImDrawList_AddTextImFontPtr(dl, ImGui_GetFont(), size, pos, color, text);
}

static const pin_value *ed_pin_authored(const node *n, int kind, int index)
{
    return kind == ED_PIN_INPUT ? &n->input_pins[index].default_value
                                : &n->output_pins[index].default_value;
}

static int ed_edit_hit(const editor *e, ImVec2 mouse, ImVec2 pos, float width)
{
    return ed_point_in_rect(
        mouse,
        pos,
        (ImVec2){ pos.x + width, pos.y + ed_font_size(e) + 8.0f * e->zoom }
    );
}

static void ed_begin_value_edit(editor *e, node *n, int kind, int index, ImVec2 pos)
{
    ImGuiIO *io = ImGui_GetIO();
    if (!e->edit_history_open && ImGui_IsMouseClicked(0) &&
        ed_edit_hit(e, io->MousePos, pos, ED_VALUE_WIDTH * e->zoom)) {
        ed_begin_authoring_edit(e);
        e->pending_snapshot = ed_snapshot(e);
        e->edit_history_open = 1;
        e->edit_changed = 0;
    }
}

static void ed_finish_value_edit(editor *e, int changed)
{
    if (changed) e->edit_changed = 1;
    if (e->edit_history_open && ImGui_IsItemDeactivated()) {
        if (e->edit_changed) ed_commit_snapshot(e);
        else ed_cancel_snapshot(e);
        e->edit_history_open = 0;
        e->edit_changed = 0;
    }
}

static int ed_draw_value_editor(editor *e, node *n, int kind, int index, ImVec2 pos)
{
    pin_value authored = *ed_pin_authored(n, kind, index);
    pin_value *v = &authored;
    int changed = 0;
    char id[32];
    snprintf(id, sizeof id, "##value_%d_%d", kind, index);
    ImGui_SetCursorScreenPos(pos);
    ImGui_PushFontFloat(NULL, ImGui_GetStyle()->FontSizeBase * e->zoom);
    ImGui_SetNextItemWidth(ED_VALUE_WIDTH * e->zoom);
    ImGui_PushStyleVarImVec2(
        ImGuiStyleVar_FramePadding,
        ed_scale(ImGui_GetStyle()->FramePadding, e->zoom)
    );
    ImGui_PushIDPtr(n);
    ImGui_PushIDInt(kind * 1000 + index);
    switch (v->type) {
        case PIN_BOOL: changed = ImGui_Checkbox(id, &v->b); break;
        case PIN_I8:
            changed = ImGui_InputScalar(id, ImGuiDataType_S8, &v->i8);
            break;
        case PIN_U8:
            changed = ImGui_InputScalar(id, ImGuiDataType_U8, &v->u8);
            break;
        case PIN_I16:
            changed = ImGui_InputScalar(id, ImGuiDataType_S16, &v->i16);
            break;
        case PIN_U16:
            changed = ImGui_InputScalar(id, ImGuiDataType_U16, &v->u16);
            break;
        case PIN_I32: changed = ImGui_InputInt(id, &v->i); break;
        case PIN_ENUM: changed = ImGui_InputInt(id, &v->e); break;
        case PIN_U32:
            changed = ImGui_InputScalar(id, ImGuiDataType_U32, &v->u);
            break;
        case PIN_I64:
            changed = ImGui_InputScalar(id, ImGuiDataType_S64, &v->i64);
            break;
        case PIN_U64:
            changed = ImGui_InputScalar(id, ImGuiDataType_U64, &v->u64);
            break;
        case PIN_F32:
            changed = ImGui_InputFloatEx(id, &v->f, 0.1f, 0.0f, "%.3f", 0);
            break;
        case PIN_F64:
            changed = ImGui_InputDoubleEx(id, &v->d, 0.1, 0.0, "%.3f", 0);
            break;
        case PIN_STR: {
            if (e->edit_text_node != n || e->edit_text_kind != kind ||
                e->edit_text_index != index) {
                snprintf(e->edit_text, sizeof e->edit_text, "%s", v->str ? v->str : "");
                e->edit_text_node = n;
                e->edit_text_kind = kind;
                e->edit_text_index = index;
            }
            changed = ImGui_InputText(id, e->edit_text, sizeof e->edit_text, 0);
            if (changed) {
                char *copy = graph_own_str(e->g, e->edit_text, strlen(e->edit_text));
                if (copy) v->str = copy;
            }
            if (!ImGui_IsItemActive()) e->edit_text_node = NULL;
            break;
        }
        case PIN_STRUCT:
        case PIN_FLOW:
        default: break;
    }
    ImGui_PopID();
    ImGui_PopID();
    ImGui_PopStyleVar();
    ImGui_PopFont();
    if (changed)
        graph_set_pin_default(e->g, n, kind == ED_PIN_OUTPUT, index, v);
    ed_finish_value_edit(e, changed);
    return changed;
}

static const char *ed_pin_type_core_name(pin_type type)
{
    switch (type) {
        case PIN_BOOL: return "bool";
        case PIN_I8: return "int8";
        case PIN_U8: return "byte";
        case PIN_I16: return "int16";
        case PIN_U16: return "uint16";
        case PIN_I32: return "int";
        case PIN_U32: return "uint";
        case PIN_I64: return "int64";
        case PIN_U64: return "uint64";
        case PIN_F32: return "float";
        case PIN_F64: return "double";
        case PIN_STR: return "const char *";
        case PIN_STRUCT: return "struct";
        case PIN_ENUM: return "enum";
        case PIN_FLOW:
        default: return NULL;
    }
}

static int ed_pin_type_combo(const char *label, pin_type *type)
{
    int selected = *type;
    const char *items = "Exec\0Bool\0Int8\0Byte\0Int16\0UInt16\0Integer\0"
                        "UInt32\0Integer64\0UInt64\0Real (float)\0Real "
                        "(double)\0String\0Struct\0Enum\0\0";
    if (!ImGui_Combo(label, &selected, items)) return 0;
    *type = (pin_type)selected;
    return 1;
}

static int ed_pin_container_combo(const char *label, pin_container *container)
{
    int selected = *container;
    const char *items = "Single\0Array\0Set\0Map\0\0";
    if (!ImGui_Combo(label, &selected, items)) return 0;
    *container = (pin_container)selected;
    return 1;
}

static void ed_details_begin_mutation(editor *e)
{
    if (e->details_history_open) return;
    ed_begin_authoring_edit(e);
    e->pending_snapshot = ed_snapshot(e);
    e->details_history_open = 1;
}

static void ed_details_finish_mutation(editor *e, int changed)
{
    if (changed) e->edit_changed = 1;
    if (e->details_history_open && ImGui_IsItemDeactivated()) {
        if (e->edit_changed) ed_commit_snapshot(e);
        else ed_cancel_snapshot(e);
        e->details_history_open = 0;
        e->edit_changed = 0;
    }
}

static int ed_draw_details_default(editor *e, node *n, int output, int index)
{
    pin_spec *spec = output ? &n->output_pins[index] : &n->input_pins[index];
    if (spec->kind != PIN_DATA) {
        ImGui_TextUnformatted("(exec)");
        return 0;
    }
    pin_value value = spec->default_value;
    int changed = 0;
    char id[32];
    snprintf(id, sizeof id, "Default##%d", index);
    ImGui_SetNextItemWidth(160.0f);
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
            changed = ImGui_InputFloatEx(id, &value.f, 0.1f, 0.0f, "%.3f", 0);
            break;
        case PIN_F64:
            changed = ImGui_InputDoubleEx(id, &value.d, 0.1, 0.0, "%.3f", 0);
            break;
        case PIN_ENUM: changed = ImGui_InputInt(id, &value.e); break;
        case PIN_STR: {
            snprintf(e->edit_text, sizeof e->edit_text, "%s", value.str ? value.str : "");
            changed = ImGui_InputText(id, e->edit_text, sizeof e->edit_text, 0);
            value.str = e->edit_text;
            break;
        }
        case PIN_FLOW:
        case PIN_STRUCT:
        default: ImGui_TextUnformatted("(not editable)"); break;
    }
    if (changed) {
        ed_details_begin_mutation(e);
        if (graph_set_pin_default(e->g, n, output, index, &value) == 0)
            e->edit_changed = 1;
    }
    ed_details_finish_mutation(e, changed);
    return changed;
}

static void ed_draw_details_pin(editor *e, node *n, int output, int index)
{
    pin_spec *spec = output ? &n->output_pins[index] : &n->input_pins[index];
    ImGui_PushIDInt((output ? 10000 : 0) + index);
    ImGui_Separator();
    ImGui_Text("%s %d", output ? "Output" : "Input", index);
    if (spec->kind == PIN_EXEC) {
        ImGui_SameLine();
        ImGui_TextUnformatted("Exec");
    }
    snprintf(e->details_pin_name, sizeof e->details_pin_name, "%s", spec->name ? spec->name : "");
    if (ImGui_InputText("Name", e->details_pin_name, sizeof e->details_pin_name, 0)) {
        ed_details_begin_mutation(e);
        if (graph_set_pin_name(e->g, n, output, index, e->details_pin_name) == 0)
            e->edit_changed = 1;
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    if (spec->kind == PIN_DATA) {
        pin_type type = spec->type;
        if (ed_pin_type_combo("Type", &type)) {
            ed_details_begin_mutation(e);
            if (graph_set_pin_type(e->g, n, output, index, type, ed_pin_type_core_name(type)) ==
                0)
                e->edit_changed = 1;
        }
        ed_details_finish_mutation(e, ImGui_IsItemEdited());
        pin_container container = spec->container;
        if (ed_pin_container_combo("Container", &container)) {
            ed_details_begin_mutation(e);
            if (graph_set_pin_container(e->g, n, output, index, container) == 0)
                e->edit_changed = 1;
        }
        ed_details_finish_mutation(e, ImGui_IsItemEdited());
        ImGui_TextUnformatted("Default");
        ImGui_SameLine();
        ed_draw_details_default(e, n, output, index);
    }
    ImGui_PopID();
}

static void ed_draw_tree(editor *e)
{
    ImGui_SetNextWindowSize((ImVec2){ 220.0f, 420.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Blueprint", NULL, 0)) {
        ImGui_End();
        return;
    }
    ImGui_TextUnformatted(e->g->name && e->g->name[0] ? e->g->name : "Graph");
    ImGui_SeparatorText("Nodes");
    for (int i = 0; i < e->g->node_count; i++) {
        node *n = e->g->nodes[i];
        const char *title = n->title_override && n->title_override[0]
                              ? n->title_override
                              : (n->desc ? n->desc->name : "Node");
        char label[256];
        if (n->desc && n->desc->flow_kind == NODE_FLOW_EVENT)
            snprintf(label, sizeof label, "%s: %s", title, n->reference_name ? n->reference_name : "(unnamed)");
        else snprintf(label, sizeof label, "%s##tree_%d", title, i);
        ImGui_PushIDInt(i);
        if (ImGui_SelectableEx(label, ed_node_selected(e, n), 0, (ImVec2){ 0.0f, 0.0f }))
            editor_select_node(e, n);
        ImGui_PopID();
    }
    ImGui_End();
}

static void ed_draw_details(editor *e)
{
    ImGui_SetNextWindowSize((ImVec2){ 340.0f, 600.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Details", NULL, 0)) {
        ImGui_End();
        return;
    }
    if (e->selected_count != 1) {
        ImGui_TextUnformatted(e->selected_count ? "Multiple nodes selected" : "Nothing selected");
        ImGui_End();
        return;
    }
    node *n = e->selected[0];
    e->details_node = n;
    ImGui_Text("%s", n->desc && n->desc->name ? n->desc->name : "Node");
    snprintf(e->details_title, sizeof e->details_title, "%s", n->title_override ? n->title_override : "");
    if (ImGui_InputText("Title override", e->details_title, sizeof e->details_title, 0)) {
        ed_details_begin_mutation(e);
        if (graph_set_node_title(e->g, n, e->details_title) == 0)
            e->edit_changed = 1;
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    snprintf(e->details_comment, sizeof e->details_comment, "%s", n->comment ? n->comment : "");
    if (ImGui_InputTextMultiline("Comment", e->details_comment, sizeof e->details_comment)) {
        ed_details_begin_mutation(e);
        if (graph_set_node_comment(e->g, n, e->details_comment) == 0)
            e->edit_changed = 1;
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    bool disabled = n->disabled != 0;
    if (ImGui_Checkbox("Disabled", &disabled)) {
        ed_details_begin_mutation(e);
        if (graph_set_node_disabled(e->g, n, disabled) == 0)
            e->edit_changed = 1;
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    ImGui_TextUnformatted("Reference target");
    uint64_t reference_id = n->reference_id;
    if (ImGui_InputScalar("Reference ID", ImGuiDataType_U64, &reference_id)) {
        ed_details_begin_mutation(e);
        if (graph_set_node_reference(e->g, n, reference_id, n->reference_name) == 0)
            e->edit_changed = 1;
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    snprintf(
        e->details_reference_name,
        sizeof e->details_reference_name,
        "%s",
        n->reference_name ? n->reference_name : ""
    );
    const char *reference_label = n->desc && n->desc->flow_kind == NODE_FLOW_EVENT
                                    ? "Event name"
                                    : "Reference name";
    if (ImGui_InputText(reference_label, e->details_reference_name, sizeof e->details_reference_name, 0)) {
        ed_details_begin_mutation(e);
        if (graph_set_node_reference(e->g, n, n->reference_id, e->details_reference_name) == 0)
            e->edit_changed = 1;
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());

    if (n->desc && n->desc->flow_kind == NODE_FLOW_EVENT) {
        ImGui_SeparatorText("Event parameters");
        if (ImGui_Button("Add parameter")) {
            pin_spec parameter = { "Parameter", PIN_I32, "int", PIN_DATA };
            ed_snapshot_before(e);
            (void)graph_add_pin(e->g, n, 1, &parameter);
        }
    } else if (n->desc && n->desc->flow_kind == NODE_FLOW_SEQUENCE) {
        ImGui_SeparatorText("Sequence outputs");
        if (ImGui_Button("Add output pin")) {
            char name[32];
            snprintf(name, sizeof name, "Then%d", n->output_count);
            pin_spec pin = { name, PIN_FLOW, NULL, PIN_EXEC };
            ed_snapshot_before(e);
            (void)graph_add_pin(e->g, n, 1, &pin);
        }
        ImGui_SameLine();
        ImGui_BeginDisabled(n->output_count <= 2);
        if (ImGui_Button("Remove last output pin")) {
            ed_snapshot_before(e);
            (void)graph_remove_pin(e->g, n, 1, n->output_count - 1);
        }
        ImGui_EndDisabled();
    }
    for (int p = 0; p < n->input_count; p++) ed_draw_details_pin(e, n, 0, p);
    for (int p = 0; p < n->output_count; p++) {
        ed_draw_details_pin(e, n, 1, p);
        if (n->desc && n->desc->flow_kind == NODE_FLOW_EVENT && p > 0) {
            ImGui_SameLine();
            ImGui_PushIDInt(20000 + p);
            if (p > 1 && ImGui_Button("Up")) {
                ed_snapshot_before(e);
                (void)graph_move_pin(e->g, n, 1, p, p - 1);
            }
            ImGui_SameLine();
            if (p + 1 < n->output_count && ImGui_Button("Down")) {
                ed_snapshot_before(e);
                (void)graph_move_pin(e->g, n, 1, p, p + 1);
            }
            ImGui_SameLine();
            if (ImGui_Button("Remove")) {
                ed_snapshot_before(e);
                (void)graph_remove_pin(e->g, n, 1, p);
            }
            ImGui_PopID();
        }
    }
    ImGui_End();
}

static int ed_node_dragged(const editor *e, const node *n)
{
    for (int i = 0; i < e->drag_count; i++)
        if (e->drag_nodes[i].n == n) return 1;
    return 0;
}

static int ed_node_on_top(const editor *e, const node *n)
{
    return ed_node_dragged(e, n) || ed_node_selected(e, n) || e->front_node == n;
}

static void ed_draw_controls_for_node(editor *e, ed_node_view *view)
{
    node *n = view->n;
    for (int p = 0; p < n->input_count; p++) {
        if (!ed_pin_editable(e->g, n, ED_PIN_INPUT, p)) continue;
        ed_pin_view *pin = &e->pins[view->input_first + p];
        ImVec2 label = ed_text_size(e, n->input_pins[p].name);
        ImVec2 pos = ed_add(
            pin->pos,
            (ImVec2){ (ED_PIN_RADIUS + 9.0f) * e->zoom + label.x,
                      -ed_font_size(e) * 0.5f - 2.0f * e->zoom }
        );
        ed_begin_value_edit(e, n, ED_PIN_INPUT, p, pos);
        ed_draw_value_editor(e, n, ED_PIN_INPUT, p, pos);
        e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    }
    for (int p = 0; p < n->output_count; p++) {
        if (!ed_pin_editable(e->g, n, ED_PIN_OUTPUT, p)) continue;
        ed_pin_view *pin = &e->pins[view->output_first + p];
        ImVec2 label = ed_text_size(e, n->output_pins[p].name);
        ImVec2 pos = { pin->pos.x - (ED_PIN_RADIUS + 9.0f) * e->zoom - label.x -
                           ED_VALUE_WIDTH * e->zoom,
                       pin->pos.y - ed_font_size(e) * 0.5f - 2.0f * e->zoom };
        ed_begin_value_edit(e, n, ED_PIN_OUTPUT, p, pos);
        ed_draw_value_editor(e, n, ED_PIN_OUTPUT, p, pos);
        e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    }
}

static void ed_draw_controls(editor *e, int on_top)
{
    ImGui_PushClipRect(e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    for (int i = 0; i < e->view_count; i++)
        if (ed_node_on_top(e, e->views[i].n) == on_top)
            ed_draw_controls_for_node(e, &e->views[i]);
    ImGui_PopClipRect();
}

static void ed_draw_grid(editor *e, ImDrawList *dl)
{
    ImDrawList_AddRectFilled(
        dl,
        e->canvas_min,
        ed_add(e->canvas_min, e->canvas_size),
        IM_COL32(22, 24, 30, 255)
    );
    float spacing = ED_GRID_SPACING * e->zoom;
    if (spacing < 8.0f) return;
    float x = e->canvas_min.x + fmodf(e->pan.x, spacing);
    float y = e->canvas_min.y + fmodf(e->pan.y, spacing);
    for (; x <= e->canvas_min.x + e->canvas_size.x; x += spacing)
        ImDrawList_AddLine(
            dl,
            (ImVec2){ x, e->canvas_min.y },
            (ImVec2){ x, e->canvas_min.y + e->canvas_size.y },
            IM_COL32(38, 41, 50, 255)
        );
    for (; y <= e->canvas_min.y + e->canvas_size.y; y += spacing)
        ImDrawList_AddLine(
            dl,
            (ImVec2){ e->canvas_min.x, y },
            (ImVec2){ e->canvas_min.x + e->canvas_size.x, y },
            IM_COL32(38, 41, 50, 255)
        );
}

static void ed_draw_link(editor *e, ImDrawList *dl, const edge *ed)
{
    ImVec2 p0, p1, p2, p3;
    if (!ed_link_curve(e, ed, &p0, &p1, &p2, &p3)) return;
    ed_link_ref ref = { ed->from, ed->from_pin, ed->to, ed->to_pin };
    int selected = ed_link_selected(e, &ref);
    const pin_spec *source = ed->from && ed->from_pin >= 0 &&
                                     ed->from_pin < ed->from->output_count
                               ? &ed->from->output_pins[ed->from_pin]
                               : NULL;
    ImVec4 base = source ? ed_spec_color(source) : ed_pin_color(PIN_FLOW);
    ImU32 color = ed_color(base);
    if (selected) color = ed_color((ImVec4){ 1.0f, 0.82f, 0.39f, 1.0f });
    int hovered = e->has_hovered_link && ed_link_equal(&ref, &e->hovered_link);
    if (!selected && hovered)
        color = ed_color((ImVec4){ 0.85f, 0.92f, 1.0f, 1.0f });
    float thickness = selected
                        ? 4.0f
                        : (hovered ? 3.5f : (ed->kind == GRAPH_EDGE_EXEC ? 2.5f : 1.5f));
    ImDrawList_AddBezierCubic(dl, p0, p1, p2, p3, color, thickness, 0);
}

static void ed_draw_node(editor *e, ImDrawList *dl, const ed_node_view *v)
{
    node *n = v->n;
    const node_desc *d = n->desc;
    int selected = ed_node_selected(e, n);
    ImU32 bg = ed_color(ED_NODE_BODY);
    ImU32 border = ed_color(ED_NODE_BORDER);
    ImU32 title = ed_color(ed_node_title_color(n));
    int hovered = e->hovered_node == n;
    if (hovered) bg = ed_color((ImVec4){ 0.25f, 0.28f, 0.34f, 1.0f });
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
    ImDrawList_AddRectFilledEx(
        dl,
        v->min,
        (ImVec2){ v->max.x, v->min.y + v->title_height },
        title,
        5.0f * e->zoom,
        ImDrawFlags_RoundCornersTop
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
    ed_draw_text(
        e,
        dl,
        ed_add(v->min, (ImVec2){ ED_PAD * e->zoom, ED_TITLE_PAD * e->zoom }),
        IM_COL32(235, 238, 245, 255),
        n->title_override ? n->title_override : d->name
    );

    int rows = n->input_count > n->output_count ? n->input_count : n->output_count;
    for (int row = 0; row < rows; row++) {
        if (row < n->input_count) {
            const pin_spec *spec = &n->input_pins[row];
            ed_pin_view *pin = &e->pins[v->input_first + row];
            ImU32 color = ed_color(ed_spec_color(spec));
            ImDrawList_AddCircleFilled(dl, pin->pos, ed_max(2.0f, ED_PIN_RADIUS * e->zoom), color, 0);
            ImDrawList_AddCircle(
                dl,
                pin->pos,
                ed_max(2.0f, ED_PIN_RADIUS * e->zoom),
                ed_dim_color(color, 0.28f)
            );
            ed_draw_text(
                e,
                dl,
                ed_add(pin->pos, (ImVec2){ (ED_PIN_RADIUS + 5.0f) * e->zoom, -ed_font_size(e) * 0.5f }),
                IM_COL32(220, 224, 232, 255),
                spec->name
            );
            const char *value = eval_view_input(e->ev, e->g, n, row);
            if (value && !ed_pin_editable(e->g, n, ED_PIN_INPUT, row)) {
                ImVec2 label = ed_text_size(e, spec->name);
                ed_draw_text(
                    e,
                    dl,
                    ed_add(
                        pin->pos,
                        (ImVec2){ (ED_PIN_RADIUS + 9.0f) * e->zoom + label.x,
                                  -ed_font_size(e) * 0.5f }
                    ),
                    IM_COL32(130, 205, 255, 255),
                    value
                );
            }
        }
        if (row < n->output_count) {
            const pin_spec *spec = &n->output_pins[row];
            ed_pin_view *pin = &e->pins[v->output_first + row];
            ImU32 color = ed_color(ed_spec_color(spec));
            ImVec2 label = ed_text_size(e, spec->name);
            ImDrawList_AddCircleFilled(dl, pin->pos, ed_max(2.0f, ED_PIN_RADIUS * e->zoom), color, 0);
            ImDrawList_AddCircle(
                dl,
                pin->pos,
                ed_max(2.0f, ED_PIN_RADIUS * e->zoom),
                ed_dim_color(color, 0.28f)
            );
            ed_draw_text(
                e,
                dl,
                (ImVec2){ pin->pos.x - (ED_PIN_RADIUS + 5.0f) * e->zoom - label.x,
                          pin->pos.y - ed_font_size(e) * 0.5f },
                IM_COL32(220, 224, 232, 255),
                spec->name
            );
            const char *value = eval_view_output(e->ev, n, row);
            if (value && value[0] && !ed_pin_editable(e->g, n, ED_PIN_OUTPUT, row)) {
                ImVec2 val = ed_text_size(e, value);
                ed_draw_text(
                    e,
                    dl,
                    (ImVec2){ pin->pos.x - (ED_PIN_RADIUS + 9.0f) * e->zoom -
                                  label.x - val.x,
                              pin->pos.y - ed_font_size(e) * 0.5f },
                    IM_COL32(130, 205, 255, 255),
                    value
                );
            }
        }
    }
}

static void ed_draw_canvas(editor *e)
{
    ImDrawList *dl = ImGui_GetWindowDrawList();
    ImDrawList_PushClipRect(dl, e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    ed_draw_grid(e, dl);
    for (int i = 0; i < e->g->edge_count; i++)
        ed_draw_link(e, dl, &e->g->edges[i]);
    for (int i = 0; i < e->view_count; i++)
        if (!ed_node_on_top(e, e->views[i].n))
            ed_draw_node(e, dl, &e->views[i]);

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
    if (e->connect_error[0])
        ed_draw_text(
            e,
            dl,
            ed_add(e->connect_error_pos, (ImVec2){ 8.0f, 8.0f }),
            IM_COL32(255, 110, 110, 255),
            e->connect_error
        );

    ImDrawList_PopClipRect(dl);
}

static void ed_draw_front_nodes(editor *e)
{
    ImDrawList *dl = ImGui_GetWindowDrawList();
    ImDrawList_PushClipRect(dl, e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    for (int i = 0; i < e->view_count; i++)
        if (ed_node_on_top(e, e->views[i].n)) ed_draw_node(e, dl, &e->views[i]);
    ImDrawList_PopClipRect(dl);
}

static void ed_draw_selection_overlay(const editor *e)
{
    if (!e->selecting) return;
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

static int ed_run_live(const editor *e);

static char *ed_snapshot(const editor *e) { return graph_save_json(e->g); }

static void ed_commit_snapshot(editor *e)
{
    if (!e->pending_snapshot) return;
    ed_push_string(&e->undo, &e->undo_count, &e->undo_cap, e->pending_snapshot);
    e->pending_snapshot = NULL;
    ed_free_string_stack(e->redo, &e->redo_count);
}

static void ed_snapshot_before(editor *e)
{
    ed_begin_authoring_edit(e);
    char *snapshot = ed_snapshot(e);
    if (!snapshot) return;
    ed_push_string(&e->undo, &e->undo_count, &e->undo_cap, snapshot);
    ed_free_string_stack(e->redo, &e->redo_count);
}

static void ed_cancel_snapshot(editor *e)
{
    free(e->pending_snapshot);
    e->pending_snapshot = NULL;
}

static int ed_run_live(const editor *e);

/* RUN-14: every graph swap funnels through here. A live flow_run holds raw
 * node pointers in its continuation frames, so graph_free() below would leave
 * the run dereferencing freed memory on its next resume. Guarding the choke
 * point covers undo, redo and load together. */
static int ed_adopt_json(editor *e, const char *json)
{
    assert(!ed_run_live(e));
    if (ed_run_live(e)) return -1;
    graph *fresh = graph_load_json(json);
    if (!fresh) return -1;
    graph_free(e->g);
    *e->g = *fresh;
    free(fresh);
    ed_clear_selection(e);
    e->dragging = 0;
    e->linking = 0;
    e->view_initialized = 0;
    e->reveal_target = NULL;
    e->context_node = NULL;
    e->has_context_link = 0;
    e->front_node = NULL;
    e->spawn_link_pending = 0;
    e->edit_text_node = NULL;
    e->drag_count = 0;
    e->log_count = 0;
    return 0;
}

static void ed_undo(editor *e)
{
    ed_begin_authoring_edit(e);
    if (e->undo_count == 0) return;
    char *target = e->undo[--e->undo_count];
    char *current = graph_save_json(e->g);
    if (current)
        ed_push_string(&e->redo, &e->redo_count, &e->redo_cap, current);
    ed_adopt_json(e, target);
    free(target);
}

static void ed_redo(editor *e)
{
    ed_begin_authoring_edit(e);
    if (e->redo_count == 0) return;
    char *target = e->redo[--e->redo_count];
    char *current = graph_save_json(e->g);
    if (current)
        ed_push_string(&e->undo, &e->undo_count, &e->undo_cap, current);
    ed_adopt_json(e, target);
    free(target);
}

static void ed_delete_selected(editor *e)
{
    if (e->selected_count == 0 && e->selected_link_count == 0) return;
    ed_snapshot_before(e);
    for (int i = e->selected_link_count - 1; i >= 0; i--) {
        int index = ed_edge_index(e, &e->selected_links[i]);
        if (index >= 0) {
            edge ed = e->g->edges[index];
            graph_remove_edge(e->g, ed.from, ed.from_pin, ed.to, ed.to_pin);
        }
    }
    int front_deleted = 0;
    for (int i = e->selected_count - 1; i >= 0; i--) {
        if (e->selected[i] == e->front_node) front_deleted = 1;
        graph_remove_node(e->g, e->selected[i]);
    }
    if (front_deleted) e->front_node = NULL;
    ed_clear_selection(e);
}

static void ed_copy_selected(editor *e)
{
    if (e->selected_count == 0) return;
    char *json = graph_save_nodes_json(e->g, e->selected, e->selected_count);
    if (!json) return;
    ImGui_SetClipboardText(json);
    free(e->clipboard);
    e->clipboard = json;
}

static void ed_cut_selected(editor *e)
{
    ed_copy_selected(e);
    if (e->selected_count == 0) return;
    ed_delete_selected(e);
}

static void ed_paste(editor *e)
{
    const char *clipboard = ImGui_GetClipboardText();
    if (!clipboard || !clipboard[0]) clipboard = e->clipboard;
    if (!clipboard) return;
    ed_snapshot_before(e);
    int first = e->g->node_count;
    if (graph_append_json(e->g, clipboard, 24.0f, 24.0f) == 0) {
        ed_clear_selection(e);
        for (int i = first; i < e->g->node_count; i++)
            ed_add_selected(e, e->g->nodes[i]);
    }
}

static void ed_duplicate_selected(editor *e)
{
    if (e->selected_count == 0) return;
    char *json = graph_save_nodes_json(e->g, e->selected, e->selected_count);
    if (!json) return;
    ed_snapshot_before(e);
    int first = e->g->node_count;
    if (graph_append_json(e->g, json, 24.0f, 24.0f) == 0) {
        ed_clear_selection(e);
        for (int i = first; i < e->g->node_count; i++)
            ed_add_selected(e, e->g->nodes[i]);
    }
    free(json);
}

static void ed_begin_drag(editor *e, ImVec2 mouse)
{
    ed_begin_authoring_edit(e);
    e->drag_count = e->selected_count;
    if (e->drag_count > e->drag_cap) {
        ed_drag_node *grown = (ed_drag_node *)
            realloc(e->drag_nodes, (size_t)e->drag_count * sizeof *grown);
        if (!grown) {
            e->drag_count = 0;
            return;
        }
        e->drag_nodes = grown;
        e->drag_cap = e->drag_count;
    }
    for (int i = 0; i < e->drag_count; i++)
        e->drag_nodes[i] = (ed_drag_node){ e->selected[i],
                                           e->selected[i]->x,
                                           e->selected[i]->y };
    e->drag_start_mouse = mouse;
    e->dragging = 1;
    e->pending_snapshot = ed_snapshot(e);
}

static void ed_update_drag(editor *e, ImVec2 mouse)
{
    ImVec2 delta = ed_scale(ed_sub(mouse, e->drag_start_mouse), 1.0f / e->zoom);
    for (int i = 0; i < e->drag_count; i++) {
        e->drag_nodes[i].n->x = e->drag_nodes[i].x + delta.x;
        e->drag_nodes[i].n->y = e->drag_nodes[i].y + delta.y;
    }
}

static int ed_connect(editor *e, const ed_pin_view *a, const ed_pin_view *b)
{
    ed_begin_authoring_edit(e);
    e->connect_error[0] = '\0';
    const ed_pin_view *output = a->kind == ED_PIN_OUTPUT ? a : b;
    const ed_pin_view *input = a->kind == ED_PIN_INPUT ? a : b;
    if (output->kind != ED_PIN_OUTPUT || input->kind != ED_PIN_INPUT) return 0;
    const pin_spec *out_spec = &output->n->output_pins[output->index];
    const pin_spec *in_spec = &input->n->input_pins[input->index];
    const char *out_name = out_spec->type_name ? out_spec->type_name : "unknown";
    const char *in_name = in_spec->type_name ? in_spec->type_name : "unknown";
    if (out_spec->kind != in_spec->kind) {
        snprintf(e->connect_error, sizeof e->connect_error, "Cannot connect %s to %s", out_name, in_name);
        return 0;
    }
    if (out_spec->kind == PIN_DATA && !pin_types_compatible(out_spec, in_spec)) {
        snprintf(e->connect_error, sizeof e->connect_error, "Cannot connect %s to %s", out_name, in_name);
        return 0;
    }
    if (out_spec->kind == PIN_EXEC) {
        int order = output->n->desc->flow_kind == NODE_FLOW_SEQUENCE ? output->index
                                                                     : 0;
        return graph_connect_exec(
                   e->g,
                   output->n,
                   output->index,
                   input->n,
                   input->index,
                   order
               ) == 0;
    }
    return graph_connect_data(e->g, output->n, output->index, input->n, input->index) == 0;
}

#define ED_SEARCH_MAX 128

typedef struct ed_search_match
{
    int registry_index;
    int score;
} ed_search_match;

static char ed_search_lower(char c)
{ return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }

static int ed_search_length(const char *text)
{
    int length = 0;
    while (text && text[length] && length < ED_SEARCH_MAX - 1) length++;
    return length;
}

static int ed_damerau_levenshtein(const char *a, const char *b)
{
    int al = ed_search_length(a), bl = ed_search_length(b);
    int pp[ED_SEARCH_MAX] = { 0 }, p[ED_SEARCH_MAX] = { 0 }, c[ED_SEARCH_MAX] = { 0 };
    for (int j = 0; j <= bl; j++) p[j] = j;
    for (int i = 1; i <= al; i++) {
        c[0] = i;
        for (int j = 1; j <= bl; j++) {
            int v = p[j - 1] +
                    (ed_search_lower(a[i - 1]) != ed_search_lower(b[j - 1]));
            if (c[j - 1] + 1 < v) v = c[j - 1] + 1;
            if (p[j] + 1 < v) v = p[j] + 1;
            if (i > 1 && j > 1 &&
                ed_search_lower(a[i - 1]) == ed_search_lower(b[j - 2]) &&
                ed_search_lower(a[i - 2]) == ed_search_lower(b[j - 1]) &&
                pp[j - 2] + 1 < v)
                v = pp[j - 2] + 1;
            c[j] = v;
        }
        memcpy(pp, p, sizeof pp);
        memcpy(p, c, sizeof p);
    }
    return p[bl];
}

static int ed_best_name_distance(const char *name, const char *filter)
{
    int nl = ed_search_length(name), fl = ed_search_length(filter), best = ED_SEARCH_MAX;
    int low = fl > 2 ? fl - 2 : 1, high = fl + 2;
    for (int start = 0; start < nl; start++)
        for (int length = low; length <= high && start + length <= nl; length++) {
            char window[ED_SEARCH_MAX];
            memcpy(window, name + start, (size_t)length);
            window[length] = '\0';
            int d = ed_damerau_levenshtein(window, filter);
            if (d < best) best = d;
        }
    return best;
}

static int ed_name_score(const char *name, const char *filter)
{
    if (!filter || !filter[0]) return 0;
    int score = 0, previous = -1, first = -1, matched = 1;
    for (int fi = 0; filter[fi]; fi++) {
        int found = -1;
        for (int ni = previous + 1; name[ni]; ni++)
            if (ed_search_lower(name[ni]) == ed_search_lower(filter[fi])) {
                found = ni;
                break;
            }
        if (found < 0) {
            matched = 0;
            break;
        }
        if (first < 0) first = found;
        score += found == previous + 1 ? 8 : 1;
        if (found == 0 || name[found - 1] == ' ' || name[found - 1] == '_')
            score += 5;
        previous = found;
    }
    int fl = ed_search_length(filter), distance = ed_best_name_distance(name, filter);
    int limit = fl <= 3 ? 1 : fl / 3;
    if (!matched && distance > limit) return -1;
    if (!matched) score = 0;
    if (matched && first == 0) score += 120;
    if (matched && first > 0 && (name[first - 1] == ' ' || name[first - 1] == '_'))
        score += 60;
    if (distance == 0) score += 60;
    else if (distance <= limit) score += 32 - distance * 8;
    return score;
}

static void ed_open_context_menu(editor *e, ImVec2 mouse)
{
    e->context_node = NULL;
    e->has_context_link = 0;
    ed_pin_view *pin = ed_hit_pin(e, mouse);
    ed_node_view *node_view = pin ? NULL : ed_hit_node(e, mouse);
    ed_link_ref link;
    if (node_view) {
        e->context_node = node_view->n;
        ImGui_OpenPopup("node_menu", 0);
    } else if (!pin && ed_hit_link(e, mouse, &link)) {
        e->context_link = link;
        e->has_context_link = 1;
        ImGui_OpenPopup("link_menu", 0);
    } else {
        ImVec2 world = ed_screen_to_world(e, mouse);
        e->popup_x = world.x;
        e->popup_y = world.y;
        ImGui_OpenPopup("spawn_menu", 0);
    }
}

static int ed_spawn_desc_compatible(const editor *e, const node_desc *desc)
{
    if (!e->spawn_link_pending) return 1;
    const pin_spec
        *source = e->spawn_link_pin.kind == ED_PIN_OUTPUT
                    ? &e->spawn_link_pin.n->output_pins[e->spawn_link_pin.index]
                    : &e->spawn_link_pin.n->input_pins[e->spawn_link_pin.index];
    const pin_spec *pins = e->spawn_link_pin.kind == ED_PIN_OUTPUT ? desc->inputs
                                                                   : desc->outputs;
    int count = e->spawn_link_pin.kind == ED_PIN_OUTPUT ? desc->input_count
                                                        : desc->output_count;
    for (int i = 0; i < count; i++) {
        const pin_spec *candidate = &pins[i];
        if (source->kind != candidate->kind) continue;
        if (source->kind == PIN_EXEC ||
            (e->spawn_link_pin.kind == ED_PIN_OUTPUT
                 ? pin_types_compatible(source, candidate)
                 : pin_types_compatible(candidate, source)))
            return 1;
    }
    return 0;
}

static int ed_autowire_spawn(editor *e, node *n)
{
    int connected = 0;
    if (e->spawn_link_pin.kind == ED_PIN_OUTPUT) {
        for (int p = 0; p < n->input_count && !connected; p++) {
            ed_pin_view target = { n, ED_PIN_INPUT, p, { 0.0f, 0.0f } };
            const pin_spec *a = &e->spawn_link_pin.n
                                     ->output_pins[e->spawn_link_pin.index];
            const pin_spec *b = &n->input_pins[p];
            if (a->kind == b->kind &&
                (a->kind == PIN_EXEC || pin_types_compatible(a, b)))
                connected = ed_connect(e, &e->spawn_link_pin, &target);
        }
    } else {
        for (int p = 0; p < n->output_count && !connected; p++) {
            ed_pin_view target = { n, ED_PIN_OUTPUT, p, { 0.0f, 0.0f } };
            const pin_spec *a = &n->output_pins[p];
            const pin_spec *b = &e->spawn_link_pin.n
                                     ->input_pins[e->spawn_link_pin.index];
            if (a->kind == b->kind &&
                (a->kind == PIN_EXEC || pin_types_compatible(a, b)))
                connected = ed_connect(e, &e->spawn_link_pin, &target);
        }
    }
    return connected;
}

static void ed_handle_popups(editor *e)
{
    if (ImGui_BeginPopup("node_menu", 0)) {
        node *n = e->context_node;
        if (n && n->desc && strcmp(n->desc->name, "Sequence") == 0) {
            if (ImGui_MenuItem("Add output pin")) {
                char name[32];
                snprintf(name, sizeof name, "Then%d", n->output_count);
                pin_spec pin = { name, PIN_FLOW, NULL, PIN_EXEC };
                ed_snapshot_before(e);
                graph_add_pin(e->g, n, 1, &pin);
            }
            if (n->output_count > 2 && ImGui_MenuItem("Remove last output pin")) {
                ed_snapshot_before(e);
                graph_remove_pin(e->g, n, 1, n->output_count - 1);
            }
            ImGui_Separator();
        }
        if (n && ImGui_MenuItem(n->disabled ? "Enable" : "Disable")) {
            ed_snapshot_before(e);
            graph_set_node_disabled(e->g, n, !n->disabled);
        }
        if (ImGui_MenuItem("Delete")) {
            ed_select_node(e, e->context_node, 0);
            ed_delete_selected(e);
        }
        ImGui_EndPopup();
    }
    if (ImGui_BeginPopup("link_menu", 0)) {
        if (ImGui_MenuItem("Delete Link") && e->has_context_link) {
            e->selected_count = 0;
            e->selected_link_count = 0;
            ed_add_selected_link(e, &e->context_link);
            ed_delete_selected(e);
        }
        ImGui_EndPopup();
    }
    if (ImGui_BeginPopup("spawn_menu", 0)) {
        if (ImGui_IsWindowAppearing()) {
            ImGui_SetKeyboardFocusHere();
            e->search_selected = 0;
        }
        ImGui_InputText("Filter", e->search, sizeof e->search, 0);
        ImGui_Separator();
        int count = registry_node_count();
        ed_search_match *matches = count ? (ed_search_match *)malloc(
                                               (size_t)count * sizeof *matches
                                           )
                                         : NULL;
        int match_count = 0;
        for (int i = 0; i < count; i++) {
            const node_desc *desc = registry_node(i);
            if (!ed_spawn_desc_compatible(e, desc)) continue;
            int score = ed_name_score(desc->name, e->search);
            if (score < 0) continue;
            int at = match_count++;
            while (at > 0 && matches[at - 1].score < score) {
                matches[at] = matches[at - 1];
                at--;
            }
            matches[at] = (ed_search_match){ i, score };
        }
        if (match_count == 0) e->search_selected = -1;
        else {
            if (e->search_selected < 0) e->search_selected = 0;
            if (e->search_selected >= match_count)
                e->search_selected = match_count - 1;
            if (ImGui_IsKeyPressed(ImGuiKey_DownArrow))
                e->search_selected = (e->search_selected + 1) % match_count;
            if (ImGui_IsKeyPressed(ImGuiKey_UpArrow))
                e->search_selected = (e->search_selected + match_count - 1) % match_count;
        }
        int activate = ImGui_IsKeyPressed(ImGuiKey_Enter);
        for (int m = 0; m < match_count; m++) {
            const node_desc *desc = registry_node(matches[m].registry_index);
            int clicked = ImGui_SelectableEx(
                desc->name,
                m == e->search_selected,
                0,
                (ImVec2){ 0.0f, 0.0f }
            );
            if (clicked || (activate && m == e->search_selected)) {
                if (!e->spawn_link_pending) ed_snapshot_before(e);
                node *n = graph_add_node(e->g, desc);
                if (n) {
                    n->x = e->popup_x;
                    n->y = e->popup_y;
                    if (e->spawn_link_pending) {
                        ed_autowire_spawn(e, n);
                        ed_commit_snapshot(e);
                        e->spawn_link_pending = 0;
                    }
                    e->search[0] = '\0';
                    e->search_selected = 0;
                    ImGui_CloseCurrentPopup();
                }
            }
        }
        if (match_count == 0)
            ImGui_TextUnformatted(
                e->spawn_link_pending ? "No compatible nodes" : "No matching nodes"
            );
        free(matches);
        ImGui_EndPopup();
    }
    if (e->spawn_link_pending && !ImGui_IsPopupOpen("spawn_menu", 0)) {
        ed_cancel_snapshot(e);
        e->spawn_link_pending = 0;
    }
}

static int ed_run_live(const editor *e)
{ return e && e->run && !editor_preview_allowed(flow_run_status(e->run)); }

static void ed_release_terminal_run(editor *e);

static void ed_begin_authoring_edit(editor *e)
{
    if (!e || !ed_run_live(e)) return;
    (void)flow_run_stop(e->run);
    ed_release_terminal_run(e);
    eval_view_set_run(e->ev, NULL);
}

static const char *ed_flow_state_name(flow_state state)
{
    switch (state) {
        case FLOW_RUNNING: return "RUNNING";
        case FLOW_SUSPENDED: return "SUSPENDED";
        case FLOW_PAUSED: return "PAUSED";
        case FLOW_COMPLETED: return "COMPLETED";
        case FLOW_FAILED: return "FAILED";
        case FLOW_IDLE:
        default: return "IDLE";
    }
}

static node *ed_entry_at(const editor *e, int index)
{
    int at = 0;
    for (int i = 0; i < e->g->node_count; i++) {
        node *n = e->g->nodes[i];
        if (n->desc->flow_kind != NODE_FLOW_EVENT) continue;
        if (at++ == index) return n;
    }
    return NULL;
}

static int ed_entry_count(const editor *e)
{
    int count = 0;
    for (int i = 0; i < e->g->node_count; i++)
        if (e->g->nodes[i]->desc->flow_kind == NODE_FLOW_EVENT) count++;
    return count;
}

static void ed_release_terminal_run(editor *e)
{
    if (!e->run) return;
    flow_state state = flow_run_status(e->run);
    if (state == FLOW_IDLE || state == FLOW_COMPLETED || state == FLOW_FAILED) {
        flow_run_free(e->run);
        e->run = NULL;
    }
}

static void ed_start_run(editor *e)
{
    ed_release_terminal_run(e);
    if (e->run) return;
    node *entry = ed_entry_at(e, e->entry_index);
    if (!entry) return;
    flow_options options = {
        .initial_time = e->monotonic_time,
        .sink = ed_log_sink,
        .sink_user = e,
    };
    flow_error error;
    e->run = flow_run_start(e->g, entry, (flow_inputs){ 0 }, &options, &error);
    e->run_started_at = e->monotonic_time;
    if (e->run) (void)flow_run_resume(e->run);
}

static void ed_run_pause(editor *e)
{
    if (e->run) (void)flow_run_pause(e->run);
}
static void ed_run_resume(editor *e)
{
    if (e->run) (void)flow_run_resume(e->run);
}
static void ed_run_step(editor *e)
{
    if (e->run) (void)flow_run_step(e->run);
}
static void ed_run_stop(editor *e)
{
    if (!e->run) return;
    (void)flow_run_stop(e->run);
    ed_release_terminal_run(e);
    eval_view_set_run(e->ev, NULL);
}

static void ed_toolbar_action(
    editor *e,
    const char *label,
    int enabled,
    const char *reason,
    void (*action)(editor *)
)
{
    ImGui_BeginDisabled(!enabled);
    int clicked = ImGui_Button(label);
    if (!enabled && ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui_SetTooltip("%s", reason);
    ImGui_EndDisabled();
    if (enabled && clicked && action) action(e);
    ImGui_SameLine();
}

static void ed_draw_toolbar(editor *e)
{
    int entries = ed_entry_count(e);
    if (e->entry_index >= entries) e->entry_index = entries ? entries - 1 : 0;
    node *entry = ed_entry_at(e, e->entry_index);
    const char *entry_name = entry && entry->reference_name ? entry->reference_name
                                                            : "(unnamed event)";
    if (ImGui_BeginCombo("Entry", entry_name, 0)) {
        int at = 0;
        for (int i = 0; i < e->g->node_count; i++) {
            node *n = e->g->nodes[i];
            if (n->desc->flow_kind != NODE_FLOW_EVENT) continue;
            char label[160];
            snprintf(
                label,
                sizeof label,
                "%s##entry_%d",
                n->reference_name ? n->reference_name : "(unnamed event)",
                at
            );
            if (ImGui_SelectableEx(label, at == e->entry_index, 0, (ImVec2){ 0.0f, 0.0f }))
                e->entry_index = at;
            at++;
        }
        ImGui_EndCombo();
    }
    ImGui_SameLine();
    ImGui_BeginDisabled(true);
    ImGui_Button("Compile");
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui_SetTooltip("Compile is not available in Phase 1");
    ImGui_EndDisabled();
    ImGui_SameLine();
    ed_toolbar_action(e, "Undo", e->undo_count > 0, "No undo entry is available", ed_undo);
    ed_toolbar_action(e, "Redo", e->redo_count > 0, "No redo entry is available", ed_redo);

    flow_state state = e->run ? flow_run_status(e->run) : FLOW_IDLE;
    int can_run = (!e->run || state == FLOW_IDLE || state == FLOW_COMPLETED ||
                   state == FLOW_FAILED) &&
                  entry;
    int can_pause = state == FLOW_RUNNING || state == FLOW_SUSPENDED;
    int can_resume = state == FLOW_PAUSED || state == FLOW_SUSPENDED;
    int can_step = can_resume;
    int can_stop = state != FLOW_IDLE;
    ed_toolbar_action(
        e,
        "Run",
        can_run,
        entry ? "Run is available only when no run is active"
              : "No event entry point is available",
        ed_start_run
    );
    ed_toolbar_action(e, "Pause", can_pause, "Pause is available while running or suspended", ed_run_pause);
    ed_toolbar_action(e, "Resume", can_resume, "Resume is available only while paused or suspended", ed_run_resume);
    ed_toolbar_action(e, "Step", can_step, "Step is available only while paused or suspended", ed_run_step);
    ed_toolbar_action(e, "Stop", can_stop, "Stop is available only for a live run", ed_run_stop);
    ImGui_NewLine();
    if (e->run) {
        ImGui_Text("State: %s  Steps: %lu", ed_flow_state_name(state), flow_run_steps(e->run));
    } else {
        ImGui_Text("State: IDLE  Steps: 0");
    }
}

static void ed_draw_log(editor *e)
{
    ImGui_BeginDisabled(e->log_count == 0);
    int clear = ImGui_Button("Clear");
    ImGui_EndDisabled();
    if (clear) e->log_count = 0;
    if (ImGui_BeginChild("output_log", (ImVec2){ 0.0f, 0.0f }, 0, 0)) {
        int follow = ImGui_GetScrollY() >= ImGui_GetScrollMaxY() - 2.0f;
        for (int i = 0; i < e->log_count; i++) {
            const ed_log_entry *entry = &e->log[i];
            ImGui_PushIDInt(i);
            char label[ED_LOG_MESSAGE + 128];
            snprintf(
                label,
                sizeof label,
                "[%8.3f | step %lu] %s: %s",
                entry->timestamp,
                entry->step,
                entry->origin && entry->origin->desc ? entry->origin->desc->name
                                                     : "<unknown>",
                entry->message
            );
            if (ImGui_Selectable(label) && entry->origin)
                editor_reveal_node(e, (node *)entry->origin);
            ImGui_PopID();
        }
        if (follow && e->log_count) ImGui_SetScrollHereY(1.0f);
        ImGui_EndChild();
    }
}

static void ed_center_initial_view(editor *e)
{
    if (e->reveal_target) {
        ed_node_view *view = ed_view_for(e, e->reveal_target);
        if (view) {
            ImVec2 node_center = ed_scale(ed_add(view->min, view->max), 0.5f);
            ImVec2 canvas_center = ed_add(e->canvas_min, ed_scale(e->canvas_size, 0.5f));
            e->pan = ed_add(e->pan, ed_sub(canvas_center, node_center));
        } else {
            e->pan = ed_sub(
                ed_scale(e->canvas_size, 0.5f),
                ed_scale((ImVec2){ e->reveal_target->x, e->reveal_target->y }, e->zoom)
            );
        }
        e->reveal_target = NULL;
        e->view_initialized = 1;
        return;
    }
    if (e->view_initialized) return;
    ImVec2 min = { 0.0f, 0.0f };
    ImVec2 max = { 0.0f, 0.0f };
    if (e->view_count > 0) {
        min.x = min.y = 1e30f;
        max.x = max.y = -1e30f;
        for (int i = 0; i < e->view_count; i++) {
            ImVec2 a = ed_screen_to_world(e, e->views[i].min);
            ImVec2 b = ed_screen_to_world(e, e->views[i].max);
            if (a.x < min.x) min.x = a.x;
            if (a.y < min.y) min.y = a.y;
            if (b.x > max.x) max.x = b.x;
            if (b.y > max.y) max.y = b.y;
        }
    }
    ImVec2 center = { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
    e->pan = ed_sub(ed_scale(e->canvas_size, 0.5f), ed_scale(center, e->zoom));
    e->view_initialized = 1;
}

static int ed_control_down(const ImGuiIO *io)
{
    return io->KeyCtrl || ImGui_IsKeyDown(ImGuiKey_LeftCtrl) ||
           ImGui_IsKeyDown(ImGuiKey_RightCtrl);
}

static int ed_selection_modifier(const ImGuiIO *io)
{
    return ed_control_down(io) || io->KeySuper ||
           ImGui_IsKeyDown(ImGuiKey_LeftSuper) ||
           ImGui_IsKeyDown(ImGuiKey_RightSuper);
}

static void ed_handle_input(editor *e)
{
    ImGuiIO *io = ImGui_GetIO();
    ImVec2 mouse = io->MousePos;
    int hovered = ImGui_IsWindowHovered(0) &&
                  ed_point_in_rect(mouse, e->canvas_min, ed_add(e->canvas_min, e->canvas_size));
    /* Canvas gestures use the backend state directly. ImGui_IsMouseDown()
     * applies key ownership, which can belong to a value widget while the
     * canvas gesture is already in progress. */
    int control_down = ed_control_down(io);
    int control_secondary = control_down && io->MouseDown[1];
    int mouse_down = io->MouseDown[0] || control_secondary;
    int mouse_clicked = io->MouseClicked[0] || (control_down && io->MouseClicked[1]);
    int mouse_released = io->MouseReleased[0] || io->MouseReleased[1];

    /* Continue an active gesture before checking current-frame ImGui controls.
     * A marquee may pass over a value editor without becoming that editor's
     * interaction, and must keep receiving mouse movement. */
    int pan_held = io->MouseDown[2] || (ImGui_IsKeyDown(ImGuiKey_Space) && mouse_down);
    if (e->panning) {
        if (pan_held) e->pan = ed_add(e->pan, io->MouseDelta);
        else e->panning = 0;
        return;
    }
    if (e->linking) {
        e->link_mouse = mouse;
        if (ImGui_IsKeyPressed(ImGuiKey_Escape)) {
            ed_cancel_snapshot(e);
            e->connect_error[0] = '\0';
            e->linking = 0;
            return;
        }
        if (mouse_released) {
            ed_pin_view *target = ed_hit_pin(e, mouse);
            if (target && target->n != e->link_pin.n &&
                ed_connect(e, &e->link_pin, target)) {
                e->connect_error[0] = '\0';
                ed_commit_snapshot(e);
                e->linking = 0;
            } else if (!target) {
                ImVec2 world = ed_screen_to_world(e, mouse);
                e->popup_x = world.x;
                e->popup_y = world.y;
                e->spawn_link_pending = 1;
                e->spawn_link_pin = e->link_pin;
                e->linking = 0;
                ImGui_OpenPopup("spawn_menu", 0);
            } else {
                if (e->connect_error[0]) e->connect_error_pos = mouse;
                ed_cancel_snapshot(e);
                e->linking = 0;
            }
        }
        return;
    }
    if (e->dragging) {
        if (!mouse_released) {
            ed_update_drag(e, mouse);
        } else {
            ImVec2 delta = ed_sub(mouse, e->drag_start_mouse);
            if (delta.x != 0.0f || delta.y != 0.0f) ed_commit_snapshot(e);
            else ed_cancel_snapshot(e);
            if (!delta.x && !delta.y && e->drag_toggle_selected) {
                if (e->selected_count > 1)
                    ed_remove_selected(e, e->drag_toggle_node);
                else ed_clear_selection(e);
            }
            e->dragging = 0;
            e->drag_toggle_selected = 0;
            e->drag_toggle_node = NULL;
            e->drag_count = 0;
        }
        return;
    }
    if (e->selecting) {
        if (!mouse_released) {
            e->select_end = mouse;
            ed_select_marquee(e);
        } else {
            ed_select_marquee(e);
            e->selecting = 0;
        }
        return;
    }

    /* The canvas dummy is an ImGui item, so do not use global item-hover state
     * to gate gestures. Only real controls and active widgets block a new
     * canvas gesture. */
    if (ImGui_IsKeyPressed(ImGuiKey_Escape) && ImGui_IsAnyItemActive()) {
        ImGui_ClearActiveID();
        return;
    }
    if (hovered && mouse_clicked && !e->controls_hovered && ImGui_IsAnyItemActive())
        ImGui_ClearActiveID();
    if (ImGui_IsAnyItemActive() || e->controls_hovered) return;

    if (hovered && io->MouseWheel != 0.0f) {
        ImVec2 before = ed_screen_to_world(e, mouse);
        e->zoom *= powf(1.1f, io->MouseWheel);
        if (e->zoom < ED_MIN_ZOOM) e->zoom = ED_MIN_ZOOM;
        if (e->zoom > ED_MAX_ZOOM) e->zoom = ED_MAX_ZOOM;
        e->pan = ed_sub(ed_sub(mouse, e->canvas_min), ed_scale(before, e->zoom));
    }
    if (hovered && pan_held) {
        e->panning = 1;
        e->pan = ed_add(e->pan, io->MouseDelta);
        return;
    }
    if (hovered && ImGui_IsMouseClicked(1) && !control_down) {
        ed_open_context_menu(e, mouse);
        return;
    }
    if (!hovered || !mouse_clicked) return;

    ed_pin_view *pin = ed_hit_pin(e, mouse);
    if (pin) {
        ed_begin_authoring_edit(e);
        e->linking = 1;
        e->connect_error[0] = '\0';
        e->link_pin = *pin;
        e->link_mouse = mouse;
        e->pending_snapshot = ed_snapshot(e);
        return;
    }
    ed_node_view *v = ed_hit_node(e, mouse);
    if (v) {
        int append = ed_selection_modifier(io);
        int already_selected = ed_node_selected(e, v->n);
        e->front_node = v->n;
        e->drag_toggle_selected = append && already_selected;
        e->drag_toggle_node = e->drag_toggle_selected ? v->n : NULL;
        if (!already_selected) ed_select_node(e, v->n, append);
        if (ed_node_selected(e, v->n)) ed_begin_drag(e, mouse);
        return;
    }
    ed_link_ref link;
    if (ed_hit_link(e, mouse, &link)) {
        int append = ed_selection_modifier(io);
        if (!append) {
            e->selected_count = 0;
            e->selected_link_count = 0;
        }
        if (append && ed_link_selected(e, &link))
            ed_remove_selected_link(e, &link);
        else ed_add_selected_link(e, &link);
        return;
    }
    if (!ed_selection_modifier(io)) ed_clear_selection(e);
    e->selecting = 1;
    e->select_additive = ed_selection_modifier(io);
    e->select_start = mouse;
    e->select_end = mouse;
}

static void ed_handle_shortcuts(editor *e)
{
    ImGuiIO *io = ImGui_GetIO();
    if (!ImGui_IsWindowFocused(0)) return;
    if (ImGui_GetIO()->WantTextInput) return;
    int modifier = io->KeyCtrl || io->KeySuper;
    if (modifier && ImGui_IsKeyPressed(ImGuiKey_Z)) {
        if (io->KeyShift) ed_redo(e);
        else ed_undo(e);
    } else if (modifier && ImGui_IsKeyPressed(ImGuiKey_Y)) {
        ed_redo(e);
    } else if (modifier && ImGui_IsKeyPressed(ImGuiKey_C)) {
        ed_copy_selected(e);
    } else if (modifier && ImGui_IsKeyPressed(ImGuiKey_X)) {
        ed_cut_selected(e);
    } else if (modifier && ImGui_IsKeyPressed(ImGuiKey_V)) {
        ed_paste(e);
    } else if (modifier && ImGui_IsKeyPressed(ImGuiKey_D)) {
        ed_duplicate_selected(e);
    } else if (modifier && ImGui_IsKeyPressed(ImGuiKey_S)) {
        editor_save(e, "graph.json");
    } else if (modifier && ImGui_IsKeyPressed(ImGuiKey_O)) {
        editor_load(e, "graph.json");
    } else if (ImGui_IsKeyPressed(ImGuiKey_Delete) || ImGui_IsKeyPressed(ImGuiKey_Backspace)) {
        ed_delete_selected(e);
    }
}

editor *editor_new(graph *g)
{
    editor *e = (editor *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->g = g;
    e->ev = eval_view_new();
    e->zoom = 1.0f;
    return e;
}

void editor_tick(editor *e, double monotonic_time)
{
    if (!e) return;
    e->monotonic_time = monotonic_time;
    if (!e->run) return;
    flow_state state = flow_run_status(e->run);
    if (state == FLOW_RUNNING || state == FLOW_SUSPENDED)
        (void)flow_run_tick_at(e->run, monotonic_time);
}

void editor_free(editor *e)
{
    if (!e) return;
    ed_free_string_stack(e->undo, &e->undo_count);
    ed_free_string_stack(e->redo, &e->redo_count);
    free(e->undo);
    free(e->redo);
    free(e->pending_snapshot);
    free(e->clipboard);
    flow_run_free(e->run);
    free(e->views);
    free(e->pins);
    free(e->selected);
    free(e->selected_links);
    free(e->drag_nodes);
    eval_view_free(e->ev);
    free(e);
}

node *editor_spawn(editor *e, const node_desc *desc, float x, float y)
{
    ed_snapshot_before(e);
    node *n = graph_add_node(e->g, desc);
    if (!n) return NULL;
    n->x = x;
    n->y = y;
    return n;
}

int editor_save(const editor *e, const char *path)
{
    if (!e) return -1;
    char *json = graph_save_json(e->g);
    if (!json) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(json);
        return -1;
    }
    int ok = fputs(json, f) >= 0;
    fclose(f);
    free(json);
    return ok ? 0 : -1;
}

static int ed_read_file(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return -1;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    *out = buf;
    return 0;
}

int editor_load(editor *e, const char *path)
{
    if (!e) return -1;
    ed_begin_authoring_edit(e);
    char *json = NULL;
    if (ed_read_file(path, &json)) return -1;
    ed_snapshot_before(e);
    int result = ed_adopt_json(e, json);
    free(json);
    return result;
}

void editor_draw(editor *e)
{
    if (!e || !e->ev) return;
    ImGuiIO *io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    (void)ImGui_DockSpaceOverViewport();
    flow_state preview_state = e->run ? flow_run_status(e->run) : FLOW_IDLE;
    if (editor_preview_allowed(preview_state)) eval_view_refresh(e->ev, e->g);
    eval_view_set_run(e->ev, ed_run_live(e) ? e->run : NULL);
    ImGui_SetNextWindowSize((ImVec2){ 960.0f, 640.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Node Canvas", NULL, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui_End();
        return;
    }

    ed_draw_toolbar(e);
    /* A Run click may free a terminal flow_run and replace it. Refresh the
     * display source before layout can ask for any pin values. */
    eval_view_set_run(e->ev, ed_run_live(e) ? e->run : NULL);
    e->canvas_min = ImGui_GetCursorScreenPos();
    e->canvas_size = ImGui_GetContentRegionAvail();
    ed_build_layout(e);
    ed_center_initial_view(e);
    ImVec2 hover_mouse = io->MousePos;
    e->hovered_node = NULL;
    e->has_hovered_link = 0;
    if (ed_point_in_rect(hover_mouse, e->canvas_min, ed_add(e->canvas_min, e->canvas_size)))
        ed_update_hover(e, hover_mouse);
    e->controls_hovered = 0;
    ed_draw_canvas(e);
    ed_draw_controls(e, 0);
    ed_draw_front_nodes(e);
    ed_draw_controls(e, 1);
    const char *error = eval_view_error(e->ev);
    if (error[0]) {
        ImGui_SetCursorScreenPos(ed_add(e->canvas_min, (ImVec2){ 12.0f, 12.0f }));
        ImGui_TextColored((ImVec4){ 1.0f, 0.35f, 0.35f, 1.0f }, "Graph error: %s", error);
    }
    ed_handle_input(e);
    ed_draw_selection_overlay(e);
    ImGui_SetCursorScreenPos(e->canvas_min);
    ImGui_Dummy(e->canvas_size);
    ed_handle_shortcuts(e);
    ed_handle_popups(e);
    ImGui_End();
    ed_draw_tree(e);
    ImGui_SetNextWindowSize((ImVec2){ 420.0f, 180.0f }, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("Output Log", NULL, 0)) ed_draw_log(e);
    ImGui_End();
    ed_draw_details(e);
    ImGui_SetNextWindowSize((ImVec2){ 420.0f, 180.0f }, ImGuiCond_FirstUseEver);
    if (ImGui_Begin("Compiler Results", NULL, 0))
        ImGui_TextUnformatted("No compiler diagnostics yet.");
    ImGui_End();
}
