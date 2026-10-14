#ifndef CHIBI_WORKSPACE_SIMULATION_H
#define CHIBI_WORKSPACE_SIMULATION_H
#include "authoring/workspace_runtime.h"
#include "runtime/input_timeline.h"

#define WORKSPACE_TIMELINE_KEYS_MAX 4096
#define WORKSPACE_TIMELINE_BYTES_MAX (1024u * 1024u)
/* Optional CPU host capability. Runtime outlives this service. No ambient
 * keyboard/controller reads: callers explicitly supply focused samples. All
 * functions are serial at a quiescent host boundary, outside target dispatch. */
typedef struct workspace_simulation workspace_simulation;
typedef struct
{
    uint64_t controls;
    input_controller_state controllers[INPUT_CONTROLLER_MAX];
    CF_V2 movement, aim;
    bool aim_point, firing;
    input_pointer_ray pointer;
} workspace_simulation_input;
typedef struct
{
    bool ok, admitted, recording, replaying, truncated, pending;
    int keys;
    uint64_t steps, end_step;
    unsigned captured;
    bool typing;
    double scale;
    /* Borrowed until the next operation/free; native bindings copy strings. */
    const char *timeline, *error, *hash;
} workspace_simulation_result;
workspace_simulation *workspace_simulation_new(workspace_runtime *);
void workspace_simulation_free(workspace_simulation *);
/* Canonical Control names and ordinary UI key aliases (A..Z, digits,
 * ArrowUp/Down/Left/Right, Space, Shift, Control, Alt, mouse buttons). Unknown
 * names, invalid device bits and nonfinite samples refuse before mutation. */
bool workspace_simulation_keys(const char *const *, int, uint64_t *, char *error, size_t);
workspace_simulation_result workspace_simulation_sample(
    workspace_simulation *,
    int session,
    const workspace_simulation_input *
);
workspace_simulation_result workspace_simulation_clear(workspace_simulation *, int session);
/* UI event ingress uses canonical UI key names, last-drawn NDC pointer, raw
 * wheel units and bounded UTF8 text. Gameplay timeline excludes these events;
 * Consumed interactive UI input or dispatched UI events stop recording/replay;
 * interrupted recordings cannot export as complete gameplay timelines. */
workspace_simulation_result workspace_simulation_ui(
    workspace_simulation *,
    int session,
    const char *operation,
    const char *key,
    const char *text,
    float x,
    float y,
    float wheel_x,
    float wheel_y,
    int button,
    unsigned modifiers
);
/* Recording/replay/export verify the retained executed artifact bytes and SDK
 * identity before changing timeline or runtime state. Record/replay restart clocks,
 * input, shared state and the empty in-memory save baseline before seeded Begin.
 * Trusted prepared imports
 * are supported; source evidence independently requires an owned frozen root. */
workspace_simulation_result workspace_simulation_timeline(
    workspace_simulation *,
    int session,
    const char *operation,
    unsigned seed,
    const char *timeline
);
workspace_simulation_result workspace_simulation_time_scale(
    workspace_simulation *,
    int session,
    bool write,
    double scale
);
/* One prepare call per request; authored hosts own debounce/frame budgets.
 * prepare/reset are refused after gameplay advances; play must reset preview
 * materials before the first gameplay tick. status does not mutate. */
workspace_simulation_result workspace_simulation_materials(
    workspace_simulation *,
    int session,
    const char *operation,
    float x,
    float y,
    float radius
);
/* CPU codec v2 requires initial_save:{}; v1 and other baselines are unsupported.
 * Identity is the SHA256 of exact executable and SDK
 * lineage; controls are encoded as hexadecimal strings, never lossy doubles.
 * On refusal outputs are untouched. Caller owns successful bytes/keys. */
bool workspace_simulation_encode(
    const input_keyframe *,
    int count,
    unsigned seed,
    uint64_t steps,
    const char *identity,
    char **json,
    char *error,
    size_t
);
bool workspace_simulation_decode(
    const char *json,
    const char *identity,
    input_keyframe **keys,
    int *count,
    unsigned *seed,
    uint64_t *steps,
    char *error,
    size_t
);
#endif
