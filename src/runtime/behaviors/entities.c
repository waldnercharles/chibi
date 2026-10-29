#include "runtime/behaviors/entities.h"

#include "execution/function.h"
#include "execution/request.h"
#include "execution/status.h"
#include "runtime/behaviors/entities_native.h"
#include "runtime/component_catalog.h"
#include "runtime/tilemap.h"
#include "runtime/transform_propagation.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"
#include "types/trace.h"

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
    scene *prototype;
    flow_host_request *wait;
    ecs_entity entity;
    ecs_entity *tree; /* the entity of each prototype object, the first being entity */
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

struct entities
{
    ecs_world *world;
    entity_host host;
    materials *materials;
    prototypes *prototypes;
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
    entity_query_profile query_profile;
    struct tilemap_entry *tilemaps;
};

typedef struct tilemap_entry
{
    ecs_entity entity;
    tilemap_grid grid;
} tilemap_entry;

const tilemap_grid *entities_tilemap(entities *entities, ecs_entity entity, const char **error)
{
    *error = NULL;
    const Tilemap *map = ecs_entity_is_alive(entities->world, entity) &&
                                 !entities_is_retiring(entities, entity)
                           ? Tilemap_get_const(entities->world, entity)
                           : NULL;
    if (!map) {
        *error = "Tilemap query requires a live Tilemap entity";
        return NULL;
    }
    for (int i = acount(entities->tilemaps) - 1; i >= 0; i--) {
        ecs_entity cached = entities->tilemaps[i].entity;
        if (!ecs_entity_is_alive(entities->world, cached) ||
            !Tilemap_get_const(entities->world, cached)) {
            tilemap_grid_free(&entities->tilemaps[i].grid);
            entities->tilemaps[i] = alast(entities->tilemaps);
            apop(entities->tilemaps);
        }
    }
    tilemap_entry *entry = NULL;
    for (int i = 0; i < acount(entities->tilemaps); i++) {
        if (ecs_entity_to_u64(entities->tilemaps[i].entity) ==
            ecs_entity_to_u64(entity)) {
            entry = &entities->tilemaps[i];
            break;
        }
    }
    if (!entry) {
        apush(entities->tilemaps, ((tilemap_entry){ .entity = entity }));
        entry = &alast(entities->tilemaps);
    }
    if (!tilemap_grid_update(&entry->grid, map->cells, map->columns, map->rows, map->frame_width, map->frame_height)) {
        *error = entry->grid.error;
        return NULL;
    }
    return &entry->grid;
}

ecs_world *entities_world(const entities *entities)
{
    return entities->world;
}

entities *entities_new(ecs_world *world, const entity_host *host)
{
    entities *entities = xcalloc(1, sizeof *entities);
    entities->world = world;
    if (host) {
        assert(host->destroy && host->attach && host->dispatch);
        entities->host = *host;
    }
    entities->prototypes = prototypes_new();
    return entities;
}

void entities_bind_materials(entities *entities, materials *materials)
{
    entities->materials = materials;
}

materials *entities_materials(const entities *entities)
{
    return entities ? entities->materials : NULL;
}

bool entities_queries_preserved(const behavior_function *function)
{
    /* Positions are world placements, which parents' Orientation turns. */
    const uint64_t dependencies[] = { COMPONENT_Transform.id,
                                      COMPONENT_Orientation.id,
                                      COMPONENT_Collider.id };
    return behavior_function_preserves_components(function, dependencies, 3);
}

void entities_query_scope(entities *entities, const behavior_function *function)
{
    if (!entities) {
        return;
    }
    bool stable = entities_queries_preserved(function);
    if (!stable) {
        entities->nearest_count = 0;
    }
    entities->query_scope = stable;
}

static void release_request(spawn_request *request)
{
    flow_host_release(request->wait);
    scene_free(request->prototype);
    afree(request->tree);
}

void entities_copy_live(const entities *entities, ecs_entity **out)
{
    aclear(*out);
    for (int i = 0; i < acount(entities->owned); i++) {
        ecs_entity entity = entities->owned[i];
        if (ecs_entity_is_alive(entities->world, entity)) {
            apush(*out, entity);
        }
    }
}

bool entities_owns(const entities *entities, ecs_entity entity)
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

static void clear_global_bindings(entities *entities)
{
    for (int i = 0; i < acount(entities->global_bindings); i++) {
        free(entities->global_bindings[i].name);
    }
    afree(entities->global_bindings);
}

static void clear_global_values(entities *entities)
{
    for (int i = 0; i < acount(entities->global_values); i++) {
        global_value *value = &entities->global_values[i];
        arena_free(&value->storage);
        free(value->name);
    }
    aclear(entities->global_values);
}

static global_binding *find_global_binding(entities *entities, const char *name)
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

static const global_binding *find_global_binding_const(const entities *entities, const char *name)
{
    for (int i = 0; i < acount(entities->global_bindings); i++) {
        const global_binding *binding = &entities->global_bindings[i];
        if (!strcmp(binding->name, name)) {
            return binding;
        }
    }
    return NULL;
}

static global_value *find_global_value(entities *entities, const char *name)
{
    for (int i = 0; i < acount(entities->global_values); i++) {
        global_value *value = &entities->global_values[i];
        if (!strcmp(value->name, name)) {
            return value;
        }
    }
    return NULL;
}

static const global_value *find_global_value_const(const entities *entities, const char *name)
{
    for (int i = 0; i < acount(entities->global_values); i++) {
        const global_value *value = &entities->global_values[i];
        if (!strcmp(value->name, name)) {
            return value;
        }
    }
    return NULL;
}

int entities_global_name_count(const entities *entities)
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

const char *entities_global_name_at(const entities *entities, int index)
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

static bool global_value_header_valid(const pin_value *value)
{
    if (!value || value->type <= PIN_FLOW || value->type >= PIN_POINTER ||
        value->container < PIN_CONTAINER_SINGLE ||
        value->container > PIN_CONTAINER_MAP) {
        return false;
    }
    if (value->record) {
        return value->type == PIN_STRUCT && pin_record_valid(value->record) &&
               pin_value_type(value);
    }
    return value->type != PIN_STRUCT || value->container != PIN_CONTAINER_SINGLE ||
           !value->ptr || pin_value_type(value);
}
static bool global_value_type_matches(const pin_value *left, const pin_value *right)
{
    /* A number's type names it; only named types compare their names. */
    bool named = left->type == PIN_STRUCT || left->type == PIN_ENUM ||
                 left->type == PIN_HANDLE;
    return left->type == right->type && left->container == right->container &&
           pin_value_layout_same(left, right) &&
           (!named || (!left->type_name && !right->type_name) ||
            (left->type_name && right->type_name &&
             !strcmp(left->type_name, right->type_name)));
}

static bool is_entity_value(const pin_value *value)
{
    return value->type == PIN_HANDLE && value->container == PIN_CONTAINER_SINGLE &&
           value->type_name && !strcmp(value->type_name, "ecs.entity");
}

static int set_global_binding(entities *entities, const char *name, ecs_entity entity, bool reject_duplicate)
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

int entities_rebuild_global_bindings(entities *entities)
{
    if (!entities) {
        return -1;
    }
    global_binding *previous = entities->global_bindings;
    entities->global_bindings = NULL;
    if (!ecs_world_component(entities->world, COMPONENT_GlobalBinding.id)) {
        entities->global_bindings = previous;
        clear_global_bindings(entities);
        return 0;
    }
    ecs_query query;
    if (ecs_query_begin(
            entities->world,
            &query,
            &(ecs_component_id){ COMPONENT_GlobalBinding.id },
            1
        ) != ECS_OK) {
        clear_global_bindings(entities);
        entities->global_bindings = previous;
        return -1;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GlobalBinding *binding = GlobalBinding_get_const(entities->world, entity);
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

int entities_get_global_value(const entities *entities, const char *name, arena *storage, pin_value *out)
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
                entities_is_retiring(entities, entity)) {
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
        entities_is_retiring(entities, entity)) {
        entity = (ecs_entity){ 0 };
    }
    *out = (pin_value){ .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(entity) };
    return 0;
}

int entities_set_global_value(entities *entities, const char *name, const pin_value *value)
{
    if (!entities || !name || !name[0] || !global_value_header_valid(value) ||
        pin_value_has_managed_pointer(value)) {
        return -1;
    }
    const global_binding *binding = find_global_binding_const(entities, name);
    if (binding && !is_entity_value(value)) {
        return -1;
    }
    global_value *stored = find_global_value(entities, name);
    /* Float and double are one Real: a global keeps the width it was first
       written with. */
    pin_value coerced = *value;
    if (stored && coerced.type == PIN_F32 && stored->value.type == PIN_F64) {
        coerced.d = (double)coerced.f;
        coerced.type = PIN_F64;
        coerced.type_name = stored->value.type_name;
    } else if (stored && coerced.type == PIN_F64 && stored->value.type == PIN_F32) {
        coerced.f = (float)coerced.d;
        coerced.type = PIN_F32;
        coerced.type_name = stored->value.type_name;
    }
    if (stored && !global_value_type_matches(&stored->value, &coerced)) {
        return -1;
    }
    /* Validate/copy before changing the namespace or retiring the old owner. */
    arena replacement = { 0 };
    pin_value snapshot;
    if (pin_value_capture_to_arena(&replacement, &snapshot, &coerced)) {
        arena_free(&replacement);
        return -1;
    }
    if (!stored) {
        apush(entities->global_values, ((global_value){ .name = xstrdup(name) }));
        stored = &alast(entities->global_values);
    }
    arena previous = stored->storage;
    stored->storage = replacement;
    stored->value = snapshot;
    arena_free(&previous);
    return 0;
}

static void release_event(event_request *event)
{
    arena_free(&event->storage);
    afree(event->values);
    free(event->name);
}

void entities_reset(entities *entities)
{
    for (int i = 0; i < acount(entities->tilemaps); i++) {
        tilemap_grid_free(&entities->tilemaps[i].grid);
    }
    aclear(entities->tilemaps);
    entities_query_scope(entities, NULL);
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
        assert(entities->host.destroy);
        int result = entities->host.destroy(
            entities->host.context,
            entities->owned,
            acount(entities->owned)
        );
        assert(!result);
        (void)result;
    }
    aclear(entities->owned);
    aclear(entities->destroy);
}

void entities_free(entities *entities)
{
    entities_reset(entities);
    prototypes_free(entities->prototypes);
    afree(entities->spawns);
    afree(entities->owned);
    afree(entities->destroy);
    afree(entities->global_bindings);
    afree(entities->global_values);
    afree(entities->events);
    afree(entities->tilemaps);
    for (int i = 0; i < NEAREST_CACHE_MAX; i++) {
        afree(entities->nearest[i].entities);
    }
    for (int i = 0; i < 32; i++) {
        afree(entities->parallel_candidates[i]);
    }
    free(entities);
}

int entities_register_prepared(entities *entities, const char *identity, scene *prototype)
{
    char error[256];
    return prototypes_register(entities->prototypes, identity, prototype, error, sizeof error);
}
scene *entities_prepare_spawn(entities *entities, const char *identity, const char **message)
{
    for (int i = 0; identity && i < prototypes_count(entities->prototypes); i++) {
        const char *name = prototypes_name(entities->prototypes, i);
        scene *prototype = (scene *)prototypes_find(entities->prototypes, name);
        if (!strcmp(name, identity) || !strcmp(scene_name(prototype), identity)) {
            scene_retain(prototype);
            return prototype;
        }
    }
    *message = "Spawn Prototype: unknown prototype";
    return NULL;
}

void entities_request_destroy(entities *entities, ecs_entity entity)
{
    apush(entities->destroy, entity);
}

static bool destroy_requested(const entities *entities, ecs_entity entity)
{
    for (int i = 0; i < acount(entities->destroy); i++) {
        if (ecs_entity_to_u64(entities->destroy[i]) == ecs_entity_to_u64(entity)) {
            return true;
        }
    }
    return false;
}

bool entities_is_retiring(const entities *entities, ecs_entity entity)
{
    return destroy_requested(entities, entity) ||
           ecs_entity_is_retiring(entities->world, entity);
}

int entities_pending_events(const entities *entities)
{
    return acount(entities->events);
}

int entities_request_event(entities *entities, ecs_entity target, const char *name, execution_arguments inputs)
{
    if (!name || !name[0] || inputs.count < 0 || (inputs.count && !inputs.values)) {
        return FLOW_ERR_ARGUMENT;
    }
    /* Queued events live outside every managed heap, so no collection can
     * keep a pointer's target alive until dispatch. */
    for (int i = 0; i < inputs.count; i++) {
        if (inputs.values[i].container != PIN_CONTAINER_SINGLE ||
            pin_value_has_managed_pointer(&inputs.values[i])) {
            return FLOW_ERR_ARGUMENT;
        }
    }
    event_request event = { .target = target, .name = xstrdup(name) };
    for (int i = 0; i < inputs.count; i++) {
        pin_value copy;
        if (pin_value_capture_to_arena(&event.storage, &copy, &inputs.values[i]) != 0) {
            release_event(&event);
            return FLOW_ERR_ARGUMENT;
        }
        apush(event.values, copy);
    }
    apush(entities->events, event);
    return FLOW_OK;
}

int entities_dispatch_events(entities *entities)
{
    if (entities_initializing(entities)) {
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
            !entities_is_retiring(entities, event->target)) {
            assert(entities->host.dispatch);
            int result = entities->host.dispatch(
                entities->host.context,
                event->target,
                event->name,
                (execution_arguments){ .values = event->values,
                                       .count = acount(event->values) }
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

bool entities_initializing(const entities *entities)
{
    for (int i = 0; i < acount(entities->spawns); i++) {
        if (entities->spawns[i].entity.slot) {
            return true;
        }
    }
    return false;
}

static int tick_initializers(entities *entities, double monotonic_time)
{
    flow_host_request **roots = NULL;
    for (int i = 0; i < acount(entities->spawns); i++) {
        spawn_request *request = &entities->spawns[i];
        const void *owner = execution_request_owner(request->wait);
        if (!request->entity.slot || !owner) {
            continue;
        }
        bool found = false;
        for (int j = 0; j < acount(roots); j++) {
            found |= execution_request_owner(roots[j]) == owner;
        }
        if (!found) {
            execution_request_retain(request->wait);
            apush(roots, request->wait);
        }
    }
    int result = FLOW_OK;
    for (int i = 0; i < acount(roots); i++) {
        flow_host_request *request = roots[i];
        if (execution_request_state(request) == EXECUTION_OWNER_PAUSED ||
            !execution_request_ready(request, monotonic_time)) {
            continue;
        }
        int tick = execution_request_tick(request, monotonic_time);
        if (tick < 0 && result == FLOW_OK) {
            result = tick;
        }
    }
    for (int i = 0; i < acount(roots); i++) {
        flow_host_release(roots[i]);
    }
    afree(roots);
    return result;
}

static int finish_spawn(entities *entities, spawn_request *request)
{
    if (!execution_request_owner(request->wait)) {
        return -1;
    }
    if (execution_request_state(request->wait) != EXECUTION_OWNER_COMPLETED &&
        !execution_request_owner_valid(request->wait)) {
        flow_host_cancel(request->wait);
        return -1;
    }
    if (!request->entity.slot) {
        char error[256];
        if (ecs_world_apply_commands(entities->world) ||
            scene_register_components(request->prototype, entities->world, error, sizeof error)) {
            flow_host_cancel(request->wait);
            return -1;
        }
        scene_instance *instance = scene_instantiate_components(
            request->prototype,
            entities->world,
            error,
            sizeof error
        );
        if (!instance) {
            flow_host_cancel(request->wait);
            return -1;
        }
        request->tree = scene_instance_take_entities(instance);
        request->entity = request->tree[0];
        apush(entities->owned, request->entity);
        if (entities_rebuild_global_bindings(entities) != 0) {
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
    execution_owner_state state = execution_request_state(request->wait);
    if (!execution_request_owner(request->wait) ||
        state == EXECUTION_OWNER_FAILED || state == EXECUTION_OWNER_IDLE) {
        return -1;
    }
    if (!ecs_entity_is_alive(entities->world, request->entity) ||
        destroy_requested(entities, request->entity)) {
        flow_host_cancel(request->wait);
        return -1;
    }
    if (state == EXECUTION_OWNER_PAUSED || state == EXECUTION_OWNER_RUNNING ||
        state == EXECUTION_OWNER_SUSPENDED) {
        return 1;
    }
    assert(entities->host.attach);
    for (int object = 0; object < scene_object_count(request->prototype); object++) {
        for (int i = 0; i < scene_object_behavior_count(request->prototype, object);
             i++) {
            if (entities->host.attach(
                    entities->host.context,
                    request->tree[object],
                    scene_object_behavior(request->prototype, object, i)
                ) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int entities_flush(entities *entities)
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
            entities->host.destroy(entities->host.context, &request->entity, 1);
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
    /* A completed child can resume a root that destroys its own entity.
     * Finalize earlier siblings before that destruction cancels the root's
     * continuations; their initialized entities now have independent lives. */
    for (int i = acount(entities->spawns) - 1; i >= 0; i--) {
        /* Begin can append spawns and reallocate the queue. */
        spawn_request request = entities->spawns[i];
        if (!request.entity.slot || !execution_request_owner(request.wait) ||
            execution_request_state(request.wait) != EXECUTION_OWNER_COMPLETED) {
            continue;
        }
        int result = finish_spawn(entities, &request);
        assert(result <= 0);
        if (result < 0 && ecs_entity_is_alive(entities->world, request.entity)) {
            entities->host.destroy(entities->host.context, &request.entity, 1);
        }
        release_request(&request);
        adel(entities->spawns, i);
    }
    if (acount(entities->destroy)) {
        assert(entities->host.destroy);
        int result = entities->host.destroy(
            entities->host.context,
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

static bool initializer_ready(const entities *entities, double now)
{
    for (int i = 0; i < acount(entities->spawns); i++) {
        const spawn_request *request = &entities->spawns[i];
        if (!request->entity.slot || !execution_request_owner(request->wait) ||
            !ecs_entity_is_alive(entities->world, request->entity) ||
            destroy_requested(entities, request->entity)) {
            return true;
        }
        execution_owner_state state = execution_request_state(request->wait);
        if (state == EXECUTION_OWNER_COMPLETED ||
            state == EXECUTION_OWNER_FAILED || state == EXECUTION_OWNER_IDLE ||
            execution_request_ready(request->wait, now)) {
            return true;
        }
    }
    return false;
}

int entities_flush_initializers(entities *entities, double now)
{
    for (int batch = 0; batch < SPAWN_INITIALIZER_BATCHES_MAX; batch++) {
        int result = entities_flush(entities);
        if (result < 0) {
            return result;
        }
        result = tick_initializers(entities, now);
        if (result < 0) {
            return result;
        }
        if (!initializer_ready(entities, now)) {
            return entities_initializing(entities);
        }
    }
    return FLOW_ERR_STEP_BUDGET;
}

static int destroy_command(void *service, const char *, const pin_value *arguments, int count)
{
    assert(count == 1);
    (void)count;
    entities_request_destroy(service, ecs_entity_from_u64(arguments[0].u64));
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
    entities *entities = service;
    arena storage = { 0 };
    const char *name;
    const char *error = pin_text_cstring(&storage, arguments[0], &name, NULL);
    if (error) {
        *message = error;
        arena_free(&storage);
        return FLOW_ERR_NODE;
    }
    scene *prototype = entities_prepare_spawn(entities, name, message);
    arena_free(&storage);
    if (!prototype) {
        return FLOW_ERR_NODE;
    }
    entities_submit_spawn(entities, prototype, request);
    return FLOW_OK;
}

const node_command ENTITY_SPAWN_COMMAND = {
    .service = "game.entities",
    .missing_service = "Spawn Prototype requires a game world",
    .begin = spawn_command
};
static int send_command(void *service, const char *reference, const pin_value *arguments, int count)
{
    assert(count >= 1);
    return entities_request_event(
        service,
        ecs_entity_from_u64(arguments[0].u64),
        reference,
        (execution_arguments){ .values = arguments + 1, .count = count - 1 }
    );
}
const node_command ENTITY_DESTROY_COMMAND = {
    .service = "game.entities",
    .missing_service = "Destroy Entity requires a game world",
    .invoke = destroy_command
};
const node_command ENTITY_SEND_COMMAND = {
    .service = "game.entities",
    .missing_service = "Send Event requires a game world",
    .missing_reference = "Choose an event definition",
    .failure_message = "Could not queue event payload",
    .invoke = send_command
};
static int add_pair_command(void *service, const char *, const pin_value *args, int count)
{
    assert(count == 3);
    (void)count;
    return ecs_world_record_add_pair(
        entities_world(service),
        ecs_entity_from_u64(args[0].u64),
        args[1].u64,
        ecs_entity_from_u64(args[2].u64),
        0
    );
}
static int remove_pair_command(void *service, const char *, const pin_value *args, int count)
{
    assert(count == 3);
    (void)count;
    return ecs_world_record_remove_pair(
        entities_world(service),
        ecs_entity_from_u64(args[0].u64),
        args[1].u64,
        ecs_entity_from_u64(args[2].u64),
        0
    );
}
static const char *has_pair_data(void *service, const pin_value *args, pin_value *out)
{
    out[0].b = ecs_has_pair(
        entities_world(service),
        ecs_entity_from_u64(args[0].u64),
        args[1].u64,
        ecs_entity_from_u64(args[2].u64)
    );
    return NULL;
}
const node_command ENTITY_ADD_PAIR_COMMAND = {
    .service = "game.entities",
    .missing_service = "add_pair requires an entity world",
    .invoke = add_pair_command
};
const node_command ENTITY_REMOVE_PAIR_COMMAND = {
    .service = "game.entities",
    .missing_service = "remove_pair requires an entity world",
    .invoke = remove_pair_command
};
const node_host_call ENTITY_HAS_PAIR_CALL = {
    .service = "game.entities",
    .missing_service = "has_pair requires an entity world",
    .invoke = has_pair_data
};

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
    entities *entities,
    ecs_entity entity,
    const pin_value *inputs,
    pin_value *out,
    double *best
)
{
    if (destroy_requested(entities, entity)) {
        return;
    }
    CF_V2 position = transform_world_2d(entities->world, entity).position;
    if (!isfinite(position.x) || !isfinite(position.y)) {
        return;
    }
    nearest_position((nearest_target){ position, ecs_entity_to_u64(entity) }, inputs, out, best);
}

static const char *nearest_entity_data(void *service, const pin_value *inputs, pin_value *out)
{
    entities *entities = service;
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
    ecs_component_id required[] = { COMPONENT_Transform.id, COMPONENT_Collider.id };
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
        const Collider *collider = Collider_get_const(entities->world, entity);
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

const node_host_call ENTITY_NEAREST_CALL = { .service = "game.entities",
                                             .invoke = nearest_entity_data,
                                             .reusable = true };

const node_access ENTITY_QUERY_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
static const uint64_t *const nearest_reads[] = { &COMPONENT_Transform.id,
                                                 &COMPONENT_Orientation.id,
                                                 &COMPONENT_Collider.id,
                                                 NULL };
const node_access ENTITY_NEAREST_ACCESS = { .read_refs = nearest_reads,
                                            .flags = NODE_EFFECT_WORLD_READ |
                                                     NODE_EFFECT_PARALLEL };

/* Result headers and struct backing belong to the invocation. Global payloads
 * are snapshotted so another host write cannot invalidate a suspended reader. */
static pin_value global_result_coerce(const pin_value *out, pin_value value)
{
    if (out->container == PIN_CONTAINER_SINGLE &&
        value.container == PIN_CONTAINER_SINGLE) {
        if (out->type == PIN_F64 && value.type == PIN_F32) {
            value.d = value.f;
            value.type = PIN_F64;
        } else if (out->type == PIN_F32 && value.type == PIN_F64) {
            value.f = (float)value.d;
            value.type = PIN_F32;
        }
    }
    return value;
}
static const char *global_result_check(const pin_value *out, const pin_value *value)
{
    if (!global_value_header_valid(out) || !global_value_header_valid(value)) {
        return "Global Variable result has invalid type metadata";
    }
    pin_value converted = global_result_coerce(out, *value);
    if (!global_value_type_matches(out, &converted)) {
        return "Global Variable result differs from its declared type";
    }
    if (out->type == PIN_STRUCT && out->container == PIN_CONTAINER_SINGLE) {
        if (!value->ptr || !out->ptr) {
            return "Global Variable has no struct payload for its declared "
                   "result";
        }
        const type_desc *type = pin_value_type(out);
        const type_desc *source = pin_value_type(value);
        if (!type || !source || !type->size || type->size != source->size) {
            return "Global Variable result has invalid struct storage";
        }
    }
    return NULL;
}
static const char *global_result(pin_value *out, pin_value value)
{
    const char *error = global_result_check(out, &value);
    if (error) {
        return error;
    }
    value = global_result_coerce(out, value);
    if (out->type == PIN_STRUCT && out->container == PIN_CONTAINER_SINGLE) {
        const type_desc *type = pin_value_type(out);
        memcpy(out->ptr, value.ptr, type->size);
        value.ptr = out->ptr;
    }
    if (!value.record) {
        value.type_name = out->type_name;
    }
    *out = value;
    return NULL;
}
static const char *get_global_host(
    const behavior_function_context *context,
    void *service,
    const pin_value *arguments,
    pin_value *out
)
{
    const char *key;
    const char *key_error = pin_text_cstring(context->value_storage, arguments[0], &key, NULL);
    if (key_error) {
        return key_error;
    }

    if (!context->value_storage) {
        return "Get Global Variable requires invocation storage";
    }
    pin_value value;
    if (entities_get_global_value(service, key, context->value_storage, &value)) {
        return "Get Global Variable references an unknown value";
    }
    return global_result(out, value);
}
static const char *set_global_host(
    const behavior_function_context *context,
    void *service,
    const pin_value *arguments,
    pin_value *out
)
{
    const char *key;
    const char *key_error = pin_text_cstring(context->value_storage, arguments[0], &key, NULL);
    if (key_error) {
        return key_error;
    }

    if (!context->value_storage) {
        return "Set Global Variable requires invocation storage";
    }
    const char *error = global_result_check(out, &arguments[1]);
    if (error) {
        return error;
    }
    /* Setter results retain the input value, independently of stored float
     * width or the getter's live-entity filtering. Own it before committing. */
    pin_value value;
    if (pin_value_snapshot_to_arena(context->value_storage, &value, &arguments[1])) {
        return "Set Global Variable could not snapshot its result";
    }
    if (entities_set_global_value(service, key, &arguments[1])) {
        return "Set Global Variable received an invalid value";
    }
    return global_result(out, value);
}
const node_host_call ENTITY_GET_GLOBAL_CALL = {
    .service = "game.entities",
    .context = get_global_host,
    .missing_service = "Get Global Variable requires a game world",
};
const node_host_call ENTITY_SET_GLOBAL_CALL = {
    .service = "game.entities",
    .context = set_global_host,
    .missing_service = "Set Global Variable requires a game world",
};

const node_access ENTITY_GLOBAL_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_access ENTITY_GLOBAL_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static const ecs_component_desc *query_component(entities *entities, const char *key)
{
    for (int i = 0; key && i < ecs_world_component_count(entities->world); i++) {
        const ecs_component_desc *component = ecs_world_component_at(entities->world, i);
        if (!strcmp(component->key ? component->key : component->name, key)) {
            return component;
        }
    }
    return NULL;
}

static const char *has_component_host(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *inputs,
    pin_value *out
)
{
    const char *text;
    const char *error = pin_text_cstring(ctx->value_storage, inputs[0], &text, NULL);
    if (error) {
        return error;
    }

    entities *entities = service;
    if (!entities) {
        return "Has Entities With Component requires a game world";
    }
    const char *key = text;
    if (!key || !key[0]) {
        return "Choose a component for Has Entities With Component";
    }
    const ecs_component_desc *component = query_component(entities, key);
    if (!component) {
        return "Selected component is not registered in this world";
    }
    out->b = ecs_component_has_entities(
        entities->world,
        component->id,
        entities->destroy,
        acount(entities->destroy)
    );
    return NULL;
}

const node_host_call ENTITY_HAS_COMPONENT_CALL = { .service = "game.entities",
                                                   .context = has_component_host };

const char *entities_find_snapshot(void *service, const char *key, pin_array *out)
{
    entities *entities = service;
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
        if (entities_is_retiring(entities, entity)) {
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

const node_access ENTITY_STRUCTURAL_ACCESS = { .flags = NODE_EFFECT_STRUCTURAL };

void entities_submit_spawn(entities *entities, scene *prototype, flow_host_request *request)
{
    assert(prototype && request && entities->host.attach && entities->host.destroy);
    apush(entities->spawns, ((spawn_request){ .prototype = prototype, .wait = request }));
}
