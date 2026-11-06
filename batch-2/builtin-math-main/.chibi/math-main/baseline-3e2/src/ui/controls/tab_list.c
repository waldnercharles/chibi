/* <tab-list>: authored tabs keep their actions and selection in the app;
 * the shared control supplies navigation and a scrollable strip. */
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"
#include "ui/input_feel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct tab_list
{
    rml_element *selected;
    int count;
    float width, content, position, tab_width;
    bool shown;
} tab_list;

static rml_listener *listener;

static bool role_is(rml_element *el, const char *name)
{
    char role[24];
    control_string(el, "role", role, sizeof role);
    return !strcmp(role, name);
}

static rml_element *ancestor(rml_element *root, rml_element *target, const char *role)
{
    for (; target && target != root; target = rml_element_parent(target)) {
        if (role_is(target, role)) {
            return target;
        }
    }
    return NULL;
}

static bool enabled(rml_element *el)
{
    return !control_bool(el, "disabled") && !control_bool(el, "data-disabled") &&
           rml_element_is_visible(el, true);
}

static void navigate(rml_element *el, const rml_event *e)
{
    if (e->modifiers & (RML_KM_CTRL | RML_KM_SHIFT | RML_KM_ALT | RML_KM_META)) {
        return;
    }
    bool end = e->key == RML_KEY_HOME || e->key == RML_KEY_END;
    int dir = e->key == RML_KEY_LEFT || e->key == RML_KEY_END ? -1 : 1;
    if (!end && e->key != RML_KEY_LEFT && e->key != RML_KEY_RIGHT) {
        return;
    }
    rml_element *from = ancestor(el, e->target, "tab");
    if (!from) {
        return;
    }
    int count = rml_element_query_selector_all(el, "[role=tab]", NULL, 0), at = -1;
    rml_element **tabs = xmalloc((size_t)(count + 1) * sizeof *tabs);
    rml_element_query_selector_all(el, "[role=tab]", tabs, count);
    for (int i = 0; i < count; i++) {
        if (tabs[i] == from) {
            at = i;
        }
    }
    for (int n = 1; n <= count; n++) {
        int i = end ? (dir > 0 ? n - 1 : count - n)
                    : ((at + dir * n) % count + count) % count;
        if (!enabled(tabs[i])) {
            continue;
        }
        rml_element_focus(tabs[i], true);
        rml_element_scroll_into_view(tabs[i], RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
        rml_element_click(tabs[i]);
        break;
    }
    free(tabs);
    rml_event_stop_propagation(e);
}

static void on_event(void *, const rml_event *e)
{
    rml_element *el = e->current;
    if (e->id == RML_EVENT_KEYDOWN) {
        navigate(el, e);
        return;
    }
    if (e->id == RML_EVENT_MOUSESCROLL) {
        if (e->modifiers & (RML_KM_CTRL | RML_KM_META | RML_KM_ALT)) {
            return;
        }
        rml_vec2 delta = rml_event_wheel(e);
        float along = delta.x != 0 ? delta.x : delta.y;
        if (along == 0 ||
            rml_element_scroll_width(el) <= rml_element_client_width(el)) {
            return;
        }
        rml_element_set_scroll_left(
            el,
            rml_element_scroll_left(el) + along * FEEL_RMLUI_SCROLL_DP * control_dp(el)
        );
        rml_event_stop_propagation(e);
        control_redraw(el);
        return;
    }
    if (e->id == RML_EVENT_MOUSEDOWN && e->button == 2) {
        rml_element *row = ancestor(el, e->target, "presentation");
        rml_element *close = row ? rml_element_query_selector(row, "[data-tab-close]")
                                 : NULL;
        if (close && enabled(close)) {
            rml_element_click(close);
            rml_event_stop_propagation(e);
        }
    }
}

/* Compare content coordinates, which stay stable while the user scrolls.
 * A change in selection, contents or geometry reveals the selected row;
 * unrelated document layouts leave a manually scrolled strip alone. */
static void sync(rml_element *el, tab_list *t)
{
    bool shown = rml_element_is_visible(el, true);
    if (!shown) {
        t->shown = false;
        return;
    }
    rml_element *selected = rml_element_query_selector(el, "[role=tab][aria-selected=true]");
    rml_element *row = selected ? ancestor(el, selected, "presentation") : NULL;
    rml_element *reveal = row ? row : selected;
    int count = rml_element_query_selector_all(el, "[role=tab]", NULL, 0);
    float width = rml_element_client_width(el),
          content = rml_element_scroll_width(el);
    float position = reveal
                       ? rml_element_absolute_offset(reveal, RML_BOX_BORDER).x -
                             rml_element_absolute_offset(el, RML_BOX_CONTENT).x +
                             rml_element_scroll_left(el)
                       : 0;
    float tab_width = reveal ? rml_element_box_size(reveal, RML_BOX_BORDER).x : 0;
    bool changed = !t->shown || t->selected != selected || t->count != count ||
                   fabsf(t->width - width) > 0.5f ||
                   fabsf(t->content - content) > 0.5f ||
                   fabsf(t->position - position) > 0.5f ||
                   fabsf(t->tab_width - tab_width) > 0.5f;
    t->selected = selected;
    t->count = count;
    t->width = width;
    t->content = content;
    t->position = position;
    t->tab_width = tab_width;
    t->shown = true;
    control_flag(el, "data-overflow", content > width + 0.5f);
    if (changed && reveal) {
        rml_element_scroll_into_view(reveal, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
    }
}

static void tab_update(rml_element *el, void *user)
{
    tab_list *t = user;
    if (!t) {
        t = xcalloc(1, sizeof *t);
        rml_element_set_user(el, t);
        rml_element_add_event_listener(el, RML_EVENT_KEYDOWN, listener, true);
        rml_element_add_event_listener(el, RML_EVENT_MOUSESCROLL, listener, true);
        rml_element_add_event_listener(el, RML_EVENT_MOUSEDOWN, listener, true);
    }
    sync(el, t);
}

static void tab_layout(rml_element *el, void *user)
{
    if (user) {
        sync(el, user);
    }
}

static void tab_release(void *user)
{
    free(user);
}

static const rml_element_hooks HOOKS = {
    .update = tab_update,
    .layout = tab_layout,
    .release = tab_release,
};

void tab_list_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
        listener = rml_listener_create(on_event, NULL);
    }
    rml_register_element_instancer("tab-list", instancer);
}
