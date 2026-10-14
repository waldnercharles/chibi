#ifndef CHIBI_WORKSPACE_RUNTIME_H
#define CHIBI_WORKSPACE_RUNTIME_H
#include "authoring/runtime_properties.h"
#include "authoring/workspace_documents.h"
#include "render/debug_geometry.h"
#include "render/radiance.h"
#include "render/view.h"
#include "runtime/behaviors/entities.h"
#include "runtime/player_prepared.h"
#include "support/asset_value.h"
#include "ui/rml_backend.h"

typedef struct workspace_runtime workspace_runtime;
typedef struct render_ui render_ui;
typedef struct
{
    workspace_documents *documents; /* borrowed, service outlives session */
    const char *root, *compiler; /* copied canonical root and absolute compiler */
    player_host host; /* copied; linker, callbacks and native catalog outlive service */
    behavior_backend backend;
    /* Static presentation: no engine_frame/Begin/simulation, target UI events
     * or source writes. Restricted host/native catalog remains caller-owned. */
    bool read_only_presentation;
    void *ui_userdata;
    /* Called in the retiring target's type/resource scopes before every world
     * retirement, including before its first draw (UI argument NULL). Host
     * retires borrowed target models/previews and DOM registrations here. */
    void (*ui_retire)(void *, render_ui *);
    const char *engine_version, *build_id, *manifest_hash; /* copied SDK identity */
} workspace_runtime_config;
typedef struct
{
    bool ok, preparing, ready, playing;
    int session, document, revision;
    uint64_t steps, texture, texture_revision;
    double time;
    const char *status, *error, *log;
} workspace_runtime_result;
/* One committed world and one candidate at a time. Source compilation occurs
 * in a child process over a frozen rooted snapshot plus exact open-document
 * overlays. Failure retains the committed world. The host must pump while a
 * job is running; native jobs have the platform service's 60-second deadline.
 * No Studio types, selection, command registry or tools policy are retained.
 * Graphics context must be alive for draw/free after the first draw. */
workspace_runtime *workspace_runtime_new(const workspace_runtime_config *, char *error, size_t);
void workspace_runtime_free(workspace_runtime *);
workspace_runtime_result workspace_runtime_prepare(workspace_runtime *, int document, int revision);
/* Compile a caller-supplied virtual scene in the owned frozen project only.
 * Relative path must be unused (including named drafts), .kdl and <=1024 bytes;
 * source is UTF-8 <=1MiB. Host callbacks in config remain explicit: thumbnail
 * callers must supply restricted effects. Failure preserves committed world. */
/* Virtual scene compiled from an owned overlay snapshot, bound to a source
 * document revision. No caller-owned source/root outlives the call. */
workspace_runtime_result workspace_runtime_prepare_document_scene_source(
    workspace_runtime *,
    int document,
    int revision,
    const char *relative_scene,
    const char *source
);
/* Create-exclusive private UTF-8 inputs in the frozen workspace. Paths are
 * workspace-relative; no caller document/file is created or mutated. */
workspace_runtime_result workspace_runtime_prepare_document_sources(
    workspace_runtime *,
    int document,
    int revision,
    const char *scene,
    const char *const *paths,
    const char *const *sources,
    int count
);
workspace_runtime_result workspace_runtime_prepare_scene_source(
    workspace_runtime *,
    const char *relative_scene,
    const char *source
);
/* Trusted host/CLI entry for an already compiled artifact. No script file-open
 * node is exposed; roots are borrowed only during bounded decode. */
workspace_runtime_result workspace_runtime_open_prepared(
    workspace_runtime *,
    const char *artifact,
    const char *resource_root,
    int document,
    int revision
);
workspace_runtime_result workspace_runtime_poll(workspace_runtime *);
/* Read committed status without polling jobs or touching GPU. */
workspace_runtime_result workspace_runtime_current(workspace_runtime *);
bool workspace_runtime_read_only(const workspace_runtime *);
declared_components *workspace_runtime_types(const workspace_runtime *);
workspace_runtime_result workspace_runtime_cancel(workspace_runtime *);
/* Fresh seeded execution over the retained prepared scene. Preflight verifies
 * the executed artifact and options before retiring borrowed UI/model views.
 * On admitted Begin failure the caller must stop; preflight refusal changes no
 * engine state. Same world/session owners, zero clock, seed-before-Begin. */
workspace_runtime_result workspace_runtime_restart(
    workspace_runtime *,
    int session,
    const player_restart_input *,
    bool *admitted
);
workspace_runtime_result workspace_runtime_control(workspace_runtime *, int session, const char *operation);
workspace_runtime_result workspace_runtime_draw(
    workspace_runtime *,
    int session,
    int width,
    int height,
    double seconds
);
/* Read active authored camera using the same runtime lens/competing-camera
 * contract as standalone Play. Returns1 found,0 absent,-1 invalid/stale. */
bool workspace_runtime_apply_camera(workspace_runtime *, int session, char *error, size_t);
int workspace_runtime_camera(workspace_runtime *, int session, engine_viewport_view *, char *error, size_t);
/* Presentation settings only; canonical source and simulation are unchanged. */
render_debug_geometry_result workspace_runtime_debug_geometry_status(const workspace_runtime *);
bool workspace_runtime_presentation(
    workspace_runtime *,
    int session,
    bool unlit,
    bool fogless,
    bool colliders,
    bool cameras
);
bool workspace_runtime_set_view(workspace_runtime *, int session, const engine_viewport_view *, bool perspective);
/* Projection/rays use the last successfully drawn image camera, including
 * pending resize/view changes. Points are NDC (Y-up);2D occupies worldXZ. */
bool workspace_runtime_ray(workspace_runtime *, int session, CF_V2 ndc, CF_Ray3 *, char *error, size_t);
bool workspace_runtime_project(
    workspace_runtime *,
    int session,
    CF_V3 world,
    CF_V3 *ndc,
    bool *visible,
    char *error,
    size_t
);
bool workspace_runtime_object_subject(
    const workspace_runtime *,
    int session,
    int object,
    char subject[64],
    char *error,
    size_t
);
bool workspace_runtime_subject(
    const workspace_runtime *,
    int session,
    uint64_t owner,
    char subject[64],
    char *error,
    size_t
);
typedef struct
{
    int object;
    char *prototype, *path,
        *source_path; /* Caller-owned strings, including empty values. */
    uint64_t creation;
    bool known, authored, spawned;
} workspace_runtime_subject_info;
/* Source identity is retained from frozen compiler inputs; unknown metadata
 * stays explicit. Live subject/session checks precede every query. */
bool workspace_runtime_subject_origin(
    const workspace_runtime *,
    int session,
    const char *subject,
    workspace_runtime_subject_info *,
    char *error,
    size_t
);
bool workspace_runtime_origin(const workspace_runtime *, int session, uint64_t owner, entity_origin *out);
bool workspace_runtime_component(
    workspace_runtime *,
    int session,
    int object,
    const char *component,
    bool *present,
    char *error,
    size_t
);
bool workspace_runtime_pick_subject(
    workspace_runtime *,
    int session,
    float x,
    float y,
    int *object,
    char subject[64],
    char *error,
    size_t
);
bool workspace_runtime_subject_properties(
    workspace_runtime *,
    int session,
    const char *subject,
    int first,
    int count,
    arena *,
    pin_array **rows,
    pin_array **choices,
    int *total,
    char **name,
    char *error,
    size_t
);
bool workspace_runtime_subject_write(
    workspace_runtime *,
    int session,
    const char *subject,
    const char *component,
    const char *field,
    const char *json,
    char *error,
    size_t
);
bool workspace_runtime_pick(workspace_runtime *, int session, float x, float y, int *object, char *error, size_t);
/* Exclusion indices refer to the committed scene's authored object order.
 * The authored controller owns hidden/locked/isolate policies and translates
 * JSON paths to these indices; runtime-created entities have no source index. */
bool workspace_runtime_skip(workspace_runtime *, int session, const int *objects, int count, bool picking);
workspace_runtime_result workspace_runtime_input(
    workspace_runtime *,
    int session,
    const char *action,
    float value,
    bool pressed
);
/* Inspect/edit finite reflected float/double fields through ECS owning writes.
 * Write requires paused simulation; nested fields use dot-separated names. */
/* Owned bounded JSON snapshot of a reflected dotted field, including finite
 * nested records. Read-only at a quiescent host boundary; does not mutate the
 * target or its evidence trace. Refusal leaves *json NULL. Limits are the
 * shared value_json 1 MiB / depth 16 / 65536-value budgets. */
bool workspace_runtime_value(
    workspace_runtime *,
    int session,
    int object,
    const char *component,
    const char *field,
    char **json,
    char *error,
    size_t
);
bool workspace_runtime_number(
    workspace_runtime *,
    int session,
    int object,
    const char *component,
    const char *field,
    bool write,
    double value,
    double *out,
    char *error,
    size_t size
);
bool workspace_runtime_grid(workspace_runtime *, int session, float increment);
bool workspace_runtime_bounds(
    workspace_runtime *,
    int session,
    const int *objects,
    int count,
    CF_V3 *min,
    CF_V3 *max,
    char *error,
    size_t size
);
const scene_instance *workspace_runtime_scene(const workspace_runtime *);
const world *workspace_runtime_world(const workspace_runtime *);
/* Borrowed until ui_retire is called or runtime free. NULL before first draw. */
render_ui *workspace_runtime_ui(const workspace_runtime *);
/* Explicit target-UI ingress. Pointer x/y are last-drawn NDC (Y-up), wheel
 * values retain host units. No implicit host forwarding or renderer creation.
 * Validation refusal leaves outputs and target untouched. Dispatch failure
 * reports false with outputs describing admitted input; callbacks execute in target
 * type/resource scopes. Captured bits use rml_input_capture. */
bool workspace_runtime_ui_input(
    workspace_runtime *,
    int session,
    const rml_input *,
    unsigned *captured,
    bool *typing,
    bool *dispatched,
    char *error,
    size_t
);
/* Trusted producer attaches an already validated bounded gameplay timeline.
 * Owned copy is frozen by evidence prepare; NULL clears previous attachment.
 * This does not attest that replay executed or gameplay was validated. */
bool workspace_runtime_input_timeline(workspace_runtime *, int session, const char *json);
/* Exact frozen executable sources, borrowed until next successful replacement.
 * Empty for trusted prepared imports whose source lineage was not supplied. */
const char *workspace_runtime_source_root(const workspace_runtime *);
const engine_frame_timing *workspace_runtime_timing(const workspace_runtime *);
bool workspace_runtime_lighting(const workspace_runtime *, render_radiance_status *);
typedef struct
{
    bool ok, pending;
    int job;
    const char *state, *directory, *error;
} workspace_runtime_evidence_result;
/* One retained job per service, monotonic handles. Requires a successfully
 * drawn, unchanged viewport and frozen compiler lineage. Copies CPU provenance
 * before transferring the canvas; poll never blocks and times out after10s.
 * Capturing does not advance gameplay; next draw owns a new canvas. */
workspace_runtime_evidence_result workspace_runtime_evidence_begin(
    workspace_runtime *,
    int session,
    const char *directory
);
workspace_runtime_evidence_result workspace_runtime_evidence_poll(workspace_runtime *, int job);
workspace_runtime_evidence_result workspace_runtime_evidence_cancel(workspace_runtime *, int job);
/* Host must call when mutating the target through an external debugger/bridge;
 * evidence reports untracked coverage instead of claiming replay completeness. */
/* Trusted generic host services can attest a bounded explicit admitted call.
 * Details must be an object <=8 KiB/256 nodes/depth8. A full trace records
 * truncation without refusing the operation. No script binding is exposed. */
bool workspace_runtime_record_call(
    workspace_runtime *,
    int session,
    const char *operation,
    const av_value *details
);
const char *workspace_runtime_artifact_relative(const workspace_runtime *);
/* Digest/count of the exact owned bytes passed to the successful player
 * decoder, retained across scene reset. Verify performs bounded no-follow IO
 * without changing player, recording identity, timing or trace. */
const char *workspace_runtime_artifact_sha256(const workspace_runtime *);
size_t workspace_runtime_artifact_bytes(const workspace_runtime *);
bool workspace_runtime_verify_artifact(const workspace_runtime *, int session, char *error, size_t);
const char *workspace_runtime_sdk_build(const workspace_runtime *);
const char *workspace_runtime_sdk_manifest(const workspace_runtime *);
void workspace_runtime_mark_external_change(workspace_runtime *);
/* Borrowed only synchronously until world mutation/retirement. Reads are
 * permitted for static presentations; writes reject static/stale sessions. */
typedef struct save save;
save *workspace_runtime_save(workspace_runtime *, int session, bool write, char *error, size_t);

bool workspace_runtime_trace(workspace_runtime *, int session, char **json, char *, size_t);
#endif
