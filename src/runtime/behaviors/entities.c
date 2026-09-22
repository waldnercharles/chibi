#include "runtime/behaviors/entities.h"

#include "graph/model/registry.h"
#include "graph/nodes/particle_nodes.h"
#include "graph/nodes/vector_nodes.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/value_array.h"
#include "runtime/behaviors/entity_query.h"
#include "runtime/generated.h"
#include "runtime/materials.h"
#include "runtime/rhythm.h"
#include "runtime/scenes/physics.h"
#include "runtime/visual_motion.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SPAWN_BATCH_MAX = 64,
    SPAWN_INITIALIZER_BATCHES_MAX = 256,
    EVENT_BATCH_MAX = 64
};

typedef struct
{
    game_prototype *prototype;
    char *json;
} prototype_record;

typedef struct
{
    game_prototype *prototype;
    flow_host_request *wait;
    ecs_entity entity;
} spawn_request;

typedef struct
{
    ecs_entity target;
    char *name;
    pin_value *values;
    arena storage;
} event_request;

enum
{
    NEAREST_CACHE_MAX = 8
};
typedef struct
{
    unsigned categories;
    ecs_entity *entities;
} nearest_candidates;
typedef struct
{
    CF_V2 position;
    uint64_t handle;
} nearest_target;

typedef struct
{
    char *name;
    ecs_entity entity;
} global_binding;

typedef struct
{
    char *name;
    pin_value value;
    arena storage;
} global_value;

struct game_entities
{
    ecs_world *world;
    game_behavior_world *behaviors;
    game_materials *materials;
    prototype_record *prototypes;
    ecs_entity *owned;
    spawn_request *spawns;
    ecs_entity *destroy;
    event_request *events;
    nearest_candidates nearest[NEAREST_CACHE_MAX];
    int nearest_count;
    nearest_target *parallel_candidates[32];
    global_binding *global_bindings;
    global_value *global_values;
    bool parallel_queries;
    bool query_scope, profile_queries;
    game_entity_query_profile query_profile;
};

game_entities *game_entities_new(ecs_world *world, game_behavior_world *behaviors)
{
    game_entities *entities = xcalloc(1, sizeof *entities);
    entities->world = world;
    entities->behaviors = behaviors;
    return entities;
}

void game_entities_bind_materials(game_entities *entities, game_materials *materials)
{
    entities->materials = materials;
}

game_materials *game_entities_materials(const game_entities *entities)
{
    return entities ? entities->materials : NULL;
}

bool game_entities_queries_preserved(const behavior_function *function)
{
    const uint64_t dependencies[] = { GAME_COMPONENT_GameTransform.id,
                                      GAME_COMPONENT_GameCollider.id };
    return behavior_function_preserves_components(function, dependencies, 2);
}

void game_entities_query_scope(game_entities *entities, const behavior_function *function)
{
    if (!entities) {
        return;
    }
    bool stable = game_entities_queries_preserved(function);
    if (!stable) {
        entities->nearest_count = 0;
    }
    entities->query_scope = stable;
}
bool game_entities_parallel_begin(game_entities *entities, unsigned categories)
{
    if (!entities || !categories) {
        return true;
    }
    assert(!entities->parallel_queries);
    if (entities->profile_queries) {
        return false;
    }
    for (int i = 0; i < 32; i++) {
        aclear(entities->parallel_candidates[i]);
    }
    ecs_component_id components[] = { GAME_COMPONENT_GameTransform.id,
                                      GAME_COMPONENT_GameCollider.id };
    ecs_query query;
    if (ecs_query_begin(entities->world, &query, components, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            unsigned matching = GAME_GameCollider_get_const(entities->world, entity)
                                    ->category &
                                categories;
            if (!matching || game_entities_is_retiring(entities, entity)) {
                continue;
            }
            CF_V2 position = *GAME_GameTransform_get_const(entities->world, entity);
            if (!isfinite(position.x) || !isfinite(position.y)) {
                continue;
            }
            nearest_target target = { position, ecs_entity_to_u64(entity) };
            while (matching) {
                int bit = __builtin_ctz(matching);
                apush(entities->parallel_candidates[bit], target);
                matching &= matching - 1;
            }
        }
        ecs_query_end(&query);
    }
    entities->parallel_queries = true;
    return true;
}
void game_entities_parallel_end(game_entities *entities)
{
    if (entities) {
        entities->parallel_queries = false;
    }
}
void game_entities_profile_queries(game_entities *entities, bool enabled)
{
    entities->profile_queries = enabled;
    entities->query_profile = (game_entity_query_profile){ 0 };
}
game_entity_query_profile game_entities_query_profile(const game_entities *entities)
{
    return entities->query_profile;
}

static void release_request(spawn_request *request)
{
    flow_host_release(request->wait);
    game_prototype_free(request->prototype);
}

void game_entities_copy_live(const game_entities *entities, ecs_entity **out)
{
    aclear(*out);
    for (int i = 0; i < acount(entities->owned); i++) {
        ecs_entity entity = entities->owned[i];
        if (ecs_entity_is_alive(entities->world, entity)) {
            apush(*out, entity);
        }
    }
}

bool game_entities_owns(const game_entities *entities, ecs_entity entity)
{
    if (!ecs_entity_is_alive(entities->world, entity)) {
        return false;
    }
    for (int i = 0; i < acount(entities->owned); i++) {
        if (ecs_entity_to_u64(entities->owned[i]) == ecs_entity_to_u64(entity)) {
            return true;
        }
    }
    return false;
}

static void clear_global_bindings(game_entities *entities)
{
    for (int i = 0; i < acount(entities->global_bindings); i++) {
        free(entities->global_bindings[i].name);
    }
    aclear(entities->global_bindings);
}

static void clear_global_values(game_entities *entities)
{
    for (int i = 0; i < acount(entities->global_values); i++) {
        global_value *value = &entities->global_values[i];
        arena_free(&value->storage);
        free(value->name);
    }
    aclear(entities->global_values);
}

static global_binding *find_global_binding(game_entities *entities, const char *name)
{
    for (int i = 0; i < acount(entities->global_bindings); i++) {
        global_binding *binding = &entities->global_bindings[i];
        if (strcmp(binding->name, name) != 0) {
            continue;
        }
        return binding;
    }
    return NULL;
}

static const global_binding *find_global_binding_const(const game_entities *entities, const char *name)
{
    for (int i = 0; i < acount(entities->global_bindings); i++) {
        const global_binding *binding = &entities->global_bindings[i];
        if (!strcmp(binding->name, name)) {
            return binding;
        }
    }
    return NULL;
}

static global_value *find_global_value(game_entities *entities, const char *name)
{
    for (int i = 0; i < acount(entities->global_values); i++) {
        global_value *value = &entities->global_values[i];
        if (!strcmp(value->name, name)) {
            return value;
        }
    }
    return NULL;
}

static const global_value *find_global_value_const(const game_entities *entities, const char *name)
{
    for (int i = 0; i < acount(entities->global_values); i++) {
        const global_value *value = &entities->global_values[i];
        if (!strcmp(value->name, name)) {
            return value;
        }
    }
    return NULL;
}

int game_entities_global_name_count(const game_entities *entities)
{
    if (!entities) {
        return 0;
    }
    int count = acount(entities->global_bindings);
    for (int i = 0; i < acount(entities->global_values); i++) {
        if (!find_global_binding_const(entities, entities->global_values[i].name)) {
            count++;
        }
    }
    return count;
}

const char *game_entities_global_name_at(const game_entities *entities, int index)
{
    if (!entities || index < 0) {
        return NULL;
    }
    if (index < acount(entities->global_bindings)) {
        return entities->global_bindings[index].name;
    }
    index -= acount(entities->global_bindings);
    for (int i = 0; i < acount(entities->global_values); i++) {
        const char *name = entities->global_values[i].name;
        if (find_global_binding_const(entities, name)) {
            continue;
        }
        if (index-- == 0) {
            return name;
        }
    }
    return NULL;
}

static bool global_value_type_matches(const pin_value *left, const pin_value *right)
{
    return left->type == right->type && left->container == right->container &&
           ((!left->type_name && !right->type_name) ||
            (left->type_name && right->type_name &&
             !strcmp(left->type_name, right->type_name)));
}

static bool is_entity_value(const pin_value *value)
{
    return value->type == PIN_HANDLE && value->container == PIN_CONTAINER_SINGLE &&
           value->type_name && !strcmp(value->type_name, "ecs.entity");
}

static int set_global_binding(game_entities *entities, const char *name, ecs_entity entity, bool reject_duplicate)
{
    if (!name || !name[0]) {
        return -1;
    }
    global_binding *binding = find_global_binding(entities, name);
    if (binding) {
        if (reject_duplicate && binding->entity.slot &&
            ecs_entity_is_alive(entities->world, binding->entity) &&
            ecs_entity_to_u64(binding->entity) != ecs_entity_to_u64(entity)) {
            return -1;
        }
        binding->entity = entity;
        return 0;
    }
    apush(entities->global_bindings, ((global_binding){ .name = xstrdup(name), .entity = entity }));
    return 0;
}

int game_entities_rebuild_global_bindings(game_entities *entities)
{
    if (!entities) {
        return -1;
    }
    global_binding *previous = entities->global_bindings;
    entities->global_bindings = NULL;
    if (!ecs_world_component(entities->world, GAME_COMPONENT_GameGlobalBinding.id)) {
        entities->global_bindings = previous;
        clear_global_bindings(entities);
        return 0;
    }
    ecs_query query;
    if (ecs_query_begin(
            entities->world,
            &query,
            &(ecs_component_id){ GAME_COMPONENT_GameGlobalBinding.id },
            1
        ) != ECS_OK) {
        clear_global_bindings(entities);
        entities->global_bindings = previous;
        return -1;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameGlobalBinding
            *binding = GAME_GameGlobalBinding_get_const(entities->world, entity);
        if (binding->name && binding->name[0] &&
            set_global_binding(entities, binding->name, entity, true) != 0) {
            ecs_query_end(&query);
            clear_global_bindings(entities);
            entities->global_bindings = previous;
            return -1;
        }
    }
    ecs_query_end(&query);
    global_binding *rebuilt = entities->global_bindings;
    entities->global_bindings = previous;
    clear_global_bindings(entities);
    entities->global_bindings = rebuilt;
    return 0;
}

int game_entities_get_global_value(
    const game_entities *entities,
    const char *name,
    arena *storage,
    pin_value *out
)
{
    if (!entities || !name || !name[0] || !storage || !out) {
        return -1;
    }
    const global_value *value = find_global_value_const(entities, name);
    if (value) {
        if (pin_value_snapshot_to_arena(storage, out, &value->value) != 0) {
            return -1;
        }
        if (is_entity_value(out)) {
            ecs_entity entity = ecs_entity_from_u64(out->u64);
            if (!entity.slot || !ecs_entity_is_alive(entities->world, entity) ||
                game_entities_is_retiring(entities, entity)) {
                out->u64 = 0;
            }
        }
        return 0;
    }
    const global_binding *binding = find_global_binding_const(entities, name);
    if (!binding) {
        return -1;
    }
    ecs_entity entity = binding->entity;
    if (!entity.slot || !ecs_entity_is_alive(entities->world, entity) ||
        game_entities_is_retiring(entities, entity)) {
        entity = (ecs_entity){ 0 };
    }
    *out = (pin_value){ .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(entity) };
    return 0;
}

int game_entities_set_global_value(game_entities *entities, const char *name, const pin_value *value)
{
    if (!entities || !name || !name[0] || !value || value->type == PIN_FLOW ||
        (value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE &&
         value->ptr && !registry_type_by_name(value->type_name))) {
        return -1;
    }
    const global_binding *binding = find_global_binding_const(entities, name);
    if (binding && !is_entity_value(value)) {
        return -1;
    }
    global_value *stored = find_global_value(entities, name);
    if (stored && !global_value_type_matches(&stored->value, value)) {
        return -1;
    }
    if (!stored) {
        apush(entities->global_values, ((global_value){ .name = xstrdup(name) }));
        stored = &alast(entities->global_values);
    }
    arena_reset(&stored->storage);
    if (pin_value_snapshot_to_arena(&stored->storage, &stored->value, value) != 0) {
        arena_reset(&stored->storage);
        stored->value = (pin_value){ 0 };
        return -1;
    }
    return 0;
}

static void release_event(event_request *event)
{
    arena_free(&event->storage);
    afree(event->values);
    free(event->name);
}

void game_entities_reset(game_entities *entities)
{
    game_entities_query_scope(entities, NULL);
    clear_global_bindings(entities);
    clear_global_values(entities);
    for (int i = 0; i < acount(entities->events); i++) {
        release_event(&entities->events[i]);
    }
    aclear(entities->events);
    for (int i = 0; i < acount(entities->spawns); i++) {
        spawn_request *request = &entities->spawns[i];
        flow_host_cancel(request->wait);
        release_request(request);
    }
    aclear(entities->spawns);
    if (acount(entities->owned)) {
        int result = game_behavior_destroy_entities(
            entities->behaviors,
            entities->owned,
            acount(entities->owned)
        );
        assert(!result);
        (void)result;
    }
    aclear(entities->owned);
    aclear(entities->destroy);
}

void game_entities_free(game_entities *entities)
{
    game_entities_reset(entities);
    for (int i = 0; i < acount(entities->prototypes); i++) {
        game_prototype_free(entities->prototypes[i].prototype);
        free(entities->prototypes[i].json);
    }
    afree(entities->prototypes);
    afree(entities->spawns);
    afree(entities->owned);
    afree(entities->destroy);
    afree(entities->global_bindings);
    afree(entities->global_values);
    afree(entities->events);
    for (int i = 0; i < NEAREST_CACHE_MAX; i++) {
        afree(entities->nearest[i].entities);
    }
    for (int i = 0; i < 32; i++) {
        afree(entities->parallel_candidates[i]);
    }
    free(entities);
}

int game_entities_register(game_entities *entities, const game_prototype *prototype)
{
    char *json = game_prototype_save(prototype);
    if (!json) {
        return -1;
    }
    int existing = -1;
    for (int i = 0; i < acount(entities->prototypes); i++) {
        prototype_record *record = &entities->prototypes[i];
        if (strcmp(record->prototype->identity, prototype->identity) != 0) {
            continue;
        }
        if (strcmp(record->json, json) == 0) {
            free(json);
            return 0;
        }
        existing = i;
        break;
    }
    char error[256];
    game_prototype *copy = game_prototype_load(json, prototype->registry, error, sizeof error);
    if (!copy) {
        free(json);
        return -1;
    }
    prototype_record replacement = { .prototype = copy, .json = json };
    if (existing >= 0) {
        game_prototype_free(entities->prototypes[existing].prototype);
        free(entities->prototypes[existing].json);
        entities->prototypes[existing] = replacement;
        return 0;
    }
    apush(entities->prototypes, replacement);
    return 0;
}

static game_prototype *prepare_spawn(game_entities *entities, const char *identity, const char **message)
{
    const game_prototype *prototype = NULL;
    for (int i = 0; identity && i < acount(entities->prototypes); i++) {
        if (strcmp(entities->prototypes[i].prototype->identity, identity) == 0 ||
            strcmp(entities->prototypes[i].prototype->name, identity) == 0) {
            prototype = entities->prototypes[i].prototype;
            break;
        }
    }
    if (!prototype) {
        *message = "Spawn Prototype: unknown prototype";
        return NULL;
    }
    char error[256];
    game_prototype *copy = game_prototype_clone(prototype, error, sizeof error);
    if (!copy) {
        *message = "Spawn Prototype: could not capture defaults";
    }
    return copy;
}

int game_entities_request_spawn(game_entities *entities, flow_context *ctx, const char *identity)
{
    const char *message = NULL;
    game_prototype *copy = prepare_spawn(entities, identity, &message);
    if (!copy) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, message);
    }
    int result = flow_context_continue(ctx, 0);
    flow_host_request *wait = result < 0 ? NULL : flow_context_host_wait(ctx);
    if (!wait) {
        game_prototype_free(copy);
        return result < 0 ? result : FLOW_ERR_STATE;
    }
    apush(entities->spawns, ((spawn_request){ .prototype = copy, .wait = wait }));
    return FLOW_OK;
}

void game_entities_request_destroy(game_entities *entities, ecs_entity entity)
{
    apush(entities->destroy, entity);
}

static bool destroy_requested(const game_entities *entities, ecs_entity entity)
{
    for (int i = 0; i < acount(entities->destroy); i++) {
        if (ecs_entity_to_u64(entities->destroy[i]) == ecs_entity_to_u64(entity)) {
            return true;
        }
    }
    return false;
}

bool game_entities_is_retiring(const game_entities *entities, ecs_entity entity)
{
    return destroy_requested(entities, entity) ||
           ecs_entity_is_retiring(entities->world, entity);
}

int game_entities_pending_events(const game_entities *entities)
{
    return acount(entities->events);
}

int game_entities_request_event(game_entities *entities, ecs_entity target, const char *name, flow_inputs inputs)
{
    if (!name || !name[0] || inputs.count < 0 || inputs.user_data ||
        (inputs.count && !inputs.values)) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < inputs.count; i++) {
        if (inputs.values[i].container != PIN_CONTAINER_SINGLE) {
            return FLOW_ERR_ARGUMENT;
        }
    }
    event_request event = { .target = target, .name = xstrdup(name) };
    for (int i = 0; i < inputs.count; i++) {
        pin_value copy;
        if (pin_value_snapshot_to_arena(&event.storage, &copy, &inputs.values[i]) != 0) {
            release_event(&event);
            return FLOW_ERR_ARGUMENT;
        }
        apush(event.values, copy);
    }
    apush(entities->events, event);
    return FLOW_OK;
}

int game_entities_dispatch_events(game_entities *entities)
{
    if (game_entities_initializing(entities)) {
        return FLOW_OK;
    }
    event_request *batch = entities->events;
    entities->events = NULL;
    event_request *pending = NULL;
    int error = FLOW_OK;
    for (int i = 0; i < acount(batch); i++) {
        event_request *event = &batch[i];
        if (i >= EVENT_BATCH_MAX) {
            apush(pending, *event);
            continue;
        }
        if (ecs_entity_is_alive(entities->world, event->target) &&
            !game_entities_is_retiring(entities, event->target)) {
            int result = game_behavior_dispatch(
                entities->behaviors,
                event->target,
                event->name,
                (flow_inputs){ .values = event->values, .count = acount(event->values) },
                NULL
            );
            if (result < 0 && error == FLOW_OK) {
                error = result;
            }
        }
        release_event(event);
    }
    afree(batch);
    for (int i = 0; i < acount(entities->events); i++) {
        apush(pending, entities->events[i]);
    }
    afree(entities->events);
    entities->events = pending;
    return error;
}

bool game_entities_initializing(const game_entities *entities)
{
    for (int i = 0; i < acount(entities->spawns); i++) {
        if (entities->spawns[i].entity.slot) {
            return true;
        }
    }
    return false;
}

static int tick_initializers(game_entities *entities, double monotonic_time)
{
    flow_run **roots = NULL;
    for (int i = 0; i < acount(entities->spawns); i++) {
        spawn_request *request = &entities->spawns[i];
        if (!request->entity.slot) {
            continue;
        }
        flow_run *run = flow_host_run(request->wait);
        if (!run) {
            continue;
        }
        bool found = false;
        for (int j = 0; j < acount(roots); j++) {
            found |= roots[j] == run;
        }
        if (!found) {
            apush(roots, run);
        }
    }
    int result = FLOW_OK;
    for (int i = 0; i < acount(roots); i++) {
        flow_run *run = roots[i];
        if (flow_run_status(run) == FLOW_PAUSED ||
            !flow_run_ready(run, monotonic_time)) {
            continue;
        }
        int tick = flow_run_tick_at(run, monotonic_time);
        if (tick < 0 && result == FLOW_OK) {
            result = tick;
        }
    }
    afree(roots);
    return result;
}

static int finish_spawn(game_entities *entities, spawn_request *request)
{
    flow_run *run = flow_host_run(request->wait);
    if (!run) {
        return -1;
    }
    if (!request->entity.slot) {
        char error[256];
        if (game_prototype_spawn_components(
                request->prototype,
                entities->world,
                &request->entity,
                error,
                sizeof error
            ) != ECS_OK) {
            flow_host_cancel(request->wait);
            return -1;
        }
        apush(entities->owned, request->entity);
        if (game_entities_rebuild_global_bindings(entities) != 0) {
            flow_host_cancel(request->wait);
            return -1;
        }
        pin_value value = { .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .u64 = ecs_entity_to_u64(request->entity) };
        if (!flow_host_complete(request->wait, 1, value) ||
            flow_host_resume(request->wait) < 0) {
            return -1;
        }
    }
    run = flow_host_run(request->wait);
    if (!run || flow_run_status(run) == FLOW_FAILED || flow_run_status(run) == FLOW_IDLE) {
        return -1;
    }
    if (!ecs_entity_is_alive(entities->world, request->entity) ||
        destroy_requested(entities, request->entity)) {
        flow_host_cancel(request->wait);
        return -1;
    }
    if (flow_run_status(run) == FLOW_PAUSED || flow_run_status(run) == FLOW_RUNNING ||
        flow_run_status(run) == FLOW_SUSPENDED) {
        return 1;
    }
    for (int i = 0; i < acount(request->prototype->graphs); i++) {
        if (game_behavior_attach(
                entities->behaviors,
                request->entity,
                request->prototype->graphs[i]
            ) != 0) {
            return -1;
        }
    }
    return 0;
}

int game_entities_flush(game_entities *entities)
{
    spawn_request *batch = entities->spawns;
    entities->spawns = NULL;
    int blocked = 0;
    int processed = 0;
    spawn_request *pending = NULL;
    for (int i = 0; i < acount(batch); i++) {
        spawn_request *request = &batch[i];
        if (processed >= SPAWN_BATCH_MAX) {
            apush(pending, *request);
            blocked |= request->entity.slot != 0;
            continue;
        }
        bool started = request->entity.slot != 0;
        int result = finish_spawn(entities, request);
        /* Waiting ancestors share the newest child's host continuation. They
         * must not consume the batch and starve that child's spawn request. */
        processed += !started || result <= 0;
        if (result > 0) {
            apush(pending, *request);
            blocked = 1;
            continue;
        }
        if (result < 0 && request->entity.slot &&
            ecs_entity_is_alive(entities->world, request->entity)) {
            game_behavior_destroy_entity(entities->behaviors, request->entity);
        }
        release_request(request);
    }
    afree(batch);
    /* Keep older requests ahead of work submitted by initialization or Begin. */
    for (int i = 0; i < acount(entities->spawns); i++) {
        apush(pending, entities->spawns[i]);
    }
    afree(entities->spawns);
    entities->spawns = pending;
    if (acount(entities->destroy)) {
        int result = game_behavior_destroy_entities(
            entities->behaviors,
            entities->destroy,
            acount(entities->destroy)
        );
        if (result) {
            return result;
        }
    }
    aclear(entities->destroy);
    int alive = 0;
    for (int i = 0; i < acount(entities->owned); i++) {
        if (ecs_entity_is_alive(entities->world, entities->owned[i])) {
            entities->owned[alive++] = entities->owned[i];
        }
    }
    asetlen(entities->owned, alive);
    return blocked;
}

static bool initializer_ready(const game_entities *entities, double now)
{
    for (int i = 0; i < acount(entities->spawns); i++) {
        const spawn_request *request = &entities->spawns[i];
        flow_run *run = flow_host_run(request->wait);
        if (!request->entity.slot || !run ||
            !ecs_entity_is_alive(entities->world, request->entity) ||
            destroy_requested(entities, request->entity)) {
            return true;
        }
        flow_state state = flow_run_status(run);
        if (state == FLOW_COMPLETED || state == FLOW_FAILED ||
            state == FLOW_IDLE || flow_run_ready(run, now)) {
            return true;
        }
    }
    return false;
}

int game_entities_flush_initializers(game_entities *entities, double now)
{
    for (int batch = 0; batch < SPAWN_INITIALIZER_BATCHES_MAX; batch++) {
        int result = game_entities_flush(entities);
        if (result < 0) {
            return result;
        }
        result = tick_initializers(entities, now);
        if (result < 0) {
            return result;
        }
        if (!initializer_ready(entities, now)) {
            return game_entities_initializing(entities);
        }
    }
    return FLOW_ERR_STEP_BUDGET;
}

static int spawn_node(node *, flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    pin_value prototype;
    if (!entities) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Spawn Prototype requires a game world");
    }
    int result = flow_context_eval_data(ctx, 1, &prototype);
    return result < 0 ? result
                      : game_entities_request_spawn(entities, ctx, prototype.str);
}

static int destroy_command(void *service, const char *, const pin_value *arguments, int count)
{
    assert(count == 1);
    (void)count;
    game_entities_request_destroy(service, ecs_entity_from_u64(arguments[0].u64));
    return FLOW_OK;
}

static int spawn_command(
    void *service,
    const char *,
    const pin_value *arguments,
    int count,
    flow_host_request *request,
    const char **message
)
{
    assert(count == 1);
    (void)count;
    game_entities *entities = service;
    game_prototype *copy = prepare_spawn(entities, arguments[0].str, message);
    if (!copy) {
        return FLOW_ERR_NODE;
    }
    apush(entities->spawns, ((spawn_request){ .prototype = copy, .wait = request }));
    return FLOW_OK;
}

static const node_command SPAWN_COMMAND = {
    .service = "game.entities",
    .missing_service = "Spawn Prototype requires a game world",
    .begin = spawn_command
};
static int send_command(void *service, const char *reference, const pin_value *arguments, int count)
{
    assert(count >= 1);
    return game_entities_request_event(
        service,
        ecs_entity_from_u64(arguments[0].u64),
        reference,
        (flow_inputs){ .values = arguments + 1, .count = count - 1 }
    );
}
static const node_command DESTROY_COMMAND = {
    .service = "game.entities",
    .missing_service = "Destroy Entity requires a game world",
    .invoke = destroy_command
};
static const node_command SEND_COMMAND = {
    .service = "game.entities",
    .missing_service = "Send Event requires a game world",
    .missing_reference = "Choose an event definition",
    .failure_message = "Could not queue event payload",
    .invoke = send_command
};

static int destroy_node(node *, flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    pin_value entity;
    if (!entities) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Destroy Entity requires a game world");
    }
    int result = flow_context_eval_data(ctx, 1, &entity);
    if (result < 0) {
        return result;
    }
    destroy_command(entities, NULL, &entity, 1);
    return flow_context_continue(ctx, 0);
}

static int send_event_node(node *n, flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    if (!entities) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Send Event requires a game world");
    }
    if (!n->reference_name || !n->reference_name[0]) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Choose an event definition");
    }
    int count = n->input_count - 1;
    pin_value *values = flow_context_scratch_alloc(ctx, sizeof *values * (size_t)count);
    for (int i = 0; i < count; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    int result = send_command(entities, n->reference_name, values, count);
    return result < 0
             ? flow_context_fail(ctx, result, "Could not queue event payload")
             : flow_context_continue(ctx, 0);
}

static void nearest_position(nearest_target target, const pin_value *inputs, pin_value *out, double *best)
{
    if (target.handle == inputs[3].u64) {
        return;
    }
    CF_V2 origin = *(const CF_V2 *)inputs[0].ptr;
    double x = (double)target.position.x - origin.x,
           y = (double)target.position.y - origin.y;
    double distance = x * x + y * y;
    if (distance > *best ||
        (distance == *best && out[0].b && target.handle > out[1].u64)) {
        return;
    }
    *best = distance;
    out[0].b = true;
    out[1].u64 = target.handle;
    *(CF_V2 *)out[2].ptr = target.position;
    out[3].f = (float)sqrt(distance);
}

static void nearest_candidate(
    game_entities *entities,
    ecs_entity entity,
    const pin_value *inputs,
    pin_value *out,
    double *best
)
{
    if (destroy_requested(entities, entity)) {
        return;
    }
    CF_V2 position = *GAME_GameTransform_get_const(entities->world, entity);
    if (!isfinite(position.x) || !isfinite(position.y)) {
        return;
    }
    nearest_position((nearest_target){ position, ecs_entity_to_u64(entity) }, inputs, out, best);
}

static const char *nearest_entity_data(void *service, const pin_value *inputs, pin_value *out)
{
    game_entities *entities = service;
    if (!entities) {
        return "Find Nearest Entity requires a game world";
    }
    CF_V2 origin = inputs[0].ptr ? *(const CF_V2 *)inputs[0].ptr : (CF_V2){ NAN, NAN };
    CF_V2 *position = out[2].ptr;
    *position = (CF_V2){ 0 };
    out[0].b = false;
    out[1].u64 = 0;
    out[3].f = 0;
    double radius = inputs[1].f;
    if (!isfinite(origin.x) || !isfinite(origin.y) || !isfinite(radius) ||
        radius < 0 || !inputs[2].u) {
        return NULL;
    }
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameCollider.id };
    if (!ecs_world_component(entities->world, required[0]) ||
        !ecs_world_component(entities->world, required[1])) {
        return NULL;
    }
    double best = radius * radius;
    if (entities->parallel_queries) {
        unsigned categories = inputs[2].u;
        while (categories) {
            int bit = __builtin_ctz(categories);
            const nearest_target *candidates = entities->parallel_candidates[bit];
            for (int i = 0; i < acount(candidates); i++) {
                nearest_position(candidates[i], inputs, out, &best);
            }
            categories &= categories - 1;
        }
        return NULL;
    }
    nearest_candidates *cached = NULL;
    if (entities->profile_queries) {
        entities->query_profile.searches++;
    }
    for (int i = 0; entities->query_scope && i < entities->nearest_count; i++) {
        if (entities->nearest[i].categories != inputs[2].u) {
            continue;
        }
        cached = &entities->nearest[i];
        for (int j = 0; j < acount(cached->entities); j++) {
            nearest_candidate(entities, cached->entities[j], inputs, out, &best);
        }
        if (entities->profile_queries) {
            entities->query_profile.matching_candidates += acount(cached->entities);
        }
        return NULL;
    }
    ecs_query query;
    if (ecs_query_begin(entities->world, &query, required, 2) != ECS_OK) {
        return "Find Nearest Entity could not query the world";
    }
    if (entities->query_scope && entities->nearest_count < NEAREST_CACHE_MAX) {
        cached = &entities->nearest[entities->nearest_count++];
        cached->categories = inputs[2].u;
        aclear(cached->entities);
        if (entities->profile_queries) {
            entities->query_profile.cache_builds++;
        }
    }
    uint64_t candidates = 0, matching = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        candidates++;
        const GameCollider *collider = GAME_GameCollider_get_const(entities->world, entity);
        if (!(collider->category & inputs[2].u)) {
            continue;
        }
        matching++;
        if (cached) {
            apush(cached->entities, entity);
        }
        nearest_candidate(entities, entity, inputs, out, &best);
    }
    ecs_query_end(&query);
    if (entities->profile_queries) {
        entities->query_profile.world_candidates += candidates;
        entities->query_profile.matching_candidates += matching;
    }
    return NULL;
}

static const char *nearest_entity(const eval_context *ctx, const pin_value *inputs, arena *a, pin_value *out)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    if (!entities) {
        return "Find Nearest Entity requires a game world";
    }
    out[0] = (pin_value){ .type = PIN_BOOL, .type_name = "bool" };
    out[1] = (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity" };
    out[2] = (pin_value){ .type = PIN_STRUCT,
                          .type_name = "CF_V2",
                          .ptr = arena_alloc(a, sizeof(CF_V2)) };
    out[3] = (pin_value){ .type = PIN_F32, .type_name = "float" };
    return nearest_entity_data(entities, inputs, out);
}
static const node_host_call NEAREST_CALL = { .service = "game.entities",
                                             .invoke = nearest_entity_data,
                                             .reusable = true };

unsigned game_entities_query_categories(const behavior_function *function)
{
    return behavior_function_host_mask(function, &NEAREST_CALL, 2);
}

static const node_access NEAREST_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
static const uint64_t *const nearest_reads[] = { &GAME_COMPONENT_GameTransform.id,
                                                 &GAME_COMPONENT_GameCollider.id,
                                                 NULL };
static const node_access NEAREST_PARALLEL_ACCESS = { .read_refs = nearest_reads,
                                                     .flags = NODE_EFFECT_WORLD_READ |
                                                              NODE_EFFECT_PARALLEL };

static const char *get_global_variable(
    const eval_context *ctx,
    const pin_value *inputs,
    arena *storage,
    pin_value *out
)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    if (!entities) {
        return "Get Global Variable requires a game world";
    }
    if (game_entities_get_global_value(entities, inputs[0].str, storage, out) != 0) {
        return "Get Global Variable references an unknown value";
    }
    return NULL;
}

static int set_global_variable_node(node *, flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    if (!entities) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Set Global Variable requires a game world");
    }
    pin_value name, value;
    int result = flow_context_eval_data(ctx, 1, &name);
    if (result < 0) {
        return result;
    }
    result = flow_context_eval_data(ctx, 2, &value);
    if (result < 0) {
        return result;
    }
    if (game_entities_set_global_value(entities, name.str, &value) != 0) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Set Global Variable received an invalid value");
    }
    *flow_context_output_value(ctx, 1) = value;
    return flow_context_continue(ctx, 0);
}

static const node_access GLOBAL_VARIABLE_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
static const node_access GLOBAL_VARIABLE_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static pin_spec GET_GLOBAL_VARIABLE_IN[] = {
    { .name = "Name", .type = PIN_NAME, .type_name = "name" }
};
static pin_spec GET_GLOBAL_VARIABLE_OUT[] = { { .name = "Value",
                                                .type = PIN_FLOW,
                                                .kind = PIN_DATA,
                                                .flags = PIN_FLAG_WILDCARD,
                                                .group = 1 } };
static node_desc GET_GLOBAL_VARIABLE_NODE = {
    .name = "Get Global Variable",
    .input_count = 1,
    .inputs = GET_GLOBAL_VARIABLE_IN,
    .output_count = 1,
    .outputs = GET_GLOBAL_VARIABLE_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = get_global_variable,
    .category = "Global",
    .keywords = "global shared variable binding reference entity value",
    .access = &GLOBAL_VARIABLE_READ_ACCESS,
};
static pin_spec SET_GLOBAL_VARIABLE_IN[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Name", .type = PIN_NAME, .type_name = "name" },
    { .name = "Value", .type = PIN_FLOW, .kind = PIN_DATA, .flags = PIN_FLAG_WILDCARD, .group = 1 },
};
static pin_spec SET_GLOBAL_VARIABLE_OUT[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Value", .type = PIN_FLOW, .kind = PIN_DATA, .flags = PIN_FLAG_WILDCARD, .group = 1 },
};
static node_desc SET_GLOBAL_VARIABLE_NODE = {
    .name = "Set Global Variable",
    .input_count = 3,
    .inputs = SET_GLOBAL_VARIABLE_IN,
    .output_count = 2,
    .outputs = SET_GLOBAL_VARIABLE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = set_global_variable_node,
    .display_class = NODE_DISPLAY_VARIABLE,
    .category = "Global",
    .keywords = "global shared variable binding reference entity value publish",
    .access = &GLOBAL_VARIABLE_WRITE_ACCESS,
};

static const ecs_component_desc *query_component(game_entities *entities, const char *key)
{
    for (int i = 0; key && i < ecs_world_component_count(entities->world); i++) {
        const ecs_component_desc *component = ecs_world_component_at(entities->world, i);
        if (!strcmp(component->key ? component->key : component->name, key)) {
            return component;
        }
    }
    return NULL;
}

static const char *has_entities_with_component(
    const eval_context *ctx,
    const pin_value *inputs,
    arena *,
    pin_value *out
)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    if (!entities) {
        return "Has Entities With Component requires a game world";
    }
    const char *key = inputs[0].str;
    if (!key || !key[0]) {
        return "Choose a component for Has Entities With Component";
    }
    const ecs_component_desc *component = query_component(entities, key);
    if (!component) {
        return "Selected component is not registered in this world";
    }
    *out = (pin_value){
        .type = PIN_BOOL,
        .type_name = "bool",
        .b = ecs_component_has_entities(
            entities->world,
            component->id,
            entities->destroy,
            acount(entities->destroy)
        ),
    };
    return NULL;
}

static const char *find_entity_snapshot(void *service, const char *key, pin_array *out)
{
    game_entities *entities = service;
    out->count = 0;
    if (!entities) {
        return "Find Entities With Component requires a game world";
    }
    const ecs_component_desc *component = query_component(entities, key);
    if (!component) {
        return "Choose a registered component for Find Entities With Component";
    }
    ecs_query query;
    if (ecs_query_begin(entities->world, &query, &component->id, 1) != ECS_OK) {
        return "Find Entities With Component could not query the world";
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (game_entities_is_retiring(entities, entity)) {
            continue;
        }
        if (out->count == out->capacity) {
            int result = pin_array_reserve(out, out->capacity ? out->capacity * 2 : 8);
            assert(!result);
            (void)result;
        }
        /* Handles have no payload ownership; append directly to the snapshot. */
        out->items[out->count++] = (pin_value){ .type = PIN_HANDLE,
                                                .type_name = "ecs.entity",
                                                .u64 = ecs_entity_to_u64(entity) };
    }
    ecs_query_end(&query);
    return NULL;
}
static const char *find_entities_with_component(
    const eval_context *ctx,
    const pin_value *inputs,
    arena *storage,
    pin_value *out
)
{
    pin_array snapshot = { .element_type = PIN_HANDLE, .type_name = "ecs.entity" };
    const char *error = find_entity_snapshot(
        eval_context_service(ctx, "game.entities"),
        inputs[0].str,
        &snapshot
    );
    if (!error) {
        *out = (pin_value){ .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .container = PIN_CONTAINER_ARRAY,
                            .ptr = pin_array_clone_to_arena(storage, &snapshot) };
    }
    free(snapshot.items);
    return error;
}

static pin_spec HAS_ENTITIES_IN[] = { { .name = "Component", .type = PIN_NAME } };
static pin_spec HAS_ENTITIES_OUT[] = {
    { .name = "Found", .type = PIN_BOOL, .type_name = "bool" }
};
static node_desc HAS_ENTITIES_NODE = {
    .name = "Has Entities With Component",
    .input_count = 1,
    .inputs = HAS_ENTITIES_IN,
    .output_count = 1,
    .outputs = HAS_ENTITIES_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = has_entities_with_component,
    .category = "Entities",
    .keywords = "query has any exists find component present alive",
    .access = &NEAREST_ACCESS,
};
static pin_spec FIND_ENTITIES_OUT[] = { { .name = "Entities",
                                          .type = PIN_HANDLE,
                                          .type_name = "ecs.entity",
                                          .container = PIN_CONTAINER_ARRAY } };
static node_desc FIND_ENTITIES_NODE = {
    .name = "Find Entities With Component",
    .input_count = 1,
    .inputs = HAS_ENTITIES_IN,
    .output_count = 1,
    .outputs = FIND_ENTITIES_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = find_entities_with_component,
    .query_service = "game.entities",
    .query = find_entity_snapshot,
    .work = NODE_WORK_WORLD_QUERY,
    .category = "Entities",
    .keywords = "query find component entities array snapshot",
    .access = &NEAREST_ACCESS,
};
static pin_spec NEAREST_IN[] = {
    { .name = "Position", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Radius", .type = PIN_F32, .type_name = "float" },
    { .name = "Categories", .type = PIN_U32, .type_name = "unsigned int" },
    { .name = "Exclude", .type = PIN_HANDLE, .type_name = "ecs.entity" },
};
static pin_spec NEAREST_OUT[] = {
    { .name = "Found", .type = PIN_BOOL, .type_name = "bool" },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Position", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Distance", .type = PIN_F32, .type_name = "float" },
};
static node_desc NEAREST_NODE = {
    .name = "Find Nearest Entity",
    .input_count = 4,
    .inputs = NEAREST_IN,
    .output_count = 4,
    .outputs = NEAREST_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = nearest_entity,
    .host_call = &NEAREST_CALL,
    .work = NODE_WORK_WORLD_QUERY,
    .category = "Entities",
    .keywords = "nearest find search entity position radius category target",
    .access = &NEAREST_PARALLEL_ACCESS,
};

static const node_access STRUCTURAL_ACCESS = { .flags = NODE_EFFECT_STRUCTURAL };
static pin_spec SPAWN_IN[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
                               { .name = "Prototype", .type = PIN_NAME } };
static pin_spec SPAWN_OUT[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" }
};
static pin_spec DESTROY_IN[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" }
};
static pin_spec DESTROY_OUT[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
static node_desc SPAWN_NODE = { .name = "Spawn Prototype",
                                .input_count = 2,
                                .output_count = 2,
                                .inputs = SPAWN_IN,
                                .outputs = SPAWN_OUT,
                                .flow_kind = NODE_FLOW_CUSTOM,
                                .flow = spawn_node,
                                .command = &SPAWN_COMMAND,
                                .latent = 1,
                                .display_class = NODE_DISPLAY_FLOW,
                                .category = "Entities",
                                .access = &STRUCTURAL_ACCESS };
static node_desc DESTROY_NODE = { .name = "Destroy Entity",
                                  .input_count = 2,
                                  .output_count = 1,
                                  .inputs = DESTROY_IN,
                                  .outputs = DESTROY_OUT,
                                  .flow_kind = NODE_FLOW_CUSTOM,
                                  .flow = destroy_node,
                                  .command = &DESTROY_COMMAND,
                                  .display_class = NODE_DISPLAY_FLOW,
                                  .category = "Entities",
                                  .access = &STRUCTURAL_ACCESS };
static pin_spec DAMAGE_IN[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
};
static node_desc DAMAGE_NODE = {
    .name = "Send Event",
    .input_count = 2,
    .output_count = 1,
    .inputs = DAMAGE_IN,
    .outputs = DESTROY_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = send_event_node,
    .command = &SEND_COMMAND,
    .display_class = NODE_DISPLAY_EVENT,
    .category = "Events",
    .keywords = "send entity custom event message",
    .access = &STRUCTURAL_ACCESS,
};
static node_desc *const ENTITY_NODES[] = { &SPAWN_NODE,
                                           &DESTROY_NODE,
                                           &DAMAGE_NODE,
                                           &NEAREST_NODE,
                                           &GET_GLOBAL_VARIABLE_NODE,
                                           &SET_GLOBAL_VARIABLE_NODE,
                                           &HAS_ENTITIES_NODE,
                                           &NODE_Normalize_Vector_2,
                                           &FIND_ENTITIES_NODE,
                                           &NODE_Count_Matching_Entities,
                                           &GAME_NODE_GetContactEntities,
                                           &GAME_NODE_Raycast,
                                           &NODE_Direction_To,
                                           &NODE_Atan2,
                                           &NODE_Random_Range_Vector,
                                           &NODE_Set_Visual_Reaction,
                                           &NODE_Emit_Particles,
                                           &NODE_Emit_Directed_Particles,
                                           &NODE_Scene_Rhythm };
static const type_desc ENTITY_TYPE = { .name = "ecs.entity", .size = sizeof(uint64_t) };
static const type_desc *const ENTITY_TYPES[] = { &ENTITY_TYPE };
const registry_pack GAME_ENTITY_PACK = {
    .name = "game.entities",
    .nodes = ENTITY_NODES,
    .node_count = (int)(sizeof ENTITY_NODES / sizeof ENTITY_NODES[0]),
    .types = ENTITY_TYPES,
    .type_count = 1
};
