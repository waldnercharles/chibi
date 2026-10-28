#ifndef CHIBI_AUTHORING_SOURCE_COSTS_H
#define CHIBI_AUTHORING_SOURCE_COSTS_H

#include "authoring/source_collection.h"

typedef enum
{
    AUTHORING_COST_REPORT_NONE,
    AUTHORING_COST_REPORT_TEXT,
    AUTHORING_COST_REPORT_JSON
} authoring_cost_report_mode;

/* Render the prepared sources' existing static cost facts through the packaged
 * Chibi reporter. No authored game function runs and no file/cache is written.
 * The caller owns the returned text (free). Returns NULL with an error for an
 * invalid source collection or a reporter failure. Sources remain caller-owned.
 * Native authoring hosts provide rendering; source_prepare.h exposes the raw
 * per-function facts independently of this formatter. Reports cover finalized
 * function bodies and their callees, not binding/subscription/query evaluator
 * roots or backend/activation setup. */
char *authoring_sources_cost_report(const authoring_sources *, bool json, char *error, size_t size);

#endif
