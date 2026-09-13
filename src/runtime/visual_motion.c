#include "runtime/visual_motion.h"

#include "graph/runtime/flow.h"
#include "runtime/generated.h"

#include <assert.h>
#include <math.h>

static bool finite_vector(CF_V3 value);

/* Target can name another instance's entity. Keep this write serialized until
 * entry analysis proves disjoint targets. */
static const node_access visual_reaction_access = {
    .reads = &GAME_COMPONENT_GameVisualMotion.id,
    .read_count = 1,
    .writes = &GAME_COMPONENT_GameVisualMotion.id,
    .write_count = 1
};
static pin_spec visual_reaction_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Rotation", .type = PIN_STRUCT, .type_name = "CF_V3" },
    { .name = "Lift", .type = PIN_F32, .type_name = "float" },
    { .name = "Target", .type = PIN_HANDLE, .type_name = "ecs.entity", .flags = PIN_FLAG_DEFAULT_SELF },
};
static pin_spec visual_reaction_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
};

static int set_visual_reaction(node *, flow_context *ctx)
{
    pin_value rotation, lift, target;
    int result = flow_context_eval_data(ctx, 1, &rotation);
    if (result < 0) {
        return result;
    }
    result = flow_context_eval_data(ctx, 2, &lift);
    if (result < 0) {
        return result;
    }
    result = flow_context_eval_data(ctx, 3, &target);
    if (result < 0) {
        return result;
    }
    if (rotation.type != PIN_STRUCT || !rotation.ptr || lift.type != PIN_F32 ||
        target.type != PIN_HANDLE || !isfinite(lift.f)) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "Trigger Visual Reaction needs finite rotation and lift");
    }
    ecs_world *world = flow_context_service(ctx, "ecs.world");
    if (!world) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Trigger Visual Reaction requires an ECS world");
    }
    ecs_entity entity = ecs_entity_from_u64(target.u64);
    GameVisualMotion *motion = GAME_GameVisualMotion_get(world, entity);
    if (!motion) {
        return flow_context_fail(ctx, FLOW_ERR_STATE, "Trigger Visual Reaction target has no GameVisualMotion");
    }
    CF_V3 value = *(const CF_V3 *)rotation.ptr;
    if (!finite_vector(value) || !isfinite(motion->reaction_duration) ||
        motion->reaction_duration < 0) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, "Trigger Visual Reaction needs finite motion state");
    }
    motion->reaction_rotation = value;
    motion->reaction_lift = lift.f;
    motion->reaction_age = 0;
    return flow_context_continue(ctx, 0);
}

node_desc NODE_Set_Visual_Reaction = {
    .name = "Trigger Visual Reaction",
    .input_count = 4,
    .output_count = 1,
    .inputs = visual_reaction_inputs,
    .outputs = visual_reaction_outputs,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = set_visual_reaction,
    .lower = game_visual_reaction_lower,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Visual/Motion",
    .keywords = "trigger visual reaction bump recoil hit lift rotation",
    .access = &visual_reaction_access,
};

static bool finite_vector(CF_V3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool valid(const GameVisualMotion *motion)
{
    return finite_vector(motion->rotation) &&
           finite_vector(motion->angular_velocity) &&
           finite_vector(motion->reaction_rotation) && isfinite(motion->speed) &&
           motion->speed >= 0 && isfinite(motion->rest_elevation) &&
           isfinite(motion->reaction_lift) && isfinite(motion->reaction_age) &&
           motion->reaction_age >= 0 && isfinite(motion->reaction_duration) &&
           motion->reaction_duration >= 0;
}

static bool advance(GameVisualMotion *motion, float delta, GameVisualTransform *visual, float *elevation)
{
    motion->rotation = cf_add_v3(
        motion->rotation,
        cf_mul_v3_f(motion->angular_velocity, motion->speed * delta)
    );
    motion->reaction_age = fminf(motion->reaction_age + delta, motion->reaction_duration);
    float phase = motion->reaction_duration > 0
                    ? motion->reaction_age / motion->reaction_duration
                    : 1;
    /* Exact endpoints avoid a residual offset when the pulse completes. */
    float amplitude = phase > 0 && phase < 1 ? sinf(3.141592654f * phase) : 0;
    CF_V3 rotation = cf_add_v3(motion->rotation, cf_mul_v3_f(motion->reaction_rotation, amplitude));
    visual->pitch = rotation.x;
    visual->yaw = rotation.y;
    visual->roll = rotation.z;
    *elevation = motion->rest_elevation + motion->reaction_lift * amplitude;
    return finite_vector(motion->rotation) && finite_vector(rotation) &&
           isfinite(*elevation);
}

int game_visual_motion_tick(ecs_world *world, float delta)
{
    assert(world);
    if (!isfinite(delta) || delta < 0) {
        return FLOW_ERR_ARGUMENT;
    }
    ecs_component_id component = GAME_COMPONENT_GameVisualMotion.id;
    if (!ecs_world_component(world, component)) {
        return FLOW_OK;
    }
    /* Query only the source component so invalid dependencies cannot be
     * silently excluded. The first pass validates before any world mutation. */
    for (int pass = 0; pass < 2; pass++) {
        ecs_query query;
        int result = ecs_query_begin(world, &query, &component, 1);
        if (result != ECS_OK) {
            return result;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            GameVisualMotion *motion = GAME_GameVisualMotion_get(world, entity);
            GameVisualTransform *visual = GAME_GameVisualTransform_get(world, entity);
            GameGeometry3D *spatial = GAME_GameGeometry3D_get(world, entity);
            if (!visual || !spatial) {
                ecs_query_end(&query);
                return FLOW_ERR_STATE;
            }
            if (!pass) {
                GameVisualMotion next = *motion;
                GameVisualTransform resolved = *visual;
                float elevation;
                if (!valid(motion) || !advance(&next, delta, &resolved, &elevation)) {
                    ecs_query_end(&query);
                    return FLOW_ERR_ARGUMENT;
                }
                continue;
            }
            advance(motion, delta, visual, &spatial->elevation);
        }
        ecs_query_end(&query);
    }
    return FLOW_OK;
}
