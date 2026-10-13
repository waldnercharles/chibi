#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/union.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static behavior_function_builder *builder;
static behavior_function *function;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static bool pause_source;

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
    const pin_value cases[] = { { .type = PIN_F64, .d = 2.75 },
                                { .type = PIN_STR, .str = "" },
                                { .type = PIN_STR },
                                { .type = PIN_STR, .str = "original" } };
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
            if (j == 3) {
                input.str = text;
            }
            REQUIRE(begin(&input, 1, &output, backend) == FLOW_OK);
            const pin_union *retained = output.ptr;
            REQUIRE(retained && retained->tag == 42 && retained->count == 1);
            if (!j) {
                REQUIRE(retained->values[0].d == input.d);
            } else if (input.str) {
                text[0] = 'X';
                REQUIRE(!strcmp(retained->values[0].str, cases[j].str));
            } else {
                REQUIRE(!retained->values[0].str);
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
    syntax = script_parse(
        "library Scalar { Eval :: pure (amount: number) -> number { "
        "value: number | Cancelled = amount; "
        "if value is number found { return found; } return 0; } }"
    );
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
    pin_value input = { .type = PIN_F64, .d = 3.75 }, output = { .type = PIN_F64 };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(begin(&input, 1, &output, backend) == FLOW_OK);
        REQUIRE(output.d == input.d);
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
        input.str = original;
        REQUIRE(begin(&input, 1, &output, mode) == FLOW_OK);
        original[0] = 'X';
        REQUIRE(output.str && !strcmp(output.str, "snapshot"));
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
    pu_clear_setup();
}
