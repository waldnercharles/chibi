#ifndef CBLUEPRINT_EDITOR_EVAL_VIEW_H
#define CBLUEPRINT_EDITOR_EVAL_VIEW_H

#include "../core/flow.h"
#include "../core/graph.h"

typedef struct eval_view eval_view;

/* Owns a scratch arena and re-runs the core pull-eval so pin values are ready
 * to display. The editor borrows the graph; this is view-side only. */
eval_view *eval_view_new(void);
void eval_view_free(eval_view *v);

/* Re-evaluates every output pin for preview. The editor MUST skip this while
 * editor_preview_allowed() is false. */
void eval_view_refresh(eval_view *v, graph *g);
void eval_view_set_run(eval_view *v, const flow_run *run);
void eval_view_set_instance(eval_view *v, const behavior_instance *bi);
const char *eval_view_error(const eval_view *v);

/* Formatted value for a DATA output pin, or "" for EXEC/bad pins. */
const char *eval_view_output(const eval_view *v, const node *n, int pin);

/* Formatted value for a DATA input; NULL for EXEC or driven pins. */
const char *eval_view_input(const eval_view *v, const graph *g, const node *n, int pin);

#endif
