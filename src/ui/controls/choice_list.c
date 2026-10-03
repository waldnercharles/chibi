/* <choice-list> (controls.h): choices under a search field, as Base UI's
 * Combobox lists them and cmdk filters them. Its descendants with
 * role="option" are the choices, each with a value attribute and its label
 * in data-label or its .label; its first <input> searches them by
 * support/text_search.h's ranking. Filtered options and role="group"
 * elements left with none take data-filtered, and the list data-empty when
 * nothing is left. The highlight (data-highlighted) starts on the first
 * shown option and moves with the pointer and with Up, Down, Home and End
 * in the field; Enter or a click chooses, sending change with ev.value, the
 * option's value. Shown again, it clears its search and highlights the
 * current option (aria-selected="true" or data-current), its search field
 * showing the list's placeholder when it has one. The search field's own
 * input and change stay inside it. */
#include "support/array.h"
#include "support/text_search.h"
#include "support/xalloc.h"
#include "ui/controls/controls_internal.h"

#include <stdlib.h>
#include <string.h>

#define QUERY_TEXT 256
#define LABEL_TEXT 256

typedef struct choice_list
{
    bool shown; /* visible at the last update */
} choice_list;

static rml_listener *listener;

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
    filter(el);
    rml_element *current = rml_element_query_selector(el, "[role=option][aria-selected=true], [role=option][data-current]");
    if (current) {
        highlight(el, current, true);
    }
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

/* Keys in the search field move the highlight and choose. */
static void key(rml_element *el, const rml_event *e)
{
    if (e->target != search_of(el)) {
        return;
    }
    rml_element *lit = highlighted(el);
    switch (e->key) {
        case RML_KEY_DOWN: move(el, 1, false); break;
        case RML_KEY_UP: move(el, -1, false); break;
        case RML_KEY_HOME: move(el, 1, true); break;
        case RML_KEY_END: move(el, -1, true); break;
        case RML_KEY_RETURN:
        case RML_KEY_NUMPADENTER:
            if (lit && enabled(lit)) {
                rml_element_click(lit);
            }
            break;
        default: return;
    }
    rml_event_stop_propagation(e);
}

static void on_event(void *, const rml_event *e)
{
    rml_element *el = e->current;
    rml_element *option = option_at(el, e->target);
    switch (e->id) {
        case RML_EVENT_KEYDOWN: key(el, e); return;
        case RML_EVENT_MOUSEMOVE:
            if (option) {
                highlight(el, option, false);
            }
            return;
        case RML_EVENT_CLICK:
            if (option && enabled(option)) {
                choose(el, option);
            }
            return;
        default: break;
    }
    /* The search field's own input and change stay inside; typing filters. */
    if (e->target == el) {
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
    if (shown && !c->shown) {
        reset(el);
    }
    c->shown = shown;
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
