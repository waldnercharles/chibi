#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
/* The debugger's runtime support: stepping through function calls in every
   backend (Step enters a call, Step Over runs one as a node, Step Out finishes
   the call it is in), and failed runs logged at their node. */
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "runtime/behaviors/behavior.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <string.h>

typedef struct
{
    behavior_asset *asset;
    node *call, *inside, *after_inside;
    char printed[16];
} stepping;

static void printed(const node *, const char *message, void *user)
{
    stepping *s = user;
    size_t length = strlen(s->printed);
    assert(length + strlen(message) < sizeof s->printed);
    strcat(s->printed, message);
}

static node *print(graph *g, const char *text)
{
    node *n = graph_add_node(g, &NODE_PrintString);
    pin_value value = { .type = PIN_STR, .type_name = "const char *", .str = text };
    assert(graph_set_pin_default(g, n, 0, 1, &value) == 0);
    return n;
}

/* Go: print a, call F, print d. F: print b, print c. The breakpoint is on
   print b, or on the call. */
static stepping make_stepping(bool inside)
{
    stepping s = { .asset = behavior_asset_new("Stepping") };
    graph *f = behavior_asset_add_function(s.asset, "F", 0);
    node *entry = graph_node_by_id(f, f->entry_id);
    node *ret = graph_node_by_id(f, f->return_id);
    s.inside = print(f, "b");
    s.after_inside = print(f, "c");
    graph_connect_exec(f, entry, 0, s.inside, 0, 0);
    graph_connect_exec(f, s.inside, 0, s.after_inside, 0, 0);
    graph_connect_exec(f, s.after_inside, 0, ret, 0, 0);
    graph *g = s.asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    assert(graph_set_event_name(g, event, "Go") == 0);
    node *before = print(g, "a");
    s.call = behavior_asset_add_function_call_node(s.asset, f);
    node *after = print(g, "d");
    graph_connect_exec(g, event, 0, before, 0, 0);
    graph_connect_exec(g, before, 0, s.call, 0, 0);
    graph_connect_exec(g, s.call, 0, after, 0, 0);
    assert(graph_set_breakpoint(inside ? f : g, inside ? s.inside : s.call, 1) == 0);
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(s.asset, &report) == 0);
    compile_report_free(&report);
    return s;
}

static unsigned depth(const flow_run *run)
{
    return flow_run_call_depth(flow_run_active_leaf(run));
}

/* Dispatches Go on `backend` and returns the run paused at the breakpoint. */
static flow_run *go(stepping *s, behavior_backend backend, behavior_instance **out)
{
    s->printed[0] = '\0';
    behavior_instance *bi = behavior_instance_create(s->asset, NULL, NULL, NULL);
    assert(bi && behavior_instance_set_backend(bi, backend) == 0);
    behavior_instance_set_breakpoints_enabled(bi, true);
    bi->sink = printed;
    bi->sink_user = s;
    flow_run *run = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_status(run) == FLOW_PAUSED);
    *out = bi;
    return run;
}

static void finish(stepping *s, flow_run *run, behavior_instance *bi, const char *label)
{
    test_check(
        flow_run_resume(run) == FLOW_OK &&
            flow_run_status(run) == FLOW_COMPLETED && !strcmp(s->printed, "abcd"),
        label
    );
    behavior_instance_destroy(bi);
}

static void step_within_call(behavior_backend backend)
{
    stepping s = make_stepping(true);
    behavior_instance *bi;
    flow_run *run = go(&s, backend, &bi);
    test_check(
        flow_run_current_node(run) == s.inside && depth(run) == 1 &&
            !strcmp(s.printed, "a"),
        "a breakpoint inside a function pauses the caller's run in the call"
    );
    test_check(
        flow_run_step_over(run) == FLOW_PAUSED_RESULT &&
            flow_run_current_node(run) == s.after_inside && depth(run) == 1 &&
            !strcmp(s.printed, "ab"),
        "Step Over inside a call advances one node of that call"
    );
    test_check(
        flow_run_step_out(run) == FLOW_PAUSED_RESULT && depth(run) == 0 &&
            !strcmp(s.printed, "abc"),
        "Step Out finishes the call and pauses in its caller"
    );
    finish(&s, run, bi, "a run resumed after Step Out completes once");
    behavior_asset_free(s.asset);
}

static void step_at_call(behavior_backend backend, int command)
{
    stepping s = make_stepping(false);
    behavior_instance *bi;
    flow_run *run = go(&s, backend, &bi);
    assert(flow_run_current_node(run) == s.call && depth(run) == 0);
    if (command == 0) {
        test_check(
            flow_run_step(run) == FLOW_PAUSED_RESULT &&
                flow_run_current_node(run) == s.inside && depth(run) == 1 &&
                !strcmp(s.printed, "a"),
            "Step at a call pauses at the call's first node"
        );
        test_check(
            flow_run_step_out(run) == FLOW_PAUSED_RESULT && depth(run) == 0 &&
                !strcmp(s.printed, "abc"),
            "Step Out from an entered call returns to the caller"
        );
    } else if (command == 1) {
        test_check(
            flow_run_step_over(run) == FLOW_PAUSED_RESULT && depth(run) == 0 &&
                !strcmp(s.printed, "abc"),
            "Step Over at a call runs the whole call"
        );
    } else {
        test_check(
            flow_run_step_out(run) == FLOW_OK &&
                flow_run_status(run) == FLOW_COMPLETED && !strcmp(s.printed, "abcd"),
            "Step Out outside any call resumes the run"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(s.asset);
        return;
    }
    finish(&s, run, bi, "a run resumed after stepping completes once");
    behavior_asset_free(s.asset);
}

static int fail(node *, struct flow_context *ctx)
{
    return flow_context_fail(ctx, FLOW_ERR_NODE, "resumed failure");
}

static pin_spec exec_in[] = { { .name = "Exec", .kind = PIN_EXEC } };
static const node_desc failing = {
    .name = "Resumed failure test",
    .inputs = exec_in,
    .input_count = 1,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = fail,
};

/* A run the debugger resumes into a failure is logged like one that fails at once. */
static void resumed_failure()
{
    behavior_asset *asset = behavior_asset_new("Failing");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(graph_set_event_name(asset->graph, event, "Go") == 0);
    node *broken = graph_add_node(asset->graph, &failing);
    graph_connect_exec(asset->graph, event, 0, broken, 0, 0);
    assert(graph_set_breakpoint(asset->graph, broken, 1) == 0);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    assert(game_behavior_register_asset(world, "failing", asset) == 0);
    ecs_entity entity = ecs_world_create_entity(ecs);
    assert(game_behavior_attach(world, entity, "failing") == 0);
    behavior_instance *instance = game_behavior_instance(world, entity, "failing");
    assert(behavior_instance_set_backend(instance, BEHAVIOR_ORACLE) == 0);
    behavior_instance_set_breakpoints_enabled(instance, true);
    assert(game_behavior_dispatch(world, entity, "Go", (flow_inputs){ 0 }, NULL) >= 0);
    game_behavior_attachment_info info;
    flow_run *run = game_behavior_paused_run(world, &info);
    assert(run && game_behavior_log_count(world) == 0);
    assert(flow_run_resume(run) == FLOW_ERR_NODE);
    game_behavior_pump(world, 0);
    game_behavior_log_info log = { 0 };
    test_check(
        game_behavior_log_count(world) == 1 &&
            game_behavior_log_info_at(world, 0, &log) == 0 && log.failure &&
            log.origin == broken && !strcmp(log.asset_name, "failing") &&
            !strcmp(log.message, "resumed failure") &&
            !game_behavior_paused_run(world, &info),
        "a run that fails after the debugger resumes it is logged at its node"
    );
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(asset);
}

/* A Tick that fails in the prepared update loop is logged at its node. */
static void failed_tick()
{
    behavior_asset *asset = behavior_asset_new("Endless");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    node *tick = graph_add_node(asset->graph, &NODE_Event);
    assert(graph_set_event_name(asset->graph, tick, "Tick") == 0);
    node *loop = graph_add_node(asset->graph, &NODE_WhileLoop);
    assert(
        graph_set_pin_default(asset->graph, loop, 0, 1, &(pin_value){ .type = PIN_BOOL, .b = true }) == 0
    );
    graph_connect_exec(asset->graph, tick, 0, loop, 0, 0);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    assert(game_behavior_register_asset(world, "endless", asset) == 0);
    ecs_entity entity = ecs_world_create_entity(ecs);
    assert(game_behavior_attach(world, entity, "endless") == 0);
    game_behavior_update(world, NULL);
    game_behavior_log_info log = { 0 };
    test_check(
        game_behavior_last_update(world).prepared == 1 &&
            game_behavior_log_count(world) == 1 &&
            game_behavior_log_info_at(world, 0, &log) == 0 && log.failure &&
            log.origin == loop,
        "a Tick that fails in the prepared update loop is logged at its node"
    );
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(asset);
}

void test_debugging()
{
    failed_tick();
    resumed_failure();
    const behavior_backend backends[] = { BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 3; b++) {
        step_within_call(backends[b]);
        for (int command = 0; command < 3; command++) {
            step_at_call(backends[b], command);
        }
    }
}
