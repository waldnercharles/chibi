#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/status.h"
#include "runtime/audio.h"
#include "runtime/behaviors/entities_native.h"
#include "runtime/behaviors/entities_source.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "runtime/engine_input.h"
#include "runtime/material_field_prepared.h"
#include "runtime/physics3d_native.h"
#include "runtime/physics_native.h"
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
#include <unistd.h>

static world *simulation;
static scene_instance *test_scene_instance;
static material_fields *prepared_fields;
static unsigned char *field_bytes;
static int ticks;
static player *test_player;
static unsigned char *module_bytes;
static unsigned char *player_bytes;
static event_catalog *test_event_catalog, *wrong_events;
static ecs_world *test_world;
static source_registry *registry;
static source_entities *host;
static scene *test_scene, *other_scene;
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
    return TYPE_PACK;
}
static void cleanup()
{
    if (test_player) {
        player_free(test_player);
    }
    test_player = NULL;
    free(module_bytes);
    module_bytes = NULL;
    free(player_bytes);
    player_bytes = NULL;
    event_catalog_free(test_event_catalog);
    test_event_catalog = NULL;
    event_catalog_free(wrong_events);
    wrong_events = NULL;
    if (test_scene_instance) {
        scene_instance_free(test_scene_instance);
    }
    test_scene_instance = NULL;
    if (simulation) {
        world_free(simulation);
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
        source_entities_free(host);
    }
    host = NULL;
    if (registry) {
        source_registry_free(registry);
    }
    registry = NULL;
    scene_free(test_scene);
    test_scene = NULL;
    scene_free(other_scene);
    other_scene = NULL;
    execution_module_free(module);
    module = NULL;
    ecs_world_free(test_world);
    test_world = NULL;
    continuation = (typeof(continuation)){ 0 };
    old_begins = new_begins = 0;
    observed = 0;
}
static const char *observe(void *service, const pin_value *, pin_value *)
{
    const Transform *transform = Transform_get_const(test_world, continuation.entity);
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
    REQUIRE(source_register(registry, replacement ? "New" : "Old", module, NULL) == 0);
    execution_module_free(module);
    module = NULL;
    return true;
}
static bool prototype(float x, const char *behavior)
{
    Transform transform = { .x = x, .y = 2 };
    scene_component component = { &COMPONENT_Transform, &transform };
    scene_object object = { .name = "Spawned",
                            .components = &component,
                            .component_count = 1,
                            .behaviors = &behavior,
                            .behavior_count = 1 };
    scene_spec spec = { .name = "NativePrototype",
                        .physics = physics_defaults(),
                        .objects = &object,
                        .object_count = 1 };
    char error[192];
    test_scene = scene_prepare(&spec, error, sizeof error);
    REQUIRE(test_scene);
    REQUIRE(entities_register_prepared(source_entities_host(host), "Selected", test_scene) == 0);
    scene_free(test_scene);
    test_scene = NULL;
    return true;
}
/* A turret under the spawned ship, each with its own behavior. */
static bool ship_prototype()
{
    Transform hull = { .x = 3, .y = 2 }, turret = { .x = 5 };
    scene_component components[] = { { &COMPONENT_Transform, &hull },
                                     { &COMPONENT_Transform, &turret } };
    const char *behaviors[] = { "Old", "New" };
    scene_object objects[] = {
        { .name = "Ship",
          .components = &components[0],
          .component_count = 1,
          .behaviors = &behaviors[0],
          .behavior_count = 1 },
        { .name = "Turret",
          .components = &components[1],
          .component_count = 1,
          .behaviors = &behaviors[1],
          .behavior_count = 1 },
    };
    const int parents[] = { -1, 0 };
    scene_spec spec = { .name = "Ship",
                        .physics = physics_defaults(),
                        .objects = objects,
                        .object_count = 2,
                        .parents = parents };
    char error[192];
    test_scene = scene_prepare(&spec, error, sizeof error);
    REQUIRE(test_scene);
    REQUIRE(entities_register_prepared(source_entities_host(host), "Selected", test_scene) == 0);
    scene_free(test_scene);
    test_scene = NULL;
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
    Transform *transform = Transform_get(test_world, continuation.entity);
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
        ENTITY_SPAWN_COMMAND
            .begin(source_entities_host(host), NULL, &argument, 1, continuation.request, &error) == FLOW_OK
    );
    return true;
}
static bool finish()
{
    continuation.state = EXECUTION_OWNER_COMPLETED;
    execution_request_finish(continuation.request, FLOW_HOST_FINISHED);
    REQUIRE(entities_flush(source_entities_host(host)) == 0);
    continuation.request = NULL; /* Host completion consumed both references. */
    REQUIRE(continuation.detached);
    REQUIRE(source_begin(registry, 1, 0) == 0);
    return true;
}
TEST_CASE(native_prepared_spawn_generations)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int i = 0; i < 2; i++) {
        behavior_backend backend = backends[i];
        REQUIRE(type_catalog_find("Transform") == COMPONENT_Transform.type);
        test_world = ecs_world_new();
        registry = source_registry_new(test_world);
        host = source_entities_new(registry);
        source_entities_time(host, 1, 0, true);
        REQUIRE(register_behavior(backend, false));
        REQUIRE(register_behavior(backend, true));
        REQUIRE(prototype(3, "Old"));
        REQUIRE(queue(3, 33));
        REQUIRE(prototype(7, "New")); /* Pending request retains Old generation. */
        REQUIRE(entities_flush(source_entities_host(host)) == 1);
        REQUIRE(continuation.payload_valid && ecs_world_live_count(test_world) == 1);
        REQUIRE(old_begins == 0 && new_begins == 0);
        REQUIRE(finish());
        REQUIRE(old_begins == 1 && new_begins == 0 && observed == 33);
        REQUIRE(queue(7, 77));
        REQUIRE(entities_flush(source_entities_host(host)) == 1);
        REQUIRE(continuation.payload_valid && old_begins == 1 && new_begins == 0);
        REQUIRE(finish());
        REQUIRE(old_begins == 1 && new_begins == 1 && observed == 77);
        REQUIRE(queue(7, 88));
        REQUIRE(entities_flush(source_entities_host(host)) == 1);
        ecs_entity canceled = continuation.entity;
        REQUIRE(ecs_world_live_count(test_world) == 3);
        execution_request_detach(continuation.request);
        continuation.request = NULL;
        REQUIRE(entities_flush(source_entities_host(host)) == 0);
        REQUIRE(
            !ecs_entity_is_alive(test_world, canceled) &&
            ecs_world_live_count(test_world) == 2
        );
        REQUIRE(source_begin(registry, 2, 0) == 0);
        REQUIRE(old_begins == 1 && new_begins == 1);
        cleanup();
    }
    return true;
}
/* A prototype's children spawn under it and start their own behaviors; a
 * canceled spawn takes them with it. */
TEST_CASE(native_prepared_spawn_whole_tree)
{
    test_world = ecs_world_new();
    registry = source_registry_new(test_world);
    host = source_entities_new(registry);
    source_entities_time(host, 1, 0, true);
    REQUIRE(register_behavior(BEHAVIOR_BYTECODE, false));
    REQUIRE(register_behavior(BEHAVIOR_BYTECODE, true));
    REQUIRE(ship_prototype());
    REQUIRE(queue(3, 33));
    REQUIRE(entities_flush(source_entities_host(host)) == 1);
    ecs_entity ship = continuation.entity;
    REQUIRE(continuation.payload_valid && ecs_world_live_count(test_world) == 2);
    REQUIRE(ecs_pair_source_count(test_world, ECS_CHILD_OF, ship) == 1);
    REQUIRE(finish());
    REQUIRE(old_begins == 1 && new_begins == 1);
    REQUIRE(queue(3, 44));
    REQUIRE(entities_flush(source_entities_host(host)) == 1);
    REQUIRE(ecs_world_live_count(test_world) == 4);
    execution_request_detach(continuation.request);
    continuation.request = NULL;
    REQUIRE(entities_flush(source_entities_host(host)) == 0);
    REQUIRE(ecs_world_live_count(test_world) == 2 && ecs_entity_is_alive(test_world, ship));
    /* Destroying the ship stops its children's behaviors with it, including
     * a child reparented under it in the same boundary. */
    ecs_entity turret = ecs_pair_source(test_world, ECS_CHILD_OF, ship, 0);
    ecs_entity escort = ecs_world_create_entity(test_world);
    REQUIRE(source_attach(registry, escort, "New", true, 1, 0) == 0);
    REQUIRE(ecs_world_record_add_pair(test_world, escort, ECS_CHILD_OF, ship, 0) == ECS_OK);
    entities_request_destroy(source_entities_host(host), ship);
    REQUIRE(entities_flush(source_entities_host(host)) == 0);
    REQUIRE(ecs_world_live_count(test_world) == 0);
    REQUIRE(source_detach_entity(registry, turret) == 0);
    REQUIRE(source_detach_entity(registry, escort) == 0);
    cleanup();
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
        world_options options = { .modules = WORLD_PHYSICS | WORLD_MATERIALS,
                                  .fields = prepared_fields };
        simulation = world_new(&options, error, sizeof error);
        REQUIRE(simulation);
        prepared_fields = NULL;
        ecs_world *ecs = world_ecs(simulation);
        source_registry *sources = world_sources(simulation);
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
        REQUIRE(source_register(sources, "Clock", module, NULL) == 0);
        execution_module_free(module);
        module = NULL;
        Transform transform = { 0 };
        Velocity velocity = { .x = 1 };
        Body rigid = { .type = BODY_DYNAMIC };
        Collider collider = { .shape = COLLIDER_BOX, .width = 1, .height = 1 };
        const scene_component components[] = {
            { &COMPONENT_Transform, &transform },
            { &COMPONENT_Velocity, &velocity },
            { &COMPONENT_Body, &rigid },
            { &COMPONENT_Collider, &collider },
        };
        const char *behavior = "Clock";
        const scene_object object = { .name = "Moving",
                                      .components = components,
                                      .component_count = 4,
                                      .behaviors = &behavior,
                                      .behavior_count = 1 };
        const scene_spec spec = { .name = "Prepared",
                                  .physics = physics_defaults(),
                                  .objects = &object,
                                  .object_count = 1 };
        test_scene = scene_prepare(&spec, error, sizeof error);
        REQUIRE(test_scene);
        test_scene_instance = scene_instantiate(test_scene, ecs, sources, error, sizeof error);
        REQUIRE(test_scene_instance);
        scene_free(test_scene);
        test_scene = NULL;
        engine *runtime = world_engine(simulation);
        REQUIRE(engine_frame(runtime, 0) == 0);
        REQUIRE(engine_frame(runtime, .05) == 0);
        REQUIRE(engine_last_timing(runtime)->simulation_steps == 3 && ticks == 3);
        ecs_entity entity = scene_instance_entity(test_scene_instance, 0);
        const Transform *moved = Transform_get_const(ecs, entity);
        REQUIRE(moved && fabsf(moved->x - .05f) < .00001f && moved->y == 0);
        material_fields *catalog = materials_fields(world_materials(simulation));
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
    const char *error = INPUT_HELD_CALL.invoke(service, &action, &held);
    if (!error) {
        error = INPUT_PRESSED_CALL.invoke(service, &action, &pressed);
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
static behavior_function *host_body(
    const node_host_call *call,
    const node_access *access,
    const pin_value *in,
    int count,
    uint64_t identity
)
{
    behavior_function_builder *builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, in, count, NULL, 0, NULL, 0);
    int host_call = behavior_function_builder_host(builder, call, access, NULL, 0, NULL, 0);
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
        { "Jump", (UINT64_C(1) << CONTROL_Z) | (UINT64_C(1) << CONTROL_X), false },
    };
    /* A count without actions, an unnamed or empty-named action, one action
     * listed twice, an action without controls or with one outside
     * Control, or more than the most actions a map holds is rejected. */
    const input_action unnamed = { NULL, UINT64_C(1) << CONTROL_Z, false };
    const input_action empty = { "", UINT64_C(1) << CONTROL_Z, false };
    const input_action twice[] = {
        { "Jump", UINT64_C(1) << CONTROL_Z, false },
        { "Jump", UINT64_C(1) << CONTROL_X, false },
    };
    const input_action uncontrolled = { "Jump", 0, false };
    const input_action none = { "Jump", UINT64_C(1) << CONTROL_NONE, false };
    const input_action beyond = { "Jump", UINT64_C(1) << CONTROL_COUNT, false };
    static char names[INPUT_ACTION_MAX + 1][8];
    static input_action many[INPUT_ACTION_MAX + 1];
    for (int i = 0; i <= INPUT_ACTION_MAX; i++) {
        snprintf(names[i], sizeof names[i], "A%d", i);
        many[i] = (input_action){ names[i], UINT64_C(1) << CONTROL_Z, false };
    }
    world_options invalid[] = {
        { .modules = WORLD_INPUT, .action_count = 1 },
        { .modules = WORLD_INPUT, .actions = &unnamed, .action_count = 1 },
        { .modules = WORLD_INPUT, .actions = twice, .action_count = 2 },
        { .modules = WORLD_INPUT, .actions = &empty, .action_count = 1 },
        { .modules = WORLD_INPUT, .actions = &uncontrolled, .action_count = 1 },
        { .modules = WORLD_INPUT, .actions = &none, .action_count = 1 },
        { .modules = WORLD_INPUT, .actions = &beyond, .action_count = 1 },
        { .modules = WORLD_INPUT, .actions = many, .action_count = INPUT_ACTION_MAX + 1 },
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        REQUIRE(!world_new(&invalid[i], error, sizeof error));
        REQUIRE(!strcmp(error, "Invalid runtime world options"));
    }
    world_options most = { .modules = WORLD_INPUT,
                           .actions = many,
                           .action_count = INPUT_ACTION_MAX };
    world *full = world_new(&most, error, sizeof error);
    REQUIRE(full);
    world_free(full);
    world_options options = { .modules = WORLD_INPUT,
                              .actions = actions,
                              .action_count = 1 };
    simulation = world_new(&options, error, sizeof error);
    REQUIRE(simulation);
    ecs_world *ecs = world_ecs(simulation);
    static const node_host_call tick = { .service = INPUT_SERVICE,
                                         .invoke = observe_jump,
                                         .optional_service = true };
    static const node_host_call press = { .invoke = count_jump };
    const pin_value dt = { .type = PIN_F32 };
    behavior_function *tick_body = host_body(&tick, &INPUT_READ_ACCESS, &dt, 1, 990);
    behavior_function *press_body = host_body(&press, &INPUT_READ_ACCESS, NULL, 0, 980);
    REQUIRE(tick_body && press_body);
    execution_module_plan *plan = execution_module_plan_new();
    plan->id = 992;
    plan->valid = true;
    handler(plan, "Tick", 993, &dt, 1, tick_body);
    handler(plan, "Jump", 983, NULL, 0, press_body);
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    execution_module_plan_free(plan);
    REQUIRE(module);
    REQUIRE(source_register(world_sources(simulation), "Jumper", module, NULL) == 0);
    execution_module_free(module);
    module = NULL;
    ecs_entity jumper = ecs_world_create_entity(ecs);
    REQUIRE(Input_add(ecs, jumper, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    REQUIRE(source_attach(world_sources(simulation), jumper, "Jumper", false, 0, 0) == 0);
    *out = world_engine(simulation);
    REQUIRE(engine_frame(*out, 0) == 0);
    return true;
}
TEST_CASE(native_action_state_combines_sources_per_step)
{
    engine *runtime;
    REQUIRE(jump_world(&runtime));
    const uint64_t z = UINT64_C(1) << CONTROL_Z, x = UINT64_C(1) << CONTROL_X,
                   c = UINT64_C(1) << CONTROL_C;
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
    const uint64_t z = UINT64_C(1) << CONTROL_Z;
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
#define Z_DOWN (UINT64_C(1) << CONTROL_Z)
#define X_DOWN (UINT64_C(1) << CONTROL_X)
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
static ecs_entity crate;
/* Pushes the crate through the 2D physics the world bound, as Apply Impulse
 * does. */
static const char *kick(void *service, const pin_value *, pin_value *)
{
    CF_V2 impulse = { 32, 0 };
    const pin_value in[] = { { .u64 = ecs_entity_to_u64(crate) }, { .ptr = &impulse } };
    return PHYSICS_IMPULSE_CALL.invoke(service, in, NULL);
}
/* And through its 3D physics, as Apply Impulse 3D does. */
static const char *kick_3d(void *service, const pin_value *, pin_value *)
{
    CF_V3 impulse = { 64000, 0, 0 };
    const pin_value in[] = { { .u64 = ecs_entity_to_u64(crate) }, { .ptr = &impulse } };
    return PHYSICS3D_IMPULSE_CALL.invoke(service, in, NULL);
}
/* Registers a source whose Tick makes one host call, and attaches it to the
 * crate. */
static bool kicker(const char *name, const node_host_call *call, const node_access *access, uint64_t id)
{
    char error[192];
    const pin_value dt = { .type = PIN_F32 };
    behavior_function *body = host_body(call, access, &dt, 1, id);
    REQUIRE(body);
    execution_module_plan *plan = execution_module_plan_new();
    plan->id = id + 2;
    plan->valid = true;
    handler(plan, "Tick", 993, &dt, 1, body);
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    execution_module_plan_free(plan);
    REQUIRE(module);
    REQUIRE(source_register(world_sources(simulation), name, module, NULL) == 0);
    execution_module_free(module);
    module = NULL;
    REQUIRE(source_attach(world_sources(simulation), crate, name, false, 0, 0) == 0);
    return true;
}
/* The world binds its 2D and 3D physics as the services the push calls use:
 * a meter square weighs 1 kg in 2D, and a meter cube 1000 kg in 3D, as each
 * step writes to the bodies. */
TEST_CASE(native_world_binds_physics_for_pushes)
{
    cleanup();
    char error[192];
    world_options options = { .modules = WORLD_PHYSICS | WORLD_PHYSICS3D };
    simulation = world_new(&options, error, sizeof error);
    REQUIRE(simulation);
    ecs_world *ecs = world_ecs(simulation);
    crate = ecs_world_create_entity(ecs);
    REQUIRE(Transform_add(ecs, crate, 0) == ECS_OK);
    REQUIRE(Velocity_add(ecs, crate, 0) == ECS_OK);
    REQUIRE(Body_add(ecs, crate, 0) == ECS_OK);
    REQUIRE(Collider_add(ecs, crate, 0) == ECS_OK);
    REQUIRE(Transform3D_add(ecs, crate, 0) == ECS_OK);
    REQUIRE(Body3D_add(ecs, crate, 0) == ECS_OK);
    REQUIRE(Collider3D_add(ecs, crate, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    Body_get(ecs, crate)->type = BODY_DYNAMIC;
    *Collider_get(ecs, crate) = (Collider){ .shape = COLLIDER_BOX,
                                            .width = 32,
                                            .height = 32 };
    *Transform3D_get(ecs, crate) = (Transform3D){ .rotation = cf_quat_identity(),
                                                  .scale = { 1, 1, 1 } };
    Body3D_get(ecs, crate)->type = BODY_DYNAMIC;
    *Collider3D_get(ecs, crate) = (Collider3D){ .shape = COLLIDER3D_BOX,
                                                .size = { 32, 32, 32 } };
    static const node_host_call push = { .service = "game.physics", .invoke = kick };
    static const node_host_call push_3d = { .service = "game.physics3d",
                                            .invoke = kick_3d };
    REQUIRE(kicker("Kick", &push, &PHYSICS_PUSH_ACCESS, 960));
    REQUIRE(kicker("Kick3D", &push_3d, &PHYSICS3D_PUSH_ACCESS, 970));
    engine *runtime = world_engine(simulation);
    REQUIRE(engine_frame(runtime, 0) == 0);
    REQUIRE(engine_frame(runtime, 1.5 * ENGINE_FIXED_SECONDS) == 0);
    REQUIRE(engine_last_timing(runtime)->simulation_steps == 1);
    REQUIRE(fabsf(Velocity_get_const(ecs, crate)->x - 32) < .01f);
    REQUIRE(fabsf(Body3D_get_const(ecs, crate)->linear_velocity.x - 64) < .01f);
    REQUIRE(fabsf(Body_get_const(ecs, crate)->mass - 1) < 1e-5f);
    REQUIRE(fabsf(Body3D_get_const(ecs, crate)->mass - 1000) < .01f);
    cleanup();
    return true;
}
static bool resolve_player_host(void *, uint64_t id, execution_prepared_host *out)
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
        Transform transform = { .x = 4 };
        GlobalBinding binding = { .name = "Player" };
        scene_component components[] = { { &COMPONENT_Transform, &transform },
                                         { &COMPONENT_GlobalBinding, &binding } };
        const char *behavior = "Initial";
        scene_object object = { .name = "Initial",
                                .components = components,
                                .component_count = 2,
                                .behaviors = &behavior,
                                .behavior_count = 1 };
        scene_spec scene_spec = { .name = "Player",
                                  .physics = physics_defaults(),
                                  .objects = &object,
                                  .object_count = 1 };
        test_scene = scene_prepare(&scene_spec, error, sizeof error);
        REQUIRE(test_scene);
        const execution_prepared_linker linker = { .host = resolve_player_host };
        package_module record = { .name = "Initial",
                                  .id = 992,
                                  .data = module_bytes,
                                  .length = length };
        player_prototype prototype = { .name = "Selected", .scene = test_scene };
        player_spec spec = { .modules = &record,
                             .module_count = 1,
                             .linker = &linker,
                             .backend = backends[b],
                             .prototypes = &prototype,
                             .prototype_count = 1,
                             .scene = test_scene };
        char field_name[] = "message", default_text[] = "Saved payload";
        execution_module_event_field_info custom_field = {
            .name = field_name,
            .initial = { .type = PIN_STR, .str = default_text },
            .has_default = true
        };
        event_declaration declarations[] = {
            { .event = { .id = 993, .abi = 994, .name = "Begin" } },
            { .event = { .id = 996, .abi = 997, .name = "Unhandled", .field_count = 1 },
              .fields = &custom_field }
        };
        test_event_catalog = event_catalog_new(declarations, 2, NULL, error, sizeof error);
        REQUIRE(test_event_catalog);
        field_name[0] = 'X';
        default_text[0] = 'X';
        spec.world.events = test_event_catalog;
        record.name = "Missing";
        REQUIRE(!player_new(&spec, error, sizeof error));
        REQUIRE(ticks == 0 && scene_object_count(test_scene) == 1);
        record.name = "Initial";
        size_t player_length;
        player_application application = { .title = "Native",
                                           .width = 640,
                                           .height = 360,
                                           .fullscreen = true,
                                           .icon = "art/icon.png" };
        REQUIRE(
            player_prepared_write(&spec, &application, &player_bytes, &player_length, error, sizeof error)
        );
        char artifact_path[] = "/tmp/chibi-native-player-XXXXXX";
        int artifact_fd = mkstemp(artifact_path);
        REQUIRE(artifact_fd >= 0);
        bool written = write(artifact_fd, player_bytes, player_length) ==
                       (ssize_t)player_length;
        close(artifact_fd);
        player_application read = { 0 };
        bool read_back = written &&
                         player_application_read(artifact_path, &read, error, sizeof error);
        unlink(artifact_path);
        REQUIRE(read_back);
        REQUIRE(!strcmp(read.title, "Native") && !strcmp(read.icon, "art/icon.png"));
        REQUIRE(read.width == 640 && read.height == 360 && read.fullscreen);
        player_application_free(&read);
        /* An icon outside the resource root is refused, as on reading. */
        application.icon = "../outside.png";
        unsigned char *unsafe = NULL;
        size_t unsafe_length = 0;
        REQUIRE(
            !player_prepared_write(&spec, &application, &unsafe, &unsafe_length, error, sizeof error)
        );
        REQUIRE(!unsafe);
        application.icon = "art/icon.png";
        player_host player_host_context = {
            .components = { .components = REGISTRY_COMPONENTS,
                            .component_count = REGISTRY_COMPONENT_COUNT },
            .linker = &linker
        };
        REQUIRE(!player_prepared_read(
            player_bytes,
            player_length - 1,
            &player_host_context,
            backends[b],
            error,
            sizeof error
        ));
        REQUIRE(ticks == 0);
        player_host_context.decoded_budget = 1;
        REQUIRE(
            !player_prepared_read(player_bytes, player_length, &player_host_context, backends[b], error, sizeof error)
        );
        REQUIRE(ticks == 0);
        player_host_context.decoded_budget = 0;
        test_player = player_prepared_read(
            player_bytes,
            player_length,
            &player_host_context,
            backends[b],
            error,
            sizeof error
        );
        if (!test_player) {
            fprintf(stderr, "prepared player: %s\n", error);
        }
        REQUIRE(test_player);
        REQUIRE(ticks == 0);
        scene_free(test_scene);
        test_scene = NULL;
        free(module_bytes);
        module_bytes = NULL;
        free(player_bytes);
        player_bytes = NULL;
        event_catalog_free(test_event_catalog);
        test_event_catalog = NULL;
        world *runtime_world = player_world(test_player);
        entities *entities = world_entities(runtime_world);
        REQUIRE(entities_global_name_count(entities) == 1);
        REQUIRE(!strcmp(entities_global_name_at(entities, 0), "Player"));
        source_registry *sources = world_sources(runtime_world);
        execution_module_event_info event;
        execution_module_event_field_info saved_field;
        REQUIRE(source_event(sources, "Unhandled", &event));
        REQUIRE(event.id == 996 && event.abi == 997 && event.field_count == 1);
        REQUIRE(source_event_field(sources, "Unhandled", 0, &saved_field));
        REQUIRE(
            !strcmp(saved_field.name, "message") &&
            !strcmp(saved_field.initial.str, "Saved payload")
        );
        event_declaration wrong = { .event = { .id = 993, .abi = 999, .name = "Begin" } };
        wrong_events = event_catalog_new(&wrong, 1, NULL, error, sizeof error);
        REQUIRE(wrong_events);
        REQUIRE(source_set_events(sources, wrong_events) < 0);
        REQUIRE(source_event(sources, "Unhandled", &event) && event.id == 996);
        execution_module_event_field_info invalid_field = { .name = "invalid",
                                                            .initial = { .type = PIN_FLOW } };
        event_declaration invalid = {
            .event = { .id = 998, .abi = 999, .name = "Invalid", .field_count = 1 },
            .fields = &invalid_field
        };
        REQUIRE(!event_catalog_new(&invalid, 1, NULL, error, sizeof error));
        invalid_field.initial = (pin_value){ .type = PIN_STR, .str = "owner" };
        invalid_field.has_default = true;
        invalid_field.default_owner = true;
        REQUIRE(!event_catalog_new(&invalid, 1, NULL, error, sizeof error));

        ecs_entity entity = scene_instance_entity(player_scene(test_player), 0);
        REQUIRE(Transform_get_const(world_ecs(runtime_world), entity)->x == 4);
        REQUIRE(engine_frame(world_engine(runtime_world), 0) == 0);
        REQUIRE(engine_frame(world_engine(runtime_world), .02) == 0);
        REQUIRE(ticks == 1);
        REQUIRE(source_attachment_count(world_sources(runtime_world)) == 1);
        cleanup();
        test_world = ecs_world_new();
        ecs_entity caller_entity = ecs_world_create_entity(test_world);
        REQUIRE(ecs_entity_is_alive(test_world, caller_entity));
        world_options borrowed = { .ecs = test_world };
        simulation = world_new(&borrowed, error, sizeof error);
        REQUIRE(simulation && world_ecs(simulation) == test_world);
        world_free(simulation);
        simulation = NULL;
        REQUIRE(ecs_entity_is_alive(test_world, caller_entity));
        cleanup();
    }
    return true;
}
static scene *one_object_scene(const char *name, float x, const char *global)
{
    Transform transform = { .x = x };
    GlobalBinding binding = { .name = global };
    scene_component components[] = { { &COMPONENT_Transform, &transform },
                                     { &COMPONENT_GlobalBinding, &binding } };
    scene_object object = { .name = name,
                            .components = components,
                            .component_count = global ? 2 : 1 };
    scene_spec spec = { .name = name,
                        .physics = physics_defaults(),
                        .objects = &object,
                        .object_count = 1 };
    char error[192];
    return scene_prepare(&spec, error, sizeof error);
}
/* A parent and its child bound to one global name. */
static scene *twice_bound_scene(const char *global)
{
    GlobalBinding binding = { .name = global };
    scene_component component = { &COMPONENT_GlobalBinding, &binding };
    scene_object objects[] = {
        { .name = "First", .components = &component, .component_count = 1 },
        { .name = "Second", .components = &component, .component_count = 1 }
    };
    scene_spec spec = { .name = "Twice",
                        .physics = physics_defaults(),
                        .objects = objects,
                        .parents = (const int[]){ -1, 0 },
                        .object_count = 2 };
    char error[192];
    return scene_prepare(&spec, error, sizeof error);
}
static float first_x(const player *p)
{
    ecs_entity entity = scene_instance_entity(player_scene(p), 0);
    return Transform_get_const(world_ecs(player_world(p)), entity)->x;
}
/* Asks for a scene the way Load Scene does and starts it as hosts do. */
static bool follow(const char *name, char *error, size_t size)
{
    scene_flow *flow = world_scenes(player_world(test_player));
    pin_value in = { .type = PIN_STR, .str = name };
    if (SCENE_LOAD_CALL.invoke(flow, &in, NULL)) {
        return false;
    }
    char *next = scene_flow_take(flow);
    bool loaded = player_load_scene(test_player, next, error, size);
    free(next);
    return loaded;
}
TEST_CASE(native_player_loads_named_scenes)
{
    char error[192];
    test_scene = one_object_scene("Hero", 1, "Player");
    other_scene = one_object_scene("Goal", 2, NULL);
    REQUIRE(test_scene && other_scene);
    player_named_scene named = { "Start", other_scene };
    player_spec spec = { .world = { .modules = WORLD_AUDIO },
                         .scene = test_scene,
                         .scene_name = "Start",
                         .scenes = &named,
                         .scene_count = 1 };
    REQUIRE(!player_new(&spec, error, sizeof error));
    size_t length;
    player_application application = { 0 };
    /* Entering a scene that binds one global to two entities fails, so the
       player refuses it when the package is written or opened. */
    scene *twice = twice_bound_scene("Player");
    REQUIRE(twice);
    named = (player_named_scene){ "Twice", twice };
    REQUIRE(!player_new(&spec, error, sizeof error));
    REQUIRE(!strcmp(error, "Scene 'Twice' binds two entities to the global 'Player'"));
    error[0] = 0;
    REQUIRE(
        !player_prepared_write(&spec, &application, &player_bytes, &length, error, sizeof error)
    );
    REQUIRE(!strcmp(error, "Scene 'Twice' binds two entities to the global 'Player'"));
    /* So does a prototype tree that binds it twice, as writing it would. */
    named = (player_named_scene){ "Next", other_scene };
    spec.prototypes = &(player_prototype){ "Twice", twice };
    spec.prototype_count = 1;
    error[0] = 0;
    REQUIRE(!player_new(&spec, error, sizeof error));
    REQUIRE(!strcmp(error, "Scene 'Twice' binds two entities to the global 'Player'"));
    spec.prototypes = NULL;
    spec.prototype_count = 0;
    scene_free(twice);
    REQUIRE(
        player_prepared_write(&spec, &application, &player_bytes, &length, error, sizeof error)
    );
    player_host host_context = {
        .components = { .components = REGISTRY_COMPONENTS,
                        .component_count = REGISTRY_COMPONENT_COUNT },
        .linker = &(execution_prepared_linker){ .host = resolve_player_host }
    };
    test_player = player_prepared_read(player_bytes, length, &host_context, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(test_player);
    world *w = player_world(test_player);
    scene_flow *flow = world_scenes(w);
    save *save = world_modules(w)->save;
    REQUIRE(!strcmp(scene_flow_current(flow), "Start"));
    REQUIRE(first_x(test_player) == 1);
    REQUIRE(entities_global_name_count(world_entities(w)) == 1);
    engine *runtime = world_engine(w);
    REQUIRE(engine_frame(runtime, 0) == 0 && engine_frame(runtime, .05) == 0);
    uint64_t steps = engine_last_timing(runtime)->simulation_steps;
    REQUIRE(steps > 0);

    pin_value missing = { .type = PIN_STR, .str = "Missing" };
    REQUIRE(!strcmp(SCENE_LOAD_CALL.invoke(flow, &missing, NULL), "No scene named 'Missing'"));
    REQUIRE(!scene_flow_take(flow));

    REQUIRE(save_set(save, "level", "1") && save_commit(save));
    REQUIRE(save_set(save, "unsaved", "1"));
    audio *audio = engine_audio(runtime);
    REQUIRE(audio_tone(audio, (tone){ 440, 220, 0.1f, 0.5f, 0.5f, 0 }));
    uint64_t cues = audio_hash(audio);
    REQUIRE(follow("Next", error, sizeof error));
    REQUIRE(!strcmp(scene_flow_current(flow), "Next"));
    REQUIRE(ecs_world_live_count(world_ecs(w)) == 1);
    REQUIRE(first_x(test_player) == 2);
    REQUIRE(entities_global_name_count(world_entities(w)) == 0);
    REQUIRE(!strcmp(save_get(save, "level"), "1") && !save_get(save, "unsaved"));
    /* The clock and sounds go on across a load. */
    REQUIRE(audio_count(audio) == 1 && audio_hash(audio) == cues);
    REQUIRE(engine_frame(runtime, .1) == 0);
    REQUIRE(engine_last_timing(runtime)->simulation_steps > steps);

    REQUIRE(!SCENE_RELOAD_CALL.invoke(flow, NULL, NULL));
    char *next = scene_flow_take(flow);
    REQUIRE(!strcmp(next, "Next"));
    free(next);
    REQUIRE(follow("Start", error, sizeof error));
    REQUIRE(ecs_world_live_count(world_ecs(w)) == 1);
    REQUIRE(first_x(test_player) == 1);
    REQUIRE(entities_global_name_count(world_entities(w)) == 1);

    REQUIRE(!player_load_scene(test_player, "Missing", error, sizeof error));
    REQUIRE(!strcmp(error, "No scene named 'Missing'"));
    engine_reset(runtime);
    REQUIRE(audio_count(audio) == 0);
    return true;
}
static int staged_sounds;
static void *stage_sounds(void *, const player_asset *assets, int count, size_t *, char *, size_t)
{
    staged_sounds = count && assets[0].kind == PLAYER_ASSET_SOUND ? count : -1;
    return &staged_sounds;
}
static bool any_scene(void *, void *, const scene *, char *, size_t)
{
    return true;
}
static void keep_sounds(void *, void *, world *) {}
static void drop_sounds(void *, void *) {}
static const char *call(const node_host_call *host, audio *audio, pin_value a, pin_value b)
{
    pin_value in[] = { a, b, { .type = PIN_F32, .f = 0.5f } };
    return host->invoke(audio, in, NULL);
}
TEST_CASE(native_player_plays_sounds)
{
    char error[192];
    Transform transform = { .x = 1 };
    scene_component component = { &COMPONENT_Transform, &transform };
    scene_object object = { .name = "Hero",
                            .components = &component,
                            .component_count = 1 };
    scene_spec scene_spec = { .name = "Start",
                              .physics = physics_defaults(),
                              .objects = &object,
                              .object_count = 1 };
    test_scene = scene_prepare(&scene_spec, error, sizeof error);
    REQUIRE(test_scene);
    /* Mono 16-bit PCM at 44.1 kHz with two samples. */
    static const unsigned char wav[] = {
        'R',  'I',  'F', 'F', 40,   0,    0, 0, 'W', 'A', 'V', 'E',
        'f',  'm',  't', ' ', 16,   0,    0, 0, 1,   0,   1,   0,
        0x44, 0xAC, 0,   0,   0x88, 0x58, 1, 0, 2,   0,   16,  0,
        'd',  'a',  't', 'a', 4,    0,    0, 0, 0,   0,   0,   0
    };
    player_asset sound = { .name = "Jump",
                           .kind = PLAYER_ASSET_SOUND,
                           .data = wav,
                           .length = sizeof wav };
    player_resource_host resources = { .stage = stage_sounds,
                                       .scene = any_scene,
                                       .commit = keep_sounds,
                                       .discard = drop_sounds,
                                       .retire = drop_sounds };
    player_spec spec = { .world = { .modules = WORLD_AUDIO },
                         .scene = test_scene,
                         .assets = &sound,
                         .asset_count = 1,
                         .resources = resources };
    size_t length;
    player_application application = { 0 };
    REQUIRE(
        player_prepared_write(&spec, &application, &player_bytes, &length, error, sizeof error)
    );
    player_host host_context = {
        .components = { .components = REGISTRY_COMPONENTS,
                        .component_count = REGISTRY_COMPONENT_COUNT },
        .linker = &(execution_prepared_linker){ .host = resolve_player_host },
        .resources = resources
    };
    test_player = player_prepared_read(player_bytes, length, &host_context, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(test_player && staged_sounds == 1);
    audio *sounds = world_modules(player_world(test_player))->audio;
    REQUIRE(audio_sound_count(sounds) == 1);
    REQUIRE(!memcmp(audio_sound(sounds, 0, &length), wav, sizeof wav) && length == sizeof wav);
    /* Recordings identify the library by its names and files. */
    audio *library = audio_new();
    uint64_t empty = audio_library_hash(library);
    audio_add_sound(library, "Jump", wav, sizeof wav);
    REQUIRE(audio_library_hash(library) == audio_library_hash(sounds));
    REQUIRE(audio_library_hash(library) != empty);
    audio_add_sound(library, "Land", wav, sizeof wav);
    REQUIRE(audio_library_hash(library) != audio_library_hash(sounds));
    audio_free(library);
    /* The library's records, names and copies count against the retained
     * storage budget. */
    size_t budget = 1 << 20;
    spec.decoded_remaining = &budget;
    player *charged = player_new(&spec, error, sizeof error);
    REQUIRE(charged);
    player_free(charged);
    size_t used = (1 << 20) - budget;
    REQUIRE(used > sizeof wav + sizeof "Jump");
    budget = used - 1;
    REQUIRE(!player_new(&spec, error, sizeof error) && budget == used - 1);
    REQUIRE(!strcmp(error, "Prepared player retained storage budget exceeded"));

    pin_value jump = { .type = PIN_STR, .str = "Jump" },
              missing = { .type = PIN_STR, .str = "Missing" };
    pin_value quarter = { .type = PIN_F32, .f = 0.25f },
              two = { .type = PIN_F32, .f = 2 };
    REQUIRE(!call(&AUDIO_SOUND_CALL, sounds, jump, quarter));
    REQUIRE(!strcmp(call(&AUDIO_SOUND_CALL, sounds, missing, quarter), "No sound named 'Missing'"));
    REQUIRE(call(&AUDIO_SOUND_CALL, sounds, jump, two));
    audio_voice voices[2];
    REQUIRE(audio_take(sounds, voices, 2) == 1 && audio_count(sounds) == 1);
    REQUIRE(voices[0].sound == 0 && voices[0].volume == 0.25f && voices[0].pan == 0.5f);
    REQUIRE(audio_tone(sounds, (tone){ 440, 220, 0.1f, 0.5f, 0.5f, 0 }));
    REQUIRE(audio_take(sounds, voices, 2) == 1 && voices[0].sound == -1);

    float fade;
    uint64_t started, serial;
    REQUIRE(!call(&AUDIO_MUSIC_CALL, sounds, jump, two));
    REQUIRE(audio_music(sounds, &fade, &started) == 0 && fade == 2);
    /* The playing track keeps playing when a scene asks for it again. */
    REQUIRE(!call(&AUDIO_MUSIC_CALL, sounds, jump, quarter));
    REQUIRE(audio_music(sounds, &fade, &serial) == 0 && serial == started && fade == 2);
    REQUIRE(!call(&AUDIO_STOP_MUSIC_CALL, sounds, quarter, quarter));
    REQUIRE(audio_music(sounds, &fade, &serial) == -1 && serial != started && fade == 0.25f);
    REQUIRE(!call(&AUDIO_MUSIC_VOLUME_CALL, sounds, quarter, quarter));
    REQUIRE(call(&AUDIO_SOUND_VOLUME_CALL, sounds, two, two));
    REQUIRE(audio_volume(sounds, AUDIO_MUSIC) == 0.25f);
    REQUIRE(audio_volume(sounds, AUDIO_MASTER) == 1 && audio_volume(sounds, AUDIO_SOUNDS) == 1);

    uint64_t generation = audio_generation(sounds), hash = audio_hash(sounds);
    REQUIRE(hash && !call(&AUDIO_MUSIC_CALL, sounds, jump, two) && audio_hash(sounds) != hash);
    REQUIRE(!call(&AUDIO_SOUND_CALL, sounds, jump, quarter));
    REQUIRE(!call(&AUDIO_MASTER_VOLUME_CALL, sounds, quarter, quarter));
    REQUIRE(!call(&AUDIO_SOUND_VOLUME_CALL, sounds, quarter, quarter));
    REQUIRE(audio_volume(sounds, AUDIO_SOUNDS) == 0.25f);
    engine_reset(world_engine(player_world(test_player)));
    REQUIRE(
        audio_generation(sounds) != generation &&
        audio_music(sounds, &fade, &serial) == -1
    );
    REQUIRE(!audio_hash(sounds) && !audio_count(sounds));
    REQUIRE(!audio_take(sounds, voices, 2) && audio_sound_count(sounds) == 1);
    for (int i = 0; i < AUDIO_BUSES; i++) {
        REQUIRE(audio_volume(sounds, i) == 1);
    }
    static const unsigned char text[] = "not a sound file";
    sound = (player_asset){ .name = "Text",
                            .kind = PLAYER_ASSET_SOUND,
                            .data = text,
                            .length = sizeof text };
    REQUIRE(!player_asset_valid(&sound));
    /* Players only take sounds they can decode: not WAVE_FORMAT_EXTENSIBLE,
     * surround, 12-bit samples, less than one whole frame, or 48 kHz, which
     * the 44.1 kHz mixer would play slow and low. */
    static const struct
    {
        int at, value;
    } undecodable[] = { { 20, 0xFFFE }, { 22, 6 }, { 34, 12 }, { 40, 1 }, { 24, 48000 } };
    for (size_t i = 0; i < sizeof undecodable / sizeof *undecodable; i++) {
        unsigned char edited[sizeof wav];
        memcpy(edited, wav, sizeof wav);
        edited[undecodable[i].at] = undecodable[i].value & 0xFF;
        edited[undecodable[i].at + 1] = undecodable[i].value >> 8;
        REQUIRE(!sound_format_of(edited, sizeof edited));
    }
    REQUIRE(!sound_format_of(wav, sizeof wav - 1)); /* samples cut short */
    unsigned char real[sizeof wav];
    memcpy(real, wav, sizeof wav);
    real[20] = 3, real[34] = 32; /* one float sample */
    REQUIRE(sound_format_of(real, sizeof real) == SOUND_WAV);
    /* Chunks pad to even sizes, and samples only count after the format. */
    unsigned char padded[sizeof wav + 10] = { 0 };
    memcpy(padded, wav, 36);
    memcpy(padded + 36, "LIST\1\0\0\0", 8);
    memcpy(padded + 46, wav + 36, 12);
    REQUIRE(sound_format_of(padded, sizeof padded) == SOUND_WAV);
    unsigned char early[sizeof wav];
    memcpy(early, wav, 12);
    memcpy(early + 12, wav + 36, 12);
    memcpy(early + 24, wav + 12, 24);
    REQUIRE(!sound_format_of(early, sizeof early));
    /* A page holding only a stereo 44.1 kHz Vorbis identification header, an
     * empty page that ends the stream, and a few trailing bytes. */
    unsigned char ogg[88] = { 'O', 'g', 'g', 'S' };
    ogg[5] = 2, ogg[26] = 1, ogg[27] = 30, ogg[28] = 1, ogg[39] = 2;
    ogg[40] = 0x44, ogg[41] = 0xAC;
    memcpy(ogg + 29, "vorbis", 6);
    memcpy(ogg + 58, "OggS", 4);
    ogg[63] = 4;
    memcpy(ogg + 85, "TAG", 3);
    REQUIRE(sound_format_of(ogg, sizeof ogg) == SOUND_OGG);
    REQUIRE(sound_format_of(ogg, 85) == SOUND_OGG);
    /* Cut inside the header or the last page, or before the stream ends. */
    REQUIRE(!sound_format_of(ogg, 40));
    REQUIRE(!sound_format_of(ogg, 84));
    REQUIRE(!sound_format_of(ogg, 58));
    /* The first page must not end the stream. */
    ogg[5] = 6;
    REQUIRE(!sound_format_of(ogg, sizeof ogg));
    ogg[5] = 2;
    ogg[40] = 0x80, ogg[41] = 0xBB; /* 48 kHz */
    REQUIRE(!sound_format_of(ogg, sizeof ogg));
    ogg[40] = 0x44, ogg[41] = 0xAC;
    ogg[39] = 6;
    REQUIRE(!sound_format_of(ogg, sizeof ogg));
    memcpy(ogg + 28, "OpusHead", 8);
    ogg[39] = 2;
    REQUIRE(!sound_format_of(ogg, sizeof ogg));
    sound = (player_asset){ .name = "Jump",
                            .kind = PLAYER_ASSET_SOUND,
                            .length = sizeof wav,
                            .path = "content/sounds/Jump.wav",
                            .content_hash = 1,
                            .external = true };
    REQUIRE(!player_asset_valid(&sound));
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
    REQUIRE(project_components().component_count == REGISTRY_COMPONENT_COUNT);
    declared_components_free(none);
    REQUIRE(type_catalog_find("Charge") == type);
    ecs_component_catalog components = declared_components_ecs(declared);
    REQUIRE(components.component_count == 1 && components.components[0]->type == type);
    REQUIRE(components.components[0]->layout_hash == type_layout_hash(type));
    ecs_component_catalog project = project_components();
    REQUIRE(script_catalog_project() == declared_components_catalog(declared));
    REQUIRE(project.component_count == REGISTRY_COMPONENT_COUNT + 1);
    REQUIRE(project.components[REGISTRY_COMPONENT_COUNT] == components.components[0]);
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
    scene_component data = { components.components[0], &row };
    scene_object object = { .name = "Cell", .components = &data, .component_count = 1 };
    scene_spec scene_spec = { .name = "Declared",
                              .physics = physics_defaults(),
                              .objects = &object,
                              .object_count = 1 };
    test_scene = scene_prepare(&scene_spec, error, sizeof error);
    REQUIRE(test_scene);
    /* The input map travels with the package. */
    const input_action jump_map = { "Jump", UINT64_C(1) << CONTROL_Z, false };
    player_spec spec = { .world = { .components = components,
                                    .actions = &jump_map,
                                    .action_count = 1 },
                         .declared = declared,
                         .scene = test_scene };
    /* A player keeps its own set's layout while a newer set is published. */
    declared_component smaller = { .name = "Charge", .fields = &level, .field_count = 1 };
    declared_components *newer = declared_components_new(&smaller, 1, error, sizeof error);
    REQUIRE(newer && type_catalog_find("Charge")->size != type->size);
    test_player = player_new(&spec, error, sizeof error);
    REQUIRE(test_player);
    ecs_world *own = world_ecs(player_world(test_player));
    REQUIRE(ecs_world_component(own, charge_id)->type == type);
    player_free(test_player);
    test_player = NULL;
    declared_components_free(newer);
    player_application application = { 0 };
    size_t length;
    /* A map the world would reject, such as a count without actions or an
     * action without controls, is rejected before it is written. */
    const input_action loose = { "Jump", 0, false };
    const input_action *rejected[] = { NULL, &loose };
    for (int i = 0; i < 2; i++) {
        spec.world.actions = rejected[i];
        REQUIRE(
            !player_prepared_write(&spec, &application, &player_bytes, &length, error, sizeof error)
        );
        REQUIRE(strstr(error, "Invalid prepared player record metadata") && !player_bytes);
    }
    spec.world.actions = &jump_map;
    REQUIRE(
        player_prepared_write(&spec, &application, &player_bytes, &length, error, sizeof error)
    );
    scene_free(test_scene);
    test_scene = NULL;
    declared_components_free(declared);
    REQUIRE(!type_catalog_find("Charge") && type_catalog_find("Battery"));
    REQUIRE(script_catalog_project() == declared_components_catalog(older));
    declared_components_free(older);
    REQUIRE(!type_catalog_find("Battery") && !script_catalog_project());
    REQUIRE(project_components().component_count == REGISTRY_COMPONENT_COUNT);
    /* A measure's unit and range reach its type field for editors. */
    declared_field rate = { .name = "rate",
                            .type = "number",
                            .unit = "u/s",
                            .min = 0,
                            .max = 10 };
    declared_component measured = { .name = "Charge", .fields = &rate, .field_count = 1 };
    declared_components *hinted = declared_components_new(&measured, 1, error, sizeof error);
    REQUIRE(hinted);
    const type_field *hint = &type_catalog_find("Charge")->fields[0];
    REQUIRE(
        hint->type == PIN_F64 && !strcmp(hint->unit, "u/s") && hint->min == 0 &&
        hint->max == 10
    );
    declared_components_free(hinted);
    player_host player_host_context = {
        .components = { .components = REGISTRY_COMPONENTS,
                        .component_count = REGISTRY_COMPONENT_COUNT }
    };
    test_player = player_prepared_read(
        player_bytes,
        length,
        &player_host_context,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    if (!test_player) {
        fprintf(stderr, "prepared player: %s\n", error);
    }
    REQUIRE(test_player);
    REQUIRE(type_catalog_find("Charge"));
    ecs_world *ecs = world_ecs(player_world(test_player));
    ecs_entity cell = scene_instance_entity(player_scene(test_player), 0);
    const charge *read = ecs_component_data_const(ecs, cell, charge_id);
    REQUIRE(read && read->level == 1.25f && read->cycles == 2 && read->full);
    REQUIRE(read->drift.x == 1 && read->drift.y == -1);
    REQUIRE(project_components().component_count == REGISTRY_COMPONENT_COUNT + 1);
    player_free(test_player);
    test_player = NULL;
    REQUIRE(!type_catalog_find("Charge"));
    return true;
}
void test_tool_native()
{
    test_native_catalog_enable();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_prepared_spawn_generations);
    RUN_TEST_CASE(native_prepared_spawn_whole_tree);
    RUN_TEST_CASE(native_prepared_world_runs_source_and_real_physics);
    RUN_TEST_CASE(native_player_loads_package_and_retires_producers);
    RUN_TEST_CASE(native_player_loads_named_scenes);
    RUN_TEST_CASE(native_player_plays_sounds);
    RUN_TEST_CASE(native_player_reads_declared_components);
    RUN_TEST_CASE(native_action_state_combines_sources_per_step);
    RUN_TEST_CASE(native_recorded_taps_replay_per_step);
    RUN_TEST_CASE(native_pause_keeps_held_actions);
    RUN_TEST_CASE(native_world_binds_physics_for_pushes);
    pu_clear_setup();
}
