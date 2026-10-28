#include "backend.h"
#include "ecs/ecs.h"
#include "execution/function.h"
#include "execution/module.h"
#include "execution/program_internal.h"
#include "execution/status.h"
#include "language/lower.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    double x, y;
} query_position;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static ecs_world *world;
static const script_catalog_field position_fields[] = {
    { .id = 1011, .name = "x", .type = { .storage = PIN_F64 }, .writable = true },
    { .id = 1012,
      .name = "y",
      .type = { .storage = PIN_F64 },
      .offset = offsetof(query_position, y),
      .writable = true },
};
static script_catalog_type position_type = {
    .id = 101,
    .name = "Position",
    .type = { .storage = PIN_STRUCT, .nominal = 101 },
    .fields = position_fields,
    .field_count = 2,
    .size = sizeof(query_position),
    .component = true,
    .component_id = 501,
};
static const script_catalog_parameter step = { .name = "dt",
                                               .type = { .storage = PIN_F64 } };
static const script_catalog_parameter duration = { .name = "Duration",
                                                   .type = { .storage = PIN_F64 } };
static const char *const completed[] = { "Completed" };
static const char *const delay_entries[] = { "In" };
static const script_catalog_parameter assertion_parameters[] = {
    { .name = "condition", .type = { .storage = PIN_BOOL } },
    { .name = "message", .type = { .storage = PIN_STR } },
};
static const script_catalog_capability capabilities[] = {
    { .id = 30,
      .name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &step,
      .parameter_count = 1 },
    { .id = 31, .name = "Begin", .kind = SCRIPT_CAPABILITY_EVENT },
    { .id = 32, .name = "mutate", .effects = SCRIPT_EFFECT_WRITE },
    { .id = 33,
      .name = "Delay",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .operation = SCRIPT_OPERATION_DELAY,
      .entries = delay_entries,
      .entry_count = 1,
      .parameters = &duration,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = 34,
      .name = "assert",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .parameters = assertion_parameters,
      .parameter_count = 2 },
};
static const script_catalog catalog = {
    .types = &position_type,
    .type_count = 1,
    .capabilities = capabilities,
    .capability_count = 5,
    .entity_type = 40,
    .tick_event = 30,
};
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
static void prepare()
{
    cleanup();
    world = ecs_world_new();
    const type_desc *type = type_catalog_find("Position");
    ecs_component_desc component = { .id = 501,
                                     .key = "query.position",
                                     .name = "Position",
                                     .type = type,
                                     .element_size = sizeof(query_position) };
    ecs_world_register_component(world, &component, NULL);
    position_type.layout = type_layout_hash(type);
}
static bool host(void *user, uint64_t id, script_lower_host *out)
{
    (void)user;
    if (id != 33 && id != 34) {
        return false;
    }
    *out = (script_lower_host){ .intrinsic = id == 33 ? BF_DELAY : BF_ASSERT };
    return true;
}
static bool compile(const char *source, behavior_backend backend)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "query semantic: %s\n", analysis->diagnostics[i].message);
    }
    if (!analysis->valid) {
        return false;
    }
    script_lower_bindings bindings = { .host = host };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    for (int i = 0; i < plan->diagnostic_count; i++) {
        fprintf(stderr, "query lower: %s\n", plan->diagnostics[i].message);
    }
    if (!plan->valid) {
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "query module: %s\n", error);
        return false;
    }
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}
static ecs_entity positioned(double x)
{
    ecs_entity entity = ecs_world_create_entity(world);
    query_position value = { .x = x };
    ecs_world_record_add(world, entity, 501, &value, 0);
    ecs_world_apply_commands(world);
    return entity;
}
TEST_CASE(query_nested_native_collections_bind_and_capture)
{
    const char *sources[] = {
        "Apply :: system(p: Position, live: ^Position, copies := [[p]]) {"
        "local := copies; live.y = local[0][0].x; }",
        "Apply :: behavior { on Begin :: () { values := [[self.Position]];"
        "matches := query(copies := values); self.Position.x = 100;"
        "for row in matches { self.Position.y = row.copies[0][0].x; } } }"
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int source = 0; source < 2; source++) {
            prepare();
            ecs_entity entity = positioned(2);
            REQUIRE(compile(sources[source], backends[b]));
            if (source) {
                execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
                REQUIRE(instance);
                REQUIRE(execution_instance_dispatch(instance, 31, NULL, 0, 0, .1, 10000) == FLOW_OK);
            } else {
                REQUIRE(execution_program_step(program, 0, .1, 10000) == FLOW_OK);
            }
            const query_position *value = ecs_component_data_const(world, entity, 501);
            REQUIRE(value && value->x == (source ? 100 : 2) && value->y == 2);
        }
    }
    return true;
}

TEST_CASE(query_system_shares_snapshot_with_guard_and_body)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        REQUIRE(compile(
            "Apply :: system(before: Position, live: ^Position, copy := "
            "Copy(before), "
            "amount := copy.x * 2, if amount > 0) {"
            "Copy :: (value: Position) -> Position { return value; }"
            "live.x += amount; live.y = copy.x + dt; }",
            backends[b]
        ));
        REQUIRE(execution_program_step(program, 0, .5, 10000) == FLOW_OK);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        REQUIRE(value && value->x == 6 && value->y == 2.5);
    }
    return true;
}
TEST_CASE(query_empty_systems_require_attached_instances)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity first = positioned(2), second = positioned(7);
        REQUIRE(compile(
            "Apply :: system(/* manually registered */) { self.Position.x += "
            "1; "
            "self.Position.y += dt; }",
            backends[b]
        ));
        REQUIRE(execution_program_step(program, 0, .25, 10000) == FLOW_OK);
        const query_position *one = ecs_component_data_const(world, first, 501);
        const query_position *two = ecs_component_data_const(world, second, 501);
        REQUIRE(one && one->x == 2 && one->y == 0);
        REQUIRE(two && two->x == 7 && two->y == 0);
        REQUIRE(execution_program_attach(program, ecs_entity_to_u64(first)));
        REQUIRE(execution_program_step(program, 1, .25, 10000) == FLOW_OK);
        REQUIRE(execution_program_step(program, 2, .5, 10000) == FLOW_OK);
        one = ecs_component_data_const(world, first, 501);
        two = ecs_component_data_const(world, second, 501);
        REQUIRE(one && one->x == 4 && one->y == .75);
        REQUIRE(two && two->x == 7 && two->y == 0);
        REQUIRE(!execution_program_instance(program, ecs_entity_to_u64(second)));
    }
    return true;
}
TEST_CASE(query_pure_helpers_can_mutate_their_own_collections)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        REQUIRE(compile(
            "Apply :: system(p: ^Position?, if p, if Sum() == 5, amount := "
            "Sum()) {"
            "Sum :: () -> int { values := [1, 2]; values[0] = 3; return "
            "values[0] + values[1]; }"
            "p.x += amount; }",
            backends[b]
        ));
        REQUIRE(execution_program_step(program, 0, .1, 10000) == FLOW_OK);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        REQUIRE(value && value->x == 7);
    }
    return true;
}
TEST_CASE(query_values_capture_and_restart_iteration)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        REQUIRE(compile(
            "Apply :: behavior { on Begin :: () { threshold := 1.0;"
            "matches := query(p: ^Position, if p.x > threshold); threshold = "
            "100.0;"
            "for row in matches { row.p.x += 1; }"
            "for row in matches { row.p.y = row.p.x; } } }",
            backends[b]
        ));
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 31, NULL, 0, 0, 0, 10000) == FLOW_OK);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        REQUIRE(value && value->x == 3 && value->y == 3);
    }
    return true;
}
TEST_CASE(query_guards_skip_later_computations)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(0);
        REQUIRE(
            compile("Apply :: system(p: ^Position, if p.x > 0, amount := 10 / int(p.x)) { p.y = amount; }", backends[b])
        );
        REQUIRE(execution_program_step(program, 0, .25, 10000) == FLOW_OK);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        REQUIRE(value && value->y == 0);
    }
    return true;
}
TEST_CASE(query_value_bindings_and_headers_are_readonly)
{
    const char *sources[] = {
        "Apply :: system(p: Position) { p.x = 2; }",
        "Apply :: system(p: ^Position, amount := mutate()) {}",
        "Apply :: behavior { on Begin :: () { matches := query(p: Position); "
        "for row in matches { row.p.x = 2; } } }",
        "Apply :: behavior { on Begin :: () { matches := query(p: Position); "
        "for row in matches { matches = row; } } }",
        "Apply :: system(target: int in self.ChildOf) {}",
        "Apply :: system(p: ^Position) { dt = 1; }",
        "Apply :: system(dt := 1) {}",
        "Apply :: system(p: Position, if Check()) { Check :: () -> bool { q := "
        "query(value := p.x); return true; } }",
        "Apply :: system(inner := query(p: Position, if p.x > dt)) {}",
        "Apply :: behavior { on Begin :: () { value := 1; q := query(p: "
        "Position, ptr := &value); } }",
        "Apply :: system(p: Position, if Check()) { Check :: () -> bool { "
        "return p.x > 0; } }",
        "Apply :: system(p: ^Position, if dt > 0) {}",
        "Apply :: system(p: ^Position) { helper :: () { p.x += dt; } helper(); "
        "}",
        "Apply :: behavior { threshold := 1.0; Limit :: () -> f64 { return "
        "threshold; }"
        "on Begin :: () { q := query(p: Position, if p.x > Limit()); } }",
    };
    for (size_t i = 0; i < sizeof sources / sizeof *sources; i++) {
        prepare();
        syntax = script_parse(sources[i]);
        if (syntax->diagnostic_count) {
            fprintf(stderr, "query rejected fixture %zu: %s\n", i, sources[i]);
        }
        REQUIRE(!syntax->diagnostic_count);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(!analysis->valid);
    }
    return true;
}
TEST_CASE(query_snapshots_observe_reduction_components)
{
    const char *sources[] = {
        "Apply :: system(live: ^Position) { reduce live.x += 1; }",
        "Apply :: system(before: Position, live: ^Position) { reduce live.x += "
        "1; }",
        "Apply :: system(live: ^Position, seen := live.x) { reduce live.x += "
        "1; }",
        "Apply :: system(live: ^Position, alias := live, unused := live) { "
        "reduce alias.x += 1; }",
    };
    for (int i = 0; i < 4; i++) {
        prepare();
        syntax = script_parse(sources[i]);
        REQUIRE(!syntax->diagnostic_count);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(analysis->valid == (i == 0 || i == 3));
        if (i == 1 || i == 2) {
            bool reported = false;
            for (int j = 0; j < analysis->diagnostic_count; j++) {
                reported |= strstr(analysis->diagnostics[j].message, "outside reductions") !=
                            NULL;
            }
            REQUIRE(reported);
        }
    }
    return true;
}
TEST_CASE(query_optional_row_fields_narrow_in_body)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        ecs_world_create_entity(world);
        REQUIRE(compile(
            "Apply :: behavior { on Begin :: () { matches := query(p: "
            "^Position?, copy: Position?);"
            "for row in matches { if row.p { if row.copy { if row.copy { "
            "row.p.y += 1; } } } }"
            "for row in matches { if row.p != nil { row.p.y += 1; } } } }",
            backends[b]
        ));
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 31, NULL, 0, 0, 0, 10000) == FLOW_OK);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        REQUIRE(value && value->y == 2);
    }
    return true;
}
TEST_CASE(query_pointer_aliases_preserve_reduction_targets)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        REQUIRE(compile(
            "Apply :: system(live: ^Position, alias := live, unused := live) { "
            "reduce alias.x += 1; }",
            backends[b]
        ));
        REQUIRE(execution_program_step(program, 0, .1, 10000) == FLOW_OK);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        REQUIRE(value && value->x == 3);
    }
    return true;
}
TEST_CASE(query_returned_snapshots_survive_and_removed_references_fail)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        REQUIRE(compile(
            "Apply :: behavior { First :: () { while true {"
            "for outer in query(before: Position, p: ^Position) {"
            "for inner in query(other: Position) { return outer; } } } }"
            "on Begin :: () { saved := First(); copy := saved.before; await "
            "Delay(1);"
            "self.Position.y = copy.x; saved.p.x = copy.x; } }",
            backends[b]
        ));
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 31, NULL, 0, 0, 0, 10000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == 1);
        REQUIRE(execution_program_step(program, 1.1, .1, 10000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == 0);
        size_t retained = execution_program_memory_usage(program).payload_bytes;
        for (int i = 0; i < 64; i++) {
            REQUIRE(
                execution_instance_dispatch(instance, 31, NULL, 0, 2 + i * 2, 0, 10000) == FLOW_OK
            );
            REQUIRE(execution_program_step(program, 3.1 + i * 2, .1, 10000) == FLOW_OK);
            REQUIRE(execution_instance_pending(instance) == 0);
            size_t after = execution_program_memory_usage(program).payload_bytes;
            if (i < 16 && after > retained) {
                retained = after;
            }
            /* Later cycles must stay within the initial collection high-water mark. */
            REQUIRE(after <= retained);
        }
        REQUIRE(execution_instance_dispatch(instance, 31, NULL, 0, 132, 0, 10000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == 1);
        ecs_world_record_remove(world, entity, 501, 0);
        ecs_world_apply_commands(world);
        query_position replacement = { .x = 50, .y = 99 };
        ecs_world_record_add(world, entity, 501, &replacement, 0);
        ecs_world_apply_commands(world);
        REQUIRE(execution_program_step(program, 133.1, .1, 10000) < 0);
        const execution_program_failure *failure = execution_program_error(program);
        REQUIRE(failure && failure->message[0]);
        REQUIRE(execution_instance_pending(instance) == 0);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        /* The saved value snapshot remains usable; the removed reference never
         * retargets the replacement component. */
        REQUIRE(value && value->x == 50 && value->y == 2);
    }
    return true;
}
TEST_CASE(query_captures_existing_pointers_and_projects_component_fields)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        REQUIRE(compile(
            "Apply :: behavior { on Begin :: () { count := 0; counter := "
            "&count;"
            "matches := query(p: ^Position, cell := counter, field := &p.x);"
            "for row in matches { row.cell^ += 1; row.field^ += 3; } "
            "self.Position.y = count; } }",
            backends[b]
        ));
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 31, NULL, 0, 0, 0, 10000) == FLOW_OK);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        REQUIRE(value && value->x == 5 && value->y == 1);
    }
    return true;
}
TEST_CASE(query_nested_descriptions_capture_at_construction)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        REQUIRE(compile(
            "Apply :: behavior { on Begin :: () { threshold := 1.0;"
            "matches := query(before: Position, inner := query(p: ^Position, "
            "if p.x > threshold));"
            "threshold = 100.0; for row in matches { for hit in row.inner { "
            "hit.p.y += 1; } } } }",
            backends[b]
        ));
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(entity));
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 31, NULL, 0, 0, 0, 10000) == FLOW_OK);
        const query_position *value = ecs_component_data_const(world, entity, 501);
        REQUIRE(value && value->y == 1);
    }
    return true;
}
TEST_CASE(query_pure_helper_failure_preserves_error)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        positioned(0);
        REQUIRE(compile(
            "Apply :: system(p: Position, live: ^Position, nested := "
            "query(other: Position), checked := Check(int(p.x)), if "
            "has(ChildOf)) {"
            "Check :: (n: int) -> bool { assert(n > 0, \"row helper failed\"); "
            "return true; } }",
            backends[b]
        ));
        REQUIRE(execution_program_step(program, 0, .1, 10000) < 0);
        const execution_program_failure *failure = execution_program_error(program);
        if (!failure || !strstr(failure->message, "row helper failed")) {
            fprintf(stderr, "query helper failure: %s\n", failure ? failure->message : "no engine failure");
        }
        REQUIRE(failure && strstr(failure->message, "row helper failed"));
        size_t retained = execution_program_memory_usage(program).payload_bytes;
        for (int i = 0; i < 128; i++) {
            execution_program_clear_error(program);
            REQUIRE(execution_program_step(program, i + 1, .1, 10000) < 0);
            failure = execution_program_error(program);
            REQUIRE(failure && strstr(failure->message, "row helper failed"));
            size_t after = execution_program_memory_usage(program).payload_bytes;
            if (i < 32 && after > retained) {
                retained = after;
            }
            REQUIRE(after <= retained);
        }
    }
    return true;
}
TEST_CASE(query_scheduled_rows_survive_payload_collection)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        prepare();
        ecs_entity entity = positioned(2);
        REQUIRE(compile(
            "Apply :: system(before: Position, live: ^Position) { latest := 0; "
            "for i in 0..<256 { trash := [int(before.x), i]; latest = "
            "trash[0]; } "
            "await Delay(1); live.y = before.x + latest; live.x += 1; }",
            backends[b]
        ));
        REQUIRE(execution_program_step(program, 0, .1, 500000) == FLOW_OK);
        execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(entity));
        REQUIRE(instance && execution_instance_pending(instance) == 1);
        execution_activation *activation = instance->activations[0];
        REQUIRE(activation->payload.kept > 0);
        REQUIRE(activation->context.query_row);
        REQUIRE(arena_contains(&activation->payload, activation->context.query_row));
        query_position *value = ecs_component_data(world, entity, 501);
        REQUIRE(value);
        value->x = 20;
        REQUIRE(execution_program_pump(program, 1.1, .1, 500000) == FLOW_OK);
        REQUIRE(execution_instance_pending(instance) == 0);
        value = ecs_component_data(world, entity, 501);
        REQUIRE(value && value->x == 21 && value->y == 4);
    }
    return true;
}
void test_language_query_values()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(query_system_shares_snapshot_with_guard_and_body);
    RUN_TEST_CASE(query_nested_native_collections_bind_and_capture);
    RUN_TEST_CASE(query_empty_systems_require_attached_instances);
    RUN_TEST_CASE(query_values_capture_and_restart_iteration);
    RUN_TEST_CASE(query_pure_helpers_can_mutate_their_own_collections);
    RUN_TEST_CASE(query_guards_skip_later_computations);
    RUN_TEST_CASE(query_value_bindings_and_headers_are_readonly);
    RUN_TEST_CASE(query_optional_row_fields_narrow_in_body);
    RUN_TEST_CASE(query_snapshots_observe_reduction_components);
    RUN_TEST_CASE(query_pointer_aliases_preserve_reduction_targets);
    RUN_TEST_CASE(query_pure_helper_failure_preserves_error);
    RUN_TEST_CASE(query_nested_descriptions_capture_at_construction);
    RUN_TEST_CASE(query_captures_existing_pointers_and_projects_component_fields);
    RUN_TEST_CASE(query_returned_snapshots_survive_and_removed_references_fail);
    RUN_TEST_CASE(query_scheduled_rows_survive_payload_collection);
}
