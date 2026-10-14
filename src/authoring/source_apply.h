#ifndef CHIBI_AUTHORING_SOURCE_APPLY_H
#define CHIBI_AUTHORING_SOURCE_APPLY_H

#include "authoring/source_prepare.h"

typedef struct source_registry source_registry;

/* Applies one named registration; changing a declaration's name requires the
 * caller's explicit rename operation. Publication preserves compatible state.
 * Begin runs at the runtime's next permitted event boundary. The live
 * registration retains its module independently of this snapshot. */
int authoring_source_apply(
    source_registry *,
    const char *registration,
    const authoring_source *,
    bool restart,
    char *error,
    size_t error_size
);

#endif
