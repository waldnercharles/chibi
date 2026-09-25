/* Graph canvas input: replayed window events, gestures and shortcuts. */
#include "editor/studio/studio_graph_internal.h"
#include "support/array.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum
{
    CURVE_POINTS = 256,
};

static const float THRESHOLD = 4; /* window px before a press drags */
static const float WIRE_HIT = 6;  /* window px */

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
    graph *gr = sg_active_graph(s);
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

static void begin_link(studio *s, node *n, int kind, int pin)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
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
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    node *from = sg_node_by_id(s, g->link_node);
    node *to = sg_node_by_id(s, hit.node);
    g->gesture = STUDIO_GRAPH_IDLE;
    if (!from) {
        return;
    }
    if (to && hit.kind >= 0 && hit.pin >= 0) {
        if (sg_pins_compatible(from, g->link_kind, g->link_pin, to, hit.kind, hit.pin) ||
            hit.kind != g->link_kind) {
            sg_connect_pins(s, from, g->link_kind, g->link_pin, to, hit.kind, hit.pin);
        }
        return;
    }
    if (to && to != from) {
        int target = sg_body_target(gr, from, g->link_kind, g->link_pin, to);
        if (target >= 0) {
            sg_connect_pins(
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
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
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
        if (!sg_node_visible(gr, n) || !sg_node_box(s, n, b)) {
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
    sg_zoom_at(g, g->zoom * factor, ev->x, ev->y);
}

static void press(studio *s, int button, graph_hit hit, bool on_control)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
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
    graph_comment_box *box = sg_comment_by_id(s, hit.comment);
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
    node *n = sg_node_by_id(s, hit.node);
    if (n && g->double_click && ed_is_graph_call(n)) {
        /* Double-click enters a call (Blender's Tab); Back returns. */
        editor_open_graph_call(e, n->id);
        return;
    }
    if (n && hit.kind >= 0 && hit.pin >= 0 && !sg_graph_readonly(s)) {
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
        sg_link_ref_of(&gr->edges[g->hover_edge], &link);
        if ((mods & KIT_MOD_ALT) && !sg_graph_readonly(s)) {
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
    editor *e = sg_graph_editor(s);
    switch (g->gesture) {
        case STUDIO_GRAPH_PRESS:
            if (drag_distance(g) < THRESHOLD || sg_graph_readonly(s)) {
                break;
            }
            g->gesture = STUDIO_GRAPH_MOVE;
            asetlen(g->drag, 0);
            if (g->press_comment) {
                sg_comment_contents(s, g->press_comment, g->in.modifiers & KIT_MOD_SHIFT);
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
            graph_comment_box *box = sg_comment_by_id(s, g->press_comment);
            if (box) {
                box->x = g->comment_x + dx;
                box->y = g->comment_y + dy;
            }
            for (int i = 0; i < acount(g->drag); i++) {
                node *n = sg_node_by_id(s, g->drag[i].id);
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
    editor *e = sg_graph_editor(s);
    switch (g->gesture) {
        case STUDIO_GRAPH_PRESS: {
            /* A click on a selected node without a drag narrows the selection
               to it; with a modifier it toggles. */
            node *n = sg_node_by_id(s, g->press_node);
            if (n && g->press_toggle) {
                ed_remove_selected(e, n);
            } else if (n && !(g->in.modifiers & (KIT_MOD_SHIFT | KIT_MOD_CTRL | KIT_MOD_META))) {
                sg_select_only(e, n);
            }
            break;
        }
        case STUDIO_GRAPH_MOVE: {
            graph_comment_box *box = sg_comment_by_id(s, g->press_comment);
            bool moved = box && (box->x != g->comment_x || box->y != g->comment_y);
            for (int i = 0; i < acount(g->drag); i++) {
                node *n = sg_node_by_id(s, g->drag[i].id);
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
            node *n = sg_node_by_id(s, at.node);
            int wire = n || at.comment ? -1 : hit_wire(s);
            g->menu_comment = at.comment;
            if (n || wire >= 0 || at.comment) {
                if (n && !ed_node_selected(e, n)) {
                    sg_select_only(e, n);
                }
                g->menu_node = n ? n->id : 0;
                g->menu_edge = wire;
                g->menu_request = true;
                g->menu_x = g->press_x;
                g->menu_y = g->press_y;
            } else if (!sg_graph_readonly(s)) {
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

/* A cancelled move puts the frame and nodes back and records nothing. */
void sg_cancel_gesture(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
    if (g->gesture == STUDIO_GRAPH_MOVE) {
        graph_comment_box *box = sg_comment_by_id(s, g->press_comment);
        if (box) {
            box->x = g->comment_x;
            box->y = g->comment_y;
        }
        for (int i = 0; i < acount(g->drag); i++) {
            node *n = sg_node_by_id(s, g->drag[i].id);
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
void sg_replay(studio *s)
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
                sg_to_world(g, g->press_x, g->press_y, &g->press_wx, &g->press_wy);
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

static void snap_link(studio *s, graph_hit *hit)
{
    studio_graph *g = &s->graph;
    graph *gr = sg_active_graph(s);
    node *from = sg_node_by_id(s, g->link_node);
    int want = g->link_kind == ED_PIN_OUTPUT ? ED_PIN_INPUT : ED_PIN_OUTPUT;
    float best = 20 / g->zoom;
    for (int i = 0; from && i < acount(gr->nodes); i++) {
        node *n = gr->nodes[i];
        int count = want == ED_PIN_INPUT ? n->input_count : n->output_count;
        if (n == from || !sg_node_visible(gr, n)) {
            continue;
        }
        for (int p = 0; p < count; p++) {
            float x, y;
            if (!ed_pin_visible(gr, n, want, p) ||
                !sg_pins_compatible(from, g->link_kind, g->link_pin, n, want, p)) {
                continue;
            }
            sg_socket_at(s, n, want, p, &x, &y);
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

void sg_pointer(studio *s)
{
    studio_graph *g = &s->graph;
    graph *gr = sg_active_graph(s);
    sg_to_world(
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
    node *hovered = sg_node_by_id(s, hit.node);
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
void sg_shortcuts(studio *s)
{
    studio_graph *g = &s->graph;
    editor *e = sg_graph_editor(s);
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
            sg_cancel_gesture(s);
        } else if (!mod && (key == SDLK_DELETE || key == SDLK_BACKSPACE || key == SDLK_X)) {
            sg_delete_selection(s);
        } else if (!mod && !shift && key == SDLK_F) {
            sg_frame_nodes(s, acount(e->selected) > 0, true);
        } else if (!mod && key == SDLK_HOME) {
            sg_frame_nodes(s, false, true);
        } else if (mod && key == SDLK_A) {
            graph *gr = sg_active_graph(s);
            ed_clear_selection(e);
            for (int n = 0; n < acount(gr->nodes); n++) {
                if (sg_node_visible(gr, gr->nodes[n])) {
                    ed_add_selected(e, gr->nodes[n]);
                }
            }
        } else if ((mod && key == SDLK_D) || (shift && key == SDLK_D)) {
            if (!sg_graph_readonly(s)) {
                ed_duplicate_selected(e);
            }
        } else if (mod && key == SDLK_C) {
            sg_copy_selection(s);
        } else if (mod && key == SDLK_X) {
            sg_copy_selection(s);
            sg_delete_selection(s);
        } else if (mod && key == SDLK_V) {
            sg_paste(s);
        } else if (mod && key == SDLK_G && !sg_graph_readonly(s) && acount(e->selected)) {
            sg_collapse_to_function(s);
        } else if ((shift && key == SDLK_A) || key == SDLK_TAB) {
            if (!sg_graph_readonly(s)) {
                g->spawn_pending = true;
                g->spawn_x = g->world_x;
                g->spawn_y = g->world_y;
                kit_command_open(s->kit, "Add a node");
            }
        } else if (!mod && !shift && key == SDLK_C && !sg_graph_readonly(s)) {
            sg_frame_selection(s);
        } else if (!mod && shift && key == SDLK_L) {
            sg_arrange(s, false);
        } else if (key == SDLK_F9 && !sg_graph_readonly(s)) {
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
