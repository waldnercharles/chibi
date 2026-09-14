#ifndef CHIBI_AUTOMATION_GRAPH_OPTIMIZE_H
#define CHIBI_AUTOMATION_GRAPH_OPTIMIZE_H

#include "automation/controller.h"

#include <stddef.h>

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;
typedef struct yyjson_val yyjson_val;

/* Run the shared optimizer on a bounded asset clone and return its proposal
 * and per-candidate report. Publication and dry-run policy belong to the controller. */
int automation_graph_optimize(
    const char *source_json,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited_json,
    char *error,
    size_t error_size
);

#endif
