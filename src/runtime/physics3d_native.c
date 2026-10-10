#include "runtime/physics3d_native.h"

#include "execution/function.h"
#include "execution/service.h"
#include "runtime/behaviors/entities.h"
#include "runtime/scenes/physics3d.h"
#include "support/arena.h"
#include "support/array.h"
#include "types/array.h"

#include <assert.h>
#include <string.h>

static ecs_world *context_world(const behavior_function_context *context)
{
    for (int i = 0; i < acount(context->services); i++) {
        if (!strcmp(context->services[i].type_name, "ecs.world")) {
            return context->services[i].value;
        }
    }
    return NULL;
}

static const char *write_hit(int result, const char *problem, game_physics3d_hit hit, pin_value *out)
{
    if (result < 0) {
        return problem;
    }
    assert(out[2].ptr && out[3].ptr);
    out[0].b = hit.found;
    out[1].u64 = ecs_entity_to_u64(hit.entity);
    *(CF_V3 *)out[2].ptr = hit.point;
    *(CF_V3 *)out[3].ptr = hit.normal;
    out[4].f = hit.distance;
    return NULL;
}

static const char *raycast_host(
    const behavior_function_context *context,
    void *entities,
    const pin_value *in,
    pin_value *out
)
{
    ecs_world *world = context_world(context);
    if (!world || !entities || !in[0].ptr || !in[1].ptr) {
        return "Raycast 3D requires a game world and Start and End vectors";
    }
    game_physics3d_hit hit;
    int result = game_physics3d_raycast(
        world,
        entities,
        *(const CF_V3 *)in[0].ptr,
        *(const CF_V3 *)in[1].ptr,
        in[2].u,
        ecs_entity_from_u64(in[3].u64),
        in[4].b,
        &hit
    );
    return write_hit(result, "Raycast 3D requires coordinates within 100 km and valid colliders", hit, out);
}

static const char *cast_host(
    const behavior_function_context *context,
    void *entities,
    const pin_value *in,
    pin_value *out
)
{
    ecs_world *world = context_world(context);
    if (!world || !entities || !in[1].ptr) {
        return "Shape Cast 3D requires a game world and a Translation vector";
    }
    game_physics3d_hit hit;
    int result = game_physics3d_shape_cast(
        world,
        entities,
        ecs_entity_from_u64(in[0].u64),
        *(const CF_V3 *)in[1].ptr,
        in[2].u,
        in[3].b,
        &hit
    );
    return write_hit(
        result,
        "Shape Cast 3D requires a Translation within 100 km and valid "
        "colliders",
        hit,
        out
    );
}

static const char *contacts_host(
    const behavior_function_context *context,
    void *entities,
    const pin_value *in,
    pin_value *out
)
{
    ecs_world *world = context_world(context);
    if (!world || !entities) {
        return "Contact Entities 3D requires a game world";
    }
    ecs_entity *handles = NULL;
    int result = game_physics3d_contact_entities(
        world,
        entities,
        ecs_entity_from_u64(in[0].u64),
        in[1].u,
        &handles
    );
    if (result < 0) {
        afree(handles);
        return "Contact Entities 3D requires valid Collider3D and Transform3D "
               "components";
    }
    arena *storage = context->value_storage;
    pin_array *array = arena_alloc(storage, sizeof *array);
    *array = (pin_array){ .element_type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .count = acount(handles),
                          .capacity = acount(handles) };
    if (array->count) {
        array->items = arena_alloc(storage, (size_t)array->count * sizeof *array->items);
        for (int i = 0; i < array->count; i++) {
            array->items[i] = (pin_value){ .type = PIN_HANDLE,
                                           .type_name = "ecs.entity",
                                           .u64 = ecs_entity_to_u64(handles[i]) };
        }
    }
    afree(handles);
    out->ptr = array;
    return NULL;
}

const node_host_call GAME_PHYSICS3D_RAYCAST_CALL = { .service = "game.entities",
                                                     .context = raycast_host,
                                                     .reusable = true };
const node_host_call GAME_PHYSICS3D_CAST_CALL = { .service = "game.entities",
                                                  .context = cast_host,
                                                  .reusable = true };
const node_host_call GAME_PHYSICS3D_CONTACTS_CALL = {
    .service = "game.entities",
    .context = contacts_host,
    .reusable = true
};
const node_access GAME_PHYSICS3D_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };

static const char
    missing_impulse[] = "Apply Impulse 3D requires a 3D physics world";
static const char
    missing_force[] = "Apply Force 3D requires a 3D physics world";

static const char *impulse_call(void *service, const pin_value *in, pin_value *)
{
    if (!service) {
        return missing_impulse;
    }
    if (!in[1].ptr || game_physics3d_apply_impulse(
                          service,
                          ecs_entity_from_u64(in[0].u64),
                          *(const CF_V3 *)in[1].ptr
                      ) < 0) {
        return "Apply Impulse 3D requires a dynamic Body3D and a finite "
               "Impulse";
    }
    return NULL;
}

static const char *force_call(void *service, const pin_value *in, pin_value *)
{
    if (!service) {
        return missing_force;
    }
    if (!in[1].ptr || game_physics3d_apply_force(
                          service,
                          ecs_entity_from_u64(in[0].u64),
                          *(const CF_V3 *)in[1].ptr
                      ) < 0) {
        return "Apply Force 3D requires a dynamic Body3D and a finite Force";
    }
    return NULL;
}

const node_host_call GAME_PHYSICS3D_IMPULSE_CALL = {
    .service = "game.physics3d",
    .invoke = impulse_call,
    .missing_service = missing_impulse
};
const node_host_call GAME_PHYSICS3D_FORCE_CALL = { .service = "game.physics3d",
                                                   .invoke = force_call,
                                                   .missing_service = missing_force };
const node_access GAME_PHYSICS3D_PUSH_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
