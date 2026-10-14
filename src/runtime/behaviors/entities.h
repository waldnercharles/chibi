#ifndef CHIBI_ENTITIES_H
#define CHIBI_ENTITIES_H

#include "execution/arguments.h"
#include "execution/function.h"
#include "runtime/scenes/prototypes.h"

typedef struct arena arena;

enum
{
    ENTITIES_EVENT_QUEUE_COUNT_MAX = 65536
};

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
/* Optional owning-instance names. A lookup returns borrowed text or NULL for
 * an entity outside that instance. The caller owns context/code until unbound;
 * names are copied into activation storage by the authored capability. This
 * does not register global ECS names or merge separate prefab namespaces. */
void entities_bind_names(entities *, void *context, const char *(*lookup)(void *, ecs_entity));

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
void entities_profile_queries(entities *entities, bool enabled);
entity_query_profile entities_query_profile(const entities *entities);
/* Replace a caller-owned core array with live host-spawned handles in spawn order. */
void entities_copy_live(const entities *entities, ecs_entity **out);
bool entities_owns(const entities *entities, ecs_entity entity);
/* Exact provenance for live host-spawned prototype objects. Strings are
 * borrowed while the entity remains live, until structural processing/reset.
 * Creation is monotonic for the owning service/world lifetime.
 * Missing provenance, retired entities and reused slots return false. */
typedef struct
{
    const char *prototype;
    int object;
    uint64_t creation;
} entity_origin;
bool entities_origin(const entities *, ecs_entity, entity_origin *);

/* Authoritative registered immutable prototype generations, borrowed only at
 * quiescent boundaries until registration/reset/free. Origin iteration includes
 * retained record slots; false skips dead/retiring entries, never reuses IDs. */
int entities_prototype_count(const entities *);
const char *entities_prototype_name_at(const entities *, int);
const scene *entities_prototype_scene_at(const entities *, int);
int entities_origin_count(const entities *);
bool entities_origin_at(const entities *, int, ecs_entity *, entity_origin *);
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
/* Queue owned payload snapshots, including homogeneous arrays/sets and
 * reflected structs with nested strings. Task tokens, maps, malformed layouts
 * and payloads beyond depth16/4096values/1MiB are refused without queuing.
 * Queue ownership is bounded to65536events/16MiB, including a dispatch batch.
 * Delivery is deferred; dead or retiring targets are skipped. */
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

/* Snapshot category membership and positions before concurrent nearest reads.
 * No transform/collider or structural writes are allowed until end. */
unsigned entities_query_categories(const behavior_function *function);
bool entities_parallel_begin(entities *entities, unsigned categories);
void entities_parallel_end(entities *entities);

#endif
