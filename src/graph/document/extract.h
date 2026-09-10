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

#endif
