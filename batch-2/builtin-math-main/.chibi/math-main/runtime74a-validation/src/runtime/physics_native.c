#include "runtime/physics_native.h"

#include "execution/function.h"
#include "execution/service.h"
#include "runtime/behaviors/entities.h"
#include "runtime/scenes/physics.h"
#include "support/arena.h"
#include "support/array.h"
#include "types/array.h"

#include <assert.h>
#include <string.h>

const char *physics_native_raycast(
    ecs_world *world,
    entities *entities,
    const pin_value *inputs,
    arena *storage,
    pin_value *out
)
{
    if (!world || !entities || !inputs[0].ptr || !inputs[1].ptr) {
        return "Raycast requires a game world and Start and End vectors";
    }
    ray_hit hit;
    if (physics_raycast(
            world,
            entities,
            *(CF_V2 *)inputs[0].ptr,
            *(CF_V2 *)inputs[1].ptr,
            inputs[2].u,
            ecs_entity_from_u64(inputs[3].u64),
            inputs[4].b,
            &hit
        ) < 0) {
        return "Raycast requires finite coordinates and valid colliders";
    }
    CF_V2 *point = arena_alloc(storage, sizeof *point);
    CF_V2 *normal = arena_alloc(storage, sizeof *normal);
    *point = hit.point;
    *normal = hit.normal;
    out[0] = (pin_value){ .type = PIN_BOOL, .b = hit.found };
    out[1] = (pin_value){ .type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .u64 = ecs_entity_to_u64(hit.entity) };
    out[2] = (pin_value){ .type = PIN_STRUCT, .type_name = "vec2", .ptr = point };
    out[3] = (pin_value){ .type = PIN_STRUCT, .type_name = "vec2", .ptr = normal };
    out[4] = (pin_value){ .type = PIN_F32, .f = hit.distance };
    return NULL;
}

const char *physics_native_contacts(
    ecs_world *world,
    entities *entities,
    const pin_value *inputs,
    arena *a,
    pin_value *out
)
{
    if (!world || !entities) {
        return "Get Contact Entities requires a game world";
    }
    ecs_entity *handles = NULL;
    int result = physics_contact_entities(
        world,
        entities,
        ecs_entity_from_u64(inputs[0].u64),
        inputs[1].u,
        &handles
    );
    if (result < 0) {
        afree(handles);
        return "Get Contact Entities requires valid Collider and Transform "
               "components";
    }
    pin_array shape = { .element_type = PIN_HANDLE, .type_name = "ecs.entity" };
    pin_array *array = pin_array_snapshot_like(a, &shape, acount(handles));
    assert(array);
    for (int i = 0; i < array->count; i++) {
        pin_value item = { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .u64 = ecs_entity_to_u64(handles[i]) };
        int stored = pin_array_set(array, i, &item);
        assert(!stored);
        (void)stored;
    }
    afree(handles);
    *out = (pin_value){ .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array };
    return NULL;
}

static ecs_world *context_world(const behavior_function_context *context)
{
    for (int i = 0; i < acount(context->services); i++) {
        if (!strcmp(context->services[i].type_name, "ecs.world")) {
            return context->services[i].value;
        }
    }
    return NULL;
}
static const char *raycast_host(
    const behavior_function_context *context,
    void *entities,
    const pin_value *in,
    pin_value *out
)
{
    pin_value value[5] = { 0 };
    const char *error = physics_native_raycast(
        context_world(context),
        entities,
        in,
        context->value_storage,
        value
    );
    if (error) {
        return error;
    }
    assert(out[2].ptr && out[3].ptr);
    out[0].b = value[0].b;
    out[1].u64 = value[1].u64;
    *(CF_V2 *)out[2].ptr = *(CF_V2 *)value[2].ptr;
    *(CF_V2 *)out[3].ptr = *(CF_V2 *)value[3].ptr;
    out[4].f = value[4].f;
    return NULL;
}
static const char *contacts_host(
    const behavior_function_context *context,
    void *entities,
    const pin_value *in,
    pin_value *out
)
{
    pin_value value = { 0 };
    const char *error = physics_native_contacts(
        context_world(context),
        entities,
        in,
        context->value_storage,
        &value
    );
    if (error) {
        return error;
    }
    out->ptr = value.ptr;
    return NULL;
}
const node_host_call PHYSICS_RAYCAST_CALL = { .service = "game.entities",
                                              .context = raycast_host,
                                              .reusable = true };
const node_host_call PHYSICS_CONTACTS_CALL = { .service = "game.entities",
                                               .context = contacts_host,
                                               .reusable = true };
const node_access PHYSICS_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };

static const char missing_impulse[] = "Apply Impulse requires a physics world";
static const char missing_force[] = "Apply Force requires a physics world";

static const char *impulse_call(void *service, const pin_value *in, pin_value *)
{
    if (!service) {
        return missing_impulse;
    }
    if (!in[1].ptr || physics_apply_impulse(
                          service,
                          ecs_entity_from_u64(in[0].u64),
                          *(const CF_V2 *)in[1].ptr
                      ) < 0) {
        return "Apply Impulse requires a dynamic Body and a finite Impulse";
    }
    return NULL;
}

static const char *force_call(void *service, const pin_value *in, pin_value *)
{
    if (!service) {
        return missing_force;
    }
    if (!in[1].ptr || physics_apply_force(
                          service,
                          ecs_entity_from_u64(in[0].u64),
                          *(const CF_V2 *)in[1].ptr
                      ) < 0) {
        return "Apply Force requires a dynamic Body and a finite Force";
    }
    return NULL;
}

const node_host_call PHYSICS_IMPULSE_CALL = { .service = "game.physics",
                                              .invoke = impulse_call,
                                              .missing_service = missing_impulse };
const node_host_call PHYSICS_FORCE_CALL = { .service = "game.physics",
                                            .invoke = force_call,
                                            .missing_service = missing_force };
const node_access PHYSICS_PUSH_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
