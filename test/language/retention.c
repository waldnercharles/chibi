#include "backend.h"
#include "ecs/ecs.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <stdio.h>
#include <string.h>

enum
{
    RUN = 10,
    MARKER_TYPE = 101,
    MARKER_COMPONENT = 501
};
static const type_field marker_field = { .name = "value",
                                         .type = PIN_I32,
                                         .size = sizeof(int),
                                         .type_name = "int" };
static const type_desc marker_type = { .name = "RetentionMarker",
                                       .size = sizeof(int),
                                       .fields = &marker_field,
                                       .field_count = 1 };
static const type_desc *const types[] = { &marker_type };
static const type_catalog_pack reflected = { .types = types, .type_count = 1 };
static const ecs_component_desc marker = {
    .id = MARKER_COMPONENT,
    .key = "retention.marker",
    .name = "RetentionMarker",
    .element_size = sizeof(int),
    .type = &marker_type,
};
static script_catalog_type script_marker = {
    .id = MARKER_TYPE,
    .name = "RetentionMarker",
    .type = { .storage = PIN_STRUCT, .nominal = MARKER_TYPE },
    .size = sizeof(int),
    .component = true,
    .component_id = MARKER_COMPONENT,
};
static const script_catalog_capability run = { .id = RUN,
                                               .name = "Run",
                                               .kind = SCRIPT_CAPABILITY_EVENT };
static const script_catalog catalog = { .types = &script_marker,
                                        .type_count = 1,
                                        .capabilities = &run,
                                        .capability_count = 1 };
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static ecs_world *world;
static bool project_types;

static void cleanup()
{
    execution_program_free(program);
    execution_module_free(module);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
    program = NULL;
    module = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
}
static bool compile(const char *source, behavior_backend backend)
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(stderr, "retention semantic: %s\n", analysis->diagnostics[i].message);
        }
        return false;
    }
    const script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(stderr, "retention lower: %s\n", plan->diagnostics[i].message);
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "retention publish: %s\n", error);
    }
    return module != NULL;
}
static bool prepare(const char *source, behavior_backend backend)
{
    type_catalog_set_project(&reflected);
    project_types = true;
    script_marker.layout = type_layout_hash(&marker_type);
    world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &marker, NULL) == ECS_OK);
    REQUIRE(compile(source, backend));
    program = execution_program_new(module, world, NULL);
    REQUIRE(program);
    return true;
}
static uint64_t state_id(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if ((symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE) &&
            !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}
static bool integer(execution_instance *instance, const char *name, int expected)
{
    const pin_value *value = execution_instance_state(instance, state_id(name));
    return value && value->type == PIN_I32 && value->i == expected;
}
static execution_instance *attach()
{
    uint64_t owner = ecs_entity_to_u64(ecs_world_create_entity(world));
    return execution_program_attach(program, owner);
}
TEST_CASE(retention_helpers_keep_owner_state_and_copy_plain_values)
{
    const char *
        source = "Counter :: behavior { result: int = 0; copied: int = 0; Next "
                 ":: () -> int { visits: retain int = 0; seen: retain = false; "
                 "fresh: int = 0; fresh += 1; visits += 1; seen = true; copy: "
                 "int = visits; copy += 100; copied = copy; return visits + "
                 "fresh; } on Run :: () { result = Next() + Next(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(source, backends[b]));
        execution_instance *first = attach(), *second = attach();
        REQUIRE(first && second);
        REQUIRE(execution_instance_dispatch(first, RUN, NULL, 0, 0, 0, 1000) == FLOW_OK);
        REQUIRE(integer(first, "visits", 2) && integer(first, "result", 5));
        REQUIRE(integer(first, "copied", 102) && integer(second, "visits", 0));
        REQUIRE(execution_instance_dispatch(first, RUN, NULL, 0, 1, 0, 1000) == FLOW_OK);
        REQUIRE(integer(first, "visits", 4) && integer(first, "result", 9));
        REQUIRE(execution_instance_dispatch(second, RUN, NULL, 0, 2, 0, 1000) == FLOW_OK);
        REQUIRE(integer(second, "visits", 2) && integer(second, "result", 5));
        const pin_value *seen = execution_instance_state(first, state_id("seen"));
        REQUIRE(seen && seen->type == PIN_BOOL && seen->b);
        cleanup();
    }
    return true;
}
TEST_CASE(retention_system_owner_survives_row_exit_and_registration_isolation)
{
    const char *source = "Counter :: system( row: ^RetentionMarker) { total: "
                         "retain int = 0; visits: retain int = "
                         "0; Next :: () { calls:retain int=0; calls+=1; } "
                         "total += 1; visits += 1; Next(); }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(source, backends[b]));
        ecs_entity owners[] = { ecs_world_create_entity(world),
                                ecs_world_create_entity(world) };
        int value = 0;
        for (int i = 0; i < 2; i++) {
            REQUIRE(
                ecs_world_record_add(world, owners[i], MARKER_COMPONENT, &value, 0) == ECS_OK
            );
        }
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 0, 0, 1000) == FLOW_OK);
        execution_instance *
            first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        execution_instance *
            second = execution_program_instance(program, ecs_entity_to_u64(owners[1]));
        REQUIRE(first && second);
        REQUIRE(integer(first, "total", 2) && integer(second, "total", 2));
        REQUIRE(integer(first, "visits", 2) && integer(second, "visits", 2));
        REQUIRE(integer(first, "calls", 2) && integer(second, "calls", 2));
        REQUIRE(ecs_world_record_remove(world, owners[0], MARKER_COMPONENT, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(owners[0])));
        REQUIRE(integer(second, "total", 3) && integer(second, "visits", 3));
        REQUIRE(ecs_world_record_add(world, owners[0], MARKER_COMPONENT, &value, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(execution_program_step(program, 2, 0, 1000) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(first && integer(first, "visits", 5));
        REQUIRE(integer(second, "visits", 5) && integer(first, "total", 5));
        REQUIRE(integer(first, "calls", 5));
        execution_program *other = execution_program_new(module, world, NULL);
        REQUIRE(other);
        int status = execution_program_step(other, 3, 0, 1000);
        execution_instance *independent = execution_program_instance(
            other,
            ecs_entity_to_u64(owners[0])
        );
        bool isolated = independent && integer(independent, "total", 2) &&
                        integer(independent, "visits", 2);
        execution_program_free(other);
        REQUIRE(status == FLOW_OK && isolated && integer(first, "total", 5));
        for (int i = 0; i < 2; i++) {
            REQUIRE(!ecs_world_record_remove(world, owners[i], MARKER_COMPONENT, 0));
        }
        REQUIRE(!ecs_world_apply_commands(world));
        REQUIRE(execution_program_step(program, 4, 0, 1000) == FLOW_OK);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(owners[0])));
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(owners[1])));
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        REQUIRE(!ecs_world_record_add(world, owners[0], MARKER_COMPONENT, &value, 0));
        REQUIRE(!ecs_world_apply_commands(world));
        REQUIRE(execution_program_step(program, 5, 0, 1000) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(first && integer(first, "total", 6) && integer(first, "calls", 6));
        REQUIRE(execution_program_replace(program, module, true) == FLOW_OK);
        REQUIRE(execution_program_step(program, 6, 0, 1000) == FLOW_OK);
        first = execution_program_instance(program, ecs_entity_to_u64(owners[0]));
        REQUIRE(first && integer(first, "total", 1) && integer(first, "calls", 1));
        cleanup();
    }
    return true;
}
TEST_CASE(retention_helper_identity_preserves_values_until_restart)
{
    const char *before = "Counter :: behavior { result: int = 0; Next :: () -> "
                         "int { visits: retain int = 0; visits += 1; return "
                         "visits; } on Run :: () { result = Next(); } }";
    const char *after = "Counter :: behavior { result: int = 0; Next :: () -> "
                        "int { visits: retain int = 100; visits += 1; return "
                        "visits; } on Run :: () { result = Next(); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(before, backends[b]));
        uint64_t owner = ecs_entity_to_u64(ecs_world_create_entity(world));
        execution_instance *instance = execution_program_attach(program, owner);
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, RUN, NULL, 0, 0, 0, 1000) == FLOW_OK);
        REQUIRE(integer(instance, "visits", 1));
        uint64_t identity = state_id("visits");
        execution_module_free(module);
        module = NULL;
        REQUIRE(compile(after, backends[b]));
        REQUIRE(state_id("visits") == identity);
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        instance = execution_program_instance(program, owner);
        REQUIRE(instance && integer(instance, "visits", 1));
        REQUIRE(execution_instance_dispatch(instance, RUN, NULL, 0, 1, 0, 1000) == FLOW_OK);
        REQUIRE(integer(instance, "result", 2));
        REQUIRE(execution_program_replace(program, module, true) == FLOW_OK);
        instance = execution_program_instance(program, owner);
        REQUIRE(instance && integer(instance, "visits", 100));
        REQUIRE(execution_instance_dispatch(instance, RUN, NULL, 0, 2, 0, 1000) == FLOW_OK);
        REQUIRE(integer(instance, "result", 101));
        cleanup();
    }
    return true;
}
void test_language_retention()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(retention_helpers_keep_owner_state_and_copy_plain_values);
    RUN_TEST_CASE(retention_system_owner_survives_row_exit_and_registration_isolation);
    RUN_TEST_CASE(retention_helper_identity_preserves_values_until_restart);
    pu_clear_setup();
}
