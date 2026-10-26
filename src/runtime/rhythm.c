#include "runtime/rhythm.h"

#include "ecs/execution.h"
#include "execution/function.h"
#include "runtime/component_catalog.h"

#include <math.h>

bool rhythm_sample(ecs_world *world, double time, beat *out)
{
    Rhythm settings = { 120, .18f };
    ecs_component_id component = COMPONENT_Rhythm.id;
    const ecs_execution_column
        *column = ecs_execution_find_column((const ecs_execution_view *)world, component);
    if (column && column->count > 1) {
        return false;
    }
    if (column && column->count == 1) {
        settings = *(const Rhythm *)((const char *)column->data +
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
    *out = (beat){ phase, decay * decay, period, duration };
    return true;
}

static const char *sample_rhythm(
    const behavior_function_context *context,
    void *world,
    const pin_value *,
    pin_value *out
)
{
    if (!context->instance_time) {
        return RHYTHM_NO_SCENE;
    }
    beat beat;
    if (!rhythm_sample(world, *context->instance_time, &beat)) {
        return RHYTHM_INVALID;
    }
    out[0].f = (float)beat.phase;
    out[1].f = (float)beat.pulse;
    out[2].f = (float)beat.period;
    out[3].f = (float)beat.duration;
    return NULL;
}
const node_host_call RHYTHM_CALL = {
    .service = "ecs.world",
    .context = sample_rhythm,
    .missing_service = RHYTHM_NO_SCENE,
};

const node_access RHYTHM_ACCESS = { .reads = &COMPONENT_Rhythm.id, .read_count = 1 };
