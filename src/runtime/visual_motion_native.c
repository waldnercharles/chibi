#include "runtime/visual_motion_native.h"

#include "runtime/component_catalog.h"

#include <math.h>

static bool finite_vector(CF_V3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

/* Target can name another instance's entity. Keep this write serialized until
 * entry analysis proves disjoint targets. */
const node_access GAME_VISUAL_REACTION_ACCESS = {
    .reads = &GAME_COMPONENT_GameVisualMotion.id,
    .read_count = 1,
    .writes = &GAME_COMPONENT_GameVisualMotion.id,
    .write_count = 1
};
static const char *visual_reaction(void *service, const pin_value *arguments, pin_value *)
{
    ecs_world *world = service;
    pin_value rotation = arguments[0], lift = arguments[1], target = arguments[2];
    if (rotation.type != PIN_STRUCT || !rotation.ptr || lift.type != PIN_F32 ||
        target.type != PIN_HANDLE || !isfinite(lift.f)) {
        return "Trigger Visual Reaction needs finite rotation and lift";
    }
    if (!world) {
        return "Trigger Visual Reaction requires an ECS world";
    }
    ecs_entity entity = ecs_entity_from_u64(target.u64);
    GameVisualMotion *motion = GAME_GameVisualMotion_get(world, entity);
    if (!motion) {
        return "Trigger Visual Reaction target has no GameVisualMotion";
    }
    CF_V3 value = *(const CF_V3 *)rotation.ptr;
    if (!finite_vector(value) || !isfinite(motion->reaction_duration) ||
        motion->reaction_duration < 0) {
        return "Trigger Visual Reaction needs finite motion state";
    }
    motion->reaction_rotation = value;
    motion->reaction_lift = lift.f;
    motion->reaction_age = 0;
    return NULL;
}
const node_host_call GAME_VISUAL_REACTION_CALL = {
    .service = "ecs.world",
    .invoke = visual_reaction,
    .missing_service = "Trigger Visual Reaction requires an ECS world",
};
