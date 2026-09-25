/* Virtualized lists after Virtuoso: a scroll container sized for every row, of
 * which only the visible range plus overscan is emitted. Rows have one fixed
 * height, as kit rows do. Following lists (logs) stay pinned to the bottom only
 * while they were already within 4 dp of it; reveal keeps a row in view with
 * the least scrolling. */
#include "editor/rmlui/kit_internal.h"
#include "support/math.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define BOTTOM_THRESHOLD 4.0f
#define DEFAULT_OVERSCAN 120.0f

/* Pinned when the user left it at the bottom of the previous content, whose
 * height w->value holds. */
static float follow(widget *w, const kit_list_desc *d, float total, float view, float scroll)
{
    float previous_max = math_max((float)w->value - view, 0.0f);
    if (d->follow && w->at_bottom && total > w->value) {
        return math_max(total - view, 0.0f);
    }
    w->at_bottom = scroll >= previous_max - BOTTOM_THRESHOLD;
    return scroll;
}

/* Scrolls the least to bring a newly revealed row into view. */
static float reveal(const widget *w, const kit_list_desc *d, float row, float view, float scroll)
{
    if (d->reveal < 0 || d->reveal >= d->count || d->reveal == w->pending_index) {
        return scroll;
    }
    float top = d->reveal * row;
    if (top < scroll) {
        return top;
    }
    if (top + row > scroll + view) {
        return top + row - view;
    }
    return scroll;
}

static void set_dp(rml_element *el, const char *property, float value)
{
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%.2fdp", value);
    rml_element_set_property(el, property, buffer);
}

bool kit_list_begin(kit *k, const char *key, const char *cls, const kit_list_desc *d, int *first, int *last)
{
    const kit_class list[] = { { cls, cls != NULL } };
    node *n = emit(k, key, KIND_LIST, "div", kit_classes(k, "vlist", list, 1));
    widget *w = &n->w;
    if (!w->body) {
        w->body = raw(n->el, "div", "vlist-content", NULL);
        n->container = w->body;
        w->pending_index = -1;
        w->at_bottom = true;
    }
    float s = dp(k);
    float row = math_max(1.0f, d->row_height);
    float total = d->count * row;
    float view = rml_element_client_height(n->el) / s;
    float scrolled = rml_element_scroll_top(n->el);
    float scroll = follow(w, d, total, view, scrolled / s);
    scroll = reveal(w, d, row, view, scroll);
    w->pending_index = d->reveal;
    scroll = math_clamp(scroll, 0.0f, math_max(total - view, 0.0f));
    if (fabsf(scroll * s - scrolled) >= 1) {
        rml_element_set_scroll_top(n->el, scroll * s);
    }
    w->value = total;
    float overscan = d->overscan > 0 ? d->overscan : DEFAULT_OVERSCAN;
    int from = math_max(0, (int)floorf((scroll - overscan) / row));
    /* Before the first layout the viewport is unknown; emit a screenful. */
    float extent = view > 0 ? view : 800;
    int to = math_min(d->count - 1, (int)ceilf((scroll + extent + overscan) / row));
    set_dp(w->body, "height", total);
    set_dp(w->body, "padding-top", from * row);
    push_node(k, n);
    *first = from;
    *last = to;
    return from <= to;
}

void kit_list_end(kit *k)
{
    assert(alast(k->stack)->kind == KIND_LIST);
    pop_node(k);
}
