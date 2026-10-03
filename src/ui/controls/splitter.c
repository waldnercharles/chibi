/* <splitter axis min max value invert/> (controls.h): a bar that resizes
 * what it separates. Dragging moves the value with the pointer along its
 * axis, in dp, within its bounds; input reports each move and change the
 * release. While idle it shows its value attribute's. */
#include "support/math.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct splitter
{
    rml_element *el;
    double value, start, min, max;
    bool vertical, invert;
    float press;
    bool pressing, dragging, dirty;
    bool pending, committed; /* what C owners take */
} splitter;

static rml_listener *listener;

static float along(const splitter *s, const rml_event *e)
{
    return s->vertical ? e->mouse_y : e->mouse_x;
}

static void reflect(splitter *s)
{
    control_flag(s->el, "data-dragging", s->dragging);
}

static void drag(splitter *s, const rml_event *e)
{
    if (e->id == RML_EVENT_DRAGSTART) {
        s->press = along(s, e);
        s->start = s->value;
        s->dragging = true;
        reflect(s);
        return;
    }
    if (e->id == RML_EVENT_DRAGEND) {
        s->dragging = s->pressing = false;
        s->committed = s->dirty = true;
        reflect(s);
        control_dispatch_number(s->el, "change", s->value, false);
        return;
    }
    double delta = (along(s, e) - s->press) / control_dp(s->el) * (s->invert ? -1 : 1);
    s->value = math_clamp(s->start + delta, s->min, s->max);
    s->pending = true;
    control_redraw(s->el);
    control_dispatch_number(s->el, "input", s->value, false);
}

static void on_event(void *, const rml_event *e)
{
    splitter *s = rml_element_user(e->current);
    switch (e->id) {
        case RML_EVENT_MOUSEDOWN: s->pressing = e->button == 0; return;
        case RML_EVENT_MOUSEUP: s->pressing = s->dragging; return;
        case RML_EVENT_DRAGSTART:
        case RML_EVENT_DRAG:
        case RML_EVENT_DRAGEND: drag(s, e); return;
        default: return;
    }
}

static const rml_event_id EVENTS[] = {
    RML_EVENT_MOUSEDOWN, RML_EVENT_MOUSEUP, RML_EVENT_DRAGSTART,
    RML_EVENT_DRAG,      RML_EVENT_DRAGEND,
};

static void read_attributes(splitter *s)
{
    char axis[8];
    control_string(s->el, "axis", axis, sizeof axis);
    s->vertical = !strcmp(axis, "y");
    s->min = control_number(s->el, "min", -INFINITY);
    s->max = control_number(s->el, "max", INFINITY);
    s->invert = control_bool(s->el, "invert");
    if (!s->dragging) {
        s->value = control_number(s->el, "value", 0);
    }
    rml_element_set_property(s->el, "cursor", s->vertical ? "ns-resize" : "ew-resize");
    s->dirty = false;
}

static splitter *splitter_new(rml_element *el)
{
    splitter *s = xcalloc(1, sizeof *s);
    s->el = el;
    s->dirty = true;
    rml_element_set_user(el, s);
    rml_element_set_property(el, "drag", "drag");
    for (size_t i = 0; i < sizeof EVENTS / sizeof *EVENTS; i++) {
        rml_element_add_event_listener(el, EVENTS[i], listener, false);
    }
    return s;
}

static void splitter_update(rml_element *el, void *user)
{
    splitter *s = user ? user : splitter_new(el);
    if (s->dirty) {
        read_attributes(s);
    }
}

static void splitter_attribute(rml_element *, void *user, const char *name)
{
    splitter *s = user;
    static const char *const NAMES[] = { "axis", "min", "max", "value", "invert" };
    for (size_t i = 0; s && i < sizeof NAMES / sizeof *NAMES; i++) {
        s->dirty |= !strcmp(name, NAMES[i]);
    }
}

static void splitter_release(void *user)
{
    free(user);
}

static const rml_element_hooks HOOKS = { .release = splitter_release,
                                         .update = splitter_update,
                                         .attribute = splitter_attribute };

void splitter_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
        listener = rml_listener_create(on_event, NULL);
    }
    rml_register_element_instancer("splitter", instancer);
}

bool splitter_take(rml_element *el, double *value, bool *committed)
{
    splitter *s = rml_element_user(el);
    *committed = s && s->committed;
    bool pending = s && s->pending;
    if (pending) {
        *value = s->value;
    }
    if (s) {
        s->pending = s->committed = false;
    }
    return pending;
}

bool splitter_active(const rml_element *el)
{
    const splitter *s = rml_element_user(el);
    return s && (s->pressing || s->dragging);
}
