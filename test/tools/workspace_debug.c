#include "authoring/workspace_debug.h"

#include "authoring/workspace_debug_definition.h"
#include "execution/function.h"
#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/scenes/scene.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static workspace_debug *debug;
static entities *prototype_entities;
static source_registry *registry;
static ecs_world *ecs;
static execution_module *module;
static execution_module_plan *plan;
static behavior_function_builder *builder;
static int entered, left;
static bool scope_ok;
static char frozen_root[1024];
static arena activity_storage;
static const char *source_root(void *, int session)
{
    return session == 7 && frozen_root[0] ? frozen_root : NULL;
}
static source_registry *resolve(void *, int session)
{
    return session == 7 ? registry : NULL;
}
static void *enter(void *, int session, const char **error)
{
    *error = NULL;
    scope_ok &= session == 7;
    entered++;
    return &entered;
}
static void leave(void *, void *token)
{
    scope_ok &= token == &entered;
    left++;
}
static void cleanup()
{
    arena_free(&activity_storage);
    if (prototype_entities) {
        entities_free(prototype_entities);
        prototype_entities = NULL;
    }
    workspace_debug_free(debug);
    debug = NULL;
    if (registry) {
        source_registry_free(registry);
    }
    registry = NULL;
    execution_module_free(module);
    module = NULL;
    execution_module_plan_free(plan);
    plan = NULL;
    if (builder) {
        behavior_function_builder_free(builder);
    }
    builder = NULL;
    if (ecs) {
        ecs_world_free(ecs);
    }
    ecs = NULL;
    entered = left = 0;
    scope_ok = true;
    if (frozen_root[0]) {
        tool_remove_tree(frozen_root);
    }
    frozen_root[0] = 0;
}
static const char *fail_native(void *, const pin_value *, pin_value *)
{
    return "intentional debugger failure";
}
static const node_host_call failing_call = { .invoke = fail_native };
static bool fixture(uint64_t *owner, bool failing)
{
    ecs = ecs_world_new();
    ecs_entity entity = ecs_world_create_entity(ecs);
    if (ecs_world_apply_commands(ecs) != ECS_OK) {
        return false;
    }
    *owner = ecs_entity_to_u64(entity);
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, NULL, 0, NULL, 0);
    behavior_function_builder_origin(
        builder,
        (
            behavior_function_source
        ){ .asset_name = "Observed", .start = 10, .end = 30, .line = 2, .column = 1 }
    );
    behavior_function_builder_source_point(
        builder,
        (
            behavior_source_point
        ){ .occurrence = 101, .start = 10, .end = 20, .line = 2, .kind = BEHAVIOR_SOURCE_STATEMENT }
    );
    behavior_function_builder_source_point(
        builder,
        (
            behavior_source_point
        ){ .occurrence = 102, .start = 21, .end = 30, .line = 3, .kind = BEHAVIOR_SOURCE_STATEMENT }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = 0 }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = 1 }
    );
    if (failing) {
        static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
        int host = behavior_function_builder_host(builder, &failing_call, &access, NULL, 0, NULL, 0);
        if (!behavior_function_builder_host_identity(builder, host, 2003, 2004)) {
            return false;
        }
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_HOST_CALL,
                                             .target = host,
                                             .left = BEHAVIOR_FUNCTION_HOST_DATA }
        );
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    builder = NULL;
    if (!body) {
        fprintf(stderr, "debug function: %s\n", error);
        return false;
    }
    plan = execution_module_plan_new();
    plan->id = 2001;
    plan->valid = true;
    apush(plan->events, ((execution_module_event_declaration){ .id = 1001, .abi = 1002, .name = "Tick" }));
    plan->event_count = 1;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 2002,
                                      .event = 1001,
                                      .event_abi = 1002,
                                      .kind = EXECUTION_FUNCTION_HANDLER,
                                      .body = body })
    );
    plan->function_count = 1;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!module) {
        fprintf(stderr, "debug module: %s\n", error);
        return false;
    }
    registry = source_registry_new(ecs);
    if (source_register(registry, "Observed", module, NULL)) {
        return false;
    }
    execution_program *p = source_program(registry, "Observed");
    if (!execution_program_attach(p, *owner)) {
        return false;
    }
    debug = workspace_debug_new(resolve, NULL);
    workspace_debug_set_scope(debug, enter, leave);
    workspace_debug_set_source_root(debug, source_root);
    return true;
}
TEST_CASE(debugger_controls_exact_activation_and_keeps_existing_run_to_breakpoint)
{
    uint64_t owner;
    REQUIRE(fixture(&owner, false));
    char error[256];
    int handle = 0;
    REQUIRE(source_program_count(registry) == 1);
    REQUIRE(workspace_debug_open(debug, 7, "Observed", owner, &handle, error, sizeof error));
    int duplicate = 0;
    REQUIRE(!workspace_debug_open(debug, 7, "Observed", owner, &duplicate, error, sizeof error));
    REQUIRE(
        workspace_debug_control(debug, handle, "breakpoint", 2001, 2002, 101, true, error, sizeof error)
    );
    REQUIRE(
        workspace_debug_control(debug, handle, "breakpoint", 2001, 2002, 102, true, error, sizeof error)
    );
    execution_program *p = source_program(registry, "Observed");
    execution_instance *instance = execution_program_instance(p, owner);
    REQUIRE(execution_instance_dispatch(instance, 1001, NULL, 0, 0, 0, 1000) == FLOW_OK);
    workspace_debug_view view;
    REQUIRE(workspace_debug_read(debug, handle, &view));
    REQUIRE(view.cursor.point.occurrence == 101 && view.live);
    REQUIRE(
        workspace_debug_control(debug, handle, "run-to", 2001, 2002, 102, true, error, sizeof error)
    );
    REQUIRE(workspace_debug_read(debug, handle, &view));
    REQUIRE(view.cursor.point.occurrence == 102);
    REQUIRE(execution_inspector_has_breakpoint(view.inspector, 2001, 2002, 102));
    REQUIRE(workspace_debug_control(debug, handle, "resume", 0, 0, 0, false, error, sizeof error));
    REQUIRE(!execution_program_paused(p));
    REQUIRE(entered == left && scope_ok);
    return true;
}
TEST_CASE(debugger_retains_observed_generation_after_runtime_retirement)
{
    snprintf(frozen_root, sizeof frozen_root, "%s/debug-source-XXXXXX", tool_root());
    REQUIRE(mkdtemp(frozen_root));
    char path[1200];
    snprintf(path, sizeof path, "%s/content", frozen_root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/behaviors", frozen_root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/behaviors/Observed.chibi", frozen_root);
    const char *original = "behavior Observed {\n on Tick {}\n}\n";
    tool_write(path, original);
    uint64_t owner;
    REQUIRE(fixture(&owner, false));
    char error[256];
    int first = 0;
    REQUIRE(workspace_debug_open(debug, 7, "Observed", owner, &first, error, sizeof error));
    REQUIRE(workspace_debug_control(debug, first, "pause", 0, 0, 0, false, error, sizeof error));
    execution_instance *instance = execution_program_instance(source_program(registry, "Observed"), owner);
    REQUIRE(execution_instance_dispatch(instance, 1001, NULL, 0, 0, 0, 1000) == FLOW_OK);
    const char *source_path, *text;
    REQUIRE(
        workspace_debug_source(debug, first, "Observed", &source_path, &text, error, sizeof error)
    );
    REQUIRE(!strcmp(source_path, "content/behaviors/Observed.chibi") && !strcmp(text, original));
    tool_write(path, "behavior Observed { on Begin {} }");
    tool_remove_tree(frozen_root);
    frozen_root[0] = 0;
    source_registry_free(registry);
    registry = NULL;
    workspace_debug_view view;
    REQUIRE(workspace_debug_read(debug, first, &view));
    REQUIRE(!view.live && view.cursor.retired && view.cursor.point.occurrence == 101);
    REQUIRE(!workspace_debug_control(debug, first, "resume", 0, 0, 0, false, error, sizeof error));
    REQUIRE(
        workspace_debug_source(debug, first, "Observed", &source_path, &text, error, sizeof error)
    );
    REQUIRE(!strcmp(text, original));
    REQUIRE(workspace_debug_close(debug, first));
    REQUIRE(!workspace_debug_close(debug, first));
    REQUIRE(!workspace_debug_read(debug, first, &view));
    REQUIRE(entered == left && scope_ok);
    return true;
}
TEST_CASE(debugger_retains_runtime_failure_after_program_clear_and_retirement)
{
    uint64_t owner;
    REQUIRE(fixture(&owner, true));
    char error[256];
    int handle = 0;
    REQUIRE(workspace_debug_open(debug, 7, "Observed", owner, &handle, error, sizeof error));
    execution_program *p = source_program(registry, "Observed");
    execution_instance *instance = execution_program_instance(p, owner);
    REQUIRE(execution_instance_dispatch(instance, 1001, NULL, 0, 0, 0, 1000) < 0);
    workspace_debug_view view;
    REQUIRE(workspace_debug_read(debug, handle, &view));
    REQUIRE(view.failure && view.failure->owner == owner);
    REQUIRE(view.failure->module_id == 2001 && view.failure->function_id == 2002);
    REQUIRE(!strcmp(view.failure->message, "intentional debugger failure"));
    execution_program_clear_error(p);
    source_registry_free(registry);
    registry = NULL;
    REQUIRE(workspace_debug_read(debug, handle, &view));
    REQUIRE(
        !view.live && view.failure &&
        !strcmp(view.failure->message, "intentional debugger failure")
    );
    REQUIRE(
        workspace_debug_control(debug, handle, "clear-error", 0, 0, 0, false, error, sizeof error)
    );
    REQUIRE(workspace_debug_read(debug, handle, &view) && !view.failure);
    REQUIRE(entered == left && scope_ok);
    return true;
}
TEST_CASE(test_tool_workspace_debug_missing_host_is_optional_and_refuses_without_effects)
{
    pin_value output[32], unchanged[32];
    memset(output, 0xa5, sizeof output);
    memcpy(unchanged, output, sizeof output);
    for (int i = 0; i < WORKSPACE_DEBUG_DEFINITION_PACK.binding_count; i++) {
        const node_host_call *call = WORKSPACE_DEBUG_DEFINITION_PACK.bindings[i]
                                         .implementation.call;
        REQUIRE(call && call->optional_service && call->context);
        const char *error = call->context(NULL, NULL, NULL, output);
        REQUIRE(error && strstr(error, "authoring host"));
        REQUIRE(!memcmp(output, unchanged, sizeof output));
    }
    return true;
}
static const char *activity_copy(void *storage, const char *text)
{
    char *copy = arena_alloc(storage, strlen(text) + 1);
    strcpy(copy, text);
    return copy;
}
TEST_CASE(debugger_activity_snapshot_requires_exact_generation_owner_and_survives_retirement)
{
    uint64_t owner;
    REQUIRE(fixture(&owner, false));
    char error[256];
    int handle = 0;
    REQUIRE(workspace_debug_open(debug, 7, "Observed", owner, &handle, error, sizeof error));
    execution_program *program = source_program(registry, "Observed");
    execution_instance *instance = execution_program_instance(program, owner);
    REQUIRE(execution_instance_dispatch(instance, 1001, NULL, 0, 0, 0, 1000) == FLOW_OK);
    workspace_debug_view view;
    REQUIRE(workspace_debug_read(debug, handle, &view));
    REQUIRE(view.cursor.generation != 0);
    behavior_function_context context = { .value_storage = &activity_storage,
                                          .copy_string = activity_copy };
    const node_host_call *call = WORKSPACE_DEBUG_DEFINITION_PACK.bindings[6]
                                     .implementation.call;
    pin_value input[] = { { .i = handle },
                          { .u64 = view.cursor.generation },
                          { .u64 = owner } },
              output[5] = { 0 };
    REQUIRE(!call->context(&context, debug, input, output));
    REQUIRE(output[0].b && !*output[4].str && !output[3].b);
    pin_array *rows = output[1].ptr;
    REQUIRE(rows->count == 2);
    workspace_debug_activity *edge = NULL;
    for (int i = 0; i < rows->count; i++) {
        workspace_debug_activity *row = rows->items[i].ptr;
        REQUIRE(
            row->generation == input[1].u64 && row->owner == owner &&
            row->module == 2001 && row->function == 2002
        );
        if (row->count != 1 || strcmp(row->source, "Observed")) {
            fprintf(
                stderr,
                "activity occurrence=%llu count=%llu source=%s\n",
                (unsigned long long)row->occurrence,
                (unsigned long long)row->count,
                row->source
            );
        }
        REQUIRE(row->count == 1 && !strcmp(row->source, "Observed"));
        if (row->occurrence == 102) {
            edge = row;
        }
    }
    REQUIRE(
        edge && edge->from_module == 2001 && edge->from_function == 2002 &&
        edge->from_occurrence == 101
    );
    input[1].u64++;
    REQUIRE(!call->context(&context, debug, input, output));
    REQUIRE(
        !output[0].b && ((pin_array *)output[1].ptr)->count == 0 &&
        strstr(output[4].str, "generation")
    );
    input[1].u64--;
    input[2].u64++;
    REQUIRE(!call->context(&context, debug, input, output));
    REQUIRE(
        !output[0].b && ((pin_array *)output[1].ptr)->count == 0 &&
        strstr(output[4].str, "owner")
    );
    input[2].u64--;
    REQUIRE(execution_instance_dispatch(instance, 1001, NULL, 0, 0, 0, 1000) == FLOW_OK);
    REQUIRE(edge->count == 1 && !strcmp(edge->source, "Observed"));
    source_registry_free(registry);
    registry = NULL;
    REQUIRE(!call->context(&context, debug, input, output));
    REQUIRE(output[0].b && output[3].b && ((pin_array *)output[1].ptr)->count >= 2);
    bool twice = false;
    pin_array *retained = output[1].ptr;
    for (int i = 0; i < retained->count; i++) {
        const workspace_debug_activity *row = retained->items[i].ptr;
        twice |= row->occurrence == 102 && row->count == 2;
    }
    REQUIRE(twice && edge->count == 1);
    REQUIRE(workspace_debug_close(debug, handle));
    REQUIRE(!call->context(&context, debug, input, output));
    REQUIRE(
        !output[0].b && ((pin_array *)output[1].ptr)->count == 0 &&
        strstr(output[4].str, "Invalid")
    );
    REQUIRE(edge->count == 1 && !strcmp(edge->source, "Observed"));
    arena_free(&activity_storage);
    return true;
}
static uint64_t mock_creation;
static bool owner_metadata(void *, int session, uint64_t owner, workspace_debug_owner *out)
{
    if (session != 7 || !owner) {
        return false;
    }
    *out = (workspace_debug_owner){ .prototype = "Proven",
                                    .creation = mock_creation,
                                    .object = 2 };
    return true;
}
TEST_CASE(debugger_owner_chronology_refuses_unrepresentable_unsigned_sequences)
{
    uint64_t owner;
    REQUIRE(fixture(&owner, false));
    workspace_debug_set_owner_resolver(debug, owner_metadata);
    const type_field *field = NULL;
    for (int i = 0; i < WORKSPACE_DEBUG_ROW_LAYOUT.field_count; i++) {
        if (!strcmp(WORKSPACE_DEBUG_ROW_LAYOUT.fields[i].name, "creation")) {
            field = &WORKSPACE_DEBUG_ROW_LAYOUT.fields[i];
        }
    }
    REQUIRE(field && field->type == PIN_I64 && field->size == sizeof(int64_t));
    workspace_debug_owner view;
    mock_creation = INT64_MAX;
    REQUIRE(workspace_debug_owner_read(debug, 7, owner, &view));
    REQUIRE(
        view.creation == (uint64_t)INT64_MAX &&
        !strcmp(view.prototype, "Proven") && view.object == 2
    );
    mock_creation = (uint64_t)INT64_MAX + 1;
    REQUIRE(!workspace_debug_owner_read(debug, 7, owner, &view));
    REQUIRE(!view.prototype && !view.creation);
    mock_creation = UINT64_MAX;
    REQUIRE(!workspace_debug_owner_read(debug, 7, owner, &view));
    REQUIRE(!view.prototype && !view.creation);
    mock_creation = 1;
    REQUIRE(!workspace_debug_owner_read(debug, 8, owner, &view));
    REQUIRE(!view.prototype && !view.creation);
    return true;
}
static entities *resolve_entities(void *, int session)
{
    return session == 7 ? prototype_entities : NULL;
}
TEST_CASE(debugger_prototype_inventory_is_authoritative_before_first_spawn_and_owns_snapshot_rows)
{
    uint64_t owner;
    REQUIRE(fixture(&owner, false));
    prototype_entities = entities_new(ecs, NULL);
    const char *behaviors[] = { "Observed" };
    scene_object object = { .name = "Root", .behaviors = behaviors, .behavior_count = 1 };
    char error[256];
    scene *prototype = scene_prepare(
        &(scene_spec){ .name = "DisplayOnly",
                       .physics = physics_defaults(),
                       .objects = &object,
                       .object_count = 1 },
        error,
        sizeof error
    );
    REQUIRE(prototype);
    REQUIRE(!entities_register_prepared(prototype_entities, "nested/Canonical", prototype));
    scene_free(prototype);
    workspace_debug_set_entities(debug, resolve_entities);
    behavior_function_context context = { .value_storage = &activity_storage,
                                          .copy_string = activity_copy };
    const node_host_call *query = WORKSPACE_DEBUG_DEFINITION_PACK.bindings[7]
                                      .implementation.call;
    pin_value inputs[] = { { .i = 7 }, { .i = 0 }, { .i = 1 }, { .str = "canonical" } },
              outputs[3] = { 0 };
    REQUIRE(!query->context(&context, debug, inputs, outputs));
    REQUIRE(!outputs[2].str[0] && outputs[1].i == 1);
    const pin_array *rows = outputs[0].ptr;
    REQUIRE(rows->count == 1);
    const workspace_debug_row *row = rows->items[0].ptr;
    REQUIRE(
        !strcmp(row->kind, "prototype") && !strcmp(row->prototype, "nested/Canonical") &&
        !strcmp(row->name, "Observed") && row->object == 0 && !row->owner && !row->creation
    );
    object.behaviors = NULL;
    object.behavior_count = 0;
    prototype = scene_prepare(
        &(scene_spec){ .name = "BareDisplay",
                       .physics = physics_defaults(),
                       .objects = &object,
                       .object_count = 1 },
        error,
        sizeof error
    );
    REQUIRE(prototype);
    REQUIRE(!entities_register_prepared(prototype_entities, "bare/Canonical", prototype));
    scene_free(prototype);
    inputs[3].str = "bare/Canonical";
    REQUIRE(!query->context(&context, debug, inputs, outputs));
    REQUIRE(
        !*outputs[2].str && outputs[1].i == 1 && ((pin_array *)outputs[0].ptr)->count == 1
    );
    const workspace_debug_row *bare = ((pin_array *)outputs[0].ptr)->items[0].ptr;
    REQUIRE(!strcmp(bare->prototype, "bare/Canonical") && !bare->name[0] && bare->object == 0);
    const char *missing[] = { "Missing" };
    object.behaviors = missing;
    object.behavior_count = 1;
    prototype = scene_prepare(
        &(scene_spec){ .name = "Unavailable",
                       .physics = physics_defaults(),
                       .objects = &object,
                       .object_count = 1 },
        error,
        sizeof error
    );
    REQUIRE(prototype);
    REQUIRE(!entities_register_prepared(prototype_entities, "missing/Canonical", prototype));
    scene_free(prototype);
    inputs[3].str = "Missing";
    REQUIRE(!query->context(&context, debug, inputs, outputs));
    REQUIRE(
        !*outputs[2].str && outputs[1].i == 1 && ((pin_array *)outputs[0].ptr)->count == 1
    );
    const workspace_debug_row
        *unresolved = ((pin_array *)outputs[0].ptr)->items[0].ptr;
    REQUIRE(
        !strcmp(unresolved->prototype, "missing/Canonical") &&
        !unresolved->name[0] && strstr(unresolved->text, "unavailable")
    );
    inputs[3].str = "canonical";
    const node_host_call *spawns = WORKSPACE_DEBUG_DEFINITION_PACK.bindings[8]
                                       .implementation.call;
    pin_value spawn_inputs[] = { { .i = 7 },
                                 { .str = "nested/Canonical" },
                                 { .i = 0 },
                                 { .i64 = 0 } },
              spawn_outputs[2] = { 0 };
    REQUIRE(!spawns->context(&context, debug, spawn_inputs, spawn_outputs));
    REQUIRE(!spawn_outputs[1].str[0] && ((pin_array *)spawn_outputs[0].ptr)->count == 0);
    inputs[1].i = -1;
    REQUIRE(!query->context(&context, debug, inputs, outputs));
    REQUIRE(
        *outputs[2].str && ((pin_array *)outputs[0].ptr)->count == 0 &&
        outputs[1].i == 0
    );
    inputs[1].i = 0;
    inputs[2].i = 257;
    REQUIRE(!query->context(&context, debug, inputs, outputs));
    REQUIRE(*outputs[2].str);
    inputs[2].i = 0;
    REQUIRE(!query->context(&context, debug, inputs, outputs));
    REQUIRE(
        !*outputs[2].str && outputs[1].i == 3 && ((pin_array *)outputs[0].ptr)->count == 0
    );
    spawn_inputs[3].i64 = -1;
    REQUIRE(!spawns->context(&context, debug, spawn_inputs, spawn_outputs));
    REQUIRE(*spawn_outputs[1].str && ((pin_array *)spawn_outputs[0].ptr)->count == 0);
    source_registry_free(registry);
    registry = NULL;
    inputs[2].i = 1;
    REQUIRE(!query->context(&context, debug, inputs, outputs));
    REQUIRE(strstr(outputs[2].str, "committed") && ((pin_array *)outputs[0].ptr)->count == 0);
    entities_free(prototype_entities);
    prototype_entities = NULL;
    REQUIRE(!strcmp(row->prototype, "nested/Canonical") && !strcmp(row->name, "Observed"));
    return true;
}
void test_tool_workspace_debug()
{
    pu_setup(NULL, cleanup);
    scope_ok = true;
    RUN_TEST_CASE(debugger_controls_exact_activation_and_keeps_existing_run_to_breakpoint);
    RUN_TEST_CASE(debugger_retains_observed_generation_after_runtime_retirement);
    RUN_TEST_CASE(debugger_retains_runtime_failure_after_program_clear_and_retirement);
    RUN_TEST_CASE(test_tool_workspace_debug_missing_host_is_optional_and_refuses_without_effects);
    RUN_TEST_CASE(debugger_activity_snapshot_requires_exact_generation_owner_and_survives_retirement);
    RUN_TEST_CASE(debugger_owner_chronology_refuses_unrepresentable_unsigned_sequences);
    RUN_TEST_CASE(
        debugger_prototype_inventory_is_authoritative_before_first_spawn_and_owns_snapshot_rows
    );
    pu_clear_setup();
}
