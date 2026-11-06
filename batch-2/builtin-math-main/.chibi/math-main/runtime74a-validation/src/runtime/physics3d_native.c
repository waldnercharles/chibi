#include "runtime/physics3d_native.h"

#include "execution/function.h"
#include "runtime/scenes/physics3d.h"
#include "support/arena.h"
#include "support/array.h"
#include "types/array.h"

#include <assert.h>

static const char *write_hit(int result, const char *problem, physics3d_hit hit, pin_value *out)
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

static const char missing_raycast[] = "Raycast 3D requires a 3D physics world";
static const char missing_cast[] = "Shape Cast 3D requires a 3D physics world";
static const char
    missing_contacts[] = "Contact Entities 3D requires a 3D physics world";

static const char *raycast_call(void *physics, const pin_value *in, pin_value *out)
{
    if (!physics) {
        return missing_raycast;
    }
    if (!in[0].ptr || !in[1].ptr) {
        return "Raycast 3D requires Start and End vectors";
    }
    physics3d_hit hit;
    int result = physics3d_raycast(
        physics,
        *(const CF_V3 *)in[0].ptr,
        *(const CF_V3 *)in[1].ptr,
        in[2].u,
        ecs_entity_from_u64(in[3].u64),
        in[4].b,
        &hit
    );
    return write_hit(result, "Raycast 3D requires coordinates within 100 km and valid colliders", hit, out);
}

static const char *cast_call(void *physics, const pin_value *in, pin_value *out)
{
    if (!physics) {
        return missing_cast;
    }
    if (!in[1].ptr) {
        return "Shape Cast 3D requires a Translation vector";
    }
    physics3d_hit hit;
    int result = physics3d_shape_cast(
        physics,
        ecs_entity_from_u64(in[0].u64),
        *(const CF_V3 *)in[1].ptr,
        in[2].u,
        in[3].b,
        &hit
    );
    return write_hit(
        result,
        "Shape Cast 3D requires a Translation within 100 km, valid "
        "colliders and a caster that is not a mesh",
        hit,
        out
    );
}

static const char *contacts_host(
    const behavior_function_context *context,
    void *physics,
    const pin_value *in,
    pin_value *out
)
{
    if (!physics) {
        return missing_contacts;
    }
    ecs_entity *handles = NULL;
    int result = physics3d_contact_entities(
        physics,
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
    pin_array shape = { .element_type = PIN_HANDLE, .type_name = "ecs.entity" };
    pin_array *array = pin_array_snapshot_like(storage, &shape, acount(handles));
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
    out->ptr = array;
    return NULL;
}

const node_host_call PHYSICS3D_RAYCAST_CALL = { .service = "game.physics3d",
                                                .invoke = raycast_call,
                                                .missing_service = missing_raycast,
                                                .reusable = true };
const node_host_call PHYSICS3D_CAST_CALL = { .service = "game.physics3d",
                                             .invoke = cast_call,
                                             .missing_service = missing_cast,
                                             .reusable = true };
const node_host_call PHYSICS3D_CONTACTS_CALL = { .service = "game.physics3d",
                                                 .context = contacts_host,
                                                 .missing_service = missing_contacts,
                                                 .reusable = true };
const node_access PHYSICS3D_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };

static const char
    missing_impulse[] = "Apply Impulse 3D requires a 3D physics world";
static const char
    missing_force[] = "Apply Force 3D requires a 3D physics world";

static const char *impulse_call(void *service, const pin_value *in, pin_value *)
{
    if (!service) {
        return missing_impulse;
    }
    if (!in[1].ptr || physics3d_apply_impulse(
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
    if (!in[1].ptr || physics3d_apply_force(
                          service,
                          ecs_entity_from_u64(in[0].u64),
                          *(const CF_V3 *)in[1].ptr
                      ) < 0) {
        return "Apply Force 3D requires a dynamic Body3D and a finite Force";
    }
    return NULL;
}

const node_host_call PHYSICS3D_IMPULSE_CALL = { .service = "game.physics3d",
                                                .invoke = impulse_call,
                                                .missing_service = missing_impulse };
const node_host_call PHYSICS3D_FORCE_CALL = { .service = "game.physics3d",
                                              .invoke = force_call,
                                              .missing_service = missing_force };
const node_access PHYSICS3D_PUSH_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
