/* Virtualized list windows (list.h), the kit's kit_list ported to any
 * element: what Virtuoso computes from the scroll position, with rows of one
 * height or of the heights their tops give. */
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

static float top_of(const ui_list *list, float row, int i)
{
    return list->tops ? list->tops[i] : (float)i * row;
}

/* The row at `y` dp from the top, before the first or past the last. */
static int row_at(const ui_list *list, float row, float y)
{
    if (!list->tops) {
        return (int)floorf(y / row);
    }
    int low = 0, high = list->count;
    while (low < high) {
        int mid = (low + high + 1) / 2;
        if (list->tops[mid] <= y) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
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

/* Scrolls the least to bring a newly revealed row into view, its top
   `above` dp below the scroller's padding edge. */
static float reveal(const ui_list *list, float row, float above, float view, float scroll)
{
    if (list->reveal < 0 || list->reveal >= list->count) {
        return scroll;
    }
    float top = above + top_of(list, row, list->reveal),
          bottom = above + top_of(list, row, list->reveal + 1);
    if (top < scroll) {
        return top;
    }
    if (bottom > scroll + view) {
        return bottom - view;
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
    /* The scroller's padding scrolls with the rows, so its end is past it. */
    float padding = rml_element_box_size(scroller, RML_BOX_PADDING).y -
                    rml_element_box_size(scroller, RML_BOX_CONTENT).y;
    float above = rml_element_absolute_offset(scroller, RML_BOX_CONTENT).y -
                  rml_element_absolute_offset(scroller, RML_BOX_PADDING).y;
    float rows = top_of(list, row, list->count);
    float total = rows + padding / ratio;
    float view = rml_element_client_height(scroller) / ratio;
    float scrolled = rml_element_scroll_top(scroller);
    float scroll = follow(state, list, total, view, scrolled / ratio);
    if (list->reveal >= 0 && list->reveal < list->count &&
        list->reveal != state->revealed) {
        state->reveal_row = list->reveal;
        state->reveal_pending = true;
    }
    state->revealed = list->reveal;
    ui_list requested = *list;
    requested.reveal = state->reveal_pending ? state->reveal_row : -1;
    scroll = reveal(&requested, row, above / ratio, view, scroll);
    scroll = fminf(fmaxf(scroll, 0), fmaxf(total - view, 0));
    if (fabsf(scroll * ratio - scrolled) >= 1) {
        rml_element_set_scroll_top(scroller, scroll * ratio);
    }
    state->scroll = rml_element_scroll_top(scroller) / ratio;
    /* A growing spacer may still have its previous scroll range until Rml
     * lays it out. Retry the reveal instead of consuming the clamped scroll. */
    state->reveal_pending = state->reveal_pending && state->reveal_row < list->count &&
                            fabsf(state->scroll - scroll) >= 1 / ratio;
    state->total = total;
    float overscan = list->overscan > 0 ? list->overscan : DEFAULT_OVERSCAN;
    float extent = view > 0 ? view : UNLAID_VIEW;
    int length = (int)ceilf((extent + 2 * overscan) / row) + 1;
    length = length < list->count ? length : list->count;
    int start = row_at(list, row, scroll - overscan);
    start = start > list->count - length ? list->count - length : start;
    *first = start > 0 ? start : 0;
    set_dp(spacer, "height", rows);
    set_dp(spacer, "padding-top", top_of(list, row, *first));
    return length;
}
