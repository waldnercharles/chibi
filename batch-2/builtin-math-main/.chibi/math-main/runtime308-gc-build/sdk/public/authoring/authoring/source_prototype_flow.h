#ifndef CHIBI_AUTHORING_SOURCE_PROTOTYPE_FLOW_H
#define CHIBI_AUTHORING_SOURCE_PROTOTYPE_FLOW_H

#include "authoring/source_navigation.h"

/* A value origin that reaches a Prototype parameter, including typed symbolic
 * references passed through locals or authored parameters. */
typedef struct
{
    int document;
    script_range range;
    uint64_t prototype;
} authoring_prototype_name;

/* Follows values into prototype parameters through locals, state, authored
 * function parameters and their defaults, across documents. The pass is
 * flow-insensitive: any value assigned to a symbol may reach each read of it.
 * Typed references carry catalog IDs; textual origins are resolved by name.
 * Constants that name no prototype are omitted. Returns an owned array
 * (support/array.h) the caller frees with afree. */
authoring_prototype_name *authoring_prototype_names(
    const authoring_document *,
    int count,
    const script_catalog *
);
#endif
