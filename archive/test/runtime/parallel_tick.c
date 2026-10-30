#include "authoring/source_prepare.h"
#include "runtime/behaviors/source.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

static ecs_world *world;
static source_registry *registry;
static authoring_source *prepared;
static execution_module *retained;
static int *publication_override;
static int override_value = 4;
static uint64_t count_id[2];
static int values[2] = { 1, 2 };
static bool swap_service, registry_services;
static int swap_status;
static const script_catalog_parameter result = { .name = "Value",
                                                 .type = { .storage = PIN_I32 } };
static const script_catalog_capability capabilities[] = {
    { .id = 1, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    { .id = 2,
      .name = "read",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = &result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_HOST,
      .completion_outcome = -1 },
};
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = 2 };
static const char *read_value(void *user, const pin_value *, pin_value *out)
{
    out[0] = (pin_value){ .type = PIN_I32, .i = *(int *)user };
    if (swap_service) {
        swap_service = false;
        swap_status = source_bind_service(registry, "tick.value", &values[1]);
    }
    return NULL;
}
static bool host(void *, uint64_t id, script_lower_host *out)
{
    if (id != 2) {
        return false;
    }
    static const node_host_call call = { .service = "tick.value", .invoke = read_value };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (script_lower_host){ .call = &call, .access = &access };
    return true;
}
static void setup()
{
    world = ecs_world_new();
    registry = source_registry_new(world);
    swap_service = false;
    registry_services = false;
    publication_override = NULL;
    swap_status = -1;
}
static void cleanup()
{
    authoring_source_free(prepared);
    prepared = NULL;
    execution_module_free(retained);
    retained = NULL;
    if (registry) {
        source_registry_free(registry);
    }
    registry = NULL;
    if (world) {
        ecs_world_free(world);
    }
    world = NULL;
}
static bool publish(int index, const char *name, const char *text, behavior_backend backend, bool replace)
{
    script_lower_bindings bindings = { .host = host };
    prepared = authoring_source_prepare_document(name, name, text, &catalog, &bindings, backend);
    REQUIRE(prepared && prepared->valid);
    count_id[index] = 0;
    for (int i = 0; i < prepared->analysis->symbol_count; i++) {
        const script_symbol *s = &prepared->analysis->symbols[i];
        if (s->kind == SCRIPT_STATE && !strcmp(s->name, "count")) {
            count_id[index] = s->id;
        }
    }
    REQUIRE(count_id[index]);
    service_binding *services = NULL;
    if (!registry_services || publication_override) {
        apush(
            services,
            ((service_binding){ "tick.value",
                                publication_override ? publication_override : &values[0] })
        );
    }
    source_publication entry = { .name = name,
                                 .previous_name = replace ? name : NULL,
                                 .module = prepared->module };
    int status = source_publish(registry, &entry, 1, services, NULL, 0);
    afree(services);
    if (status) {
        fprintf(stderr, "source Tick publication: %s\n", source_error(registry));
    }
    REQUIRE(!status);
    execution_module_free(retained);
    retained = prepared->module;
    execution_module_retain(retained);
    authoring_source_free(prepared);
    prepared = NULL;
    return true;
}
static int count(const char *name, ecs_entity owner, int index)
{
    execution_program *program = source_program(registry, name);
    execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(owner));
    const pin_value *value = instance
                               ? execution_instance_state(instance, count_id[index])
                               : NULL;
    return value && value->type == PIN_I32 ? value->i : -1;
}
TEST_CASE(source_tick_rebinds_service_after_current_host_call)
{
    registry_services = true;
    for (int variant = 0; variant < 2; variant++) {
        char service_name[] = "tick.value";
        REQUIRE(!source_bind_service(registry, service_name, &values[0]));
        memset(service_name, 'x', sizeof service_name - 1);
        behavior_backend backend = variant ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE;
        publication_override = &values[1];
        REQUIRE(
            publish(0, "Counter", "Counter :: behavior { count:int=0; on Tick :: () { count += read() } }", backend, backend != BEHAVIOR_BYTECODE)
        );
        ecs_entity owner = ecs_world_create_entity(world);
        REQUIRE(!ecs_world_apply_commands(world));
        source_publication defaults = { .name = "Defaults", .module = retained };
        REQUIRE(!source_publish(registry, &defaults, 1, NULL, NULL, 0));
        REQUIRE(!source_attach(registry, owner, "Defaults", false, 0, 0));
        /* Dispatch counts matching registered modules, including unattached Counter. */
        REQUIRE(source_dispatch(registry, owner, "Tick", NULL, 0, 0, .25) == 2);
        REQUIRE(count("Defaults", owner, 0) == 1);
        REQUIRE(source_detach_entity(registry, owner) == 1);
        REQUIRE(!source_remove(registry, "Defaults"));
        REQUIRE(!source_attach(registry, owner, "Counter", false, 0, 0));
        REQUIRE(source_dispatch(registry, owner, "Tick", NULL, 0, 0, .25) == 1);
        REQUIRE(count("Counter", owner, 0) == 2);
        source_publication entry = { .name = "Counter",
                                     .previous_name = "Counter",
                                     .module = retained };
        REQUIRE(!source_publish(registry, &entry, 1, NULL, NULL, 0));
        REQUIRE(source_dispatch(registry, owner, "Tick", NULL, 0, 1, .25) == 1);
        REQUIRE(count("Counter", owner, 0) == 4);
        service_binding *overrides = NULL;
        apush(overrides, ((service_binding){ "tick.value", &override_value }));
        int status = source_publish(registry, &entry, 1, overrides, NULL, 0);
        afree(overrides);
        REQUIRE(!status);
        REQUIRE(source_dispatch(registry, owner, "Tick", NULL, 0, 2, .25) == 1);
        REQUIRE(count("Counter", owner, 0) == 8);
        REQUIRE(!source_bind_service(registry, "tick.value", &values[0]));
        REQUIRE(source_bind_service(registry, "tick.value", NULL) != 0);
        REQUIRE(!source_publish(registry, &entry, 1, NULL, NULL, 0));
        REQUIRE(source_dispatch(registry, owner, "Tick", NULL, 0, 3, .25) == 1);
        REQUIRE(count("Counter", owner, 0) == 9);
        REQUIRE(source_detach_entity(registry, owner) == 1);
        REQUIRE(!source_attach(registry, owner, "Counter", false, 4, 0));
        swap_service = true;
        REQUIRE(source_dispatch(registry, owner, "Tick", NULL, 0, 4, .25) == 1);
        REQUIRE(!swap_service && !swap_status && count("Counter", owner, 0) == 1);
        REQUIRE(source_dispatch(registry, owner, "Tick", NULL, 0, 5, .25) == 1);
        REQUIRE(count("Counter", owner, 0) == 3);
        REQUIRE(source_detach_entity(registry, owner) == 1);
    }
    return true;
}
static int dispatch_all(double now)
{
    int status = source_dispatch_all(registry, "Tick", NULL, 0, now, .25);
    if (status != 2) {
        fprintf(stderr, "Tick dispatched %d registered modules: %s\n", status, source_error(registry));
    }
    return status;
}
TEST_CASE(source_tick_preserves_incremental_attachment_state_and_order)
{
    REQUIRE(
        publish(0, "Counter", "Counter :: behavior { count:int=10; on Tick :: () { count += read() } }", BEHAVIOR_BYTECODE, false)
    );
    REQUIRE(
        publish(1, "Alternate", "Alternate :: behavior { count:int=10; on Tick :: () { count += read() } }", BEHAVIOR_SLJIT, false)
    );
    ecs_entity owners[4];
    for (int i = 0; i < 4; i++) {
        owners[i] = ecs_world_create_entity(world);
    }
    REQUIRE(!ecs_world_apply_commands(world));
    for (int i = 0; i < 4; i++) {
        REQUIRE(!source_attach(registry, owners[i], "Counter", false, 0, 0));
    }
    REQUIRE(dispatch_all(0) == 2);
    for (int i = 0; i < 4; i++) {
        REQUIRE(count("Counter", owners[i], 0) == 11);
    }
    const char *alternate[] = { "Alternate" };
    REQUIRE(!source_set_attachments(registry, owners[1], alternate, 1, false, 1, .25));
    REQUIRE(dispatch_all(1) == 2);
    REQUIRE(count("Counter", owners[0], 0) == 12 && count("Counter", owners[2], 0) == 12);
    REQUIRE(count("Alternate", owners[1], 1) == 11);
    const char *ordered[] = { "Alternate", "Counter" };
    REQUIRE(!source_set_attachments(registry, owners[0], ordered, 2, false, 2, .25));
    REQUIRE(source_attachment_count(registry) == 5);
    uint64_t sequence = 0;
    bool order = false;
    for (int i = 0; i + 1 < source_attachment_count(registry); i++) {
        source_attachment_info a = source_attachment_at(registry, i);
        source_attachment_info b = source_attachment_at(registry, i + 1);
        if (ecs_entity_to_u64(a.entity) == ecs_entity_to_u64(owners[0]) &&
            !strcmp(a.name, "Alternate")) {
            order = !strcmp(b.name, "Counter") &&
                    ecs_entity_to_u64(b.entity) == ecs_entity_to_u64(owners[0]);
            sequence = b.sequence;
        }
    }
    REQUIRE(order && sequence);
    REQUIRE(dispatch_all(2) == 2);
    REQUIRE(count("Counter", owners[0], 0) == 13 && count("Alternate", owners[0], 1) == 11);
    REQUIRE(source_detach_entity(registry, owners[2]) == 1);
    REQUIRE(!source_attach(registry, owners[2], "Counter", false, 3, .25));
    REQUIRE(dispatch_all(3) == 2);
    REQUIRE(count("Counter", owners[2], 0) == 11 && count("Counter", owners[3], 0) == 14);
    REQUIRE(
        publish(0, "Counter", "Counter :: behavior { count:int=10; on Tick :: () { count += read() } }", BEHAVIOR_SLJIT, true)
    );
    REQUIRE(dispatch_all(4) == 2);
    REQUIRE(count("Counter", owners[0], 0) == 15 && count("Counter", owners[2], 0) == 12);
    bool retained = false;
    for (int i = 0; i < source_attachment_count(registry); i++) {
        source_attachment_info info = source_attachment_at(registry, i);
        retained |= info.sequence == sequence && !strcmp(info.name, "Counter");
    }
    REQUIRE(retained);
    return true;
}
void test_parallel_ticks()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(source_tick_rebinds_service_after_current_host_call);
    RUN_TEST_CASE(source_tick_preserves_incremental_attachment_state_and_order);
    pu_clear_setup();
}
