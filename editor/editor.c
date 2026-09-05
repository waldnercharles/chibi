#include "editor.h"

#include "../core/conv_nodes.h"
#include "../core/flow_visual.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "arrange.h"
#include "eval_view.h"
#include "preview_gate.h"
#include "ui_shell.h"

#include <assert.h>
#include <ctype.h>
// pi-lens-ignore: fatal error
#include <imgui/dcimgui.h>

CIMGUI_API void ImGui_ClearActiveID(void);

extern const unsigned char editor_nerd_symbols_font[];
extern const size_t editor_nerd_symbols_font_size;

#include <float.h>
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
#define ED_FIT_MARGIN 32.0f
#define ED_VALUE_WIDTH 86.0f
#define ED_TEXT_WIDTH 120.0f
#define ED_LOG_MAX 128
#define ED_LOG_MESSAGE 256
#define ED_TOAST_WIDTH 320.0f
#define ED_TOAST_PAD 10.0f
#define ED_TOAST_ICON 18.0f
#define ED_TOAST_GAP 8.0f
#define ED_TOAST_CLOSE 20.0f
#define ED_TOAST_DURATION 4.0
#define ED_TOAST_FADE 0.4

/* Flow pacing. */
#define ED_FLOW_DOT_SPACING 64.0f
#define ED_FLOW_CURVE_SAMPLES 32

/* Flow timing. */
#define ED_FLOW_ATTACK_HOLD 0.3
#define ED_FLOW_ATTACK_DECAY 0.4
#define ED_FLOW_SUSTAIN_HOLD 0.4
#define ED_FLOW_RELEASE 1.5

/* Flow wire sizing. */
#define ED_FLOW_SELECTED_THICKNESS 4.0f
#define ED_FLOW_HOVERED_THICKNESS 3.0f
#define ED_FLOW_ATTACK_THICKNESS 4.0f
#define ED_FLOW_SUSTAIN_THICKNESS 3.5f
#define ED_FLOW_DATA_SUSTAIN_THICKNESS 2.5f
#define ED_FLOW_RELEASE_THICKNESS 2.5f
#define ED_FLOW_RESTING_EXEC_THICKNESS 2.5f
#define ED_FLOW_RESTING_DATA_THICKNESS 1.5f

/* Flow bubble sizing. */
#define ED_FLOW_DOT_RADIUS 4.0f
#define ED_FLOW_MAX_BUBBLE_RADIUS 5.0f
#define ED_FLOW_BUBBLE_BASE_WEIGHT 1.0f
#define ED_FLOW_BUBBLE_HEAT_WEIGHT 0.65f
#define ED_FLOW_BUBBLE_MIN_LINE_RATIO 0.6f
#define ED_FLOW_BUBBLE_COLOR_BLEND 0.55f

/* Flow overview scaling. */
#define ED_FLOW_MAX_OVERVIEW_SCALE 2.0f
#define ED_FLOW_HEAT_OVERVIEW_GAIN 1.1f

enum
{
    ED_ICON_WARNING_CODEPOINT = 0xf071,
    ED_ICON_CLOSE_CODEPOINT = 0xf00d,
    ED_ICON_ADD = 0xf067,
    ED_ICON_SETTINGS = 0xf013
};

static const ImWchar ED_ICON_GLYPH_RANGES[] = { 0xf000, 0xf385, 0xf400, 0xf533, 0 };
static const char ED_ICON_WARNING[] = "\xef\x81\xb1";
static const char ED_ICON_GRAPH[] = "\xef\x86\xb2";
static const char ED_ICON_EVENT[] = "\xef\x88\x99";
static const char ED_ICON_FUNCTION[] = "\xef\x83\xa8";
static const char ED_ICON_MACRO[] = "\xef\x87\x98";
static const char ED_ICON_VARIABLE[] = "\xef\x87\x80";

typedef enum ed_pin_kind
{
    ED_PIN_INPUT,
    ED_PIN_OUTPUT
} ed_pin_kind;

typedef struct ed_node_view
{
    node *n;
    uint64_t id;
    ImVec2 min;
    ImVec2 max;
    float title_height;
    int input_first;
    int output_first;
    int draw_frame;
    editor_node_ui drawn;
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
    uint64_t from_id;
    uint64_t from_pin_id;
    uint64_t to_id;
    uint64_t to_pin_id;
} ed_link_ref;

typedef struct ed_log_entry
{
    const node *origin;
    double timestamp;
    unsigned long step;
    char message[ED_LOG_MESSAGE];
} ed_log_entry;

typedef struct ed_graph_view
{
    uint64_t graph_id;
    ImVec2 pan;
    float zoom;
    int view_initialized;
    int entry_index;
} ed_graph_view;

static int ed_event_add_declared_params(editor *e, node *n, const editor_event_signature *signature);
static const flow_traversal *ed_latest_flow_record(const editor *e, const edge *ed);

struct editor
{
    behavior_asset *bp;
    editor_ui_context ui_context;
    int graph_visible;
    editor_save_handler save_handler;
    void *save_user;
    graph *active_graph;
    ed_graph_view *graph_views;
    eval_view *ev;
    ImFont *icon_font;
    behavior_asset *preview_behavior_asset;
    behavior_instance *instance; /* preview instance, or NULL after an explicit reset */
    const behavior_instance *inspection_instance; /* borrowed runtime target */
    const flow_run *inspection_run; /* borrowed runtime activation */
    const behavior_instance_trace_entry *inspection_trace;
    int inspection_trace_count;
    unsigned instance_generation;
    unsigned edit_revision;
    const behavior_asset *edit_revision_bp;
    char *edit_revision_snapshot;
    char *saved_snapshot;
    int edit_revision_initialized;
    int edit_pending;
    flow_run *activation; /* selected activation the toolbar controls */
    compile_report compile;
    int instance_destroyed; /* the preview was reset or has not been created */
    int entry_index;
    double monotonic_time;
    double execution_pulse_time;
    uint64_t execution_pulse_graph_id;
    int execution_pulse_active;
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
    ed_pin_view *pins;
    node **selected;
    ed_link_ref *selected_links;

    int selecting;
    int select_additive;
    int panning;
    ImVec2 select_start;
    ImVec2 select_end;

    int dragging;
    int dragging_handle;
    int handle_edge;
    int handle_side;
    int drag_toggle_selected;
    node *drag_toggle_node;
    ImVec2 drag_start_mouse;
    ed_drag_node *drag_nodes;

    int linking;
    ed_pin_view link_pin;
    ImVec2 link_mouse;

    char *pending_snapshot;
    int edit_history_open;
    int edit_changed;
    char edit_text[1024];
    node *edit_text_node;
    int edit_text_kind;
    int edit_text_index;
    char *clipboard;
    char search[64];
    char behavior_asset_search[128];
    int search_selected;
    float popup_x;
    float popup_y;
    node *context_node;
    graph *context_graph;
    char graph_rename[256];
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
    double connect_error_until;
    behavior_variable *selected_variable;
    int details_history_open;
    char details_title[256];
    char details_comment[512];
    const editor_event_signature *event_catalog;
    int event_catalog_count;

    char **undo;
    char **redo;
};

static ImVec2 ed_add(ImVec2 a, ImVec2 b)
{
    return (ImVec2){ a.x + b.x, a.y + b.y };
}

static ImVec2 ed_sub(ImVec2 a, ImVec2 b)
{
    return (ImVec2){ a.x - b.x, a.y - b.y };
}

static ImVec2 ed_scale(ImVec2 a, float s)
{
    return (ImVec2){ a.x * s, a.y * s };
}

static float ed_max(float a, float b)
{
    return a > b ? a : b;
}

static float ed_min(float a, float b)
{
    return a < b ? a : b;
}

static float ed_abs(float a)
{
    return a < 0.0f ? -a : a;
}

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
    if (display < NODE_DISPLAY_PURE || display > NODE_DISPLAY_LITERAL) {
        display = NODE_DISPLAY_PURE;
    }
    return ED_NODE_TITLES[display];
}

static const char *ed_node_title(const node *n, char *buf, size_t cap)
{
    if (n->reference_name && n->reference_name[0] &&
        (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable ||
         n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
         n->desc == &NODE_CallMacro)) {
        snprintf(buf, cap, "%s %s", n->desc->name, n->reference_name);
        return buf;
    }
    return n->desc->name;
}

static int ed_event_name_used(const editor *e, const char *name)
{
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        const node *n = e->active_graph->nodes[i];
        if (n->desc->flow_kind == NODE_FLOW_EVENT && n->reference_name &&
            strcmp(n->reference_name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ed_name_spawned_event(editor *e, node *n)
{
    if (!n->desc || n->desc->flow_kind != NODE_FLOW_EVENT || !e->event_catalog_count) {
        return 0;
    }
    for (int i = 0; i < e->event_catalog_count; i++) {
        const editor_event_signature *signature = &e->event_catalog[i];
        if (ed_event_name_used(e, signature->name)) {
            continue;
        }
        if (graph_set_node_reference(e->active_graph, n, 0, signature->name) != 0) {
            return -1;
        }
        return ed_event_add_declared_params(e, n, signature) == signature->param_count
                 ? 0
                 : -1;
    }
    return -1;
}

static ImU32 ed_color(ImVec4 c)
{
    int r = (int)(c.x * 255.0f);
    int g = (int)(c.y * 255.0f);
    int b = (int)(c.z * 255.0f);
    int a = (int)(c.w * 255.0f);
    if (r < 0) {
        r = 0;
    }
    if (r > 255) {
        r = 255;
    }
    if (g < 0) {
        g = 0;
    }
    if (g > 255) {
        g = 255;
    }
    if (b < 0) {
        b = 0;
    }
    if (b > 255) {
        b = 255;
    }
    if (a < 0) {
        a = 0;
    }
    if (a > 255) {
        a = 255;
    }
    return IM_COL32(r, g, b, a);
}

static ImU32 ed_dim_color(ImU32 c, float factor)
{
    int r = (int)((c & 255u) * factor);
    int g = (int)(((c >> 8) & 255u) * factor);
    int b = (int)(((c >> 16) & 255u) * factor);
    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }
    return IM_COL32(r, g, b, (c >> 24) & 255u);
}

static void ed_free_string_stack(char ***stack)
{
    for (int i = 0; i < acount(*stack); i++) {
        free((*stack)[i]);
    }
    afree(*stack);
}

static void ed_push_string(char ***stack, char *value)
{
    if (!value) {
        return;
    }
    if (acount(*stack) == ED_HIST_MAX) {
        free((*stack)[0]);
        memmove(*stack, *stack + 1, (size_t)(ED_HIST_MAX - 1) * sizeof **stack);
        (*stack)[ED_HIST_MAX - 1] = value;
        return;
    }
    apush(*stack, value);
}

static int ed_link_equal(const ed_link_ref *a, const ed_link_ref *b)
{
    return a->from_id == b->from_id && a->from_pin_id == b->from_pin_id &&
           a->to_id == b->to_id && a->to_pin_id == b->to_pin_id;
}

static void ed_clear_selection(editor *e)
{
    aclear(e->selected);
    aclear(e->selected_links);
}

static ed_graph_view *ed_graph_view_for(editor *e, uint64_t graph_id, int create)
{
    for (int i = 0; i < acount(e->graph_views); i++) {
        if (e->graph_views[i].graph_id == graph_id) {
            return &e->graph_views[i];
        }
    }
    if (!create) {
        return NULL;
    }
    apush(e->graph_views, (ed_graph_view){ .graph_id = graph_id, .zoom = 1.0f });
    return &alast(e->graph_views);
}

static void ed_save_graph_view(editor *e)
{
    if (!e->active_graph) {
        return;
    }
    ed_graph_view *view = ed_graph_view_for(e, e->active_graph->graph_id, 1);
    view->pan = e->pan;
    view->zoom = e->zoom;
    view->view_initialized = e->view_initialized;
    view->entry_index = e->entry_index;
}

static void ed_load_graph_view(editor *e)
{
    if (!e->active_graph) {
        e->pan = (ImVec2){ 0.0f, 0.0f };
        e->zoom = 1.0f;
        e->view_initialized = 0;
        e->entry_index = 0;
        return;
    }
    ed_graph_view *view = ed_graph_view_for(e, e->active_graph->graph_id, 0);
    if (!view) {
        e->pan = (ImVec2){ 0.0f, 0.0f };
        e->zoom = 1.0f;
        e->view_initialized = 0;
        e->entry_index = 0;
        return;
    }
    e->pan = view->pan;
    e->zoom = view->zoom;
    e->view_initialized = view->view_initialized;
    e->entry_index = view->entry_index;
}

static int ed_switch_graph(editor *e, graph *graph)
{
    if (!e || !graph || graph->owner != e->bp) {
        return -1;
    }
    if (e->active_graph == graph) {
        return 0;
    }
    ed_save_graph_view(e);
    e->active_graph = graph;
    ed_load_graph_view(e);
    ed_clear_selection(e);
    e->selected_variable = NULL;
    e->reveal_target = NULL;
    e->context_node = NULL;
    e->has_context_link = 0;
    e->front_node = NULL;
    e->hovered_node = NULL;
    e->has_hovered_link = 0;
    e->spawn_link_pending = 0;
    e->dragging = 0;
    e->linking = 0;
    aclear(e->drag_nodes);
    return 0;
}

static int ed_link_selected(const editor *e, const ed_link_ref *link)
{
    for (int i = 0; i < acount(e->selected_links); i++) {
        if (ed_link_equal(link, &e->selected_links[i])) {
            return 1;
        }
    }
    return 0;
}

static void ed_add_selected_link(editor *e, const ed_link_ref *link)
{
    if (ed_link_selected(e, link)) {
        return;
    }
    apush(e->selected_links, *link);
}

static void ed_remove_selected_link(editor *e, const ed_link_ref *link)
{
    for (int i = 0; i < acount(e->selected_links); i++) {
        if (!ed_link_equal(link, &e->selected_links[i])) {
            continue;
        }
        adel(e->selected_links, i);
        return;
    }
}

static int ed_node_selected(const editor *e, const node *n)
{
    for (int i = 0; i < acount(e->selected); i++) {
        if (e->selected[i] == n) {
            return 1;
        }
    }
    return 0;
}

static void ed_add_selected(editor *e, node *n)
{
    if (ed_node_selected(e, n)) {
        return;
    }
    apush(e->selected, n);
}

static void ed_remove_selected(editor *e, node *n)
{
    for (int i = 0; i < acount(e->selected); i++) {
        if (e->selected[i] != n) {
            continue;
        }
        adel(e->selected, i);
        return;
    }
}

static void ed_select_node(editor *e, node *n, int append)
{
    e->selected_variable = NULL;
    if (!append) {
        aclear(e->selected);
        aclear(e->selected_links);
    }
    if (n) {
        ed_add_selected(e, n);
        e->front_node = n;
    }
}

static void ed_select_variable(editor *e, behavior_variable *v)
{
    ed_clear_selection(e);
    e->selected_variable = v;
}

static void ed_center_initial_view(editor *e);

static void ed_log_sink(const node *origin, const char *message, void *user)
{
    editor *e = (editor *)user;
    if (!e) {
        return;
    }
    if (e->log_count == ED_LOG_MAX) {
        memmove(e->log, e->log + 1, (size_t)(ED_LOG_MAX - 1) * sizeof *e->log);
    }
    ed_log_entry *entry = &e->log[e->log_count < ED_LOG_MAX ? e->log_count++ : ED_LOG_MAX - 1];
    entry->origin = origin;
    entry->timestamp = e->monotonic_time - e->run_started_at;
    entry->step = e->activation ? flow_run_steps(e->activation) : 0;
    snprintf(entry->message, sizeof entry->message, "%s", message ? message : "");
}

void editor_select_node(editor *e, node *n)
{
    if (e) {
        ed_select_node(e, n, 0);
    }
}

void editor_reveal_node(editor *e, node *n)
{
    if (!e || !n) {
        return;
    }
    ed_select_node(e, n, 0);
    e->pan = ed_sub(ed_scale(e->canvas_size, 0.5f), ed_scale((ImVec2){ n->x, n->y }, e->zoom));
    e->reveal_target = n;
    e->view_initialized = 0;
    editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
}

int editor_reveal_node_by_id(editor *e, uint64_t graph_id, uint64_t node_id)
{
    if (!e || editor_open_graph(e, graph_id) != 0) {
        return -1;
    }
    node *n = graph_node_by_id(e->active_graph, node_id);
    if (!n) {
        return -1;
    }
    editor_reveal_node(e, n);
    return 0;
}

static ImVec4 ed_spec_color(const pin_spec *spec)
{
    return spec->kind == PIN_EXEC ? (ImVec4){ 1.0f, 0.55f, 0.20f, 1.0f }
                                  : ed_pin_color(spec->type);
}

static ImFont *ed_load_icon_font(void)
{
    ImFontAtlas *atlas = ImGui_GetIO()->Fonts;
    if (!atlas->Fonts.Size) {
        ImFontAtlas_AddFontDefaultVector(atlas, NULL);
    }
    ImFontConfig config = { 0 };
    config.FontDataOwnedByAtlas = false;
    config.MergeMode = true;
    config.GlyphMaxAdvanceX = FLT_MAX;
    config.RasterizerMultiply = 1.0f;
    config.RasterizerDensity = 1.0f;
    config.ExtraSizeScale = 1.0f;
    return ImFontAtlas_AddFontFromMemoryTTF(
        ImGui_GetIO()->Fonts,
        (void *)editor_nerd_symbols_font,
        (int)editor_nerd_symbols_font_size,
        16.0f,
        &config,
        ED_ICON_GLYPH_RANGES
    );
}

static void ed_draw_centered_icon(
    ImDrawList *dl,
    ImFont *font,
    float font_size,
    ImVec2 min,
    ImVec2 max,
    ImU32 color,
    ImWchar codepoint
)
{
    ImFontBaked *baked = ImFont_GetFontBaked(font, font_size);
    ImFontGlyph *glyph = ImFontBaked_FindGlyphNoFallback(baked, codepoint);
    assert(glyph);
    float scale = font_size / baked->Size;
    float width = (glyph->X1 - glyph->X0) * scale;
    float height = (glyph->Y1 - glyph->Y0) * scale;
    ImVec2 pos = { min.x + (max.x - min.x - width) * 0.5f - glyph->X0 * scale,
                   min.y + (max.y - min.y - height) * 0.5f - glyph->Y0 * scale };
    ImFont_RenderChar(font, dl, font_size, pos, color, codepoint);
}

static int ed_point_in_rect(ImVec2 p, ImVec2 min, ImVec2 max);

static int ed_connect_error_rect(const editor *e, ImVec2 *min, ImVec2 *max, ImVec2 *close_min, ImVec2 *close_max)
{
    if (!e->connect_error[0] ||
        e->monotonic_time >= e->connect_error_until + ED_TOAST_FADE) {
        return 0;
    }
    float width = e->canvas_size.x < ED_TOAST_WIDTH ? e->canvas_size.x : ED_TOAST_WIDTH;
    float text_width = width - ED_TOAST_PAD * 2.0f - ED_TOAST_ICON -
                       ED_TOAST_GAP - ED_TOAST_CLOSE;
    float font_size = ImGui_GetFontSize();
    if (text_width < font_size) {
        text_width = font_size;
    }
    float text_height = ImGui_CalcTextSizeEx(e->connect_error, NULL, false, text_width)
                            .y;
    float height = ED_TOAST_PAD * 2.0f;
    height += text_height > ED_TOAST_ICON ? text_height : ED_TOAST_ICON;
    ImVec2 canvas_max = ed_add(e->canvas_min, e->canvas_size);
    ImVec2 pos = ed_add(e->connect_error_pos, (ImVec2){ 12.0f, 12.0f });
    if (pos.x + width > canvas_max.x) {
        pos.x = canvas_max.x - width;
    }
    if (pos.y + height > canvas_max.y) {
        pos.y = canvas_max.y - height;
    }
    if (pos.x < e->canvas_min.x) {
        pos.x = e->canvas_min.x;
    }
    if (pos.y < e->canvas_min.y) {
        pos.y = e->canvas_min.y;
    }
    *min = pos;
    *max = ed_add(pos, (ImVec2){ width, height });
    *close_min = (ImVec2){ max->x - ED_TOAST_PAD - ED_TOAST_CLOSE,
                           min->y + ED_TOAST_PAD };
    *close_max = (ImVec2){ max->x - ED_TOAST_PAD, close_min->y + ED_TOAST_CLOSE };
    return 1;
}

static void ed_clear_connect_error(editor *e)
{
    e->connect_error[0] = '\0';
}

static int ed_connect_error_alpha(const editor *e)
{
    double remaining = e->connect_error_until - e->monotonic_time;
    if (remaining >= 0.0) {
        return 255;
    }
    double fade_remaining = ED_TOAST_FADE + remaining;
    if (fade_remaining <= 0.0) {
        return 0;
    }
    return (int)(255.0 * fade_remaining / ED_TOAST_FADE);
}

static void ed_draw_connect_error(const editor *e)
{
    ImVec2 min, max, close_min, close_max;
    if (!ed_connect_error_rect(e, &min, &max, &close_min, &close_max)) {
        return;
    }
    int alpha = ed_connect_error_alpha(e);
    if (!alpha) {
        return;
    }
    ImDrawList *dl = ImGui_GetWindowDrawList();
    int close_hovered = ed_point_in_rect(ImGui_GetIO()->MousePos, close_min, close_max);
    if (close_hovered) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList_PushClipRect(dl, e->canvas_min, ed_add(e->canvas_min, e->canvas_size), true);
    ImDrawList_AddRectFilledEx(dl, min, max, IM_COL32(24, 27, 34, (int)(alpha * 0.94f)), 6.0f, ImDrawFlags_RoundCornersAll);
    ImDrawList_AddRectEx(dl, min, max, IM_COL32(210, 74, 74, alpha), 6.0f, ImDrawFlags_RoundCornersAll, 1.0f);
    if (e->icon_font) {
        ImDrawList_AddTextImFontPtr(
            dl,
            e->icon_font,
            ED_TOAST_ICON,
            (ImVec2){ min.x + ED_TOAST_PAD, min.y + ED_TOAST_PAD },
            IM_COL32(210, 74, 74, alpha),
            ED_ICON_WARNING
        );
    } else {
        ImVec2 icon_center = { min.x + ED_TOAST_PAD + ED_TOAST_ICON * 0.5f,
                               min.y + ED_TOAST_PAD + ED_TOAST_ICON * 0.5f };
        ImDrawList_AddCircleFilled(dl, icon_center, ED_TOAST_ICON * 0.5f, IM_COL32(210, 74, 74, alpha), 0);
        ImDrawList_AddText(
            dl,
            (ImVec2){ icon_center.x - 2.0f, icon_center.y - 9.0f },
            IM_COL32(255, 242, 242, alpha),
            "!"
        );
    }
    ImDrawList_AddTextImFontPtrEx(
        dl,
        ImGui_GetFont(),
        ImGui_GetFontSize(),
        (ImVec2){ min.x + ED_TOAST_PAD + ED_TOAST_ICON + ED_TOAST_GAP, min.y + ED_TOAST_PAD },
        IM_COL32(255, 224, 224, alpha),
        e->connect_error,
        NULL,
        max.x - min.x - ED_TOAST_PAD * 2.0f - ED_TOAST_ICON - ED_TOAST_GAP - ED_TOAST_CLOSE,
        NULL
    );
    ImDrawList_AddRectFilledEx(
        dl,
        close_min,
        close_max,
        IM_COL32(180, 50, 55, close_hovered ? (int)(alpha * 0.78f) : (int)(alpha * 0.16f)),
        4.0f,
        ImDrawFlags_RoundCornersAll
    );
    ImU32 close_color = IM_COL32(255, close_hovered ? 245 : 190, close_hovered ? 245 : 190, alpha);
    if (e->icon_font) {
        ed_draw_centered_icon(dl, e->icon_font, ED_TOAST_CLOSE, close_min, close_max, close_color, ED_ICON_CLOSE_CODEPOINT);
    } else {
        ImDrawList_AddLineEx(
            dl,
            ed_add(close_min, (ImVec2){ 5.0f, 5.0f }),
            ed_add(close_max, (ImVec2){ -5.0f, -5.0f }),
            close_color,
            close_hovered ? 2.5f : 2.0f
        );
        ImDrawList_AddLineEx(
            dl,
            ed_add(close_min, (ImVec2){ 5.0f, ED_TOAST_CLOSE - 5.0f }),
            ed_add(close_max, (ImVec2){ -5.0f, -ED_TOAST_CLOSE + 5.0f }),
            close_color,
            close_hovered ? 2.5f : 2.0f
        );
    }
    ImDrawList_PopClipRect(dl);
}

static const char *ed_pin_type_name(const pin_spec *spec)
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

static ImVec2 ed_world_to_screen(const editor *e, float x, float y)
{
    return ed_add(e->canvas_min, ed_add(e->pan, ed_scale((ImVec2){ x, y }, e->zoom)));
}

static ImVec2 ed_screen_to_world(const editor *e, ImVec2 p)
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

static int ed_edge_index(const editor *e, const ed_link_ref *link)
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

static int ed_pin_editable(const graph *g, const node *n, int kind, int pin)
{
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    if (spec->kind != PIN_DATA) {
        return 0;
    }
    return kind == ED_PIN_INPUT ? !ed_input_connected(g, n, pin)
                                : n->desc->pure_kind == NODE_PURE_CONST;
}

static float ed_pin_text_width(const editor *e, const graph *g, const node *n, int kind, int pin)
{
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    ImVec2 label = ImGui_CalcTextSize(spec->name);
    if (ed_pin_editable(g, n, kind, pin)) {
        return label.x + ED_VALUE_WIDTH + 16.0f;
    }

    const char *value = kind == ED_PIN_INPUT ? eval_view_input(e->ev, g, n, pin)
                                             : eval_view_output(e->ev, n, pin);
    if (value && value[0]) {
        ImVec2 val = ImGui_CalcTextSize(value);
        label.x += 4.0f + val.x;
    }
    return label.x;
}

static ImVec2 ed_node_dimensions(const editor *e, const graph *g, const node *n)
{
    float left = 0.0f;
    float right = 0.0f;
    for (int p = 0; p < n->input_count; p++) {
        left = ed_max(left, ed_pin_text_width(e, g, n, ED_PIN_INPUT, p));
    }
    for (int p = 0; p < n->output_count; p++) {
        right = ed_max(right, ed_pin_text_width(e, g, n, ED_PIN_OUTPUT, p));
    }

    char node_title[256];
    ImVec2 title_size = ImGui_CalcTextSize(ed_node_title(n, node_title, sizeof node_title));
    float width = ed_max(title_size.x, left + right + 28.0f) + 2.0f * ED_PAD;
    float line = ImGui_GetTextLineHeight();
    float row_height = line + 6.0f;
    float title_height = line + 2.0f * ED_TITLE_PAD;
    int rows = n->input_count > n->output_count ? n->input_count : n->output_count;
    float height = title_height + ED_PAD + rows * row_height + ED_PAD;
    return (ImVec2){ width, height };
}

static void ed_build_layout(editor *e)
{
    int pin_total = 0;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        const node *n = e->active_graph->nodes[i];
        pin_total += n->input_count + n->output_count;
    }
    ed_grow_views(e, acount(e->active_graph->nodes), pin_total);

    const float line = ImGui_GetTextLineHeight();
    int pin_at = 0;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        ImVec2 dimensions = ed_node_dimensions(e, e->active_graph, n);
        float title_height = line + 2.0f * ED_TITLE_PAD;
        float row_height = line + 6.0f;
        ImVec2 min = ed_world_to_screen(e, n->x, n->y);

        ImVec2 max = ed_add(min, ed_scale(dimensions, e->zoom));

        ed_node_view *v = &e->views[i];
        v->n = n;
        v->id = n->id;
        v->draw_frame = -1;
        v->drawn = (editor_node_ui){ 0 };
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
    asetlen(e->views, acount(e->active_graph->nodes));
    asetlen(e->pins, pin_at);
}

static int ed_point_in_rect(ImVec2 p, ImVec2 min, ImVec2 max)
{
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

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

static ed_pin_view *ed_hit_pin(editor *e, ImVec2 p)
{
    float radius = ed_max(ED_PIN_RADIUS * e->zoom + 4.0f, 6.0f);
    for (int i = acount(e->pins) - 1; i >= 0; i--) {
        ed_pin_view *pin = &e->pins[i];
        if (ed_abs(pin->pos.x - p.x) <= radius && ed_abs(pin->pos.y - p.y) <= radius) {
            return pin;
        }
    }
    return NULL;
}

static ed_node_view *ed_hit_node(editor *e, ImVec2 p)
{
    for (int i = acount(e->views) - 1; i >= 0; i--) {
        ed_node_view *v = &e->views[i];
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

static ImVec4 ed_lerp4(ImVec4 a, ImVec4 b, float t)
{
    return (ImVec4){ a.x + (b.x - a.x) * t,
                     a.y + (b.y - a.y) * t,
                     a.z + (b.z - a.z) * t,
                     a.w + (b.w - a.w) * t };
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

static float ed_distance(ImVec2 a, ImVec2 b)
{
    ImVec2 d = ed_sub(b, a);
    return sqrtf(d.x * d.x + d.y * d.y);
}

static float ed_curve_length(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3)
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

static ImVec2 ed_curve_at_distance(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float distance, float length)
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

static float ed_flow_heat(double age, float *thickness, ImVec4 *color, ImVec4 base, int exec)
{
    static const ImVec4 attack = { 1.0f, 0.05f, 0.0f, 1.0f };
    static const ImVec4 sustain = { 1.0f, 0.7f, 0.4f, 1.0f };
    static const ImVec4 resting = { 0.5f, 0.5f, 0.5f, 1.0f };
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
    ImVec4 envelope = ed_lerp4(resting, ed_lerp4(sustain, attack, attack_alpha), sustain_alpha);
    *color = exec ? (ImVec4){ base.x * envelope.x,
                              base.y * envelope.y,
                              base.z * envelope.z,
                              base.w * envelope.w }
                  : ed_lerp4(base, ed_lerp4(base, sustain, 0.45f), sustain_alpha);
    return sustain_alpha;
}

static int ed_link_curve(const editor *e, const edge *ed, ImVec2 *p0, ImVec2 *p1, ImVec2 *p2, ImVec2 *p3)
{
    node *from_node = graph_node_by_id(e->active_graph, ed->from_id);
    node *to_node = graph_node_by_id(e->active_graph, ed->to_id);
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

static int ed_hit_handle(const editor *e, ImVec2 mouse, int *edge_index, int *side)
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

static void ed_route_segment(
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

static int ed_hit_link_segment(const editor *e, ImVec2 p, ed_link_ref *out, int *route_segment)
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

static int ed_hit_link(const editor *e, ImVec2 p, ed_link_ref *out)
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
{
    return ImGui_GetFontSize() * e->zoom;
}

static ImVec2 ed_text_size(const editor *e, const char *text)
{
    return ed_scale(ImGui_CalcTextSize(text), e->zoom);
}

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
    if (changed) {
        e->edit_changed = 1;
    }
    if (e->edit_history_open && ImGui_IsItemDeactivated()) {
        if (e->edit_changed) {
            ed_commit_snapshot(e);
        } else {
            ed_cancel_snapshot(e);
        }
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
                char *copy = graph_own_str(e->active_graph, e->edit_text, strlen(e->edit_text));
                if (copy) {
                    v->str = copy;
                }
            }
            if (!ImGui_IsItemActive()) {
                e->edit_text_node = NULL;
            }
            break;
        }
        case PIN_NAME: {
            if (e->edit_text_node != n || e->edit_text_kind != kind ||
                e->edit_text_index != index) {
                snprintf(e->edit_text, sizeof e->edit_text, "%s", v->str ? v->str : "");
                e->edit_text_node = n;
                e->edit_text_kind = kind;
                e->edit_text_index = index;
            }
            changed = ImGui_InputText(id, e->edit_text, sizeof e->edit_text, 0);
            if (changed) {
                char *copy = graph_own_str(e->active_graph, e->edit_text, strlen(e->edit_text));
                if (copy) {
                    v->str = copy;
                }
            }
            if (!ImGui_IsItemActive()) {
                e->edit_text_node = NULL;
            }
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
    if (changed) {
        graph_set_pin_default(e->active_graph, n, kind == ED_PIN_OUTPUT, index, v);
    }
    ed_finish_value_edit(e, changed);
    return changed;
}

static int ed_pin_type_combo(const char *label, pin_type *type)
{
    int selected = *type;
    const char *items = "Exec\0Bool\0Int8\0Byte\0Int16\0UInt16\0Integer\0"
                        "UInt32\0Integer64\0UInt64\0Real (float)\0Real "
                        "(double)\0Name\0String\0Handle\0Struct\0Enum\0\0";
    if (!ImGui_Combo(label, &selected, items)) {
        return 0;
    }
    *type = (pin_type)selected;
    return 1;
}

static int ed_pin_container_combo(const char *label, pin_container *container)
{
    int selected = *container;
    const char *items = "Single\0Array\0Set\0Map\0\0";
    if (!ImGui_Combo(label, &selected, items)) {
        return 0;
    }
    *container = (pin_container)selected;
    return 1;
}

static void ed_details_begin_mutation(editor *e)
{
    if (e->details_history_open) {
        return;
    }
    ed_begin_authoring_edit(e);
    e->pending_snapshot = ed_snapshot(e);
    e->details_history_open = 1;
}

static void ed_details_finish_mutation(editor *e, int changed)
{
    if (changed) {
        e->edit_changed = 1;
    }
    if (e->details_history_open && ImGui_IsItemDeactivated()) {
        if (e->edit_changed) {
            ed_commit_snapshot(e);
        } else {
            ed_cancel_snapshot(e);
        }
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
    snprintf(id, sizeof id, "Value##%d", index);
    if (value.type != PIN_STR) {
        ImGui_SetNextItemWidth(160.0f);
    }
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
            changed = ImGui_InputTextMultilineEx(
                id,
                e->edit_text,
                sizeof e->edit_text,
                (ImVec2){ 0.0f, 0.0f },
                ImGuiInputTextFlags_WordWrap,
                NULL,
                NULL
            );
            value.str = e->edit_text;
            break;
        }
        case PIN_NAME: {
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
        if (graph_set_pin_default(e->active_graph, n, output, index, &value) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, changed);
    return changed;
}

static void ed_draw_details_pin(editor *e, node *n, int output, int index)
{
    pin_spec *spec = output ? &n->output_pins[index] : &n->input_pins[index];
    ImGui_PushIDInt((output ? 10000 : 0) + index);
    ImGui_Separator();
    ImGui_Text(
        "%s: %s (%s)",
        spec->name ? spec->name : "Pin",
        spec->kind == PIN_EXEC ? "Exec" : pin_type_name(spec->type),
        spec->container == PIN_CONTAINER_SINGLE ? "value" : "array"
    );
    if (spec->kind == PIN_DATA) {
        ed_draw_details_default(e, n, output, index);
    }
    ImGui_PopID();
}

/* UE's Details panel treats a wire as a first-class selection.  Keep this
 * deliberately read-only: the canvas remains the place where topology is
 * edited, while the panel answers the useful "what is this connection?"
 * question without making the user hunt for either endpoint. */
static void ed_draw_details_edge_endpoint(editor *e, const char *label, uint64_t node_id, uint64_t pin_id)
{
    node *n = graph_node_by_id(e->active_graph, node_id);
    int output = 0;
    int pin_index = -1;
    const pin_spec *pin = n ? graph_pin_by_id(n, pin_id, &output, &pin_index) : NULL;
    char title[256];
    const char *node_name = n ? ed_node_title(n, title, sizeof title) : "Missing node";
    const char *pin_name = pin && pin->name ? pin->name : "Missing pin";
    ImGui_Text("%s", label);
    ImGui_SameLine();
    ImGui_Text("%s.%s", node_name, pin_name);
    if (pin) {
        ImGui_Text(
            "    %s | %s | %s",
            output ? "Output" : "Input",
            pin->kind == PIN_EXEC ? "Exec" : "Data",
            pin->kind == PIN_EXEC ? "flow" : pin_type_name(pin->type)
        );
    }
    ImGui_SameLine();
    ImGui_PushIDInt((int)(node_id ^ pin_id));
    if (ImGui_SmallButton("Focus")) {
        editor_reveal_node(e, n);
    }
    ImGui_PopID();
}

static void ed_draw_details_edge(editor *e, const ed_link_ref *ref)
{
    int edge_index = ed_edge_index(e, ref);
    if (edge_index < 0) {
        ImGui_TextUnformatted("Connection no longer exists");
        return;
    }
    const edge *ed = &e->active_graph->edges[edge_index];
    ImGui_TextUnformatted("Connection");
    ImGui_Text("Kind: %s", graph_edge_kind_name(ed->kind));
    ed_draw_details_edge_endpoint(e, "From:", ed->from_id, ed->from_pin_id);
    ed_draw_details_edge_endpoint(e, "To:", ed->to_id, ed->to_pin_id);
    if (ed->kind == GRAPH_EDGE_EXEC) {
        ImGui_Text("Execution order: %d", ed->exec_order);
    } else if (ed->conversion) {
        ImGui_Text("Conversion: %s", ed->conversion->name);
    } else {
        ImGui_TextUnformatted("Conversion: none");
    }
    ImGui_Text("Reroute points: %d", ed->reroute_count);
    const flow_traversal *record = ed_latest_flow_record(e, ed);
    ImGui_SeparatorText("Runtime");
    if (record) {
        ImGui_Text(
            "Last traversed: %.3fs ago",
            ed_max(0.0f, (float)(e->monotonic_time - record->timestamp))
        );
        ImGui_Text("Activation: %lu", record->activation);
    } else {
        ImGui_TextUnformatted("Not traversed in the current run");
    }
}

static const char *ed_unique_variable_name(const editor *e)
{
    static char buf[64];
    for (int i = 1; i < 10000; i++) {
        snprintf(buf, sizeof buf, "NewVariable%d", i);
        if (!behavior_asset_find_variable_by_name(e->bp, buf)) {
            return buf;
        }
    }
    return "NewVariable";
}

static ImVec2 ed_view_center_world(const editor *e)
{
    ImVec2 center = ed_add(e->canvas_min, ed_scale(e->canvas_size, 0.5f));
    return ed_screen_to_world(e, center);
}

static void ed_show_error(editor *e, const char *message)
{
    ed_clear_connect_error(e);
    snprintf(e->connect_error, sizeof e->connect_error, "%s", message ? message : "error");
    e->connect_error_pos = ImGui_GetIO()->MousePos;
    e->connect_error_until = e->monotonic_time + ED_TOAST_DURATION;
}

static int ed_draw_variable_default(editor *e, behavior_variable *v)
{
    pin_value value = v->default_value;
    int changed = 0;
    char id[32];
    snprintf(id, sizeof id, "Value##var");
    if (value.type != PIN_STR) {
        ImGui_SetNextItemWidth(160.0f);
    }
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
            changed = ImGui_InputTextMultilineEx(
                id,
                e->edit_text,
                sizeof e->edit_text,
                (ImVec2){ 0.0f, 0.0f },
                ImGuiInputTextFlags_WordWrap,
                NULL,
                NULL
            );
            value.str = e->edit_text;
            break;
        }
        case PIN_NAME: {
            snprintf(e->edit_text, sizeof e->edit_text, "%s", value.str ? value.str : "");
            changed = ImGui_InputText(id, e->edit_text, sizeof e->edit_text, 0);
            value.str = e->edit_text;
            break;
        }
        case PIN_FLOW:
        case PIN_STRUCT:
        case PIN_HANDLE:
        default: ImGui_TextUnformatted("(not editable)"); break;
    }
    if (changed) {
        ed_details_begin_mutation(e);
        if (behavior_asset_set_variable_default(e->bp, v->id, &value) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, changed);
    return changed;
}

static void ed_draw_details_variable(editor *e, behavior_variable *v)
{
    ImGui_Text("Variable: %s", v->name ? v->name : "?");
    ImGui_Separator();
    snprintf(e->details_title, sizeof e->details_title, "%s", v->name ? v->name : "");
    if (ImGui_InputText("Name", e->details_title, sizeof e->details_title, 0)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_rename_variable(e->bp, v->id, e->details_title) == 0) {
            e->edit_changed = 1;
        } else if (e->details_title[0]) {
            ed_show_error(e, "Variable name is empty or already in use");
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    pin_type type = v->type;
    if (ed_pin_type_combo("Type", &type)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_retype_variable(e->bp, v->id, type, pin_type_name(type)) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    pin_container container = v->container;
    if (ed_pin_container_combo("Container", &container)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_set_variable_container(e->bp, v->id, container) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    snprintf(e->details_comment, sizeof e->details_comment, "%s", v->category ? v->category : "");
    if (ImGui_InputText("Category", e->details_comment, sizeof e->details_comment, 0)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_set_variable_category(e->bp, v->id, e->details_comment) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    ed_draw_variable_default(e, v);
    ImGui_Separator();
    if (ImGui_Button("Add Get node")) {
        ed_snapshot_before(e);
        node *n = behavior_asset_add_get_node(e->bp, v->id);
        if (n) {
            ImVec2 world = ed_view_center_world(e);
            n->x = world.x - 160.0f;
            n->y = world.y;
            ed_select_node(e, n, 0);
        }
    }
    ImGui_SameLine();
    if (ImGui_Button("Add Set node")) {
        ed_snapshot_before(e);
        node *n = behavior_asset_add_set_node(e->bp, v->id);
        if (n) {
            ImVec2 world = ed_view_center_world(e);
            n->x = world.x + 160.0f;
            n->y = world.y;
            ed_select_node(e, n, 0);
        }
    }
    ImGui_Separator();
    if (ImGui_Button("Delete variable")) {
        ed_snapshot_before(e);
        if (behavior_asset_delete_variable(e->bp, v->id) == 0) {
            e->selected_variable = NULL;
        }
    }
}

static void ed_draw_details_behavior_asset(editor *e)
{
    ImGui_Text("Behavior Asset: %s", e->bp->name && e->bp->name[0] ? e->bp->name : "Unnamed");
    ImGui_Separator();
    snprintf(
        e->details_title,
        sizeof e->details_title,
        "%s",
        e->bp->name ? e->bp->name : ""
    );
    if (ImGui_InputText("Name", e->details_title, sizeof e->details_title, 0)) {
        ed_details_begin_mutation(e);
        if (behavior_asset_set_name(e->bp, e->details_title) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
}

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
        return "EventGraph";
    }
    return g->name && g->name[0] ? g->name : "Unnamed";
}

static const char *ed_variable_category(const behavior_variable *v)
{
    return v->category && v->category[0] ? v->category : "Default";
}

static int ed_variable_category_seen(const behavior_asset *bp, int index)
{
    const char *category = ed_variable_category(&bp->variables[index]);
    for (int i = 0; i < index; i++) {
        if (strcmp(category, ed_variable_category(&bp->variables[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ed_variable_category_matches(const behavior_asset *bp, int index, const char *filter)
{
    const char *category = ed_variable_category(&bp->variables[index]);
    if (ed_text_matches(category, filter)) {
        return 1;
    }
    for (int i = index; i < acount(bp->variables); i++) {
        if (strcmp(category, ed_variable_category(&bp->variables[i])) == 0 &&
            ed_text_matches(bp->variables[i].name, filter)) {
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
        if (behavior_asset_find_graph_by_name(e->bp, name) == NULL) {
            break;
        }
    }
    ed_snapshot_before(e);
    graph *g = kind == GRAPH_MACRO ? behavior_asset_add_macro(e->bp, name)
             : kind == GRAPH_EVENT ? behavior_asset_add_graph(e->bp, kind, name)
                                   : behavior_asset_add_function(e->bp, name, pure);
    if (g) {
        editor_open_graph(e, g->graph_id);
    }
    return g;
}

static node *ed_place_graph_call_at(editor *e, graph *target, ImVec2 world)
{
    if (!e->active_graph || !target) {
        return NULL;
    }
    ed_snapshot_before(e);
    node *call = target->kind == GRAPH_MACRO
                   ? behavior_asset_add_macro_call_node_to_graph(e->bp, e->active_graph, target)
                   : behavior_asset_add_function_call_node_to_graph(e->bp, e->active_graph, target);
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

static int ed_delete_graph(editor *e, graph *target)
{
    if (!target || target == e->bp->graph || target->owner != e->bp ||
        !behavior_asset_mutable(e->bp)) {
        return -1;
    }
    ed_snapshot_before(e);
    if (target == e->active_graph && ed_switch_graph(e, e->bp->graph) != 0) {
        return -1;
    }
    return behavior_asset_delete_graph(e->bp, target->graph_id);
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
    ed_tree_member_icon(
        g->kind == GRAPH_MACRO ? ED_ICON_MACRO : ED_ICON_FUNCTION,
        g->kind == GRAPH_MACRO ? (ImVec4){ 0.75f, 0.55f, 1.0f, 1.0f }
                               : (ImVec4){ 0.45f, 0.70f, 1.0f, 1.0f }
    );
    char label[192];
    snprintf(label, sizeof label, "%s##member", ed_graph_display_name(g));
    if (ImGui_SelectableEx(label, e->active_graph == g, 0, (ImVec2){ 0.0f, 0.0f })) {
        editor_open_graph(e, g->graph_id);
    }
    if (ImGui_BeginDragDropSource(0)) {
        uint64_t graph_id = g->graph_id;
        (void)ImGui_SetDragDropPayload("CB_GRAPH_CALL", &graph_id, sizeof graph_id, 0);
        ImGui_Text("Place call: %s", ed_graph_display_name(g));
        ImGui_EndDragDropSource();
    }
    int delete_requested = 0;
    int rename_requested = 0;
    if (ImGui_BeginPopupContextItemEx("member_menu", 0)) {
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
        if (ImGui_MenuItem("Place Call Node")) {
            (void)ed_place_graph_call(e, g);
            ImGui_CloseCurrentPopup();
        }
        ImGui_Separator();
        delete_requested = ImGui_MenuItem(
            g->kind == GRAPH_MACRO ? "Delete Macro" : "Delete Function"
        );
        if (delete_requested) {
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
    }
    ImGui_PopID();
    if (rename_requested) {
        ImGui_OpenPopup("rename_graph", 0);
    }
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_None)) {
        ImGui_SetTooltip("Drag to the canvas, or right-click for more actions");
    }
    return delete_requested && ed_delete_graph(e, g) == 0;
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
    if (e->active_graph == g) {
        if (e->front_node == n) {
            e->front_node = NULL;
        }
        ed_clear_selection(e);
    }
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
        if (g != e->bp->graph) {
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
            ed_tree_member_icon(ED_ICON_EVENT, (ImVec4){ 1.0f, 0.65f, 0.35f, 1.0f });
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
            if (ImGui_IsItemHovered(ImGuiHoveredFlags_None)) {
                ImGui_SetTooltip("Right-click to delete event");
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
    return delete_requested && ed_delete_graph(e, g) == 0;
}

static int ed_draw_variable_member(editor *e, behavior_variable *v, int index)
{
    ed_tree_member_icon(ED_ICON_VARIABLE, (ImVec4){ 0.80f, 0.55f, 0.35f, 1.0f });
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
        delete_requested = ImGui_MenuItem("Delete Variable");
        if (delete_requested) {
            ImGui_CloseCurrentPopup();
        }
        ImGui_EndPopup();
    }
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_None)) {
        ImGui_SetTooltip(
            "%s%s\nRight-click to delete variable",
            pin_type_name(v->type),
            v->container == PIN_CONTAINER_SINGLE ? "" : " array"
        );
    }
    if (!delete_requested) {
        return 0;
    }
    ed_snapshot_before(e);
    if (behavior_asset_delete_variable(e->bp, v->id) != 0) {
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

static void ed_draw_add_popup(editor *e)
{
    if (!ImGui_BeginPopup("my_behavior_asset_add", 0)) {
        return;
    }
    if (ImGui_MenuItem("Event Graph")) {
        (void)ed_create_graph(e, GRAPH_EVENT, 0);
        ImGui_CloseCurrentPopup();
    }
    if (ImGui_MenuItem("Function")) {
        (void)ed_create_graph(e, GRAPH_FUNCTION, 0);
        ImGui_CloseCurrentPopup();
    }
    if (ImGui_MenuItem("Pure Function")) {
        (void)ed_create_graph(e, GRAPH_FUNCTION, 1);
        ImGui_CloseCurrentPopup();
    }
    if (ImGui_MenuItem("Macro")) {
        (void)ed_create_graph(e, GRAPH_MACRO, 0);
        ImGui_CloseCurrentPopup();
    }
    if (ImGui_MenuItem("Variable")) {
        ed_snapshot_before(e);
        behavior_variable *v = behavior_asset_add_variable(
            e->bp,
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

static void ed_draw_tree(editor *e)
{
    ImGui_SetNextWindowSize((ImVec2){ 260.0f, 600.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET)) {
        ImGui_SetNextWindowFocus();
    }
    if (!ImGui_Begin("My Behavior Asset", NULL, 0)) {
        ImGui_End();
        return;
    }

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
    if (editor_ui_icon_button("my_behavior_asset_settings", ED_ICON_SETTINGS, true)) {
        ed_clear_selection(e);
        e->selected_variable = NULL;
        editor_ui_request_focus(EDITOR_UI_FOCUS_INSPECTOR);
    }
    if (ImGui_IsItemHovered(ImGuiHoveredFlags_None)) {
        ImGui_SetTooltip("Behavior Asset settings");
    }

    const char *filter = e->behavior_asset_search;
    if (filter[0]) {
        ImGui_SetNextItemOpen(true, ImGuiCond_Always);
    }
    bool graphs_open = ImGui_CollapsingHeader(
        "GRAPHS##member_graphs",
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap
    );
    if (editor_ui_right_icon_button("member_graphs_add", ED_ICON_ADD)) {
        (void)ed_create_graph(e, GRAPH_EVENT, 0);
    }
    if (graphs_open) {
        for (int i = 0; i < acount(e->bp->graphs); i++) {
            graph *g = e->bp->graphs[i];
            if (g->kind == GRAPH_EVENT && ed_draw_event_graph(e, g, i, filter)) {
                break;
            }
        }
    }

    int function_count = 0;
    int macro_count = 0;
    for (int i = 0; i < acount(e->bp->graphs); i++) {
        function_count += e->bp->graphs[i]->kind == GRAPH_FUNCTION;
        macro_count += e->bp->graphs[i]->kind == GRAPH_MACRO;
    }
    char section[64];
    snprintf(section, sizeof section, "FUNCTIONS (%d)##member_functions", function_count);
    if (filter[0]) {
        ImGui_SetNextItemOpen(true, ImGuiCond_Always);
    }
    bool functions_open = ImGui_CollapsingHeader(section, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (editor_ui_right_icon_button("member_functions_add", ED_ICON_ADD)) {
        (void)ed_create_graph(e, GRAPH_FUNCTION, 0);
    }
    if (functions_open) {
        for (int i = 0; i < acount(e->bp->graphs); i++) {
            graph *g = e->bp->graphs[i];
            if (g->kind == GRAPH_FUNCTION && ed_draw_graph_member(e, g, i, filter)) {
                break;
            }
        }
    }

    snprintf(section, sizeof section, "MACROS (%d)##member_macros", macro_count);
    if (filter[0]) {
        ImGui_SetNextItemOpen(true, ImGuiCond_Always);
    }
    bool macros_open = ImGui_CollapsingHeader(section, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (editor_ui_right_icon_button("member_macros_add", ED_ICON_ADD)) {
        (void)ed_create_graph(e, GRAPH_MACRO, 0);
    }
    if (macros_open) {
        for (int i = 0; i < acount(e->bp->graphs); i++) {
            graph *g = e->bp->graphs[i];
            if (g->kind == GRAPH_MACRO && ed_draw_graph_member(e, g, i, filter)) {
                break;
            }
        }
    }

    snprintf(
        section,
        sizeof section,
        "VARIABLES (%d)##member_variables",
        acount(e->bp->variables)
    );
    if (filter[0]) {
        ImGui_SetNextItemOpen(true, ImGuiCond_Always);
    }
    bool variables_open = ImGui_CollapsingHeader(section, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (editor_ui_right_icon_button("member_variables_add", ED_ICON_ADD)) {
        ed_snapshot_before(e);
        behavior_variable *v = behavior_asset_add_variable(
            e->bp,
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
        for (int i = 0; i < acount(e->bp->variables); i++) {
            behavior_variable *v = &e->bp->variables[i];
            const char *category = ed_variable_category(v);
            if (ed_variable_category_seen(e->bp, i) ||
                !ed_variable_category_matches(e->bp, i, filter)) {
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
                for (int j = i; j < acount(e->bp->variables); j++) {
                    behavior_variable *member = &e->bp->variables[j];
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

    ed_draw_graph_rename_popup(e);
    ImGui_End();
}

/* EVT-3 makes a duplicate name a compile error, so a name already taken by
 * another event node is offered as disabled rather than silently creating one. */
static int ed_event_name_taken(const editor *e, const node *self, const char *name)
{
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        const node *n = e->active_graph->nodes[i];
        if (n == self || n->desc->flow_kind != NODE_FLOW_EVENT || !n->reference_name) {
            continue;
        }
        if (strcmp(n->reference_name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static const editor_event_signature *ed_event_signature(const editor *e, const char *name)
{
    for (int i = 0; i < e->event_catalog_count; i++) {
        if (name && strcmp(e->event_catalog[i].name, name) == 0) {
            return &e->event_catalog[i];
        }
    }
    return NULL;
}

static int ed_event_param_count(const node *n)
{
    int count = 0;
    for (int i = 0; i < n->output_count; i++) {
        count += n->output_pins[i].kind == PIN_DATA;
    }
    return count;
}

/* Appends only the declared parameters this event is still missing, so no
 * existing pin is retyped or removed and no edge is invalidated under EVT-5. */
static int ed_event_add_declared_params(editor *e, node *n, const editor_event_signature *signature)
{
    int added = 0;
    for (int i = ed_event_param_count(n); i < signature->param_count; i++) {
        const editor_event_param *parameter = &signature->params[i];
        pin_spec pin = { parameter->name, parameter->type, parameter->type_name, PIN_DATA };
        added += graph_add_pin(e->active_graph, n, 1, &pin) >= 0;
    }
    return added;
}

static void ed_details_event_catalog(editor *e, node *n)
{
    if (!e->event_catalog_count) {
        return;
    }
    const char *current = n->reference_name && n->reference_name[0]
                            ? n->reference_name
                            : "(unnamed event)";
    if (ImGui_BeginCombo("Host events", current, 0)) {
        for (int i = 0; i < e->event_catalog_count; i++) {
            const editor_event_signature *signature = &e->event_catalog[i];
            int taken = ed_event_name_taken(e, n, signature->name);
            int selected = n->reference_name &&
                           strcmp(n->reference_name, signature->name) == 0;
            if (ImGui_SelectableEx(
                    signature->name,
                    selected,
                    taken ? ImGuiSelectableFlags_Disabled : 0,
                    (ImVec2){ 0.0f, 0.0f }
                )) {
                ed_snapshot_before(e);
                if (graph_set_node_reference(
                        e->active_graph,
                        n,
                        n->reference_id,
                        signature->name
                    ) == 0) {
                    /* A parameterless event gets the whole declared signature,
                     * which is the only case where adding pins cannot surprise. */
                    if (!ed_event_param_count(n)) {
                        ed_event_add_declared_params(e, n, signature);
                    }
                    e->edit_changed = 1;
                }
            }
            if (taken) {
                ImGui_SameLine();
                ImGui_TextUnformatted("(in use)");
            }
        }
        ImGui_EndCombo();
    }
    const editor_event_signature *signature = ed_event_signature(e, n->reference_name);
    if (!signature) {
        return;
    }
    for (int i = 0; i < signature->param_count; i++) {
        ImGui_Text(
            "  %d %s (%s)",
            i + 1,
            signature->params[i].name,
            signature->params[i].type_name
        );
    }
}

static void ed_draw_details(editor *e)
{
    ImGui_SetNextWindowSize((ImVec2){ 340.0f, 600.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_INSPECTOR)) {
        ImGui_SetNextWindowFocus();
    }
    if (!ImGui_Begin("Inspector", NULL, 0)) {
        ImGui_End();
        return;
    }
    if (e->selected_variable) {
        ed_draw_details_variable(e, e->selected_variable);
        ImGui_End();
        return;
    }
    if (acount(e->selected) == 0 && acount(e->selected_links) == 1) {
        ed_draw_details_edge(e, &e->selected_links[0]);
        ImGui_End();
        return;
    }
    if (acount(e->selected) == 0 && acount(e->selected_links) > 1) {
        ImGui_Text("%d connections selected", acount(e->selected_links));
        ImGui_TextUnformatted("Select one connection to inspect its endpoints and runtime activity.");
        ImGui_End();
        return;
    }
    if (acount(e->selected) == 0) {
        ed_draw_details_behavior_asset(e);
        ImGui_End();
        return;
    }
    if (acount(e->selected) != 1) {
        ImGui_TextUnformatted("Multiple nodes selected");
        ImGui_End();
        return;
    }
    node *n = e->selected[0];
    char node_title[256];
    ImGui_Text("%s", ed_node_title(n, node_title, sizeof node_title));
    bool disabled = n->disabled != 0;
    if (ImGui_Checkbox("Disabled", &disabled)) {
        ed_details_begin_mutation(e);
        if (graph_set_node_disabled(e->active_graph, n, disabled) == 0) {
            e->edit_changed = 1;
        }
    }
    ed_details_finish_mutation(e, ImGui_IsItemEdited());
    if (n->desc && n->desc->flow_kind == NODE_FLOW_EVENT) {
        ImGui_SeparatorText("Event signature");
        ed_details_event_catalog(e, n);
    } else if (n->desc && n->desc->flow_kind == NODE_FLOW_SEQUENCE) {
        ImGui_SeparatorText("Sequence outputs");
        if (ImGui_Button("Add output pin")) {
            char name[32];
            snprintf(name, sizeof name, "Then%d", n->output_count);
            pin_spec pin = { name, PIN_FLOW, NULL, PIN_EXEC };
            ed_snapshot_before(e);
            (void)graph_add_pin(e->active_graph, n, 1, &pin);
        }
        ImGui_SameLine();
        ImGui_BeginDisabled(n->output_count <= 2);
        if (ImGui_Button("Remove last output pin")) {
            ed_snapshot_before(e);
            (void)graph_remove_pin(e->active_graph, n, 1, n->output_count - 1);
        }
        ImGui_EndDisabled();
    }
    for (int p = 0; p < n->input_count; p++) {
        ed_draw_details_pin(e, n, 0, p);
    }
    for (int p = 0; p < n->output_count; p++) {
        ed_draw_details_pin(e, n, 1, p);
    }
    ImGui_End();
}

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

static int ed_selection_modifier(const ImGuiIO *io);

static void ed_draw_controls_for_node(editor *e, ed_node_view *view)
{
    node *n = view->n;
    for (int p = 0; p < n->input_count; p++) {
        if (!ed_pin_editable(e->active_graph, n, ED_PIN_INPUT, p)) {
            continue;
        }
        ed_pin_view *pin = &e->pins[view->input_first + p];
        ImVec2 label = ed_text_size(e, n->input_pins[p].name);
        ImVec2 pos = ed_add(
            pin->pos,
            (ImVec2){ (ED_PIN_RADIUS + 9.0f) * e->zoom + label.x,
                      -ed_font_size(e) * 0.5f - 2.0f * e->zoom }
        );
        ed_begin_value_edit(e, n, ED_PIN_INPUT, p, pos);
        ed_draw_value_editor(e, n, ED_PIN_INPUT, p, pos);
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
        ImVec2 label = ed_text_size(e, n->output_pins[p].name);
        ImVec2 pos = { pin->pos.x - (ED_PIN_RADIUS + 9.0f) * e->zoom - label.x -
                           ED_VALUE_WIDTH * e->zoom,
                       pin->pos.y - ed_font_size(e) * 0.5f - 2.0f * e->zoom };
        ed_begin_value_edit(e, n, ED_PIN_OUTPUT, p, pos);
        ed_draw_value_editor(e, n, ED_PIN_OUTPUT, p, pos);
        if (ImGui_IsItemClicked()) {
            ed_select_node(e, n, ed_selection_modifier(ImGui_GetIO()));
        }
        e->controls_hovered |= ImGui_IsItemHovered(0) || ImGui_IsItemActive();
    }
}

static void ed_draw_controls(editor *e, int on_top)
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
        IM_COL32(22, 24, 30, 255)
    );
    float spacing = ED_GRID_SPACING * e->zoom;
    if (spacing < 8.0f) {
        return;
    }
    float x = e->canvas_min.x + fmodf(e->pan.x, spacing);
    float y = e->canvas_min.y + fmodf(e->pan.y, spacing);
    for (; x <= e->canvas_min.x + e->canvas_size.x; x += spacing) {
        ImDrawList_AddLine(
            dl,
            (ImVec2){ x, e->canvas_min.y },
            (ImVec2){ x, e->canvas_min.y + e->canvas_size.y },
            IM_COL32(38, 41, 50, 255)
        );
    }
    for (; y <= e->canvas_min.y + e->canvas_size.y; y += spacing) {
        ImDrawList_AddLine(
            dl,
            (ImVec2){ e->canvas_min.x, y },
            (ImVec2){ e->canvas_min.x + e->canvas_size.x, y },
            IM_COL32(38, 41, 50, 255)
        );
    }
}

static const flow_run *ed_flow_run_for_display(const editor *e);

static int ed_flow_edge_matches(const flow_traversal *record, const edge *ed)
{
    return record->kind == ed->kind && record->from_id == ed->from_id &&
           record->from_pin_id == ed->from_pin_id &&
           record->to_id == ed->to_id && record->to_pin_id == ed->to_pin_id;
}

static const flow_traversal *ed_latest_flow_record(const editor *e, const edge *ed)
{
    const flow_run *run = ed_flow_run_for_display(e);
    if (run) {
        int count = 0;
        const flow_traversal *records = flow_run_traversals(run, &count);
        for (int i = count - 1; i >= 0; i--) {
            if (ed_flow_edge_matches(&records[i], ed)) {
                return &records[i];
            }
        }
    }
    if (!e->inspection_instance) {
        return NULL;
    }
    for (int i = e->inspection_trace_count - 1; i >= 0; i--) {
        const flow_traversal *record = &e->inspection_trace[i].traversal;
        if (ed_flow_edge_matches(record, ed)) {
            return record;
        }
    }
    return NULL;
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
    return e->inspection_instance ? e->inspection_run : e->activation;
}

static float ed_flow_overview_scale(float zoom)
{
    if (zoom <= 0.0f) {
        return ED_FLOW_MAX_OVERVIEW_SCALE;
    }
    return ed_max(1.0f, ed_min(ED_FLOW_MAX_OVERVIEW_SCALE, 1.0f / zoom));
}

static int ed_execution_pulse_active(const editor *e)
{
    return e->execution_pulse_active && e->active_graph &&
           e->execution_pulse_graph_id == e->active_graph->graph_id;
}

static ed_flow_style ed_flow_style_for_edge(const editor *e, const edge *ed, ImVec4 base, int selected, int hovered)
{
    float resting = selected ? ED_FLOW_SELECTED_THICKNESS
                             : (hovered ? ED_FLOW_HOVERED_THICKNESS
                                        : (ed->kind == GRAPH_EDGE_EXEC
                                               ? ED_FLOW_RESTING_EXEC_THICKNESS
                                               : ED_FLOW_RESTING_DATA_THICKNESS));
    ed_flow_style style = {
        .thickness = resting,
        .bubble_radius = ED_FLOW_DOT_RADIUS,
        .opacity = 0.0f,
        .bubble_opacity = 0.0f,
        .wire_color = base,
        .bubble_color = ed_lerp4(base, (ImVec4){ 1.0f, 1.0f, 1.0f, 1.0f }, ED_FLOW_BUBBLE_COLOR_BLEND),
    };
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
    float overview = ed_flow_overview_scale(e->zoom);
    float gain = 1.0f + (overview - 1.0f) * (ED_FLOW_HEAT_OVERVIEW_GAIN - 1.0f) /
                            (ED_FLOW_MAX_OVERVIEW_SCALE - 1.0f);
    float heat_delta = ed_max(0.0f, hot_thickness - resting);
    style.thickness = resting + heat_delta * gain;
    style.wire_color = ed_lerp4(base, hot_color, style.opacity);
    style.bubble_color = hot_color;
    float size_ratio = style.thickness / ED_FLOW_ATTACK_THICKNESS;
    style.bubble_radius = ED_FLOW_DOT_RADIUS * overview *
                          (ED_FLOW_BUBBLE_BASE_WEIGHT +
                           ED_FLOW_BUBBLE_HEAT_WEIGHT * size_ratio);
    style.bubble_radius = ed_min(
        ED_FLOW_MAX_BUBBLE_RADIUS,
        ed_max(style.bubble_radius, style.thickness * ED_FLOW_BUBBLE_MIN_LINE_RATIO)
    );
    style.bubble_radius *= style.opacity;
    style.bubble_opacity = 1.0f;
    return style;
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
    ImVec2 points[17 * 17];
    int count = 0;
    for (int segment = 0; segment <= ed->reroute_count; segment++) {
        ImVec2 start, c1, c2, end;
        ed_route_segment(e, ed, segment, p0, p3, &start, &c1, &c2, &end);
        for (int sample = segment ? 1 : 0; sample <= 16; sample++) {
            points[count++] = ed_bezier(start, c1, c2, end, (float)sample / 16.0f);
        }
    }
    float length = 0.0f;
    for (int i = 1; i < count; i++) {
        length += ed_distance(points[i - 1], points[i]);
    }
    float spacing = ED_FLOW_DOT_SPACING * e->zoom;
    if (length <= 0.0f || spacing <= 0.0f) {
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
}

static void ed_draw_link(editor *e, ImDrawList *dl, const edge *ed)
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
    ImVec4 base = source ? ed_spec_color(source) : ed_pin_color(PIN_FLOW);
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
    if (!ed->reroute_count) {
        ImDrawList_AddBezierCubic(dl, p0, p1, p2, p3, color, style.thickness, 0);
    } else {
        for (int segment = 0; segment <= ed->reroute_count; segment++) {
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
    double marker_age = (ed_latest_flow_record(e, ed) || ed_execution_pulse_active(e))
                          ? e->monotonic_time
                          : 0.0;
    if (ed->reroute_count) {
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

static void ed_draw_node(editor *e, ImDrawList *dl, ed_node_view *v)
{
    v->draw_frame = ImGui_GetFrameCount();
    v->drawn = (editor_node_ui){ 0 };
    node *n = v->n;
    int selected = ed_node_selected(e, n);
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
    char node_title[256];
    ed_draw_text(
        e,
        dl,
        ed_add(v->min, (ImVec2){ ED_PAD * e->zoom, ED_TITLE_PAD * e->zoom }),
        IM_COL32(235, 238, 245, 255),
        ed_node_title(n, node_title, sizeof node_title)
    );

    int rows = n->input_count > n->output_count ? n->input_count : n->output_count;
    float pin_r = ed_max(2.0f, ED_PIN_RADIUS * e->zoom);
    for (int row = 0; row < rows; row++) {
        if (row < n->input_count) {
            const pin_spec *spec = &n->input_pins[row];
            ed_pin_view *pin = &e->pins[v->input_first + row];
            ed_count_drawn_pin(v, pin, spec, dl);
            ImU32 color = ed_color(ed_spec_color(spec));
            ImDrawList_AddCircleFilled(dl, pin->pos, pin_r, color, 0);
            ImDrawList_AddCircle(dl, pin->pos, pin_r, ed_dim_color(color, 0.28f));
            ed_draw_text(
                e,
                dl,
                ed_add(pin->pos, (ImVec2){ (ED_PIN_RADIUS + 5.0f) * e->zoom, -ed_font_size(e) * 0.5f }),
                IM_COL32(220, 224, 232, 255),
                spec->name
            );
            const char *value = eval_view_input(e->ev, e->active_graph, n, row);
            if (value && !ed_pin_editable(e->active_graph, n, ED_PIN_INPUT, row)) {
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
            ed_count_drawn_pin(v, pin, spec, dl);
            ImU32 color = ed_color(ed_spec_color(spec));
            ImVec2 label = ed_text_size(e, spec->name);
            ImDrawList_AddCircleFilled(dl, pin->pos, pin_r, color, 0);
            ImDrawList_AddCircle(dl, pin->pos, pin_r, ed_dim_color(color, 0.28f));
            ed_draw_text(
                e,
                dl,
                (ImVec2){ pin->pos.x - (ED_PIN_RADIUS + 5.0f) * e->zoom - label.x,
                          pin->pos.y - ed_font_size(e) * 0.5f },
                IM_COL32(220, 224, 232, 255),
                spec->name
            );
            const char *value = eval_view_output(e->ev, n, row);
            if (value && value[0] &&
                !ed_pin_editable(e->active_graph, n, ED_PIN_OUTPUT, row)) {
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

static void ed_draw_canvas(editor *e)
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

static void ed_draw_front_nodes(editor *e)
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

static void ed_draw_selection_overlay(const editor *e)
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

static char *ed_snapshot(const editor *e)
{
    return behavior_asset_save_json(e->bp);
}

static void ed_commit_snapshot(editor *e)
{
    if (!e->pending_snapshot) {
        return;
    }
    e->edit_pending = 1;
    ed_push_string(&e->undo, e->pending_snapshot);
    e->pending_snapshot = NULL;
    ed_free_string_stack(&e->redo);
}

static void ed_snapshot_before(editor *e)
{
    ed_begin_authoring_edit(e);
    char *snapshot = ed_snapshot(e);
    if (!snapshot) {
        return;
    }
    e->edit_pending = 1;
    ed_push_string(&e->undo, snapshot);
    ed_free_string_stack(&e->redo);
}

static void ed_cancel_snapshot(editor *e)
{
    free(e->pending_snapshot);
    e->pending_snapshot = NULL;
}

static int ed_activation_live(const editor *e);

static void ed_accept_graph_call_drop(editor *e)
{
    if (!ImGui_BeginDragDropTarget()) {
        return;
    }
    const ImGuiPayload *payload = ImGui_AcceptDragDropPayload("CB_GRAPH_CALL", 0);
    if (payload && payload->Delivery && payload->DataSize == sizeof(uint64_t)) {
        uint64_t graph_id;
        memcpy(&graph_id, payload->Data, sizeof graph_id);
        graph *target = behavior_asset_find_graph(e->bp, graph_id);
        if (target && (target->kind == GRAPH_FUNCTION || target->kind == GRAPH_MACRO)) {
            (void)ed_place_graph_call_at(e, target, ed_screen_to_world(e, ImGui_GetMousePos()));
        }
    }
    ImGui_EndDragDropTarget();
}

static void ed_destroy_preview(editor *e)
{
    if (e->instance) {
        behavior_instance_destroy(e->instance);
        e->instance = NULL;
    }
    e->activation = NULL;
    behavior_asset_free(e->preview_behavior_asset);
    e->preview_behavior_asset = NULL;
    e->instance_destroyed = 1;
    eval_view_set_run(e->ev, NULL);
}

/* Every behavior_asset swap must release the preview copy and its activations. */
static int ed_adopt_behavior_asset(editor *e, const char *json)
{
    ed_destroy_preview(e);
    uint64_t active_graph_id = e->active_graph ? e->active_graph->graph_id : 0;
    ed_save_graph_view(e);
    behavior_asset *fresh = behavior_asset_load_json(json);
    if (!fresh) {
        return -1;
    }
    /* The value preview borrows nodes from the old asset. */
    eval_view_free(e->ev);
    e->ev = eval_view_new();
    asetlen(e->views, 0);
    asetlen(e->pins, 0);
    behavior_asset_free(e->bp);
    e->bp = fresh;
    e->active_graph = behavior_asset_find_graph(e->bp, active_graph_id);
    if (!e->active_graph) {
        e->active_graph = e->bp->graph;
    }
    ed_load_graph_view(e);
    compile_report_free(&e->compile);
    compile_report_init(&e->compile);
    ed_clear_selection(e);
    e->selected_variable = NULL;
    e->behavior_asset_search[0] = '\0';
    e->dragging = 0;
    e->linking = 0;
    e->reveal_target = NULL;
    e->context_node = NULL;
    e->context_graph = NULL;
    e->graph_rename[0] = '\0';
    e->has_context_link = 0;
    e->front_node = NULL;
    e->spawn_link_pending = 0;
    e->edit_text_node = NULL;
    aclear(e->drag_nodes);
    e->log_count = 0;
    e->edit_pending = 1;
    return 0;
}

static void ed_undo(editor *e)
{
    ed_begin_authoring_edit(e);
    if (acount(e->undo) == 0) {
        return;
    }
    char *target = apop(e->undo);
    char *current = behavior_asset_save_json(e->bp);
    if (current) {
        ed_push_string(&e->redo, current);
    }
    ed_adopt_behavior_asset(e, target);
    free(target);
}

static void ed_redo(editor *e)
{
    ed_begin_authoring_edit(e);
    if (acount(e->redo) == 0) {
        return;
    }
    char *target = apop(e->redo);
    char *current = behavior_asset_save_json(e->bp);
    if (current) {
        ed_push_string(&e->undo, current);
    }
    ed_adopt_behavior_asset(e, target);
    free(target);
}

static void ed_format_text_repair(editor *e, node *n);

static void ed_delete_selected(editor *e)
{
    if (acount(e->selected) == 0 && acount(e->selected_links) == 0) {
        return;
    }
    ed_snapshot_before(e);
    for (int i = acount(e->selected_links) - 1; i >= 0; i--) {
        int index = ed_edge_index(e, &e->selected_links[i]);
        if (index >= 0) {
            edge ed = e->active_graph->edges[index];
            node *from = graph_node_by_id(e->active_graph, ed.from_id);
            node *to = graph_node_by_id(e->active_graph, ed.to_id);
            int from_pin = -1, to_pin = -1, fo = 0, to_out = 0;
            if (from && to &&
                graph_pin_by_id(from, ed.from_pin_id, &fo, &from_pin) && fo &&
                graph_pin_by_id(to, ed.to_pin_id, &to_out, &to_pin) && !to_out) {
                if (graph_remove_edge(e->active_graph, from, from_pin, to, to_pin) == 0) {
                    ed_format_text_repair(e, to);
                }
            }
        }
    }
    int front_deleted = 0;
    for (int i = acount(e->selected) - 1; i >= 0; i--) {
        if (e->selected[i] == e->front_node) {
            front_deleted = 1;
        }
        graph_remove_node(e->active_graph, e->selected[i]);
    }
    if (front_deleted) {
        e->front_node = NULL;
    }
    ed_clear_selection(e);
}

static void ed_copy_selected(editor *e)
{
    if (acount(e->selected) == 0) {
        return;
    }
    char *json = graph_save_nodes_json(e->active_graph, e->selected, acount(e->selected));
    if (!json) {
        return;
    }
    ImGui_SetClipboardText(json);
    free(e->clipboard);
    e->clipboard = json;
}

static void ed_cut_selected(editor *e)
{
    ed_copy_selected(e);
    if (acount(e->selected) == 0) {
        return;
    }
    ed_delete_selected(e);
}

static void ed_paste(editor *e)
{
    const char *clipboard = ImGui_GetClipboardText();
    if (!clipboard || !clipboard[0]) {
        clipboard = e->clipboard;
    }
    if (!clipboard) {
        return;
    }
    ed_snapshot_before(e);
    int first = acount(e->active_graph->nodes);
    if (graph_append_json(e->active_graph, clipboard, 24.0f, 24.0f) == 0) {
        ed_clear_selection(e);
        for (int i = first; i < acount(e->active_graph->nodes); i++) {
            ed_add_selected(e, e->active_graph->nodes[i]);
        }
    }
}

static void ed_duplicate_selected(editor *e)
{
    if (acount(e->selected) == 0) {
        return;
    }
    char *json = graph_save_nodes_json(e->active_graph, e->selected, acount(e->selected));
    if (!json) {
        return;
    }
    ed_snapshot_before(e);
    int first = acount(e->active_graph->nodes);
    if (graph_append_json(e->active_graph, json, 24.0f, 24.0f) == 0) {
        ed_clear_selection(e);
        for (int i = first; i < acount(e->active_graph->nodes); i++) {
            ed_add_selected(e, e->active_graph->nodes[i]);
        }
    }
    free(json);
}

static void ed_begin_drag(editor *e, ImVec2 mouse)
{
    ed_begin_authoring_edit(e);
    asetlen(e->drag_nodes, acount(e->selected));
    for (int i = 0; i < acount(e->drag_nodes); i++) {
        e->drag_nodes[i] = (ed_drag_node){ e->selected[i],
                                           e->selected[i]->x,
                                           e->selected[i]->y };
    }
    e->drag_start_mouse = mouse;
    e->dragging = 1;
    e->pending_snapshot = ed_snapshot(e);
}

static void ed_update_drag(editor *e, ImVec2 mouse)
{
    ImVec2 delta = ed_scale(ed_sub(mouse, e->drag_start_mouse), 1.0f / e->zoom);
    for (int i = 0; i < acount(e->drag_nodes); i++) {
        e->drag_nodes[i].n->x = e->drag_nodes[i].x + delta.x;
        e->drag_nodes[i].n->y = e->drag_nodes[i].y + delta.y;
    }
}

static void ed_update_handle_drag(editor *e, ImVec2 mouse)
{
    edge *ed = &e->active_graph->edges[e->handle_edge];
    assert(e->handle_side >= 0 && e->handle_side < ed->reroute_count);
    ImVec2 point = ed_screen_to_world(e, mouse);
    ed->reroutes[e->handle_side].x = point.x;
    ed->reroutes[e->handle_side].y = point.y;
}

/* CAT-38: a Format Text node always carries one trailing Generic argument
 * pin; connecting the last pin adopts its type and reveals the next spare.
 * Capped so the group cannot grow without bound. */
#define ED_FMT_TEXT_MAX_ARGS 12

static int ed_format_text_generic(const node *n, int index)
{
    if (!n->desc || strcmp(n->desc->name, "Format Text") != 0 || index < 1 ||
        index >= n->input_count) {
        return 0;
    }
    const pin_spec *pin = &n->input_pins[index];
    return pin->kind == PIN_DATA && pin->type == PIN_FLOW &&
           (pin->flags & PIN_FLAG_WILDCARD);
}

static void ed_append_generic_arg(editor *e, node *n)
{
    if (!n->desc || strcmp(n->desc->name, "Format Text") != 0) {
        return;
    }
    if (n->input_count - 1 >= ED_FMT_TEXT_MAX_ARGS) {
        return;
    }
    char name[32];
    snprintf(name, sizeof name, "Arg%d", n->input_count - 1);
    pin_spec pin = { .name = name, .type = PIN_FLOW, .kind = PIN_DATA, .flags = PIN_FLAG_WILDCARD };
    graph_add_pin(e->active_graph, n, 0, &pin);
}

static void ed_format_text_repair(editor *e, node *n)
{
    if (!n->desc || strcmp(n->desc->name, "Format Text") != 0) {
        return;
    }
    int generic_count = 0;
    for (int i = 1; i < n->input_count; i++) {
        generic_count += ed_format_text_generic(n, i);
    }
    for (int i = n->input_count - 1; i > 0 && generic_count > 1; i--) {
        if (!ed_format_text_generic(n, i)) {
            continue;
        }
        if (graph_remove_pin(e->active_graph, n, 0, i) == 0) {
            generic_count--;
        }
    }
}

static void ed_format_text_ensure_spare(editor *e, node *n)
{
    if (!n->desc || strcmp(n->desc->name, "Format Text") != 0) {
        return;
    }
    for (int i = 1; i < n->input_count; i++) {
        if (ed_format_text_generic(n, i)) {
            return;
        }
    }
    ed_append_generic_arg(e, n);
}

static int ed_connection_specs_valid(const pin_spec *output, const pin_spec *input, pin_kind expected_kind)
{
    if (output->kind != expected_kind || input->kind != expected_kind) {
        return 0;
    }
    if (expected_kind != PIN_DATA || (input->flags & PIN_FLAG_WILDCARD) ||
        pin_types_compatible(output, input)) {
        return 1;
    }
    return conversion_for_pins(output, input) != NULL;
}

static int ed_connect(editor *e, const ed_pin_view *a, const ed_pin_view *b)
{
    ed_begin_authoring_edit(e);
    ed_clear_connect_error(e);
    const ed_pin_view *output = a->kind == ED_PIN_OUTPUT ? a : b;
    const ed_pin_view *input = a->kind == ED_PIN_INPUT ? a : b;
    if (output->kind != ED_PIN_OUTPUT || input->kind != ED_PIN_INPUT) {
        return 0;
    }
    const pin_spec *out_spec = &output->n->output_pins[output->index];
    const pin_spec *in_spec = &input->n->input_pins[input->index];
    const char *out_name = ed_pin_type_name(out_spec);
    const char *in_name = ed_pin_type_name(in_spec);
    if (!ed_connection_specs_valid(out_spec, in_spec, out_spec->kind)) {
        snprintf(e->connect_error, sizeof e->connect_error, "Cannot connect %s output to %s input", out_name, in_name);
        e->connect_error_until = e->monotonic_time + ED_TOAST_DURATION;
        return 0;
    }
    /* PIN-11: a Generic data input accepts any data peer and adopts its
     * type in core; it is not a type mismatch and never toasts. */
    int generic_target = out_spec->kind == PIN_DATA &&
                         (in_spec->flags & PIN_FLAG_WILDCARD);
    if (out_spec->kind == PIN_DATA && !generic_target &&
        !pin_types_compatible(out_spec, in_spec)) {
        const node_desc *conv = conversion_for_pins(out_spec, in_spec);
        assert(conv);
        return graph_connect_data_conversion(
                   e->active_graph,
                   output->n,
                   output->index,
                   input->n,
                   input->index,
                   conv
               ) == 0;
    }
    if (out_spec->kind == PIN_EXEC) {
        int order = output->n->desc->flow_kind == NODE_FLOW_SEQUENCE ? output->index
                                                                     : 0;
        return graph_connect_exec(
                   e->active_graph,
                   output->n,
                   output->index,
                   input->n,
                   input->index,
                   order
               ) == 0;
    }
    int ok = graph_connect_data(
                 e->active_graph,
                 output->n,
                 output->index,
                 input->n,
                 input->index
             ) == 0;
    if (ok && generic_target) {
        ed_format_text_ensure_spare(e, input->n);
    }
    return ok;
}

#define ED_SEARCH_MAX 128

typedef struct ed_search_match
{
    int registry_index;
    int score;
} ed_search_match;

static char ed_search_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static int ed_search_length(const char *text)
{
    int length = 0;
    while (text && text[length] && length < ED_SEARCH_MAX - 1) {
        length++;
    }
    return length;
}

static int ed_damerau_levenshtein(const char *a, const char *b)
{
    int al = ed_search_length(a), bl = ed_search_length(b);
    int pp[ED_SEARCH_MAX] = { 0 }, p[ED_SEARCH_MAX] = { 0 }, c[ED_SEARCH_MAX] = { 0 };
    for (int j = 0; j <= bl; j++) {
        p[j] = j;
    }
    for (int i = 1; i <= al; i++) {
        c[0] = i;
        for (int j = 1; j <= bl; j++) {
            int v = p[j - 1] +
                    (ed_search_lower(a[i - 1]) != ed_search_lower(b[j - 1]));
            if (c[j - 1] + 1 < v) {
                v = c[j - 1] + 1;
            }
            if (p[j] + 1 < v) {
                v = p[j] + 1;
            }
            if (i > 1 && j > 1 &&
                ed_search_lower(a[i - 1]) == ed_search_lower(b[j - 2]) &&
                ed_search_lower(a[i - 2]) == ed_search_lower(b[j - 1]) &&
                pp[j - 2] + 1 < v) {
                v = pp[j - 2] + 1;
            }
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
    for (int start = 0; start < nl; start++) {
        for (int length = low; length <= high && start + length <= nl; length++) {
            char window[ED_SEARCH_MAX];
            memcpy(window, name + start, (size_t)length);
            window[length] = '\0';
            int d = ed_damerau_levenshtein(window, filter);
            if (d < best) {
                best = d;
            }
        }
    }
    return best;
}

static int ed_name_score(const char *name, const char *filter)
{
    if (!filter || !filter[0]) {
        return 0;
    }
    int score = 0, previous = -1, first = -1, matched = 1;
    for (int fi = 0; filter[fi]; fi++) {
        int found = -1;
        for (int ni = previous + 1; name[ni]; ni++) {
            if (ed_search_lower(name[ni]) == ed_search_lower(filter[fi])) {
                found = ni;
                break;
            }
        }
        if (found < 0) {
            matched = 0;
            break;
        }
        if (first < 0) {
            first = found;
        }
        score += found == previous + 1 ? 8 : 1;
        if (found == 0 || name[found - 1] == ' ' || name[found - 1] == '_') {
            score += 5;
        }
        previous = found;
    }
    int fl = ed_search_length(filter), distance = ed_best_name_distance(name, filter);
    int limit = fl <= 3 ? 1 : fl / 3;
    if (!matched && distance > limit) {
        return -1;
    }
    if (!matched) {
        score = 0;
    }
    if (matched && first == 0) {
        score += 120;
    }
    if (matched && first > 0 && (name[first - 1] == ' ' || name[first - 1] == '_')) {
        score += 60;
    }
    if (distance == 0) {
        score += 60;
    } else if (distance <= limit) {
        score += 32 - distance * 8;
    }
    return score;
}

/* CAT-27: a descriptor's searchable text is its name plus its declared
 * keyword list. The best keyword match scores below any name match so a
 * keyword hit never outranks the node's own name. */
static int ed_desc_score(const node_desc *desc, const char *filter)
{
    int score = ed_name_score(desc->name, filter);
    if (score >= 0 || !desc->keywords) {
        return score;
    }
    int best = -1;
    const char *kw = desc->keywords;
    while (*kw) {
        while (*kw == ' ') {
            kw++;
        }
        if (!*kw) {
            break;
        }
        const char *end = kw;
        while (*end && *end != ' ') {
            end++;
        }
        char word[64];
        size_t len = (size_t)(end - kw);
        if (len >= sizeof word) {
            len = sizeof word - 1;
        }
        memcpy(word, kw, len);
        word[len] = '\0';
        int s = ed_name_score(word, filter);
        if (s > best) {
            best = s;
        }
        kw = end;
    }
    return best;
}

static const char *ed_desc_category(const node_desc *d)
{
    return d && d->category && d->category[0] ? d->category : "Basic";
}

static int ed_search_match_cmp(const void *a, const void *b)
{
    const ed_search_match *ma = (const ed_search_match *)a;
    const ed_search_match *mb = (const ed_search_match *)b;
    const char *ca = ed_desc_category(registry_node(ma->registry_index));
    const char *cb = ed_desc_category(registry_node(mb->registry_index));
    int c = strcmp(ca, cb);
    return c ? c : mb->score - ma->score;
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

/* PIN-11: a Generic (wildcard) data pin accepts any data peer; the type is
 * adopted in core on connect, so the editor treats it as compatible. */
static int ed_data_connectable(const pin_spec *source, const pin_spec *candidate)
{
    if (source->kind == PIN_EXEC) {
        return 1;
    }
    if (candidate->flags & PIN_FLAG_WILDCARD) {
        return 1;
    }
    return pin_types_compatible(source, candidate);
}

static int ed_system_node_allowed(const node_desc *desc)
{
    static const char *const names[] = {
        "Event",
        "Get_GameInput",
        "Set_GameInput",
        "Get_GameMovement",
        "Set_GameMovement",
        "Get_GameTransform",
        "Set_GameTransform",
        "Add",
        "Multiply",
        "Subtract",
        "Divide",
        "Min",
        "Max",
        "Clamp",
        "Modulo",
        "Negate",
        "Abs",
        "Delta Time",
    };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        if (desc && desc->name && strcmp(desc->name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ed_compact_system_node(editor *e, node *n)
{
    if (e->ui_context != EDITOR_UI_CONTEXT_SYSTEM || !n || !n->desc) {
        return 0;
    }
    if (n->desc == &NODE_Event || !n->desc->type) {
        return 0;
    }
    int get = strncmp(n->desc->name, "Get_", 4) == 0;
    int set = strncmp(n->desc->name, "Set_", 4) == 0;
    if (!get && !set) {
        return 0;
    }
    if (get) {
        for (int i = n->input_count - 1; i >= 0; i--) {
            if (graph_remove_pin(e->active_graph, n, 0, i) != 0) {
                return -1;
            }
        }
        for (int i = n->output_count - 1; i >= 0; i--) {
            if (graph_remove_pin(e->active_graph, n, 1, i) != 0) {
                return -1;
            }
        }
    } else {
        for (int i = n->input_count - 1; i >= 0; i--) {
            if (graph_remove_pin(e->active_graph, n, 0, i) != 0) {
                return -1;
            }
        }
        pin_spec exec = { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC };
        if (graph_add_pin(e->active_graph, n, 0, &exec) < 0) {
            return -1;
        }
    }
    int output = get;
    if (n->desc->type->alias_of) {
        pin_spec pin = {
            .name = "Value",
            .type = PIN_STRUCT,
            .type_name = n->desc->type->alias_of,
            .kind = PIN_DATA,
        };
        return graph_add_pin(e->active_graph, n, output, &pin) < 0 ? -1 : 0;
    }
    for (int i = 0; i < n->desc->type->field_count; i++) {
        const type_field *field = &n->desc->type->fields[i];
        pin_spec pin = {
            .name = field->name,
            .type = field->type,
            .type_name = field->type_name,
            .kind = PIN_DATA,
        };
        if (graph_add_pin(e->active_graph, n, output, &pin) < 0) {
            return -1;
        }
    }
    return 0;
}

static int ed_spawn_desc_compatible(const editor *e, const node_desc *desc)
{
    if (e->ui_context == EDITOR_UI_CONTEXT_SYSTEM && !ed_system_node_allowed(desc)) {
        return 0;
    }
    if (desc == &NODE_CallFunction || desc == &NODE_CallPureFunction ||
        desc == &NODE_CallMacro) {
        return 0;
    }
    if (!e->spawn_link_pending) {
        return 1;
    }
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
        if (source->kind != candidate->kind) {
            continue;
        }
        if (source->kind == PIN_EXEC ||
            (e->spawn_link_pin.kind == ED_PIN_OUTPUT
                 ? ed_data_connectable(source, candidate)
                 : ed_data_connectable(candidate, source))) {
            return 1;
        }
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
            if (a->kind == b->kind && ed_data_connectable(a, b)) {
                connected = ed_connect(e, &e->spawn_link_pin, &target);
            }
        }
    } else {
        for (int p = 0; p < n->output_count && !connected; p++) {
            ed_pin_view target = { n, ED_PIN_OUTPUT, p, { 0.0f, 0.0f } };
            const pin_spec *a = &n->output_pins[p];
            const pin_spec *b = &e->spawn_link_pin.n
                                     ->input_pins[e->spawn_link_pin.index];
            if (a->kind == b->kind && ed_data_connectable(a, b)) {
                connected = ed_connect(e, &e->spawn_link_pin, &target);
            }
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
                graph_add_pin(e->active_graph, n, 1, &pin);
            }
            if (n->output_count > 2 && ImGui_MenuItem("Remove last output pin")) {
                ed_snapshot_before(e);
                graph_remove_pin(e->active_graph, n, 1, n->output_count - 1);
            }
            ImGui_Separator();
        }
        if (n && ImGui_MenuItem(n->disabled ? "Enable" : "Disable")) {
            ed_snapshot_before(e);
            graph_set_node_disabled(e->active_graph, n, !n->disabled);
        }
        if (n && node_behavior_for(n) != NODE_BEHAVIOR_PURE &&
            ImGui_MenuItem(n->breakpoint ? "Clear Breakpoint" : "Set Breakpoint")) {
            ed_snapshot_before(e);
            graph_set_breakpoint(e->active_graph, n, !n->breakpoint);
        }
        if (n && ImGui_MenuItem(
                     n->breakpoint_enabled ? "Disable All Breakpoints" : "Enable All Breakpoints"
                 )) {
            ed_snapshot_before(e);
            graph_set_all_breakpoints(e->active_graph, !e->active_graph->breakpoints_enabled);
        }
        if (n && ImGui_MenuItem("Toggle data watches")) {
            ed_snapshot_before(e);
            for (int i = 0; i < n->input_count; i++) {
                if (n->input_pins[i].kind == PIN_DATA) {
                    graph_set_pin_watch(
                        e->active_graph,
                        n,
                        0,
                        i,
                        !(n->input_pins[i].flags & PIN_FLAG_WATCH)
                    );
                }
            }
            for (int i = 0; i < n->output_count; i++) {
                if (n->output_pins[i].kind == PIN_DATA) {
                    graph_set_pin_watch(
                        e->active_graph,
                        n,
                        1,
                        i,
                        !(n->output_pins[i].flags & PIN_FLAG_WATCH)
                    );
                }
            }
        }
        if (n && ImGui_MenuItem("Split first struct pin")) {
            for (int i = 0; i < n->input_count; i++) {
                if (n->input_pins[i].type == PIN_STRUCT) {
                    ed_snapshot_before(e);
                    graph_split_pin(e->active_graph, n, 0, i);
                    break;
                }
            }
        }
        if (n && ImGui_MenuItem("Recombine split pin")) {
            for (int i = 0; i < n->input_count; i++) {
                if (n->input_pins[i].split_parent) {
                    ed_snapshot_before(e);
                    graph_recombine_pin(e->active_graph, n, 0, i);
                    break;
                }
            }
        }
        if (ImGui_MenuItem("Delete")) {
            ed_select_node(e, e->context_node, 0);
            ed_delete_selected(e);
        }
        ImGui_EndPopup();
    }
    if (ImGui_BeginPopup("link_menu", 0)) {
        if (ImGui_MenuItem("Delete Link") && e->has_context_link) {
            aclear(e->selected);
            aclear(e->selected_links);
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
        if (e->ui_context != EDITOR_UI_CONTEXT_SYSTEM &&
            ImGui_MenuItem("Add Comment Box")) {
            ed_snapshot_before(e);
            graph_add_comment_box(e->active_graph, "Comment", e->popup_x, e->popup_y, 320.0f, 160.0f);
            e->spawn_link_pending = 0;
            ImGui_CloseCurrentPopup();
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
            if (!ed_spawn_desc_compatible(e, desc)) {
                continue;
            }
            int score = ed_desc_score(desc, e->search);
            if (score < 0) {
                continue;
            }
            matches[match_count++] = (ed_search_match){ i, score };
        }
        if (match_count > 1) {
            qsort(matches, (size_t)match_count, sizeof *matches, ed_search_match_cmp);
        }
        if (match_count == 0) {
            e->search_selected = -1;
        } else {
            if (e->search_selected < 0) {
                e->search_selected = 0;
            }
            if (e->search_selected >= match_count) {
                e->search_selected = match_count - 1;
            }
            if (ImGui_IsKeyPressed(ImGuiKey_DownArrow)) {
                e->search_selected = (e->search_selected + 1) % match_count;
            }
            if (ImGui_IsKeyPressed(ImGuiKey_UpArrow)) {
                e->search_selected = (e->search_selected + match_count - 1) % match_count;
            }
        }
        int activate = ImGui_IsKeyPressed(ImGuiKey_Enter);
        const char *category = NULL;
        for (int m = 0; m < match_count; m++) {
            const node_desc *desc = registry_node(matches[m].registry_index);
            const char *next_category = ed_desc_category(desc);
            if (!category || strcmp(category, next_category) != 0) {
                ImGui_TextDisabled("%s", next_category);
                category = next_category;
            }
            int clicked = ImGui_SelectableEx(
                desc->name,
                m == e->search_selected,
                0,
                (ImVec2){ 0.0f, 0.0f }
            );
            if (clicked || (activate && m == e->search_selected)) {
                if (!e->spawn_link_pending) {
                    ed_snapshot_before(e);
                }
                node *n = graph_add_node(e->active_graph, desc);
                if (n && ed_compact_system_node(e, n) != 0) {
                    graph_remove_node(e->active_graph, n);
                    n = NULL;
                }
                if (n && ed_name_spawned_event(e, n) != 0) {
                    graph_remove_node(e->active_graph, n);
                    n = NULL;
                }
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
        if (match_count == 0) {
            ImGui_TextUnformatted(
                e->spawn_link_pending ? "No compatible nodes" : "No matching nodes"
            );
        }
        free(matches);
        ImGui_EndPopup();
    }
    if (e->spawn_link_pending && !ImGui_IsPopupOpen("spawn_menu", 0)) {
        ed_cancel_snapshot(e);
        e->spawn_link_pending = 0;
    }
}

static int ed_activation_live(const editor *e)
{
    return e && e->activation &&
           !editor_preview_allowed(flow_run_status(e->activation));
}

/* Authoring edits mutate the working Behavior Asset, not the preview snapshot. */
static void ed_begin_authoring_edit(editor *) {}

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
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        node *n = e->active_graph->nodes[i];
        if (n->desc->flow_kind != NODE_FLOW_EVENT) {
            continue;
        }
        if (at++ == index) {
            return n;
        }
    }
    return NULL;
}

static int ed_entry_count(const editor *e)
{
    int count = 0;
    for (int i = 0; i < acount(e->active_graph->nodes); i++) {
        if (e->active_graph->nodes[i]->desc->flow_kind == NODE_FLOW_EVENT) {
            count++;
        }
    }
    return count;
}

static void ed_dispatch_selected(editor *e)
{
    if (e->active_graph != e->bp->graph) {
        return;
    }
    node *entry = ed_entry_at(e, e->entry_index);
    if (!entry || !entry->reference_name) {
        return;
    }
    flow_error error = { 0 };
    flow_run *act = editor_dispatch(e, entry->reference_name, (flow_inputs){ 0 }, &error);
    if (!act) {
        char message[ED_LOG_MESSAGE];
        snprintf(
            message,
            sizeof message,
            "Dispatch %s failed: %s",
            entry->reference_name,
            error.message ? error.message : "unknown error"
        );
        ed_show_error(e, message);
    }
}

static void ed_run_pause(editor *e)
{
    if (e->activation) {
        (void)flow_run_pause(e->activation);
    }
}
static void ed_compile(editor *e)
{
    compile_report_free(&e->compile);
    compile_report_init(&e->compile);
    behavior_asset_compile(e->bp, &e->compile);
}

static void ed_zoom_fit(editor *e);

/* Layered auto-arrange. The measurement and the wire topology come from the
 * editor; the layout itself lives in arrange.c, which knows no ImGui. */
static int ed_node_index(const graph *g, const node *n)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i] == n) {
            return i;
        }
    }
    return -1;
}

static void ed_auto_arrange(editor *e)
{
    graph *g = e->active_graph;
    int count = acount(g->nodes);
    if (!count) {
        return;
    }
    float line = ImGui_GetTextLineHeight();
    arrange_box *boxes = NULL;
    arrange_point *pos = NULL;
    arrange_link *links = NULL;
    asetlen(boxes, count);
    asetlen(pos, count);
    for (int i = 0; i < count; i++) {
        node *n = g->nodes[i];
        ImVec2 size = ed_node_dimensions(e, g, n);
        boxes[i] = (arrange_box){ size.x, size.y, line + 2.0f * ED_TITLE_PAD, line + 6.0f, ED_PAD };
        pos[i] = (arrange_point){ n->x, n->y };
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *ed = &g->edges[i];
        node *from = graph_node_by_id(g, ed->from_id);
        node *to = graph_node_by_id(g, ed->to_id);
        if (!from || !to) {
            continue;
        }
        int from_out = 0, to_out = 0, from_pin = -1, to_pin = -1;
        if (!graph_pin_by_id(from, ed->from_pin_id, &from_out, &from_pin) || !from_out) {
            continue;
        }
        if (!graph_pin_by_id(to, ed->to_pin_id, &to_out, &to_pin) || to_out) {
            continue;
        }
        int from_at = ed_node_index(g, from);
        int to_at = ed_node_index(g, to);
        if (from_at < 0 || to_at < 0) {
            continue;
        }
        apush(links, (arrange_link){ from_at, to_at, from_pin, to_pin });
    }
    arrange_graph layout = { boxes, count, links, acount(links) };
    arrange_layout(&layout, pos);

    int moved = 0;
    for (int i = 0; i < count && !moved; i++) {
        moved = g->nodes[i]->x != pos[i].x || g->nodes[i]->y != pos[i].y;
    }
    /* An already arranged graph must not push a do-nothing undo entry, and
     * must not destroy the preview instance at the edit choke point. */
    if (moved) {
        ed_snapshot_before(e);
        for (int i = 0; i < count; i++) {
            g->nodes[i]->x = pos[i].x;
            g->nodes[i]->y = pos[i].y;
        }
    }
    /* The arranged graph rarely still sits where the view was looking, so
     * frame it. Fitting moves zoom and pan, so the screen-space layout is
     * rebuilt after it rather than before. */
    ed_zoom_fit(e);
    ed_build_layout(e);
    afree(links);
    afree(pos);
    afree(boxes);
}

static void ed_zoom_fit(editor *e)
{
    int count = acount(e->active_graph->nodes);
    if (!count || e->canvas_size.x <= 0.0f || e->canvas_size.y <= 0.0f) {
        return;
    }
    node *first = e->active_graph->nodes[0];
    ImVec2 first_size = ed_node_dimensions(e, e->active_graph, first);
    float min_x = first->x;
    float max_x = first->x + first_size.x;
    float min_y = first->y;
    float max_y = first->y + first_size.y;
    for (int i = 1; i < count; i++) {
        node *n = e->active_graph->nodes[i];
        ImVec2 size = ed_node_dimensions(e, e->active_graph, n);
        if (n->x < min_x) {
            min_x = n->x;
        }
        if (n->x + size.x > max_x) {
            max_x = n->x + size.x;
        }
        if (n->y < min_y) {
            min_y = n->y;
        }
        if (n->y + size.y > max_y) {
            max_y = n->y + size.y;
        }
    }
    for (int i = 0; i < acount(e->active_graph->edges); i++) {
        const edge *ed = &e->active_graph->edges[i];
        for (int point = 0; point < ed->reroute_count; point++) {
            min_x = ed_min(min_x, ed->reroutes[point].x);
            max_x = ed_max(max_x, ed->reroutes[point].x);
            min_y = ed_min(min_y, ed->reroutes[point].y);
            max_y = ed_max(max_y, ed->reroutes[point].y);
        }
    }
    float width = ed_max(max_x - min_x, 1.0f);
    float height = ed_max(max_y - min_y, 1.0f);
    float available_x = ed_max(e->canvas_size.x - 2.0f * ED_FIT_MARGIN, 1.0f);
    float available_y = ed_max(e->canvas_size.y - 2.0f * ED_FIT_MARGIN, 1.0f);
    e->zoom = ed_min(available_x / width, available_y / height);
    if (e->zoom < ED_MIN_ZOOM) {
        e->zoom = ED_MIN_ZOOM;
    }
    if (e->zoom > ED_MAX_ZOOM) {
        e->zoom = ED_MAX_ZOOM;
    }
    ImVec2 center = { (min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f };
    e->pan = ed_sub(ed_scale(e->canvas_size, 0.5f), ed_scale(center, e->zoom));
    e->view_initialized = 1;
}

static void ed_run_resume(editor *e)
{
    if (e->activation) {
        (void)flow_run_resume(e->activation);
    }
}
static void ed_run_step(editor *e)
{
    if (e->activation) {
        (void)flow_run_step(e->activation);
    }
}
static void ed_run_step_over(editor *e)
{
    if (e->activation) {
        (void)flow_run_step_over(e->activation);
    }
}
static void ed_run_stop(editor *e)
{
    behavior_instance *target = e->inspection_instance
                                  ? (behavior_instance *)e->inspection_instance
                                  : e->instance;
    if (!target || !e->activation) {
        return;
    }
    (void)behavior_instance_stop_activation(target, e->activation);
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
    if (!enabled && ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui_SetTooltip("%s", reason);
    }
    ImGui_EndDisabled();
    if (enabled && clicked && action) {
        action(e);
    }
    ImGui_SameLine();
}

static void ed_draw_toolbar(editor *e)
{
    if (e->ui_context == EDITOR_UI_CONTEXT_SYSTEM) {
        ed_toolbar_action(e, "Compile", 1, "", ed_compile);
        ed_toolbar_action(e, "Zoom to fit", acount(e->active_graph->nodes) > 0, "The graph has no nodes", ed_zoom_fit);
        ed_toolbar_action(e, "Auto arrange", acount(e->active_graph->nodes) > 0, "The graph has no nodes", ed_auto_arrange);
        ImGui_SameLine();
        ed_toolbar_action(e, "Undo", acount(e->undo) > 0, "No undo entry is available", ed_undo);
        ed_toolbar_action(e, "Redo", acount(e->redo) > 0, "No redo entry is available", ed_redo);
        ImGui_NewLine();
        ImGui_TextUnformatted("Tick runs once for each Entity matching the System query.");
        return;
    }
    int entries = ed_entry_count(e);
    if (e->entry_index >= entries) {
        e->entry_index = entries ? entries - 1 : 0;
    }
    node *entry = ed_entry_at(e, e->entry_index);
    const char *entry_name = entry && entry->reference_name ? entry->reference_name
                                                            : "(unnamed event)";
    if (ImGui_BeginCombo("Entry", entry_name, 0)) {
        int at = 0;
        for (int i = 0; i < acount(e->active_graph->nodes); i++) {
            node *n = e->active_graph->nodes[i];
            if (n->desc->flow_kind != NODE_FLOW_EVENT) {
                continue;
            }
            char label[160];
            snprintf(
                label,
                sizeof label,
                "%s##entry_%d",
                n->reference_name ? n->reference_name : "(unnamed event)",
                at
            );
            if (ImGui_SelectableEx(label, at == e->entry_index, 0, (ImVec2){ 0.0f, 0.0f })) {
                e->entry_index = at;
            }
            at++;
        }
        ImGui_EndCombo();
    }
    ImGui_SameLine();
    ed_toolbar_action(e, "Compile", 1, "", ed_compile);
    ed_toolbar_action(e, "Zoom to fit", acount(e->active_graph->nodes) > 0, "The graph has no nodes", ed_zoom_fit);
    ed_toolbar_action(e, "Auto arrange", acount(e->active_graph->nodes) > 0, "The graph has no nodes", ed_auto_arrange);
    ImGui_SameLine();
    ed_toolbar_action(e, "Undo", acount(e->undo) > 0, "No undo entry is available", ed_undo);
    ed_toolbar_action(e, "Redo", acount(e->redo) > 0, "No redo entry is available", ed_redo);

    const behavior_instance *target_instance = e->inspection_instance
                                                 ? e->inspection_instance
                                                 : e->instance;
    int attached = target_instance != NULL;
    int can_dispatch = e->active_graph == e->bp->graph && entry && entry->reference_name;
    ed_toolbar_action(
        e,
        "Dispatch",
        can_dispatch,
        e->active_graph != e->bp->graph
            ? "Dispatch is available from the default event graph"
        : !entry || !entry->reference_name
            ? "Select a named event entry point"
            : "Dispatch the selected event; attach its runtime automatically",
        ed_dispatch_selected
    );
    flow_state state = e->activation ? flow_run_status(e->activation) : FLOW_IDLE;
    int can_pause = state == FLOW_RUNNING || state == FLOW_SUSPENDED;
    int can_resume = state == FLOW_PAUSED || state == FLOW_SUSPENDED;
    int can_step = can_resume;
    int can_stop = attached && state != FLOW_IDLE;
    ed_toolbar_action(e, "Pause", can_pause, "Pause is available while running or suspended", ed_run_pause);
    ed_toolbar_action(e, "Resume", can_resume, "Resume is available only while paused or suspended", ed_run_resume);
    ed_toolbar_action(e, "Step", can_step, "Step is available only while paused or suspended", ed_run_step);
    ed_toolbar_action(e, "Step Over", can_step, "Step over is available only while paused or suspended", ed_run_step_over);
    ed_toolbar_action(e, "Stop", can_stop, "Stop is available only for a live activation", ed_run_stop);
    ImGui_NewLine();
    if (attached) {
        ImGui_Text(
            "Runtime: attached  Activation: %s  Steps: %lu",
            ed_flow_state_name(state),
            e->activation ? flow_run_steps(e->activation) : 0
        );
    } else if (e->instance_destroyed) {
        ImGui_TextColored((ImVec4){ 1.0f, 0.6f, 0.3f, 1.0f }, "Runtime snapshot reset; Dispatch will create a new one");
    } else {
        ImGui_Text("Runtime: none  Activation: IDLE  Steps: 0");
    }
}

static void ed_draw_log(editor *e)
{
    ImGui_BeginDisabled(e->log_count == 0);
    int clear = ImGui_Button("Clear");
    ImGui_EndDisabled();
    if (clear) {
        e->log_count = 0;
    }
    int visible = ImGui_BeginChild("output_log", (ImVec2){ 0.0f, 0.0f }, 0, 0);
    if (visible) {
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
            if (ImGui_Selectable(label) && entry->origin) {
                editor_reveal_node(e, (node *)entry->origin);
            }
            ImGui_PopID();
        }
        if (follow && e->log_count) {
            ImGui_SetScrollHereY(1.0f);
        }
    }
    ImGui_EndChild();
}

static void ed_draw_output_log(editor *e)
{
    ImGui_SetNextWindowSize((ImVec2){ 520.0f, 220.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_OUTPUT_LOG)) {
        ImGui_SetNextWindowFocus();
    }
    if (!ImGui_Begin("Output Log", NULL, 0)) {
        ImGui_End();
        return;
    }
    ed_draw_log(e);
    ImGui_End();
}

static void ed_draw_compiler_results(editor *e)
{
    ImGui_SetNextWindowSize((ImVec2){ 520.0f, 220.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_COMPILER_RESULTS)) {
        ImGui_SetNextWindowFocus();
    }
    if (!ImGui_Begin("Compiler Results", NULL, 0)) {
        ImGui_End();
        return;
    }
    ImGui_Text("Errors: %d  Warnings: %d", e->compile.error_count, e->compile.warning_count);
    if (!e->compile.error_count && !e->compile.warning_count) {
        ImGui_TextUnformatted("No compiler diagnostics.");
    }
    for (int i = 0; i < acount(e->compile.items); i++) {
        const compile_diagnostic *item = &e->compile.items[i];
        ImGui_Text("%s  %s", item->severity == CMPL_ERROR ? "Error:" : "Warning:", item->message);
    }
    ImGui_End();
}

static void ed_center_initial_view(editor *e)
{
    if (e->canvas_size.x <= 0.0f || e->canvas_size.y <= 0.0f) {
        return;
    }
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
    if (e->view_initialized) {
        return;
    }
    ImVec2 min = { 0.0f, 0.0f };
    ImVec2 max = { 0.0f, 0.0f };
    if (acount(e->views) > 0) {
        min.x = min.y = 1e30f;
        max.x = max.y = -1e30f;
        for (int i = 0; i < acount(e->views); i++) {
            ImVec2 a = ed_screen_to_world(e, e->views[i].min);
            ImVec2 b = ed_screen_to_world(e, e->views[i].max);
            if (a.x < min.x) {
                min.x = a.x;
            }
            if (a.y < min.y) {
                min.y = a.y;
            }
            if (b.x > max.x) {
                max.x = b.x;
            }
            if (b.y > max.y) {
                max.y = b.y;
            }
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
        if (pan_held) {
            e->pan = ed_add(e->pan, io->MouseDelta);
        } else {
            e->panning = 0;
        }
        return;
    }
    if (e->linking) {
        e->link_mouse = mouse;
        if (ImGui_IsKeyPressed(ImGuiKey_Escape)) {
            ed_cancel_snapshot(e);
            ed_clear_connect_error(e);
            e->linking = 0;
            return;
        }
        if (mouse_released) {
            ed_pin_view *target = ed_hit_pin(e, mouse);
            if (target && target->n != e->link_pin.n &&
                ed_connect(e, &e->link_pin, target)) {
                ed_clear_connect_error(e);
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
                if (e->connect_error[0]) {
                    e->connect_error_pos = mouse;
                }
                ed_cancel_snapshot(e);
                e->linking = 0;
            }
        }
        return;
    }
    if (e->connect_error[0] && ImGui_IsKeyPressed(ImGuiKey_Escape)) {
        ed_clear_connect_error(e);
        return;
    }
    if (e->connect_error[0] && mouse_clicked) {
        ImVec2 min, max, close_min, close_max;
        if (ed_connect_error_rect(e, &min, &max, &close_min, &close_max) &&
            ed_point_in_rect(mouse, min, max)) {
            if (ed_point_in_rect(mouse, close_min, close_max)) {
                ed_clear_connect_error(e);
            }
            return;
        }
    }
    if (e->dragging) {
        if (!mouse_released) {
            ed_update_drag(e, mouse);
        } else {
            ImVec2 delta = ed_sub(mouse, e->drag_start_mouse);
            if (delta.x != 0.0f || delta.y != 0.0f) {
                ed_commit_snapshot(e);
            } else {
                ed_cancel_snapshot(e);
            }
            if (!delta.x && !delta.y && e->drag_toggle_selected) {
                if (acount(e->selected) > 1) {
                    ed_remove_selected(e, e->drag_toggle_node);
                } else {
                    ed_clear_selection(e);
                }
            }
            e->dragging = 0;
            e->drag_toggle_selected = 0;
            e->drag_toggle_node = NULL;
            aclear(e->drag_nodes);
        }
        return;
    }
    if (e->dragging_handle) {
        if (!mouse_released) {
            ed_update_handle_drag(e, mouse);
        } else {
            ed_commit_snapshot(e);
            e->dragging_handle = 0;
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
    if (hovered && mouse_clicked && !e->controls_hovered && ImGui_IsAnyItemActive()) {
        ImGui_ClearActiveID();
    }
    if (ImGui_IsAnyItemActive() || e->controls_hovered) {
        return;
    }

    if (hovered && io->MouseWheel != 0.0f) {
        ImVec2 before = ed_screen_to_world(e, mouse);
        e->zoom *= math_pow(1.1f, io->MouseWheel);
        if (e->zoom < ED_MIN_ZOOM) {
            e->zoom = ED_MIN_ZOOM;
        }
        if (e->zoom > ED_MAX_ZOOM) {
            e->zoom = ED_MAX_ZOOM;
        }
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
    if (!hovered || !mouse_clicked) {
        return;
    }

    int handle_edge = -1, handle_side = -1;
    if (ed_hit_handle(e, mouse, &handle_edge, &handle_side)) {
        edge *ed = &e->active_graph->edges[handle_edge];
        if (io->KeyAlt) {
            ed_snapshot_before(e);
            for (int i = handle_side; i + 1 < ed->reroute_count; i++) {
                ed->reroutes[i] = ed->reroutes[i + 1];
            }
            ed->reroute_count--;
            return;
        }
        ed_begin_authoring_edit(e);
        e->pending_snapshot = ed_snapshot(e);
        e->handle_edge = handle_edge;
        e->handle_side = handle_side;
        e->dragging_handle = 1;
        ed_update_handle_drag(e, mouse);
        return;
    }

    /* Double-clicking (or Alt-clicking) a wire inserts a reroute point into
     * the segment that was clicked. Alt-clicking an existing point removes it. */
    if (io->KeyAlt || io->MouseDoubleClicked[0]) {
        ed_link_ref link;
        int segment;
        if (ed_hit_link_segment(e, mouse, &link, &segment)) {
            int index = ed_edge_index(e, &link);
            edge *ed = &e->active_graph->edges[index];
            if (ed->reroute_count < GRAPH_EDGE_MAX_REROUTES) {
                ed_begin_authoring_edit(e);
                e->pending_snapshot = ed_snapshot(e);
                for (int i = ed->reroute_count; i > segment; i--) {
                    ed->reroutes[i] = ed->reroutes[i - 1];
                }
                ImVec2 point = ed_screen_to_world(e, mouse);
                ed->reroutes[segment].x = point.x;
                ed->reroutes[segment].y = point.y;
                ed->reroute_count++;
                e->handle_edge = index;
                e->handle_side = segment;
                e->dragging_handle = 1;
                return;
            }
        }
    }

    ed_pin_view *pin = ed_hit_pin(e, mouse);
    if (pin) {
        ed_select_node(e, pin->n, ed_selection_modifier(io));
        ed_begin_authoring_edit(e);
        e->linking = 1;
        ed_clear_connect_error(e);
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
        if (!already_selected) {
            ed_select_node(e, v->n, append);
        }
        if (ed_node_selected(e, v->n)) {
            ed_begin_drag(e, mouse);
        }
        return;
    }
    ed_link_ref link;
    if (ed_hit_link(e, mouse, &link)) {
        int append = ed_selection_modifier(io);
        if (!append) {
            aclear(e->selected);
            aclear(e->selected_links);
        }
        if (append && ed_link_selected(e, &link)) {
            ed_remove_selected_link(e, &link);
        } else {
            ed_add_selected_link(e, &link);
        }
        return;
    }
    if (!ed_selection_modifier(io)) {
        ed_clear_selection(e);
    }
    e->selecting = 1;
    e->select_additive = ed_selection_modifier(io);
    e->select_start = mouse;
    e->select_end = mouse;
}

static void ed_handle_shortcuts(editor *e)
{
    ImGuiIO *io = ImGui_GetIO();
    if (!ImGui_IsWindowFocused(0)) {
        return;
    }
    if (ImGui_GetIO()->WantTextInput) {
        return;
    }
    int modifier = io->KeyCtrl || io->KeySuper;
    if (modifier && ImGui_IsKeyPressed(ImGuiKey_Z)) {
        if (io->KeyShift) {
            ed_redo(e);
        } else {
            ed_undo(e);
        }
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
        if (e->save_handler) {
            e->save_handler(e, e->save_user);
        } else {
            editor_save(e, "behavior_asset.json");
        }
    } else if (ImGui_IsKeyPressed(ImGuiKey_Delete) || ImGui_IsKeyPressed(ImGuiKey_Backspace)) {
        ed_delete_selected(e);
    }
}

editor *editor_new(behavior_asset *bp)
{
    registry_validate();
    editor *e = (editor *)calloc(1, sizeof *e);
    if (!e) {
        return NULL;
    }
    e->bp = bp;
    e->ui_context = EDITOR_UI_CONTEXT_BEHAVIOR;
    e->graph_visible = 1;
    e->active_graph = bp ? bp->graph : NULL;
    e->edit_revision_bp = bp;
    e->saved_snapshot = bp ? behavior_asset_save_json(bp) : NULL;
    compile_report_init(&e->compile);
    e->ev = eval_view_new();
    e->icon_font = ed_load_icon_font();
    e->zoom = 1.0f;
    return e;
}

behavior_instance *editor_preview_instance(const editor *e)
{
    return e ? e->instance : NULL;
}

unsigned editor_preview_generation(const editor *e)
{
    return e ? e->instance_generation : 0;
}

unsigned editor_edit_revision(const editor *e)
{
    return e ? e->edit_revision : 0;
}

void editor_undo(editor *e)
{
    if (e) {
        ed_undo(e);
    }
}

void editor_redo(editor *e)
{
    if (e) {
        ed_redo(e);
    }
}

void editor_preview_create(editor *e)
{
    if (!e || e->instance || e->inspection_instance) {
        return;
    }
    char *json = behavior_asset_save_json(e->bp);
    e->preview_behavior_asset = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!e->preview_behavior_asset) {
        return;
    }
    e->instance = behavior_instance_create(e->preview_behavior_asset, NULL, NULL, NULL);
    if (!e->instance) {
        behavior_asset_free(e->preview_behavior_asset);
        e->preview_behavior_asset = NULL;
        return;
    }
    e->instance->sink = ed_log_sink;
    e->instance->sink_user = e;
    e->instance_generation++;
    e->instance_destroyed = 0;
    e->activation = NULL;
}

void editor_preview_reset(editor *e)
{
    if (e) {
        ed_destroy_preview(e);
    }
}

static flow_run *editor_dispatch_instance(
    editor *e,
    behavior_instance *target,
    const char *name,
    flow_inputs inputs,
    flow_error *error
)
{
    if (!target) {
        return NULL;
    }
    flow_run *act = behavior_instance_dispatch(target, name, inputs, error);
    if (!act) {
        return NULL;
    }
    e->activation = act;
    e->run_started_at = e->monotonic_time;
    return act;
}

flow_run *editor_dispatch_preview(editor *e, const char *name, flow_inputs inputs, flow_error *error)
{
    if (!e) {
        return NULL;
    }
    if (!e->instance) {
        editor_preview_create(e);
    }
    return editor_dispatch_instance(e, e->instance, name, inputs, error);
}

flow_run *editor_dispatch(editor *e, const char *name, flow_inputs inputs, flow_error *error)
{
    if (!e) {
        return NULL;
    }
    if (e->inspection_instance) {
        return editor_dispatch_instance(e, (behavior_instance *)e->inspection_instance, name, inputs, error);
    }
    return editor_dispatch_preview(e, name, inputs, error);
}

static void ed_refresh_edit_revision(editor *e)
{
    if (e->edit_revision_initialized && !e->edit_pending) {
        return;
    }
    char *json = behavior_asset_save_json(e->bp);
    if (!json) {
        return;
    }
    int changed = e->edit_revision_initialized
                    ? e->edit_revision_bp != e->bp || !e->edit_revision_snapshot ||
                          strcmp(e->edit_revision_snapshot, json) != 0
                    : e->edit_pending;
    free(e->edit_revision_snapshot);
    e->edit_revision_snapshot = json;
    e->edit_revision_bp = e->bp;
    e->edit_revision_initialized = 1;
    e->edit_pending = 0;
    if (changed) {
        e->edit_revision++;
    }
}

void editor_tick(editor *e, double monotonic_time)
{
    if (!e) {
        return;
    }
    ed_refresh_edit_revision(e);
    e->monotonic_time = monotonic_time;
    if (e->connect_error[0] && monotonic_time >= e->connect_error_until + ED_TOAST_FADE) {
        ed_clear_connect_error(e);
    }
    if (e->instance) {
        (void)behavior_instance_tick(e->instance, monotonic_time);
    }
}

const behavior_asset *editor_behavior_asset(const editor *e)
{
    return e ? e->bp : NULL;
}

int editor_open_behavior_asset(editor *e, const behavior_asset *source, uint64_t graph_id)
{
    if (!e || !source || !behavior_asset_find_graph(source, graph_id)) {
        return -1;
    }
    ed_begin_authoring_edit(e);
    char *json = behavior_asset_save_json(source);
    if (!json) {
        return -1;
    }
    int result = ed_adopt_behavior_asset(e, json);
    free(json);
    if (result != 0) {
        return -1;
    }
    free(e->saved_snapshot);
    e->saved_snapshot = behavior_asset_save_json(e->bp);
    ed_free_string_stack(&e->undo);
    ed_free_string_stack(&e->redo);
    e->inspection_instance = NULL;
    e->inspection_run = NULL;
    return editor_open_graph(e, graph_id);
}

int editor_open_graph(editor *e, uint64_t graph_id)
{
    if (!e || !e->bp) {
        return -1;
    }
    return ed_switch_graph(e, behavior_asset_find_graph(e->bp, graph_id));
}

const graph *editor_active_graph(const editor *e)
{
    return e ? e->active_graph : NULL;
}

int editor_command_apply(editor *e, editor_command command)
{
    if (!e || !e->bp) {
        return -1;
    }
    switch (command.kind) {
        case EDITOR_COMMAND_CREATE_GRAPH: {
            if (!command.data.create_graph.name ||
                !command.data.create_graph.name[0] || !behavior_asset_mutable(e->bp) ||
                behavior_asset_find_graph_by_name(
                    e->bp,
                    command.data.create_graph.name
                )) {
                return -1;
            }
            ed_snapshot_before(e);
            graph *created = command.data.create_graph.graph_kind == GRAPH_FUNCTION
                               ? behavior_asset_add_function(
                                     e->bp,
                                     command.data.create_graph.name,
                                     0
                                 )
                           : command.data.create_graph.graph_kind == GRAPH_MACRO
                               ? behavior_asset_add_macro(
                                     e->bp,
                                     command.data.create_graph.name
                                 )
                               : behavior_asset_add_graph(
                                     e->bp,
                                     command.data.create_graph.graph_kind,
                                     command.data.create_graph.name
                                 );
            return created ? 0 : -1;
        }
        case EDITOR_COMMAND_RENAME_GRAPH: {
            const char *name = command.data.rename_graph.name;
            graph *target = behavior_asset_find_graph(
                e->bp,
                command.data.rename_graph.graph_id
            );
            graph *same = behavior_asset_find_graph_by_name(e->bp, name);
            if (!name || !name[0] || !behavior_asset_mutable(e->bp) || !target ||
                command.data.rename_graph.graph_id == 0 || (same && same != target)) {
                return -1;
            }
            ed_snapshot_before(e);
            return behavior_asset_rename_graph(e->bp, command.data.rename_graph.graph_id, name);
        }
        case EDITOR_COMMAND_OPEN_GRAPH:
            return editor_open_graph(e, command.data.open_graph.graph_id);
        case EDITOR_COMMAND_SPAWN_NODE:
            if (!command.data.spawn_node.desc || !e->active_graph ||
                !behavior_asset_mutable(e->bp)) {
                return -1;
            }
            return editor_spawn(
                       e,
                       command.data.spawn_node.desc,
                       command.data.spawn_node.x,
                       command.data.spawn_node.y
                   )
                     ? 0
                     : -1;
        case EDITOR_COMMAND_SET_PIN_DEFAULT: {
            node *n = graph_node_by_id(
                e->active_graph,
                command.data.set_pin_default.node_id
            );
            const pin_value *value = command.data.set_pin_default.value;
            if (!n || !value ||
                (command.data.set_pin_default.output != 0 &&
                 command.data.set_pin_default.output != 1) ||
                !behavior_asset_mutable(e->bp)) {
                return -1;
            }
            const pin_spec *pins = command.data.set_pin_default.output
                                     ? n->output_pins
                                     : n->input_pins;
            int pin_count = command.data.set_pin_default.output ? n->output_count
                                                                : n->input_count;
            int pin_index = command.data.set_pin_default.pin_index;
            if (pin_index < 0 || pin_index >= pin_count ||
                pins[pin_index].kind != PIN_DATA ||
                value->type != pins[pin_index].type) {
                return -1;
            }
            ed_snapshot_before(e);
            return graph_set_pin_default(
                e->active_graph,
                n,
                command.data.set_pin_default.output,
                pin_index,
                value
            );
        }
        case EDITOR_COMMAND_CONNECT_DATA:
        case EDITOR_COMMAND_CONNECT_EXEC: {
            node *from = graph_node_by_id(e->active_graph, command.data.connect.from_node);
            node *to = graph_node_by_id(e->active_graph, command.data.connect.to_node);
            if (!from || !to) {
                return -1;
            }
            if (command.data.connect.from_pin < 0 ||
                command.data.connect.from_pin >= from->output_count ||
                command.data.connect.to_pin < 0 ||
                command.data.connect.to_pin >= to->input_count) {
                return -1;
            }
            pin_kind expected_kind = command.kind == EDITOR_COMMAND_CONNECT_DATA
                                       ? PIN_DATA
                                       : PIN_EXEC;
            if (!behavior_asset_mutable(e->bp) ||
                !ed_connection_specs_valid(
                    &from->output_pins[command.data.connect.from_pin],
                    &to->input_pins[command.data.connect.to_pin],
                    expected_kind
                )) {
                return -1;
            }
            char *snapshot = ed_snapshot(e);
            int pending = e->edit_pending;
            ed_pin_view output = {
                from,
                ED_PIN_OUTPUT,
                command.data.connect.from_pin,
                { 0.0f, 0.0f },
            };
            ed_pin_view input = {
                to,
                ED_PIN_INPUT,
                command.data.connect.to_pin,
                { 0.0f, 0.0f },
            };
            int result = ed_connect(e, &output, &input) ? 0 : -1;
            if (result == 0) {
                e->edit_pending = 1;
                if (snapshot) {
                    ed_push_string(&e->undo, snapshot);
                }
                ed_free_string_stack(&e->redo);
                return 0;
            }
            char *current = ed_snapshot(e);
            if (snapshot && current && strcmp(current, snapshot) != 0) {
                if (ed_adopt_behavior_asset(e, snapshot) != 0) {
                    abort();
                }
                free(e->edit_revision_snapshot);
                e->edit_revision_snapshot = ed_snapshot(e);
                e->edit_revision_bp = e->bp;
                e->edit_revision_initialized = 1;
            }
            free(current);
            free(snapshot);
            e->edit_pending = pending;
            return result;
        }
        case EDITOR_COMMAND_COMPILE:
            ed_compile(e);
            return compile_report_blocked(&e->compile) ? -1 : 0;
        case EDITOR_COMMAND_SAVE:
            return command.data.file.path ? editor_save(e, command.data.file.path)
                                          : -1;
        case EDITOR_COMMAND_LOAD:
            return command.data.file.path ? editor_load(e, command.data.file.path)
                                          : -1;
    }
    return -1;
}

void editor_query_state(const editor *e, editor_query *out)
{
    assert(out);
    *out = (editor_query){ 0 };
    if (!e || !e->bp) {
        return;
    }
    out->active_graph = e->active_graph;
    out->graph_count = acount(e->bp->graphs);
    out->graph_visible = e->graph_visible && e->ui_context != EDITOR_UI_CONTEXT_SCENE;
    out->compile = &e->compile;
    if (e->edit_revision_initialized && !e->edit_pending &&
        e->edit_revision_bp == e->bp && e->edit_revision_snapshot) {
        out->dirty = !e->saved_snapshot ||
                     strcmp(e->edit_revision_snapshot, e->saved_snapshot) != 0;
        return;
    }
    char *json = behavior_asset_save_json(e->bp);
    out->dirty = !json || !e->saved_snapshot || strcmp(json, e->saved_snapshot) != 0;
    free(json);
}

void editor_set_inspection_instance(editor *e, const behavior_instance *instance)
{
    if (!e) {
        return;
    }
    if (!instance && e->inspection_instance) {
        e->activation = NULL;
        e->inspection_trace = NULL;
        e->inspection_trace_count = 0;
    }
    e->inspection_instance = instance;
    if (instance) {
        e->activation = (flow_run *)e->inspection_run;
    }
}

void editor_set_inspection_run(editor *e, const flow_run *run)
{
    if (!e) {
        return;
    }
    e->inspection_run = run;
    if (e->inspection_instance) {
        e->activation = (flow_run *)run;
    }
}

void editor_set_inspection_trace(editor *e, const behavior_instance_trace_entry *trace, int count)
{
    if (!e) {
        return;
    }
    e->inspection_trace = trace;
    e->inspection_trace_count = count > 0 ? count : 0;
}

void editor_set_execution_pulse(editor *e, double monotonic_time)
{
    if (!e || !e->active_graph) {
        return;
    }
    e->execution_pulse_time = monotonic_time;
    e->execution_pulse_graph_id = e->active_graph->graph_id;
    e->execution_pulse_active = 1;
}

void editor_free(editor *e)
{
    if (!e) {
        return;
    }
    ed_free_string_stack(&e->undo);
    ed_free_string_stack(&e->redo);
    free(e->pending_snapshot);
    free(e->clipboard);
    free(e->edit_revision_snapshot);
    free(e->saved_snapshot);
    ed_destroy_preview(e);
    compile_report_free(&e->compile);
    behavior_asset_free(e->bp);
    afree(e->graph_views);
    afree(e->views);
    afree(e->pins);
    afree(e->selected);
    afree(e->selected_links);
    afree(e->drag_nodes);
    eval_view_free(e->ev);
    free(e);
}

node *editor_spawn(editor *e, const node_desc *desc, float x, float y)
{
    ed_snapshot_before(e);
    node *n = graph_add_node(e->active_graph, desc);
    if (!n) {
        return NULL;
    }
    if (ed_name_spawned_event(e, n) != 0) {
        graph_remove_node(e->active_graph, n);
        ed_cancel_snapshot(e);
        return NULL;
    }
    n->x = x;
    n->y = y;
    return n;
}

int editor_save(editor *e, const char *path)
{
    if (!e || !path) {
        return -1;
    }
    char *json = behavior_asset_save_json(e->bp);
    if (!json) {
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(json);
        return -1;
    }
    int ok = fputs(json, f) >= 0;
    if (fclose(f) != 0) {
        ok = 0;
    }
    if (!ok) {
        free(json);
        return -1;
    }
    free(e->saved_snapshot);
    e->saved_snapshot = json;
    e->edit_revision_initialized = 0;
    return 0;
}

static int ed_read_file(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
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
    if (!e || !path) {
        return -1;
    }
    ed_begin_authoring_edit(e);
    char *json = NULL;
    if (ed_read_file(path, &json)) {
        return -1;
    }
    ed_snapshot_before(e);
    int result = ed_adopt_behavior_asset(e, json);
    if (result == 0) {
        free(e->saved_snapshot);
        e->saved_snapshot = behavior_asset_save_json(e->bp);
        e->edit_revision_initialized = 0;
    }
    free(json);
    return result;
}

void editor_prepare_docking(void)
{
    editor_ui_prepare();
}

void editor_set_ui_context(editor *e, editor_ui_context context)
{
    if (!e || context < EDITOR_UI_CONTEXT_SCENE || context > EDITOR_UI_CONTEXT_SYSTEM) {
        return;
    }
    e->ui_context = context;
    if (context == EDITOR_UI_CONTEXT_SYSTEM && e->active_graph) {
        e->active_graph->dataflow = 1;
    }
}

void editor_set_graph_visible(editor *e, int visible)
{
    if (e) {
        e->graph_visible = visible != 0;
    }
}

void editor_set_save_handler(editor *e, editor_save_handler handler, void *user)
{
    if (!e) {
        return;
    }
    e->save_handler = handler;
    e->save_user = user;
}

int editor_host_captures_keyboard(void)
{
    const ImGuiIO *io = ImGui_GetIO();
    return io->WantCaptureKeyboard || io->WantTextInput;
}

void editor_set_event_catalog(editor *e, const editor_event_signature *events, int count)
{
    if (!e) {
        return;
    }
    e->event_catalog = count > 0 ? events : NULL;
    e->event_catalog_count = count > 0 ? count : 0;
}

void editor_draw(editor *e)
{
    if (!e || !e->ev) {
        return;
    }
    if (e->ui_context == EDITOR_UI_CONTEXT_SCENE || !e->graph_visible) {
        ed_draw_compiler_results(e);
        ed_draw_output_log(e);
        return;
    }
    ImGuiIO *io = ImGui_GetIO();
    flow_state preview_state = e->activation ? flow_run_status(e->activation) : FLOW_IDLE;
    const behavior_instance *display_instance = e->inspection_instance
                                                  ? e->inspection_instance
                                                  : e->instance;
    const flow_run *display_run = e->inspection_run
                                    ? e->inspection_run
                                    : (e->inspection_instance
                                           ? NULL
                                           : (ed_activation_live(e) ? e->activation
                                                                    : NULL));
    eval_view_set_instance(e->ev, display_instance);
    if (!e->inspection_run && editor_preview_allowed(preview_state)) {
        eval_view_refresh(e->ev, e->active_graph);
    }
    eval_view_set_run(e->ev, display_run);
    ImGui_SetNextWindowSize((ImVec2){ 960.0f, 640.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_NODE_CANVAS)) {
        ImGui_SetNextWindowFocus();
    }
    if (!ImGui_Begin("Node Canvas", NULL, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui_End();
        return;
    }
    if (ImGui_BeginTabBar("asset_tabs", 0)) {
        const char *name = e->bp->name && e->bp->name[0] ? e->bp->name : "Behavior";
        if (ImGui_BeginTabItem(name, NULL, 0)) {
            ImGui_EndTabItem();
        }
        ImGui_EndTabBar();
    }

    ed_draw_toolbar(e);
    /* A Dispatch click selects a fresh activation on the instance. Refresh the
     * display source before layout can ask for any pin values. */
    eval_view_set_run(e->ev, display_run);
    ImVec2 canvas_min = ImGui_GetCursorScreenPos();
    ImVec2 canvas_size = ImGui_GetContentRegionAvail();
    int canvas_changed = e->canvas_min.x != canvas_min.x ||
                         e->canvas_min.y != canvas_min.y ||
                         e->canvas_size.x != canvas_size.x ||
                         e->canvas_size.y != canvas_size.y;
    if (e->view_initialized && canvas_changed) {
        e->view_initialized = 0;
    }
    e->canvas_min = canvas_min;
    e->canvas_size = canvas_size;
    ed_build_layout(e);
    if (canvas_changed) {
        ed_zoom_fit(e);
    } else {
        ed_center_initial_view(e);
    }
    ImVec2 hover_mouse = io->MousePos;
    e->hovered_node = NULL;
    e->has_hovered_link = 0;
    if (ed_point_in_rect(hover_mouse, e->canvas_min, ed_add(e->canvas_min, e->canvas_size))) {
        ed_update_hover(e, hover_mouse);
    }
    e->controls_hovered = 0;
    ed_draw_canvas(e);
    ed_draw_controls(e, 0);
    ed_draw_front_nodes(e);
    ed_draw_controls(e, 1);
    ed_draw_connect_error(e);
    const char *error = eval_view_error(e->ev);
    if (error[0]) {
        ImGui_SetCursorScreenPos(ed_add(e->canvas_min, (ImVec2){ 12.0f, 12.0f }));
        ImGui_TextColored((ImVec4){ 1.0f, 0.35f, 0.35f, 1.0f }, "Graph error: %s", error);
    }
    ed_handle_input(e);
    ed_draw_selection_overlay(e);
    ImGui_SetCursorScreenPos(e->canvas_min);
    ImGui_Dummy(e->canvas_size);
    ed_accept_graph_call_drop(e);
    ed_handle_shortcuts(e);
    ed_handle_popups(e);
    ImGui_End();
    if (e->ui_context == EDITOR_UI_CONTEXT_BEHAVIOR) {
        ed_draw_tree(e);
        ed_draw_details(e);
    } else if (e->ui_context == EDITOR_UI_CONTEXT_SYSTEM) {
        /* Systems do not have the Behavior Asset tree, but their graph still needs
         * the same UE-style Details panel for node and wire inspection. */
        ed_draw_details(e);
    }
    if (e->ui_context != EDITOR_UI_CONTEXT_SYSTEM) {
        ed_draw_compiler_results(e);
        ed_draw_output_log(e);
    }
}
