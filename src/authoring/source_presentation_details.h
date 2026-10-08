#ifndef CHIBI_AUTHORING_SOURCE_PRESENTATION_DETAILS_H
#define CHIBI_AUTHORING_SOURCE_PRESENTATION_DETAILS_H

#include "authoring/source_presentation.h"

#include <yyjson.h>

typedef struct
{
    uint64_t previous, current;
} source_presentation_identity_map;

/* Optional complete snapshots. The caller checks the exact source hash and
 * validates all other metadata before applying any part of the sidecar. */
bool source_presentation_details_write(yyjson_mut_doc *, yyjson_mut_val *root, const authoring_source_presentation *);
bool source_presentation_details_validate(yyjson_val *root, const authoring_source_presentation *);
/* Requires successful validation against this unchanged presentation. */
void source_presentation_details_apply(yyjson_val *root, authoring_source_presentation *);
/* Missing mapped owners/edges are discarded; topology is never constructed.
 * A NULL map preserves exact origin identities. A supplied map is exhaustive. */
bool source_presentation_details_carry(
    const authoring_source_presentation *previous,
    authoring_source_presentation *current,
    const source_presentation_identity_map *,
    int count
);
#endif
