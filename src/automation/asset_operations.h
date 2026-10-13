#ifndef CHIBI_AUTOMATION_ASSET_OPERATIONS_H
#define CHIBI_AUTOMATION_ASSET_OPERATIONS_H

#include "assets/model_import.h"
#include "graph/model/behavior_asset.h"
#include "support/asset_value.h"

#include <stdbool.h>
#include <stddef.h>
#include <yyjson.h>

/* JSON transport adapters. Shared asset modules own the actual operations. */
bool automation_asset_operation(const char *operation);
int automation_asset_handle(
    const char *project,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *response,
    char *error,
    size_t size
);
yyjson_mut_val *automation_asset_value(yyjson_mut_doc *, const av_value *);
void automation_model_report(yyjson_mut_doc *, yyjson_mut_val *, const model_import_report *);
void automation_shader_report(yyjson_mut_doc *, yyjson_mut_val *, const behavior_asset *, bool cpu_sample);
int automation_resources_attach(
    const char *project,
    yyjson_val *request,
    yyjson_mut_doc *,
    yyjson_mut_val *,
    char *error,
    size_t size
);

#endif
