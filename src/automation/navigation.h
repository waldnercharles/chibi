#ifndef CHIBI_AUTOMATION_NAVIGATION_H
#define CHIBI_AUTOMATION_NAVIGATION_H

#include "authoring/reference_index.h"
#include "automation/controller.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;
typedef struct yyjson_val yyjson_val;

/* How the catalog names one of the index's assets: its family, name and
 * content-relative path, which is NULL for an unresolved asset. */
typedef struct
{
    const char *kind;
    const char *name;
    const char *path;
} navigation_asset;

/* The project's references, and the catalog names of their assets. */
typedef struct
{
    const reference_index *index;
    const navigation_asset *assets; /* one per index asset */
} navigation;

/* find_references and call_hierarchy write their result into `result` and
 * return a CHIBI_AUTOMATION status, with the reason in `error`. A request
 * selects an asset ({"asset":{"kind","name"}}), and within it a graph
 * ("graph" or "graph_id"), a "variable", a function's or model's "parameter" or
 * a "local"; or an "event"; or a "component" and its "field". */
int automation_find_references(
    const navigation *nav,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t error_size
);
int automation_call_hierarchy(
    const navigation *nav,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t error_size
);
/* Adds per-role use counts ("uses") to an inspected graph asset: to itself,
 * its function and macro summaries in "graphs", and its "variables". */
void automation_add_use_counts(const navigation *nav, int asset, yyjson_mut_doc *doc, yyjson_mut_val *result);

#endif
