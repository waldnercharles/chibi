#include "backend.h"
#include "ecs/ecs.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

enum
{
    OWNER_COUNT = 8,
    MAINTENANCE_COUNT = 12,
    TRIAL_COUNT = 5,
    BUDGET = 10000
};

typedef struct
{
    double x, y;
} binding_position;
typedef struct
{
    double x;
} binding_velocity;
static const script_catalog_field position_fields[] = {
    { .id = 1011, .name = "x", .type = { .storage = PIN_F64 }, .writable = true },
    { .id = 1012,
      .name = "y",
      .type = { .storage = PIN_F64 },
      .writable = true,
      .offset = offsetof(binding_position, y) },
};
static const script_catalog_field velocity_field = {
    .id = 1021,
    .name = "x",
    .type = { .storage = PIN_F64 },
    .writable = true,
};
static script_catalog_type types[] = {
    { .id = 101,
      .name = "Position",
      .type = { .storage = PIN_STRUCT, .nominal = 101 },
      .fields = position_fields,
      .field_count = 2,
      .size = sizeof(binding_position),
      .component = true,
      .component_id = 501 },
    { .id = 102,
      .name = "Velocity",
      .type = { .storage = PIN_STRUCT, .nominal = 102 },
      .fields = &velocity_field,
      .field_count = 1,
      .size = sizeof(binding_velocity),
      .component = true,
      .component_id = 502 },
};
static const script_catalog_global owner_global = {
    .id = 30,
    .name = "self",
    .type = { .storage = PIN_HANDLE, .nominal = 700 },
};
static const script_catalog catalog = {
    .types = types,
    .type_count = 2,
    .globals = &owner_global,
    .global_count = 1,
    .entity_type = 700,
};
static script_syntax *syntax[2];
static script_analysis *analysis[2];
static execution_module *modules[2];
static execution_program *programs[2];
static ecs_world *world;
static ecs_entity owners[OWNER_COUNT];

static void cleanup()
{
    for (int i = 0; i < 2; i++) {
        execution_program_free(programs[i]);
        execution_module_free(modules[i]);
        script_analysis_free(analysis[i]);
        script_syntax_free(syntax[i]);
        programs[i] = NULL;
        modules[i] = NULL;
        analysis[i] = NULL;
        syntax[i] = NULL;
    }
    ecs_world_free(world);
    world = NULL;
}

static bool global(void *, uint64_t id, behavior_function_instruction *out)
{
    assert(id == owner_global.id);
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}

static bool compile(int index, const char *source, behavior_backend backend)
{
    syntax[index] = script_parse(source);
    analysis[index] = script_analyze(syntax[index], &catalog);
    if (!analysis[index]->valid) {
        for (int i = 0; i < analysis[index]->diagnostic_count; i++) {
            fprintf(
                stderr,
                "binding benchmark semantic: %s\n",
                analysis[index]->diagnostics[i].message
            );
        }
        return false;
    }
    const script_lower_bindings bindings = { .global = global };
    execution_module_plan *plan = script_lower(analysis[index], &catalog, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(
                stderr,
                "binding benchmark lower: %s\n",
                plan->diagnostics[i].message
            );
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
    modules[index] = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!modules[index]) {
        fprintf(stderr, "binding benchmark publish: %s\n", error);
        return false;
    }
    programs[index] = execution_program_new(modules[index], world, NULL);
    return programs[index] != NULL;
}

static bool prepare(behavior_backend backend)
{
    world = ecs_world_new();
    for (int i = 0; i < 2; i++) {
        const type_desc *type = type_catalog_find(types[i].name);
        REQUIRE(type && type->size == types[i].size);
        ecs_component_desc component = {
            .id = types[i].component_id,
            .key = types[i].name,
            .name = types[i].name,
            .element_size = type->size,
            .type = type,
        };
        REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
        types[i].layout = type_layout_hash(type);
        REQUIRE(ecs_component_track_changes(world, types[i].component_id) == ECS_OK);
    }
    /* Reverse dependency order forces the cross-program schedule to order
     * Velocity.x -> Position.x -> Position.y for each owner. */
    REQUIRE(compile(0, "behavior Finish { bind self.Position.y = self.Position.x + 1; }", backend));
    REQUIRE(
        compile(1, "behavior Seed { seed: double = 1; bind self.Velocity.x = seed; bind self.Position.x = self.Velocity.x + 1; }", backend)
    );
    for (int i = 0; i < OWNER_COUNT; i++) {
        owners[i] = ecs_world_create_entity(world);
        REQUIRE(
            ecs_world_record_add(world, owners[i], 501, &(binding_position){ 0 }, 0) == ECS_OK
        );
        REQUIRE(
            ecs_world_record_add(world, owners[i], 502, &(binding_velocity){ 0 }, 0) == ECS_OK
        );
        for (int p = 0; p < 2; p++) {
            REQUIRE(
                execution_program_attach(programs[p], ecs_entity_to_u64(owners[i])) != NULL
            );
        }
    }
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    return true;
}

static bool correct_values()
{
    for (int i = 0; i < OWNER_COUNT; i++) {
        const binding_position *p = ecs_component_data_const(world, owners[i], 501);
        const binding_velocity *v = ecs_component_data_const(world, owners[i], 502);
        REQUIRE(p && v && p->x == 2 && p->y == 3 && v->x == 1);
    }
    return true;
}

static double seconds()
{
    struct timespec time;
    int status = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(status == 0);
    (void)status;
    return time.tv_sec + time.tv_nsec * 1e-9;
}

TEST_CASE(binding_schedule_benchmark)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int churn = 0; churn < 2; churn++) {
            REQUIRE(prepare(backends[b]));
            char error[256];
            REQUIRE(
                execution_program_bindings_all(programs, 2, 0, 0, BUDGET, error, sizeof error) == FLOW_OK
            );
            REQUIRE(correct_values());
            printf(
                "BINDING_BENCH backend=%s mode=%s owners=%d "
                "bindings_per_owner=3 maintenance=%d trials=%d\n",
                behavior_backend_name(backends[b]),
                churn ? "membership-churn" : "stable",
                OWNER_COUNT,
                MAINTENANCE_COUNT,
                TRIAL_COUNT
            );
            for (int trial = 0; trial < TRIAL_COUNT; trial++) {
                uint64_t position_versions[OWNER_COUNT],
                    velocity_versions[OWNER_COUNT];
                for (int i = 0; i < OWNER_COUNT; i++) {
                    position_versions[i] = ecs_component_version(world, owners[i], 501);
                    velocity_versions[i] = ecs_component_version(world, owners[i], 502);
                }
                int status = FLOW_OK;
                bool attached = true;
                xalloc_stats before, after;
                xalloc_stats_get(&before);
                uint64_t blocks = arena_block_allocations();
                double start = seconds();
                for (int n = 0; n < MAINTENANCE_COUNT; n++) {
                    if (churn) {
                        uint64_t owner = ecs_entity_to_u64(owners[OWNER_COUNT - 1]);
                        if (n % 2) {
                            attached &= execution_program_attach(programs[1], owner) !=
                                        NULL;
                        } else {
                            execution_program_detach(programs[1], owner);
                        }
                    }
                    status |= execution_program_bindings_all(
                        programs,
                        2,
                        1 + trial * MAINTENANCE_COUNT + n,
                        0,
                        BUDGET,
                        error,
                        sizeof error
                    );
                }
                double elapsed = seconds() - start;
                xalloc_stats_get(&after);
                blocks = arena_block_allocations() - blocks;
                REQUIRE(attached && status == FLOW_OK);
                REQUIRE(correct_values());
                for (int i = 0; i < OWNER_COUNT; i++) {
                    uint64_t seed_runs = churn && i == OWNER_COUNT - 1
                                           ? MAINTENANCE_COUNT / 2
                                           : MAINTENANCE_COUNT;
                    REQUIRE(
                        ecs_component_version(world, owners[i], 501) -
                            position_versions[i] ==
                        MAINTENANCE_COUNT + seed_runs
                    );
                    REQUIRE(
                        ecs_component_version(world, owners[i], 502) -
                            velocity_versions[i] ==
                        seed_runs
                    );
                }
                printf("BINDING_BENCH trial=%d seconds=%.9f us_per_maintenance=%.3f\n", trial, elapsed, elapsed * 1e6 / MAINTENANCE_COUNT);
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

void test_language_binding_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(binding_schedule_benchmark);
    pu_clear_setup();
}
