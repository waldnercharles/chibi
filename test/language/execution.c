#include "ecs/execution.h"

#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "capabilities/catalog.h"
#include "compiler_fixture.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/integer_report.h"
#include "execution/intrinsics.h"
#include "execution/jit_internal.h"
#include "execution/log.h"
#include "execution/managed.h"
#include "execution/module.h"
#include "execution/module_effects.h"
#include "execution/preparation_budget.h"
#include "execution/prepared_internal.h"
#include "execution/query.h"
#include "execution/scalar.h"
#include "execution/service.h"
#include "execution/status.h"
#include "runtime/arrays.h"
#include "runtime/behaviors/package.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy.h"
#include "types/pointer.h"
#include "types/query.h"
#include "types/record.h"
#include "types/schema.h"
#include "types/text.h"
#include "types/trace.h"
#include "types/union.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    int32_t value;
} fixture_box;
static const type_field box_field = { .name = "value",
                                      .type = PIN_I32,
                                      .size = sizeof(int32_t),
                                      .type_name = "int" };
static const type_desc box_type = { .name = "fixture.box",
                                    .size = sizeof(fixture_box),
                                    .fields = &box_field,
                                    .field_count = 1 };
typedef struct
{
    const char *text;
} fixture_c_text;
static const type_field c_text_field = { .name = "text",
                                         .type = PIN_STR,
                                         .size = sizeof(const char *),
                                         .type_name = "const char *" };
static const type_desc c_text_type = { .name = "fixture.c_text",
                                       .size = sizeof(fixture_c_text),
                                       .fields = &c_text_field,
                                       .field_count = 1 };
static const type_field narrow_enum_field = { .name = "mode", .type = PIN_ENUM, .size = 1 };
static const type_desc narrow_enum_type = { .name = "fixture.narrow_enum",
                                            .size = 1,
                                            .fields = &narrow_enum_field,
                                            .field_count = 1 };
static const type_desc *const types[] = { &box_type, &c_text_type, &narrow_enum_type };

int type_catalog_pack_count()
{
    return 1;
}

type_catalog_pack type_catalog_pack_at(int index)
{
    assert(index == 0);
    return (type_catalog_pack){ .types = types, .type_count = 3 };
}

static behavior_function_builder *builder;
static ecs_world *pointer_world;
static execution_program *pointer_program;
static behavior_function *function, *callee, *reentered;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage, managed;
static behavior_managed_heap invocation_heap;
static behavior_managed_heap comparison_heap;
static behavior_managed_heap pointer_heap;
static behavior_managed_heap secondary_pointer_heap;
static pin_value pointer_root;
static bool pointer_root_registered;
static bool return_types_active;
static int host_calls;
static compiler_fixture sequences;
static service_binding *log_services;
static execution_module_plan *prepared_plan;
static execution_module *prepared_module, *imported_module;
static unsigned char *prepared_bytes, *prepared_mutation, *prepared_again;

static void release_invocation()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&invocation_heap);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
}

static void cleanup()
{
    compiler_fixture_free(&sequences);
    if (return_types_active) {
        type_catalog_set_project(NULL);
        return_types_active = false;
    }
    execution_program_free(pointer_program);
    pointer_program = NULL;
    ecs_world_free(pointer_world);
    pointer_world = NULL;
    release_invocation();
    behavior_managed_heap_free(&comparison_heap);
    if (pointer_root_registered) {
        behavior_managed_heap_unroot(&pointer_heap, &pointer_root);
        pointer_root_registered = false;
    }
    behavior_managed_heap_free(&pointer_heap);
    behavior_managed_heap_free(&secondary_pointer_heap);
    pointer_root = (pin_value){ 0 };
    execution_module_plan_free(prepared_plan);
    execution_module_free(prepared_module);
    execution_module_free(imported_module);
    free(prepared_bytes);
    free(prepared_again);
    afree(prepared_mutation);
    prepared_plan = NULL;
    prepared_module = imported_module = NULL;
    prepared_bytes = prepared_mutation = prepared_again = NULL;
    arena_free(&managed);
    afree(log_services);
    if (builder) {
        behavior_function_builder_free(builder);
    }
    behavior_function_free(function);
    behavior_function_free(callee);
    behavior_function_free(reentered);
    builder = NULL;
    function = callee = reentered = NULL;
    host_calls = 0;
}

static int emit(behavior_function_instruction instruction)
{
    return behavior_function_builder_emit(builder, instruction);
}

static int slot(pin_type type)
{
    return behavior_function_builder_slot(builder, type);
}

static int address_slot()
{
    int index = slot(BF_POINTER_TYPE);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, index, BEHAVIOR_ADDRESS));
    return index;
}

static bool finish()
{
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "execution builder: %s\n", error);
    }
    return function != NULL;
}

static int invoke(
    const pin_value *arguments,
    int count,
    pin_value *variables,
    pin_value *results,
    bool native,
    unsigned budget,
    behavior_managed_heap *heap
)
{
    bool resumable = behavior_function_resumable(function);
    if (resumable) {
        behavior_function_activation_init(function, &activation);
    }
    context = (behavior_function_context){ .activation = resumable ? &activation : NULL,
                                           .arguments = arguments,
                                           .argument_count = count,
                                           .variables = variables,
                                           .results = results,
                                           .managed_heap = heap,
                                           .budget = budget };
    behavior_function_bind_storage(&context, &storage);
    assert(behavior_function_bind(function, &context, NULL, 0));
    return behavior_function_run(function, &context, native);
}

static int run(
    const pin_value *arguments,
    int count,
    pin_value *variables,
    pin_value *results,
    bool native,
    unsigned budget
)
{
    release_invocation();
    behavior_managed_heap_init(&invocation_heap);
    return invoke(arguments, count, variables, results, native, budget, &invocation_heap);
}

static const char *scale(void *, const pin_value *arguments, pin_value *results)
{
    host_calls++;
    results[0].d = arguments[0].d * 2;
    return NULL;
}

static int source_pauses, source_captures;
static int source_pause(void *, behavior_function_context *, const behavior_source_point *)
{
    source_pauses++;
    return 1;
}
static void source_capture(
    void *,
    behavior_function_context *,
    const behavior_source_watch *,
    const pin_value *value
)
{
    assert(value && value->i == 37);
    source_captures++;
}
TEST_CASE(execution_source_inspection_is_lazy_and_preserves_resume)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    int point = behavior_function_builder_source_point(
        builder,
        (behavior_source_point){ .occurrence = 1, .kind = BEHAVIOR_SOURCE_STATEMENT }
    );
    int watch = behavior_function_builder_source_watch(
        builder,
        (behavior_source_watch){
            .origin = { .occurrence = 2, .kind = BEHAVIOR_SOURCE_EXPRESSION },
            .initial = result }
    );
    int value = slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .output = value,
                                          .value = { .type = PIN_I32, .i = 37 } });
    emit((behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = point });
    emit((
        behavior_function_instruction
    ){ .op = BF_SOURCE_WATCH, .type = PIN_I32, .left = value, .target = watch });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = value });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    REQUIRE(!function->artifact->inspection_code);
    int observers = 0;
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        behavior_function_op op = function->instructions[pc].op;
        if (op == BF_SOURCE_POINT || op == BF_SOURCE_WATCH) {
            observers++;
            REQUIRE(!behavior_function_runtime_keeps(function, pc));
        }
    }
    REQUIRE(observers == 2);
    execution_cost_summary costs = behavior_function_costs(function);
    REQUIRE(costs.complete && !costs.proven && !costs.possible);
    pin_value output = result;
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(run(NULL, 0, NULL, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(output.i == 37);
        REQUIRE(!function->artifact->inspection_code);
    }
    for (int backend = 0; backend < 2; backend++) {
        source_pauses = source_captures = 0;
        context.source_pause = source_pause;
        context.source_capture = source_capture;
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_PAUSED_RESULT);
        REQUIRE(source_pauses == 1 && source_captures == 0);
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
        REQUIRE(source_pauses == 1 && source_captures == 1);
    }
#ifndef __EMSCRIPTEN__
    void *inspection = function->artifact->inspection_code;
    REQUIRE(inspection);
    REQUIRE(behavior_function_compile_inspection(function));
    REQUIRE(function->artifact->inspection_code == inspection);
#endif
    /* Detaching after a pause preserves the canonical continuation's state. */
    REQUIRE(behavior_function_run(function, &context, true) == FLOW_PAUSED_RESULT);
    context.source_pause = NULL;
    context.source_capture = NULL;
    REQUIRE(behavior_function_run(function, &context, true) == FLOW_OK);
    REQUIRE(source_captures == 1);
    return true;
}

TEST_CASE(execution_branches_state_and_host_calls)
{
    builder = behavior_function_builder_new();
    pin_value argument = { .type = PIN_F64 }, result = { .type = PIN_F64 };
    pin_value initial = { .type = PIN_F64, .d = 10 };
    behavior_function_builder_signature(builder, &argument, 1, &result, 1, &initial, 1);
    const node_host_call host = { .invoke = scale };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int capability = behavior_function_builder_host(builder, &host, &access, &argument, 1, &result, 1);
    int input = slot(PIN_F64), state = slot(PIN_F64), zero = slot(PIN_F64);
    int negative = slot(PIN_BOOL), sum = slot(PIN_F64);
    int pointer = address_slot(), scaled = slot(PIN_F64);
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_F64, .output = input, .value = argument });
    emit((behavior_function_instruction){ .op = BF_VARIABLE,
                                          .type = PIN_F64,
                                          .output = state });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_F64,
                                          .output = zero,
                                          .value = { .type = PIN_F64 } });
    emit((
        behavior_function_instruction
    ){ .op = BF_LT, .type = PIN_F64, .output = negative, .left = input, .right = zero });
    int branch = emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                       .left = negative });
    emit((
        behavior_function_instruction
    ){ .op = BF_ADD, .type = PIN_F64, .output = sum, .left = state, .right = input });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_first(builder, capability) });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_F64,
                                          .left = sum,
                                          .right = pointer,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_results(builder, capability) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_F64,
                                          .output = scaled,
                                          .left = pointer,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_STORE, .type = PIN_F64, .left = sum });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = scaled });
    int jump = emit((behavior_function_instruction){ .op = BF_JUMP });
    int unchanged = emit((behavior_function_instruction){ .op = BF_RESULT,
                                                          .type = PIN_F64,
                                                          .left = state });
    int end = emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(behavior_function_builder_patch_target(builder, branch, unchanged));
    REQUIRE(behavior_function_builder_patch_target(builder, jump, end));
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
#ifndef __EMSCRIPTEN__
    REQUIRE(behavior_function_entry(function) != NULL);
#endif
    for (int backend = 0; backend < 2; backend++) {
        pin_value variable = initial, output = result;
        argument.d = 3;
        host_calls = 0;
        REQUIRE(run(&argument, 1, &variable, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(variable.d == 13 && output.d == 26 && host_calls == 1);
        argument.d = -4;
        REQUIRE(run(&argument, 1, &variable, &output, backend == 1, 100) == FLOW_OK);
        if (variable.d != 13 || output.d != 13 || host_calls != 1) {
            fprintf(
                stderr,
                "branch backend=%s state=%g result=%g host_calls=%d\n",
                backend ? "native" : "VM",
                variable.d,
                output.d,
                host_calls
            );
        }
        REQUIRE(variable.d == 13 && output.d == 13 && host_calls == 1);
        argument.d = 2;
        REQUIRE(run(&argument, 1, &variable, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(variable.d == 15 && output.d == 30 && host_calls == 2);
    }
    return true;
}

static int running_seen;
static const char *observe_running(void *, const pin_value *, pin_value *results)
{
    running_seen = invocation_heap.running_calls;
    results[0].d = 1;
    return NULL;
}
/* A run with no authored calls still counts as running, so a run a native
 * reenters from it is nested and cannot collect the outer run's values. */
TEST_CASE(execution_runs_without_authored_calls_count_as_running)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_F64 };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    const node_host_call host = { .invoke = observe_running };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int capability = behavior_function_builder_host(builder, &host, &access, NULL, 0, &result, 1);
    int pointer = address_slot(), value = slot(PIN_F64);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_results(builder, capability) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_F64,
                                          .output = value,
                                          .left = pointer,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = value });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function) && !function->calls);
    for (int backend = 0; backend < 2; backend++) {
        pin_value output = result;
        running_seen = -1;
        REQUIRE(run(NULL, 0, NULL, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(running_seen == 1 && invocation_heap.running_calls == 0 && output.d == 1);
    }
    return true;
}

static bool reentered_native;
static size_t reentered_allocated;
/* Runs `reentered`, which has authored calls, on the invocation heap from
 * inside a native, as a host that calls back into script does. */
static const char *reenter(void *, const pin_value *arguments, pin_value *results)
{
    behavior_function_activation inner_activation = { 0 };
    bool resumable = behavior_function_resumable(reentered);
    if (resumable) {
        behavior_function_activation_init(reentered, &inner_activation);
    }
    arena inner_storage = { 0 };
    pin_value result = { .type = PIN_I32 };
    behavior_function_context inner = { .activation = resumable ? &inner_activation : NULL,
                                        .results = &result,
                                        .managed_heap = &invocation_heap,
                                        .budget = 100 };
    behavior_function_bind_storage(&inner, &inner_storage);
    assert(behavior_function_bind(reentered, &inner, NULL, 0));
    int status = behavior_function_run(reentered, &inner, reentered_native);
    reentered_allocated = invocation_heap.storage.allocated;
    behavior_function_release_host(&inner);
    behavior_function_activation_free(&inner_activation);
    arena_free(&inner_storage);
    results[0].d = status == FLOW_OK ? result.i : -1;
    /* The outer run's array, read after the nested run returns. */
    const pin_array *held = arguments ? arguments[0].ptr : NULL;
    for (int i = 0; held && i < held->count; i++) {
        results[0].d += pin_array_get(held, i).i * (i + 1) * 100;
    }
    return NULL;
}
/* A run that a native reenters from a call-free run reaches a safepoint whose
 * heap is due for collection, but the outer run's array lives only in its
 * suspended C frame: the nested run leaves collection to the outer one, and
 * the array survives. */
TEST_CASE(execution_reentered_runs_keep_the_outer_runs_values)
{
    pin_value number = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, &number, 1, NULL, 0);
    int seven = slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .output = seven,
                                          .value = { .type = PIN_I32, .i = 7 } });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = seven });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, &number, 1, NULL, 0);
    int call = behavior_function_builder_call(builder, callee, NULL, 0, &number, 1);
    int called = address_slot(), value = slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = called,
        .left = behavior_function_builder_call_results(builder, call) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_I32,
                                          .output = value,
                                          .left = called,
                                          .offset = offsetof(pin_value, i) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = value });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    reentered = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    pin_value shape = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY };
    pin_value total = { .type = PIN_F64 };
    behavior_function_builder_signature(builder, &shape, 1, &total, 1, NULL, 0);
    const node_host_call host = { .invoke = reenter };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int capability = behavior_function_builder_host(builder, &host, &access, &shape, 1, &total, 1);
    int held = address_slot(), header = address_slot(),
        pointer = address_slot(), inner = slot(PIN_F64);
    behavior_function_builder_root(builder, BEHAVIOR_BANK_SLOT, held, shape);
    emit((behavior_function_instruction){ .op = BF_ARRAY,
                                          .type = BF_POINTER_TYPE,
                                          .target = BF_ARGUMENT,
                                          .left = 0,
                                          .output = held,
                                          .value = shape });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .left = behavior_function_builder_host_first(builder, capability),
        .output = header });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = BF_POINTER_TYPE,
                                          .left = held,
                                          .right = header,
                                          .offset = offsetof(pin_value, ptr) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_results(builder, capability) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_F64,
                                          .output = inner,
                                          .left = pointer,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = inner });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(!function->calls && reentered->calls);
    REQUIRE(behavior_function_compile(callee) && behavior_function_compile(reentered));
    REQUIRE(behavior_function_compile(function));
    static const int items[] = { 11, 12, 13 };
    pin_value argument = shape;
    argument.ptr = pin_array_allocate(&managed, PIN_I32, NULL, 3);
    for (int i = 0; i < 3; i++) {
        ((pin_array *)argument.ptr)->items[i] = (pin_value){ .type = PIN_I32,
                                                             .i = items[i] };
    }
    for (int backend = 0; backend < 2; backend++) {
        /* Alone, the reentered run collects the heap at its call. */
        release_invocation();
        behavior_managed_heap_init(&invocation_heap);
        invocation_heap.call_collection_bytes = 1;
        REQUIRE(pin_array_allocate(&invocation_heap.storage, PIN_I32, NULL, 64));
        reentered_native = backend == 1;
        pin_value alone = { 0 };
        reenter(NULL, NULL, &alone);
        REQUIRE(alone.d == 7 && !invocation_heap.storage.allocated && !reentered_allocated);
        /* Inside the outer run, the same heap is due but nothing collects. */
        release_invocation();
        behavior_managed_heap_init(&invocation_heap);
        invocation_heap.call_collection_bytes = 1;
        pin_value output = total;
        int status = invoke(&argument, 1, NULL, &output, backend == 1, 100, &invocation_heap);
        REQUIRE(status == FLOW_OK && reentered_allocated);
        REQUIRE(output.d == 7 + 100 * (11 + 2 * 12 + 3 * 13));
    }
    return true;
}

TEST_CASE(execution_finished_definition_owns_producer_strings)
{
    char text[] = { 'p', 0, 'x', (char)0xff, 0 };
    const char expected[] = { 'p', 0, 'x', (char)0xff };
    char type_name[] = "fixture.text", origin[] = "fixture.chibi";
    builder = behavior_function_builder_new();
    pin_string descriptor = { .bytes = text, .length = sizeof text - 1 };
    pin_value initial = { .type = PIN_STR, .text = &descriptor, .type_name = type_name };
    behavior_function_builder_signature(builder, NULL, 0, &initial, 1, NULL, 0);
    int source = behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .asset_name = origin, .parent = -1, .line = 3, .column = 2 }
    );
    int output = slot(PIN_STR);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_STR,
                                          .output = output,
                                          .value = initial,
                                          .source = source });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_STR, .left = output });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    behavior_function_builder_free(builder);
    builder = NULL;
    memset(text, 'X', sizeof text);
    memset(type_name, 'X', sizeof type_name - 1);
    memset(origin, 'X', sizeof origin - 1);
    int count = 0;
    const pin_value *defaults = behavior_function_results(function, &count);
    REQUIRE(count == 1 && defaults[0].type == PIN_STR);
    REQUIRE(
        pin_text(defaults[0]).bytes != text &&
        pin_text(defaults[0]).length == sizeof expected &&
        !memcmp(pin_text(defaults[0]).bytes, expected, sizeof expected)
    );
    REQUIRE(defaults[0].type_name != type_name && !strcmp(defaults[0].type_name, "fixture.text"));
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value result = defaults[0];
        REQUIRE(run(NULL, 0, NULL, &result, backend == 1, 100) == FLOW_OK);
        REQUIRE(
            pin_text(result).length == sizeof expected &&
            !memcmp(pin_text(result).bytes, expected, sizeof expected)
        );
    }
    return true;
}

TEST_CASE(execution_verifier_rejects_invalid_lowering)
{
    const behavior_function_instruction invalid[] = {
        { .op = BF_CONSTANT, .type = PIN_I32, .output = 99, .value = { .type = PIN_I32 } },
        { .op = BF_ADD, .type = PIN_I32, .left = 99 },
        { .op = BF_BRANCH, .left = 1, .target = 99 },
        { .op = BF_HOST_CALL, .left = BEHAVIOR_FUNCTION_HOST_DATA, .target = 0 },
        { .op = BF_CONSTANT, .type = PIN_I32, .value = { .type = PIN_BOOL } },
        { .op = BF_ADD, .type = PIN_I32, .left = 1 },
        { .op = BF_CONSTANT, .type = PIN_I32, .output = 1, .value = { .type = PIN_I32 } },
        { .op = BF_STORE, .type = PIN_I32, .target = 0 },
        { .op = BF_ARGUMENT, .type = PIN_I32, .left = 0 },
    };
    for (int i = 0; i < 9; i++) {
        cleanup();
        builder = behavior_function_builder_new();
        slot(PIN_I32);
        slot(PIN_BOOL);
        emit(invalid[i]);
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(function == NULL);
        REQUIRE(error[0] != '\0');
    }
    cleanup();
    builder = behavior_function_builder_new();
    char error[256];
    REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
    REQUIRE(strstr(error, "END") != NULL);
    return true;
}

TEST_CASE(execution_budget_limits_checked_back_edges)
{
    builder = behavior_function_builder_new();
    int check = emit((behavior_function_instruction){ .op = BF_CHECK, .node_id = 17 });
    emit((behavior_function_instruction){ .op = BF_JUMP, .target = check });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(run(NULL, 0, NULL, NULL, backend == 1, 3) == FLOW_ERR_STEP_BUDGET);
        REQUIRE(context.failure == BEHAVIOR_FUNCTION_BUDGET);
        REQUIRE(context.node_id == 17 && context.error_steps == 3);
    }
    return true;
}

TEST_CASE(execution_linked_function_returns_checked_value)
{
    pin_value number = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &number, 1, &number, 1, NULL, 0);
    int input = slot(PIN_I32), two = slot(PIN_I32), product = slot(PIN_I32);
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = input, .value = number });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .output = two,
                                          .value = { .type = PIN_I32, .i = 2 } });
    emit((
        behavior_function_instruction
    ){ .op = BF_MUL, .type = PIN_I32, .output = product, .left = input, .right = two });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = product });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    execution_cost_summary costs = behavior_function_costs(function);
    REQUIRE(costs.complete && !costs.proven && !costs.possible);
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, &number, 1, NULL, 0);
    pin_value argument = { .type = PIN_I32, .i = 21 };
    int call = behavior_function_builder_call(builder, callee, &argument, 1, &number, 1);
    int pointer = address_slot(), output = slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_call_results(builder, call) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_I32,
                                          .output = output,
                                          .left = pointer,
                                          .offset = offsetof(pin_value, i) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = output });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE && !costs.possible);
    REQUIRE(behavior_function_compile(callee));
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value result = number;
        REQUIRE(run(NULL, 0, NULL, &result, backend == 1, 100) == FLOW_OK);
        REQUIRE(result.type == PIN_I32 && result.i == 42);
    }
    return true;
}

static const char *temporary_helper_message;
static const char *temporary_helper_error(arena *storage, const pin_value *, int, pin_value *)
{
    const char message[] = "temporary helper failure";
    char *copy = arena_alloc(storage, sizeof message);
    memcpy(copy, message, sizeof message);
    temporary_helper_message = copy;
    return copy;
}
static void copy_schema_header(int source, int destination)
{
    const size_t offsets[] = { offsetof(pin_value, d),
                               offsetof(pin_value, type_name),
                               offsetof(pin_value, record),
                               offsetof(pin_value, schema) };
    for (int i = 0; i < 4; i++) {
        int field = address_slot();
        emit((behavior_function_instruction){ .op = BF_READ,
                                              .type = BF_POINTER_TYPE,
                                              .left = source,
                                              .offset = offsets[i],
                                              .output = field });
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = BF_POINTER_TYPE,
                                              .left = field,
                                              .right = destination,
                                              .offset = offsets[i] });
    }
}
TEST_CASE(execution_helper_returns_keep_native_schema_after_input_retirement)
{
    static type_field field;
    static type_desc type;
    static const type_desc *types[] = { &type };
    static type_catalog_pack pack = { .types = types, .type_count = 1 };
    field = c_text_field;
    type = c_text_type;
    type.name = "fixture.return_private";
    type.fields = &field;
    type_catalog_set_project(&pack);
    return_types_active = true;
    pin_value value = { .type = PIN_STRUCT, .type_name = type.name };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &value, 1, &value, 1, NULL, 0);
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_construct_call,
        &EXECUTION_assignment_access,
        &value,
        1,
        &value,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_construct_call) |
                  EXECUTION_INTRINSIC_IN_RESULT;
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    int arguments = address_slot(), input = address_slot(),
        returned = address_slot(), results = address_slot();
    emit((behavior_function_instruction){
        .op = BF_CONTEXT,
        .type = BF_POINTER_TYPE,
        .offset = offsetof(behavior_function_context, arguments),
        .output = arguments });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .left = behavior_function_builder_host_first(builder, host),
        .output = input });
    copy_schema_header(arguments, input);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .left = behavior_function_builder_host_results(builder, host),
        .output = returned });
    emit((behavior_function_instruction){
        .op = BF_CONTEXT,
        .type = BF_POINTER_TYPE,
        .offset = offsetof(behavior_function_context, results),
        .output = results });
    copy_schema_header(returned, results);
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(function->builds_results);
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &value, 1, NULL, 0, NULL, 0);
    int call = behavior_function_builder_call(builder, callee, &value, 1, &value, 1);
    int output = behavior_function_builder_call_results(builder, call);
    arguments = address_slot();
    input = address_slot();
    emit((behavior_function_instruction){
        .op = BF_CONTEXT,
        .type = BF_POINTER_TYPE,
        .offset = offsetof(behavior_function_context, arguments),
        .output = arguments });
    emit((behavior_function_instruction){ .op = BF_HOST_SLOT,
                                          .type = BF_POINTER_TYPE,
                                          .left = output - 1,
                                          .output = input });
    copy_schema_header(arguments, input);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(callee) && behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        release_invocation();
        field = c_text_field;
        type.size = sizeof(fixture_c_text);
        if (!return_types_active) {
            type_catalog_set_project(&pack);
            return_types_active = true;
        }
        char text[] = "retained";
        fixture_c_text source = { .text = text };
        pin_value argument = value;
        argument.ptr = &source;
        argument.schema = pin_schema_create(&managed, &type, (type_resolver){ 0 });
        REQUIRE(argument.schema);
        behavior_function_activation_init(function, &activation);
        context = (behavior_function_context){ .activation = &activation,
                                               .arguments = &argument,
                                               .argument_count = 1,
                                               .budget = 1000 };
        behavior_function_bind_storage(&context, &storage);
        REQUIRE(behavior_function_bind(function, &context, NULL, 0));
        behavior_function_prepare_host(function, &context);
        REQUIRE(behavior_function_run(function, &context, backend != 0) == FLOW_OK);
        pin_value *result = &context.data->values[output];
        REQUIRE(result->schema && arena_contains(&storage, result->schema));
        REQUIRE(result->ptr && arena_contains(&storage, result->ptr));
        type_catalog_set_project(NULL);
        return_types_active = false;
        text[0] = 'X';
        field.offset = SIZE_MAX;
        type.size = 1;
        arena_free(&managed);
        pin_value retained;
        REQUIRE(!pin_value_snapshot_to_arena(&managed, &retained, result));
        release_invocation();
        const type_desc *layout = pin_value_type(&retained);
        REQUIRE(layout && layout->size == sizeof(fixture_c_text));
        pin_value read;
        REQUIRE(!pin_read_field(&managed, retained.ptr, &layout->fields[0], &read));
        REQUIRE(read.text && read.text->length == 8 && !memcmp(read.text->bytes, "retained", 8));
        arena_free(&managed);
    }
    return true;
}
TEST_CASE(execution_owned_intrinsics_validate_action_state_targets)
{
    for (int variant = 0; variant < 5; variant++) {
        cleanup();
        builder = behavior_function_builder_new();
        const node_host_call *call = variant == 4 ? &EXECUTION_retrigger_action_call
                                                  : &EXECUTION_state_demand_call;
        pin_value arguments[] = {
            { .type = BF_POINTER_TYPE },
            { .type = PIN_U64, .u64 = 995001 },
            variant == 4 ? (pin_value){ .type = PIN_F64 }
                         : (pin_value){ .type = PIN_I32, .i = variant == 2 ? -1 : 0 }
        };
        pin_value result = { .type = variant == 4 ? PIN_BOOL : PIN_I32 };
        int host = behavior_function_builder_host(
            builder,
            call,
            &EXECUTION_assignment_access,
            arguments,
            3,
            &result,
            1
        );
        REQUIRE(behavior_function_builder_kind(
            builder,
            BEHAVIOR_BANK_DATA,
            behavior_function_builder_host_first(builder, host),
            BEHAVIOR_ADDRESS
        ));
        uint64_t id = execution_intrinsic_id(call);
        REQUIRE(
            behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(function->host_calls[host].call != call);
        prepared_plan = execution_module_plan_new();
        prepared_plan->id = 995002;
        prepared_plan->valid = true;
        apush(
            prepared_plan->functions,
            ((execution_module_function){ .id = 995003,
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .body = function })
        );
        prepared_plan->function_count = 1;
        function = NULL;
        if (variant) {
            execution_module_action action = {
                .id = 995001,
                .state_kind = variant == 1 ? NATIVE_STATE_NONE : NATIVE_STATE_ONCE
            };
            if (variant != 1) {
                action.outcomes = xmalloc(sizeof *action.outcomes);
                action.outcomes[0] = UINT64_C(995004);
                action.outcome_count = 1;
            }
            apush(prepared_plan->actions, action);
            prepared_plan->action_count = 1;
        }
        char error[256];
        prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
        size_t length = 0;
        if (variant == 3) {
            REQUIRE(prepared_module);
            REQUIRE(
                execution_prepared_write(prepared_module, &prepared_bytes, &length, error, sizeof error)
            );
            execution_module_free(prepared_module);
            prepared_module = execution_prepared_read(
                prepared_bytes,
                length,
                NULL,
                BEHAVIOR_BYTECODE,
                error,
                sizeof error
            );
            REQUIRE(prepared_module);
            continue;
        }
        REQUIRE(!prepared_module && prepared_plan);
        REQUIRE(strstr(error, "action state contract"));
        const execution_module forged = { .plan = *prepared_plan,
                                          .backend = BEHAVIOR_BYTECODE,
                                          .references = 1 };
        REQUIRE(execution_prepared_write(&forged, &prepared_bytes, &length, error, sizeof error));
        prepared_module = execution_prepared_read(prepared_bytes, length, NULL, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(!prepared_module && strstr(error, "action state contract"));
    }
    return true;
}
TEST_CASE(execution_raw_scalar_helper_preserves_owned_failure_text)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    const node_host_call host = { .value = temporary_helper_error };
    const node_access access = { .flags = NODE_EFFECT_PURE };
    int capability = behavior_function_builder_host(builder, &host, &access, NULL, 0, &result, 1);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    int call = behavior_function_builder_call(builder, callee, NULL, 0, &result, 1);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(callee));
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        release_invocation();
        behavior_function_activation_init(function, &activation);
        context = (behavior_function_context){ .activation = &activation,
                                               .budget = 100 };
        behavior_function_bind_storage(&context, &storage);
        REQUIRE(behavior_function_bind(function, &context, NULL, 0));
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_ERR_INVALID_GRAPH);
        REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
        REQUIRE(context.error_message && context.error_message != temporary_helper_message);
        REQUIRE(!strcmp(context.error_message, "temporary helper failure"));
    }
    return true;
}

static int rebound_values[2];
static const char *rebound_host(void *service, const pin_value *, pin_value *results)
{
    if (results[0].type != PIN_I32 || results[0].i != 17) {
        return "helper retained its previous host result";
    }
    assert(host_calls < 2);
    rebound_values[host_calls++] = *(const int *)service;
    results[0].i = *(const int *)service;
    return NULL;
}

static const char *reused_box(void *service, const pin_value *, pin_value *results)
{
    fixture_box *box = results[0].ptr;
    if (!box || box->value) {
        return "host buffer retained its previous payload";
    }
    box->value = *(const int *)service;
    return NULL;
}

TEST_CASE(execution_reused_host_storage_clears_context_and_rebinds_fixed_buffers)
{
    builder = behavior_function_builder_new();
    const pin_value result = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    const node_host_call host = { .service = "fixture.rebound", .invoke = reused_box };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int capability = behavior_function_builder_host(builder, &host, &access, NULL, 0, &result, 1);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    int service = 31;
    apush(log_services, ((service_binding){ "fixture.rebound", &service }));
    for (int backend = 0; backend < 2; backend++) {
        release_invocation();
        context.budget = 100;
        REQUIRE(behavior_function_bind(function, &context, log_services, 42));
        /* The run copies the native result out of the fixed buffer. */
        context.value_storage = &managed;
        behavior_function_prepare_host(function, &context);
        behavior_data_storage *data = context.data;
        fixture_box *box = data->values[0].ptr;
        size_t bytes = behavior_function_host_storage_bytes(&context);
        uint64_t allocations = behavior_function_data_allocations();
        REQUIRE(bytes >= sizeof *data + sizeof *box);
        for (int invocation = 0; invocation < 2; invocation++) {
            service = invocation ? 73 : 31;
            REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
            REQUIRE(box->value == service);
            behavior_function_reset_host(&context);
            REQUIRE(context.data == data && context.owned_data);
            REQUIRE(
                !context.services && !context.owner && !context.source_function &&
                !context.arguments && !context.results && !context.value_storage
            );
            REQUIRE(data->values[0].ptr == box && !box->value && !data->calls[0].service);
            REQUIRE(behavior_function_host_storage_bytes(&context) == bytes);
            context.budget = 100;
            REQUIRE(behavior_function_bind(function, &context, log_services, 43));
            context.value_storage = &managed;
            behavior_function_prepare_host(function, &context);
            REQUIRE(data->calls[0].service == &service);
        }
        REQUIRE(behavior_function_data_allocations() == allocations);
    }
    return true;
}

TEST_CASE(execution_reused_helper_resets_host_bank_and_rebinds_service)
{
    builder = behavior_function_builder_new();
    pin_value initial = { .type = PIN_I32, .i = 17 };
    const node_host_call host = { .service = "fixture.rebound", .invoke = rebound_host };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int capability = behavior_function_builder_host(builder, &host, &access, NULL, 0, &initial, 1);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    int call = behavior_function_builder_call(builder, callee, NULL, 0, NULL, 0);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){ .op = BF_SUSPEND });
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(callee));
    execution_cost_summary costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE);
    REQUIRE(costs.possible == (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE));
    REQUIRE(costs.possible_sites[0].function == callee);
    REQUIRE(strstr(costs.possible_sites[0].reason, "Native call"));
    REQUIRE(behavior_function_compile(function));
    int first = 31, second = 73;
    apush(log_services, ((service_binding){ "fixture.rebound", &first }));
    for (int backend = 0; backend < 2; backend++) {
        release_invocation();
        behavior_function_activation_init(function, &activation);
        context = (behavior_function_context){ .activation = &activation,
                                               .budget = 100 };
        behavior_function_bind_storage(&context, &storage);
        log_services[0].value = &first;
        REQUIRE(behavior_function_bind(function, &context, log_services, 0));
        host_calls = 0;
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_SUSPENDED_RESULT);
        REQUIRE(host_calls == 1 && rebound_values[0] == first);
        /* The completed helper remains available while its parent is suspended.
         * Reusing it must initialize its host values and select current services. */
        log_services[0].value = &second;
        behavior_function_rebind_services(function, &context, log_services);
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
        REQUIRE(host_calls == 2 && rebound_values[1] == second);
    }
    return true;
}

TEST_CASE(execution_costs_verify_intrinsics_and_follow_recursive_calls)
{
    builder = behavior_function_builder_new();
    pin_value values[] = {
        { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY },
        { .type = PIN_I32, .i = 37 },
    };
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_construct_call,
        &EXECUTION_value_access,
        values,
        2,
        values,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_construct_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    execution_cost_summary costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY));
    REQUIRE(!costs.possible);
    REQUIRE(costs.proven_sites[1].capability == id);
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    int call = behavior_function_builder_call(builder, callee, NULL, 0, NULL, 0);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && !costs.possible);
    REQUIRE(costs.proven_sites[1].function == callee);
    /* Recursion revisits a definition without unbounded traversal. */
    function->calls[call].function = function;
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE && !costs.possible);
    function->calls[call].function = callee;
    /* A matching stable ID alone must not lend a native callback the verified
     * intrinsic's cost contract. */
    const node_host_call changed = { .invoke = scale };
    callee->host_calls[host].call = &changed;
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE);
    REQUIRE(costs.possible == (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE));
    callee->host_calls[host].call = &EXECUTION_construct_call;
    cleanup();
    builder = behavior_function_builder_new();
    pin_value tag = { .type = PIN_U64, .u64 = PIN_UNION_CANCELLED_TAG };
    pin_value result = { .type = PIN_UNION };
    host = behavior_function_builder_host(
        builder,
        &EXECUTION_union_make_call,
        &EXECUTION_value_access,
        &tag,
        1,
        &result,
        1
    );
    id = execution_intrinsic_id(&EXECUTION_union_make_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && !costs.proven && !costs.possible);
    const node_host_call *patterns[] = { &EXECUTION_union_test_call,
                                         &EXECUTION_union_get_call };
    for (int j = 0; j < 2; j++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value args[] = { { .type = PIN_UNION },
                             tag,
                             j ? (pin_value){ .type = PIN_I32 } : tag,
                             { .type = PIN_BOOL, .b = true },
                             tag };
        pin_value output = { .type = j ? PIN_UNION : PIN_BOOL };
        host = behavior_function_builder_host(
            builder,
            patterns[j],
            &EXECUTION_value_access,
            args,
            j ? 5 : 3,
            &output,
            1
        );
        id = execution_intrinsic_id(patterns[j]);
        REQUIRE(
            behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        costs = behavior_function_costs(function);
        REQUIRE(costs.complete && !costs.proven && !costs.possible);
        execution_module_function entry = { .id = 1,
                                            .kind = EXECUTION_FUNCTION_SYSTEM,
                                            .body = function };
        execution_module module = { .plan = { .functions = &entry,
                                              .function_count = 1 } };
        REQUIRE(execution_module_build_effects(&module));
        execution_effect_summary effects = execution_module_effects(&module);
        REQUIRE(effects.declared && effects.parallel_safe && effects.deterministic);
        REQUIRE(!effects.structural && !effects.host_sink && !effects.world_read);
        REQUIRE(!effects.read_count && !effects.write_count && !module.effect_ids);
    }
    cleanup();
    builder = behavior_function_builder_new();
    pin_value empty = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    host = behavior_function_builder_host(
        builder,
        &EXECUTION_construct_call,
        &EXECUTION_value_access,
        &empty,
        1,
        &empty,
        1
    );
    id = execution_intrinsic_id(&EXECUTION_construct_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE);
    REQUIRE(costs.possible == EXECUTION_COST_COPY);
    return true;
}

TEST_CASE(execution_verifier_rejects_invalid_saved_aggregate_roots)
{
    for (int invalid = 0; invalid < 7; invalid++) {
        cleanup();
        builder = behavior_function_builder_new();
        int pointer = address_slot();
        behavior_function_builder_root(
            builder,
            BEHAVIOR_BANK_SLOT,
            pointer,
            (pin_value){ .type = PIN_TASK, .container = PIN_CONTAINER_ARRAY }
        );
        behavior_function *draft = (behavior_function *)
            behavior_function_builder_definition(builder);
        behavior_value_root *root = &draft->value_roots[0];
        switch (invalid) {
            case 0: root->bank = BEHAVIOR_BANK_ARGUMENT; break;
            case 1: root->index = acount(draft->slots); break;
            case 2: root->index = -1; break;
            case 3: root->shape.container = PIN_CONTAINER_SINGLE; break;
            case 4: root->shape.ptr = (void *)(uintptr_t)1; break;
            case 5: draft->slot_kinds[pointer] = BEHAVIOR_VALUE; break;
            case 6: {
                behavior_value_root duplicate = *root;
                apush(draft->value_roots, duplicate);
                break;
            }
        }
        emit((behavior_function_instruction){ .op = BF_END });
        char error[192];
        REQUIRE(!behavior_function_builder_finish(builder, error, sizeof error));
        REQUIRE(error[0]);
    }
    return true;
}

TEST_CASE(execution_costs_include_managed_scalar_result_and_state_escape)
{
    const pin_type types[] = { PIN_I32, PIN_STR, PIN_NAME, PIN_UNION, PIN_TASK };
    for (int mode = 0; mode < 10; mode++) {
        int i = mode % 5;
        bool persistent = mode >= 5;
        cleanup();
        builder = behavior_function_builder_new();
        pin_value initial = { .type = types[i] };
        behavior_function_builder_signature(builder, &initial, 1, &initial, !persistent, &initial, persistent);
        int value = slot(types[i]);
        emit((
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = types[i], .value = initial, .output = value });
        emit((behavior_function_instruction){ .op = persistent ? BF_STORE : BF_RESULT,
                                              .type = types[i],
                                              .left = value });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        execution_cost_summary costs = behavior_function_costs(function);
        REQUIRE(costs.complete && !costs.possible);
        REQUIRE(costs.proven == (i ? EXECUTION_COST_ESCAPE : 0));
    }
    return true;
}

typedef struct
{
    pin_value expected;
    int matches, others;
} visited_aggregate;
static void count_aggregate(void *user, const pin_value *value)
{
    visited_aggregate *seen = user;
    if (value->container != PIN_CONTAINER_ARRAY) {
        return;
    }
    if (value->type == seen->expected.type && value->ptr == seen->expected.ptr) {
        seen->matches++;
    } else {
        seen->others++;
    }
}
TEST_CASE(execution_saved_aggregate_roots_keep_only_live_shapes)
{
    for (int union_array = 0; union_array < 2; union_array++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value shape = { .type = union_array ? PIN_UNION : PIN_TASK,
                            .container = PIN_CONTAINER_ARRAY };
        pin_value arguments[] = { shape, shape };
        pin_value results[] = { { .type = BF_POINTER_TYPE },
                                { .type = BF_POINTER_TYPE } };
        behavior_function_builder_signature(builder, arguments, 2, results, 2, NULL, 0);
        int input[2], locals[2];
        for (int i = 0; i < 2; i++) {
            input[i] = address_slot();
            locals[i] = behavior_function_builder_local(builder, results[i]);
            REQUIRE(
                behavior_function_builder_kind(builder, BEHAVIOR_BANK_LOCAL, locals[i], BEHAVIOR_ADDRESS)
            );
            REQUIRE(
                behavior_function_builder_kind(builder, BEHAVIOR_BANK_RESULT, i, BEHAVIOR_ADDRESS)
            );
            behavior_function_builder_root(builder, BEHAVIOR_BANK_SLOT, input[i], shape);
            behavior_function_builder_root(builder, BEHAVIOR_BANK_LOCAL, locals[i], shape);
            emit((behavior_function_instruction){ .op = BF_ARRAY,
                                                  .type = BF_POINTER_TYPE,
                                                  .target = BF_ARGUMENT,
                                                  .left = i,
                                                  .output = input[i],
                                                  .value = shape });
            emit((behavior_function_instruction){ .op = BF_SAVE,
                                                  .type = BF_POINTER_TYPE,
                                                  .target = locals[i],
                                                  .left = input[i] });
        }
        emit((behavior_function_instruction){ .op = BF_SUSPEND });
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = BF_POINTER_TYPE,
                                              .left = input[0] });
        int restored = address_slot();
        emit((behavior_function_instruction){ .op = BF_LOCAL,
                                              .type = BF_POINTER_TYPE,
                                              .left = locals[0],
                                              .output = restored });
        emit((
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = BF_POINTER_TYPE, .left = restored, .target = 1 });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(acount(function->data_values) == 0);
        REQUIRE(acount(function->suspensions) == 1);
        REQUIRE(function->suspensions[0].root_count == 2);
        REQUIRE(behavior_function_compile(function));
        for (int i = 0; i < 2; i++) {
            arguments[i].ptr = pin_array_allocate(&managed, shape.type, NULL, 1);
            ((pin_array *)arguments[i].ptr)->items[0] = (pin_value){
                .type = shape.type
            };
        }
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(run(arguments, 2, NULL, results, backend == 1, 100) == FLOW_SUSPENDED_RESULT);
            /* Value arguments have independent managed shells. Capture the
             * actual bank while suspended; completion restores the C bank. */
            const pin_value *active_arguments = context.arguments;
            pin_value live = active_arguments[0];
            REQUIRE(active_arguments != arguments);
            REQUIRE(
                live.ptr != arguments[0].ptr && live.ptr != active_arguments[1].ptr
            );
            REQUIRE(arena_contains(&invocation_heap.storage, live.ptr));
            REQUIRE(((pin_array *)live.ptr)->count == 1);
            REQUIRE(pin_array_get(live.ptr, 0).type == shape.type);
            /* Remove the independent argument roots to exercise only saved
             * slot/local metadata, including rejection of the dead array. */
            context.arguments = NULL;
            context.argument_count = 0;
            visited_aggregate seen = { .expected = live };
            behavior_function_visit_values(function, &context, count_aggregate, &seen);
            REQUIRE(seen.matches == 2 && seen.others == 0);
            context.arguments = active_arguments;
            context.argument_count = 2;
            REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
            REQUIRE(context.arguments == arguments);
            /* These result banks deliberately expose raw saved addresses;
             * they do not publish owning language array values. */
            REQUIRE(results[0].ptr == live.ptr && results[1].ptr == live.ptr);
            for (int i = 0; i < 2; i++) {
                REQUIRE(((pin_array *)arguments[i].ptr)->count == 1);
                REQUIRE(pin_array_get(arguments[i].ptr, 0).type == shape.type);
            }
        }
    }
    return true;
}

TEST_CASE(execution_verifier_rejects_invalid_banks_and_metadata)
{
    const behavior_function_instruction invalid[] = {
        { .op = BF_STORE_VALUE },
        { .op = BF_STORE_HEADER, .left = 2, .right = 1 },
        { .op = BF_STORE_TYPE_NAME, .left = 2 },
        { .op = BF_RESULT_PRESENT, .left = 1 },
        { .op = BF_RESULT_TYPE_NAME, .left = 2 },
        { .op = BF_RESULT_ARRAY, .left = 2, .right = 2, .type = PIN_I32 },
        { .op = BF_STORAGE_NAME, .output = 2, .type = BF_POINTER_TYPE },
        { .op = BF_PRESENT, .output = 1, .type = PIN_BOOL, .target = 999 },
        { .op = BF_DIV, .type = PIN_I32, .target = 999 },
        { .op = BF_WATCH_RESET },
        { .op = BF_TRACE },
        { .op = BF_ENTER },
        { .op = BF_LEAVE },
        { .op = BF_RETRIGGER, .output = 1, .type = PIN_BOOL, .left = 3 },
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        cleanup();
        builder = behavior_function_builder_new();
        slot(PIN_I32);
        slot(PIN_BOOL);
        address_slot();
        slot(PIN_F64);
        emit(invalid[i]);
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
        REQUIRE(error[0] != '\0');
    }
    cleanup();
    builder = behavior_function_builder_new();
    emit((behavior_function_instruction){ .op = BF_JUMP, .target = 0 });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
    REQUIRE(error[0] != '\0');
    return true;
}

TEST_CASE(execution_verifier_rejects_signature_and_local_type_mismatches)
{
    const behavior_function_instruction invalid[] = {
        { .op = BF_ARGUMENT, .type = PIN_BOOL, .output = 1, .value = { .type = PIN_BOOL } },
        { .op = BF_LOCAL, .type = PIN_BOOL, .output = 1 },
        { .op = BF_SAVE, .type = PIN_BOOL, .left = 1 },
        { .op = BF_RESULT, .type = PIN_BOOL, .left = 1 },
        { .op = BF_STORE, .type = PIN_BOOL, .left = 1 },
    };
    pin_value number = { .type = PIN_I32 };
    for (int i = 0; i < 5; i++) {
        cleanup();
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, &number, 1, &number, 1, &number, 1);
        behavior_function_builder_local(builder, number);
        slot(PIN_I32);
        slot(PIN_BOOL);
        emit(invalid[i]);
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
        REQUIRE(error[0] != '\0');
    }
    cleanup();
    builder = behavior_function_builder_new();
    pin_value aggregate = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    behavior_function_builder_signature(builder, &aggregate, 1, NULL, 0, NULL, 0);
    slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                          .type = PIN_I32,
                                          .indirect = true,
                                          .offset = sizeof(fixture_box),
                                          .value = number });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
    REQUIRE(error[0] != '\0');
    return true;
}

TEST_CASE(execution_integer_bitwise_preserves_width_and_valid_shifts)
{
    const pin_type integers[] = { PIN_I8,  PIN_U8,  PIN_I16, PIN_U16,
                                  PIN_I32, PIN_U32, PIN_I64, PIN_U64 };
    const behavior_function_op operations[] = { BF_BIT_AND, BF_BIT_OR,
                                                BF_BIT_XOR, BF_BIT_NOT,
                                                BF_SHL,     BF_SHR };
    for (int t = 0; t < 8; t++) {
        cleanup();
        pin_type type = integers[t];
        unsigned width = (unsigned)behavior_scalar_size(type) * 8;
        uint64_t mask = UINT64_MAX >> (64 - width);
        uint64_t sign = UINT64_C(1) << (width - 1);
        pin_value arguments[2] = { { .type = type }, { .type = type } };
        pin_value results[6];
        for (int i = 0; i < 6; i++) {
            results[i] = (pin_value){ .type = type };
        }
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, arguments, 2, results, 6, NULL, 0);
        int left = slot(type), right = slot(type);
        emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                              .type = type,
                                              .output = left,
                                              .left = 0,
                                              .value = arguments[0] });
        emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                              .type = type,
                                              .output = right,
                                              .left = 1,
                                              .value = arguments[1] });
        for (int i = 0; i < 6; i++) {
            int output = slot(type);
            emit((behavior_function_instruction){ .op = operations[i],
                                                  .type = type,
                                                  .output = output,
                                                  .left = left,
                                                  .right = i == 3 ? -1 : right });
            emit((
                behavior_function_instruction
            ){ .op = BF_RESULT, .type = type, .left = output, .target = i });
        }
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(behavior_function_compile(function));
        const struct
        {
            uint64_t left, count, shl, signed_shr, unsigned_shr;
        } cases[] = {
            { mask - 7, 1, mask - 15, mask - 3, (mask >> 1) - 3 },
            { sign, 0, sign, sign, sign },
            { sign, width - 1, 0, mask, 1 },
            { sign, 1, 0, sign | (sign >> 1), sign >> 1 },
            { mask, width - 1, sign, mask, 1 },
        };
        for (int backend = 0; backend < 2; backend++) {
            for (int c = 0; c < 5; c++) {
                arguments[0] = behavior_scalar_from_bits(type, cases[c].left);
                arguments[1] = behavior_scalar_from_bits(type, cases[c].count);
                REQUIRE(run(arguments, 2, NULL, results, backend == 1, 100) == FLOW_OK);
                const uint64_t expected[] = {
                    cases[c].left & cases[c].count,
                    (cases[c].left | cases[c].count) & mask,
                    (cases[c].left ^ cases[c].count) & mask,
                    mask ^ cases[c].left,
                    cases[c].shl,
                    behavior_scalar_signed(type) ? cases[c].signed_shr
                                                 : cases[c].unsigned_shr,
                };
                for (int i = 0; i < 6; i++) {
                    uint64_t actual = behavior_scalar_bits(results[i]) & mask;
                    if (actual != expected[i]) {
                        fprintf(
                            stderr,
                            "bitwise type=%d backend=%d case=%d op=%d "
                            "actual=%llx expected=%llx\n",
                            type,
                            backend,
                            c,
                            operations[i],
                            (unsigned long long)actual,
                            (unsigned long long)expected[i]
                        );
                    }
                    REQUIRE(actual == expected[i]);
                }
            }
        }
    }
    return true;
}

/* A count at or past the width shifts every bit out, as in Odin: 0, or all
 * ones for a signed right shift of a negative value. */
TEST_CASE(execution_integer_shift_past_the_width_shifts_every_bit_out)
{
    const pin_type integers[] = { PIN_I8,  PIN_U8,  PIN_I16, PIN_U16,
                                  PIN_I32, PIN_U32, PIN_I64, PIN_U64 };
    for (int t = 0; t < 8; t++) {
        pin_type type = integers[t];
        unsigned width = (unsigned)behavior_scalar_size(type) * 8;
        const uint64_t counts[] = { width, width + 1, UINT64_MAX };
        for (int op = 0; op < 2; op++) {
            cleanup();
            pin_value arguments[2] = { { .type = type }, { .type = type } };
            pin_value result = { .type = type };
            builder = behavior_function_builder_new();
            behavior_function_builder_signature(builder, arguments, 2, &result, 1, NULL, 0);
            int left = slot(type), right = slot(type), output = slot(type);
            for (int a = 0; a < 2; a++) {
                emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                                      .type = type,
                                                      .output = a ? right : left,
                                                      .left = a,
                                                      .value = arguments[a] });
            }
            emit((behavior_function_instruction){ .op = op ? BF_SHR : BF_SHL,
                                                  .type = type,
                                                  .output = output,
                                                  .left = left,
                                                  .right = right });
            emit((behavior_function_instruction){ .op = BF_RESULT,
                                                  .type = type,
                                                  .left = output });
            emit((behavior_function_instruction){ .op = BF_END });
            REQUIRE(finish());
            REQUIRE(behavior_function_compile(function));
            for (int backend = 0; backend < 2; backend++) {
                for (int c = 0; c < 3; c++) {
                    /* One, then all ones: -1 for a signed type. */
                    for (int all = 0; all < 2; all++) {
                        arguments[0] = behavior_scalar_from_bits(type, all ? UINT64_MAX : 1);
                        arguments[1] = behavior_scalar_from_bits(type, counts[c]);
                        REQUIRE(run(arguments, 2, NULL, &result, backend == 1, 100) == FLOW_OK);
                        bool fill = op && all && behavior_scalar_signed(type);
                        REQUIRE(
                            behavior_scalar_bits(result) ==
                            behavior_scalar_bits(
                                behavior_scalar_from_bits(type, fill ? UINT64_MAX : 0)
                            )
                        );
                    }
                }
            }
        }
    }
    return true;
}

TEST_CASE(execution_bitwise_verifier_rejects_non_integer_and_mismatched_operands)
{
    const pin_type invalid[] = { PIN_F32, PIN_F64, PIN_BOOL, PIN_ENUM, PIN_HANDLE };
    for (int i = 0; i < 6; i++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_type type = i == 5 ? PIN_I32 : invalid[i];
        int left = slot(type), right = slot(i == 5 ? PIN_I64 : type),
            output = slot(type);
        emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                              .type = type,
                                              .output = left,
                                              .value = { .type = type } });
        emit((behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = i == 5 ? PIN_I64 : type,
            .output = right,
            .value = { .type = i == 5 ? PIN_I64 : type } });
        emit((
            behavior_function_instruction
        ){ .op = BF_BIT_AND, .type = type, .left = left, .right = right, .output = output });
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(function == NULL && strstr(error, "bitwise"));
    }
    return true;
}

/* Six tiny two-frame definitions, at most twelve instructions each. The
 * variable-count definition runs three counts; each constant-count definition
 * runs once, on bytecode and the compiled backend: twenty invocations total.
 * A shift never fails, so every definition takes the fast path and an unused
 * shift past the width lets both persistent stores complete. */
TEST_CASE(execution_fast_shift_past_the_width_keeps_both_stores)
{
    const int counts[] = { 1, 32, -1 };
    for (int op = 0; op < 2; op++) {
        for (int mode = 0; mode < 3; mode++) {
            cleanup();
            builder = behavior_function_builder_new();
            pin_value arguments[] = { { .type = PIN_I32, .i = 4 }, { .type = PIN_I32 } };
            pin_value variable = { .type = PIN_I32 };
            behavior_function_builder_signature(builder, arguments, 2, NULL, 0, &variable, 1);
            /* The fixture owns this mutable builder. Add the internal child
             * frame metadata without expanding the public builder interface. */
            behavior_function *draft = (behavior_function *)
                behavior_function_builder_definition(builder);
            apush(
                draft->frames,
                ((behavior_function_frame){ .source = { .parent = 0 }, .depth = 1, .stack = -1 })
            );
            int before = slot(PIN_I32), after = slot(PIN_I32),
                left = slot(PIN_I32), right = slot(PIN_I32), unused = slot(PIN_I32);
            emit((behavior_function_instruction){ .op = BF_CHECK });
            emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                                  .type = PIN_I32,
                                                  .output = before,
                                                  .value = { .type = PIN_I32, .i = 7 } });
            emit((
                behavior_function_instruction
            ){ .op = BF_STORE, .type = PIN_I32, .left = before, .target = 0 });
            emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                                  .type = PIN_I32,
                                                  .output = left,
                                                  .left = 0,
                                                  .value = variable });
            emit((behavior_function_instruction){
                .op = mode ? BF_CONSTANT : BF_ARGUMENT,
                .type = PIN_I32,
                .output = right,
                .left = 1,
                .value = { .type = PIN_I32, .i = mode == 2 ? 32 : 1 } });
            emit((behavior_function_instruction){ .op = BF_CHECK, .frame = 1 });
            emit((behavior_function_instruction){ .op = op ? BF_SHR : BF_SHL,
                                                  .type = PIN_I32,
                                                  .output = unused,
                                                  .left = left,
                                                  .right = right,
                                                  .frame = 1,
                                                  .node_id = 73,
                                                  .pin = 3 });
            emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                                  .type = PIN_I32,
                                                  .output = after,
                                                  .value = { .type = PIN_I32, .i = 9 } });
            emit((
                behavior_function_instruction
            ){ .op = BF_STORE, .type = PIN_I32, .left = after, .target = 0 });
            emit((behavior_function_instruction){ .op = BF_END });
            REQUIRE(finish());
            REQUIRE(behavior_function_frame_count(function) == 2);
            REQUIRE(behavior_function_compile(function));
            for (int backend = 0; backend < 2; backend++) {
                for (int c = 0; c < (mode ? 1 : 3); c++) {
                    arguments[1].i = counts[c];
                    variable.i = 0;
                    REQUIRE(run(arguments, 2, &variable, NULL, backend == 1, 100) == FLOW_OK);
                    REQUIRE(variable.i == 9);
                }
            }
            REQUIRE(function->fast);
        }
    }
    return true;
}

static execution_log_message logged;
static char logged_text[64], logged_name[64];
static int log_count;
static void collect_log(void *, const execution_log_message *message)
{
    log_count++;
    logged = *message;
    snprintf(logged_text, sizeof logged_text, "%s", message->text);
    snprintf(logged_name, sizeof logged_name, "%s", message->program_name);
}

TEST_CASE(execution_print_string_is_synchronous_and_preserves_context)
{
    builder = behavior_function_builder_new();
    pin_value text = { .type = PIN_STR };
    behavior_function_builder_signature(builder, &text, 1, NULL, 0, NULL, 0);
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int host = behavior_function_builder_host(builder, &EXECUTION_PrintString, &access, &text, 1, NULL, 0);
    int input = slot(PIN_STR), pointer = address_slot();
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_STR, .output = input, .value = text });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_first(builder, host) });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_STR,
                                          .left = input,
                                          .right = pointer,
                                          .offset = offsetof(pin_value, str) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                          .node_id = 991 });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    execution_log_service sink = { .emit = collect_log };
    for (int backend = 0; backend < 2; backend++) {
        for (int mode = 0; mode < 3; mode++) {
            release_invocation();
            afree(log_services);
            if (mode != 2) {
                apush(log_services, ((service_binding){ EXECUTION_LOG_SERVICE, &sink }));
            }
            pin_string descriptor = { .bytes = "logged before return",
                                      .length = sizeof("logged before return") - 1 };
            text.text = mode == 0 ? &descriptor : NULL;
            context = (behavior_function_context){
                .arguments = &text,
                .argument_count = 1,
                .budget = 100,
                .now = 1.25,
                .program_name = "PublicLog"
            };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(function, &context, log_services, 77));
            log_count = 0;
            REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
            REQUIRE(log_count == (mode == 2 ? 0 : 1));
            REQUIRE(context.wake_time == 0 && context.pending_commands == 0);
            if (mode != 2) {
                REQUIRE(logged.owner == 77 && logged.node_id == 991 && logged.now == 1.25);
                REQUIRE(!strcmp(logged_name, "PublicLog"));
                REQUIRE(!strcmp(logged_text, mode == 0 ? "logged before return" : ""));
            }
        }
    }
    pin_value invalid = { .type = PIN_I32, .i = 1 };
    log_count = 0;
    REQUIRE(
        !strcmp(execution_print_string(&context, &sink, &invalid, NULL), "Print String input is not a string")
    );
    REQUIRE(log_count == 0);
    return true;
}

typedef struct
{
    const void *address;
    int visits;
} traced_address;
static void count_address(void *user, const void *address)
{
    traced_address *trace = user;
    trace->visits += address == trace->address;
}
TEST_CASE(execution_numeric_address_bits_do_not_retain_saved_storage)
{
    for (int reference = 0; reference < 2; reference++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_type type = reference ? BF_POINTER_TYPE : PIN_U64;
        pin_value initial = { .type = type };
        behavior_function_builder_signature(builder, &initial, 1, &initial, 1, NULL, 0);
        int local = behavior_function_builder_local(builder, initial);
        int input = slot(type), saved = slot(type);
        behavior_value_kind kind = reference ? BEHAVIOR_REFERENCE : BEHAVIOR_VALUE;
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_ARGUMENT, 0, kind));
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_RESULT, 0, kind));
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_LOCAL, local, kind));
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, input, kind));
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, saved, kind));
        emit((
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = type, .output = input, .value = initial });
        emit((
            behavior_function_instruction
        ){ .op = BF_SAVE, .type = type, .target = local, .left = input });
        emit((
            behavior_function_instruction
        ){ .op = BF_LOCAL, .type = type, .left = local, .output = saved });
        emit((behavior_function_instruction){ .op = BF_RESULT, .type = type, .left = saved });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(behavior_function_kind(function, BEHAVIOR_BANK_LOCAL, local) == kind);
        REQUIRE(behavior_function_compile(function));
        void *allocation = arena_alloc(&managed, 32);
        pin_value argument = { .type = type, .u64 = (uint64_t)(uintptr_t)allocation };
        for (int backend = 0; backend < 2; backend++) {
            pin_value result = initial;
            REQUIRE(run(&argument, 1, NULL, &result, backend == 1, 100) == FLOW_OK);
            REQUIRE(context.returned && result.u64 == argument.u64);
            traced_address trace = { .address = allocation };
            behavior_function_trace_values(function, &context, count_address, &trace);
            REQUIRE(trace.visits == (reference ? 2 : 0));
            REQUIRE(
                behavior_function_retains_storage(function, &context, &managed) ==
                (reference != 0)
            );
        }
    }
    return true;
}

TEST_CASE(execution_unary_numeric_operands_ignore_unrelated_address_slot)
{
    builder = behavior_function_builder_new();
    pin_value initial[] = { { .type = PIN_F64 },
                            { .type = PIN_I32 },
                            { .type = PIN_F32 },
                            { .type = PIN_F64 } };
    behavior_function_builder_signature(builder, NULL, 0, initial, 4, NULL, 0);
    REQUIRE(address_slot() == 0);
    int value = slot(PIN_F64), negative = slot(PIN_F64);
    int integer = slot(PIN_I32), narrowed = slot(PIN_F32);
    int absolute = slot(PIN_F64);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_F64,
                                          .output = value,
                                          .value = { .type = PIN_F64, .d = 5.5 } });
    emit((
        behavior_function_instruction
    ){ .op = BF_NEG, .type = PIN_F64, .left = value, .output = negative });
    emit((
        behavior_function_instruction
    ){ .op = BF_CAST, .type = PIN_I32, .left = negative, .output = integer });
    emit((
        behavior_function_instruction
    ){ .op = BF_CONVERT, .type = PIN_F32, .left = negative, .output = narrowed });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_F64, .left = negative, .target = 0 });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_I32, .left = integer, .target = 1 });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_F32, .left = narrowed, .target = 2 });
    emit((
        behavior_function_instruction
    ){ .op = BF_ABS, .type = PIN_F64, .left = negative, .output = absolute });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_F64, .left = absolute, .target = 3 });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value results[4];
        memcpy(results, initial, sizeof results);
        REQUIRE(run(NULL, 0, NULL, results, backend == 1, 100) == FLOW_OK);
        REQUIRE(context.returned);
        REQUIRE(results[0].type == PIN_F64 && results[0].d == -5.5);
        REQUIRE(results[1].type == PIN_I32 && results[1].i == -5);
        REQUIRE(results[2].type == PIN_F32 && results[2].f == -5.5f);
        REQUIRE(results[3].type == PIN_F64 && results[3].d == 5.5);
    }
    return true;
}

static int selected_action_outcome;
static const char *select_action(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *arguments,
    int argument_count,
    pin_value *results,
    int result_count,
    const uint64_t *outcomes,
    int outcome_count,
    int *selected
)
{
    (void)service;
    (void)arguments;
    if (!ctx || ctx->node_id != 991 || argument_count != 0 ||
        result_count != 1 || results[0].type != PIN_I32 || outcome_count != 2 ||
        outcomes[0] != 101 || outcomes[1] != 202) {
        return "Action signature or owned outcome IDs changed";
    }
    results[0].i = 37;
    *selected = selected_action_outcome;
    return NULL;
}
TEST_CASE(execution_synchronous_action_owns_outcomes_and_validates_selection)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    const node_host_call call = { .action = select_action };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int host = behavior_function_builder_host(builder, &call, &access, NULL, 0, &result, 1);
    uint64_t duplicate[] = { 101, 101 }, zero[] = { 0, 202 };
    REQUIRE(!behavior_function_builder_host_outcomes(builder, host, duplicate, 2));
    REQUIRE(!behavior_function_builder_host_outcomes(builder, host, zero, 2));
    uint64_t outcomes[] = { 101, 202 };
    REQUIRE(behavior_function_builder_host_outcomes(builder, host, outcomes, 2));
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_FLOW,
                                          .node_id = 991 });
    int pointer = address_slot(), value = slot(PIN_I32);
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_results(builder, host) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_I32,
                                          .output = value,
                                          .left = pointer,
                                          .offset = offsetof(pin_value, i) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = value });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    outcomes[0] = outcomes[1] = 999;
    behavior_function_builder_free(builder);
    builder = NULL;
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value output = result;
        selected_action_outcome = 1;
        REQUIRE(run(NULL, 0, NULL, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(context.returned_outcome == 202);
        REQUIRE(output.type == PIN_I32 && output.i == 37);
        output = result;
        selected_action_outcome = 2;
        REQUIRE(run(NULL, 0, NULL, &output, backend == 1, 100) == FLOW_ERR_NODE);
        REQUIRE(
            context.error_message &&
            !strcmp(context.error_message, "Action selected an invalid outcome")
        );
        REQUIRE(context.returned_outcome == 0);
        REQUIRE(output.type == PIN_I32 && output.i == 0);
    }
    return true;
}

/* The result bit runs eligible intrinsics in caller storage. Union payloads
 * remain independent values, including repeated sources and nested unions. */
TEST_CASE(execution_intrinsics_build_in_result_storage)
{
    uint64_t push = execution_intrinsic_id(&EXECUTION_array_push_call),
             make = execution_intrinsic_id(&EXECUTION_union_make_call),
             keep = execution_intrinsic_id(&EXECUTION_union_keep_call);
    REQUIRE(
        execution_intrinsic_resolve(push | EXECUTION_INTRINSIC_IN_RESULT) == &EXECUTION_array_push_call
    );
    REQUIRE(
        execution_intrinsic_access(push | EXECUTION_INTRINSIC_IN_RESULT) == &EXECUTION_assignment_access
    );
    REQUIRE(!execution_intrinsic_resolve(make | EXECUTION_INTRINSIC_IN_RESULT));
    REQUIRE(!execution_intrinsic_resolve(keep));
    REQUIRE(
        execution_intrinsic_resolve(keep | EXECUTION_INTRINSIC_IN_RESULT) == &EXECUTION_union_keep_call
    );
    REQUIRE(execution_intrinsic_abi(keep | EXECUTION_INTRINSIC_IN_RESULT) == 1);
    /* A native capability id is a hash, so its bit 0x100 says nothing:
     * this is Resize's. */
    REQUIRE(!execution_intrinsic_in_result(UINT64_C(0xcb19ce8d1a7cd77f)));
    arena storage = { 0 }, other = { 0 };
    pin_array *held = pin_array_snapshot_allocate(&storage, PIN_I32, NULL, 1),
              *foreign = pin_array_snapshot_allocate(&other, PIN_I32, NULL, 1);
    held->items[0] = (pin_value){ .type = PIN_I32, .i = 1 };
    foreign->items[0] = (pin_value){ .type = PIN_I32, .i = 2 };
    pin_value args[] = { { .type = PIN_U64, .u64 = 7 },
                         { .type = PIN_U64, .u64 = 3 },
                         { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = held },
                         { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = foreign },
                         { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = held } },
              out = { .type = PIN_UNION };
    REQUIRE(!EXECUTION_union_keep_call.value(&storage, args, 5, &out));
    const pin_union *value = out.ptr;
    REQUIRE(value->tag == 7 && value->count == 3);
    REQUIRE(value->values[0].ptr != held);
    REQUIRE(value->values[0].ptr != value->values[2].ptr);
    REQUIRE(arena_contains(&storage, value->values[0].ptr));
    REQUIRE(
        value->values[1].ptr != foreign &&
        arena_contains(&storage, value->values[1].ptr)
    );
    REQUIRE(
        value->values[2].ptr != held &&
        arena_contains(&storage, value->values[2].ptr)
    );
    REQUIRE(pin_array_get(value->values[1].ptr, 0).i == 2);
    pin_value changed = { .type = PIN_I32, .i = 9 };
    REQUIRE(!pin_array_snapshot_set(&storage, value->values[0].ptr, 0, &changed));
    REQUIRE(pin_array_get(held, 0).i == 1);
    REQUIRE(pin_array_get(value->values[2].ptr, 0).i == 1);
    /* A different mask preserves the same independent VALUE contract. */
    args[1].u64 = 5;
    REQUIRE(!EXECUTION_union_keep_call.value(&storage, args, 5, &out));
    value = out.ptr;
    REQUIRE(value->values[0].ptr != held && value->values[2].ptr != held);
    REQUIRE(value->values[0].ptr != value->values[2].ptr);
    REQUIRE(pin_array_get(value->values[2].ptr, 0).i == 1);
    /* Nested payloads copy their outer values independently as well. */
    pin_value inner = out,
              nested[] = { args[0], { .type = PIN_U64, .u64 = 3 }, inner, args[2] };
    REQUIRE(!EXECUTION_union_keep_call.value(&storage, nested, 4, &out));
    value = out.ptr;
    REQUIRE(value->values[0].ptr != inner.ptr && value->values[1].ptr != held);
    const pin_union *nested_value = value->values[0].ptr;
    const pin_union *original = inner.ptr;
    REQUIRE(nested_value->values[0].ptr != original->values[0].ptr);
    REQUIRE(!pin_array_snapshot_set(&storage, nested_value->values[0].ptr, 0, &changed));
    REQUIRE(pin_array_get(original->values[0].ptr, 0).i == 1);
    REQUIRE(pin_array_get(value->values[1].ptr, 0).i == 1);
    arena_free(&storage);
    arena_free(&other);
    /* Both constructors have the same payload costs, independent of the old
     * KEEP mask: no payload, plain scalar, collection, and named scalar. The
     * function returns the union's header bits, so its construction is live. */
    for (int form = 0; form < 3; form++) {
        for (int payload = -1; payload < 3; payload++) {
            builder = behavior_function_builder_new();
            pin_value bits = { .type = PIN_U64 };
            behavior_function_builder_signature(builder, NULL, 0, &bits, 1, NULL, 0);
            bool keeping = form != 0;
            pin_value inputs[] = { { .type = PIN_U64, .u64 = 7 },
                                   { .type = PIN_U64, .u64 = (uint64_t)(form - 1) },
                                   { .type = PIN_I32,
                                     .type_name = payload == 2 ? "fixture.Count" : NULL,
                                     .container = payload == 1
                                                    ? PIN_CONTAINER_ARRAY
                                                    : PIN_CONTAINER_SINGLE } },
                      made = { .type = PIN_UNION };
            if (!keeping) {
                inputs[1] = inputs[2];
            }
            uint64_t id = keeping ? keep | EXECUTION_INTRINSIC_IN_RESULT : make;
            int host = behavior_function_builder_host(
                builder,
                keeping ? &EXECUTION_union_keep_call : &EXECUTION_union_make_call,
                keeping ? &EXECUTION_assignment_access : &EXECUTION_value_access,
                inputs,
                (keeping ? 2 : 1) + (payload >= 0),
                &made,
                1
            );
            REQUIRE(
                behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
            );
            emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                                  .target = host,
                                                  .left = BEHAVIOR_FUNCTION_HOST_DATA });
            int pointer = address_slot(), held = slot(PIN_U64);
            emit((behavior_function_instruction){
                .op = BF_HOST_SLOT,
                .type = BF_POINTER_TYPE,
                .output = pointer,
                .left = behavior_function_builder_host_results(builder, host) });
            emit((behavior_function_instruction){ .op = BF_READ,
                                                  .type = PIN_U64,
                                                  .output = held,
                                                  .left = pointer,
                                                  .offset = offsetof(pin_value, u64) });
            emit((behavior_function_instruction){ .op = BF_RESULT,
                                                  .type = PIN_U64,
                                                  .left = held });
            emit((behavior_function_instruction){ .op = BF_END });
            REQUIRE(finish());
            execution_cost_summary costs = behavior_function_costs(function);
            REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE);
            REQUIRE(costs.possible == (payload >= 1 ? EXECUTION_COST_COPY : 0));
            behavior_function_free(function);
            function = NULL;
            behavior_function_builder_free(builder);
            builder = NULL;
        }
    }
    return true;
}
TEST_CASE(execution_union_extraction_checks_dynamic_payload_type)
{
    pin_value payload, value;
    REQUIRE(!pin_value_text(&managed, &payload, "owned payload", sizeof("owned payload") - 1));
    REQUIRE(!pin_union_value(&managed, 42, &payload, 1, &value));
    builder = behavior_function_builder_new();
    pin_value args[] = { value,
                         { .type = PIN_U64, .u64 = 42 },
                         { .type = PIN_I32 },
                         { .type = PIN_BOOL } };
    pin_value text = { .type = PIN_STR };
    REQUIRE(!EXECUTION_union_get_call.value(&managed, args, 4, &text));
    REQUIRE(
        pin_text(text).length == sizeof("owned payload") - 1 &&
        !memcmp(pin_text(text).bytes, "owned payload", sizeof("owned payload") - 1)
    );
    text.type_name = "fixture.text_alias";
    REQUIRE(EXECUTION_union_get_call.value(&managed, args, 4, &text));
    REQUIRE(text.type_name && !strcmp(text.type_name, "fixture.text_alias"));
    /* A native port's C spelling names no type: it extracts into a plain cell. */
    pin_value spelled = payload, native;
    spelled.type_name = "const char *";
    REQUIRE(!pin_union_value(&managed, 42, &spelled, 1, &native));
    pin_value native_args[] = { native, args[1], args[2], args[3] };
    pin_value plain = { .type = PIN_STR };
    REQUIRE(!EXECUTION_union_get_call.value(&managed, native_args, 4, &plain));
    REQUIRE(pin_text(plain).length == sizeof("owned payload") - 1);
    pin_value output = { .type = PIN_F64 };
    args[3].b = true;
    REQUIRE(EXECUTION_union_get_call.value(&managed, args, 4, &output));
    REQUIRE(output.type == PIN_F64);
    args[3].b = false;
    pin_value invalid_tag = { .type = PIN_U64 };
    REQUIRE(EXECUTION_union_make_call.value(&managed, &invalid_tag, 1, &output));
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_union_get_call,
        &EXECUTION_value_access,
        args,
        4,
        &output,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_union_get_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(run(NULL, 0, NULL, NULL, backend == 1, 100) == FLOW_ERR_INVALID_GRAPH);
        REQUIRE(
            context.error_message &&
            !strcmp(context.error_message, "Union payload differs from the extraction type")
        );
    }
    return true;
}

TEST_CASE(execution_prepared_identity_includes_instruction_schema)
{
    enum
    {
        SCHEMA_COUNT = BF_END + 1,
        MAX_BYTES = 4096
    };
    const char *names[SCHEMA_COUNT] = {
#define FUNCTION_NAME(op) [BF_##op] = #op,
#define MATH_NAME(op, name, fn) FUNCTION_NAME(op)
        BEHAVIOR_FUNCTION_OPS(FUNCTION_NAME, MATH_NAME)
#undef MATH_NAME
#undef FUNCTION_NAME
    };
    const char *foreign[SCHEMA_COUNT];
    memcpy(foreign, names, sizeof foreign);
    foreign[BF_REQUIRE] = names[BF_ASSERT];
    foreign[BF_ASSERT] = names[BF_REQUIRE];
    REQUIRE(sizeof names == sizeof foreign);
    REQUIRE(!strcmp(names[BF_END], foreign[BF_END]));
    REQUIRE(!strcmp(names[BF_REQUIRE], "REQUIRE"));
    REQUIRE(!strcmp(foreign[BF_REQUIRE], "ASSERT"));
    uint64_t current_abi = prepared_abi(names, SCHEMA_COUNT);
    uint64_t foreign_abi = prepared_abi(foreign, SCHEMA_COUNT);
    REQUIRE(current_abi != foreign_abi);
    REQUIRE(current_abi == prepared_abi(names, SCHEMA_COUNT));

    builder = behavior_function_builder_new();
    int value = slot(PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .output = value,
                                          .value = { .type = PIN_BOOL, .b = false } });
    /* Under the swapped mapping this valid REQUIRE would become ASSERT with
     * target BUDGET, changing DATA failure into a step-budget failure. */
    emit((behavior_function_instruction){ .op = BF_REQUIRE,
                                          .type = PIN_BOOL,
                                          .left = value,
                                          .target = BEHAVIOR_FUNCTION_BUDGET,
                                          .pin = 7 });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(acount(function->instructions) == 3);
    prepared_plan = execution_module_plan_new();
    prepared_plan->id = 1;
    prepared_plan->valid = true;
    apush(
        prepared_plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = function })
    );
    function = NULL; /* The plan now owns the definition. */
    prepared_plan->function_count = 1;
    char error[192] = { 0 };
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module && !prepared_plan);
    size_t length = 0;
    REQUIRE(
        execution_prepared_write(prepared_module, &prepared_bytes, &length, error, sizeof error)
    );
    REQUIRE(length > 0 && length < MAX_BYTES);
    execution_module_free(prepared_module);
    prepared_module = NULL;
    behavior_function_builder_free(builder);
    builder = NULL;

    /* Decode after retiring the producer, and retain its original failure kind. */
    prepared_module = execution_prepared_read(prepared_bytes, length, NULL, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module);
    REQUIRE(execution_module_function_count(prepared_module) == 1);
    REQUIRE(execution_module_import_count(prepared_module) == 0);
    const behavior_function *decoded = execution_module_function_at(prepared_module, 0)
                                           .body;
    REQUIRE(acount(decoded->instructions) == 3);
    REQUIRE(decoded->instructions[1].op == BF_REQUIRE);
    context = (behavior_function_context){ .budget = 16 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(decoded, &context, NULL, 0));
    REQUIRE(behavior_function_run(decoded, &context, false) == FLOW_ERR_INVALID_GRAPH);
    REQUIRE(context.failure == BEHAVIOR_FUNCTION_DATA && context.error_pin == 7);
    release_invocation();
    execution_module_free(prepared_module);
    prepared_module = NULL;

    execution_wire wire = { .data = prepared_bytes, .size = length, .reading = true };
    uint64_t header[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        execution_wire_u64(&wire, &header[i]);
    }
    REQUIRE(!wire.error && wire.offset < length);
    REQUIRE(header[1] == EXECUTION_PREPARED_VERSION && header[2] == sizeof(void *));
    /* This fails with the original layout-only header, not an arbitrary bit flip. */
    REQUIRE(header[3] == current_abi);
    size_t payload_offset = wire.offset;
    wire = (execution_wire){ 0 };
    for (int i = 0; i < 4; i++) {
        uint64_t field = i == 3 ? foreign_abi : header[i];
        execution_wire_u64(&wire, &field);
    }
    prepared_mutation = wire.data;
    REQUIRE(!wire.error);
    for (size_t i = payload_offset; i < length; i++) {
        apush(prepared_mutation, prepared_bytes[i]);
    }
    REQUIRE(acount(prepared_mutation) < MAX_BYTES);
    const size_t budget = 1024 * 1024;
    size_t remaining = budget;
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    prepared_module = execution_prepared_read_budget(
        prepared_mutation,
        (size_t)acount(prepared_mutation),
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    xalloc_stats_get(&after);
    REQUIRE(!prepared_module);
    REQUIRE(!strcmp(error, "prepared target ABI differs"));
    REQUIRE(remaining == budget);
    /* Only the empty plan was allocated: no function/artifact decoding or
     * definition finalization can precede the schema mismatch. */
    REQUIRE(after.calls - before.calls == 1);
    REQUIRE(after.requested_bytes - before.requested_bytes == sizeof(execution_module_plan));
    return true;
}

/* Real builder-owned banks exercise word boundaries without forged counts. */
TEST_CASE(execution_preparation_reserves_the_larger_matrix_phase)
{
    const int sizes[] = { 0, 1, 63, 64, 65 };
    for (int pause = 0; pause < 2; pause++) {
        for (int si = 0; si < 5; si++) {
            for (int li = 0; li < 5; li++) {
                cleanup();
                builder = behavior_function_builder_new();
                size_t slots = sizes[si], locals = sizes[li], count = pause + 1;
                for (size_t i = 0; i < slots; i++) {
                    slot(PIN_I32);
                }
                for (size_t i = 0; i < locals; i++) {
                    behavior_function_builder_local(builder, (pin_value){ .type = PIN_I32 });
                }
                if (pause) {
                    emit((behavior_function_instruction){ .op = BF_SUSPEND });
                }
                emit((behavior_function_instruction){ .op = BF_END });
                const behavior_function *draft = behavior_function_builder_definition(builder);
                REQUIRE((size_t)acount(draft->slots) == slots);
                REQUIRE((size_t)acount(draft->locals) == locals);
                REQUIRE((size_t)acount(draft->instructions) == count);
                size_t defined = (count + 1) * (slots / 64 + 1) * sizeof(uint64_t);
                size_t live = (count + 1) * ((slots + locals + 63) / 64 + 1) *
                              sizeof(uint64_t);
                /* Keep all other conservative terms, including phases this
                 * host-free fixture does not need, separate from the matrices.
                 * The runtime bodies and their map stay with the definition;
                 * the rest is verification scratch. */
                size_t retained = 2 * (sizeof(dyna_hdr) +
                                       16 * sizeof(behavior_function_instruction)) +
                                  (count + 1) * sizeof(int);
                if (pause) {
                    retained += sizeof(dyna_hdr) +
                                16 * sizeof(behavior_function_suspension);
                }
                size_t fixed = count * (2 * sizeof(bool) + 3 * sizeof(int)) +
                               slots * sizeof(int) +
                               count * (sizeof(int) + 2 * sizeof(bool)) +
                               (slots + 1) * (2 * sizeof(int) + 3 * sizeof(bool)) +
                               sizeof(int) + sizeof(bool) +
                               (slots + 1) * (3 * sizeof(max_align_t) + sizeof(int)) +
                               (count + 1) * sizeof(bool) +
                               (slots + 1) * 2 * sizeof(int) +
                               (locals + 1) * sizeof(bool) +
                               (slots + 1) *
                                   (2 * sizeof(int) + 2 * sizeof(max_align_t)) +
                               sizeof(bool);
                size_t expected = fixed + (defined > live ? defined : live);
                behavior_function_preparation bytes = behavior_function_preparation_bytes(draft);
                REQUIRE(bytes.retained == retained);
                REQUIRE(bytes.scratch == expected);
                REQUIRE(expected < fixed + defined + live);
            }
        }
    }
    cleanup();
    builder = behavior_function_builder_new();
    behavior_function *draft = (behavior_function *)
        behavior_function_builder_definition(builder);
    node_access invalid = { .read_count = -1 };
    apush(draft->host_calls, ((behavior_function_host_call){ .access = &invalid }));
    draft->batchable = true;
    emit((behavior_function_instruction){ .op = BF_HOST_CALL });
    emit((behavior_function_instruction){ .op = BF_END });
    /* Malformed descriptors still propagate SIZE_MAX. This does not claim
     * machine-size arithmetic overflow coverage from tiny real banks. */
    REQUIRE(behavior_function_preparation_bytes(draft).retained == SIZE_MAX);
    REQUIRE(behavior_function_preparation_bytes(draft).scratch == SIZE_MAX);
    return true;
}

/* A definition of `slots` slots that adds into one of them `additions` times.
 * Its verification matrices grow with both, its retained storage only with
 * its instructions. */
/* A definition of `additions` additions over `slots` slots; with `jumps`, a
 * jump to the next addition ends each, so each starts a block. */
static behavior_function *sparse_blocks(int slots, int additions, bool jumps, char *error, size_t size)
{
    pin_value number = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &number, 1, &number, 1, NULL, 0);
    int input = slot(PIN_I32), sum = slot(PIN_I32);
    for (int i = 2; i < slots; i++) {
        slot(PIN_I32);
    }
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = input, .value = number });
    for (int i = 0; i < additions; i++) {
        emit((
            behavior_function_instruction
        ){ .op = BF_ADD, .type = PIN_I32, .output = sum, .left = input, .right = input });
        if (jumps) {
            emit((behavior_function_instruction){ .op = BF_JUMP, .target = 3 + 2 * i });
        }
    }
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = sum });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    behavior_function *sparse = behavior_function_builder_finish(builder, error, size);
    behavior_function_builder_free(builder);
    builder = NULL;
    return sparse;
}
static behavior_function *sparse_definition(int slots, int additions, char *error, size_t size)
{
    return sparse_blocks(slots, additions, false, error, size);
}
enum
{
    SPARSE_SLOTS = 64 * 512,
    SPARSE_ADDITIONS = 600
};
/* The prepared bytes of a module of `count` sparse definitions. */
static bool sparse_module(int count, unsigned char **bytes, size_t *length)
{
    char error[192] = { 0 };
    prepared_plan = execution_module_plan_new();
    prepared_plan->id = 1;
    prepared_plan->valid = true;
    for (int i = 0; i < count; i++) {
        behavior_function *sparse = sparse_definition(SPARSE_SLOTS, SPARSE_ADDITIONS, error, sizeof error);
        if (!sparse) {
            return false;
        }
        apush(
            prepared_plan->functions,
            ((execution_module_function){ .id = (uint64_t)i + 1,
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .body = sparse })
        );
        prepared_plan->function_count = i + 1;
    }
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    bool written = prepared_module &&
                   execution_prepared_write(prepared_module, bytes, length, error, sizeof error);
    execution_module_free(prepared_module);
    prepared_module = NULL;
    return written;
}
/* The least decoded storage limit that loads the module. */
static size_t least_decoded_limit(const unsigned char *bytes, size_t length)
{
    size_t low = 0, high = EXECUTION_WIRE_MAX_BYTES;
    while (low + 1 < high) {
        size_t middle = low + (high - low) / 2,
               remaining = 2 * (size_t)EXECUTION_WIRE_MAX_BYTES;
        execution_prepared_linker bounded = { .decoded_bytes = middle };
        char error[192];
        execution_module *m = execution_prepared_read_budget(
            bytes,
            length,
            &bounded,
            BEHAVIOR_BYTECODE,
            &remaining,
            error,
            sizeof error
        );
        if (m) {
            high = middle;
        } else {
            low = middle;
        }
        execution_module_free(m);
    }
    return high;
}
/* The bytes of runtime code that preparation leaves the loaded module's
 * definition `index`, as the vectors it holds report them: an observation of
 * the result, not the preparation estimate. */
static size_t observed_retained(const unsigned char *bytes, size_t length, int index)
{
    size_t remaining = 2 * (size_t)EXECUTION_WIRE_MAX_BYTES;
    execution_prepared_linker loose = { .decoded_bytes = EXECUTION_WIRE_MAX_BYTES };
    char error[192];
    execution_module *m = execution_prepared_read_budget(
        bytes,
        length,
        &loose,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    assert(m);
    const behavior_function *f = execution_module_function_at(m, index).body;
    size_t held = (size_t)acap(f->runtime_instructions) * sizeof *f->runtime_instructions +
                  (size_t)acap(f->runtime_body) * sizeof *f->runtime_body;
    execution_module_free(m);
    return held;
}
static execution_module *resolve_imported(void *, uint64_t id, uint64_t)
{
    if (!imported_module || execution_module_id(imported_module) != id) {
        return NULL;
    }
    execution_module_retain(imported_module);
    return imported_module;
}
/* The prepared bytes of a module of `count` definitions of `additions` each,
 * importing imported_module when `imports` is set. */
static bool linked_module(
    uint64_t id,
    int count,
    int additions,
    bool imports,
    unsigned char **bytes,
    size_t *length
)
{
    char error[192] = { 0 };
    prepared_plan = execution_module_plan_new();
    prepared_plan->id = id;
    prepared_plan->valid = true;
    for (int i = 0; i < count; i++) {
        behavior_function *f = sparse_definition(2, additions, error, sizeof error);
        if (!f) {
            return false;
        }
        apush(
            prepared_plan->functions,
            ((execution_module_function){ .id = (uint64_t)i + 1,
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .body = f })
        );
        prepared_plan->function_count = i + 1;
    }
    if (imports) {
        execution_module_retain(imported_module);
        apush(prepared_plan->imports, imported_module);
        prepared_plan->import_count = 1;
    }
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    bool written = prepared_module &&
                   execution_prepared_write(prepared_module, bytes, length, error, sizeof error);
    execution_module_free(prepared_module);
    prepared_module = NULL;
    return written;
}
/* The least record limit that loads the module, resolving imported_module. */
static size_t least_record_limit(const unsigned char *bytes, size_t length)
{
    size_t low = 0, high = EXECUTION_WIRE_MAX_RECORDS;
    while (low + 1 < high) {
        size_t middle = low + (high - low) / 2;
        execution_prepared_linker bounded = { .import = resolve_imported,
                                              .decoded_records = middle };
        char error[192];
        execution_module *m = execution_prepared_read(bytes, length, &bounded, BEHAVIOR_BYTECODE, error, sizeof error);
        if (m) {
            high = middle;
        } else {
            low = middle;
        }
        execution_module_free(m);
    }
    return high;
}
/* Reads the module within `limit` records, resolving imported_module. A
 * refusal must name the record bound and leave the budget as it was. */
static execution_module *record_bounded_read(
    const unsigned char *bytes,
    size_t length,
    size_t limit,
    bool *refused
)
{
    execution_prepared_linker bounded = { .import = resolve_imported,
                                          .decoded_bytes = 4 * (size_t)EXECUTION_WIRE_MAX_BYTES,
                                          .decoded_records = limit };
    size_t remaining = 4 * (size_t)EXECUTION_WIRE_MAX_BYTES, before = remaining;
    char error[192] = { 0 };
    execution_module *m = execution_prepared_read_budget(
        bytes,
        length,
        &bounded,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    *refused = !m && remaining == before && strstr(error, "record") &&
               strstr(error, "limit");
    return m;
}
/* An import's fingerprint re-encodes the dependency under the importer's
 * linker bound, so a bound raised past the default admits a dependency
 * above it. A small importer of a larger dependency needs the dependency's
 * records, not only its own. */
TEST_CASE(execution_import_fingerprint_takes_the_linker_record_limit)
{
    unsigned char *dependency = NULL, *alone = NULL, *importer = NULL;
    size_t dependency_length = 0, alone_length = 0, importer_length = 0;
    REQUIRE(linked_module(2, 8, 64, false, &dependency, &dependency_length));
    char error[192];
    imported_module = execution_prepared_read(
        dependency,
        dependency_length,
        NULL,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    REQUIRE(imported_module);
    REQUIRE(linked_module(3, 1, 1, false, &alone, &alone_length));
    REQUIRE(linked_module(3, 1, 1, true, &importer, &importer_length));
    size_t own = least_record_limit(dependency, dependency_length),
           small = least_record_limit(alone, alone_length),
           linked = least_record_limit(importer, importer_length);
    free(dependency);
    free(alone);
    REQUIRE(small < own && linked >= own);
    /* The search's bound loads the importer, and one record fewer refuses
     * it with the record diagnostic and no charge. */
    bool refused = false;
    execution_module *m = record_bounded_read(importer, importer_length, linked, &refused);
    bool loaded = m != NULL;
    execution_module_free(m);
    m = record_bounded_read(importer, importer_length, linked - 1, &refused);
    bool refused_below = !m && refused;
    execution_module_free(m);
    free(importer);
    REQUIRE(loaded && refused_below);
    return true;
}
TEST_CASE(execution_preparation_reserves_one_definitions_scratch)
{
    /* Definitions are verified one at a time, so three more of them need
     * their retained storage and decoded payload, never their scratch. Each
     * definition's reservation covers the runtime code it is observed to keep
     * once loaded, so undercharging that code fails here. */
    char error[192] = { 0 };
    behavior_function *sparse = sparse_definition(SPARSE_SLOTS, SPARSE_ADDITIONS, error, sizeof error);
    REQUIRE(sparse);
    behavior_function_preparation one = behavior_function_preparation_bytes(sparse);
    behavior_function_free(sparse);
    unsigned char *single = NULL, *several = NULL;
    size_t single_length = 0, several_length = 0;
    REQUIRE(sparse_module(1, &single, &single_length));
    REQUIRE(sparse_module(4, &several, &several_length));
    size_t first = least_decoded_limit(single, single_length);
    size_t all = least_decoded_limit(several, several_length);
    size_t kept = observed_retained(several, several_length, 3);
    free(single);
    free(several);
    REQUIRE(kept > 0 && one.retained >= kept);
    REQUIRE(all > first && all - first < one.scratch);
    return true;
}
/* The least aggregate budget that loads the module. */
static size_t least_aggregate_budget(const unsigned char *bytes, size_t length)
{
    size_t low = 0, high = EXECUTION_WIRE_MAX_BYTES;
    while (low + 1 < high) {
        size_t middle = low + (high - low) / 2, remaining = middle;
        execution_prepared_linker loose = { .decoded_bytes = EXECUTION_WIRE_MAX_BYTES };
        char error[192];
        execution_module *m = execution_prepared_read_budget(
            bytes,
            length,
            &loose,
            BEHAVIOR_BYTECODE,
            &remaining,
            error,
            sizeof error
        );
        if (m) {
            high = middle;
        } else {
            low = middle;
        }
        execution_module_free(m);
    }
    return high;
}
TEST_CASE(execution_preparation_reserves_the_runtime_code_it_keeps)
{
    /* Runtime code vectors are sized once, and preparation reserves that
     * size rather than doubled capacity. A module of many small definitions
     * then loads within about its canonical storage plus one definition's
     * scratch; reserving doubled vectors needs half of every definition's
     * retained bytes more. */
    enum
    {
        DEFINITIONS = 64,
        ADDITIONS = 512
    };
    char error[192] = { 0 };
    behavior_function *f = sparse_definition(2, ADDITIONS, error, sizeof error);
    REQUIRE(f);
    behavior_function_preparation one = behavior_function_preparation_bytes(f);
    behavior_function_free(f);
    unsigned char *bytes = NULL;
    size_t length = 0;
    REQUIRE(linked_module(4, DEFINITIONS, ADDITIONS, false, &bytes, &length));
    size_t least = least_aggregate_budget(bytes, length), remaining = least;
    execution_module *m = execution_prepared_read_budget(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    free(bytes);
    REQUIRE(m);
    size_t storage = execution_module_storage_bytes(m);
    execution_module_free(m);
    REQUIRE(one.retained > 0 && least >= storage);
    REQUIRE(least - storage < one.scratch + DEFINITIONS * one.retained / 8);
    return true;
}
/* The verifier keeps one defined-slot set per block, so a definition as
 * long as the package compiler's EmitBody passes, and one whose sets pass
 * 64 MiB is refused before they are allocated: 4097 words per block is
 * 49.2 MB for 1500 blocks, then 68.9 MB for 2100. */
TEST_CASE(execution_verifier_admits_a_large_compiler_function)
{
    char error[192] = { 0 };
    function = sparse_definition(64 * 4096, 8192, error, sizeof error);
    REQUIRE(function);
    behavior_function_free(function);
    function = sparse_blocks(64 * 4096, 1500, true, error, sizeof error);
    REQUIRE(function);
    behavior_function_free(function);
    function = sparse_blocks(64 * 4096, 2100, true, error, sizeof error);
    REQUIRE(!function && !strcmp(error, "definition exceeds verifier storage budget"));
    return true;
}

TEST_CASE(execution_decoded_preparation_cleans_partial_live_roots)
{
    builder = behavior_function_builder_new();
    pin_value shape = { .type = PIN_TASK, .container = PIN_CONTAINER_ARRAY },
              result = { .type = BF_POINTER_TYPE };
    behavior_function_builder_signature(builder, &shape, 1, &result, 1, NULL, 0);
    int input = address_slot();
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_RESULT, 0, BEHAVIOR_ADDRESS));
    behavior_function_builder_root(builder, BEHAVIOR_BANK_SLOT, input, shape);
    emit((behavior_function_instruction){ .op = BF_ARRAY,
                                          .type = BF_POINTER_TYPE,
                                          .target = BF_ARGUMENT,
                                          .output = input,
                                          .value = shape });
    emit((behavior_function_instruction){ .op = BF_SUSPEND });
    emit((behavior_function_instruction){ .op = BF_RESULT,
                                          .type = BF_POINTER_TYPE,
                                          .left = input });
    emit((behavior_function_instruction){ .op = BF_END });
    behavior_function *draft = (behavior_function *)
        behavior_function_builder_definition(builder);
    /* Permit the copied live-value index, but not the subsequent root. The
     * decoder uses this same bounded arena path; no allocator is mocked. */
    arena_init(&draft->owned, 4096);
    arena_alloc(&draft->owned, 4096 - _Alignof(max_align_t));
    size_t budget = arena_storage_bytes(&draft->owned);
    char error[192];
    REQUIRE(!behavior_function_prepare_definition(draft, budget, error, sizeof error));
    REQUIRE(!strcmp(error, "suspension live sets exceed preparation budget"));
    REQUIRE(acount(draft->suspensions) == 1);
    const behavior_function_suspension *saved = &draft->suspensions[0];
    REQUIRE(saved->value_count == 1 && saved->root_count == 1);
    REQUIRE(
        saved->values && !saved->roots && arena_contains(&draft->owned, saved->values)
    );
    REQUIRE(arena_storage_bytes(&draft->owned) == budget && !draft->frozen);
    cleanup(); /* The partially prepared definition still has one owner. */
    return true;
}

TEST_CASE(execution_decoded_suspensions_charge_exact_live_sets)
{
    /* A wide chain followed by many pauses keeps one value live. This allowance
     * rejects summing the sequential matrices, but admits their larger phase. */
    enum
    {
        WIDE_SLOTS = 1024,
        WIDE_PAUSES = 512
    };
    const size_t budget = 1472 * 1024;
    pin_value number = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &number, 1, &number, 1, NULL, 0);
    int input = slot(PIN_I32), sum = input;
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = input, .value = number });
    for (int i = 1; i < WIDE_SLOTS; i++) {
        int next = slot(PIN_I32);
        emit((
            behavior_function_instruction
        ){ .op = BF_ADD, .type = PIN_I32, .output = next, .left = sum, .right = input });
        sum = next;
    }
    for (int i = 0; i < WIDE_PAUSES; i++) {
        emit((behavior_function_instruction){ .op = BF_SUSPEND });
    }
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = sum });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(acount(function->slots) == WIDE_SLOTS && acount(function->suspensions) == WIDE_PAUSES);
    prepared_plan = execution_module_plan_new();
    prepared_plan->id = 1;
    prepared_plan->valid = true;
    apush(
        prepared_plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = function })
    );
    function = NULL;
    prepared_plan->function_count = 1;
    char error[192] = { 0 };
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module);
    size_t length = 0;
    REQUIRE(
        execution_prepared_write(prepared_module, &prepared_bytes, &length, error, sizeof error)
    );
    execution_module_free(prepared_module);
    prepared_module = NULL;
    behavior_function_builder_free(builder);
    builder = NULL;
    for (int backend = 0; backend < 2; backend++) {
        size_t remaining = 1;
        prepared_module = execution_prepared_read_budget(
            prepared_bytes,
            length,
            NULL,
            backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
            &remaining,
            error,
            sizeof error
        );
        REQUIRE(!prepared_module && error[0] && remaining == 1);
        remaining = budget;
        prepared_module = execution_prepared_read_budget(
            prepared_bytes,
            length,
            NULL,
            backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
            &remaining,
            error,
            sizeof error
        );
        if (!prepared_module) {
            fprintf(stderr, "decoded wide suspensions: %s\n", error);
        }
        REQUIRE(prepared_module);
        REQUIRE(budget - remaining == execution_module_storage_bytes(prepared_module));
        const behavior_function
            *decoded = execution_module_function_at(prepared_module, 0).body;
        REQUIRE(acount(decoded->suspensions) == WIDE_PAUSES);
        for (int i = 0; i < WIDE_PAUSES; i++) {
            const behavior_function_suspension *s = &decoded->suspensions[i];
            REQUIRE(s->value_count == 1 && s->values[0] == sum && !s->root_count);
            REQUIRE(arena_contains(&decoded->owned, s->values));
        }
        /* run() borrows the module's definition; cleanup must never free it. */
        function = (behavior_function *)decoded;
        pin_value argument = { .type = PIN_I32, .i = 3 }, result = number;
        int pauses = 0,
            flow = run(&argument, 1, NULL, &result, backend == 1, 1u << 20);
        while (flow == FLOW_SUSPENDED_RESULT) {
            pauses++;
            flow = behavior_function_run(function, &context, backend == 1);
        }
        release_invocation();
        function = NULL;
        REQUIRE(flow == FLOW_OK && pauses == WIDE_PAUSES && result.i == 3 * WIDE_SLOTS);
        execution_module_free(prepared_module);
        prepared_module = NULL;
    }
    return true;
}

typedef struct
{
    pin_value input;
    pin_type to;
    behavior_cast_mode mode;
    uint64_t bits; /* Integer result bits, or the double's representation. */
} cast_vector;
static pin_value cast_input(pin_type type, uint64_t bits)
{
    return behavior_scalar_from_bits(type, bits);
}
static pin_value cast_real(pin_type type, double x)
{
    return type == PIN_F32 ? (pin_value){ .type = PIN_F32, .f = (float)x }
                           : (pin_value){ .type = PIN_F64, .d = x };
}
static uint64_t cast_double_bits(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}
static uint64_t cast_result_bits(pin_value value)
{
    return value.type == PIN_F64 ? cast_double_bits(value.d)
                                 : behavior_scalar_bits(value);
}
/* No cast fails: EXACT clamps a value that does not fit, as CLAMP does. */
TEST_CASE(execution_numeric_casts_apply_exact_wrap_and_clamp)
{
    enum
    {
        E = BEHAVIOR_CAST_EXACT,
        W = BEHAVIOR_CAST_WRAP,
        C = BEHAVIOR_CAST_CLAMP
    };
    const uint64_t big = UINT64_C(1) << 40, top = UINT64_C(1) << 63;
    const cast_vector vectors[] = {
        { cast_input(PIN_I64, 300), PIN_I32, E, 300 },
        { cast_input(PIN_I64, big), PIN_I32, E, INT32_MAX },
        { cast_input(PIN_I64, big + 7), PIN_I32, W, 7 },
        { cast_input(PIN_I64, big), PIN_I32, C, INT32_MAX },
        { cast_input(PIN_I64, (uint64_t)-(int64_t)big), PIN_I32, C, (uint64_t)(int64_t)INT32_MIN },
        { cast_input(PIN_U64, 255), PIN_U8, E, 255 },
        { cast_input(PIN_U64, 300), PIN_U8, E, 255 },
        { cast_input(PIN_U64, 300), PIN_U8, W, 44 },
        { cast_input(PIN_U64, 300), PIN_U8, C, 255 },
        { cast_input(PIN_I32, (uint64_t)-1), PIN_U64, E, 0 },
        { cast_input(PIN_I32, (uint64_t)-1), PIN_U64, W, UINT64_MAX },
        { cast_input(PIN_I32, (uint64_t)-1), PIN_U64, C, 0 },
        { cast_input(PIN_I32, 5), PIN_U64, E, 5 },
        { cast_input(PIN_U64, top), PIN_I64, E, INT64_MAX },
        { cast_input(PIN_U64, top), PIN_I64, C, INT64_MAX },
        { cast_input(PIN_U64, top), PIN_I64, W, top },
        { cast_input(PIN_I8, (uint64_t)-1), PIN_U16, E, 0 },
        { cast_input(PIN_I8, (uint64_t)-1), PIN_I32, E, UINT64_MAX },
        { cast_input(PIN_U8, 200), PIN_I32, E, 200 },
        { cast_input(PIN_I16, 200), PIN_I8, W, (uint64_t)-56 },
        { cast_input(PIN_U32, 0x80000000u), PIN_I32, E, INT32_MAX },
        { cast_input(PIN_U32, 0x80000000u), PIN_I32, C, INT32_MAX },
        { cast_real(PIN_F64, 0x1p63 + 4096), PIN_U64, E, top + 4096 },
        { cast_real(PIN_F64, 1e30), PIN_U64, E, UINT64_MAX },
        { cast_real(PIN_F64, -5), PIN_U64, E, 0 },
        { cast_real(PIN_F64, NAN), PIN_I32, E, 0 },
        { cast_real(PIN_F64, -40000.5), PIN_I16, E, (uint64_t)(int64_t)INT16_MIN },
        { cast_real(PIN_F64, 123.9), PIN_I16, E, 123 },
        { cast_real(PIN_F32, 2.75), PIN_I32, E, 2 },
        { cast_real(PIN_F64, 0x1p63), PIN_I64, E, INT64_MAX },
        { cast_real(PIN_F64, -1e30), PIN_I64, E, (uint64_t)INT64_MIN },
        { cast_real(PIN_F32, 0x1p63), PIN_U64, E, top },
        { cast_real(PIN_F64, -0.0), PIN_U8, E, 0 },
        { cast_input(PIN_U64, UINT64_MAX), PIN_F64, E, cast_double_bits(0x1p64) },
        { cast_input(PIN_I64, (uint64_t)-3), PIN_F64, E, cast_double_bits(-3) },
    };
    for (size_t v = 0; v < sizeof vectors / sizeof *vectors; v++) {
        const cast_vector *c = &vectors[v];
        pin_value from = { .type = c->input.type }, returned = { .type = c->to };
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, &from, 1, &returned, 1, NULL, 0);
        int input = slot(from.type), output = slot(c->to);
        emit((
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = from.type, .output = input, .value = from });
        emit((behavior_function_instruction){ .op = BF_CAST,
                                              .type = c->to,
                                              .output = output,
                                              .left = input,
                                              .target = c->mode,
                                              .pin = 3 });
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = returned.type,
                                              .left = output });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(behavior_function_compile(function));
        for (int backend = 0; backend < 2; backend++) {
            pin_value result = returned;
            int flow = run(&c->input, 1, NULL, &result, backend == 1, 16);
            if (flow != FLOW_OK || cast_result_bits(result) != c->bits) {
                fprintf(
                    stderr,
                    "cast vector %zu backend %d: flow %d bits %llx\n",
                    v,
                    backend,
                    flow,
                    (unsigned long long)cast_result_bits(result)
                );
            }
            REQUIRE(
                flow == FLOW_OK && result.type == c->to &&
                cast_result_bits(result) == c->bits
            );
        }
        release_invocation();
        behavior_function_free(function);
        function = NULL;
        behavior_function_builder_free(builder);
        builder = NULL;
    }
    return true;
}

/* One reportable operation with its own source in a fresh definition with
 * report sites: two typed arguments, or one for a unary operation or a cast. */
static bool reporting_operation(behavior_function_op op, pin_type from, pin_type to, int mode)
{
    bool binary = op != BF_NEG && op != BF_ABS && op != BF_CAST;
    pin_value arguments[2] = { { .type = from }, { .type = from } },
              returned = { .type = to };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, arguments, binary ? 2 : 1, &returned, 1, NULL, 0);
    int source = behavior_function_builder_origin(
        builder,
        (
            behavior_function_source
        ){ .asset_name = "integers.chibi", .parent = -1, .line = 4, .column = 9 }
    );
    int left = slot(from), right = binary ? slot(from) : 0, output = slot(to);
    for (int a = 0; a < (binary ? 2 : 1); a++) {
        emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                              .type = from,
                                              .output = a ? right : left,
                                              .left = a,
                                              .value = arguments[a] });
    }
    emit((behavior_function_instruction){ .op = op,
                                          .type = to,
                                          .output = output,
                                          .left = left,
                                          .right = right,
                                          .target = op == BF_CAST ? mode
                                                  : op == BF_DIV || op == BF_MOD ? -1
                                                                                 : 0,
                                          .source = source });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = to, .left = output });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    if (!finish()) {
        return false;
    }
    behavior_function_report_integers(function);
    return function->integer_reports && behavior_function_compile(function);
}
/* The kind the run marked; -1 for none and -2 for more than one. Clears the
 * sites for the next run. */
static int reported_kind()
{
    int kind = -1, marks = 0;
    int count = function->integer_report_sites * EXECUTION_INTEGER_REPORT_KINDS;
    for (int site = 0; site < count; site++) {
        if (atomic_exchange(&function->integer_reports[site], 0)) {
            kind = site % EXECUTION_INTEGER_REPORT_KINDS;
            marks++;
        }
    }
    return marks > 1 ? -2 : kind;
}
typedef struct
{
    pin_type type;
    behavior_function_op op;
    uint64_t left, right;
    int kind;
} integer_report_vector;
/* One default source and one explicit addition source: two occurrence/delivered
 * arrays, three kinds each, so this fixture retains exactly twelve report bytes.
 * The suspend keeps fast-body storage identical with reports off and on. */
static bool integer_report_budget_definition()
{
    builder = behavior_function_builder_new();
    pin_value number = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, &number, 1, &number, 1, NULL, 0);
    int input = slot(PIN_I32), one = slot(PIN_I32), sum = slot(PIN_I32);
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = input, .value = number });
    emit((behavior_function_instruction){ .op = BF_SUSPEND });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .output = one,
                                          .value = { .type = PIN_I32, .i = 1 } });
    int origin = behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .asset_name = "report-budget.chibi",
                                    .parent = -1,
                                    .start = 0,
                                    .end = 5,
                                    .line = 1,
                                    .column = 1 }
    );
    REQUIRE(origin == 1);
    emit((behavior_function_instruction){ .op = BF_ADD,
                                          .type = PIN_I32,
                                          .left = input,
                                          .right = one,
                                          .output = sum,
                                          .source = origin });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = sum });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(!function->fast && acount(function->sources) == 2);
    behavior_function_builder_free(builder);
    builder = NULL;
    return true;
}
static bool integer_report_budget_fixture(bool nested)
{
    REQUIRE(integer_report_budget_definition());
    if (nested) {
        callee = function;
        function = NULL;
        REQUIRE(integer_report_budget_definition());
        /* The plan owns both verified definitions through this root. */
        apush(function->definitions, callee);
        callee = NULL;
    }
    prepared_plan = execution_module_plan_new();
    prepared_plan->id = 1;
    prepared_plan->valid = true;
    apush(
        prepared_plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = function })
    );
    prepared_plan->function_count = 1;
    function = NULL;
    char error[256];
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module);
    return true;
}
static execution_module *read_integer_report_budget(size_t length, bool enabled, size_t *remaining)
{
    char error[256];
    execution_integer_reports_enable(enabled);
    execution_module *module = execution_prepared_read_budget(
        prepared_bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        remaining,
        error,
        sizeof error
    );
    execution_integer_reports_enable(false);
    return module;
}
static bool check_integer_report_budget(bool nested)
{
    REQUIRE(integer_report_budget_fixture(nested));
    char error[256];
    size_t length = 0;
    REQUIRE(
        execution_prepared_write(prepared_module, &prepared_bytes, &length, error, sizeof error)
    );
    execution_module_free(prepared_module);
    prepared_module = NULL;
    const size_t budget = 32 * 1024;
    size_t charged[2], retained[2], admitted[2];
    for (int enabled = 0; enabled < 2; enabled++) {
        size_t remaining = budget;
        prepared_module = read_integer_report_budget(length, enabled, &remaining);
        REQUIRE(prepared_module);
        const behavior_function
            *decoded = execution_module_function_at(prepared_module, 0).body;
        REQUIRE(decoded && !decoded->fast && !decoded->artifact->code);
        REQUIRE(acount(decoded->sources) == 2 && acount(decoded->definitions) == (int)nested);
        if (nested) {
            const behavior_function *child = decoded->definitions[0];
            REQUIRE(!child->fast && !child->artifact->code && acount(child->sources) == 2);
            REQUIRE((child->integer_reports != NULL) == (enabled != 0));
        }
        REQUIRE((decoded->integer_reports != NULL) == (enabled != 0));
        REQUIRE(!enabled || decoded->integer_report_sites == 2);
        /* Accounting follows allocated ownership even after the global switch
         * is off. The bank remains live through another module reference. */
        execution_module_retain(prepared_module);
        execution_module_free(prepared_module);
        charged[enabled] = budget - remaining;
        retained[enabled] = execution_module_storage_bytes(prepared_module);
        REQUIRE(charged[enabled] == retained[enabled]);
        size_t again_length = 0;
        REQUIRE(
            execution_prepared_write(prepared_module, &prepared_again, &again_length, error, sizeof error)
        );
        REQUIRE(again_length == length && !memcmp(prepared_bytes, prepared_again, length));
        free(prepared_again);
        prepared_again = NULL;
        execution_module_free(prepared_module);
        prepared_module = NULL;
        /* At most sixteen tiny reads locate the boundary without assuming
         * pointer widths or unrelated arena/vector capacities. */
        size_t low = 0, high = budget;
        while (low + 1 < high) {
            size_t limit = low + (high - low) / 2;
            remaining = limit;
            prepared_module = read_integer_report_budget(length, enabled, &remaining);
            if (prepared_module) {
                high = limit;
                execution_module_free(prepared_module);
                prepared_module = NULL;
            } else {
                REQUIRE(remaining == limit);
                low = limit;
            }
        }
        admitted[enabled] = high;
        remaining = high;
        prepared_module = read_integer_report_budget(length, enabled, &remaining);
        REQUIRE(prepared_module && high - remaining == charged[enabled]);
        execution_module_free(prepared_module);
        prepared_module = NULL;
        remaining = high - 1;
        prepared_module = read_integer_report_budget(length, enabled, &remaining);
        REQUIRE(!prepared_module && remaining == high - 1);
    }
    /* Independent fixture oracle: not another call to the storage helper. */
    size_t expected = nested ? 24 : 12;
    REQUIRE(charged[1] == charged[0] + expected);
    REQUIRE(retained[1] == retained[0] + expected);
    REQUIRE(admitted[1] == admitted[0] + expected);
    return true;
}
TEST_CASE(execution_prepared_integer_reports_charge_owned_storage)
{
    return check_integer_report_budget(false);
}
TEST_CASE(execution_prepared_integer_reports_charge_owned_children)
{
    return check_integer_report_budget(true);
}
static execution_module *integer_budget_import(void *user, uint64_t id, uint64_t)
{
    execution_module *module = user;
    if (execution_module_id(module) != id) {
        return NULL;
    }
    execution_module_retain(module);
    return module;
}
TEST_CASE(execution_integer_reports_charge_retained_package_imports)
{
    REQUIRE(integer_report_budget_fixture(false));
    char error[256];
    size_t length = 0;
    REQUIRE(
        execution_prepared_write(prepared_module, &prepared_bytes, &length, error, sizeof error)
    );
    /* The package owns an empty root; the linker supplies the same library
     * generation twice. Its definition must be charged once, even with the
     * startup switch off while the package retains an enabled generation. */
    prepared_plan = execution_module_plan_new();
    prepared_plan->id = 2;
    prepared_plan->valid = true;
    execution_module_retain(prepared_module);
    apush(prepared_plan->imports, prepared_module);
    execution_module_retain(prepared_module);
    apush(prepared_plan->imports, prepared_module);
    prepared_plan->import_count = 2;
    execution_module *owner = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(owner);
    size_t owner_length = 0;
    REQUIRE(execution_prepared_write(owner, &prepared_again, &owner_length, error, sizeof error));
    execution_module_free(owner);
    execution_module_free(prepared_module);
    prepared_module = NULL;
    const size_t budget = 64 * 1024;
    size_t charged[2];
    for (int enabled = 0; enabled < 2; enabled++) {
        size_t remaining = budget;
        prepared_module = read_integer_report_budget(length, enabled, &remaining);
        REQUIRE(prepared_module);
        const behavior_function
            *decoded = execution_module_function_at(prepared_module, 0).body;
        REQUIRE(decoded && !decoded->fast && acount(decoded->sources) == 2);
        REQUIRE((decoded->integer_reports != NULL) == (enabled != 0));
        const execution_prepared_linker linker = { .user = prepared_module,
                                                   .import = integer_budget_import };
        package_module records[] = {
            { .name = "Owner", .id = 2, .data = prepared_again, .length = owner_length },
        };
        remaining = budget;
        package_collection *collection = package_read_budget(
            records,
            1,
            &linker,
            NULL,
            BEHAVIOR_BYTECODE,
            &remaining,
            error,
            sizeof error
        );
        REQUIRE(collection && !execution_integer_reports_enabled());
        execution_module *loaded = package_collection_module(collection, 0);
        REQUIRE(execution_module_import_count(loaded) == 2);
        REQUIRE(execution_module_import_at(loaded, 0) == prepared_module);
        REQUIRE(execution_module_import_at(loaded, 1) == prepared_module);
        charged[enabled] = budget - remaining;
        package_collection_free(collection);
        execution_module_free(prepared_module);
        prepared_module = NULL;
    }
    REQUIRE(charged[1] == charged[0] + 12);
    return true;
}

/* Each integer operation reports exactly when its declared-width result
 * differs from the exact one, or its divisor is zero, on both backends. The
 * values are those a shipped build computes. */
TEST_CASE(execution_integer_reports_mark_overflow_and_zero_divisors)
{
    enum
    {
        O = EXECUTION_INTEGER_OVERFLOW,
        Z = EXECUTION_INTEGER_ZERO_DIVISOR,
        N = -1
    };
    const uint64_t i8_min = (uint64_t)INT8_MIN, i32_min = (uint64_t)INT32_MIN,
                   i64_min = (uint64_t)INT64_MIN, ones = UINT64_MAX,
                   p31 = UINT64_C(1) << 31, p32 = UINT64_C(1) << 32;
    const integer_report_vector vectors[] = {
        { PIN_I8, BF_ADD, 127, 1, O },
        { PIN_I8, BF_ADD, 126, 1, N },
        { PIN_I8, BF_ADD, i8_min, ones, O },
        { PIN_I8, BF_SUB, i8_min, 1, O },
        { PIN_I8, BF_SUB, 0, i8_min, O },
        { PIN_I8, BF_MUL, 16, 8, O },
        { PIN_I8, BF_MUL, (uint64_t)-16, 8, N },
        { PIN_I8, BF_MUL, i8_min, ones, O },
        { PIN_U8, BF_ADD, 255, 1, O },
        { PIN_U8, BF_SUB, 0, 1, O },
        { PIN_U8, BF_SUB, 1, 1, N },
        { PIN_U8, BF_MUL, 16, 16, O },
        { PIN_U8, BF_MUL, 15, 17, N },
        { PIN_I16, BF_MUL, 256, 128, O },
        { PIN_U16, BF_MUL, 65535, 65535, O },
        { PIN_U16, BF_ADD, 65535, 0, N },
        { PIN_I32, BF_ADD, INT32_MAX, 1, O },
        { PIN_I32, BF_SUB, i32_min, 1, O },
        { PIN_I32, BF_MUL, 65536, 32768, O },
        { PIN_I32, BF_MUL, (uint64_t)-65536, 32768, N },
        { PIN_I32, BF_MUL, i32_min, ones, O },
        { PIN_U32, BF_ADD, UINT32_MAX, 1, O },
        { PIN_U32, BF_SUB, 0, 1, O },
        { PIN_U32, BF_MUL, 65536, 65536, O },
        { PIN_U32, BF_MUL, 65535, 65537, N },
        { PIN_I64, BF_ADD, INT64_MAX, 1, O },
        { PIN_I64, BF_ADD, INT64_MAX, ones, N },
        { PIN_I64, BF_SUB, i64_min, 1, O },
        { PIN_I64, BF_MUL, p32, p31, O },
        { PIN_I64, BF_MUL, (uint64_t)-(int64_t)p32, p31, N },
        { PIN_I64, BF_MUL, i64_min, ones, O },
        { PIN_I64, BF_MUL, ones, i64_min, O },
        { PIN_I64, BF_MUL, ones, INT64_MAX, N },
        { PIN_I64, BF_MUL, 3, UINT64_C(1) << 62, O },
        { PIN_U64, BF_ADD, UINT64_MAX, 1, O },
        { PIN_U64, BF_SUB, 0, 1, O },
        { PIN_U64, BF_MUL, p32, p32, O },
        { PIN_U64, BF_MUL, p32 - 1, p32 + 1, N },
        { PIN_I32, BF_DIV, 7, 0, Z },
        { PIN_I32, BF_DIV, i32_min, ones, O },
        { PIN_I32, BF_DIV, i32_min, 1, N },
        { PIN_U8, BF_DIV, 9, 0, Z },
        { PIN_I8, BF_DIV, i8_min, ones, O },
        { PIN_I64, BF_DIV, i64_min, ones, O },
        { PIN_U64, BF_DIV, 5, 0, Z },
        { PIN_I32, BF_MOD, 7, 0, Z },
        { PIN_I32, BF_MOD, i32_min, ones, N },
        { PIN_I8, BF_MOD, i8_min, ones, N },
        { PIN_U16, BF_MOD, 3, 0, Z },
        { PIN_I8, BF_NEG, i8_min, 0, O },
        { PIN_I8, BF_NEG, 127, 0, N },
        { PIN_U8, BF_NEG, 0, 0, N },
        { PIN_U8, BF_NEG, 1, 0, O },
        { PIN_I32, BF_NEG, i32_min, 0, O },
        { PIN_U32, BF_NEG, 0, 0, N },
        { PIN_I64, BF_NEG, i64_min, 0, O },
        { PIN_U64, BF_NEG, 5, 0, O },
        { PIN_I16, BF_ABS, (uint64_t)INT16_MIN, 0, O },
        { PIN_I16, BF_ABS, (uint64_t)-32767, 0, N },
        { PIN_I64, BF_ABS, i64_min, 0, O },
    };
    for (size_t v = 0; v < sizeof vectors / sizeof *vectors; v++) {
        const integer_report_vector *c = &vectors[v];
        cleanup();
        REQUIRE(reporting_operation(c->op, c->type, c->type, 0));
        pin_value arguments[2] = { behavior_scalar_from_bits(c->type, c->left),
                                   behavior_scalar_from_bits(c->type, c->right) },
                  expected;
        behavior_scalar_integer_op(c->op, arguments[0], arguments[1], &expected, NULL);
        for (int backend = 0; backend < 2; backend++) {
            pin_value result = { .type = c->type };
            int flow = run(arguments, 2, NULL, &result, backend == 1, 16);
            int kind = reported_kind();
            if (flow != FLOW_OK || kind != c->kind ||
                behavior_scalar_bits(result) != behavior_scalar_bits(expected)) {
                fprintf(
                    stderr,
                    "integer report vector %zu backend %d: flow %d kind %d\n",
                    v,
                    backend,
                    flow,
                    kind
                );
            }
            REQUIRE(flow == FLOW_OK && kind == c->kind);
            REQUIRE(behavior_scalar_bits(result) == behavior_scalar_bits(expected));
        }
    }
    cleanup();
    return true;
}

/* EXACT conversions report a value that clamps, including NaN, and a float
 * that truncates into range does not; wrapping conversions never report. */
TEST_CASE(execution_integer_reports_mark_clamping_conversions)
{
    enum
    {
        E = BEHAVIOR_CAST_EXACT,
        W = BEHAVIOR_CAST_WRAP,
        C = EXECUTION_INTEGER_CLAMP,
        N = -1
    };
    const struct
    {
        pin_value input;
        pin_type to;
        int mode, kind;
    } vectors[] = {
        { cast_input(PIN_I64, UINT64_C(1) << 40), PIN_I32, E, C },
        { cast_input(PIN_I64, 300), PIN_I32, E, N },
        { cast_input(PIN_I64, UINT64_C(1) << 40), PIN_I32, W, N },
        { cast_input(PIN_U64, 300), PIN_U8, E, C },
        { cast_input(PIN_I32, (uint64_t)-1), PIN_U64, E, C },
        { cast_input(PIN_I32, 5), PIN_U64, E, N },
        { cast_input(PIN_U64, UINT64_C(1) << 63), PIN_I64, E, C },
        { cast_input(PIN_I8, (uint64_t)-1), PIN_I32, E, N },
        { cast_input(PIN_U32, 0x80000000u), PIN_I32, E, C },
        { cast_real(PIN_F64, NAN), PIN_I32, E, C },
        { cast_real(PIN_F64, -128), PIN_I8, E, N },
        { cast_real(PIN_F64, -128.9), PIN_I8, E, N },
        { cast_real(PIN_F64, -129), PIN_I8, E, C },
        { cast_real(PIN_F64, 127.9), PIN_I8, E, N },
        { cast_real(PIN_F64, 128), PIN_I8, E, C },
        { cast_real(PIN_F64, -0.5), PIN_U8, E, N },
        { cast_real(PIN_F64, -1), PIN_U8, E, C },
        { cast_real(PIN_F64, 2147483647), PIN_I32, E, N },
        { cast_real(PIN_F64, -2147483648.9), PIN_I32, E, N },
        { cast_real(PIN_F64, -2147483649.0), PIN_I32, E, C },
        { cast_real(PIN_F32, 2.75), PIN_I32, E, N },
        { cast_real(PIN_F32, 0x1p31), PIN_I32, E, C },
        { cast_real(PIN_F64, 0x1p63), PIN_I64, E, C },
        { cast_real(PIN_F64, -0x1p63), PIN_I64, E, N },
        { cast_real(PIN_F64, 0x1p64), PIN_U64, E, C },
        { cast_real(PIN_F64, 0x1.fffffffffffffp63), PIN_U64, E, N },
    };
    for (size_t v = 0; v < sizeof vectors / sizeof *vectors; v++) {
        cleanup();
        pin_type from = vectors[v].input.type, to = vectors[v].to;
        REQUIRE(reporting_operation(BF_CAST, from, to, vectors[v].mode));
        pin_value expected;
        behavior_scalar_cast(
            vectors[v].input,
            to,
            (behavior_cast_mode)vectors[v].mode,
            &expected
        );
        for (int backend = 0; backend < 2; backend++) {
            pin_value result = { .type = to };
            int flow = run(&vectors[v].input, 1, NULL, &result, backend == 1, 16);
            int kind = reported_kind();
            if (flow != FLOW_OK || kind != vectors[v].kind) {
                fprintf(stderr, "cast report vector %zu backend %d: flow %d kind %d\n", v, backend, flow, kind);
            }
            REQUIRE(flow == FLOW_OK && kind == vectors[v].kind);
            REQUIRE(behavior_scalar_bits(result) == behavior_scalar_bits(expected));
        }
    }
    cleanup();
    return true;
}

/* Results written out by hand, independent of the scalar helpers the vectors
 * above compare both backends against: a development build keeps each value a
 * shipped build computes while it reports. */
TEST_CASE(execution_integer_reports_keep_literal_shipped_values)
{
    enum
    {
        O = EXECUTION_INTEGER_OVERFLOW,
        Z = EXECUTION_INTEGER_ZERO_DIVISOR,
        C = EXECUTION_INTEGER_CLAMP,
        N = -1,
        E = BEHAVIOR_CAST_EXACT,
        W = BEHAVIOR_CAST_WRAP
    };
    const struct
    {
        behavior_function_op op;
        pin_value left;
        int64_t right;
        pin_type to;
        int mode;
        int64_t expected;
        int kind;
    } cases[] = {
        { BF_ADD, cast_input(PIN_I32, INT32_MAX), 1, PIN_I32, 0, INT32_MIN, O },
        { BF_SUB, cast_input(PIN_U8, 0), 1, PIN_U8, 0, 255, O },
        { BF_MUL, cast_input(PIN_I8, 16), 8, PIN_I8, 0, INT8_MIN, O },
        { BF_MUL, cast_input(PIN_I64, (uint64_t)INT64_MIN), -1, PIN_I64, 0, INT64_MIN, O },
        { BF_NEG, cast_input(PIN_I32, (uint64_t)INT32_MIN), 0, PIN_I32, 0, INT32_MIN, O },
        { BF_ABS, cast_input(PIN_I16, (uint64_t)INT16_MIN), 0, PIN_I16, 0, INT16_MIN, O },
        { BF_DIV, cast_input(PIN_I32, (uint64_t)INT32_MIN), -1, PIN_I32, 0, INT32_MIN, O },
        { BF_MOD, cast_input(PIN_I32, (uint64_t)INT32_MIN), -1, PIN_I32, 0, 0, N },
        { BF_DIV, cast_input(PIN_I32, 7), 0, PIN_I32, 0, 0, Z },
        { BF_MOD, cast_input(PIN_I32, 7), 0, PIN_I32, 0, 7, Z },
        { BF_MOD, cast_input(PIN_I8, (uint64_t)-9), 0, PIN_I8, 0, -9, Z },
        { BF_DIV, cast_input(PIN_U64, 5), 0, PIN_U64, 0, 0, Z },
        { BF_CAST, cast_input(PIN_I64, UINT64_C(1) << 40), 0, PIN_I32, E, INT32_MAX, C },
        { BF_CAST, cast_input(PIN_I64, (uint64_t)-(INT64_C(1) << 40)), 0, PIN_I32, E, INT32_MIN, C },
        { BF_CAST, cast_input(PIN_I32, (uint64_t)-1), 0, PIN_U64, E, 0, C },
        { BF_CAST, cast_input(PIN_U64, 300), 0, PIN_U8, E, 255, C },
        { BF_CAST, cast_input(PIN_I64, (UINT64_C(1) << 40) + 5), 0, PIN_I32, W, 5, N },
        { BF_CAST, cast_real(PIN_F64, NAN), 0, PIN_I32, E, 0, C },
        { BF_CAST, cast_real(PIN_F64, -129), 0, PIN_I8, E, INT8_MIN, C },
        { BF_CAST, cast_real(PIN_F64, 1e10), 0, PIN_U16, E, UINT16_MAX, C },
        { BF_CAST, cast_real(PIN_F64, -128.9), 0, PIN_I8, E, INT8_MIN, N },
        { BF_CAST, cast_real(PIN_F32, 2.75), 0, PIN_I32, E, 2, N },
    };
    for (size_t v = 0; v < sizeof cases / sizeof *cases; v++) {
        cleanup();
        pin_type from = cases[v].left.type, to = cases[v].to;
        behavior_function_op op = cases[v].op;
        int count = op == BF_NEG || op == BF_ABS || op == BF_CAST ? 1 : 2;
        REQUIRE(reporting_operation(op, from, to, cases[v].mode));
        pin_value arguments[2] = { cases[v].left },
                  expected = behavior_scalar_from_bits(to, (uint64_t)cases[v].expected);
        if (count == 2) {
            arguments[1] = behavior_scalar_from_bits(from, (uint64_t)cases[v].right);
        }
        for (int backend = 0; backend < 2; backend++) {
            pin_value result = { .type = to };
            int flow = run(arguments, count, NULL, &result, backend == 1, 16);
            int kind = reported_kind();
            if (flow != FLOW_OK || kind != cases[v].kind ||
                behavior_scalar_bits(result) != behavior_scalar_bits(expected)) {
                fprintf(stderr, "literal integer case %zu backend %d: flow %d kind %d\n", v, backend, flow, kind);
            }
            REQUIRE(flow == FLOW_OK && kind == cases[v].kind);
            REQUIRE(behavior_scalar_bits(result) == behavior_scalar_bits(expected));
        }
    }
    cleanup();
    return true;
}

/* Address arithmetic belongs to the runtime and never reports. */
TEST_CASE(execution_integer_reports_skip_address_arithmetic)
{
    REQUIRE(reporting_operation(BF_ADD, BF_POINTER_TYPE, BF_POINTER_TYPE, 0));
    const behavior_function_instruction *add = &function->instructions[2];
    REQUIRE(add->op == BF_ADD && behavior_integer_reportable(function, add));
    function->slot_kinds[add->left] = BEHAVIOR_ADDRESS;
    REQUIRE(!behavior_integer_reportable(function, add));
    cleanup();
    return true;
}

/* The guarded fast body shares the same operation across sites, so a body
 * with report sites drops it, and each site reports on its own. */
TEST_CASE(execution_integer_reports_run_every_site_without_a_fast_body)
{
    pin_value variables[3] = { { .type = PIN_I32 },
                               { .type = PIN_I32 },
                               { .type = PIN_I32 } };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, NULL, 0, variables, 3);
    /* An inlined child frame, as in the fast shift case above. */
    behavior_function *draft = (behavior_function *)
        behavior_function_builder_definition(builder);
    apush(
        draft->frames,
        ((behavior_function_frame){ .source = { .parent = 0 }, .depth = 1, .stack = -1 })
    );
    int x = slot(PIN_I32), one = slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_CHECK });
    emit((
        behavior_function_instruction
    ){ .op = BF_VARIABLE, .type = PIN_I32, .output = x, .left = 0 });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .output = one,
                                          .value = { .type = PIN_I32, .i = 1 } });
    emit((behavior_function_instruction){ .op = BF_CHECK, .frame = 1 });
    for (int site = 1; site <= 2; site++) {
        int source = behavior_function_builder_origin(
            builder,
            (
                behavior_function_source
            ){ .asset_name = "integers.chibi", .parent = -1, .line = site, .column = 5 }
        );
        int sum = slot(PIN_I32);
        emit((behavior_function_instruction){ .op = BF_ADD,
                                              .type = PIN_I32,
                                              .output = sum,
                                              .left = x,
                                              .right = one,
                                              .frame = 1,
                                              .source = source });
        emit((
            behavior_function_instruction
        ){ .op = BF_STORE, .type = PIN_I32, .left = sum, .target = site });
    }
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(function->fast);
    int adds = 0;
    for (int pc = 0; pc < acount(function->fast->instructions); pc++) {
        adds += function->fast->instructions[pc].op == BF_ADD;
    }
    REQUIRE(adds == 1);
    behavior_function_report_integers(function);
    REQUIRE(!function->fast);
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        variables[0].i = INT32_MAX;
        REQUIRE(run(NULL, 0, variables, NULL, backend == 1, 16) == FLOW_OK);
        REQUIRE(variables[1].i == INT32_MIN && variables[2].i == INT32_MIN);
        int marked = 0;
        for (int site = 0;
             site < function->integer_report_sites * EXECUTION_INTEGER_REPORT_KINDS;
             site++) {
            if (atomic_exchange(&function->integer_reports[site], 0)) {
                REQUIRE(site % EXECUTION_INTEGER_REPORT_KINDS == EXECUTION_INTEGER_OVERFLOW);
                marked++;
            }
        }
        REQUIRE(marked == 2);
    }
    cleanup();
    return true;
}

TEST_CASE(execution_string_equality_and_iteration_use_full_bytes)
{
    pin_value args[] = { { .type = PIN_STR }, { .type = PIN_STR } };
    pin_value returned = { .type = PIN_BOOL };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, args, 2, &returned, 1, NULL, 0);
    int x = slot(PIN_STR), y = slot(PIN_STR), same = slot(PIN_BOOL);
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_STR, .left = 0, .output = x, .value = args[0] });
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_STR, .left = 1, .output = y, .value = args[1] });
    emit((
        behavior_function_instruction
    ){ .op = BF_STRING_EQUAL, .type = PIN_BOOL, .left = x, .right = y, .output = same });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = same });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish() && behavior_function_compile(function));
    const char first[] = { 'A', 0, 'B', (char)0xff, 0 };
    const char second[] = { 'A', 0, 'C', (char)0xff, 0 };
    pin_string texts[] = { { .bytes = first, .length = sizeof first - 1 },
                           { .bytes = second, .length = sizeof second - 1 },
                           { .bytes = first, .length = 1 },
                           { .bytes = first, .length = sizeof first - 1 } };
    const bool expected[] = { true, false, false, true };
    for (int backend = 0; backend < 2; backend++) {
        args[0].text = &texts[0];
        for (int n = 0; n < 4; n++) {
            args[1].text = &texts[n];
            pin_value result = returned;
            REQUIRE(run(args, 2, NULL, &result, backend == 1, 100) == FLOW_OK);
            REQUIRE(result.b == expected[n]);
        }
    }
    pin_value cursor = { 0 };
    REQUIRE(!EXECUTION_string_cursor_call.value(&managed, args, 1, &cursor));
    pin_value step[] = { cursor, { .type = PIN_I32, .i = 1 } }, item = { 0 };
    REQUIRE(!EXECUTION_string_item_call.value(&managed, step, 2, &item));
    REQUIRE(item.type == PIN_STR && pin_text(item).length == 1 && pin_text(item).bytes[0] == 0);
    REQUIRE(!EXECUTION_string_next_call.value(&managed, step, 2, &item) && item.i == 2);
    step[1].i = 3;
    REQUIRE(EXECUTION_string_item_call.value(&managed, step, 2, &item));
    return true;
}

TEST_CASE(execution_native_string_fields_validate_before_publication)
{
    char source[] = "before";
    fixture_c_text native = { source };
    pin_value root = { .type = PIN_STRUCT, .type_name = "fixture.c_text", .ptr = &native };
    behavior_function_context owner = { .value_storage = &managed };
    pin_value reference = execution_reference_value(&managed, &owner, &root, root);
    pin_value field_args[] = { reference,
                               { .type = PIN_STR },
                               { .type = PIN_U64 },
                               { .type = PIN_BOOL } };
    pin_value field = { 0 };
    REQUIRE(!EXECUTION_reference_field_call.value(&managed, field_args, 4, &field));
    pin_value read = { .type = PIN_STR };
    REQUIRE(!EXECUTION_reference_read_call.value(&managed, &field, 1, &read));
    memset(source, 'x', sizeof source - 1);
    REQUIRE(pin_text(read).length == 6 && !memcmp(pin_text(read).bytes, "before", 6));
    const char binary[] = { 'A', 0, 'B', 0 };
    pin_string descriptor = { .bytes = binary, .length = 3 };
    pin_value writes[] = { field, { .type = PIN_STR, .text = &descriptor } }, result;
    void *previous = root.ptr;
    REQUIRE(EXECUTION_reference_write_call.value(&managed, writes, 2, &result));
    REQUIRE(root.ptr == previous && native.text == source && !strcmp(source, "xxxxxx"));
    pin_value construct[] = { { .type = PIN_STRUCT, .type_name = "fixture.c_text" },
                              { .type = PIN_U64 },
                              writes[1] };
    result = (pin_value){ .type = PIN_I32, .i = 77 };
    REQUIRE(EXECUTION_construct_call.value(&managed, construct, 3, &result));
    REQUIRE(result.type == PIN_I32 && result.i == 77);
    descriptor = (pin_string){ .bytes = "after", .length = 5 };
    /* A valid write then lands in the referenced value itself. */
    REQUIRE(!EXECUTION_reference_write_call.value(&managed, writes, 2, &result));
    REQUIRE(root.ptr == previous && !strcmp(native.text, "after"));
    construct[2] = writes[1];
    REQUIRE(!EXECUTION_construct_call.value(&managed, construct, 3, &result));
    REQUIRE(result.type == PIN_STRUCT && !strcmp(((fixture_c_text *)result.ptr)->text, "after"));
    pin_value named = { .type = PIN_NAME, .str = "stable" };
    REQUIRE(!EXECUTION_snapshot_call.value(&managed, &writes[1], 1, &named));
    REQUIRE(named.type == PIN_NAME && !strcmp(named.str, "after"));
    const char *kept = named.str;
    descriptor = (pin_string){ .bytes = binary, .length = 3 };
    REQUIRE(EXECUTION_snapshot_call.value(&managed, &writes[1], 1, &named));
    REQUIRE(named.type == PIN_NAME && named.str == kept);
    pin_value converted = { .type = PIN_STR };
    REQUIRE(!EXECUTION_snapshot_call.value(&managed, &named, 1, &converted));
    REQUIRE(pin_text(converted).length == 5 && !memcmp(pin_text(converted).bytes, "after", 5));
    return true;
}

/* Five fixed address pairs on two backends: ten calls, budget 100 each. */
TEST_CASE(execution_pointer_comparisons_use_location_identity)
{
    builder = behavior_function_builder_new();
    pin_value headers[] = { { .type = PIN_POINTER }, { .type = PIN_POINTER } };
    pin_value results[] = { { .type = PIN_BOOL }, { .type = PIN_BOOL } };
    behavior_function_builder_signature(builder, headers, 2, results, 2, NULL, 0);
    int left = slot(PIN_POINTER), right = slot(PIN_POINTER);
    int equal = slot(PIN_BOOL), different = slot(PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                          .type = PIN_POINTER,
                                          .left = 0,
                                          .output = left,
                                          .value = headers[0] });
    emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                          .type = PIN_POINTER,
                                          .left = 1,
                                          .output = right,
                                          .value = headers[1] });
    emit((
        behavior_function_instruction
    ){ .op = BF_EQ, .type = PIN_POINTER, .left = left, .right = right, .output = equal });
    emit((
        behavior_function_instruction
    ){ .op = BF_NE, .type = PIN_POINTER, .left = left, .right = right, .output = different });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_BOOL, .target = 0, .left = equal });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_BOOL, .target = 1, .left = different });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    behavior_managed_heap_init(&comparison_heap);
    type_field fields[] = {
        { .name = "first", .type = PIN_I32, .type_name = "int" },
        { .name = "second", .type = PIN_I32, .type_name = "int" },
    };
    const pin_record *record = pin_record_create(
        &comparison_heap.storage,
        "fixture.pointer_pair",
        434,
        fields,
        2
    );
    REQUIRE(record);
    int32_t pair[] = { 17, 23 };
    pin_value base = { .type = PIN_STRUCT, .type_name = record->type.name, .record = record };
    behavior_function_context owner = { .managed_heap = &comparison_heap };
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&owner }, base, base };
    make[2].ptr = pair;
    pin_value root = { .type = PIN_POINTER, .type_name = "^fixture.pointer_pair" },
              other = root;
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &root));
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &other));
    pin_value field[] = { root,
                          { .type = PIN_I32 },
                          { .type = PIN_U64, .u64 = record->type.fields[0].offset },
                          { .type = PIN_BOOL },
                          base };
    pin_value first = { .type = PIN_POINTER, .type_name = "^int" },
              same = first, next = first, separate = first;
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, field, 5, &first));
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, field, 5, &same));
    REQUIRE(first.ptr != same.ptr);
    field[2].u64 = record->type.fields[1].offset;
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, field, 5, &next));
    field[0] = other;
    field[2].u64 = record->type.fields[0].offset;
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, field, 5, &separate));
    const struct
    {
        pin_pointer *left, *right;
        bool equal;
    } cases[] = {
        { first.ptr, same.ptr, true },      { first.ptr, next.ptr, false },
        { first.ptr, separate.ptr, false }, { NULL, NULL, true },
        { first.ptr, NULL, false },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        pin_value args[] = { { .type = PIN_POINTER, .ptr = cases[i].left },
                             { .type = PIN_POINTER, .ptr = cases[i].right } };
        for (int backend = 0; backend < 2; backend++) {
            release_invocation();
            REQUIRE(invoke(args, 2, NULL, results, backend != 0, 100, &comparison_heap) == FLOW_OK);
            REQUIRE(results[0].b == cases[i].equal && results[1].b != cases[i].equal);
        }
    }
    return true;
}
static const char *count_native_boundary(void *, const pin_value *, pin_value *)
{
    host_calls++;
    return NULL;
}
/* Nine fixed union payloads and two backends, eighteen total invocations. */
TEST_CASE(execution_native_boundary_rejects_hidden_managed_pointers)
{
    for (int variant = 0; variant < 9; variant++) {
        cleanup();
        pin_value payload = { .type = variant ? PIN_POINTER : PIN_I32 };
        pin_array array = { .element_type = PIN_POINTER,
                            .count = 1,
                            .capacity = 1,
                            .items = &payload };
        pin_value nested = variant == 2
                             ? (pin_value){ .type = PIN_POINTER,
                                            .container = PIN_CONTAINER_ARRAY,
                                            .ptr = &array }
                             : payload;
        int32_t plain_rows[] = { 3, 7 };
        pin_pointer *pointer_rows[] = { NULL, NULL };
        if (variant >= 3 && variant < 5) {
            type_field field = { .name = "value",
                                 .type = variant == 3 ? PIN_I32 : PIN_POINTER,
                                 .type_name = variant == 3 ? NULL : "^int" };
            const pin_record *
                record = pin_record_create(&managed, "fixture.boundary_rows", 431, &field, 1);
            REQUIRE(record);
            array = (pin_array){ .element_type = PIN_STRUCT,
                                 .type_name = record->type.name,
                                 .record = record,
                                 .count = 2,
                                 .capacity = 2,
                                 .stride = record->type.size,
                                 .data = variant == 3 ? (void *)plain_rows
                                                      : (void *)pointer_rows };
            nested = (pin_value){ .type = PIN_STRUCT,
                                  .container = PIN_CONTAINER_ARRAY,
                                  .type_name = record->type.name,
                                  .record = record,
                                  .ptr = &array };
            REQUIRE(pin_array_valid(&array));
        }
        pin_value boxed[2];
        const pin_string text[] = { { .bytes = "first", .length = 5 },
                                    { .bytes = "second", .length = 6 } };
        const pin_string *text_rows[] = { &text[0], &text[1] };
        if (variant >= 5 && variant < 7) {
            const type_field field = { .name = "text",
                                       .type = PIN_STR,
                                       .text_descriptor = true };
            const pin_record *
                record = pin_record_create(&managed, "fixture.boundary_text", 432, &field, 1);
            REQUIRE(record);
            for (int i = 0; i < 2; i++) {
                boxed[i] = (pin_value){ .type = PIN_STRUCT,
                                        .type_name = record->type.name,
                                        .record = record,
                                        .ptr = &text_rows[i] };
            }
            array = (pin_array){ .element_type = PIN_STRUCT,
                                 .type_name = record->type.name,
                                 .record = record,
                                 .count = 2,
                                 .capacity = 2,
                                 .items = boxed };
            nested = (pin_value){ .type = PIN_STRUCT,
                                  .container = PIN_CONTAINER_ARRAY,
                                  .type_name = record->type.name,
                                  .record = record,
                                  .ptr = &array };
            REQUIRE(pin_array_valid(&array));
        }
        uint8_t mode[] = { 1, 2 };
        if (variant >= 7) {
            nested = (pin_value){ .type = PIN_STRUCT,
                                  .type_name = narrow_enum_type.name,
                                  .ptr = mode };
            if (variant == 8) {
                for (int i = 0; i < 2; i++) {
                    boxed[i] = nested;
                    boxed[i].ptr = &mode[i];
                }
                array = (pin_array){ .element_type = PIN_STRUCT,
                                     .type_name = narrow_enum_type.name,
                                     .count = 2,
                                     .capacity = 2,
                                     .items = boxed };
                nested.container = PIN_CONTAINER_ARRAY;
                nested.ptr = &array;
            }
        }
        bool rejected = variant == 1 || variant == 2 || variant == 4 || variant == 6;
        pin_value argument;
        REQUIRE(!pin_union_value(&managed, 1, &nested, 1, &argument));
        builder = behavior_function_builder_new();
        const node_host_call call = { .invoke = count_native_boundary };
        const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
        int host = behavior_function_builder_host(builder, &call, &access, &argument, 1, NULL, 0);
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        if (variant == 6) {
            /* A dynamic bank value can disagree with its array's declaration;
             * the native boundary must inspect that element's actual header. */
            pin_union *value = function
                                   ->data_values[function->host_calls[host].first]
                                   .initial.ptr;
            pin_array *actual = value->values[0].ptr;
            REQUIRE(actual && !actual->stride && actual->count == 2);
            actual->items[1] = (pin_value){ .type = PIN_POINTER, .type_name = "^int" };
        }
        REQUIRE(behavior_function_compile(function));
        for (int backend = 0; backend < 2; backend++) {
            host_calls = 0;
            int status = run(NULL, 0, NULL, NULL, backend != 0, 100);
            REQUIRE(rejected ? status != FLOW_OK : status == FLOW_OK);
            REQUIRE(host_calls == (rejected ? 0 : 1));
            if (rejected) {
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_DATA);
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, "Native calls cannot retain managed pointer arguments")
                );
            }
        }
    }
    return true;
}
/* Four malformed dynamic payloads, two instructions and two backends each.
 * They enter an otherwise valid bank after definition freeze, so the boundary
 * itself must reject before any callback or invalid memory access. */
TEST_CASE(execution_native_boundary_rejects_malformed_aggregate_storage)
{
    for (int variant = 0; variant < 4; variant++) {
        cleanup();
        pin_value payload = { .type = PIN_I32 }, argument;
        REQUIRE(!pin_union_value(&managed, 1, &payload, 1, &argument));
        builder = behavior_function_builder_new();
        const node_host_call call = { .invoke = count_native_boundary };
        const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
        int host = behavior_function_builder_host(builder, &call, &access, &argument, 1, NULL, 0);
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        pin_union *actual = function
                                ->data_values[function->host_calls[host].first]
                                .initial.ptr;
        pin_array malformed = { .element_type = variant ? PIN_I32 : PIN_POINTER,
                                .count = 1,
                                .capacity = 1,
                                .stride = variant ? sizeof(int32_t) : sizeof(void *) };
        type_field field = { .name = "value",
                             .type = variant == 2 ? PIN_POINTER : PIN_UNION,
                             .offset = variant == 2 ? SIZE_MAX : 0,
                             .size = variant == 2 ? sizeof(void *) : 1 };
        pin_record record = { .type = { .name = "fixture.invalid_boundary",
                                        .size = sizeof(void *),
                                        .fields = &field,
                                        .field_count = 1 },
                              .id = 433 };
        void *record_payload = NULL;
        actual->values[0] = variant < 2
                              ? (pin_value){ .type = PIN_I32,
                                             .container = PIN_CONTAINER_ARRAY,
                                             .ptr = &malformed }
                              : (pin_value){ .type = PIN_STRUCT,
                                             .type_name = record.type.name,
                                             .record = &record,
                                             .ptr = &record_payload };
        REQUIRE(behavior_function_compile(function));
        for (int backend = 0; backend < 2; backend++) {
            host_calls = 0;
            REQUIRE(run(NULL, 0, NULL, NULL, backend != 0, 100) != FLOW_OK);
            REQUIRE(host_calls == 0 && context.failure == BEHAVIOR_FUNCTION_DATA);
            REQUIRE(
                context.error_message &&
                !strcmp(context.error_message, "Cannot retain language value")
            );
        }
    }
    return true;
}
static const char *rebound_kernel(arena *, const pin_value *, int, pin_value *results)
{
    host_calls++;
    results[0] = (pin_value){ .type = PIN_I32, .i = -1 };
    return NULL;
}
/* One pointer array, its own and a rebound callback, two backends each. A
 * borrowing kernel skips the argument walk only through its own callback; any
 * other callback is checked like a native call and fails before it runs. */
TEST_CASE(execution_rebound_borrowing_call_checks_its_arguments)
{
    cleanup();
    pin_value payload = { .type = PIN_POINTER };
    pin_array array = { .element_type = PIN_POINTER,
                        .count = 1,
                        .capacity = 1,
                        .items = &payload };
    pin_value argument = { .type = PIN_POINTER,
                           .container = PIN_CONTAINER_ARRAY,
                           .ptr = &array };
    pin_value output = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    int host = behavior_function_builder_host(
        builder,
        &NATIVE_ARRAY_length_CALL,
        &NATIVE_ARRAY_ACCESS,
        &argument,
        1,
        &output,
        1
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    int result = function->host_calls[host].result_first;
    for (int rebound = 0; rebound < 2; rebound++) {
        for (int backend = 0; backend < 2; backend++) {
            release_invocation();
            host_calls = 0;
            context = (behavior_function_context){ .budget = 100 };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(function, &context, NULL, 0));
            behavior_function_prepare_data(function, NULL, &context.data);
            context.owned_data = true;
            if (rebound) {
                context.data->calls[host].value = rebound_kernel;
            }
            int status = behavior_function_run(function, &context, backend != 0);
            if (!rebound) {
                REQUIRE(status == FLOW_OK && context.data->values[result].i == 1);
                continue;
            }
            REQUIRE(
                status != FLOW_OK && host_calls == 0 &&
                context.data->values[result].i == 0
            );
            REQUIRE(context.failure == BEHAVIOR_FUNCTION_DATA);
            REQUIRE(
                context.error_message &&
                !strcmp(context.error_message, "Native calls cannot retain managed pointer arguments")
            );
        }
    }
    return true;
}
/* Two malformed arguments to one borrowing kernel: a top-level array and one
 * nested in a boxed array's union. Bank values enter at definition freeze,
 * which validates their whole storage, so the call never runs on them. */
TEST_CASE(execution_borrowing_call_arguments_validate_at_definition)
{
    for (int variant = 0; variant < 2; variant++) {
        cleanup();
        int32_t rows[] = { 1, 2 };
        pin_array malformed = { .element_type = PIN_I32,
                                .count = 1,
                                .capacity = 1,
                                .stride = 2 * sizeof(int32_t),
                                .data = rows };
        pin_value array = { .type = PIN_I32,
                            .container = PIN_CONTAINER_ARRAY,
                            .ptr = &malformed };
        pin_value inner = { .type = PIN_I32 }, item;
        REQUIRE(!pin_union_value(&managed, 1, &inner, 1, &item));
        ((pin_union *)item.ptr)->values[0] = array;
        pin_array boxed = { .element_type = PIN_UNION,
                            .count = 1,
                            .capacity = 1,
                            .items = &item };
        pin_value argument = variant ? (pin_value){ .type = PIN_UNION,
                                                    .container = PIN_CONTAINER_ARRAY,
                                                    .ptr = &boxed }
                                     : array;
        pin_value output = { .type = PIN_I32 };
        builder = behavior_function_builder_new();
        behavior_function_builder_host(
            builder,
            &NATIVE_ARRAY_length_CALL,
            &NATIVE_ARRAY_ACCESS,
            &argument,
            1,
            &output,
            1
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = 0,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(!function && !strcmp(error, "function value ownership is invalid"));
    }
    return true;
}
/* Two fixed canonical intrinsic bodies; inspect metadata without executing. */
TEST_CASE(execution_managed_pointer_access_is_not_query_or_parallel_safe)
{
    const node_host_call *calls[] = { &EXECUTION_pointer_read_call,
                                      &EXECUTION_pointer_write_call };
    for (int i = 0; i < 2; i++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value args[] = { { .type = PIN_POINTER }, { .type = PIN_I32 } };
        pin_value output = { .type = i ? PIN_BOOL : PIN_I32 };
        int host = behavior_function_builder_host(
            builder,
            calls[i],
            &EXECUTION_value_access,
            args,
            2,
            &output,
            1
        );
        uint64_t id = execution_intrinsic_id(calls[i]);
        REQUIRE(
            behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(!execution_query_predicate_valid(function, 0));
        execution_module_function entry = { .id = 1,
                                            .kind = EXECUTION_FUNCTION_SYSTEM,
                                            .body = function };
        execution_module module = { .plan = { .functions = &entry,
                                              .function_count = 1 } };
        REQUIRE(execution_module_build_effects(&module));
        REQUIRE(!execution_module_effects(&module).parallel_safe);
        free(module.effect_ids);
        if (i) {
            execution_prepare_argument_sharing(function);
            /* The write copies a scalar into the pointee, which a lent
             * argument never holds. */
            REQUIRE(function->argument_sharing == BEHAVIOR_SHARING_SAFE);
            REQUIRE(!execution_builds_safely(function));
        }
    }
    return true;
}
/* Two flat record layouts share their first field's type and offset. Five
 * direct calls, one refused root and two bounded backend calls check layouts. */
TEST_CASE(execution_pointer_fields_require_the_full_base_layout)
{
    behavior_managed_heap_init(&pointer_heap);
    const type_field fields[] = { { .name = "x", .type = PIN_I32 },
                                  { .name = "tail", .type = PIN_I32 } };
    type_field changed[] = { fields[0], fields[1] };
    changed[1].type = PIN_F32;
    const pin_record *record = pin_record_create(&managed, "fixture.projected", 101, fields, 2);
    const pin_record *stale = pin_record_create(&managed, "fixture.projected", 101, changed, 2);
    REQUIRE(record && stale && record->layout != stale->layout);
    REQUIRE(record->type.fields[0].offset == stale->type.fields[0].offset);
    REQUIRE(record->type.fields[0].type == stale->type.fields[0].type);
    struct
    {
        int32_t x, tail;
    } value = { 17, 23 };
    pin_value base = { .type = PIN_STRUCT, .type_name = record->type.name, .record = record };
    behavior_function_context owner = { .managed_heap = &pointer_heap };
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&owner }, base, base };
    make[2].ptr = &value;
    pointer_root = (pin_value){ .type = PIN_POINTER, .type_name = "^fixture.projected" };
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &pointer_root));
    pin_value field[] = { pointer_root,
                          { .type = PIN_I32 },
                          { .type = PIN_U64, .u64 = record->type.fields[0].offset },
                          { .type = PIN_BOOL },
                          base };
    pin_value projected = { .type = PIN_POINTER, .type_name = "^int" };
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, field, 5, &projected));
    pin_value read[] = { projected, { .type = PIN_I32 } },
              result = { .type = PIN_I32 };
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &result) && result.i == 17);
    field[4].record = stale;
    size_t before, capacity, after;
    arena_usage(&pointer_heap.storage, &before, &capacity);
    void *previous = projected.ptr;
    const char *error = EXECUTION_pointer_field_call.value(&managed, field, 5, &projected);
    REQUIRE(error && !strcmp(error, "Managed pointer base differs from its expected record layout"));
    arena_usage(&pointer_heap.storage, &after, &capacity);
    REQUIRE(projected.ptr == previous && before == after);
    builder = behavior_function_builder_new();
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_pointer_field_call,
        &EXECUTION_value_access,
        field,
        5,
        &projected,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_pointer_field_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        release_invocation();
        int status = invoke(NULL, 0, NULL, NULL, backend != 0, 100, &pointer_heap);
        if (context.failure != BEHAVIOR_FUNCTION_HOST_DATA) {
            fprintf(
                stderr,
                "pointer projection backend=%d status=%d failure=%d "
                "message=%s\n",
                backend,
                status,
                context.failure,
                context.error_message ? context.error_message : ""
            );
        }
        REQUIRE(status != FLOW_OK);
        REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
        REQUIRE(
            context.error_message &&
            !strcmp(context.error_message, "Managed pointer base differs from its expected record layout")
        );
    }
    pin_value native = { .type = PIN_STRUCT, .type_name = "NativeOnly" };
    make[1] = make[2] = native;
    previous = projected.ptr;
    error = EXECUTION_pointer_make_call.value(&managed, make, 3, &projected);
    REQUIRE(error && !strcmp(error, "Managed value requires a resolved record layout"));
    REQUIRE(projected.ptr == previous);
    error = behavior_managed_heap_root(&pointer_heap, &native);
    REQUIRE(error && !strcmp(error, "Managed value requires a resolved record layout"));
    REQUIRE(native.type == PIN_STRUCT && !native.ptr && !native.record);
    REQUIRE(!strcmp(native.type_name, "NativeOnly") && !acount(pointer_heap.roots));
    return true;
}
/* A scan of a record of scalars still checks its nested layouts: one that
 * does not resolve is an error, and a new value's nested native layout still
 * has its freeze checked. */
TEST_CASE(execution_managed_scan_checks_nested_layouts)
{
    type_field unnamed = { .name = "", .type = PIN_I32, .size = sizeof(int) };
    type_desc child = { .name = "ScanChild",
                        .size = sizeof(int),
                        .fields = &unnamed,
                        .field_count = 1 };
    type_field inner = { .name = "inner",
                         .type = PIN_STRUCT,
                         .type_name = "ScanMissing",
                         .size = sizeof(int) };
    pin_record outer = { .type = { .name = "ScanOuter",
                                   .size = sizeof(int),
                                   .fields = &inner,
                                   .field_count = 1 } };
    int bytes = 0;
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = "ScanOuter",
                        .ptr = &bytes,
                        .record = &outer };
    const char *error = behavior_managed_value_error(&value);
    REQUIRE(error && !strcmp(error, "Managed value requires a resolved nested record layout"));
    const type_desc *types[] = { &child };
    type_catalog_pack project = { .types = types, .type_count = 1 };
    type_catalog_set_project(&project);
    inner.layout = &child;
    error = behavior_managed_value_error(&value);
    type_catalog_set_project(NULL);
    REQUIRE(error && !strcmp(error, "Cannot retain native record layout"));
    return true;
}
/* Two heaps, one cell each, five direct intrinsic calls and one refused root. */
TEST_CASE(execution_pointer_owners_reject_foreign_cells_without_mutation)
{
    behavior_managed_heap_init(&pointer_heap);
    behavior_managed_heap_init(&secondary_pointer_heap);
    behavior_function_context first = { .managed_heap = &pointer_heap };
    behavior_function_context second = { .managed_heap = &secondary_pointer_heap };
    const pin_value header = { .type = PIN_POINTER, .type_name = "fixture.owner" };
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&first },
                         header,
                         header };
    pin_value local = header, foreign = header, output = header;
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &local));
    make[0].u64 = (uintptr_t)&second;
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &foreign));
    pin_value rooted = foreign;
    const char *error = behavior_managed_heap_root(&pointer_heap, &rooted);
    REQUIRE(error && !strcmp(error, "Managed pointers cannot cross heap owners"));
    REQUIRE(rooted.ptr == foreign.ptr && !acount(pointer_heap.roots));
    make[0].u64 = (uintptr_t)&first;
    make[2] = foreign;
    output.ptr = local.ptr;
    error = EXECUTION_pointer_make_call.value(&managed, make, 3, &output);
    REQUIRE(error && !strcmp(error, "Managed pointers cannot cross heap owners"));
    REQUIRE(output.ptr == local.ptr);
    pin_value write[] = { local, foreign }, written = { 0 };
    error = EXECUTION_pointer_write_call.value(&managed, write, 2, &written);
    REQUIRE(error && !strcmp(error, "Managed pointers cannot cross heap owners"));
    REQUIRE(written.type == PIN_BOOL && !written.b);
    pin_value read[] = { local, header };
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &output));
    REQUIRE(output.type == PIN_POINTER && !output.ptr);
    return true;
}
/* A query one heap owns is refused as another heap's root, which keeps
 * no root. */
TEST_CASE(execution_query_owners_reject_foreign_heaps_without_mutation)
{
    behavior_managed_heap_init(&pointer_heap);
    behavior_managed_heap_init(&secondary_pointer_heap);
    pin_query *query = arena_alloc(&pointer_heap.storage, sizeof *query);
    *query = (pin_query){ .kind = PIN_QUERY_DESCRIPTION,
                          .storage = &pointer_heap.storage };
    pin_value value = { .type = PIN_QUERY, .type_name = "fixture.query", .ptr = query };
    const char *error = behavior_managed_heap_root(&secondary_pointer_heap, &value);
    REQUIRE(error && !strcmp(error, "Queries cannot cross heap owners"));
    REQUIRE(value.ptr == query && !acount(secondary_pointer_heap.roots));
    REQUIRE(
        !behavior_managed_heap_root(&pointer_heap, &value) &&
        acount(pointer_heap.roots) == 1
    );
    behavior_managed_heap_unroot(&pointer_heap, &value);
    return true;
}
/* Three cyclic cells and one final scalar cell. Four quiescent collections,
 * one registered-header reassignment, then heap destruction; no unbounded walks. */
TEST_CASE(execution_managed_heap_preserves_roots_and_reclaims_cycles)
{
    behavior_managed_heap_init(&pointer_heap);
    behavior_function_context owner = { .managed_heap = &pointer_heap };
    const pin_value link_type = { .type = PIN_POINTER,
                                  .type_name = "fixture.cyclic_pointer" };
    pin_value make[] = {
        { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&owner },
        link_type,
        link_type,
    };
    pointer_root = link_type;
    pin_value other = link_type, self = link_type;
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &pointer_root));
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &other));
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &self));
    REQUIRE(pointer_root.ptr && other.ptr && self.ptr);
    REQUIRE(
        pointer_root.ptr != other.ptr && other.ptr != self.ptr &&
        pointer_root.ptr != self.ptr
    );
    pin_value write[] = { pointer_root, other }, written = { 0 };
    REQUIRE(
        !EXECUTION_pointer_write_call.value(&managed, write, 2, &written) &&
        written.b
    );
    write[0] = other;
    write[1] = pointer_root;
    REQUIRE(
        !EXECUTION_pointer_write_call.value(&managed, write, 2, &written) &&
        written.b
    );
    write[0] = self;
    write[1] = self;
    REQUIRE(
        !EXECUTION_pointer_write_call.value(&managed, write, 2, &written) &&
        written.b
    );
    REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
    pointer_root_registered = true;
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    REQUIRE(arena_contains(&pointer_heap.storage, pointer_root.ptr));
    REQUIRE(arena_contains(&pointer_heap.storage, other.ptr));
    REQUIRE(!arena_contains(&pointer_heap.storage, self.ptr));
    pin_value read[] = { pointer_root, link_type }, next = link_type;
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &next));
    REQUIRE(next.ptr == other.ptr);
    read[0] = next;
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &next));
    REQUIRE(next.ptr == pointer_root.ptr);
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    REQUIRE(arena_contains(&pointer_heap.storage, other.ptr));
    void *unrooted = pointer_root.ptr;
    behavior_managed_heap_unroot(&pointer_heap, &pointer_root);
    pointer_root_registered = false;
    REQUIRE(!pointer_root.ptr && !pointer_root.type);
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    size_t bytes, capacity;
    arena_usage(&pointer_heap.storage, &bytes, &capacity);
    REQUIRE(bytes == 0);
    REQUIRE(!arena_contains(&pointer_heap.storage, unrooted));
    REQUIRE(!arena_contains(&pointer_heap.storage, other.ptr));
    make[1] = (pin_value){ .type = PIN_I32 };
    make[2] = (pin_value){ .type = PIN_I32, .i = 17 };
    REQUIRE(!EXECUTION_pointer_make_call.value(&managed, make, 3, &pointer_root));
    char first_label[] = "^fixture.first_int";
    char second_label[] = "^fixture.second_int";
    pointer_root.type_name = first_label;
    REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
    pointer_root_registered = true;
    void *cell = pointer_root.ptr;
    pointer_root.type_name = second_label;
    REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
    memset(first_label, 'x', sizeof first_label - 1);
    memset(second_label, 'y', sizeof second_label - 1);
    REQUIRE(acount(pointer_heap.roots) == 1);
    REQUIRE(!strcmp(pointer_root.type_name, "^fixture.second_int"));
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    REQUIRE(pointer_root.ptr == cell && arena_contains(&pointer_heap.storage, cell));
    REQUIRE(!strcmp(pointer_root.type_name, "^fixture.second_int"));
    behavior_managed_heap_unroot(&pointer_heap, &pointer_root);
    pointer_root_registered = false;
    REQUIRE(!acount(pointer_heap.roots));
    REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
    pointer_root_registered = true;
    behavior_managed_heap_free(&pointer_heap);
    pointer_root_registered = false;
    REQUIRE(pointer_root.type == PIN_FLOW && !pointer_root.ptr);
    return true;
}
typedef struct
{
    const char *entry;
    int expected;
} sequence_case;
static bool sequence_results(const char *source, const sequence_case *cases, int count)
{
    source_package_input input = { "sequences/Main.chibi", source, strlen(source) };
    REQUIRE(compiler_fixture_prepare(&sequences, &input, 1));
    for (int backend = 0; backend < 2; backend++) {
        for (int i = 0; i < count; i++) {
            pin_value result = { 0 };
            REQUIRE(compiler_fixture_call(
                &sequences,
                backend,
                "project:sequences",
                cases[i].entry,
                NULL,
                0,
                100000,
                &storage,
                &result
            ));
            if (result.type != PIN_I32 || result.i != cases[i].expected) {
                fprintf(
                    stderr,
                    "sequence %s backend=%d: expected %d, got type=%d "
                    "value=%d\n",
                    cases[i].entry,
                    backend,
                    cases[i].expected,
                    result.type,
                    result.i
                );
            }
            REQUIRE(result.type == PIN_I32 && result.i == cases[i].expected);
            arena_free(&storage);
        }
    }
    return true;
}
static const type_desc *owned_native_type(void *user, const char *name)
{
    const type_desc *const *types = user;
    for (int i = 0; i < 2; i++) {
        if (!strcmp(types[i]->name, name)) {
            return types[i];
        }
    }
    return NULL;
}
TEST_CASE(execution_native_schema_snapshots_keep_nested_text_after_sources_retire)
{
    char outer_name[] = "fixture.owned_outer",
         inner_name[] = "fixture.owned_inner";
    type_field inner_field = c_text_field;
    type_desc inner = { .name = inner_name,
                        .size = sizeof(fixture_c_text),
                        .fields = &inner_field,
                        .field_count = 1 };
    type_field outer_field = { .name = "inner",
                               .type = PIN_STRUCT,
                               .size = sizeof(fixture_c_text),
                               .type_name = inner_name };
    type_desc outer = { .name = outer_name,
                        .size = sizeof(fixture_c_text),
                        .fields = &outer_field,
                        .field_count = 1 };
    const type_desc *types[] = { &outer, &inner };
    const pin_schema *schema = pin_schema_create(
        &managed,
        &outer,
        (type_resolver){ .find = owned_native_type, .user = types }
    );
    REQUIRE(schema);
    char bytes[] = "kept";
    fixture_c_text native = { bytes };
    pin_value source = { .type = PIN_STRUCT,
                         .type_name = outer_name,
                         .schema = schema,
                         .ptr = &native };
    behavior_managed_heap_init(&pointer_heap);
    REQUIRE(!pin_value_snapshot_owned_to_arena(&pointer_heap.storage, &pointer_root, &source));
    REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
    pointer_root_registered = true;
    arena_reset(&managed);
    memset(outer_name, 'x', sizeof outer_name - 1);
    memset(inner_name, 'y', sizeof inner_name - 1);
    memset(bytes, 'z', sizeof bytes - 1);
    inner_field.type = PIN_I64;
    outer_field.offset = SIZE_MAX;
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    const type_desc *retained = pin_value_type(&pointer_root);
    REQUIRE(retained && !strcmp(retained->name, "fixture.owned_outer"));
    REQUIRE(retained->fields[0].offset == 0);
    const type_desc *nested = pin_schema_type(pointer_root.schema, "fixture.owned_inner");
    REQUIRE(nested && nested->fields[0].type == PIN_STR);
    REQUIRE(!strcmp(((fixture_c_text *)pointer_root.ptr)->text, "kept"));
    REQUIRE(!pin_value_managed_boundary_error(&pointer_root));
    pin_array packed = { .element_type = PIN_STRUCT,
                         .type_name = retained->name,
                         .schema = pointer_root.schema,
                         .items = &pointer_root,
                         .count = 1,
                         .capacity = 1 };
    pin_value array_argument = { .type = PIN_STRUCT,
                                 .container = PIN_CONTAINER_ARRAY,
                                 .type_name = retained->name,
                                 .schema = pointer_root.schema,
                                 .ptr = &packed };
    REQUIRE(!pin_value_managed_boundary_error(&array_argument));
    pin_value hidden = { .type = PIN_POINTER, .type_name = "^int" };
    pin_array boxed = packed;
    boxed.stride = 0;
    boxed.data = NULL;
    boxed.items = &hidden;
    array_argument.ptr = &boxed;
    REQUIRE(pin_value_managed_boundary_error(&array_argument));
    type_field scalar_field = { .name = "value", .type = PIN_I32, .size = sizeof(int32_t) };
    type_desc scalar_type = { .name = "fixture.boundary_inner",
                              .size = sizeof(int32_t),
                              .fields = &scalar_field,
                              .field_count = 1 };
    type_field nested_field = { .name = "nested",
                                .type = PIN_STRUCT,
                                .type_name = scalar_type.name,
                                .size = sizeof(int32_t) };
    type_desc nested_type = { .name = "fixture.boundary_outer",
                              .size = sizeof(int32_t),
                              .fields = &nested_field,
                              .field_count = 1 };
    const type_desc *plain_types[] = { &nested_type, &scalar_type };
    const pin_schema *plain_schema = pin_schema_create(
        &managed,
        &nested_type,
        (type_resolver){ .find = owned_native_type, .user = plain_types }
    );
    REQUIRE(plain_schema);
    scalar_field.type = PIN_POINTER;
    int32_t scalar = 42;
    pin_array plain = { .element_type = PIN_STRUCT,
                        .type_name = nested_type.name,
                        .schema = plain_schema,
                        .data = &scalar,
                        .stride = sizeof scalar,
                        .count = 1,
                        .capacity = 1 };
    array_argument.type_name = nested_type.name;
    array_argument.schema = plain_schema;
    array_argument.ptr = &plain;
    REQUIRE(!pin_value_managed_boundary_error(&array_argument));
    builder = behavior_function_builder_new();
    const node_host_call call = { .invoke = count_native_boundary };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int host = behavior_function_builder_host(builder, &call, &access, &pointer_root, 1, NULL, 0);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        host_calls = 0;
        REQUIRE(run(NULL, 0, NULL, NULL, backend != 0, 100) == FLOW_OK);
        REQUIRE(host_calls == 1);
    }
    release_invocation();
    pin_value member = { .type = PIN_STRUCT,
                         .type_name = nested->name,
                         .schema = pointer_root.schema,
                         .ptr = pointer_root.ptr },
              copy;
    REQUIRE(!pin_value_snapshot_to_arena(&managed, &copy, &member));
    REQUIRE(pin_value_type(&copy) && !strcmp(((fixture_c_text *)copy.ptr)->text, "kept"));
    behavior_managed_heap_unroot(&pointer_heap, &pointer_root);
    pointer_root_registered = false;
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    size_t used, capacity;
    arena_usage(&pointer_heap.storage, &used, &capacity);
    REQUIRE(!used);
    return true;
}

TEST_CASE(execution_component_pointers_keep_identity_and_invalidate_removed_targets)
{
    pointer_world = ecs_world_new();
    ecs_component_id id = 9191;
    ecs_component_desc component = { .id = id,
                                     .key = "fixture.box",
                                     .name = "Box",
                                     .element_size = sizeof(fixture_box),
                                     .type = &box_type };
    REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(pointer_world);
    fixture_box initial = { 10 };
    REQUIRE(ecs_world_record_add(pointer_world, entity, id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    REQUIRE(ecs_component_track_changes(pointer_world, id) == ECS_OK);
    behavior_managed_heap_init(&pointer_heap);
    behavior_function_context owner = { .managed_heap = &pointer_heap,
                                        .world = ecs_world_execution(pointer_world) };
    pin_value type = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&owner },
                         { .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(entity) },
                         { .type = PIN_U64, .u64 = id },
                         type,
                         { .type = PIN_U64, .u64 = ecs_layout_hash(&box_type) },
                         { .type = PIN_U64, .u64 = sizeof(fixture_box) } };
    pointer_root = (pin_value){ .type = PIN_POINTER, .type_name = "^fixture.box" };
    make[4].u64++;
    REQUIRE(EXECUTION_component_pointer_call.value(&managed, make, 6, &pointer_root));
    REQUIRE(!pointer_root.ptr);
    make[4].u64--;
    make[5].u64++;
    REQUIRE(EXECUTION_component_pointer_call.value(&managed, make, 6, &pointer_root));
    REQUIRE(!pointer_root.ptr);
    make[5].u64--;
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &pointer_root));
    REQUIRE(pointer_root.ptr);
    REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
    pointer_root_registered = true;
    pin_value alias = pointer_root;
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &alias));
    REQUIRE(pin_pointer_equal(pointer_root.ptr, alias.ptr));
    ecs_component_cell *retained = ((pin_pointer *)pointer_root.ptr)->component;
    ecs_component_cell_retain(retained);
    REQUIRE(ecs_component_cell_alive(retained));
    pin_value project[] = { pointer_root,
                            { .type = PIN_I32 },
                            { .type = PIN_U64, .u64 = offsetof(fixture_box, value) },
                            { .type = PIN_BOOL },
                            type };
    pin_value field = { .type = PIN_POINTER }, result = { .type = PIN_I32 };
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, project, 5, &field));
    for (int i = 0; i < 128; i++) {
        ecs_entity other = ecs_world_create_entity(pointer_world);
        REQUIRE(ecs_world_record_add(pointer_world, other, id, &initial, 0) == ECS_OK);
    }
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    ((fixture_box *)ecs_component_data(pointer_world, entity, id))->value = 12;
    REQUIRE(ecs_component_cell_alive(retained));
    pin_value read[] = { field, { .type = PIN_I32 } };
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    REQUIRE(result.i == 12);
    uint64_t before = ecs_component_version(pointer_world, entity, id);
    pin_value write[] = { field, { .type = PIN_I32, .i = 15 } }, written = { 0 };
    REQUIRE(!EXECUTION_pointer_write_call.value(&managed, write, 2, &written));
    REQUIRE(((fixture_box *)ecs_component_data(pointer_world, entity, id))->value == 15);
    REQUIRE(ecs_component_version(pointer_world, entity, id) > before);
    REQUIRE(ecs_world_record_remove(pointer_world, entity, id, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    REQUIRE(!ecs_component_cell_alive(retained));
    const char *error = EXECUTION_pointer_read_call.value(&managed, read, 2, &result);
    REQUIRE(error && !strcmp(error, "Component reference target is no longer available"));
    REQUIRE(EXECUTION_pointer_write_call.value(&managed, write, 2, &written));
    initial.value = 100;
    REQUIRE(ecs_world_record_add(pointer_world, entity, id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    pin_value replacement = pointer_root;
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &replacement));
    REQUIRE(!pin_pointer_equal(pointer_root.ptr, replacement.ptr));
    REQUIRE(!ecs_component_cell_alive(retained));
    write[1].i = 20;
    REQUIRE(EXECUTION_pointer_write_call.value(&managed, write, 2, &written));
    REQUIRE(((fixture_box *)ecs_component_data(pointer_world, entity, id))->value == 100);
    project[0] = replacement;
    pin_value replacement_field = { .type = PIN_POINTER };
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, project, 5, &replacement_field));
    write[0] = replacement_field;
    REQUIRE(!EXECUTION_pointer_write_call.value(&managed, write, 2, &written));
    REQUIRE(((fixture_box *)ecs_component_data(pointer_world, entity, id))->value == 20);
    pointer_root = field;
    REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
    ecs_world_free(pointer_world);
    pointer_world = NULL;
    REQUIRE(!ecs_component_cell_alive(retained));
    REQUIRE(EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    pin_value whole_read[] = { replacement, type }, whole = type;
    error = EXECUTION_pointer_read_call.value(&managed, whole_read, 2, &whole);
    REQUIRE(error && !strcmp(error, "Component reference target is no longer available"));
    REQUIRE(EXECUTION_pointer_write_call.value(&managed, write, 2, &written));
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    REQUIRE(arena_contains(&pointer_heap.storage, pointer_root.ptr));
    REQUIRE(!arena_contains(&pointer_heap.storage, replacement.ptr));
    behavior_managed_heap_unroot(&pointer_heap, &pointer_root);
    pointer_root_registered = false;
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    size_t bytes, capacity;
    arena_usage(&pointer_heap.storage, &bytes, &capacity);
    REQUIRE(bytes == 0);
    REQUIRE(!ecs_component_cell_alive(retained));
    ecs_component_cell_release(retained);
    return true;
}

TEST_CASE(execution_component_targets_do_not_revive_after_lookup_loss_or_entity_reuse)
{
    pointer_world = ecs_world_new();
    ecs_component_desc component = { .id = 9198,
                                     .key = "fixture.box",
                                     .name = "Box",
                                     .element_size = sizeof(fixture_box),
                                     .type = &box_type };
    REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(pointer_world);
    fixture_box value = { 29 };
    REQUIRE(ecs_world_record_add(pointer_world, entity, component.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    behavior_managed_heap_init(&pointer_heap);
    behavior_function_context owner = { .managed_heap = &pointer_heap,
                                        .world = ecs_world_execution(pointer_world) };
    pin_value type = { .type = PIN_STRUCT, .type_name = box_type.name };
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&owner },
                         { .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(entity) },
                         { .type = PIN_U64, .u64 = component.id },
                         type,
                         { .type = PIN_U64, .u64 = ecs_layout_hash(&box_type) },
                         { .type = PIN_U64, .u64 = sizeof(fixture_box) } };
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &pointer_root));
    pin_value read[] = { pointer_root, type }, result = type;
    ecs_execution_column *column = (ecs_execution_column *)
        ecs_execution_find_column(owner.world, component.id);
    void *data = column->data;
    column->data = NULL;
    const char *error = EXECUTION_pointer_read_call.value(&managed, read, 2, &result);
    column->data = data;
    REQUIRE(error && !strcmp(error, "Component reference target is no longer available"));
    REQUIRE(EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    pin_value replacement = { .type = PIN_POINTER };
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &replacement));
    REQUIRE(!pin_pointer_equal(pointer_root.ptr, replacement.ptr));
    read[0] = replacement;
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    REQUIRE(((fixture_box *)result.ptr)->value == 29);
    REQUIRE(ecs_world_record_destroy(pointer_world, entity, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    REQUIRE(EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    ecs_entity reused = ecs_world_create_entity(pointer_world);
    REQUIRE(reused.slot == entity.slot && reused.generation != entity.generation);
    value.value = 41;
    REQUIRE(ecs_world_record_add(pointer_world, reused, component.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    REQUIRE(EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    make[1].u64 = ecs_entity_to_u64(reused);
    pin_value current = { .type = PIN_POINTER };
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &current));
    REQUIRE(!pin_pointer_equal(current.ptr, replacement.ptr));
    read[0] = current;
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    REQUIRE(((fixture_box *)result.ptr)->value == 41);
    return true;
}

TEST_CASE(execution_component_references_reject_unavailable_write_paths)
{
    pointer_world = ecs_world_new();
    ecs_component_desc component = { .id = 9196,
                                     .key = "fixture.box",
                                     .name = "Box",
                                     .element_size = sizeof(fixture_box),
                                     .type = &box_type };
    REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(pointer_world);
    fixture_box initial = { 23 };
    REQUIRE(ecs_world_record_add(pointer_world, entity, component.id, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    behavior_managed_heap_init(&pointer_heap);
    behavior_function_context owner = { .managed_heap = &pointer_heap,
                                        .world = ecs_world_execution(pointer_world) };
    pin_value type = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&owner },
                         { .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(entity) },
                         { .type = PIN_U64, .u64 = component.id },
                         type,
                         { .type = PIN_U64, .u64 = ecs_layout_hash(&box_type) },
                         { .type = PIN_U64, .u64 = sizeof(fixture_box) } };
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &pointer_root));
    pin_value reference = { .type = BF_POINTER_TYPE };
    REQUIRE(!EXECUTION_pointer_reference_call.value(&managed, &pointer_root, 1, &reference));
    pin_value field_args[] = { reference,
                               { .type = PIN_I32 },
                               { .type = PIN_U64, .u64 = sizeof(fixture_box) },
                               { .type = PIN_BOOL } };
    pin_value unavailable = { .type = BF_POINTER_TYPE };
    /* Logical reference paths are checked at use; this deliberately bypasses
     * the validated managed pointer projection intrinsic. */
    REQUIRE(!EXECUTION_reference_field_call.value(&managed, field_args, 4, &unavailable));
    builder = behavior_function_builder_new();
    pin_value argument = { .type = BF_POINTER_TYPE };
    behavior_function_builder_signature(builder, &argument, 1, NULL, 0, NULL, 0);
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_ARGUMENT, 0, BEHAVIOR_REFERENCE));
    int input = slot(BF_POINTER_TYPE), address = address_slot();
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, input, BEHAVIOR_REFERENCE));
    emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                          .type = BF_POINTER_TYPE,
                                          .output = input,
                                          .value = argument });
    pin_value writes[] = { argument, { .type = PIN_I32, .i = 99 } };
    pin_value output = { .type = PIN_BOOL };
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_reference_write_call,
        &EXECUTION_assignment_access,
        writes,
        2,
        &output,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_reference_write_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    int first = behavior_function_builder_host_first(builder, host);
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first, BEHAVIOR_REFERENCE));
    emit((behavior_function_instruction){ .op = BF_HOST_SLOT,
                                          .type = BF_POINTER_TYPE,
                                          .left = first,
                                          .output = address });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = BF_POINTER_TYPE,
                                          .left = input,
                                          .right = address,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish() && behavior_function_compile(function));
    for (int invalidated = 0; invalidated < 2; invalidated++) {
        if (invalidated) {
            REQUIRE(ecs_world_record_remove(pointer_world, entity, component.id, 0) == ECS_OK);
            REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
        }
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(run(&unavailable, 1, NULL, NULL, backend != 0, 100) == FLOW_ERR_INVALID_GRAPH);
            REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
            REQUIRE(
                context.error_message &&
                !strcmp(context.error_message, invalidated ? "Component reference target is no longer available" : "Referenced storage is unavailable")
            );
            pin_value read[] = { pointer_root, type }, retained = type;
            if (invalidated) {
                REQUIRE(EXECUTION_pointer_read_call.value(&managed, read, 2, &retained));
            } else {
                REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &retained));
                REQUIRE(((fixture_box *)retained.ptr)->value == initial.value);
            }
        }
    }
    return true;
}

TEST_CASE(execution_invalid_component_metadata_is_charged_and_collects_on_update)
{
    pointer_world = ecs_world_new();
    size_t length = 64 * 1024;
    char *description = arena_alloc(&managed, length + 1);
    memset(description, 'd', length);
    description[length] = 0;
    type_field field = c_text_field;
    field.description = description;
    type_desc type = c_text_type;
    type.fields = &field;
    ecs_component_desc component = { .id = 9194,
                                     .key = "fixture.c_text",
                                     .name = "Text",
                                     .element_size = sizeof(fixture_c_text),
                                     .type = &type };
    REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(pointer_world);
    char *text = arena_alloc(&managed, length + 1);
    memset(text, 'a', length);
    text[length] = 0;
    fixture_c_text value = { text };
    REQUIRE(ecs_world_record_add(pointer_world, entity, component.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    prepared_plan = execution_module_plan_new();
    prepared_plan->valid = true;
    prepared_plan->id = 985127;
    char error[192];
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module);
    pointer_program = execution_program_new(prepared_module, pointer_world, NULL);
    REQUIRE(pointer_program);
    behavior_managed_heap *heap = execution_program_managed_heap(pointer_program);
    behavior_function_context owner = { .managed_heap = heap,
                                        .world = ecs_world_execution(pointer_world) };
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&owner },
                         { .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(entity) },
                         { .type = PIN_U64, .u64 = component.id },
                         { .type = PIN_STRUCT, .type_name = "fixture.c_text" },
                         { .type = PIN_U64, .u64 = ecs_layout_hash(&c_text_type) },
                         { .type = PIN_U64, .u64 = sizeof(fixture_c_text) } };
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &pointer_root));
    size_t attached = heap->storage.external;
    REQUIRE(attached > length);
    REQUIRE(ecs_world_record_remove(pointer_world, entity, component.id, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    REQUIRE(heap->storage.external == attached);
    REQUIRE(heap->storage.allocated > length);
    execution_memory_usage before = execution_program_memory_usage(pointer_program);
    REQUIRE(before.managed_bytes > length && before.managed_capacity >= before.managed_bytes);
    REQUIRE(execution_program_update(pointer_program, 0, 0.016, 1000) == FLOW_OK);
    REQUIRE(!arena_contains(&heap->storage, pointer_root.ptr));
    REQUIRE(!heap->storage.external);
    execution_memory_usage after = execution_program_memory_usage(pointer_program);
    REQUIRE(after.managed_bytes < length);
    execution_program_free(pointer_program);
    pointer_program = NULL;
    ecs_world_free(pointer_world);
    pointer_world = NULL;
    return true;
}

TEST_CASE(execution_component_pointer_keeps_native_text_and_narrow_layouts)
{
    pointer_world = ecs_world_new();
    type_field private_field = c_text_field;
    type_desc private_type = { .name = "fixture.private_text",
                               .size = sizeof(fixture_c_text),
                               .fields = &private_field,
                               .field_count = 1 };
    ecs_component_desc text_component = { .id = 9192,
                                          .key = "fixture.c_text",
                                          .name = "Text",
                                          .element_size = sizeof(fixture_c_text),
                                          .type = &private_type };
    ecs_component_desc enum_component = { .id = 9193,
                                          .key = "fixture.narrow_enum",
                                          .name = "Mode",
                                          .element_size = 1,
                                          .type = &narrow_enum_type };
    REQUIRE(ecs_world_register_component(pointer_world, &text_component, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(pointer_world, &enum_component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(pointer_world);
    char bytes[] = "original";
    fixture_c_text text = { bytes };
    uint8_t mode = 7;
    REQUIRE(ecs_world_record_add(pointer_world, entity, text_component.id, &text, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(pointer_world, entity, enum_component.id, &mode, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    behavior_managed_heap_init(&pointer_heap);
    behavior_function_context owner = { .managed_heap = &pointer_heap,
                                        .world = ecs_world_execution(pointer_world) };
    pin_value type = { .type = PIN_STRUCT, .type_name = "fixture.private_text" };
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&owner },
                         { .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(entity) },
                         { .type = PIN_U64, .u64 = text_component.id },
                         type,
                         { .type = PIN_U64, .u64 = ecs_layout_hash(&private_type) },
                         { .type = PIN_U64, .u64 = sizeof(fixture_c_text) } };
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &pointer_root));
    pin_value project[] = { pointer_root,
                            { .type = PIN_STR },
                            { .type = PIN_U64 },
                            { .type = PIN_BOOL },
                            type };
    pin_value text_field = { .type = PIN_POINTER };
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, project, 5, &text_field));
    make[2].u64 = enum_component.id;
    make[3].type_name = "fixture.narrow_enum";
    make[4].u64 = ecs_layout_hash(&narrow_enum_type);
    make[5].u64 = 1;
    pin_value enum_pointer = { .type = PIN_POINTER }, enum_field = enum_pointer;
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &enum_pointer));
    project[0] = enum_pointer;
    project[1].type = PIN_ENUM;
    project[4] = make[3];
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, project, 5, &enum_field));
    pin_value read_mode[] = { enum_field, { .type = PIN_ENUM } },
              result_mode = read_mode[1];
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read_mode, 2, &result_mode));
    REQUIRE(result_mode.e == 7);
    pin_value read[] = { text_field, { .type = PIN_STR } }, result = read[1];
    REQUIRE(!EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    ecs_world_free(pointer_world);
    pointer_world = NULL;
    private_field.type = PIN_I64;
    private_type.field_count = 0;
    memset(bytes, 'x', sizeof bytes - 1);
    REQUIRE(EXECUTION_pointer_read_call.value(&managed, read, 2, &result));
    REQUIRE(pin_text(result).length == strlen("original"));
    REQUIRE(!memcmp(pin_text(result).bytes, "original", strlen("original")));
    pin_value updated;
    REQUIRE(!pin_value_text(&managed, &updated, "changed", 7));
    pin_value write[] = { text_field, updated }, written = { 0 };
    REQUIRE(EXECUTION_pointer_write_call.value(&managed, write, 2, &written));
    REQUIRE(EXECUTION_pointer_read_call.value(&managed, read_mode, 2, &result_mode));
    REQUIRE(result_mode.e == 7);
    pin_value invalid = ecs_component_cell_value(
        ((pin_pointer *)pointer_root.ptr)->component
    );
    REQUIRE(!invalid.ptr && invalid.schema);
    REQUIRE(!strcmp(pin_value_type(&invalid)->name, "fixture.private_text"));
    REQUIRE(pin_value_type(&invalid)->fields[0].type == PIN_STR);
    behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
    size_t used, capacity;
    arena_usage(&pointer_heap.storage, &used, &capacity);
    REQUIRE(!used);
    return true;
}

typedef struct
{
    uint64_t entity, component, layout;
    size_t offset;
    pin_value declared, value;
    execution_reduction_op op;
    int count;
} fixture_reduction;
static int capture_component_reduction(
    behavior_function_context *consumer,
    uint64_t entity,
    uint64_t component,
    uint64_t layout,
    size_t offset,
    pin_value declared,
    pin_value value,
    execution_reduction_op op
)
{
    fixture_reduction *capture = consumer->inspection_user;
    *capture = (fixture_reduction){ .entity = entity,
                                    .component = component,
                                    .layout = layout,
                                    .offset = offset,
                                    .declared = declared,
                                    .value = value,
                                    .op = op,
                                    .count = capture->count + 1 };
    return FLOW_OK;
}
TEST_CASE(execution_component_pointer_reductions_use_the_consumer_and_live_identity)
{
    pointer_world = ecs_world_new();
    ecs_component_desc component = { .id = 9195,
                                     .key = "fixture.box",
                                     .name = "Box",
                                     .element_size = sizeof(fixture_box),
                                     .type = &box_type };
    REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(pointer_world);
    fixture_box value = { 17 };
    REQUIRE(ecs_world_record_add(pointer_world, entity, component.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    behavior_managed_heap_init(&pointer_heap);
    behavior_function_context producer = { .managed_heap = &pointer_heap,
                                           .world = ecs_world_execution(pointer_world) };
    pin_value type = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    uint64_t layout = ecs_layout_hash(&box_type);
    pin_value make[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&producer },
                         { .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(entity) },
                         { .type = PIN_U64, .u64 = component.id },
                         type,
                         { .type = PIN_U64, .u64 = layout },
                         { .type = PIN_U64, .u64 = sizeof(fixture_box) } };
    REQUIRE(!EXECUTION_component_pointer_call.value(&managed, make, 6, &pointer_root));
    pin_value project[] = { pointer_root,
                            { .type = PIN_I32 },
                            { .type = PIN_U64, .u64 = offsetof(fixture_box, value) },
                            { .type = PIN_BOOL },
                            type };
    pin_value field = { .type = PIN_POINTER }, reference = { .type = BF_POINTER_TYPE };
    REQUIRE(!EXECUTION_pointer_field_call.value(&managed, project, 5, &field));
    REQUIRE(!EXECUTION_pointer_reference_call.value(&managed, &field, 1, &reference));
    /* The query's producing frame is gone; only the current system owns the
     * contribution buffer. */
    producer = (behavior_function_context){ 0 };
    fixture_reduction capture = { 0 };
    context = (behavior_function_context){ .world = ecs_world_execution(pointer_world),
                                           .reduce_component = capture_component_reduction,
                                           .inspection_user = &capture };
    pin_value args[] = { reference,
                         { .type = PIN_I32, .i = 3 },
                         { .type = PIN_I32, .i = EXECUTION_REDUCTION_ADD_UNORDERED } };
    pin_value out = { .type = PIN_BOOL };
    REQUIRE(!EXECUTION_reference_reduce_call.context(&context, NULL, args, &out));
    REQUIRE(out.b && capture.count == 1);
    REQUIRE(
        capture.entity == ecs_entity_to_u64(entity) &&
        capture.component == component.id
    );
    REQUIRE(capture.layout == layout && capture.offset == offsetof(fixture_box, value));
    REQUIRE(capture.declared.type == PIN_I32 && capture.value.i == 3);
    REQUIRE(capture.op == EXECUTION_REDUCTION_ADD_UNORDERED);
    REQUIRE(
        ((fixture_box *)ecs_component_data(pointer_world, entity, component.id))->value == 17
    );
    ecs_execution_view other_world = { 0 };
    context.world = &other_world;
    REQUIRE(EXECUTION_reference_reduce_call.context(&context, NULL, args, &out));
    REQUIRE(capture.count == 1);
    context.world = ecs_world_execution(pointer_world);
    REQUIRE(ecs_world_record_remove(pointer_world, entity, component.id, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    REQUIRE(EXECUTION_reference_reduce_call.context(&context, NULL, args, &out));
    REQUIRE(ecs_world_record_add(pointer_world, entity, component.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    REQUIRE(EXECUTION_reference_reduce_call.context(&context, NULL, args, &out));
    REQUIRE(capture.count == 1);
    ecs_world_free(pointer_world);
    pointer_world = NULL;
    REQUIRE(EXECUTION_reference_reduce_call.context(&context, NULL, args, &out));
    REQUIRE(capture.count == 1);
    context = (behavior_function_context){ 0 };
    return true;
}

TEST_CASE(execution_query_native_array_captures_keep_retired_child_schemas)
{
    for (int nested = 0; nested < 3; nested++) {
        cleanup();
        behavior_managed_heap_init(&pointer_heap);
        arena_init_collectable(&storage);
        type_field child_field = box_field;
        type_desc child_type = { .name = "fixture.query_child",
                                 .size = sizeof(fixture_box),
                                 .fields = &child_field,
                                 .field_count = 1 };
        type_field root_field = { .name = "inner",
                                  .type = PIN_STRUCT,
                                  .type_name = child_type.name,
                                  .size = sizeof(fixture_box) };
        type_desc root_type = { .name = "fixture.query_native_array",
                                .size = sizeof(fixture_box),
                                .fields = &root_field,
                                .field_count = 1 };
        const type_desc *types[] = { &child_type, &root_type };
        type_catalog_pack pack = { .types = types, .type_count = 2 };
        type_catalog_set_project(&pack);
        return_types_active = true;
        uint64_t layout = type_layout_hash(&root_type);
        const pin_schema *schema = pin_schema_create(&managed, &root_type, (type_resolver){ 0 });
        REQUIRE(schema);
        child_field.type = PIN_F32;
        const pin_schema *incompatible = pin_schema_create(&managed, &root_type, (type_resolver){ 0 });
        REQUIRE(incompatible && pin_schema_layout(incompatible, root_type.name) != layout);
        child_field.type = PIN_I32;
        type_desc other_type = root_type;
        other_type.name = "fixture.query_other_native_array";
        const pin_schema *other_schema = pin_schema_create(&managed, &other_type, (type_resolver){ 0 });
        REQUIRE(other_schema);
        fixture_box original = { 12 };
        pin_array source = { .element_type = PIN_STRUCT,
                             .type_name = root_type.name,
                             .schema = schema,
                             .count = 1,
                             .capacity = 1,
                             .stride = sizeof original,
                             .data = &original };
        pin_array *array = pin_array_snapshot_to_arena(&pointer_heap.storage, &source);
        source.schema = incompatible;
        pin_array *bad_array = pin_array_snapshot_to_arena(&pointer_heap.storage, &source);
        REQUIRE(array && bad_array);
        pin_value capture = { .type = PIN_STRUCT,
                              .container = PIN_CONTAINER_ARRAY,
                              .type_name = root_type.name };
        pin_value actual = capture, bad = capture;
        actual.ptr = array;
        bad.ptr = bad_array;
        source.type_name = other_type.name;
        source.schema = other_schema;
        pin_value wrong_nominal = capture;
        wrong_nominal.type_name = other_type.name;
        wrong_nominal.ptr = pin_array_snapshot_to_arena(&pointer_heap.storage, &source);
        REQUIRE(wrong_nominal.ptr);
        char collection_name[64];
        if (nested) {
            char identity[96];
            script_type element = { .storage = PIN_STRUCT,
                                    .container = PIN_CONTAINER_ARRAY,
                                    .nominal = 985152 };
            snprintf(
                identity,
                sizeof identity,
                "%d:%" PRIu64 ":%" PRIu64,
                PIN_CONTAINER_ARRAY,
                script_type_tag((script_type){ 0 }),
                script_type_tag(element)
            );
            snprintf(
                collection_name,
                sizeof collection_name,
                "__collection_%" PRIu64,
                script_catalog_id("collection-type", identity)
            );
            capture.type_name = collection_name;
            pin_value contained = actual;
            pin_array outer = { .element_type = PIN_STRUCT,
                                .type_name = capture.type_name,
                                .items = &contained,
                                .count = 1,
                                .capacity = 1 };
            actual = capture;
            if (nested == 2) {
                outer.count = 0;
            }
            actual.ptr = pin_array_snapshot_to_arena(&pointer_heap.storage, &outer);
            outer.count = 1;
            contained = bad;
            bad = capture;
            bad.ptr = pin_array_snapshot_to_arena(&pointer_heap.storage, &outer);
            contained = wrong_nominal;
            wrong_nominal = capture;
            wrong_nominal.ptr = pin_array_snapshot_to_arena(&pointer_heap.storage, &outer);
            REQUIRE(actual.ptr && bad.ptr && wrong_nominal.ptr);
        }
        builder = behavior_function_builder_new();
        pin_value results[] = { { .type = PIN_BOOL }, capture };
        behavior_function_builder_signature(builder, &capture, 1, results, 2, NULL, 0);
        pin_value header = { .type = PIN_STRUCT,
                             .type_name = root_type.name,
                             .schema = schema };
        behavior_function_builder_values(builder, &header, 1);
        int accepted = slot(PIN_BOOL), value = address_slot();
        emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                              .type = PIN_BOOL,
                                              .output = accepted,
                                              .value = { .type = PIN_BOOL, .b = true } });
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = PIN_BOOL,
                                              .left = accepted });
        emit((behavior_function_instruction){ .op = BF_ARRAY,
                                              .type = BF_POINTER_TYPE,
                                              .output = value,
                                              .target = BF_ARGUMENT,
                                              .value = capture });
        int result_bank = address_slot(), result_index = slot(PIN_I32),
            result_header = address_slot();
        emit((behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = BF_POINTER_TYPE,
            .output = result_bank,
            .offset = offsetof(behavior_function_context, results) });
        emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                              .type = PIN_I32,
                                              .output = result_index,
                                              .value = { .type = PIN_I32, .i = 1 } });
        emit((behavior_function_instruction){ .op = BF_INDEX,
                                              .type = BF_POINTER_TYPE,
                                              .output = result_header,
                                              .left = result_bank,
                                              .right = result_index,
                                              .offset = sizeof(pin_value) });
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = BF_POINTER_TYPE,
                                              .left = value,
                                              .right = result_header,
                                              .offset = offsetof(pin_value, d) });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        prepared_plan = execution_module_plan_new();
        prepared_plan->valid = true;
        prepared_plan->id = 985150;
        pin_value row = { .type = PIN_QUERY,
                          .type_name = "fixture.query_native_array_row" };
        apush(
            prepared_plan->types,
            ((execution_type_requirement){ .id = 985151,
                                           .storage = PIN_QUERY,
                                           .name = row.type_name,
                                           .size = sizeof(void *) })
        );
        apush(
            prepared_plan->types,
            ((execution_type_requirement){ .id = 985152,
                                           .storage = PIN_STRUCT,
                                           .name = root_type.name,
                                           .size = root_type.size,
                                           .layout = layout })
        );
        apush(
            prepared_plan->types,
            ((execution_type_requirement){ .id = 985153,
                                           .storage = PIN_STRUCT,
                                           .name = other_type.name,
                                           .size = other_type.size,
                                           .layout = type_layout_hash(&other_type) })
        );
        prepared_plan->type_count = 3;
        apush(
            prepared_plan->queries,
            ((execution_module_query){ .kind = EXECUTION_QUERY_HAS,
                                       .required = true,
                                       .component = 9191 })
        );
        prepared_plan->query_count = 1;
        apush(
            prepared_plan->descriptions,
            ((execution_module_query_description){ .query = 0, .row_type = row, .evaluate = function })
        );
        prepared_plan->description_count = 1;
        function = NULL;
        char error[256];
        prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(prepared_module);
        pointer_world = ecs_world_new();
        ecs_component_desc component = { .id = 9191,
                                         .key = "fixture.box",
                                         .name = box_type.name,
                                         .type = &box_type,
                                         .element_size = sizeof(fixture_box) };
        REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
        ecs_entity entity = ecs_world_create_entity(pointer_world);
        REQUIRE(
            ecs_world_record_add(pointer_world, entity, component.id, &original, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
        type_catalog_set_project(NULL);
        return_types_active = false;
        child_field.type = PIN_F32;
        root_type.field_count = 0;
        original.value = 99;
        arena_free(&managed);
        size_t length, measured;
        uint64_t fingerprint;
        REQUIRE(
            execution_prepared_write(prepared_module, &prepared_bytes, &length, error, sizeof error)
        );
        REQUIRE(
            execution_prepared_measure(prepared_module, &measured, &fingerprint, error, sizeof error)
        );
        REQUIRE(measured == length && fingerprint);
        apush(log_services, ((service_binding){ .type_name = "ecs.world", .value = pointer_world }));
        context = (behavior_function_context){ .module = prepared_module,
                                               .managed_heap = &pointer_heap,
                                               .services = log_services,
                                               .world = ecs_world_execution(pointer_world),
                                               .budget = 1000 };
        behavior_function_bind_storage(&context, &storage);
        pin_value arguments[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&context },
                                  { .type = PIN_I32 },
                                  bad };
        pin_value rejected = row;
        const char *failure = EXECUTION_query_make_call.value(&storage, arguments, 3, &rejected);
        REQUIRE(failure && strstr(failure, "capture type"));
        arguments[2] = wrong_nominal;
        failure = EXECUTION_query_make_call.value(&storage, arguments, 3, &rejected);
        REQUIRE(failure && strstr(failure, "capture type"));
        arguments[2] = actual;
        pointer_root = row;
        REQUIRE(!EXECUTION_query_make_call.value(&storage, arguments, 3, &pointer_root));
        REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
        pointer_root_registered = true;
        behavior_managed_heap_collect(&pointer_heap, NULL, NULL);
        pin_value begin[] = { arguments[0], pointer_root },
                  cursor = { .type = BF_POINTER_TYPE };
        failure = EXECUTION_query_begin_call.value(&storage, begin, 2, &cursor);
        if (failure) {
            fprintf(stderr, "query native capture begin: %s\n", failure);
        }
        REQUIRE(!failure);
        pin_value next[] = { arguments[0], cursor },
                  returned[] = { { .type = PIN_BOOL }, row };
        REQUIRE(!EXECUTION_query_next_call.context(&context, NULL, next, returned));
        REQUIRE(returned[0].b);
        pin_value get[] = { returned[1], { .type = PIN_I32 } }, copied = capture;
        REQUIRE(!EXECUTION_query_row_get_call.value(&storage, get, 2, &copied));
        if (nested == 2) {
            REQUIRE(((pin_array *)copied.ptr)->count == 0);
            REQUIRE(!EXECUTION_query_end_call.value(&storage, next, 2, &rejected));
            continue;
        }
        if (nested) {
            copied = pin_array_get(copied.ptr, 0);
        }
        pin_value item = pin_array_get(copied.ptr, 0);
        REQUIRE(item.schema && ((fixture_box *)item.ptr)->value == 12);
        const type_desc *retained_child = pin_schema_type(item.schema, "fixture.query_child");
        REQUIRE(retained_child && retained_child->fields[0].type == PIN_I32);
        REQUIRE(!EXECUTION_query_end_call.value(&storage, next, 2, &rejected));
    }
    return true;
}

TEST_CASE(execution_query_iterators_capture_values_and_recheck_snapshot_membership)
{
    behavior_managed_heap_init(&pointer_heap);
    arena_init_collectable(&storage);
    pointer_world = ecs_world_new();
    ecs_component_desc component = { .id = 9191,
                                     .key = "fixture.box",
                                     .name = "Box",
                                     .element_size = sizeof(fixture_box),
                                     .type = &box_type,
                                     .layout_hash = type_layout_hash(&box_type) };
    REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
    ecs_entity first = ecs_world_create_entity(pointer_world);
    ecs_entity second = ecs_world_create_entity(pointer_world);
    fixture_box value = { 1 };
    REQUIRE(ecs_world_record_add(pointer_world, first, component.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(pointer_world, second, component.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(pointer_world, first, ECS_DISABLED, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);

    builder = behavior_function_builder_new();
    pin_value captured = { .type = PIN_I32 };
    pin_value results[] = { { .type = PIN_BOOL }, captured };
    behavior_function_builder_signature(builder, &captured, 1, results, 2, NULL, 0);
    int accepted = slot(PIN_BOOL), amount = slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .output = accepted,
                                          .value = { .type = PIN_BOOL, .b = true } });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = accepted });
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = amount, .value = captured });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_I32, .target = 1, .left = amount });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    prepared_plan = execution_module_plan_new();
    prepared_plan->valid = true;
    prepared_plan->id = 985123;
    pin_value row_type = { .type = PIN_QUERY, .type_name = "fixture.query_row" };
    apush(
        prepared_plan->types,
        ((execution_type_requirement){ .id = 985124,
                                       .storage = PIN_QUERY,
                                       .name = row_type.type_name,
                                       .size = sizeof(void *) })
    );
    prepared_plan->type_count = 1;
    apush(
        prepared_plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_HAS,
                                   .required = true,
                                   .component = component.id })
    );
    apush(
        prepared_plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_HAS, .component = ECS_DISABLED })
    );
    apush(prepared_plan->queries, ((execution_module_query){ .kind = EXECUTION_QUERY_VALUE }));
    execution_module_query guard = { .kind = EXECUTION_QUERY_ANY, .child_count = 2 };
    guard.children = xmalloc(2 * sizeof *guard.children);
    guard.children[0] = 1;
    guard.children[1] = 2;
    apush(prepared_plan->queries, guard);
    execution_module_query all = { .kind = EXECUTION_QUERY_ALL, .child_count = 2 };
    all.children = xmalloc(2 * sizeof *all.children);
    all.children[0] = 0;
    all.children[1] = 3;
    apush(prepared_plan->queries, all);
    prepared_plan->query_count = 5;
    apush(
        prepared_plan->descriptions,
        ((execution_module_query_description){ .query = 4,
                                               .row_type = row_type,
                                               .evaluate = function })
    );
    prepared_plan->description_count = 1;
    function = NULL;
    char error[256];
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!prepared_module) {
        fprintf(stderr, "query module: %s\n", error);
    }
    REQUIRE(prepared_module);
    apush(log_services, ((service_binding){ "ecs.world", pointer_world }));
    context = (behavior_function_context){ .module = prepared_module,
                                           .managed_heap = &pointer_heap,
                                           .services = log_services,
                                           .world = ecs_world_execution(pointer_world),
                                           .budget = 1000 };
    behavior_function_bind_storage(&context, &storage);
    pin_value arguments[] = { { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&context },
                              { .type = PIN_I32 },
                              { .type = PIN_I32, .i = 28 } };
    pointer_root = row_type;
    REQUIRE(!EXECUTION_query_make_call.value(&storage, arguments, 3, &pointer_root));
    REQUIRE(!behavior_managed_heap_root(&pointer_heap, &pointer_root));
    pointer_root_registered = true;
    arguments[2].i = 77;
    pin_value begin_args[] = { arguments[0], pointer_root };
    pin_value cursor_a = { .type = BF_POINTER_TYPE }, cursor_b = cursor_a;
    REQUIRE(!EXECUTION_query_begin_call.value(&storage, begin_args, 2, &cursor_a));
    REQUIRE(!EXECUTION_query_begin_call.value(&storage, begin_args, 2, &cursor_b));
    pin_value next_args[] = { arguments[0], cursor_a };
    pin_value next_results[] = { { .type = PIN_BOOL }, row_type };
    REQUIRE(!EXECUTION_query_next_call.context(&context, NULL, next_args, next_results));
    REQUIRE(next_results[0].b);
    pin_value get_args[] = { next_results[1], { .type = PIN_I32 } };
    pin_value field = captured;
    REQUIRE(!EXECUTION_query_row_get_call.value(&storage, get_args, 2, &field));
    REQUIRE(field.i == 28);
    next_args[1] = cursor_b;
    REQUIRE(!EXECUTION_query_next_call.context(&context, NULL, next_args, next_results));
    REQUIRE(next_results[0].b);
    /* The dynamic guard names Disabled, so both independent iterations must
     * include the disabled entity before ordinary membership is rechecked. */
    REQUIRE(!EXECUTION_query_next_call.context(&context, NULL, next_args, next_results));
    REQUIRE(next_results[0].b);
    REQUIRE(!EXECUTION_query_next_call.context(&context, NULL, next_args, next_results));
    REQUIRE(!next_results[0].b);
    REQUIRE(ecs_world_record_remove(pointer_world, second, component.id, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
    REQUIRE(!EXECUTION_query_next_call.context(&context, NULL, next_args, next_results));
    REQUIRE(!next_results[0].b);
    /* The query identity retains its original generation after its producer releases it. */
    execution_module_free(prepared_module);
    prepared_module = NULL;
    next_args[1] = cursor_a;
    REQUIRE(!EXECUTION_query_next_call.context(&context, NULL, next_args, next_results));
    REQUIRE(!next_results[0].b);
    REQUIRE(!EXECUTION_query_end_call.value(&storage, next_args, 2, &field));
    REQUIRE(!EXECUTION_query_end_call.value(&storage, next_args, 2, &field));
    REQUIRE(EXECUTION_query_next_call.context(&context, NULL, next_args, next_results));
    return true;
}

static int query_effect_call(
    const node_host_call *call,
    const pin_value *arguments,
    const int *slots,
    int count,
    pin_value result
)
{
    int host = behavior_function_builder_host(builder, call, NULL, arguments, count, &result, 1);
    uint64_t id = execution_intrinsic_id(call);
    execution_intrinsic kind = execution_intrinsic_kind(id);
    assert(behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id)));
    int first = behavior_function_builder_host_first(builder, host);
    if (count && arguments[0].type == BF_POINTER_TYPE) {
        behavior_value_kind input = kind == EXECUTION_INTRINSIC_REFERENCE_FIELD ||
                                            kind == EXECUTION_INTRINSIC_REFERENCE_WRITE
                                      ? BEHAVIOR_REFERENCE
                                      : BEHAVIOR_ADDRESS;
        assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first, input));
    }
    bool reference = kind == EXECUTION_INTRINSIC_POINTER_REFERENCE ||
                     kind == EXECUTION_INTRINSIC_REFERENCE_FIELD;
    if (reference) {
        assert(behavior_function_builder_kind(
            builder,
            BEHAVIOR_BANK_DATA,
            behavior_function_builder_host_results(builder, host),
            BEHAVIOR_REFERENCE
        ));
    }
    for (int i = 0; i < count; i++) {
        if (slots[i] < 0) {
            continue;
        }
        int address = address_slot();
        emit((behavior_function_instruction){ .op = BF_HOST_SLOT,
                                              .type = BF_POINTER_TYPE,
                                              .output = address,
                                              .left = first + i });
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = arguments[i].type,
                                              .left = slots[i],
                                              .right = address,
                                              .offset = offsetof(pin_value, d) });
    }
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                          .target = host });
    int address = address_slot(), output = slot(result.type);
    if (reference) {
        assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, output, BEHAVIOR_REFERENCE));
    }
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = address,
        .left = behavior_function_builder_host_results(builder, host) });
    emit((behavior_function_instruction){ .op = BF_LOAD,
                                          .type = result.type,
                                          .output = output,
                                          .left = address,
                                          .offset = offsetof(pin_value, d) });
    return output;
}

TEST_CASE(execution_owned_query_intrinsics_bind_the_world_without_component_metadata)
{
    for (int variant = 0; variant < 3; variant++) {
        cleanup();
        pointer_world = ecs_world_new();
        ecs_component_desc component = { .id = 9196,
                                         .key = "fixture.original",
                                         .name = box_type.name,
                                         .element_size = sizeof(fixture_box),
                                         .type = &box_type };
        ecs_relationship_desc relationship = { .id = 9198,
                                               .key = "fixture.targets",
                                               .name = "Targets" };
        REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
        REQUIRE(ecs_world_register_relationship(pointer_world, &relationship, NULL) == ECS_OK);
        ecs_entity owner = ecs_world_create_entity(pointer_world),
                   target = ecs_world_create_entity(pointer_world);
        fixture_box value = { 1 };
        REQUIRE(ecs_world_record_add(pointer_world, owner, component.id, &value, 0) == ECS_OK);
        REQUIRE(
            ecs_world_record_add_pair(pointer_world, owner, relationship.id, target, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
        builder = behavior_function_builder_new();
        pin_value result = { .type = variant ? PIN_HANDLE : PIN_BOOL };
        behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
        int context_slot = address_slot(), owner_slot = slot(PIN_HANDLE);
        emit((behavior_function_instruction){ .op = BF_CONTEXT,
                                              .type = BF_POINTER_TYPE,
                                              .output = context_slot,
                                              .target = 1 });
        emit((behavior_function_instruction){ .op = BF_OWNER,
                                              .type = PIN_HANDLE,
                                              .output = owner_slot });
        pin_value arguments[] = {
            { .type = BF_POINTER_TYPE },
            { .type = PIN_HANDLE },
            { .type = PIN_U64, .u64 = variant == 2 ? relationship.id : component.id },
            { .type = variant == 2 ? PIN_I32 : PIN_U64 },
            { .type = PIN_I32 }
        };
        const int slots[] = { context_slot, owner_slot, -1, -1, -1 };
        const node_host_call *call = variant == 0 ? &EXECUTION_component_has_call
                                   : variant == 1 ? &EXECUTION_query_source_call
                                                  : &EXECUTION_query_target_call;
        int output = query_effect_call(
            call,
            arguments,
            slots,
            variant == 0   ? 3
            : variant == 1 ? 5
                           : 4,
            result
        );
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = result.type,
                                              .left = output });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(!acount(function->components));
        apush(log_services, ((service_binding){ .type_name = "ecs.world", .value = pointer_world }));
        context = (behavior_function_context){ .results = &result, .budget = 1000 };
        behavior_function_bind_storage(&context, &storage);
        REQUIRE(
            behavior_function_bind(function, &context, log_services, ecs_entity_to_u64(owner))
        );
        REQUIRE(context.world == ecs_world_execution(pointer_world));
        REQUIRE(behavior_function_run(function, &context, false) == FLOW_OK);
        REQUIRE(
            variant ? result.u64 == ecs_entity_to_u64(variant == 1 ? owner : target)
                    : result.b
        );
    }
    return true;
}

TEST_CASE(execution_body_relationship_traversals_allow_later_scene_registration)
{
    pointer_world = ecs_world_new();
    ecs_entity owner = ecs_world_create_entity(pointer_world),
               target = ecs_world_create_entity(pointer_world);
    ecs_relationship_desc relationship = { .id = 9198,
                                           .key = "fixture.targets",
                                           .name = "Targets" };
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_HANDLE };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    int context_slot = address_slot(), owner_slot = slot(PIN_HANDLE);
    emit((behavior_function_instruction){ .op = BF_CONTEXT,
                                          .type = BF_POINTER_TYPE,
                                          .output = context_slot,
                                          .target = 1 });
    emit((behavior_function_instruction){ .op = BF_OWNER,
                                          .type = PIN_HANDLE,
                                          .output = owner_slot });
    pin_value arguments[] = { { .type = BF_POINTER_TYPE },
                              { .type = PIN_HANDLE },
                              { .type = PIN_U64, .u64 = relationship.id },
                              { .type = PIN_I32 } };
    const int slots[] = { context_slot, owner_slot, -1, -1 };
    int output = query_effect_call(&EXECUTION_query_target_call, arguments, slots, 4, result);
    emit((behavior_function_instruction){ .op = BF_RESULT,
                                          .type = PIN_HANDLE,
                                          .left = output });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    prepared_plan = execution_module_plan_new();
    prepared_plan->valid = true;
    prepared_plan->id = 985140;
    apush(
        prepared_plan->functions,
        ((execution_module_function){ .id = 985141,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = function })
    );
    prepared_plan->function_count = 1;
    function = NULL;
    char error[256];
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module);
    apush(log_services, ((service_binding){ .type_name = "ecs.world", .value = pointer_world }));
    for (int registered = 0; registered < 2; registered++) {
        if (registered) {
            REQUIRE(ecs_world_register_relationship(pointer_world, &relationship, NULL) == ECS_OK);
            REQUIRE(
                ecs_world_record_add_pair(pointer_world, owner, relationship.id, target, 0) == ECS_OK
            );
            REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
        }
        REQUIRE(execution_module_components_valid(prepared_module, pointer_world));
        pointer_program = execution_program_new(prepared_module, pointer_world, NULL);
        REQUIRE(pointer_program);
        execution_program_free(pointer_program);
        pointer_program = NULL;
        release_invocation();
        context = (behavior_function_context){ .results = &result, .budget = 1000 };
        behavior_function_bind_storage(&context, &storage);
        const behavior_function *body = prepared_module->plan.functions[0].body;
        REQUIRE(behavior_function_bind(body, &context, log_services, ecs_entity_to_u64(owner)));
        REQUIRE(behavior_function_run(body, &context, false) == FLOW_OK);
        REQUIRE(result.u64 == (registered ? ecs_entity_to_u64(target) : 0));
    }
    return true;
}

enum
{
    QUERY_EFFECT_POINTER_TYPE = 985133,
    QUERY_EFFECT_NATIVE_TYPE = 985134
};
static bool query_effect_native_type(void *, uint64_t id, execution_prepared_type *out)
{
    if (id != QUERY_EFFECT_NATIVE_TYPE) {
        return false;
    }
    *out = (execution_prepared_type){ .name = box_type.name,
                                      .storage = PIN_STRUCT,
                                      .size = sizeof(fixture_box),
                                      .layout = type_layout_hash(&box_type),
                                      .type = &box_type };
    return true;
}

static bool query_effect_plan(int variant)
{
    const uint64_t components[] = { 9196, 9197 };
    pin_value pointer = { .type = PIN_POINTER, .type_name = "^fixture.box" };
    pin_value row = { .type = PIN_QUERY, .type_name = "fixture.effect_row" };
    builder = behavior_function_builder_new();
    pin_value results[] = { { .type = PIN_BOOL }, pointer };
    behavior_function_builder_signature(builder, NULL, 0, results, 2, NULL, 0);
    int context_slot = address_slot(), owner_slot = slot(PIN_HANDLE);
    emit((behavior_function_instruction){ .op = BF_CONTEXT,
                                          .type = BF_POINTER_TYPE,
                                          .output = context_slot,
                                          .target = 1 });
    emit((behavior_function_instruction){ .op = BF_OWNER,
                                          .type = PIN_HANDLE,
                                          .output = owner_slot });
    int pointers[2];
    for (int i = 0; i < 2; i++) {
        pin_value arguments[] = { { .type = BF_POINTER_TYPE },
                                  { .type = PIN_HANDLE },
                                  { .type = PIN_U64, .u64 = components[i] },
                                  { .type = PIN_STRUCT, .type_name = box_type.name },
                                  { .type = PIN_U64, .u64 = type_layout_hash(&box_type) },
                                  { .type = PIN_U64, .u64 = sizeof(fixture_box) } };
        const int inputs[] = { context_slot, owner_slot, -1, -1, -1, -1 };
        pointers[i] = query_effect_call(&EXECUTION_component_pointer_call, arguments, inputs, 6, pointer);
    }
    int result_bank = address_slot(), ordinal = slot(PIN_I32),
        destination = address_slot();
    emit((behavior_function_instruction){
        .op = BF_CONTEXT,
        .type = BF_POINTER_TYPE,
        .output = result_bank,
        .offset = offsetof(behavior_function_context, results) });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .output = ordinal,
                                          .value = { .type = PIN_I32, .i = 1 } });
    emit((behavior_function_instruction){ .op = BF_INDEX,
                                          .type = BF_POINTER_TYPE,
                                          .output = destination,
                                          .left = result_bank,
                                          .right = ordinal,
                                          .offset = sizeof(pin_value) });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_POINTER,
                                          .left = pointers[0],
                                          .right = destination,
                                          .offset = offsetof(pin_value, d) });
    int replacement = pointers[variant >= 3];
    if (variant == 1 || variant == 3) {
        emit((
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = PIN_POINTER, .target = 1, .left = replacement });
    }
    if (variant == 2 || variant == 4) {
        int offset = slot(BF_POINTER_TYPE), alias = address_slot();
        emit((behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = BF_POINTER_TYPE,
            .output = offset,
            .value = { .type = BF_POINTER_TYPE, .u64 = offsetof(pin_value, d) } });
        emit((behavior_function_instruction){ .op = BF_ADD,
                                              .type = BF_POINTER_TYPE,
                                              .output = alias,
                                              .left = destination,
                                              .right = offset });
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = PIN_POINTER,
                                              .left = replacement,
                                              .right = alias });
    }
    int accepted = slot(PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .output = accepted,
                                          .value = { .type = PIN_BOOL, .b = true } });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = accepted });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, NULL, 0, NULL, 0);
    context_slot = address_slot();
    emit((behavior_function_instruction){ .op = BF_CONTEXT,
                                          .type = BF_POINTER_TYPE,
                                          .output = context_slot,
                                          .target = 1 });
    pin_value context_type = { .type = BF_POINTER_TYPE };
    int current = query_effect_call(&EXECUTION_query_current_row_call, &context_type, &context_slot, 1, row);
    pin_value get[] = { row, { .type = PIN_I32 } };
    int get_slots[] = { current, -1 };
    int selected = query_effect_call(&EXECUTION_query_row_get_call, get, get_slots, 2, pointer);
    pin_value reference = { .type = BF_POINTER_TYPE };
    int root = query_effect_call(&EXECUTION_pointer_reference_call, &pointer, &selected, 1, reference);
    pin_value field[] = { reference,
                          { .type = PIN_I32 },
                          { .type = PIN_U64, .u64 = offsetof(fixture_box, value) },
                          { .type = PIN_BOOL } };
    int field_slots[] = { root, -1, -1, -1 };
    int member = query_effect_call(&EXECUTION_reference_field_call, field, field_slots, 4, reference);
    pin_value write[] = { reference, { .type = PIN_I32, .i = 31 } };
    int write_slots[] = { member, -1 };
    query_effect_call(&EXECUTION_reference_write_call, write, write_slots, 2, (pin_value){ .type = PIN_BOOL });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    prepared_plan = execution_module_plan_new();
    prepared_plan->valid = true;
    prepared_plan->id = 985130;
    prepared_plan->query = 0;
    prepared_plan->system_description = 1;
    int *children = xmalloc(2 * sizeof *children);
    children[0] = 1;
    children[1] = 2;
    apush(
        prepared_plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                   .children = children,
                                   .child_count = 2 })
    );
    for (int i = 0; i < 2; i++) {
        apush(
            prepared_plan->queries,
            ((execution_module_query){ .kind = EXECUTION_QUERY_HAS,
                                       .component = components[i] })
        );
    }
    prepared_plan->query_count = 3;
    apush(
        prepared_plan->types,
        ((execution_type_requirement){ .id = 985131,
                                       .storage = PIN_QUERY,
                                       .name = row.type_name,
                                       .size = sizeof(void *) })
    );
    apush(
        prepared_plan->types,
        ((execution_type_requirement){ .id = QUERY_EFFECT_POINTER_TYPE,
                                       .storage = PIN_POINTER,
                                       .name = pointer.type_name,
                                       .size = sizeof(void *) })
    );
    apush(
        prepared_plan->types,
        ((execution_type_requirement){ .id = QUERY_EFFECT_NATIVE_TYPE,
                                       .storage = PIN_STRUCT,
                                       .name = box_type.name,
                                       .size = sizeof(fixture_box),
                                       .layout = type_layout_hash(&box_type) })
    );
    prepared_plan->type_count = 3;
    apush(
        prepared_plan->descriptions,
        ((execution_module_query_description){ .query = 0, .row_type = row, .evaluate = callee })
    );
    prepared_plan->description_count = 1;
    callee = NULL;
    apush(
        prepared_plan->functions,
        ((execution_module_function){ .id = 985132,
                                      .kind = EXECUTION_FUNCTION_SYSTEM,
                                      .body = function })
    );
    prepared_plan->function_count = 1;
    function = NULL;
    char error[256];
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!prepared_module) {
        fprintf(stderr, "query effect module: %s\n", error);
    }
    REQUIRE(prepared_module);
    return true;
}

TEST_CASE(execution_query_row_effects_account_for_every_result_assignment)
{
    for (int variant = 0; variant < 5; variant++) {
        cleanup();
        REQUIRE(query_effect_plan(variant));
        if (!variant) {
            type_field incompatible_field = box_field;
            incompatible_field.type = PIN_F32;
            incompatible_field.type_name = "f32";
            type_desc incompatible_type = box_type;
            incompatible_type.fields = &incompatible_field;
            pointer_world = ecs_world_new();
            for (int i = 0; i < 2; i++) {
                ecs_component_desc component = {
                    .id = 9196 + (uint64_t)i,
                    .key = i ? "fixture.other" : "fixture.original",
                    .name = box_type.name,
                    .element_size = sizeof(fixture_box),
                    .type = i ? &box_type : &incompatible_type
                };
                REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
            }
            pointer_program = execution_program_new(prepared_module, pointer_world, NULL);
            REQUIRE(!pointer_program);
            ecs_world_free(pointer_world);
            pointer_world = NULL;
        }
        pointer_world = ecs_world_new();
        for (int i = 0; i < 2; i++) {
            ecs_component_desc component = { .id = 9196 + (uint64_t)i,
                                             .key = i ? "fixture.other" : "fixture.original",
                                             .name = box_type.name,
                                             .element_size = sizeof(fixture_box),
                                             .type = &box_type };
            REQUIRE(ecs_world_register_component(pointer_world, &component, NULL) == ECS_OK);
        }
        ecs_entity entity = ecs_world_create_entity(pointer_world);
        fixture_box initial = { 1 };
        REQUIRE(ecs_world_record_add(pointer_world, entity, 9196, &initial, 0) == ECS_OK);
        initial.value = 2;
        REQUIRE(ecs_world_record_add(pointer_world, entity, 9197, &initial, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(pointer_world) == ECS_OK);
        for (int prepared = 0; prepared < 2; prepared++) {
            if (prepared) {
                size_t length;
                char error[256];
                bool written = execution_prepared_write(
                    prepared_module,
                    &prepared_bytes,
                    &length,
                    error,
                    sizeof error
                );
                if (!written) {
                    fprintf(stderr, "query effects prepared write variant=%d: %s\n", variant, error);
                }
                REQUIRE(written);
                execution_module_free(prepared_module);
                const execution_prepared_linker linker = { .type = query_effect_native_type };
                prepared_module = execution_prepared_read(
                    prepared_bytes,
                    length,
                    &linker,
                    BEHAVIOR_BYTECODE,
                    error,
                    sizeof error
                );
                if (!prepared_module) {
                    fprintf(stderr, "query effects prepared read variant=%d: %s\n", variant, error);
                }
                REQUIRE(prepared_module);
            }
            execution_effect_summary effects = execution_module_effects(prepared_module);
            REQUIRE(effects.declared == (variant < 3));
            REQUIRE(effects.parallel_safe == (variant < 3));
            bool reads_original = false, reads_other = false;
            for (int i = 0; i < effects.read_count; i++) {
                reads_original |= effects.reads[i] == 9196;
                reads_other |= effects.reads[i] == 9197;
            }
            REQUIRE(reads_original && reads_other);
            if (variant < 3) {
                REQUIRE(effects.write_count == 1 && effects.writes[0] == 9196);
            } else {
                REQUIRE(effects.world_read && effects.unknown_operation >= 0);
            }
            ((fixture_box *)ecs_component_data(pointer_world, entity, 9196))->value = 1;
            ((fixture_box *)ecs_component_data(pointer_world, entity, 9197))->value = 2;
            pointer_program = execution_program_new(prepared_module, pointer_world, NULL);
            REQUIRE(pointer_program);
            int status = execution_program_prepare_system(pointer_program, 0, .016, 1000);
            if (status != FLOW_OK) {
                const execution_program_failure *failure = execution_program_error(pointer_program);
                fprintf(
                    stderr,
                    "query effects variant=%d prepared=%d prepare=%d "
                    "error=%s\n",
                    variant,
                    prepared,
                    status,
                    failure ? failure->message : ""
                );
            }
            REQUIRE(status == FLOW_OK);
            REQUIRE(execution_program_run_system(pointer_program) == FLOW_OK);
            REQUIRE(execution_program_finish_system(pointer_program) == FLOW_OK);
            REQUIRE(
                ((fixture_box *)ecs_component_data(pointer_world, entity, 9196))->value ==
                (variant < 3 ? 31 : 1)
            );
            REQUIRE(
                ((fixture_box *)ecs_component_data(pointer_world, entity, 9197))->value ==
                (variant < 3 ? 2 : 31)
            );
            execution_program_free(pointer_program);
            pointer_program = NULL;
        }
    }
    return true;
}
TEST_CASE(execution_sequence_values_copy_at_ownership_boundaries)
{
    const char *source =
        "package sequences\n"
        "Change :: (xs: int[]) -> int[] { xs.append(9); return xs; }\n"
        "Pair :: () -> (left: int[], right: int[]) { return ([1], [2]); }\n"
        "R :: struct { x: int; }\n"
        "pub Assignment :: () -> int { a: int[] = [1]; b := a; b.append(2); "
        "b[0] = 9; return length(a) * 100 + a[0] * 10 + length(b); }\n"
        "pub Arguments :: () -> int { a: int[] = [1]; b := Change(a); "
        "return length(a) * 10 + length(b); }\n"
        "pub NestedArrays :: () -> int { a: int[][] = [[1], [2]]; b := a; "
        "b[0].append(3); b[1][0] = 9; "
        "return length(a[0]) * 100 + a[1][0] * 10 + length(b[0]); }\n"
        "pub NestedMapArrays :: () -> int { a: [int: int[]] = [1: [4, 5]]; b "
        ":= a; "
        "b[1].append(6); b[2] = [7]; "
        "return length(a[1]) * 100 + length(b[1]) * 10 + length(a[2]); }\n"
        "pub NestedMaps :: () -> int { a: [int: [int: int]] = [1: [2: 3]]; b "
        ":= a; "
        "b[1][2] = 9; return a[1][2] * 10 + b[1][2]; }\n"
        "pub Records :: () -> int { a: R[] = []; a.append({x: 4}); b := a; "
        "b[0].x = 9; return a[0].x * 10 + b[0].x; }\n"
        "pub Clone :: () -> int { a: int[] = [1]; b := a.clone(); b[0] = 9; "
        "return a[0] * 10 + b[0]; }\n"
        "pub PointerIdentity :: () -> int { a: int[] = [1]; p := &a; q := p; b "
        ":= a; "
        "p.append(2); q.append(3); return length(a) * 100 + length(b) * 10 + "
        "length(q^); }\n"
        "pub TupleAlias :: () -> int { p := Pair(); if (length(p.left) > 0) { "
        "q := p; seen := q.left[0]; } "
        "return p.left[0] * 10 + p.right[0]; }\n";
    const sequence_case cases[] = {
        { "Assignment", 112 },   { "Arguments", 12 },
        { "NestedArrays", 122 }, { "NestedMapArrays", 230 },
        { "NestedMaps", 39 },    { "Records", 49 },
        { "Clone", 19 },         { "PointerIdentity", 313 },
        { "TupleAlias", 12 },
    };
    return sequence_results(source, cases, sizeof cases / sizeof *cases);
}
/* A local that reads an unchanged local in place shares its collection, so
 * leaving the local's scope must leave that collection to its owner. */
TEST_CASE(execution_scoped_lent_locals_leave_their_owner_intact)
{
    /* Each owner is built by a call, so its length and contents are read at
     * run time after the lent local's scope has exited. */
    const char *source =
        "package sequences\n"
        "Pair :: union { Row(xs: int[]) | None }\n"
        "Make :: (n: int) -> int[] { xs: int[] = []; "
        "for i in 0..<n { xs.append(i + 1); } return xs; }\n"
        "Shape :: (xs: int[]) -> int { return length(xs) * 10 + xs[length(xs) "
        "- 1]; }\n"
        "Width :: (p: Pair) -> int { switch p { case .Row(xs): return "
        "Shape(xs); "
        "case: return -1; } }\n"
        "Param :: (xs: int[], flag: bool) -> int { if flag { alias := xs; "
        "if length(alias) == 0 { return -1; } } return Shape(xs); }\n"
        /* Locals lent a whole local. */
        "pub Scoped :: () -> int { xs := Make(2); if length(xs) > 0 { "
        "alias := xs; if length(alias) == 0 { return -1; } } return Shape(xs); "
        "}\n"
        "pub Passed :: () -> int { xs := Make(4); "
        "n := Param(xs, true); return n * 100 + Shape(xs); }\n"
        "pub PassedFalse :: () -> int { xs := Make(4); "
        "n := Param(xs, false); return n * 100 + Shape(xs); }\n"
        "pub Broken :: () -> int { xs := Make(3); total := 0; "
        "for i in 0..<3 { alias := xs; total += length(alias); "
        "if i == 1 { break; } } return total * 100 + Shape(xs); }\n"
        "pub Continued :: () -> int { xs := Make(3); total := 0; "
        "for i in 0..<3 { alias := xs; if i == 1 { continue; } "
        "total += length(alias); } return total * 100 + Shape(xs); }\n"
        "pub Returned :: () -> int { xs := Make(3); total := 0; "
        "for i in 0..<3 { alias := xs; total += length(alias); "
        "if i == 1 { return total * 100 + Shape(xs); } } return -1; }\n"
        "pub Whole :: () -> int { held := Pair.Row(Make(3)); "
        "if Width(held) > 0 { alias := held; if Width(alias) < 0 { return -1; "
        "} } "
        "return Width(held); }\n"
        /* Locals and loop items lent an element. */
        "pub Looped :: () -> int { rows: int[][] = [Make(3), Make(1)]; total "
        ":= 0; "
        "for i in 0..<3 { row := rows[0]; total += length(row); } "
        "return total * 100 + Shape(rows[0]); }\n"
        "pub RowBroken :: () -> int { rows: int[][] = [Make(3), Make(1)]; "
        "total := 0; "
        "for i in 0..<3 { row := rows[0]; total += length(row); "
        "if i == 1 { break; } } return total * 100 + Shape(rows[0]); }\n"
        "pub RowContinued :: () -> int { rows: int[][] = [Make(3), Make(1)]; "
        "total := 0; for i in 0..<3 { row := rows[0]; if i == 1 { continue; } "
        "total += length(row); } return total * 100 + Shape(rows[0]); }\n"
        "pub RowReturned :: () -> int { rows: int[][] = [Make(3), Make(1)]; "
        "total := 0; for i in 0..<3 { row := rows[0]; total += length(row); "
        "if i == 1 { return total * 100 + Shape(rows[0]); } } return -1; }\n"
        "pub Element :: () -> int { pairs: Pair[] = [Pair.Row(Make(2)), "
        "Pair.None]; "
        "if length(pairs) > 0 { pair := pairs[0]; if Width(pair) < 0 { return "
        "-1; } } "
        "return Width(pairs[0]); }\n"
        "pub Items :: () -> int { rows: int[][] = [Make(3), Make(1)]; total := "
        "0; "
        "for row in rows { total += length(row); } "
        "return total * 10000 + Shape(rows[0]) * 100 + Shape(rows[1]); }\n"
        "pub ItemsBroken :: () -> int { rows: int[][] = [Make(3), Make(1)]; "
        "total := 0; for row in rows { total += length(row); "
        "if length(row) == 3 { break; } } "
        "return total * 10000 + Shape(rows[0]) * 100 + Shape(rows[1]); }\n";
    const sequence_case cases[] = {
        { "Scoped", 22 },        { "Passed", 4444 },
        { "PassedFalse", 4444 }, { "Broken", 633 },
        { "Continued", 633 },    { "Returned", 633 },
        { "Whole", 33 },         { "Looped", 933 },
        { "RowBroken", 633 },    { "RowContinued", 633 },
        { "RowReturned", 633 },  { "Element", 22 },
        { "Items", 43311 },      { "ItemsBroken", 33311 },
    };
    return sequence_results(source, cases, sizeof cases / sizeof *cases);
}
TEST_CASE(execution_strict_sequence_ranges_are_optional_and_copied)
{
    const char *source =
        "package sequences\n"
        "pub Bounds :: () -> int { a: int[] = [1, 2, 3, 4]; "
        "negative := a[-1..<2]; if negative { return -1; } "
        "reversed := a[3..<2]; if reversed { return -2; } "
        "past := a.slice(0..<5); if past { return -3; } "
        "outside := a[5..<5]; if outside { return -4; } "
        "empty := a[0..<0]; if empty { tail := a.slice(4..<4); if tail { "
        "whole := a[0..<4]; if whole { return length(empty) + length(tail) + "
        "length(whole); } } } "
        "return -5; }\n"
        "pub CopiedRanges :: () -> int { a: int[] = [1, 2, 3]; b := a[0..<2]; "
        "c := a.slice(1..<3); if b { if c { b[0] = 9; c[0] = 8; "
        "return a[0] * 100 + a[1] * 10 + b[0] + c[0]; } } return -1; }\n"
        "pub Clamped :: () -> int { a: int[] = [1, 2, 3]; b := a.slice(2, 99); "
        "b[0] = 8; c := a.slice(99, 4); "
        "return a[2] * 100 + length(b) * 10 + length(c) + b[0]; }\n"
        "pub Bytes :: () -> int { text: string = \"a\\u0000bc\"; "
        "part := text[1..<3]; if part { copied := part.clone(); "
        "window := text.view(1..<3); if window { "
        "return copied.length() * 100 + copied[0] + copied[1] + window[0] + "
        "window[1]; } } return -1; }\n";
    const sequence_case cases[] = {
        { "Bounds", 4 },
        { "CopiedRanges", 137 },
        { "Clamped", 318 },
        { "Bytes", 396 },
    };
    return sequence_results(source, cases, sizeof cases / sizeof *cases);
}
TEST_CASE(execution_pointer_views_follow_rebinding_and_fixed_positions)
{
    const char *
        source = "package sequences\n"
                 "Pass :: (v: view int[]) -> view int[] { return v; }\n"
                 "pub Rebound :: () -> int { a: int[] = [1, 2, 3, 4]; p := &a; "
                 "held := p.view(1..<3); if held { v := Pass(held); b := a; "
                 "v[0] = 20; "
                 "p^ = [5, 6, 7, 8]; v[1] = 70; "
                 "return a[1] * 100 + a[2] * 10 + b[1]; } return -1; }\n"
                 "pub Regrown :: () -> int { a: int[] = [1, 2, 3]; p := &a; "
                 "held := p.view(1..<3); if held { p^ = [9]; expired := "
                 "held.slice(0..<0); "
                 "if expired { return -2; } p.append(8); p.append(7); "
                 "if held { return held[0] * 10 + held[1]; } } return -1; }\n"
                 "pub EmptyWindow :: () -> int { a: int[] = [1, 2]; p := &a; "
                 "held := p.view(2..<2); if held { p^ = [9]; expired := "
                 "held.slice(0..<0); "
                 "if expired { return -2; } p.append(8); if held { "
                 "restored := held.slice(0..<0); "
                 "if restored { return length(held) + length(restored); } } } "
                 "return -1; }\n";
    const sequence_case cases[] = {
        { "Rebound", 1302 },
        { "Regrown", 87 },
        { "EmptyWindow", 0 },
    };
    return sequence_results(source, cases, sizeof cases / sizeof *cases);
}
TEST_CASE(execution_sequence_places_preserve_effectful_evaluation_order)
{
    const char *source =
        "package sequences\n"
        "Grow :: (p: ^(int[])) -> int { p.append(9); return 0; }\n"
        "Observe :: (xs: int[], ignored: int) -> int { return xs[0]; }\n"
        "Replace :: (p: ^(int[])) -> int { p^ = [8, 9]; return 7; }\n"
        "Index :: (p: ^(int[])) -> int { p^ = [8, 9]; return 0; }\n"
        "Next :: (p: ^int) -> int { n := p^; p^ += 1; return n; }\n"
        "ReplaceNested :: (p: ^(int[][])) -> int { p^ = [[8, 9]]; return 7; }\n"
        "pub Arguments :: () -> int { a: int[] = [1]; observed := Observe(a, "
        "Grow(&a)); "
        "return observed * 10 + length(a); }\n"
        "pub ReceiverIndex :: () -> int { a: int[] = [1, 2]; value := "
        "a[Index(&a)]; "
        "return value * 10 + a[0]; }\n"
        "pub Assignment :: () -> int { a: int[] = [1, 2]; a[0] = Replace(&a); "
        "return a[0] * 10 + a[1]; }\n"
        "pub MethodReceiver :: () -> int { a: int[] = [1]; "
        "a.append(Replace(&a)); "
        "return length(a) * 100 + a[0] * 10 + a[2]; }\n"
        "pub Compound :: () -> int { a: int[] = [1, 2]; index: int = 0; "
        "a[Next(&index)] += Replace(&a); return index * 100 + a[0] * 10 + "
        "a[1]; }\n"
        "pub NestedPlace :: () -> int { a: int[][] = [[1, 2]]; "
        "a[0][1] = ReplaceNested(&a); return a[0][0] * 10 + a[0][1]; }\n";
    const sequence_case cases[] = {
        { "Arguments", 12 },  { "ReceiverIndex", 18 },
        { "Assignment", 79 }, { "MethodReceiver", 387 },
        { "Compound", 189 },  { "NestedPlace", 87 },
    };
    return sequence_results(source, cases, sizeof cases / sizeof *cases);
}
TEST_CASE(execution_calls_lend_arguments_and_callees_copy_writes)
{
    const char *source =
        "package sequences\n"
        "Row :: struct { key: string; depth: int; }\n"
        "Change :: (row: Row, xs: int[], nested: int[][]) -> int { "
        "row.key = \"changed\"; row.depth = 9; nested[0].append(5); "
        "xs.append(7); xs[0] = 9; return length(xs) * 10 + length(nested[0]); "
        "}\n"
        "Same :: (xs: int[]) -> int[] { return xs; }\n"
        "Read :: (nested: int[][], xs: int[]) -> int { "
        "return length(nested[0]) * 10 + length(xs); }\n"
        "Twice :: (xs: int[]) -> int { return Read([xs], xs); }\n"
        "Reassign :: (xs: int[]) -> int { xs = [7, 8, 9]; xs.append(1); "
        "return length(xs); }\n"
        "Bump :: (rows: Row[]) -> int { rows[0].depth += 3; return "
        "rows[0].depth; }\n"
        "Through :: (xs: int[], p: ^(int[])) -> int { p.append(9); "
        "return length(xs); }\n"
        "pub Writes :: () -> int { xs: int[] = [1]; nested: int[][] = [[1, "
        "2]]; "
        "row: Row = {key: \"a\", depth: 1}; n := Change(row, xs, nested); "
        "if row.key != \"a\" or row.depth != 1 or xs[0] != 1 { return -1; } "
        "return n * 100 + length(nested[0]) * 10 + length(xs); }\n"
        "pub Returns :: () -> int { xs: int[] = [1, 2]; kept := Same(xs); "
        "xs.append(3); xs[0] = 5; "
        "return kept[0] * 100 + length(kept) * 10 + length(xs); }\n"
        "pub Temporaries :: () -> int { "
        "return Read([[1, 2, 3]], Same([4, 5])) + Twice([6]); }\n"
        "pub Reassigned :: () -> int { xs: int[] = [1]; n := Reassign(xs); "
        "return n * 10 + length(xs); }\n"
        "pub Elements :: () -> int { rows: Row[] = [{key: \"a\", depth: 1}]; "
        "n := Bump(rows); m := Bump(rows); return n * 100 + m * 10 + "
        "rows[0].depth; }\n"
        "pub Pointed :: () -> int { a: int[] = [1]; p := &a; n := Through(a, "
        "p); "
        "return n * 10 + length(a); }\n"
        "Behind :: (xs: int[], p: ^(int[][])) -> int { p^[0].append(9); "
        "return length(xs); }\n"
        "pub PointedElement :: () -> int { a: int[][] = [[1]]; p := &a; "
        "n := Behind(p^[0], p); return n * 10 + length(a[0]); }\n"
        "Grow :: (xs: ^(int[])) -> int { xs^[0] = 5; return 0; }\n"
        "Observe :: (xs: int[], ignored: int) -> int { return xs[0]; }\n"
        "pub ReferencedLater :: () -> int { a: int[] = [1]; "
        "n := Observe(a, Grow(&a)); return n * 10 + a[0]; }\n"
        "Peek :: (p: ^(int[]), xs: int[]) -> int { p^[0] = 5; return xs[0]; "
        "}\n"
        "pub ReferencedEarlier :: () -> int { a: int[] = [1]; "
        "n := Peek(&a, a); return n * 10 + a[0]; }\n"
        "pub LocalAfterWrite :: () -> int { rows: Row[] = [{key: \"a\", depth: "
        "1}]; "
        "row := rows[0]; rows[0].depth = 7; nested: int[][] = [[1]]; inner := "
        "nested[0]; "
        "nested[0].append(2); return row.depth * 10 + length(inner); }\n"
        "Later :: (rows: Row[]) -> int { row := rows[0]; rows[0].depth = 9; "
        "return row.depth; }\n"
        "pub ParameterAfterWrite :: () -> int { rows: Row[] = [{key: \"a\", "
        "depth: 1}]; "
        "return Later(rows); }\n"
        "pub MovedInLoop :: () -> int { total: int = 0; i: int = 0; "
        "while (i < 3) { xs: int[] = [i, i]; made: Row = {key: \"k\", depth: "
        "i}; "
        "made.depth += 1; total += length(xs) + made.depth - 2; i += 1; } "
        "return total; }\n"
        "pub LoopAfterWrite :: () -> int { rows: Row[] = [{key: \"a\", depth: "
        "1}, "
        "{key: \"b\", depth: 2}]; total: int = 0; for row in rows { "
        "rows[1].depth = 9; "
        "rows.append({key: \"c\", depth: 3}); total = total * 10 + row.depth; "
        "} "
        "return total * 10 + length(rows); }\n"
        "pub FieldAfterWrite :: () -> int { rows: Row[] = [{key: \"a\", depth: "
        "1}]; "
        "first := rows[0].depth; rows[0] = {key: \"b\", depth: 5}; "
        "return first * 10 + rows[0].depth; }\n";
    const sequence_case cases[] = {
        { "Writes", 2321 },
        { "Returns", 123 },
        { "Temporaries", 43 },
        { "LocalAfterWrite", 11 },
        { "ParameterAfterWrite", 1 },
        { "MovedInLoop", 6 },
        { "LoopAfterWrite", 124 },
        { "FieldAfterWrite", 15 },
        { "Reassigned", 41 },
        { "Elements", 441 },
        { "Pointed", 12 },
        { "PointedElement", 12 },
        { "ReferencedLater", 15 },
        { "ReferencedEarlier", 15 },
    };
    return sequence_results(source, cases, sizeof cases / sizeof *cases);
}
TEST_CASE(execution_lent_records_copy_nothing)
{
    const char *
        source = "package sequences\n"
                 "Row :: struct { key: string; label: string; depth: int; }\n"
                 "Depth :: (row: Row, rows: Row[]) -> int { "
                 "return row.depth + length(rows) + 1; }\n"
                 "pub Calls :: (count: int) -> int { "
                 "row: Row = {key: \"k\", label: \"label\", depth: 1}; "
                 "rows: Row[] = [row, row]; total: int = 0; i: int = 0; "
                 "while (i < count) { item := row; "
                 "total += Depth(item, rows); i += 1; } "
                 "return total; }\n"
                 "Find :: (rows: Row[], depth: int) -> int { i: int = 0; "
                 "while (i < length(rows)) { found := rows[i]; "
                 "if (found.depth == depth) { return i; } i += 1; } return -1; "
                 "}\n"
                 "Sum :: (rows: Row[]) -> int { total: int = 0; "
                 "for row in rows { total += row.depth; } i: int = 0; "
                 "while (i < length(rows)) { total += rows[i].depth; i += 1; } "
                 "return total; }\n"
                 "pub Scans :: (count: int) -> int { "
                 "row: Row = {key: \"k\", label: \"label\", depth: 1}; "
                 "rows: Row[] = [row, row]; total: int = 0; i: int = 0; "
                 "while (i < count) { total += Find(rows, 2) + Sum(rows) - 2; "
                 "i += 1; } return total; }\n"
                 "pub Count :: (total: ^int, rows: Row[]) { total^ += "
                 "length(rows); "
                 "}\n"
                 "pub Scoped :: (count: int) -> int { "
                 "row: Row = {key: \"k\", label: \"label\", depth: 1}; "
                 "rows: Row[] = [row, row]; total: int = 0; i: int = 0; "
                 "while (i < count) { Count(&total, rows); i += 1; } "
                 "return total / 2; }\n"
                 "pub Natives :: (count: int) -> int { "
                 "row: Row = {key: \"k\", label: \"label\", depth: 1}; "
                 "rows: Row[] = [row, row]; total: int = 0; i: int = 0; "
                 "while (i < count) { if (rows.contains(row)) { total += 1; } "
                 "i += 1; } return total; }\n"
                 "pub Appends :: (count: int) -> int { "
                 "row: Row = {key: \"k\", label: \"label\", depth: 1}; "
                 "rows: Row[] = [row, row]; total: int = 0; i: int = 0; "
                 "while (i < count) { grown := append(rows, row); "
                 "total += length(grown) - 2; i += 1; } return total; }\n"
                 "pub Builds :: (count: int) -> int { "
                 "row: Row = {key: \"k\", label: \"label\", depth: 1}; "
                 "total: int = 0; i: int = 0; "
                 "while (i < count) { grown: Row[] = [row, row, row]; "
                 "total += length(grown) - 2; i += 1; } return total; }\n";
    source_package_input input = { "sequences/Main.chibi", source, strlen(source) };
    REQUIRE(compiler_fixture_prepare(&sequences, &input, 1));
    /* A lent argument or local, an indexed local, a for-each item, a field
     * read through an index, an argument to a native that only reads it and
     * an argument beside a scoped pointer the callee writes copy no record. */
    const char *functions[] = { "Calls", "Scans", "Natives", "Scoped" };
    const int scales[] = { 4, 1, 1, 1 };
    for (int f = 0; f < 4; f++) {
        for (int backend = 0; backend < 2; backend++) {
            uint64_t records[2];
            const int counts[] = { 1, 50 };
            for (int i = 0; i < 2; i++) {
                pin_value argument = { .type = PIN_I32, .i = counts[i] },
                          result = { 0 };
                pin_copy_scope copies = { 0 };
                pin_copy_scope_begin(&copies);
                bool called = compiler_fixture_call(
                    &sequences,
                    backend,
                    "project:sequences",
                    functions[f],
                    &argument,
                    1,
                    100000,
                    &storage,
                    &result
                );
                pin_copy_scope_end(&copies);
                REQUIRE(called && result.type == PIN_I32 && result.i == counts[i] * scales[f]);
                records[i] = copies.counts.record_bytes;
                arena_free(&storage);
            }
            REQUIRE(records[1] == records[0]);
        }
    }
    /* Writing through a scoped pointer leaves the other arguments lent: the
     * caller lends none that shares the pointer's location. */
    for (int backend = 0; backend < 2; backend++) {
        const authoring_package *package = compiler_fixture_package(&sequences, backend, "project:sequences");
        uint64_t count = compiler_fixture_export(&sequences, backend, "project:sequences", "Count");
        REQUIRE(package && package->module && count);
        const behavior_function *body = NULL;
        for (int i = 0; i < execution_module_function_count(package->module); i++) {
            execution_module_function_info
                info = execution_module_function_at(package->module, i);
            if (info.id == count) {
                body = info.body;
            }
        }
        REQUIRE(body && body->argument_sharing == BEHAVIOR_SHARING_SAFE);
    }
    /* Appending copies only the item it adds, a third of what a literal of
     * three items copies, and never the lent array. */
    for (int backend = 0; backend < 2; backend++) {
        uint64_t grown[2];
        for (int f = 0; f < 2; f++) {
            uint64_t records[2];
            const int counts[] = { 1, 50 };
            for (int i = 0; i < 2; i++) {
                pin_value argument = { .type = PIN_I32, .i = counts[i] },
                          result = { 0 };
                pin_copy_scope copies = { 0 };
                pin_copy_scope_begin(&copies);
                bool called = compiler_fixture_call(
                    &sequences,
                    backend,
                    "project:sequences",
                    f ? "Builds" : "Appends",
                    &argument,
                    1,
                    100000,
                    &storage,
                    &result
                );
                pin_copy_scope_end(&copies);
                REQUIRE(called && result.type == PIN_I32 && result.i == counts[i]);
                records[i] = copies.counts.record_bytes;
                arena_free(&storage);
            }
            grown[f] = records[1] - records[0];
        }
        REQUIRE(grown[0] > 0 && grown[0] * 3 == grown[1]);
    }
    return true;
}

static bool query_prepared_plan_rejected()
{
    const execution_module forged = { .plan = *prepared_plan,
                                      .backend = BEHAVIOR_BYTECODE,
                                      .references = 1 };
    size_t length = 0;
    char error[256];
    REQUIRE(execution_prepared_write(&forged, &prepared_bytes, &length, error, sizeof error));
    execution_module *loaded = execution_prepared_read(
        prepared_bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    REQUIRE(!loaded);
    REQUIRE(strstr(error, "query"));
    free(prepared_bytes);
    prepared_bytes = NULL;
    return true;
}
TEST_CASE(execution_query_evaluators_reject_indirect_result_alias_writes)
{
    const pin_record *record = pin_record_create(&managed, "fixture.query_alias", 985134, &box_field, 1);
    REQUIRE(record);
    pin_value aggregate = { .type = PIN_STRUCT,
                            .type_name = record->type.name,
                            .record = record };
    pin_value results[] = { { .type = PIN_BOOL }, aggregate };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &aggregate, 1, results, 2, NULL, 0);
    int alias = address_slot(), destination = address_slot(), field = address_slot();
    int index = slot(PIN_I32), number = slot(PIN_I32), accepted = slot(PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_ARRAY,
                                          .type = BF_POINTER_TYPE,
                                          .target = BF_ARGUMENT,
                                          .value = aggregate,
                                          .output = alias });
    emit((behavior_function_instruction){
        .op = BF_CONTEXT,
        .type = BF_POINTER_TYPE,
        .offset = offsetof(behavior_function_context, results),
        .output = destination });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .value = { .type = PIN_I32, .i = 1 },
                                          .output = index });
    emit((behavior_function_instruction){ .op = BF_INDEX,
                                          .type = BF_POINTER_TYPE,
                                          .left = destination,
                                          .right = index,
                                          .offset = sizeof(pin_value),
                                          .output = field });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = BF_POINTER_TYPE,
                                          .left = alias,
                                          .right = field,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .value = { .type = PIN_I32, .i = 9 },
                                          .output = number });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_I32, .left = number, .target = 1, .indirect = true });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .value = { .type = PIN_BOOL, .b = true },
                                          .output = accepted });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = accepted });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(!execution_query_evaluator_valid(function, 1));
    prepared_plan = execution_module_plan_new();
    prepared_plan->valid = true;
    prepared_plan->id = 985135;
    pin_value row = { .type = PIN_QUERY, .type_name = "fixture.query_indirect" };
    apush(
        prepared_plan->types,
        ((execution_type_requirement){ .id = 985136,
                                       .storage = PIN_QUERY,
                                       .name = row.type_name,
                                       .size = sizeof(void *) })
    );
    apush(
        prepared_plan->types,
        ((execution_type_requirement){ .id = record->id,
                                       .storage = PIN_STRUCT,
                                       .name = record->type.name,
                                       .size = record->type.size,
                                       .record = record,
                                       .layout = record->layout })
    );
    prepared_plan->type_count = 2;
    apush(prepared_plan->queries, ((execution_module_query){ .kind = EXECUTION_QUERY_ALL }));
    prepared_plan->query_count = 1;
    apush(
        prepared_plan->descriptions,
        ((execution_module_query_description){ .query = 0, .row_type = row, .evaluate = function })
    );
    prepared_plan->description_count = 1;
    function = NULL;
    char error[256];
    REQUIRE(!execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error));
    REQUIRE(strstr(error, "query"));
    REQUIRE(query_prepared_plan_rejected());
    return true;
}
TEST_CASE(execution_query_evaluators_reject_borrowed_collection_mutation)
{
    for (int variant = 0; variant < 5; variant++) {
        cleanup();
        pin_value array = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY };
        pin_array *items = pin_array_snapshot_allocate(&managed, PIN_I32, NULL, 1);
        items->items[0] = (pin_value){ .type = PIN_I32, .i = 1 };
        pin_value borrowed = array;
        borrowed.ptr = items;
        builder = behavior_function_builder_new();
        pin_value result = { .type = PIN_BOOL };
        behavior_function_builder_signature(builder, &array, 1, &result, 1, NULL, 0);
        int local = behavior_function_builder_values(builder, &array, 1);
        int source = address_slot(), target = address_slot();
        emit((behavior_function_instruction){
            .op = BF_ARRAY,
            .type = BF_POINTER_TYPE,
            .target = variant == 1 ? BF_CONSTANT : BF_ARGUMENT,
            .value = variant == 1 ? borrowed : array,
            .output = source });
        if (variant >= 2) {
            int snapshot = behavior_function_builder_host(
                builder,
                &EXECUTION_snapshot_call,
                &EXECUTION_value_access,
                &array,
                1,
                &array,
                1
            );
            uint64_t id = execution_intrinsic_id(&EXECUTION_snapshot_call);
            REQUIRE(
                behavior_function_builder_host_identity(builder, snapshot, id, execution_intrinsic_abi(id))
            );
            int header = address_slot();
            emit((behavior_function_instruction){
                .op = BF_HOST_SLOT,
                .type = BF_POINTER_TYPE,
                .left = behavior_function_builder_host_first(builder, snapshot),
                .output = header });
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = BF_POINTER_TYPE,
                                                  .left = source,
                                                  .right = header,
                                                  .offset = offsetof(pin_value, d) });
            if (variant == 2 || variant == 4) {
                const size_t offsets[] = { offsetof(pin_value, type),
                                           offsetof(pin_value, container) };
                const int values[] = { PIN_HANDLE, PIN_CONTAINER_SINGLE };
                for (int field = 0; field < 2; field++) {
                    int offset = slot(BF_POINTER_TYPE), value = slot(PIN_I32),
                        alias = address_slot();
                    emit((behavior_function_instruction){
                        .op = BF_CONSTANT,
                        .type = BF_POINTER_TYPE,
                        .output = offset,
                        .value = { .type = BF_POINTER_TYPE, .u64 = offsets[field] } });
                    emit((behavior_function_instruction){ .op = BF_ADD,
                                                          .type = BF_POINTER_TYPE,
                                                          .output = alias,
                                                          .left = header,
                                                          .right = offset });
                    emit((behavior_function_instruction){
                        .op = BF_CONSTANT,
                        .type = PIN_I32,
                        .output = value,
                        .value = { .type = PIN_I32, .i = values[field] } });
                    emit((behavior_function_instruction){ .op = BF_WRITE,
                                                          .type = PIN_I32,
                                                          .left = value,
                                                          .right = alias });
                }
            }
            emit((behavior_function_instruction){
                .op = BF_HOST_CALL,
                .target = snapshot,
                .left = BEHAVIOR_FUNCTION_HOST_DATA,
                .right = variant == 3 ? BF_HOST_BORROW_ARRAY : 0 });
            if (variant == 4) {
                local = behavior_function_builder_host_results(builder, snapshot);
            }
            int returned = address_slot();
            source = address_slot();
            emit((behavior_function_instruction){
                .op = BF_HOST_SLOT,
                .type = BF_POINTER_TYPE,
                .left = behavior_function_builder_host_results(builder, snapshot),
                .output = returned });
            emit((behavior_function_instruction){ .op = BF_LOAD,
                                                  .type = BF_POINTER_TYPE,
                                                  .left = returned,
                                                  .offset = offsetof(pin_value, d),
                                                  .output = source });
        }
        emit((behavior_function_instruction){ .op = BF_HOST_SLOT,
                                              .type = BF_POINTER_TYPE,
                                              .left = local,
                                              .output = target });
        if (variant != 4) {
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = BF_POINTER_TYPE,
                                                  .left = source,
                                                  .right = target,
                                                  .offset = offsetof(pin_value, d) });
        }
        pin_value arguments[] = { { .type = BF_POINTER_TYPE },
                                  { .type = PIN_I32 },
                                  { .type = PIN_I32, .i = 9 } };
        int host = behavior_function_builder_host(
            builder,
            &EXECUTION_array_store_call,
            &EXECUTION_assignment_access,
            arguments,
            3,
            &result,
            1
        );
        int first = behavior_function_builder_host_first(builder, host);
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first, BEHAVIOR_ADDRESS));
        uint64_t id = execution_intrinsic_id(&EXECUTION_array_store_call);
        REQUIRE(
            behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
        );
        int input = address_slot();
        emit((behavior_function_instruction){ .op = BF_HOST_SLOT,
                                              .type = BF_POINTER_TYPE,
                                              .left = first,
                                              .output = input });
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = BF_POINTER_TYPE,
                                              .left = target,
                                              .right = input,
                                              .offset = offsetof(pin_value, d) });
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        int accepted = slot(PIN_BOOL);
        emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                              .type = PIN_BOOL,
                                              .output = accepted,
                                              .value = { .type = PIN_BOOL, .b = true } });
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = PIN_BOOL,
                                              .left = accepted });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        if (variant == 3) {
            char error[256];
            REQUIRE(!behavior_function_builder_finish(builder, error, sizeof error));
            REQUIRE(strstr(error, "runtime host flags"));
            continue;
        }
        REQUIRE(finish());
        REQUIRE(!execution_query_evaluator_valid(function, 1));
        prepared_plan = execution_module_plan_new();
        prepared_plan->valid = true;
        prepared_plan->id = 985132;
        pin_value row = { .type = PIN_QUERY, .type_name = "fixture.query_borrowed" };
        apush(
            prepared_plan->types,
            ((execution_type_requirement){ .id = 985133,
                                           .storage = PIN_QUERY,
                                           .name = row.type_name,
                                           .size = sizeof(void *) })
        );
        prepared_plan->type_count = 1;
        apush(prepared_plan->queries, ((execution_module_query){ .kind = EXECUTION_QUERY_ALL }));
        prepared_plan->query_count = 1;
        apush(
            prepared_plan->descriptions,
            ((execution_module_query_description){ .query = 0, .row_type = row, .evaluate = function })
        );
        prepared_plan->description_count = 1;
        function = NULL;
        char error[256];
        REQUIRE(!execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error));
        REQUIRE(strstr(error, "query"));
        REQUIRE(query_prepared_plan_rejected());
    }
    return true;
}
TEST_CASE(execution_query_evaluators_reject_unproved_local_release)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_BOOL }, argument = { .type = BF_POINTER_TYPE };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_local_release_call,
        &EXECUTION_assignment_access,
        &argument,
        1,
        &result,
        1
    );
    int first = behavior_function_builder_host_results(builder, host) - 1;
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first, BEHAVIOR_REFERENCE));
    uint64_t id = execution_intrinsic_id(&EXECUTION_local_release_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    int accepted = slot(PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .output = accepted,
                                          .value = { .type = PIN_BOOL, .b = true } });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = accepted });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(!execution_query_predicate_valid(function, 1));
    REQUIRE(!execution_query_evaluator_valid(function, 1));
    prepared_plan = execution_module_plan_new();
    prepared_plan->valid = true;
    prepared_plan->id = 985130;
    pin_value row_type = { .type = PIN_QUERY, .type_name = "fixture.query_release" };
    apush(
        prepared_plan->types,
        ((execution_type_requirement){ .id = 985131,
                                       .storage = PIN_QUERY,
                                       .name = row_type.type_name,
                                       .size = sizeof(void *) })
    );
    prepared_plan->type_count = 1;
    apush(prepared_plan->queries, ((execution_module_query){ .kind = EXECUTION_QUERY_ALL }));
    prepared_plan->query_count = 1;
    apush(
        prepared_plan->descriptions,
        ((execution_module_query_description){ .query = 0,
                                               .row_type = row_type,
                                               .evaluate = function })
    );
    prepared_plan->description_count = 1;
    function = NULL;
    char error[256];
    REQUIRE(!execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error));
    REQUIRE(strstr(error, "query"));
    REQUIRE(query_prepared_plan_rejected());
    return true;
}
TEST_CASE(execution_query_descriptions_reject_unowned_terms_and_null_legacy_gates)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    int accepted = slot(PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .output = accepted,
                                          .value = { .type = PIN_BOOL, .b = true } });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = accepted });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    prepared_plan = execution_module_plan_new();
    prepared_plan->valid = true;
    prepared_plan->id = 985125;
    pin_value row_type = { .type = PIN_QUERY, .type_name = "fixture.query_empty" };
    apush(
        prepared_plan->types,
        ((execution_type_requirement){ .id = 985126,
                                       .storage = PIN_QUERY,
                                       .name = row_type.type_name,
                                       .size = sizeof(void *) })
    );
    prepared_plan->type_count = 1;
    apush(prepared_plan->queries, ((execution_module_query){ .kind = EXECUTION_QUERY_ALL }));
    apush(
        prepared_plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_CHANGED,
                                   .component = 9191,
                                   .source_variable = INT_MAX })
    );
    prepared_plan->query_count = 2;
    apush(
        prepared_plan->descriptions,
        ((execution_module_query_description){ .query = 0,
                                               .row_type = row_type,
                                               .evaluate = function })
    );
    prepared_plan->description_count = 1;
    function = NULL;
    char error[256];
    REQUIRE(!execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error));
    REQUIRE(strstr(error, "reachable"));
    REQUIRE(query_prepared_plan_rejected());

    prepared_plan->queries[1] = (execution_module_query){ .kind = EXECUTION_QUERY_VALUE };
    prepared_plan->query = 1;
    REQUIRE(!execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error));
    REQUIRE(strstr(error, "query"));
    REQUIRE(query_prepared_plan_rejected());

    prepared_plan->query = -1;
    prepared_plan->descriptions[0].query = 1;
    prepared_plan->queries[1].children = xmalloc(sizeof(int));
    prepared_plan->queries[1].children[0] = 0;
    prepared_plan->queries[1].child_count = 1;
    REQUIRE(!execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error));
    REQUIRE(strstr(error, "query"));
    REQUIRE(query_prepared_plan_rejected());

    free(prepared_plan->queries[1].children);
    prepared_plan->queries[1] = (execution_module_query){ .kind = EXECUTION_QUERY_VALUE };
    prepared_plan->queries[0].children = xmalloc(sizeof(int));
    prepared_plan->queries[0].children[0] = 1;
    prepared_plan->queries[0].child_count = 1;
    int *children = xmalloc(2 * sizeof *children);
    children[0] = 0;
    children[1] = 1; /* Shared leaf is outside the descriptor on this path. */
    apush(
        prepared_plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                   .children = children,
                                   .child_count = 2 })
    );
    prepared_plan->query_count = 3;
    prepared_plan->query = 2;
    prepared_plan->system_description = 1;
    prepared_plan->descriptions[0].query = 0;
    REQUIRE(!execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error));
    REQUIRE(strstr(error, "query"));
    REQUIRE(query_prepared_plan_rejected());
    return true;
}
TEST_CASE(execution_jit_usage_preserves_distinct_uses_and_control_entries)
{
    behavior_function f = { 0 };
    asetlen(f.slots, 8);
    asetlen(f.instructions, 7);
    asetlen(f.runtime_map, 8);
    memset(f.runtime_map, 0, 8 * sizeof *f.runtime_map);
    f.instructions[0] = (behavior_function_instruction){ .op = BF_CONSTANT };
    f.instructions[1] = (behavior_function_instruction){ .op = BF_ADD,
                                                         .left = 0,
                                                         .right = 0 };
    f.instructions[2] = (behavior_function_instruction){ .op = BF_SELECT,
                                                         .left = 1,
                                                         .right = 2,
                                                         .target = 3 };
    f.instructions[3] = (behavior_function_instruction){ .op = BF_FMA,
                                                         .left = 2,
                                                         .right = 2,
                                                         .target = 4 };
    f.instructions[4] = (behavior_function_instruction){ .op = BF_BRANCH,
                                                         .left = 5,
                                                         .target = 6 };
    f.instructions[5] = (behavior_function_instruction){ .op = BF_JUMP, .target = 1 };
    f.instructions[6] = (behavior_function_instruction){ .op = BF_CONSTANT };
    int *uses;
    bool *targets;
    behavior_jit_usage(&f, &uses, &targets);
    /* A repeated operand at one PC is one use; a second PC is not. Even
     * instructions omitted by runtime_map participate in the original proof. */
    const int expected[] = { 1, 2, -2, 2, 3, 4, -1, -1, -1 };
    int observed[9];
    memcpy(observed, uses, sizeof observed);
    bool entries[8];
    memcpy(entries, targets, sizeof entries);
    free(uses);
    free(targets);
    f.instructions[6] = (behavior_function_instruction){ .op = BF_NEG, .left = 0 };
    behavior_jit_usage(&f, &uses, &targets);
    int repeated = uses[0];
    free(uses);
    free(targets);
    afree(f.slots);
    afree(f.instructions);
    afree(f.runtime_map);
    for (int i = 0; i < 9; i++) {
        REQUIRE(observed[i] == expected[i]);
    }
    for (int pc = 0; pc < 8; pc++) {
        REQUIRE(entries[pc] == (pc == 1 || pc == 6));
    }
    REQUIRE(repeated == -2);
    return true;
}

void test_language_execution()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(execution_owned_query_intrinsics_bind_the_world_without_component_metadata);
    RUN_TEST_CASE(execution_body_relationship_traversals_allow_later_scene_registration);
    RUN_TEST_CASE(execution_query_row_effects_account_for_every_result_assignment);
    RUN_TEST_CASE(execution_query_native_array_captures_keep_retired_child_schemas);
    RUN_TEST_CASE(execution_query_evaluators_reject_indirect_result_alias_writes);
    RUN_TEST_CASE(execution_query_evaluators_reject_borrowed_collection_mutation);
    RUN_TEST_CASE(execution_query_evaluators_reject_unproved_local_release);
    RUN_TEST_CASE(execution_query_descriptions_reject_unowned_terms_and_null_legacy_gates);
    RUN_TEST_CASE(execution_query_iterators_capture_values_and_recheck_snapshot_membership);
    RUN_TEST_CASE(execution_component_pointers_keep_identity_and_invalidate_removed_targets);
    RUN_TEST_CASE(execution_native_schema_snapshots_keep_nested_text_after_sources_retire);
    RUN_TEST_CASE(execution_component_pointer_keeps_native_text_and_narrow_layouts);
    RUN_TEST_CASE(execution_invalid_component_metadata_is_charged_and_collects_on_update);
    RUN_TEST_CASE(execution_component_references_reject_unavailable_write_paths);
    RUN_TEST_CASE(execution_component_targets_do_not_revive_after_lookup_loss_or_entity_reuse);
    RUN_TEST_CASE(execution_component_pointer_reductions_use_the_consumer_and_live_identity);
    RUN_TEST_CASE(execution_jit_usage_preserves_distinct_uses_and_control_entries);
    RUN_TEST_CASE(execution_sequence_values_copy_at_ownership_boundaries);
    RUN_TEST_CASE(execution_scoped_lent_locals_leave_their_owner_intact);
    RUN_TEST_CASE(execution_strict_sequence_ranges_are_optional_and_copied);
    RUN_TEST_CASE(execution_pointer_views_follow_rebinding_and_fixed_positions);
    RUN_TEST_CASE(execution_sequence_places_preserve_effectful_evaluation_order);
    RUN_TEST_CASE(execution_calls_lend_arguments_and_callees_copy_writes);
    RUN_TEST_CASE(execution_lent_records_copy_nothing);
    RUN_TEST_CASE(execution_prepared_identity_includes_instruction_schema);
    RUN_TEST_CASE(execution_pointer_owners_reject_foreign_cells_without_mutation);
    RUN_TEST_CASE(execution_query_owners_reject_foreign_heaps_without_mutation);
    RUN_TEST_CASE(execution_managed_scan_checks_nested_layouts);
    RUN_TEST_CASE(execution_pointer_fields_require_the_full_base_layout);
    RUN_TEST_CASE(execution_pointer_comparisons_use_location_identity);
    RUN_TEST_CASE(execution_native_boundary_rejects_hidden_managed_pointers);
    RUN_TEST_CASE(execution_native_boundary_rejects_malformed_aggregate_storage);
    RUN_TEST_CASE(execution_rebound_borrowing_call_checks_its_arguments);
    RUN_TEST_CASE(execution_borrowing_call_arguments_validate_at_definition);
    RUN_TEST_CASE(execution_managed_pointer_access_is_not_query_or_parallel_safe);
    RUN_TEST_CASE(execution_managed_heap_preserves_roots_and_reclaims_cycles);
    RUN_TEST_CASE(execution_union_extraction_checks_dynamic_payload_type);
    RUN_TEST_CASE(execution_intrinsics_build_in_result_storage);
    RUN_TEST_CASE(execution_branches_state_and_host_calls);
    RUN_TEST_CASE(execution_runs_without_authored_calls_count_as_running);
    RUN_TEST_CASE(execution_reentered_runs_keep_the_outer_runs_values);
    RUN_TEST_CASE(execution_source_inspection_is_lazy_and_preserves_resume);
    RUN_TEST_CASE(execution_costs_verify_intrinsics_and_follow_recursive_calls);
    RUN_TEST_CASE(execution_costs_include_managed_scalar_result_and_state_escape);
    RUN_TEST_CASE(execution_finished_definition_owns_producer_strings);
    RUN_TEST_CASE(execution_string_equality_and_iteration_use_full_bytes);
    RUN_TEST_CASE(execution_native_string_fields_validate_before_publication);
    RUN_TEST_CASE(execution_verifier_rejects_invalid_lowering);
    RUN_TEST_CASE(execution_budget_limits_checked_back_edges);
    RUN_TEST_CASE(execution_verifier_rejects_invalid_banks_and_metadata);
    RUN_TEST_CASE(execution_verifier_rejects_invalid_saved_aggregate_roots);
    RUN_TEST_CASE(execution_saved_aggregate_roots_keep_only_live_shapes);
    RUN_TEST_CASE(execution_verifier_rejects_signature_and_local_type_mismatches);
    RUN_TEST_CASE(execution_linked_function_returns_checked_value);
    RUN_TEST_CASE(execution_helper_returns_keep_native_schema_after_input_retirement);
    RUN_TEST_CASE(execution_owned_intrinsics_validate_action_state_targets);
    RUN_TEST_CASE(execution_raw_scalar_helper_preserves_owned_failure_text);
    RUN_TEST_CASE(execution_reused_helper_resets_host_bank_and_rebinds_service);
    RUN_TEST_CASE(execution_reused_host_storage_clears_context_and_rebinds_fixed_buffers);
    RUN_TEST_CASE(execution_integer_bitwise_preserves_width_and_valid_shifts);
    RUN_TEST_CASE(execution_integer_shift_past_the_width_shifts_every_bit_out);
    RUN_TEST_CASE(execution_fast_shift_past_the_width_keeps_both_stores);
    RUN_TEST_CASE(execution_bitwise_verifier_rejects_non_integer_and_mismatched_operands);
    RUN_TEST_CASE(execution_print_string_is_synchronous_and_preserves_context);
    RUN_TEST_CASE(execution_numeric_address_bits_do_not_retain_saved_storage);
    RUN_TEST_CASE(execution_unary_numeric_operands_ignore_unrelated_address_slot);
    RUN_TEST_CASE(execution_synchronous_action_owns_outcomes_and_validates_selection);
    RUN_TEST_CASE(execution_preparation_reserves_the_larger_matrix_phase);
    RUN_TEST_CASE(execution_preparation_reserves_one_definitions_scratch);
    RUN_TEST_CASE(execution_preparation_reserves_the_runtime_code_it_keeps);
    RUN_TEST_CASE(execution_import_fingerprint_takes_the_linker_record_limit);
    RUN_TEST_CASE(execution_verifier_admits_a_large_compiler_function);
    RUN_TEST_CASE(execution_decoded_preparation_cleans_partial_live_roots);
    RUN_TEST_CASE(execution_decoded_suspensions_charge_exact_live_sets);
    RUN_TEST_CASE(execution_numeric_casts_apply_exact_wrap_and_clamp);
    RUN_TEST_CASE(execution_prepared_integer_reports_charge_owned_storage);
    RUN_TEST_CASE(execution_prepared_integer_reports_charge_owned_children);
    RUN_TEST_CASE(execution_integer_reports_charge_retained_package_imports);
    RUN_TEST_CASE(execution_integer_reports_mark_overflow_and_zero_divisors);
    RUN_TEST_CASE(execution_integer_reports_mark_clamping_conversions);
    RUN_TEST_CASE(execution_integer_reports_keep_literal_shipped_values);
    RUN_TEST_CASE(execution_integer_reports_skip_address_arithmetic);
    RUN_TEST_CASE(execution_integer_reports_run_every_site_without_a_fast_body);
    pu_clear_setup();
}
