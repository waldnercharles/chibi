#ifndef CHIBI_AUTHORING_SHADER_EVIDENCE_H
#define CHIBI_AUTHORING_SHADER_EVIDENCE_H

#include "authoring/shader_report.h"

/* Caller freezes one successfully drawn preview and its linked asset at the
 * same edit revision. Parameters NULL selects authored defaults. Context
 * resolver callbacks are used synchronously only to verify resident image bytes
 * during prepare, and never serialized. No GPU/app/process work. */
typedef struct
{
    const behavior_asset *snapshot;
    const char *content_root, *destination;
    bool cpu_attempted, cpu_valid;
    shader_sample_context cpu_context;
    float cpu_outputs[SHADER_OUTPUT_MAX][4];
    shader_error cpu_error;
    const void *png;
    size_t png_size;
    int width, height;
    float time, yaw, pitch, zoom;
    float beat_phase, beat_pulse, influence[3];
    const char *engine_version, *engine_build_id, *engine_manifest_hash;
    const char *backend, *video_driver;
    unsigned edit_revision;
    bool has_camera;
    float camera_center[2], camera_span[2];
    const float (*parameters)[4];
} shader_authoring_evidence;

typedef struct shader_authoring_evidence_plan shader_authoring_evidence_plan;
/* Freeze synchronously before GPU readback. Owns source, settings, parameter
 * values, CPU data, identity strings and raw dependency bytes. Ignores png fields.
 * If resolve_image is provided, decoded disk pixels must match each current
 * resident image variant; the callback is called only during prepare. Explicit
 * parameter arrays require unique nonempty exposed names without same-domain
 * scope aliases, such as Factor/material.Factor. NULL preserves source defaults
 * by slot and emits no name-keyed replay overrides. */
shader_authoring_evidence_plan *shader_authoring_evidence_prepare(
    const shader_authoring_evidence *,
    char *error,
    size_t size
);
/* No project reads, callbacks or GPU work. PNG must describe the frozen input.
 * Destination remains exclusive; a successful plan cannot publish twice there. */
bool shader_authoring_evidence_finalize(
    const shader_authoring_evidence_plan *,
    const void *png,
    size_t png_size,
    char *error,
    size_t size
);
void shader_authoring_evidence_free(shader_authoring_evidence_plan *);

/* Creates destination exclusively after CPU validation and dependency reads.
 * Limits: 128 files, 16 MiB/file, 64 MiB total, 16..512 pixels/side and the
 * bounded preview pixel budget. Copies PNG and raw image dependencies exactly.
 * A failed publication retains its partial directory and reports its path.
 * Producer/caller owns GPU provenance; validation does not prove a PNG was
 * rendered by the supplied asset. Never overwrites an existing destination. */
bool shader_authoring_export_evidence(const shader_authoring_evidence *, char *error, size_t size);
#endif
