#ifndef CHIBI_MODEL_EXPORT_H
#define CHIBI_MODEL_EXPORT_H
#include "runtime/models/model_bake.h"

#include <stddef.h>
typedef struct
{
    const char *asset, *scope;
    uint64_t parameter_id;
    float value[4];
    int width;
} model_export_override;
typedef struct
{
    model_bake_options bake;
    const model_export_override *overrides;
    int override_count;
} model_export_options;
typedef struct
{
    int triangles;
    uint64_t samples, work;
    float cell_diagonal, max_residual;
    model_bounds bounds;
} model_export_summary;
/* Saved content only; never publishes a model or changes source. Destination
 * must be a new directory whose parent exists. Failure removes staged output. */
bool model_export_bundle(
    const char *content_root,
    const char *model_name,
    const char *destination,
    const model_export_options *options,
    model_export_summary *summary,
    char *error,
    size_t size
);
#endif
