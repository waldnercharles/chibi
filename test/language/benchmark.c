#include "backend.h"
#include "ecs/ecs.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include "../../src/execution/wasm_jit.h"
#endif

#define LOOP_COUNT 100
#define DISPATCH_COUNT 200
#define TRIAL_COUNT 5
#define INSTRUCTION_BUDGET 100000
#define EXPECTED_TOTAL 14950
#define RUN_EVENT 10

static const script_catalog_capability run_event = {
    .id = RUN_EVENT,
    .name = "Run",
    .kind = SCRIPT_CAPABILITY_EVENT,
};
static const script_catalog catalog = {
    .capabilities = &run_event,
    .capability_count = 1,
};
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static ecs_world *world;
static double publish_seconds;
#ifdef __EMSCRIPTEN__
static execution_wasm_jit_stats publish_stats;
#endif
static double monotonic_seconds();

static void cleanup()
{
    execution_program_free(program);
    execution_module_free(module);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    program = NULL;
    module = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
}

static bool compile(const char *source, behavior_backend backend)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(stderr, "benchmark semantic: %s\n", analysis->diagnostics[i].message);
        }
        return false;
    }
    const script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(stderr, "benchmark lower: %s\n", plan->diagnostics[i].message);
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
#endif
    double start = monotonic_seconds();
    module = execution_module_finish(&plan, backend, error, sizeof error);
    publish_seconds = monotonic_seconds() - start;
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_get_stats(&after);
    publish_stats = (execution_wasm_jit_stats){
        .compilations = after.compilations - before.compilations,
        .executions = after.executions - before.executions,
        .compile_failures = after.compile_failures - before.compile_failures,
    };
#endif
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "benchmark publish: %s\n", error);
        return false;
    }
    world = ecs_world_new();
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}

static uint64_t total_id()
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, "total")) {
            return analysis->symbols[i].id;
        }
    }
    return 0;
}

static double monotonic_seconds()
{
    struct timespec time;
    int status = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(status == 0);
    return time.tv_sec + time.tv_nsec * 1e-9;
}

TEST_CASE(scalar_authored_call_benchmark)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    const char *expressions[] = { "i * 3 + 1", "Step(i)" };
    for (int b = 0; b < 2; b++) {
        for (int helper = 0; helper < 2; helper++) {
            char source[1024];
            int size = snprintf(
                source,
                sizeof source,
                "behavior Bench { total: double = 0; "
                "Step :: fn(x: double) -> (value: double) { return(x * 3 + "
                "1); } "
                "on Run() { sum := 0.0; i := 0.0; "
                "while i < %d { sum += %s; i += 1; } total = sum; } }",
                LOOP_COUNT,
                expressions[helper]
            );
            REQUIRE(size > 0 && (size_t)size < sizeof source);
            REQUIRE(compile(source, backends[b]));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance != NULL);
            uint64_t state = total_id();
            REQUIRE(state != 0);
            REQUIRE(
                execution_instance_dispatch(instance, RUN_EVENT, NULL, 0, 0, 0, INSTRUCTION_BUDGET) == FLOW_OK
            );
            const pin_value *total = execution_instance_state(instance, state);
            REQUIRE(total && total->type == PIN_F64 && total->d == EXPECTED_TOTAL);
            printf(
                "BENCH backend=%s helper=%d loop=%d dispatches=%d trials=%d "
                "module_bytes=%zu publish_ms=%.6f\n",
                behavior_backend_name(backends[b]),
                helper,
                LOOP_COUNT,
                DISPATCH_COUNT,
                TRIAL_COUNT,
                execution_module_storage_bytes(module),
                publish_seconds * 1e3
            );
#ifdef __EMSCRIPTEN__
            printf(
                "PUBLISH installations=%llu executions=%llu "
                "compile_failures=%llu\n",
                (unsigned long long)publish_stats.compilations,
                (unsigned long long)publish_stats.executions,
                (unsigned long long)publish_stats.compile_failures
            );
#endif
            for (int trial = 0; trial < TRIAL_COUNT; trial++) {
                int status = FLOW_OK;
                xalloc_stats before, after;
                xalloc_stats_get(&before);
                uint64_t blocks = arena_block_allocations();
                double start = monotonic_seconds();
                for (int n = 0; n < DISPATCH_COUNT; n++) {
                    status |= execution_instance_dispatch(instance, RUN_EVENT, NULL, 0, 0, 0, INSTRUCTION_BUDGET);
                }
                double elapsed = monotonic_seconds() - start;
                xalloc_stats_get(&after);
                blocks = arena_block_allocations() - blocks;
                REQUIRE(status == FLOW_OK);
                REQUIRE(execution_instance_state(instance, state)->d == EXPECTED_TOTAL);
                printf(
                    "BENCH trial=%d seconds=%.9f ns_per_loop=%.3f "
                    "us_per_dispatch=%.3f\n",
                    trial,
                    elapsed,
                    elapsed * 1e9 / (LOOP_COUNT * DISPATCH_COUNT),
                    elapsed * 1e6 / DISPATCH_COUNT
                );
                printf(
                    "ALLOC calls=%llu requested_bytes=%llu arena_blocks=%llu\n",
                    (unsigned long long)(after.calls - before.calls),
                    (unsigned long long)(after.requested_bytes - before.requested_bytes),
                    (unsigned long long)blocks
                );
            }
            cleanup();
        }
    }
    return true;
}

void test_language_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scalar_authored_call_benchmark);
    pu_clear_setup();
}
