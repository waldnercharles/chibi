#ifndef CHIBI_VISUAL_MODEL_H
#define CHIBI_VISUAL_MODEL_H
#include "authoring/workspace_documents.h"
#include "graph/modeling/model_compile.h"
#include "runtime/models/model_bake.h"
#include "runtime/models/model_inspection.h"

/* Owned CPU model generation; input catalog is borrowed during compilation.
 * Prepared parameters, bounds, parts and anchors remain valid after source
 * documents close. No publication, renderer or editor controller is involved. */
typedef struct
{
    int document, revision;
    behavior_asset *source;
    model_program program;
    model_inspection inspection;
    model_bounds bounds;
    float parameters[SHADER_PARAMETER_MAX][4];
    shader_error error;
    bool valid;
} visual_model_report;
void visual_model_report_init(visual_model_report *);
void visual_model_report_free(visual_model_report *);
bool visual_model_inspect(
    const workspace_documents *,
    int document,
    int revision,
    const behavior_asset *const *catalog,
    int count,
    visual_model_report *
);
/* Resolver/user are borrowed until report release and must remain live for
 * image-backed bounds, hit sampling and CPU mesh baking. */
bool visual_model_inspect_images(
    const workspace_documents *,
    int document,
    int revision,
    const behavior_asset *const *catalog,
    int count,
    shader_image_resolver,
    void *image_user,
    visual_model_report *
);
/* Source-owned model reference alignment. Empty image clears the alignment.
 * Read allocates image; free explicitly. Candidate never changes history. */
typedef struct
{
    const char *image;
    float center_x, center_y, height, angle;
    bool locked;
} visual_model_reference;
bool visual_model_reference_read(
    const workspace_documents *,
    int document,
    int revision,
    visual_model_reference *,
    shader_error *
);
void visual_model_reference_free(visual_model_reference *);
char *visual_model_reference_prepare(
    const workspace_documents *,
    int document,
    int revision,
    const visual_model_reference *,
    shader_error *
);
/* Local declaration edits use stable source node IDs, declared widths/ranges,
 * and the same document transaction/history as text and graph edits. */
/* Returns an owned canonical candidate without changing document/history. */
char *visual_model_prepare_default(
    workspace_documents *,
    int document,
    int revision,
    uint64_t node,
    int width,
    const float values[4],
    shader_error *
);
bool visual_model_set_default(
    workspace_documents *,
    int document,
    int revision,
    uint64_t node,
    int width,
    const float values[4],
    int *result_revision,
    shader_error *
);
bool visual_model_bake(
    const visual_model_report *,
    const model_bake_options *,
    model_bake_result *,
    shader_error *
);
#endif
