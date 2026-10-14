/* Drag and drop for every context (drag.h): one session, driven by the
 * pointer and key events each context's root hears first, as they capture,
 * so documents and their listeners never see a press, click or key the drag
 * takes. Targets and the source are found by their attributes, so authored
 * markup, the kit's elements and C surfaces share it. */
#include "ui/drag.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "ui/input_feel.h"
#include "ui/motion.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The overlay's place from the pointer, in dp: beside it and below. */
#define OVERLAY_X 14.0f
#define OVERLAY_Y 10.0f
/* A tree row's top and bottom quarters drop before and after it. */
#define TREE_EDGE 0.25f
/* Elements a query reads without allocating. */
#define QUERY_BATCH 64

typedef enum drag_phase
{
    DRAG_IDLE,
    DRAG_PENDING, /* pressed on a source, not moved far enough */
    DRAG_ACTIVE,
} drag_phase;

/* The overlay element's user data: presence's enter and exit. */
typedef struct overlay_state
{
    int updates; /* since it showed; it enters on the second */
    bool leaving;
    double hide_at;
} overlay_state;

typedef struct drag_session
{
    rml_context *context;
    drag_phase phase;
    rml_element_ref *source;
    float x0, y0, x, y; /* the press and the pointer, in context pixels */
    char *type, *payload, *label, *icon, *hint;
    rml_element_ref *over;
    const char *zone;
    /* The hover element the targets were last marked under; only
       compared, never read. */
    const rml_element *scanned;
    rml_element_ref *overlay;
    rml_element *overlay_icon, *overlay_label, *overlay_hint;
    bool swallow_click; /* the release ended a drag: its click is none */
    bool dragged;       /* a drag began since the last click */
    /* The drop that landed, until taken or the next press. */
    rml_element_ref *landed;
    char *landed_type, *landed_payload;
    const char *landed_zone;
} drag_session;

/* Each context with the root its listener is on, as a root knows no
   context. */
typedef struct drag_context
{
    rml_context *context;
    rml_element *root;
} drag_context;

static drag_session session;
static rml_listener *listener;
static drag_context *contexts;

/* ---------- Helpers ---------- */

static void set_string(char **s, const char *text)
{
    free(*s);
    *s = xstrdup(text ? text : "");
}

static rml_element *ref_get(const rml_element_ref *ref)
{
    return ref ? rml_element_ref_get(ref) : NULL;
}

static void ref_set(rml_element_ref **ref, rml_element *el)
{
    if (*ref) {
        rml_element_ref_destroy(*ref);
    }
    *ref = el ? rml_element_ref_create(el) : NULL;
}

/* The attribute's value as an owned string, "" when absent. */
static char *attribute(const rml_element *el, const char *name)
{
    size_t size = rml_element_attribute(el, name, NULL, 0) + 1;
    char *value = xmalloc(size);
    rml_element_attribute(el, name, value, size);
    return value;
}

static void flag(rml_element *el, const char *name, bool on)
{
    if (on == rml_element_has_attribute(el, name)) {
        return;
    }
    if (on) {
        rml_element_set_attribute(el, name, "");
        return;
    }
    rml_element_remove_attribute(el, name);
}

/* Replaces the element's content with one text node. */
static void set_text(rml_element *el, const char *text)
{
    while (rml_element_child_count(el)) {
        rml_element_remove_child(el, rml_element_first_child(el));
    }
    if (text[0]) {
        rml_element_set_text(rml_element_create_child(el, "#text", NULL), text);
    }
}

static float dp_ratio(rml_context *context)
{
    float ratio = rml_context_dp_ratio(context);
    return ratio > 0 ? ratio : 1;
}

/* Every element under `root` matching `selector`, as an array to afree. */
static rml_element **query(rml_element *root, const char *selector)
{
    rml_element *batch[QUERY_BATCH];
    int count = rml_element_query_selector_all(root, selector, batch, QUERY_BATCH);
    rml_element **all = NULL;
    asetlen(all, count);
    if (count <= QUERY_BATCH) {
        if (count) {
            memcpy(all, batch, (size_t)count * sizeof *all);
        }
        return all;
    }
    rml_element_query_selector_all(root, selector, all, count);
    return all;
}

/* ---------- Sources and targets ---------- */

/* The element or ancestor with a data-drag type, or NULL. */
static rml_element *source_at(rml_element *el)
{
    for (; el; el = rml_element_parent(el)) {
        if (rml_element_has_attribute(el, "data-drag")) {
            char type[2];
            rml_element_attribute(el, "data-drag", type, sizeof type);
            return type[0] ? el : NULL;
        }
    }
    return NULL;
}

/* Whether the element's data-drop names `type` among its words. */
static bool accepts(const rml_element *el, const char *type)
{
    if (!rml_element_has_attribute(el, "data-drop")) {
        return false;
    }
    char *types = attribute(el, "data-drop");
    size_t length = strlen(type);
    bool found = false;
    for (const char *p = types; *p && !found; p += strcspn(p, " \t\n")) {
        p += strspn(p, " \t\n");
        found = !strncmp(p, type, length) && strchr(" \t\n", p[length]);
    }
    free(types);
    return found;
}

/* The nearest element under the pointer that takes the drag, or NULL. */
static rml_element *target_at(rml_element *el)
{
    for (; el; el = rml_element_parent(el)) {
        if (accepts(el, session.type)) {
            return el;
        }
    }
    return NULL;
}

/* Where in the target the pointer drops, by its data-drop-zones. */
static const char *zone_in(rml_element *target)
{
    if (rml_element_has_attribute(target, "data-drop-zones-type")) {
        char *type = attribute(target, "data-drop-zones-type");
        bool matches = !strcmp(type, session.type);
        free(type);
        if (!matches) {
            return "";
        }
    }
    char zones[8] = "";
    if (rml_element_has_attribute(target, "data-drop-zones")) {
        rml_element_attribute(target, "data-drop-zones", zones, sizeof zones);
    }
    bool tree = !strcmp(zones, "tree");
    if (!tree && strcmp(zones, "list")) {
        return "";
    }
    rml_vec2 at = rml_element_absolute_offset(target, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(target, RML_BOX_BORDER);
    float t = (session.y - at.y) / fmaxf(size.y, 1);
    float edge = tree ? TREE_EDGE : 0.5f;
    if (t < edge) {
        return "before";
    }
    return t >= 1 - edge || !tree ? "after" : "inside";
}

/* Marks every target in the context that takes the drag, and unmarks those
 * that no longer do. */
static void mark_targets(rml_context *context)
{
    rml_element *root = rml_context_root(context);
    rml_element **marked = query(root, "[data-drop-accept]");
    for (int i = 0; i < acount(marked); i++) {
        flag(marked[i], "data-drop-accept", accepts(marked[i], session.type));
    }
    afree(marked);
    rml_element **targets = query(root, "[data-drop]");
    for (int i = 0; i < acount(targets); i++) {
        if (accepts(targets[i], session.type)) {
            flag(targets[i], "data-drop-accept", true);
        }
    }
    afree(targets);
}

static void unmark_targets(rml_context *context)
{
    rml_element **marked = query(rml_context_root(context), "[data-drop-accept]");
    for (int i = 0; i < acount(marked); i++) {
        flag(marked[i], "data-drop-accept", false);
    }
    afree(marked);
}

/* ---------- Overlay ---------- */

static overlay_state *overlay_of(rml_element *el)
{
    return rml_element_user(el);
}

/* The document drawn over all the others: the highest z-index, the later
 * one of equals. */
static rml_element *topmost_document(rml_context *context)
{
    rml_element *top = NULL;
    long top_z = LONG_MIN;
    for (int i = 0; i < rml_context_document_count(context); i++) {
        rml_element *document = rml_context_document(context, i);
        if (!rml_element_is_visible(document, false)) {
            continue;
        }
        char z[16];
        rml_element_property(document, "z-index", z, sizeof z);
        long index = strtol(z, NULL, 10); /* "auto" reads 0 */
        if (index >= top_z) {
            top = document;
            top_z = index;
        }
    }
    return top;
}

static rml_element *overlay_part(rml_element *parent, const char *cls)
{
    rml_element *el = rml_element_create_child(parent, "span", NULL);
    rml_element_set_class_names(el, cls);
    return el;
}

/* The overlay in `document`, made there or moved from the one it was in. */
static rml_element *overlay_in(rml_element *document)
{
    rml_element *el = ref_get(session.overlay);
    if (el && rml_element_owner_document(el) == document) {
        return el;
    }
    if (el) {
        rml_element_remove_child(rml_element_parent(el), el);
    }
    el = rml_element_create_child(document, "drag-overlay", NULL);
    rml_element_set_user(el, xcalloc(1, sizeof(overlay_state)));
    rml_element_set_class_names(el, "drag-overlay");
    rml_element_set_property(el, "position", "absolute");
    rml_element_set_property(el, "left", "0px");
    rml_element_set_property(el, "top", "0px");
    rml_element_set_property(el, "pointer-events", "none");
    session.overlay_icon = overlay_part(el, "icon");
    rml_element *text = overlay_part(el, "text");
    session.overlay_label = overlay_part(text, "label");
    session.overlay_hint = overlay_part(text, "hint");
    ref_set(&session.overlay, el);
    return el;
}

static void show_overlay(rml_context *context)
{
    rml_element *document = topmost_document(context);
    if (!document) {
        return;
    }
    rml_element *el = overlay_in(document);
    *overlay_of(el) = (overlay_state){ 0 };
    rml_element_remove_property(el, "display");
    flag(el, "data-ending-style", false);
    flag(el, "data-starting-style", true);
    set_text(session.overlay_icon, session.icon);
    set_text(session.overlay_label, session.label);
    set_text(session.overlay_hint, "");
    flag(el, "data-hint", false);
    flag(el, "data-over", false);
    rml_context_request_next_update(context, 0);
}

static void hide_overlay()
{
    rml_element *el = ref_get(session.overlay);
    if (!el) {
        return;
    }
    overlay_state *o = overlay_of(el);
    o->leaving = true;
    o->hide_at = rml_elapsed_time() + motion_transition_time(el);
    flag(el, "data-starting-style", false);
    flag(el, "data-ending-style", true);
    rml_context *context = rml_element_context(el);
    if (context) {
        rml_context_request_next_update(context, 0);
    }
}

/* Beside the pointer, or before it where the context ends; moved by
 * transform alone, which lays nothing out. */
static void place_overlay(rml_context *context)
{
    rml_element *el = ref_get(session.overlay);
    if (!el) {
        return;
    }
    float ratio = dp_ratio(context);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    rml_vec2 view = rml_element_box_size(rml_context_root(context), RML_BOX_BORDER);
    rml_vec2 origin = rml_element_absolute_offset(rml_element_owner_document(el), RML_BOX_BORDER);
    float x = session.x + OVERLAY_X * ratio;
    if (x + size.x > view.x) {
        x = fmaxf(session.x - OVERLAY_X * ratio - size.x, 0);
    }
    float y = session.y + OVERLAY_Y * ratio;
    char transform[64];
    snprintf(
        transform,
        sizeof transform,
        "translate(%.1fpx, %.1fpx)",
        x - origin.x,
        y - origin.y
    );
    rml_element_set_property(el, "transform", transform);
}

static void show_hint(const char *text)
{
    if (!strcmp(session.hint ? session.hint : "", text)) {
        return;
    }
    set_string(&session.hint, text);
    rml_element *el = ref_get(session.overlay);
    if (el) {
        set_text(session.overlay_hint, text);
        flag(el, "data-hint", text[0]);
    }
}

/* Presence for the overlay: its starting style holds for one update, so
 * its transition runs; once it has left it no longer displays. */
static void overlay_update(rml_element *el, void *user)
{
    overlay_state *o = user;
    if (!o) {
        return;
    }
    if (rml_element_has_attribute(el, "data-starting-style") && ++o->updates >= 2) {
        flag(el, "data-starting-style", false);
    } else if (rml_element_has_attribute(el, "data-starting-style")) {
        rml_context_request_next_update(rml_element_context(el), 0);
    }
    if (o->leaving && rml_elapsed_time() >= o->hide_at) {
        o->leaving = false;
        rml_element_set_property(el, "display", "none");
    } else if (o->leaving) {
        rml_context_request_next_update(rml_element_context(el), o->hide_at - rml_elapsed_time());
    }
}

static void overlay_release(void *user)
{
    free(user);
}

/* ---------- Session ---------- */

/* Marks where in a target with zones the pointer drops; "" unmarks it. A
 * target without zones keeps whatever data-drop-zone its owner sets. */
static void mark_zone(rml_element *target, const char *zone)
{
    if (!rml_element_has_attribute(target, "data-drop-zones")) {
        return;
    }
    char shown[8] = "";
    rml_element_attribute(target, "data-drop-zone", shown, sizeof shown);
    if (zone[0] && strcmp(shown, zone)) {
        rml_element_set_attribute(target, "data-drop-zone", zone);
    } else if (!zone[0] && rml_element_has_attribute(target, "data-drop-zone")) {
        rml_element_remove_attribute(target, "data-drop-zone");
    }
}

static void set_over(rml_element *target, const char *zone)
{
    rml_element *old = ref_get(session.over);
    if (old != target) {
        if (old) {
            flag(old, "data-drop-over", false);
            mark_zone(old, "");
        }
        ref_set(&session.over, target);
        char *hint = target ? attribute(target, "data-drop-hint") : xstrdup("");
        show_hint(hint);
        free(hint);
    }
    session.zone = zone;
    if (target) {
        flag(target, "data-drop-over", true);
        mark_zone(target, zone);
    }
    rml_element *overlay = ref_get(session.overlay);
    if (overlay) {
        flag(overlay, "data-over", target != NULL);
    }
}

/* Follows the pointer: the target under it, and the overlay. */
static void track(rml_context *context)
{
    rml_element *hover = rml_context_hover_element(context);
    if (hover != session.scanned) {
        mark_targets(context);
        session.scanned = hover;
    }
    rml_element *target = target_at(hover);
    set_over(target, target ? zone_in(target) : "");
    place_overlay(context);
}

static void forget_landed()
{
    ref_set(&session.landed, NULL);
    free(session.landed_type);
    free(session.landed_payload);
    session.landed_type = session.landed_payload = NULL;
    session.landed_zone = "";
}

/* Ends a press or a drag, leaving the elements as they were before it. */
static void end_drag()
{
    rml_element *source = ref_get(session.source);
    if (source) {
        flag(source, "data-dragging", false);
    }
    if (session.phase == DRAG_ACTIVE) {
        set_over(NULL, "");
        unmark_targets(session.context);
        hide_overlay();
    }
    ref_set(&session.source, NULL);
    ref_set(&session.over, NULL);
    char **owned[] = { &session.type,
                       &session.payload,
                       &session.label,
                       &session.icon,
                       &session.hint };
    for (size_t i = 0; i < sizeof owned / sizeof *owned; i++) {
        free(*owned[i]);
        *owned[i] = NULL;
    }
    session.phase = DRAG_IDLE;
    session.scanned = NULL;
}

/* Drops on `target`: kept for ui_drag_dropped and sent as `drop`. */
static void land(rml_element *target)
{
    forget_landed();
    ref_set(&session.landed, target);
    session.landed_type = xstrdup(session.type);
    session.landed_payload = xstrdup(session.payload);
    session.landed_zone = session.zone;
    end_drag();
    const char *names[] = { "type", "payload", "zone" };
    const char *values[] = { session.landed_type,
                             session.landed_payload,
                             session.landed_zone };
    rml_element_dispatch_texts(target, "drop", 3, names, values);
}

static void activate(rml_context *context)
{
    session.phase = DRAG_ACTIVE;
    session.dragged = true;
    session.scanned = NULL;
    rml_element *source = ref_get(session.source);
    if (source) {
        flag(source, "data-dragging", true);
    }
    show_overlay(context);
}

/* ---------- Events ---------- */

static void press(rml_context *context, const rml_event *e)
{
    if (e->button != 0) {
        return;
    }
    /* A drag still running missed its release, outside every element. */
    end_drag();
    forget_landed();
    session.swallow_click = false;
    rml_element *source = source_at(e->target);
    if (!source) {
        return;
    }
    session.context = context;
    session.phase = DRAG_PENDING;
    ref_set(&session.source, source);
    session.x0 = session.x = e->mouse_x;
    session.y0 = session.y = e->mouse_y;
    session.type = attribute(source, "data-drag");
    session.payload = attribute(source, "data-drag-payload");
    session.label = rml_element_has_attribute(source, "data-drag-label")
                      ? attribute(source, "data-drag-label")
                      : xstrdup(session.payload);
    session.icon = attribute(source, "data-drag-icon");
    session.hint = xstrdup("");
    session.zone = "";
}

static void move(rml_context *context, const rml_event *e)
{
    if (session.phase == DRAG_IDLE || session.context != context) {
        return;
    }
    session.x = e->mouse_x;
    session.y = e->mouse_y;
    float moved = hypotf(session.x - session.x0, session.y - session.y0);
    if (session.phase == DRAG_PENDING && moved < FEEL_DRAG_DP * dp_ratio(context)) {
        return;
    }
    if (session.phase == DRAG_PENDING) {
        activate(context);
    }
    track(context);
}

static void release(rml_context *context, const rml_event *e)
{
    if (e->button != 0 || session.phase == DRAG_IDLE || session.context != context) {
        return;
    }
    if (session.phase == DRAG_PENDING) {
        end_drag();
        return;
    }
    session.x = e->mouse_x;
    session.y = e->mouse_y;
    track(context);
    session.swallow_click = true;
    rml_element *target = ref_get(session.over);
    if (target) {
        land(target);
        return;
    }
    end_drag();
}

static void click(const rml_event *e)
{
    if (session.swallow_click) {
        session.swallow_click = false;
        rml_event_stop_propagation(e);
        return;
    }
    session.dragged = false;
}

static void key(rml_context *context, const rml_event *e)
{
    if (e->key != RML_KEY_ESCAPE || session.phase != DRAG_ACTIVE ||
        session.context != context) {
        return;
    }
    end_drag();
    session.swallow_click = true;
    rml_event_stop_propagation(e);
}

static rml_context *context_of(const rml_element *root)
{
    for (int i = 0; i < acount(contexts); i++) {
        if (contexts[i].root == root) {
            return contexts[i].context;
        }
    }
    return NULL;
}

static void on_event(void *, const rml_event *e)
{
    rml_context *context = context_of(e->current);
    assert(context);
    switch (e->id) {
        case RML_EVENT_MOUSEDOWN: press(context, e); return;
        case RML_EVENT_MOUSEMOVE: move(context, e); return;
        case RML_EVENT_MOUSEUP: release(context, e); return;
        case RML_EVENT_CLICK: click(e); return;
        case RML_EVENT_DBLCLICK:
            if (session.dragged) {
                rml_event_stop_propagation(e);
            }
            return;
        case RML_EVENT_KEYDOWN: key(context, e); return;
        default: return;
    }
}

static const rml_event_id EVENTS[] = {
    RML_EVENT_MOUSEDOWN, RML_EVENT_MOUSEMOVE, RML_EVENT_MOUSEUP,
    RML_EVENT_CLICK,     RML_EVENT_DBLCLICK,  RML_EVENT_KEYDOWN,
};

static void context_created(rml_context *context)
{
    rml_element *root = rml_context_root(context);
    apush(contexts, ((drag_context){ context, root }));
    for (size_t i = 0; i < sizeof EVENTS / sizeof *EVENTS; i++) {
        rml_element_add_event_listener(root, EVENTS[i], listener, true);
    }
}

/* Lets go of what the context holds, touching none of its elements. */
static void context_destroyed(rml_context *context)
{
    for (int i = 0; i < acount(contexts); i++) {
        if (contexts[i].context == context) {
            adel(contexts, i);
            break;
        }
    }
    rml_element *overlay = ref_get(session.overlay);
    if (overlay && rml_element_context(overlay) == context) {
        ref_set(&session.overlay, NULL);
    }
    if (session.context != context) {
        return;
    }
    session.phase = DRAG_IDLE;
    ref_set(&session.source, NULL);
    ref_set(&session.over, NULL);
    end_drag();
    forget_landed();
    session.context = NULL;
}

static const rml_context_hooks CONTEXT_HOOKS = { .create = context_created,
                                                 .destroy = context_destroyed };
static const rml_element_hooks OVERLAY_HOOKS = { .release = overlay_release,
                                                 .update = overlay_update };

void ui_drag_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&OVERLAY_HOOKS);
        listener = rml_listener_create(on_event, NULL);
    }
    rml_register_element_instancer("drag-overlay", instancer);
    rml_add_context_hooks(&CONTEXT_HOOKS);
}

/* ---------- C owners ---------- */

static void fill(ui_drag_item *item, const char *type, const char *payload, const char *zone)
{
    if (item) {
        *item = (ui_drag_item){ .type = type, .payload = payload, .zone = zone };
    }
}

bool ui_drag_active(rml_context *context, ui_drag_item *item)
{
    if (session.phase != DRAG_ACTIVE || session.context != context) {
        return false;
    }
    fill(item, session.type, session.payload, session.zone);
    return true;
}

bool ui_drag_over(const rml_element *target, ui_drag_item *item)
{
    if (session.phase != DRAG_ACTIVE || !target || ref_get(session.over) != target) {
        return false;
    }
    fill(item, session.type, session.payload, session.zone);
    return true;
}

bool ui_drag_dropped(const rml_element *target, const char *type, ui_drag_item *item)
{
    if (!target || ref_get(session.landed) != target ||
        (type && strcmp(session.landed_type, type))) {
        return false;
    }
    ref_set(&session.landed, NULL);
    fill(item, session.landed_type, session.landed_payload, session.landed_zone);
    return true;
}

void ui_drag_hint(rml_context *context, const char *text)
{
    if (session.phase == DRAG_ACTIVE && session.context == context) {
        show_hint(text ? text : "");
    }
}

void ui_drag_cancel(rml_context *context)
{
    if (session.phase == DRAG_IDLE || session.context != context) {
        return;
    }
    session.swallow_click = session.phase == DRAG_ACTIVE;
    end_drag();
}
