#ifndef CHIBI_CODE_PRINT_H
#define CHIBI_CODE_PRINT_H

#include "graph/model/behavior_asset.h"
#include "support/asset_value.h"

/* The returned readable source owns every executable declaration. Identities,
 * layout and debugger annotations are added separately by the source editor. */
char *code_print_behavior(const behavior_asset *asset, char *error, size_t size);
/* Optional JSON presentation metadata with exact emitted source anchors. */
char *code_print_behavior_with_metadata(
    const behavior_asset *asset,
    char **metadata,
    char *error,
    size_t size
);

/* Semantic code label for an executable relationship root; false for an
 * ordinary event. Presentation does not expose generated dispatch names. */
bool code_print_reactive_event(
    const behavior_asset *asset,
    const graph *graph,
    const node *event,
    char *out,
    size_t size
);

/* Systems share executable printing with behaviors, and own their query header. */
char *code_print_system_with_metadata(
    const behavior_asset *asset,
    const av_value *query,
    char **metadata,
    char *error,
    size_t size
);

#endif
