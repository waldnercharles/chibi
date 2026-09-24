/* Drag and drop after dnd-kit: a source is picked up once the pointer moves 5 dp
 * with the button held, or is held 200 ms within 10 dp. An overlay follows the
 * pointer, the target under it that accepts the type is marked data-drop-over,
 * release drops there and Escape cancels. Clicks during a drag are swallowed so a
 * drop never also activates what it lands on. */
#include "editor/rmlui/kit_internal.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace kit_impl {

namespace {

constexpr float kActivationDistance = 5;
constexpr double kActivationDelay = 0.2;
constexpr float kDelayTolerance = 10;

} // namespace

void drag_begin_frame(kit *k)
{
    DragState &d = k->drag;
    if (!d.pending) {
        return;
    }
    float moved = std::hypot(k->in.mouse_x - d.x0, k->in.mouse_y - d.y0);
    if (!d.active) {
        if (!k->in.down[0]) {
            d = DragState{};
            return;
        }
        bool held = k->in.time - d.t0 >= kActivationDelay && moved <= kDelayTolerance;
        if (moved < kActivationDistance && !held) {
            return;
        }
        d.active = true;
        close_all_popups(k);
    }
    /* The nearest node under the pointer that accepts this type. */
    d.over.clear();
    for (Node *n = node_for(k, k->context->GetHoverElement()); n; n = n->parent) {
        if (n->drop_type == d.type) {
            d.over = n->path;
            break;
        }
    }
    if (!k->in.down[0]) {
        d.dropped = !d.over.empty();
        d.target = d.over;
        d.active = false;
        d.pending = d.dropped;
    }
}

void drag_end_frame(kit *k)
{
    DragState &d = k->drag;
    if (d.dropped) {
        d = DragState{};
        return;
    }
    if (!d.active) {
        return;
    }
    Node *n = emit_in(k, k->layer, "@drag", Kind::Box, "div", "drag-overlay");
    presence(k, n);
    if (!n->w.label_el) {
        n->w.icon_el = raw_icon(k, n->el, "", 0);
        n->w.label_el = raw(k, n->el, "span", "label");
    }
    if (n->w.icon != d.icon) {
        n->w.icon = d.icon;
        n->w.icon_el->SetInnerRML(d.icon ? utf8(d.icon) : "");
    }
    set_element_text(n->w.label_el, d.label);
    set_flag(n, "data-over", !d.over.empty());
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1fdp", k->in.mouse_x + 14);
    n->el->SetProperty("left", buffer);
    std::snprintf(buffer, sizeof buffer, "%.1fdp", k->in.mouse_y + 10);
    n->el->SetProperty("top", buffer);
}

void drag_cancel(kit *k)
{
    k->drag = DragState{};
}

} // namespace kit_impl

using namespace kit_impl;

extern "C" void kit_drag_source(kit *k, const kit_drag_desc *desc)
{
    Node *n = k->last;
    assert(n && desc->type);
    DragState &d = k->drag;
    if ((n->events & EV_PRESS) && !d.pending) {
        d = DragState{};
        d.pending = true;
        d.source = n->path;
        d.x0 = k->in.mouse_x;
        d.y0 = k->in.mouse_y;
        d.t0 = k->in.time;
    }
    bool mine = d.pending && d.source == n->path;
    if (mine) {
        d.type = desc->type;
        d.payload = desc->payload ? desc->payload : "";
        d.label = desc->label ? desc->label : d.payload;
        d.icon = desc->icon;
    }
    set_flag(n, "data-dragging", mine && d.active);
}

extern "C" bool kit_drop_target(kit *k, const char *type, char *payload, size_t size)
{
    Node *n = k->last;
    assert(n && type);
    n->drop_type = type;
    const DragState &d = k->drag;
    set_flag(n, "data-drop-over", d.active && d.type == type && d.over == n->path);
    if (!d.dropped || d.target != n->path || d.type != type) {
        return false;
    }
    std::snprintf(payload, size, "%s", d.payload.c_str());
    return true;
}

extern "C" bool kit_dragging(const kit *k, const char *type)
{
    return k->drag.active && (!type || k->drag.type == type);
}
