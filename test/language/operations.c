#include "authoring/native_bindings.h"
#include "authoring/source_collection.h"
#include "backend.h"
#include "execution/function.h"
#include "execution/operation.h"
#include "execution/status.h"
#include "runtime/core_definition.h"
#include "third_party/pico_headers/pico_unit.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static native_capabilities *native, *other;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static execution_program *program;
static authoring_sources *sources[2];
static void cleanup()
{
    execution_program_free(program);
    execution_module_free(module);
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    for (int i = 0; i < 2; i++) {
        authoring_sources_free(sources[i]);
        sources[i] = NULL;
    }
    native_capabilities_free(other);
    native_capabilities_free(native);
    program = NULL;
    module = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    native = other = NULL;
}
static const native_capability_definition *definition(script_operation operation)
{
    for (int i = 0; i < CORE_DEFINITION_PACK.definition_count; i++) {
        if (CORE_DEFINITION_PACK.definitions[i].operation == operation) {
            return &CORE_DEFINITION_PACK.definitions[i];
        }
    }
    return NULL;
}
static const script_catalog_capability *declaration(script_operation operation)
{
    return script_catalog_capability_by_id(
        native_capabilities_catalog(native),
        native_capability_definition_id(definition(operation))
    );
}
static native_capabilities *single(const native_capability_definition *value, char *error, size_t size)
{
    const native_definition_pack pack = { .definitions = value, .definition_count = 1 };
    return native_capabilities_from_definition_packs(NULL, 0, &pack, 1, error, size);
}
static native_capabilities *with_core(const native_capability_pack *packs, int count, char *error, size_t size)
{
    native_capability_definition alias = *definition(SCRIPT_OPERATION_OWNER);
    alias.canonical_name = "OwnerAlias";
    alias.source_name = "owner_alias";
    const native_definition_pack definitions[] = {
        CORE_DEFINITION_PACK,
        { .definitions = &alias, .definition_count = 1 },
    };
    return native_capabilities_from_definition_packs(
        packs,
        count,
        definitions,
        NATIVE_COUNT(definitions),
        error,
        size
    );
}
static bool core()
{
    char error[256];
    native = with_core(NULL, 0, error, sizeof error);
    if (!native) {
        fprintf(stderr, "operation catalog: %s\n", error);
    }
    return native != NULL;
}
TEST_CASE(operations_preserve_unknown_abi)
{
    /* Fixed ABI-v1 vectors from the pre-operation contract at d1154cdb.
     * Keep these independent of both the operation table and generated packs. */
    const script_catalog_parameter number = { .type = { .storage = PIN_F64 } };
    const char *const entries[] = { "In" }, *const outcomes[] = { "Completed" };
    const script_catalog_capability values[] = {
        { .kind = SCRIPT_CAPABILITY_FUNCTION,
          .completion_outcome = -1,
          .effects = SCRIPT_EFFECT_HOST },
        { .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = &number,
          .result_count = 1,
          .completion_outcome = -1,
          .pure = true },
        { .kind = SCRIPT_CAPABILITY_ACTION,
          .parameters = &number,
          .parameter_count = 1,
          .entries = entries,
          .entry_count = 1,
          .outcomes = outcomes,
          .outcome_count = 1,
          .completion_outcome = 0,
          .effects = SCRIPT_EFFECT_SUSPEND },
    };
    const uint64_t expected[] = {
        UINT64_C(0xc2f37c26b74a6739),
        UINT64_C(0x3d0512e6a499e18a),
        UINT64_C(0x84c107c0906de0ba),
    };
    const script_catalog catalog = { 0 };
    for (int i = 0; i < NATIVE_COUNT(values); i++) {
        REQUIRE(script_catalog_capability_abi(&catalog, &values[i]) == expected[i]);
    }
    return true;
}
TEST_CASE(operations_generate_checked_bindings_and_conservative_contracts)
{
    REQUIRE(core());
    const script_catalog *catalog = native_capabilities_catalog(native);
    const behavior_function_op instructions[] = { BF_OWNER, BF_DELAY };
    for (int i = SCRIPT_OPERATION_OWNER; i <= SCRIPT_OPERATION_DELAY; i++) {
        const script_catalog_capability *value = declaration(i);
        REQUIRE(value && script_catalog_operation_valid(value));
        const native_capability_binding *binding = native_capabilities_find(
            native,
            value->id,
            script_catalog_capability_abi(catalog, value)
        );
        REQUIRE(binding && binding->intrinsic == instructions[i - SCRIPT_OPERATION_OWNER]);
        REQUIRE(
            !binding->call && !binding->command && !binding->query && !binding->access
        );
        REQUIRE(binding->result_count == value->result_count);
        const script_operation_contract *contract = script_operation_describe(i);
        REQUIRE(contract && contract->version == 1 && contract->effects == value->effects);
        REQUIRE(contract->guarantees & SCRIPT_OPERATION_ACTIVATION_THREAD);
        if (i == SCRIPT_OPERATION_DELAY) {
            REQUIRE(!(contract->guarantees & SCRIPT_OPERATION_NO_REENTRY));
            REQUIRE(contract->guarantees & SCRIPT_OPERATION_SAVES_LIVE_VALUES);
            REQUIRE(
                !(contract->guarantees & (SCRIPT_OPERATION_NO_ALLOCATION |
                                          SCRIPT_OPERATION_NO_PAYLOAD_COPY |
                                          SCRIPT_OPERATION_NO_ARGUMENT_RETENTION))
            );
        } else {
            REQUIRE(contract->guarantees & SCRIPT_OPERATION_NO_REENTRY);
            REQUIRE(contract->guarantees & SCRIPT_OPERATION_NO_RESULT_ALIAS);
            REQUIRE(contract->guarantees & SCRIPT_OPERATION_NO_ALLOCATION);
        }
    }
    REQUIRE(!script_operation_describe(SCRIPT_OPERATION_UNKNOWN));
    REQUIRE(!script_operation_describe((script_operation)999));
    REQUIRE(!behavior_operation_instruction(SCRIPT_OPERATION_UNKNOWN));
    int assertion = script_catalog_find_capability(catalog, "assert", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(assertion >= 0);
    const script_catalog_capability *assert_capability = &catalog->capabilities[assertion];
    REQUIRE(assert_capability->operation == SCRIPT_OPERATION_UNKNOWN);
    const native_capability_binding *assert_binding = native_capabilities_find(
        native,
        assert_capability->id,
        script_catalog_capability_abi(catalog, assert_capability)
    );
    REQUIRE(assert_binding && assert_binding->intrinsic == BF_ASSERT);
    return true;
}
TEST_CASE(operations_reject_malformed_shared_declarations)
{
    const native_capability_definition original = *definition(SCRIPT_OPERATION_OWNER);
    char error[256];
    for (int change = 0; change < 12; change++) {
        native_capability_definition value = original;
        native_parameter_definition result = original.outputs[0];
        value.outputs = &result;
        switch (change) {
            case 0: value.effects = SCRIPT_EFFECT_HOST; break;
            case 1: value.pure = false; break;
            case 2: value.variadic = true; break;
            case 3: value.operation = (script_operation)999; break;
            case 4: result.storage = PIN_F64; break;
            case 5: result.container = PIN_CONTAINER_ARRAY; break;
            case 6: result.type_flags = SCRIPT_TYPE_REFERENCE; break;
            case 7: value.output_count = 0; break;
            case 8: value.state_access = SCRIPT_STATE_ACCESS_READ; break;
            case 9:
                value.inputs = &result;
                value.input_count = 1;
                break;
            case 10: value.kind = SCRIPT_CAPABILITY_EVENT; break;
            case 11: value.operation = SCRIPT_OPERATION_DELAY; break;
        }
        other = single(&value, error, sizeof error);
        REQUIRE(!other && strstr(error, "Invalid engine operation declaration"));
    }
    native_capability_definition delay = *definition(SCRIPT_OPERATION_DELAY);
    delay.effects = 0;
    other = single(&delay, error, sizeof error);
    REQUIRE(!other && strstr(error, "Invalid engine operation declaration"));
    return true;
}
TEST_CASE(operations_reject_raw_scalar_semantic_metadata)
{
    REQUIRE(core());
    script_catalog_capability value = *declaration(SCRIPT_OPERATION_OWNER);
    const script_catalog_parameter original = value.results[0];
    script_catalog_parameter result = original;
    value.results = &result;
    const script_catalog catalog = { .capabilities = &value, .capability_count = 1 };
    const native_capability_binding binding = { .id = value.id, .intrinsic = BF_OWNER };
    const native_capability_pack pack = { .catalog = &catalog,
                                          .bindings = &binding,
                                          .binding_count = 1 };
    char error[256];
    other = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(other);
    native_capabilities_free(other);
    other = NULL;
    const unsigned flags[] = { SCRIPT_TYPE_ERROR,     SCRIPT_TYPE_OPTIONAL,
                               SCRIPT_TYPE_REFERENCE, SCRIPT_TYPE_RESULTS,
                               SCRIPT_TYPE_UNTYPED,   SCRIPT_TYPE_GENERIC,
                               SCRIPT_TYPE_TASK,      SCRIPT_TYPE_NAMESPACE,
                               SCRIPT_TYPE_UNION,     SCRIPT_TYPE_SINGLETON,
                               SCRIPT_TYPE_LITERAL,   SCRIPT_TYPE_BOXED_OPTIONAL };
    for (int change = 0; change < NATIVE_COUNT(flags) + 4; change++) {
        result = original;
        if (change < NATIVE_COUNT(flags)) {
            result.type.flags = flags[change];
        } else if (change == NATIVE_COUNT(flags)) {
            result.type.parameter = 1;
        } else if (change == NATIVE_COUNT(flags) + 1) {
            result.reference_kind = SCRIPT_REFERENCE_COMPONENT;
        } else if (change == NATIVE_COUNT(flags) + 2) {
            result.member = 1;
        } else {
            result.type.collection = 1;
        }
        other = native_capabilities_new(&pack, 1, error, sizeof error);
        if (other) {
            fprintf(stderr, "raw scalar metadata accepted: change %d\n", change);
        }
        REQUIRE(!other && strstr(error, "Invalid engine operation declaration"));
    }
    return true;
}
TEST_CASE(operations_reject_dormant_raw_metadata)
{
    REQUIRE(core());
    for (int operation = SCRIPT_OPERATION_OWNER; operation <= SCRIPT_OPERATION_DELAY;
         operation++) {
        const script_catalog_capability original = *declaration(operation);
        REQUIRE(!original.component && !original.component_field && !original.aggregate);
        REQUIRE(original.component_entity == 0 && original.component_value == 0);
        REQUIRE(original.flow.kind == SCRIPT_FLOW_ACCESS_NONE && original.flow.break_entry == 0);
        for (int i = 0; i < 2; i++) {
            REQUIRE(
                original.flow.inputs[i] == 0 && original.flow.results[i] == 0 &&
                original.flow.outcomes[i] == 0
            );
        }
        script_catalog_capability value = original;
        const script_catalog catalog = { .capabilities = &value,
                                         .capability_count = 1 };
        const native_capability_binding binding = {
            .id = value.id,
            .intrinsic = behavior_operation_instruction(operation)
        };
        const native_capability_pack pack = { .catalog = &catalog,
                                              .bindings = &binding,
                                              .binding_count = 1 };
        char error[256] = { 0 };
        other = native_capabilities_new(&pack, 1, error, sizeof error);
        REQUIRE(other);
        native_capabilities_free(other);
        other = NULL;
        for (int change = 0; change < 12; change++) {
            value = original;
            switch (change) {
                case 0: value.component = 1; break;
                case 1: value.component_field = 1; break;
                case 2: value.aggregate = 1; break;
                case 3: value.component_entity = -1; break;
                case 4: value.component_value = -1; break;
                case 5: value.flow.inputs[0] = -1; break;
                case 6: value.flow.inputs[1] = -1; break;
                case 7: value.flow.results[0] = -1; break;
                case 8: value.flow.results[1] = -1; break;
                case 9: value.flow.outcomes[0] = -1; break;
                case 10: value.flow.outcomes[1] = -1; break;
                case 11: value.flow.break_entry = -1; break;
            }
            error[0] = '\0';
            other = native_capabilities_new(&pack, 1, error, sizeof error);
            if (other || !strstr(error, "Invalid engine operation declaration")) {
                fprintf(stderr, "dormant metadata: operation %d, field %d: %s\n", operation, change, error);
            }
            REQUIRE(!other && strstr(error, "Invalid engine operation declaration"));
        }
    }
    return true;
}
static const char *opaque_owner(void *, const pin_value *, pin_value *out)
{
    out->u64 = 7;
    return NULL;
}
TEST_CASE(operations_reject_binding_mismatch_and_distinguish_opaque_abi)
{
    REQUIRE(core());
    script_catalog_capability value = *declaration(SCRIPT_OPERATION_OWNER);
    script_catalog catalog = { .capabilities = &value, .capability_count = 1 };
    native_capability_binding binding = { .id = value.id, .intrinsic = BF_DELAY };
    native_capability_pack pack = { .catalog = &catalog,
                                    .bindings = &binding,
                                    .binding_count = 1 };
    char error[256];
    other = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(!other && strstr(error, "differs from engine operation contract"));
    static const node_host_call call = { .invoke = opaque_owner };
    binding.intrinsic = 0;
    binding.call = &call;
    other = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(!other && strstr(error, "differs from engine operation contract"));
    uint64_t known = script_catalog_capability_abi(&catalog, &value);
    value.operation = SCRIPT_OPERATION_UNKNOWN;
    uint64_t opaque = script_catalog_capability_abi(&catalog, &value);
    REQUIRE(known != opaque);
    other = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(other && native_capabilities_find(other, value.id, opaque));
    REQUIRE(!native_capabilities_find(other, value.id, known));
    REQUIRE(!script_operation_describe(
        native_capabilities_catalog(other)->capabilities[0].operation
    ));
    native_capabilities_free(other);
    other = NULL;
    binding.call = NULL;
    binding.intrinsic = BF_OWNER;
    other = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(!other && strstr(error, "differs from engine operation contract"));
    value.operation = SCRIPT_OPERATION_OWNER;
    pack.binding_count = 0;
    other = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(!other && strstr(error, "has no execution binding"));
    const native_definition_binding override = { .canonical_name = CORE_SELF_NAME,
                                                 .implementation = {
                                                     .intrinsic = BF_OWNER } };
    const native_definition_pack shared = { .definitions = definition(SCRIPT_OPERATION_OWNER),
                                            .definition_count = 1,
                                            .bindings = &override,
                                            .binding_count = 1 };
    other = native_capabilities_from_definition_packs(NULL, 0, &shared, 1, error, sizeof error);
    REQUIRE(!other && strstr(error, "binding is generated"));
    return true;
}
TEST_CASE(operation_identity_invalidates_source_reuse)
{
    REQUIRE(core());
    script_catalog_capability value = *declaration(SCRIPT_OPERATION_OWNER);
    const script_catalog catalog = { .capabilities = &value, .capability_count = 1 };
    const source_catalog_edit edit = {
        .path = "/operations/libraries/Probe.chibi",
        .text = "Probe :: library { Value :: () -> int { return 1; } }"
    };
    const script_lower_bindings bindings = { .generation = 1 };
    for (int i = 0; i < 2; i++) {
        sources[i] = authoring_sources_prepare_incremental(
            source_catalog_replaced("/operations", &catalog, &edit, 1),
            &bindings,
            BEHAVIOR_BYTECODE,
            i ? sources[0] : NULL
        );
        REQUIRE(sources[i] && authoring_sources_count(sources[i]) == 1);
        REQUIRE(authoring_sources_at(sources[i], 0)->analysis->valid);
        REQUIRE(authoring_sources_at(sources[i], 0)->module);
        REQUIRE(!authoring_sources_reused(sources[i]));
        value.operation = SCRIPT_OPERATION_UNKNOWN;
    }
    return true;
}
static void diagnostic(const char *phase, const char *source, const script_diagnostic *value)
{
    fprintf(
        stderr,
        "operation %s %d:%d [%d,%d): %s\nsource: %s\n",
        phase,
        value->range.line,
        value->range.column,
        value->range.start,
        value->range.end,
        value->message,
        source
    );
}
static bool analyze_source(const char *source, const script_catalog *catalog)
{
    syntax = script_parse(source);
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        diagnostic("syntax", source, &syntax->diagnostics[i]);
    }
    analysis = script_analyze(syntax, catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        diagnostic("semantic", source, &analysis->diagnostics[i]);
    }
    return analysis->valid;
}
TEST_CASE(operations_reject_state_access_calls)
{
    REQUIRE(core());
    script_catalog_capability capabilities[] = {
        *declaration(SCRIPT_OPERATION_OWNER),
        { .id = 1, .name = "Run", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 }
    };
    capabilities[0].name = "ReadOwner";
    capabilities[0].qualified_name = NULL;
    const script_catalog catalog = { .capabilities = capabilities,
                                     .capability_count = NATIVE_COUNT(capabilities),
                                     .entity_type = script_catalog_id("type", "ecs.entity") };
    const char *source[] = {
        "Probe :: behavior { value: entity = 0; on Run :: () { saved := "
        "ReadOwner(); } }",
        "Probe :: behavior { value: entity = 0; on Run :: () { "
        "ReadOwner(self); } }"
    };
    const script_state_access access[] = { SCRIPT_STATE_ACCESS_READ,
                                           SCRIPT_STATE_ACCESS_WRITE };
    for (int i = 0; i < NATIVE_COUNT(source); i++) {
        capabilities[0].state_access = access[i];
        /* A state is read or assigned by its name, never through a call. */
        syntax = script_parse(source[i]);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(!analysis->valid && analysis->diagnostic_count >= 1);
        REQUIRE(strstr(analysis->diagnostics[0].message, "Read or assign the state by name"));
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        analysis = NULL;
        syntax = NULL;
    }
    return true;
}
static bool wrong_host(void *, uint64_t, script_lower_host *out)
{
    *out = (script_lower_host){ .intrinsic = BF_DELAY };
    return true;
}
TEST_CASE(operations_reject_custom_lowerer_binding_mismatch)
{
    REQUIRE(core());
    const script_catalog *catalog = native_capabilities_catalog(native);
    REQUIRE(
        analyze_source("Probe :: library { Value :: () -> bool { return owner_alias() == owner_alias(); } }", catalog)
    );
    const script_lower_bindings bindings = { .host = wrong_host };
    plan = script_lower(analysis, catalog, &bindings);
    REQUIRE(plan && !plan->valid);
    REQUIRE(plan->diagnostic_count == 2);
    REQUIRE(strstr(plan->diagnostics[0].message, "differs from engine operation contract"));
    return true;
}
TEST_CASE(owner_alias_preserves_invocation_effects)
{
    REQUIRE(core());
    const script_catalog *catalog = native_capabilities_catalog(native);
    REQUIRE(declaration(SCRIPT_OPERATION_OWNER)->effects == SCRIPT_EFFECT_INVOCATION_READ);
    REQUIRE(
        analyze_source("Probe :: library { Value :: () -> bool { return owner_alias() == owner_alias(); } }", catalog)
    );
    REQUIRE(analysis->function_count == 1);
    REQUIRE(analysis->functions[0].effects & SCRIPT_EFFECT_INVOCATION_READ);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    syntax = script_parse("Probe :: system(if owner_alias() == owner_alias()) {}");
    analysis = script_analyze(syntax, catalog);
    REQUIRE(!syntax->diagnostic_count && !analysis->valid);
    bool rejected = false;
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        rejected |= strstr(analysis->diagnostics[i].message, "Query predicate cannot read") !=
                    NULL;
    }
    REQUIRE(rejected);
    return true;
}
static uint64_t state(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, name)) {
            return analysis->symbols[i].id;
        }
    }
    return 0;
}
static bool operation_backend_entries(behavior_run_counts counts, behavior_backend backend, const char *phase)
{
    printf(
        "operations phase=%s backend=%s bytecode=%" PRIu64 " sljit=%" PRIu64
        " wasm=%" PRIu64 " inspected_sljit=%" PRIu64 " inspected_wasm=%" PRIu64
        " inspection_fallback=%" PRIu64 " size_fallback=%" PRIu64
        " overflow=%d\n",
        phase,
        backend == BEHAVIOR_BYTECODE ? "bytecode"
        : backend == BEHAVIOR_SLJIT  ? "sljit"
                                     : "wasm",
        counts.bytecode_entries,
        counts.sljit_entries,
        counts.wasm_entries,
        counts.inspected_sljit_entries,
        counts.inspected_wasm_entries,
        counts.inspection_bytecode_fallbacks,
        counts.size_bytecode_fallbacks,
        counts.overflow
    );
    REQUIRE(!counts.overflow);
    REQUIRE(!counts.inspection_bytecode_fallbacks && !counts.size_bytecode_fallbacks);
    REQUIRE(!counts.inspected_sljit_entries && !counts.inspected_wasm_entries);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE ? counts.bytecode_entries > 0
                                     : counts.bytecode_entries == 0
    );
    REQUIRE(backend == BEHAVIOR_SLJIT ? counts.sljit_entries > 0 : counts.sljit_entries == 0);
    REQUIRE(backend == BEHAVIOR_WASM ? counts.wasm_entries > 0 : counts.wasm_entries == 0);
    return true;
}
TEST_CASE(operations_preserve_context_and_delay_lifetimes_on_both_backends)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    const script_catalog_parameter duration = { .name = "duration",
                                                .type = { .storage = PIN_F64 } };
    const script_catalog_capability event = { .id = 1,
                                              .name = "Run",
                                              .kind = SCRIPT_CAPABILITY_EVENT,
                                              .parameters = &duration,
                                              .parameter_count = 1,
                                              .completion_outcome = -1 };
    const script_catalog base = { .capabilities = &event,
                                  .capability_count = 1,
                                  .entity_type = script_catalog_id("type", "ecs.entity") };
    const native_capability_pack pack = { .catalog = &base };
    const char
        *source = "Probe :: behavior { before: entity = 0; resumed_value: "
                  "entity = 0; same: "
                  "bool = false; "
                  "text: string = \"old\"; on Run :: (duration: f64) { "
                  "before = "
                  "owner_alias(); "
                  "saved := before; same = owner_alias() == self; "
                  "saved_text := text; "
                  "text = \"new\"; "
                  "await delay(duration); resumed_value = saved; text = "
                  "saved_text; } }";
    for (int b = 0; b < 2; b++) {
        cleanup();
        char error[256];
        native = with_core(&pack, 1, error, sizeof error);
        REQUIRE(native);
        const script_catalog *catalog = native_capabilities_catalog(native);
        REQUIRE(analyze_source(source, catalog));
        script_lower_bindings bindings = authoring_native_bindings(native);
        plan = script_lower(analysis, catalog, &bindings);
        REQUIRE(plan && plan->valid);
        module = execution_module_finish(&plan, backends[b], error, sizeof error);
        REQUIRE(module);
        program = execution_program_new(module, NULL, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, 7);
        REQUIRE(instance);
        const double waits[] = { 0, -1, 0.25, NAN };
        for (int i = 0; i < 4; i++) {
            const pin_value argument = { .type = PIN_F64, .d = waits[i] };
            behavior_run_scope run = { 0 };
            behavior_run_scope_begin(&run);
            int status = execution_instance_dispatch(instance, 1, &argument, 1, i, 0.125, 1000);
            behavior_run_scope_end(&run);
            REQUIRE(status == FLOW_OK);
            REQUIRE(operation_backend_entries(run.counts, backends[b], "dispatch"));
            REQUIRE(execution_instance_state(instance, state("before"))->u64 == 7);
            REQUIRE(execution_instance_state(instance, state("same"))->b);
            REQUIRE(execution_instance_pending(instance) == (i >= 2));
            if (i == 2) {
                REQUIRE(!strcmp(
                    pin_text(*execution_instance_state(instance, state("text"))).bytes,
                    "new"
                ));
                behavior_run_scope_reset(&run);
                behavior_run_scope_begin(&run);
                status = execution_program_update(program, i + 0.25, 0.5, 1000);
                behavior_run_scope_end(&run);
                REQUIRE(status == FLOW_OK);
                REQUIRE(operation_backend_entries(run.counts, backends[b], "resume"));
                REQUIRE(!execution_instance_pending(instance));
            }
            if (i < 3) {
                REQUIRE(
                    execution_instance_state(instance, state("resumed_value"))->u64 == 7
                );
                REQUIRE(!strcmp(
                    pin_text(*execution_instance_state(instance, state("text"))).bytes,
                    "old"
                ));
            }
        }
    }
    return true;
}
void test_language_operations()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(operations_preserve_unknown_abi);
    RUN_TEST_CASE(operations_generate_checked_bindings_and_conservative_contracts);
    RUN_TEST_CASE(operations_reject_malformed_shared_declarations);
    RUN_TEST_CASE(operations_reject_raw_scalar_semantic_metadata);
    RUN_TEST_CASE(operations_reject_dormant_raw_metadata);
    RUN_TEST_CASE(operations_reject_binding_mismatch_and_distinguish_opaque_abi);
    RUN_TEST_CASE(operation_identity_invalidates_source_reuse);
    RUN_TEST_CASE(operations_reject_state_access_calls);
    RUN_TEST_CASE(operations_reject_custom_lowerer_binding_mismatch);
    RUN_TEST_CASE(owner_alias_preserves_invocation_effects);
    RUN_TEST_CASE(operations_preserve_context_and_delay_lifetimes_on_both_backends);
    pu_clear_setup();
}
