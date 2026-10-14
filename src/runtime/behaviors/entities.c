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
#include "types/union.h"

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
    char *prototype_identity;
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
    size_t bytes;
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
    void *name_context;
    const char *(*name_lookup)(void *, ecs_entity);
    materials *materials;
    prototypes *prototypes;
    ecs_entity *owned;
    struct entity_origin_entry *origins;
    uint64_t creation_sequence;
    spawn_request *spawns;
    ecs_entity *destroy;
    event_request *events;
    int event_count;
    size_t event_bytes;
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

typedef struct entity_origin_entry
{
    ecs_entity entity;
    char *prototype;
    int object;
    uint64_t creation;
} entity_origin_entry;

static void prune_origins(entities *service)
{
    int live = 0;
    for (int i = 0; i < acount(service->origins); i++) {
        entity_origin_entry entry = service->origins[i];
        if (ecs_entity_is_alive(service->world, entry.entity) &&
            !entities_is_retiring(service, entry.entity)) {
            service->origins[live++] = entry;
        } else {
            free(entry.prototype);
        }
    }
    if (service->origins) {
        asetlen(service->origins, live);
    }
}

bool entities_origin(const entities *service, ecs_entity entity, entity_origin *out)
{
    *out = (entity_origin){ 0 };
    if (!service || !ecs_entity_is_alive(service->world, entity) ||
        entities_is_retiring(service, entity)) {
        return false;
    }
    for (int i = 0; i < acount(service->origins); i++) {
        const entity_origin_entry *entry = &service->origins[i];
        if (ecs_entity_to_u64(entry->entity) == ecs_entity_to_u64(entity)) {
            *out = (entity_origin){ entry->prototype, entry->object, entry->creation };
            return true;
        }
    }
    return false;
}

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
bool entities_parallel_begin(entities *entities, unsigned categories)
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
    ecs_component_id components[] = { COMPONENT_Transform.id, COMPONENT_Collider.id };
    ecs_query query;
    if (ecs_query_begin(entities->world, &query, components, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            unsigned matching = Collider_get_const(entities->world, entity)->category &
                                categories;
            if (!matching || entities_is_retiring(entities, entity)) {
                continue;
            }
            CF_V2 position = transform_world_2d(entities->world, entity).position;
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
void entities_parallel_end(entities *entities)
{
    if (entities) {
        entities->parallel_queries = false;
    }
}
void entities_profile_queries(entities *entities, bool enabled)
{
    entities->profile_queries = enabled;
    entities->query_profile = (entity_query_profile){ 0 };
}
entity_query_profile entities_query_profile(const entities *entities)
{
    return entities->query_profile;
}

static void release_request(spawn_request *request)
{
    flow_host_release(request->wait);
    scene_free(request->prototype);
    free(request->prototype_identity);
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

int entities_prototype_count(const entities *s)
{
    return s ? prototypes_count(s->prototypes) : 0;
}
const char *entities_prototype_name_at(const entities *s, int i)
{
    return s && i >= 0 && i < prototypes_count(s->prototypes)
             ? prototypes_name(s->prototypes, i)
             : NULL;
}
const scene *entities_prototype_scene_at(const entities *s, int i)
{
    const char *key = entities_prototype_name_at(s, i);
    return key ? prototypes_find(s->prototypes, key) : NULL;
}
int entities_origin_count(const entities *s)
{
    return s ? acount(s->origins) : 0;
}
bool entities_origin_at(const entities *s, int i, ecs_entity *entity, entity_origin *out)
{
    *entity = (ecs_entity){ 0 };
    *out = (entity_origin){ 0 };
    if (!s || i < 0 || i >= acount(s->origins)) {
        return false;
    }
    const entity_origin_entry *entry = &s->origins[i];
    if (!ecs_entity_is_alive(s->world, entry->entity) ||
        entities_is_retiring(s, entry->entity)) {
        return false;
    }
    *entity = entry->entity;
    *out = (entity_origin){ entry->prototype, entry->object, entry->creation };
    return true;
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

static bool global_value_type_matches(const pin_value *left, const pin_value *right)
{
    /* A number's type names it; only named types compare their names. */
    bool named = left->type == PIN_STRUCT || left->type == PIN_ENUM ||
                 left->type == PIN_HANDLE;
    return left->type == right->type && left->container == right->container &&
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
    if (!entities || !name || !name[0] || !value || value->type == PIN_FLOW ||
        (value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE &&
         value->ptr && !type_catalog_find(value->type_name))) {
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
    value = &coerced;
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
    entities->event_count = 0;
    entities->event_bytes = 0;
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
    for (int i = 0; i < acount(entities->origins); i++) {
        free(entities->origins[i].prototype);
    }
    aclear(entities->origins);
    aclear(entities->destroy);
}

void entities_free(entities *entities)
{
    entities_reset(entities);
    prototypes_free(entities->prototypes);
    afree(entities->spawns);
    afree(entities->owned);
    afree(entities->origins);
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

enum
{
    EVENT_PAYLOAD_DEPTH_MAX = 16,
    EVENT_PAYLOAD_VALUES_MAX = 4096,
    EVENT_PAYLOAD_BYTES_MAX = 1024 * 1024,
    EVENT_QUEUE_BYTES_MAX = 16 * 1024 * 1024,
    EVENT_NAME_BYTES_MAX = 1024
};
typedef struct
{
    size_t bytes;
    int values;
} event_budget;
static bool event_bytes(event_budget *budget, size_t bytes)
{
    if (bytes > EVENT_PAYLOAD_BYTES_MAX - budget->bytes) {
        return false;
    }
    budget->bytes += bytes;
    return true;
}
static bool event_string(event_budget *budget, const char *text)
{
    if (!text) {
        return true;
    }
    size_t remaining = EVENT_PAYLOAD_BYTES_MAX - budget->bytes;
    size_t bytes = 0;
    while (bytes < remaining && text[bytes]) {
        bytes++;
    }
    return bytes < remaining && event_bytes(budget, bytes + 1);
}
static bool event_value(event_budget *, const pin_value *, int);
static bool event_struct(event_budget *budget, const type_desc *type, const void *data, int depth)
{
    if (!type || !type->size || !data || depth >= EVENT_PAYLOAD_DEPTH_MAX ||
        !event_bytes(budget, type->size)) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset > type->size || field->size > type->size - field->offset) {
            return false;
        }
        const unsigned char *member = (const unsigned char *)data + field->offset;
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_catalog_find(field->type_name);
            if (!nested || nested->size != field->size ||
                !event_struct(budget, nested, member, depth + 1)) {
                return false;
            }
            continue;
        }
        if (field->type == PIN_STR || field->type == PIN_NAME) {
            if (field->size != sizeof(const char *) ||
                !event_string(budget, *(const char *const *)member)) {
                return false;
            }
            continue;
        }
        if (field->type == PIN_UNION) {
            if (field->size != sizeof(void *)) {
                return false;
            }
            pin_value value = { .type = PIN_UNION, .ptr = *(void *const *)member };
            if (!event_value(budget, &value, depth + 1)) {
                return false;
            }
            continue;
        }
        if (field->type <= PIN_FLOW || field->type >= PIN_TASK) {
            return false;
        }
    }
    return true;
}
static bool event_value(event_budget *budget, const pin_value *value, int depth)
{
    if (depth >= EVENT_PAYLOAD_DEPTH_MAX || ++budget->values > EVENT_PAYLOAD_VALUES_MAX ||
        !event_bytes(budget, sizeof *value) ||
        !event_string(budget, value->type_name)) {
        return false;
    }
    if (value->type <= PIN_FLOW || value->type > PIN_UNION || value->type == PIN_TASK) {
        return false;
    }
    if (value->container != PIN_CONTAINER_SINGLE) {
        if (value->container != PIN_CONTAINER_ARRAY &&
            value->container != PIN_CONTAINER_SET) {
            return false;
        }
        const pin_array *array = value->ptr;
        if (!array) {
            return true;
        }
        if (array->count < 0 || array->count > EVENT_PAYLOAD_VALUES_MAX - budget->values ||
            array->count > array->capacity || (array->count && !array->items) ||
            array->element_type != value->type || !event_bytes(budget, sizeof *array) ||
            !event_string(budget, array->type_name)) {
            return false;
        }
        if (value->type >= PIN_HANDLE && (value->type_name || array->type_name) &&
            (!value->type_name || !array->type_name ||
             strcmp(value->type_name, array->type_name))) {
            return false;
        }
        for (int i = 0; i < array->count; i++) {
            const pin_value *item = &array->items[i];
            if (item->container != PIN_CONTAINER_SINGLE ||
                item->type != array->element_type ||
                (value->type >= PIN_HANDLE && (array->type_name || item->type_name) &&
                 (!array->type_name || !item->type_name ||
                  strcmp(array->type_name, item->type_name))) ||
                !event_value(budget, item, depth + 1)) {
                return false;
            }
        }
        /* A set snapshot allocates its hash index, bounded by this allowance. */
        if (value->container == PIN_CONTAINER_SET &&
            !event_bytes(budget, (size_t)array->count * 4 * sizeof(int))) {
            return false;
        }
        return true;
    }
    if (value->type == PIN_STR || value->type == PIN_NAME) {
        return event_string(budget, value->str);
    }
    if (value->type == PIN_STRUCT) {
        return event_struct(budget, type_catalog_find(value->type_name), value->ptr, depth + 1);
    }
    if (value->type == PIN_UNION && value->ptr) {
        const pin_union *tagged = value->ptr;
        if (tagged->count < 0 ||
            tagged->count > EVENT_PAYLOAD_VALUES_MAX - budget->values ||
            !event_bytes(budget, sizeof *tagged)) {
            return false;
        }
        for (int i = 0; i < tagged->count; i++) {
            if (!event_value(budget, &tagged->values[i], depth + 1)) {
                return false;
            }
        }
    }
    return true;
}
int entities_request_event(entities *entities, ecs_entity target, const char *name, execution_arguments inputs)
{
    size_t name_bytes = 0;
    if (name) {
        while (name_bytes < EVENT_NAME_BYTES_MAX && name[name_bytes]) {
            name_bytes++;
        }
    }
    if (!name || !name[0] || name_bytes == EVENT_NAME_BYTES_MAX ||
        inputs.count < 0 || inputs.count > EVENT_PAYLOAD_VALUES_MAX ||
        (inputs.count && !inputs.values)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (entities->event_count >= ENTITIES_EVENT_QUEUE_COUNT_MAX) {
        return FLOW_ERR_STEP_BUDGET;
    }
    event_budget budget = { 0 };
    if (!event_string(&budget, name)) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < inputs.count; i++) {
        if (!event_value(&budget, &inputs.values[i], 0)) {
            return FLOW_ERR_ARGUMENT;
        }
    }
    if (budget.bytes > EVENT_QUEUE_BYTES_MAX - entities->event_bytes) {
        return FLOW_ERR_STEP_BUDGET;
    }
    event_request event = { .target = target,
                            .name = xstrdup(name),
                            .bytes = budget.bytes };
    for (int i = 0; i < inputs.count; i++) {
        pin_value copy;
        if (pin_value_snapshot_to_arena(&event.storage, &copy, &inputs.values[i]) != 0) {
            release_event(&event);
            return FLOW_ERR_ARGUMENT;
        }
        apush(event.values, copy);
    }
    apush(entities->events, event);
    entities->event_count++;
    entities->event_bytes += event.bytes;
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
        entities->event_count--;
        entities->event_bytes -= event->bytes;
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
        prune_origins(entities);
        int objects = scene_object_count(request->prototype);
        const char *prototype_identity = request->prototype_identity;
        if (objects < 1 ||
            (prototype_identity &&
             (objects > 65536 - acount(entities->origins) ||
              (uint64_t)objects > UINT64_MAX - entities->creation_sequence))) {
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
        for (int object = 0; prototype_identity && object < objects; object++) {
            apush(
                entities->origins,
                ((entity_origin_entry){ request->tree[object],
                                        xstrdup(prototype_identity),
                                        object,
                                        ++entities->creation_sequence })
            );
        }
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
    scene *prototype = entities_prepare_spawn(entities, arguments[0].str, message);
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

unsigned entities_query_categories(const behavior_function *function)
{
    return behavior_function_host_mask(function, &ENTITY_NEAREST_CALL, 2);
}

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
static const char *global_result(pin_value *out, pin_value value)
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
    if (!global_value_type_matches(out, &value)) {
        return "Global Variable result differs from its declared type";
    }
    if (out->type == PIN_STRUCT && out->container == PIN_CONTAINER_SINGLE) {
        if (!value.ptr) {
            return "Global Variable has no struct payload for its declared "
                   "result";
        }
        const type_desc *type = type_catalog_find(out->type_name);
        assert(type && out->ptr);
        memcpy(out->ptr, value.ptr, type->size);
        return NULL;
    }
    value.type_name = out->type_name;
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
    assert(context->value_storage);
    pin_value value;
    if (entities_get_global_value(service, arguments[0].str, context->value_storage, &value)) {
        return "Get Global Variable references an unknown value";
    }
    return global_result(out, value);
}
static const char *set_global_host(void *service, const pin_value *arguments, pin_value *out)
{
    if (entities_set_global_value(service, arguments[0].str, &arguments[1])) {
        return "Set Global Variable received an invalid value";
    }
    return global_result(out, arguments[1]);
}
const node_host_call ENTITY_GET_GLOBAL_CALL = {
    .service = "game.entities",
    .context = get_global_host,
    .missing_service = "Get Global Variable requires a game world",
};
const node_host_call ENTITY_SET_GLOBAL_CALL = {
    .service = "game.entities",
    .invoke = set_global_host,
    .missing_service = "Set Global Variable requires a game world",
};

void entities_bind_names(entities *owner, void *context, const char *(*lookup)(void *, ecs_entity))
{
    assert(owner);
    owner->name_context = context;
    owner->name_lookup = lookup;
}

static const char *entity_name_host(
    const behavior_function_context *context,
    void *service,
    const pin_value *arguments,
    pin_value *out
)
{
    entities *owner = service;
    ecs_entity entity = ecs_entity_from_u64(arguments[0].u64);
    const char *name = NULL;
    if (owner && !entities_is_retiring(owner, entity) && ecs_entity_is_alive(owner->world, entity)) {
        name = owner->name_lookup ? owner->name_lookup(owner->name_context, entity) : NULL;
        if (!name) name = ecs_entity_name(owner->world, entity);
    }
    name = name ? name : "";
    if (!context->value_storage) return "Get Entity Name requires result storage";
    size_t size = strlen(name) + 1;
    char *copy = arena_alloc(context->value_storage, size);
    if (!copy) return "Get Entity Name could not allocate result text";
    memcpy(copy, name, size);
    out->str = copy;
    return NULL;
}

const node_host_call ENTITY_NAME_CALL = {
    .service = "game.entities", .context = entity_name_host,
    .missing_service = "Get Entity Name requires a game world", .reusable = true,
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

static const char *has_component_host(void *service, const pin_value *inputs, pin_value *out)
{
    entities *entities = service;
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
    out->b = ecs_component_has_entities(
        entities->world,
        component->id,
        entities->destroy,
        acount(entities->destroy)
    );
    return NULL;
}

const node_host_call ENTITY_HAS_COMPONENT_CALL = { .service = "game.entities",
                                                   .invoke = has_component_host };

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
    char *identity = NULL;
    for (int i = 0; i < prototypes_count(entities->prototypes); i++) {
        const char *key = prototypes_name(entities->prototypes, i);
        if (prototypes_find(entities->prototypes, key) == prototype) {
            identity = xstrdup(key);
            break;
        }
    }
    apush(
        entities->spawns,
        ((spawn_request){ .prototype = prototype, .prototype_identity = identity, .wait = request })
    );
}
