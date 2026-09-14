#include "graph/nodes/particle_nodes.h"

#include "graph/runtime/flow.h"
#include "render/particles.h"

#include <cute_math.h>

static const char missing_particles[] = "Emit Particles requires a game world";
static const char
    missing_directed[] = "Emit Directed Particles requires a game world";

static const char *burst_call(void *service, const pin_value *values, pin_value *)
{
    game_particles *particles = service;
    if (!particles) {
        return missing_particles;
    }
    if (!values[0].ptr) {
        return "Emit Particles requires a position";
    }
    const CF_V2 *position = values[0].ptr;
    game_particle_burst burst = {
        .position = { position->x, position->y },
        .color = values[1].u,
        .count = values[2].i,
        .speed = values[3].f,
        .lifetime = values[4].f,
    };
    return game_particles_emit(particles, &burst) < 0
             ? "Emit Particles requires finite position, nonnegative count and "
               "speed, and positive lifetime"
             : NULL;
}
static const node_host_call burst_host = { .service = "game.particles",
                                           .invoke = burst_call,
                                           .missing_service = missing_particles };
static int emit_particles(node *, flow_context *ctx)
{
    game_particles *particles = flow_context_service(ctx, "game.particles");
    if (!particles) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, missing_particles);
    }
    pin_value values[5];
    for (int i = 0; i < 5; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    const char *error = burst_call(particles, values, NULL);
    return error ? flow_context_fail(ctx, FLOW_ERR_NODE, error)
                 : flow_context_continue(ctx, 0);
}

static pin_spec INPUTS[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Position", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Color",
      .flags = PIN_FLAG_COLOR,
      .type = PIN_U32,
      .type_name = "unsigned int",
      .default_value = { .type = PIN_U32, .u = 0xF0CD69FFu } },
    { .name = "Count",
      .type = PIN_I32,
      .type_name = "int",
      .default_value = { .type = PIN_I32, .i = 12 } },
    { .name = "Speed",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 80.0f } },
    { .name = "Lifetime",
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0.3f } },
};
static pin_spec OUTPUTS[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
};
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };

node_desc NODE_Emit_Particles = {
    .name = "Emit Particles",
    .input_count = 6,
    .inputs = INPUTS,
    .output_count = 1,
    .outputs = OUTPUTS,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = emit_particles,
    .host_call = &burst_host,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Effects",
    .keywords = "particles burst sparks impact",
    .access = &ACCESS,
};

static const char *directed_call(void *service, const pin_value *v, pin_value *)
{
    game_particles *particles = service;
    if (!particles) {
        return missing_directed;
    }
    if (!v[0].ptr || !v[1].ptr) {
        return "Position and Direction are required";
    }
    const CF_V2 *position = v[0].ptr, *direction = v[1].ptr;
    game_particle_spray spray = {
        .position = { position->x, position->y },
        .direction = { direction->x, direction->y },
        .count = v[2].i,
        .color = v[3].u,
        .end_color = v[4].u,
        .speed_min = v[5].f,
        .speed_max = v[6].f,
        .lifetime_min = v[7].f,
        .lifetime_max = v[8].f,
        .size_min = v[9].f,
        .size_max = v[10].f,
        .spread = v[11].f,
        .drag_min = v[12].f,
        .drag_max = v[13].f,
        .emission_min = v[14].f,
        .emission_max = v[15].f,
        .size_exponent = v[16].f,
        .elevation = v[17].f,
        .shape = v[18].i,
    };
    if (game_particles_emit_directed(particles, &spray) < 0) {
        return "Directed particle ranges and direction must be finite and "
               "valid";
    }
    return NULL;
}

static const node_host_call directed_host = { .service = "game.particles",
                                              .invoke = directed_call,
                                              .missing_service = missing_directed };
static int emit_directed(node *, flow_context *ctx)
{
    game_particles *particles = flow_context_service(ctx, "game.particles");
    if (!particles) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, missing_directed);
    }
    pin_value values[19];
    for (int i = 0; i < 19; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    const char *error = directed_call(particles, values, NULL);
    return error ? flow_context_fail(ctx, FLOW_ERR_NODE, error)
                 : flow_context_continue(ctx, 0);
}

static pin_spec DIRECTED_INPUTS[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Position", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Direction", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Count",
      .type = PIN_I32,
      .type_name = "int",
      .default_value = { .type = PIN_I32, .i = 1 } },
    { .name = "Color",
      .flags = PIN_FLAG_ADVANCED | PIN_FLAG_COLOR,
      .type = PIN_U32,
      .type_name = "unsigned int",
      .default_value = { .type = PIN_U32, .u = 0xFF991AFFu } },
    { .name = "End Color",
      .flags = PIN_FLAG_ADVANCED | PIN_FLAG_COLOR,
      .type = PIN_U32,
      .type_name = "unsigned int",
      .default_value = { .type = PIN_U32, .u = 0x4D00FF80u } },
    { .name = "Min Speed",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 30 } },
    { .name = "Max Speed",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 60 } },
    { .name = "Min Lifetime",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0.2f } },
    { .name = "Max Lifetime",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0.4f } },
    { .name = "Min Size",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 2 } },
    { .name = "Max Size",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 4 } },
    { .name = "Spread",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 1.571f } },
    { .name = "Min Drag",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 4 } },
    { .name = "Max Drag",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 8 } },
    { .name = "Min Emission",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0.2f } },
    { .name = "Max Emission",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0.4f } },
    { .name = "Size Exponent",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 1.5f } },
    { .name = "Elevation",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_F32,
      .type_name = "float",
      .default_value = { .type = PIN_F32, .f = 0 } },
    { .name = "Shape",
      .flags = PIN_FLAG_ADVANCED,
      .type = PIN_I32,
      .type_name = "int",
      .default_value = { .type = PIN_I32, .i = GAME_PARTICLE_ROUND } },
};
node_desc NODE_Emit_Directed_Particles = {
    .name = "Emit Directed Particles",
    .input_count = 20,
    .inputs = DIRECTED_INPUTS,
    .output_count = 1,
    .outputs = OUTPUTS,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = emit_directed,
    .host_call = &directed_host,
    .display_class = NODE_DISPLAY_FLOW,
    .category = "Effects",
    .keywords = "particles spray trail cone",
    .access = &ACCESS,
};
