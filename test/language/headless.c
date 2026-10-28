#include "authoring/source_collection.h"
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

#if defined(CHIBI_TEST_SOURCE_ASSETS) && !defined(__EMSCRIPTEN__)
#include "authoring/asset_compiler_host.h"
#include "authoring/source_costs.h"
#endif

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
static authoring_sources *cost_sources;

static void cleanup()
{
    headless_consumer_cleanup();
    free(prepared_bytes);
    prepared_bytes = NULL;
    execution_module_free(module);
    authoring_source_free(source);
    authoring_sources_free(cost_sources);
    module = NULL;
    source = NULL;
    cost_sources = NULL;
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
    const char
        *text = "Headless :: behavior { count: retain int = 0;\n"
                "on Begin :: () { count = count + 1; }\n"
                "on Tick :: () { PrintString(\"headless tick\"); count = "
                "count + 2; }\n"
                "on Bump :: (amount: int) { count = count + amount; }\n"
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

static int cost_probe_calls;

static const char *cost_probe(void *, const pin_value *, pin_value *)
{
    cost_probe_calls++;
    return NULL;
}

static bool resolve_cost_probe(void *, uint64_t id, script_lower_host *out)
{
    if (id != 51) {
        return false;
    }
    static const node_host_call call = { .invoke = cost_probe, .optional_service = true };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (script_lower_host){ .call = &call, .access = &access };
    return true;
}

TEST_CASE(headless_source_cost_facts_keep_unknown_native_costs_and_quiet_bodies)
{
    const script_catalog_capability capabilities[] = {
        { .id = 10, .name = "Begin", .kind = SCRIPT_CAPABILITY_EVENT },
        { .id = 11, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT },
        { .id = 51, .name = "Probe", .kind = SCRIPT_CAPABILITY_FUNCTION, .effects = SCRIPT_EFFECT_HOST }
    };
    const script_catalog catalog = { .capabilities = capabilities,
                                     .capability_count = 3,
                                     .tick_event = 11 };
    const script_lower_bindings bindings = { .host = resolve_cost_probe };
    const char *text = "Facts :: behavior {\n"
                       "    on Begin :: () {\n"
                       "        Probe();\n"
                       "    }\n"
                       "    on Tick :: () { value := 1 + 2; }\n"
                       "}\n";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        cleanup();
        cost_probe_calls = 0;
        source = authoring_source_prepare("Facts.chibi", text, &catalog, &bindings, backends[backend]);
        REQUIRE(source->valid && source->diagnostic_count == 0);
        REQUIRE(cost_probe_calls == 0);
        bool native = false, quiet = false;
        for (int i = 0; i < source->cost_count; i++) {
            const authoring_function_costs *facts = &source->costs[i];
            uint64_t event = 0;
            for (int j = 0; j < execution_module_function_count(source->module); j++) {
                execution_module_function_info
                    info = execution_module_function_at(source->module, j);
                if (info.id == facts->function) {
                    event = info.event;
                    break;
                }
            }
            REQUIRE(facts->entry && facts->summary.complete);
            REQUIRE(facts->origin.asset_name && !strcmp(facts->origin.asset_name, "Facts.chibi"));
            if (event == 11) {
                REQUIRE(!facts->summary.proven && !facts->summary.possible);
                quiet = true;
            }
            if (event != 10) {
                continue;
            }
            REQUIRE(!facts->summary.proven);
            REQUIRE(
                facts->summary.possible ==
                (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE)
            );
            for (int bit = 0; bit < EXECUTION_COST_KINDS; bit++) {
                const execution_cost_site *site = &facts->summary.possible_sites[bit];
                REQUIRE(site->capability == 51);
                REQUIRE(site->reason && strstr(site->reason, "Native call"));
                REQUIRE(site->function);
                const behavior_function_source *origin = behavior_function_instruction_source(
                    site->function,
                    site->instruction
                );
                REQUIRE(origin && origin->asset_name && !strcmp(origin->asset_name, "Facts"));
                REQUIRE(origin->line == 3 && origin->column == 9);
                REQUIRE(origin->start == (int)(strstr(text, "Probe()") - text));
            }
            native = true;
        }
        REQUIRE(native && quiet);
        REQUIRE(source->valid && source->diagnostic_count == 0 && cost_probe_calls == 0);
    }
    return true;
}

static const authoring_source *cost_source(const char *name)
{
    for (int i = 0; i < authoring_sources_count(cost_sources); i++) {
        const authoring_source *item = authoring_sources_at(cost_sources, i);
        if (!strcmp(item->analysis->name, name)) {
            return item;
        }
    }
    return NULL;
}

static void prepare_cost_library(behavior_backend backend)
{
    const script_catalog_capability capabilities[] = {
        { .id = 10, .name = "Begin", .kind = SCRIPT_CAPABILITY_EVENT },
        { .id = 51, .name = "Probe", .kind = SCRIPT_CAPABILITY_FUNCTION, .effects = SCRIPT_EFFECT_HOST }
    };
    const script_catalog catalog = { .capabilities = capabilities,
                                     .capability_count = 2 };
    const script_lower_bindings bindings = { .host = resolve_cost_probe };
    const source_catalog_edit edits[] = {
        { .path = "/headless-costs/libraries/Shared.chibi",
          .text = "Shared :: library {\n"
                  "    Work :: () {\n"
                  "        Probe();\n"
                  "    }\n"
                  "}\n" },
        { .path = "/headless-costs/behaviors/Caller.chibi",
          .text = "Caller :: behavior {\n"
                  "    on Begin :: () { Shared.Work(); }\n"
                  "}\n" }
    };
    cost_sources = authoring_sources_prepare_catalog(
        source_catalog_replaced("/headless-costs", &catalog, edits, 2),
        &bindings,
        backend
    );
}

TEST_CASE(headless_source_cost_facts_preserve_linked_library_evidence)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        cleanup();
        cost_probe_calls = 0;
        prepare_cost_library(backends[backend]);
        REQUIRE(authoring_sources_valid(cost_sources));
        const authoring_source *caller = cost_source("Caller");
        const authoring_source *library = cost_source("Shared");
        REQUIRE(caller && library && caller->cost_count == 1 && library->cost_count == 1);
        const authoring_function_costs *facts = &caller->costs[0];
        REQUIRE(facts->summary.complete);
        REQUIRE(
            facts->origin.asset_name && !strcmp(facts->origin.asset_name, caller->path)
        );
        REQUIRE(
            facts->summary.possible ==
            (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE)
        );
        for (int bit = 0; bit < EXECUTION_COST_KINDS; bit++) {
            const execution_cost_site *site = &facts->summary.possible_sites[bit];
            REQUIRE(site->function && site->capability == 51);
            REQUIRE(site->reason && strstr(site->reason, "Native call"));
            const behavior_function_source
                *origin = behavior_function_instruction_source(site->function, site->instruction);
            REQUIRE(origin && origin->asset_name && !strcmp(origin->asset_name, "Shared"));
            REQUIRE(origin->line == 3 && origin->column == 9);
        }
        REQUIRE(cost_probe_calls == 0);
    }
    return true;
}

#if defined(CHIBI_TEST_SOURCE_ASSETS) && !defined(__EMSCRIPTEN__)
TEST_CASE(headless_cost_formatter_failure_preserves_prepared_sources)
{
    prepare_cost_library(BEHAVIOR_BYTECODE);
    REQUIRE(authoring_sources_valid(cost_sources));
    const authoring_source *caller = cost_source("Caller");
    REQUIRE(caller && caller->cost_count == 1);
    const asset_compiler_options *defaults = asset_source_default_options();
    REQUIRE(defaults);
    asset_compiler_options unavailable = *defaults;
    unavailable.compiler = NULL;
    unavailable.compiler_length = 0;
    asset_source_set_default(&unavailable);
    char error[512] = { 0 };
    char *report = authoring_sources_cost_report(cost_sources, true, error, sizeof error);
    asset_source_set_default(defaults);
    bool failed = !report && strstr(error, "Cost reporter failed") &&
                  strstr(error, "unavailable");
    free(report);
    REQUIRE(failed);
    REQUIRE(authoring_sources_valid(cost_sources));
    REQUIRE(cost_source("Caller") == caller && caller->cost_count == 1);
    REQUIRE(asset_source_default_options() == defaults);
    return true;
}
#endif

void test_language_headless()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(headless_canonical_module_runs_without_game_services);
    RUN_TEST_CASE(headless_prepared_artifact_retires_producer_and_file);
    RUN_TEST_CASE(headless_source_cost_facts_keep_unknown_native_costs_and_quiet_bodies);
    RUN_TEST_CASE(headless_source_cost_facts_preserve_linked_library_evidence);
#if defined(CHIBI_TEST_SOURCE_ASSETS) && !defined(__EMSCRIPTEN__)
    RUN_TEST_CASE(headless_cost_formatter_failure_preserves_prepared_sources);
#endif
    pu_clear_setup();
}
