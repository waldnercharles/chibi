#ifndef CHIBI_EDITOR_EVAL_VIEW_H
#define CHIBI_EDITOR_EVAL_VIEW_H

#include "graph/model/graph.h"
#include "graph/runtime/flow.h"

typedef struct eval_view eval_view;

/* Return true after formatting a value. Context is borrowed until replaced. */
typedef bool (*editor_value_formatter)(
    const pin_value *value,
    const behavior_instance *instance,
    char *buf,
    size_t cap,
    void *user
);
void eval_view_set_formatter(eval_view *v, editor_value_formatter formatter, void *user);

/* Owns a scratch arena and re-runs the core pull-eval so pin values are ready
 * to display. The editor borrows the graph; this is view-side only. */
eval_view *eval_view_new(void);
void eval_view_free(eval_view *v);

/* Re-evaluates every output pin for preview. The editor MUST skip this while
 * editor_preview_allowed() is false. */
void eval_view_refresh(eval_view *v, graph *g);
/* Borrow the project catalog; linked preview generations are owned by the view. */
void eval_view_set_catalog(eval_view *v, const behavior_asset *const *assets, int count);
void eval_view_set_run(eval_view *v, const flow_run *run);
void eval_view_set_instance(eval_view *v, const behavior_instance *bi);
const char *eval_view_error(const eval_view *v);
/* The graph previews run: g, or its linked and lowered copy when the asset
 * references libraries or holds component nodes. NULL when linking fails. */
graph *eval_view_graph(eval_view *v, graph *g);

/* Formatted value for a DATA output pin, or "" for EXEC/bad pins. */
const char *eval_view_output(const eval_view *v, const node *n, int pin);

/* Formatted value for a DATA input; NULL for EXEC or driven pins. */
const char *eval_view_input(const eval_view *v, const graph *g, const node *n, int pin);

#endif
