#ifndef CHIBI_GAME_ENTITIES_H
#define CHIBI_GAME_ENTITIES_H

#include "execution/arguments.h"
#include "execution/function.h"
#include "runtime/scenes/prototypes.h"

typedef struct arena arena;

typedef struct game_entities game_entities;
typedef struct tilemap_grid tilemap_grid;
/* Borrowed until the next tilemap query or entity service reset. Serial access only. */
const tilemap_grid *game_entities_tilemap(game_entities *, ecs_entity, const char **error);
typedef struct game_materials game_materials;
void game_entities_bind_materials(game_entities *entities, struct game_materials *materials);
struct game_materials *game_entities_materials(const game_entities *entities);

typedef struct
{
    void *context;
    int (*destroy)(void *, const ecs_entity *, int count);
    int (*attach)(void *, ecs_entity, const char *name);
    int (*dispatch)(void *, ecs_entity, const char *event, execution_arguments);
} game_entity_host;
/* Hooks and context borrow the owning simulation lifetime. A NULL host supports
 * queries/globals only; any spawn, event delivery or destruction requires hooks. */
game_entities *game_entities_new(ecs_world *world, const game_entity_host *host);
ecs_world *game_entities_world(const game_entities *);
void game_entities_free(game_entities *entities);
/* Ordered broadcaster only: consecutive proven bodies may reuse query
 * membership. NULL, an unproven body or the broadcast end invalidates it.
 * No query scope may survive yielding to user code or structural mutation. */
void game_entities_query_scope(game_entities *entities, const behavior_function *function);
bool game_entities_queries_preserved(const behavior_function *function);
/* Optional diagnostic counters. Enabling collection also clears the counters. */
typedef struct
{
    uint64_t searches, world_candidates, matching_candidates, cache_builds;
} game_entity_query_profile;
void game_entities_profile_queries(game_entities *entities, bool enabled);
game_entity_query_profile game_entities_query_profile(const game_entities *entities);
/* Replace a caller-owned core array with live host-spawned handles in spawn order. */
void game_entities_copy_live(const game_entities *entities, ecs_entity **out);
bool game_entities_owns(const game_entities *entities, ecs_entity entity);
/* Names currently available to Get Global Variable. Results are borrowed
 * until the corresponding binding/value set is rebuilt or reset. */
int game_entities_global_name_count(const game_entities *entities);
const char *game_entities_global_name_at(const game_entities *entities, int index);
/* Rebuild authored global entity bindings after a batch of entities is created. */
int game_entities_rebuild_global_bindings(game_entities *entities);
/* Global values are scoped to this running ECS world. Values are copied into
 * caller-owned arena storage and remain valid until that arena is reset. */
int game_entities_get_global_value(
    const game_entities *entities,
    const char *name,
    arena *storage,
    pin_value *out
);
int game_entities_set_global_value(game_entities *entities, const char *name, const pin_value *value);
/* Cancel requests and remove host-spawned entities, retaining prototype snapshots. */
void game_entities_reset(game_entities *entities);
int game_entities_register_prepared(game_entities *, const char *identity, game_scene *);
void game_entities_request_destroy(game_entities *entities, ecs_entity entity);
bool game_entities_is_retiring(const game_entities *entities, ecs_entity entity);
/* Queue copied payloads. Containers and user_data are rejected. Delivery is
 * deferred; dead or retiring targets are skipped. */
int game_entities_request_event(
    game_entities *entities,
    ecs_entity target,
    const char *name,
    execution_arguments inputs
);
/* Dispatch one bounded batch while simulation is playing or stepping. Events
 * submitted by handlers remain queued for the next dispatch boundary. */
int game_entities_dispatch_events(game_entities *entities);
int game_entities_pending_events(const game_entities *entities);
/* Process one bounded batch outside all queries. Positive means a started
 * initializer is still pending; ordinary simulation must wait. */
int game_entities_flush(game_entities *entities);
bool game_entities_initializing(const game_entities *entities);
/* Drain ready spawn continuations at the same simulation time. Positive means
 * waiting on authored time or the debugger; a runaway chain returns a budget
 * error. Does not dispatch ordinary events. */
int game_entities_flush_initializers(game_entities *entities, double monotonic_time);

/* Snapshot category membership and positions before concurrent nearest reads.
 * No transform/collider or structural writes are allowed until end. */
unsigned game_entities_query_categories(const behavior_function *function);
bool game_entities_parallel_begin(game_entities *entities, unsigned categories);
void game_entities_parallel_end(game_entities *entities);

#endif
