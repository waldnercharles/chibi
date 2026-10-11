#include "headless_consumer.h"

#include "execution/log.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "runtime/behaviors/source.h"
#include "third_party/pico_headers/pico_unit.h"

#include <string.h>

typedef struct
{
    int32_t value;
} headless_counter;
static const type_field counter_field = {
    .name = "value",
    .type = PIN_I32,
    .size = sizeof(int32_t),
    .type_name = "int",
};
static const type_desc counter_type = {
    .name = "HeadlessCounter",
    .size = sizeof(headless_counter),
    .fields = &counter_field,
    .field_count = 1,
};
static const ecs_component_desc counter_component = {
    .id = 801,
    .key = "headless.counter",
    .name = "HeadlessCounter",
    .element_size = sizeof(headless_counter),
    .type = &counter_type,
};
static ecs_world *ecs;
static source_registry *behaviors;
static execution_module *pending;

static bool prepared_type(void *, uint64_t id, execution_prepared_type *out)
{
    if (id == 700) {
        *out = (execution_prepared_type){ .name = "ecs.entity",
                                          .storage = PIN_HANDLE,
                                          .size = sizeof(uint64_t) };
        return true;
    }
    if (id != 101) {
        return false;
    }
    *out = (execution_prepared_type){ .name = counter_type.name,
                                      .type = &counter_type,
                                      .storage = PIN_STRUCT,
                                      .size = counter_type.size,
                                      .layout = type_layout_hash(&counter_type) };
    return true;
}
static bool prepared_host(void *user, uint64_t id, execution_prepared_host *out)
{
    if (id != 13) {
        return false;
    }
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (execution_prepared_host){ .call = &EXECUTION_PrintString,
                                      .access = &access,
                                      .abi = *(uint64_t *)user };
    return true;
}
execution_module *headless_consumer_read(const void *data, size_t length, behavior_backend backend, uint64_t print_abi)
{
    execution_prepared_linker linker = { .user = &print_abi,
                                         .type = prepared_type,
                                         .host = prepared_host };
    char error[256];
    execution_module *module = execution_prepared_read(data, length, &linker, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "headless prepared: %s\n", error);
    }
    return module;
}
const ecs_component_desc *headless_consumer_component()
{
    return &counter_component;
}
void headless_consumer_cleanup()
{
    if (behaviors) {
        source_registry_free(behaviors);
    }
    execution_module_free(pending);
    ecs_world_free(ecs);
    pending = NULL;
    behaviors = NULL;
    ecs = NULL;
}
bool headless_consumer_run(execution_module *module)
{
    pending = module;
    REQUIRE(execution_module_event_count(module) == 3);
    int bump = -1;
    for (int i = 0; i < execution_module_event_count(module); i++) {
        execution_module_event_info event = execution_module_event_at(module, i);
        if (event.id == 12) {
            bump = i;
        }
    }
    REQUIRE(bump >= 0);
    execution_module_event_info event = execution_module_event_at(module, bump);
    REQUIRE(event.abi && !strcmp(event.name, "Bump") && event.field_count == 1);
    execution_module_event_field_info field = execution_module_event_field_at(module, bump, 0);
    REQUIRE(!strcmp(field.name, "amount") && field.initial.type == PIN_I32);
    REQUIRE(!field.has_default && !field.default_owner && !field.nominal);
    ecs = ecs_world_new();
    REQUIRE(ecs_world_register_component(ecs, &counter_component, NULL) == ECS_OK);
    behaviors = source_registry_new(ecs);
    REQUIRE(source_register(behaviors, "Headless", pending, NULL) == FLOW_OK);
    REQUIRE(source_register(behaviors, "Headless", pending, NULL) < 0);
    execution_module_free(pending);
    pending = NULL;
    ecs_entity entity = ecs_world_create_entity(ecs);
    REQUIRE(ecs_world_record_add(ecs, entity, counter_component.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    REQUIRE(source_attach(behaviors, entity, "Headless", false, 0, 0) == FLOW_OK);
    REQUIRE(source_step(behaviors, 0, 0) == FLOW_OK);
    headless_counter
        *counter = ecs_component_data(ecs, entity, counter_component.id);
    REQUIRE(counter && counter->value == 1);
    REQUIRE(source_dispatch(behaviors, entity, "Tick", NULL, 0, 1, 0.25) >= 0);
    REQUIRE(source_step(behaviors, 1, 0.25) == FLOW_OK);
    REQUIRE(counter->value == 3);
    pin_value amount = { .type = PIN_I32, .i = 5 };
    REQUIRE(source_dispatch(behaviors, entity, "Bump", &amount, 1, 2, 0) >= 0);
    REQUIRE(source_step(behaviors, 2, 0) == FLOW_OK);
    REQUIRE(counter->value == 8);
    REQUIRE(source_begin(behaviors, 2, 0) == FLOW_OK);
    REQUIRE(source_step(behaviors, 2, 0) == FLOW_OK);
    REQUIRE(counter->value == 8);
    execution_program *program = source_program(behaviors, "Headless");
    REQUIRE(execution_program_instance(program, ecs_entity_to_u64(entity)) != NULL);
    REQUIRE(ecs_world_destroy_entity(ecs, entity) == ECS_OK);
    REQUIRE(source_step(behaviors, 3, 0) == FLOW_OK);
    REQUIRE(execution_program_instance(program, ecs_entity_to_u64(entity)) == NULL);
    REQUIRE(source_remove(behaviors, "Headless") == FLOW_OK);
    REQUIRE(!source_registered(behaviors, "Headless"));
    return true;
}
