#include "runtime/camera_shake.h"

#include "graph/runtime/flow.h"
#include "runtime/generated.h"
#include "support/value_noise.h"

#include <assert.h>
#include <math.h>

/* Lattice seeds of the two independent offset axes. */
enum
{
    SHAKE_SEED_X = 0x5a1c,
    SHAKE_SEED_Y = 0x3e97
};

CF_V2 game_camera_shake_offset(const GameCameraShake *shake, double seconds)
{
    float amount = shake->max_offset * shake->trauma * shake->trauma;
    if (!(amount > 0) || !isfinite(shake->frequency)) {
        return (CF_V2){ 0 };
    }
    float t = (float)fmod(seconds * shake->frequency, 65536.0);
    return (CF_V2){ amount * value_noise(SHAKE_SEED_X, t, 0.5f),
                    amount * value_noise(SHAKE_SEED_Y, t, 0.5f) };
}

/* Visits every camera shake; the query holds no structural edits. */
static void each_shake(ecs_world *world, float trauma, float seconds)
{
    ecs_component_id id = GAME_COMPONENT_GameCameraShake.id;
    if (!ecs_world_component(world, id)) {
        return;
    }
    ecs_query query;
    int result = ecs_query_begin(world, &query, &id, 1);
    assert(result == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        GameCameraShake *shake = GAME_GameCameraShake_get(world, entity);
        float decay = isfinite(shake->decay) ? fmaxf(shake->decay, 0) : 0;
        float next = shake->trauma + trauma - decay * seconds;
        shake->trauma = isfinite(next) ? fminf(fmaxf(next, 0), 1) : 0;
    }
    ecs_query_end(&query);
}

void game_camera_shake_tick(ecs_world *world, float real_seconds)
{
    each_shake(world, 0, real_seconds);
}

static const char missing_world[] = "Add Camera Trauma requires a game world";

static const char *trauma_call(void *service, const pin_value *in, pin_value *)
{
    if (!service) {
        return missing_world;
    }
    if (!isfinite(in[0].f)) {
        return "Add Camera Trauma needs a finite Amount";
    }
    each_shake(service, in[0].f, 0);
    return NULL;
}
static int add_trauma(node *, flow_context *ctx)
{
    pin_value amount;
    int result = flow_context_eval_data(ctx, 1, &amount);
    if (result < 0) {
        return result;
    }
    const char *error = trauma_call(flow_context_service(ctx, "ecs.world"), &amount, NULL);
    return error ? flow_context_fail(ctx, FLOW_ERR_NODE, error)
                 : flow_context_continue(ctx, 0);
}

static const node_access SINK = { .flags = NODE_EFFECT_HOST_SINK };
static pin_spec TRAUMA_IN[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Amount",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = .3f } },
};
static pin_spec THEN_OUT[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
static node_desc ADD_TRAUMA = {
    .name = "Add Camera Trauma",
    .category = "Camera",
    .keywords = "camera shake trauma screen shake impact juice",
    .description = "Adds Amount to every GameCameraShake's trauma, kept from 0 "
                   "to 1; the view shakes with trauma squared.",
    .input_count = 2,
    .inputs = TRAUMA_IN,
    .output_count = 1,
    .outputs = THEN_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = add_trauma,
    .host_call = &(const node_host_call){ .service = "ecs.world",
                                          .invoke = trauma_call,
                                          .missing_service = missing_world },
    .display_class = NODE_DISPLAY_FLOW,
    .access = &SINK,
};
static node_desc *const NODES[] = { &ADD_TRAUMA };
const registry_pack GAME_CAMERA_PACK = { .name = "game.camera",
                                         .nodes = NODES,
                                         .node_count = 1 };
