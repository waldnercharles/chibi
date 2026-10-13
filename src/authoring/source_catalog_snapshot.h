#ifndef CHIBI_AUTHORING_SOURCE_CATALOG_SNAPSHOT_H
#define CHIBI_AUTHORING_SOURCE_CATALOG_SNAPSHOT_H
#include "capabilities/catalog.h"
#include "support/arena.h"

bool source_catalog_snapshot(arena *, const script_catalog *, script_catalog *);
/* Comparison facts are captured in the same construction as the owning
 * default snapshots. Never reconstruct them using later global descriptors. */
bool source_catalog_default_key(const script_catalog *, unsigned char **);
bool source_catalog_metadata_equal(const script_catalog *, const script_catalog *);
struct source_catalog;
bool source_catalog_same(const struct source_catalog *, const struct source_catalog *);
#endif
