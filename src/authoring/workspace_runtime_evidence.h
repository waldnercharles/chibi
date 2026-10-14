#ifndef CHIBI_WORKSPACE_RUNTIME_EVIDENCE_H
#define CHIBI_WORKSPACE_RUNTIME_EVIDENCE_H
#include "authoring/workspace_file.h"
#include "render/view.h"

#include <stdint.h>
/* CPU-only immutable planner shared by viewport hosts and CLI producers.
 * The producer attests that settings describe exactly the captured last draw.
 * Frozen root is compiler-owned, never the current mutable workspace. */
typedef struct
{
    const char *frozen_root, *artifact_relative, *scene;
    const char *executed_artifact_sha256;
    size_t executed_artifact_bytes;
    const char *engine_version, *build_id, *manifest_hash, *backend, *video_driver;
    const char *replay_trace_json;
    const char *input_timeline_json; /* optional trusted gameplay-only timeline */
    int session, document, revision, width, height;
    uint64_t steps, random_state;
    bool random_isolated;
    double host_time, simulation_time;
    engine_viewport_view camera;
    CF_M4x4 view_matrix, projection_matrix;
    float grid;
    bool unlit, fogless, colliders, camera_frames, debug_truncated;
    int debug_unsupported;
    bool perspective, playing, untracked, trace_truncated, gpu_rendered;
} workspace_runtime_evidence_input;
typedef struct workspace_runtime_evidence_plan workspace_runtime_evidence_plan;
/* 126 source files/46MiB, or125/45MiB with<=1MiB timeline;16MiB each,4096 visited entries/depth16.
 * Links/unreadable/special files refuse. Input strings/settings copied. */
workspace_runtime_evidence_plan *workspace_runtime_evidence_prepare(
    const workspace_runtime_evidence_input *,
    char *error,
    size_t
);
void workspace_runtime_evidence_free(workspace_runtime_evidence_plan *);
/* Exclusive rooted bundle; manifest last. No source reads/callback/GPU work.
 * PNG must match frozen dimensions. Partial IO failure remains visibly partial. */
bool workspace_runtime_evidence_finalize(
    const workspace_runtime_evidence_plan *,
    workspace_files *,
    const char *directory,
    const void *png,
    size_t png_bytes,
    char *error,
    size_t
);
#endif
