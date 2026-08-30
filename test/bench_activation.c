/* test/bench_activation.c -- headless per-instance activation benchmark. */
#include "../core/array.h"
#include "../core/behavior_asset.h"
#include "../core/compile.h"
#include "../core/flow.h"
#include "../core/instance.h"
#include "../core/registry.h"
#include "../core/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum
{
    DEFAULT_INSTANCE_COUNT = 100,
    DEFAULT_FRAME_COUNT = 100,
    GRAPH_NODE_COUNT = 8,
    RESOLVED_NODE_COUNT = 7,
};

static const double TICK_DELTA_SECONDS = 0.016;

static const char *const RESOLVED_NODE_NAMES[RESOLVED_NODE_COUNT] = {
    "Event",        "Branch", "Get Variable", "Set Variable",
    "Reroute Data", "Select", "Bool",
};

static void fail(const char *message)
{
    fprintf(stderr, "bench_activation: %s\n", message);
    exit(EXIT_FAILURE);
}

static int parse_count(const char *text, int *out)
{
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < 1 || value > INT_MAX) {
        return -1;
    }
    *out = (int)value;
    return 0;
}

static const node_desc *resolve_node(const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    if (!desc) {
        fprintf(stderr, "bench_activation: unresolved node descriptor: %s\n", name);
    }
    return desc;
}

static behavior_asset *make_behavior_asset(void)
{
    const node_desc *event_desc = resolve_node("Event");
    const node_desc *branch_desc = resolve_node("Branch");
    const node_desc *get_desc = resolve_node("Get Variable");
    const node_desc *set_desc = resolve_node("Set Variable");
    const node_desc *reroute_desc = resolve_node("Reroute Data");
    const node_desc *select_desc = resolve_node("Select");
    const node_desc *bool_desc = resolve_node("Bool");
    if (!event_desc || !branch_desc || !get_desc || !set_desc ||
        !reroute_desc || !select_desc || !bool_desc) {
        return NULL;
    }
    if (get_desc != &NODE_GetVariable || set_desc != &NODE_SetVariable) {
        fail("variable descriptors do not match the registry descriptors");
    }

    behavior_asset *bp = behavior_asset_new("Activation benchmark");
    if (!bp) {
        return NULL;
    }
    behavior_variable *counter = behavior_asset_add_variable(
        bp,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 0 },
        "Benchmark"
    );
    behavior_variable *ticked = behavior_asset_add_variable(
        bp,
        "Ticked",
        PIN_BOOL,
        "bool",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_BOOL, .b = false },
        "Benchmark"
    );
    if (!counter || !ticked) {
        behavior_asset_free(bp);
        return NULL;
    }

    graph *g = bp->graph;
    node *event = graph_add_node(g, event_desc);
    node *branch = graph_add_node(g, branch_desc);
    node *set = behavior_asset_add_set_node(bp, ticked->id);
    node *get = behavior_asset_add_get_node(bp, counter->id);
    node *reroute_a = graph_add_node(g, reroute_desc);
    node *reroute_b = graph_add_node(g, reroute_desc);
    node *select = graph_add_node(g, select_desc);
    node *truth = graph_add_node(g, bool_desc);
    if (!event || !branch || !set || !get || !reroute_a || !reroute_b ||
        !select || !truth) {
        behavior_asset_free(bp);
        return NULL;
    }
    if (graph_set_event_name(g, event, "Tick") != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    pin_spec delta = { "DeltaSeconds", PIN_F64, "double", PIN_DATA };
    if (graph_add_pin(g, event, 1, &delta) < 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    if (graph_set_pin_default(g, truth, 0, 0, &(pin_value){ .type = PIN_BOOL, .b = true }) !=
        0) {
        behavior_asset_free(bp);
        return NULL;
    }
    if (graph_connect_exec(g, event, 0, branch, 0, 0) != 0 ||
        graph_connect_exec(g, branch, 0, set, 0, 0) != 0 ||
        graph_connect_data(g, get, 0, reroute_a, 0) != 0 ||
        graph_connect_data(g, reroute_a, 0, reroute_b, 0) != 0 ||
        graph_connect_data(g, reroute_b, 0, select, 0) != 0 ||
        graph_connect_data(g, truth, 0, select, 1) != 0 ||
        graph_connect_data(g, select, 0, branch, 1) != 0 ||
        graph_connect_data(g, select, 0, set, 1) != 0) {
        behavior_asset_free(bp);
        return NULL;
    }
    assert(acount(g->nodes) == GRAPH_NODE_COUNT);
    return bp;
}

static void check_queues_empty(behavior_instance **instances, int count)
{
    for (int i = 0; i < count; i++) {
        assert(acount(instances[i]->activations) == 0);
#ifdef NDEBUG
        if (acount(instances[i]->activations) != 0) {
            fail("an instance retained a completed activation");
        }
#endif
    }
}

static void dispatch_and_release(behavior_instance *instance, const flow_inputs *inputs)
{
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Tick", *inputs, &error);
    if (!run) {
        fprintf(
            stderr,
            "bench_activation: Tick dispatch failed: code=%d message=%s\n",
            error.code,
            error.message ? error.message : "<none>"
        );
        exit(EXIT_FAILURE);
    }
    if (flow_run_status(run) != FLOW_COMPLETED) {
        const flow_error *run_error = flow_run_error(run);
        fprintf(
            stderr,
            "bench_activation: Tick did not complete: state=%d code=%d "
            "message=%s\n",
            flow_run_status(run),
            run_error ? run_error->code : 0,
            run_error && run_error->message ? run_error->message : "<none>"
        );
        behavior_instance_release_activation(instance, run);
        exit(EXIT_FAILURE);
    }
    if (behavior_instance_release_activation(instance, run) != 0) {
        fail("could not release completed activation");
    }
    assert(acount(instance->activations) == 0);
#ifdef NDEBUG
    if (acount(instance->activations) != 0) {
        fail("an instance retained a completed activation");
    }
#endif
}

static uint64_t elapsed_ns(struct timespec start, struct timespec end)
{
    int64_t seconds = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nanoseconds = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
    return (uint64_t)(seconds * INT64_C(1000000000) + nanoseconds);
}

static void print_compile_errors(const compile_report *report)
{
    for (int i = 0; i < acount(report->items); i++) {
        fprintf(stderr, "bench_activation: %s\n", report->items[i].message);
    }
}

static uint64_t run_variant(behavior_asset *bp, int instance_count, int frame_count, int pooled)
{
    behavior_instance **instances = (behavior_instance **)
        xcalloc((size_t)instance_count, sizeof *instances);
    for (int i = 0; i < instance_count; i++) {
        instances[i] = behavior_instance_create(bp, NULL, NULL, NULL);
        if (!instances[i]) {
            fail("could not create benchmark instance");
        }
        if (pooled && behavior_instance_set_activation_pool(instances[i], 1) != 0) {
            fail("could not enable activation pool");
        }
    }
    flow_inputs inputs = {
        .values = &(pin_value){ .type = PIN_F64, .d = TICK_DELTA_SECONDS },
        .count = 1,
    };
    for (int i = 0; i < instance_count; i++) {
        dispatch_and_release(instances[i], &inputs);
    }
    check_queues_empty(instances, instance_count);

    struct timespec start;
    struct timespec end;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        fail("clock_gettime start failed");
    }
    for (int frame = 0; frame < frame_count; frame++) {
        for (int i = 0; i < instance_count; i++) {
            dispatch_and_release(instances[i], &inputs);
        }
        check_queues_empty(instances, instance_count);
    }
    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
        fail("clock_gettime end failed");
    }

    behavior_variable *ticked_variable = behavior_asset_find_variable_by_name(bp, "Ticked");
    pin_value ticked;
    if (!ticked_variable ||
        behavior_instance_get_variable(instances[0], ticked_variable->id, &ticked) != 0 ||
        !ticked.b) {
        fail("benchmark graph did not take the Branch true path");
    }
    uint64_t total_ns = elapsed_ns(start, end);
    size_t activation_count = (size_t)instance_count * (size_t)frame_count;
    printf("variant=%s\n", pooled ? "pooled_dense_store" : "unpooled_baseline");
    printf("elapsed_ns=%llu\n", (unsigned long long)total_ns);
    printf("activations=%zu\n", activation_count);
    printf("ns_per_activation=%.3f\n", (double)total_ns / (double)activation_count);

    for (int i = 0; i < instance_count; i++) {
        behavior_instance_destroy(instances[i]);
    }
    free(instances);
    return total_ns;
}

int main(int argc, char **argv)
{
    int instance_count = DEFAULT_INSTANCE_COUNT;
    int frame_count = DEFAULT_FRAME_COUNT;
    if (argc > 3 || (argc > 1 && parse_count(argv[1], &instance_count) != 0) ||
        (argc > 2 && parse_count(argv[2], &frame_count) != 0)) {
        fprintf(stderr, "usage: %s [instances] [frames]\n", argv[0]);
        return EXIT_FAILURE;
    }

    behavior_asset *bp = make_behavior_asset();
    if (!bp) {
        fail("could not construct benchmark behavior_asset");
    }
    compile_report report;
    compile_report_init(&report);
    if (behavior_asset_compile(bp, &report) != 0) {
        print_compile_errors(&report);
        compile_report_free(&report);
        behavior_asset_free(bp);
        fail("benchmark behavior_asset did not compile");
    }
    compile_report_free(&report);

    printf("bench_activation N=%d F=%d\n", instance_count, frame_count);
    printf("graph_nodes=%d\n", GRAPH_NODE_COUNT);
    printf("resolved_nodes=");
    for (int i = 0; i < RESOLVED_NODE_COUNT; i++) {
        printf("%s%s", i ? ", " : "", RESOLVED_NODE_NAMES[i]);
    }
    printf("\n");
    printf("resolution=registry_node_by_name(name) returned non-NULL for every descriptor\n");
    run_variant(bp, instance_count, frame_count, 0);
    run_variant(bp, instance_count, frame_count, 1);
    behavior_asset_free(bp);
    return EXIT_SUCCESS;
}
