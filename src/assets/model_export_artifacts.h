#ifndef CHIBI_MODEL_EXPORT_ARTIFACTS_H
#define CHIBI_MODEL_EXPORT_ARTIFACTS_H
#include "assets/model_export.h"

typedef struct
{
    const char *name, *path, *text;
} model_export_source;
typedef struct
{
    const char *name;     /* Static bundle-relative filename. */
    unsigned char *bytes; /* Owned; text artifacts have an extra trailing NUL. */
    size_t size;
} model_export_artifact;
typedef struct
{
    model_export_artifact files[4];
    size_t size;
} model_export_artifacts;
/* Pure CPU serialization of one retained bake and its original source snapshot.
 * No disk reads, compilation, baking or appearance evaluation. Inputs are
 * borrowed for this call only. Result must be zero initialized; refusal leaves
 * it unchanged. max_bytes bounds the combined artifact bytes. */
bool model_export_artifacts_build(
    const char *name,
    const model_program *,
    const model_bake_result *,
    const model_export_options *,
    const model_export_source *,
    int source_count,
    size_t max_bytes,
    model_export_artifacts *,
    char *error,
    size_t
);
void model_export_artifacts_free(model_export_artifacts *);
#endif
