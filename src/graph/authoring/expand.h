#ifndef CHIBI_GRAPH_EXPAND_H
#define CHIBI_GRAPH_EXPAND_H

#include "graph/model/behavior_asset.h"

#include <stdint.h>

/* Unreal's Expand Node: a call to one of the asset's own macros or functions
 * becomes a copy of the body it calls, wired as the call was; the called graph
 * stays. A function with execution pins expands only when every execution
 * output in its body is wired, since its call continues wherever it ends.
 * NULL means `call` can expand; otherwise a stable, user-readable reason. */
const char *behavior_asset_can_expand_call(
    const behavior_asset *asset,
    const graph *caller,
    const node *call
);
/* Leaves the original untouched. Success returns an owned replacement asset
 * and appends the copied nodes' IDs to *out_ids (support/array.h). */
const char *behavior_asset_expand_call(
    const behavior_asset *asset,
    const graph *caller,
    const node *call,
    behavior_asset **out_asset,
    uint64_t **out_ids
);

#endif
