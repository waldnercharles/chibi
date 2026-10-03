/* Drag and drop after dnd-kit: a source is picked up once the pointer moves
 * FEEL_DRAG_DP with the button held, dnd-kit's distance constraint; holding
 * still never picks it up, so a slow click still clicks. An overlay follows
 * the pointer, the target under it that accepts the type is marked
 * data-drop-over, release drops there and Escape cancels. Clicks during a drag
 * are swallowed so a drop never also activates what it lands on. */
#include "editor/rmlui/kit_internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void drag_reset(drag_state *d)
{
    char *owned[] = { d->type,   d->payload, d->label,
                      d->source, d->over,    d->target };
    for (size_t i = 0; i < sizeof owned / sizeof *owned; i++) {
        free(owned[i]);
    }
    *d = (drag_state){ 0 };
}

/* Picks the pending source up once it moved far enough. False while it
 * waits, and once released first, which ends it. */
static bool activate(kit *k, drag_state *d)
{
    if (!k->in.down[0]) {
        drag_reset(d);
        return false;
    }
    if (hypotf(k->in.mouse_x - d->x0, k->in.mouse_y - d->y0) < FEEL_DRAG_DP) {
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
    for (node *n = node_for(k, rml_context_hover_element(k->context)); n; n = n->parent) {
        if (str_eq(n->drop_type, d->type)) {
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
        w->label_el = raw(n->el, "span", "label", NULL);
    }
    if (w->icon != d->icon) {
        w->icon = d->icon;
        char text[5];
        utf8(text, d->icon);
        rml_element_set_inner_rml(w->icon_el, text);
    }
    set_element_text(w->label_el, d->label);
    set_flag(n, "data-over", !str_eq(d->over, ""));
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%.1fdp", k->in.mouse_x + 14);
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
    str_set(&n->drop_type, type);
    const drag_state *d = &k->drag;
    set_flag(n, "data-drop-over", d->active && str_eq(d->type, type) && str_eq(d->over, n->path));
    if (!d->dropped || !str_eq(d->target, n->path) || !str_eq(d->type, type)) {
        return false;
    }
    snprintf(payload, size, "%s", d->payload ? d->payload : "");
    return true;
}

bool kit_dragging(const kit *k, const char *type)
{
    return k->drag.active && (!type || str_eq(k->drag.type, type));
}
