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

/* Re-evaluates every output pin for preview. The editor MUST skip this while a
 * run owns pin storage. */
void eval_view_refresh(eval_view *v, graph *g);
/* Borrow the project catalog; linked preview generations are owned by the view. */
void eval_view_set_catalog(eval_view *v, const behavior_asset *const *assets, int count);
void eval_view_set_run(eval_view *v, const flow_run *run);
void eval_view_set_instance(eval_view *v, const behavior_instance *bi);
const char *eval_view_error(const eval_view *v);

/* Live: a run's values stand in for the preview, those of the inspected run
 * or else of the instance's latest completed runs (Unreal's watches while
 * the game plays). A pin none of them recorded has no value. */
bool eval_view_live(const eval_view *v);
/* A pure output, which records nothing itself, reads as what a consumer of
 * it in `g` received. */
int eval_view_live_value(const eval_view *v, const graph *g, const node *n, int output, int pin, pin_value *out);
/* The value as the host's formatter, else pin_value_to_string, writes it;
 * valid until eight more are formatted. */
const char *eval_view_format_value(const eval_view *v, const pin_value *value);

/* Formatted value for a DATA output pin, or "" for EXEC/bad pins. */
const char *eval_view_output(const eval_view *v, const node *n, int pin);

/* Formatted value for a DATA input; NULL for EXEC or driven pins. */
const char *eval_view_input(const eval_view *v, const graph *g, const node *n, int pin);

#endif
