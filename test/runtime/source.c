#include "assets/prototype.h"
#include "authoring/language_catalog.h"
#include "execution/module.h"
#include "execution/status.h"
#include "host/native_default.h"
#include "runtime/behaviors/live.h"
#include "runtime/components.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

extern const ecs_component_desc GAME_COMPONENT_GameTransform;
static ecs_world *ecs;
static game_live_world *live;
static authoring_language_catalog *catalog;
static game_prototype *prototype;
static uint64_t counter;

static void cleanup()
{
    game_prototype_free(prototype);
    if (live) {
        game_live_world_free(live);
    }
    ecs_world_free(ecs);
    authoring_language_catalog_free(catalog);
    prototype = NULL;
    live = NULL;
    ecs = NULL;
    catalog = NULL;
    counter = 0;
}
static execution_module *compile_source(const char *text, behavior_backend backend)
{
    script_syntax *syntax = script_parse(text);
    script_analysis *analysis = script_analyze(syntax, authoring_language_catalog_view(catalog));
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "source lifecycle semantic: %s\n",
            analysis->diagnostics[i].message
        );
    }
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, "count")) {
            counter = analysis->symbols[i].id;
        }
    }
    script_lower_bindings bindings = authoring_language_bindings(catalog);
    execution_module_plan
        *plan = script_lower(analysis, authoring_language_catalog_view(catalog), &bindings);
    for (int i = 0; i < plan->diagnostic_count; i++) {
        fprintf(stderr, "source lifecycle lower: %s\n", plan->diagnostics[i].message);
    }
    char error[256];
    execution_module *module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "source lifecycle compilation: %s\n", error);
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return module;
}
static bool register_source(const char *name, const char *text, behavior_backend backend)
{
    execution_module *module = compile_source(text, backend);
    if (!module) {
        return false;
    }
    char error[256];
    int result = game_live_register_module(live, name, module, error, sizeof error);
    if (result) {
        fprintf(stderr, "source lifecycle registration: %s\n", error);
    }
    execution_module_free(module);
    return result == 0;
}
static int attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
}
static execution_instance *instance(ecs_entity entity)
{
    return execution_program_instance(
        game_live_module_program(live, "source.lifecycle"),
        ecs_entity_to_u64(entity)
    );
}
static float count(ecs_entity entity)
{
    const pin_value *value = execution_instance_state(instance(entity), counter);
    return value && value->type == PIN_F32 ? value->f : -1;
}
TEST_CASE(source_modules_follow_native_prototype_and_update_lifecycle)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        script_catalog_parameter amount = { .name = "amount",
                                            .type = { .storage = PIN_F32 } };
        script_catalog_capability events[] = {
            { .id = script_catalog_id("event", "Pulse"),
              .name = "Pulse",
              .kind = SCRIPT_CAPABILITY_EVENT,
              .parameters = &amount,
              .parameter_count = 1 },
            { .id = script_catalog_id("event", "Wait"),
              .name = "Wait",
              .kind = SCRIPT_CAPABILITY_EVENT },
        };
        script_catalog project = { .capabilities = events, .capability_count = 2 };
        char error[256];
        native_capabilities *native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
        REQUIRE(native != NULL);
        authoring_language_catalog_input input = { .native = native,
                                                   .project = &project };
        catalog = authoring_language_catalog_new(&input, error, sizeof error);
        native_capabilities_free(native);
        REQUIRE(catalog != NULL);
        ecs = ecs_world_new();
        REQUIRE(ecs_world_register_component(ecs, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
        live = game_live_world_new_modules(ecs, 0);
        REQUIRE(live != NULL);
        REQUIRE(register_source(
            "source.lifecycle",
            "behavior Lifecycle { count: float = 0; "
            "on Begin { count += 1; } on Tick(dt) { count += dt; } "
            "on Pulse(amount) { count += amount; } "
            "on Wait { await Delay(10); count += 100; } }",
            backends[b]
        ));
        REQUIRE(
            register_source("source.binding", "behavior Binding { bind self.Transform.y = self.Transform.x * 2; }", backends[b])
        );
        REQUIRE(register_source(
            "source.drift",
            "system Drift(transform: Transform) { shared runs:float=0; "
            "runs+=1; transform.x += runs; }",
            backends[b]
        ));
        prototype = game_prototype_load(
            "name \"SourceFixture\"\n(array)behaviors \"source.lifecycle\"\n",
            &(game_prototype_registry){ 0 },
            error,
            sizeof error
        );
        REQUIRE(prototype != NULL);
        game_behavior_set_deferred(live->behaviors, true);
        ecs_entity entity;
        game_prototype_spawn_hooks hooks = { .attach = attach,
                                             .attach_user = live->behaviors };
        REQUIRE(game_prototype_spawn(prototype, ecs, &hooks, &entity, error, sizeof error) == 0);
        REQUIRE(count(entity) == 0);
        REQUIRE(game_behavior_attach(live->behaviors, entity, "source.binding") == 0);
        GameTransform transform = { .x = 1 };
        REQUIRE(
            ecs_world_record_add(ecs, entity, GAME_COMPONENT_GameTransform.id, &transform, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
        game_behavior_set_deferred(live->behaviors, false);
        REQUIRE(game_behavior_begin_pending(live->behaviors) == 0);
        REQUIRE(count(entity) == 1);
        REQUIRE(game_behavior_begin_pending(live->behaviors) == 0);
        REQUIRE(count(entity) == 1);
        game_behavior_set_time(live->behaviors, 0.5);
        game_behavior_set_delta_seconds(live->behaviors, 0.5);
        flow_error failure;
        REQUIRE(game_behavior_update(live->behaviors, &failure) >= 0);
        REQUIRE(count(entity) == 1.5f);
        const GameTransform *row = ecs_component_data_const(
            ecs,
            entity,
            GAME_COMPONENT_GameTransform.id
        );
        REQUIRE(row && row->x == 2 && row->y == 4);
        pin_value pulse = { .type = PIN_F32, .f = 3 };
        REQUIRE(
            game_behavior_dispatch(
                live->behaviors,
                entity,
                "Pulse",
                (flow_inputs){ .values = &pulse, .count = 1 },
                &failure
            ) >= 0
        );
        REQUIRE(count(entity) == 4.5f);
        execution_module *replacement = compile_source(
            "behavior Lifecycle { count: float = 999; "
            "on Begin { count += 1; } on Tick(dt) { count += dt; } "
            "on Pulse(amount) { count += amount * 2; } "
            "on Wait { await Delay(10); count += 100; } }",
            backends[b]
        );
        REQUIRE(replacement != NULL);
        int replaced = game_live_replace_module(live, "source.lifecycle", replacement, false, error, sizeof error);
        execution_module_free(replacement);
        REQUIRE(replaced == 0 && count(entity) == 4.5f);
        pulse.f = 2;
        REQUIRE(
            game_behavior_dispatch(
                live->behaviors,
                entity,
                "Pulse",
                (flow_inputs){ .values = &pulse, .count = 1 },
                &failure
            ) >= 0
        );
        REQUIRE(count(entity) == 8.5f);
        REQUIRE(
            game_behavior_dispatch(live->behaviors, entity, "Wait", (flow_inputs){ 0 }, &failure) >= 0
        );
        REQUIRE(execution_instance_pending(instance(entity)) == 1);
        REQUIRE(game_behavior_detach_asset(live->behaviors, entity, "source.lifecycle") == 1);
        REQUIRE(instance(entity) == NULL);
        REQUIRE(game_behavior_pump(live->behaviors, 20) >= 0);
        REQUIRE(instance(entity) == NULL);
        row = ecs_component_data_const(ecs, entity, GAME_COMPONENT_GameTransform.id);
        REQUIRE(row && row->x == 2 && row->y == 4);
        REQUIRE(game_live_remove_module(live, "source.lifecycle") == 0);
        REQUIRE(!game_live_has_module(live, "source.lifecycle"));
        game_source_registry *sources = game_behavior_sources(live->behaviors);
        REQUIRE(game_source_is_system(sources, "source.drift"));
        REQUIRE(!game_source_is_system(sources, "source.binding"));
        REQUIRE(game_source_select_systems(sources, NULL, 0) == 0);
        REQUIRE(game_source_step(sources, 21, .5) >= 0);
        row = ecs_component_data_const(ecs, entity, GAME_COMPONENT_GameTransform.id);
        REQUIRE(row && row->x == 2 && row->y == 4);
        const char *selected[] = { "source.drift" };
        REQUIRE(game_source_select_systems(sources, selected, 1) == 0);
        REQUIRE(game_source_step(sources, 22, .5) >= 0);
        REQUIRE(row->x == 2 && row->y == 4);
        const char *invalid[] = { "source.binding" };
        REQUIRE(game_source_select_systems(sources, invalid, 1) < 0);
        REQUIRE(game_source_run_system(sources, "source.drift", 22, .5) >= 0);
        REQUIRE(row->x == 4 && row->y == 4);
        REQUIRE(game_source_maintain(sources, 22, .5) >= 0);
        REQUIRE(row->y == 8);
        REQUIRE(game_source_select_systems(sources, NULL, 0) == 0);
        REQUIRE(game_source_run_system(sources, "source.drift", 23, .5) < 0);
        REQUIRE(game_source_pump(sources, 23, .5) >= 0);
        REQUIRE(game_source_maintain(sources, 23, .5) >= 0);
        REQUIRE(row->x == 4 && row->y == 8);
    }
    return true;
}
void test_source_lifecycle()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_modules_follow_native_prototype_and_update_lifecycle);
    pu_clear_setup();
}
