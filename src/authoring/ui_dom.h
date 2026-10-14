#ifndef CHIBI_UI_DOM_H
#define CHIBI_UI_DOM_H
#include "support/arena.h"
#include "types/array.h"

#include <stdbool.h>
typedef struct ui_dom ui_dom;
typedef struct rml_context rml_context;
typedef struct
{
    const char *key, *parent, *tag, *id, *classes, *text;
    int depth;
    double x, y, width, height;
    /* Geometry is physical pixels; dp_ratio converts it to authored dp. */
    double content_width, content_height, dp_ratio;
    double margin_top, margin_right, margin_bottom, margin_left;
    double border_top, border_right, border_bottom, border_left;
    double padding_top, padding_right, padding_bottom, padding_left;
    const char *source_asset, *source_key;
    int source_line, source_start;
    bool visible;
} ui_dom_row;
typedef struct
{
    const char *name, *value, *path, *rule;
    int line, specificity;
} ui_dom_property;
typedef struct
{
    bool ok, complete;
    int revision, total;
    const char *error;
} ui_dom_status;
typedef struct
{
    const char *key;
    bool selected;
} ui_dom_context_row;
/* Frame-thread confined inspection of a borrowed existing RmlUi context. Retains at most 4096 SDK weak references; retired elements never resolve
 * to reused addresses. Tree walk <=4096 nodes/depth64, pages
 * <=256. Keys are monotonic service-owned weak identities and resolve afresh. Results
 * copy to caller arena. Context must be cleared before its owner destroys it. */
ui_dom *ui_dom_new();
void ui_dom_free(ui_dom *);
void ui_dom_context(ui_dom *, rml_context *);
/* Registers at most 16 named borrowed contexts, key <=127 bytes. Re-registering
 * the same key/pointer is a no-op and never changes user selection. Unregister
 * before context destruction; selected retirement restores debug pseudo states
 * and invalidates all element keys, then selects application if still live. */
bool ui_dom_register(ui_dom *, const char *key, rml_context *);
void ui_dom_unregister(ui_dom *, const char *key);
ui_dom_status ui_dom_contexts(ui_dom *, arena *, pin_array **);
ui_dom_status ui_dom_select(ui_dom *, const char *key);
ui_dom_status ui_dom_query(ui_dom *, int first, int count, arena *, pin_array **);
ui_dom_status ui_dom_inspect(
    ui_dom *,
    const char *key,
    const char *mode,
    int first,
    int count,
    arena *,
    pin_array **,
    ui_dom_row *
);
ui_dom_status ui_dom_pick(ui_dom *, double x, double y, const char *ignore_selector, arena *, ui_dom_row *);
/* Debug pseudo states only: hover/active/focus/checked. Re-resolves a live key;
 * it changes no source document. Caller owns which pseudo states to toggle. Service teardown/context rebinding
 * restores states still forced by this service. */
/* Resolves one literal length unit against a live element. Axis x/y chooses the
 * percentage base (containing width/height); variables and calc are refused. */
ui_dom_status ui_dom_unit(ui_dom *, const char *key, const char *unit, const char *axis, double *pixels);
/* Measures a single line in a registered context without changing selection.
 * Size and width are dp before element transforms. Family/style come from the
 * live selector; requested size/weight override those two font properties.
 * False (width zero) until the context, selector and font are available. */
bool ui_dom_measure_text(
    ui_dom *,
    const char *context,
    const char *selector,
    const char *text,
    float size,
    int weight,
    float *width
);
ui_dom_status ui_dom_pseudo(ui_dom *, const char *key, const char *pseudo, bool on);
#endif
