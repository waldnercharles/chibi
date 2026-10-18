#ifndef CHIBI_SOURCE_AUTHORITY_H
#define CHIBI_SOURCE_AUTHORITY_H

#include "execution/authority.h"
#include "runtime/behaviors/source.h"

/* Explicit host request origin; ordinary dispatch, Begin and maintenance never
 * inherit the token of an ambient running callback. */
int source_dispatch_tagged(
    source_registry *,
    execution_dispatch *,
    ecs_entity,
    const char *event,
    const pin_value *,
    int count,
    double now,
    double delta
);
int source_dispatch_all_tagged(
    source_registry *,
    execution_dispatch *,
    const char *event,
    const pin_value *,
    int count,
    double now,
    double delta
);

#endif
