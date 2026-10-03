/* Drag and drop after dnd-kit: a source is picked up once the pointer moves 5
 * dp with the button held, or is held 200 ms within 10 dp. An overlay follows
 * the pointer, every target that accepts the type is marked data-drop-accept,
 * the one under it data-drop-over, release drops there and Escape cancels.
 * Clicks during a drag are swallowed so a drop never also activates what it
 * lands on. */
#include "editor/rmlui/kit_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define ACTIVATION_DISTANCE 5.0f
#define ACTIVATION_DELAY 0.2
#define DELAY_TOLERANCE 10.0f
/* The overlay's distance from the pointer, in dp. */
#define OVERLAY_OFFSET 14.0f

static void drag_reset(drag_state *d)
{
    char *owned[] = { d->type, d->payload, d->label, d->source,
                      d->over, d->target,  d->hint };
    for (size_t i = 0; i < sizeof owned / sizeof *owned; i++) {
        free(owned[i]);
    }
    *d = (drag_state){ 0 };
}

void drop_types_clear(node *n)
{
    for (int i = 0; i < acount(n->drop_types); i++) {
        free(n->drop_types[i]);
    }
    afree(n->drop_types);
    n->drop_types = NULL;
}

static bool accepts(const node *n, const char *type)
{
    for (int i = 0; type && i < acount(n->drop_types); i++) {
        if (str_eq(n->drop_types[i], type)) {
            return true;
        }
    }
    return false;
}

/* Picks the pending source up once it moved or was held long enough. False
 * while it waits, and once released first, which ends it. */
static bool activate(kit *k, drag_state *d)
{
    if (!k->in.down[0]) {
        drag_reset(d);
        return false;
    }
    float moved = hypotf(k->in.mouse_x - d->x0, k->in.mouse_y - d->y0);
    bool held = k->in.time - d->t0 >= ACTIVATION_DELAY && moved <= DELAY_TOLERANCE;
    if (moved < ACTIVATION_DISTANCE && !held) {
        return false;
    }
    d->active = true;
    close_all_popups(k);
    return true;
}

void drag_begin_frame(kit *k)
{
    drag_state *d = &k->drag;
    if (!d->pending || (!d->active && !activate(k, d))) {
        return;
    }
    /* The nearest node under the pointer that accepts this type. */
    str_set(&d->over, "");
    str_set(&d->hint, "");
    for (node *n = node_for(k, rml_context_hover_element(k->context)); n; n = n->parent) {
        if (accepts(n, d->type)) {
            str_set(&d->over, n->path);
            break;
        }
    }
    if (k->in.down[0]) {
        return;
    }
    d->dropped = !str_eq(d->over, "");
    str_set(&d->target, d->over);
    d->active = false;
    d->pending = d->dropped;
}

void drag_end_frame(kit *k)
{
    drag_state *d = &k->drag;
    if (d->dropped) {
        drag_reset(d);
        return;
    }
    if (!d->active) {
        return;
    }
    node *n = emit_in(k, k->layer, "@drag", KIND_BOX, "div", "drag-overlay");
    presence(k, n);
    widget *w = &n->w;
    if (!w->label_el) {
        w->icon_el = raw_icon(n->el, "", 0);
        rml_element *text = raw(n->el, "span", "text", NULL);
        w->label_el = raw(text, "span", "label", NULL);
        w->meta_el = raw(text, "span", "hint", NULL);
    }
    if (w->icon != d->icon) {
        w->icon = d->icon;
        char text[5];
        utf8(text, d->icon);
        rml_element_set_inner_rml(w->icon_el, text);
    }
    set_element_text(w->label_el, d->label);
    set_element_text(w->meta_el, d->hint);
    set_flag(n, "data-over", !str_eq(d->over, ""));
    set_flag(n, "data-hint", !str_eq(d->hint, ""));
    /* Beside the pointer, or before it where the window ends. */
    kit_rect shown = element_rect(k, n->el);
    float x = k->in.mouse_x + OVERLAY_OFFSET;
    if (x + shown.w > k->in.width) {
        x = fmaxf(k->in.mouse_x - OVERLAY_OFFSET - shown.w, 0);
    }
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%.1fdp", x);
    rml_element_set_property(n->el, "left", buffer);
    snprintf(buffer, sizeof buffer, "%.1fdp", k->in.mouse_y + 10);
    rml_element_set_property(n->el, "top", buffer);
}

void drag_cancel(kit *k)
{
    drag_reset(&k->drag);
}

/* ---------- API ---------- */

void kit_drag_source(kit *k, const kit_drag_desc *desc)
{
    node *n = k->last;
    assert(n && desc->type);
    drag_state *d = &k->drag;
    if ((n->events & EV_PRESS) && !d->pending) {
        drag_reset(d);
        d->pending = true;
        str_set(&d->source, n->path);
        d->x0 = k->in.mouse_x;
        d->y0 = k->in.mouse_y;
        d->t0 = k->in.time;
    }
    bool mine = d->pending && str_eq(d->source, n->path);
    if (mine) {
        str_set(&d->type, desc->type);
        str_set(&d->payload, desc->payload);
        str_set(&d->label, desc->label ? desc->label : d->payload);
        d->icon = desc->icon;
    }
    set_flag(n, "data-dragging", mine && d->active);
}

bool kit_drop_target(kit *k, const char *type, char *payload, size_t size)
{
    node *n = k->last;
    assert(n && type);
    apush(n->drop_types, xstrdup(type));
    const drag_state *d = &k->drag;
    bool accepted = d->active && accepts(n, d->type);
    set_flag(n, "data-drop-accept", accepted);
    set_flag(n, "data-drop-over", accepted && str_eq(d->over, n->path));
    if (!d->dropped || !str_eq(d->target, n->path) || !str_eq(d->type, type)) {
        return false;
    }
    snprintf(payload, size, "%s", d->payload ? d->payload : "");
    return true;
}

bool kit_drop_hover(kit *k, char *type, size_t type_size, char *payload, size_t size)
{
    node *n = k->last;
    const drag_state *d = &k->drag;
    if (!n || !d->active || !accepts(n, d->type) || !str_eq(d->over, n->path)) {
        return false;
    }
    snprintf(type, type_size, "%s", d->type);
    snprintf(payload, size, "%s", d->payload ? d->payload : "");
    return true;
}

void kit_drag_hint(kit *k, const char *text)
{
    if (k->drag.active) {
        str_set(&k->drag.hint, text);
    }
}

bool kit_dragging(const kit *k, const char *type)
{
    return k->drag.active && (!type || str_eq(k->drag.type, type));
}
