/* The behavior graph canvas: nodes are RmlUi elements in a context of their own
 * whose dp ratio is the zoom, so text is rasterized at its displayed size and
 * clipping keeps working (a transformed element would not clip). The canvas is
 * a texture shown by a surface in the Behavior area; window input is replayed
 * into it in graph units. Interaction follows docs/editor-graph-references.md;
 * edits go through the graph editor's mutation helpers and history. */
#include "editor/graph/search.h"
#include "editor/studio/studio_internal.h"
#include "editor/ui/highlight.h"
#include "editor/ui/labels.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/extract.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "platform/cute.h"
#include "support/array.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    PATH = 96,
    CURVE_POINTS = 256,
};

static const float ZOOM_MIN = 0.1f, ZOOM_MAX = 2.0f;
static const float GRID = 32;
static const float HEAD = 24, PAD = 4, ROW = 22; /* graph.rcss row pitch */
static const float THRESHOLD = 4;   /* window px before a press drags */
static const float WIRE_HIT = 6;    /* window px */
static const float FAR_ZOOM = 0.5f; /* below: titles only */
static const double FIT_SECONDS = 0.3;

/* ---------- Model access ---------- */

static editor *graph_editor(const studio *s)
{
    return s->scene->graph_editor;
}

bool studio_graph_active(const studio *s)
{
    const editor *e = graph_editor(s);
    return e && s->scene->graph_document_id && e->active_graph;
}

static graph *active_graph(const studio *s)
{
    return studio_graph_active(s) ? graph_editor(s)->active_graph : NULL;
}

static bool graph_readonly(const studio *s)
{
    const graph *g = active_graph(s);
    return !g || g->math_expression || !behavior_asset_mutable(graph_editor(s)->asset);
}

/* Streaming edits must reach the preview on the next tick. */
static void touched(editor *e)
{
    e->edit_pending = 1;
}

static bool node_visible(const graph *g, const node *n)
{
    return n && n->desc && !graph_node_is_inline(g, n);
}

/* ---------- Node layout ----------
   Nodes are sized here rather than by RmlUi, from measured text and the
   graph.rcss metrics, so a node's box and sockets are known before it is laid
   out: arrange, framing and hit tests never wait a frame.

   A full node's rows follow the ImGui editor and Blender: execution pins pair
   up first, then every data output, then every data input, so inputs always
   sit below outputs. Unlabeled execution pins may sit in the title instead
   (Unity Visual Scripting). Operators draw as a symbol between their inputs
   and output, and one-output reads as pills (Unreal's compact nodes). */

typedef enum
{
    FORM_FULL,
    FORM_OPERATOR,
    FORM_GETTER,
} node_form;

struct studio_node_layout
{
    uint64_t id;
    node_form form;
    float w, h;
    float label;           /* input label column, so input editors line up */
    float top;             /* the first row's top, below the node's */
    float column;          /* an operator's input column */
    int head_in, head_out; /* pins drawn in the title, or -1 */
    int code_lines;        /* an expression's source lines */
};
typedef struct studio_node_layout node_layout;

static const node_layout *layout_of(const studio *s, node *n);

static bool pin_exec(const node *n, int kind, int pin)
{
    return (kind == ED_PIN_INPUT ? n->input_pins : n->output_pins)[pin].kind == PIN_EXEC;
}

static bool in_head(const node_layout *l, int kind, int pin)
{
    return pin == (kind == ED_PIN_INPUT ? l->head_in : l->head_out);
}

/* Whether a pin has a row: visible and not drawn in the title. */
static bool pin_in_rows(const graph *g, const node *n, const node_layout *l, int kind, int pin)
{
    return ed_pin_visible(g, n, kind, pin) && !in_head(l, kind, pin);
}

/* Index among the row pins on the same side of the same kind (exec or data). */
static int pin_order(const graph *g, const node *n, const node_layout *l, int kind, int pin)
{
    int order = 0;
    for (int p = 0; p < pin; p++) {
        order += pin_in_rows(g, n, l, kind, p) &&
                 pin_exec(n, kind, p) == pin_exec(n, kind, pin);
    }
    return order;
}

static int pin_count(const graph *g, const node *n, const node_layout *l, int kind, bool exec)
{
    int count = 0;
    for (int p = 0; p < (kind == ED_PIN_INPUT ? n->input_count : n->output_count); p++) {
        count += pin_in_rows(g, n, l, kind, p) && pin_exec(n, kind, p) == exec;
    }
    return count;
}

static int exec_rows(const graph *g, const node *n, const node_layout *l)
{
    int in = pin_count(g, n, l, ED_PIN_INPUT, true),
        out = pin_count(g, n, l, ED_PIN_OUTPUT, true);
    return in > out ? in : out;
}

static int row_count(const graph *g, const node *n, const node_layout *l)
{
    return exec_rows(g, n, l) + pin_count(g, n, l, ED_PIN_OUTPUT, false) +
           pin_count(g, n, l, ED_PIN_INPUT, false);
}

static int pin_row(const graph *g, const node *n, const node_layout *l, int kind, int pin)
{
    int order = pin_order(g, n, l, kind, pin);
    if (pin_exec(n, kind, pin) || l->form == FORM_OPERATOR) {
        return order;
    }
    int outputs = kind == ED_PIN_INPUT ? pin_count(g, n, l, ED_PIN_OUTPUT, false) : 0;
    return exec_rows(g, n, l) + outputs + order;
}

static void node_path(char *out, size_t size, uint64_t id)
{
    snprintf(out, size, "world/stage/n%llu", (unsigned long long)id);
}

/* Data rows are keyed i<pin> and o<pin> in the body; an execution row e<n>
   holds the pair of pins that share it, and title pins sit in the head. */
static void socket_path(char *out, size_t size, const studio *s, node *n, int kind, int pin)
{
    const node_layout *l = layout_of(s, n);
    char side = kind == ED_PIN_INPUT ? 'i' : 'o';
    unsigned long long id = n->id;
    if (in_head(l, kind, pin)) {
        snprintf(out, size, "world/stage/n%llu/h/%c%d/s", id, side, pin);
        return;
    }
    if (pin_exec(n, kind, pin)) {
        int row = pin_order(active_graph(s), n, l, kind, pin);
        snprintf(out, size, "world/stage/n%llu/b/e%d/%c%d/s", id, row, side, pin);
        return;
    }
    snprintf(out, size, "world/stage/n%llu/b/%c%d/s", id, side, pin);
}

/* ---------- Palette ----------
   Colors drawn as geometry follow the theme; graph.rcss themes the elements. */

typedef struct
{
    uint32_t node;      /* the node body: a hollow socket's fill */
    uint32_t edge;      /* around a filled socket */
    uint32_t exec;      /* execution sockets and wires */
    uint32_t exec_idle; /* an unwired execution socket */
    uint32_t hot;       /* a hovered or offered socket's ring */
    uint32_t grid, grid_major;
    float shade; /* data type colors darken on light canvases */
} canvas_palette;

static const canvas_palette DARK = { 0x2b2b2fffu, 0x111113ffu,
                                     0xe8e8eaffu, 0x8a8a90ffu,
                                     0xffffffffu, 0xffffff0au,
                                     0xffffff14u, 1 };
static const canvas_palette LIGHT = { 0xfbfbfcffu, 0xfbfbfcffu, 0x52525bffu,
                                      0xa1a1aaffu, 0x18181bffu, 0x0000000du,
                                      0x0000001au, 0.8f };

static const canvas_palette *palette(const studio *s)
{
    return s->light ? &LIGHT : &DARK;
}

/* A pin's color as 0xRRGGBBAA. */
static uint32_t pin_color(const studio *s, const pin_spec *spec, float alpha)
{
    const canvas_palette *p = palette(s);
    if (spec->kind == PIN_EXEC) {
        return (p->exec & ~255u) | (uint32_t)lroundf(fminf(alpha, 1) * 255);
    }
    ImVec4 c = ed_spec_color(spec);
    return ((uint32_t)lroundf(c.x * p->shade * 255) << 24) |
           ((uint32_t)lroundf(c.y * p->shade * 255) << 16) |
           ((uint32_t)lroundf(c.z * p->shade * 255) << 8) |
           (uint32_t)lroundf(fminf(c.w * alpha, 1) * 255);
}

static bool is_expression(const studio *s, const node *n)
{
    return behavior_expression_source(graph_editor(s)->asset, n) != NULL;
}

static const char *node_kind(const studio *s, const node *n)
{
    if (is_expression(s, n)) {
        return "expression";
    }
    if (ed_is_graph_call(n)) {
        return "call";
    }
    switch (n->desc->display_class) {
        case NODE_DISPLAY_EVENT: return "event";
        case NODE_DISPLAY_FUNCTION: return "function";
        case NODE_DISPLAY_FLOW: return "flow";
        case NODE_DISPLAY_VARIABLE: return "variable";
        case NODE_DISPLAY_LITERAL: return "literal";
        default: return "pure";
    }
}

static uint32_t node_icon(const studio *s, const node *n)
{
    if (is_expression(s, n)) {
        return KIT_ICON_EXPRESSION;
    }
    if (ed_is_graph_call(n)) {
        return KIT_ICON_FUNCTION;
    }
    switch (n->desc->display_class) {
        case NODE_DISPLAY_EVENT: return KIT_ICON_EVENT;
        case NODE_DISPLAY_FLOW: return KIT_ICON_BRANCH;
        case NODE_DISPLAY_FUNCTION: return KIT_ICON_FUNCTION;
        case NODE_DISPLAY_VARIABLE: return KIT_ICON_FIELD;
        default: return 0;
    }
}

/* ---------- Measurement ---------- */

typedef enum
{
    VALUE_NONE,
    VALUE_CHOICE,
    VALUE_BOOL,
    VALUE_VECTOR,
    VALUE_TEXT,
    VALUE_NUMBER,
    VALUE_CHIP,
} value_kind;

typedef struct
{
    const pin_spec *spec;
    bool connected;
    bool inline_source;
    bool show_label;
    char label[128];
    char chip[160];
    uint32_t chip_icon;
    value_kind value;
} pin_parts;

static const float SOCKET_PAD = 10, PART_GAP = 6, COLUMN_GAP = 12, HEAD_PAD = 10;
static const float NODE_MIN = 120, NODE_MAX = 360, EDGE = 1, BOTTOM = 6;
static const float OPERATOR_MIN = 56, OPERATOR_PAD = 4, SYMBOL_SIZE = 18;
enum
{
    CODE_LINES = 12,
};
/* graph.rcss .gcode: 16 dp lines, 8 dp of padding and 4 below; 28 dp across. */
static const float CODE_SIZE = 11, CODE_LINE = 16, CODE_PAD = 12, CODE_INSET = 28;

static value_kind value_kind_of(const studio *s, node *n, int output, int p)
{
    const pin_spec *spec = output ? &n->output_pins[p] : &n->input_pins[p];
    int width = ed_vector_width(spec);
    const editor_choices *choices = ed_pin_choices(graph_editor(s), n, output, p);
    if (spec->container != PIN_CONTAINER_SINGLE) {
        return VALUE_NONE;
    }
    if (choices && choices->count > 0) {
        return VALUE_CHOICE;
    }
    if (spec->type == PIN_BOOL) {
        return VALUE_BOOL;
    }
    if (width > 0 && !(spec->flags & PIN_FLAG_COLOR)) {
        return VALUE_VECTOR;
    }
    if (spec->type == PIN_STR || spec->type == PIN_NAME) {
        return VALUE_TEXT;
    }
    return spec->type >= PIN_I8 && spec->type <= PIN_F64 ? VALUE_NUMBER : VALUE_NONE;
}

static bool exec_label(const pin_spec *spec, const char *label)
{
    return spec->kind == PIN_EXEC &&
           (!strcmp(label, "Exec") || !strcmp(label, "Then") ||
            !strcmp(label, "In") || !strcmp(label, "Out"));
}

/* "transform.position" reads as "Transform › Position". */
static void path_label(const char *path, char *out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    for (const char *at = path; *at && used < size;) {
        const char *end = strchr(at, '.');
        int length = end ? (int)(end - at) : (int)strlen(at);
        char part[128], word[128];
        snprintf(part, sizeof part, "%.*s", length, at);
        editor_identifier_label(part, word, sizeof word);
        used += (size_t)
            snprintf(out + used, size - used, "%s%s", used ? " \xe2\x80\xba " : "", word);
        at += length + (end != NULL);
    }
}

/* A bound value's chip: what it reads, and an icon for where it comes from.
   Component values always belong to the node's owner, so "Self" is implied. */
static uint32_t binding_chip(const studio *s, const pin_binding *b, char *out, size_t size)
{
    editor *e = graph_editor(s);
    char first[128], second[160];
    if (s->prefs.short_bindings) {
        switch (b->kind) {
            case PIN_BINDING_COMPONENT:
            case PIN_BINDING_COMPONENT_FIELD: {
                const ecs_component_desc *c = ed_component_by_key(b->component_key);
                const char *name = c && c->name ? c->name : b->component_key;
                if (!name) {
                    break;
                }
                editor_component_label(name, first, sizeof first);
                if (b->kind == PIN_BINDING_COMPONENT || !b->field_name) {
                    snprintf(out, size, "%s", first);
                    return KIT_ICON_COMPONENT;
                }
                path_label(b->field_name, second, sizeof second);
                snprintf(out, size, "%s \xe2\x80\xba %s", first, second);
                return KIT_ICON_COMPONENT;
            }
            case PIN_BINDING_VARIABLE: {
                const behavior_variable *v = behavior_asset_find_variable_by_id(e->asset, b->variable_id);
                bool here = !b->source_asset || !e->asset->name ||
                            !strcmp(b->source_asset, e->asset->name);
                if (!here || !v || !v->name) {
                    break;
                }
                editor_identifier_label(v->name, out, size);
                return KIT_ICON_VARIABLE;
            }
            case PIN_BINDING_LOCAL: {
                const graph_local *local = graph_local_by_id(e->active_graph, b->local_id);
                if (!local || !local->name) {
                    break;
                }
                editor_identifier_label(local->name, out, size);
                return KIT_ICON_LOCAL;
            }
            case PIN_BINDING_EVENT_OUTPUT: {
                node *event = graph_node_by_id(e->active_graph, b->event_node_id);
                int output = 0;
                const pin_spec *pin = event
                                        ? graph_pin_by_id(event, b->event_pin_id, &output, NULL)
                                        : NULL;
                if (!pin || !output || !pin->name) {
                    break;
                }
                editor_identifier_label(pin->name, second, sizeof second);
                if (event->reference_name) {
                    snprintf(out, size, "%s \xe2\x80\xba %s", event->reference_name, second);
                } else {
                    snprintf(out, size, "%s", second);
                }
                return KIT_ICON_EVENT;
            }
            case PIN_BINDING_DELTA_TIME:
                snprintf(out, size, "Delta Time");
                return KIT_ICON_TIMER;
            default: break;
        }
    }
    /* Full paths, and missing sources, which say what is missing. */
    snprintf(out, size, "%s", ed_binding_label(e, b, second, sizeof second));
    return 0;
}

static void pin_parts_of(const studio *s, node *n, int kind, int p, pin_parts *out)
{
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    bool input = kind == ED_PIN_INPUT;
    const pin_spec *spec = input ? &n->input_pins[p] : &n->output_pins[p];
    *out = (pin_parts){ .spec = spec };
    node *src = NULL;
    int src_pin = -1;
    if (input) {
        out->connected = graph_input_source(gr, n, p, &src, &src_pin) != 0;
        out->inline_source = out->connected && graph_node_is_inline(gr, src);
    } else {
        out->connected = graph_output_local(gr, n, p) != NULL;
    }
    /* Execution pins, and outputs, are wired when any edge touches them. */
    for (int i = 0; i < acount(gr->edges) && !out->connected; i++) {
        const edge *ed = &gr->edges[i];
        out->connected = input ? ed->to_id == n->id && ed->to_pin_id == spec->id
                               : ed->from_id == n->id && ed->from_pin_id == spec->id;
    }
    ed_pin_label(n, spec, out->label, sizeof out->label);
    out->show_label = !exec_label(spec, out->label);
    if (out->inline_source) {
        ed_node_title(e, src, out->chip, sizeof out->chip);
        out->value = VALUE_CHIP;
    } else if (input && spec->binding) {
        out->chip_icon = binding_chip(s, spec->binding, out->chip, sizeof out->chip);
        out->value = VALUE_CHIP;
    } else if ((!input || !out->connected) && ed_pin_editable(gr, n, kind, p)) {
        out->value = value_kind_of(s, n, !input, p);
    }
}

/* Measured as the canvas draws it at this zoom, so text never clips; far out,
   where rows hide, at the least zoom that shows them. */
static float text_width(const studio *s, const char *text, float size, int weight)
{
    float ratio = fmaxf(s->graph.zoom, FAR_ZOOM) * cf_app_get_pixel_scale();
    return s->graph.kit ? kit_text_width(s->graph.kit, text, size, weight, ratio)
                        : strlen(text) * size * 0.55f;
}

/* The least width of a pin's editor or chip; editors then fill the row. */
static float value_width(const studio *s, const pin_parts *parts)
{
    switch (parts->value) {
        case VALUE_CHOICE: return 96;
        case VALUE_BOOL: return 16;
        case VALUE_VECTOR: return 58.0f * (float)ed_vector_width(parts->spec);
        case VALUE_TEXT: return 96;
        case VALUE_NUMBER: return 64;
        case VALUE_CHIP:
            return fminf(text_width(s, parts->chip, 11, 400) + 12 + (parts->chip_icon ? 16 : 0), 150);
        default: return 0;
    }
}

/* Operators and one-output reads compact (Unreal); anything with execution
   pins, several outputs or an editable output stays a full node. */
static node_form form_of(const studio *s, node *n)
{
    graph *gr = active_graph(s);
    if (!s->prefs.compact_nodes || ed_is_graph_call(n)) {
        return FORM_FULL;
    }
    int inputs = 0, outputs = 0, output = -1;
    for (int kind = ED_PIN_INPUT; kind <= ED_PIN_OUTPUT; kind++) {
        int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
        for (int p = 0; p < count; p++) {
            if (!ed_pin_visible(gr, n, kind, p)) {
                continue;
            }
            if (pin_exec(n, kind, p)) {
                return FORM_FULL;
            }
            inputs += kind == ED_PIN_INPUT;
            outputs += kind == ED_PIN_OUTPUT;
            output = kind == ED_PIN_OUTPUT ? p : output;
        }
    }
    if (outputs != 1) {
        return FORM_FULL;
    }
    pin_parts parts;
    pin_parts_of(s, n, ED_PIN_OUTPUT, output, &parts);
    if (parts.value != VALUE_NONE) {
        return FORM_FULL;
    }
    if (!inputs) {
        return FORM_GETTER;
    }
    return ed_node_appearance(n->desc).symbol ? FORM_OPERATOR : FORM_FULL;
}

/* The first execution pin on a side moves into the title when unlabeled. */
static int title_pin(const graph *gr, node *n, int kind)
{
    int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
    for (int p = 0; p < count; p++) {
        if (!ed_pin_visible(gr, n, kind, p) || !pin_exec(n, kind, p)) {
            continue;
        }
        const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[p]
                                                    : &n->output_pins[p];
        char label[128];
        ed_pin_label(n, spec, label, sizeof label);
        return exec_label(spec, label) ? p : -1;
    }
    return -1;
}

/* Expressions show their source in the body, and pills drop the "Get" every
   read would repeat. */
static const char *node_title(const studio *s, node *n, node_form form, char *buf, size_t size)
{
    if (is_expression(s, n)) {
        snprintf(buf, size, "Expression");
        return buf;
    }
    ed_node_title(graph_editor(s), n, buf, size);
    if (form == FORM_GETTER && !strncmp(buf, "Get ", 4)) {
        memmove(buf, buf + 4, strlen(buf + 4) + 1);
    }
    return buf;
}

/* Breaks source at spaces into lines no wider than `width`, filling starts
   with each line's offset; returns the line count. A word wider than a line
   keeps a line of its own, and the last line takes whatever is left. */
static int code_lines(const studio *s, const char *code, float width, int starts[CODE_LINES])
{
    int count = 1, start = 0, fits = -1; /* fits: the end of the text that fits */
    starts[0] = 0;
    for (int i = 0; count < CODE_LINES;) {
        while (code[i] && code[i] != ' ') {
            i++;
        }
        char line[512];
        snprintf(line, sizeof line, "%.*s", i - start, code + start);
        if (fits > start && text_width(s, line, CODE_SIZE, 400) > width) {
            start = fits + 1;
            starts[count++] = start;
            fits = -1;
            continue;
        }
        fits = i;
        if (!code[i]) {
            break;
        }
        i++;
    }
    return count;
}

/* Nodes are border-box: rows and code sit inside the node's one dp border. */
static float code_room(const node_layout *l)
{
    return l->w - 2 * EDGE - CODE_INSET;
}

static node_layout measure_operator(const studio *s, node *n, node_layout l, const char *symbol)
{
    graph *gr = active_graph(s);
    float values = 0;
    int inputs = 0;
    for (int p = 0; p < n->input_count; p++) {
        if (!ed_pin_visible(gr, n, ED_PIN_INPUT, p)) {
            continue;
        }
        pin_parts parts;
        pin_parts_of(s, n, ED_PIN_INPUT, p, &parts);
        values = fmaxf(values, value_width(s, &parts));
        inputs++;
    }
    l.column = SOCKET_PAD + values;
    l.w = ceilf(
        fmaxf(2 * EDGE + l.column + 2 * PART_GAP + text_width(s, symbol, SYMBOL_SIZE, 700) + SOCKET_PAD, OPERATOR_MIN)
    );
    l.top = EDGE + OPERATOR_PAD;
    l.h = 2 * (EDGE + OPERATOR_PAD) + (float)(inputs > 1 ? inputs : 1) * ROW;
    return l;
}

static node_layout measure_node(const studio *s, node *n)
{
    graph *gr = active_graph(s);
    node_layout l = { .id = n->id, .form = form_of(s, n), .head_in = -1, .head_out = -1 };
    char title[256];
    node_title(s, n, l.form, title, sizeof title);
    ed_node_presentation look = ed_node_appearance(n->desc);
    if (l.form == FORM_OPERATOR) {
        return measure_operator(s, n, l, look.symbol);
    }
    float width = text_width(s, title, 12, 600) + 2 * HEAD_PAD +
                  (look.symbol || node_icon(s, n) ? 20 : 0);
    if (l.form == FORM_GETTER) {
        for (int p = 0; p < n->output_count; p++) {
            l.head_out = ed_pin_visible(gr, n, ED_PIN_OUTPUT, p) ? p : l.head_out;
        }
        l.w = ceilf(2 * EDGE + width + PART_GAP);
        l.h = 2 * EDGE + HEAD;
        return l;
    }
    if (s->prefs.title_flow) {
        l.head_in = title_pin(gr, n, ED_PIN_INPUT);
        l.head_out = title_pin(gr, n, ED_PIN_OUTPUT);
    }
    float values = 0, exec_in = 0, exec_out = 0;
    for (int kind = ED_PIN_INPUT; kind <= ED_PIN_OUTPUT; kind++) {
        int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
        for (int p = 0; p < count; p++) {
            if (!pin_in_rows(gr, n, &l, kind, p)) {
                continue;
            }
            pin_parts parts;
            pin_parts_of(s, n, kind, p, &parts);
            float label = parts.show_label ? text_width(s, parts.label, 12, 400) : 0;
            float value = value_width(s, &parts);
            if (pin_exec(n, kind, p)) {
                float *side = kind == ED_PIN_INPUT ? &exec_in : &exec_out;
                *side = fmaxf(*side, SOCKET_PAD + label);
            } else if (kind == ED_PIN_INPUT) {
                l.label = fmaxf(l.label, label);
                values = fmaxf(values, value);
            } else {
                width = fmaxf(width, 2 * SOCKET_PAD + label + (value > 0 ? PART_GAP + value : 0));
            }
        }
    }
    width = fmaxf(width, exec_in + exec_out + COLUMN_GAP);
    l.label = ceilf(l.label);
    if (l.label > 0 || values > 0) {
        width = fmaxf(
            width,
            2 * SOCKET_PAD + l.label + (l.label > 0 && values > 0 ? PART_GAP : 0) + values
        );
    }
    const char *code = behavior_expression_source(graph_editor(s)->asset, n);
    if (code) {
        width = fmaxf(width, text_width(s, code, CODE_SIZE, 400) + CODE_INSET);
    }
    l.w = ceilf(fminf(fmaxf(2 * EDGE + width, NODE_MIN), NODE_MAX));
    if (code) {
        int starts[CODE_LINES];
        l.code_lines = code_lines(s, code, code_room(&l), starts);
    }
    l.top = EDGE + HEAD + PAD +
            (l.code_lines ? CODE_PAD + (float)l.code_lines * CODE_LINE : 0);
    int rows = row_count(gr, n, &l);
    l.h = rows || l.code_lines ? l.top + (float)rows * ROW + BOTTOM + EDGE
                               : 2 * EDGE + HEAD;
    return l;
}

/* Measured once per frame; mutations clear the cache. */
static const node_layout *layout_of(const studio *s, node *n)
{
    studio_graph *g = (studio_graph *)&s->graph;
    for (int i = 0; i < acount(g->layouts); i++) {
        if (g->layouts[i].id == n->id) {
            return &g->layouts[i];
        }
    }
    node_layout l = measure_node(s, n);
    apush(g->layouts, l);
    return &alast(g->layouts);
}

/* Where a socket sits below its node's top. */
static float pin_y(const studio *s, node *n, int kind, int pin)
{
    node_layout l = *layout_of(s, n);
    if (in_head(&l, kind, pin)) {
        return EDGE + HEAD / 2;
    }
    if (l.form == FORM_OPERATOR && kind == ED_PIN_OUTPUT) {
        return l.h / 2;
    }
    return l.top + ((float)pin_row(active_graph(s), n, &l, kind, pin) + 0.5f) * ROW;
}

/* A node's box in graph units. */
static bool node_box(const studio *s, node *n, float box[4])
{
    const node_layout *l = layout_of(s, n);
    box[0] = n->x;
    box[1] = n->y;
    box[2] = l->w;
    box[3] = l->h;
    return true;
}

typedef struct
{
    uint64_t node;
    int kind; /* -1 on the node body */
    int pin;
    uint64_t comment; /* a comment frame's title */
} graph_hit;

static graph_hit parse_hit(const char *path)
{
    graph_hit hit = { 0, -1, -1, 0 };
    unsigned long long id;
    int used = 0;
    if (path && sscanf(path, "world/stage/c%llu/t", &id) == 1) {
        hit.comment = id;
        return hit;
    }
    if (!path || sscanf(path, "world/stage/n%llu%n", &id, &used) != 1) {
        return hit;
    }
    hit.node = id;
    /* Only a pin's socket and label take a wire; the rest of its row
   belongs to the node, so the node stays easy to grab. */
    const char *row = path + used;
    int exec, pin, end = 0;
    char side;
    if (sscanf(row, "/b/e%d/%c%d%n", &exec, &side, &pin, &end) != 3 &&
        sscanf(row, "/b/%c%d%n", &side, &pin, &end) != 2 &&
        sscanf(row, "/h/%c%d%n", &side, &pin, &end) != 2) {
        return hit;
    }
    const char *part = row + end;
    bool grip = !strncmp(part, "/s", 2) || !strcmp(part, "/l");
    if (grip && (side == 'i' || side == 'o')) {
        hit.kind = side == 'i' ? ED_PIN_INPUT : ED_PIN_OUTPUT;
        hit.pin = pin;
    }
    return hit;
}

/* ---------- Camera ---------- */

static float canvas_w(const studio_graph *g)
{
    return g->surface.rect.w / g->zoom;
}

static float canvas_h(const studio_graph *g)
{
    return g->surface.rect.h / g->zoom;
}

/* Window px to graph units. */
static void to_world(const studio_graph *g, float wx, float wy, float *x, float *y)
{
    *x = g->x + (wx - g->surface.rect.x) / g->zoom;
    *y = g->y + (wy - g->surface.rect.y) / g->zoom;
}

static void zoom_at(studio_graph *g, float zoom, float wx, float wy)
{
    zoom = fminf(fmaxf(zoom, ZOOM_MIN), ZOOM_MAX);
    float fx, fy;
    to_world(g, wx, wy, &fx, &fy);
    g->zoom = zoom;
    g->x = fx - (wx - g->surface.rect.x) / zoom;
    g->y = fy - (wy - g->surface.rect.y) / zoom;
    g->camera.active = false;
}

static void camera_to(studio *s, float zoom, float x, float y, bool animate)
{
    studio_graph *g = &s->graph;
    if (!animate || s->prefs.reduced_motion) {
        g->zoom = zoom;
        g->x = x;
        g->y = y;
        g->camera.active = false;
        return;
    }
    g->camera.from_zoom = g->zoom;
    g->camera.from_x = g->x;
    g->camera.from_y = g->y;
    g->camera.zoom = zoom;
    g->camera.x = x;
    g->camera.y = y;
    g->camera.start = s->time;
    g->camera.active = true;
}

/* Interpolates in screen space so the path neither swoops nor drifts: the
   visible center moves linearly while zoom eases (easeInOutCubic). */
static void camera_step(studio *s)
{
    studio_graph *g = &s->graph;
    if (!g->camera.active) {
        return;
    }
    double t = fmin((s->time - g->camera.start) / FIT_SECONDS, 1);
    float u = (float)(t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2);
    float w = g->surface.rect.w, h = g->surface.rect.h;
    float from_cx = g->camera.from_x + w / 2 / g->camera.from_zoom;
    float from_cy = g->camera.from_y + h / 2 / g->camera.from_zoom;
    float to_cx = g->camera.x + w / 2 / g->camera.zoom;
    float to_cy = g->camera.y + h / 2 / g->camera.zoom;
    g->zoom = g->camera.from_zoom + (g->camera.zoom - g->camera.from_zoom) * u;
    g->x = from_cx + (to_cx - from_cx) * u - w / 2 / g->zoom;
    g->y = from_cy + (to_cy - from_cy) * u - h / 2 / g->zoom;
    g->camera.active = t < 1;
}

/* Fits the selection, or everything, with 10% padding and never past 1:1. */
static void frame_nodes(studio *s, bool selection, bool animate)
{
    studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    editor *e = graph_editor(s);
    if (!gr || g->surface.rect.w <= 0) {
        return;
    }
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float box[4];
        if (!node_visible(gr, n) || (selection && !ed_node_selected(e, n)) ||
            !node_box(s, n, box)) {
            continue;
        }
        lo[0] = fminf(lo[0], box[0]);
        lo[1] = fminf(lo[1], box[1]);
        hi[0] = fmaxf(hi[0], box[0] + box[2]);
        hi[1] = fmaxf(hi[1], box[1] + box[3]);
    }
    if (lo[0] > hi[0]) {
        if (selection) {
            frame_nodes(s, false, animate);
        }
        return;
    }
    float w = g->surface.rect.w, h = g->surface.rect.h;
    float bw = hi[0] - lo[0], bh = hi[1] - lo[1];
    float zoom = fminf(fminf(w / (bw * 1.2f), h / (bh * 1.2f)), 1);
    zoom = fminf(fmaxf(zoom, ZOOM_MIN), ZOOM_MAX);
    camera_to(s, zoom, lo[0] + bw / 2 - w / 2 / zoom, lo[1] + bh / 2 - h / 2 / zoom, animate);
}

static studio_graph_view *view_for(studio_graph *g, uint64_t graph_id)
{
    for (int i = 0; i < acount(g->views); i++) {
        if (g->views[i].graph_id == graph_id) {
            return &g->views[i];
        }
    }
    return NULL;
}

/* Undo replaces the asset and its graphs, so views key on the behavior's name
   and the graph's saved ID rather than on memory. */
static uint64_t graph_key(const studio *s)
{
    graph *gr = active_graph(s);
    if (!gr) {
        return 0;
    }
    uint64_t h = 14695981039346656037ull;
    for (const char *c = s->scene->graph_document_id; *c; c++) {
        h = (h ^ (unsigned char)*c) * 1099511628211ull;
    }
    return (h ^ gr->graph_id) * 1099511628211ull | 1;
}

/* Each graph keeps its own camera; one seen for the first time is framed.
   Returns true when a different graph came into view. */
static bool follow_graph(studio *s)
{
    studio_graph *g = &s->graph;
    uint64_t id = graph_key(s);
    if (id == g->graph_id) {
        return false;
    }
    studio_graph_view *old = view_for(g, g->graph_id);
    if (g->graph_id && !old) {
        apush(g->views, ((studio_graph_view){ g->graph_id }));
        old = &alast(g->views);
    }
    if (old) {
        *old = (studio_graph_view){ g->graph_id, g->zoom, g->x, g->y };
    }
    g->graph_id = id;
    g->gesture = STUDIO_GRAPH_IDLE;
    g->camera.active = false;
    studio_graph_view *view = view_for(g, id);
    g->framed = view != NULL;
    if (view) {
        g->zoom = view->zoom;
        g->x = view->x;
        g->y = view->y;
    } else {
        g->zoom = 1;
        g->x = -40;
        g->y = -40;
    }
    return true;
}

/* ---------- Arrange ---------- */

typedef struct
{
    node **nodes;
    arrange_box *boxes;
    arrange_point *pos;
    arrange_link *links;
    int *edge_link; /* per edge: its link, or -1 */
} arrange_input;

static int input_index(node *const *nodes, const node *n)
{
    for (int i = 0; i < acount(nodes); i++) {
        if (nodes[i] == n) {
            return i;
        }
    }
    return -1;
}

static void arrange_input_build(const studio *s, arrange_input *in)
{
    graph *gr = active_graph(s);
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float box[4];
        if (!node_visible(gr, n) || !node_box(s, n, box)) {
            continue;
        }
        apush(in->nodes, n);
        apush(in->boxes, ((arrange_box){ box[2], box[3] }));
        apush(in->pos, ((arrange_point){ n->x, n->y }));
    }
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        apush(in->edge_link, -1);
        node *from = graph_node_by_id(gr, ed->from_id),
             *to = graph_node_by_id(gr, ed->to_id);
        int from_out = 0, to_out = 0, from_pin = -1, to_pin = -1;
        if (!from || !to ||
            !graph_pin_by_id(from, ed->from_pin_id, &from_out, &from_pin) || !from_out ||
            !graph_pin_by_id(to, ed->to_pin_id, &to_out, &to_pin) || to_out) {
            continue;
        }
        int a = input_index(in->nodes, from), b = input_index(in->nodes, to);
        if (a < 0 || b < 0) {
            continue;
        }
        in->edge_link[i] = acount(in->links);
        apush(
            in->links,
            ((arrange_link){ .from = a,
                             .to = b,
                             .from_y = pin_y(s, from, ED_PIN_OUTPUT, from_pin),
                             .to_y = pin_y(s, to, ED_PIN_INPUT, to_pin) })
        );
    }
}

static void arrange_input_free(arrange_input *in)
{
    afree(in->nodes);
    afree(in->boxes);
    afree(in->pos);
    afree(in->links);
    afree(in->edge_link);
}

static arrange_graph arrange_of(const arrange_input *in)
{
    return (arrange_graph){ in->boxes, acount(in->boxes), in->links, acount(in->links) };
}

static uint64_t hash(uint64_t h, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ bytes[i]) * 1099511628211ull;
    }
    return h;
}

/* Lanes for wires that skip a column, rebuilt only when the layout changes. */
static void update_routes(studio *s)
{
    studio_graph *g = &s->graph;
    arrange_input in = { 0 };
    arrange_input_build(s, &in);
    uint64_t key = 14695981039346656037ull;
    key = hash(key, in.boxes, sizeof *in.boxes * (size_t)acount(in.boxes));
    key = hash(key, in.pos, sizeof *in.pos * (size_t)acount(in.pos));
    key = hash(key, in.links, sizeof *in.links * (size_t)acount(in.links));
    if (key != g->measured_key || !g->routes.start) {
        arrange_graph layout = arrange_of(&in);
        arrange_route(&layout, in.pos, &g->routes);
        g->measured_key = key;
    }
    afree(g->edge_route);
    g->edge_route = in.edge_link;
    in.edge_link = NULL;
    arrange_input_free(&in);
}

/* A user's Arrange moves every node as one undo step. Opening a graph lays
   out only its automatic nodes, whose positions documents don't keep, so that
   is neither an edit nor history. */
static void arrange(studio *s, bool automatic_only)
{
    editor *e = graph_editor(s);
    arrange_input in = { 0 };
    arrange_input_build(s, &in);
    arrange_graph layout = arrange_of(&in);
    arrange_layout(&layout, in.pos);
    bool moved = false;
    for (int i = 0; i < acount(in.nodes) && !moved; i++) {
        moved = !(automatic_only && in.nodes[i]->placed) &&
                (in.nodes[i]->x != in.pos[i].x || in.nodes[i]->y != in.pos[i].y);
    }
    if (moved && !automatic_only) {
        ed_snapshot_before(e);
    }
    for (int i = 0; moved && i < acount(in.nodes); i++) {
        if (!(automatic_only && in.nodes[i]->placed)) {
            in.nodes[i]->x = in.pos[i].x;
            in.nodes[i]->y = in.pos[i].y;
            in.nodes[i]->placed = false;
        }
    }
    arrange_input_free(&in);
}

/* ---------- Mutations ---------- */

static node *node_by_id(const studio *s, uint64_t id)
{
    graph *gr = active_graph(s);
    return gr && id ? graph_node_by_id(gr, id) : NULL;
}

static graph_comment_box *comment_by_id(const studio *s, uint64_t id)
{
    graph *gr = active_graph(s);
    for (int i = 0; gr && id && i < acount(gr->comment_boxes); i++) {
        if (gr->comment_boxes[i].id == id) {
            return &gr->comment_boxes[i];
        }
    }
    return NULL;
}

/* The nodes a frame carries: those wholly inside it when the drag begins. */
static void comment_contents(studio *s, uint64_t id, bool frame_only)
{
    studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    graph_comment_box *box = comment_by_id(s, id);
    if (!box || frame_only) {
        return;
    }
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float b[4];
        if (node_visible(gr, n) && node_box(s, n, b) && b[0] >= box->x &&
            b[1] >= box->y && b[0] + b[2] <= box->x + box->width &&
            b[1] + b[3] <= box->y + box->height) {
            apush(g->drag, ((typeof(*g->drag)){ n->id, n->x, n->y }));
        }
    }
}

/* C frames the selection with a comment (Unreal, Blender's Ctrl J). */
static void frame_selection(studio *s)
{
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    float lo[2] = { INFINITY, INFINITY }, hi[2] = { -INFINITY, -INFINITY };
    for (int i = 0; i < acount(e->selected); i++) {
        float b[4];
        if (node_box(s, e->selected[i], b)) {
            lo[0] = fminf(lo[0], b[0]);
            lo[1] = fminf(lo[1], b[1]);
            hi[0] = fmaxf(hi[0], b[0] + b[2]);
            hi[1] = fmaxf(hi[1], b[1] + b[3]);
        }
    }
    if (lo[0] > hi[0]) {
        return;
    }
    const float pad = 24, title = 32;
    ed_snapshot_before(e);
    graph_add_comment_box(
        gr,
        "Comment",
        lo[0] - pad,
        lo[1] - pad - title,
        hi[0] - lo[0] + 2 * pad,
        hi[1] - lo[1] + 2 * pad + title
    );
}

static void show_error(studio *s, const char *message)
{
    kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                           .title = "Can't connect",
                           .description = message }
    );
    studio_record(s, KIT_TOAST_WARNING, message);
}

/* Connects two sockets through the editor's rules (conversions, inline
   sources, exec ordering). Reports the refusal the editor gives. */
static bool connect_pins(studio *s, node *a, int a_kind, int a_pin, node *b, int b_kind, int b_pin)
{
    editor *e = graph_editor(s);
    ed_pin_view from = { a, a_kind, a_pin, { 0, 0 }, 0 };
    ed_pin_view to = { b, b_kind, b_pin, { 0, 0 }, 0 };
    ed_clear_connect_error(e);
    e->pending_snapshot = ed_snapshot(e);
    int connected = ed_connect_with_inline(e, &from, &to, false);
    if (connected) {
        ed_commit_snapshot(e);
    } else {
        ed_cancel_snapshot(e);
        if (e->connect_error[0]) {
            show_error(s, e->connect_error);
        }
    }
    return connected;
}

static bool pins_compatible(const node *a, int a_kind, int a_pin, const node *b, int b_kind, int b_pin)
{
    if (a == b || a_kind == b_kind) {
        return false;
    }
    const pin_spec *out = a_kind == ED_PIN_OUTPUT ? &a->output_pins[a_pin]
                                                  : &b->output_pins[b_pin];
    const pin_spec *in = a_kind == ED_PIN_OUTPUT ? &b->input_pins[b_pin]
                                                 : &a->input_pins[a_pin];
    return out->kind == in->kind && ed_connection_specs_valid(out, in, out->kind) != 0;
}

/* Dropped on a node body: the first compatible socket, exact types first. */
static int body_target(const graph *gr, const node *from, int kind, int pin, const node *to)
{
    int want = kind == ED_PIN_OUTPUT ? ED_PIN_INPUT : ED_PIN_OUTPUT;
    int count = want == ED_PIN_INPUT ? to->input_count : to->output_count;
    int fallback = -1;
    for (int p = 0; p < count; p++) {
        if (!ed_pin_visible(gr, to, want, p) ||
            !pins_compatible(from, kind, pin, to, want, p)) {
            continue;
        }
        node *src = NULL;
        int src_pin = -1;
        bool taken = want == ED_PIN_INPUT &&
                     graph_input_source(gr, to, p, &src, &src_pin);
        if (!taken) {
            return p;
        }
        if (fallback < 0) {
            fallback = p;
        }
    }
    return fallback;
}

static void select_only(editor *e, node *n)
{
    ed_clear_selection(e);
    if (n) {
        ed_add_selected(e, n);
    }
}

static void delete_selection(studio *s)
{
    editor *e = graph_editor(s);
    if (graph_readonly(s) || (!acount(e->selected) && !acount(e->selected_links))) {
        return;
    }
    ed_delete_selected(e);
}

static void copy_selection(studio *s)
{
    editor *e = graph_editor(s);
    if (!acount(e->selected)) {
        return;
    }
    char *json = graph_save_nodes_json(e->active_graph, e->selected, acount(e->selected));
    if (json) {
        cf_clipboard_set(json);
        free(e->clipboard);
        e->clipboard = json;
    }
}

static void paste(studio *s)
{
    editor *e = graph_editor(s);
    if (graph_readonly(s)) {
        return;
    }
    char *clipboard = cf_clipboard_get();
    const char *text = clipboard && clipboard[0] ? clipboard : e->clipboard;
    if (text) {
        ed_snapshot_before(e);
        int first = acount(e->active_graph->nodes);
        if (graph_append_json(e->active_graph, text, 24, 24) == 0) {
            ed_clear_selection(e);
            for (int i = first; i < acount(e->active_graph->nodes); i++) {
                if (node_visible(e->active_graph, e->active_graph->nodes[i])) {
                    ed_add_selected(e, e->active_graph->nodes[i]);
                }
            }
        }
    }
    if (clipboard) {
        cf_free(clipboard);
    }
}

/* ---------- Input ---------- */

static float drag_distance(const studio_graph *g)
{
    return hypotf(
        g->in.mouse_x * g->zoom + g->surface.rect.x - g->press_x,
        g->in.mouse_y * g->zoom + g->surface.rect.y - g->press_y
    );
}

/* The wire under the pointer, from the curves drawn last frame. */
static int hit_wire(const studio *s)
{
    const studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    float best = WIRE_HIT / g->zoom;
    int found = -1;
    float points[CURVE_POINTS * 2];
    for (int w = 0; w < acount(g->wire_edges); w++) {
        int count = kit_wire_points(g->kit, "back/wires", w, points, CURVE_POINTS);
        count = count < CURVE_POINTS ? count : CURVE_POINTS;
        for (int i = 1; i < count; i++) {
            float ax = points[i * 2 - 2], ay = points[i * 2 - 1];
            float bx = points[i * 2], by = points[i * 2 + 1];
            float dx = bx - ax, dy = by - ay, px = g->in.mouse_x - ax,
                  py = g->in.mouse_y - ay;
            float t = fmaxf(0, fminf(1, (px * dx + py * dy) / fmaxf(dx * dx + dy * dy, 1e-6f)));
            float d = hypotf(px - dx * t, py - dy * t);
            if (d < best) {
                best = d;
                found = g->wire_edges[w];
            }
        }
    }
    return found < acount(gr->edges) ? found : -1;
}

static void link_ref_of(const edge *ed, ed_link_ref *out)
{
    *out = (ed_link_ref){ ed->from_id, ed->from_pin_id, ed->to_id, ed->to_pin_id };
}

static void begin_link(studio *s, node *n, int kind, int pin)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    node *src = NULL;
    int src_pin = -1;
    /* Dragging a connected data input picks its wire up (Blender, Rete). */
    if (kind == ED_PIN_INPUT && n->input_pins[pin].kind == PIN_DATA &&
        graph_input_source(gr, n, pin, &src, &src_pin) &&
        !graph_node_is_inline(gr, src)) {
        ed_snapshot_before(e);
        graph_remove_edge(gr, src, src_pin, n, pin);
        n = src;
        kind = ED_PIN_OUTPUT;
        pin = src_pin;
    }
    g->gesture = STUDIO_GRAPH_LINK;
    g->link_node = n->id;
    g->link_kind = kind;
    g->link_pin = pin;
}

static void end_link(studio *s, graph_hit hit)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    node *from = node_by_id(s, g->link_node);
    node *to = node_by_id(s, hit.node);
    g->gesture = STUDIO_GRAPH_IDLE;
    if (!from) {
        return;
    }
    if (to && hit.kind >= 0 && hit.pin >= 0) {
        if (pins_compatible(from, g->link_kind, g->link_pin, to, hit.kind, hit.pin) ||
            hit.kind != g->link_kind) {
            connect_pins(s, from, g->link_kind, g->link_pin, to, hit.kind, hit.pin);
        }
        return;
    }
    if (to && to != from) {
        int target = body_target(gr, from, g->link_kind, g->link_pin, to);
        if (target >= 0) {
            connect_pins(
                s,
                from,
                g->link_kind,
                g->link_pin,
                to,
                g->link_kind == ED_PIN_OUTPUT ? ED_PIN_INPUT : ED_PIN_OUTPUT,
                target
            );
        }
        return;
    }
    if (!to) {
        /* Dropped on empty canvas: search for a node that fits, spawned here
           and wired (Blender, Unreal). */
        e->spawn_link_pending = 1;
        e->spawn_link_pin = (ed_pin_view){ from, g->link_kind, g->link_pin, { 0, 0 }, 0 };
        e->pending_snapshot = ed_snapshot(e);
        g->spawn_pending = true;
        g->spawn_x = g->world_x;
        g->spawn_y = g->world_y;
        kit_command_open(s->kit, "Add a node for this wire");
    }
}

static void finish_box(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    float x0 = fminf(g->box[0], g->box[2]), x1 = fmaxf(g->box[0], g->box[2]);
    float y0 = fminf(g->box[1], g->box[3]), y1 = fmaxf(g->box[1], g->box[3]);
    ed_clear_selection(e);
    for (int i = 0; i < acount(g->box_base); i++) {
        node *n = graph_node_by_id(gr, g->box_base[i]);
        if (n) {
            ed_add_selected(e, n);
        }
    }
    for (int i = 0; i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        float b[4];
        if (!node_visible(gr, n) || !node_box(s, n, b)) {
            continue;
        }
        bool inside = b[0] < x1 && b[0] + b[2] > x0 && b[1] < y1 && b[1] + b[3] > y0;
        if (inside && !ed_node_selected(e, n)) {
            ed_add_selected(e, n);
        }
    }
}

/* Scrolling zooms at the pointer, from a wheel or a trackpad alike (Blender,
   Unreal); panning is a middle, right or Space drag. A notch is about 15%, and
   one event never more than doubles or halves the zoom. */
static void wheel(studio_graph *g, const editor_rml_input *ev)
{
    float factor = fminf(fmaxf(powf(2, -ev->wheel_y * 0.2f), 0.5f), 2.0f);
    zoom_at(g, g->zoom * factor, ev->x, ev->y);
}

static void press(studio *s, int button, graph_hit hit, bool on_control)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    unsigned mods = g->in.modifiers;
    bool additive = mods & (KIT_MOD_SHIFT | KIT_MOD_CTRL | KIT_MOD_META);
    if (button == 2 || (button == 0 && g->space)) {
        g->gesture = STUDIO_GRAPH_PAN;
        return;
    }
    if (button == 1) {
        g->gesture = STUDIO_GRAPH_RIGHT;
        return;
    }
    if (on_control) {
        return;
    }
    g->press_comment = 0;
    graph_comment_box *box = comment_by_id(s, hit.comment);
    if (box) {
        /* Dragging a frame's title carries what it frames; Shift moves the
           frame alone (Unreal, Blender). */
        g->press_comment = box->id;
        g->comment_x = box->x;
        g->comment_y = box->y;
        g->press_node = 0;
        g->press_toggle = false;
        g->gesture = STUDIO_GRAPH_PRESS;
        return;
    }
    node *n = node_by_id(s, hit.node);
    if (n && g->double_click && ed_is_graph_call(n)) {
        /* Double-click enters a call (Blender's Tab); Back returns. */
        editor_open_graph_call(e, n->id);
        return;
    }
    if (n && hit.kind >= 0 && hit.pin >= 0 && !graph_readonly(s)) {
        begin_link(s, n, hit.kind, hit.pin);
        return;
    }
    if (n) {
        bool selected = ed_node_selected(e, n);
        g->press_node = n->id;
        g->press_toggle = additive && selected;
        if (!selected) {
            if (!additive) {
                ed_clear_selection(e);
            }
            ed_add_selected(e, n);
        }
        e->front_node = n;
        g->gesture = STUDIO_GRAPH_PRESS;
        return;
    }
    if (g->hover_edge >= 0 && g->hover_edge < acount(gr->edges)) {
        ed_link_ref link;
        link_ref_of(&gr->edges[g->hover_edge], &link);
        if ((mods & KIT_MOD_ALT) && !graph_readonly(s)) {
            ed_clear_selection(e);
            ed_add_selected_link(e, &link);
            ed_delete_selected(e);
            return;
        }
        if (!additive) {
            ed_clear_selection(e);
        }
        ed_add_selected_link(e, &link);
        return;
    }
    g->gesture = STUDIO_GRAPH_BOX;
    g->box[0] = g->press_wx;
    g->box[1] = g->press_wy;
    g->box[2] = g->world_x;
    g->box[3] = g->world_y;
    asetlen(g->box_base, 0);
    if (additive) {
        for (int i = 0; i < acount(e->selected); i++) {
            apush(g->box_base, e->selected[i]->id);
        }
    }
    ed_clear_selection(e);
}

static void drag(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    switch (g->gesture) {
        case STUDIO_GRAPH_PRESS:
            if (drag_distance(g) < THRESHOLD || graph_readonly(s)) {
                break;
            }
            g->gesture = STUDIO_GRAPH_MOVE;
            asetlen(g->drag, 0);
            if (g->press_comment) {
                comment_contents(s, g->press_comment, g->in.modifiers & KIT_MOD_SHIFT);
            } else {
                for (int i = 0; i < acount(e->selected); i++) {
                    node *n = e->selected[i];
                    apush(g->drag, ((typeof(*g->drag)){ n->id, n->x, n->y }));
                }
            }
            e->pending_snapshot = ed_snapshot(e);
            /* fallthrough */
        case STUDIO_GRAPH_MOVE: {
            float dx = g->world_x - g->press_wx, dy = g->world_y - g->press_wy;
            graph_comment_box *box = comment_by_id(s, g->press_comment);
            if (box) {
                box->x = g->comment_x + dx;
                box->y = g->comment_y + dy;
            }
            for (int i = 0; i < acount(g->drag); i++) {
                node *n = node_by_id(s, g->drag[i].id);
                if (n) {
                    n->x = g->drag[i].x + dx;
                    n->y = g->drag[i].y + dy;
                    n->placed = true;
                }
            }
            break;
        }
        case STUDIO_GRAPH_BOX:
            g->box[2] = g->world_x;
            g->box[3] = g->world_y;
            finish_box(s);
            break;
        case STUDIO_GRAPH_RIGHT:
            if (drag_distance(g) >= THRESHOLD) {
                g->gesture = STUDIO_GRAPH_PAN;
            }
            break;
        default: break;
    }
}

static void release(studio *s, graph_hit hit)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    switch (g->gesture) {
        case STUDIO_GRAPH_PRESS: {
            /* A click on a selected node without a drag narrows the selection
               to it; with a modifier it toggles. */
            node *n = node_by_id(s, g->press_node);
            if (n && g->press_toggle) {
                ed_remove_selected(e, n);
            } else if (n && !(g->in.modifiers & (KIT_MOD_SHIFT | KIT_MOD_CTRL | KIT_MOD_META))) {
                select_only(e, n);
            }
            break;
        }
        case STUDIO_GRAPH_MOVE: {
            graph_comment_box *box = comment_by_id(s, g->press_comment);
            bool moved = box && (box->x != g->comment_x || box->y != g->comment_y);
            for (int i = 0; i < acount(g->drag); i++) {
                node *n = node_by_id(s, g->drag[i].id);
                moved |= n && (n->x != g->drag[i].x || n->y != g->drag[i].y);
            }
            if (moved) {
                ed_commit_snapshot(e);
            } else {
                ed_cancel_snapshot(e);
            }
            break;
        }
        case STUDIO_GRAPH_LINK: end_link(s, hit); return;
        case STUDIO_GRAPH_RIGHT: {
            /* A right click that didn't move opens the node or wire menu, or on
               empty canvas the add-node search (Unreal's action menu). */
            graph_hit at = parse_hit(g->press_path[0] ? g->press_path : NULL);
            node *n = node_by_id(s, at.node);
            int wire = n || at.comment ? -1 : hit_wire(s);
            g->menu_comment = at.comment;
            if (n || wire >= 0 || at.comment) {
                if (n && !ed_node_selected(e, n)) {
                    select_only(e, n);
                }
                g->menu_node = n ? n->id : 0;
                g->menu_edge = wire;
                g->menu_request = true;
                g->menu_x = g->press_x;
                g->menu_y = g->press_y;
            } else if (!graph_readonly(s)) {
                g->spawn_pending = true;
                g->spawn_x = g->world_x;
                g->spawn_y = g->world_y;
                kit_command_open(s->kit, "Add a node");
            }
            break;
        }
        default: break;
    }
    g->gesture = STUDIO_GRAPH_IDLE;
}

static void cancel_gesture(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    if (g->gesture == STUDIO_GRAPH_MOVE) {
        for (int i = 0; i < acount(g->drag); i++) {
            node *n = node_by_id(s, g->drag[i].id);
            if (n) {
                n->x = g->drag[i].x;
                n->y = g->drag[i].y;
            }
        }
        ed_cancel_snapshot(e);
    }
    g->gesture = STUDIO_GRAPH_IDLE;
}

/* Replays this frame's window input into the canvas context: pointer input
   while the canvas is hovered or holds a press, keys while one of its fields
   has focus. The canvas's own gestures read the replayed state. */
static void replay(studio *s)
{
    studio_graph *g = &s->graph;
    kit_frame_input *in = &g->in;
    for (int b = 0; b < 3; b++) {
        in->pressed[b] = in->released[b] = false;
    }
    in->wheel = 0;
    g->press_button = -1;
    bool blocked = kit_command_is_open(s->kit) ||
                   (kit_wants_pointer(s->kit) && !g->surface.hovered);
    for (int i = 0; i < acount(g->events); i++) {
        editor_rml_input ev = g->events[i];
        bool pointer = ev.type <= EDITOR_RML_POINTER_WHEEL;
        if (pointer) {
            float cx = (ev.x - g->surface.rect.x) / g->zoom;
            float cy = (ev.y - g->surface.rect.y) / g->zoom;
            bool inside = ev.x >= g->surface.rect.x && ev.y >= g->surface.rect.y &&
                          ev.x < g->surface.rect.x + g->surface.rect.w &&
                          ev.y < g->surface.rect.y + g->surface.rect.h;
            bool take = g->captured || (inside && !blocked && g->surface.hovered);
            if (ev.type == EDITOR_RML_POINTER_DOWN && !take) {
                kit_blur(g->kit);
                continue;
            }
            if (!take) {
                if (ev.type == EDITOR_RML_POINTER_MOVE && in->mouse_x > -1e8f) {
                    editor_rml_input leave = { .type = EDITOR_RML_POINTER_LEAVE };
                    editor_rml_backend_process_input(g->backend, &leave);
                    in->mouse_x = in->mouse_y = -1e9f;
                }
                continue;
            }
            in->mouse_x = cx;
            in->mouse_y = cy;
            ev.x = cx;
            ev.y = cy;
            in->modifiers = ((ev.modifiers & EDITOR_RML_MOD_CONTROL) ? KIT_MOD_CTRL
                                                                     : 0u) |
                            ((ev.modifiers & EDITOR_RML_MOD_SHIFT) ? KIT_MOD_SHIFT : 0u) |
                            ((ev.modifiers & EDITOR_RML_MOD_ALT) ? KIT_MOD_ALT : 0u) |
                            ((ev.modifiers & EDITOR_RML_MOD_META) ? KIT_MOD_META : 0u);
            if (ev.type == EDITOR_RML_POINTER_WHEEL) {
                wheel(g, &g->events[i]);
                continue;
            }
            if (ev.type == EDITOR_RML_POINTER_DOWN && ev.button >= 0 && ev.button < 3) {
                in->down[ev.button] = in->pressed[ev.button] = true;
                g->captured = true;
                s->focus_area = g->area;
                /* Hit-test where the press happened: a quick click or drag can
                   arrive within one frame. */
                const char *path = kit_hovered_path(g->kit);
                snprintf(g->press_path, sizeof g->press_path, "%s", path ? path : "");
                float wx = g->events[i].x, wy = g->events[i].y;
                g->double_click = ev.button == 0 &&
                                  s->time - g->last_press_time < 0.35 &&
                                  hypotf(wx - g->last_press_x, wy - g->last_press_y) <
                                      THRESHOLD;
                g->last_press_time = g->double_click ? 0 : s->time;
                g->last_press_x = wx;
                g->last_press_y = wy;
                g->press_control = kit_pointer_on_control(g->kit);
                g->press_button = ev.button;
                g->press_x = g->events[i].x;
                g->press_y = g->events[i].y;
                to_world(g, g->press_x, g->press_y, &g->press_wx, &g->press_wy);
            }
            if (ev.type == EDITOR_RML_POINTER_UP && ev.button >= 0 && ev.button < 3) {
                in->down[ev.button] = false;
                in->released[ev.button] = true;
                g->captured = in->down[0] || in->down[1] || in->down[2];
            }
        } else if (!kit_wants_keyboard(g->kit)) {
            continue;
        }
        editor_rml_backend_process_input(g->backend, &ev);
    }
    asetlen(g->events, 0);
}

/* Where a socket sits, from the same metrics that size nodes. */
static void socket_at(const studio *s, node *n, int kind, int pin, float *x, float *y)
{
    *x = kind == ED_PIN_INPUT ? n->x : n->x + layout_of(s, n)->w;
    *y = n->y + pin_y(s, n, kind, pin);
}

static void snap_link(studio *s, graph_hit *hit)
{
    studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    node *from = node_by_id(s, g->link_node);
    int want = g->link_kind == ED_PIN_OUTPUT ? ED_PIN_INPUT : ED_PIN_OUTPUT;
    float best = 20 / g->zoom;
    for (int i = 0; from && i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        int count = want == ED_PIN_INPUT ? n->input_count : n->output_count;
        if (n == from || !node_visible(gr, n)) {
            continue;
        }
        for (int p = 0; p < count; p++) {
            float x, y;
            if (!ed_pin_visible(gr, n, want, p) ||
                !pins_compatible(from, g->link_kind, g->link_pin, n, want, p)) {
                continue;
            }
            socket_at(s, n, want, p, &x, &y);
            float d = hypotf(x - g->world_x, y - g->world_y);
            if (d < best) {
                best = d;
                *hit = (graph_hit){ n->id, want, p, 0 };
            }
        }
    }
}

/* Dragging near an edge scrolls the view, faster closer in: up to 900 px/s
   within 40 px of the edge (xyflow). */
static void auto_pan(studio *s)
{
    studio_graph *g = &s->graph;
    double dt = fmin(s->time - g->pan_time, 0.05);
    g->pan_time = s->time;
    if (g->gesture != STUDIO_GRAPH_MOVE && g->gesture != STUDIO_GRAPH_BOX &&
        g->gesture != STUDIO_GRAPH_LINK) {
        return;
    }
    const float edge = 40, speed = 900;
    float px = g->in.mouse_x * g->zoom, py = g->in.mouse_y * g->zoom;
    float w = g->surface.rect.w, h = g->surface.rect.h;
    float vx = px < edge     ? -(edge - px) / edge
             : px > w - edge ? (px - (w - edge)) / edge
                             : 0;
    float vy = py < edge     ? -(edge - py) / edge
             : py > h - edge ? (py - (h - edge)) / edge
                             : 0;
    vx = fmaxf(-1, fminf(1, vx));
    vy = fmaxf(-1, fminf(1, vy));
    if (vx == 0 && vy == 0) {
        return;
    }
    g->x += vx * speed * (float)dt / g->zoom;
    g->y += vy * speed * (float)dt / g->zoom;
    g->world_x += vx * speed * (float)dt / g->zoom;
    g->world_y += vy * speed * (float)dt / g->zoom;
    g->camera.active = false;
}

static void pointer(studio *s)
{
    studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    to_world(
        g,
        g->in.mouse_x * g->zoom + g->surface.rect.x,
        g->in.mouse_y * g->zoom + g->surface.rect.y,
        &g->world_x,
        &g->world_y
    );
    bool inside = g->in.mouse_x > -1e8f;
    graph_hit hit = inside ? parse_hit(kit_hovered_path(g->kit))
                           : (graph_hit){ 0, -1, -1, 0 };
    bool on_control = inside && kit_pointer_on_control(g->kit);
    node *hovered = node_by_id(s, hit.node);
    g->hover_node = hovered ? hovered->id : 0;
    g->hover_kind = hovered && !on_control ? hit.kind : -1;
    g->hover_pin = hovered && !on_control ? hit.pin : -1;
    g->hover_edge = inside && !hovered && g->gesture == STUDIO_GRAPH_IDLE
                      ? hit_wire(s)
                      : -1;
    if (g->press_button >= 0 && g->gesture == STUDIO_GRAPH_IDLE) {
        press(s, g->press_button, parse_hit(g->press_path[0] ? g->press_path : NULL), g->press_control);
    }
    if (g->gesture == STUDIO_GRAPH_LINK && hit.kind < 0 && inside) {
        /* A wire snaps to the nearest socket it fits within 20 px (xyflow). */
        snap_link(s, &hit);
        g->hover_node = hit.node;
        g->hover_kind = hit.kind;
        g->hover_pin = hit.pin;
    }
    auto_pan(s);
    if (g->gesture == STUDIO_GRAPH_PAN) {
        /* The graph point grabbed at the press stays under the pointer. */
        g->x = g->press_wx - g->in.mouse_x;
        g->y = g->press_wy - g->in.mouse_y;
        g->world_x = g->press_wx;
        g->world_y = g->press_wy;
        g->camera.active = false;
    } else {
        drag(s);
    }
    bool released = g->in.released[0] || g->in.released[1] || g->in.released[2];
    if (released && !(g->in.down[0] || g->in.down[1] || g->in.down[2])) {
        if (g->gesture == STUDIO_GRAPH_PAN) {
            g->gesture = STUDIO_GRAPH_IDLE;
        } else {
            release(s, hit);
        }
        g->press_comment = 0;
    }
    (void)gr;
}

/* Canvas shortcuts act on the area under the pointer or last clicked (Blender). */
static void collapse_to_function(studio *s);
static void convert_to_expression(studio *s);

static void shortcuts(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    bool over = s->hover_area == g->area ||
                (s->hover_area < 0 && s->focus_area == g->area);
    g->space = over && SDL_GetKeyboardState(NULL)[SDL_SCANCODE_SPACE];
    if (!over || kit_wants_keyboard(s->kit) || kit_wants_keyboard(g->kit) ||
        kit_command_is_open(s->kit)) {
        return;
    }
    for (int i = 0; i < acount(s->keys); i++) {
        SDL_Keycode key = s->keys[i].key;
        bool mod = studio_primary(s, s->keys[i].mod);
        bool shift = s->keys[i].mod & SDL_KMOD_SHIFT;
        if (key == SDLK_ESCAPE && g->gesture != STUDIO_GRAPH_IDLE) {
            cancel_gesture(s);
        } else if (!mod && (key == SDLK_DELETE || key == SDLK_BACKSPACE || key == SDLK_X)) {
            delete_selection(s);
        } else if (!mod && !shift && key == SDLK_F) {
            frame_nodes(s, acount(e->selected) > 0, true);
        } else if (!mod && key == SDLK_HOME) {
            frame_nodes(s, false, true);
        } else if (mod && key == SDLK_A) {
            graph *gr = active_graph(s);
            ed_clear_selection(e);
            for (int n = 0; n < acount(gr->nodes); n++) {
                if (node_visible(gr, gr->nodes[n])) {
                    ed_add_selected(e, gr->nodes[n]);
                }
            }
        } else if ((mod && key == SDLK_D) || (shift && key == SDLK_D)) {
            if (!graph_readonly(s)) {
                ed_duplicate_selected(e);
            }
        } else if (mod && key == SDLK_C) {
            copy_selection(s);
        } else if (mod && key == SDLK_X) {
            copy_selection(s);
            delete_selection(s);
        } else if (mod && key == SDLK_V) {
            paste(s);
        } else if (mod && key == SDLK_G && !graph_readonly(s) && acount(e->selected)) {
            collapse_to_function(s);
        } else if ((shift && key == SDLK_A) || key == SDLK_TAB) {
            if (!graph_readonly(s)) {
                g->spawn_pending = true;
                g->spawn_x = g->world_x;
                g->spawn_y = g->world_y;
                kit_command_open(s->kit, "Add a node");
            }
        } else if (!mod && !shift && key == SDLK_C && !graph_readonly(s)) {
            frame_selection(s);
        } else if (!mod && shift && key == SDLK_L) {
            arrange(s, false);
        } else if (key == SDLK_F9 && !graph_readonly(s)) {
            /* Breakpoints on the selected nodes that run (Unreal). */
            bool snapshot = false;
            for (int n = 0; n < acount(e->selected); n++) {
                node *target = e->selected[n];
                if (node_behavior_for(target) == NODE_BEHAVIOR_PURE) {
                    continue;
                }
                if (!snapshot) {
                    ed_snapshot_before(e);
                    snapshot = true;
                }
                graph_set_breakpoint(e->active_graph, target, !target->breakpoint);
            }
        }
    }
}

/* ---------- Usage ----------
   Nodes no event reaches, and pure nodes whose values nothing used reads, draw
   dimmed: the author sees dead code before the compiler prunes it. */

static bool is_unused(const studio_graph *g, uint64_t id)
{
    for (int i = 0; i < acount(g->unused); i++) {
        if (g->unused[i] == id) {
            return true;
        }
    }
    return false;
}

static void find_unused(studio *s)
{
    studio_graph *g = &s->graph;
    graph *gr = active_graph(s);
    asetlen(g->unused, 0);
    int count = acount(gr->nodes);
    bool *used = calloc((size_t)(count ? count : 1), sizeof *used);
    bool roots = false;
    for (int i = 0; i < count; i++) {
        const node *n = gr->nodes[i];
        bool root = n->desc->flow_kind == NODE_FLOW_EVENT ||
                    n->id == gr->entry_id || n->id == gr->return_id || gr->dataflow;
        used[i] = root;
        roots |= root;
    }
    if (!roots) {
        free(used);
        return;
    }
    /* Exec reach forward, then data demand backward, to a fixed point. */
    for (bool changed = true; changed;) {
        changed = false;
        for (int e = 0; e < acount(gr->edges); e++) {
            const edge *ed = &gr->edges[e];
            int from = -1, to = -1;
            for (int i = 0; i < count && (from < 0 || to < 0); i++) {
                from = gr->nodes[i]->id == ed->from_id ? i : from;
                to = gr->nodes[i]->id == ed->to_id ? i : to;
            }
            if (from < 0 || to < 0) {
                continue;
            }
            if (ed->kind == GRAPH_EDGE_EXEC && used[from] && !used[to]) {
                used[to] = changed = true;
            }
            if (ed->kind == GRAPH_EDGE_DATA && used[to] && !used[from] &&
                node_behavior_for(gr->nodes[from]) == NODE_BEHAVIOR_PURE) {
                used[from] = changed = true;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        if (!used[i] && node_visible(gr, gr->nodes[i])) {
            apush(g->unused, gr->nodes[i]->id);
        }
    }
    free(used);
}

/* ---------- Refactoring ---------- */

/* Ctrl+G: the selection becomes a call to a new function whose interface is
   the wires crossing its edge (Blender, Unreal). */
static void collapse_to_function(studio *s)
{
    editor *e = graph_editor(s);
    for (int i = 1;; i++) {
        snprintf(e->extract_name, sizeof e->extract_name, i == 1 ? "New Function" : "New Function %d", i);
        if (!behavior_asset_find_graph_by_name(e->asset, e->extract_name)) {
            break;
        }
    }
    const char *error = ed_extract_selection(e);
    if (error) {
        kit_toast(
            s->kit,
            0,
            &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                               .title = "Can't collapse",
                               .description = error }
        );
    }
}

static void convert_to_expression(studio *s)
{
    editor *e = graph_editor(s);
    behavior_expression_error error = { 0 };
    node **selection = NULL;
    for (int i = 0; i < acount(e->selected); i++) {
        apush(selection, e->selected[i]);
    }
    if (!ed_convert_behavior_expression_selection(e, selection, acount(selection), &error)) {
        kit_toast(
            s->kit,
            0,
            &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                               .title = "Can't convert",
                               .description = error.message[0] ? error.message : NULL }
        );
    }
    afree(selection);
}

/* ---------- Menus ---------- */

static bool menu_item(kit *k, const char *key, const char *label, uint32_t icon, const char *shortcut, bool disabled)
{
    kit_menu_item_desc d = { .label = label,
                             .icon = icon,
                             .shortcut = shortcut,
                             .checked = -1,
                             .disabled = disabled };
    return kit_menu_item(k, key, &d);
}

void studio_graph_menus(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = s->kit;
    editor *e = graph_editor(s);
    if (g->menu_request) {
        kit_popup_open_at(k, "graph-menu", g->menu_x, g->menu_y);
        g->menu_request = false;
    }
    if (!kit_popup_begin(k, "graph-menu", "menu")) {
        return;
    }
    graph *gr = active_graph(s);
    node *n = node_by_id(s, g->menu_node);
    bool readonly = graph_readonly(s);
    if (n) {
        char title[256];
        kit_menu_group(k, "title", ed_node_title(e, n, title, sizeof title));
        if (ed_is_graph_call(n) &&
            menu_item(k, "open", "Open Graph", KIT_ICON_EXTERNAL, NULL, false)) {
            editor_open_graph_call(e, n->id);
        }
        if (menu_item(k, "dup", "Duplicate", KIT_ICON_COPY, studio_chord(s, "Shift+D"), readonly)) {
            ed_duplicate_selected(e);
        }
        if (menu_item(k, "delete", "Delete", KIT_ICON_TRASH, "X", readonly)) {
            delete_selection(s);
        }
        kit_menu_separator(k, "s1");
        const char *extract = behavior_asset_can_extract_function(
            e->asset,
            gr,
            e->selected,
            acount(e->selected)
        );
        if (menu_item(
                k,
                "collapse",
                "Collapse to Function",
                KIT_ICON_FUNCTION,
                studio_chord(s, "Mod+G"),
                readonly || extract || e->ui_context == EDITOR_UI_CONTEXT_SYSTEM
            )) {
            collapse_to_function(s);
        }
        const char *expression = behavior_expression_can_convert_selection(
            e->asset,
            gr,
            e->selected,
            acount(e->selected)
        );
        if (menu_item(
                k,
                "expression",
                "Convert to Expression",
                KIT_ICON_EXPRESSION,
                NULL,
                readonly || expression || e->ui_context == EDITOR_UI_CONTEXT_SYSTEM
            )) {
            convert_to_expression(s);
        }
        kit_menu_separator(k, "s0");
        if (menu_item(
                k,
                "mute",
                n->disabled ? "Enable" : "Disable",
                n->disabled ? KIT_ICON_EYE : KIT_ICON_EYE_OFF,
                NULL,
                readonly
            )) {
            ed_snapshot_before(e);
            graph_set_node_disabled(gr, n, !n->disabled);
        }
        if (node_behavior_for(n) != NODE_BEHAVIOR_PURE &&
            menu_item(k, "break", n->breakpoint ? "Clear Breakpoint" : "Set Breakpoint", KIT_ICON_CIRCLE, "F9", readonly)) {
            ed_snapshot_before(e);
            graph_set_breakpoint(gr, n, !n->breakpoint);
        }
    } else if (comment_by_id(s, g->menu_comment)) {
        if (menu_item(k, "delete", "Delete Comment", KIT_ICON_TRASH, NULL, readonly)) {
            ed_snapshot_before(e);
            graph_remove_comment_box(gr, g->menu_comment);
        }
    } else if (g->menu_edge >= 0 && g->menu_edge < acount(gr->edges)) {
        if (menu_item(k, "delete", "Delete Wire", KIT_ICON_TRASH, studio_chord(s, "Alt+Click"), readonly)) {
            ed_link_ref link;
            link_ref_of(&gr->edges[g->menu_edge], &link);
            ed_clear_selection(e);
            ed_add_selected_link(e, &link);
            ed_delete_selected(e);
        }
    }
    kit_popup_end(k);
}

/* ---------- Emission ---------- */

/* A pin's value editor, in the canvas or in Properties. */
static void emit_value_in(studio *s, kit *k, node *n, int output, int p)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    const pin_spec *spec = output ? &n->output_pins[p] : &n->input_pins[p];
    if (spec->container != PIN_CONTAINER_SINGLE) {
        return;
    }
    pin_value value = spec->default_value;
    bool changed = false, streaming = false;
    unsigned flags = graph_readonly(s) ? KIT_READONLY : 0;
    const editor_choices *choices = ed_pin_choices(e, n, output, p);
    int width = ed_vector_width(spec);
    double number = 0;
    float vector[4] = { 0 };
    char text[256];
    if (choices && choices->count > 0) {
        const char *labels[64];
        int count = choices->count < 64 ? choices->count : 64, index = 0;
        for (int i = 0; i < count; i++) {
            labels[i] = choices->items[i].label;
            if (choices->items[i].value ==
                (spec->type == PIN_ENUM ? value.e : value.i)) {
                index = i;
            }
        }
        if (kit_choice(k, "v", &index, labels, count, flags)) {
            if (spec->type == PIN_ENUM) {
                value.e = (int32_t)choices->items[index].value;
            } else {
                value.i = (int32_t)choices->items[index].value;
            }
            changed = true;
        }
    } else if (spec->type == PIN_BOOL) {
        bool b = value.b;
        if (kit_checkbox(k, "v", &b, NULL, flags)) {
            value.b = b;
            changed = true;
        }
    } else if (width > 0 && !(spec->flags & PIN_FLAG_COLOR)) {
        if (spec->default_value.ptr) {
            memcpy(vector, spec->default_value.ptr, (size_t)width * sizeof(float));
        }
        static const char *const axes[] = { "X", "Y", "Z", "W" };
        for (int a = 0; a < width; a++) {
            char key[8];
            snprintf(key, sizeof key, "v%d", a);
            double axis = vector[a];
            kit_number_desc desc = { .precision = 2,
                                     .step = 0.1,
                                     .addon = axes[a],
                                     .addon_cls = a == 0 ? "x"
                                                : a == 1 ? "y"
                                                         : NULL,
                                     .flags = flags };
            if (kit_number(k, key, &axis, &desc)) {
                vector[a] = (float)axis;
                changed = streaming = true;
            }
            if (kit_item_committed(k)) {
                g->value_editing = false;
            }
        }
        if (changed) {
            value.ptr = vector;
        }
    } else if (spec->type == PIN_STR || spec->type == PIN_NAME) {
        snprintf(text, sizeof text, "%s", value.str ? value.str : "");
        if (kit_text_field(k, "v", text, sizeof text, NULL, flags)) {
            value.str = text;
            changed = true;
        }
    } else if (spec->type >= PIN_I8 && spec->type <= PIN_F64) {
        bool integer = spec->type < PIN_F32;
        switch (spec->type) {
            case PIN_I8: number = value.i8; break;
            case PIN_U8: number = value.u8; break;
            case PIN_I16: number = value.i16; break;
            case PIN_U16: number = value.u16; break;
            case PIN_I32: number = value.i; break;
            case PIN_U32: number = value.u; break;
            case PIN_I64: number = (double)value.i64; break;
            case PIN_U64: number = (double)value.u64; break;
            case PIN_F32: number = value.f; break;
            default: number = value.d; break;
        }
        kit_number_desc desc = { .integer = integer,
                                 .precision = integer ? 0 : 2,
                                 .step = integer ? 1 : 0.1,
                                 .flags = flags };
        if (kit_number(k, "v", &number, &desc)) {
            switch (spec->type) {
                case PIN_I8: value.i8 = (int8_t)number; break;
                case PIN_U8: value.u8 = (uint8_t)number; break;
                case PIN_I16: value.i16 = (int16_t)number; break;
                case PIN_U16: value.u16 = (uint16_t)number; break;
                case PIN_I32: value.i = (int32_t)number; break;
                case PIN_U32: value.u = (uint32_t)number; break;
                case PIN_I64: value.i64 = (int64_t)number; break;
                case PIN_U64: value.u64 = (uint64_t)number; break;
                case PIN_F32: value.f = (float)number; break;
                default: value.d = number; break;
            }
            changed = streaming = true;
        }
        if (kit_item_committed(k)) {
            g->value_editing = false;
        }
    }
    if (!changed || graph_readonly(s)) {
        return;
    }
    /* A scrub is one undo step; discrete edits are one each. */
    if (!streaming || !g->value_editing) {
        ed_snapshot_before(e);
        g->value_editing = streaming;
    }
    graph_set_pin_default(gr, n, output, p, &value);
    touched(e);
}

static void emit_value(studio *s, node *n, int output, int p)
{
    emit_value_in(s, s->graph.kit, n, output, p);
}

/* Sockets are drawn as geometry: filled when wired, a ring when not, and ringed
   by the hot color while hovered or offered as a target. Execution pins are
   arrows and many-valued pins squares. */
static kit_dot_desc socket_dot(const studio *s, const pin_spec *spec, bool connected, bool hot, float alpha)
{
    const canvas_palette *p = palette(s);
    bool exec = spec->kind == PIN_EXEC;
    uint32_t color = pin_color(s, spec, 1);
    kit_dot_desc d = { exec ? KIT_DOT_ARROW
                       : spec->container != PIN_CONTAINER_SINGLE ? KIT_DOT_SQUARE
                                                                 : KIT_DOT_CIRCLE,
                       connected ? color : p->node,
                       hot         ? p->hot
                       : connected ? p->edge
                       : exec      ? p->exec_idle
                                   : color,
                       hot         ? 2.0f
                       : connected ? (exec ? 0.0f : 1.0f)
                                   : 1.5f };
    uint32_t a = (uint32_t)lroundf(255 * alpha);
    d.fill = (d.fill & ~255u) | (((d.fill & 255u) * a) / 255);
    d.ring = (d.ring & ~255u) | (((d.ring & 255u) * a) / 255);
    return d;
}

/* One pin: a row, half an execution row, or a socket in the title. Input
   labels share one width per node so the editors line up; compact nodes and
   title pins go unlabeled. */
static void emit_pin(studio *s, node *n, int kind, int p, const char *cls, float label_width)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    node_layout l = *layout_of(s, n);
    bool input = kind == ED_PIN_INPUT;
    pin_parts parts;
    pin_parts_of(s, n, kind, p, &parts);
    char key[16], width[24];
    snprintf(key, sizeof key, input ? "i%d" : "o%d", p);
    bool hovered = g->hover_node == n->id && g->hover_kind == kind &&
                   g->hover_pin == p;
    node *link_from = g->gesture == STUDIO_GRAPH_LINK ? node_by_id(s, g->link_node)
                                                      : NULL;
    bool source = link_from == n && g->link_kind == kind && g->link_pin == p;
    bool fits = link_from &&
                pins_compatible(link_from, g->link_kind, g->link_pin, n, kind, p);
    /* While a wire is dragged, sockets it can't reach recede (imgui-node-editor). */
    float alpha = link_from && !source && !fits      ? 0.2f
                : is_unused(g, n->id) || n->disabled ? 0.45f
                                                     : 1;
    kit_push(k, key, NULL, cls);
    kit_flag(k, "data-hover", hovered);
    kit_push(k, "s", "span", "socket");
    kit_dot_desc dot = socket_dot(s, parts.spec, parts.connected || parts.inline_source, hovered, alpha);
    kit_dot(k, "d", NULL, &dot);
    kit_pop(k);
    if (!input && parts.value != VALUE_NONE) {
        emit_value(s, n, 1, p);
    }
    if (parts.show_label && l.form == FORM_FULL && !in_head(&l, kind, p)) {
        kit_label(k, "l", "gpin-label", parts.label);
        if (input && label_width > 0 && !pin_exec(n, kind, p)) {
            snprintf(width, sizeof width, "%.0fdp", label_width);
            kit_style(k, "width", width);
        }
    }
    if (input && parts.value == VALUE_CHIP) {
        kit_push(k, "c", NULL, parts.inline_source ? "chip source" : "chip");
        if (parts.chip_icon) {
            kit_icon(k, "i", NULL, parts.chip_icon);
        }
        kit_label(k, "t", "chip-text", parts.chip);
        kit_pop(k);
    } else if (input && parts.value != VALUE_NONE) {
        emit_value(s, n, 0, p);
    }
    kit_pop(k);
}

static void emit_dp(kit *k, const char *property, float value)
{
    char text[32];
    snprintf(text, sizeof text, "%.1fdp", value);
    kit_style(k, property, text);
}

static void emit_head(studio *s, node *n, const node_layout *l)
{
    kit *k = s->graph.kit;
    char title[256];
    kit_push(k, "h", NULL, "gnode-head");
    if (l->head_in >= 0) {
        emit_pin(s, n, ED_PIN_INPUT, l->head_in, "hpin in", 0);
    }
    ed_node_presentation look = ed_node_appearance(n->desc);
    uint32_t icon = node_icon(s, n);
    if (look.symbol) {
        kit_label(k, "sym", "symbol", look.symbol);
    } else if (icon) {
        kit_icon(k, "i", NULL, icon);
    }
    kit_label(k, "t", "title", node_title(s, n, l->form, title, sizeof title));
    if (l->head_out >= 0) {
        emit_pin(s, n, ED_PIN_OUTPUT, l->head_out, "hpin out", 0);
    }
    kit_pop(k);
}

/* An expression's source, highlighted, on the lines measure_node wrapped. */
static void emit_code(studio *s, node *n, const node_layout *l)
{
    kit *k = s->graph.kit;
    const char *code = behavior_expression_source(graph_editor(s)->asset, n);
    int starts[CODE_LINES], length = (int)strlen(code);
    int count = code_lines(s, code, code_room(l), starts);
    kit_push(k, "code", NULL, "gcode");
    for (int i = 0; i < count; i++) {
        int end = i + 1 < count ? starts[i + 1] - 1 : length;
        char key[8], line[512], markup[2048];
        snprintf(key, sizeof key, "l%d", i);
        snprintf(line, sizeof line, "%.*s", end - starts[i], code + starts[i]);
        highlight_markup(HIGHLIGHT_EXPRESSION, line, markup, sizeof markup);
        kit_markup(k, key, "gcode-line", markup);
    }
    kit_pop(k);
}

static void emit_rows(studio *s, node *n, const node_layout *l)
{
    kit *k = s->graph.kit;
    graph *gr = active_graph(s);
    char key[16];
    kit_push(k, "b", NULL, "gnode-body");
    if (l->code_lines) {
        emit_code(s, n, l);
    }
    for (int row = 0; row < exec_rows(gr, n, l); row++) {
        snprintf(key, sizeof key, "e%d", row);
        kit_push(k, key, NULL, "grow exec");
        for (int kind = ED_PIN_INPUT; kind <= ED_PIN_OUTPUT; kind++) {
            int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
            for (int p = 0; p < count; p++) {
                if (pin_in_rows(gr, n, l, kind, p) && pin_exec(n, kind, p) &&
                    pin_order(gr, n, l, kind, p) == row) {
                    emit_pin(s, n, kind, p, kind == ED_PIN_INPUT ? "ghalf in" : "ghalf out", 0);
                }
            }
        }
        kit_pop(k);
    }
    for (int p = 0; p < n->output_count; p++) {
        if (pin_in_rows(gr, n, l, ED_PIN_OUTPUT, p) && !pin_exec(n, ED_PIN_OUTPUT, p)) {
            emit_pin(s, n, ED_PIN_OUTPUT, p, "grow out", 0);
        }
    }
    for (int p = 0; p < n->input_count; p++) {
        if (pin_in_rows(gr, n, l, ED_PIN_INPUT, p) && !pin_exec(n, ED_PIN_INPUT, p)) {
            emit_pin(s, n, ED_PIN_INPUT, p, "grow in", l->label);
        }
    }
    kit_pop(k);
}

/* Inputs stack on the left, the output centers on the right, and the symbol
   takes the space between, so none of them overlap. */
static void emit_operator(studio *s, node *n, const node_layout *l)
{
    kit *k = s->graph.kit;
    graph *gr = active_graph(s);
    float body = l->h - 2 * EDGE;
    kit_push(k, "b", NULL, "gnode-body");
    for (int p = 0; p < n->input_count; p++) {
        if (ed_pin_visible(gr, n, ED_PIN_INPUT, p)) {
            emit_pin(s, n, ED_PIN_INPUT, p, "grow in", 0);
            emit_dp(k, "width", l->column);
        }
    }
    kit_label(k, "sym", "gsymbol", ed_node_appearance(n->desc).symbol);
    emit_dp(k, "left", l->column + PART_GAP);
    emit_dp(k, "right", SOCKET_PAD);
    emit_dp(k, "line-height", body);
    for (int p = 0; p < n->output_count; p++) {
        if (ed_pin_visible(gr, n, ED_PIN_OUTPUT, p)) {
            emit_pin(s, n, ED_PIN_OUTPUT, p, "gout out", 0);
            emit_dp(k, "top", (body - ROW) / 2);
        }
    }
    kit_pop(k);
}

static void emit_node(studio *s, node *n)
{
    static const char *const FORMS[] = { "full", "operator", "getter" };
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    editor *e = graph_editor(s);
    node_layout l = *layout_of(s, n);
    char key[32];
    snprintf(key, sizeof key, "n%llu", (unsigned long long)n->id);
    kit_push(k, key, NULL, "gnode");
    emit_dp(k, "left", n->x);
    emit_dp(k, "top", n->y);
    emit_dp(k, "width", l.w);
    kit_attr(k, "data-kind", node_kind(s, n));
    kit_attr(k, "data-form", FORMS[l.form]);
    kit_flag(k, "data-selected", ed_node_selected(e, n));
    kit_flag(k, "data-active", e->front_node == n && ed_node_selected(e, n) && acount(e->selected) > 1);
    kit_flag(k, "data-disabled", n->disabled != 0);
    kit_flag(k, "data-dim", is_unused(g, n->id));
    kit_flag(k, "data-breakpoint", n->breakpoint != 0);
    if (l.form == FORM_OPERATOR) {
        emit_operator(s, n, &l);
    } else {
        emit_head(s, n, &l);
    }
    if (l.form == FORM_FULL && (l.code_lines || row_count(active_graph(s), n, &l))) {
        emit_rows(s, n, &l);
    }
    kit_pop(k);
}

/* Wires follow the selection: attached wires brighten, others recede. */
static void emit_wires(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    kit_wire *wires = NULL;
    struct
    {
        char from[PATH], to[PATH];
    } *paths = NULL;
    float *via = NULL;
    int *via_first = NULL;
    asetlen(g->wire_edges, 0);
    update_routes(s);
    bool focus = acount(e->selected) > 0 || g->hover_node;
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        node *from = graph_node_by_id(gr, ed->from_id),
             *to = graph_node_by_id(gr, ed->to_id);
        int fo = 0, to_out = 0, fp = -1, tp = -1;
        if (!node_visible(gr, from) || !node_visible(gr, to) ||
            !graph_pin_by_id(from, ed->from_pin_id, &fo, &fp) || !fo ||
            !graph_pin_by_id(to, ed->to_pin_id, &to_out, &tp) || to_out) {
            continue;
        }
        apush(g->wire_edges, i);
        apush(paths, ((typeof(*paths)){ { 0 } }));
        socket_path(alast(paths).from, PATH, s, from, ED_PIN_OUTPUT, fp);
        socket_path(alast(paths).to, PATH, s, to, ED_PIN_INPUT, tp);
        const pin_spec *spec = &from->output_pins[fp];
        bool exec = spec->kind == PIN_EXEC;
        ed_link_ref ref;
        link_ref_of(ed, &ref);
        bool attached = ed_node_selected(e, from) || ed_node_selected(e, to) ||
                        g->hover_node == from->id || g->hover_node == to->id ||
                        g->hover_edge == i || ed_link_selected(e, &ref);
        float alpha = !focus || attached ? 1 : 0.35f;
        if (is_unused(g, from->id) || is_unused(g, to->id)) {
            alpha *= 0.5f;
        }
        kit_wire wire = { .color = pin_color(s, spec, alpha),
                          .width = (exec ? 2.5f : 1.5f) + (attached && focus ? 0.5f : 0),
                          .flags = ed->conversion ? KIT_WIRE_DASHED : 0 };
        /* Executed wires glow and carry markers while their heat lasts. */
        editor_edge_activity activity;
        editor_query_edge_activity(e, ed, &activity);
        if (activity.opacity > 0) {
            wire.color = pin_color(s, spec, 1);
            wire.width += 1.5f * activity.opacity;
            wire.flags |= KIT_WIRE_FLOW;
            wire.flow = (float)s->time;
        }
        if (ed_link_selected(e, &ref)) {
            wire.color = 0xffa53aff;
        }
        /* Authored reroutes win; otherwise the lanes arrange routes through. */
        int first = acount(via) / 2, count = 0;
        if (ed->reroute_count) {
            for (int r = 0; r < ed->reroute_count; r++) {
                apush(via, ed->reroutes[r].x - g->x);
                apush(via, ed->reroutes[r].y - g->y);
            }
            count = ed->reroute_count;
        } else if (g->edge_route && i < acount(g->edge_route) && g->edge_route[i] >= 0) {
            int link = g->edge_route[i];
            for (int p = g->routes.start[link]; p < g->routes.start[link + 1]; p++) {
                apush(via, g->routes.points[p].x - g->x);
                apush(via, g->routes.points[p].y - g->y);
                count++;
            }
        }
        wire.via_count = count;
        apush(via_first, first);
        apush(wires, wire);
    }
    /* Pointers into the grown arrays are taken once they stop growing. */
    for (int w = 0; w < acount(wires); w++) {
        wires[w].from = paths[w].from;
        wires[w].to = paths[w].to;
        wires[w].via = wires[w].via_count ? via + via_first[w] * 2 : NULL;
    }
    kit_wires_desc desc = { .wires = wires,
                            .count = acount(wires),
                            .style = s->prefs.wires,
                            .grid = GRID,
                            .grid_x = g->x,
                            .grid_y = g->y,
                            .grid_color = palette(s)->grid,
                            .grid_major_color = palette(s)->grid_major };
    kit_wires(k, "wires", NULL, &desc);
    afree(wires);
    afree(paths);
    afree(via);
    afree(via_first);
}

static void emit_front(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    char value[32];
    if (g->gesture == STUDIO_GRAPH_BOX) {
        float x0 = fminf(g->box[0], g->box[2]) - g->x,
              y0 = fminf(g->box[1], g->box[3]) - g->y;
        kit_leaf(k, "marquee", NULL, "marquee");
        snprintf(value, sizeof value, "%.1fdp", x0);
        kit_style(k, "left", value);
        snprintf(value, sizeof value, "%.1fdp", y0);
        kit_style(k, "top", value);
        snprintf(value, sizeof value, "%.1fdp", fabsf(g->box[2] - g->box[0]));
        kit_style(k, "width", value);
        snprintf(value, sizeof value, "%.1fdp", fabsf(g->box[3] - g->box[1]));
        kit_style(k, "height", value);
    }
    node *from = g->gesture == STUDIO_GRAPH_LINK ? node_by_id(s, g->link_node) : NULL;
    if (!from) {
        return;
    }
    char anchor[PATH], target[PATH];
    socket_path(anchor, sizeof anchor, s, from, g->link_kind, g->link_pin);
    const pin_spec *spec = g->link_kind == ED_PIN_OUTPUT
                             ? &from->output_pins[g->link_pin]
                             : &from->input_pins[g->link_pin];
    node *over = node_by_id(s, g->hover_node);
    bool snap = over && g->hover_kind >= 0 &&
                pins_compatible(from, g->link_kind, g->link_pin, over, g->hover_kind, g->hover_pin);
    if (snap) {
        socket_path(target, sizeof target, s, over, g->hover_kind, g->hover_pin);
    }
    bool invalid = over && g->hover_kind >= 0 && !snap;
    kit_wire wire = { .color = invalid ? 0xe5534bff : pin_color(s, spec, 1),
                      .width = spec->kind == PIN_EXEC ? 2.5f : 2,
                      .to_x = g->in.mouse_x,
                      .to_y = g->in.mouse_y,
                      .from_x = g->in.mouse_x,
                      .from_y = g->in.mouse_y };
    /* Wires always run output to input, whichever end is being dragged. */
    if (g->link_kind == ED_PIN_OUTPUT) {
        wire.from = anchor;
        wire.to = snap ? target : NULL;
    } else {
        wire.to = anchor;
        wire.from = snap ? target : NULL;
    }
    kit_wires(
        k,
        "link",
        NULL,
        &(kit_wires_desc){ .wires = &wire, .count = 1, .style = s->prefs.wires }
    );
}

/* A wire that converts shows what it converts at its middle, so implicit
   work stays visible without a node (Unreal's conversion nodes, folded). */
static void emit_conversions(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    graph *gr = active_graph(s);
    if (g->zoom < FAR_ZOOM) {
        return;
    }
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        node *from = graph_node_by_id(gr, ed->from_id),
             *to = graph_node_by_id(gr, ed->to_id);
        int fo = 0, to_out = 0, fp = -1, tp = -1;
        if (!ed->conversion || !node_visible(gr, from) || !node_visible(gr, to) ||
            !graph_pin_by_id(from, ed->from_pin_id, &fo, &fp) || !fo ||
            !graph_pin_by_id(to, ed->to_pin_id, &to_out, &tp) || to_out) {
            continue;
        }
        float ax, ay, bx, by;
        socket_at(s, from, ED_PIN_OUTPUT, fp, &ax, &ay);
        socket_at(s, to, ED_PIN_INPUT, tp, &bx, &by);
        float mx = (ax + bx) / 2, my = (ay + by) / 2;
        if (ed->reroute_count) {
            mx = ed->reroutes[ed->reroute_count / 2].x;
            my = ed->reroutes[ed->reroute_count / 2].y;
        }
        char key[32], text[96], value[32];
        const node_desc *c = ed->conversion;
        if (c->conversion_from && c->conversion_to) {
            snprintf(text, sizeof text, "%s \xe2\x86\x92 %s", c->conversion_from, c->conversion_to);
        } else {
            editor_node_label(c, text, sizeof text);
        }
        snprintf(key, sizeof key, "x%d", i);
        kit_label(k, key, "gconversion", text);
        snprintf(value, sizeof value, "%.1fdp", mx);
        kit_style(k, "left", value);
        snprintf(value, sizeof value, "%.1fdp", my);
        kit_style(k, "top", value);
    }
}

/* Comment frames sit behind nodes; their titles stay readable when zoomed
   out (Unreal). The frame's color tints its fill and edge. */
static void emit_comment(studio *s, const graph_comment_box *box)
{
    kit *k = s->graph.kit;
    char key[32], value[48];
    snprintf(key, sizeof key, "c%llu", (unsigned long long)box->id);
    kit_push(k, key, NULL, "gcomment");
    snprintf(value, sizeof value, "%.1fdp", box->x);
    kit_style(k, "left", value);
    snprintf(value, sizeof value, "%.1fdp", box->y);
    kit_style(k, "top", value);
    snprintf(value, sizeof value, "%.1fdp", box->width);
    kit_style(k, "width", value);
    snprintf(value, sizeof value, "%.1fdp", box->height);
    kit_style(k, "height", value);
    uint32_t c = box->color ? box->color : 0x8a8a96ffu;
    snprintf(value, sizeof value, "#%02x%02x%02x1f", c >> 24, (c >> 16) & 255, (c >> 8) & 255);
    kit_style(k, "background-color", value);
    snprintf(value, sizeof value, "#%02x%02x%02x66", c >> 24, (c >> 16) & 255, (c >> 8) & 255);
    kit_style(k, "border-color", value);
    kit_label(k, "t", "gcomment-title", box->title && box->title[0] ? box->title : "Comment");
    kit_pop(k);
}

static void emit_canvas(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    graph *gr = active_graph(s);
    kit_root(k, "back");
    emit_wires(s);
    kit_root(k, "world");
    kit_push(k, "stage", NULL, "stage");
    char value[32];
    snprintf(value, sizeof value, "%.2fdp", -g->x);
    kit_style(k, "left", value);
    snprintf(value, sizeof value, "%.2fdp", -g->y);
    kit_style(k, "top", value);
    for (int i = 0; i < acount(gr->comment_boxes); i++) {
        emit_comment(s, &gr->comment_boxes[i]);
    }
    emit_conversions(s);
    for (int i = 0; i < acount(gr->nodes); i++) {
        if (node_visible(gr, gr->nodes[i])) {
            emit_node(s, gr->nodes[i]);
        }
    }
    kit_pop(k);
    kit_root(k, "front");
    emit_front(s);
}

/* ---------- Host ---------- */

static bool ensure_canvas(studio *s)
{
    studio_graph *g = &s->graph;
    if (g->kit) {
        return true;
    }
    float scale = cf_app_get_pixel_scale();
    g->backend = editor_rml_backend_create(
        (int)fmaxf(g->surface.rect.w, 1),
        (int)fmaxf(g->surface.rect.h, 1),
        scale
    );
    if (!g->backend) {
        return false;
    }
    g->kit = kit_create(editor_rml_backend_context(g->backend), "graph.rml");
    g->in.mouse_x = g->in.mouse_y = -1e9f;
    if (g->zoom <= 0) {
        g->zoom = 1;
    }
    return g->kit != NULL;
}

void studio_graph_frame(studio *s, double delta_seconds)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    kit_rect r = g->surface.rect;
    if (g->area < 0 || r.w < 1 || r.h < 1 || !studio_graph_active(s) ||
        !ensure_canvas(s)) {
        asetlen(g->events, 0);
        g->captured = false;
        g->area = -1;
        return;
    }
    /* Opening arranges automatic nodes; an undo that swaps the asset in place
       must not, or it would add history of its own. */
    if (!follow_graph(s)) {
        e->arrange_pending = false;
    }
    /* Node settings change node sizes, so automatic nodes arrange again. */
    int forms = s->prefs.short_bindings | s->prefs.title_flow << 1 |
                s->prefs.compact_nodes << 2;
    e->arrange_pending |= forms != g->forms;
    g->forms = forms;
    float scale = cf_app_get_pixel_scale();
    editor_rml_backend_resize_pixels(
        g->backend,
        (int)lroundf(r.w * scale),
        (int)lroundf(r.h * scale),
        g->zoom * scale
    );
    asetlen(g->layouts, 0);
    replay(s);
    pointer(s);
    shortcuts(s);
    asetlen(g->layouts, 0); /* gestures may have edited the graph */
    camera_step(s);
    if (e->arrange_pending) {
        arrange(s, true);
        e->arrange_pending = false;
    }
    if (!g->framed) {
        frame_nodes(s, false, false);
        g->framed = true;
    }
    editor_rml_backend_resize_pixels(
        g->backend,
        (int)lroundf(r.w * scale),
        (int)lroundf(r.h * scale),
        g->zoom * scale
    );
    g->in.time = s->time;
    g->in.dp_ratio = g->zoom * scale;
    g->in.width = canvas_w(g);
    g->in.height = canvas_h(g);
    kit_set_body_class(g->kit, "far", g->zoom < FAR_ZOOM);
    kit_set_body_class(g->kit, "comfortable", s->prefs.comfortable);
    kit_set_body_class(g->kit, "light", s->light);
    kit_set_theme(g->kit, "reduced-motion", s->prefs.reduced_motion);
    find_unused(s);
    kit_begin(g->kit, &g->in);
    emit_canvas(s);
    kit_end(g->kit);
    editor_rml_backend_update(g->backend, delta_seconds);
    editor_rml_backend_render(g->backend);
    editor_rml_backend_set_texture(s->backend, "graph", editor_rml_backend_texture(g->backend));
}

void studio_graph_free(studio *s)
{
    studio_graph *g = &s->graph;
    kit_destroy(g->kit);
    editor_rml_backend_destroy(g->backend);
    arrange_routes_free(&g->routes);
    afree(g->edge_route);
    afree(g->wire_edges);
    afree(g->events);
    afree(g->views);
    afree(g->drag);
    afree(g->box_base);
    afree(g->layouts);
    afree(g->unused);
    *g = (studio_graph){ 0 };
}

bool studio_graph_wants_keyboard(const studio *s)
{
    return s->graph.kit && kit_wants_keyboard(s->graph.kit);
}

bool studio_graph_accepts_text(const studio *s)
{
    return s->graph.kit && kit_accepts_text(s->graph.kit);
}

const char *studio_graph_cursor(const studio *s)
{
    const studio_graph *g = &s->graph;
    if (!g->kit || !(g->surface.hovered || g->captured)) {
        return NULL;
    }
    if (g->gesture == STUDIO_GRAPH_PAN || g->gesture == STUDIO_GRAPH_MOVE) {
        return "move";
    }
    if (g->gesture == STUDIO_GRAPH_LINK || g->hover_pin >= 0) {
        return "crosshair";
    }
    return editor_rml_backend_cursor(g->backend);
}

bool studio_graph_can_replay(studio *s, bool redo)
{
    const editor *e = graph_editor(s);
    return studio_graph_active(s) &&
           (redo ? editor_can_redo(e) : editor_can_undo(e));
}

void studio_graph_history(studio *s, bool redo)
{
    editor *e = graph_editor(s);
    s->graph.gesture = STUDIO_GRAPH_IDLE;
    if (redo) {
        editor_redo(e);
    } else {
        editor_undo(e);
    }
}

/* ---------- Area ---------- */

/* Adds a graph under the first free name and opens it, as one undo step. */
static void create_graph(editor *e, graph_kind kind, const char *base)
{
    char name[128];
    snprintf(name, sizeof name, "%s", base);
    for (int i = 2; behavior_asset_find_graph_by_name(e->asset, name); i++) {
        snprintf(name, sizeof name, "%s %d", base, i);
    }
    editor_command create = { .kind = EDITOR_COMMAND_CREATE_GRAPH,
                              .data.create_graph = { kind, name } };
    if (editor_command_apply(e, create) == 0) {
        graph *made = behavior_asset_find_graph_by_name(e->asset, name);
        if (made) {
            editor_open_graph(e, made->graph_id);
        }
    }
}

void studio_behavior_header(studio *s, int area)
{
    kit *k = s->kit;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    char label[256];
    if (!gr) {
        kit_label(k, "title", "dim", "No behavior open");
        return;
    }
    if (editor_graph_history_count(e) > 0) {
        kit_button_desc back = { .icon = KIT_ICON_LEFT, .tip = "Back to the caller" };
        if (kit_button(k, "back", &back)) {
            editor_graph_back(e);
        }
    }
    char key[24];
    snprintf(key, sizeof key, "graphs:%d", area);
    snprintf(
        label,
        sizeof label,
        "%s \xe2\x80\xba %s",
        s->scene->graph_document_id,
        gr->name && gr->name[0] ? gr->name : "Event Graph"
    );
    kit_button_desc pick = { .label = label,
                             .icon = KIT_ICON_GRAPH,
                             .cls = "graph-pick",
                             .tip = "Graphs in this behavior" };
    if (kit_button(k, "pick", &pick)) {
        kit_popup_open(k, key);
    }
    if (kit_popup_begin(k, key, "menu")) {
        kit_menu_group(k, "group", "Graphs");
        for (int i = 0; i < acount(e->asset->graphs); i++) {
            graph *item = e->asset->graphs[i];
            /* An expression's generated function belongs to its node, which
               opens it; listing them would repeat every expression here. */
            if (item->math_expression && item != gr) {
                continue;
            }
            char item_key[24];
            snprintf(item_key, sizeof item_key, "g%d", i);
            kit_menu_item_desc d = {
                .label = item->name && item->name[0] ? item->name : "Event Graph",
                .icon = item->kind == GRAPH_FUNCTION ? KIT_ICON_FUNCTION
                      : item->kind == GRAPH_MACRO    ? KIT_ICON_EXPRESSION
                                                     : KIT_ICON_EVENT,
                .checked = -1,
                .current = item == gr
            };
            if (kit_menu_item(k, item_key, &d)) {
                editor_open_graph(e, item->graph_id);
            }
        }
        kit_menu_separator(k, "s0");
        bool locked = !behavior_asset_mutable(e->asset);
        if (menu_item(k, "function", "New Function", KIT_ICON_ADD, NULL, locked)) {
            create_graph(e, GRAPH_FUNCTION, "New Function");
        }
        if (menu_item(k, "macro", "New Macro", KIT_ICON_ADD, NULL, locked)) {
            create_graph(e, GRAPH_MACRO, "New Macro");
        }
        kit_popup_end(k);
    }
    kit_leaf(k, "gap", NULL, "spacer");
    /* Which runtime instance the wires show (Unreal's debug object). */
    app_scene_authoring *scene = s->scene;
    if (scene->debug_status == APP_DEBUG_AVAILABLE) {
        char debug[160];
        snprintf(
            debug,
            sizeof debug,
            "%s",
            runtime_entity_label(scene, scene->debug.entity)
        );
        kit_label(k, "debug", "badge accent debug-target", debug);
        kit_tip(k, "Wires show this instance running", NULL);
    } else if (scene->debug_status == APP_DEBUG_STALE) {
        kit_label(k, "debug", "badge debug-target", "Edited");
        kit_tip(k, "The running behavior differs from this graph; save to update it", NULL);
    }
    if (s->graph.kit && s->graph.surface.rect.w > 0) {
        kit_number_flow(
            k,
            "zoom",
            "readout",
            roundf(s->graph.zoom * 100),
            &(kit_number_flow_desc){ .suffix = "%" }
        );
    }
    kit_button_desc tidy = { .icon = KIT_ICON_LAYOUT,
                             .tip = "Arrange",
                             .shortcut = studio_chord(s, "Shift+L"),
                             .flags = graph_readonly(s) ? KIT_DISABLED : 0 };
    if (kit_button(k, "arrange", &tidy)) {
        arrange(s, false);
        frame_nodes(s, false, true);
    }
    kit_button_desc fit = { .icon = KIT_ICON_FRAME, .tip = "Frame all", .shortcut = "Home" };
    if (kit_button(k, "frame", &fit)) {
        frame_nodes(s, false, true);
    }
}

void studio_behavior_body(studio *s, int area)
{
    kit *k = s->kit;
    studio_graph *g = &s->graph;
    if (!studio_graph_active(s)) {
        kit_push(k, "empty", NULL, "pending-editor");
        kit_icon(k, "icon", NULL, KIT_ICON_GRAPH);
        kit_label(k, "name", NULL, "Open a behavior from Assets");
        kit_pop(k);
        g->area = -1;
        return;
    }
    g->area = area;
    kit_surface(k, "graph", "graph-surface", "graph", &g->surface);
    studio_graph_menus(s);
}

/* ---------- Add node ---------- */

/* How well a search result can take the dragged wire: 3 an exact socket type,
   2 a socket reached by conversion, 1 only a wildcard, 0 nothing typed. */
static int link_fit(const editor *e, const ed_search_match *match)
{
    const node_desc *desc = match->desc;
    const ed_pin_view *link = &e->spawn_link_pin;
    if (!desc || match->function || match->variable || !link->n) {
        return 1;
    }
    bool from_output = link->kind == ED_PIN_OUTPUT;
    const pin_spec *source = from_output ? &link->n->output_pins[link->index]
                                         : &link->n->input_pins[link->index];
    const pin_spec *pins = from_output ? desc->inputs : desc->outputs;
    int count = from_output ? desc->input_count : desc->output_count, best = 0;
    for (int i = 0; i < count; i++) {
        const pin_spec *pin = &pins[i];
        if (pin->kind != source->kind || (pin->flags & PIN_FLAG_SETTING)) {
            continue;
        }
        bool named = pin->type == PIN_STRUCT || pin->type == PIN_ENUM;
        bool exact = pin->type == source->type &&
                     (!named || (pin->type_name && source->type_name &&
                                 !strcmp(pin->type_name, source->type_name)));
        int fit = pin->flags & PIN_FLAG_WILDCARD ? 1 : exact ? 3 : 2;
        best = fit > best ? fit : best;
    }
    return best;
}

/* A node spawned from a wire lands with its wired socket at the drop point
   (Blender, Unreal). */
static void place_on_wire(studio *s, node *n, const ed_pin_view *from)
{
    graph *gr = active_graph(s);
    bool input = from->kind == ED_PIN_OUTPUT;
    int count = input ? n->input_count : n->output_count;
    for (int p = 0; p < count; p++) {
        bool wired = false;
        if (input) {
            node *src = NULL;
            int src_pin = -1;
            wired = graph_input_source(gr, n, p, &src, &src_pin) && src == from->n;
        } else {
            for (int i = 0; i < acount(gr->edges) && !wired; i++) {
                wired = gr->edges[i].from_id == n->id &&
                        gr->edges[i].from_pin_id == n->output_pins[p].id &&
                        gr->edges[i].to_id == from->n->id;
            }
        }
        if (!wired) {
            continue;
        }
        asetlen(s->graph.layouts, 0);
        n->y -= pin_y(s, n, input ? ED_PIN_INPUT : ED_PIN_OUTPUT, p);
        if (!input) {
            n->x -= layout_of(s, n)->w;
        }
        return;
    }
}

bool studio_graph_palette(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = graph_editor(s);
    if (!g->spawn_pending) {
        return false;
    }
    if (!kit_command_is_open(s->kit) || !studio_graph_active(s)) {
        g->spawn_pending = false;
        if (e && e->spawn_link_pending) {
            ed_cancel_snapshot(e);
            e->spawn_link_pending = 0;
        }
        return false;
    }
    ed_search_match *found = ed_search_matches(e, kit_command_query(s->kit));
    /* For a dragged wire, nodes whose socket matches its type exactly come
       first, then conversions, then wildcards; each band keeps search order. */
    ed_search_match *matches = NULL;
    for (int band = 3; band >= 0; band--) {
        for (int i = 0; i < acount(found); i++) {
            if ((e->spawn_link_pending ? link_fit(e, &found[i]) : 3) == band) {
                apush(matches, found[i]);
            }
        }
    }
    afree(found);
    int count = acount(matches) < 200 ? acount(matches) : 200;
    kit_command_item *items = NULL;
    for (int i = 0; i < count; i++) {
        apush(
            items,
            ((kit_command_item){ matches[i].name,
                                 matches[i].category,
                                 NULL,
                                 NULL,
                                 NULL,
                                 matches[i].function ? KIT_ICON_FUNCTION
                                 : matches[i].host_event || matches[i].event ? KIT_ICON_EVENT
                                 : matches[i].variable ? KIT_ICON_FIELD
                                                       : 0 })
        );
    }
    int chosen = kit_command_ranked(s->kit, items, acount(items));
    if (chosen >= 0 && chosen < count) {
        bool linked = e->spawn_link_pending;
        ed_pin_view from = e->spawn_link_pin;
        node *n = ed_spawn_match_at(e, &matches[chosen], g->spawn_x, g->spawn_y);
        if (n) {
            select_only(e, n);
            n->placed = true;
            if (linked) {
                place_on_wire(s, n, &from);
            }
        }
        g->spawn_pending = false;
    }
    afree(items);
    afree(matches);
    return true;
}

/* ---------- Properties ---------- */

uint64_t studio_graph_node(const studio *s)
{
    const editor *e = graph_editor(s);
    return studio_graph_active(s) && acount(e->selected) > 0 ? e->selected[0]->id : 0;
}

/* The Node tab: every pin, advanced ones included, with what feeds it; the
   canvas shows the common case, this shows everything (Unreal's Details). */
void studio_node_properties(studio *s)
{
    kit *k = s->kit;
    editor *e = graph_editor(s);
    graph *gr = active_graph(s);
    node *n = node_by_id(s, studio_graph_node(s));
    if (!n) {
        kit_label(k, "empty", "prop-empty", "No node selected");
        return;
    }
    char title[256], label[160], text[256];
    bool readonly = graph_readonly(s);
    kit_push(k, "identity", NULL, "identity");
    kit_push(k, "glyph", NULL, "glyph");
    kit_icon(k, "icon", NULL, node_icon(s, n) ? node_icon(s, n) : KIT_ICON_HEXAGON);
    kit_pop(k);
    kit_push(k, "who", NULL, "who");
    kit_label(k, "name", "title", ed_node_title(e, n, title, sizeof title));
    kit_push(k, "lineage", NULL, "lineage");
    kit_label(k, "of", NULL, n->desc->category ? n->desc->category : n->desc->name);
    kit_pop(k);
    kit_pop(k);
    kit_pop(k);
    const char *source = n->desc == &NODE_BehaviorExpression
                           ? behavior_expression_source(e->asset, n)
                           : NULL;
    if (source) {
        /* The expression is the node: edit it here, see errors in place. */
        if (kit_panel_begin(k, "expression", "Expression", NULL, true)) {
            studio_graph *g = &s->graph;
            if (g->expression_node != n->id) {
                snprintf(g->expression, sizeof g->expression, "%s", source);
                g->expression_node = n->id;
                g->expression_error[0] = '\0';
            }
            if (kit_text_field(k, "source", g->expression, sizeof g->expression, "a + b", readonly ? KIT_READONLY : 0)) {
                behavior_expression_error error = { 0 };
                if (editor_set_behavior_expression(e, n, g->expression, &error) != 0) {
                    snprintf(
                        g->expression_error,
                        sizeof g->expression_error,
                        "%s",
                        error.message[0] ? error.message : "Invalid expression"
                    );
                } else {
                    g->expression_error[0] = '\0';
                }
            }
            if (g->expression_error[0]) {
                kit_label(k, "error", "notice", g->expression_error);
            }
        }
        kit_panel_end(k);
    }
    if (n->input_count && kit_panel_begin(k, "inputs", "Inputs", NULL, true)) {
        for (int p = 0; p < n->input_count; p++) {
            const pin_spec *spec = &n->input_pins[p];
            if (spec->flags & PIN_FLAG_SETTING && spec->kind != PIN_DATA) {
                continue;
            }
            char key[16];
            snprintf(key, sizeof key, "i%d", p);
            ed_pin_label(n, spec, label, sizeof label);
            kit_prop_begin(k, key, label);
            node *src = NULL;
            int src_pin = -1;
            if (spec->kind == PIN_EXEC) {
                kit_label(k, "v", "dim", "Execution");
            } else if (graph_input_source(gr, n, p, &src, &src_pin)) {
                char source[160];
                ed_pin_label(src, &src->output_pins[src_pin], source, sizeof source);
                snprintf(
                    text,
                    sizeof text,
                    "%s \xe2\x80\xba %s",
                    ed_node_title(e, src, title, sizeof title),
                    source
                );
                kit_label(k, "v", "dim", text);
            } else if (spec->binding) {
                kit_label(k, "v", "chip", ed_binding_label(e, spec->binding, text, sizeof text));
            } else if (!readonly && value_kind_of(s, n, 0, p) != VALUE_NONE) {
                emit_value_in(s, k, n, 0, p);
            } else {
                kit_label(k, "v", "dim", ed_pin_type_name(spec));
            }
            kit_prop_end(k);
        }
    }
    if (n->input_count) {
        kit_panel_end(k);
    }
    if (n->output_count && kit_panel_begin(k, "outputs", "Outputs", NULL, true)) {
        for (int p = 0; p < n->output_count; p++) {
            const pin_spec *spec = &n->output_pins[p];
            char key[16];
            snprintf(key, sizeof key, "o%d", p);
            ed_pin_label(n, spec, label, sizeof label);
            kit_prop_begin(k, key, label);
            if (ed_pin_editable(gr, n, ED_PIN_OUTPUT, p) && !readonly &&
                value_kind_of(s, n, 1, p) != VALUE_NONE) {
                emit_value_in(s, k, n, 1, p);
            } else {
                kit_label(k, "v", "dim", spec->kind == PIN_EXEC ? "Execution" : ed_pin_type_name(spec));
            }
            kit_prop_end(k);
        }
    }
    if (n->output_count) {
        kit_panel_end(k);
    }
    if (kit_panel_begin(k, "node", "Node", NULL, true)) {
        unsigned flags = readonly ? KIT_DISABLED : 0;
        bool disabled = n->disabled != 0;
        kit_prop_begin(k, "disabled", "Disabled");
        if (kit_checkbox(k, "v", &disabled, NULL, flags)) {
            ed_snapshot_before(e);
            graph_set_node_disabled(gr, n, disabled);
        }
        kit_prop_end(k);
        if (node_behavior_for(n) != NODE_BEHAVIOR_PURE) {
            bool breakpoint = n->breakpoint != 0;
            kit_prop_begin(k, "break", "Breakpoint");
            if (kit_checkbox(k, "v", &breakpoint, NULL, flags)) {
                ed_snapshot_before(e);
                graph_set_breakpoint(gr, n, breakpoint);
            }
            kit_prop_end(k);
        }
        if (ed_node_has_advanced_inputs(n)) {
            bool advanced = n->show_advanced;
            kit_prop_begin(k, "advanced", "Advanced Inputs");
            if (kit_checkbox(k, "v", &advanced, "Show on node", flags)) {
                ed_snapshot_before(e);
                n->show_advanced = advanced;
            }
            kit_prop_end(k);
        }
    }
    kit_panel_end(k);
}
