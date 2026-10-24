#include "backend.h"
#include "ecs/ecs.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum
{
    RUN_EVENT = 10,
    DISPATCHES = 32,
    TRIALS = 5,
    INSTRUCTION_BUDGET = 10000
};

static const script_catalog_capability event = {
    .id = RUN_EVENT,
    .name = "Run",
    .kind = SCRIPT_CAPABILITY_EVENT,
};
static const script_catalog catalog = { .capabilities = &event, .capability_count = 1 };
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static execution_program *program;
static ecs_world *world;
static arena input_storage;

static void cleanup()
{
    execution_program_free(program);
    execution_module_free(module);
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    arena_free(&input_storage);
    program = NULL;
    module = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
}

static double seconds()
{
    struct timespec time;
    int status = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(status == 0);
    (void)status;
    return time.tv_sec + time.tv_nsec * 1e-9;
}

static uint64_t state_id(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}

static bool compile(behavior_backend backend, int helpers)
{
    const char *sources[] = {
        "BorrowBench :: behavior { values: f64[] = []; total: f64 = 0;on "
        "Run :: () { sum: f64 = 0; for value in values { sum += value; } total "
        "+= sum; "
        "} }",
        "BorrowBench :: behavior { values: f64[] = []; total: f64 = "
        "0;Sum :: (input: f64[]) -> f64 { sum: f64 = 0; for value in input "
        "{ sum += value; } return sum; }on Run :: () { total += Sum(values); } "
        "}",
        "BorrowBench :: behavior { values: f64[] = []; total: f64 = "
        "0;Sum :: (input: f64[]) -> f64 { sum: f64 = 0; for value in input "
        "{ sum += value; } return sum; }Forward :: (input: f64[]) -> f64 "
        "{ return Sum(input); }on Run :: () { total += Forward(values); } }"
    };
    syntax = script_parse(sources[helpers]);
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    if (analysis && !analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "borrow benchmark semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
    }
    REQUIRE(analysis && analysis->valid);
    const script_lower_bindings bindings = { 0 };
    plan = script_lower(analysis, &catalog, &bindings);
    if (plan && !plan->valid) {
        for (int i = 0; i < plan->diagnostic_count; i++) {
            fprintf(stderr, "borrow benchmark lower: %s\n", plan->diagnostics[i].message);
        }
    }
    REQUIRE(plan && plan->valid);
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "borrow benchmark publish: %s\n", error);
    }
    REQUIRE(module);
    world = ecs_world_new();
    program = execution_program_new(module, world, NULL);
    REQUIRE(program);
    return true;
}

TEST_CASE(borrow_authored_collection_benchmark)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    const int sizes[] = { 1, 128 };
    for (int backend = 0; backend < 2; backend++) {
        for (int workload = 0; workload < 6; workload++) {
            int helpers = workload / 2, sample = workload % 2;
            REQUIRE(compile(backends[backend], helpers));
            uint64_t values_id = state_id("values"), total_id = state_id("total");
            REQUIRE(values_id && total_id);
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            REQUIRE(instance);
            int items = sizes[sample];
            pin_array *values = pin_array_allocate(&input_storage, PIN_F64, NULL, items);
            for (int i = 0; i < items; i++) {
                values->items[i].d = i + 1;
            }
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    values_id,
                    (pin_value){ .type = PIN_F64, .container = PIN_CONTAINER_ARRAY, .ptr = values }
                ) == FLOW_OK
            );
            arena_free(&input_storage);
            double expected = (double)items * (items + 1) / 2;
            REQUIRE(
                execution_instance_dispatch(instance, RUN_EVENT, NULL, 0, 0, 0, INSTRUCTION_BUDGET) == FLOW_OK
            );
            REQUIRE(execution_instance_state(instance, total_id)->d == expected);
            printf(
                "BORROW backend=%s helpers=%d items=%d dispatches=%d trials=%d "
                "element_visits=%d expected=%.0f module_bytes=%zu\n",
                behavior_backend_name(backends[backend]),
                helpers,
                items,
                DISPATCHES,
                TRIALS,
                items * DISPATCHES,
                expected,
                execution_module_storage_bytes(module)
            );
            for (int trial = 0; trial < TRIALS; trial++) {
                int status = FLOW_OK;
                xalloc_stats before, after;
                xalloc_stats_get(&before);
                uint64_t blocks = arena_block_allocations();
                double start = seconds();
                for (int i = 0; i < DISPATCHES; i++) {
                    double now = (double)(1 + trial * DISPATCHES + i);
                    status |= execution_instance_dispatch(instance, RUN_EVENT, NULL, 0, now, 1, INSTRUCTION_BUDGET);
                }
                double elapsed = seconds() - start;
                xalloc_stats_get(&after);
                blocks = arena_block_allocations() - blocks;
                double checksum = execution_instance_state(instance, total_id)->d;
                REQUIRE(status == FLOW_OK);
                REQUIRE(checksum == expected * (1 + (trial + 1) * DISPATCHES));
                printf(
                    "BORROW trial=%d seconds=%.9f us_per_dispatch=%.3f "
                    "checksum=%.0f alloc_calls=%llu requested_bytes=%llu "
                    "arena_blocks=%llu\n",
                    trial,
                    elapsed,
                    elapsed * 1e6 / DISPATCHES,
                    checksum,
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

void test_language_borrow_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(borrow_authored_collection_benchmark);
    pu_clear_setup();
}
