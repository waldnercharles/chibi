#ifndef CHIBI_ENTITIES_H
#define CHIBI_ENTITIES_H

#include "execution/arguments.h"
#include "execution/function.h"
#include "runtime/scenes/prototypes.h"

typedef struct arena arena;

typedef struct entities entities;
typedef struct tilemap_grid tilemap_grid;
/* Borrowed until the next tilemap query or entity service reset. Serial access only. */
const tilemap_grid *entities_tilemap(entities *, ecs_entity, const char **error);
typedef struct materials materials;
void entities_bind_materials(entities *entities, struct materials *materials);
struct materials *entities_materials(const entities *entities);

typedef struct
{
    void *context;
    int (*destroy)(void *, const ecs_entity *, int count);
    int (*attach)(void *, ecs_entity, const char *name);
    int (*dispatch)(void *, ecs_entity, const char *event, execution_arguments);
} entity_host;
/* Hooks and context borrow the owning simulation lifetime. A NULL host supports
 * queries/globals only; any spawn, event delivery or destruction requires hooks. */
entities *entities_new(ecs_world *world, const entity_host *host);
ecs_world *entities_world(const entities *);
void entities_free(entities *entities);
/* Ordered broadcaster only: consecutive proven bodies may reuse query
 * membership. NULL, an unproven body or the broadcast end invalidates it.
 * No query scope may survive yielding to user code or structural mutation. */
void entities_query_scope(entities *entities, const behavior_function *function);
bool entities_queries_preserved(const behavior_function *function);
/* Optional diagnostic counters. Enabling collection also clears the counters. */
typedef struct
{
    uint64_t searches, world_candidates, matching_candidates, cache_builds;
} entity_query_profile;
/* Replace a caller-owned core array with live host-spawned handles in spawn order. */
void entities_copy_live(const entities *entities, ecs_entity **out);
bool entities_owns(const entities *entities, ecs_entity entity);
/* Names currently available to Get Global Variable. Results are borrowed
 * until the corresponding binding/value set is rebuilt or reset. */
int entities_global_name_count(const entities *entities);
const char *entities_global_name_at(const entities *entities, int index);
/* Rebuild authored global entity bindings after a batch of entities is created. */
int entities_rebuild_global_bindings(entities *entities);
/* Global values are scoped to this running ECS world. Values are copied into
 * caller-owned arena storage and remain valid until that arena is reset. */
int entities_get_global_value(const entities *entities, const char *name, arena *storage, pin_value *out);
int entities_set_global_value(entities *entities, const char *name, const pin_value *value);
/* Cancel requests and remove host-spawned entities, retaining prototype snapshots. */
void entities_reset(entities *entities);
int entities_register_prepared(entities *, const char *identity, scene *);
void entities_request_destroy(entities *entities, ecs_entity entity);
bool entities_is_retiring(const entities *entities, ecs_entity entity);
/* Queue copied payloads. Containers and user_data are rejected. Delivery is
 * deferred; dead or retiring targets are skipped. */
int entities_request_event(entities *entities, ecs_entity target, const char *name, execution_arguments inputs);
/* Dispatch one bounded batch while simulation is playing or stepping. Events
 * submitted by handlers remain queued for the next dispatch boundary. */
int entities_dispatch_events(entities *entities);
int entities_pending_events(const entities *entities);
/* Process one bounded batch outside all queries. Positive means a started
 * initializer is still pending; ordinary simulation must wait. */
int entities_flush(entities *entities);
bool entities_initializing(const entities *entities);
/* Drain ready spawn continuations at the same simulation time. Positive means
 * waiting on authored time or the debugger; a runaway chain returns a budget
 * error. Does not dispatch ordinary events. */
int entities_flush_initializers(entities *entities, double monotonic_time);

#endif
