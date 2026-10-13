#include "execution/borrow.h"

#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static behavior_function_builder *builder;
static behavior_function *function;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module, *decoded;
static unsigned char *prepared, *corrupt;
static bool borrowed_function;
static int result_bank, opaque_calls;
static bool pause;
static const pin_value array_shape = { .type = PIN_F64,
                                       .container = PIN_CONTAINER_ARRAY };

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
    if (!borrowed_function) {
        behavior_function_free(function);
    }
    if (builder) {
        behavior_function_builder_free(builder);
    }
    execution_module_plan_free(plan);
    execution_module_free(module);
    execution_module_free(decoded);
    free(prepared);
    free(corrupt);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    function = NULL;
    builder = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    module = decoded = NULL;
    prepared = corrupt = NULL;
    borrowed_function = false;
    pause = false;
}

static int emit(behavior_function_instruction i)
{
    return behavior_function_builder_emit(builder, i);
}

static int value(behavior_function_instruction i, bool address)
{
    i.output = behavior_function_builder_slot(
        builder,
        i.op >= BF_LT && i.op <= BF_NE ? PIN_BOOL : i.type
    );
    if (address) {
        assert(
            behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, i.output, BEHAVIOR_ADDRESS)
        );
    }
    emit(i);
    return i.output;
}

static int host_address(int bank)
{
    return value(
        (behavior_function_instruction){ .op = BF_HOST_SLOT,
                                         .type = BF_POINTER_TYPE,
                                         .left = bank },
        true
    );
}

static int snapshot(int input, const node_host_call *call, int flag)
{
    int host = behavior_function_builder_host(
        builder,
        call,
        &EXECUTION_value_access,
        &array_shape,
        1,
        &array_shape,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_snapshot_call);
    assert(behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id)));
    int address = host_address(behavior_function_builder_host_first(builder, host));
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = BF_POINTER_TYPE,
                                          .left = input,
                                          .right = address,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                          .right = flag });
    result_bank = behavior_function_builder_host_results(builder, host);
    address = host_address(result_bank);
    int result = value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = address,
                                         .offset = offsetof(pin_value, d) },
        true
    );
    behavior_function_builder_root(builder, BEHAVIOR_BANK_SLOT, result, array_shape);
    return result;
}

static int first_item(int array)
{
    int items = value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = array,
                                         .offset = offsetof(pin_array, items) },
        true
    );
    int zero = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .value = { .type = PIN_I32 } },
        false
    );
    return value(
        (behavior_function_instruction){ .op = BF_INDEX,
                                         .type = BF_POINTER_TYPE,
                                         .left = items,
                                         .right = zero,
                                         .offset = sizeof(pin_value) },
        true
    );
}

static const char *opaque(arena *a, const pin_value *args, int count, pin_value *out)
{
    opaque_calls++;
    return EXECUTION_snapshot_call.value(a, args, count, out);
}

enum
{
    COUNT,
    PAYLOAD,
    CAPACITY,
    IDENTITY,
    RAW_WRITE,
    VARIABLE_ALIAS,
    RESULT_ALIAS,
    HOST_ALIAS,
    PAUSE,
    WAIT,
    OPAQUE,
    ESCAPE,
    FORGED_FLAG
};

static bool fixture(int kind)
{
    builder = behavior_function_builder_new();
    pin_value output = { .type = kind == IDENTITY ? PIN_BOOL : PIN_F64 };
    if (kind == ESCAPE) {
        output = array_shape;
    }
    pin_value scalar = { .type = PIN_F64 };
    behavior_function_builder_signature(
        builder,
        &array_shape,
        1,
        &output,
        1,
        &scalar,
        kind == VARIABLE_ALIAS ? 1 : 0
    );
    if (kind == HOST_ALIAS) {
        REQUIRE(behavior_function_builder_values(builder, &scalar, 1) == 0);
    }
    int original = value(
        (behavior_function_instruction){ .op = BF_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .left = 0,
                                         .target = BF_ARGUMENT,
                                         .value = array_shape },
        true
    );
    node_host_call external = { .value = opaque };
    int held = snapshot(
        original,
        kind == OPAQUE ? &external : &EXECUTION_snapshot_call,
        kind == FORGED_FLAG ? BF_HOST_BORROW_ARRAY : 0
    );
    if (kind == RAW_WRITE) {
        int local = behavior_function_builder_local(builder, (pin_value){ .type = BF_POINTER_TYPE });
        assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_LOCAL, local, BEHAVIOR_ADDRESS));
        behavior_function_builder_root(builder, BEHAVIOR_BANK_LOCAL, local, array_shape);
        emit((behavior_function_instruction){ .op = BF_SAVE,
                                              .type = BF_POINTER_TYPE,
                                              .left = original,
                                              .target = local });
        original = value(
            (behavior_function_instruction){ .op = BF_LOCAL,
                                             .type = BF_POINTER_TYPE,
                                             .left = local },
            true
        );
    }
    if (kind == RAW_WRITE || kind == VARIABLE_ALIAS || kind == RESULT_ALIAS ||
        kind == HOST_ALIAS) {
        int replacement = value(
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = PIN_F64,
                                             .value = { .type = PIN_F64, .d = 99 } },
            false
        );
        if (kind == RAW_WRITE || kind == HOST_ALIAS) {
            int item = kind == RAW_WRITE ? first_item(original) : host_address(0);
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = PIN_F64,
                                                  .left = replacement,
                                                  .right = item,
                                                  .offset = offsetof(pin_value, d) });
        } else {
            emit((behavior_function_instruction){
                .op = kind == VARIABLE_ALIAS ? BF_STORE : BF_RESULT,
                .type = PIN_F64,
                .left = replacement });
        }
    }
    if (kind == PAUSE) {
        int point = behavior_function_builder_source_point(
            builder,
            (behavior_source_point){ .occurrence = 1, .kind = BEHAVIOR_SOURCE_STATEMENT }
        );
        emit((behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = point });
    }
    if (kind == WAIT) {
        emit((behavior_function_instruction){ .op = BF_SUSPEND });
    }
    int result;
    if (kind == ESCAPE) {
        int name = value(
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = BF_POINTER_TYPE,
                                             .value = { .type = BF_POINTER_TYPE } },
            true
        );
        emit((behavior_function_instruction){ .op = BF_RESULT_ARRAY,
                                              .type = PIN_F64,
                                              .left = held,
                                              .right = name,
                                              .value = array_shape });
    } else {
        if (kind == IDENTITY) {
            result = value(
                (behavior_function_instruction){ .op = BF_EQ,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = original,
                                                 .right = held },
                false
            );
        } else if (kind == COUNT || kind == CAPACITY || kind == OPAQUE || kind == FORGED_FLAG) {
            int count = value(
                (behavior_function_instruction){
                    .op = BF_READ,
                    .type = PIN_I32,
                    .left = held,
                    .offset = kind == CAPACITY ? offsetof(pin_array, capacity)
                                               : offsetof(pin_array, count) },
                false
            );
            result = value(
                (behavior_function_instruction){ .op = BF_CAST, .type = PIN_F64, .left = count },
                false
            );
        } else {
            int item = first_item(held);
            result = value(
                (behavior_function_instruction){ .op = BF_LOAD,
                                                 .type = PIN_F64,
                                                 .left = item,
                                                 .offset = offsetof(pin_value, d) },
                false
            );
        }
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = output.type,
                                              .left = result });
    }
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (kind == FORGED_FLAG) {
        REQUIRE(!function);
        REQUIRE(strstr(error, "runtime host flags"));
        return true;
    }
    if (!function) {
        fprintf(stderr, "borrow fixture: %s\n", error);
    }
    REQUIRE(function && behavior_function_compile(function));
    return true;
}

static int inspect(void *, behavior_function_context *, const behavior_source_point *)
{
    return pause;
}

static void prepare(pin_value *arguments, int count, pin_value *results, pin_value *variables, int mode)
{
    release_run();
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    context = (behavior_function_context){ .arguments = arguments,
                                           .argument_count = count,
                                           .results = results,
                                           .variables = variables,
                                           .activation = &activation,
                                           .budget = 10000,
                                           .source_pause = mode >= 2 ? inspect : NULL };
    behavior_function_bind_storage(&context, &storage);
    assert(behavior_function_bind(function, &context, NULL, 0));
    behavior_function_prepare_host(function, &context);
}

static int begin(pin_value *arguments, int count, pin_value *results, pin_value *variables, int mode)
{
    prepare(arguments, count, results, variables, mode);
    return behavior_function_run(function, &context, mode % 2);
}

static int guarded()
{
    int guarded = 0;
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        if (function->instructions[pc].op == BF_HOST_CALL) {
            assert(!function->instructions[pc].right);
            guarded += function->runtime_body[pc].right == BF_HOST_BORROW_ARRAY;
        }
    }
    return guarded;
}

static size_t bytes()
{
    size_t used, capacity;
    arena_usage(&storage, &used, &capacity);
    return used;
}

TEST_CASE(borrow_authored_readonly_loop_keeps_scalar_writes)
{
    const script_catalog_capability event = { .id = 10,
                                              .name = "Run",
                                              .kind = SCRIPT_CAPABILITY_EVENT };
    const script_catalog catalog = { .capabilities = &event, .capability_count = 1 };
    syntax = script_parse("behavior Borrow { values: double[]=[]; total: double=0; on Run { sum:=0.0; for value in values { sum+=value; } total+=sum; } }");
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis && analysis->valid);
    const script_lower_bindings bindings = { 0 };
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    for (int j = 0; j < plan->function_count; j++) {
        if (plan->functions[j].body) {
            function = plan->functions[j].body;
            plan->functions[j].body = NULL;
            break;
        }
    }
    REQUIRE(function && behavior_function_compile(function));
    REQUIRE(function->variable_count == 2);
    REQUIRE(guarded() == 2);
    pin_value items[128];
    for (int j = 0; j < 128; j++) {
        items[j] = (pin_value){ .type = PIN_F64, .d = j + 1 };
    }
    const int sizes[] = { 0, 1, 128 };
    int array_index = function->variable_defaults[0].container == PIN_CONTAINER_ARRAY
                        ? 0
                        : 1;
    for (int mode = 0; mode < 4; mode++) {
        for (int j = 0; j < 3; j++) {
            int count = sizes[j];
            pin_array array = { .element_type = PIN_F64,
                                .count = count,
                                .capacity = 128,
                                .items = items };
            pin_value variables[2];
            memcpy(variables, function->variable_defaults, sizeof variables);
            variables[array_index].ptr = &array;
            REQUIRE(begin(NULL, 0, NULL, variables, mode) == FLOW_OK);
            REQUIRE(variables[1 - array_index].d == (double)count * (count + 1) / 2);
            REQUIRE(mode < 2 ? bytes() == 0 : bytes() > 0);
        }
    }
    return true;
}

TEST_CASE(borrow_guard_preserves_null_empty_mixed_headers_and_errors)
{
    REQUIRE(fixture(COUNT));
    REQUIRE(guarded() == 1);
    for (int mode = 0; mode < 4; mode++) {
        for (int variant = 0; variant < 8; variant++) {
            pin_value item = { .type = PIN_F64, .d = 7 };
            pin_array array = { .element_type = PIN_F64,
                                .count = 1,
                                .capacity = 1,
                                .items = &item };
            pin_value input = array_shape, output = { .type = PIN_F64 };
            input.ptr = &array;
            if (variant == 0) {
                input.ptr = NULL;
            } else if (variant == 1) {
                array.count = 0;
            } else if (variant == 3) {
                item = (pin_value){ .type = PIN_STR, .str = "retained" };
            } else if (variant == 4) {
                array.element_type = PIN_I32;
            } else if (variant == 5) {
                array.type_name = "unexpected";
            } else if (variant == 6) {
                item.type_name = "unexpected";
            } else if (variant == 7) {
                item = (pin_value){ .type = PIN_STRUCT,
                                    .type_name = "unknown.borrow.type",
                                    .ptr = &array };
            }
            int status = begin(&input, 1, &output, NULL, mode);
            if (variant == 7) {
                REQUIRE(status == FLOW_ERR_INVALID_GRAPH);
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, "Cannot retain language value")
                );
                continue;
            }
            REQUIRE(status == FLOW_OK);
            REQUIRE(output.d == (variant < 2 ? 0 : 1));
            bool borrowed = mode < 2 && variant <= 2;
            REQUIRE(variant == 0 || (borrowed ? bytes() == 0 : bytes() > 0));
            REQUIRE(
                borrowed ? context.data->values[result_bank].ptr == input.ptr
                         : variant == 0 ||
                               context.data->values[result_bank].ptr != input.ptr
            );
            if (variant == 3) {
                pin_array *retained = context.data->values[result_bank].ptr;
                REQUIRE(retained->items[0].str != item.str);
                REQUIRE(!strcmp(retained->items[0].str, item.str));
            }
        }
    }
    /* A bad negative length follows the canonical path, whose allocator asserts;
     * check only guard selection without deliberately crashing the process. */
    pin_array bad = { .element_type = PIN_F64, .count = -1 };
    pin_value input = array_shape, output;
    input.ptr = &bad;
    REQUIRE(
        !behavior_borrow_array(&context, 0, 1, acount(function->data_values), &input, &output)
    );
    return true;
}

TEST_CASE(borrow_guards_scalar_banks_aliasing_array_storage)
{
    for (int kind = VARIABLE_ALIAS; kind <= HOST_ALIAS; kind++) {
        cleanup();
        REQUIRE(fixture(kind));
        REQUIRE(guarded() == 1);
        for (int mode = 0; mode < 4; mode++) {
            pin_value item = { .type = PIN_F64, .d = 7 }, output = item;
            pin_array array = { .element_type = PIN_F64,
                                .count = 1,
                                .capacity = 1,
                                .items = &item };
            pin_value input = array_shape;
            input.ptr = &array;
            prepare(
                &input,
                1,
                kind == RESULT_ALIAS ? &item : &output,
                kind == VARIABLE_ALIAS ? &item : NULL,
                mode
            );
            if (kind == HOST_ALIAS) {
                context.data->values[0] = item;
                array.items = context.data->values;
            }
            REQUIRE(behavior_function_run(function, &context, mode % 2) == FLOW_OK);
            REQUIRE((kind == RESULT_ALIAS ? item.d : output.d) == 7);
            REQUIRE(kind != VARIABLE_ALIAS || item.d == 99);
            REQUIRE(kind != HOST_ALIAS || array.items[0].d == 99);
            REQUIRE(bytes() > 0);
        }
    }
    return true;
}

TEST_CASE(borrow_rejects_mutation_identity_headers_escape_and_opaque_calls)
{
    const int kinds[] = { RAW_WRITE, IDENTITY, CAPACITY, ESCAPE, OPAQUE };
    for (int j = 0; j < 5; j++) {
        cleanup();
        int kind = kinds[j];
        REQUIRE(fixture(kind));
        REQUIRE(guarded() == 0);
        for (int mode = 0; mode < 4; mode++) {
            pin_value item = { .type = PIN_F64, .d = 7 };
            pin_array array = { .element_type = PIN_F64,
                                .count = 1,
                                .capacity = 8,
                                .items = &item };
            pin_value input = array_shape, output = function->result_defaults[0];
            input.ptr = &array;
            opaque_calls = 0;
            REQUIRE(begin(&input, 1, &output, NULL, mode) == FLOW_OK);
            REQUIRE(bytes() > 0);
            if (kind == IDENTITY) {
                REQUIRE(!output.b);
            } else if (kind == ESCAPE) {
                REQUIRE(output.ptr && output.ptr != input.ptr);
                REQUIRE(((pin_array *)output.ptr)->items[0].d == 7);
            } else {
                REQUIRE(output.d == (kind == RAW_WRITE ? 7 : 1));
            }
            REQUIRE(kind != RAW_WRITE || item.d == 99);
            REQUIRE(opaque_calls == (kind == OPAQUE ? 1 : 0));
        }
    }
    return true;
}

TEST_CASE(borrow_preserves_ownership_across_debug_and_real_suspension)
{
    for (int kind = PAUSE; kind <= WAIT; kind++) {
        cleanup();
        REQUIRE(fixture(kind));
        REQUIRE(guarded() == (kind == PAUSE ? 1 : 0));
        for (int backend = 0; backend < 2; backend++) {
            pin_value item = { .type = PIN_F64, .d = 7 }, output = item;
            pin_array array = { .element_type = PIN_F64,
                                .count = 1,
                                .capacity = 1,
                                .items = &item };
            pin_value input = array_shape;
            input.ptr = &array;
            pause = kind == PAUSE;
            REQUIRE(
                begin(&input, 1, &output, NULL, backend + (pause ? 2 : 0)) ==
                (pause ? FLOW_PAUSED_RESULT : FLOW_SUSPENDED_RESULT)
            );
            REQUIRE(bytes() > 0);
            behavior_function_activation clone = { 0 };
            behavior_function_activation_init(function, &clone);
            behavior_function_copy_activation(function, &clone, &activation);
            behavior_function_activation_free(&activation);
            activation = clone;
            item.d = 99;
            pause = false;
            context.source_pause = kind == WAIT ? inspect : NULL;
            REQUIRE(behavior_function_run(function, &context, backend) == FLOW_OK);
            REQUIRE(output.d == 7);
        }
    }
    return true;
}

TEST_CASE(borrow_flags_cannot_enter_canonical_or_prepared_code)
{
    REQUIRE(fixture(FORGED_FLAG));
    cleanup();
    const script_catalog catalog = { 0 };
    const script_lower_bindings bindings = { 0 };
    syntax = script_parse("library Borrow { Count :: pure (values:double[])->double { total:=0.0; for value in values { total+=1; } return total; } }");
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis && analysis->valid);
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    char error[256];
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    borrowed_function = true;
    function = (behavior_function *)execution_module_function_at(module, 0).body;
    REQUIRE(function && guarded() == 1);
    size_t length, corrupt_length;
    REQUIRE(execution_prepared_write(module, &prepared, &length, error, sizeof error));
    REQUIRE(length < 65536);
    const execution_prepared_linker linker = { 0 };
    decoded = execution_prepared_read(prepared, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(decoded);
    function = (behavior_function *)execution_module_function_at(decoded, 0).body;
    REQUIRE(function && guarded() == 1);
    pin_value items[] = { { .type = PIN_F64, .d = 2 }, { .type = PIN_F64, .d = 3 } };
    pin_array array = { .element_type = PIN_F64, .count = 2, .capacity = 2, .items = items };
    pin_value input = array_shape, output = { .type = PIN_F64 };
    input.ptr = &array;
    REQUIRE(begin(&input, 1, &output, NULL, 0) == FLOW_OK);
    REQUIRE(output.d == 2 && bytes() == 0);
    release_run();
    /* Forge a canonical instruction solely to produce a hostile prepared
     * fixture. Its reader must reject the flag before recomputing runtime IR. */
    function = (behavior_function *)execution_module_function_at(module, 0).body;
    int host = -1;
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        if (function->instructions[pc].op == BF_HOST_CALL) {
            host = pc;
            break;
        }
    }
    REQUIRE(host >= 0);
    function->instructions[host].right = BF_HOST_BORROW_ARRAY;
    bool written = execution_prepared_write(module, &corrupt, &corrupt_length, error, sizeof error);
    function->instructions[host].right = 0;
    REQUIRE(written && corrupt_length < 65536);
    execution_module *invalid = execution_prepared_read(
        corrupt,
        corrupt_length,
        &linker,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    REQUIRE(!invalid);
    /* The prepared reader wraps the detailed builder diagnostic checked above. */
    REQUIRE(!strcmp(error, "prepared definition verification failed"));
    return true;
}

void test_language_borrow()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(borrow_authored_readonly_loop_keeps_scalar_writes);
    RUN_TEST_CASE(borrow_guard_preserves_null_empty_mixed_headers_and_errors);
    RUN_TEST_CASE(borrow_guards_scalar_banks_aliasing_array_storage);
    RUN_TEST_CASE(borrow_rejects_mutation_identity_headers_escape_and_opaque_calls);
    RUN_TEST_CASE(borrow_preserves_ownership_across_debug_and_real_suspension);
    RUN_TEST_CASE(borrow_flags_cannot_enter_canonical_or_prepared_code);
    pu_clear_setup();
}
