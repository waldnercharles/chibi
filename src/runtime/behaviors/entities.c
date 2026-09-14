#include "runtime/behaviors/entities.h"

#include "graph/model/registry.h"
#include "graph/nodes/particle_nodes.h"
#include "graph/nodes/vector_nodes.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/value_array.h"
#include "runtime/generated.h"
#include "runtime/rhythm.h"
#include "runtime/scenes/physics.h"
#include "runtime/visual_motion.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"

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
} event_request;

struct game_entities
{
    ecs_world *world;
    game_behavior_world *behaviors;
    prototype_record *prototypes;
    ecs_entity *owned;
    spawn_request *spawns;
    ecs_entity *destroy;
    event_request *events;
};

game_entities *game_entities_new(ecs_world *world, game_behavior_world *behaviors)
{
    game_entities *entities = xcalloc(1, sizeof *entities);
    entities->world = world;
    entities->behaviors = behaviors;
    return entities;
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

static void release_event(event_request *event)
{
    for (int i = 0; i < acount(event->values); i++) {
        free((void *)event->values[i].type_name);
        pin_value_destroy(&event->values[i]);
    }
    afree(event->values);
    free(event->name);
}

void game_entities_reset(game_entities *entities)
{
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
    for (int i = 0; i < acount(entities->owned); i++) {
        ecs_entity entity = entities->owned[i];
        if (ecs_entity_is_alive(entities->world, entity)) {
            game_behavior_destroy_entity(entities->behaviors, entity);
        }
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
    afree(entities->events);
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

int game_entities_request_spawn(game_entities *entities, flow_context *ctx, const char *identity)
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
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Spawn Prototype: unknown prototype");
    }
    char error[256];
    game_prototype *copy = game_prototype_clone(prototype, error, sizeof error);
    if (!copy) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Spawn Prototype: could not capture defaults");
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
        if (inputs.values[i].type == PIN_STRUCT ||
            inputs.values[i].container != PIN_CONTAINER_SINGLE) {
            return FLOW_ERR_ARGUMENT;
        }
    }
    event_request event = { .target = target, .name = xstrdup(name) };
    for (int i = 0; i < inputs.count; i++) {
        pin_value copy;
        if (pin_value_copy(&copy, &inputs.values[i]) != 0) {
            release_event(&event);
            return FLOW_ERR_ARGUMENT;
        }
        copy.type_name = copy.type_name ? xstrdup(copy.type_name) : NULL;
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
    for (int i = 0; i < acount(entities->destroy); i++) {
        ecs_entity entity = entities->destroy[i];
        if (ecs_entity_is_alive(entities->world, entity)) {
            game_behavior_destroy_entity(entities->behaviors, entity);
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
    game_entities_request_destroy(entities, ecs_entity_from_u64(entity.u64));
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
    int result = game_entities_request_event(
        entities,
        ecs_entity_from_u64(values[0].u64),
        n->reference_name,
        (flow_inputs){ .values = &values[1], .count = count - 1 }
    );
    return result < 0
             ? flow_context_fail(ctx, result, "Could not queue event payload")
             : flow_context_continue(ctx, 0);
}

static const char *nearest_entity(const eval_context *ctx, const pin_value *inputs, arena *a, pin_value *out)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    if (!entities) {
        return "Find Nearest Entity requires a game world";
    }
    CF_V2 origin = inputs[0].ptr ? *(const CF_V2 *)inputs[0].ptr : (CF_V2){ NAN, NAN };
    CF_V2 *position = arena_alloc(a, sizeof *position);
    *position = (CF_V2){ 0 };
    out[0] = (pin_value){ .type = PIN_BOOL, .type_name = "bool" };
    out[1] = (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity" };
    out[2] = (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = position };
    out[3] = (pin_value){ .type = PIN_F32, .type_name = "float" };
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
    ecs_query query;
    if (ecs_query_begin(entities->world, &query, required, 2) != ECS_OK) {
        return "Find Nearest Entity could not query the world";
    }
    double best = radius * radius;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        uint64_t handle = ecs_entity_to_u64(entity);
        if (handle == inputs[3].u64 || destroy_requested(entities, entity)) {
            continue;
        }
        const GameCollider *collider = GAME_GameCollider_get_const(entities->world, entity);
        if (!(collider->category & inputs[2].u)) {
            continue;
        }
        const GameTransform *transform = GAME_GameTransform_get_const(entities->world, entity);
        if (!isfinite(transform->x) || !isfinite(transform->y)) {
            continue;
        }
        double x = (double)transform->x - origin.x;
        double y = (double)transform->y - origin.y;
        double distance = x * x + y * y;
        if (distance > best || (distance == best && out[0].b && handle > out[1].u64)) {
            continue;
        }
        best = distance;
        out[0].b = true;
        out[1].u64 = handle;
        *position = *transform;
        out[3].f = (float)sqrt(distance);
    }
    ecs_query_end(&query);
    return NULL;
}

static const node_access NEAREST_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };

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

static const char *find_entities_with_component(
    const eval_context *ctx,
    const pin_value *inputs,
    arena *storage,
    pin_value *out
)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    if (!entities) {
        return "Find Entities With Component requires a game world";
    }
    const ecs_component_desc *component = query_component(entities, inputs[0].str);
    if (!component) {
        return "Choose a registered component for Find Entities With Component";
    }
    ecs_query query;
    if (ecs_query_begin(entities->world, &query, &component->id, 1) != ECS_OK) {
        return "Find Entities With Component could not query the world";
    }
    pin_value *items = NULL;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (game_entities_is_retiring(entities, entity)) {
            continue;
        }
        pin_value value = { .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .u64 = ecs_entity_to_u64(entity) };
        apush(items, value);
    }
    ecs_query_end(&query);
    pin_array snapshot = { .element_type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .count = acount(items),
                           .items = items };
    *out = (pin_value){ .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = pin_array_clone_to_arena(storage, &snapshot) };
    afree(items);
    return NULL;
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
    .category = "Entities",
    .keywords = "nearest find search entity position radius category target",
    .access = &NEAREST_ACCESS,
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
    .display_class = NODE_DISPLAY_EVENT,
    .category = "Events",
    .keywords = "send entity custom event message",
    .access = &STRUCTURAL_ACCESS,
};
static node_desc *const ENTITY_NODES[] = { &SPAWN_NODE,
                                           &DESTROY_NODE,
                                           &DAMAGE_NODE,
                                           &NEAREST_NODE,
                                           &HAS_ENTITIES_NODE,
                                           &NODE_Normalize_Vector_2,
                                           &FIND_ENTITIES_NODE,
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
