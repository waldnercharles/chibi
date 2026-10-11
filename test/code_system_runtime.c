#include "authoring/language_catalog.h"
#include "authoring/preview_runtime.h"
#include "authoring/source_format.h"
#include "authoring/source_prepare.h"
#include "execution/status.h"
#include "host/native_default.h"
#include "runtime/component_catalog.h"
#include "runtime/source_input.h"
#include "runtime/systems/scheduler.h"
#include "support/array.h"
#include "support/text_file.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ecs_world *world;
    preview_runtime *live;
    authoring_language_catalog *catalog;
    authoring_source *source;
    execution_program *program;
} system_fixture;

static system_fixture active;
static char *formatted;
static ecs_world *input_world;
static source_registry *input_sources;
static engine_input *test_source_input;
static authoring_language_catalog *input_catalog;
static authoring_source *input_source;

static system_fixture fixture_new()
{
    system_fixture f = { .world = ecs_world_new() };
    const ecs_component_desc *components[] = {
        &COMPONENT_Health,    &COMPONENT_Aim,      &COMPONENT_Collider,
        &COMPONENT_Transform, &COMPONENT_Movement, &COMPONENT_Input
    };
    for (int i = 0; i < 6; i++) {
        assert(!ecs_world_register_component(f.world, components[i], NULL));
    }
    f.live = preview_runtime_new(f.world);
    const script_catalog_capability child = { .id = 100001,
                                              .name = "Child",
                                              .kind = SCRIPT_CAPABILITY_PROTOTYPE,
                                              .completion_outcome = -1 };
    const script_catalog project = { .capabilities = &child, .capability_count = 1 };
    char error[256];
    native_capabilities *table = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    assert(table);
    const authoring_language_catalog_input input = { .native = table,
                                                     .project = &project };
    f.catalog = authoring_language_catalog_new(&input, error, sizeof error);
    native_capabilities_free(table);
    if (!f.catalog) {
        fprintf(stderr, "system catalog: %s\n", error);
    }
    assert(f.catalog);
    active = f;
    return f;
}
static bool prepare(system_fixture *f, const char *text, behavior_backend backend)
{
    script_lower_bindings bindings = authoring_language_bindings(f->catalog);
    f->source = authoring_source_prepare_document(
        "System.chibi",
        "systems/System.chibi",
        text,
        authoring_language_catalog_view(f->catalog),
        &bindings,
        backend
    );
    active = *f;
    for (int i = 0; i < f->source->analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "system semantic: %s\n",
            f->source->analysis->diagnostics[i].message
        );
    }
    for (int i = 0; i < f->source->diagnostic_count; i++) {
        fprintf(stderr, "system preparation: %s\n", f->source->diagnostics[i].message);
    }
    if (!f->source->valid) {
        return false;
    }
    service_binding *services = NULL;
    apush(services, ((service_binding){ "game.entities", f->live->entities }));
    apush(services, ((service_binding){ "ecs.world", f->world }));
    int result = source_register(f->live->sources, "System", f->source->module, services);
    afree(services);
    if (result) {
        fprintf(stderr, "system publication: %s\n", source_error(f->live->sources));
        fprintf(stderr, "system registry: %s\n", source_error(f->live->sources));
        int count = 0;
        const char *const *services = execution_module_services(f->source->module, &count);
        for (int i = 0; i < count; i++) {
            fprintf(stderr, "system required service: %s\n", services[i]);
        }
    }
    if (result) {
        return false;
    }
    const char *selected[] = { "System" };
    result = source_is_system(f->live->sources, "System")
               ? source_select_systems(f->live->sources, selected, 1)
               : 0;
    if (result < 0) {
        fprintf(stderr, "system selection: %s\n", source_error(f->live->sources));
        return false;
    }
    f->program = source_program(f->live->sources, "System");
    return f->program != NULL;
}
static void fixture_free(system_fixture *f)
{
    preview_runtime_free(f->live);
    authoring_source_free(f->source);
    authoring_language_catalog_free(f->catalog);
    ecs_world_free(f->world);
    active = (system_fixture){ 0 };
}
static bool tick(system_fixture *f, double now)
{
    int result = execution_program_step(f->program, now, .1, 10000);
    if (result < 0) {
        const execution_program_failure *error = execution_program_error(f->program);
        fprintf(stderr, "system execution: %s\n", error ? error->message : "unknown failure");
    }
    return result >= 0;
}
static execution_instance *instance(system_fixture *f, ecs_entity owner)
{
    return execution_program_instance(f->program, ecs_entity_to_u64(owner));
}
static double state(system_fixture *f, ecs_entity owner, const char *name)
{
    uint64_t id = 0;
    for (int i = 0; i < f->source->analysis->symbol_count; i++) {
        const script_symbol *symbol = &f->source->analysis->symbols[i];
        if ((symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE) &&
            !strcmp(symbol->name, name)) {
            id = symbol->id;
            break;
        }
    }
    if (!id || !instance(f, owner)) {
        fprintf(
            stderr,
            "system state '%s': missing declared state or query owner %llu\n",
            name,
            (unsigned long long)ecs_entity_to_u64(owner)
        );
        return NAN;
    }
    const pin_value *value = execution_instance_state(instance(f, owner), id);
    assert(value && value->type == PIN_F64);
    return value->d;
}
static ecs_entity health(system_fixture *f)
{
    ecs_entity owner = ecs_world_create_entity(f->world);
    assert(!Health_add(f->world, owner, 0));
    assert(!ecs_world_apply_commands(f->world));
    Health_get(f->world, owner)->current = 0;
    return owner;
}

static bool persistent_wait_membership(behavior_backend backend)
{
    system_fixture f = fixture_new();
    for (int i = 0; i < 256; i++) {
        ecs_world_create_entity(f.world);
    }
    ecs_entity a = health(&f), b = health(&f);
    REQUIRE(prepare(
        &f,
        "system Counters(health:Health) { state count:double=0; shared "
        "total:double=0; "
        "count+=1; total+=1; await delay(Duration:1); count+=10; total+=10; }",
        backend
    ));
    REQUIRE(tick(&f, 0));
    REQUIRE(instance(&f, a) && instance(&f, b));
    REQUIRE(execution_instance_pending(instance(&f, a)) == 1);
    REQUIRE(state(&f, a, "count") == 1 && state(&f, b, "count") == 1);
    REQUIRE(state(&f, b, "total") == 2);
    REQUIRE(tick(&f, .25));
    REQUIRE(state(&f, b, "total") == 2);
    REQUIRE(!Health_remove(f.world, a, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(tick(&f, .5));
    REQUIRE(!instance(&f, a));
    REQUIRE(!Health_add(f.world, a, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(tick(&f, .75));
    REQUIRE(state(&f, a, "count") == 1 && state(&f, b, "total") == 3);
    REQUIRE(execution_program_pump(f.program, 1.1, .1, 10000) >= 0);
    REQUIRE(state(&f, b, "count") == 11 && state(&f, b, "total") == 13);
    REQUIRE(tick(&f, 1.2));
    REQUIRE(state(&f, b, "count") == 12);
    REQUIRE(!ecs_world_destroy_entity(f.world, a));
    ecs_entity replacement = health(&f);
    REQUIRE(replacement.slot == a.slot && replacement.generation != a.generation);
    REQUIRE(tick(&f, 1.3));
    REQUIRE(!instance(&f, a) && state(&f, replacement, "count") == 1);
    fixture_free(&f);
    return true;
}
static bool register_child(system_fixture *f)
{
    char error[256];
    const scene_object object = { .name = "Child" };
    scene *child = scene_prepare(
        &(scene_spec){ .name = "Child",
                       .physics = physics_defaults(),
                       .objects = &object,
                       .object_count = 1 },
        error,
        sizeof error
    );
    if (!child) {
        fprintf(stderr, "system prototype preparation: %s\n", error);
    }
    bool registered = child &&
                      !entities_register_prepared(f->live->entities, "Child", child);
    scene_free(child);
    return registered;
}
static int live_children(system_fixture *f)
{
    ecs_entity *children = NULL;
    entities_copy_live(f->live->entities, &children);
    int count = 0;
    for (int i = 0; i < acount(children); i++) {
        count += ecs_entity_is_alive(f->world, children[i]);
    }
    afree(children);
    return count;
}
static bool host_spawn_resume(behavior_backend backend)
{
    system_fixture f = fixture_new();
    REQUIRE(register_child(&f));
    ecs_entity owner = health(&f);
    REQUIRE(prepare(
        &f,
        "system Spawner(health:Health) { state completed:double=0; shared "
        "total:double=0; "
        "await spawn(Prototype:\"Child\"); completed+=1; total+=1; }",
        backend
    ));
    REQUIRE(tick(&f, 0));
    REQUIRE(execution_instance_pending(instance(&f, owner)) == 1);
    REQUIRE(state(&f, owner, "completed") == 0);
    REQUIRE(entities_flush(f.live->entities) >= 0);
    REQUIRE(entities_flush_initializers(f.live->entities, 0) >= 0);
    REQUIRE(execution_program_pump(f.program, .1, .1, 10000) >= 0);
    REQUIRE(state(&f, owner, "completed") == 1 && state(&f, owner, "total") == 1);
    REQUIRE(tick(&f, .2));
    REQUIRE(execution_instance_pending(instance(&f, owner)) == 1);
    REQUIRE(!Health_remove(f.world, owner, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(tick(&f, .3));
    REQUIRE(!instance(&f, owner));
    REQUIRE(entities_flush(f.live->entities) >= 0);
    REQUIRE(live_children(&f) == 1);
    fixture_free(&f);
    return true;
}
/* A spawn delivered inside an awaited function keeps initializing after that
 * function returns, until the activation that called it finishes. */
static bool nested_spawn_outlives_frame(behavior_backend backend)
{
    system_fixture f = fixture_new();
    REQUIRE(register_child(&f));
    REQUIRE(prepare(
        &f,
        "behavior Spawner {\n"
        "    made := 0.0\n"
        "    function Make() {\n"
        "        await spawn(Child)\n"
        "        made += 1\n"
        "    }\n"
        "    on Begin {\n"
        "        await Make()\n"
        "        made += 1\n"
        "    }\n"
        "}\n",
        backend
    ));
    ecs_entity owner = ecs_world_create_entity(f.world);
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(!source_attach(f.live->sources, owner, "System", false, 0, 0));
    REQUIRE(entities_flush(f.live->entities) >= 0);
    REQUIRE(entities_flush_initializers(f.live->entities, 0) >= 0);
    REQUIRE(state(&f, owner, "made") == 2);
    REQUIRE(entities_flush(f.live->entities) >= 0);
    REQUIRE(live_children(&f) == 1);
    fixture_free(&f);
    return true;
}
static bool live_component_resume(behavior_backend backend)
{
    system_fixture f = fixture_new();
    ecs_entity owner = health(&f);
    Health_get(f.world, owner)->current = 2;
    REQUIRE(prepare(
        &f,
        "system ReadHealth(health:Health) { state observed:double=0; "
        "await delay(Duration:1); observed=health.Current; }",
        backend
    ));
    REQUIRE(tick(&f, 0));
    Health_get(f.world, owner)->current = 7;
    REQUIRE(tick(&f, 1.1));
    REQUIRE(state(&f, owner, "observed") == 7);
    fixture_free(&f);
    return true;
}
static bool optional_membership_roundtrip(behavior_backend backend)
{
    const char
        *text = "system Optional(health:Health, trigger:Aim?) "
                "without Collider { "
                "if trigger { if trigger.Firing { health.Current+=7; } } "
                "else { health.Current+=2; } }";
    script_syntax *syntax = script_parse(text);
    char error[256];
    char *saved = authoring_source_format(syntax, error, sizeof error);
    formatted = saved;
    script_syntax_free(syntax);
    REQUIRE(saved);
    for (int pass = 0; pass < 2; pass++) {
        system_fixture f = fixture_new();
        ecs_entity present = health(&f), absent = health(&f), excluded = health(&f);
        REQUIRE(!Aim_add(f.world, present, 0));
        REQUIRE(!Collider_add(f.world, excluded, 0));
        REQUIRE(!ecs_world_apply_commands(f.world));
        Aim_get(f.world, present)->firing = true;
        REQUIRE(prepare(&f, pass ? saved : text, backend));
        REQUIRE(tick(&f, 0));
        REQUIRE(instance(&f, present) && instance(&f, absent) && !instance(&f, excluded));
        REQUIRE(Health_get_const(f.world, present)->current == 7);
        REQUIRE(Health_get_const(f.world, absent)->current == 2);
        REQUIRE(Health_get_const(f.world, excluded)->current == 0);
        REQUIRE(!Aim_remove(f.world, present, 0));
        REQUIRE(!ecs_world_apply_commands(f.world));
        REQUIRE(tick(&f, .1));
        REQUIRE(Health_get_const(f.world, present)->current == 9);
        REQUIRE(Health_get_const(f.world, absent)->current == 4);
        REQUIRE(Health_get_const(f.world, excluded)->current == 0);
        fixture_free(&f);
    }
    free(saved);
    formatted = NULL;
    return true;
}
/* Lowers any(...) with a nested not(...) to an OR chain with a scoped NOT. */
static bool any_query_membership(behavior_backend backend)
{
    system_fixture f = fixture_new();
    ecs_entity bare = health(&f), aimed = health(&f), collider = health(&f);
    REQUIRE(!Aim_add(f.world, aimed, 0));
    REQUIRE(!Aim_add(f.world, collider, 0));
    REQUIRE(!Collider_add(f.world, collider, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(prepare(
        &f,
        "system AnyQuery(health:Health) { "
        "query any(has(Collider), not(has(Aim))); "
        "health.Current+=1; }",
        backend
    ));
    REQUIRE(tick(&f, 0));
    REQUIRE(instance(&f, bare) && !instance(&f, aimed) && instance(&f, collider));
    REQUIRE(Health_get_const(f.world, bare)->current == 1);
    REQUIRE(Health_get_const(f.world, aimed)->current == 0);
    REQUIRE(Health_get_const(f.world, collider)->current == 1);
    fixture_free(&f);
    return true;
}
static bool empty_any_query(behavior_backend backend)
{
    system_fixture f = fixture_new();
    ecs_entity bare = health(&f), collider = health(&f);
    REQUIRE(!Collider_add(f.world, collider, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(prepare(
        &f,
        "system EmptyAny(health:Health) { "
        "query any(has(Collider), any()); "
        "health.Current+=1; }",
        backend
    ));
    REQUIRE(tick(&f, 0));
    REQUIRE(!instance(&f, bare) && instance(&f, collider));
    fixture_free(&f);
    return true;
}
static bool reactive_checkpoint(behavior_backend backend)
{
    system_fixture f = fixture_new();
    ecs_entity owner = ecs_world_create_entity(f.world);
    REQUIRE(!Aim_add(f.world, owner, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(prepare(&f, "system Watch(aim:Aim) { bind aim.Manual=!aim.Firing; }", backend));
    REQUIRE(tick(&f, 0));
    REQUIRE(Aim_get(f.world, owner)->manual);
    Aim_get(f.world, owner)->firing = true;
    REQUIRE(tick(&f, .1));
    REQUIRE(!Aim_get(f.world, owner)->manual);
    Aim_get(f.world, owner)->firing = false;
    REQUIRE(tick(&f, .2));
    REQUIRE(Aim_get(f.world, owner)->manual);
    REQUIRE(!Aim_remove(f.world, owner, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(tick(&f, .3));
    REQUIRE(!instance(&f, owner));
    fixture_free(&f);
    return true;
}
static bool synchronous_batch_path(behavior_backend backend)
{
    system_fixture f = fixture_new();
    ecs_entity owner = ecs_world_create_entity(f.world);
    REQUIRE(!Transform_add(f.world, owner, 0));
    REQUIRE(!Movement_add(f.world, owner, 0));
    REQUIRE(!Input_add(f.world, owner, 0));
    REQUIRE(!ecs_world_apply_commands(f.world));
    REQUIRE(prepare(
        &f,
        "system Movement(position:Transform, movement:Movement, input:Input) { "
        "position.X+=input.Move.X*movement.Speed*dt; }",
        backend
    ));
    Input_get(f.world, owner)->move.x = 2;
    Movement_get(f.world, owner)->speed = 3;
    REQUIRE(tick(&f, 0));
    REQUIRE(fabs(Transform_get_const(f.world, owner)->x - .6) < .00001);
    fixture_free(&f);
    return true;
}
static bool camera_follow_runtime(behavior_backend backend)
{
    system_fixture f = fixture_new();
    char *text = text_file_read("test/fixtures/language/content/behaviors/CameraFollow.chibi");
    REQUIRE(text);
    bool prepared = prepare(&f, text, backend);
    free(text);
    REQUIRE(prepared);
    ecs_entity camera = ecs_world_create_entity(f.world), players[2];
    REQUIRE(!Transform_add(f.world, camera, 0));
    for (int i = 0; i < 2; i++) {
        players[i] = ecs_world_create_entity(f.world);
        REQUIRE(!Transform_add(f.world, players[i], 0));
    }
    REQUIRE(!ecs_world_apply_commands(f.world));
    source_registry *registry = f.live->sources;
    REQUIRE(!source_attach(registry, camera, "System", false, 0, 0));
    const float deltas[] = { 0, 1.f / 60, .1f, .025f };
    double x = 0, y = 0;
    for (int step = 0; step < 4; step++) {
        ecs_entity player = players[step < 2 ? 0 : 1];
        Transform target = { .x = step < 2 ? 20 : -7, .y = 3 + step * 2 };
        *Transform_get(f.world, player) = target;
        pin_value value = { .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .u64 = ecs_entity_to_u64(player) };
        REQUIRE(!entities_set_global_value(f.live->entities, "Player", &value));
        pin_value delta = { .type = PIN_F32, .f = deltas[step] };
        int dispatched = source_dispatch(
            registry,
            camera,
            "Tick",
            &delta,
            1,
            step,
            delta.f
        );
        if (dispatched < 0) {
            fprintf(stderr, "CameraFollow dispatch: %d %s\n", dispatched, source_error(registry));
        }
        REQUIRE(dispatched == 1);
        double weight = 1 - exp(-6 * (double)deltas[step]);
        x += (target.x - x) * weight;
        y += (target.y - y) * weight;
        const Transform *actual = Transform_get_const(f.world, camera);
        REQUIRE(fabs(actual->x - x) < .00001 && fabs(actual->y - y) < .00001);
    }
    fixture_free(&f);
    return true;
}
static void cleanup()
{
    if (test_source_input) {
        engine_input_free(test_source_input);
    }
    test_source_input = NULL;
    if (input_sources) {
        source_registry_free(input_sources);
    }
    input_sources = NULL;
    authoring_source_free(input_source);
    input_source = NULL;
    authoring_language_catalog_free(input_catalog);
    input_catalog = NULL;
    ecs_world_free(input_world);
    input_world = NULL;
    if (active.world) {
        fixture_free(&active);
    }
    free(formatted);
    formatted = NULL;
}
TEST_CASE(source_system_persistent_wait_membership)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(persistent_wait_membership(backends[i]));
    }
    return true;
}
TEST_CASE(source_system_host_spawn_resume)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(host_spawn_resume(backends[i]));
    }
    return true;
}
TEST_CASE(source_behavior_nested_spawn_outlives_frame)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(nested_spawn_outlives_frame(backends[i]));
    }
    return true;
}
TEST_CASE(source_system_live_component_resume)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(live_component_resume(backends[i]));
    }
    return true;
}
TEST_CASE(source_system_optional_membership_roundtrip)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(optional_membership_roundtrip(backends[i]));
    }
    return true;
}
TEST_CASE(source_system_any_query_membership)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(any_query_membership(backends[i]));
    }
    return true;
}
TEST_CASE(source_system_empty_any_query_matches_nothing)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(empty_any_query(backends[i]));
    }
    return true;
}
TEST_CASE(source_system_reactive_checkpoint)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(reactive_checkpoint(backends[i]));
    }
    return true;
}
TEST_CASE(source_system_synchronous_batch_path)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(synchronous_batch_path(backends[i]));
    }
    return true;
}
TEST_CASE(source_system_camera_follow_runtime)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        REQUIRE(camera_follow_runtime(backends[i]));
    }
    return true;
}

TEST_CASE(source_input_actions_deliver_one_typed_edge_without_graph_objects)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        input_world = ecs_world_new();
        REQUIRE(!ecs_world_register_component(input_world, &COMPONENT_Input, NULL));
        ecs_entity owner = ecs_world_create_entity(input_world);
        REQUIRE(!Input_add(input_world, owner, 0));
        REQUIRE(!ecs_world_apply_commands(input_world));
        const script_catalog_capability pulse = {
            .id = script_catalog_id("event", "Pulse"),
            .name = "Pulse",
            .kind = SCRIPT_CAPABILITY_EVENT,
            .completion_outcome = -1,
        };
        const script_catalog project = { .capabilities = &pulse,
                                         .capability_count = 1 };
        char error[256];
        native_capabilities *table = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
        REQUIRE(table);
        const authoring_language_catalog_input catalog_input = { .native = table,
                                                                 .project = &project };
        input_catalog = authoring_language_catalog_new(&catalog_input, error, sizeof error);
        native_capabilities_free(table);
        REQUIRE(input_catalog);
        const script_lower_bindings bindings = authoring_language_bindings(input_catalog);
        input_source = authoring_source_prepare_document(
            "Input.chibi",
            "systems/Input.chibi",
            "system InputWatch(controls:Input){ state pulses:double=0; on "
            "Pulse{pulses+=1;} }",
            authoring_language_catalog_view(input_catalog),
            &bindings,
            backends[b]
        );
        for (int i = 0; i < input_source->diagnostic_count; i++) {
            fprintf(stderr, "input source: %s\n", input_source->diagnostics[i].message);
        }
        REQUIRE(input_source->valid);
        input_sources = source_registry_new(input_world);
        REQUIRE(
            source_register(input_sources, "InputWatch", input_source->module, NULL) == FLOW_OK
        );
        REQUIRE(source_step(input_sources, 0, .1) == FLOW_OK);
        system_fixture f = { .world = input_world,
                             .source = input_source,
                             .program = source_program(input_sources, "InputWatch") };
        REQUIRE(f.program && state(&f, owner, "pulses") == 0);
        source_input bridge = { .sources = input_sources, .now = .1, .delta = .1 };
        const engine_input_host host = source_input_host(&bridge);
        test_source_input = engine_input_new(input_world, &host);
        REQUIRE(engine_input_action(test_source_input, "Missing", true) == FLOW_ERR_UNKNOWN_EVENT);
        REQUIRE(engine_input_action(test_source_input, "Pulse", true) == 1);
        REQUIRE(engine_input_apply(test_source_input) == FLOW_OK);
        REQUIRE(state(&f, owner, "pulses") == 1);
        bridge.now = .3;
        REQUIRE(engine_input_action(test_source_input, "Pulse", true) == 0);
        REQUIRE(engine_input_apply(test_source_input) == FLOW_OK);
        bridge.now = .4;
        REQUIRE(engine_input_action(test_source_input, "Pulse", false) == 1);
        REQUIRE(engine_input_apply(test_source_input) == FLOW_OK);
        REQUIRE(state(&f, owner, "pulses") == 1);
    }
    return true;
}

void test_code_system_runtime()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_system_persistent_wait_membership);
    RUN_TEST_CASE(source_system_host_spawn_resume);
    RUN_TEST_CASE(source_behavior_nested_spawn_outlives_frame);
    RUN_TEST_CASE(source_system_live_component_resume);
    RUN_TEST_CASE(source_system_optional_membership_roundtrip);
    RUN_TEST_CASE(source_system_any_query_membership);
    RUN_TEST_CASE(source_system_empty_any_query_matches_nothing);
    RUN_TEST_CASE(source_system_reactive_checkpoint);
    RUN_TEST_CASE(source_system_synchronous_batch_path);
    RUN_TEST_CASE(source_system_camera_follow_runtime);
    RUN_TEST_CASE(source_input_actions_deliver_one_typed_edge_without_graph_objects);
    pu_clear_setup();
}
