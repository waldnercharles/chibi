#include "compositional_records.h"

#include "backend.h"
#include "capabilities/catalog_snapshot.h"
#include "compiler_fixture.h"
#include "compositional_records_source.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "language/lower.h"
#include "package_compiler_chibi.h"
#include "runtime/core_definition.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static compiler_fixture fixture;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static execution_module *next;
static execution_program *program;
static ecs_world *world;
static behavior_function_activation activation;
static behavior_function_context context;
static behavior_managed_heap managed;
static arena storage, retained, catalog_storage;
static unsigned char *image;
static size_t image_size;
static const script_catalog catalog;
static const script_lower_bindings bindings;
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
const char COMPOSITIONAL_RECORD_ALIAS_REJECTION
    [] = "package facts\n"
         "Node :: struct { value: int; }\n"
         "Clear :: (slot: ^(^Node?)) { slot^ = nil; }\n"
         "pub Main :: () -> int { value: Node = {value: 7}; "
         "p: ^Node? = &value; if p != nil { Clear(&p); return p.value; } "
         "return 0; }\n";

bool compositional_records_depth_source(char *source, size_t size, int count, bool arrays)
{
    int used = snprintf(source, size, "package facts\n");
    if (used < 0 || (size_t)used >= size) {
        return false;
    }
    for (int i = count - 1; i >= 0; i--) {
        int wrote = i ? snprintf(
                            source + used,
                            size - (size_t)used,
                            "Depth%d :: struct { child: Depth%d%s; }\n",
                            i,
                            i - 1,
                            arrays || i % 2 ? "[]" : ""
                        )
                      : snprintf(source + used, size - (size_t)used, "Depth0 :: struct { value: int; }\n");
        if (wrote < 0 || (size_t)wrote >= size - (size_t)used) {
            return false;
        }
        used += wrote;
    }
    int wrote = snprintf(source + used, size - (size_t)used, "pub Main :: () -> int { return 0; }\n");
    return wrote > 0 && (size_t)wrote < size - (size_t)used;
}

bool compositional_records_fanout_source(char *source, size_t size)
{
    int used = snprintf(source, size, "package facts\nDepth0 :: struct { value: int; }\n");
    if (used < 0 || (size_t)used >= size) {
        return false;
    }
    for (int i = 1; i < 9; i++) {
        int wrote = snprintf(source + used, size - (size_t)used, "Depth%d :: struct {", i);
        if (wrote < 0 || (size_t)wrote >= size - (size_t)used) {
            return false;
        }
        used += wrote;
        for (int field = 0; field < 10; field++) {
            wrote = snprintf(source + used, size - (size_t)used, " child%d: Depth%d[];", field, i - 1);
            if (wrote < 0 || (size_t)wrote >= size - (size_t)used) {
                return false;
            }
            used += wrote;
        }
        wrote = snprintf(source + used, size - (size_t)used, " }\n");
        if (wrote < 0 || (size_t)wrote >= size - (size_t)used) {
            return false;
        }
        used += wrote;
    }
    int wrote = snprintf(source + used, size - (size_t)used, "pub Main :: () -> int { return 0; }\n");
    return wrote > 0 && (size_t)wrote < size - (size_t)used;
}

static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    behavior_managed_heap_free(&managed);
    arena_free(&storage);
}

static void clear_analysis()
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
}

static void cleanup()
{
    finish_call();
    compiler_fixture_free(&fixture);
    execution_program_free(program);
    program = NULL;
    execution_module_free(module);
    execution_module_free(next);
    next = NULL;
    if (world) {
        ecs_world_free(world);
        world = NULL;
    }
    execution_module_plan_free(plan);
    module = NULL;
    plan = NULL;
    clear_analysis();
    arena_free(&retained);
    arena_free(&catalog_storage);
    free(image);
    image = NULL;
    image_size = 0;
}

TEST_CASE(compositional_records_authored_values)
{
    const source_package_input source = { "compositional_records/Values.chibi",
                                          COMPOSITIONAL_RECORDS_SOURCE,
                                          sizeof COMPOSITIONAL_RECORDS_SOURCE - 1 };
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, &source, 1, core, 1));
    REQUIRE(compiler_fixture_tests(&fixture));
    return true;
}

static bool analyze_source(const char *source)
{
    clear_analysis();
    syntax = script_parse(source);
    REQUIRE(!syntax->diagnostic_count);
    if (!strncmp(source, "package ", 8)) {
        const script_package_document document = { "/facts/Main.chibi",
                                                   "facts/Main.chibi",
                                                   syntax };
        const script_package_options options = { .documents = &document,
                                                 .document_count = 1,
                                                 .identity = "project:facts",
                                                 .native = &catalog };
        analysis = script_analyze_package(&options);
    } else {
        analysis = script_analyze(syntax, &catalog);
    }
    REQUIRE(analysis);
    return true;
}

static bool rejects(const char *source, const char *message)
{
    REQUIRE(analyze_source(source));
    REQUIRE(!analysis->valid);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        if (strstr(analysis->diagnostics[i].message, message)) {
            return true;
        }
    }
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "compositional record rejection: %s\n",
            analysis->diagnostics[i].message
        );
    }
    REQUIRE(false);
    return false;
}

TEST_CASE(compositional_records_reject_unbounded_layouts)
{
    REQUIRE(rejects("Records :: library { Node :: struct { next: Node; } }", "recursive"));
    REQUIRE(rejects(
        "Records :: library { Left :: struct { right: Right; } "
        "Right :: struct { left: Left; } }",
        "recursive"
    ));
    REQUIRE(rejects("Records :: library { Node :: struct { children: Node[]; } }", "recursive"));
    return true;
}

TEST_CASE(compositional_records_require_explicit_fields)
{
    REQUIRE(rejects(
        "Records :: library { Inner :: struct { value: int; } "
        "Outer :: struct { inner: Inner; } "
        "Make :: () -> Outer { return {}; } }",
        "Missing required"
    ));
    REQUIRE(rejects(
        "Records :: library { Numbers :: struct { values: int[]; } "
        "Make :: () -> Numbers { return {}; } }",
        "Missing required"
    ));
    return true;
}

TEST_CASE(compositional_records_union_fields_are_single_and_explicit)
{
    REQUIRE(rejects(
        "Records :: library { Phase :: union { Idle | Done(score: int) } "
        "Fighter :: struct { phase: Phase; } "
        "Make :: () -> Fighter { return {}; } }",
        "Missing required"
    ));
    REQUIRE(rejects(
        "Records :: library { Phase :: union { Idle | Done(score: int) } "
        "Fighter :: struct { phases: Phase[]; } }",
        "Record fields require"
    ));
    REQUIRE(rejects(
        "Records :: library { Phase :: union { Idle | Done(score: int) } "
        "Fighter :: struct { phase: Phase?; } }",
        "Record fields require"
    ));
    return true;
}

TEST_CASE(compositional_records_pointer_guards_respect_aliases)
{
    REQUIRE(rejects(COMPOSITIONAL_RECORD_ALIAS_REJECTION, "Nullable pointer requires a presence guard"));
    return true;
}

TEST_CASE(compositional_records_bound_record_depth)
{
    char source[4096];
    for (int arrays = 0; arrays < 2; arrays++) {
        REQUIRE(compositional_records_depth_source(source, sizeof source, 16, arrays));
        REQUIRE(analyze_source(source));
        for (int i = 0; !analysis->valid && i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "compositional depth %d: %s\n",
                arrays,
                analysis->diagnostics[i].message
            );
        }
        REQUIRE(analysis->valid);
        REQUIRE(compositional_records_depth_source(source, sizeof source, 17, arrays));
        REQUIRE(rejects(source, "recursive"));
    }
    return true;
}

TEST_CASE(compositional_records_bound_metadata_expansion)
{
    char source[4096];
    REQUIRE(compositional_records_fanout_source(source, sizeof source));
    REQUIRE(rejects(source, "layout"));
    return true;
}

static bool member(const pin_value *value, const char *name, pin_value *out)
{
    const type_desc *type = pin_value_type(value);
    REQUIRE(type && value->record && pin_record_valid(value->record));
    const type_field *field = pin_type_field_path(type, name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&retained, value->ptr, field, out));
    return true;
}

static bool call_retained(uint64_t id, behavior_backend backend, pin_value *saved)
{
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info info = execution_module_function_at(module, i);
        if (info.id == id) {
            function = info.body;
        }
    }
    REQUIRE(function && function->argument_count == 0 && function->result_count == 1);
    pin_value result = function->result_defaults[0];
    behavior_function_prepare_results(function, &result, &storage);
    behavior_managed_heap_init(&managed);
    context = (behavior_function_context){ .activation = &activation,
                                           .results = &result,
                                           .managed_heap = &managed,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "compositional prepared call: %s\n",
            context.error_message ? context.error_message : ""
        );
    }
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE
            ? runs.counts.bytecode_entries > 0 && !runs.counts.sljit_entries &&
                  !runs.counts.wasm_entries
        : backend == BEHAVIOR_SLJIT
            ? runs.counts.sljit_entries > 0 && !runs.counts.bytecode_entries &&
                  !runs.counts.wasm_entries
            : runs.counts.wasm_entries > 0 && !runs.counts.bytecode_entries &&
                  !runs.counts.sljit_entries
    );
    REQUIRE(!pin_value_capture_to_arena(&retained, saved, &result));
    finish_call();
    execution_module_free(module);
    module = NULL;
    return true;
}

/* Chibi cannot retire its producing compiler and prepared byte buffer. This
 * boundary checks the nested descriptors and values after all producers die. */
TEST_CASE(compositional_records_prepared_values_outlive_producers)
{
    syntax = script_parse(
        "Records :: library { Inner :: struct { label: string; } "
        "Outer :: struct { inner: Inner; values: int[]; } "
        "Make :: () -> Outer { return {inner: {label: \"retained\"}, "
        "values: [11, 13]}; } }"
    );
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis->valid && analysis->function_count == 1);
    uint64_t id = analysis->symbols[analysis->functions[0].symbol].id;
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    char error[256];
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    clear_analysis();
    REQUIRE(execution_prepared_write(module, &image, &image_size, error, sizeof error));
    execution_module_free(module);
    module = NULL;
    REQUIRE(image && image_size > 32);
    for (int b = 0; b < 2; b++) {
        unsigned char *input = malloc(image_size);
        REQUIRE(input);
        memcpy(input, image, image_size);
        module = execution_prepared_read(input, image_size, NULL, backends[b], error, sizeof error);
        memset(input, 0xa5, image_size);
        free(input);
        if (!module) {
            fprintf(stderr, "compositional prepared read: %s\n", error);
        }
        REQUIRE(module);
        pin_value saved, inner, label, values;
        REQUIRE(call_retained(id, backends[b], &saved));
        REQUIRE(saved.record && arena_contains(&retained, saved.record));
        REQUIRE(member(&saved, "inner", &inner));
        REQUIRE(inner.record && arena_contains(&retained, inner.record));
        REQUIRE(member(&inner, "label", &label));
        REQUIRE(!strcmp(pin_text(label).bytes, "retained"));
        REQUIRE(member(&saved, "values", &values));
        REQUIRE(values.container == PIN_CONTAINER_ARRAY && values.type == PIN_I32);
        const pin_array *array = values.ptr;
        REQUIRE(array && array->count == 2 && arena_contains(&retained, array));
        REQUIRE(pin_array_get(array, 0).i == 11 && pin_array_get(array, 1).i == 13);
        arena_free(&retained);
    }
    return true;
}

static const script_catalog_capability TOUCH = { .id = 71,
                                                 .name = "Touch",
                                                 .kind = SCRIPT_CAPABILITY_EVENT };

static bool compile_reload(int generation, behavior_backend backend, execution_module **out, uint64_t *state)
{
    clear_analysis();
    char source[512];
    int wrote = snprintf(
        source,
        sizeof source,
        "Records :: library { Child :: struct { value: int; %s } "
        "Outer :: struct { child: Child; values: int[]; } }",
        generation == 2 ? "extra: int = 0;" : ""
    );
    REQUIRE(wrote > 0 && (size_t)wrote < sizeof source);
    REQUIRE(analyze_source(source) && analysis->valid);
    script_catalog source_catalog = { .types = analysis->types,
                                      .type_count = analysis->type_count,
                                      .capabilities = &TOUCH,
                                      .capability_count = 1 },
                   owned;
    REQUIRE(script_catalog_snapshot(&catalog_storage, &source_catalog, &owned));
    clear_analysis();
    wrote = snprintf(
        source,
        sizeof source,
        "Store :: behavior { row: Records.Outer = {child: {value: %d}, "
        "values: [%d]}; on Touch :: () { row.child.value += 1; "
        "row.values[0] += 1; } }",
        generation == 2   ? 300
        : generation == 1 ? 30
                          : 3,
        generation == 2   ? 500
        : generation == 1 ? 50
                          : 5
    );
    REQUIRE(wrote > 0 && (size_t)wrote < sizeof source);
    syntax = script_parse(source);
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze(syntax, &owned);
    for (int i = 0; !analysis->valid && i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "compositional reload: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid);
    *state = 0;
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, "row")) {
            *state = analysis->symbols[i].id;
        }
    }
    REQUIRE(*state);
    plan = script_lower(analysis, &owned, &bindings);
    REQUIRE(plan && plan->valid);
    char error[256];
    *out = execution_module_finish(&plan, backend, error, sizeof error);
    if (!*out) {
        fprintf(stderr, "compositional reload module: %s\n", error);
    }
    REQUIRE(*out);
    clear_analysis();
    arena_free(&catalog_storage);
    return true;
}

static bool reload_value(execution_instance *instance, uint64_t state, int expected, int expected_element, bool changed)
{
    const pin_value *row = execution_instance_state(instance, state);
    pin_value child, value, values;
    REQUIRE(row && member(row, "child", &child));
    REQUIRE(member(&child, "value", &value) && value.i == expected);
    if (changed) {
        REQUIRE(member(&child, "extra", &value) && value.i == 0);
    }
    REQUIRE(member(row, "values", &values));
    REQUIRE(values.container == PIN_CONTAINER_ARRAY && values.ptr);
    REQUIRE(pin_array_valid(values.ptr) && ((const pin_array *)values.ptr)->count == 1);
    REQUIRE(pin_array_get(values.ptr, 0).i == expected_element);
    return true;
}

TEST_CASE(compositional_records_reload_checks_child_layouts)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        uint64_t state, replaced;
        REQUIRE(compile_reload(0, backends[b], &module, &state));
        world = ecs_world_new();
        REQUIRE(world);
        uint64_t owner = ecs_entity_to_u64(ecs_world_create_entity(world));
        REQUIRE(owner && ecs_world_apply_commands(world) == ECS_OK);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, owner);
        REQUIRE(instance && reload_value(instance, state, 3, 5, false));
        uint64_t first_layout = execution_instance_state(instance, state)
                                    ->record->layout;
        REQUIRE(execution_instance_dispatch(instance, TOUCH.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        REQUIRE(reload_value(instance, state, 4, 6, false));
        REQUIRE(compile_reload(1, backends[b], &next, &replaced) && replaced == state);
        REQUIRE(execution_program_replace(program, next, false) == FLOW_OK);
        execution_module_free(module);
        module = next;
        next = NULL;
        instance = execution_program_instance(program, owner);
        REQUIRE(reload_value(instance, state, 4, 6, false));
        REQUIRE(execution_instance_state(instance, state)->record->layout == first_layout);
        REQUIRE(compile_reload(2, backends[b], &next, &replaced) && replaced == state);
        REQUIRE(execution_program_replace(program, next, false) == FLOW_OK);
        execution_module_free(module);
        module = next;
        next = NULL;
        instance = execution_program_instance(program, owner);
        /* The incompatible child uses its new initial value; the compatible
         * array field retains its previously mutated elements. */
        REQUIRE(reload_value(instance, state, 300, 6, true));
        REQUIRE(execution_instance_state(instance, state)->record->layout != first_layout);
        REQUIRE(execution_instance_dispatch(instance, TOUCH.id, NULL, 0, 0, 0, 10000) == FLOW_OK);
        REQUIRE(reload_value(instance, state, 301, 7, true));
    }
    return true;
}

void test_language_compositional_records()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(compositional_records_authored_values);
    RUN_TEST_CASE(compositional_records_reject_unbounded_layouts);
    RUN_TEST_CASE(compositional_records_require_explicit_fields);
    RUN_TEST_CASE(compositional_records_union_fields_are_single_and_explicit);
    RUN_TEST_CASE(compositional_records_pointer_guards_respect_aliases);
    RUN_TEST_CASE(compositional_records_bound_record_depth);
    RUN_TEST_CASE(compositional_records_bound_metadata_expansion);
    RUN_TEST_CASE(compositional_records_prepared_values_outlive_producers);
    RUN_TEST_CASE(compositional_records_reload_checks_child_layouts);
    pu_clear_setup();
    test_language_package_compiler_chibi_records();
}
