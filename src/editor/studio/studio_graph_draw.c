/* Graph canvas rendering: nodes, pins, wires and comments as kit elements. */
#include "editor/studio/studio_graph_internal.h"
#include "editor/ui/highlight.h"
#include "editor/ui/labels.h"
#include "graph/nodes/behavior_expression.h"
#include "support/array.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum
{
    PATH = 96,
};

static const float GRID = 32;

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

/* ---------- Emission ---------- */

/* Data rows are keyed i<pin> and o<pin> in the body; an execution row e<n>
   holds the pair of pins that share it, and title pins sit in the head. */
static void socket_path(char *out, size_t size, const studio *s, node *n, int kind, int pin)
{
    const node_layout *l = sg_layout_of(s, n);
    char side = kind == ED_PIN_INPUT ? 'i' : 'o';
    unsigned long long id = n->id;
    if (sg_in_head(l, kind, pin)) {
        snprintf(out, size, "world/stage/n%llu/h/%c%d/s", id, side, pin);
        return;
    }
    if (sg_pin_exec(n, kind, pin)) {
        int row = sg_pin_order(sg_active_graph(s), n, l, kind, pin);
        snprintf(out, size, "world/stage/n%llu/b/e%d/%c%d/s", id, row, side, pin);
        return;
    }
    snprintf(out, size, "world/stage/n%llu/b/%c%d/s", id, side, pin);
}

/* Streaming edits must reach the preview on the next tick. */
static void touched(editor *e)
{
    e->edit_pending = 1;
}

/* A pin's value editor, in the canvas or in Properties. */
void sg_emit_value_in(studio *s, kit *k, node *n, int output, int p)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    const pin_spec *spec = output ? &n->output_pins[p] : &n->input_pins[p];
    if (spec->container != PIN_CONTAINER_SINGLE) {
        return;
    }
    pin_value value = spec->default_value;
    bool changed = false, streaming = false;
    unsigned flags = sg_graph_readonly(s) ? KIT_READONLY : 0;
    const editor_choices *choices = ed_pin_choices(e, n, output, p);
    int width = ed_vector_width(spec);
    double number = 0;
    float vector[4] = { 0 };
    char text[256];
    int count = choices ? editor_choices_count(choices) : 0;
    if (count > 0) {
        /* Name catalogs, such as a component setting, match by name. */
        const char **labels = NULL;
        int index = -1;
        for (int i = 0; i < count; i++) {
            editor_choice choice = editor_choice_at(choices, i);
            apush(labels, choice.label);
            bool selected = spec->type == PIN_NAME
                              ? value.str && choice.name &&
                                    !strcmp(value.str, choice.name)
                              : choice.value ==
                                    (spec->type == PIN_ENUM ? value.e : value.i);
            index = selected ? i : index;
        }
        if (kit_choice(k, "v", &index, labels, count, flags)) {
            editor_choice choice = editor_choice_at(choices, index);
            if (spec->type == PIN_NAME) {
                value.str = choice.name;
            } else if (spec->type == PIN_ENUM) {
                value.e = choice.value;
            } else {
                value.i = choice.value;
            }
            changed = true;
        }
        afree(labels);
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
    if (!changed || sg_graph_readonly(s)) {
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
    sg_emit_value_in(s, s->graph.kit, n, output, p);
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
    node_layout l = *sg_layout_of(s, n);
    bool input = kind == ED_PIN_INPUT;
    pin_parts parts;
    sg_pin_parts_of(s, n, kind, p, &parts);
    char key[16], width[24];
    snprintf(key, sizeof key, input ? "i%d" : "o%d", p);
    bool hovered = g->hover_node == n->id && g->hover_kind == kind &&
                   g->hover_pin == p;
    node *link_from = g->gesture == STUDIO_GRAPH_LINK ? sg_node_by_id(s, g->link_node)
                                                      : NULL;
    bool source = link_from == n && g->link_kind == kind && g->link_pin == p;
    bool fits = link_from &&
                sg_pins_compatible(link_from, g->link_kind, g->link_pin, n, kind, p);
    /* While a wire is dragged, sockets it can't reach recede (imgui-node-editor). */
    float alpha = link_from && !source && !fits         ? 0.2f
                : sg_is_unused(g, n->id) || n->disabled ? 0.45f
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
    if (parts.show_label && l.form == FORM_FULL && !sg_in_head(&l, kind, p)) {
        kit_label(k, "l", "gpin-label", parts.label);
        if (input && label_width > 0 && !sg_pin_exec(n, kind, p)) {
            snprintf(width, sizeof width, "%.0fdp", label_width);
            kit_style(k, "width", width);
        }
    }
    /* Watched pins, and the pin under the pointer, show their last value
       beside the socket unless an editor already shows it (Unreal). */
    editor *e = sg_graph_editor(s);
    bool edited = parts.value != VALUE_NONE && parts.value != VALUE_CHIP;
    /* Previews can't read bindings, so a bound input shows only a run's value. */
    bool bound = input && parts.value == VALUE_CHIP && !parts.inline_source;
    if (parts.spec->kind == PIN_DATA && !edited && (!bound || ed_display_run(e)) &&
        ((parts.spec->flags & PIN_FLAG_WATCH) || hovered)) {
        const char *value = input ? eval_view_input(e->ev, sg_active_graph(s), n, p)
                                  : eval_view_output(e->ev, n, p);
        if (value && value[0]) {
            kit_label(k, "w", "gwatch", value);
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
    uint32_t icon = sg_node_icon(s, n);
    if (look.symbol) {
        kit_label(k, "sym", "symbol", look.symbol);
    } else if (icon) {
        kit_icon(k, "i", NULL, icon);
    }
    kit_label(k, "t", "title", sg_node_title(s, n, l->form, title, sizeof title));
    if (l->head_out >= 0) {
        emit_pin(s, n, ED_PIN_OUTPUT, l->head_out, "hpin out", 0);
    }
    kit_pop(k);
}

/* An expression's source, highlighted, on the lines measure_node wrapped. */
static void emit_code(studio *s, node *n, const node_layout *l)
{
    kit *k = s->graph.kit;
    const char *code = behavior_expression_source(sg_graph_editor(s)->asset, n);
    int starts[CODE_LINES], length = (int)strlen(code);
    int count = sg_code_lines(s, code, sg_code_room(l), starts);
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
    graph *gr = sg_active_graph(s);
    char key[16];
    kit_push(k, "b", NULL, "gnode-body");
    if (l->code_lines) {
        emit_code(s, n, l);
    }
    for (int row = 0; row < sg_exec_rows(gr, n, l); row++) {
        snprintf(key, sizeof key, "e%d", row);
        kit_push(k, key, NULL, "grow exec");
        for (int kind = ED_PIN_INPUT; kind <= ED_PIN_OUTPUT; kind++) {
            int count = kind == ED_PIN_INPUT ? n->input_count : n->output_count;
            for (int p = 0; p < count; p++) {
                if (sg_pin_in_rows(gr, n, l, kind, p) && sg_pin_exec(n, kind, p) &&
                    sg_pin_order(gr, n, l, kind, p) == row) {
                    emit_pin(s, n, kind, p, kind == ED_PIN_INPUT ? "ghalf in" : "ghalf out", 0);
                }
            }
        }
        kit_pop(k);
    }
    for (int p = 0; p < n->output_count; p++) {
        if (sg_pin_in_rows(gr, n, l, ED_PIN_OUTPUT, p) &&
            !sg_pin_exec(n, ED_PIN_OUTPUT, p)) {
            emit_pin(s, n, ED_PIN_OUTPUT, p, "grow out", 0);
        }
    }
    for (int p = 0; p < n->input_count; p++) {
        if (sg_pin_in_rows(gr, n, l, ED_PIN_INPUT, p) &&
            !sg_pin_exec(n, ED_PIN_INPUT, p)) {
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
    graph *gr = sg_active_graph(s);
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
    editor *e = sg_graph_editor(s);
    node_layout l = *sg_layout_of(s, n);
    char key[32];
    snprintf(key, sizeof key, "n%llu", (unsigned long long)n->id);
    kit_push(k, key, NULL, "gnode");
    emit_dp(k, "left", n->x);
    emit_dp(k, "top", n->y);
    emit_dp(k, "width", l.w);
    kit_attr(k, "data-kind", sg_node_kind(s, n));
    kit_attr(k, "data-form", FORMS[l.form]);
    kit_flag(k, "data-selected", ed_node_selected(e, n));
    kit_flag(k, "data-active", e->front_node == n && ed_node_selected(e, n) && acount(e->selected) > 1);
    kit_flag(k, "data-disabled", n->disabled != 0);
    kit_flag(k, "data-dim", sg_is_unused(g, n->id));
    kit_flag(k, "data-breakpoint", n->breakpoint != 0);
    if (l.form == FORM_OPERATOR) {
        emit_operator(s, n, &l);
    } else {
        emit_head(s, n, &l);
    }
    if (l.form == FORM_FULL &&
        (l.code_lines || sg_row_count(sg_active_graph(s), n, &l))) {
        emit_rows(s, n, &l);
    }
    kit_pop(k);
}

/* Wires follow the selection: attached wires brighten, others recede. */
static void emit_wires(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    kit_wire *wires = NULL;
    struct
    {
        char from[PATH], to[PATH];
    } *paths = NULL;
    float *via = NULL;
    int *via_first = NULL;
    asetlen(g->wire_edges, 0);
    sg_update_routes(s);
    bool focus = acount(e->selected) > 0 || g->hover_node;
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        node *from = graph_node_by_id(gr, ed->from_id),
             *to = graph_node_by_id(gr, ed->to_id);
        int fo = 0, to_out = 0, fp = -1, tp = -1;
        if (!sg_node_visible(gr, from) || !sg_node_visible(gr, to) ||
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
        sg_link_ref_of(ed, &ref);
        bool attached = ed_node_selected(e, from) || ed_node_selected(e, to) ||
                        g->hover_node == from->id || g->hover_node == to->id ||
                        g->hover_edge == i || ed_link_selected(e, &ref);
        float alpha = !focus || attached ? 1 : 0.35f;
        if (sg_is_unused(g, from->id) || sg_is_unused(g, to->id)) {
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
        if (g->gesture == STUDIO_GRAPH_MOVE && g->insert_edge == i) {
            wire.color = palette(s)->hot;
            wire.width += 1;
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
    node *from = g->gesture == STUDIO_GRAPH_LINK ? sg_node_by_id(s, g->link_node)
                                                 : NULL;
    if (!from) {
        return;
    }
    char anchor[PATH], target[PATH];
    socket_path(anchor, sizeof anchor, s, from, g->link_kind, g->link_pin);
    const pin_spec *spec = g->link_kind == ED_PIN_OUTPUT
                             ? &from->output_pins[g->link_pin]
                             : &from->input_pins[g->link_pin];
    node *over = sg_node_by_id(s, g->hover_node);
    bool snap = over && g->hover_kind >= 0 &&
                sg_pins_compatible(from, g->link_kind, g->link_pin, over, g->hover_kind, g->hover_pin);
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
    graph *gr = sg_active_graph(s);
    if (g->zoom < FAR_ZOOM) {
        return;
    }
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        node *from = graph_node_by_id(gr, ed->from_id),
             *to = graph_node_by_id(gr, ed->to_id);
        int fo = 0, to_out = 0, fp = -1, tp = -1;
        if (!ed->conversion || !sg_node_visible(gr, from) || !sg_node_visible(gr, to) ||
            !graph_pin_by_id(from, ed->from_pin_id, &fo, &fp) || !fo ||
            !graph_pin_by_id(to, ed->to_pin_id, &to_out, &tp) || to_out) {
            continue;
        }
        float ax, ay, bx, by;
        sg_socket_at(s, from, ED_PIN_OUTPUT, fp, &ax, &ay);
        sg_socket_at(s, to, ED_PIN_INPUT, tp, &bx, &by);
        float mx = (ax + bx) / 2, my = (ay + by) / 2;
        if (ed->reroute_count) {
            mx = ed->reroutes[ed->reroute_count / 2].x;
            my = ed->reroutes[ed->reroute_count / 2].y;
        }
        char key[32], text[96];
        const node_desc *c = ed->conversion;
        if (c->conversion_from && c->conversion_to) {
            snprintf(text, sizeof text, "%s \xe2\x86\x92 %s", c->conversion_from, c->conversion_to);
        } else {
            editor_node_label(c, text, sizeof text);
        }
        snprintf(key, sizeof key, "x%d", i);
        kit_label(k, key, "gconversion", text);
        float width = ceilf(sg_text_width(s, text, 10, 400)) + 14;
        emit_dp(k, "left", mx);
        emit_dp(k, "top", my);
        emit_dp(k, "width", width);
        emit_dp(k, "margin-left", -width / 2);
    }
}

/* A wire's reroute points, drawn as dots in its color (Blender). */
static void emit_reroutes(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    graph *gr = sg_active_graph(s);
    for (int i = 0; i < acount(gr->edges); i++) {
        const edge *ed = &gr->edges[i];
        node *from, *to;
        int from_pin, to_pin;
        if (!ed->reroute_count ||
            !sg_edge_ends(gr, ed, &from, &from_pin, &to, &to_pin) ||
            !sg_node_visible(gr, from) || !sg_node_visible(gr, to)) {
            continue;
        }
        uint32_t color = pin_color(s, &from->output_pins[from_pin], 1);
        for (int r = 0; r < ed->reroute_count; r++) {
            char key[32];
            snprintf(key, sizeof key, "r%d_%d", i, r);
            bool hot = g->hover_reroute == i * GRAPH_EDGE_MAX_REROUTES + r ||
                       (g->gesture == STUDIO_GRAPH_REROUTE &&
                        g->reroute_edge == i && g->reroute_point == r);
            kit_push(k, key, NULL, "greroute");
            emit_dp(k, "left", ed->reroutes[r].x);
            emit_dp(k, "top", ed->reroutes[r].y);
            kit_dot_desc dot = { KIT_DOT_CIRCLE,
                                 color,
                                 hot ? palette(s)->hot : palette(s)->edge,
                                 hot ? 2.0f : 1.0f };
            kit_dot(k, "d", NULL, &dot);
            kit_pop(k);
        }
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

void sg_emit_canvas(studio *s)
{
    studio_graph *g = &s->graph;
    kit *k = g->kit;
    graph *gr = sg_active_graph(s);
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
    emit_reroutes(s);
    for (int i = 0; i < acount(gr->nodes); i++) {
        if (sg_node_visible(gr, gr->nodes[i])) {
            emit_node(s, gr->nodes[i]);
        }
    }
    kit_pop(k);
    kit_root(k, "front");
    emit_front(s);
}
