/* Virtualized list windows (list.h), the kit's kit_list ported to any
 * element: what Virtuoso computes from the scroll position, with rows of one
 * height. */
#include "ui/list.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* dp from the bottom that still counts as at it, for following. */
#define BOTTOM_THRESHOLD 4.0f
#define DEFAULT_OVERSCAN 120.0f
/* What a list fills before its view has a height. */
#define UNLAID_VIEW 800.0f

static float row_height(rml_element *scroller, const ui_list *list)
{
    if (list->row > 0) {
        return list->row;
    }
    char value[32];
    rml_element_property(scroller, "--row", value, sizeof value);
    float row = strtof(value, NULL);
    assert(isfinite(row) && row > 0);
    return row;
}

/* Pinned while the view is at the bottom. Where it is counts only once it
 * moved since the last call: a scroll set before the content grew stops at
 * the old bottom until the content lays out, and is still pinned. */
static float follow(ui_list_state *state, const ui_list *list, float total, float view, float scroll)
{
    if (fabsf(scroll - state->scroll) >= 0.5f) {
        state->at_bottom = scroll >= fmaxf(state->total - view, 0) - BOTTOM_THRESHOLD;
    }
    return list->follow && state->at_bottom ? fmaxf(total - view, 0) : scroll;
}

/* Scrolls the least to bring a newly revealed row into view. */
static float reveal(const ui_list_state *state, const ui_list *list, float row, float view, float scroll)
{
    if (list->reveal < 0 || list->reveal >= list->count ||
        list->reveal == state->revealed) {
        return scroll;
    }
    float top = list->reveal * row;
    if (top < scroll) {
        return top;
    }
    if (top + row > scroll + view) {
        return top + row - view;
    }
    return scroll;
}

/* Setting a style to the value it holds lays nothing out
   (rmlui-same-style.patch). */
static void set_dp(rml_element *el, const char *property, float value)
{
    char text[32];
    snprintf(text, sizeof text, "%.2fdp", value);
    rml_element_set_property(el, property, text);
}

int ui_list_window(rml_element *scroller, const ui_list *list, ui_list_state *state, int *first)
{
    rml_element *spacer = rml_element_first_child(scroller);
    assert(spacer && list->count >= 0);
    rml_context *context = rml_element_context(scroller);
    float ratio = context ? rml_context_dp_ratio(context) : 1;
    ratio = ratio > 0 ? ratio : 1;
    float row = row_height(scroller, list);
    float total = list->count * row;
    float view = rml_element_client_height(scroller) / ratio;
    float scrolled = rml_element_scroll_top(scroller);
    float scroll = follow(state, list, total, view, scrolled / ratio);
    scroll = reveal(state, list, row, view, scroll);
    state->revealed = list->reveal;
    scroll = fminf(fmaxf(scroll, 0), fmaxf(total - view, 0));
    if (fabsf(scroll * ratio - scrolled) >= 1) {
        rml_element_set_scroll_top(scroller, scroll * ratio);
    }
    state->scroll = rml_element_scroll_top(scroller) / ratio;
    state->total = total;
    float overscan = list->overscan > 0 ? list->overscan : DEFAULT_OVERSCAN;
    float extent = view > 0 ? view : UNLAID_VIEW;
    int length = (int)ceilf((extent + 2 * overscan) / row) + 1;
    length = length < list->count ? length : list->count;
    int start = (int)floorf((scroll - overscan) / row);
    start = start > list->count - length ? list->count - length : start;
    *first = start > 0 ? start : 0;
    set_dp(spacer, "height", total);
    set_dp(spacer, "padding-top", *first * row);
    return length;
}
