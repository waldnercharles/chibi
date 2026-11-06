#ifndef CHIBI_CAPABILITIES_CATALOG_SNAPSHOT_H
#define CHIBI_CAPABILITIES_CATALOG_SNAPSHOT_H
#include "capabilities/catalog.h"
#include "support/arena.h"

/* Copies declarations, names and default values into caller-owned storage.
 * The source may retire on return. A false result leaves partial arena storage
 * for the caller to release; the output is not a valid catalog. */
bool script_catalog_snapshot(arena *, const script_catalog *, script_catalog *);
#endif
