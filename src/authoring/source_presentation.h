#ifndef CHIBI_AUTHORING_SOURCE_PRESENTATION_H
#define CHIBI_AUTHORING_SOURCE_PRESENTATION_H

#include "graph/model/behavior_asset.h"
#include "language/semantic.h"

typedef struct authoring_source_presentation authoring_source_presentation;
typedef enum
{
    SOURCE_PRESENTATION_SYMBOL,
    SOURCE_PRESENTATION_FUNCTION,
    SOURCE_PRESENTATION_EXPRESSION,
    SOURCE_PRESENTATION_STATEMENT,
    SOURCE_PRESENTATION_QUERY,
    SOURCE_PRESENTATION_BINDING,
} source_presentation_kind;
typedef struct
{
    uint64_t identity, occurrence, node_occurrence, semantic, capability;
    uint64_t graph, node, pin;
    source_presentation_kind kind;
    script_range range;
    int function, record, operation, entry;
    int called_function;
    int display_class;
    int node_kind, node_record;
    uint64_t port_name, function_name;
    int port_index, result_index, parameter;
    bool output, flow;
    uint64_t member;
    unsigned effects;
    script_type type;
    script_operator op;
    uint64_t owner, parent;
    bool writable, disabled;
} source_presentation_origin;

/* Copies resolved meaning into a graph view, without parsing or executing it.
 * Descriptors have no execution callbacks. The source module remains the sole
 * execution authority. Invalid analyses retain their available authoring facts.
 * Optional positions/comments are carried from a previous semantic identity.
 * Returns NULL when a draft has no source text or named program root. */
authoring_source_presentation *authoring_source_presentation_new(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *previous
);
/* Independently owned view copy, including dynamic descriptors and defaults. */
authoring_source_presentation *authoring_source_presentation_clone(const authoring_source_presentation *);
authoring_source_presentation *authoring_source_presentation_retain(authoring_source_presentation *);
void authoring_source_presentation_free(authoring_source_presentation *);
/* Borrowed view. Graph clones borrow its dynamic descriptors: retain this
 * generation until every borrowed view has been released. Use the dedicated
 * presentation clone rather than registry serialization. Graph mutations do not
 * change source or execution; adopt edits through source replacement/reanalysis. */
behavior_asset *authoring_source_presentation_view(const authoring_source_presentation *);
/* Consumes a uniquely owned generation and transfers its independently owned
 * graph view. Source origins/text are released; use retained generations for
 * editing/navigation instead. The caller frees the view with behavior_asset_free. */
behavior_asset *authoring_source_presentation_take_view(authoring_source_presentation *);
const char *authoring_source_presentation_text(const authoring_source_presentation *);
const char *authoring_source_presentation_identity(const authoring_source_presentation *);
/* Checks resolved record pairing as well as exact document identity/text. */
bool authoring_source_presentation_matches(
    const authoring_source_presentation *,
    const script_analysis *,
    const script_catalog *
);
/* Local authored callee graph, or zero for a native/imported/non-call origin. */
uint64_t authoring_source_presentation_call_graph(
    const authoring_source_presentation *,
    uint64_t graph,
    uint64_t node
);
int authoring_source_presentation_origin_count(const authoring_source_presentation *);
const source_presentation_origin *authoring_source_presentation_origin_at(
    const authoring_source_presentation *,
    int index
);
const source_presentation_origin *authoring_source_presentation_origin(
    const authoring_source_presentation *,
    uint64_t graph,
    uint64_t node,
    uint64_t pin
);
/* Editor state: in-session layout, breakpoints, watches and comments, as an
 * owned JSON snapshot bound to the exact source text and semantic origin IDs.
 * Projects never store it. Restore takes a snapshot and ignores a malformed
 * or stale one before any presentation mutation. */
char *authoring_source_presentation_metadata(const authoring_source_presentation *);
bool authoring_source_presentation_restore(authoring_source_presentation *, const char *json);
/* Replaces the exact source region identified by this generation. Returns
 * owned text for canonical reanalysis; never adopts an unchecked graph edit. */
char *authoring_source_presentation_replace(
    const authoring_source_presentation *,
    const source_presentation_origin *,
    const char *text
);
#endif
