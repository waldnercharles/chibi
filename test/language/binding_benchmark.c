#include "backend.h"
#include "capabilities/catalog_snapshot.h"
#include "ecs/ecs.h"
#include "execution/module.h"
#include "execution/program_internal.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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
static arena managed_catalog, managed_saved, managed_input;
static behavior_run_scope managed_runs;
static bool managed_project_types;

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
    if (managed_runs.active) {
        behavior_run_scope_end(&managed_runs);
    }
    for (int i = 0; i < 17; i++) {
        execution_program_free(extra_programs[i]);
        extra_programs[i] = NULL;
    }
    for (int i = 0; i < 3; i++) {
        clear_slot(i);
    }
    ecs_world_free(world);
    world = NULL;
    arena_free(&managed_catalog);
    arena_free(&managed_saved);
    arena_free(&managed_input);
    if (managed_project_types) {
        type_catalog_set_project(NULL);
        managed_project_types = false;
    }
}

static bool global(void *, uint64_t id, behavior_function_instruction *out)
{
    assert(id == owner_global.id);
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}

static bool compile_catalog(int index, const char *source, behavior_backend backend, const script_catalog *active)
{
    syntax[index] = script_parse(source);
    analysis[index] = script_analyze(syntax[index], active);
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
    execution_module_plan *plan = script_lower(analysis[index], active, &bindings);
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

static bool compile(int index, const char *source, behavior_backend backend)
{
    return compile_catalog(index, source, backend, &catalog);
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
    REQUIRE(
        compile(0, "Finish :: behavior { bind self.Position.y = self.Position.x + 1; }", backend)
    );
    REQUIRE(
        compile(1, "Seed :: behavior { seed: f64 = 1; bind self.Velocity.x = seed; bind self.Position.x = self.Velocity.x + 1; }", backend)
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
            replace(1, "Seed :: behavior { bind self.Position.x = self.Velocity.x + 1; bind self.Velocity.x = 4; }", backends[b])
        );
        REQUIRE(maintain(programs, 2, 2));
        REQUIRE(values(5, 6, 4));
        clear_slot(1);
        REQUIRE(
            compile(1, "Seed :: behavior { bind self.Velocity.x = 7; bind self.Position.x = self.Velocity.x + 1; }", backends[b])
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
            replace(1, "Seed :: behavior { bind self.Velocity.x = 99; bind self.Position.x = self.Position.y + 1; }", backends[b])
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
            replace(1, "Seed :: behavior { bind self.Position.x = self.Velocity.x + 1; bind self.Velocity.x = 4; }", backends[b])
        );
        REQUIRE(maintain(programs, 2, 2));
        REQUIRE(values(5, 6, 4));
        /* Independent conflicting writers preserve caller order, including
         * when a formerly cached first program becomes the second program. */
        REQUIRE(replace(0, "Finish :: behavior { bind self.Velocity.x = 10; }", backends[b]));
        REQUIRE(replace(1, "Seed :: behavior { bind self.Velocity.x = 20; }", backends[b]));
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
            compile(0, "Gate :: system( p:^Position , if p.x > 0) { bind self.Velocity.x = p.x + 1; }", backends[b])
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
        REQUIRE(compile(0, "Constant :: behavior { bind self.Velocity.x = 42; }", backends[b]));
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
            compile(0, "Reader :: behavior { target: entity; offsets: f64[] = [1]; choice: int = 0; bind self.Position.y = target.Velocity.x + offsets[choice]; }", backends[b])
        );
        REQUIRE(
            compile(1, "Writer :: behavior { seed: f64 = 1; bind self.Velocity.x = seed; }", backends[b])
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

/* Two tiny source documents per backend; one owner, three bindings and six
 * maintenance calls. The array and record are dependency evaluator RESULTS,
 * rather than just inputs to an unrelated host decoder. */
enum
{
    MANAGED_COMPONENT = 503,
    MANAGED_TYPE = 103,
    MANAGED_FIELD = 1031,
    MANAGED_MATCH = 1032
};
typedef struct
{
    const char *text;
    bool matches;
} binding_label;
static const type_field label_fields[] = {
    { .name = "text", .type = PIN_STR, .size = sizeof(const char *) },
    { .name = "matches",
      .type = PIN_BOOL,
      .offset = offsetof(binding_label, matches),
      .size = sizeof(bool) }
};
static const type_desc label_type = { .name = "BindingLabel",
                                      .size = sizeof(binding_label),
                                      .fields = label_fields,
                                      .field_count = 2 };
static bool managed_prepare(behavior_backend backend, uint64_t ids[3])
{
    static const char
        declaration[] = "Bound :: library { Row :: struct { label: "
                        "string; count: int; } }";
    static const char source[] =
        "Managed :: behavior { text: string = \"first\"; items: int[] = [1]; "
        "row: Bound.Row = {label: \"first\", count: 1}; "
        "bind self.BindingLabel.text = text; "
        "bind self.BindingLabel.matches = (items == items or true) and "
        "row == row; "
        "bind self.Position.y = 42; }";
    static const type_desc *const reflected[] = { &label_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    type_catalog_set_project(&pack);
    managed_project_types = true;
    world = ecs_world_new();
    const type_desc *position = type_catalog_find("Position");
    REQUIRE(position && position->size == sizeof(binding_position));
    const ecs_component_desc components[] = { { .id = 501,
                                                .key = "Position",
                                                .name = "Position",
                                                .element_size = sizeof(binding_position),
                                                .type = position },
                                              { .id = MANAGED_COMPONENT,
                                                .key = "BindingLabel",
                                                .name = "BindingLabel",
                                                .element_size = sizeof(binding_label),
                                                .type = &label_type } };
    for (int i = 0; i < 2; i++) {
        REQUIRE(ecs_world_register_component(world, &components[i], NULL) == ECS_OK);
    }
    types[0].layout = type_layout_hash(position);
    static const script_catalog empty;
    syntax[1] = script_parse(declaration);
    REQUIRE(!syntax[1]->diagnostic_count);
    analysis[1] = script_analyze(syntax[1], &empty);
    REQUIRE(analysis[1]->valid && analysis[1]->type_count == 1);
    const script_catalog_field fields[] = { { .id = MANAGED_FIELD,
                                              .name = "text",
                                              .type = { .storage = PIN_STR },
                                              .c_string = true,
                                              .writable = true },
                                            { .id = MANAGED_MATCH,
                                              .name = "matches",
                                              .type = { .storage = PIN_BOOL },
                                              .offset = offsetof(binding_label, matches),
                                              .writable = true } };
    const script_catalog_type selected[] = {
        types[0],
        { .id = MANAGED_TYPE,
          .name = "BindingLabel",
          .type = { .storage = PIN_STRUCT, .nominal = MANAGED_TYPE },
          .fields = fields,
          .field_count = 2,
          .size = sizeof(binding_label),
          .layout = type_layout_hash(&label_type),
          .component = true,
          .component_id = MANAGED_COMPONENT },
        analysis[1]->types[0]
    };
    script_catalog source_catalog = catalog, active;
    source_catalog.types = selected;
    source_catalog.type_count = 3;
    REQUIRE(script_catalog_snapshot(&managed_catalog, &source_catalog, &active));
    clear_slot(1);
    behavior_run_scope_reset(&managed_runs);
    behavior_run_scope_begin(&managed_runs);
    bool compiled = compile_catalog(0, source, backend, &active);
    behavior_run_scope_end(&managed_runs);
    REQUIRE(compiled && execution_module_backend(modules[0]) == backend);
    /* The array/record defaults use BYTECODE; the literal string is copied. */
    REQUIRE(managed_runs.counts.bytecode_entries == 2);
    REQUIRE(!managed_runs.counts.sljit_entries && !managed_runs.counts.wasm_entries);
    REQUIRE(!managed_runs.counts.overflow && !managed_runs.counts.inspection_bytecode_fallbacks);
    static const char *const names[] = { "text", "items", "row" };
    for (int i = 0; i < 3; i++) {
        ids[i] = state_id(0, names[i]);
        REQUIRE(ids[i]);
    }
    /* Source/catalog owners retire before any binding evaluator executes. */
    script_analysis_free(analysis[0]);
    script_syntax_free(syntax[0]);
    analysis[0] = NULL;
    syntax[0] = NULL;
    arena_free(&managed_catalog);
    owners[0] = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add(world, owners[0], 501, &(binding_position){ 0 }, 0) == ECS_OK);
    REQUIRE(
        ecs_world_record_add(world, owners[0], MANAGED_COMPONENT, &(binding_label){ .text = "" }, 0) == ECS_OK
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(execution_program_attach(programs[0], ecs_entity_to_u64(owners[0])));
    return true;
}
static bool managed_values(const pin_value *values, const arena *storage, int item, const char *text)
{
    for (int i = 0; i < 2; i++) {
        REQUIRE(values[i].type == PIN_I32 && values[i].container == PIN_CONTAINER_ARRAY);
        const pin_array *array = values[i].ptr;
        REQUIRE(array && arena_contains(storage, array) && array->count == 1);
        const void *items = array->stride ? array->data : (const void *)array->items;
        REQUIRE(array->element_type == PIN_I32 && arena_contains(storage, items));
        pin_value first = pin_array_get(array, 0);
        REQUIRE(first.type == PIN_I32 && first.i == item);
    }
    for (int i = 2; i < 4; i++) {
        const pin_value *row = &values[i];
        REQUIRE(row->type == PIN_STRUCT && row->container == PIN_CONTAINER_SINGLE && row->ptr);
        REQUIRE(arena_contains(storage, row->ptr) && arena_contains(storage, row->record));
        REQUIRE(pin_record_valid(row->record) && row->record->type.field_count == 2);
        REQUIRE(row->type_name == row->record->type.name && !strcmp(row->type_name, "Bound.Row"));
        REQUIRE(
            arena_contains(storage, row->type_name) &&
            arena_contains(storage, row->record->type.fields)
        );
        pin_value label, count;
        REQUIRE(
            !pin_read_field(&managed_input, row->ptr, &row->record->type.fields[0], &label)
        );
        REQUIRE(
            !pin_read_field(&managed_input, row->ptr, &row->record->type.fields[1], &count)
        );
        REQUIRE(label.type == PIN_STR && arena_contains(storage, pin_text(label).bytes));
        REQUIRE(!strcmp(pin_text(label).bytes, text) && count.type == PIN_I32 && count.i == 1);
    }
    return true;
}
static bool managed_maintain(behavior_backend backend, int pass)
{
    behavior_run_scope_reset(&managed_runs);
    behavior_run_scope_begin(&managed_runs);
    bool ok = maintain(programs, 1, pass);
    behavior_run_scope_end(&managed_runs);
    REQUIRE(ok && !managed_runs.counts.overflow);
    const behavior_run_counts *runs = &managed_runs.counts;
    uint64_t selected = backend == BEHAVIOR_BYTECODE ? runs->bytecode_entries
                      : backend == BEHAVIOR_SLJIT    ? runs->sljit_entries
                                                     : runs->wasm_entries;
    REQUIRE(
        !runs->inspection_bytecode_fallbacks &&
        !runs->inspected_sljit_entries && !runs->inspected_wasm_entries
    );
    REQUIRE(
        (backend == BEHAVIOR_BYTECODE || !runs->bytecode_entries) &&
        (backend == BEHAVIOR_SLJIT || !runs->sljit_entries) &&
        (backend == BEHAVIOR_WASM || !runs->wasm_entries)
    );
    /* Three dependency evaluators always run. Cold publication adds three
     * bodies; only the changed state's binding body reruns thereafter. */
    static const uint64_t expected[] = { 6, 3, 4, 4, 4, 3 };
    REQUIRE(selected == expected[pass]);
    return true;
}
TEST_CASE(binding_schedule_snapshots_managed_evaluator_results)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        uint64_t ids[3];
        REQUIRE(managed_prepare(backends[b], ids));
        const execution_module_plan *plan = &modules[0]->plan;
        REQUIRE(plan->binding_count == 3);
        int managed = -1, text_binding = -1, constant = -1;
        for (int i = 0; i < plan->binding_count; i++) {
            if (plan->bindings[i].target == MANAGED_MATCH) {
                managed = i;
            }
            if (plan->bindings[i].target == MANAGED_FIELD) {
                text_binding = i;
            }
            if (plan->bindings[i].component == 501) {
                constant = i;
            }
        }
        REQUIRE(managed >= 0 && text_binding >= 0 && constant >= 0);
        const execution_module_binding *binding = &plan->bindings[managed];
        const execution_module_binding *text = &plan->bindings[text_binding];
        REQUIRE(text->body->result_count == 1 && text->body->result_defaults[0].type == PIN_STR);
        REQUIRE(
            text->dependency_count == 1 &&
            text->dependencies->result_defaults[0].type == PIN_STR
        );
        /* Distinct SYMBOL occurrences are intentionally collected separately.
         * Array reads snapshot separately, so pointer equality is guarded by
         * true. Record equality keeps the result fixed while changed state
         * still requires fresh, independently owned evaluator results. */
        REQUIRE(
            binding->body->result_count == 1 &&
            binding->body->result_defaults[0].type == PIN_BOOL
        );
        REQUIRE(binding->dependency_count == 4 && binding->dependencies->result_count == 4);
        REQUIRE(plan->bindings[constant].dependency_count == 0);
        REQUIRE(
            plan->bindings[constant].dependencies &&
            plan->bindings[constant].dependencies->result_count == 0
        );
        execution_instance *instance = execution_program_instance(
            programs[0],
            ecs_entity_to_u64(owners[0])
        );
        REQUIRE(instance);
        pin_value saved[5];
        int expected_item = 1;
        const char *expected_text = "first", *expected_label = "first";
        for (int pass = 0; pass < 6; pass++) {
            binding_cache *cache = &instance->bindings[managed];
            binding_cache *text_cache = &instance->bindings[text_binding];
            if (pass >= 2 && pass <= 4) {
                int index = pass - 2;
                char producer[] = "second";
                pin_value value = { .type = PIN_STR,
                                    .text = &(const pin_string){
                                        .bytes = producer,
                                        .length = (uint32_t)strlen(producer) } };
                pin_value item = { .type = PIN_I32, .i = 2 };
                pin_array array = { .element_type = PIN_I32,
                                    .count = 1,
                                    .capacity = 1,
                                    .items = &item };
                if (index == 1) {
                    value = (pin_value){ .type = PIN_I32,
                                         .container = PIN_CONTAINER_ARRAY,
                                         .ptr = &array };
                }
                if (index == 2) {
                    REQUIRE(!pin_value_snapshot_to_arena(
                        &managed_input,
                        &value,
                        execution_instance_state(instance, ids[index])
                    ));
                    {
                        const char *error = pin_write_field(
                            &managed_input,
                            value.ptr,
                            &value.record->type.fields[0],
                            (pin_value){ .type = PIN_STR,
                                         .text = &(const pin_string){
                                             .bytes = producer,
                                             .length = (uint32_t)strlen(producer) } }
                        );
                        if (error) {
                            abort();
                        }
                    };
                }
                REQUIRE(execution_instance_set_state(instance, ids[index], value) == FLOW_OK);
                memset(producer, 'x', sizeof producer - 1);
                item.i = -1;
                array.count = 0;
                arena_free(&managed_input);
                /* State replacement and caller-buffer retirement cannot mutate
                 * the previous evaluator results retained by either binding. */
                REQUIRE(
                    managed_values(cache->inputs, &cache->storage, expected_item, expected_label)
                );
                REQUIRE(!strcmp(pin_text(text_cache->inputs[0]).bytes, expected_text));
                REQUIRE(!strcmp(pin_text(text_cache->output).bytes, expected_text));
                if (index == 0) {
                    expected_text = "second";
                }
                if (index == 1) {
                    expected_item = 2;
                }
                if (index == 2) {
                    expected_label = "second";
                }
            }
            REQUIRE(managed_maintain(backends[b], pass));
            REQUIRE(
                cache->valid &&
                managed_values(cache->inputs, &cache->storage, expected_item, expected_label)
            );
            REQUIRE(cache->output.type == PIN_BOOL && cache->output.b);
            REQUIRE(text_cache->valid && text_cache->inputs[0].type == PIN_STR);
            REQUIRE(
                arena_contains(
                    &text_cache->storage,
                    pin_text(text_cache->inputs[0]).bytes
                ) &&
                !strcmp(pin_text(text_cache->inputs[0]).bytes, expected_text)
            );
            REQUIRE(
                text_cache->output.type == PIN_STR &&
                arena_contains(
                    &text_cache->storage,
                    pin_text(text_cache->output).bytes
                )
            );
            REQUIRE(!strcmp(pin_text(text_cache->output).bytes, expected_text));
            const binding_label *label = ecs_component_data_const(world, owners[0], MANAGED_COMPONENT);
            const binding_position *position = ecs_component_data_const(world, owners[0], 501);
            REQUIRE(
                label && label->matches &&
                label->text != pin_text(text_cache->output).bytes &&
                !strcmp(label->text, expected_text)
            );
            REQUIRE(position && position->y == 42);
            REQUIRE(instance->bindings[constant].valid);
            if (!pass) {
                for (int i = 0; i < 4; i++) {
                    REQUIRE(!pin_value_snapshot_to_arena(
                        &managed_saved,
                        &saved[i],
                        &cache->inputs[i]
                    ));
                }
                REQUIRE(!pin_value_snapshot_to_arena(
                    &managed_saved,
                    &saved[4],
                    &text_cache->output
                ));
            }
        }
        /* No producing program, module, source, state, cache or world survives. */
        clear_slot(0);
        ecs_world_free(world);
        world = NULL;
        REQUIRE(managed_values(saved, &managed_saved, 1, "first"));
        REQUIRE(
            arena_contains(&managed_saved, pin_text(saved[4]).bytes) &&
            !strcmp(pin_text(saved[4]).bytes, "first")
        );
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
    RUN_TEST_CASE(binding_schedule_snapshots_managed_evaluator_results);
    pu_clear_setup();
}
