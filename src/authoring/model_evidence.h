#ifndef CHIBI_MODEL_EVIDENCE_H
#define CHIBI_MODEL_EVIDENCE_H
#include "authoring/workspace_file.h"
#include "graph/model/behavior_asset.h"
#include "runtime/images/catalog.h"
/* CPU-only frozen evidence planner. Resource callbacks are consumed during
 * prepare; finalize performs no project reads or mutable catalog queries. */
typedef struct
{
    const behavior_asset *source;
    const behavior_asset *const *catalog;
    int catalog_count, width, height, revision, part, section_axis;
    float time, yaw, pitch, zoom, section_offset, pan_x, pan_y, reference_opacity;
    bool reference_visible;
    const float (*parameters)[4];
    render_image_loader images;
    shader_image_resolver resolve_image;
    void *image_user;
    const char *engine_version, *build_id, *manifest_sha256, *backend, *video_driver;
    bool gpu_rendered;
} model_evidence_input;
typedef struct model_evidence_plan model_evidence_plan;
model_evidence_plan *model_evidence_prepare(const model_evidence_input *, char *error, size_t);
void model_evidence_free(model_evidence_plan *);
/* Exclusive bounded rooted publication; writes manifest.json last. The PNG
 * producer owns GPU provenance. CPU mesh baking is separate coverage. */
bool model_evidence_finalize(
    const model_evidence_plan *,
    workspace_files *,
    const char *directory,
    const void *png,
    size_t png_bytes,
    char *error,
    size_t
);
#endif
