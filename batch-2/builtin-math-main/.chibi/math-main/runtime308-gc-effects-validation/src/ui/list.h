#ifndef CHIBI_UI_LIST_H
#define CHIBI_UI_LIST_H

/* Virtualized lists after Virtuoso (docs/editor-ui-references.md), for any
 * scrolling element: a scroller whose first child, the spacer, is as tall
 * as every row, of which only a window, the rows in view and an overscan
 * beyond each edge, is shown, starting at the spacer's padding-top (the
 * spacer's box-sizing is border-box). The
 * window keeps its length while the list scrolls, so a data-for over it
 * reuses its elements and only their values change. A following list (a
 * log) stays at the bottom, the scroller's padding below the last row, as
 * rows arrive while it was within 4 dp of it; reveal scrolls the least that
 * brings a row into view. Rows are one height, or give their tops when they
 * differ, as a list's group headings and items do (Virtuoso's item sizes).
 *
 * projects/studio's List widget is such a scroller; the kit's kit_list is
 * one too. */

#include "ui/rml.h"

#include <stdbool.h>

typedef struct ui_list
{
    int count; /* rows */
    float row; /* row height in dp, the shortest with tops; 0 reads --row */
    /* Each row's top in dp and then the end of the last (count + 1), or
       NULL when every row is `row` tall. */
    const float *tops;
    int reveal;     /* a row to scroll into view once it changes; -1, none */
    float overscan; /* dp shown beyond each edge of the view; 0, 120 */
    bool follow;    /* stays at the bottom as rows arrive while it was there */
} ui_list;

/* What a list keeps between calls; start it as UI_LIST_STATE_INIT. */
typedef struct ui_list_state
{
    int revealed;        /* the reveal last requested */
    int reveal_row;      /* the row awaiting content layout */
    bool reveal_pending; /* its scroll was clamped before layout */
    float total;         /* the content height last placed, in dp */
    float scroll;        /* where the view was left, in dp */
    bool at_bottom;      /* the view was at the bottom of it */
} ui_list_state;

#define UI_LIST_STATE_INIT { .revealed = -1, .at_bottom = true }

/* The window of `scroller`'s rows to show: sizes its spacer for every row,
 * scrolls for reveal and follow, and starts the window at the first of the
 * rows returned, which `*first` names. Returns how many rows the window
 * holds: as many as fill the view and the overscan, fewer only when the list
 * has fewer, and the same for every scroll position. Before the scroller is
 * laid out it fills 800 dp. */
int ui_list_window(rml_element *scroller, const ui_list *list, ui_list_state *state, int *first);

#endif
