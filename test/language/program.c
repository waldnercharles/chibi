#include "assets/project.h"
#include "assets/project_migrate.h"
#include "ecs/ecs.h"
#include "execution/delay.h"
#include "execution/inspection.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/identity.h"
#include "language/lower.h"
#include "runtime/core_definition.h"
#include "runtime/native_definitions.h"
#include "runtime/state_definition.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* SDK fixture catalog: Position{x,y:double}, Velocity{x:double},
 * Impulse(amount:double,enabled:bool), Tick(dt:double), Delay(Duration:double),
 * self:entity and dt:double. These examples require this embedding catalog.
 * Run: make language-build && make test-language-program (selector --program).
 * Semantic type IDs intentionally differ from authoritative ECS component IDs. */
#define EXAMPLE_SOURCE_CAPACITY 1024
#define MEMORY_TEXT_MIN 512
#define MEMORY_TEXT_STEP 32
#define MEMORY_TEXT_MAX 4096
#define MEMORY_PLATEAU_SLACK 4096

typedef struct
{
    double x, y;
} program_position;
typedef struct
{
    double x;
} program_velocity;
static const type_field position_fields[] = {
    { .name = "x", .type = PIN_F64, .size = sizeof(double), .type_name = "double" },
    { .name = "y",
      .type = PIN_F64,
      .offset = offsetof(program_position, y),
      .size = sizeof(double),
      .type_name = "double" },
};
static const type_field velocity_field = {
    .name = "x",
    .type = PIN_F64,
    .size = sizeof(double),
    .type_name = "double",
};
static const type_desc position_type = {
    .name = "Position",
    .size = sizeof(program_position),
    .fields = position_fields,
    .field_count = 2,
};
static const type_desc velocity_type = {
    .name = "Velocity",
    .size = sizeof(program_velocity),
    .fields = &velocity_field,
    .field_count = 1,
};
static const type_desc *const reflected_types[] = { &position_type, &velocity_type };
type_catalog_pack test_program_types()
{
    return (type_catalog_pack){ .types = reflected_types, .type_count = 2 };
}
static const ecs_component_desc position_component = {
    .id = 501,
    .key = "program.position",
    .name = "Position",
    .element_size = sizeof(program_position),
    .type = &position_type,
};
static const ecs_component_desc velocity_component = {
    .id = 502,
    .key = "program.velocity",
    .name = "Velocity",
    .element_size = sizeof(program_velocity),
    .type = &velocity_type,
};
static const script_catalog_field script_position_fields[] = {
    {
        .id = 1011,
        .name = "x",
        .type = { .storage = PIN_F64 },
        .writable = true,
    },
    { .id = 1012,
      .name = "y",
      .type = { .storage = PIN_F64 },
      .writable = true,
      .offset = offsetof(program_position, y) },
};
static const script_catalog_field script_velocity_field = {
    .id = 1021,
    .name = "x",
    .type = { .storage = PIN_F64 },
    .writable = true,
};
static script_catalog_type script_types[] = {
    { .id = 101,
      .name = "Position",
      .type = { .storage = PIN_STRUCT, .nominal = 101 },
      .fields = script_position_fields,
      .field_count = 2,
      .size = sizeof(program_position),
      .component = true,
      .component_id = 501 },
    { .id = 102,
      .name = "Velocity",
      .type = { .storage = PIN_STRUCT, .nominal = 102 },
      .fields = &script_velocity_field,
      .field_count = 1,
      .size = sizeof(program_velocity),
      .component = true,
      .component_id = 502 },
};
static const script_catalog_parameter tick_parameter = {
    .name = "dt",
    .type = { .storage = PIN_F64 },
};
static const script_catalog_parameter impulse_parameters[] = {
    { .name = "amount", .type = { .storage = PIN_F64 } },
    { .name = "enabled", .type = { .storage = PIN_BOOL } },
};
static const script_catalog_parameter delay_parameter = {
    .name = "Duration",
    .type = { .storage = PIN_F64 },
};
static const char *const completed[] = { "Completed" };
static const script_catalog_parameter library_results[] = {
    { .name = "local_count", .type = { .storage = PIN_F64 } },
    { .name = "position_x", .type = { .storage = PIN_F64 } },
};
static const script_catalog_parameter bump_parameter = {
    .name = "x",
    .type = { .storage = PIN_F64, .flags = SCRIPT_TYPE_REFERENCE },
};
static const script_catalog_parameter failed_result = { .name = "Result",
                                                        .type = { .storage = PIN_F64 } };
static const script_catalog_parameter managed_result = { .name = "Result",
                                                         .type = { .storage = PIN_STR } };
static const script_catalog_capability capabilities[] = {
    { .id = 10,
      .name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &tick_parameter,
      .parameter_count = 1 },
    { .id = 11,
      .name = "Impulse",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = impulse_parameters,
      .parameter_count = 2 },
    { .id = 20,
      .name = "Delay",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = &delay_parameter,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = 40,
      .name = "library_work",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = library_results,
      .result_count = 2,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_READ | SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_SUSPEND },
    { .id = 41,
      .name = "library_bump",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &bump_parameter,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_READ | SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_SUSPEND },
    { .id = 21,
      .name = "timer",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = &delay_parameter,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = 22,
      .name = "make_thing",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .results = &failed_result,
      .result_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = 50,
      .name = "fail_native",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = &failed_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
    { .id = 60,
      .name = "managed_text",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = &managed_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
};
static const script_catalog_global globals[] = {
    { .id = 30, .name = "self", .type = { .storage = PIN_HANDLE, .nominal = 700 } },
    { .id = 31, .name = "dt", .type = { .storage = PIN_F64 } },
};
static const script_catalog catalog = {
    .types = script_types,
    .type_count = 2,
    .capabilities = capabilities,
    .capability_count = sizeof capabilities / sizeof *capabilities,
    .globals = globals,
    .global_count = 2,
    .entity_type = 700,
    .tick_event = 10,
};
static ecs_world *world;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_module_plan *owned_plan;
static execution_program *program;
static execution_module *imported;
static execution_module *prior_module;
static execution_program *other_program;
static native_capabilities *state_native;
static execution_inspector *debugger;

static void cleanup()
{
    execution_program_free(other_program);
    execution_program_free(program);
    execution_inspector_free(debugger);
    debugger = NULL;
    execution_module_free(module);
    execution_module_free(prior_module);
    execution_module_plan_free(owned_plan);
    execution_module_free(imported);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    native_capabilities_free(state_native);
    state_native = NULL;
    program = NULL;
    module = NULL;
    owned_plan = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
    imported = NULL;
    prior_module = NULL;
    other_program = NULL;
}
static void prepare()
{
    cleanup();
    world = ecs_world_new();
    assert(ecs_world_register_component(world, &position_component, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &velocity_component, NULL) == ECS_OK);
    script_types[0].layout = type_layout_hash(&position_type);
    script_types[1].layout = type_layout_hash(&velocity_type);
}
static bool global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id == 30) {
        *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
        return true;
    }
    if (id == 31) {
        *out = (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = PIN_F64,
            .offset = offsetof(behavior_function_context, delta_seconds),
        };
        return true;
    }
    return false;
}
static char failure_message[64];
static const char *fail_native(arena *, const pin_value *, int, pin_value *)
{
    return failure_message;
}
static const node_host_call failing_call = { .value = fail_native };
static int managed_text_calls;
static const char *managed_text(arena *storage, const pin_value *, int count, pin_value *out)
{
    assert(count == 0);
    static char text[MEMORY_TEXT_MAX + 1];
    int index = managed_text_calls++;
    int size = MEMORY_TEXT_MIN + index * MEMORY_TEXT_STEP;
    assert(size <= MEMORY_TEXT_MAX);
    memset(text, 'x', (size_t)size);
    text[0] = (char)('A' + index % 26);
    text[size] = 0;
    const pin_value value = { .type = PIN_STR, .str = text };
    return pin_value_snapshot_to_arena(storage, out, &value)
             ? "managed text snapshot failed"
             : NULL;
}
static const node_host_call managed_call = { .value = managed_text };
static bool host(void *, uint64_t id, script_lower_host *out)
{
    if (id == 60) {
        *out = (script_lower_host){ .call = &managed_call };
        return true;
    }
    if (id == 21) {
        *out = (script_lower_host){ .command = &EXECUTION_Delay };
        return true;
    }
    if (id == 50) {
        *out = (script_lower_host){ .call = &failing_call };
        return true;
    }
    if ((id == 40 || id == 41) && imported) {
        for (int i = 0; i < execution_module_function_count(imported); i++) {
            execution_module_function_info function = execution_module_function_at(imported, i);
            if (function.kind == EXECUTION_FUNCTION_AUTHORED) {
                *out = (script_lower_host){ .function = function.body,
                                            .module = imported };
                return true;
            }
        }
        return false;
    }
    if (id != 20) {
        return false;
    }
    *out = (script_lower_host){ .intrinsic = BF_DELAY };
    return true;
}
static bool compile_catalog(const char *source, behavior_backend backend, const script_catalog *active)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, active);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(stderr, "program semantic: %s\n", analysis->diagnostics[i].message);
        }
        return false;
    }
    const script_lower_bindings bindings = { .global = global, .host = host };
    execution_module_plan *plan = script_lower(analysis, active, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(
                stderr,
                "program lower %d:%d: %s\n",
                plan->diagnostics[i].range.line,
                plan->diagnostics[i].range.column,
                plan->diagnostics[i].message
            );
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    int status = module ? FLOW_OK : FLOW_ERR_COMPILE;
    if (status != FLOW_OK) {
        fprintf(stderr, "program publish: %s\n", error);
    }
    if (status != FLOW_OK) {
        return false;
    }
    return true;
}
static bool compile_module(const char *source, behavior_backend backend)
{
    return compile_catalog(source, backend, &catalog);
}
static bool compile_state(const char *source, behavior_backend backend)
{
    const native_capability_pack base = {
        .catalog = &catalog,
        .layouts = reflected_types,
        .layout_count = 2,
    };
    char error[256];
    const native_definition_pack packs[] = { NATIVE_STATE_DEFINITION_PACK,
                                             CORE_DEFINITION_PACK };
    state_native = native_capabilities_from_definition_packs(&base, 1, packs, 2, error, sizeof error);
    if (!state_native) {
        fprintf(stderr, "state catalog: %s\n", error);
        return false;
    }
    if (!compile_catalog(source, backend, native_capabilities_catalog(state_native))) {
        return false;
    }
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}
static bool compile(const char *source, behavior_backend backend)
{
    if (!compile_module(source, backend)) {
        return false;
    }
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}
static bool read_example(const char *path, char source[EXAMPLE_SOURCE_CAPACITY])
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "program example cannot open: %s\n", path);
        return false;
    }
    size_t count = fread(source, 1, EXAMPLE_SOURCE_CAPACITY, file);
    bool valid = count < EXAMPLE_SOURCE_CAPACITY && !ferror(file);
    if (fclose(file)) {
        valid = false;
    }
    if (!valid) {
        fprintf(stderr, "program example read failed or exceeded source limit: %s\n", path);
        return false;
    }
    source[count] = '\0';
    return true;
}
static uint64_t state_id(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if ((analysis->symbols[i].kind == SCRIPT_STATE ||
             analysis->symbols[i].kind == SCRIPT_SHARED_STATE) &&
            !strcmp(analysis->symbols[i].name, name)) {
            return analysis->symbols[i].id;
        }
    }
    return 0;
}
static ecs_entity positioned(double x, bool moving)
{
    ecs_entity entity = ecs_world_create_entity(world);
    program_position position = { .x = x };
    assert(ecs_world_record_add(world, entity, 501, &position, 0) == ECS_OK);
    if (moving) {
        program_velocity velocity = { .x = 4 };
        assert(ecs_world_record_add(world, entity, 502, &velocity, 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

TEST_CASE(program_typed_event_changes_state_on_vm_and_sljit)
{
    char source[EXAMPLE_SOURCE_CAPACITY];
    REQUIRE(read_example(CHIBI_LANGUAGE_EXAMPLE_DIR "/accumulator.chibi", source));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        prepare();
        REQUIRE(compile(source, backends[i]));
        ecs_entity owner = ecs_world_create_entity(world);
        uint64_t handle = ecs_entity_to_u64(owner);
        execution_instance *instance = execution_program_attach(program, handle);
        REQUIRE(instance != NULL && state_id("total") != 0);
        pin_value payload[] = { { .type = PIN_F64, .d = 7 },
                                { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_program_emit(program, 11, handle, payload, 2) == FLOW_OK);
        payload[0].d = 99;
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        const pin_value *total = execution_instance_state(instance, state_id("total"));
        const pin_value *seen = execution_instance_state(instance, state_id("seen"));
        REQUIRE(total && total->type == PIN_F64 && total->d == 7);
        REQUIRE(seen && seen->type == PIN_BOOL && seen->b);
        payload[0].d = 3;
        payload[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, payload, 2, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_state(instance, state_id("total"))->d == 7);
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}

TEST_CASE(program_system_membership_exit_cancels_pending_work)
{
    char source[EXAMPLE_SOURCE_CAPACITY];
    REQUIRE(read_example(CHIBI_LANGUAGE_EXAMPLE_DIR "/motion.chibi", source));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        prepare();
        ecs_entity moving = positioned(1, true), stationary = positioned(10, false);
        REQUIRE(compile(source, backends[i]));
        REQUIRE(execution_program_step(program, 0, 0.5, 1000) == FLOW_OK);
        const program_position *position = ecs_component_data_const(world, moving, 501);
        REQUIRE(position && position->x == 3 && position->y == 0);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, stationary, 501))
                ->x == 10
        );
        execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(moving));
        REQUIRE(instance && execution_instance_pending(instance) == 1);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(stationary)));
        REQUIRE(ecs_world_record_remove(world, moving, 502, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 20, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(moving)));
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, moving, 501))
                ->y == 0
        );
    }
    return true;
}

TEST_CASE(program_component_binding_runs_and_source_cycle_is_rejected)
{
    char source[EXAMPLE_SOURCE_CAPACITY];
    REQUIRE(read_example(CHIBI_LANGUAGE_EXAMPLE_DIR "/binding.chibi", source));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        prepare();
        ecs_entity entity = positioned(6, false);
        REQUIRE(compile(source, backends[i]));
        REQUIRE(execution_program_attach(program, ecs_entity_to_u64(entity)) != NULL);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        const program_position *position = ecs_component_data_const(world, entity, 501);
        REQUIRE(position && position->x == 6 && position->y == 12);
        REQUIRE(
            ecs_component_write_field(world, entity, 501, 0, (pin_value){ .type = PIN_F64, .d = 9 }) == ECS_OK
        );
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, entity, 501))
                ->y == 18
        );
    }
    prepare();
    syntax = script_parse("behavior Cycle { bind self.Position.x = self.Position.y; bind self.Position.y = self.Position.x; }");
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(!analysis->valid && analysis->diagnostic_count > 0);
    REQUIRE(module == NULL && program == NULL);
    return true;
}
static bool number_state(execution_instance *instance, const char *name, double expected)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    return value && value->type == PIN_F64 && value->d == expected;
}
TEST_CASE(program_imported_library_preserves_owner_namespace_and_reader_after_replace)
{
    const char *library_source = "library Store { count: double = 10;"
                                 "function Work() -> (local_count: double, "
                                 "position_x: double) {"
                                 "count += 1; await Delay(1); return(count, "
                                 "self.Position.x); } }";
    const char
        *owner_source = "behavior Owner { count: double = 0; local_count: "
                        "double = 0; position_x: double = 0;"
                        "on Impulse(amount, enabled) { count += amount; values "
                        ":= await library_work();"
                        "local_count = values.local_count; position_x = "
                        "values.position_x; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_module(library_source, backends[b]));
        imported = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile(owner_source, backends[b]));
        REQUIRE(execution_module_import_count(module) == 1);
        ecs_entity owners[] = { positioned(6, false), positioned(9, false) };
        execution_instance *instances[] = {
            execution_program_attach(program, ecs_entity_to_u64(owners[0])),
            execution_program_attach(program, ecs_entity_to_u64(owners[1])),
        };
        REQUIRE(instances[0] && instances[1]);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        for (int i = 0; i < 2; i++) {
            REQUIRE(execution_instance_dispatch(instances[i], 11, args, 2, i, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instances[i]) == 1);
            REQUIRE(number_state(instances[i], "count", 1));
            REQUIRE(number_state(instances[i], "local_count", 0));
            REQUIRE(execution_program_update(program, i + 1, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instances[i]) == 0);
            REQUIRE(number_state(instances[i], "local_count", 11));
            REQUIRE(number_state(instances[i], "position_x", i ? 9 : 6));
        }
        prior_module = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile_module(owner_source, backends[b]));
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        execution_module_free(prior_module);
        prior_module = NULL;
        instances[0] = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        instances[1] = execution_program_instance(program, ecs_entity_to_u64(owners[1]));
        REQUIRE(instances[0] && instances[1]);
        REQUIRE(
            ecs_component_write_field(world, owners[0], 501, 0, (pin_value){ .type = PIN_F64, .d = 13 }) ==
            ECS_OK
        );
        REQUIRE(execution_instance_dispatch(instances[0], 11, args, 2, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instances[0], "count", 2));
        REQUIRE(number_state(instances[0], "local_count", 12));
        REQUIRE(number_state(instances[0], "position_x", 13));
        REQUIRE(number_state(instances[1], "count", 1));
        REQUIRE(execution_instance_dispatch(instances[0], 11, args, 2, 3, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instances[0]) == 1);
        execution_program_detach(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owners[0])) == NULL);
        REQUIRE(execution_program_update(program, 4, 0, 1000) == FLOW_OK);
        instances[0] = execution_program_attach(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(instances[0] != NULL && number_state(instances[0], "count", 0));
        REQUIRE(execution_instance_dispatch(instances[0], 11, args, 2, 4, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instances[0], "local_count", 11));
        REQUIRE(number_state(instances[0], "position_x", 13));
        other_program = execution_program_new(module, world, NULL);
        REQUIRE(other_program != NULL);
        execution_instance *other = execution_program_attach(
            other_program,
            ecs_entity_to_u64(owners[0])
        );
        REQUIRE(other != NULL);
        REQUIRE(execution_instance_dispatch(other, 11, args, 2, 10, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(other_program, 11, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(other, "count", 1));
        REQUIRE(number_state(other, "local_count", 11));
        REQUIRE(number_state(other, "position_x", 13));
        REQUIRE(number_state(instances[1], "local_count", 11));
    }
    return true;
}
TEST_CASE(program_system_shared_state_survives_membership_and_is_registration_owned)
{
    const char *source = "system Shared(position: Position) { shared total: "
                         "double = 0; state local_count: double = 0;"
                         "total += 1; local_count += 1; position.y = total; }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owners[] = { positioned(1, false), positioned(2, false) };
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        execution_instance *
            first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        execution_instance *
            second = execution_program_instance(program, ecs_entity_to_u64(owners[1]));
        REQUIRE(first && second);
        REQUIRE(number_state(first, "total", 2) && number_state(second, "total", 2));
        REQUIRE(number_state(first, "local_count", 1) && number_state(second, "local_count", 1));
        REQUIRE(ecs_world_record_remove(world, owners[0], 501, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owners[0])) == NULL);
        REQUIRE(number_state(second, "total", 3) && number_state(second, "local_count", 2));
        REQUIRE(
            ((const program_position *)ecs_component_data_const(world, owners[1], 501))
                ->y == 3
        );
        program_position row = { .x = 1 };
        REQUIRE(ecs_world_record_add(world, owners[0], 501, &row, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(
            first && number_state(first, "total", 5) &&
            number_state(first, "local_count", 1)
        );
        REQUIRE(number_state(second, "local_count", 3));
        other_program = execution_program_new(module, world, NULL);
        REQUIRE(other_program != NULL);
        REQUIRE(execution_program_step(other_program, 3, 0, 1000) == FLOW_OK);
        execution_instance *other = execution_program_instance(
            other_program,
            ecs_entity_to_u64(owners[0])
        );
        REQUIRE(
            other && number_state(other, "total", 2) &&
            number_state(other, "local_count", 1)
        );
        REQUIRE(number_state(first, "total", 5));
    }
    return true;
}
static bool string_state(execution_instance *instance, const char *name, const char *expected)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    return value && value->type == PIN_STR && !strcmp(value->str, expected);
}
TEST_CASE(program_managed_state_survives_overwrite_and_multiple_resumes)
{
    const char
        *source = "behavior Managed { count: double = 0; text: string = "
                  "\"initial\"; observed: string = \"\";"
                  "on Impulse(amount, enabled) { count += amount; saved := "
                  "text;"
                  "text = \"changed\"; await Delay(1); observed = saved; saved "
                  "= text;"
                  "text = \"final\"; await Delay(1); observed = saved; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        for (int round = 0; round < 2; round++) {
            double now = round * 3;
            char initial[] = "before";
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    state_id("text"),
                    (pin_value){ .type = PIN_STR, .str = initial }
                ) == FLOW_OK
            );
            initial[0] = 'X';
            REQUIRE(string_state(instance, "text", "before"));
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, now, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(string_state(instance, "text", "changed"));
            REQUIRE(execution_instance_state(instance, state_id("count"))->d == round + 1);
            char replacement[] = "between";
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    state_id("text"),
                    (pin_value){ .type = PIN_STR, .str = replacement }
                ) == FLOW_OK
            );
            replacement[0] = 'X';
            REQUIRE(execution_program_update(program, now + 1, 0, 1000) == FLOW_OK);
            REQUIRE(string_state(instance, "observed", "before"));
            REQUIRE(string_state(instance, "text", "final"));
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(execution_instance_state(instance, state_id("count"))->d == round + 1);
            REQUIRE(execution_program_update(program, now + 2, 0, 1000) == FLOW_OK);
            REQUIRE(string_state(instance, "observed", "between"));
            REQUIRE(execution_instance_pending(instance) == 0);
            REQUIRE(
                execution_instance_set_state(
                    instance,
                    state_id("text"),
                    (pin_value){ .type = PIN_STR, .str = "after" }
                ) == FLOW_OK
            );
            REQUIRE(string_state(instance, "observed", "between"));
        }
        execution_program_detach(program, ecs_entity_to_u64(owner));
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owner)) == NULL);
    }
    return true;
}
TEST_CASE(program_result_tuple_copies_survive_mutation_loop_and_resume)
{
    const char *
        source = "behavior TupleCopies { first: double = 0; second: double = 0;"
                 "before_text: string = \"\"; after_text: string = \"\";"
                 "function Pair(n: double, t: string) -> (total: double, "
                 "message: string) {"
                 "return(n, t); }"
                 "on Impulse(amount, enabled) { i: double = 0; while i < 2 {"
                 "current := Pair(i, \"before\"); saved := current;"
                 "current.total = 99; current.message = \"mutated\";"
                 "current = Pair(100, \"replacement\"); await Delay(1);"
                 "first += saved.total; before_text = saved.message;"
                 "saved = current; await Delay(1);"
                 "second += saved.total; after_text = saved.message; i += 1; } "
                 "} }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int round = 0; round < 2; round++) {
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(execution_program_update(program, round * 2 + 1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "first", round));
            REQUIRE(string_state(instance, "before_text", "before"));
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(execution_program_update(program, round * 2 + 2, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "second", (round + 1) * 100));
            REQUIRE(string_state(instance, "after_text", "replacement"));
        }
        REQUIRE(execution_instance_pending(instance) == 0);
        execution_program_detach(program, ecs_entity_to_u64(owner));
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owner)) == NULL);
    }
    return true;
}
static bool memory_text_state(execution_instance *instance, const char *name, int produced)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    int size = MEMORY_TEXT_MIN + produced * MEMORY_TEXT_STEP;
    return value && value->type == PIN_STR && value->str[0] == 'A' + produced % 26 &&
           strlen(value->str) == (size_t)size && value->str[size - 1] == 'x';
}
TEST_CASE(program_managed_payload_memory_is_bounded_across_root_and_outlined_resumes)
{
    const char *body = "saved := managed_text(); i: double = 0; while i < 64 {"
                       "current := Pair(i, managed_text()); copied := current;"
                       "current.message = \"discarded\"; text = "
                       "copied.message; observed = saved;"
                       "if i == 3 { saved = text; } await Delay(1); count += "
                       "1; i += 1; }";
    const char *starts[] = { "on Impulse(amount, enabled) {", "function Run() -> () {" };
    const char *ends[] = { "}", "} on Impulse(amount, enabled) { await Run(); }" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int outlined = 0; outlined < 2; outlined++) {
        char source[2048];
        int size = snprintf(
            source,
            sizeof source,
            "behavior Retained { count: double = 0; text: string = \"\";"
            "observed: string = \"\";"
            "function Pair(n: double, t: string) -> (total: double, message: "
            "string) {"
            "return(n, t); } %s %s %s }",
            starts[outlined],
            body,
            ends[outlined]
        );
        REQUIRE(size > 0 && (size_t)size < sizeof source);
        for (int b = 0; b < 2; b++) {
            prepare();
            managed_text_calls = 0;
            REQUIRE(compile(source, backends[b]));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance != NULL);
            const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                       { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(memory_text_state(instance, "observed", 0));
            execution_memory_usage warm = { 0 };
            for (int step = 1; step <= 64; step++) {
                REQUIRE(execution_program_update(program, step, 0, 1000) == FLOW_OK);
                REQUIRE(number_state(instance, "count", step));
                REQUIRE(memory_text_state(instance, "observed", step <= 3 ? 0 : 4));
                REQUIRE(execution_instance_pending(instance) == (step < 64));
                if (step == 8) {
                    warm = execution_program_memory_usage(program);
                    REQUIRE(warm.payload_bytes >= MEMORY_TEXT_MIN);
                    REQUIRE(warm.payload_capacity >= warm.payload_bytes);
                    REQUIRE(warm.payload_capacity <= warm.payload_bytes * 2 + MEMORY_PLATEAU_SLACK);
                }
                if (step == 40) {
                    execution_memory_usage steady = execution_program_memory_usage(program);
                    /* Payloads grow by 32 bytes per native call. Live bytes can
                     * grow with them; capacity must stay within one live wave
                     * plus 4KiB, rather than retaining every smaller dead slot. */
                    REQUIRE(steady.payload_bytes >= warm.payload_bytes);
                    REQUIRE(steady.payload_bytes <= warm.payload_bytes * 3 + MEMORY_PLATEAU_SLACK);
                    REQUIRE(steady.payload_capacity <= steady.payload_bytes * 2 + MEMORY_PLATEAU_SLACK);
                }
            }
            REQUIRE(managed_text_calls == 65);
            REQUIRE(memory_text_state(instance, "text", 64));
            execution_memory_usage complete = execution_program_memory_usage(program);
            REQUIRE(complete.payload_bytes == 0 && complete.payload_capacity == 0);
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 100, 0, 1000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 1);
            REQUIRE(execution_program_memory_usage(program).payload_bytes > 0);
            execution_program_detach(program, ecs_entity_to_u64(owner));
            execution_memory_usage detached = execution_program_memory_usage(program);
            REQUIRE(detached.payload_bytes == 0 && detached.payload_capacity == 0);
        }
    }
    return true;
}
TEST_CASE(program_lazy_tuple_initializes_once_and_preserves_group_writes)
{
    const char *consumers[] = {
        "on Impulse(amount, enabled) { first = pair.total; await Delay(1);"
        "pair.total = 77; second = pair.total; text = pair.message; }",
        "on Impulse(amount, enabled) { pair.total = 77; first = pair.total;"
        "second = pair.total; text = pair.message; }",
        "on Impulse(amount, enabled) { pair = Pair(); first = pair.total;"
        "second = pair.total; text = pair.message; }",
        "action pending = timer(1);"
        "on Impulse(amount, enabled) { first = pair.total; pending(); }"
        "after pending.Completed { second = pair.total; text = pair.message; }",
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int c = 0; c < 4; c++) {
        char source[EXAMPLE_SOURCE_CAPACITY];
        int length = snprintf(
            source,
            sizeof source,
            "behavior LazyTuple { calls: double = 0; first: double = 0;"
            "second: double = 0; text: string = \"\";"
            "function Pair() -> (total: double, message: string) {"
            "calls += 1; return(calls, \"captured\"); }"
            "local pair = Pair(); %s }",
            consumers[c]
        );
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        for (int b = 0; b < 2; b++) {
            prepare();
            REQUIRE(compile(source, backends[b]));
            ecs_entity owner = ecs_world_create_entity(world);
            uint64_t entity = ecs_entity_to_u64(owner);
            execution_instance *instance = execution_program_attach(program, entity);
            REQUIRE(instance);
            const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                       { .type = PIN_BOOL, .b = true } };
            for (int round = 0; round < 2; round++) {
                double now = round * 2;
                REQUIRE(
                    execution_instance_dispatch(instance, 11, args, 2, now, 0, 1000) == FLOW_OK
                );
                REQUIRE(number_state(instance, "calls", round + 1));
                REQUIRE(number_state(instance, "first", c == 1 ? 77 : round + 1));
                REQUIRE(execution_program_update(program, now + 1, 0, 1000) == FLOW_OK);
                REQUIRE(number_state(instance, "calls", round + 1));
                REQUIRE(number_state(instance, "second", c < 2 ? 77 : round + 1));
                REQUIRE(string_state(instance, "text", "captured"));
                REQUIRE(execution_instance_pending(instance) == 0);
            }
            execution_program_detach(program, entity);
            execution_memory_usage usage = execution_program_memory_usage(program);
            REQUIRE(usage.payload_bytes == 0 && usage.payload_capacity == 0);
        }
    }
    return true;
}
TEST_CASE(program_sibling_timer_actions_complete_independently_and_cancel_on_detach)
{
    const char *source = "behavior Timers { early_count: double = 0; "
                         "late_count: double = 0;"
                         "action early = timer(1); action late = timer(3);"
                         "on Impulse(amount, enabled) { early(); late(); }"
                         "after early.Completed { early_count += 1; }"
                         "after late.Completed { late_count += 1; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 0) &&
            number_state(instance, "late_count", 0)
        );
        REQUIRE(execution_program_update(program, 0.5, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 0) &&
            number_state(instance, "late_count", 0)
        );
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 1) &&
            number_state(instance, "late_count", 0)
        );
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 1) &&
            number_state(instance, "late_count", 0)
        );
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 1) &&
            number_state(instance, "late_count", 1)
        );
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 4, 0, 1000) == FLOW_OK);
        execution_program_detach(program, ecs_entity_to_u64(owner));
        REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owner)) == NULL);
        instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        REQUIRE(execution_program_update(program, 10, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "early_count", 0) &&
            number_state(instance, "late_count", 0)
        );
    }
    return true;
}
TEST_CASE(program_failure_snapshot_owns_outlined_source_diagnostics)
{
    const char
        *source = "behavior Failure {\n"
                  "  function Outline() -> (Result: double) {\n"
                  "    return fail_native();\n"
                  "  }\n"
                  "  on Impulse(amount, enabled) { output := Outline(); }\n"
                  "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_program_set_name(program, "owned diagnostic fixture");
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        strcpy(failure_message, "first native failure");
        int status = execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000);
        REQUIRE(status < 0);
        const execution_program_failure *failure = execution_program_error(program);
        REQUIRE(failure != NULL);
        execution_program_failure copy = *failure;
        REQUIRE(copy.status == status && copy.kind == BEHAVIOR_FUNCTION_HOST_DATA);
        REQUIRE(copy.module_id == execution_module_id(module));
        REQUIRE(copy.owner == ecs_entity_to_u64(owner) && copy.node_id != 0);
        uint64_t outlined = 0;
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            if (f.kind == EXECUTION_FUNCTION_AUTHORED) {
                outlined = f.id;
            }
        }
        REQUIRE(outlined != 0 && copy.function_id == outlined);
        int start = (int)(strstr(source, "fail_native()") - source);
        REQUIRE(copy.start == start && copy.end == start + (int)strlen("fail_native()"));
        REQUIRE(copy.line == 3 && copy.column == 12);
        REQUIRE(
            !strcmp(copy.message, "first native failure") &&
            !strcmp(copy.asset_name, "Failure")
        );
        strcpy(failure_message, "second native failure");
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) < 0);
        REQUIRE(!strcmp(execution_program_error(program)->message, "first native failure"));
        execution_program_clear_error(program);
        REQUIRE(execution_program_error(program) == NULL);
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 2, 0, 1000) < 0);
        REQUIRE(!strcmp(execution_program_error(program)->message, "second native failure"));
        execution_program_free(program);
        program = NULL;
        REQUIRE(
            !strcmp(copy.message, "first native failure") && copy.line == 3 &&
            copy.start == start
        );
    }
    return true;
}
TEST_CASE(program_module_finish_preserves_failure_and_consumes_success)
{
    char initial[] = "producer text";
    owned_plan = execution_module_plan_new();
    owned_plan->id = 123;
    owned_plan->valid = true;
    apush(
        owned_plan->states,
        ((execution_module_state){
            .id = 456,
            .initial = { .type = PIN_STR, .str = initial },
        })
    );
    execution_module_plan *before = owned_plan;
    char error[192];
    module = execution_module_finish(&owned_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!module);
    REQUIRE(owned_plan == before && error[0]);
    owned_plan->state_count = 1;
    module = execution_module_finish(&owned_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module && !owned_plan);
    REQUIRE(execution_module_id(module) == 123);
    initial[0] = 'X';
    program = execution_program_new(module, NULL, NULL);
    REQUIRE(program);
    execution_instance *instance = execution_program_attach(program, 1);
    REQUIRE(instance);
    const pin_value *value = execution_instance_state(instance, 456);
    REQUIRE(value && value->type == PIN_STR && !strcmp(value->str, "producer text"));
    return true;
}
TEST_CASE(program_integer_and_nominal_enum_literals_preserve_exact_bits)
{
    const char *
        source = "behavior Exact { low:int64=0; high:int64=0; "
                 "unsigned_max:uint64=0; "
                 "precise:uint64=0; mode:Mode=1; on Tick { "
                 "low=-9223372036854775808; high=9223372036854775807; "
                 "unsigned_max=18446744073709551615; precise=9007199254740993; "
                 "mode=2147483647; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    const char *const names[] = { "low", "high", "unsigned_max", "precise", "mode" };
    script_catalog_type types[3];
    script_catalog exact_catalog = catalog;
    exact_catalog.types = types;
    exact_catalog.type_count = 3;
    for (int b = 0; b < 2; b++) {
        prepare();
        memcpy(types, script_types, sizeof script_types);
        types[2] = (script_catalog_type){ .id = 103,
                                          .name = "Mode",
                                          .size = sizeof(int32_t),
                                          .type = { .storage = PIN_ENUM,
                                                    .nominal = 103 } };
        syntax = script_parse(source);
        analysis = script_analyze(syntax, &exact_catalog);
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "exact literal semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
        REQUIRE(analysis->valid && !syntax->diagnostic_count);
        uint64_t ids[5];
        for (int i = 0; i < 5; i++) {
            ids[i] = state_id(names[i]);
            REQUIRE(ids[i] != 0);
        }
        const script_lower_bindings bindings = { .global = global, .host = host };
        owned_plan = script_lower(analysis, &exact_catalog, &bindings);
        REQUIRE(owned_plan && owned_plan->valid);
        char error[256];
        module = execution_module_finish(&owned_plan, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "exact literal publication: %s\n", error);
        }
        REQUIRE(module && !owned_plan);
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        const pin_value *mode = execution_instance_state(instance, ids[4]);
        REQUIRE(
            mode && mode->type == PIN_ENUM && mode->e == 1 && mode->type_name &&
            !strcmp(mode->type_name, "Mode")
        );
        const pin_value delta = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &delta, 1, 0, 0, 1000) == FLOW_OK);
        const pin_value *low = execution_instance_state(instance, ids[0]),
                        *high = execution_instance_state(instance, ids[1]),
                        *unsigned_max = execution_instance_state(instance, ids[2]),
                        *precise = execution_instance_state(instance, ids[3]);
        mode = execution_instance_state(instance, ids[4]);
        REQUIRE(low && low->type == PIN_I64 && low->i64 == INT64_MIN);
        REQUIRE(high && high->type == PIN_I64 && high->i64 == INT64_MAX);
        REQUIRE(unsigned_max && unsigned_max->type == PIN_U64 && unsigned_max->u64 == UINT64_MAX);
        REQUIRE(precise && precise->type == PIN_U64 && precise->u64 == UINT64_C(9007199254740993));
        REQUIRE(
            mode && mode->type == PIN_ENUM && mode->e == INT32_MAX &&
            mode->type_name && !strcmp(mode->type_name, "Mode")
        );
    }
    const char *const invalid[] = {
        "behavior Bad { amount:int64=9223372036854775808; }",
        "behavior Bad { amount:int64=-9223372036854775809; }",
        "behavior Bad { amount:uint64=18446744073709551616; }",
        "behavior Bad { amount:uint64=-1; }",
        "behavior Bad { amount:Mode=2147483648; }",
    };
    for (int i = 0; i < 5; i++) {
        prepare();
        syntax = script_parse(invalid[i]);
        analysis = script_analyze(syntax, &exact_catalog);
        REQUIRE(!syntax->diagnostic_count);
        REQUIRE(!analysis->valid && analysis->diagnostic_count > 0);
        REQUIRE(!module && !program);
    }
    return true;
}

TEST_CASE(program_concrete_assignments_preserve_name_byte_and_handle_storage)
{
    const char
        *source = "behavior Copies { source_byte:byte=255; copied_byte:byte=0; "
                  "source_name:name=\"token\"; copied_name:name=\"\"; "
                  "source_handle:Entity=0; copied_handle:Entity=0; "
                  "source_big:uint64=9007199254740993; copied_big:uint64=0; "
                  "count:int=0; "
                  "on Tick { copied_byte=source_byte; copied_name=source_name; "
                  "copied_handle=source_handle; copied_big=source_big; "
                  "count+=1; "
                  "count+=1; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    const char *names[] = { "copied_byte", "copied_name", "copied_handle", "copied_big", "count" };
    for (int b = 0; b < 2; b++) {
        prepare();
        script_catalog_type types[3];
        memcpy(types, script_types, sizeof script_types);
        types[2] = (script_catalog_type){ .id = 700,
                                          .name = "ecs.entity",
                                          .source_name = "Entity",
                                          .size = sizeof(uint64_t),
                                          .type = { .storage = PIN_HANDLE,
                                                    .nominal = 700 } };
        script_catalog concrete = catalog;
        concrete.types = types;
        concrete.type_count = 3;
        syntax = script_parse(source);
        analysis = script_analyze(syntax, &concrete);
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "concrete copy semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
        REQUIRE(analysis->valid && !syntax->diagnostic_count);
        uint64_t ids[5];
        uint64_t source_handle = state_id("source_handle");
        REQUIRE(source_handle);
        for (int i = 0; i < 5; i++) {
            ids[i] = state_id(names[i]);
            REQUIRE(ids[i]);
        }
        const script_lower_bindings bindings = { .global = global, .host = host };
        owned_plan = script_lower(analysis, &concrete, &bindings);
        REQUIRE(owned_plan && owned_plan->valid);
        char error[192];
        module = execution_module_finish(&owned_plan, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "concrete copy publication: %s\n", error);
        }
        REQUIRE(module && !owned_plan);
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        uint64_t handle = ecs_entity_to_u64(owner);
        REQUIRE(handle != 0);
        execution_instance *instance = execution_program_attach(program, handle);
        REQUIRE(instance);
        REQUIRE(
            execution_instance_set_state(
                instance,
                source_handle,
                (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = UINT64_MAX }
            ) == FLOW_OK
        );
        const pin_value dt = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_OK);
        const pin_value *byte = execution_instance_state(instance, ids[0]),
                        *name = execution_instance_state(instance, ids[1]),
                        *entity = execution_instance_state(instance, ids[2]),
                        *big = execution_instance_state(instance, ids[3]);
        REQUIRE(byte && byte->type == PIN_U8 && byte->u8 == UINT8_MAX);
        REQUIRE(name && name->type == PIN_NAME && name->str && !strcmp(name->str, "token"));
        REQUIRE(
            entity && entity->type == PIN_HANDLE && entity->u64 == UINT64_MAX &&
            entity->type_name && !strcmp(entity->type_name, "ecs.entity")
        );
        REQUIRE(big && big->type == PIN_U64 && big->u64 == UINT64_C(9007199254740993));
        const pin_value *count = execution_instance_state(instance, ids[4]);
        REQUIRE(count && count->type == PIN_I32 && count->i == 2);
    }
    return true;
}

static const char *native_float(arena *, const pin_value *, int count, pin_value *out)
{
    assert(count == 0);
    *out = (pin_value){ .type = PIN_F32, .f = 3.25f };
    return NULL;
}
static bool float_host(void *user, uint64_t id, script_lower_host *out)
{
    static const node_host_call call = { .value = native_float };
    if (id != 70) {
        return host(user, id, out);
    }
    *out = (script_lower_host){ .call = &call };
    return true;
}
TEST_CASE(program_source_float_width_conversion_preserves_native_input)
{
    const char
        *source = "behavior Widths { widened:double=0; narrowed:float=0; "
                  "on Tick { widened=cast<double>(NativeFloat()); "
                  "narrowed=cast<float>(widened+0.25); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    static const script_catalog_parameter result = { .name = "amount",
                                                     .type = { .storage = PIN_F32 } };
    enum
    {
        COUNT = sizeof capabilities / sizeof *capabilities
    };
    script_catalog_capability caps[COUNT + 1];
    memcpy(caps, capabilities, sizeof capabilities);
    caps[COUNT] = (script_catalog_capability){ .id = 70,
                                               .name = "NativeFloat",
                                               .kind = SCRIPT_CAPABILITY_FUNCTION,
                                               .results = &result,
                                               .result_count = 1,
                                               .completion_outcome = -1,
                                               .effects = SCRIPT_EFFECT_HOST };
    script_catalog floats = catalog;
    floats.capabilities = caps;
    floats.capability_count = COUNT + 1;
    for (int b = 0; b < 2; b++) {
        prepare();
        syntax = script_parse(source);
        analysis = script_analyze(syntax, &floats);
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "float conversion semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
        REQUIRE(analysis->valid && !syntax->diagnostic_count);
        uint64_t widened_id = state_id("widened"), narrowed_id = state_id("narrowed");
        REQUIRE(widened_id && narrowed_id);
        const script_lower_bindings bindings = { .global = global, .host = float_host };
        owned_plan = script_lower(analysis, &floats, &bindings);
        REQUIRE(owned_plan && owned_plan->valid);
        char error[192];
        module = execution_module_finish(&owned_plan, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "float conversion publication: %s\n", error);
        }
        REQUIRE(module && !owned_plan);
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        const pin_value dt = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &dt, 1, 0, 0, 1000) == FLOW_OK);
        const pin_value *widened = execution_instance_state(instance, widened_id),
                        *narrowed = execution_instance_state(instance, narrowed_id);
        REQUIRE(widened && widened->type == PIN_F64 && widened->d == 3.25);
        REQUIRE(narrowed && narrowed->type == PIN_F32 && narrowed->f == 3.5f);
    }
    return true;
}

TEST_CASE(program_state_actions_demand_inputs_route_and_preserve_per_owner_state)
{
    const char *source =
        "behavior StateMachines {"
        " evaluated:double=0; order:double=0; once_hits:double=0; "
        "count_hits:double=0;"
        " flips_a:double=0; flips_b:double=0; gate_hits:double=0;"
        " multi_a:double=0; multi_b:double=0; counter:double=-1;"
        " function Closed()->(Value:bool){evaluated+=1;return(false);}"
        " function Limit()->(Value:int){evaluated+=10;return(2);}"
        " function Start()->(Value:int){order=order*10+3;return(0);}"
        " function Random()->(Value:bool){order=order*10+2;return(false);}"
        " function Loop()->(Value:bool){order=order*10+1;return(false);}"
        " action once=do_once(\"Start Closed\":Closed());"
        " action count=do_n(N:Limit()); action flip=flip_flop();"
        " action door=gate(\"Start Closed\":Closed());"
        " action multi=multi_gate(Loop:Loop(),Random:Random(),\"Start "
        "Index\":Start());"
        " on Impulse(amount,enabled){"
        "  if enabled "
        "{once();count();counter=count.Counter;flip();door();order=0;multi();}"
        "  else {once.Reset();count.Reset();door.Close();multi.Reset();}"
        " }"
        " after once.Completed{once_hits+=1;} after count.Exit{count_hits+=1;}"
        " after flip.A{flips_a+=1;} after flip.B{flips_b+=1;}"
        " after door.Exit{gate_hits+=1;}"
        " after multi.Out0{multi_a+=1;} after multi.Out1{multi_b+=1;}"
        "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_state(source, backends[b]));
        ecs_entity owners[] = { ecs_world_create_entity(world),
                                ecs_world_create_entity(world) };
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance
            *first = execution_program_attach(program, ecs_entity_to_u64(owners[0]));
        execution_instance *
            second = execution_program_attach(program, ecs_entity_to_u64(owners[1]));
        REQUIRE(first && second);
        pin_value args[] = { { .type = PIN_F64 }, { .type = PIN_BOOL, .b = true } };
        for (int i = 0; i < 3; i++) {
            REQUIRE(execution_instance_dispatch(first, 11, args, 2, i, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(first, "evaluated", 12 + i * 10));
            REQUIRE(number_state(first, "order", 321));
        }
        REQUIRE(number_state(first, "once_hits", 1));
        REQUIRE(number_state(first, "count_hits", 2) && number_state(first, "counter", 2));
        REQUIRE(number_state(first, "flips_a", 2) && number_state(first, "flips_b", 1));
        REQUIRE(number_state(first, "gate_hits", 3));
        REQUIRE(number_state(first, "multi_a", 1) && number_state(first, "multi_b", 1));
        REQUIRE(execution_instance_dispatch(second, 11, args, 2, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(second, "evaluated", 12) && number_state(second, "multi_a", 1));
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(first);
        REQUIRE(execution_instance_dispatch(first, 11, args, 2, 4, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(first, "evaluated", 42) && number_state(first, "once_hits", 1));
        REQUIRE(number_state(first, "count_hits", 2) && number_state(first, "flips_b", 2));
        REQUIRE(number_state(first, "multi_a", 1) && number_state(first, "multi_b", 1));
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(first, 11, args, 2, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(first, "evaluated", 42) && number_state(first, "order", 321));
        args[1].b = true;
        REQUIRE(execution_instance_dispatch(first, 11, args, 2, 6, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(first, "evaluated", 52) && number_state(first, "once_hits", 2));
        REQUIRE(number_state(first, "count_hits", 3) && number_state(first, "counter", 0));
        REQUIRE(number_state(first, "gate_hits", 4));
        REQUIRE(number_state(first, "multi_a", 2) && number_state(first, "multi_b", 1));
        REQUIRE(execution_program_replace(program, module, true) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(first);
        REQUIRE(execution_instance_dispatch(first, 11, args, 2, 7, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(first, "evaluated", 12) && number_state(first, "once_hits", 1));
        REQUIRE(number_state(first, "count_hits", 1) && number_state(first, "flips_a", 1));
        REQUIRE(number_state(first, "multi_a", 1) && number_state(first, "multi_b", 0));
    }
    return true;
}
TEST_CASE(program_retrigger_retains_original_payload_and_evaluates_each_duration_once)
{
    const char *source =
        "behavior Retriggers { evaluations:double=0; completions:double=0; "
        "captured:double=0;"
        " function Duration()->(seconds:double){evaluations+=1;return(3);}"
        " action pending=retriggerable_delay(Duration:Duration());"
        " on Impulse as signal(amount,enabled){pending();}"
        " after pending.Completed{completions+=1;captured=signal.amount;} }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_state(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        pin_value args[] = { { .type = PIN_F64, .d = 1 }, { .type = PIN_BOOL } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        args[0].d = 2;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "evaluations", 2));
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "completions", 0));
        REQUIRE(execution_program_update(program, 3.5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "completions", 0));
        REQUIRE(execution_program_update(program, 4, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "completions", 1) &&
            number_state(instance, "captured", 1)
        );
        args[0].d = 7;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "evaluations", 3));
        execution_program_detach(program, ecs_entity_to_u64(owner));
        instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        REQUIRE(execution_program_update(program, 9, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "completions", 0) &&
            number_state(instance, "evaluations", 0)
        );
    }
    return true;
}

TEST_CASE(program_debugger_steps_without_replaying_and_retains_owned_watches)
{
    const char *source = "behavior Debugger { calls:double=0; "
                         "observed:double=0; text:string=\"unset\"; "
                         "function "
                         "Work()->(Value:double,Text:string){calls+=1;return("
                         "calls,\"snapshot\");} "
                         "on Tick { values:=Work(); observed=values.Value; "
                         "text=values.Text; "
                         "await Delay(1); observed+=10; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile_module(source, backends[b]));
        world = ecs_world_new();
        REQUIRE(world);
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        uint64_t owner = ecs_entity_to_u64(entity);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, owner);
        REQUIRE(instance);
        debugger = execution_program_inspector(program);
        REQUIRE(debugger);
        REQUIRE(execution_inspector_select_owner(debugger, owner));
        REQUIRE(execution_inspector_owner_count(debugger) == 1);
        REQUIRE(execution_inspector_owner_at(debugger, 0) == owner);
        uint64_t handler = 0, call = 0;
        for (int n = 0; n < execution_module_function_count(module); n++) {
            execution_module_function_info f = execution_module_function_at(module, n);
            if (f.event == 10) {
                handler = f.id;
            }
        }
        for (int n = 0; n < analysis->expression_count; n++) {
            const script_expression *e = &analysis->expressions[n];
            if (e->kind == SCRIPT_EXPR_CALL && e->right >= 0) {
                call = script_record_id(analysis, SCRIPT_RECORD_EXPRESSION, n);
            }
        }
        REQUIRE(handler && call);
        REQUIRE(
            execution_inspector_breakpoint(debugger, execution_module_id(module), handler, call, true)
        );
        pin_value tick = { .type = PIN_F64 };
        REQUIRE(execution_instance_dispatch(instance, 10, &tick, 1, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_paused(program));
        execution_debug_cursor first = execution_inspector_cursor(debugger);
        REQUIRE(first.status == FLOW_PAUSED_RESULT);
        REQUIRE(first.owner == owner && first.point.occurrence == call && first.depth == 0);
        REQUIRE(number_state(instance, "calls", 0));
        REQUIRE(execution_program_update(program, 0.5, 0, 1000) >= FLOW_OK);
        REQUIRE(number_state(instance, "calls", 0));
        REQUIRE(
            execution_inspector_breakpoint(debugger, execution_module_id(module), handler, call, false)
        );
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_INTO) == FLOW_PAUSED_RESULT);
        execution_debug_cursor child = execution_inspector_cursor(debugger);
        REQUIRE(child.run == first.run && child.step > first.step && child.depth == 1);
        REQUIRE(execution_inspector_frame_count(debugger) == 2);
        REQUIRE(number_state(instance, "calls", 0));
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_OUT) == FLOW_PAUSED_RESULT);
        REQUIRE(execution_inspector_cursor(debugger).depth == 0);
        REQUIRE(number_state(instance, "calls", 1));
        REQUIRE(number_state(instance, "observed", 0));
        for (int n = 0; n < 16 && !number_state(instance, "observed", 1); n++) {
            REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_OVER) == FLOW_PAUSED_RESULT);
            REQUIRE(number_state(instance, "calls", 1));
        }
        REQUIRE(number_state(instance, "observed", 1));
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_SUSPENDED_RESULT);
        REQUIRE(number_state(instance, "calls", 1));
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "observed", 11));
        REQUIRE(execution_inspector_pause(debugger, owner));
        REQUIRE(execution_instance_dispatch(instance, 10, &tick, 1, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "calls", 1));
        REQUIRE(execution_program_paused(program));
        REQUIRE(execution_inspector_cursor(debugger).status == FLOW_PAUSED_RESULT);
        REQUIRE(execution_inspector_cursor(debugger).run > first.run);
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_SUSPENDED_RESULT);
        REQUIRE(number_state(instance, "calls", 2));
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "observed", 12));
        int saved = -1;
        for (int n = 0; n < execution_inspector_watch_count(debugger); n++) {
            const execution_debug_watch *w = execution_inspector_watch_at(debugger, n);
            if (w->availability == EXECUTION_WATCH_AVAILABLE && w->value.type == PIN_STR &&
                w->value.str && !strcmp(w->value.str, "snapshot")) {
                saved = n;
            }
        }
        REQUIRE(saved >= 0);
        REQUIRE(execution_inspector_activity_count(debugger) > 0);
        REQUIRE(execution_inspector_activity_dropped(debugger) == 0);
        execution_debug_cursor selected;
        REQUIRE(execution_inspector_owner_cursor(debugger, owner, &selected));
        int stack_count = execution_inspector_owner_frame_count(debugger, owner);
        REQUIRE(stack_count > 0);
        execution_debug_frame frame = execution_inspector_owner_frame_at(debugger, owner, 0);
        execution_program_detach(program, owner);
        REQUIRE(execution_inspector_owner_cursor(debugger, owner, &selected));
        REQUIRE(selected.retired);
        uint64_t selected_step = selected.step;
        ecs_entity sibling = ecs_world_create_entity(world);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        uint64_t sibling_owner = ecs_entity_to_u64(sibling);
        execution_instance *other = execution_program_attach(program, sibling_owner);
        REQUIRE(other);
        REQUIRE(execution_instance_dispatch(other, 10, &tick, 1, 4, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_pending(other) > 0);
        REQUIRE(execution_program_update(program, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(other, "observed", 11));
        REQUIRE(execution_inspector_cursor(debugger).owner == sibling_owner);
        REQUIRE(execution_inspector_owner_cursor(debugger, owner, &selected));
        REQUIRE(selected.retired && selected.step == selected_step);
        REQUIRE(execution_inspector_owner_frame_count(debugger, owner) == stack_count);
        REQUIRE(
            execution_inspector_owner_frame_at(debugger, owner, 0).function ==
            frame.function
        );
        execution_program_free(program);
        program = NULL;
        execution_module_free(module);
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(execution_inspector_cursor(debugger).retired);
        REQUIRE(!strcmp(execution_inspector_watch_at(debugger, saved)->value.str, "snapshot"));
        REQUIRE(execution_inspector_control(debugger, EXECUTION_DEBUG_RESUME) == FLOW_ERR_STATE);
        REQUIRE(!execution_inspector_pause(debugger, owner));
    }
    return true;
}

static const char *const task_source =
    "behavior Tasks { steps: double = 0; out: double = 0; seen: double = 0;"
    "function Count(n: double) -> (doubled: double) { steps += 1; yield; steps "
    "+= 1;"
    "await Delay(1); steps += 1; return(n * 2); }"
    "on Impulse(amount, enabled) { t := Count(amount); if t { seen += 1; }"
    "out = await t; if !t { seen += 10; } } }";
TEST_CASE(program_task_runs_to_first_suspension_then_beside_caller)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(task_source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        const pin_value args[] = { { .type = PIN_F64, .d = 4 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "steps", 1) && number_state(instance, "seen", 1));
        REQUIRE(number_state(instance, "out", 0));
        REQUIRE(execution_program_update(program, 0.25, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "steps", 2) && number_state(instance, "out", 0));
        REQUIRE(execution_program_update(program, 0.5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "steps", 2));
        REQUIRE(execution_program_update(program, 1.25, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "steps", 3) && number_state(instance, "out", 8));
        REQUIRE(number_state(instance, "seen", 11));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_cancel_cascades_and_wakes_awaiters)
{
    const char
        *source = "behavior Cancel { inner: double = 0; outer: double = 0; "
                  "woke: double = 0;"
                  "worker: task;"
                  "function Leaf() -> () { await Delay(1); inner += 1; }"
                  "function Inner() -> () { Leaf(); Leaf(); await Delay(1); "
                  "inner += 1; }"
                  "function Outer() -> () { Inner(); Inner(); await Delay(1); "
                  "outer += 1; }"
                  "function Watch() -> () { await worker; woke += 1; }"
                  "on Impulse(amount, enabled) { if enabled { worker = "
                  "Outer(); Watch(); } "
                  "else { cancel worker; cancel worker; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 0 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) > 0);
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.5, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "woke", 1));
        REQUIRE(number_state(instance, "inner", 0) && number_state(instance, "outer", 0));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_refused_ancestor_cancel_keeps_siblings)
{
    /* C cancels its own parent, which is refused before the newer sibling A
     * is touched. */
    const char *source = "behavior K { kept: double = 0; worker: task;"
                         "function A() -> () { await Delay(1); kept += 1; }"
                         "function C() -> () { yield; cancel worker; }"
                         "function P() -> () { C(); A(); await Delay(5); }"
                         "on Impulse(amount, enabled) { worker = P() } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 0.1, 0, 1000) != FLOW_OK);
        execution_program_update(program, 2, 0, 1000);
        REQUIRE(number_state(instance, "kept", 1));
    }
    return true;
}
TEST_CASE(program_task_owns_tasks_started_by_its_continuations)
{
    /* P's outcome continuation resumes on its own and starts Spin, which
     * still belongs to P, so cancelling P stops it. */
    const char
        *source = "behavior C { ticks: double = 0; worker: task;"
                  "function Spin() -> () { while true { ticks += 1; yield } }"
                  "function Count() -> (shown: double) { return 1 }"
                  "action act = Count();"
                  "function P() -> () { act(); await Delay(10) }"
                  "after act { yield; Spin() }"
                  "on Impulse(amount, enabled) { if enabled { worker = P() } "
                  "else { cancel worker } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        pin_value args[] = { { .type = PIN_F64, .d = 0 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "ticks", 2));
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 3, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 4, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "ticks", 2));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_continuation_cannot_cancel_or_await_its_task)
{
    /* The continuation resumes on its own, so P is suspended while it runs. */
    const char *bodies[] = { "cancel worker", "await worker" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int c = 0; c < 2; c++) {
        char source[EXAMPLE_SOURCE_CAPACITY];
        int length = snprintf(
            source,
            sizeof source,
            "behavior S { worker: task;"
            "function Count() -> (shown: double) { return 1 }"
            "action act = Count();"
            "function P() -> () { act(); await Delay(10) }"
            "after act { yield; %s }"
            "on Impulse(amount, enabled) { worker = P() } }",
            bodies[c]
        );
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        for (int b = 0; b < 2; b++) {
            prepare();
            REQUIRE(compile(source, backends[b]));
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(ecs_world_create_entity(world))
            );
            const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                       { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_update(program, 1, 0, 1000) != FLOW_OK);
        }
    }
    return true;
}
TEST_CASE(program_task_slots_are_reused_without_reviving_old_handles)
{
    const char *source = "behavior Many { done: double = 0; kept: double = 0; "
                         "stale: double = 0; got: double = -1; first: task;"
                         "function Short() -> (v: double) { yield; done += 1; "
                         "return 7 }"
                         "function Long() -> () { await Delay(1); kept += 1; }"
                         "on Impulse(amount, enabled) { if !enabled { cancel "
                         "first; if first { stale += 1; } } "
                         "else if amount == 0 { first = Short(); } "
                         "else if amount == 1 { Short(); } "
                         "else if amount == 3 { got = await first } else { "
                         "Long(); } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 0 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        args[0].d = 1;
        for (int i = 0; i < 31; i++) {
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        }
        REQUIRE(execution_program_update(program, 0.1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "done", 32));
        /* Starting a task expires nothing: the first result is still kept. */
        args[0].d = 2;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.15, 0, 1000) == FLOW_OK);
        args[0].d = 3;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.15, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "got", 7));
        /* The 33rd finish expires the first, and its slot runs the next Long. */
        args[0].d = 1;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.2, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 0.3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "done", 33));
        args[0].d = 3;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.32, 0, 1000) != FLOW_OK);
        args[0].d = 2;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.35, 0, 1000) == FLOW_OK);
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0.4, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "kept", 2) && number_state(instance, "stale", 0));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_stops_with_owner_and_does_not_block_events)
{
    const char
        *source = "behavior Loop { ticks: double = 0; hits: double = 0;"
                  "function Spin() -> () { while true { ticks += 1; yield; } }"
                  "on Impulse(amount, enabled) { if enabled { Spin(); } hits "
                  "+= 1; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        uint64_t handle = ecs_entity_to_u64(owner);
        execution_instance *instance = execution_program_attach(program, handle);
        REQUIRE(instance != NULL);
        pin_value args[] = { { .type = PIN_F64, .d = 0 },
                             { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "ticks", 4) && number_state(instance, "hits", 1));
        args[1].b = false;
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 3, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "hits", 2));
        execution_program_detach(program, handle);
        instance = execution_program_attach(program, handle);
        REQUIRE(execution_program_update(program, 5, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "ticks", 0) && execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_semantics_reject_invalid_uses)
{
    static const struct
    {
        const char *source, *message;
    } cases[] = {
        { "behavior B { n: double = 0; on Impulse(amount, enabled) { cancel n; "
          "} }",
          "Cancel requires a task" },
        { "behavior B { t: task; n: double = 0; on Impulse(amount, enabled) { "
          "n = t; } }",
          "" },
        /* Only the block's own `pulse` is assigned Charge, so awaiting the
         * state gives no result. */
        { "system S(p: Position) { shared worker: task }",
          "Shared state cannot hold a task" },
        /* An unawaited action continues before its result exists. */
        { "behavior B { n: double = 0; on Impulse(amount, enabled) { "
          "x := make_thing(); n = x } }",
          "Await this call before using its result" },
        { "behavior B { pulse: task; power: double = 0;"
          "function Charge(time: double) -> (power: double) { yield; return "
          "time }"
          "on Impulse(amount, enabled) { power = await pulse;"
          "if enabled { pulse: task = Charge(1); pulse = Charge(amount) } } }",
          "" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        prepare();
        syntax = script_parse(cases[i].source);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(!analysis->valid);
        bool found = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            found |= strstr(analysis->diagnostics[d].message, cases[i].message) != NULL;
        }
        REQUIRE(found);
    }
    return true;
}

TEST_CASE(program_nested_functions_wait_for_themselves)
{
    /* Version 10 blocked on nested callees too; action initializers keep
     * their own lifetime, parenthesized or not. */
    const char *source = "behavior N { n: double = 0;"
                         "action later = (Outer());"
                         "function Outer() -> () { function Inner() -> () { "
                         "await Delay(1); } Inner(); n += 1; } }";
    const char *expected = "behavior N { n: double = 0;"
                           "action later = (Outer());"
                           "function Outer() -> () { function Inner() -> () { "
                           "await Delay(1); } await Inner(); n += 1; } }";
    char **all = chibi_migrate_task_calls(&source, 1);
    REQUIRE(all[0] && !strcmp(all[0], expected));
    free(all[0]);
    free(all);
    /* Declaring a waiting function does not make its parent wait. */
    const char *plain = "behavior P { n: double = 0;"
                        "function Outer() -> (v: double) { "
                        "function Inner() { yield } return 3 }"
                        "on Impulse(amount, enabled) { x := Outer(); n = x } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(plain, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 3));
    }
    return true;
}
TEST_CASE(program_version_11_references_migrate_to_values)
{
    const char *source = "behavior R { n: double = 1; m: int = 6;"
                         "function Bump(ref double v) -> () { v += 2; }"
                         "on Impulse(amount, enabled) { Bump(n); &n = n + 1;"
                         " m = m & 3; m &= 7; s := \"a &b\"; /* &c */ } }";
    const char *expected = "behavior R { n: double = 1; m: int = 6;"
                           "function Bump(ref double v) -> () { v += 2; }"
                           "on Impulse(amount, enabled) { Bump(n); n = n + 1;"
                           " m = m & 3; m &= 7; s := \"a &b\"; /* &c */ } }";
    char *migrated = chibi_migrate_references(source);
    REQUIRE(migrated && !strcmp(migrated, expected));
    REQUIRE(!chibi_migrate_references(expected));
    char *bound = chibi_migrate_references("x := fallback(&speed, default: 0)");
    REQUIRE(bound && !strcmp(bound, "x := fallback(binding: speed, default: 0)"));
    free(bound);
    bound = chibi_migrate_references("x := fallback((&speed), default: 0)");
    REQUIRE(bound && !strcmp(bound, "x := fallback(binding: (speed), default: 0)"));
    free(bound);
    bound = chibi_migrate_references("x := fallback(driver, &speed, default: 0)");
    REQUIRE(bound && !strcmp(bound, "x := fallback(driver, binding: speed, default: 0)"));
    free(bound);
    bound = chibi_migrate_references("x := fallback(get(a, b), ((&speed)), default: (0))");
    REQUIRE(bound && !strcmp(bound, "x := fallback(get(a, b), binding: ((speed)), default: (0))"));
    free(bound);
    bound = chibi_migrate_references("x := fallback(binding: &speed, default: f(&n))");
    REQUIRE(bound && !strcmp(bound, "x := fallback(binding: speed, default: f(n))"));
    free(bound);
    bound = chibi_migrate_references("f(Value: ref int split &count, Other: true & ok)");
    REQUIRE(bound && !strcmp(bound, "f(Value: ref int split count, Other: true & ok)"));
    free(bound);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(migrated, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 4));
    }
    free(migrated);
    return true;
}
TEST_CASE(program_version_12_results_migrate_to_returns)
{
    const char *
        source = "behavior R { n: double = 0\n"
                 "    function Pair(x: double) -> (Sum: double, Twice: "
                 "double, Kept: double) {\n"
                 "        result Sum: double = x + 1\n"
                 "        Kept = 4\n"
                 "        result Twice: double = (x) * 2\n"
                 "        if x > 10 {\n"
                 "            return\n"
                 "        }\n"
                 "        return\n"
                 "    }\n"
                 "    pure function Seven() -> (Result: double) { "
                 "result Result: double = 7; }\n"
                 "    function Half(x: double) -> (Out: double) {\n"
                 "        result Out: double = x / 2\n"
                 "        if x > 100 {\n"
                 "            Out := 3\n"
                 "            Out = 4\n"
                 "            return\n"
                 "        }\n"
                 "    }\n"
                 "    function Set(x: double) -> (Value: double) {\n"
                 "        result Value: double = x\n"
                 "        if x > 1 {\n"
                 "            Value = 10\n"
                 "        }\n"
                 "        return\n"
                 "    }\n"
                 "    function List(x: double) -> (Values: double[]) {\n"
                 "        result Values: double[] = [x]\n"
                 "        Values[0] = 2\n"
                 "        return\n"
                 "    }\n"
                 "    function Bump(x: ref double) { x += 1 }\n"
                 "    function Observe(x: double) { return x }\n"
                 "    function Wrap() { function Observe(x: ref double) { "
                 "x = 1 } }\n"
                 "    function Bumped(x: double) -> (Total: double) {\n"
                 "        result Total: double = x\n"
                 "        Bump(Total)\n"
                 "        return\n"
                 "    }\n"
                 "    function Seen(x: double) -> (Last: double) {\n"
                 "        result Last: double = x\n"
                 "        Observe(x: Last)\n"
                 "        return\n"
                 "    }\n"
                 "    function Outer() -> (X: double) { if n > 0 {} /* lead */ "
                 "result X: double = 1; function Inner() { return } return }\n"
                 "    on Impulse(amount, enabled) { p := Pair(2); n = "
                 "p.Sum + p.Twice + p.Kept + Seven() + Half(4) + Set(4) }\n"
                 "}\n";
    const char *
        expected = "behavior R { n: double = 0\n"
                   "    function Pair(x: double) -> (Sum: double, "
                   "Twice: double, Kept: double) {\n"
                   "        Kept = 4\n"
                   "        if x > 10 {\n"
                   "            return(x + 1, (x) * 2, Kept)\n"
                   "        }\n"
                   "        return(x + 1, (x) * 2, Kept)\n"
                   "    }\n"
                   "    pure function Seven() -> (Result: double) { "
                   "return 7 }\n"
                   "    function Half(x: double) -> (Out: double) {\n"
                   "        if x > 100 {\n"
                   "            Out := 3\n"
                   "            Out = 4\n"
                   "            return x / 2\n"
                   "        }\n"
                   "        return x / 2\n"
                   "    }\n"
                   "    function Set(x: double) -> (Value: double) {\n"
                   "        Value = x\n"
                   "        if x > 1 {\n"
                   "            Value = 10\n"
                   "        }\n"
                   "        return Value\n"
                   "    }\n"
                   "    function List(x: double) -> (Values: double[]) {\n"
                   "        Values = [x]\n"
                   "        Values[0] = 2\n"
                   "        return Values\n"
                   "    }\n"
                   "    function Bump(x: ref double) { x += 1 }\n"
                   "    function Observe(x: double) { return x }\n"
                   "    function Wrap() { function Observe(x: ref double) { "
                   "x = 1 } }\n"
                   "    function Bumped(x: double) -> (Total: double) {\n"
                   "        Total = x\n"
                   "        Bump(Total)\n"
                   "        return Total\n"
                   "    }\n"
                   "    function Seen(x: double) -> (Last: double) {\n"
                   "        Observe(x: Last)\n"
                   "        return x\n"
                   "    }\n"
                   "    function Outer() -> (X: double) { if n > 0 {} /* lead "
                   "*/ "
                   "function Inner() { return } return 1 }\n"
                   "    on Impulse(amount, enabled) { p := Pair(2); n "
                   "= p.Sum + p.Twice + p.Kept + Seven() + Half(4) + Set(4) }\n"
                   "}\n";
    char *migrated = chibi_migrate_results(source);
    REQUIRE(migrated && !strcmp(migrated, expected));
    REQUIRE(!chibi_migrate_results(expected));
    /* Links at the staging paths are replaced, not written through, and
     * only the top-level version node changes. */
    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], outside[256],
         staged[256], error[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/R.chibi", root);
    FILE *file = fopen(path, "wb");
    fputs(source, file);
    fclose(file);
    snprintf(outside, sizeof outside, "%s/outside", root);
    file = fopen(outside, "wb");
    fputs("keep", file);
    fclose(file);
    snprintf(staged, sizeof staged, "%s/content/R.chibi.migrating", root);
    REQUIRE(!link(outside, staged));
    char manifest[256];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    snprintf(staged, sizeof staged, "%s.migrating", manifest);
    REQUIRE(!symlink(outside, staged));
    file = fopen(manifest, "wb");
    fputs(
        "meta {\n    version 12\n}\n/-\nversion 12\n/- x {\n}\n// metadata "
        "{\nname #\"P\\\"#; /* a /* b */ { */ \"version\" "
        "0xc\ncontent "
        "\"content\"\n"
        "start_scene \"scenes/Start.kdl\"\n",
        file
    );
    fclose(file);
    REQUIRE(!chmod(manifest, 0640) && !chmod(path, 0600));
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 12);
    char text[2048];
    file = fopen(outside, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strcmp(text, "keep"));
    struct stat info;
    REQUIRE(!lstat(manifest, &info) && S_ISREG(info.st_mode));
    REQUIRE((info.st_mode & 07777) == 0640);
    REQUIRE(!stat(path, &info) && (info.st_mode & 07777) == 0600);
    file = fopen(manifest, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(strstr(text, "meta version=12\n") && strstr(text, "version 15\n"));
    file = fopen(path, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strcmp(text, expected));
    unlink(path);
    unlink(outside);
    unlink(manifest);
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(migrated, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 30));
    }
    free(migrated);
    return true;
}
TEST_CASE(program_version_10_blocking_calls_migrate_to_awaits)
{
    const char *source = "behavior B { n: double = 0;"
                         "function Wait() -> () { await Delay(1); n += 1; }"
                         "function Outer() -> (v: double, w: double) { Wait(); "
                         "return(n, 0); }"
                         "function Plain() -> () { n += 100; }"
                         "on Impulse(amount, enabled) { Plain(); x := "
                         "Outer().v; n += x; Outer(); await Wait(); } }";
    const char
        *expected = "behavior B { n: double = 0;"
                    "function Wait() -> () { await Delay(1); n += 1; }"
                    "function Outer() -> (v: double, w: double) { await "
                    "Wait(); return(n, 0); }"
                    "function Plain() -> () { n += 100; }"
                    "on Impulse(amount, enabled) { Plain(); x := (await "
                    "Outer()).v; n += x; await Outer(); await Wait(); } }";
    const char
        *library = "library Store { function Work() -> () { await Delay(1); } "
                   "function Idle() -> () { } }";
    const char
        *user = "behavior U { on Impulse(amount, enabled) { Store.Work(); "
                "store.work(); Store.Idle(); Work(); } }";
    const char *sources[] = { source, library, user };
    char **all = chibi_migrate_task_calls(sources, 3);
    char *migrated = all[0];
    REQUIRE(migrated && !strcmp(migrated, expected) && !all[1]);
    REQUIRE(
        all[2] &&
        !strcmp(
            all[2],
            "behavior U { on Impulse(amount, enabled) { await Store.Work(); "
            "await store.work(); Store.Idle(); Work(); } }"
        )
    );
    free(all[2]);
    free(all);
    all = chibi_migrate_task_calls(&expected, 1);
    REQUIRE(!all[0]);
    free(all);
    /* A call names the innermost function of that name in its scope. */
    const char
        *nested = "behavior N { function A() -> () { "
                  "function Tick() -> () { } Tick(); } "
                  "function C() -> () { "
                  "function Tick() -> () { await Delay(1); } Tick(); } }";
    all = chibi_migrate_task_calls(&nested, 1);
    REQUIRE(
        all[0] &&
        !strcmp(
            all[0],
            "behavior N { function A() -> () { function Tick() -> () { } "
            "Tick(); } function C() -> () { function Tick() -> () { await "
            "Delay(1); } await Tick(); } }"
        )
    );
    free(all[0]);
    free(all);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(migrated, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 4; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 204) && execution_instance_pending(instance) == 0);
    }
    free(migrated);

    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/B.chibi", root);
    FILE *file = fopen(path, "wb");
    fputs(source, file);
    fclose(file);
    /* Symlinks are not sources: neither a cycle nor a file outside content is
     * followed. */
    char outside[256], link[256];
    snprintf(outside, sizeof outside, "%s/Outside.chibi", root);
    file = fopen(outside, "wb");
    fputs(source, file);
    fclose(file);
    snprintf(link, sizeof link, "%s/content/Outside.chibi", root);
    REQUIRE(!symlink(outside, link));
    snprintf(link, sizeof link, "%s/content/loop", root);
    REQUIRE(!symlink(root, link));
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    file = fopen(path, "wb");
    fputs(
        "version  10 // saved by an older build\r\ncontent \"content\"\r\n"
        "start_scene \"scenes/Start.kdl\"\r\n",
        file
    );
    fclose(file);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 10);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == 0);
    char text[1024];
    file = fopen(path, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strncmp(text, "version 15\n", 11));
    unlink(path);
    snprintf(path, sizeof path, "%s/content/B.chibi", root);
    file = fopen(path, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strcmp(text, expected));
    unlink(path);
    file = fopen(outside, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strcmp(text, source));
    unlink(outside);
    unlink(link);
    snprintf(link, sizeof link, "%s/content/Outside.chibi", root);
    unlink(link);
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    return true;
}

TEST_CASE(program_version_13_values_migrate_to_their_expressions)
{
    const char *source = "behavior V { n: double = 0\n"
                         "    value twice = n * 2\n"
                         "    value more = twice + 1\n"
                         "    /* who */ value who: entity = self\n"
                         "    function Add(x: double) {\n"
                         "        value sum: double = x + more\n"
                         "        local X = entry.x\n"
                         "        return Keep(Value: sum + local.X)\n"
                         "    }\n"
                         "    function Keep(Value: double) { return Value }\n"
                         "    function Shadow(more: double) { return more }\n"
                         "    on Impulse(amount, enabled) { w := who; n = "
                         "Add(amount) + "
                         "Shadow(5) + twice }\n"
                         "}\n";
    const char *expected = "behavior V { n: double = 0\n"
                           "    /* who */ function Add(x: double) {\n"
                           "        local X = entry.x\n"
                           "        return Keep(Value: cast<double>(x + ((n * "
                           "2) + 1)) + local.X)\n"
                           "    }\n"
                           "    function Keep(Value: double) { return Value }\n"
                           "    function Shadow(more: double) { return more }\n"
                           "    on Impulse(amount, enabled) { w := "
                           "cast<entity>(self); n = Add(amount) "
                           "+ Shadow(5) + (n * 2) }\n"
                           "}\n";
    char error[256] = "";
    char *migrated = chibi_migrate_values(source, error, sizeof error);
    REQUIRE(migrated && !strcmp(migrated, expected) && !error[0]);
    REQUIRE(!chibi_migrate_values(expected, error, sizeof error) && !error[0]);
    /* Expanding `twice` where a local `n` is declared would read the local. */
    const char *shadowed = "behavior V { n: double = 0\n"
                           "    value twice = n * 2\n"
                           "    on Impulse(amount, enabled) { n := amount; m "
                           ":= twice }\n"
                           "}\n";
    REQUIRE(!chibi_migrate_values(shadowed, error, sizeof error) && strstr(error, "`n`"));
    /* Expanding `x` inside Outer would call Outer's own F. */
    const char *callable = "behavior V { n: double = 0\n"
                           "    function F() { return 1 }\n"
                           "    value x = F()\n"
                           "    function Outer() { function F() { return 2 }\n"
                           "        return x }\n"
                           "}\n";
    error[0] = '\0';
    REQUIRE(!chibi_migrate_values(callable, error, sizeof error) && strstr(error, "`F`"));
    /* An action is a callable too. */
    const char *action = "behavior V { n: double = 0\n"
                         "    function F() { return 1 }\n"
                         "    value x = F()\n"
                         "    function Outer() { action F = timer(1)\n"
                         "        return x }\n"
                         "}\n";
    error[0] = '\0';
    REQUIRE(!chibi_migrate_values(action, error, sizeof error) && strstr(error, "`F`"));
    free(migrated);
    prepare();
    REQUIRE(compile(expected, BEHAVIOR_BYTECODE));
    execution_instance *instance = execution_program_attach(
        program,
        ecs_entity_to_u64(ecs_world_create_entity(world))
    );
    const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                               { .type = PIN_BOOL, .b = true } };
    REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(number_state(instance, "n", 8));
    return true;
}
TEST_CASE(program_version_14_scenes_migrate_to_own_behaviors)
{
    const char *source = "version 1\n"
                         "name \"Start\"\n"
                         "(array)entities {\n"
                         "    - name=\"Camera\" prototype=\"Camera\"\n"
                         "    - name=\"Player\" prototype=\"Player\" {\n"
                         "        (array)behaviors \"Move\"\n"
                         "    }\n"
                         "}\n";
    const char *expected = "version 1\n"
                           "name \"Start\"\n"
                           "(array)entities {\n"
                           "    - {\n"
                           "        name \"Camera\"\n"
                           "        prototype \"Camera\"\n"
                           "        (array)behaviors\n"
                           "    }\n"
                           "    - {\n"
                           "        name \"Player\"\n"
                           "        prototype \"Player\"\n"
                           "        (array)behaviors \"Move\"\n"
                           "    }\n"
                           "}\n";
    char *migrated = chibi_migrate_scene_behaviors(source);
    REQUIRE(migrated && !strcmp(migrated, expected));
    REQUIRE(!chibi_migrate_scene_behaviors(expected));
    free(migrated);

    char root[] = "/tmp/chibi-migrate-XXXXXX", path[256], error[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/scenes", root);
    REQUIRE(!mkdir(path, 0700));
    char scene[256];
    snprintf(scene, sizeof scene, "%s/content/scenes/Start.kdl", root);
    FILE *file = fopen(scene, "wb");
    fputs(source, file);
    fclose(file);
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    file = fopen(path, "wb");
    fputs("version 14\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n", file);
    fclose(file);
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == 1);
    char text[1024];
    file = fopen(scene, "rb");
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    REQUIRE(!strcmp(text, expected));
    unlink(scene);
    unlink(path);
    snprintf(path, sizeof path, "%s/content/scenes", root);
    rmdir(path);
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    return true;
}
TEST_CASE(program_single_result_call_fields_read_the_result)
{
    const char *source = "behavior B { n: double = 0;"
                         "function P() -> (v: double) { return(n + 1); }"
                         "function W() -> (v: double) { await Delay(1); "
                         "return(n + 10); }"
                         "on Impulse(amount, enabled) { x := P().v; n += x; y "
                         ":= await W(); n += y.v;"
                         "n += (await W()).v; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 34));
    }
    return true;
}

TEST_CASE(program_results_are_inferred_from_returns)
{
    const char
        *source = "behavior B { n: double = 0;"
                  "function Faster(a: double, b: double) { if a > b { return "
                  "true } return false }"
                  "function Pick(a: double, b: double) { if Faster(a, b) { "
                  "return a } return b }"
                  "function Split(x: double) { return(Low: x, High: x * 10) }"
                  "function Later(x: double) { yield; return x + 1 }"
                  "on Impulse(amount, enabled) { n += Pick(2, 5); r := "
                  "Split(amount);"
                  " n += r.Low + r.High; n += await Later(1); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 18));
    }
    /* B's nested F calls A; A's own nested F is a different function. */
    const char
        *nested = "behavior B { n: double = 0;"
                  "function Outer() { function F() { return Inner() } return "
                  "F() }"
                  "function Inner() { function F() { return 2 } return F() }"
                  "on Impulse(amount, enabled) { n = Outer() } }";
    prepare();
    REQUIRE(compile(nested, BEHAVIOR_BYTECODE));
    execution_instance *instance = execution_program_attach(
        program,
        ecs_entity_to_u64(ecs_world_create_entity(world))
    );
    const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                               { .type = PIN_BOOL, .b = true } };
    REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
    REQUIRE(number_state(instance, "n", 2));
    static const struct
    {
        const char *source, *message;
    } invalid[] = {
        { "behavior B { function Count(n: double) { if n < 1 { return 0.0 } "
          "return Count(n - 1) + 1 } }",
          "Recursive functions need declared results" },
        { "behavior B { function Pair(x: double) { return(x, x) } }", "Name each result" },
        { "behavior B { function Mixed(x: double) { if x > 0 { return true } "
          "return x } }",
          "" },
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        prepare();
        syntax = script_parse(invalid[i].source);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(!analysis->valid);
        bool found = false;
        for (int d = 0; d < analysis->diagnostic_count; d++) {
            found |= strstr(analysis->diagnostics[d].message, invalid[i].message) != NULL;
        }
        REQUIRE(found);
    }
    return true;
}

TEST_CASE(program_task_with_several_results_awaits_a_tuple)
{
    const char *source = "behavior B { n: double = 0;"
                         "function P(x: double) -> (a: double, b: double) { "
                         "yield; return(x, x * 10); }"
                         "on Impulse(amount, enabled) { t := P(amount); r := "
                         "await t; n += r.a + r.b;"
                         "n += (await t).b; u := P(2); n += (await u).a; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 23));
    }
    return true;
}

TEST_CASE(program_library_call_starts_a_task)
{
    const char *library_source = "library Store { count: double = 10;"
                                 "function Work() -> (local_count: double, "
                                 "position_x: double) {"
                                 "count += 1; await Delay(1); count += 1; "
                                 "return(count, self.Position.x); } }";
    const char *owner_source =
        "behavior Owner { n: double = 0; local_count: double = 0;"
        "on Impulse(amount, enabled) { t := library_work(); n += 1;"
        "if t { n += 10; } local_count = (await t).local_count; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_module(library_source, backends[b]));
        imported = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile(owner_source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(positioned(3, false))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 11) && number_state(instance, "local_count", 0));
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(
            number_state(instance, "local_count", 12) &&
            execution_instance_pending(instance) == 0
        );
    }
    return true;
}

TEST_CASE(program_library_task_writes_the_callers_state)
{
    const char *library_source = "library Lib { count: double = 10;"
                                 "function Bump(x: ref double) { yield; "
                                 "x += 1; count += 1 } }";
    const char
        *owner_source = "behavior Owner { n: double = 5;"
                        "on Impulse(amount, enabled) { library_bump(n) } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile_module(library_source, backends[b]));
        imported = module;
        module = NULL;
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(compile(owner_source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 6) && execution_instance_pending(instance) == 0);
    }
    return true;
}
TEST_CASE(program_task_keeps_state_and_component_references)
{
    const char *source = "behavior R { n: double = 0;"
                         "function Bump(x: ref double) -> () { yield; x += 1; "
                         "yield; x += 1; }"
                         "on Impulse(amount, enabled) { Bump(n); "
                         "Bump(self.Position.x); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = positioned(5, false);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", 0));
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 2) && execution_instance_pending(instance) == 0);
        pin_value x;
        REQUIRE(ecs_component_read_field(world, owner, 501, 0, &x) == ECS_OK && x.d == 7);
    }
    return true;
}
TEST_CASE(program_yield_resumes_on_the_next_update_without_time)
{
    const char *source = "behavior Y { done: double = 0;"
                         "function Short() { yield; done += 1 }"
                         "on Impulse(amount, enabled) { Short() } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_pump(program, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "done", 0));
        REQUIRE(execution_program_update(program, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "done", 1));
    }
    return true;
}
TEST_CASE(program_task_state_awaits_its_function_results)
{
    const char
        *source = "behavior P { pulse: task; power: double = 0;"
                  "function Charge(time: double) -> (power: double) { "
                  "yield; return(time * 2); }"
                  "on Impulse(amount, enabled) { pulse = Charge(amount); "
                  "power = await pulse; } }";
    /* The await is analyzed before the assignment that types the state. */
    const char
        *reordered = "behavior P { pulse: task; power: double = 0;"
                     "on Impulse(amount, enabled) { Start(amount); "
                     "power = await pulse }"
                     "function Start(time: double) { pulse = Charge(time) }"
                     "function Charge(time: double) -> (power: double) { "
                     "yield; return(time * 2); }"
                     "function Other() { pulse := 1 } }";
    const char
        *local = "behavior P { power: double = 0;"
                 "function Charge(time: double) -> (power: double) { "
                 "yield; return(time * 2); }"
                 "on Impulse(amount, enabled) { t: task = Charge(amount); "
                 "power = await t } }";
    const char
        *wrapped = "behavior P { pulse: task; power: double = 0;"
                   "on Impulse(amount, enabled) { Start(amount); "
                   "power = await pulse }"
                   "function Start(time: double) { pulse = (Charge(time)) }"
                   "function Charge(time: double) -> (power: double) { "
                   "yield; return(time * 2); } }";
    const char
        *qualified = "behavior P { pulse: task; power: double = 0;"
                     "on Impulse(amount, enabled) { Start(amount); "
                     "power = await pulse }"
                     "function Start(time: double) { state.pulse = "
                     "Charge(time) }"
                     "function Charge(time: double) -> (power: double) { "
                     "yield; return(time * 2); } }";
    /* A nested callable's own `pulse` does not shadow the outer one. */
    const char
        *nested = "behavior P { pulse: task; power: double = 0;"
                  "on Impulse(amount, enabled) { Start(amount); "
                  "power = await pulse }"
                  "function Start(time: double) { "
                  "function Other(pulse: double) { } pulse = Charge(time) }"
                  "function Charge(time: double) -> (power: double) { "
                  "yield; return(time * 2); } }";
    /* A sibling block's local `pulse` does not shadow the state. */
    const char *sibling = "behavior P { pulse: task; power: double = 0;"
                          "on Impulse(amount, enabled) { Start(amount); "
                          "power = await pulse }"
                          "function Start(time: double) { "
                          "if (time > 99) { pulse := 1 } pulse = Charge(time) }"
                          "function Charge(time: double) -> (power: double) { "
                          "yield; return(time * 2); } }";
    /* A nested callee types the state for awaits analyzed after it. */
    const char *inner = "behavior P { pulse: task; power: double = 0;"
                        "on Impulse(amount, enabled) { Start(amount) }"
                        "function Start(time: double) { "
                        "function Charge(time: double) -> (power: double) { "
                        "yield; return(time * 2); } "
                        "pulse = Charge(time); power = await pulse } }";
    const char *sources[] = { source,    reordered, local,   wrapped,
                              qualified, nested,    sibling, inner };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 16; b++) {
        prepare();
        REQUIRE(compile(sources[b / 2], backends[b % 2]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        const pin_value args[] = { { .type = PIN_F64, .d = 3 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "power", 6));
    }
    /* A task of an unknown function leaves the state without results. */
    prepare();
    syntax = script_parse(
        "behavior P { pulse: task; power: double = 0;"
        "function Charge(time: double) -> (power: double) { yield; "
        "return(time) }"
        "function Keep(t: task) { pulse = t }"
        "on Impulse(amount, enabled) { pulse = Charge(amount); power = await "
        "pulse } }"
    );
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(!analysis->valid);
    return true;
}
TEST_CASE(program_component_paths_test_presence)
{
    const char *source = "behavior H { n: double = 0;"
                         "on Impulse(amount, enabled) {"
                         "if self.Velocity { n += 1 }"
                         "if !self.Velocity { n += 10 }"
                         "if self.Velocity != nil && self.Position { n += 100 }"
                         "if self.Velocity == nil || self.Velocity.x > 3 { n "
                         "+= 1000 } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 4; b++) {
        prepare();
        REQUIRE(compile(source, backends[b % 2]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(positioned(1, b / 2))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        REQUIRE(number_state(instance, "n", b / 2 ? 1101 : 1010));
    }
    return true;
}
TEST_CASE(program_branch_completes_inside_action_continuation)
{
    /* The inner continuations capture the root local Shown, which the outer
     * continuation holds only by reference, before and after resuming. */
    const char *bodies[] = { "n += 1\n inner()", "n += 1\n await Wait()\n inner()" };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int c = 0; c < 2; c++) {
        char source[EXAMPLE_SOURCE_CAPACITY];
        int length = snprintf(
            source,
            sizeof source,
            "behavior B { n: double = 0\n"
            "function Count() -> (shown: double) { return 2 }\n"
            "function Wait() { yield }\n"
            "action outer = Count()\n"
            "local Shown = outer.shown\n"
            "action inner = engine[\"Branch\"](Condition: Shown > 0)\n"
            "on Impulse(amount, enabled) { outer() }\n"
            "after outer { %s }\n"
            "after inner.True { n += 10 }\n"
            "after inner.False { n += 100 } }",
            bodies[c]
        );
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        for (int b = 0; b < 2; b++) {
            prepare();
            REQUIRE(compile_state(source, backends[b]));
            execution_instance *instance = execution_program_attach(
                program,
                ecs_entity_to_u64(positioned(1, 1))
            );
            const pin_value args[] = { { .type = PIN_F64, .d = 1 },
                                       { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
            REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
            REQUIRE(number_state(instance, "n", 11));
        }
    }
    return true;
}
TEST_CASE(program_parameters_defaults_and_contextual_words)
{
    const char *source = "behavior V { n: double = 0\n"
                         "m: double = 2\n"
                         "k: double = 0\n"
                         "who: entity\n"
                         "tiles: int[]\n"
                         "function Approach(value: double, from: double) -> "
                         "(r: double) { return value - from }\n"
                         "function Step(left: double) -> (r: double) { "
                         "while left > 0 { left -= 1 } return left }\n"
                         "function Drain(left: double) -> (r: double) { "
                         "while left > 0 { left -= 1\n yield } "
                         "return left + 7 }\n"
                         "on Impulse(amount, enabled) { split := 1.0\n"
                         "value := amount\n"
                         "n = Approach(value, split) + Step(m)\n"
                         "k = await Drain(m)\n"
                         "who = self } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        execution_instance *instance = execution_program_attach(
            program,
            ecs_entity_to_u64(ecs_world_create_entity(world))
        );
        const pin_value args[] = { { .type = PIN_F64, .d = 5 },
                                   { .type = PIN_BOOL, .b = true } };
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        REQUIRE(number_state(instance, "n", 4) && number_state(instance, "m", 2));
        REQUIRE(number_state(instance, "k", 7));
    }
    return true;
}
TEST_CASE(program_task_shares_boxed_locals)
{
    const char *source = "behavior B { seen: double = 0; kept: double = 0;"
                         "function Bump(x: ref double) -> () { yield; x += 1; "
                         "yield; x += 10; }"
                         "function Keep(x: ref double) -> () { yield; x += 1; "
                         "yield; kept += x; }"
                         "function Relay(x: ref double) -> () { Keep(x); }"
                         "on Impulse(amount, enabled) {"
                         "if amount == 0 { v := 1.0; t := Bump(v); v += 100; "
                         "await t; seen = v; }"
                         "else if amount == 1 { w := 5.0; Keep(w); }"
                         "else if amount == 2 { i := 0.0; while i < 2 { u := "
                         "i * 10; Keep(u); i += 1; } }"
                         "else { j := 0.0; while j < 2 { z := j * 100 + 2; "
                         "Relay(z); j += 1; } } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance != NULL);
        for (int i = 0; i < 4; i++) {
            const pin_value args[] = { { .type = PIN_F64, .d = i },
                                       { .type = PIN_BOOL, .b = true } };
            REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000) == FLOW_OK);
        }
        for (int i = 1; i <= 3; i++) {
            REQUIRE(execution_program_update(program, i, 0, 1000) == FLOW_OK);
        }
        /* The caller and its task share v; each declaration of u, and of z
         * through Relay, is its own; w and z outlive their callers. */
        REQUIRE(number_state(instance, "seen", 112));
        REQUIRE(number_state(instance, "kept", 6 + 1 + 11 + 3 + 103));
        REQUIRE(execution_instance_pending(instance) == 0);
    }
    return true;
}

TEST_CASE(program_assignments_and_reference_writes_do_not_allocate)
{
    /* Plain state and local writes store in place, and a reference write of
     * a number needs no scratch storage. */
    const char
        *source = "behavior R { n: double = 0; m: double = 0;"
                  "function Add(x: ref double) -> () { i := 0.0; while i "
                  "< 1000 { x += 1; i += 1; } }"
                  "on Impulse(amount, enabled) { i := 0.0; while i < 1000 "
                  "{ n += 1; i += 1; } Add(m); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        prepare();
        REQUIRE(compile(source, backends[b]));
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        const pin_value args[] = { { .type = PIN_F64, .d = 0 },
                                   { .type = PIN_BOOL, .b = true } };
        uint64_t blocks = arena_block_allocations();
        REQUIRE(execution_instance_dispatch(instance, 11, args, 2, 0, 0, 1000000) == FLOW_OK);
        REQUIRE(arena_block_allocations() - blocks < 16);
        REQUIRE(number_state(instance, "n", 1000) && number_state(instance, "m", 1000));
    }
    return true;
}

void test_language_program()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(program_module_finish_preserves_failure_and_consumes_success);
    RUN_TEST_CASE(program_typed_event_changes_state_on_vm_and_sljit);
    RUN_TEST_CASE(program_system_membership_exit_cancels_pending_work);
    RUN_TEST_CASE(program_component_binding_runs_and_source_cycle_is_rejected);
    RUN_TEST_CASE(program_imported_library_preserves_owner_namespace_and_reader_after_replace);
    RUN_TEST_CASE(program_system_shared_state_survives_membership_and_is_registration_owned);
    RUN_TEST_CASE(program_managed_state_survives_overwrite_and_multiple_resumes);
    RUN_TEST_CASE(program_result_tuple_copies_survive_mutation_loop_and_resume);
    RUN_TEST_CASE(program_managed_payload_memory_is_bounded_across_root_and_outlined_resumes);
    RUN_TEST_CASE(program_lazy_tuple_initializes_once_and_preserves_group_writes);
    RUN_TEST_CASE(program_sibling_timer_actions_complete_independently_and_cancel_on_detach);
    RUN_TEST_CASE(program_failure_snapshot_owns_outlined_source_diagnostics);
    RUN_TEST_CASE(program_integer_and_nominal_enum_literals_preserve_exact_bits);
    RUN_TEST_CASE(program_concrete_assignments_preserve_name_byte_and_handle_storage);
    RUN_TEST_CASE(program_source_float_width_conversion_preserves_native_input);
    RUN_TEST_CASE(program_state_actions_demand_inputs_route_and_preserve_per_owner_state);
    RUN_TEST_CASE(program_retrigger_retains_original_payload_and_evaluates_each_duration_once);
    RUN_TEST_CASE(program_debugger_steps_without_replaying_and_retains_owned_watches);
    RUN_TEST_CASE(program_task_runs_to_first_suspension_then_beside_caller);
    RUN_TEST_CASE(program_task_cancel_cascades_and_wakes_awaiters);
    RUN_TEST_CASE(program_task_refused_ancestor_cancel_keeps_siblings);
    RUN_TEST_CASE(program_task_owns_tasks_started_by_its_continuations);
    RUN_TEST_CASE(program_task_continuation_cannot_cancel_or_await_its_task);
    RUN_TEST_CASE(program_task_slots_are_reused_without_reviving_old_handles);
    RUN_TEST_CASE(program_task_stops_with_owner_and_does_not_block_events);
    RUN_TEST_CASE(program_task_semantics_reject_invalid_uses);
    RUN_TEST_CASE(program_version_10_blocking_calls_migrate_to_awaits);
    RUN_TEST_CASE(program_nested_functions_wait_for_themselves);
    RUN_TEST_CASE(program_version_11_references_migrate_to_values);
    RUN_TEST_CASE(program_version_12_results_migrate_to_returns);
    RUN_TEST_CASE(program_version_13_values_migrate_to_their_expressions);
    RUN_TEST_CASE(program_version_14_scenes_migrate_to_own_behaviors);
    RUN_TEST_CASE(program_single_result_call_fields_read_the_result);
    RUN_TEST_CASE(program_results_are_inferred_from_returns);
    RUN_TEST_CASE(program_task_with_several_results_awaits_a_tuple);
    RUN_TEST_CASE(program_library_call_starts_a_task);
    RUN_TEST_CASE(program_library_task_writes_the_callers_state);
    RUN_TEST_CASE(program_task_keeps_state_and_component_references);
    RUN_TEST_CASE(program_yield_resumes_on_the_next_update_without_time);
    RUN_TEST_CASE(program_task_state_awaits_its_function_results);
    RUN_TEST_CASE(program_component_paths_test_presence);
    RUN_TEST_CASE(program_branch_completes_inside_action_continuation);
    RUN_TEST_CASE(program_parameters_defaults_and_contextual_words);
    RUN_TEST_CASE(program_task_shares_boxed_locals);
    RUN_TEST_CASE(program_assignments_and_reference_writes_do_not_allocate);
    pu_clear_setup();
}
