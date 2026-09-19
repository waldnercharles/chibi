#include "runtime/rhythm.h"

#include "ecs/execution.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"

#include <math.h>

bool game_rhythm_sample(ecs_world *world, double time, game_beat *beat)
{
    GameRhythm settings = { 120, .18f };
    ecs_component_id component = GAME_COMPONENT_GameRhythm.id;
    const ecs_execution_column
        *column = ecs_execution_find_column((const ecs_execution_view *)world, component);
    if (column && column->count > 1) {
        return false;
    }
    if (column && column->count == 1) {
        settings = *(const GameRhythm *)((const char *)column->data +
                                         column->singleton * column->stride);
    }
    if (!isfinite(time) || !isfinite(settings.bpm) || settings.bpm <= 0 ||
        !isfinite(settings.pulse_width) || settings.pulse_width <= 0) {
        return false;
    }
    double period = 60.0 / settings.bpm;
    double phase = time / period;
    phase -= floor(phase);
    double duration = fmin(settings.pulse_width, period);
    double decay = fmax(0, 1 - phase * period / duration);
    *beat = (game_beat){ phase, decay * decay, period, duration };
    return true;
}

static const char *read_rhythm(const eval_context *ctx, const pin_value *, arena *, pin_value *out)
{
    ecs_world *world = eval_context_service(ctx, "ecs.world");
    if (!world || !ctx->instance) {
        return GAME_RHYTHM_NO_SCENE;
    }
    game_beat beat;
    if (!game_rhythm_sample(world, ctx->instance->clock->current_time, &beat)) {
        return GAME_RHYTHM_INVALID;
    }
    double values[] = { beat.phase, beat.pulse, beat.period, beat.duration };
    for (int i = 0; i < 4; i++) {
        out[i] = (pin_value){ .type = PIN_F64, .type_name = "double", .d = values[i] };
    }
    return NULL;
}

static pin_spec outputs[] = {
    { .name = "Phase", .type = PIN_F64, .type_name = "double" },
    { .name = "Pulse", .type = PIN_F64, .type_name = "double" },
    { .name = "Period", .type = PIN_F64, .type_name = "double" },
    { .name = "Duration", .type = PIN_F64, .type_name = "double" },
};
static const node_access access = { .reads = &GAME_COMPONENT_GameRhythm.id,
                                    .read_count = 1 };
node_desc NODE_Scene_Rhythm = {
    .name = "Scene Rhythm",
    .output_count = 4,
    .outputs = outputs,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = read_rhythm,
    .lower = game_rhythm_lower,
    .category = "Time",
    .keywords = "beat tempo phase pulse rhythm music",
    .access = &access,
};
