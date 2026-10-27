#include "backend.h"
#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include "execution/wasm_jit.h"
#endif

static behavior_function_builder *builder;
static behavior_function *function;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static bool pause_source;
static bool component_types;

static void release_run()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
}

static void cleanup()
{
    release_run();
    behavior_function_free(function);
    if (builder) {
        behavior_function_builder_free(builder);
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    function = NULL;
    builder = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    pause_source = false;
    if (component_types) {
        type_catalog_set_project(NULL);
        component_types = false;
    }
}

static int emit(behavior_function_instruction i)
{
    return behavior_function_builder_emit(builder, i);
}

static int value(behavior_function_instruction i)
{
    i.output = behavior_function_builder_slot(builder, i.type);
    emit(i);
    return i.output;
}

static int argument(pin_value type, int index)
{
    return value((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = type.type, .left = index, .value = type });
}

static int address(int bank)
{
    int slot = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, slot, BEHAVIOR_ADDRESS));
    emit((
        behavior_function_instruction
    ){ .op = BF_HOST_SLOT, .type = BF_POINTER_TYPE, .output = slot, .left = bank });
    return slot;
}

static int call(const node_host_call *primitive, pin_value *args, const int *inputs, int count, pin_value result)
{
    int host = behavior_function_builder_host(
        builder,
        primitive,
        &EXECUTION_value_access,
        args,
        count,
        &result,
        1
    );
    uint64_t id = execution_intrinsic_id(primitive);
    assert(behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id)));
    for (int j = 0; inputs && j < count; j++) {
        if (inputs[j] >= 0) {
            int pointer = address(behavior_function_builder_host_first(builder, host) + j);
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = args[j].type,
                                                  .left = inputs[j],
                                                  .right = pointer,
                                                  .offset = offsetof(pin_value, d) });
        }
    }
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    int pointer = address(behavior_function_builder_host_results(builder, host));
    return value((behavior_function_instruction){ .op = BF_LOAD,
                                                  .type = result.type,
                                                  .left = pointer,
                                                  .offset = offsetof(pin_value, d) });
}

static int make_union(pin_value payload, int input)
{
    pin_value args[] = { { .type = PIN_U64, .u64 = 42 }, payload };
    int inputs[] = { -1, input };
    return call(&EXECUTION_union_make_call, args, inputs, 2, (pin_value){ .type = PIN_UNION });
}

static int extract(int input, pin_value result, int field, bool whole, uint64_t tag)
{
    pin_value args[] = { { .type = PIN_UNION },
                         { .type = PIN_U64, .u64 = tag },
                         { .type = PIN_I32, .i = field },
                         { .type = PIN_BOOL, .b = whole } };
    int inputs[] = { input, -1, -1, -1 };
    return call(&EXECUTION_union_get_call, args, inputs, 4, result);
}

static void result(int slot, pin_type type, int index)
{
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = type, .left = slot, .target = index });
}

static bool finish()
{
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "scalar replacement builder: %s\n", error);
    }
    return function && behavior_function_compile(function);
}

static int inspect(void *, behavior_function_context *, const behavior_source_point *)
{
    return pause_source;
}

static int begin(pin_value *args, int count, pin_value *results, int mode)
{
    release_run();
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    context = (behavior_function_context){ .arguments = args,
                                           .argument_count = count,
                                           .results = results,
                                           .activation = &activation,
                                           .budget = 100,
                                           .source_pause = mode >= 2 ? inspect : NULL };
    behavior_function_bind_storage(&context, &storage);
    assert(behavior_function_bind(function, &context, NULL, 0));
    return behavior_function_run(function, &context, mode % 2);
}

static int calls(bool runtime)
{
    int count = 0;
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        count += function->instructions[pc].op == BF_HOST_CALL &&
                 (!runtime || behavior_function_runtime_keeps(function, pc));
    }
    return count;
}

TEST_CASE(scalar_unions_remove_nonescaping_payloads_snapshots_and_tag_checks)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = PIN_F64 };
    pin_value outputs[] = { input, { .type = PIN_BOOL }, { .type = PIN_BOOL } };
    behavior_function_builder_signature(builder, &input, 1, outputs, 3, NULL, 0);
    int number = argument(input, 0);
    pin_value args[] = { { .type = PIN_U64, .u64 = 42 }, input, { .type = PIN_BOOL } };
    int inputs[] = { -1, number, -1 };
    pin_value union_type = { .type = PIN_UNION };
    int object = call(&EXECUTION_union_make_call, args, inputs, 3, union_type);
    object = call(&EXECUTION_snapshot_call, &union_type, &object, 1, union_type);
    int local = behavior_function_builder_local(builder, union_type);
    emit((
        behavior_function_instruction
    ){ .op = BF_SAVE, .type = PIN_UNION, .left = object, .target = local });
    object = value((behavior_function_instruction){ .op = BF_LOCAL,
                                                    .type = PIN_UNION,
                                                    .left = local });
    object = extract(object, union_type, 0, true, 42);
    result(extract(object, input, 0, false, 42), PIN_F64, 0);
    result(extract(object, outputs[1], 1, false, 42), PIN_BOOL, 1);
    pin_value test[] = { union_type, { .type = PIN_U64, .u64 = 99 } };
    int tested[] = { object, -1 };
    result(call(&EXECUTION_union_test_call, test, tested, 2, outputs[2]), PIN_BOOL, 2);
    REQUIRE(finish());
    REQUIRE(calls(false) == 6 && calls(true) == 0);
    execution_cost_summary cost = behavior_function_costs(function);
    REQUIRE(cost.complete && !cost.proven && !cost.possible);
    const double numbers[] = { 0, -0.0, 1.5, -7.25, INFINITY, -INFINITY, NAN };
#ifdef __EMSCRIPTEN__
    REQUIRE(!behavior_function_compile_inspection(function));
#else
    REQUIRE(behavior_function_compile_inspection(function));
#endif
    for (int backend = 0; backend < 4; backend++) {
        for (int j = 0; j < 7; j++) {
            input.d = numbers[j];
            REQUIRE(begin(&input, 1, outputs, backend) == FLOW_OK);
            REQUIRE(!memcmp(&outputs[0].d, &input.d, sizeof input.d));
            REQUIRE(!outputs[1].b && !outputs[2].b);
            size_t bytes, capacity;
            arena_usage(&storage, &bytes, &capacity);
            REQUIRE(backend < 2 ? bytes == 0 : bytes > 0);
        }
    }
    return true;
}

TEST_CASE(scalar_unions_keep_escaping_and_managed_payloads_owned)
{
    const pin_value cases[] = {
        { .type = PIN_F64, .d = 2.75 },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "", .length = sizeof("") - 1 } },
        { .type = PIN_STR },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "original",
                                       .length = sizeof("original") - 1 } }
    };
    for (int j = 0; j < 4; j++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value input = cases[j], output = { .type = PIN_UNION };
        behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
        int object = make_union(input, argument(input, 0));
        result(object, PIN_UNION, 0);
        REQUIRE(finish());
        REQUIRE(calls(true) == 1);
#ifdef __EMSCRIPTEN__
        REQUIRE(!behavior_function_compile_inspection(function));
#else
        REQUIRE(behavior_function_compile_inspection(function));
#endif
        for (int backend = 0; backend < 4; backend++) {
            char text[] = "original";
            pin_string descriptor = { text, sizeof text - 1 };
            if (j == 3) {
                input.text = &descriptor;
            }
            REQUIRE(begin(&input, 1, &output, backend) == FLOW_OK);
            const pin_union *retained = output.ptr;
            REQUIRE(retained && retained->tag == 42 && retained->count == 1);
            if (!j) {
                REQUIRE(retained->values[0].d == input.d);
            } else if (input.text) {
                text[0] = 'X';
                REQUIRE(!strcmp(
                    pin_text(retained->values[0]).bytes,
                    pin_text(cases[j]).bytes
                ));
            } else {
                REQUIRE(pin_text(retained->values[0]).length == 0);
            }
        }
    }
    return true;
}

TEST_CASE(scalar_unions_preserve_failed_extraction_contracts)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = PIN_F64, .d = 3.5 }, output = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int object = make_union(input, argument(input, 0));
    result(extract(object, output, 0, false, 42), PIN_I32, 0);
    REQUIRE(finish());
    REQUIRE(calls(true) == 2);
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(begin(&input, 1, &output, backend) == FLOW_ERR_INVALID_GRAPH);
        REQUIRE(
            context.error_message &&
            !strcmp(context.error_message, "Union payload differs from the extraction type")
        );
    }
    return true;
}

TEST_CASE(scalar_unions_optimize_control_joins_but_keep_waits_owned)
{
    for (int suspension = 0; suspension < 2; suspension++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value input[] = { { .type = PIN_F64, .d = -0.0 }, { .type = PIN_BOOL } };
        pin_value output = input[0];
        behavior_function_builder_signature(builder, input, 2, &output, 1, NULL, 0);
        int object = make_union(input[0], argument(input[0], 0));
        int local = behavior_function_builder_local(builder, (pin_value){ .type = PIN_UNION });
        emit((
            behavior_function_instruction
        ){ .op = BF_SAVE, .type = PIN_UNION, .left = object, .target = local });
        if (suspension) {
            emit((behavior_function_instruction){ .op = BF_SUSPEND });
            int point = behavior_function_builder_source_point(
                builder,
                (behavior_source_point){ .occurrence = 1, .kind = BEHAVIOR_SOURCE_STATEMENT }
            );
            emit((behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = point });
        } else {
            int condition = argument(input[1], 1);
            int branch = emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                               .left = condition });
            int jump = emit((behavior_function_instruction){ .op = BF_JUMP });
            behavior_function_builder_patch_target(builder, branch, jump + 1);
            behavior_function_builder_patch_target(builder, jump, jump + 1);
        }
        object = value((behavior_function_instruction){ .op = BF_LOCAL,
                                                        .type = PIN_UNION,
                                                        .left = local });
        result(extract(object, output, 0, false, 42), PIN_F64, 0);
        REQUIRE(finish());
        REQUIRE(calls(true) == (suspension ? 2 : 0));
        for (int backend = 0; backend < 2; backend++) {
            for (int branch = 0; branch < 2; branch++) {
                input[1].b = branch;
                int status = begin(input, 2, &output, backend);
                if (suspension) {
                    REQUIRE(status == FLOW_SUSPENDED_RESULT);
                    context.source_pause = branch ? inspect : NULL;
                    status = behavior_function_run(function, &context, backend);
                }
                REQUIRE(status == FLOW_OK);
                REQUIRE(!memcmp(&output.d, &input[0].d, sizeof output.d));
                size_t bytes, capacity;
                arena_usage(&storage, &bytes, &capacity);
                REQUIRE(suspension ? bytes > 0 : bytes == 0);
            }
        }
    }
    return true;
}

static int opaque_calls;
static const char *opaque_union(arena *, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    const pin_union *object = args[0].ptr;
    assert(object && object->tag == 42);
    opaque_calls++;
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}

TEST_CASE(scalar_unions_reject_forged_intrinsic_identity)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = PIN_F64, .d = 17 }, output = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int object = make_union(input, argument(input, 0));
    pin_value args[] = { { .type = PIN_UNION }, { .type = PIN_U64, .u64 = 42 } };
    const node_host_call opaque = { .value = opaque_union };
    int host = behavior_function_builder_host(builder, &opaque, &EXECUTION_value_access, args, 2, &output, 1);
    uint64_t id = execution_intrinsic_id(&EXECUTION_union_test_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    int pointer = address(behavior_function_builder_host_first(builder, host));
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_UNION,
                                          .left = object,
                                          .right = pointer,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    pointer = address(behavior_function_builder_host_results(builder, host));
    result(
        value((behavior_function_instruction){ .op = BF_LOAD,
                                               .type = PIN_BOOL,
                                               .left = pointer,
                                               .offset = offsetof(pin_value, d) }),
        PIN_BOOL,
        0
    );
    REQUIRE(finish());
    REQUIRE(calls(true) == 2);
    for (int backend = 0; backend < 2; backend++) {
        opaque_calls = 0;
        REQUIRE(begin(&input, 1, &output, backend) == FLOW_OK);
        REQUIRE(output.b && opaque_calls == 1);
    }
    return true;
}

TEST_CASE(scalar_unions_keep_values_used_by_a_loop)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = PIN_F64, .d = 1.25 }, output = input;
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int object = make_union(input, argument(input, 0));
    int index = behavior_function_builder_local(builder, (pin_value){ .type = PIN_I32, .i = 2 });
    int loop = emit((behavior_function_instruction){ .op = BF_CHECK });
    int extracted = extract(object, output, 0, false, 42);
    int current = value((behavior_function_instruction){ .op = BF_LOCAL,
                                                         .type = PIN_I32,
                                                         .left = index });
    int one = value((
        behavior_function_instruction
    ){ .op = BF_CONSTANT, .type = PIN_I32, .value = { .type = PIN_I32, .i = 1 } });
    int next = value((
        behavior_function_instruction
    ){ .op = BF_SUB, .type = PIN_I32, .left = current, .right = one });
    emit((
        behavior_function_instruction
    ){ .op = BF_SAVE, .type = PIN_I32, .left = next, .target = index });
    int zero = value((
        behavior_function_instruction
    ){ .op = BF_CONSTANT, .type = PIN_I32, .value = { .type = PIN_I32 } });
    int condition = behavior_function_builder_slot(builder, PIN_BOOL);
    emit((
        behavior_function_instruction
    ){ .op = BF_GT, .type = PIN_I32, .output = condition, .left = next, .right = zero });
    emit((behavior_function_instruction){ .op = BF_BRANCH,
                                          .left = condition,
                                          .target = loop });
    result(extracted, PIN_F64, 0);
    REQUIRE(finish());
    REQUIRE(calls(true) == 2);
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(begin(&input, 1, &output, backend) == FLOW_OK);
        REQUIRE(output.d == input.d);
    }
    return true;
}

TEST_CASE(scalar_unions_preserve_payload_before_secondary_slot_overwrite)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = PIN_I32, .i = 7 }, output = input;
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int payload = argument(input, 0);
    int object = make_union(input, payload);
    int numerator = value((
        behavior_function_instruction
    ){ .op = BF_CONSTANT, .type = PIN_I32, .value = { .type = PIN_I32, .i = 10 } });
    int denominator = value((
        behavior_function_instruction
    ){ .op = BF_CONSTANT, .type = PIN_I32, .value = { .type = PIN_I32, .i = 3 } });
    value((behavior_function_instruction){ .op = BF_DIV,
                                           .type = PIN_I32,
                                           .left = numerator,
                                           .right = denominator,
                                           .target = payload });
    result(extract(object, output, 0, false, 42), PIN_I32, 0);
    REQUIRE(finish());
    REQUIRE(calls(true) == 2);
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(begin(&input, 1, &output, backend) == FLOW_OK);
        REQUIRE(output.i == 7);
    }
    return true;
}

TEST_CASE(scalar_unions_optimize_authored_local_patterns)
{
    const script_catalog catalog = { 0 };
    const script_lower_bindings bindings = { 0 };
    syntax = script_parse("Scalar :: library { Eval :: (amount: number) -> number { value: number | Cancelled = amount; if value is number found { return found; } return 0; } }");
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis && analysis->valid);
    plan = script_lower(analysis, &catalog, &bindings);
    if (plan && !plan->valid) {
        for (int j = 0; j < plan->diagnostic_count; j++) {
            fprintf(stderr, "scalar source lowering: %s\n", plan->diagnostics[j].message);
        }
    }
    REQUIRE(plan && plan->valid);
    for (int j = 0; j < plan->function_count; j++) {
        behavior_function *body = plan->functions[j].body;
        if (body && body->argument_count == 1 && body->result_count == 1) {
            function = body;
            plan->functions[j].body = NULL;
            break;
        }
    }
    REQUIRE(function && behavior_function_compile(function));
    REQUIRE(calls(false) > calls(true));
    int constructors = 0;
    uint64_t make = execution_intrinsic_id(&EXECUTION_union_make_call);
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        const behavior_function_instruction *i = &function->instructions[pc];
        if (i->op == BF_HOST_CALL &&
            function->host_calls[i->target].capability_id == make) {
            constructors++;
            REQUIRE(!behavior_function_runtime_keeps(function, pc));
        }
    }
    REQUIRE(constructors == 1);
    pin_value input = { .type = PIN_F32, .f = 3.75f }, output = { .type = PIN_F32 };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(begin(&input, 1, &output, backend) == FLOW_OK);
        REQUIRE(output.f == input.f);
    }
    return true;
}

TEST_CASE(scalar_unions_restore_payload_when_leaving_source_inspection)
{
    for (int extracted = 0; extracted < 2; extracted++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value input = { .type = PIN_F64 }, output = input;
        behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
        int object = make_union(input, argument(input, 0));
        int payload = extracted ? extract(object, output, 0, false, 42) : -1;
        int point = behavior_function_builder_source_point(
            builder,
            (behavior_source_point){ .occurrence = 1, .kind = BEHAVIOR_SOURCE_STATEMENT }
        );
        emit((behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = point });
        result(extracted ? payload : extract(object, output, 0, false, 42), PIN_F64, 0);
        REQUIRE(finish());
        REQUIRE(calls(true) == 0);
        const double cases[] = { 3.75, -0.0 };
        for (int backend = 0; backend < 2; backend++) {
            for (int j = 0; j < 2; j++) {
                input.d = cases[j];
                pause_source = true;
                REQUIRE(begin(&input, 1, &output, backend + 2) == FLOW_PAUSED_RESULT);
                /* A continuation must carry its complete saved state. A fresh
                 * activation prevents dead VM slots from masking a missing save. */
                behavior_function_activation resumed = { 0 };
                behavior_function_activation_init(function, &resumed);
                behavior_function_copy_activation(function, &resumed, &activation);
                behavior_function_activation_free(&activation);
                activation = resumed;
                context.source_pause = NULL;
                pause_source = false;
                REQUIRE(behavior_function_run(function, &context, backend) == FLOW_OK);
                REQUIRE(!memcmp(&output.d, &input.d, sizeof output.d));
            }
        }
    }
    return true;
}

TEST_CASE(scalar_unions_follow_local_aliases_through_diamonds)
{
    builder = behavior_function_builder_new();
    pin_value inputs[] = { { .type = PIN_F64 }, { .type = PIN_BOOL } };
    pin_value output = inputs[0], shape = { .type = PIN_UNION };
    behavior_function_builder_signature(builder, inputs, 2, &output, 1, NULL, 0);
    int object = make_union(inputs[0], argument(inputs[0], 0));
    int local = behavior_function_builder_local(builder, shape);
    int alias = behavior_function_builder_local(builder, shape);
    emit((
        behavior_function_instruction
    ){ .op = BF_SAVE, .type = PIN_UNION, .left = object, .target = local });
    int condition = argument(inputs[1], 1);
    int branch = emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                       .left = condition });
    int first = value((behavior_function_instruction){ .op = BF_LOCAL,
                                                       .type = PIN_UNION,
                                                       .left = local });
    emit((
        behavior_function_instruction
    ){ .op = BF_SAVE, .type = PIN_UNION, .left = first, .target = alias });
    int jump = emit((behavior_function_instruction){ .op = BF_JUMP });
    REQUIRE(behavior_function_builder_patch_target(builder, branch, jump + 1));
    int second = call(&EXECUTION_snapshot_call, &shape, &object, 1, shape);
    emit((
        behavior_function_instruction
    ){ .op = BF_SAVE, .type = PIN_UNION, .left = second, .target = alias });
    int join = value((behavior_function_instruction){ .op = BF_LOCAL,
                                                      .type = PIN_UNION,
                                                      .left = alias });
    int join_pc = acount(behavior_function_builder_definition(builder)->instructions) - 1;
    REQUIRE(behavior_function_builder_patch_target(builder, jump, join_pc));
    result(extract(join, output, 0, false, 42), PIN_F64, 0);
    REQUIRE(finish());
    REQUIRE(calls(false) == 3 && calls(true) == 0);
    const double samples[] = { 3.75, -0.0, INFINITY, NAN };
    for (int mode = 0; mode < 4; mode++) {
        for (int branch_value = 0; branch_value < 2; branch_value++) {
            for (int sample = 0; sample < 4; sample++) {
                inputs[0].d = samples[sample];
                inputs[1].b = branch_value;
                REQUIRE(begin(inputs, 2, &output, mode) == FLOW_OK);
                REQUIRE(!memcmp(&output.d, &inputs[0].d, sizeof output.d));
                size_t bytes, capacity;
                arena_usage(&storage, &bytes, &capacity);
                REQUIRE(mode < 2 ? bytes == 0 : bytes > 0);
            }
        }
    }
    return true;
}

TEST_CASE(scalar_unions_keep_distinct_incoming_values_materialized)
{
    builder = behavior_function_builder_new();
    pin_value inputs[] = { { .type = PIN_F64, .d = 3.75 }, { .type = PIN_BOOL } };
    pin_value output = inputs[0], shape = { .type = PIN_UNION };
    behavior_function_builder_signature(builder, inputs, 2, &output, 1, NULL, 0);
    int local = behavior_function_builder_local(builder, shape);
    int condition = argument(inputs[1], 1);
    int branch = emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                       .left = condition });
    int first = make_union(inputs[0], argument(inputs[0], 0));
    emit((
        behavior_function_instruction
    ){ .op = BF_SAVE, .type = PIN_UNION, .left = first, .target = local });
    int jump = emit((behavior_function_instruction){ .op = BF_JUMP });
    REQUIRE(behavior_function_builder_patch_target(builder, branch, jump + 1));
    pin_value other = { .type = PIN_F64, .d = -8.5 };
    int number = value((behavior_function_instruction){ .op = BF_CONSTANT,
                                                        .type = PIN_F64,
                                                        .value = other });
    int second = make_union(other, number);
    emit((
        behavior_function_instruction
    ){ .op = BF_SAVE, .type = PIN_UNION, .left = second, .target = local });
    int join = value((behavior_function_instruction){ .op = BF_LOCAL,
                                                      .type = PIN_UNION,
                                                      .left = local });
    int join_pc = acount(behavior_function_builder_definition(builder)->instructions) - 1;
    REQUIRE(behavior_function_builder_patch_target(builder, jump, join_pc));
    result(extract(join, output, 0, false, 42), PIN_F64, 0);
    REQUIRE(finish());
    REQUIRE(calls(true) == 3);
    for (int mode = 0; mode < 4; mode++) {
        for (int branch_value = 0; branch_value < 2; branch_value++) {
            inputs[1].b = branch_value;
            REQUIRE(begin(inputs, 2, &output, mode) == FLOW_OK);
            REQUIRE(output.d == (branch_value ? other.d : inputs[0].d));
            size_t bytes, capacity;
            arena_usage(&storage, &bytes, &capacity);
            REQUIRE(bytes > 0);
        }
    }
    return true;
}

TEST_CASE(scalar_unions_remove_nested_snapshots_and_keep_escaping_dependencies)
{
    for (int escaping = 0; escaping < 3; escaping++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value input = { .type = PIN_F64, .d = 7.25 },
                  shape = { .type = PIN_UNION };
        pin_value output = escaping ? shape : input;
        behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
        int inner = make_union(input, argument(input, 0));
        int outer = make_union(shape, inner);
        outer = call(&EXECUTION_snapshot_call, &shape, &outer, 1, shape);
        int returned = outer;
        if (escaping != 1) {
            returned = extract(outer, shape, 0, false, 42);
        }
        if (!escaping) {
            returned = extract(returned, input, 0, false, 42);
        }
        result(returned, output.type, 0);
        REQUIRE(finish());
        REQUIRE(calls(true) == (escaping == 1 ? 3 : escaping == 2 ? 4 : 0));
        for (int mode = 0; mode < 4; mode++) {
            REQUIRE(begin(&input, 1, &output, mode) == FLOW_OK);
            if (escaping) {
                const pin_union *payload = output.ptr;
                REQUIRE(payload && payload->tag == 42 && payload->count == 1);
                if (escaping == 1) {
                    payload = payload->values[0].ptr;
                    REQUIRE(payload && payload->tag == 42 && payload->count == 1);
                }
                REQUIRE(payload->values[0].d == input.d);
            } else {
                REQUIRE(output.d == input.d);
            }
            size_t bytes, capacity;
            arena_usage(&storage, &bytes, &capacity);
            REQUIRE(!escaping && mode < 2 ? bytes == 0 : bytes > 0);
        }
    }
    return true;
}

static int scalar_call_count;
static bool scalar_call_fails;
static const char *scalar_effect(void *, const pin_value *args, pin_value *out)
{
    scalar_call_count++;
    if (scalar_call_fails) {
        return "scalar call failed";
    }
    out[0].d = args[0].d + 1;
    return NULL;
}

TEST_CASE(scalar_unions_cross_isolated_calls_without_removing_effects_or_errors)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = PIN_F64, .d = 7.25 };
    pin_value output[] = { input, input };
    behavior_function_builder_signature(builder, &input, 1, output, 2, NULL, 0);
    int object = make_union(input, argument(input, 0));
    const node_host_call callback = { .invoke = scalar_effect };
    const node_access effects = { .flags = NODE_EFFECT_HOST_SINK };
    int host = behavior_function_builder_host(builder, &callback, &effects, &input, 1, &input, 1);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    int pointer = address(behavior_function_builder_host_results(builder, host));
    int returned = value((
        behavior_function_instruction
    ){ .op = BF_LOAD, .type = PIN_F64, .left = pointer, .offset = offsetof(pin_value, d) });
    result(returned, PIN_F64, 1);
    result(extract(object, input, 0, false, 42), PIN_F64, 0);
    REQUIRE(finish());
    REQUIRE(calls(false) == 3 && calls(true) == 1);
    for (int mode = 0; mode < 4; mode++) {
        for (int fail = 0; fail < 2; fail++) {
            scalar_call_count = 0;
            scalar_call_fails = fail;
            output[0].d = output[1].d = -99;
            REQUIRE(begin(&input, 1, output, mode) == (fail ? FLOW_ERR_INVALID_GRAPH : FLOW_OK));
            REQUIRE(scalar_call_count == 1);
            if (fail) {
                REQUIRE(output[0].d == -99 && output[1].d == -99);
                REQUIRE(!strcmp(context.error_message, "scalar call failed"));
            } else {
                REQUIRE(output[0].d == input.d && output[1].d == input.d + 1);
            }
            size_t bytes, capacity;
            arena_usage(&storage, &bytes, &capacity);
            REQUIRE(mode < 2 ? bytes == 0 : bytes > 0);
        }
    }
    return true;
}

TEST_CASE(scalar_unions_keep_nested_managed_payload_snapshots)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = PIN_STR }, output = input,
              shape = { .type = PIN_UNION };
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int inner = make_union(input, argument(input, 0));
    int outer = make_union(shape, inner);
    int payload = extract(outer, shape, 0, false, 42);
    result(extract(payload, output, 0, false, 42), PIN_STR, 0);
    REQUIRE(finish());
    REQUIRE(calls(true) == 4);
    for (int mode = 0; mode < 4; mode++) {
        char original[] = "snapshot";
        input.text = &(const pin_string){ .bytes = original,
                                          .length = (uint32_t)strlen(original) };
        REQUIRE(begin(&input, 1, &output, mode) == FLOW_OK);
        original[0] = 'X';
        REQUIRE(pin_text(output).bytes && !strcmp(pin_text(output).bytes, "snapshot"));
        size_t bytes, capacity;
        arena_usage(&storage, &bytes, &capacity);
        REQUIRE(bytes > 0);
    }
    return true;
}

TEST_CASE(scalar_unions_bound_control_flow_scratch_and_keep_safe_fallback)
{
    builder = behavior_function_builder_new();
    pin_value inputs[] = { { .type = PIN_F64, .d = -8.25 }, { .type = PIN_BOOL } };
    pin_value output = inputs[0];
    behavior_function_builder_signature(builder, inputs, 2, &output, 1, NULL, 0);
    /* Unused scalar capacity still belongs to the verified function. Keep this
     * fixture small while exceeding the separate forward-analysis cap. */
    for (int j = 0; j < 256; j++) {
        behavior_function_builder_slot(builder, PIN_F64);
    }
    int object = make_union(inputs[0], argument(inputs[0], 0));
    int condition = argument(inputs[1], 1);
    for (int j = 0; j < 512; j++) {
        int pc = emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                       .left = condition });
        REQUIRE(behavior_function_builder_patch_target(builder, pc, pc + 1));
    }
    result(extract(object, output, 0, false, 42), PIN_F64, 0);
    REQUIRE(finish());
    REQUIRE(behavior_function_scalarize_scratch(function) == 0);
    REQUIRE(calls(true) == 2);
    for (int mode = 0; mode < 2; mode++) {
        for (int branch_value = 0; branch_value < 2; branch_value++) {
            inputs[1].b = branch_value;
            REQUIRE(begin(inputs, 2, &output, mode) == FLOW_OK);
            REQUIRE(output.d == inputs[0].d);
        }
    }
    return true;
}

/* Keep the authored baseline workload identical while comparing native access
 * metadata with the unchanged reference-based member path. Tiny
 * assertion-inclusive timings describe this fixture only, never a pass/fail
 * threshold. Cold allocation counts include preparation and the first call;
 * trials reuse the canonical bank, VM scratch and resettable value arena. */
enum
{
    READ_COMPONENT = 9201,
    READ_TYPE = 9202,
    READ_SELF = 9203,
    READ_ENTITY = 9204,
    READ_TRIALS = 5,
    READ_INVOCATIONS = 8,
    READ_BUDGET = 100
};
#define READ_OWNER ((UINT64_C(7) << 32) | 1)
typedef struct
{
    int32_t signed_value;
    uint64_t wide;
    float single;
} scalar_read_row;
typedef struct
{
    int32_t signed_value;
    const char *text;
} managed_read_row;
static const type_field scalar_read_fields[] = {
    { .name = "signed_value",
      .type = PIN_I32,
      .offset = offsetof(scalar_read_row, signed_value),
      .size = sizeof(int32_t),
      .type_name = "int",
      .range_contract = true,
      .min = 0,
      .max = 10 },
    { .name = "wide",
      .type = PIN_U64,
      .offset = offsetof(scalar_read_row, wide),
      .size = sizeof(uint64_t),
      .type_name = "uint64_t" },
    { .name = "single",
      .type = PIN_F32,
      .offset = offsetof(scalar_read_row, single),
      .size = sizeof(float),
      .type_name = "number" }
};
static const type_field managed_read_fields[] = {
    { .name = "signed_value",
      .type = PIN_I32,
      .offset = offsetof(managed_read_row, signed_value),
      .size = sizeof(int32_t),
      .type_name = "int" },
    { .name = "text",
      .type = PIN_STR,
      .offset = offsetof(managed_read_row, text),
      .size = sizeof(const char *),
      .type_name = "const char *" }
};
static const type_desc scalar_read_type = { .name = "ScalarRead",
                                            .size = sizeof(scalar_read_row),
                                            .fields = scalar_read_fields,
                                            .field_count = 3 };
static const type_desc managed_read_type = { .name = "ManagedRead",
                                             .size = sizeof(managed_read_row),
                                             .fields = managed_read_fields,
                                             .field_count = 2 };
static const type_desc *const read_types[] = { &scalar_read_type, &managed_read_type };
static struct
{
    scalar_read_row rows[2];
    managed_read_row managed[2];
    uint32_t generations[2];
    unsigned char occupied[2];
    uint64_t membership;
    int lookup[2];
    ecs_execution_column column;
    ecs_execution_view world;
} read_fixture;

static void read_world(bool managed)
{
    const type_desc *type = managed ? &managed_read_type : &scalar_read_type;
    memset(&read_fixture, 0, sizeof read_fixture);
    read_fixture.rows[1] = (scalar_read_row){ -23, UINT64_C(0xfedcba9876543210), -0.0f };
    read_fixture.generations[1] = 7;
    read_fixture.occupied[1] = 1;
    read_fixture.membership = 2;
    read_fixture.column = (ecs_execution_column){
        .id = READ_COMPONENT,
        .layout = type_layout_hash(type),
        .stride = type->size,
        .data = managed ? (void *)read_fixture.managed : (void *)read_fixture.rows,
        .occupancy = &read_fixture.membership,
        .count = 1,
        .singleton = 1,
        .type = type
    };
    uint64_t hash = READ_COMPONENT * UINT64_C(11400714819323198485);
    read_fixture.lookup[(hash ^ (hash >> 32)) & 1] = 1;
    read_fixture.world = (ecs_execution_view){ .generations = read_fixture.generations,
                                               .occupied = read_fixture.occupied,
                                               .slot_count = 2,
                                               .columns = &read_fixture.column,
                                               .lookup = read_fixture.lookup,
                                               .capacity = 2 };
}

static bool read_global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != READ_SELF) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}

static int read_entity_calls;
static const char *read_entity(arena *, const pin_value *, int count, pin_value *out)
{
    assert(!count);
    read_entity_calls++;
    *out = (pin_value){ .type = PIN_HANDLE, .u64 = READ_OWNER };
    return NULL;
}
static bool read_host_binding(void *, uint64_t id, script_lower_host *out)
{
    if (id != 9500) {
        return false;
    }
    static const node_host_call call = { .value = read_entity };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (script_lower_host){ .call = &call, .access = &access };
    return true;
}

static bool read_compile_reflection(
    int field,
    bool getter,
    bool managed,
    bool reflected,
    bool explicit_entity,
    bool aggregate
)
{
    cleanup();
    const type_catalog_pack pack = { .types = read_types, .type_count = 2 };
    type_catalog_set_project(&pack);
    component_types = true;
    const type_desc *type = managed ? &managed_read_type : &scalar_read_type;
    script_catalog_field fields[3] = { 0 };
    for (int j = 0; j < type->field_count; j++) {
        const type_field *f = &type->fields[j];
        fields[j] = (script_catalog_field){ .id = (uint64_t)(9300 + j),
                                            .name = f->name,
                                            .type = { .storage = f->type },
                                            .offset = f->offset,
                                            .c_string = f->type == PIN_STR &&
                                                        !f->text_descriptor,
                                            .range_contract = f->range_contract,
                                            .min = f->min,
                                            .max = f->max };
    }
    /* Generated native access remains catalog metadata, not authored callable
     * syntax. The same property without it keeps the aggregate snapshot path. */
    fields[field].read = getter ? 9400 : 0;
    /* Generic reference banks still need a reflected root descriptor. A safe
     * offset with a catalog-only field name denies only the scalar proof. */
    if (!reflected) {
        fields[field].name = "unproven_field";
    }
    const script_catalog_type root = { .id = READ_TYPE,
                                       .name = type->name,
                                       .type = { .storage = PIN_STRUCT,
                                                 .nominal = READ_TYPE },
                                       .fields = fields,
                                       .field_count = type->field_count,
                                       .size = type->size,
                                       .layout = type_layout_hash(type),
                                       .component = true,
                                       .component_id = READ_COMPONENT };
    const script_catalog_global self = { .id = READ_SELF,
                                         .name = "self",
                                         .type = { .storage = PIN_HANDLE,
                                                   .nominal = READ_ENTITY } };
    const script_catalog_parameter output = { .name = "value",
                                              .type = fields[field].type };
    const script_catalog_parameter entity = { .name = "entity",
                                              .type = { .storage = PIN_HANDLE,
                                                        .nominal = READ_ENTITY } };
    const script_catalog_capability capabilities[] = {
        { .id = 9400,
          .name = "read_field",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = &output,
          .result_count = 1,
          .component_access = SCRIPT_COMPONENT_ACCESS_READ,
          .component = READ_TYPE,
          .component_field = fields[field].id,
          .component_entity = explicit_entity ? 0 : -1,
          .component_value = -1,
          .parameters = explicit_entity ? &entity : NULL,
          .parameter_count = explicit_entity,
          .effects = SCRIPT_EFFECT_READ },
        { .id = 9500,
          .name = "get_entity",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = &entity,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_READ }
    };
    const script_catalog catalog = { .types = &root,
                                     .type_count = 1,
                                     .globals = &self,
                                     .global_count = 1,
                                     .capabilities = capabilities,
                                     .capability_count = 2,
                                     .entity_type = READ_ENTITY };
    char source[256], expression[96];
    snprintf(
        expression,
        sizeof expression,
        "%s.%s.%s",
        explicit_entity ? "get_entity()" : "self",
        type->name,
        fields[field].name
    );
    if (aggregate) {
        snprintf(
            source,
            sizeof source,
            "Baseline :: library { Read :: () -> %s { root := self.%s; return "
            "root.%s; } }",
            script_scalar_name(fields[field].type.storage),
            type->name,
            fields[field].name
        );
    } else {
        snprintf(
            source,
            sizeof source,
            "Baseline :: library { Read :: () -> %s { return %s; } }",
            script_scalar_name(fields[field].type.storage),
            expression
        );
    }
    syntax = script_parse(source);
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    for (int j = 0; analysis && j < analysis->diagnostic_count; j++) {
        fprintf(
            stderr,
            "scalar component semantic: %s\n",
            analysis->diagnostics[j].message
        );
    }
    REQUIRE(analysis && analysis->valid);
    const script_lower_bindings bindings = { .global = read_global,
                                             .host = read_host_binding };
    plan = script_lower(analysis, &catalog, &bindings);
    for (int j = 0; plan && j < plan->diagnostic_count; j++) {
        fprintf(stderr, "scalar component lower: %s\n", plan->diagnostics[j].message);
    }
    REQUIRE(plan && plan->valid);
    for (int j = 0; j < plan->function_count; j++) {
        behavior_function *body = plan->functions[j].body;
        if (body && !body->argument_count && body->result_count == 1) {
            REQUIRE(!function);
            function = body;
            plan->functions[j].body = NULL;
        }
    }
    REQUIRE(function && behavior_function_compile(function));
#ifdef __EMSCRIPTEN__
    REQUIRE(function->artifact->wasm_jit);
#else
    REQUIRE(behavior_function_entry(function));
    REQUIRE(behavior_function_compile_inspection(function));
    REQUIRE(function->artifact->inspection_code);
#endif
    return true;
}

static bool read_compile(int field, bool getter, bool managed)
{
    return read_compile_reflection(field, getter, managed, true, false, false);
}

static int read_instructions()
{
    int count = 0;
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        count += function->instructions[pc].op == BF_COMPONENT_READ;
    }
    return count;
}

static bool read_host(const behavior_function_instruction *i, const node_host_call *primitive)
{
    if (i->op != BF_HOST_CALL) {
        return false;
    }
    const behavior_function_host_call *host = &function->host_calls[i->target];
    uint64_t id = execution_intrinsic_id(primitive);
    /* Publication owns a copy of the host descriptor. Pin both its declared
     * identity and its actual production callback, not the descriptor address. */
    return host->capability_id == id &&
           host->capability_abi == execution_intrinsic_abi(id) &&
           host->call->value == primitive->value;
}

static int read_hosts(const node_host_call *primitive, bool runtime)
{
    int count = 0;
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        count += read_host(&function->instructions[pc], primitive) &&
                 (!runtime || behavior_function_runtime_keeps(function, pc));
    }
    return count;
}

static void read_begin(pin_value *output, int mode)
{
    release_run();
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    context = (behavior_function_context){ .results = output,
                                           .activation = &activation,
                                           .budget = READ_BUDGET,
                                           .source_pause = mode >= 2 ? inspect : NULL,
                                           .world = &read_fixture.world };
    behavior_function_bind_storage(&context, &storage);
    assert(behavior_function_bind(function, &context, NULL, READ_OWNER));
    /* Retain the canonical bank across calls, independently of arena values. */
    behavior_function_prepare_host(function, &context);
}

static int read_run(int mode)
{
    arena_reset(&storage);
    context.steps = 0;
    context.returned = false;
    return behavior_function_run(function, &context, mode & 1);
}

static bool read_equal(pin_value output, int field)
{
    const scalar_read_row *row = &read_fixture.rows[1];
    const type_field *f = &scalar_read_fields[field];
    REQUIRE(output.type == f->type);
    REQUIRE(!memcmp(&output.d, (const char *)row + f->offset, f->size));
    return true;
}

static double read_seconds()
{
    struct timespec now;
    int status = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(status == 0);
    (void)status;
    return now.tv_sec + now.tv_nsec * 1e-9;
}

static bool scalar_component_properties(bool benchmark)
{
    for (int getter = 0; getter < 2; getter++) {
        for (int field = 0; field < 3; field++) {
            REQUIRE(read_compile(field, getter, false));
            read_world(false);
            REQUIRE(type_is_plain(&scalar_read_type));
            REQUIRE(calls(false) == (getter ? 0 : 2));
            REQUIRE(calls(true) == calls(false));
            REQUIRE(read_instructions() == getter);
            REQUIRE(read_hosts(&EXECUTION_reference_make_call, true) == !getter);
            REQUIRE(read_hosts(&EXECUTION_reference_field_call, true) == 0);
            REQUIRE(read_hosts(&EXECUTION_reference_read_call, true) == !getter);
            /* local_address reserves a two-cell snapshot descriptor for the
             * writable authored result, but emits no snapshot host call. */
            REQUIRE(acount(function->data_values) == (getter ? 2 : 14));
            REQUIRE(acount(function->host_calls) == (getter ? 1 : 3));
            REQUIRE(read_hosts(&EXECUTION_snapshot_call, false) == 0);
            const behavior_function_host_call
                *result_bank = &function->host_calls[acount(function->host_calls) - 1];
            REQUIRE(result_bank->capability_id == execution_intrinsic_id(&EXECUTION_snapshot_call));
            REQUIRE(
                result_bank->result_first - result_bank->first == 1 &&
                result_bank->result_count == 1
            );
            REQUIRE(result_bank->first == (getter ? 0 : 12));
            /* Record native access's conservative write metadata too. */
            REQUIRE(acount(function->component_writes) == getter);
            printf(
                "SCALAR_READ path=%s field=%s canonical_hosts=%d "
                "runtime_hosts=%d "
                "make=%d field=%d read=%d descriptors=%d "
                "primitive_bank_values=%d "
                "result_bank_values=2 bank_values=%d bank_value_bytes=%zu "
                "writes=%d\n",
                getter ? "native-metadata" : "snapshot",
                scalar_read_fields[field].name,
                calls(false),
                calls(true),
                !getter,
                0,
                !getter,
                acount(function->host_calls),
                result_bank->first,
                acount(function->data_values),
                (size_t)acount(function->data_values) * sizeof(pin_value),
                acount(function->component_writes)
            );
            for (int mode = 0; mode < 4; mode++) {
                pin_value output = { .type = scalar_read_fields[field].type };
                xalloc_stats cold_before, cold_after;
                xalloc_stats_get(&cold_before);
                uint64_t cold_blocks = arena_block_allocations();
                uint64_t cold_banks = behavior_function_data_allocations();
                uint64_t cold_vm = behavior_function_vm_allocations();
                read_begin(&output, mode);
                REQUIRE(read_run(mode) == FLOW_OK);
                xalloc_stats_get(&cold_after);
                REQUIRE(read_equal(output, field));
                size_t bytes, capacity;
                arena_usage(&storage, &bytes, &capacity);
                REQUIRE(getter ? bytes == 0 : bytes > 0);
                size_t host_bytes = behavior_function_host_storage_bytes(&context);
                REQUIRE(host_bytes > 0);
                REQUIRE(behavior_function_data_allocations() - cold_banks == 1);
                printf(
                    "SCALAR_READ_COLD path=%s field=%s backend=%s inspected=%d "
                    "alloc_calls=%llu requested_bytes=%llu arena_blocks=%llu "
                    "bank_allocs=%llu vm_allocs=%llu host_bytes=%zu "
                    "arena_bytes=%zu\n",
                    getter ? "native-metadata" : "snapshot",
                    scalar_read_fields[field].name,
                    behavior_backend_name((mode & 1) ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE),
                    mode >= 2,
                    (unsigned long long)(cold_after.calls - cold_before.calls),
                    (unsigned long long)(cold_after.requested_bytes -
                                         cold_before.requested_bytes),
                    (unsigned long long)(arena_block_allocations() - cold_blocks),
                    (unsigned long long)(behavior_function_data_allocations() - cold_banks),
                    (unsigned long long)(behavior_function_vm_allocations() - cold_vm),
                    host_bytes,
                    bytes
                );
                /* One warm-reuse pass covers correctness; repeated timing
                 * trials belong only to the explicitly requested benchmark. */
                for (int trial = 0; trial < (benchmark ? READ_TRIALS : 1); trial++) {
                    xalloc_stats before, after;
                    xalloc_stats_get(&before);
                    uint64_t blocks = arena_block_allocations();
                    uint64_t banks = behavior_function_data_allocations();
                    uint64_t vm = behavior_function_vm_allocations();
#ifdef __EMSCRIPTEN__
                    execution_wasm_jit_stats wasm_before, wasm_after;
                    execution_wasm_jit_get_stats(&wasm_before);
#endif
                    double start = benchmark ? read_seconds() : 0;
                    for (int run = 0; run < READ_INVOCATIONS; run++) {
                        context.error_message = "stale scalar read error";
                        REQUIRE(read_run(mode) == FLOW_OK);
                        REQUIRE(read_equal(output, field));
                        REQUIRE(!context.error_message);
                    }
                    double elapsed = benchmark ? read_seconds() - start : 0;
                    xalloc_stats_get(&after);
                    REQUIRE(behavior_function_data_allocations() == banks);
                    REQUIRE(behavior_function_host_storage_bytes(&context) == host_bytes);
                    arena_usage(&storage, &bytes, &capacity);
#ifdef __EMSCRIPTEN__
                    execution_wasm_jit_get_stats(&wasm_after);
                    REQUIRE(
                        wasm_after.executions - wasm_before.executions ==
                        (uint64_t)((mode & 1) ? READ_INVOCATIONS : 0)
                    );
                    REQUIRE(wasm_after.compile_failures == wasm_before.compile_failures);
#endif
                    if (benchmark) {
                        printf(
                            "SCALAR_READ path=%s field=%s backend=%s "
                            "inspected=%d "
                            "trial=%d "
                            "calls=%d assertion_inclusive_seconds=%.9f "
                            "host_bytes=%zu arena_bytes=%zu "
                            "arena_capacity=%zu "
                            "alloc_calls=%llu requested_bytes=%llu "
                            "arena_blocks=%llu bank_allocs=%llu "
                            "vm_allocs=%llu\n",
                            getter ? "native-metadata" : "snapshot",
                            scalar_read_fields[field].name,
                            behavior_backend_name(
                                (mode & 1) ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE
                            ),
                            mode >= 2,
                            trial,
                            READ_INVOCATIONS,
                            elapsed,
                            host_bytes,
                            bytes,
                            capacity,
                            (unsigned long long)(after.calls - before.calls),
                            (unsigned long long)(after.requested_bytes -
                                                 before.requested_bytes),
                            (unsigned long long)(arena_block_allocations() - blocks),
                            (unsigned long long)(behavior_function_data_allocations() - banks),
                            (unsigned long long)(behavior_function_vm_allocations() - vm)
                        );
                    }
                }
            }
        }
    }
    return true;
}

TEST_CASE(scalar_component_properties_use_proven_native_access_metadata)
{
    return scalar_component_properties(false);
}

TEST_CASE(scalar_component_read_benchmark)
{
    return scalar_component_properties(true);
}

TEST_CASE(scalar_component_reads_preserve_failures_and_same_context_recovery)
{
    for (int getter = 0; getter < 2; getter++) {
        REQUIRE(read_compile(0, getter, false));
        const behavior_function_instruction *failed = NULL;
        uintptr_t steps = 0;
        for (int pc = 0; pc < acount(function->instructions); pc++) {
            const behavior_function_instruction *i = &function->instructions[pc];
            steps += i->op == BF_CHECK;
            if (i->op == BF_COMPONENT_READ ||
                read_host(i, &EXECUTION_reference_read_call)) {
                failed = i;
                break;
            }
        }
        REQUIRE(failed && failed->source >= 0);
        const char *expression = getter ? "self.ScalarRead.signed_value"
                                        : "self.ScalarRead";
        const char *start = strstr(syntax->text, expression);
        REQUIRE(start);
        const behavior_function_source
            *origin = behavior_function_source_at(function, failed->source);
        REQUIRE(origin && origin->asset_name && !strcmp(origin->asset_name, "Baseline"));
        REQUIRE(origin->start == start - syntax->text);
        REQUIRE(origin->end == origin->start + (int)strlen(expression));
        REQUIRE(origin->line == 1 && origin->column == origin->start + 1);
        printf(
            "SCALAR_READ_FAILURE path=%s cases=9 modes=4 status=%d reason=%d "
            "frame=%d source=%d stack=%d pin=%d steps=%llu "
            "asset=%s span=%d:%d line=%d column=%d\n",
            getter ? "native-metadata" : "snapshot",
            FLOW_ERR_INVALID_GRAPH,
            BEHAVIOR_FUNCTION_HOST_DATA,
            failed->frame,
            failed->source,
            failed->stack,
            failed->pin,
            (unsigned long long)steps,
            origin->asset_name,
            origin->start,
            origin->end,
            origin->line,
            origin->column
        );
        for (int mode = 0; mode < 4; mode++) {
            pin_value output = { .type = PIN_I32 };
            read_world(false);
            read_begin(&output, mode);
            /* Every failure is followed by a success with the identical context,
             * bank and arena. No reset helper clears the old diagnostic for it. */
            for (int failure = 0; failure < 9; failure++) {
                read_world(false);
                context.world = &read_fixture.world;
                context.owner = READ_OWNER;
                switch (failure) {
                    case 0: context.world = NULL; break;
                    case 1: context.owner = UINT64_C(7) << 32; break;
                    case 2: context.owner = (UINT64_C(7) << 32) | 2; break;
                    case 3: read_fixture.occupied[1] = 0; break;
                    case 4: read_fixture.generations[1]++; break;
                    case 5: read_fixture.membership = 0; break;
                    case 6: read_fixture.world.capacity = 0; break;
                    case 7: read_fixture.column.layout ^= 1; break;
                    case 8: read_fixture.column.stride++; break;
                }
                output.i = 123456;
                context.error_message = "stale scalar read error";
                REQUIRE(read_run(mode) == FLOW_ERR_INVALID_GRAPH);
                REQUIRE(output.i == 123456);
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, "Referenced storage is unavailable")
                );
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(context.node_id == failed->node_id);
                REQUIRE(
                    context.error_frame == failed->frame &&
                    context.error_source == failed->source
                );
                REQUIRE(
                    context.error_stack == failed->stack &&
                    context.error_pin == failed->pin
                );
                REQUIRE(context.error_steps == steps && context.steps == steps);
                read_world(false);
                context.world = &read_fixture.world;
                context.owner = READ_OWNER;
                REQUIRE(read_run(mode) == FLOW_OK);
                REQUIRE(output.i == -23 && !context.error_message);
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(
                    context.error_frame == failed->frame &&
                    context.error_source == failed->source
                );
                REQUIRE(
                    context.error_stack == failed->stack &&
                    context.error_pin == failed->pin
                );
                REQUIRE(context.error_steps == steps);
            }
        }
    }
    return true;
}

static const managed_read_row *read_captured;
static int read_captures;
static void read_capture(
    void *,
    behavior_function_context *,
    const behavior_source_watch *,
    const pin_value *value
)
{
    if (!value || value->type != PIN_STRUCT || !value->type_name ||
        strcmp(value->type_name, managed_read_type.name)) {
        return;
    }
    read_captures++;
    read_captured = value->ptr;
    read_fixture.managed[1].signed_value = 99;
    ((char *)read_fixture.managed[1].text)[0] = 'X';
}

TEST_CASE(scalar_component_reads_keep_managed_root_debug_snapshots)
{
    for (int getter = 0; getter < 2; getter++) {
        REQUIRE(read_compile(0, getter, true));
        REQUIRE(!type_is_plain(&managed_read_type));
        REQUIRE(calls(false) == (getter ? 0 : 2) && calls(true) == calls(false));
        REQUIRE(read_instructions() == getter);
        REQUIRE(read_hosts(&EXECUTION_reference_read_call, true) == !getter);
        REQUIRE(acount(function->data_values) == (getter ? 2 : 14));
        for (int mode = 0; mode < 4; mode++) {
            read_world(true);
            char text[] = "retained";
            read_fixture.managed[1] = (managed_read_row){ -23, text };
            pin_value output = { .type = PIN_I32 };
            read_begin(&output, mode);
            read_captured = NULL;
            read_captures = 0;
            context.source_capture = mode >= 2 ? read_capture : NULL;
            REQUIRE(read_run(mode) == FLOW_OK);
            REQUIRE(output.i == -23);
            size_t bytes, capacity;
            arena_usage(&storage, &bytes, &capacity);
            REQUIRE(getter ? bytes == 0 : bytes > 0);
            if (!getter && mode >= 2) {
                REQUIRE(read_captures == 1 && read_captured);
                REQUIRE(read_captured != &read_fixture.managed[1]);
                REQUIRE(read_captured->signed_value == -23);
                REQUIRE(read_captured->text != text && !strcmp(read_captured->text, "retained"));
                REQUIRE(read_fixture.managed[1].signed_value == 99 && text[0] == 'X');
            } else {
                /* Immediate native scalar properties do not publish the root
                 * snapshot, even beside unrelated managed fields. */
                REQUIRE(!read_captures && !read_captured);
                REQUIRE(read_fixture.managed[1].signed_value == -23 && !strcmp(text, "retained"));
            }
            printf(
                "SCALAR_READ_MANAGED path=%s backend=%s inspected=%d "
                "root_captures=%d runtime_hosts=%d bank_values=%d "
                "arena_bytes=%zu\n",
                getter ? "native-metadata" : "snapshot",
                behavior_backend_name((mode & 1) ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE),
                mode >= 2,
                read_captures,
                calls(true),
                acount(function->data_values),
                bytes
            );
        }
    }
    return true;
}

static bool read_finish()
{
    REQUIRE(finish());
#ifdef __EMSCRIPTEN__
    REQUIRE(function->artifact->wasm_jit);
#else
    REQUIRE(behavior_function_entry(function));
    REQUIRE(behavior_function_compile_inspection(function));
    REQUIRE(function->artifact->inspection_code);
#endif
    return true;
}

/* The raw fixture deliberately puts the live row beyond the first occupancy
 * word and the selected column behind a hash collision. No ECS engine or scene
 * setup is needed to exercise the public execution-view contract. */
#define READ_RAW_LAYOUT UINT64_C(0x1720394a5b6c7d8e)
enum
{
    READ_RAW_SLOT = 65,
    READ_RAW_SLOTS = 66
};
static struct
{
    pin_value rows[READ_RAW_SLOTS];
    uint32_t generations[READ_RAW_SLOTS];
    unsigned char occupied[READ_RAW_SLOTS];
    uint64_t membership[2];
    int lookup[4];
    ecs_execution_column columns[2];
    ecs_execution_view world;
} read_raw_fixture;

static void read_raw_world(pin_value stored)
{
    memset(&read_raw_fixture, 0, sizeof read_raw_fixture);
    read_raw_fixture.rows[READ_RAW_SLOT] = stored;
    read_raw_fixture.generations[READ_RAW_SLOT] = 7;
    read_raw_fixture.occupied[READ_RAW_SLOT] = 1;
    read_raw_fixture.membership[1] = 2;
    uint64_t hash = READ_COMPONENT * UINT64_C(11400714819323198485);
    int bucket = (int)((hash ^ (hash >> 32)) & 3);
    uint64_t collision = READ_COMPONENT + 1;
    for (; collision < READ_COMPONENT + 32; collision++) {
        hash = collision * UINT64_C(11400714819323198485);
        if ((int)((hash ^ (hash >> 32)) & 3) == bucket) {
            break;
        }
    }
    assert(collision < READ_COMPONENT + 32);
    read_raw_fixture.columns[1] = (ecs_execution_column){
        .id = READ_COMPONENT,
        .layout = READ_RAW_LAYOUT,
        .stride = sizeof(pin_value),
        .data = read_raw_fixture.rows,
        .occupancy = read_raw_fixture.membership,
        .count = 1,
        .singleton = READ_RAW_SLOT
    };
    read_raw_fixture.columns[0] = read_raw_fixture.columns[1];
    read_raw_fixture.columns[0].id = collision;
    read_raw_fixture.columns[0].layout ^= 1;
    read_raw_fixture.lookup[bucket] = 1;
    read_raw_fixture.lookup[(bucket + 1) & 3] = 2;
    read_raw_fixture.world = (ecs_execution_view){
        .generations = read_raw_fixture.generations,
        .occupied = read_raw_fixture.occupied,
        .slot_count = READ_RAW_SLOTS,
        .columns = read_raw_fixture.columns,
        .lookup = read_raw_fixture.lookup,
        .capacity = 4
    };
}

static behavior_function_instruction read_raw_instruction(pin_type type, int owner)
{
    return (
        behavior_function_instruction
    ){ .op = BF_COMPONENT_READ,
       .type = type,
       .left = owner,
       .component = behavior_function_builder_component(builder, READ_COMPONENT, false, false),
       .value = { .type = PIN_U64, .u64 = READ_COMPONENT },
       .layout = READ_RAW_LAYOUT,
       .offset = sizeof(pin_value),
       .target = offsetof(pin_value, d),
       .node_id = 917,
       .pin = 3 };
}

TEST_CASE(scalar_component_reads_copy_all_widths_and_preserve_live_registers)
{
    for (int handle = 0; handle < 2; handle++) {
        for (pin_type type = PIN_BOOL; type <= PIN_F64; type++) {
            cleanup();
            builder = behavior_function_builder_new();
            pin_value args[] = { { .type = handle ? PIN_HANDLE : PIN_U64,
                                   .u64 = (UINT64_C(7) << 32) | READ_RAW_SLOT },
                                 { .type = PIN_I64, .i64 = -INT64_C(0x123456789abcde) },
                                 { .type = PIN_F64, .d = -13.75 } };
            pin_value outputs[] = { { .type = type }, args[1], args[2] };
            behavior_function_builder_signature(builder, args, 3, outputs, 3, NULL, 0);
            int owner = argument(args[0], 0);
            int integer = argument(args[1], 1), floating = argument(args[2], 2);
            int copied = value(read_raw_instruction(type, owner));
            result(copied, type, 0);
            result(integer, PIN_I64, 1);
            result(floating, PIN_F64, 2);
            REQUIRE(read_finish());
            REQUIRE(read_instructions() == 1 && calls(true) == 0);
            REQUIRE(behavior_function_kind(function, BEHAVIOR_BANK_SLOT, copied) == BEHAVIOR_VALUE);
            const ecs_execution_column *stale[] = { &read_raw_fixture.columns[0] };
            for (int mode = 0; mode < 4; mode++) {
                for (int sample = 0; sample < 3; sample++) {
                    pin_value stored = { .type = type };
                    uint64_t bits = sample == 0 ? UINT64_C(0xfedcba9876543281)
                                  : sample == 1 ? 0
                                                : UINT64_MAX;
                    if (type == PIN_BOOL) {
                        stored.b = sample != 1;
                    } else if (type == PIN_F32) {
                        stored.f = sample == 0 ? -0.0f
                                 : sample == 1 ? INFINITY
                                               : NAN;
                    } else if (type == PIN_F64) {
                        stored.d = sample == 0 ? -0.0
                                 : sample == 1 ? -INFINITY
                                               : NAN;
                    } else {
                        stored = behavior_scalar_from_bits(type, bits);
                    }
                    read_raw_world(stored);
                    read_begin(outputs, mode);
                    context.arguments = args;
                    context.argument_count = 3;
                    context.world = &read_raw_fixture.world;
                    context.columns = stale;
                    context.binding_world = NULL;
                    context.error_message = "stale scalar read error";
                    REQUIRE(read_run(mode) == FLOW_OK);
                    REQUIRE(!memcmp(&outputs[0].d, &stored.d, behavior_scalar_size(type)));
                    REQUIRE(
                        outputs[1].i64 == args[1].i64 && outputs[2].d == args[2].d
                    );
                    REQUIRE(!context.error_message);
                    size_t bytes, capacity;
                    arena_usage(&storage, &bytes, &capacity);
                    REQUIRE(bytes == 0);
                    if (sample == 0) {
                        /* BOOL must check both layout and stride, not only
                         * report component presence like BF_COMPONENT BOOL. */
                        read_raw_fixture.columns[1].layout ^= 1;
                        REQUIRE(read_run(mode) == FLOW_ERR_INVALID_GRAPH);
                        read_raw_fixture.columns[1].layout ^= 1;
                        read_raw_fixture.columns[1].stride++;
                        REQUIRE(read_run(mode) == FLOW_ERR_INVALID_GRAPH);
                        read_raw_fixture.columns[1].stride--;
                        read_raw_fixture.membership[1] = 0;
                        REQUIRE(read_run(mode) == FLOW_ERR_INVALID_GRAPH);
                        read_raw_fixture.membership[1] = 2;
                        args[0].u64 = READ_RAW_SLOT;
                        REQUIRE(read_run(mode) == FLOW_ERR_INVALID_GRAPH);
                        args[0].u64 = (UINT64_C(7) << 32) | READ_RAW_SLOT;
                        read_raw_fixture.columns[1].id++;
                        REQUIRE(read_run(mode) == FLOW_ERR_INVALID_GRAPH);
                        read_raw_fixture.columns[1].id--;
                        REQUIRE(read_run(mode) == FLOW_OK && !context.error_message);
                    }
                }
            }
        }
    }
    return true;
}

TEST_CASE(scalar_component_reads_compose_with_query_variables_and_live_values)
{
    builder = behavior_function_builder_new();
    pin_value args[] = { { .type = PIN_I64, .i64 = -INT64_C(0x123456789abcde) },
                         { .type = PIN_F64, .d = -13.75 } };
    pin_value outputs[] = { { .type = PIN_F64 },
                            args[0],
                            args[1],
                            { .type = PIN_HANDLE },
                            { .type = PIN_HANDLE } };
    behavior_function_builder_signature(builder, args, 2, outputs, 5, NULL, 0);
    int integer = argument(args[0], 0), floating = argument(args[1], 1);
    int owner = value((behavior_function_instruction){ .op = BF_QUERY_VARIABLE,
                                                       .type = PIN_HANDLE,
                                                       .target = 1,
                                                       .node_id = 918,
                                                       .pin = 4 });
    int survivor = value((
        behavior_function_instruction
    ){ .op = BF_QUERY_VARIABLE, .type = PIN_HANDLE, .target = ECS_QUERY_VARS });
    int copied = value(read_raw_instruction(PIN_F64, owner));
    int after = value((behavior_function_instruction){ .op = BF_QUERY_VARIABLE,
                                                       .type = PIN_HANDLE,
                                                       .target = 1 });
    result(copied, PIN_F64, 0);
    result(integer, PIN_I64, 1);
    result(floating, PIN_F64, 2);
    result(survivor, PIN_HANDLE, 3);
    result(after, PIN_HANDLE, 4);
    REQUIRE(read_finish());
    REQUIRE(read_instructions() == 1 && calls(true) == 0);
    uint64_t tuple[ECS_QUERY_VARS] = { 0 };
    tuple[ECS_QUERY_VARS - 1] = UINT64_C(0xfedcba9876543210);
    for (int mode = 0; mode < 4; mode++) {
        pin_value stored = { .type = PIN_F64, .d = -0.0 };
        read_raw_world(stored);
        read_begin(outputs, mode);
        context.arguments = args;
        context.argument_count = 2;
        context.world = &read_raw_fixture.world;
        for (int phase = 0; phase < 5; phase++) {
            bool missing = phase == 1, stale = phase == 3;
            tuple[0] = ((stale ? UINT64_C(8) : UINT64_C(7)) << 32) | READ_RAW_SLOT;
            context.query_variables = missing ? NULL : tuple;
            context.error_message = "stale combined read error";
            pin_value sentinels[5];
            for (int j = 0; j < 5; j++) {
                outputs[j].u64 = UINT64_C(0x123456789abcdef0) + j;
                sentinels[j] = outputs[j];
            }
            REQUIRE(read_run(mode) == (missing || stale ? FLOW_ERR_INVALID_GRAPH : FLOW_OK));
            if (missing || stale) {
                for (int j = 0; j < 5; j++) {
                    REQUIRE(outputs[j].type == sentinels[j].type);
                    REQUIRE(outputs[j].u64 == sentinels[j].u64);
                }
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(context.node_id == (missing ? 918 : 917));
                REQUIRE(context.error_pin == (missing ? 4 : 3));
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, missing ? "Query variable is not bound" : "Referenced storage is unavailable")
                );
                continue;
            }
            REQUIRE(!context.error_message);
            REQUIRE(!memcmp(&outputs[0].d, &stored.d, sizeof stored.d));
            REQUIRE(outputs[1].i64 == args[0].i64 && outputs[2].d == args[1].d);
            REQUIRE(outputs[3].u64 == tuple[ECS_QUERY_VARS - 1]);
            REQUIRE(outputs[4].u64 == tuple[0]);
        }
    }
    return true;
}

TEST_CASE(scalar_component_reads_reject_invalid_bindings_types_bounds_and_provenance)
{
    enum
    {
        INVALID_CASES = 19
    };
    for (int invalid = 0; invalid < INVALID_CASES; invalid++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value arg = { .type = invalid == 4 ? PIN_I32 : PIN_U64 };
        behavior_function_builder_signature(builder, &arg, 1, NULL, 0, NULL, 0);
        int owner = argument(arg, 0);
        behavior_function_instruction i = read_raw_instruction(PIN_U64, owner);
        switch (invalid) {
            case 0: i.component++; break;
            case 1:
                i.component = behavior_function_builder_component(builder, READ_COMPONENT, true, false);
                break;
            case 2: i.value.u64++; break;
            case 3: i.value.type = PIN_HANDLE; break;
            case 4: break;
            case 5: i.target = -1; break;
            case 6: i.target = sizeof(pin_value); break;
            case 7: i.offset = 0; break;
            case 8: i.layout = 0; break;
            case 9: i.type = PIN_HANDLE; break;
            case 10: i.type = PIN_STR; break;
            case 11:
            case 12: i.type = BF_POINTER_TYPE; break;
            case 13:
                i.offset = offsetof(pin_value, d) + sizeof(uint64_t) - 1;
                break;
            case 14: i.target = INT_MAX; break;
            case 15: i.indirect = true; break;
            case 16: i.frame = INT_MAX; break;
            case 17: i.source = INT_MAX; break;
            case 18: i.stack = 7; break;
        }
        i.output = behavior_function_builder_slot(builder, i.type);
        if (invalid == 11 || invalid == 12) {
            REQUIRE(behavior_function_builder_kind(
                builder,
                BEHAVIOR_BANK_SLOT,
                i.output,
                invalid == 11 ? BEHAVIOR_ADDRESS : BEHAVIOR_REFERENCE
            ));
        }
        emit(i);
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(!function && error[0]);
        if (invalid == 11 || invalid == 12) {
            REQUIRE(strstr(error, "provenance"));
        }
    }
#if UINTPTR_MAX == UINT64_MAX
    for (int reference = 0; reference < 2; reference++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value arg = { .type = PIN_U64 };
        behavior_function_builder_signature(builder, &arg, 1, NULL, 0, NULL, 0);
        behavior_value_kind kind = reference ? BEHAVIOR_REFERENCE : BEHAVIOR_ADDRESS;
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_ARGUMENT, 0, kind));
        int owner = argument(arg, 0);
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, owner, kind));
        value(read_raw_instruction(PIN_I32, owner));
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(!function && strstr(error, "provenance"));
    }
#endif
    return true;
}

TEST_CASE(scalar_component_reads_retain_unused_failures_short_circuit_and_budget)
{
    builder = behavior_function_builder_new();
    pin_value skip = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, &skip, 1, NULL, 0, NULL, 0);
    int owner = value((behavior_function_instruction){ .op = BF_OWNER,
                                                       .type = PIN_HANDLE });
    int condition = argument(skip, 0);
    emit((behavior_function_instruction){ .op = BF_CHECK, .node_id = 918 });
    int branch = emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                       .left = condition });
    int read_pc = acount(behavior_function_builder_definition(builder)->instructions);
    value(read_raw_instruction(PIN_I32, owner));
    int returned = emit((behavior_function_instruction){ .op = BF_RETURN });
    REQUIRE(behavior_function_builder_patch_target(builder, branch, returned));
    REQUIRE(read_finish());
    REQUIRE(behavior_function_runtime_keeps(function, read_pc));
    for (int mode = 0; mode < 4; mode++) {
        read_begin(NULL, mode);
        context.arguments = &skip;
        context.argument_count = 1;
        context.world = NULL;
        for (int selected = 0; selected < 2; selected++) {
            skip.b = selected;
            context.budget = 0;
            REQUIRE(read_run(mode) == FLOW_ERR_STEP_BUDGET);
            REQUIRE(context.failure == BEHAVIOR_FUNCTION_BUDGET && context.error_steps == 0);
            context.budget = 1;
            REQUIRE(read_run(mode) == (selected ? FLOW_OK : FLOW_ERR_INVALID_GRAPH));
            REQUIRE(context.steps == 1);
            if (!selected) {
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(context.node_id == 917 && context.error_pin == 3);
                REQUIRE(context.error_steps == 1);
                REQUIRE(!strcmp(context.error_message, "Referenced storage is unavailable"));
            }
        }
    }
    return true;
}

static int read_writes;
static bool read_relocate;
static ecs_execution_view read_replacement_world;
static ecs_execution_column read_replacement_column;
static scalar_read_row read_replacement_rows[2];
static const char *read_mutate(void *, const pin_value *, pin_value *)
{
    read_writes++;
    read_fixture.rows[1].signed_value = 71;
    if (read_relocate) {
        context.world = &read_replacement_world;
    }
    return NULL;
}

TEST_CASE(scalar_component_reads_reacquire_after_writes_and_world_changes)
{
    builder = behavior_function_builder_new();
    pin_value outputs[] = { { .type = PIN_I32 }, { .type = PIN_I32 } };
    behavior_function_builder_signature(builder, NULL, 0, outputs, 2, NULL, 0);
    int owner = value((behavior_function_instruction){ .op = BF_OWNER,
                                                       .type = PIN_HANDLE });
    behavior_function_instruction read = read_raw_instruction(PIN_I32, owner);
    read.layout = type_layout_hash(&scalar_read_type);
    read.offset = sizeof(scalar_read_row);
    read.target = offsetof(scalar_read_row, signed_value);
    int first_pc = acount(behavior_function_builder_definition(builder)->instructions);
    int first = value(read);
    const node_host_call mutation = { .invoke = read_mutate };
    const uint64_t writes[] = { READ_COMPONENT };
    const node_access access = { .writes = writes, .write_count = 1 };
    int host = behavior_function_builder_host(builder, &mutation, &access, NULL, 0, NULL, 0);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    int second_pc = acount(behavior_function_builder_definition(builder)->instructions);
    int second = value(read);
    result(first, PIN_I32, 0);
    result(second, PIN_I32, 1);
    REQUIRE(read_finish());
    REQUIRE(read_instructions() == 2 && calls(true) == 1);
    REQUIRE(behavior_function_runtime_keeps(function, first_pc));
    REQUIRE(behavior_function_runtime_keeps(function, second_pc));
    for (int mode = 0; mode < 4; mode++) {
        for (int relocate = 0; relocate < 2; relocate++) {
            read_world(false);
            read_relocate = relocate;
            read_replacement_rows[1] = (scalar_read_row){ .signed_value = -88 };
            read_replacement_column = read_fixture.column;
            read_replacement_column.data = read_replacement_rows;
            read_replacement_world = read_fixture.world;
            read_replacement_world.columns = &read_replacement_column;
            const ecs_execution_column *stale[] = { &read_replacement_column };
            read_begin(outputs, mode);
            context.columns = stale;
            context.binding_world = &read_replacement_world;
            read_writes = 0;
            REQUIRE(read_run(mode) == FLOW_OK);
            REQUIRE(read_writes == 1);
            REQUIRE(outputs[0].i == -23 && outputs[1].i == (relocate ? -88 : 71));
        }
    }
    return true;
}

TEST_CASE(scalar_component_reads_keep_unproven_and_managed_fields_generic)
{
    REQUIRE(read_compile_reflection(0, true, false, false, false, false));
    REQUIRE(read_instructions() == 0 && calls(true) == 3);
    REQUIRE(read_hosts(&EXECUTION_reference_read_call, true) == 1);
    REQUIRE(acount(function->data_values) == 19);
    for (int mode = 0; mode < 4; mode++) {
        read_world(false);
        pin_value output = { .type = PIN_I32 };
        read_begin(&output, mode);
        REQUIRE(read_run(mode) == FLOW_OK && output.i == -23);
    }
    REQUIRE(read_compile(1, true, true));
    REQUIRE(read_instructions() == 0 && calls(true) == 4);
    REQUIRE(read_hosts(&EXECUTION_reference_make_call, true) == 1);
    REQUIRE(read_hosts(&EXECUTION_reference_field_call, true) == 1);
    REQUIRE(read_hosts(&EXECUTION_reference_read_call, true) == 1);
    /* Returning a managed string retains an additional owned result. */
    REQUIRE(read_hosts(&EXECUTION_snapshot_call, true) == 1);
    REQUIRE(acount(function->data_values) == 21);
    for (int mode = 0; mode < 4; mode++) {
        read_world(true);
        char text[] = "managed field";
        read_fixture.managed[1].text = text;
        pin_value output = { .type = PIN_STR };
        read_begin(&output, mode);
        REQUIRE(read_run(mode) == FLOW_OK);
        REQUIRE(
            pin_text(output).bytes && pin_text(output).bytes != text &&
            !strcmp(pin_text(output).bytes, text)
        );
        text[0] = 'X';
        REQUIRE(!strcmp(pin_text(output).bytes, "managed field"));
    }
    return true;
}

TEST_CASE(scalar_component_reads_evaluate_explicit_entity_once)
{
    REQUIRE(read_compile_reflection(0, true, false, true, true, false));
    REQUIRE(read_instructions() == 1 && calls(true) == 1);
    for (int mode = 0; mode < 4; mode++) {
        read_world(false);
        pin_value output = { .type = PIN_I32 };
        read_begin(&output, mode);
        read_entity_calls = 0;
        REQUIRE(read_run(mode) == FLOW_OK && output.i == -23);
        REQUIRE(read_entity_calls == 1);
        read_fixture.membership = 0;
        read_entity_calls = 0;
        REQUIRE(read_run(mode) == FLOW_ERR_INVALID_GRAPH);
        REQUIRE(read_entity_calls == 1);
    }
    return true;
}
TEST_CASE(scalar_component_properties_keep_explicit_aggregate_snapshots)
{
    REQUIRE(read_compile_reflection(0, true, true, true, false, true));
    REQUIRE(read_instructions() == 0);
    for (int mode = 0; mode < 4; mode++) {
        read_world(true);
        char text[] = "retained";
        read_fixture.managed[1] = (managed_read_row){ -23, text };
        pin_value output = { .type = PIN_I32 };
        read_begin(&output, mode);
        read_captured = NULL;
        read_captures = 0;
        context.source_capture = mode >= 2 ? read_capture : NULL;
        REQUIRE(read_run(mode) == FLOW_OK && output.i == -23);
        if (mode >= 2) {
            REQUIRE(read_captures > 0 && read_captured);
            REQUIRE(read_captured != &read_fixture.managed[1]);
            REQUIRE(read_captured->signed_value == -23);
            REQUIRE(read_captured->text != text && !strcmp(read_captured->text, "retained"));
            REQUIRE(read_fixture.managed[1].signed_value == 99 && text[0] == 'X');
        } else {
            REQUIRE(!read_captures && read_fixture.managed[1].signed_value == -23);
        }
    }
    return true;
}

TEST_CASE(scalar_component_reads_keep_copied_values_across_source_inspection)
{
    builder = behavior_function_builder_new();
    pin_value output = { .type = PIN_F64 };
    behavior_function_builder_signature(builder, NULL, 0, &output, 1, NULL, 0);
    int owner = value((behavior_function_instruction){ .op = BF_OWNER,
                                                       .type = PIN_HANDLE });
    int copied = value(read_raw_instruction(PIN_F64, owner));
    int point = behavior_function_builder_source_point(
        builder,
        (behavior_source_point){ .occurrence = 1, .kind = BEHAVIOR_SOURCE_STATEMENT }
    );
    emit((behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = point });
    result(copied, PIN_F64, 0);
    REQUIRE(read_finish());
    for (int backend = 0; backend < 2; backend++) {
        pin_value stored = { .type = PIN_F64, .d = -0.0 };
        read_raw_world(stored);
        read_begin(&output, backend + 2);
        context.owner = (UINT64_C(7) << 32) | READ_RAW_SLOT;
        context.world = &read_raw_fixture.world;
        pause_source = true;
        REQUIRE(read_run(backend + 2) == FLOW_PAUSED_RESULT);
        behavior_function_activation resumed = { 0 };
        behavior_function_activation_init(function, &resumed);
        behavior_function_copy_activation(function, &resumed, &activation);
        behavior_function_activation_free(&activation);
        activation = resumed;
        read_raw_fixture.rows[READ_RAW_SLOT].d = 41.5;
        context.world = NULL;
        context.source_pause = NULL;
        pause_source = false;
        REQUIRE(behavior_function_run(function, &context, backend) == FLOW_OK);
        REQUIRE(!memcmp(&output.d, &stored.d, sizeof stored.d));
    }
    return true;
}

void test_language_scalarize()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scalar_unions_remove_nonescaping_payloads_snapshots_and_tag_checks);
    RUN_TEST_CASE(scalar_unions_keep_escaping_and_managed_payloads_owned);
    RUN_TEST_CASE(scalar_unions_preserve_failed_extraction_contracts);
    RUN_TEST_CASE(scalar_unions_optimize_control_joins_but_keep_waits_owned);
    RUN_TEST_CASE(scalar_unions_reject_forged_intrinsic_identity);
    RUN_TEST_CASE(scalar_unions_keep_values_used_by_a_loop);
    RUN_TEST_CASE(scalar_unions_preserve_payload_before_secondary_slot_overwrite);
    RUN_TEST_CASE(scalar_unions_optimize_authored_local_patterns);
    RUN_TEST_CASE(scalar_unions_restore_payload_when_leaving_source_inspection);
    RUN_TEST_CASE(scalar_unions_follow_local_aliases_through_diamonds);
    RUN_TEST_CASE(scalar_unions_keep_distinct_incoming_values_materialized);
    RUN_TEST_CASE(scalar_unions_remove_nested_snapshots_and_keep_escaping_dependencies);
    RUN_TEST_CASE(scalar_unions_cross_isolated_calls_without_removing_effects_or_errors);
    RUN_TEST_CASE(scalar_unions_keep_nested_managed_payload_snapshots);
    RUN_TEST_CASE(scalar_unions_bound_control_flow_scratch_and_keep_safe_fallback);
    RUN_TEST_CASE(scalar_component_properties_use_proven_native_access_metadata);
    RUN_TEST_CASE(scalar_component_reads_preserve_failures_and_same_context_recovery);
    RUN_TEST_CASE(scalar_component_reads_keep_managed_root_debug_snapshots);
    RUN_TEST_CASE(scalar_component_reads_copy_all_widths_and_preserve_live_registers);
    RUN_TEST_CASE(scalar_component_reads_compose_with_query_variables_and_live_values);
    RUN_TEST_CASE(scalar_component_reads_reject_invalid_bindings_types_bounds_and_provenance);
    RUN_TEST_CASE(scalar_component_reads_retain_unused_failures_short_circuit_and_budget);
    RUN_TEST_CASE(scalar_component_reads_reacquire_after_writes_and_world_changes);
    RUN_TEST_CASE(scalar_component_reads_keep_unproven_and_managed_fields_generic);
    RUN_TEST_CASE(scalar_component_reads_evaluate_explicit_entity_once);
    RUN_TEST_CASE(scalar_component_properties_keep_explicit_aggregate_snapshots);
    RUN_TEST_CASE(scalar_component_reads_keep_copied_values_across_source_inspection);
    pu_clear_setup();
}

void test_language_scalar_read_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scalar_component_read_benchmark);
    pu_clear_setup();
}
