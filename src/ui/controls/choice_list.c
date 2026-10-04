/* <choice-list> (controls.h): choices under a search field, as Base UI's
 * Combobox lists them and cmdk filters them. Its descendants with
 * role="option" are the choices, each with a value attribute and its label
 * in data-label or its .label; its first <input> searches them by
 * support/text_search.h's ranking. Filtered options and role="group"
 * elements left with none take data-filtered, and the list data-empty when
 * nothing is left. The highlight (data-highlighted) starts on the first
 * shown option and moves with the pointer and with Up, Down, PageUp,
 * PageDown, Home and End in the field (and cmdk's Ctrl+N, Ctrl+P, Ctrl+J and
 * Ctrl+K); Enter or a click chooses, sending change with ev.value, the
 * option's value. Shown again, it clears its search, which shows the list's
 * placeholder when it has one, and highlights the current option
 * (aria-selected="true" or data-current), as it does when options a list
 * loads on opening arrive. The search field's own input and change stay
 * inside it.
 *
 * A `virtualized` list's options are the app's to filter, order and window,
 * as Base UI's Combobox takes them with filter={null} and virtualized: its
 * search field's input reaches the app, each option carries aria-posinset
 * (its place among them all, from 1), and the list aria-setsize (how many
 * there are) and `highlight`, the highlighted option's place from 0, which
 * the app owns. Keys and the pointer send `highlight` with ev.value, the
 * place to highlight, stopping at either end; the list marks the option at
 * `highlight` whenever it shows, as the app's window moves under it. */
#include "support/array.h"
#include "support/text_search.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QUERY_TEXT 256
#define LABEL_TEXT 256

typedef struct choice_list
{
    bool shown;  /* visible at the last update */
    int options; /* how many it had then */
    /* A virtualized list's highlighted place: the last it sent, until the
       app's `highlight` changes, so keys pressed before the app answers
       move on from it. */
    int place, seen;
} choice_list;

/* How far a key moves the highlight: one option, a view's worth or to an
   end; signed by direction. */
enum
{
    STEP_ONE = 1,
    STEP_PAGE = 2,
    STEP_END = 3,
};

static rml_listener *listener;

static bool virtualized(const rml_element *el)
{
    return control_bool(el, "virtualized");
}

/* Every option, or only those shown; free with afree. */
static rml_element **options_of(rml_element *el, bool shown)
{
    int count = rml_element_query_selector_all(el, "[role=option]", NULL, 0);
    rml_element **all = xmalloc((size_t)(count + 1) * sizeof *all), **options = NULL;
    rml_element_query_selector_all(el, "[role=option]", all, count);
    for (int i = 0; i < count; i++) {
        bool hidden = rml_element_has_attribute(all[i], "data-filtered") ||
                      !rml_element_is_visible(all[i], true);
        if (!shown || !hidden) {
            apush(options, all[i]);
        }
    }
    free(all);
    return options;
}

static rml_element *search_of(rml_element *el)
{
    return rml_element_query_selector(el, "input");
}

static bool enabled(const rml_element *option)
{
    return !rml_element_has_attribute(option, "data-disabled");
}

static void highlight(rml_element *el, rml_element *option, bool reveal)
{
    rml_element **options = options_of(el, false);
    for (int i = 0; i < acount(options); i++) {
        control_flag(options[i], "data-highlighted", options[i] == option);
    }
    afree(options);
    if (option && reveal) {
        rml_element_scroll_into_view(option, RML_SCROLL_NEAREST, RML_SCROLL_NEAREST);
    }
}

static rml_element *highlighted(rml_element *el)
{
    rml_element **options = options_of(el, true), *lit = NULL;
    for (int i = 0; i < acount(options) && !lit; i++) {
        lit = rml_element_has_attribute(options[i], "data-highlighted") ? options[i]
                                                                        : NULL;
    }
    afree(options);
    return lit;
}

/* Moves the highlight `dir` shown options from the highlighted one,
   wrapping; with `end`, to the first or the last. */
static void move(rml_element *el, int dir, bool end)
{
    rml_element **options = options_of(el, true),
                *from = end ? NULL : highlighted(el);
    int count = acount(options), at = -1;
    for (int i = 0; i < count; i++) {
        at = options[i] == from ? i : at;
    }
    for (int n = 1; n <= count; n++) {
        int i = at < 0 ? (dir > 0 ? n - 1 : count - n)
                       : ((at + dir * n) % count + count) % count;
        if (enabled(options[i])) {
            highlight(el, options[i], true);
            break;
        }
    }
    afree(options);
}

static void label_of(rml_element *option, char *out, size_t size)
{
    control_string(option, "data-label", out, size);
    if (out[0]) {
        return;
    }
    rml_element *label = rml_element_query_selector(option, ".label");
    control_text_of(label ? label : option, out, size);
}

/* Filters the options by the search field's text and highlights the first
   one left. */
static void filter(rml_element *el)
{
    char query[QUERY_TEXT] = "";
    rml_element *search = search_of(el);
    if (search) {
        rml_element_value(search, query, sizeof query);
    }
    rml_element **options = options_of(el, false);
    bool any = false;
    for (int i = 0; i < acount(options); i++) {
        char label[LABEL_TEXT], keywords[LABEL_TEXT];
        label_of(options[i], label, sizeof label);
        control_string(options[i], "data-keywords", keywords, sizeof keywords);
        bool match = text_search_score(query, label, keywords) >= 0;
        control_flag(options[i], "data-filtered", !match);
        any |= match;
    }
    afree(options);
    int count = rml_element_query_selector_all(el, "[role=group]", NULL, 0);
    rml_element **groups = xmalloc((size_t)(count + 1) * sizeof *groups);
    rml_element_query_selector_all(el, "[role=group]", groups, count);
    for (int i = 0; i < count; i++) {
        bool none = !rml_element_query_selector(groups[i], "[role=option]:not([data-filtered])");
        control_flag(groups[i], "data-filtered", none);
    }
    free(groups);
    control_flag(el, "data-empty", !any);
    move(el, 1, true);
}

/* Filters the options and, while nothing is typed, highlights the current
   one. */
static void show_current(rml_element *el)
{
    filter(el);
    char query[QUERY_TEXT] = "";
    rml_element *search = search_of(el);
    if (search) {
        rml_element_value(search, query, sizeof query);
    }
    rml_element *current = rml_element_query_selector(el, "[role=option][aria-selected=true], [role=option][data-current]");
    if (current && !query[0]) {
        highlight(el, current, true);
    }
}

/* Opened again: the whole list, with the current option highlighted, and
   the list's placeholder in its search field, as cmdk's Command.Input. */
static void reset(rml_element *el)
{
    rml_element *search = search_of(el);
    char placeholder[LABEL_TEXT];
    control_string(el, "placeholder", placeholder, sizeof placeholder);
    if (search && placeholder[0]) {
        rml_element_set_attribute(search, "placeholder", placeholder);
    }
    if (search) {
        rml_element_set_value(search, "");
    }
    show_current(el);
}

static rml_element *option_at(rml_element *el, rml_element *target)
{
    for (; target && target != el; target = rml_element_parent(target)) {
        char role[16];
        control_string(target, "role", role, sizeof role);
        if (!strcmp(role, "option")) {
            return target;
        }
    }
    return NULL;
}

static void choose(rml_element *el, rml_element *option)
{
    char value[LABEL_TEXT];
    control_string(option, "value", value, sizeof value);
    rml_element_dispatch_value(el, "change", value, false);
}

/* cmdk's Ctrl+N, Ctrl+P, Ctrl+J and Ctrl+K: the Control key, which hosts
   that trade Control and Command for text editing, as Studio does on macOS,
   report as Meta. */
static int step_of(const rml_event *e)
{
    bool ctrl = e->modifiers & (RML_KM_CTRL | RML_KM_META);
    switch (e->key) {
        case RML_KEY_DOWN: return STEP_ONE;
        case RML_KEY_UP: return -STEP_ONE;
        case RML_KEY_NEXT: return STEP_PAGE;
        case RML_KEY_PRIOR: return -STEP_PAGE;
        case RML_KEY_END: return STEP_END;
        case RML_KEY_HOME: return -STEP_END;
        case RML_KEY_N:
        case RML_KEY_J: return ctrl ? STEP_ONE : 0;
        case RML_KEY_P:
        case RML_KEY_K: return ctrl ? -STEP_ONE : 0;
        default: return 0;
    }
}

/* How many options of `option`'s height a view of the results holds, at
   least one. */
static int page_of(rml_element *el, rml_element *option)
{
    rml_element *results = rml_element_query_selector(el, ".choice-results, .list");
    float row = option ? rml_element_box_size(option, RML_BOX_BORDER).y : 0;
    float view = rml_element_client_height(results ? results : el);
    int page = row > 0 ? (int)(view / row) : 1;
    return page > 1 ? page : 1;
}

/* Moves a list's own highlight, which wraps from one end to the other. */
static void move_own(rml_element *el, int step)
{
    int dir = step > 0 ? 1 : -1;
    if (abs(step) == STEP_END) {
        move(el, -dir, true);
        return;
    }
    int count = abs(step) == STEP_PAGE ? page_of(el, highlighted(el)) : 1;
    for (int i = 0; i < count; i++) {
        move(el, dir, false);
    }
}

/* The shown option at a virtualized list's place `at`, or NULL. */
static rml_element *option_at_place(rml_element *el, int at)
{
    char selector[64];
    snprintf(selector, sizeof selector, "[role=option][aria-posinset=\"%d\"]", at + 1);
    return rml_element_query_selector(el, selector);
}

/* The highlighted place: the app's `highlight`, or the place sent since
   it last changed. */
static int place_of(rml_element *el)
{
    choice_list *c = rml_element_user(el);
    int seen = (int)control_number(el, "highlight", 0);
    if (seen != c->seen) {
        c->place = c->seen = seen;
    }
    return c->place;
}

/* Asks the app to highlight place `at`: `highlight` with ev.value, and
   ev.pointer "1" when the pointer moved there, which scrolls nothing. */
static void send_highlight(rml_element *el, int at, bool pointer)
{
    choice_list *c = rml_element_user(el);
    c->place = at;
    char value[16];
    snprintf(value, sizeof value, "%d", at);
    const char *const names[] = { "value", "pointer" };
    const char *const values[] = { value, pointer ? "1" : "" };
    rml_element_dispatch_texts(el, "highlight", 2, names, values);
}

/* Asks the app to move a virtualized list's highlight, which stops at
   either end. */
static void move_place(rml_element *el, int step)
{
    int count = (int)control_number(el, "aria-setsize", 0), at = place_of(el);
    if (count <= 0) {
        return;
    }
    int by = abs(step) == STEP_PAGE ? page_of(el, option_at_place(el, at)) : 1;
    int next = abs(step) == STEP_END ? (step > 0 ? count - 1 : 0)
                                     : at + (step > 0 ? by : -by);
    next = next < 0 ? 0 : next >= count ? count - 1 : next;
    if (next != at) {
        send_highlight(el, next, false);
    }
}

/* Keys in the search field move the highlight and choose. */
static void key(rml_element *el, const rml_event *e)
{
    if (e->target != search_of(el)) {
        return;
    }
    int step = step_of(e);
    bool enter = e->key == RML_KEY_RETURN || e->key == RML_KEY_NUMPADENTER;
    if (!step && !enter) {
        return;
    }
    if (step && virtualized(el)) {
        move_place(el, step);
    } else if (step) {
        move_own(el, step);
    } else {
        rml_element *lit = virtualized(el) ? option_at_place(el, place_of(el))
                                           : highlighted(el);
        if (lit && enabled(lit)) {
            rml_element_click(lit);
        }
    }
    rml_event_stop_propagation(e);
}

/* The pointer over an option highlights it, without scrolling (cmdk). */
static void hovered(rml_element *el, rml_element *option)
{
    if (!virtualized(el)) {
        highlight(el, option, false);
        return;
    }
    int at = (int)control_number(option, "aria-posinset", 0) - 1;
    if (at >= 0 && at != place_of(el)) {
        send_highlight(el, at, true);
    }
}

static void on_event(void *, const rml_event *e)
{
    rml_element *el = e->current;
    rml_element *option = option_at(el, e->target);
    switch (e->id) {
        case RML_EVENT_KEYDOWN: key(el, e); return;
        case RML_EVENT_MOUSEMOVE:
            if (option && enabled(option)) {
                hovered(el, option);
            }
            return;
        case RML_EVENT_CLICK:
            if (option && enabled(option)) {
                choose(el, option);
            }
            return;
        default: break;
    }
    /* The search field's own input and change stay inside; typing filters.
       A virtualized list's app filters, so they reach it, and its
       highlight is the app's again. */
    if (e->target == el) {
        return;
    }
    if (virtualized(el)) {
        ((choice_list *)rml_element_user(el))->seen = -1;
        return;
    }
    if (e->id == RML_EVENT_INPUT && e->target == search_of(el)) {
        filter(el);
    }
    rml_event_stop_propagation(e);
}

static void list_update(rml_element *el, void *user)
{
    choice_list *c = user;
    if (!c) {
        c = xcalloc(1, sizeof *c);
        rml_element_set_user(el, c);
        rml_element_add_event_listener(el, RML_EVENT_KEYDOWN, listener, true);
        rml_element_add_event_listener(el, RML_EVENT_MOUSEMOVE, listener, false);
        rml_element_add_event_listener(el, RML_EVENT_CLICK, listener, false);
        rml_element_add_event_listener(el, RML_EVENT_INPUT, listener, false);
        rml_element_add_event_listener(el, RML_EVENT_CHANGE, listener, false);
    }
    bool shown = rml_element_is_visible(el, true);
    if (shown && virtualized(el)) {
        highlight(el, option_at_place(el, place_of(el)), false);
        c->shown = shown;
        return;
    }
    int options = shown
                    ? rml_element_query_selector_all(el, "[role=option]", NULL, 0)
                    : 0;
    if (shown && !c->shown) {
        reset(el);
    } else if (shown && options != c->options) {
        /* Options a list loads once it opens arrived. */
        show_current(el);
    }
    c->shown = shown;
    c->options = options;
}

static void list_release(void *user)
{
    free(user);
}

static const rml_element_hooks HOOKS = { .release = list_release, .update = list_update };

void choice_list_register()
{
    static rml_instancer *instancer;
    if (!instancer) {
        instancer = rml_instancer_create(&HOOKS);
        listener = rml_listener_create(on_event, NULL);
    }
    rml_register_element_instancer("choice-list", instancer);
}
