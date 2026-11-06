#include "backend.h"
#include "ecs/ecs.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Four tiny runtime specimens, two backends, at most three items per loop.
 * Each dispatch/resume has a 1000-instruction budget. Invalid cases analyze
 * only; forged cursor producers/signatures finalize six tiny definitions. */
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static ecs_world *world;
static const script_catalog_capability run = { .id = 10,
                                               .name = "Run",
                                               .kind = SCRIPT_CAPABILITY_EVENT };
static const script_catalog catalog = { .capabilities = &run, .capability_count = 1 };
static void cleanup()
{
    execution_program_free(program);
    execution_module_free(module);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    program = NULL;
    module = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
}
static execution_instance *prepare(const char *source, behavior_backend backend)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(stderr, "iteration semantic: %s\n", analysis->diagnostics[i].message);
        }
        return NULL;
    }
    script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(stderr, "iteration lower: %s\n", plan->diagnostics[i].message);
        }
        execution_module_plan_free(plan);
        return NULL;
    }
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "iteration publication: %s\n", error);
        return NULL;
    }
    world = ecs_world_new();
    program = execution_program_new(module, world, NULL);
    return program
             ? execution_program_attach(program, ecs_entity_to_u64(ecs_world_create_entity(world)))
             : NULL;
}
static const pin_value *state(execution_instance *instance, const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (!strcmp(analysis->symbols[i].name, name)) {
            return execution_instance_state(instance, analysis->symbols[i].id);
        }
    }
    return NULL;
}
TEST_CASE(iteration_array_items_and_indices_keep_snapshot)
{
    const char *source = "Loop :: behavior { sum: retain int = 0; indices: "
                         "retain int = 0; "
                         "items: retain int[] = [2,4,6]; on Run :: () { for "
                         "item,index in items { "
                         "sum += item; indices += index; items = [99]; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        execution_instance *instance = prepare(source, backends[b]);
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 1000) == FLOW_OK);
        REQUIRE(state(instance, "sum") && state(instance, "sum")->i == 12);
        REQUIRE(state(instance, "indices") && state(instance, "indices")->i == 3);
        const pin_value *items = state(instance, "items");
        REQUIRE(items && items->ptr && ((pin_array *)items->ptr)->count == 1);
        REQUIRE(pin_array_get(items->ptr, 0).i == 99);
    }
    return true;
}
TEST_CASE(iteration_unicode_strings_keep_scalar_values_and_snapshot)
{
    const char *source = "Loop :: behavior { count: retain int = 0; middle: "
                         "retain bool = false; "
                         "text: retain string = \"Aé🐈\"; on Run :: () { for "
                         "character in text { "
                         "count += 1; if character == \"é\" { middle = true; } "
                         "text = \"\"; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        execution_instance *instance = prepare(source, backends[b]);
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 1000) == FLOW_OK);
        REQUIRE(state(instance, "count") && state(instance, "count")->i == 3);
        REQUIRE(state(instance, "middle") && state(instance, "middle")->b);
        REQUIRE(
            state(instance, "text") &&
            !strcmp(pin_text(*(state(instance, "text"))).bytes, "")
        );
        REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 1, 0, 1000) == FLOW_OK);
        REQUIRE(state(instance, "count")->i == 3);
    }
    return true;
}
TEST_CASE(iteration_reference_items_write_source_and_preserve_copied_alias)
{
    const char
        *source = "Loop :: behavior { sum: retain int = 0; items: retain int[] "
                  "= [2,4,6]; "
                  "on Run :: () { alias := items; for &item in items { "
                  "item += 10; "
                  "if item == 14 { continue; } if item == 16 { break; } } "
                  "for item in alias { sum += item; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        execution_instance *instance = prepare(source, backends[b]);
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 1000) == FLOW_OK);
        REQUIRE(state(instance, "sum") && state(instance, "sum")->i == 12);
        const pin_value *items = state(instance, "items");
        REQUIRE(items && items->ptr && ((pin_array *)items->ptr)->count == 3);
        for (int i = 0; i < 3; i++) {
            REQUIRE(pin_array_get(items->ptr, i).i == 12 + 2 * i);
        }
    }
    return true;
}
TEST_CASE(iteration_reference_lifetimes_reject_invalidation_and_suspension)
{
    const char *sources[] = {
        "Bad :: behavior { on Run :: () { for &item in [1,2] { item = 3; } "
        "} }",
        "Bad :: behavior { on Run :: () { for &item in \"text\" {} } }",
        "Bad :: behavior { items: retain int[] = [1]; on Run :: () { "
        "for i, &item in items {} } }",
        "Bad :: behavior { items: retain int[] = [1]; on Run :: () { for &item "
        "in items { items = [2]; } } }",
        "Bad :: behavior { items: retain int[] = [1]; on Run :: () { for &item "
        "in items { yield; } } }",
        "Bad :: behavior { items: retain int[] = [1]; Wait :: () { yield; } "
        "on Run :: () { for &item in items { pending := Wait(); } } }",
        "Bad :: behavior { items: retain int[] = [1]; Change :: () { items[0] "
        "= 2; } "
        "on Run :: () { for &item in items { Change(); } } }",
    };
    for (int i = 0; i < (int)(sizeof sources / sizeof *sources); i++) {
        cleanup();
        syntax = script_parse(sources[i]);
        analysis = script_analyze(syntax, &catalog);
        REQUIRE(!syntax->diagnostic_count && !analysis->valid);
    }
    return true;
}
TEST_CASE(iteration_unicode_intrinsics_reject_invalid_scalars)
{
    arena storage = { 0 };
    const char *invalid[] = { "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82" };
    for (int i = 0; i < 4; i++) {
        pin_value text = { .type = PIN_STR,
                           .text = &(
                               const pin_string
                           ){ .bytes = invalid[i],
                              .length = (uint32_t)strlen(invalid[i]) } },
                  cursor;
        REQUIRE(!EXECUTION_string_cursor_call.value(&storage, &text, 1, &cursor));
        pin_value args[] = { cursor, { .type = PIN_I32 } };
        pin_value out = { 0 };
        REQUIRE(EXECUTION_string_item_call.value(&storage, args, 2, &out));
    }
    char mutable[] = "Aé";
    pin_value text = { .type = PIN_STR,
                       .text = &(const pin_string){ .bytes = mutable,
                                                    .length = (uint32_t)strlen(mutable) } },
              cursor, out;
    REQUIRE(!EXECUTION_string_cursor_call.value(&storage, &text, 1, &cursor));
    mutable[0] = 0;
    REQUIRE(!EXECUTION_string_bytes_call.value(&storage, &cursor, 1, &out));
    REQUIRE(out.i == 3);
    pin_value args[] = { cursor, { .type = PIN_I32 } };
    REQUIRE(!EXECUTION_string_item_call.value(&storage, args, 2, &out));
    REQUIRE(!strcmp(pin_text(out).bytes, "A"));
    const int offsets[] = { -1, 2, 3, INT_MAX };
    for (int i = 0; i < 4; i++) {
        args[1].i = offsets[i];
        REQUIRE(EXECUTION_string_item_call.value(&storage, args, 2, &out));
        REQUIRE(EXECUTION_string_next_call.value(&storage, args, 2, &out));
    }
    text.text = &(const pin_string){ .bytes = "", .length = (uint32_t)strlen("") };
    REQUIRE(!EXECUTION_string_cursor_call.value(&storage, &text, 1, &cursor));
    REQUIRE(!EXECUTION_string_bytes_call.value(&storage, &cursor, 1, &out));
    REQUIRE(out.i == 0);
    args[0] = cursor;
    args[1].i = 0;
    REQUIRE(EXECUTION_string_item_call.value(&storage, args, 2, &out));
    args[0].u64 = 0;
    REQUIRE(EXECUTION_string_bytes_call.value(&storage, args, 1, &out));
    REQUIRE(EXECUTION_string_item_call.value(&storage, args, 2, &out));
    arena_free(&storage);
    return true;
}
TEST_CASE(iteration_cursor_provenance_rejects_forged_inputs)
{
    for (int kind = BEHAVIOR_VALUE; kind <= BEHAVIOR_STRING_CURSOR; kind++) {
        behavior_function_builder *builder = behavior_function_builder_new();
        pin_value args[] = { { .type = BF_POINTER_TYPE,
                               .u64 = kind == BEHAVIOR_STRING_CURSOR ? 1 : 0 },
                             { .type = PIN_I32 } };
        pin_value result = { .type = PIN_STR };
        int host = behavior_function_builder_host(
            builder,
            &EXECUTION_string_item_call,
            &EXECUTION_value_access,
            args,
            2,
            &result,
            1
        );
        REQUIRE(behavior_function_builder_kind(
            builder,
            BEHAVIOR_BANK_DATA,
            behavior_function_builder_host_first(builder, host),
            kind
        ));
        uint64_t id = execution_intrinsic_id(&EXECUTION_string_item_call);
        REQUIRE(
            behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
        );
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_HOST_CALL, .target = host }
        );
        behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
        behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
        char error[256];
        behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(!function && error[0]);
        behavior_function_builder_free(builder);
    }
    for (int result = 0; result < 2; result++) {
        behavior_function_builder *builder = behavior_function_builder_new();
        pin_value pointer = { .type = BF_POINTER_TYPE };
        behavior_function_builder_signature(
            builder,
            result ? NULL : &pointer,
            result ? 0 : 1,
            result ? &pointer : NULL,
            result ? 1 : 0,
            NULL,
            0
        );
        REQUIRE(behavior_function_builder_kind(
            builder,
            result ? BEHAVIOR_BANK_RESULT : BEHAVIOR_BANK_ARGUMENT,
            0,
            BEHAVIOR_STRING_CURSOR
        ));
        behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
        behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
        char error[256];
        behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(!function && strstr(error, "invocation boundary"));
        behavior_function_builder_free(builder);
    }
    return true;
}
TEST_CASE(iteration_cursor_survives_suspension)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        execution_instance *instance = prepare(
            "Loop :: behavior { count:retain int=0; on Run :: () { for "
            "character in "
            "\"Aé\" { count+=1; yield; } } }",
            backends[b]
        );
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 1000) == FLOW_OK);
        REQUIRE(state(instance, "count")->i == 1);
        REQUIRE(execution_program_update(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(state(instance, "count")->i == 2);
        REQUIRE(execution_program_update(program, 2, 0, 1000) == FLOW_OK);
        REQUIRE(state(instance, "count")->i == 2);
    }
    return true;
}
void test_language_iteration()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(iteration_array_items_and_indices_keep_snapshot);
    RUN_TEST_CASE(iteration_unicode_strings_keep_scalar_values_and_snapshot);
    RUN_TEST_CASE(iteration_reference_items_write_source_and_preserve_copied_alias);
    RUN_TEST_CASE(iteration_reference_lifetimes_reject_invalidation_and_suspension);
    RUN_TEST_CASE(iteration_unicode_intrinsics_reject_invalid_scalars);
    RUN_TEST_CASE(iteration_cursor_provenance_rejects_forged_inputs);
    RUN_TEST_CASE(iteration_cursor_survives_suspension);
    pu_clear_setup();
}
