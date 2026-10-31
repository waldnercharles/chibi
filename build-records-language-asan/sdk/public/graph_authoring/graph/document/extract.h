#ifndef CHIBI_EXTRACT_H
#define CHIBI_EXTRACT_H

#include "graph/model/behavior_asset.h"

/* Pure calculations or one connected execution chain with one entry/exit.
 * NULL means eligible; otherwise returns a stable, user-readable reason. */
const char *behavior_asset_can_extract_function(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count
);
/* Leaves the original untouched. Success returns an owned replacement asset
 * and the call ID in the original source graph. Failure clears both outputs. */
const char *behavior_asset_extract_function(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count,
    const char *name,
    behavior_asset **out_asset,
    uint64_t *out_call_id
);

/* Collapse to Macro: execution nodes with any number of execution wires in
 * and out, each inside pin they cross at becoming a tunnel pin of the macro
 * (Unreal). Latent nodes such as Delay may be included. Data crosses as it
 * does for functions. The same ownership rules as extraction apply. */
const char *behavior_asset_can_extract_macro(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count
);
const char *behavior_asset_extract_macro(
    const behavior_asset *asset,
    const graph *source,
    node *const *selection,
    int count,
    const char *name,
    behavior_asset **out_asset,
    uint64_t *out_call_id
);

#endif
