#ifndef CHIBI_GAME_BEHAVIOR_H
#define CHIBI_GAME_BEHAVIOR_H

#include "ecs/ecs.h"
#include "graph/runtime/instance.h"
#include "runtime/audio.h"
#include "runtime/run.h"
#include "runtime/save.h"
#include "runtime/ui.h"
#include "runtime/visual_effects.h"

#include <stdint.h>

typedef struct game_behavior_world game_behavior_world;
game_audio *game_behavior_audio(game_behavior_world *world);
game_ui *game_behavior_ui(game_behavior_world *world);
game_run *game_behavior_run(game_behavior_world *world);
game_visual_effects *game_behavior_visual_effects(game_behavior_world *world);

#define GAME_BEHAVIOR_PROFILE_MAX 256
typedef struct
{
    const char *behavior;
    const char *event;
    uint64_t dispatches;
    uint64_t starts;
    uint64_t resumes;
    double inclusive_cpu_ms;
    double cpu_ms;
    double setup_ms;
} game_behavior_profile_row;

typedef struct
{
    game_behavior_profile_row rows[GAME_BEHAVIOR_PROFILE_MAX];
    int count;
    game_behavior_profile_row overflow;
    double inclusive_cpu_ms;
    double cpu_ms;
    double setup_ms;
} game_behavior_profile_frame;

/* Per display frame, including every fixed step. Labels belong to the world.
 * Snapshot remains valid until the next read or world destruction. Execution
 * intervals exclude nested root events and their setup. Setup measures successful
 * event lookup and activation creation/reuse, excluding scheduler execution.
 * World scans, scheduler bookkeeping and reaping remain unassigned. */
/* Disabled by default. Performance inspection or explicit requests enable timers. */
void game_behavior_set_profiling(game_behavior_world *world, bool enabled);
void game_behavior_profile_begin(game_behavior_world *world);
const game_behavior_profile_frame *game_behavior_profile(const game_behavior_world *world);
struct game_entities;
void game_behavior_bind_entities(game_behavior_world *world, struct game_entities *entities);
struct game_particles;
void game_behavior_bind_particles(game_behavior_world *world, struct game_particles *particles);
struct game_visibility;
void game_behavior_bind_visibility(game_behavior_world *world, struct game_visibility *visibility);

#define GAME_BEHAVIOR_LOG_MESSAGE_MAX 256

typedef struct
{
    ecs_entity entity;
    const char *asset_name;
    const node *origin;
    bool failure; /* a run that stopped on this node's error */
    char message[GAME_BEHAVIOR_LOG_MESSAGE_MAX];
} game_behavior_log_info;

game_behavior_world *game_behavior_world_new(ecs_world *world);
void game_behavior_world_free(game_behavior_world *world);
/* Restarts the stream every random draw of this world's graphs reads. */
void game_behavior_seed_random(game_behavior_world *world, uint64_t seed);
game_save *game_behavior_save(game_behavior_world *world);
int game_behavior_register_asset(game_behavior_world *world, const char *asset_name, behavior_asset *definition);
int game_behavior_unregister_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset **definition
);
/* Attachments and instances keep their asset under its new name. */
int game_behavior_rename_asset(game_behavior_world *world, const char *from, const char *to);
int game_behavior_replace_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset *definition,
    behavior_asset **old_definition
);
/* Editor setup mode: attachment creation/replacement prepares instances without
 * gameplay effects. Toggling this flag never dispatches or resets anything.
 * Leave it false during a running session, including Pause. Defaults false. */
void game_behavior_set_deferred(game_behavior_world *world, bool deferred);
/* Start prepared attachments once, in attachment order. Requires deferred=false.
 * Failed Begin remains failed until replacement/reset; retry returns -1. Runtime
 * attachments created by Begin start normally and are not started a second time. */
int game_behavior_begin_pending(game_behavior_world *world);
int game_behavior_attach(game_behavior_world *world, ecs_entity entity, const char *asset_name);
/* Replace one live entity's ordered attachments, retaining unchanged instances.
 * Prevalidation/staging failure preserves membership and existing instances.
 * Newly added behaviors execute Begin as with attach; external Begin effects
 * (component writes, queued events/spawns, sinks) cannot be rolled back.
 * Call outside dispatch; callbacks must not remove existing attachments from
 * this same entity. Names are borrowed only for this call. Returns 0 or -1. */
int game_behavior_set_attachments(
    game_behavior_world *world,
    ecs_entity entity,
    const char *const *names,
    int count
);
int game_behavior_detach_asset(game_behavior_world *world, ecs_entity entity, const char *asset_name);
int game_behavior_set_debug_hold(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    int enabled
);
int game_behavior_detach_entity(game_behavior_world *world, ecs_entity entity);
int game_behavior_destroy_entity(game_behavior_world *world, ecs_entity entity);
/* Structural boundary outside dispatch/queries: cancel and detach the selected
 * instances, compact the ordered schedule once, then apply destruction in input
 * order. Surviving attachments retain their order and state. Duplicate, retiring
 * and stale handles are allowed; stale generations cannot select replacements. */
int game_behavior_destroy_entities(game_behavior_world *world, const ecs_entity *entities, int count);
/* Returns INSTANCE_UPDATE_*; all instances pass preflight before any change. */
int game_behavior_can_preview_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset *definition
);
int game_behavior_preview_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset *definition,
    behavior_asset **old_definition
);
int game_behavior_dispatch(
    game_behavior_world *world,
    ecs_entity entity,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
);
/* Dispatches to every live attachment that declares the event. */
int game_behavior_dispatch_all(
    game_behavior_world *world,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
);
/* Dedicated Tick schedule, repaired incrementally at attachment/publication
 * changes. Eligible synchronous entries retain each entity's attachment order.
 * Proven independent entity chunks execute in parallel, with ordered barriers
 * around other entries. Inspection, waits and unproven effects retain their
 * compiled activations. */
int game_behavior_update(game_behavior_world *world, flow_error *error);
typedef struct
{
    int entries, prepared, native_loop, system_loop, scheduled;
    int parallel, parallel_tasks, workers;
} game_behavior_update_stats;
game_behavior_update_stats game_behavior_last_update(const game_behavior_world *world);
/* Performance comparison: retain compiled bodies but use the C outer loop. */
void game_behavior_set_parallel_updates(game_behavior_world *world, bool enabled);
void game_behavior_set_native_update(game_behavior_world *world, bool enabled);
/* Debug/performance comparison switch. Ordered synchronous batches are enabled
 * by default; inspected, queued and effectful host work keeps normal activations. */
void game_behavior_set_batch_enabled(game_behavior_world *world, bool enabled);
/* Returns whether at least one attached behavior declares the event. */
bool game_behavior_has_event(const game_behavior_world *world, const char *event_name);
int game_behavior_pump(game_behavior_world *world, double monotonic_time);
/* Number of attachment queues visited by the last pump, excluding idle rows. */
int game_behavior_last_pump_count(const game_behavior_world *world);
/* Set the simulation clock before dispatch, including newly attached Begin runs. */
void game_behavior_set_time(game_behavior_world *world, double simulation_time);
bool game_behavior_paused(const game_behavior_world *world);
void game_behavior_set_delta_seconds(game_behavior_world *world, double delta_seconds);
void game_behavior_set_parallel_for(
    game_behavior_world *world,
    flow_parallel_for_fn parallel_for,
    void *parallel_user
);
void game_behavior_set_sink(game_behavior_world *world, flow_sink_fn sink, void *user);
int game_behavior_log_count(const game_behavior_world *world);
int game_behavior_log_info_at(const game_behavior_world *world, int index, game_behavior_log_info *out);
void game_behavior_clear_logs(game_behavior_world *world);
int game_behavior_attachment_count(const game_behavior_world *world);
/* Select traversal/watch instrumentation independently of debugger holds. */
void game_behavior_inspect(game_behavior_world *world, const behavior_instance *instance);

/* Attachment records borrow the asset name, definition, and instance from the
 * behavior world. They remain valid until the next attach, detach, replace, or
 * world destruction operation. */
typedef struct
{
    ecs_entity entity;
    const char *asset_name;
    const behavior_asset *definition;
    behavior_instance *instance;
    uint64_t sequence;
    int debug_hold;
} game_behavior_attachment_info;

typedef struct
{
    ecs_entity entity;
    int active;
    int suspended;
    double wake_time;
} game_behavior_entity_state;

int game_behavior_attachment_count_for_entity(const game_behavior_world *world, ecs_entity entity);
/* The first activation paused at a breakpoint or step, with its attachment in
 * `out`; NULL while none is. Borrowed until the run resumes or the world
 * changes its attachments. */
flow_run *game_behavior_paused_run(game_behavior_world *world, game_behavior_attachment_info *out);
int game_behavior_attachment_info_at(
    game_behavior_world *world,
    ecs_entity entity,
    int index,
    game_behavior_attachment_info *out
);
int game_behavior_entity_state_at(
    const game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    game_behavior_entity_state *out
);
int game_behavior_asset_registered(const game_behavior_world *world, const char *asset_name);
int game_behavior_asset_count(const game_behavior_world *world);
const char *game_behavior_asset_name_at(const game_behavior_world *world, int index);
const behavior_asset *game_behavior_asset_definition_at(const game_behavior_world *world, int index);
int game_behavior_pool_size(const game_behavior_world *world, const char *asset_name);
behavior_instance *game_behavior_instance(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name
);

#endif
