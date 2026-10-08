#ifndef CHIBI_AUTHORING_SOURCE_CATALOG_SNAPSHOT_H
#define CHIBI_AUTHORING_SOURCE_CATALOG_SNAPSHOT_H
#include "capabilities/catalog.h"
#include "support/arena.h"

bool source_catalog_snapshot(arena *, const script_catalog *, script_catalog *);
#endif
