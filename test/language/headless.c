#include "authoring/source_prepare.h"
#include "backend.h"
#include "ecs/ecs.h"
#include "execution/log.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "headless_consumer.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const script_catalog_field source_field = {
    .id = 8011,
    .name = "value",
    .type = { .storage = PIN_I32 },
    .writable = true,
};
static const script_catalog_parameter amount_parameter = {
    .name = "amount",
    .type = { .storage = PIN_I32 },
};
static const script_catalog_parameter text_parameter = {
    .name = "InString",
    .type = { .storage = PIN_STR },
};
static const script_catalog_capability events[] = {
    { .id = 10, .name = "Begin", .kind = SCRIPT_CAPABILITY_EVENT },
    { .id = 11, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT },
    { .id = 12,
      .name = "Bump",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &amount_parameter,
      .parameter_count = 1 },
    { .id = 13,
      .name = "PrintString",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &text_parameter,
      .parameter_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
};
static const script_catalog_global owner = {
    .id = 30,
    .name = "self",
    .type = { .storage = PIN_HANDLE, .nominal = 700 },
};
static uint64_t print_abi;
static unsigned char *prepared_bytes;
static execution_module *module;
static authoring_source *source;

static void cleanup()
{
    headless_consumer_cleanup();
    free(prepared_bytes);
    prepared_bytes = NULL;
    execution_module_free(module);
    authoring_source_free(source);
    module = NULL;
    source = NULL;
}
static bool resolve_owner(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != owner.id) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
static bool resolve_host(void *, uint64_t id, script_lower_host *out)
{
    if (id != 13) {
        return false;
    }
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (script_lower_host){ .call = &EXECUTION_PrintString, .access = &access };
    return true;
}
static bool compile(behavior_backend backend)
{
    const ecs_component_desc *component = headless_consumer_component();
    script_catalog_type type = {
        .id = 101,
        .name = component->type->name,
        .type = { .storage = PIN_STRUCT, .nominal = 101 },
        .fields = &source_field,
        .field_count = 1,
        .size = component->element_size,
        .layout = type_layout_hash(component->type),
        .component_id = component->id,
        .component = true,
    };
    const script_catalog catalog = {
        .types = &type,
        .type_count = 1,
        .capabilities = events,
        .capability_count = 4,
        .globals = &owner,
        .global_count = 1,
        .entity_type = 700,
        .tick_event = 11,
    };
    print_abi = script_catalog_capability_abi(&catalog, &events[3]);
    const char *text = "Headless :: behavior { count: retain int = 0;\n"
                       "on Begin() { count = count + 1; }\n"
                       "on Tick() { PrintString(\"headless tick\"); count = "
                       "count + 2; }\n"
                       "on Bump(amount: int) { count = count + amount; }\n"
                       "bind self.HeadlessCounter.value = count; }\n";
    const script_lower_bindings bindings = { .global = resolve_owner,
                                             .host = resolve_host };
    source = authoring_source_prepare("Headless.chibi", text, &catalog, &bindings, backend);
    if (!source->valid) {
        for (int i = 0; i < source->diagnostic_count; i++) {
            fprintf(stderr, "headless prepare: %s\n", source->diagnostics[i].message);
        }
        return false;
    }
    bool native_cost = false;
    for (int i = 0; i < source->performance_count; i++) {
        const authoring_performance_diagnostic *d = &source->performance[i];
        native_cost |= d->possible && d->cost == EXECUTION_COST_ALLOCATE &&
                       strstr(d->message, "Native call") != NULL;
    }
    if (!native_cost || source->diagnostic_count) {
        return false;
    }
    module = source->module;
    execution_module_retain(module);
    /* The runtime retains executable definitions after the authoring snapshot retires. */
    authoring_source_free(source);
    source = NULL;
    return true;
}

TEST_CASE(headless_canonical_module_runs_without_game_services)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        cleanup();
        REQUIRE(compile(backends[backend]));
        execution_module *published = module;
        module = NULL;
        REQUIRE(headless_consumer_run(published));
    }
    return true;
}

TEST_CASE(headless_prepared_artifact_retires_producer_and_file)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        cleanup();
        REQUIRE(compile(backends[backend]));
        size_t length = 0;
        char error[256];
        REQUIRE(execution_prepared_write(module, &prepared_bytes, &length, error, sizeof error));
        REQUIRE(length > 32);
        execution_cost_summary costs = execution_module_costs(module);
        execution_module_free(module);
        module = NULL;
        /* The isolated consumer object links only installed runtime packages. */
        module = headless_consumer_read(prepared_bytes, length, backends[backend], print_abi);
        REQUIRE(module);
        execution_cost_summary loaded_costs = execution_module_costs(module);
        REQUIRE(costs.complete && loaded_costs.complete);
        REQUIRE(
            costs.proven == loaded_costs.proven &&
            costs.possible == loaded_costs.possible
        );
        free(prepared_bytes);
        prepared_bytes = NULL;
        execution_module *loaded = module;
        module = NULL;
        REQUIRE(headless_consumer_run(loaded));
    }
    return true;
}

void test_language_headless()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(headless_canonical_module_runs_without_game_services);
    RUN_TEST_CASE(headless_prepared_artifact_retires_producer_and_file);
    pu_clear_setup();
}
