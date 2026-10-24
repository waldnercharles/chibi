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

enum
{
    OWNER_COUNT = 32,
    ITERATION_COUNT = 100,
    TRIAL_COUNT = 5,
    RUN_EVENT = 10,
    INSTRUCTION_BUDGET = 1000
};
static const script_catalog_parameter amount_parameter = {
    .name = "amount",
    .type = { .storage = PIN_F64 },
};
static const script_catalog_capability run_event = {
    .id = RUN_EVENT,
    .name = "Run",
    .kind = SCRIPT_CAPABILITY_EVENT,
    .parameters = &amount_parameter,
    .parameter_count = 1,
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
static execution_instance *instances[OWNER_COUNT];
static uint64_t total_state;

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
    total_state = 0;
    memset(instances, 0, sizeof instances);
}
static bool prepare(const char *source, behavior_backend backend)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "activation benchmark semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
        return false;
    }
    const script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(
                stderr,
                "activation benchmark lower: %s\n",
                plan->diagnostics[i].message
            );
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "activation benchmark publish: %s\n", error);
        return false;
    }
    for (int i = 0; i < analysis->symbol_count; i++) {
        if ((analysis->symbols[i].kind == SCRIPT_STATE ||
             analysis->symbols[i].kind == SCRIPT_SHARED_STATE) &&
            !strcmp(analysis->symbols[i].name, "total")) {
            total_state = analysis->symbols[i].id;
        }
    }
    world = ecs_world_new();
    program = execution_program_new(module, world, NULL);
    if (!program || !total_state) {
        return false;
    }
    for (int i = 0; i < OWNER_COUNT; i++) {
        ecs_entity owner = ecs_world_create_entity(world);
        instances[i] = execution_program_attach(program, ecs_entity_to_u64(owner));
        if (!instances[i]) {
            return false;
        }
        /* System retain belongs to the registration, not each query owner. */
        if ((analysis->kind != SCRIPT_PROGRAM_SYSTEM || !i) &&
            execution_instance_set_state(
                instances[i],
                total_state,
                (pin_value){ .type = PIN_F64, .d = i }
            )) {
            return false;
        }
    }
    return true;
}
static double seconds()
{
    struct timespec now;
    int status = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(!status);
    return now.tv_sec + now.tv_nsec * 1e-9;
}
static int iterate(bool system, int iteration, double now)
{
    if (system) {
        return execution_program_step(program, now, 0, INSTRUCTION_BUDGET);
    }
    int status = FLOW_OK;
    pin_value amount = { .type = PIN_F64, .d = 1 + iteration % 3 };
    for (int i = 0; i < OWNER_COUNT; i++) {
        status |= execution_instance_dispatch(instances[i], RUN_EVENT, &amount, 1, now, 0, INSTRUCTION_BUDGET);
    }
    return status;
}
static bool check_total(double expected)
{
    double checksum = 0;
    bool system = analysis->kind == SCRIPT_PROGRAM_SYSTEM;
    for (int i = 0; i < OWNER_COUNT; i++) {
        const pin_value *value = execution_instance_state(instances[i], total_state);
        double total = system ? expected * OWNER_COUNT : expected + i;
        if (!value || value->type != PIN_F64 || value->d != total ||
            execution_instance_pending(instances[i])) {
            return false;
        }
        checksum += value->d;
    }
    return checksum ==
           (system ? expected * OWNER_COUNT * OWNER_COUNT
                   : expected * OWNER_COUNT + OWNER_COUNT * (OWNER_COUNT - 1) / 2);
}
TEST_CASE(synchronous_activation_benchmark)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    const char *names[] = { "system", "event", "helper-event" };
    const char *sources[] = {
        "Bench :: system() { total:retain f64=0; total+=1; }",
        "Bench :: behavior { total:retain f64=0; on Run :: (amount: f64) "
        "{ "
        "total+=amount; } }",
        "Bench :: behavior { total:retain f64=0; Add :: (value:f64, "
        "amount:f64)->f64 { return value+amount; } on Run :: (amount: "
        "f64) "
        "{ total=Add(total,amount); } }",
    };
    for (int backend = 0; backend < 2; backend++) {
        for (int workload = 0; workload < 3; workload++) {
            REQUIRE(prepare(sources[workload], backends[backend]));
            bool system = workload == 0;
            xalloc_stats first_before, first_after;
            xalloc_stats_get(&first_before);
            uint64_t first_blocks = arena_block_allocations();
            int first_status = iterate(system, 0, 0);
            xalloc_stats_get(&first_after);
            first_blocks = arena_block_allocations() - first_blocks;
            REQUIRE(first_status == FLOW_OK);
            REQUIRE(check_total(1));
            double trial_total = 0;
            for (int i = 0; i < ITERATION_COUNT; i++) {
                trial_total += system ? 1 : 1 + i % 3;
            }
            printf(
                "ACTIVATION backend=%s workload=%s owners=%d iterations=%d "
                "trials=%d\n",
                behavior_backend_name(backends[backend]),
                names[workload],
                OWNER_COUNT,
                ITERATION_COUNT,
                TRIAL_COUNT
            );
            printf(
                "ACTIVATION warmup_xalloc_calls=%llu requested_bytes=%llu "
                "arena_blocks=%llu\n",
                (unsigned long long)(first_after.calls - first_before.calls),
                (unsigned long long)(first_after.requested_bytes -
                                     first_before.requested_bytes),
                (unsigned long long)first_blocks
            );
            for (int trial = 0; trial < TRIAL_COUNT; trial++) {
                xalloc_stats before, after;
                xalloc_stats_get(&before);
                uint64_t blocks = arena_block_allocations();
                int status = FLOW_OK;
                double start = seconds();
                for (int i = 0; i < ITERATION_COUNT; i++) {
                    status |= iterate(system, i, 1 + trial * ITERATION_COUNT + i);
                }
                double elapsed = seconds() - start;
                xalloc_stats_get(&after);
                blocks = arena_block_allocations() - blocks;
                REQUIRE(status == FLOW_OK);
                REQUIRE(check_total(1 + (trial + 1) * trial_total));
                printf(
                    "ACTIVATION trial=%d seconds=%.9f "
                    "ns_per_root_activation=%.3f "
                    "xalloc_calls=%llu requested_bytes=%llu "
                    "arena_blocks=%llu\n",
                    trial,
                    elapsed,
                    elapsed * 1e9 / (OWNER_COUNT * ITERATION_COUNT),
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
void test_language_activation_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(synchronous_activation_benchmark);
    pu_clear_setup();
}
