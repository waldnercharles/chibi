#include "runtime/run.h"

#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"

#include <math.h>

static const char missing_run[] = "Run nodes require a game world";

/* Faster than this would stretch a fixed step past what physics solves. */
#define TIME_SCALE_MAX 4.0f

void game_run_start(game_run *run, int seed)
{
    *run = (game_run){ .seed = seed, .time_scale = 1 };
}

/* Float durations land a hair past the step that should end them. */
#define HIT_STOP_EPSILON 1e-5f

float game_run_time_scale(const game_run *run)
{
    return run->hit_stop_seconds > HIT_STOP_EPSILON ? run->hit_stop_scale
                                                    : run->time_scale;
}

void game_run_advance(game_run *run, float real_seconds)
{
    run->hit_stop_seconds = fmaxf(0, run->hit_stop_seconds - real_seconds);
}

static bool valid_scale(float scale)
{
    return isfinite(scale) && scale >= 0 && scale <= TIME_SCALE_MAX;
}

static const char *seed_call(void *service, const pin_value *, pin_value *out)
{
    game_run *run = service;
    if (!run) {
        return missing_run;
    }
    out[0] = (pin_value){ .type = PIN_I32, .type_name = "int", .i = run->seed };
    return NULL;
}
static const char *seed_read(const eval_context *ctx, const pin_value *in, arena *, pin_value *out)
{
    return seed_call(eval_context_service(ctx, "game.run"), in, out);
}
static const node_access READ = { .flags = NODE_EFFECT_WORLD_READ };
static pin_spec SEED_OUT[] = { { .name = "Seed", .type = PIN_I32, .type_name = "int" } };
static node_desc GET_SEED = {
    .name = "Get Run Seed",
    .category = "Run",
    .keywords = "seed random procedural run replay",
    .description = "Returns this run's seed: new on every start and Restart "
                   "Run, kept by Restart Scene, fixed when a host pins it.",
    .output_count = 1,
    .outputs = SEED_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = seed_read,
    .host_call = &(const node_host_call){ .service = "game.run",
                                          .invoke = seed_call,
                                          .missing_service = missing_run },
    .access = &READ,
};

static const char *request(void *service, game_run_restart restart)
{
    game_run *run = service;
    if (!run) {
        return missing_run;
    }
    run->restart = restart;
    return NULL;
}
static const char *restart_run_call(void *service, const pin_value *, pin_value *)
{
    return request(service, GAME_RUN_RESTART_RUN);
}
static const char *restart_scene_call(void *service, const pin_value *, pin_value *)
{
    return request(service, GAME_RUN_RESTART_SCENE);
}
static int finish(flow_context *ctx, const char *error)
{
    return error ? flow_context_fail(ctx, FLOW_ERR_NODE, error)
                 : flow_context_continue(ctx, 0);
}
static int restart_run(node *, flow_context *ctx)
{
    return finish(ctx, restart_run_call(flow_context_service(ctx, "game.run"), NULL, NULL));
}
static int restart_scene(node *, flow_context *ctx)
{
    return finish(ctx, restart_scene_call(flow_context_service(ctx, "game.run"), NULL, NULL));
}

static const node_access SINK = { .flags = NODE_EFFECT_HOST_SINK };
static pin_spec EXEC_IN[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC } };
static pin_spec THEN_OUT[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
#define RESTART_NODE(SYMBOL, NAME, CALL, FLOW, DESCRIPTION)                     \
    static node_desc SYMBOL = {                                                 \
        .name = NAME,                                                           \
        .category = "Run",                                                      \
        .keywords = "restart retry reset reload run scene seed",                \
        .description = DESCRIPTION,                                             \
        .input_count = 1,                                                       \
        .inputs = EXEC_IN,                                                      \
        .output_count = 1,                                                      \
        .outputs = THEN_OUT,                                                    \
        .flow_kind = NODE_FLOW_CUSTOM,                                          \
        .flow = FLOW,                                                           \
        .host_call = &(const node_host_call){ .service = "game.run",            \
                                              .invoke = CALL,                   \
                                              .missing_service = missing_run }, \
        .display_class = NODE_DISPLAY_FLOW,                                     \
        .access = &SINK                                                         \
    }
RESTART_NODE(
    RESTART_RUN,
    "Restart Run",
    restart_run_call,
    restart_run,
    "Asks the host to restart the scene on a new run seed once this step ends."
);
RESTART_NODE(RESTART_SCENE, "Restart Scene", restart_scene_call, restart_scene, "Asks the host to restart the scene on the same run seed once this step ends.");
#undef RESTART_NODE

static const char *time_scale_call(void *service, const pin_value *in, pin_value *)
{
    game_run *run = service;
    if (!run) {
        return missing_run;
    }
    if (!valid_scale(in[0].f)) {
        return "Set Time Scale needs a Scale from 0 to 4";
    }
    run->time_scale = in[0].f;
    return NULL;
}
static const char *hit_stop_call(void *service, const pin_value *in, pin_value *)
{
    game_run *run = service;
    if (!run) {
        return missing_run;
    }
    if (!isfinite(in[0].f) || in[0].f < 0 || !valid_scale(in[1].f)) {
        return "Hit Stop needs a nonnegative Duration and a Scale from 0 to 4";
    }
    run->hit_stop_seconds = in[0].f;
    run->hit_stop_scale = in[1].f;
    return NULL;
}
static int eval_inputs(flow_context *ctx, pin_value *in, int count)
{
    int result = 0;
    for (int i = 0; i < count && result >= 0; i++) {
        result = flow_context_eval_data(ctx, i + 1, &in[i]);
    }
    return result;
}
static int time_scale(node *, flow_context *ctx)
{
    pin_value in[1];
    int result = eval_inputs(ctx, in, 1);
    return result < 0
             ? result
             : finish(ctx, time_scale_call(flow_context_service(ctx, "game.run"), in, NULL));
}
static int hit_stop(node *, flow_context *ctx)
{
    pin_value in[2];
    int result = eval_inputs(ctx, in, 2);
    return result < 0
             ? result
             : finish(ctx, hit_stop_call(flow_context_service(ctx, "game.run"), in, NULL));
}
#define FLOAT_PIN(NAME, VALUE)                                                  \
    {                                                                           \
        .name = NAME, .type = PIN_F32, .type_name = "float", .default_value = { \
            .type = PIN_F32,                                                    \
            .f = VALUE,                                                         \
        }                                                                       \
    }
static pin_spec TIME_SCALE_IN[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
                                    FLOAT_PIN("Scale", 1) };
static pin_spec HIT_STOP_IN[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
                                  FLOAT_PIN("Duration", .08f),
                                  FLOAT_PIN("Scale", 0) };
#undef FLOAT_PIN
static node_desc SET_TIME_SCALE = {
    .name = "Set Time Scale",
    .category = "Run",
    .keywords = "time scale slow motion pause bullet time speed",
    .description = "Runs simulation at Scale (0 to 4) times real time; 0 "
                   "pauses "
                   "all but GameUnscaledTime entities' behaviors.",
    .input_count = 2,
    .inputs = TIME_SCALE_IN,
    .output_count = 1,
    .outputs = THEN_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = time_scale,
    .host_call = &(const node_host_call){ .service = "game.run",
                                          .invoke = time_scale_call,
                                          .missing_service = missing_run },
    .display_class = NODE_DISPLAY_FLOW,
    .access = &SINK,
};
static node_desc HIT_STOP = {
    .name = "Hit Stop",
    .category = "Run",
    .keywords = "hit stop freeze frame impact pause time scale juice",
    .description = "Runs simulation at Scale times real time for Duration real "
                   "seconds, then returns to the time scale.",
    .input_count = 3,
    .inputs = HIT_STOP_IN,
    .output_count = 1,
    .outputs = THEN_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = hit_stop,
    .host_call = &(const node_host_call){ .service = "game.run",
                                          .invoke = hit_stop_call,
                                          .missing_service = missing_run },
    .display_class = NODE_DISPLAY_FLOW,
    .access = &SINK,
};

static node_desc *const NODES[] = { &GET_SEED,
                                    &RESTART_RUN,
                                    &RESTART_SCENE,
                                    &SET_TIME_SCALE,
                                    &HIT_STOP };
const registry_pack GAME_RUN_PACK = { .name = "game.run",
                                      .nodes = NODES,
                                      .node_count = (int)(sizeof NODES / sizeof *NODES) };
