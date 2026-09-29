#include "runtime/run.h"

#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"

static const char missing_run[] = "Run nodes require a game world";

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

static node_desc *const NODES[] = { &GET_SEED, &RESTART_RUN, &RESTART_SCENE };
const registry_pack GAME_RUN_PACK = { .name = "game.run",
                                      .nodes = NODES,
                                      .node_count = (int)(sizeof NODES / sizeof *NODES) };
