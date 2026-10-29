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
/* VALUE requests a lazy result before its consumers have been connected.
 * BRANCH requests boolean execution outcomes; NONE uses the graph context.
 * Assignments always retain ACTION semantics. */
node *behavior_expression_add_mode(
    behavior_asset *asset,
    graph *caller,
    const char *source,
    graph_expression_mode mode,
    behavior_expression_error *error
);
/* Explicitly reselects the execution shape: NONE reinfers from current graph
 * connections, VALUE keeps a lazy result, BRANCH requests execution outcomes. */
int behavior_expression_refresh_mode(
    behavior_asset *asset,
    node *call,
    graph_expression_mode mode,
    behavior_expression_error *error
);
int behavior_expression_set(
    behavior_asset *asset,
    node *call,
    const char *source,
    behavior_expression_error *error
);
/* Renames an input in the source and on the call, keeping its wire or binding. */
int behavior_expression_rename_input(
    behavior_asset *asset,
    node *call,
    const char *from,
    const char *to,
    behavior_expression_error *error
);
/* Names of the call's inputs a wire or binding feeds, as a support/array.h
 * array; after a source edit, keep_unfed leaves those no longer fed. */
char **behavior_expression_fed_inputs(const graph *g, const node *call);
void behavior_expression_keep_unfed(const graph *g, const node *call, char ***names);
void behavior_expression_free_names(char **names);
/* Regenerates the established contract; a VALUE remains lazy even before its
 * consumers are connected. Graph edits requesting reinference use refresh_mode
 * with NONE. Source edits also preserve VALUE until explicitly reinferred. */
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
 * NULL when the syntax has no spelling for it. The same spelling covers the
 * behavior, shader and field node that implement an operation. */
#define BEHAVIOR_EXPRESSION_PREFIX_BINDING 25
const char *behavior_expression_spelling(const node_desc *desc, int *binding);
/* The typographic form of an operator spelling: × for *, ≤ for <=. */
const char *behavior_expression_symbol(const char *spelling);
/* A function spelling's argument count, or -1 for other nodes. */
int behavior_expression_arity(const node_desc *desc);
/* The node input that takes a function's argument; GLSL argument order can
 * differ from a node's input order, as in smoothstep(edge0, edge1, x). */
int behavior_expression_argument_input(const node_desc *desc, int argument);
/* The index-th function spelling a node implements, such as log and then ln
 * for Logarithm; NULL past the last. */
const char *behavior_expression_alias(const node_desc *desc, int index);
/* A value a formatted expression reads, which an editor may name in its own
 * style: text [start, start + length) spells `read`. */
typedef struct behavior_expression_read
{
    int start;
    int length;
    pin_binding read;
} behavior_expression_read;

/* Writes the value input `pin` of target receives, as Behavior Expression
 * grammar with typographic operators, such as `Self.Movement.speed × 2`:
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
/* Replaces each tree of at least min_nodes spellable math operations in g
 * with one expression, so dense arithmetic reads as formulas. A tree grows
 * through nodes whose only use is a plain wire into another math node; a
 * value used more than once ends its tree and becomes an input of each use.
 * Returns the number of expressions created, or -1 with error set. */
int behavior_expression_collapse_math(
    behavior_asset *asset,
    graph *g,
    int min_nodes,
    behavior_expression_error *error
);

#endif
