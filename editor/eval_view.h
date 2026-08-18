#ifndef CBLUEPRINT_EDITOR_EVAL_VIEW_H
#define CBLUEPRINT_EDITOR_EVAL_VIEW_H

#include "core/graph.h"

typedef struct eval_view eval_view;

/* Owns a scratch arena and re-runs the core pull-eval so pin values are ready
 * to display. The editor borrows the graph; this is view-side only. */
eval_view *eval_view_new(void);
void eval_view_free(eval_view *v);

/* Re-evaluates every output pin after a graph edit. Cheap (memoized); safe to
 * call every frame. */
void eval_view_refresh(eval_view *v, graph *g);

/* Formatted value for an output pin, or "" if the pin index is bad. */
const char *eval_view_output(const node *n, int pin);

/* Formatted value for an input pin that holds a literal constant; NULL if the
 * pin is driven by an edge (render nothing in that case). */
const char *eval_view_input(const graph *g, const node *n, int pin);

#endif
