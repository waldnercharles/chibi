#include "authoring/source_prepare.h"
#include "execution/inspection.h"
#include "execution/status.h"
#include "runtime/behaviors/source.h"
#include "test/support/reflected_types.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Aliases require an authoritative nominal chain. Identical field layouts
 * alone cannot grant assignment compatibility. Graph conversion edges and
 * private oracle/budget counters are replaced by actual source contracts. */
static ecs_world *alias_world;
static source_registry *alias_registry;
static authoring_source *alias_source;
static execution_inspector *alias_inspector;
static void alias_cleanup()
{
    execution_inspector_free(alias_inspector);
    alias_inspector = NULL;
    authoring_source_free(alias_source);
    alias_source = NULL;
    if (alias_registry) {
        source_registry_free(alias_registry);
    }
    alias_registry = NULL;
    if (alias_world) {
        ecs_world_free(alias_world);
    }
    alias_world = NULL;
}
static const char *to_alias(void *, const pin_value *args, pin_value *out)
{
    if (!args[0].ptr) {
        return "Missing alias input";
    }
    out[0] = (pin_value){ .type = PIN_STRUCT,
                          .type_name = "AliasTarget",
                          .ptr = args[0].ptr };
    return NULL;
}
static const char *from_alias(void *, const pin_value *args, pin_value *out)
{
    if (!args[0].ptr) {
        return "Missing alias input";
    }
    out[0] = (pin_value){ .type = PIN_STRUCT,
                          .type_name = "AliasSource",
                          .ptr = args[0].ptr };
    return NULL;
}
static bool alias_host(void *, uint64_t id, script_lower_host *out)
{
    static const node_host_call to = { .invoke = to_alias },
                                from = { .invoke = from_alias };
    static const node_access access = { 0 };
    if (id != 20 && id != 21) {
        return false;
    }
    *out = (script_lower_host){ .call = id == 20 ? &to : &from, .access = &access };
    return true;
}
static bool alias_run(bool reverse, bool nested, behavior_backend backend)
{
    const type_desc *source_type = type_catalog_find("AliasSource");
    const type_desc *target_type = type_catalog_find("AliasTarget");
    REQUIRE(source_type && target_type && target_type->alias_of);
    REQUIRE(!strcmp(target_type->alias_of, source_type->name));
    script_catalog_type types[] = {
        { .id = 501,
          .name = "AliasSource",
          .type = { .storage = PIN_STRUCT, .nominal = 501 },
          .size = source_type->size,
          .layout = type_layout_hash(source_type) },
        { .id = 502,
          .name = "AliasTarget",
          .type = { .storage = PIN_STRUCT, .nominal = 502 },
          .size = target_type->size,
          .layout = type_layout_hash(target_type),
          .underlying = 501 },
    };
    int from = reverse ? 1 : 0, to = 1 - from;
    script_catalog_parameter input = { .name = "input", .type = types[from].type };
    script_catalog_parameter result_type = { .name = "value", .type = types[to].type };
    script_catalog_capability caps[] = {
        { .id = 10,
          .name = "Pulse",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = &input,
          .parameter_count = 1,
          .completion_outcome = -1 },
        { .id = reverse ? 21 : 20,
          .name = "AliasConvert",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = &input,
          .parameter_count = 1,
          .results = &result_type,
          .result_count = 1,
          .pure = true,
          .completion_outcome = -1 },
    };
    script_catalog catalog = { .types = types,
                               .type_count = 2,
                               .capabilities = caps,
                               .capability_count = 2 };
    script_lower_bindings bindings = { .host = alias_host };
    char text[768];
    snprintf(
        text,
        sizeof text,
        "AliasCase :: behavior { output:%s=null; "
        "Convert :: (input:%s)->(Value:%s) {return "
        "AliasConvert(input);} "
        "Nested :: (input:%s)->(Value:%s) {return Convert(input);} "
        "on Pulse :: (input:%s) {output=%s;} }",
        types[to].name,
        types[from].name,
        types[to].name,
        types[from].name,
        types[to].name,
        types[from].name,
        nested ? "Nested(input)" : "input"
    );
    alias_source = authoring_source_prepare_document(
        "AliasCase.chibi",
        "AliasCase.chibi",
        text,
        &catalog,
        &bindings,
        backend
    );
    if (!alias_source->valid) {
        for (int i = 0; i < alias_source->diagnostic_count; i++) {
            const script_diagnostic *d = &alias_source->diagnostics[i];
            fprintf(
                stderr,
                "alias %d:%d [%d,%d): %s\n",
                d->range.line,
                d->range.column,
                d->range.start,
                d->range.end,
                d->message
            );
        }
    }
    REQUIRE(alias_source->valid);
    uint64_t output = 0;
    for (int i = 0; i < alias_source->analysis->symbol_count; i++) {
        script_symbol *s = &alias_source->analysis->symbols[i];
        if (s->kind == SCRIPT_STATE && !strcmp(s->name, "output")) {
            output = s->id;
        }
    }
    REQUIRE(output);
    alias_world = ecs_world_new();
    alias_registry = source_registry_new(alias_world);
    REQUIRE(!source_register(alias_registry, "AliasCase", alias_source->module, NULL));
    authoring_source_free(alias_source);
    alias_source = NULL;
    ecs_entity owner = ecs_world_create_entity(alias_world);
    REQUIRE(!ecs_world_apply_commands(alias_world));
    REQUIRE(!source_attach(alias_registry, owner, "AliasCase", false, 0, 0));
    execution_program *program = source_program(alias_registry, "AliasCase");
    alias_inspector = execution_program_inspector(program);
    REQUIRE(alias_inspector);
    uint32_t bits[] = { UINT32_C(0x80000000), UINT32_C(0x7fc12345) };
    alias_vector producer;
    _Static_assert(sizeof producer == sizeof bits, "alias payload bit extent");
    memcpy(&producer, bits, sizeof producer);
    pin_value value = { .type = PIN_STRUCT, .type_name = types[from].name, .ptr = &producer };
    REQUIRE(source_dispatch(alias_registry, owner, "Pulse", &value, 1, 0, 0) == 1);
    execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(owner));
    const pin_value *result = execution_instance_state(instance, output);
    REQUIRE(result && result->type == PIN_STRUCT && result->ptr && result->ptr != &producer);
    REQUIRE(!strcmp(result->type_name, types[to].name));
    REQUIRE(!memcmp(result->ptr, bits, sizeof bits));
    memset(&producer, 0, sizeof producer);
    REQUIRE(!memcmp(result->ptr, bits, sizeof bits));
    if (nested) {
        value.ptr = NULL;
        REQUIRE(source_dispatch(alias_registry, owner, "Pulse", &value, 1, 1, 0) < 0);
        const execution_program_failure *failure = execution_program_error(program);
        REQUIRE(failure && strstr(failure->message, "Missing alias input"));
        REQUIRE(!strcmp(failure->asset_name, "AliasCase"));
        REQUIRE(failure->start >= 0 && failure->end > failure->start);
        REQUIRE(
            execution_inspector_owner_frame_count(alias_inspector, ecs_entity_to_u64(owner)) > 1
        );
        REQUIRE(!memcmp(execution_instance_state(instance, output)->ptr, bits, sizeof bits));
    }
    /* Nominal authority is removed without changing either reflected layout. */
    types[1].underlying = 0;
    snprintf(
        text,
        sizeof text,
        "AliasCase :: behavior {output:%s=null;on Pulse :: "
        "(input:%s){output=input;}}",
        types[to].name,
        types[from].name
    );
    alias_source = authoring_source_prepare_document(
        "AliasCase.chibi",
        "AliasCase.chibi",
        text,
        &catalog,
        &bindings,
        backend
    );
    REQUIRE(!alias_source->valid && alias_source->diagnostic_count > 0);
    alias_cleanup();
    return true;
}
TEST_CASE(source_aliases_preserve_nominal_headers_owned_bits_and_nested_calls)
{
    for (int reverse = 0; reverse < 2; reverse++) {
        for (int nested = 0; nested < 2; nested++) {
            REQUIRE(alias_run(reverse, nested, BEHAVIOR_BYTECODE));
            REQUIRE(alias_run(reverse, nested, BEHAVIOR_SLJIT));
        }
    }
    return true;
}
void test_compiled_aliases()
{
    pu_setup(NULL, alias_cleanup);
    RUN_TEST_CASE(source_aliases_preserve_nominal_headers_owned_bits_and_nested_calls);
    pu_clear_setup();
}
