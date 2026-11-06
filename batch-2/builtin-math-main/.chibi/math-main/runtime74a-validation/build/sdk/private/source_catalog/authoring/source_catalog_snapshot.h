#ifndef CHIBI_AUTHORING_SOURCE_CATALOG_SNAPSHOT_H
#define CHIBI_AUTHORING_SOURCE_CATALOG_SNAPSHOT_H
#include "capabilities/catalog_snapshot.h"
#include "support/arena.h"

/* Comparison facts are captured in the same construction as the owning
 * default snapshots. Never reconstruct them using later global descriptors. */
bool source_catalog_default_key(const script_catalog *, unsigned char **);
/* Exact semantic metadata; capability category/keywords/description are display
 * text only. Each snapshot still owns its current display strings. */
bool source_catalog_metadata_equal(const script_catalog *, const script_catalog *);
struct source_catalog;
bool source_catalog_same(const struct source_catalog *, const struct source_catalog *);
#endif
