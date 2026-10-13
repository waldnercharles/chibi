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
#include <string.h>
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
static script_syntax *syntax[3];
static script_analysis *analysis[3];
static execution_module *modules[3];
static execution_program *programs[3];
static execution_program *extra_programs[17];
static ecs_world *world;
static ecs_entity owners[OWNER_COUNT];

static void clear_slot(int i)
{
    execution_program_free(programs[i]);
    execution_module_free(modules[i]);
    script_analysis_free(analysis[i]);
    script_syntax_free(syntax[i]);
    programs[i] = NULL;
    modules[i] = NULL;
    analysis[i] = NULL;
    syntax[i] = NULL;
}

static void cleanup()
{
    for (int i = 0; i < 17; i++) {
        execution_program_free(extra_programs[i]);
        extra_programs[i] = NULL;
    }
    for (int i = 0; i < 3; i++) {
        clear_slot(i);
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

static bool maintain(execution_program *const *values, int count, double now)
{
    char error[256] = { 0 };
    int status = execution_program_bindings_all(values, count, now, 0, BUDGET, error, sizeof error);
    if (status) {
        fprintf(stderr, "binding schedule: %s (%d)\n", error, status);
    }
    REQUIRE(status == FLOW_OK);
    return true;
}

static bool replace(int index, const char *source, behavior_backend backend)
{
    REQUIRE(compile(2, source, backend));
    int status = execution_program_replace(programs[index], modules[2], false);
    clear_slot(2);
    REQUIRE(status == FLOW_OK);
    return true;
}

static bool values(double x, double y, double velocity)
{
    for (int i = 0; i < OWNER_COUNT; i++) {
        const binding_position *p = ecs_component_data_const(world, owners[i], 501);
        const binding_velocity *v = ecs_component_data_const(world, owners[i], 502);
        REQUIRE(p && v && p->x == x && p->y == y && v->x == velocity);
    }
    return true;
}

TEST_CASE(binding_schedule_tracks_program_and_owner_lifetimes)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b]));
        REQUIRE(maintain(programs, 2, 0));
        REQUIRE(correct_values());
        size_t bytes = execution_program_memory_usage(programs[0]).binding_schedule_bytes;
        REQUIRE(bytes > 0 && bytes <= 64 * 1024);
        REQUIRE(maintain(programs, 2, 1));
        REQUIRE(execution_program_memory_usage(programs[0]).binding_schedule_bytes == bytes);
        /* The same module name now has reversed binding declarations. A stale
         * order would read the old Velocity.x before its replacement writes. */
        REQUIRE(
            replace(1, "behavior Seed { bind self.Position.x = self.Velocity.x + 1; bind self.Velocity.x = 4; }", backends[b])
        );
        REQUIRE(maintain(programs, 2, 2));
        REQUIRE(values(5, 6, 4));
        clear_slot(1);
        REQUIRE(
            compile(1, "behavior Seed { bind self.Velocity.x = 7; bind self.Position.x = self.Velocity.x + 1; }", backends[b])
        );
        for (int i = 0; i < OWNER_COUNT; i++) {
            REQUIRE(execution_program_attach(programs[1], ecs_entity_to_u64(owners[i])));
        }
        REQUIRE(maintain(programs, 2, 3));
        REQUIRE(values(8, 9, 7));
        uint64_t old_owner = ecs_entity_to_u64(owners[0]);
        execution_program_detach(programs[0], old_owner);
        execution_program_detach(programs[1], old_owner);
        REQUIRE(ecs_world_record_destroy(world, owners[0], 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        owners[0] = ecs_world_create_entity(world);
        REQUIRE(ecs_entity_to_u64(owners[0]) != old_owner);
        REQUIRE(
            ecs_world_record_add(world, owners[0], 501, &(binding_position){ 0 }, 0) == ECS_OK
        );
        REQUIRE(
            ecs_world_record_add(world, owners[0], 502, &(binding_velocity){ 0 }, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_attach(programs[0], ecs_entity_to_u64(owners[0])));
        REQUIRE(execution_program_attach(programs[1], ecs_entity_to_u64(owners[0])));
        REQUIRE(maintain(programs, 2, 4));
        REQUIRE(values(8, 9, 7));
        cleanup();
    }
    return true;
}

TEST_CASE(binding_schedule_preserves_order_and_rejects_cycles_before_writes)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b]));
        REQUIRE(maintain(programs, 2, 0));
        REQUIRE(
            replace(1, "behavior Seed { bind self.Velocity.x = 99; bind self.Position.x = self.Position.y + 1; }", backends[b])
        );
        uint64_t before = ecs_component_version(world, owners[0], 502);
        char error[256];
        REQUIRE(
            execution_program_bindings_all(programs, 2, 1, 0, BUDGET, error, sizeof error) == FLOW_ERR_COMPILE
        );
        REQUIRE(strstr(error, "cycle"));
        REQUIRE(correct_values());
        REQUIRE(ecs_component_version(world, owners[0], 502) == before);
        REQUIRE(
            replace(1, "behavior Seed { bind self.Position.x = self.Velocity.x + 1; bind self.Velocity.x = 4; }", backends[b])
        );
        REQUIRE(maintain(programs, 2, 2));
        REQUIRE(values(5, 6, 4));
        /* Independent conflicting writers preserve caller order, including
         * when a formerly cached first program becomes the second program. */
        REQUIRE(replace(0, "behavior Finish { bind self.Velocity.x = 10; }", backends[b]));
        REQUIRE(replace(1, "behavior Seed { bind self.Velocity.x = 20; }", backends[b]));
        REQUIRE(maintain(programs, 2, 3));
        REQUIRE(values(5, 6, 20));
        execution_program *reversed[] = { programs[1], programs[0] };
        REQUIRE(maintain(reversed, 2, 4));
        REQUIRE(values(5, 6, 10));
        REQUIRE(maintain(programs, 2, 5));
        REQUIRE(values(5, 6, 20));
        cleanup();
    }
    return true;
}

TEST_CASE(binding_schedule_rechecks_query_gates_and_uses_bounded_fallback)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b]));
        clear_slot(1);
        clear_slot(0);
        REQUIRE(
            compile(0, "system Gate query p:Position where p.x > 0 { bind self.Velocity.x = p.x + 1; }", backends[b])
        );
        for (int pass = 0; pass < 3; pass++) {
            double x = pass == 1 ? 0 : pass + 2;
            REQUIRE(
                ecs_component_write_field(world, owners[0], 501, 0, (pin_value){ .type = PIN_F64, .d = x }) ==
                ECS_OK
            );
            int prepared = execution_program_prepare_system(programs[0], pass, 0, BUDGET);
            int maintained = prepared
                               ? prepared
                               : execution_program_bindings_all(programs, 1, pass, 0, BUDGET, NULL, 0);
            int run = prepared ? prepared
                               : execution_program_run_system(programs[0]);
            int finished = prepared ? prepared
                                    : execution_program_finish_system(programs[0]);
            REQUIRE(!prepared && !maintained && !run && !finished);
            const binding_velocity *v = ecs_component_data_const(world, owners[0], 502);
            REQUIRE(v && v->x == (pass == 2 ? 5 : 3));
        }
        clear_slot(0);
        REQUIRE(compile(0, "behavior Constant { bind self.Velocity.x = 42; }", backends[b]));
        for (int i = 0; i < 17; i++) {
            extra_programs[i] = execution_program_new(modules[0], world, NULL);
            REQUIRE(extra_programs[i]);
            REQUIRE(execution_program_attach(extra_programs[i], ecs_entity_to_u64(owners[0])));
        }
        REQUIRE(maintain(extra_programs, 1, 3));
        size_t cached = execution_program_memory_usage(extra_programs[0]).binding_schedule_bytes;
        uint64_t before = ecs_component_version(world, owners[0], 502);
        REQUIRE(maintain(extra_programs, 17, 4));
        REQUIRE(ecs_component_version(world, owners[0], 502) - before == 17);
        REQUIRE(execution_program_memory_usage(extra_programs[0]).binding_schedule_bytes < cached);
        REQUIRE(maintain(extra_programs, 1, 5));
        REQUIRE(execution_program_memory_usage(extra_programs[0]).binding_schedule_bytes == cached);
        ecs_entity last = owners[0];
        for (int i = 0; i < 1024; i++) {
            last = ecs_world_create_entity(world);
            REQUIRE(
                ecs_world_record_add(world, last, 502, &(binding_velocity){ 0 }, 0) == ECS_OK
            );
            REQUIRE(execution_program_attach(extra_programs[0], ecs_entity_to_u64(last)));
        }
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(maintain(extra_programs, 1, 6));
        execution_memory_usage large = execution_program_memory_usage(extra_programs[0]);
        REQUIRE(large.instance_count == 1025 && large.binding_schedule_bytes < cached);
        REQUIRE(
            ((const binding_velocity *)ecs_component_data_const(world, last, 502))
                ->x == 42
        );
        cleanup();
    }
    return true;
}

static uint64_t state_id(int index, const char *name)
{
    for (int i = 0; i < analysis[index]->symbol_count; i++) {
        const script_symbol *symbol = &analysis[index]->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}

TEST_CASE(binding_schedule_keeps_dynamic_target_dependencies)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b]));
        clear_slot(0);
        clear_slot(1);
        REQUIRE(
            compile(0, "behavior Reader { target: entity; offsets: double[] = [1]; choice: int = 0; bind self.Position.y = target.Velocity.x + offsets[choice]; }", backends[b])
        );
        REQUIRE(
            compile(1, "behavior Writer { seed: double = 1; bind self.Velocity.x = seed; }", backends[b])
        );
        execution_instance *reader = execution_program_attach(
            programs[0],
            ecs_entity_to_u64(owners[0])
        );
        execution_instance *
            first = execution_program_attach(programs[1], ecs_entity_to_u64(owners[1]));
        execution_instance *second = execution_program_attach(
            programs[1],
            ecs_entity_to_u64(owners[2])
        );
        REQUIRE(reader && first && second);
        REQUIRE(state_id(0, "target") && state_id(0, "choice") && state_id(1, "seed"));
        REQUIRE(!execution_instance_set_state(
            reader,
            state_id(0, "target"),
            (pin_value){ .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owners[1]) }
        ));
        REQUIRE(!execution_instance_set_state(
            first,
            state_id(1, "seed"),
            (pin_value){ .type = PIN_F64, .d = 4 }
        ));
        REQUIRE(!execution_instance_set_state(
            second,
            state_id(1, "seed"),
            (pin_value){ .type = PIN_F64, .d = 7 }
        ));
        REQUIRE(maintain(programs, 2, 0));
        REQUIRE(
            ((const binding_position *)ecs_component_data_const(world, owners[0], 501))
                ->y == 5
        );
        REQUIRE(!execution_instance_set_state(
            reader,
            state_id(0, "target"),
            (pin_value){ .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owners[2]) }
        ));
        REQUIRE(!execution_instance_set_state(
            second,
            state_id(1, "seed"),
            (pin_value){ .type = PIN_F64, .d = 9 }
        ));
        REQUIRE(maintain(programs, 2, 1));
        REQUIRE(
            ((const binding_position *)ecs_component_data_const(world, owners[0], 501))
                ->y == 10
        );
        size_t cached = execution_program_memory_usage(programs[0]).binding_schedule_bytes;
        REQUIRE(!execution_instance_set_state(
            reader,
            state_id(0, "choice"),
            (pin_value){ .type = PIN_I32, .i = 1 }
        ));
        char error[256];
        REQUIRE(
            execution_program_bindings_all(programs, 2, 2, 0, BUDGET, error, sizeof error) == FLOW_ERR_ARGUMENT
        );
        REQUIRE(execution_program_memory_usage(programs[0]).binding_schedule_bytes < cached);
        REQUIRE(
            ((const binding_position *)ecs_component_data_const(world, owners[0], 501))
                ->y == 10
        );
        REQUIRE(!execution_instance_set_state(
            reader,
            state_id(0, "choice"),
            (pin_value){ .type = PIN_I32, .i = 0 }
        ));
        REQUIRE(!execution_instance_set_state(
            second,
            state_id(1, "seed"),
            (pin_value){ .type = PIN_F64, .d = 11 }
        ));
        REQUIRE(maintain(programs, 2, 3));
        REQUIRE(
            ((const binding_position *)ecs_component_data_const(world, owners[0], 501))
                ->y == 12
        );
        cleanup();
    }
    return true;
}

void test_language_binding_schedule()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(binding_schedule_tracks_program_and_owner_lifetimes);
    RUN_TEST_CASE(binding_schedule_preserves_order_and_rejects_cycles_before_writes);
    RUN_TEST_CASE(binding_schedule_rechecks_query_gates_and_uses_bounded_fallback);
    RUN_TEST_CASE(binding_schedule_keeps_dynamic_target_dependencies);
    pu_clear_setup();
}
