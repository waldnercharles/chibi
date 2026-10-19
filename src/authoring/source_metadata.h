#ifndef CHIBI_AUTHORING_SOURCE_METADATA_H
#define CHIBI_AUTHORING_SOURCE_METADATA_H

#include "language/syntax.h"

/* Optional authoring sidecars bind to exact source bytes using the existing
 * source_hash contract. No executable or graph model is constructed. */
bool authoring_source_metadata_matches(const char *json, const char *text);
/* Returns malloc-owned JSON, or NULL for malformed metadata. */
char *authoring_source_metadata_bind(const char *json, const char *text);
/* Preserves identities and presentation while remapping token-start anchors.
 * Requires a matching source hash and identical complete CST structure and
 * leaf tokens, including comments and literal bytes. Invalid, stale or unrepresentable
 * anchors return NULL; callers may discard optional metadata. Limits: 8 MiB
 * source/JSON, 256 syntax/JSON levels and 262144 leaf tokens/anchors. */
char *authoring_source_metadata_remap(
    const char *json,
    const script_syntax *before,
    const script_syntax *after
);
/* Migration-only remapping from an exact composed byte-origin map, containing
 * strlen(before->text)+1 entries. Replaced anchors may collapse onto the
 * surviving replacement token; all old and resulting anchors must start tokens.
 * Stale, malformed or unrepresentable metadata returns NULL. */
char *authoring_source_metadata_remap_positions(
    const char *json,
    const script_syntax *before,
    const script_syntax *after,
    const int *positions
);

#endif
