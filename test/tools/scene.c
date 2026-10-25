#include "runtime/scenes/scene.h"

#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/status.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int32_t value;
    const char *label;
} scene_payload;
static const type_field fields[] = {
    { .name = "value",
      .type = PIN_I32,
      .size = sizeof(int32_t),
      .offset = offsetof(scene_payload, value),
      .type_name = "int" },
    { .name = "label",
      .type = PIN_STR,
      .size = sizeof(const char *),
      .offset = offsetof(scene_payload, label),
      .type_name = "const char *" },
};
static const type_desc payload_type = { .name = "scene.payload",
                                        .size = sizeof(scene_payload),
                                        .fields = fields,
                                        .field_count = 2 };
static const ecs_component_desc payload_component = { .id = 950,
                                                      .key = "scene.payload",
                                                      .name = "ScenePayload",
                                                      .element_size = sizeof(scene_payload),
                                                      .type = &payload_type };
static ecs_world *world;
static scene *document;
static scene_instance *instances[2];
static source_registry *registry;
static behavior_function_builder *builder;
static execution_module_plan *plan;
static execution_module *module;
static char *input_label;
static int begins;
static bool all_ready;

static void cleanup()
{
    scene_instance_free(instances[0]);
    scene_instance_free(instances[1]);
    instances[0] = instances[1] = NULL;
    scene_free(document);
    document = NULL;
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
    ecs_world_free(world);
    world = NULL;
    free(input_label);
    input_label = NULL;
    begins = 0;
    all_ready = true;
}
static const char *observe(void *, const pin_value *, pin_value *)
{
    begins++;
    all_ready &= ecs_world_live_count(world) == 4;
    ecs_query query;
    ecs_component_id id = payload_component.id;
    int count = 0;
    if (ecs_query_begin(world, &query, &id, 1) != ECS_OK) {
        return "scene query unavailable";
    }
    ecs_entity entity;
    while (count < 5 && ecs_query_next(&query, &entity) > 0) {
        const scene_payload *p = ecs_component_data(world, entity, id);
        all_ready &= p && p->label && !strcmp(p->label, "owned label") &&
                     (p->value == 7 || p->value == 9);
        count++;
    }
    ecs_query_end(&query);
    all_ready &= count == 4;
    return NULL;
}
static bool register_module(behavior_backend backend)
{
    builder = behavior_function_builder_new();
    static const node_host_call host = { .invoke = observe };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int index = behavior_function_builder_host(builder, &host, &access, NULL, 0, NULL, 0);
    /* Named registry publication verifies the complete portable capability closure. */
    if (!behavior_function_builder_host_identity(builder, index, 955, 956)) {
        return false;
    }
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .target = index,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[192];
    behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
    if (!body) {
        fprintf(stderr, "scene builder: %s\n", error);
        return false;
    }
    plan = execution_module_plan_new();
    plan->id = 951;
    plan->valid = true;
    apush(plan->events, ((execution_module_event_declaration){ .id = 952, .abi = 953, .name = "Begin" }));
    plan->event_count = 1;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 954,
                                      .kind = EXECUTION_FUNCTION_HANDLER,
                                      .event = 952,
                                      .event_abi = 953,
                                      .body = body })
    );
    plan->function_count = 1;
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "scene module: %s\n", error);
        return false;
    }
    registry = source_registry_new(world);
    bool registered = source_register(registry, "Observe", module, NULL) == 0;
    if (!registered) {
        fprintf(stderr, "scene registration: %s\n", source_error(registry));
    }
    execution_module_free(module);
    module = NULL;
    behavior_function_builder_free(builder);
    builder = NULL;
    return registered;
}
static bool prepare_scene(bool missing)
{
    input_label = malloc(12);
    REQUIRE(input_label);
    memcpy(input_label, "owned label", 12);
    scene_payload payloads[] = { { 7, input_label }, { 9, input_label } };
    scene_component components[] = { { &payload_component, &payloads[0] },
                                     { &payload_component, &payloads[1] } };
    const char *first[] = { "Observe" },
               *second[] = { missing ? "Missing" : "Observe" };
    scene_object objects[] = {
        { .name = "First",
          .components = &components[0],
          .component_count = 1,
          .behaviors = first,
          .behavior_count = 1 },
        { .name = "Second",
          .components = &components[1],
          .component_count = 1,
          .behaviors = second,
          .behavior_count = 1 },
    };
    char name[] = "OwnedScene", ui[] = "OwnedUI";
    scene_spec spec = { .name = name,
                        .ui = ui,
                        .perspective = true,
                        .physics = physics_defaults(),
                        .objects = objects,
                        .object_count = 2 };
    char error[192];
    document = scene_prepare(&spec, error, sizeof error);
    REQUIRE(document);
    memset(name, 'x', sizeof name - 1);
    memset(ui, 'x', sizeof ui - 1);
    memset(input_label, 'x', 11);
    free(input_label);
    input_label = NULL;
    REQUIRE(!strcmp(scene_name(document), "OwnedScene") && !strcmp(scene_ui(document), "OwnedUI"));
    REQUIRE(scene_perspective(document) && scene_object_count(document) == 2);
    REQUIRE(physics_settings_equal(scene_physics(document), physics_defaults()));
    REQUIRE(!strcmp(scene_object_name(document, 1), "Second"));
    return scene_register_components(document, world, error, sizeof error) == 0;
}
TEST_CASE(scene_snapshot_and_complete_deferred_begin)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        world = ecs_world_new();
        REQUIRE(register_module(backends[b]));
        REQUIRE(prepare_scene(false));
        char error[192];
        for (int i = 0; i < 2; i++) {
            instances[i] = scene_instantiate(document, world, registry, error, sizeof error);
            if (!instances[i]) {
                fprintf(stderr, "scene instantiate: %s\n", error);
            }
            REQUIRE(instances[i]);
        }
        REQUIRE(begins == 0 && ecs_world_live_count(world) == 4);
        scene_free(document);
        document = NULL;
        REQUIRE(source_begin(registry, 0, 0) == FLOW_OK);
        REQUIRE(begins == 4 && all_ready);
        ecs_entity first = scene_instance_entity(instances[0], 0);
        ecs_entity other = scene_instance_entity(instances[1], 0);
        REQUIRE(first.slot != other.slot);
        pin_value changed = { .type = PIN_STR,
                              .text = &(const pin_string){
                                  .bytes = "changed",
                                  .length = sizeof("changed") - 1 } };
        REQUIRE(
            ecs_component_write_value(
                world,
                first,
                payload_component.id,
                offsetof(scene_payload, label),
                changed,
                changed
            ) == ECS_OK
        );
        const scene_payload
            *a = ecs_component_data(world, first, payload_component.id);
        const scene_payload
            *c = ecs_component_data(world, other, payload_component.id);
        REQUIRE(a && c && !strcmp(a->label, "changed") && !strcmp(c->label, "owned label"));
        scene_instance_free(instances[0]);
        instances[0] = NULL;
        REQUIRE(ecs_world_live_count(world) == 2 && !ecs_entity_is_alive(world, first));
        REQUIRE(
            execution_program_instance(source_program(registry, "Observe"), ecs_entity_to_u64(first)) == NULL
        );
        REQUIRE(source_begin(registry, 1, 0) == FLOW_OK && begins == 4);
    }
    return true;
}
TEST_CASE(scene_failure_preflight_and_pending_begin_cancellation)
{
    world = ecs_world_new();
    REQUIRE(register_module(BEHAVIOR_BYTECODE));
    REQUIRE(prepare_scene(true));
    char error[192];
    ecs_entity existing = ecs_world_create_entity(world);
    uint64_t before = ecs_world_hash(world);
    instances[0] = scene_instantiate(document, world, registry, error, sizeof error);
    REQUIRE(!instances[0] && strstr(error, "not registered"));
    REQUIRE(
        ecs_world_live_count(world) == 1 && ecs_entity_is_alive(world, existing) &&
        ecs_world_hash(world) == before && begins == 0
    );
    scene_free(document);
    document = NULL;
    REQUIRE(prepare_scene(false));
    instances[0] = scene_instantiate(document, world, registry, error, sizeof error);
    if (!instances[0]) {
        fprintf(stderr, "scene instantiate: %s\n", error);
    }
    REQUIRE(instances[0] && begins == 0);
    ecs_entity entity = scene_instance_entity(instances[0], 0);
    scene_instance_free(instances[0]);
    instances[0] = NULL;
    REQUIRE(
        ecs_world_live_count(world) == 1 && ecs_entity_is_alive(world, existing) &&
        !ecs_entity_is_alive(world, entity)
    );
    REQUIRE(source_begin(registry, 1, 0) == FLOW_OK && begins == 0);
    REQUIRE(
        execution_program_instance(source_program(registry, "Observe"), ecs_entity_to_u64(entity)) == NULL
    );
    return true;
}
void test_tool_scene()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scene_snapshot_and_complete_deferred_begin);
    RUN_TEST_CASE(scene_failure_preflight_and_pending_begin_cancellation);
    pu_clear_setup();
}
