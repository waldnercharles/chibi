#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/status.h"
#include "runtime/behaviors/entities_native.h"
#include "runtime/behaviors/entities_source.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "runtime/engine_input.h"
#include "runtime/material_field_prepared.h"
#include "runtime/player.h"
#include "runtime/player_prepared.h"
#include "runtime/world.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static game_world *simulation;
static game_scene_instance *scene_instance;
static material_fields *prepared_fields;
static unsigned char *field_bytes;
static int ticks;
static game_player *player;
static unsigned char *module_bytes;
static unsigned char *player_bytes;
static game_event_catalog *event_catalog, *wrong_events;
static ecs_world *world;
static game_source_registry *registry;
static game_source_entities *host;
static game_scene *scene;
static execution_module *module;
static bool catalog_active;
static int old_begins, new_begins;
static float observed;
static struct
{
    flow_host_request *request;
    ecs_entity entity;
    execution_owner_state state;
    float initial, initialized;
    bool payload_valid, detached;
} continuation;

/* Select the application's immutable pack composition before this selector
 * touches any runtime values. It remains selected until the process exits. */
void test_native_catalog_enable()
{
    catalog_active = true;
}
bool test_native_catalog_active()
{
    return catalog_active;
}
type_catalog_pack test_native_catalog_pack()
{
    return GAME_TYPE_PACK;
}
static void cleanup()
{
    if (player) {
        game_player_free(player);
    }
    player = NULL;
    free(module_bytes);
    module_bytes = NULL;
    free(player_bytes);
    player_bytes = NULL;
    game_event_catalog_free(event_catalog);
    event_catalog = NULL;
    game_event_catalog_free(wrong_events);
    wrong_events = NULL;
    if (scene_instance) {
        game_scene_instance_free(scene_instance);
    }
    scene_instance = NULL;
    if (simulation) {
        game_world_free(simulation);
    }
    simulation = NULL;
    if (prepared_fields) {
        material_fields_free(prepared_fields);
    }
    prepared_fields = NULL;
    free(field_bytes);
    field_bytes = NULL;
    ticks = 0;

    if (continuation.request) {
        execution_request_detach(continuation.request);
        continuation.request = NULL;
    }
    if (host) {
        game_source_entities_free(host);
    }
    host = NULL;
    if (registry) {
        game_source_registry_free(registry);
    }
    registry = NULL;
    game_scene_free(scene);
    scene = NULL;
    execution_module_free(module);
    module = NULL;
    ecs_world_free(world);
    world = NULL;
    continuation = (typeof(continuation)){ 0 };
    old_begins = new_begins = 0;
    observed = 0;
}
static const char *observe(void *service, const pin_value *, pin_value *)
{
    const GameTransform
        *transform = GAME_GameTransform_get_const(world, continuation.entity);
    if (!transform) {
        return "native Begin cannot see initialized Transform";
    }
    observed = transform->x;
    if (service) {
        new_begins++;
    } else {
        old_begins++;
    }
    return NULL;
}
static const char *observe_new(void *, const pin_value *in, pin_value *out)
{
    return observe(&new_begins, in, out);
}
static bool register_behavior(behavior_backend backend, bool replacement)
{
    static const node_host_call calls[] = { { .invoke = observe },
                                            { .invoke = observe_new } };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    behavior_function_builder *builder = behavior_function_builder_new();
    int call = behavior_function_builder_host(builder, &calls[replacement], &access, NULL, 0, NULL, 0);
    bool identified = behavior_function_builder_host_identity(builder, call, 970 + replacement, 972);
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .target = call,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[192];
    behavior_function *body = identified
                                ? behavior_function_builder_finish(builder, error, sizeof error)
                                : NULL;
    behavior_function_builder_free(builder);
    REQUIRE(body != NULL);
    execution_module_plan *plan = execution_module_plan_new();
    plan->id = 974 + replacement;
    plan->valid = true;
    apush(plan->events, ((execution_module_event_declaration){ .id = 976, .abi = 977, .name = "Begin" }));
    plan->event_count = 1;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 978 + replacement,
                                      .kind = EXECUTION_FUNCTION_HANDLER,
                                      .event = 976,
                                      .event_abi = 977,
                                      .body = body })
    );
    plan->function_count = 1;
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "native observer module: %s\n", error);
    }
    REQUIRE(module);
    REQUIRE(game_source_register(registry, replacement ? "New" : "Old", module, NULL) == 0);
    execution_module_free(module);
    module = NULL;
    return true;
}
static bool prototype(float x, const char *behavior)
{
    GameTransform transform = { .x = x, .y = 2 };
    game_scene_component component = { &GAME_COMPONENT_GameTransform, &transform };
    game_scene_object object = { .name = "Spawned",
                                 .components = &component,
                                 .component_count = 1,
                                 .behaviors = &behavior,
                                 .behavior_count = 1 };
    game_scene_spec spec = { .name = "NativePrototype",
                             .physics = game_physics_defaults(),
                             .objects = &object,
                             .object_count = 1 };
    char error[192];
    scene = game_scene_prepare(&spec, error, sizeof error);
    REQUIRE(scene);
    REQUIRE(
        game_entities_register_prepared(game_source_entities_host(host), "Selected", scene) == 0
    );
    game_scene_free(scene);
    scene = NULL;
    return true;
}
static bool complete(void *, int pin, pin_value value)
{
    continuation.payload_valid = pin == 1 && value.type == PIN_HANDLE;
    continuation.entity = ecs_entity_from_u64(value.u64);
    return continuation.payload_valid;
}
static int resume(void *)
{
    GameTransform *transform = GAME_GameTransform_get(world, continuation.entity);
    continuation.payload_valid &= transform && transform->x == continuation.initial;
    if (!transform) {
        return FLOW_ERR_NODE;
    }
    transform->x = continuation.initialized;
    continuation.state = EXECUTION_OWNER_SUSPENDED;
    return FLOW_SUSPENDED_RESULT;
}
static void detach(void *)
{
    continuation.detached = true;
}
static const void *identity(void *owner)
{
    return owner;
}
static execution_owner_state state(void *)
{
    return continuation.state;
}
static bool valid(void *)
{
    return !continuation.detached;
}
static const execution_request_sink sink = { .complete = complete,
                                             .resume = resume,
                                             .detach = detach,
                                             .identity = identity,
                                             .state = state,
                                             .valid = valid };
static bool queue(float initial, float initialized)
{
    continuation = (typeof(continuation)){ .state = EXECUTION_OWNER_SUSPENDED,
                                           .initial = initial,
                                           .initialized = initialized };
    continuation.request = execution_request_new(&sink, &continuation, 0);
    pin_value argument = { .type = PIN_NAME, .str = "Selected" };
    const char *error = NULL;
    REQUIRE(
        GAME_ENTITY_SPAWN_COMMAND.begin(
            game_source_entities_host(host),
            NULL,
            &argument,
            1,
            continuation.request,
            &error
        ) == FLOW_OK
    );
    return true;
}
static bool finish()
{
    continuation.state = EXECUTION_OWNER_COMPLETED;
    execution_request_finish(continuation.request, FLOW_HOST_FINISHED);
    REQUIRE(game_entities_flush(game_source_entities_host(host)) == 0);
    continuation.request = NULL; /* Host completion consumed both references. */
    REQUIRE(continuation.detached);
    REQUIRE(game_source_begin(registry, 1, 0) == 0);
    return true;
}
TEST_CASE(native_prepared_spawn_generations)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        behavior_backend backend = backends[i];
        REQUIRE(
            type_catalog_find("GameTransform") == GAME_COMPONENT_GameTransform.type
        );
        world = ecs_world_new();
        registry = game_source_registry_new(world);
        host = game_source_entities_new(registry);
        game_source_entities_time(host, 1, 0, true);
        REQUIRE(register_behavior(backend, false));
        REQUIRE(register_behavior(backend, true));
        REQUIRE(prototype(3, "Old"));
        REQUIRE(queue(3, 33));
        REQUIRE(prototype(7, "New")); /* Pending request retains Old generation. */
        REQUIRE(game_entities_flush(game_source_entities_host(host)) == 1);
        REQUIRE(continuation.payload_valid && ecs_world_live_count(world) == 1);
        REQUIRE(old_begins == 0 && new_begins == 0);
        REQUIRE(finish());
        REQUIRE(old_begins == 1 && new_begins == 0 && observed == 33);
        REQUIRE(queue(7, 77));
        REQUIRE(game_entities_flush(game_source_entities_host(host)) == 1);
        REQUIRE(continuation.payload_valid && old_begins == 1 && new_begins == 0);
        REQUIRE(finish());
        REQUIRE(old_begins == 1 && new_begins == 1 && observed == 77);
        REQUIRE(queue(7, 88));
        REQUIRE(game_entities_flush(game_source_entities_host(host)) == 1);
        ecs_entity canceled = continuation.entity;
        REQUIRE(ecs_world_live_count(world) == 3);
        execution_request_detach(continuation.request);
        continuation.request = NULL;
        REQUIRE(game_entities_flush(game_source_entities_host(host)) == 0);
        REQUIRE(!ecs_entity_is_alive(world, canceled) && ecs_world_live_count(world) == 2);
        REQUIRE(game_source_begin(registry, 2, 0) == 0);
        REQUIRE(old_begins == 1 && new_begins == 1);
        cleanup();
    }
    return true;
}
static const char *tick(void *, const pin_value *, pin_value *)
{
    ticks++;
    return NULL;
}
TEST_CASE(native_prepared_world_runs_source_and_real_physics)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        char error[192];
        prepared_fields = material_fields_new();
        field_parameter radius = { "radius", { .type = PIN_F32, .f = 2 } };
        const field_ir ir = { .op = FIELD_PARAMETER, .parameter = 0 };
        field_program *field = field_program_new(&ir, 1, 0, &radius, 1, error, sizeof error);
        REQUIRE(field);
        REQUIRE(material_field_register(prepared_fields, "Constant", field));
        radius.value.f = 3;
        REQUIRE(material_field_register_arguments(
            prepared_fields,
            "Constant",
            "preset/large",
            (field_context){ .parameters = &radius, .parameter_count = 1 },
            error,
            sizeof error
        ));
        size_t length;
        REQUIRE(
            material_fields_prepared_write(prepared_fields, &field_bytes, &length, error, sizeof error)
        );
        material_fields_free(prepared_fields);
        prepared_fields = material_fields_prepared_read(field_bytes, length, error, sizeof error);
        REQUIRE(prepared_fields);
        free(field_bytes);
        field_bytes = NULL;
        game_world_options options = { .modules = GAME_WORLD_PHYSICS | GAME_WORLD_MATERIALS,
                                       .fields = prepared_fields };
        simulation = game_world_new(&options, error, sizeof error);
        REQUIRE(simulation);
        prepared_fields = NULL;
        ecs_world *ecs = game_world_ecs(simulation);
        game_source_registry *sources = game_world_sources(simulation);
        static const node_host_call call = { .invoke = tick };
        static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
        behavior_function_builder *builder = behavior_function_builder_new();
        const pin_value dt = { .type = PIN_F32 };
        behavior_function_builder_signature(builder, &dt, 1, NULL, 0, NULL, 0);
        int host_call = behavior_function_builder_host(builder, &call, &access, NULL, 0, NULL, 0);
        REQUIRE(behavior_function_builder_host_identity(builder, host_call, 984, 985));
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_HOST_CALL,
                                             .target = host_call,
                                             .left = BEHAVIOR_FUNCTION_HOST_DATA }
        );
        behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
        behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
        behavior_function_builder_free(builder);
        REQUIRE(body);
        execution_module_plan *plan = execution_module_plan_new();
        plan->id = 986;
        plan->valid = true;
        execution_module_event_field *event_fields = malloc(sizeof *event_fields);
        REQUIRE(event_fields);
        *event_fields = (execution_module_event_field){ .name = "dt", .initial = dt };
        apush(
            plan->events,
            ((
                execution_module_event_declaration
            ){ .id = 987, .abi = 988, .name = "Tick", .fields = event_fields, .field_count = 1 })
        );
        plan->event_count = 1;
        apush(
            plan->functions,
            ((execution_module_function){ .id = 989,
                                          .kind = EXECUTION_FUNCTION_HANDLER,
                                          .event = 987,
                                          .event_abi = 988,
                                          .body = body })
        );
        plan->function_count = 1;
        module = execution_module_finish(&plan, backends[b], error, sizeof error);
        execution_module_plan_free(plan);
        REQUIRE(module);
        REQUIRE(game_source_register(sources, "Clock", module, NULL) == 0);
        execution_module_free(module);
        module = NULL;
        GameTransform transform = { 0 };
        GameVelocity velocity = { .x = 1 };
        GameBody rigid = { .type = GAME_BODY_DYNAMIC };
        GameCollider collider = { .shape = GAME_COLLIDER_BOX, .width = 1, .height = 1 };
        const game_scene_component components[] = {
            { &GAME_COMPONENT_GameTransform, &transform },
            { &GAME_COMPONENT_GameVelocity, &velocity },
            { &GAME_COMPONENT_GameBody, &rigid },
            { &GAME_COMPONENT_GameCollider, &collider },
        };
        const char *behavior = "Clock";
        const game_scene_object object = { .name = "Moving",
                                           .components = components,
                                           .component_count = 4,
                                           .behaviors = &behavior,
                                           .behavior_count = 1 };
        const game_scene_spec spec = { .name = "Prepared",
                                       .physics = game_physics_defaults(),
                                       .objects = &object,
                                       .object_count = 1 };
        scene = game_scene_prepare(&spec, error, sizeof error);
        REQUIRE(scene);
        scene_instance = game_scene_instantiate(scene, ecs, sources, error, sizeof error);
        REQUIRE(scene_instance);
        game_scene_free(scene);
        scene = NULL;
        engine *runtime = game_world_engine(simulation);
        REQUIRE(engine_frame(runtime, 0) == 0);
        REQUIRE(engine_frame(runtime, .05) == 0);
        REQUIRE(engine_last_timing(runtime)->simulation_steps == 3 && ticks == 3);
        ecs_entity entity = game_scene_instance_entity(scene_instance, 0);
        const GameTransform *moved = GAME_GameTransform_get_const(ecs, entity);
        REQUIRE(moved && fabsf(moved->x - .05f) < .00001f && moved->y == 0);
        material_fields *catalog = game_materials_fields(game_world_materials(simulation));
        field_context context;
        REQUIRE(
            material_field_context(catalog, "Constant", "preset/large", 0, &context, error, sizeof error)
        );
        float value;
        REQUIRE(material_field_bake(
            catalog,
            "Constant",
            context,
            (field_grid){ .width = 1, .height = 1, .cell_size = 1 },
            &value,
            NULL,
            error,
            sizeof error
        ));
        REQUIRE(value == 3);
        cleanup();
    }
    return true;
}
static struct
{
    int steps, presses;
    bool held, pressed;
} jump;
/* Reads the service the engine bound, the way Action Held and Pressed do. */
static const char *observe_jump(void *service, const pin_value *, pin_value *)
{
    const pin_value action = { .type = PIN_STR, .str = "Jump" };
    pin_value held = { .type = PIN_BOOL }, pressed = { .type = PIN_BOOL };
    const char *error = GAME_INPUT_HELD_CALL.invoke(service, &action, &held);
    if (!error) {
        error = GAME_INPUT_PRESSED_CALL.invoke(service, &action, &pressed);
    }
    jump.steps++;
    jump.held = held.b;
    jump.pressed = pressed.b;
    return error;
}
static const char *count_jump(void *, const pin_value *, pin_value *)
{
    jump.presses++;
    return NULL;
}
/* A handler whose body makes one host call. */
static behavior_function *host_body(const node_host_call *call, const pin_value *in, int count, uint64_t identity)
{
    behavior_function_builder *builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, in, count, NULL, 0, NULL, 0);
    int host_call = behavior_function_builder_host(builder, call, &GAME_INPUT_READ_ACCESS, NULL, 0, NULL, 0);
    bool identified = behavior_function_builder_host_identity(builder, host_call, identity, identity + 1);
    assert(identified);
    (void)identified;
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .target = host_call,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[192];
    behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    return body;
}
static void handler(
    execution_module_plan *plan,
    const char *event,
    uint64_t id,
    const pin_value *in,
    int count,
    behavior_function *body
)
{
    execution_module_event_field *fields = NULL;
    if (count) {
        fields = xmalloc(sizeof *fields);
        *fields = (execution_module_event_field){ .name = "dt", .initial = *in };
    }
    apush(
        plan->events,
        ((
            execution_module_event_declaration
        ){ .id = id, .abi = id + 1, .name = event, .fields = fields, .field_count = count })
    );
    apush(
        plan->functions,
        ((execution_module_function){ .id = id + 2,
                                      .kind = EXECUTION_FUNCTION_HANDLER,
                                      .event = id,
                                      .event_abi = id + 1,
                                      .body = body })
    );
    plan->event_count++;
    plan->function_count++;
}
/* Samples controls, runs exactly one fixed step and checks what Tick read. */
static bool jump_step(engine *runtime, uint64_t controls, bool held, bool pressed)
{
    bool firing;
    REQUIRE(engine_sample_controls(runtime, controls, &firing) == FLOW_OK);
    int steps = jump.steps;
    REQUIRE(engine_frame(runtime, (steps + 1.5) * ENGINE_FIXED_SECONDS) == 0);
    REQUIRE(jump.steps == steps + 1 && jump.held == held && jump.pressed == pressed);
    return true;
}
/* A world whose input map binds Jump to Z and X. Its Tick reads Jump, and an
 * input owner counts Jump events. */
static bool jump_world(engine **out)
{
    cleanup();
    jump = (typeof(jump)){ 0 };
    char error[192];
    const input_action actions[] = {
        { "Jump", (UINT64_C(1) << GAME_CONTROL_Z) | (UINT64_C(1) << GAME_CONTROL_X), false },
    };
    /* A count without actions, or an unnamed action, is rejected. */
    const input_action unnamed = { NULL, UINT64_C(1) << GAME_CONTROL_Z, false };
    game_world_options invalid[] = {
        { .modules = GAME_WORLD_INPUT, .action_count = 1 },
        { .modules = GAME_WORLD_INPUT, .actions = &unnamed, .action_count = 1 },
    };
    for (int i = 0; i < 2; i++) {
        REQUIRE(!game_world_new(&invalid[i], error, sizeof error));
        REQUIRE(!strcmp(error, "Invalid runtime world options"));
    }
    game_world_options options = { .modules = GAME_WORLD_INPUT,
                                   .actions = actions,
                                   .action_count = 1 };
    simulation = game_world_new(&options, error, sizeof error);
    REQUIRE(simulation);
    ecs_world *ecs = game_world_ecs(simulation);
    static const node_host_call tick = { .service = GAME_INPUT_SERVICE,
                                         .invoke = observe_jump,
                                         .optional_service = true };
    static const node_host_call press = { .invoke = count_jump };
    const pin_value dt = { .type = PIN_F32 };
    behavior_function *tick_body = host_body(&tick, &dt, 1, 990);
    behavior_function *press_body = host_body(&press, NULL, 0, 980);
    REQUIRE(tick_body && press_body);
    execution_module_plan *plan = execution_module_plan_new();
    plan->id = 992;
    plan->valid = true;
    handler(plan, "Tick", 993, &dt, 1, tick_body);
    handler(plan, "Jump", 983, NULL, 0, press_body);
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    execution_module_plan_free(plan);
    REQUIRE(module);
    REQUIRE(game_source_register(game_world_sources(simulation), "Jumper", module, NULL) == 0);
    execution_module_free(module);
    module = NULL;
    ecs_entity jumper = ecs_world_create_entity(ecs);
    REQUIRE(GAME_GameInput_add(ecs, jumper, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    REQUIRE(
        game_source_attach(game_world_sources(simulation), jumper, "Jumper", false, 0, 0) == 0
    );
    *out = game_world_engine(simulation);
    REQUIRE(engine_frame(*out, 0) == 0);
    return true;
}
TEST_CASE(native_action_state_combines_sources_per_step)
{
    engine *runtime;
    REQUIRE(jump_world(&runtime));
    const uint64_t z = UINT64_C(1) << GAME_CONTROL_Z,
                   x = UINT64_C(1) << GAME_CONTROL_X,
                   c = UINT64_C(1) << GAME_CONTROL_C;
    REQUIRE(jump_step(runtime, 0, false, false));
    REQUIRE(jump_step(runtime, z, true, true));
    REQUIRE(jump_step(runtime, z, true, false));
    /* Moving from one source to another never releases the action. */
    REQUIRE(jump_step(runtime, x, true, false));
    REQUIRE(jump_step(runtime, 0, false, false));
    /* A control outside the map holds nothing. */
    REQUIRE(jump_step(runtime, c, false, false));
    REQUIRE(jump.presses == 1);
    /* A tap between two steps still counts as pressed for the next one. */
    bool firing;
    REQUIRE(engine_sample_controls(runtime, x, &firing) == FLOW_OK);
    REQUIRE(jump_step(runtime, 0, false, true));
    REQUIRE(jump_step(runtime, 0, false, false));
    REQUIRE(jump.presses == 2);
    cleanup();
    return true;
}
TEST_CASE(native_pause_keeps_held_actions)
{
    engine *runtime;
    REQUIRE(jump_world(&runtime));
    const uint64_t z = UINT64_C(1) << GAME_CONTROL_Z;
    REQUIRE(jump_step(runtime, z, true, true));
    REQUIRE(jump.presses == 1);
    REQUIRE(engine_set_playing(runtime, false) == 0);
    /* Hosts sample no controls while paused; that sample is ignored. */
    bool firing = true;
    REQUIRE(engine_sample_controls(runtime, 0, &firing) == FLOW_OK && !firing);
    REQUIRE(engine_frame(runtime, (jump.steps + 0.5) * ENGINE_FIXED_SECONDS) == 0);
    REQUIRE(engine_set_playing(runtime, true) == 0);
    /* The first frame after resuming only restarts the clock. */
    REQUIRE(engine_frame(runtime, (jump.steps + 0.5) * ENGINE_FIXED_SECONDS) == 0);
    REQUIRE(jump_step(runtime, z, true, false));
    REQUIRE(jump.presses == 1);
    cleanup();
    return true;
}
#define Z_DOWN (UINT64_C(1) << GAME_CONTROL_Z)
#define X_DOWN (UINT64_C(1) << GAME_CONTROL_X)
/* The controls sampled before each step. */
static const struct
{
    int count;
    uint64_t samples[3];
} tapped_steps[] = {
    { 1, { 0 } },
    { 2, { Z_DOWN, 0 } },         /* tap */
    { 1, { Z_DOWN } },            /* press */
    { 2, { 0, Z_DOWN } },         /* release, press */
    { 1, { Z_DOWN } },            /* hold */
    { 3, { 0, Z_DOWN, 0 } },      /* release, tap */
    { 3, { X_DOWN, Z_DOWN, 0 } }, /* tap one source, then another */
    { 1, { 0 } },
    { 1, { Z_DOWN } },
    /* Move to another source, then release: never pressed again. */
    { 3, { Z_DOWN | X_DOWN, X_DOWN, 0 } },
};
#define TAPPED_STEPS (int)(sizeof tapped_steps / sizeof *tapped_steps)
/* Runs every step, sampling live controls (ignored while replaying), and keeps
 * what Tick read in each. */
static bool run_taps(engine *runtime, bool read[][2])
{
    for (int step = 0; step < TAPPED_STEPS; step++) {
        for (int i = 0; i < tapped_steps[step].count; i++) {
            bool firing;
            REQUIRE(
                engine_sample_controls(runtime, tapped_steps[step].samples[i], &firing) == FLOW_OK
            );
        }
        REQUIRE(engine_frame(runtime, (step + 1.5) * ENGINE_FIXED_SECONDS) == 0);
        REQUIRE(jump.steps == step + 1);
        read[step][0] = jump.held;
        read[step][1] = jump.pressed;
    }
    return true;
}
TEST_CASE(native_recorded_taps_replay_per_step)
{
    static const bool expected[TAPPED_STEPS][2] = {
        { false, false }, { false, true },  { true, true },  { true, true },
        { true, false },  { false, true },  { false, true }, { false, false },
        { true, true },   { false, false },
    };
    engine *runtime;
    REQUIRE(jump_world(&runtime));
    engine_record_input(runtime, 3);
    bool live[TAPPED_STEPS][2], replayed[TAPPED_STEPS][2];
    REQUIRE(run_taps(runtime, live));
    REQUIRE(!memcmp(live, expected, sizeof live));
    int count;
    const input_keyframe *recorded = engine_recorded_input(runtime, &count);
    input_keyframe *keys = NULL;
    for (int i = 0; i < count; i++) {
        apush(keys, recorded[i]);
    }
    REQUIRE(jump_world(&runtime));
    engine_replay_input(runtime, keys, count, 3);
    afree(keys);
    REQUIRE(run_taps(runtime, replayed));
    REQUIRE(!memcmp(replayed, live, sizeof live));
    cleanup();
    return true;
}
static bool player_host(void *, uint64_t id, execution_prepared_host *out)
{
    static const node_host_call call = { .invoke = tick };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    if (id != 990) {
        return false;
    }
    *out = (execution_prepared_host){ .call = &call, .access = &access, .abi = 991 };
    return true;
}
TEST_CASE(native_player_loads_package_and_retires_producers)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        char error[192];
        static const node_host_call call = { .invoke = tick };
        static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
        behavior_function_builder *builder = behavior_function_builder_new();
        int host_call = behavior_function_builder_host(builder, &call, &access, NULL, 0, NULL, 0);
        REQUIRE(behavior_function_builder_host_identity(builder, host_call, 990, 991));
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_HOST_CALL,
                                             .target = host_call,
                                             .left = BEHAVIOR_FUNCTION_HOST_DATA }
        );
        behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
        behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
        behavior_function_builder_free(builder);
        REQUIRE(body);
        execution_module_plan *plan = execution_module_plan_new();
        plan->id = 992;
        plan->valid = true;
        apush(
            plan->events,
            ((execution_module_event_declaration){ .id = 993, .abi = 994, .name = "Begin" })
        );
        plan->event_count = 1;
        apush(
            plan->functions,
            ((execution_module_function){ .id = 995,
                                          .kind = EXECUTION_FUNCTION_HANDLER,
                                          .event = 993,
                                          .event_abi = 994,
                                          .body = body })
        );
        plan->function_count = 1;
        module = execution_module_finish(&plan, backends[b], error, sizeof error);
        execution_module_plan_free(plan);
        REQUIRE(module);
        size_t length;
        REQUIRE(execution_prepared_write(module, &module_bytes, &length, error, sizeof error));
        execution_module_free(module);
        module = NULL;
        GameTransform transform = { .x = 4 };
        GameGlobalBinding binding = { .name = "Player" };
        game_scene_component components[] = {
            { &GAME_COMPONENT_GameTransform, &transform },
            { &GAME_COMPONENT_GameGlobalBinding, &binding }
        };
        const char *behavior = "Initial";
        game_scene_object object = { .name = "Initial",
                                     .components = components,
                                     .component_count = 2,
                                     .behaviors = &behavior,
                                     .behavior_count = 1 };
        game_scene_spec scene_spec = { .name = "Player",
                                       .physics = game_physics_defaults(),
                                       .objects = &object,
                                       .object_count = 1 };
        scene = game_scene_prepare(&scene_spec, error, sizeof error);
        REQUIRE(scene);
        const execution_prepared_linker linker = { .host = player_host };
        game_package_module record = { .name = "Initial",
                                       .id = 992,
                                       .data = module_bytes,
                                       .length = length };
        game_player_prototype prototype = { .name = "Selected", .scene = scene };
        game_player_spec spec = { .modules = &record,
                                  .module_count = 1,
                                  .linker = &linker,
                                  .backend = backends[b],
                                  .prototypes = &prototype,
                                  .prototype_count = 1,
                                  .scene = scene };
        char field_name[] = "message", default_text[] = "Saved payload";
        execution_module_event_field_info custom_field = {
            .name = field_name,
            .initial = { .type = PIN_STR, .str = default_text },
            .has_default = true
        };
        game_event_declaration declarations[] = {
            { .event = { .id = 993, .abi = 994, .name = "Begin" } },
            { .event = { .id = 996, .abi = 997, .name = "Unhandled", .field_count = 1 },
              .fields = &custom_field }
        };
        event_catalog = game_event_catalog_new(declarations, 2, NULL, error, sizeof error);
        REQUIRE(event_catalog);
        field_name[0] = 'X';
        default_text[0] = 'X';
        spec.world.events = event_catalog;
        record.name = "Missing";
        REQUIRE(!game_player_new(&spec, error, sizeof error));
        REQUIRE(ticks == 0 && game_scene_object_count(scene) == 1);
        record.name = "Initial";
        size_t player_length;
        REQUIRE(
            game_player_prepared_write(&spec, &player_bytes, &player_length, error, sizeof error)
        );
        game_player_host player_host_context = {
            .components = { .components = GAME_REGISTRY_COMPONENTS,
                            .component_count = GAME_REGISTRY_COMPONENT_COUNT },
            .linker = &linker
        };
        REQUIRE(!game_player_prepared_read(
            player_bytes,
            player_length - 1,
            &player_host_context,
            backends[b],
            error,
            sizeof error
        ));
        REQUIRE(ticks == 0);
        player_host_context.decoded_budget = 1;
        REQUIRE(!game_player_prepared_read(
            player_bytes,
            player_length,
            &player_host_context,
            backends[b],
            error,
            sizeof error
        ));
        REQUIRE(ticks == 0);
        player_host_context.decoded_budget = 0;
        player = game_player_prepared_read(
            player_bytes,
            player_length,
            &player_host_context,
            backends[b],
            error,
            sizeof error
        );
        if (!player) {
            fprintf(stderr, "prepared player: %s\n", error);
        }
        REQUIRE(player);
        REQUIRE(ticks == 0);
        game_scene_free(scene);
        scene = NULL;
        free(module_bytes);
        module_bytes = NULL;
        free(player_bytes);
        player_bytes = NULL;
        game_event_catalog_free(event_catalog);
        event_catalog = NULL;
        game_world *runtime_world = game_player_world(player);
        game_entities *entities = game_world_entities(runtime_world);
        REQUIRE(game_entities_global_name_count(entities) == 1);
        REQUIRE(!strcmp(game_entities_global_name_at(entities, 0), "Player"));
        game_source_registry *sources = game_world_sources(runtime_world);
        execution_module_event_info event;
        execution_module_event_field_info saved_field;
        REQUIRE(game_source_event(sources, "Unhandled", &event));
        REQUIRE(event.id == 996 && event.abi == 997 && event.field_count == 1);
        REQUIRE(game_source_event_field(sources, "Unhandled", 0, &saved_field));
        REQUIRE(
            !strcmp(saved_field.name, "message") &&
            !strcmp(saved_field.initial.str, "Saved payload")
        );
        game_event_declaration wrong = { .event = { .id = 993, .abi = 999, .name = "Begin" } };
        wrong_events = game_event_catalog_new(&wrong, 1, NULL, error, sizeof error);
        REQUIRE(wrong_events);
        REQUIRE(game_source_set_events(sources, wrong_events) < 0);
        REQUIRE(game_source_event(sources, "Unhandled", &event) && event.id == 996);
        execution_module_event_field_info invalid_field = { .name = "invalid",
                                                            .initial = { .type = PIN_FLOW } };
        game_event_declaration invalid = {
            .event = { .id = 998, .abi = 999, .name = "Invalid", .field_count = 1 },
            .fields = &invalid_field
        };
        REQUIRE(!game_event_catalog_new(&invalid, 1, NULL, error, sizeof error));
        invalid_field.initial = (pin_value){ .type = PIN_STR, .str = "owner" };
        invalid_field.has_default = true;
        invalid_field.default_owner = true;
        REQUIRE(!game_event_catalog_new(&invalid, 1, NULL, error, sizeof error));

        ecs_entity entity = game_scene_instance_entity(game_player_scene(player), 0);
        REQUIRE(
            GAME_GameTransform_get_const(game_world_ecs(runtime_world), entity)->x == 4
        );
        REQUIRE(engine_frame(game_world_engine(runtime_world), 0) == 0);
        REQUIRE(engine_frame(game_world_engine(runtime_world), .02) == 0);
        REQUIRE(ticks == 1);
        REQUIRE(game_source_attachment_count(game_world_sources(runtime_world)) == 1);
        cleanup();
        world = ecs_world_new();
        ecs_entity caller_entity = ecs_world_create_entity(world);
        REQUIRE(ecs_entity_is_alive(world, caller_entity));
        game_world_options borrowed = { .ecs = world };
        simulation = game_world_new(&borrowed, error, sizeof error);
        REQUIRE(simulation && game_world_ecs(simulation) == world);
        game_world_free(simulation);
        simulation = NULL;
        REQUIRE(ecs_entity_is_alive(world, caller_entity));
        cleanup();
    }
    return true;
}
typedef struct
{
    float level;
    int cycles;
    bool full;
    uint64_t source;
    CF_V2 drift;
} charge;
static const declared_field charge_fields[] = {
    { .name = "level", .type = "float" },
    { .name = "cycles", .type = "int" },
    { .name = "full", .type = "bool" },
    { .name = "source", .type = "entity" },
    { .name = "drift", .type = "CF_V2" },
};
static bool rejected(const char *name, const char *type, const char *message)
{
    char error[192];
    declared_field field = { .name = "value", .type = type };
    declared_component component = { .name = name, .fields = &field, .field_count = 1 };
    declared_components *set = declared_components_new(&component, 1, error, sizeof error);
    declared_components_free(set);
    return !set && strstr(error, message);
}
TEST_CASE(native_player_reads_declared_components)
{
    char error[192];
    REQUIRE(rejected("CF_V2", "float", "already an engine type"));
    REQUIRE(rejected("float", "float", "built-in script type"));
    REQUIRE(rejected("task", "float", "built-in script type"));
    REQUIRE(rejected("Charge", "vec9", "unknown type vec9"));
    REQUIRE(rejected("Charge", "handle", "unknown type handle"));
    REQUIRE(rejected("2Charge", "float", "identifier"));
    declared_component component = { .name = "Charge",
                                     .fields = charge_fields,
                                     .field_count = 5 };
    declared_component twice[] = { component, component };
    REQUIRE(!declared_components_new(twice, 2, error, sizeof error));
    REQUIRE(strstr(error, "declared twice"));
    declared_field level = { .name = "level", .type = "float" };
    declared_component other = { .name = "Battery", .fields = &level, .field_count = 1 };
    declared_components *older = declared_components_new(&other, 1, error, sizeof error);
    REQUIRE(older && type_catalog_find("Battery"));
    declared_components *declared = declared_components_new(&component, 1, error, sizeof error);
    REQUIRE(declared);
    REQUIRE(!type_catalog_find("Battery"));
    const type_desc *type = type_catalog_find("Charge");
    REQUIRE(type && type->size == sizeof(charge));
    REQUIRE(type->fields[1].offset == offsetof(charge, cycles));
    REQUIRE(type->fields[2].offset == offsetof(charge, full));
    REQUIRE(type->fields[3].offset == offsetof(charge, source));
    REQUIRE(type->fields[4].offset == offsetof(charge, drift));
    /* A replacement may reuse the names of the set it replaces, and a failed
     * one leaves that set published. */
    declared_components *replacement = declared_components_new(&component, 1, error, sizeof error);
    REQUIRE(replacement && type_catalog_find("Charge") != type);
    declared_components_free(replacement);
    REQUIRE(rejected("Charge", "vec9", "unknown type vec9"));
    REQUIRE(type_catalog_find("Charge") == type);
    /* A project that declares nothing still hides the other project's set. */
    declared_components *none = declared_components_new(NULL, 0, error, sizeof error);
    REQUIRE(none && !type_catalog_find("Charge"));
    REQUIRE(project_components().component_count == GAME_REGISTRY_COMPONENT_COUNT);
    declared_components_free(none);
    REQUIRE(type_catalog_find("Charge") == type);
    ecs_component_catalog components = declared_components_ecs(declared);
    REQUIRE(components.component_count == 1 && components.components[0]->type == type);
    ecs_component_catalog project = project_components();
    REQUIRE(script_catalog_project() == declared_components_catalog(declared));
    REQUIRE(project.component_count == GAME_REGISTRY_COMPONENT_COUNT + 1);
    REQUIRE(project.components[GAME_REGISTRY_COMPONENT_COUNT] == components.components[0]);
    const script_catalog_type *script = &declared_components_catalog(declared)->types[0];
    REQUIRE(
        script->component && script->component_id == components.components[0]->id
    );
    ecs_component_id charge_id = script->component_id;
    REQUIRE(
        script->fields[4].type.storage == PIN_STRUCT &&
        script->fields[4].type.nominal == script_catalog_id("type", "CF_V2")
    );
    charge row = { .level = 1.25f, .cycles = 2, .full = true, .drift = { 1, -1 } };
    game_scene_component data = { components.components[0], &row };
    game_scene_object object = { .name = "Cell",
                                 .components = &data,
                                 .component_count = 1 };
    game_scene_spec scene_spec = { .name = "Declared",
                                   .physics = game_physics_defaults(),
                                   .objects = &object,
                                   .object_count = 1 };
    scene = game_scene_prepare(&scene_spec, error, sizeof error);
    REQUIRE(scene);
    /* The input map travels with the package. */
    const input_action jump_map = { "Jump", UINT64_C(1) << GAME_CONTROL_Z, false };
    game_player_spec spec = { .world = { .actions = &jump_map, .action_count = 1 },
                              .declared = declared,
                              .scene = scene };
    size_t length;
    /* A count without actions is rejected before it is written. */
    spec.world.actions = NULL;
    REQUIRE(!game_player_prepared_write(&spec, &player_bytes, &length, error, sizeof error));
    REQUIRE(strstr(error, "Invalid prepared player record metadata") && !player_bytes);
    spec.world.actions = &jump_map;
    REQUIRE(game_player_prepared_write(&spec, &player_bytes, &length, error, sizeof error));
    game_scene_free(scene);
    scene = NULL;
    declared_components_free(declared);
    REQUIRE(!type_catalog_find("Charge") && type_catalog_find("Battery"));
    REQUIRE(script_catalog_project() == declared_components_catalog(older));
    declared_components_free(older);
    REQUIRE(!type_catalog_find("Battery") && !script_catalog_project());
    REQUIRE(project_components().component_count == GAME_REGISTRY_COMPONENT_COUNT);
    game_player_host player_host_context = {
        .components = { .components = GAME_REGISTRY_COMPONENTS,
                        .component_count = GAME_REGISTRY_COMPONENT_COUNT }
    };
    player = game_player_prepared_read(
        player_bytes,
        length,
        &player_host_context,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    if (!player) {
        fprintf(stderr, "prepared player: %s\n", error);
    }
    REQUIRE(player);
    REQUIRE(type_catalog_find("Charge"));
    ecs_world *ecs = game_world_ecs(game_player_world(player));
    ecs_entity cell = game_scene_instance_entity(game_player_scene(player), 0);
    const charge *read = ecs_component_data_const(ecs, cell, charge_id);
    REQUIRE(read && read->level == 1.25f && read->cycles == 2 && read->full);
    REQUIRE(read->drift.x == 1 && read->drift.y == -1);
    REQUIRE(project_components().component_count == GAME_REGISTRY_COMPONENT_COUNT + 1);
    game_player_free(player);
    player = NULL;
    REQUIRE(!type_catalog_find("Charge"));
    return true;
}
void test_tool_native()
{
    test_native_catalog_enable();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_prepared_spawn_generations);
    RUN_TEST_CASE(native_prepared_world_runs_source_and_real_physics);
    RUN_TEST_CASE(native_player_loads_package_and_retires_producers);
    RUN_TEST_CASE(native_player_reads_declared_components);
    RUN_TEST_CASE(native_action_state_combines_sources_per_step);
    RUN_TEST_CASE(native_recorded_taps_replay_per_step);
    RUN_TEST_CASE(native_pause_keeps_held_actions);
    pu_clear_setup();
}
