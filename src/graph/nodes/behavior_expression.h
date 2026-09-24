#ifndef CHIBI_BEHAVIOR_EXPRESSION_H
#define CHIBI_BEHAVIOR_EXPRESSION_H

#include "graph/model/behavior_asset.h"

typedef struct
{
    int offset;
    int length;
    char message[192];
} behavior_expression_error;

/* Behavior expressions infer their data inputs and expose one Result. Generated
 * function graphs are inspectable through the call's reference_id. Failed
 * parsing/validation leaves the entire authored asset unchanged. */
node *behavior_expression_add(
    behavior_asset *asset,
    graph *caller,
    const char *source,
    behavior_expression_error *error
);
int behavior_expression_set(
    behavior_asset *asset,
    node *call,
    const char *source,
    behavior_expression_error *error
);
int behavior_expression_refresh(behavior_asset *asset, node *call, behavior_expression_error *error);
int behavior_expression_repair(behavior_asset *asset, node *call);
/* An expression function's body is derived from its source and first call.
 * Rebuilding reproduces its IDs from the one before its entry node and leaves
 * nodes for the editor to arrange. Returns a detached graph, or NULL. */
graph *behavior_expression_rebuild(const behavior_asset *asset, const graph *generated);
/* Loading fills an expression function whose document omits its body; the
 * loader then advances the asset's ID counter past its IDs. */
int behavior_expression_restore(behavior_asset *asset, graph *generated);
const char *behavior_expression_source(const behavior_asset *asset, const node *call);

/* How Behavior Expression spells a node: an infix operator with its binding
 * power (higher binds tighter), a prefix operator bound at
 * BEHAVIOR_EXPRESSION_PREFIX_BINDING, or a function call with binding 0.
 * NULL when the syntax has no spelling for it. */
#define BEHAVIOR_EXPRESSION_PREFIX_BINDING 25
const char *behavior_expression_spelling(const node_desc *desc, int *binding);
/* A value a formatted expression reads, which an editor may name in its own
 * style: text [start, start + length) spells `read`. */
typedef struct behavior_expression_read
{
    int start;
    int length;
    pin_binding read;
} behavior_expression_read;

/* Writes the value input `pin` of target receives, as Behavior Expression
 * grammar with typographic operators, such as `Self.GameMovement.speed × 2`:
 * its wired source with the inline tree behind it, or its binding or literal.
 * Edge conversions appear as calls. Truncated to cap. The text presents graph
 * data for reading; it is never parsed back. When reads is not NULL, the
 * bindings, variables, Delta Time and Self component reads the text spells
 * are appended to *reads (support/array.h) in text order. */
void behavior_expression_format(
    const graph *g,
    const node *target,
    int pin,
    char *buf,
    size_t cap,
    behavior_expression_read **reads
);

/* A source-to-source authoring transform for a connected, pure numeric
 * calculation. The selected graph nodes are replaced by one expression call;
 * incoming wires and dormant bindings are retained on its inputs. */
const char *behavior_expression_can_convert_selection(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count
);
node *behavior_expression_convert_selection(
    behavior_asset *asset,
    graph *source,
    node *const *selection,
    int count,
    behavior_expression_error *error
);

#endif
