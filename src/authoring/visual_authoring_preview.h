#ifndef CHIBI_VISUAL_AUTHORING_PREVIEW_H
#define CHIBI_VISUAL_AUTHORING_PREVIEW_H
#include "authoring/preview.h"
#include "authoring/shader_evidence.h"
#include "authoring/visual_authoring.h"
#include "authoring/visual_model.h"

#include <stdint.h>

typedef struct visual_previews visual_previews;
struct render_resource_context;
struct workspace_resources;
typedef struct
{
    int preview, document, revision, catalog_revision, width, height;
    uint64_t texture, texture_revision;
    float time, yaw, pitch, zoom, section_offset, pan_x, pan_y, reference_opacity;
    bool reference_visible;
    int model_part, section_axis;
    bool drawn;
    const char *error;
} visual_preview_view;
typedef enum
{
    VISUAL_EVIDENCE_PENDING,
    VISUAL_EVIDENCE_COMPLETE,
    VISUAL_EVIDENCE_FAILED,
    VISUAL_EVIDENCE_CANCELLED
} visual_evidence_state;
typedef struct
{
    int job;
    visual_evidence_state state;
    const char *destination, *error;
} visual_evidence_view;
/* GPU-thread service, owns at most 16 retained surfaces and 8 evidence jobs.
 * documents/world must outlive service; paths are copied. Free before graphics app.
 * Handles are positive, monotonic and never recycled. */
visual_previews *visual_previews_new(
    workspace_documents *,
    preview_world *,
    const char *content_root,
    const char *workspace_root
);
void visual_previews_free(visual_previews *);
preview_world *visual_previews_world(visual_previews *);
/* Atomically replace the complete bounded visual generation from a complete
 * resource inventory plus exact open-document overlays. Refusal retains the
 * last valid world/resources; no master documents are created. */
bool visual_previews_refresh_catalog(visual_previews *, struct workspace_resources *, char *error, size_t);
/* Replace the default owned mutable collection before first use. The supplied
 * context is borrowed and must outlive this service and its preview world. */
void visual_previews_set_resources(visual_previews *, struct render_resource_context *);
struct render_resource_context *visual_previews_resources(visual_previews *);
/* Malloc-owned array of independently owned applied visual source snapshots.
 * Last valid applied dependencies are used while working edits are invalid. */
behavior_asset **visual_previews_catalog_snapshot(visual_previews *, int *count);
typedef struct
{
    int document, revision;
    bool valid;
    char error[256];
} visual_publication_result;
/* Bounded dependency publication. Every requested canonical revision is read
 * before publication; duplicate identities refuse the whole request. Other
 * failures are explicit per-document partial results and preserve last valid
 * applied generations. The caller owns result[count]. */
bool visual_publish_documents(
    visual_previews *,
    const int *documents,
    int count,
    visual_publication_result *,
    char *error,
    size_t
);
bool visual_preview_publish(visual_previews *, int document, int revision, char *error, size_t);
bool visual_preview_open(visual_previews *, int document, int width, int height, int *handle, char *error, size_t);
int visual_preview_handles(const visual_previews *, int *out, int capacity);
bool visual_preview_close(visual_previews *, int handle);
bool visual_preview_read(visual_previews *, int handle, visual_preview_view *);
/* Draw only an applied generation matching the expected canonical document.
 * Explicit time/view settings are finite. Publication and gesture policy are
 * caller-owned. Invalid/stale draws expose no texture and cannot be exported. */
bool visual_preview_draw(
    visual_previews *,
    int handle,
    int revision,
    float time,
    float yaw,
    float pitch,
    float zoom
);
bool visual_preview_model_inspection(
    visual_previews *,
    int handle,
    int revision,
    int part,
    int section_axis,
    float offset,
    char *error,
    size_t
);
bool visual_preview_model_reference(
    visual_previews *,
    int handle,
    int revision,
    const visual_model_reference *,
    bool visible,
    float opacity,
    char *error,
    size_t
);
bool visual_preview_model_pan(visual_previews *, int handle, int revision, float x, float y, char *error, size_t);
bool visual_preview_export(
    visual_previews *,
    int handle,
    const char *destination,
    const char *engine_version,
    const char *build_id,
    const char *manifest_hash,
    int *job,
    char *error,
    size_t
);
/* Poll performs bounded asynchronous readback and completion; pending jobs
 * fail after 10 monotonic seconds. Completed jobs remain until explicit close. */
void visual_previews_poll(visual_previews *);
bool visual_evidence_read(visual_previews *, int job, visual_evidence_view *);
bool visual_evidence_close(visual_previews *, int job);
#endif
