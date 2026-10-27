#include "execution/borrow.h"

#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module_effects.h"
#include "execution/preparation_budget.h"
#include "execution/prepared.h"
#include "execution/program_internal.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/copy.h"
#include "types/pointer.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static behavior_function_builder *builder;
static behavior_function *function;
static behavior_function *helper_function;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage, string_inputs;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module, *decoded;
static execution_program *test_program;
static execution_activation *test_activation;
static ecs_world *test_world;
static unsigned char *prepared, *corrupt;
static bool borrowed_function;
static int result_bank, opaque_calls, alias_slot;
static bool pause;
static pin_value *callback_item;
static int entered_calls;
enum
{
    NESTED_LINKS = 4,
    NESTED_LINK_SLOTS = 40000
};
static behavior_function *links[NESTED_LINKS + 1];
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
    arena_free(&string_inputs);
    if (test_activation) {
        program_activation_free(test_activation);
    }
    execution_program_free(test_program);
    ecs_world_free(test_world);
    test_activation = NULL;
    test_program = NULL;
    test_world = NULL;
    if (!borrowed_function) {
        behavior_function_free(function);
    }
    behavior_function_free(helper_function);
    for (int i = 0; i <= NESTED_LINKS; i++) {
        behavior_function_free(links[i]);
        links[i] = NULL;
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
    helper_function = NULL;
    builder = NULL;
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    module = decoded = NULL;
    prepared = corrupt = NULL;
    borrowed_function = false;
    pause = false;
    callback_item = NULL;
    entered_calls = 0;
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

/* Address the first scalar payload in either physical representation. */
static int first_item(int array)
{
    int items = value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = array,
                                         .offset = offsetof(pin_array, items) },
        true
    );
    int data = value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = array,
                                         .offset = offsetof(pin_array, data) },
        true
    );
    int stride = value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = array,
                                         .offset = offsetof(pin_array, stride) },
        false
    );
    int zero = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = BF_POINTER_TYPE,
                                         .value = { .type = BF_POINTER_TYPE } },
        false
    );
    int packed = value(
        (
            behavior_function_instruction
        ){ .op = BF_NE, .type = BF_POINTER_TYPE, .left = stride, .right = zero },
        false
    );
    int base = value(
        (behavior_function_instruction){ .op = BF_SELECT,
                                         .type = BF_POINTER_TYPE,
                                         .left = packed,
                                         .right = data,
                                         .target = items },
        true
    );
    int boxed_offset = value(
        (behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = BF_POINTER_TYPE,
            .value = { .type = BF_POINTER_TYPE, .u64 = offsetof(pin_value, d) } },
        false
    );
    int offset = value(
        (behavior_function_instruction){ .op = BF_SELECT,
                                         .type = BF_POINTER_TYPE,
                                         .left = packed,
                                         .right = zero,
                                         .target = boxed_offset },
        false
    );
    return value(
        (
            behavior_function_instruction
        ){ .op = BF_ADD, .type = BF_POINTER_TYPE, .left = base, .right = offset },
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
    SAVED_ALIAS,
    VM_ALIAS,
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
    if (kind == SAVED_ALIAS) {
        /* Leave enough saved words for one complete item header. The parameter
         * store below overwrites that item's payload in every backend. */
        int words = (int)((sizeof(pin_value) + sizeof(uint64_t) - 1) / sizeof(uint64_t));
        for (int j = 0; j < words; j++) {
            behavior_function_builder_local(builder, scalar);
        }
        behavior_function *draft = (behavior_function *)
            behavior_function_builder_definition(builder);
        draft->locals[offsetof(pin_value, d) / sizeof(uint64_t)].parameter = true;
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
        kind == HOST_ALIAS || kind == SAVED_ALIAS || kind == VM_ALIAS) {
        int replacement = value(
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = PIN_F64,
                                             .value = { .type = PIN_F64, .d = 99 } },
            false
        );
        if (kind == SAVED_ALIAS) {
            emit((behavior_function_instruction){
                .op = BF_SAVE,
                .type = PIN_F64,
                .left = replacement,
                .target = offsetof(pin_value, d) / sizeof(uint64_t) });
        } else if (kind == VM_ALIAS) {
            alias_slot = replacement;
            /* Keep the overwrite observable to ordinary IR liveness. */
            emit((behavior_function_instruction){ .op = BF_RESULT,
                                                  .type = PIN_F64,
                                                  .left = replacement });
        } else if (kind == RAW_WRITE || kind == HOST_ALIAS) {
            int item = kind == RAW_WRITE ? first_item(original) : host_address(0);
            emit((behavior_function_instruction){
                .op = BF_WRITE,
                .type = PIN_F64,
                .left = replacement,
                .right = item,
                .offset = kind == RAW_WRITE ? 0 : offsetof(pin_value, d) });
        } else {
            emit((behavior_function_instruction){
                .op = kind == VARIABLE_ALIAS ? BF_STORE : BF_RESULT,
                .type = PIN_F64,
                .left = replacement });
        }
    }
    if (kind == PAUSE || kind == SAVED_ALIAS || kind == VM_ALIAS) {
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
                (
                    behavior_function_instruction
                ){ .op = BF_LOAD, .type = PIN_F64, .left = item, .offset = 0 },
                false
            );
        }
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = output.type,
                                              .left = result });
    }
    emit((behavior_function_instruction){ .op = BF_RETURN });
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

static bool forwarding_fixture(int kind)
{
    REQUIRE(fixture(kind));
    helper_function = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    pin_value scalar = { .type = PIN_F64 };
    behavior_function_builder_signature(builder, &array_shape, 1, &scalar, 1, NULL, 0);
    int original = value(
        (behavior_function_instruction){ .op = BF_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .target = BF_ARGUMENT,
                                         .value = array_shape },
        true
    );
    int held = snapshot(original, &EXECUTION_snapshot_call, 0);
    int call = behavior_function_builder_call(builder, helper_function, &array_shape, 1, &scalar, 1);
    int argument = host_address(behavior_function_builder_call_first(builder, call));
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = BF_POINTER_TYPE,
                                          .left = held,
                                          .right = argument,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    int address = host_address(behavior_function_builder_call_results(builder, call));
    int result = value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = PIN_F64,
                                         .left = address,
                                         .offset = offsetof(pin_value, d) },
        false
    );
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = result });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    REQUIRE(function && behavior_function_compile(function));
    return true;
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

static bool authored(const char *source)
{
    const script_catalog catalog = { 0 };
    const script_lower_bindings bindings = { 0 };
    syntax = script_parse(source);
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis && analysis->valid);
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    char error[256];
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!module) {
        fprintf(stderr, "borrow authored fixture: %s\n", error);
    }
    REQUIRE(module);
    for (int i = 0; i < execution_module_function_count(module); i++) {
        behavior_function
            *body = (behavior_function *)execution_module_function_at(module, i).body;
        REQUIRE(behavior_function_compile(body));
    }
    /* Sources put the entry first, also exercising forward helper definitions. */
    function = (behavior_function *)execution_module_function_at(module, 0).body;
    borrowed_function = true;
    return true;
}

static void mutate_at_call(const behavior_function *, behavior_function_context *)
{
    entered_calls++;
    callback_item->d = 99;
}

TEST_CASE(borrow_infers_call_scoped_views_through_forwarded_helpers)
{
    REQUIRE(
        authored("Borrow :: library {Root :: (input: f64[]) -> f64 { copy := input; return Forward(copy); }Forward :: (input: f64[]) -> f64 { return Sum(input); }Sum :: (input: f64[]) -> f64 { total: f64 = 0; for value in input { total += value; } return total; } }")
    );
    REQUIRE(function->readonly_collections);
    REQUIRE(guarded() > 0);
    for (int i = 0; i < execution_module_function_count(module); i++) {
        REQUIRE(execution_module_function_at(module, i).body->readonly_collections);
    }
    pin_value items[128];
    for (int i = 0; i < 128; i++) {
        items[i] = (pin_value){ .type = PIN_F64, .d = i + 1 };
    }
    pin_array array = { .element_type = PIN_F64, .count = 128, .capacity = 128, .items = items };
    pin_value input = array_shape, output = { .type = PIN_F64 };
    input.ptr = &array;
    for (int mode = 0; mode < 4; mode++) {
        REQUIRE(begin(&input, 1, &output, NULL, mode) == FLOW_OK);
        REQUIRE(output.d == 8256);
        REQUIRE(mode < 2 ? bytes() == 0 : bytes() > 0);
    }
    release_run();
    char error[256];
    size_t length;
    REQUIRE(execution_prepared_write(module, &prepared, &length, error, sizeof error));
    const execution_prepared_linker linker = { 0 };
    decoded = execution_prepared_read(prepared, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(decoded);
    function = (behavior_function *)execution_module_function_at(decoded, 0).body;
    REQUIRE(function->readonly_collections && guarded() > 0);
    REQUIRE(begin(&input, 1, &output, NULL, 0) == FLOW_OK);
    REQUIRE(output.d == 8256 && bytes() == 0);
    return true;
}

TEST_CASE(borrow_call_callbacks_keep_their_owned_argument_snapshots)
{
    REQUIRE(
        authored("Borrow :: library {Root :: (input: f64[]) -> f64 { copy := input; return Sum(copy) + Sum(copy); }Sum :: (input: f64[]) -> f64 { total: f64 = 0; for value in input { total += value; } return total; } }")
    );
    REQUIRE(function->readonly_collections);
    for (int backend = 0; backend < 2; backend++) {
        pin_value item = { .type = PIN_F64, .d = 7 }, output = item;
        pin_array array = { .element_type = PIN_F64,
                            .count = 1,
                            .capacity = 1,
                            .items = &item };
        pin_value input = array_shape;
        input.ptr = &array;
        prepare(&input, 1, &output, NULL, backend);
        callback_item = &item;
        entered_calls = 0;
        context.call_enter = mutate_at_call;
        REQUIRE(behavior_function_run(function, &context, backend) == FLOW_OK);
        REQUIRE(output.d == 14 && item.d == 99 && entered_calls == 2);
        REQUIRE(bytes() > 0);
    }
    return true;
}

TEST_CASE(borrow_call_contract_rejects_escape_mutation_suspend_and_recursion)
{
    const char *sources[] = {
        ("Borrow :: library { Root :: (input: f64[]) -> f64[] { return "
         "Keep(input); }Keep :: (input: f64[]) -> f64[] { return input; "
         "} }"),
        ("Borrow :: library { Root :: (input: f64[]) -> f64 { return "
         "Change(input); }Change :: (input: f64[]) -> f64 { input[0] = "
         "99; return input[0]; } }"),
        ("Borrow :: library { Root :: (input: f64[]) -> f64 { return "
         "await Wait(input); }Wait :: (input: f64[]) -> f64 { yield; "
         "return input[0]; } }"),
        ("Borrow :: library { Root :: (input: f64[]) -> f64 { return "
         "Root(input); } }")
    };
    for (int i = 0; i < 4; i++) {
        cleanup();
        REQUIRE(authored(sources[i]));
        REQUIRE(!function->readonly_collections && guarded() == 0);
    }
    return true;
}

TEST_CASE(borrow_helper_pause_and_wait_retain_arguments_before_resuming)
{
    for (int kind = PAUSE; kind <= WAIT; kind++) {
        cleanup();
        REQUIRE(forwarding_fixture(kind));
        REQUIRE(function->readonly_collections == (kind == PAUSE));
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
            const behavior_function *active = function;
            behavior_function_context *child = behavior_function_active_context(&context, &active);
            REQUIRE(active == helper_function && child != &context);
            REQUIRE(
                child->arguments[0].ptr !=
                context.data->values[function->calls[0].first].ptr
            );
            const pin_array *retained = child->arguments[0].ptr;
            REQUIRE(pin_array_valid(retained));
            REQUIRE(retained->stride == sizeof(double) && !retained->items);
            REQUIRE(pin_array_get(retained, 0).d == 7);
            item.d = 99;
            pause = false;
            context.source_pause = NULL;
            REQUIRE(behavior_function_run(function, &context, backend) == FLOW_OK);
            REQUIRE(output.d == 7);
        }
    }
    return true;
}

TEST_CASE(borrow_helper_mutation_and_opaque_calls_remain_owning)
{
    const int kinds[] = { RAW_WRITE, OPAQUE };
    for (int i = 0; i < 2; i++) {
        cleanup();
        REQUIRE(forwarding_fixture(kinds[i]));
        REQUIRE(!function->readonly_collections && !helper_function->readonly_collections);
        for (int backend = 0; backend < 2; backend++) {
            pin_value item = { .type = PIN_F64, .d = 7 }, output = item;
            pin_array array = { .element_type = PIN_F64,
                                .count = 1,
                                .capacity = 1,
                                .items = &item };
            pin_value input = array_shape;
            input.ptr = &array;
            opaque_calls = 0;
            REQUIRE(begin(&input, 1, &output, NULL, backend) == FLOW_OK);
            REQUIRE(output.d == (i ? 1 : 7) && item.d == 7);
            REQUIRE(opaque_calls == i);
            REQUIRE(bytes() > 0);
        }
    }
    return true;
}

TEST_CASE(borrow_call_guards_program_state_headers_before_commit)
{
    REQUIRE(
        authored("Borrow :: behavior { scalar: f64 = 7;Root :: (input: f64[]) -> f64 { copy := input; scalar = 99; Touch(); return copy[0]; }Touch :: () -> f64 { return 0; } }")
    );
    REQUIRE(function->readonly_collections && guarded() > 0);
    uint64_t state = 0;
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, "scalar")) {
            state = analysis->symbols[i].id;
        }
    }
    REQUIRE(state);
    test_world = ecs_world_new();
    test_program = execution_program_new(module, test_world, NULL);
    REQUIRE(test_program);
    execution_instance *instance = execution_program_attach(
        test_program,
        ecs_entity_to_u64(ecs_world_create_entity(test_world))
    );
    REQUIRE(instance);
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(
            execution_instance_set_state(instance, state, (pin_value){ .type = PIN_F64, .d = 7 }) == FLOW_OK
        );
        test_activation = program_activation_new(instance, function, &array_shape, 1, 0, 0, 10000);
        REQUIRE(test_activation);
        /* Native inputs may alias stored headers as well as the active bank.
         * Inject after the public event boundary's separate ownership copy. */
        pin_array array = { .element_type = PIN_F64,
                            .count = 1,
                            .capacity = 1,
                            .items = (pin_value *)execution_instance_state(instance, state) };
        ((pin_value *)test_activation->context.arguments)[0].ptr = &array;
        REQUIRE(behavior_function_run(function, &test_activation->context, backend) == FLOW_OK);
        REQUIRE(test_activation->context.results[0].d == 7);
        REQUIRE(execution_instance_state(instance, state)->d == 99);
        program_activation_free(test_activation);
        test_activation = NULL;
    }
    return true;
}

TEST_CASE(borrow_authored_readonly_loop_keeps_scalar_writes)
{
    const script_catalog_capability event = { .id = 10,
                                              .name = "Run",
                                              .kind = SCRIPT_CAPABILITY_EVENT };
    const script_catalog catalog = { .capabilities = &event, .capability_count = 1 };
    syntax = script_parse("Borrow :: behavior { values: f64[]=[]; total: f64=0; on Run :: () { sum: f64 = 0; for value in values { sum+=value; } total+=sum; } }");
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
            const pin_string retained_text = { "retained", sizeof("retained") - 1 };
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
                item = (pin_value){ .type = PIN_STR, .text = &retained_text };
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
                REQUIRE(
                    pin_text(pin_array_get(retained, 0)).bytes != pin_text(item).bytes
                );
                REQUIRE(!strcmp(
                    pin_text(pin_array_get(retained, 0)).bytes,
                    pin_text(item).bytes
                ));
            }
        }
    }
    /* A bad negative length follows the canonical path, whose allocator asserts;
     * check only guard selection without deliberately crashing the process. */
    pin_array bad = { .element_type = PIN_F64, .count = -1 };
    pin_value input = array_shape, output;
    input.ptr = &bad;
    REQUIRE(!behavior_borrow_array(
        &context,
        (behavior_borrow_layout){ .results = 1, .data = acount(function->data_values) },
        &input,
        &output
    ));
    return true;
}

TEST_CASE(borrow_guards_scalar_banks_aliasing_array_storage)
{
    for (int kind = VARIABLE_ALIAS; kind <= VM_ALIAS; kind++) {
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
            } else if (kind == SAVED_ALIAS) {
                array.items = (pin_value *)(activation.values + acount(function->slots));
                *array.items = item;
            } else if (kind == VM_ALIAS) {
                /* Retain actual scratch from an earlier bytecode invocation. */
                REQUIRE(behavior_function_run(function, &context, false) == FLOW_OK);
                REQUIRE(activation.vm && activation.vm->values);
                arena_free(&storage);
                behavior_function_reset_data(context.data);
                array.items = activation.vm->values + alias_slot;
                *array.items = item;
            }
            REQUIRE(behavior_function_run(function, &context, mode % 2) == FLOW_OK);
            REQUIRE((kind == RESULT_ALIAS ? item.d : output.d) == 7);
            REQUIRE(kind != VARIABLE_ALIAS || item.d == 99);
            REQUIRE(kind != HOST_ALIAS || array.items[0].d == 99);
            REQUIRE(kind != SAVED_ALIAS || array.items[0].d == 99);
            REQUIRE(kind != VM_ALIAS || mode % 2 || array.items[0].d == 99);
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
                REQUIRE(pin_array_get(output.ptr, 0).d == 7);
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
    syntax = script_parse("Borrow :: library { Count :: (values:f64[])->f64 { total: f64 = 0; for value in values { total+=1; } return total; } }");
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
    REQUIRE(!strcmp(
        error,
        "prepared definition verification failed: instruction 19: runtime host "
        "flags are not "
        "canonical instructions"
    ));
    return true;
}

/* Calls pass arguments without copies only when the callee leaves the caller's
 * storage untouched: writes through argument items, state stores and real
 * suspension copy; reads, private writes, results and pauses share. */
TEST_CASE(borrow_argument_sharing_rejects_writes_through_caller_storage)
{
    static const struct
    {
        int kind;
        behavior_argument_sharing expected;
    } cases[] = {
        { COUNT, BEHAVIOR_SHARING_SAFE },
        { PAYLOAD, BEHAVIOR_SHARING_SAFE },
        { CAPACITY, BEHAVIOR_SHARING_SAFE },
        { IDENTITY, BEHAVIOR_SHARING_SAFE },
        { RAW_WRITE, BEHAVIOR_SHARING_COPY },
        { VARIABLE_ALIAS, BEHAVIOR_SHARING_COPY },
        { RESULT_ALIAS, BEHAVIOR_SHARING_SAFE },
        { HOST_ALIAS, BEHAVIOR_SHARING_SAFE },
        { SAVED_ALIAS, BEHAVIOR_SHARING_SAFE },
        { VM_ALIAS, BEHAVIOR_SHARING_SAFE },
        { PAUSE, BEHAVIOR_SHARING_SAFE },
        { WAIT, BEHAVIOR_SHARING_COPY },
        { OPAQUE, BEHAVIOR_SHARING_SAFE },
        { ESCAPE, BEHAVIOR_SHARING_SAFE },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        cleanup();
        REQUIRE(fixture(cases[i].kind));
        REQUIRE(function->argument_sharing == BEHAVIOR_SHARING_UNKNOWN);
        execution_prepare_argument_sharing(function);
        REQUIRE(function->argument_sharing == cases[i].expected);
    }
    return true;
}

/* Module functions keep source order. */
enum
{
    SELECTED,
    CONTEXT_PATH,
    LAUNDERED,
    TEXT_ADDRESS,
    SQUARE_ROOT
};
/* Definitions no lowering emits that could still reach caller storage: a
 * select between pointers, an address derived from the context or from a cell
 * holding the argument, and the caller's text stored in a cell and read back
 * as an address. A math helper stays safe. */
static bool hand_built(int kind)
{
    builder = behavior_function_builder_new();
    pin_value output = { .type = PIN_F64 };
    behavior_function_builder_signature(builder, &array_shape, 1, &output, 1, NULL, 0);
    int cell = behavior_function_builder_values(builder, &array_shape, 1);
    int argument = value(
        (behavior_function_instruction){ .op = BF_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .target = BF_ARGUMENT,
                                         .value = array_shape },
        true
    );
    int number = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64, .d = 2 } },
        false
    );
    int address = -1;
    if (kind == SELECTED) {
        int empty = value(
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = BF_POINTER_TYPE,
                                             .value = { .type = BF_POINTER_TYPE } },
            true
        );
        int choice = value(
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = PIN_BOOL,
                                             .value = { .type = PIN_BOOL } },
            false
        );
        address = value(
            (behavior_function_instruction){ .op = BF_SELECT,
                                             .type = BF_POINTER_TYPE,
                                             .left = choice,
                                             .right = empty,
                                             .target = argument },
            true
        );
    } else if (kind == CONTEXT_PATH || kind == LAUNDERED || kind == TEXT_ADDRESS) {
        int base;
        if (kind == CONTEXT_PATH) {
            base = value(
                (behavior_function_instruction){ .op = BF_CONTEXT,
                                                 .type = BF_POINTER_TYPE,
                                                 .target = 1 },
                true
            );
        } else if (kind == LAUNDERED) {
            base = host_address(cell);
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = BF_POINTER_TYPE,
                                                  .left = argument,
                                                  .right = base,
                                                  .offset = offsetof(pin_value, d) });
        } else {
            int text = value(
                (behavior_function_instruction){ .op = BF_LOAD,
                                                 .type = PIN_STR,
                                                 .left = argument,
                                                 .offset = offsetof(pin_array, items) },
                false
            );
            pin_value text_cell = { .type = PIN_STR };
            base = host_address(behavior_function_builder_values(builder, &text_cell, 1));
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = PIN_STR,
                                                  .left = text,
                                                  .right = base,
                                                  .offset = offsetof(pin_value, d) });
        }
        int zero = value(
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = BF_POINTER_TYPE,
                                             .value = { .type = BF_POINTER_TYPE } },
            false
        );
        int derived = value(
            (
                behavior_function_instruction
            ){ .op = BF_ADD, .type = BF_POINTER_TYPE, .left = base, .right = zero },
            true
        );
        address = value(
            (behavior_function_instruction){
                .op = BF_LOAD,
                .type = BF_POINTER_TYPE,
                .left = derived,
                .offset = kind == CONTEXT_PATH
                            ? offsetof(behavior_function_context, arguments)
                            : offsetof(pin_value, d) },
            true
        );
    }
    if (address >= 0) {
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = PIN_F64,
                                              .left = number,
                                              .right = address,
                                              .offset = offsetof(pin_array, count) });
    }
    int result = number;
    if (kind == SQUARE_ROOT) {
        result = value(
            (behavior_function_instruction){ .op = BF_SQRT, .type = PIN_F64, .left = number },
            false
        );
    }
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = result });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "sharing fixture %d: %s\n", kind, error);
    }
    return function != NULL;
}
TEST_CASE(borrow_argument_sharing_tracks_derived_and_selected_addresses)
{
    const struct
    {
        int kind;
        behavior_argument_sharing expected;
    } cases[] = { { SELECTED, BEHAVIOR_SHARING_COPY },
                  { CONTEXT_PATH, BEHAVIOR_SHARING_COPY },
                  { LAUNDERED, BEHAVIOR_SHARING_COPY },
                  { TEXT_ADDRESS, BEHAVIOR_SHARING_COPY },
                  { SQUARE_ROOT, BEHAVIOR_SHARING_SAFE } };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        cleanup();
        REQUIRE(hand_built(cases[i].kind));
        execution_prepare_argument_sharing(function);
        REQUIRE(function->argument_sharing == cases[i].expected);
    }
    return true;
}

/* How a definition reaches the header it rewrites: the cell's own header
 * address, one selected from two, the previous cell's moved one header along,
 * or one stored in a cell and loaded back. */
enum
{
    HEADER_DIRECT,
    HEADER_SELECTED,
    HEADER_NEIGHBOUR,
    HEADER_RELOADED
};
/* The address `route` writes `cell`'s header field at `*offset` through. */
static int header_route(int cell, int route, size_t *offset)
{
    int header = host_address(cell);
    switch (route) {
        case HEADER_SELECTED: {
            int choice = value(
                (behavior_function_instruction){ .op = BF_CONSTANT,
                                                 .type = PIN_BOOL,
                                                 .value = { .type = PIN_BOOL } },
                false
            );
            return value(
                (behavior_function_instruction){ .op = BF_SELECT,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = choice,
                                                 .right = header,
                                                 .target = header },
                true
            );
        }
        case HEADER_NEIGHBOUR: {
            int zero = value(
                (behavior_function_instruction){ .op = BF_CONSTANT,
                                                 .type = BF_POINTER_TYPE,
                                                 .value = { .type = BF_POINTER_TYPE } },
                false
            );
            *offset += sizeof(pin_value);
            return value(
                (behavior_function_instruction){ .op = BF_ADD,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = host_address(cell - 1),
                                                 .right = zero },
                true
            );
        }
        case HEADER_RELOADED: {
            pin_value pointer = { .type = BF_POINTER_TYPE };
            int held = behavior_function_builder_values(builder, &pointer, 1);
            assert(
                behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, held, BEHAVIOR_ADDRESS)
            );
            int address = host_address(held);
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = BF_POINTER_TYPE,
                                                  .left = header,
                                                  .right = address,
                                                  .offset = offsetof(pin_value, d) });
            return value(
                (behavior_function_instruction){ .op = BF_LOAD,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = address,
                                                 .offset = offsetof(pin_value, d) },
                true
            );
        }
        default: return header;
    }
}
static char retyped_error[256];
/* The argument stored in a snapshot's input, which follows a scalar cell. The
 * input's header field at `offset` then takes `header` through `route`, and
 * the definition writes through the snapshot's output. */
static bool retyped_snapshot(size_t offset, int header, int route)
{
    builder = behavior_function_builder_new();
    pin_value output = { .type = PIN_F64 };
    behavior_function_builder_signature(builder, &array_shape, 1, &output, 1, NULL, 0);
    int argument = value(
        (behavior_function_instruction){ .op = BF_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .target = BF_ARGUMENT,
                                         .value = array_shape },
        true
    );
    pin_value scalar = { .type = PIN_F64 };
    int neighbour = behavior_function_builder_values(builder, &scalar, 1);
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_snapshot_call,
        &EXECUTION_value_access,
        &array_shape,
        1,
        &array_shape,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_snapshot_call);
    assert(behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id)));
    int first = behavior_function_builder_host_first(builder, host);
    assert(first == neighbour + 1);
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = BF_POINTER_TYPE,
                                          .left = argument,
                                          .right = host_address(first),
                                          .offset = offsetof(pin_value, d) });
    int retype = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .value = { .type = PIN_I32, .i = header } },
        false
    );
    int address = header_route(first, route, &offset);
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_I32,
                                          .left = retype,
                                          .right = address,
                                          .offset = offset });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    int copy = value(
        (behavior_function_instruction){
            .op = BF_LOAD,
            .type = BF_POINTER_TYPE,
            .left = host_address(behavior_function_builder_host_results(builder, host)),
            .offset = offsetof(pin_value, d) },
        true
    );
    int number = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64, .d = 2 } },
        false
    );
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_F64,
                                          .left = number,
                                          .right = copy,
                                          .offset = offsetof(pin_array, count) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = number });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    function = behavior_function_builder_finish(builder, retyped_error, sizeof retyped_error);
    return function != NULL;
}
/* A snapshot copies by its input's runtime header. Rewriting the input's
 * container or type would make it copy the caller's array pointer as a
 * scalar, so a definition may write only a cell's payload and names. */
TEST_CASE(borrow_verifier_rejects_cell_header_rewrites)
{
    const struct
    {
        size_t offset;
        int header;
    } cases[] = { { offsetof(pin_value, container), PIN_CONTAINER_SINGLE },
                  { offsetof(pin_value, type), BF_POINTER_TYPE } };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        cleanup();
        REQUIRE(!retyped_snapshot(cases[i].offset, cases[i].header, HEADER_DIRECT));
        REQUIRE(strstr(retyped_error, "data cell header field is not writable"));
    }
    return true;
}
/* A header address the verifier cannot attribute to one cell may still
 * rewrite a header, after which a snapshot's output is not fresh. */
TEST_CASE(borrow_argument_sharing_copies_after_unplaced_header_writes)
{
    for (int route = HEADER_SELECTED; route <= HEADER_RELOADED; route++) {
        cleanup();
        REQUIRE(retyped_snapshot(offsetof(pin_value, container), PIN_CONTAINER_SINGLE, route));
        execution_prepare_argument_sharing(function);
        REQUIRE(function->argument_sharing == BEHAVIOR_SHARING_COPY);
    }
    return true;
}
/* An authored return copies by the result's header. A callee that changes
 * its result's container fails the return instead of handing back bits. */
TEST_CASE(borrow_call_rejects_result_header_rewrites)
{
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, &array_shape, 1, NULL, 0);
    int results = value(
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = BF_POINTER_TYPE,
            .offset = offsetof(behavior_function_context, results) },
        true
    );
    int single = value(
        (behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = PIN_I32,
            .value = { .type = PIN_I32, .i = PIN_CONTAINER_SINGLE } },
        false
    );
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_I32,
                                          .left = single,
                                          .right = results,
                                          .offset = offsetof(pin_value, container) });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    helper_function = behavior_function_builder_finish(builder, error, sizeof error);
    REQUIRE(helper_function && behavior_function_compile(helper_function));
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    pin_value scalar = { .type = PIN_F64 };
    behavior_function_builder_signature(builder, NULL, 0, &scalar, 1, NULL, 0);
    int call = behavior_function_builder_call(builder, helper_function, NULL, 0, &array_shape, 1);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    int number = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64, .d = 1 } },
        false
    );
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = number });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    function = behavior_function_builder_finish(builder, error, sizeof error);
    REQUIRE(function && behavior_function_compile(function));
    for (int mode = 0; mode < 2; mode++) {
        pin_value output = { .type = PIN_F64 };
        REQUIRE(begin(NULL, 0, &output, NULL, mode) != FLOW_OK);
        REQUIRE(
            context.error_message &&
            !strcmp(context.error_message, "function result differs from its declared type")
        );
    }
    return true;
}

/* A value reference to `root`'s cell, made by a host whose cells the caller
 * may reach before the call: returns the host. */
static int reference_host()
{
    pin_value headers[] = { { .type = BF_POINTER_TYPE },
                            { .type = PIN_I32 },
                            { .type = PIN_I32, .i = LANGUAGE_REFERENCE_VALUE },
                            { .type = PIN_I32, .i = -1 },
                            { .type = BF_POINTER_TYPE },
                            { .type = PIN_HANDLE },
                            { .type = PIN_U64 },
                            { .type = PIN_U64 },
                            { .type = PIN_U64 } };
    pin_value result = { .type = BF_POINTER_TYPE };
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_reference_make_call,
        &EXECUTION_value_access,
        headers,
        9,
        &result,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_reference_make_call);
    assert(behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id)));
    int first = behavior_function_builder_host_first(builder, host);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first, BEHAVIOR_ADDRESS));
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first + 4, BEHAVIOR_ADDRESS));
    int made = behavior_function_builder_host_results(builder, host);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, made, BEHAVIOR_REFERENCE));
    return host;
}
static void put_address(int cell, int address)
{
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = BF_POINTER_TYPE,
                                          .left = address,
                                          .right = host_address(cell),
                                          .offset = offsetof(pin_value, d) });
}
/* Makes the reference `host` describes to `root` and writes `number` through it. */
static void write_reference(int host, int root, int number)
{
    int first = behavior_function_builder_host_first(builder, host);
    put_address(
        first,
        value((behavior_function_instruction){ .op = BF_CONTEXT, .type = BF_POINTER_TYPE, .target = 1 }, true)
    );
    put_address(first + 4, host_address(root));
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    int made = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, made, BEHAVIOR_REFERENCE));
    emit((behavior_function_instruction){
        .op = BF_LOAD,
        .type = BF_POINTER_TYPE,
        .output = made,
        .left = host_address(behavior_function_builder_host_results(builder, host)),
        .offset = offsetof(pin_value, d) });
    pin_value headers[] = { { .type = BF_POINTER_TYPE }, { .type = PIN_I32 } },
              done = { .type = PIN_BOOL };
    int write = behavior_function_builder_host(
        builder,
        &EXECUTION_reference_write_call,
        &EXECUTION_assignment_access,
        headers,
        2,
        &done,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_reference_write_call);
    assert(behavior_function_builder_host_identity(builder, write, id, execution_intrinsic_abi(id)));
    int target = behavior_function_builder_host_first(builder, write);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, target, BEHAVIOR_REFERENCE));
    put_address(target, made);
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_I32,
                                          .left = number,
                                          .right = host_address(target + 1),
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = write,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
}
/* A value reference to an own cell writes only that cell; one aimed at a
 * second reference's kind can turn that one into a state reference, after
 * which its write must copy. */
TEST_CASE(borrow_argument_sharing_tracks_reference_kinds)
{
    for (int retargeted = 0; retargeted < 2; retargeted++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value output = { .type = PIN_I32 }, own = { .type = PIN_I32 };
        behavior_function_builder_signature(builder, &array_shape, 1, &output, 1, NULL, 0);
        int cell = behavior_function_builder_values(builder, &own, 1);
        int second = reference_host();
        int kind = value(
            (behavior_function_instruction){
                .op = BF_CONSTANT,
                .type = PIN_I32,
                .value = { .type = PIN_I32, .i = LANGUAGE_REFERENCE_STATE } },
            false
        );
        if (retargeted) {
            write_reference(
                reference_host(),
                behavior_function_builder_host_first(builder, second) + 2,
                kind
            );
        }
        write_reference(second, cell, kind);
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = PIN_I32,
                                              .left = kind });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        if (!function) {
            fprintf(stderr, "reference fixture: %s\n", error);
        }
        REQUIRE(function);
        execution_prepare_argument_sharing(function);
        REQUIRE(
            function->argument_sharing ==
            (retargeted ? BEHAVIOR_SHARING_COPY : BEHAVIOR_SHARING_SAFE)
        );
    }
    return true;
}

static behavior_function *authored_function(int index)
{
    const execution_module *owner = decoded ? decoded : module;
    assert(index < execution_module_function_count(owner));
    return (behavior_function *)execution_module_function_at(owner, index).body;
}
static pin_value strings(pin_array *array, pin_value *items, const char *const *texts, int count)
{
    pin_string *descriptors = arena_alloc(&string_inputs, (size_t)count * sizeof *descriptors);
    for (int i = 0; i < count; i++) {
        descriptors[i] = (pin_string){ texts[i], (uint32_t)strlen(texts[i]) };
        items[i] = (pin_value){ .type = PIN_STR, .text = &descriptors[i] };
    }
    *array = (pin_array){ .element_type = PIN_STR,
                          .count = count,
                          .capacity = count,
                          .items = items };
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}

/* Two union constructors: ordinary payloads become private copies, while a
 * copied pointer array still reaches the original managed cell. */
TEST_CASE(borrow_union_constructor_tracks_payload_aliases)
{
    for (int pointers = 0; pointers < 2; pointers++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value shape = { .type = pointers ? PIN_POINTER : PIN_F64,
                            .container = PIN_CONTAINER_ARRAY };
        pin_value result = { .type = PIN_UNION };
        behavior_function_builder_signature(builder, &shape, 1, &result, 1, NULL, 0);
        pin_value headers[] = { { .type = PIN_U64, .u64 = 1 }, shape };
        int host = behavior_function_builder_host(
            builder,
            &EXECUTION_union_make_call,
            &EXECUTION_value_access,
            headers,
            2,
            &result,
            1
        );
        uint64_t id = execution_intrinsic_id(&EXECUTION_union_make_call);
        REQUIRE(
            behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
        );
        int input = value(
            (behavior_function_instruction){ .op = BF_ARRAY,
                                             .type = BF_POINTER_TYPE,
                                             .left = 0,
                                             .target = BF_ARGUMENT,
                                             .value = shape },
            true
        );
        int payload = host_address(behavior_function_builder_host_first(builder, host) + 1);
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = BF_POINTER_TYPE,
                                              .left = input,
                                              .right = payload,
                                              .offset = offsetof(pin_value, d) });
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        int output = value(
            (behavior_function_instruction){
                .op = BF_LOAD,
                .type = PIN_UNION,
                .left = host_address(behavior_function_builder_host_results(builder, host)),
                .offset = offsetof(pin_value, d) },
            false
        );
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = PIN_UNION,
                                              .left = output });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(function);
        execution_prepare_argument_sharing(function);
        REQUIRE(function->argument_sharing == BEHAVIOR_SHARING_SAFE);
        REQUIRE(execution_builds_safely(function) == !pointers);

        pin_pointer cell = { .value = { .type = PIN_F64, .d = 1 } };
        pin_value item = pointers ? (pin_value){ .type = PIN_POINTER, .ptr = &cell }
                                  : (pin_value){ .type = PIN_F64, .d = 1 };
        pin_array array = { .element_type = shape.type,
                            .items = &item,
                            .count = 1,
                            .capacity = 1 };
        headers[1].ptr = &array;
        REQUIRE(!EXECUTION_union_make_call.value(&storage, headers, 2, &result));
        const pin_union *sum = result.ptr;
        REQUIRE(sum && sum->count == 1 && sum->values[0].ptr != &array);
        pin_array *copy = sum->values[0].ptr;
        if (pointers) {
            pin_value copied = pin_array_get(copy, 0);
            REQUIRE(copied.ptr == &cell);
            cell.value.d = 7;
            REQUIRE(((pin_pointer *)copied.ptr)->value.d == 7);
        } else {
            pin_value changed = { .type = PIN_F64, .d = 7 };
            REQUIRE(!pin_array_set(copy, 0, &changed));
            REQUIRE(pin_array_get(copy, 0).d == 7 && item.d == 1);
        }
    }
    return true;
}

/* A helper that only reads a table, or copies it before changing it, receives
 * the caller's array without a copy; value semantics still hold. */
TEST_CASE(borrow_argument_sharing_passes_string_tables_without_copies)
{
    REQUIRE(authored(
        "Share :: library {"
        "Root :: (input: string[]) -> int { total: int = 0; i: int = 0;"
        "while i < 64 { total += Match(input, i); i += 1; }"
        "copy := input; changed := Change(copy);"
        "if copy[0] == \"a\" { total += 1000; } return total + changed * 100; }"
        "Match :: (input: string[], at: int) -> int {"
        "if input[at % 4] == \"a\" { return 1; } return 0; }"
        "Change :: (values: string[]) -> int { local := values; local[0] "
        "= \"z\";"
        "if local[0] == \"z\" { return 1; } return 0; } }"
    ));
    for (int i = 0; i < 3; i++) {
        REQUIRE(authored_function(i)->argument_sharing == BEHAVIOR_SHARING_SAFE);
    }
    const char *texts[] = { "a", "b", "c", "d" };
    pin_value items[4];
    pin_array array;
    pin_value input = strings(&array, items, texts, 4);
    uint64_t copied[2];
    for (int run = 0; run < 2; run++) {
        /* The second run forces the copying path the proof avoids. */
        if (run) {
            authored_function(1)->argument_sharing = BEHAVIOR_SHARING_COPY;
        }
        pin_value output = { .type = PIN_I32 };
        pin_copy_scope copies = { 0 };
        pin_copy_scope_begin(&copies);
        int status = begin(&input, 1, &output, NULL, 0);
        pin_copy_scope_end(&copies);
        REQUIRE(
            status == FLOW_OK && output.i == 1116 &&
            !strcmp(pin_text(items[0]).bytes, "a")
        );
        copied[run] = copies.counts.text_bytes;
    }
    REQUIRE(copied[0] * 8 < copied[1]);
    release_run();
    char error[256];
    size_t length;
    REQUIRE(execution_prepared_write(module, &prepared, &length, error, sizeof error));
    const execution_prepared_linker linker = { 0 };
    decoded = execution_prepared_read(prepared, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(decoded);
    /* Loading recomputes the verdict instead of reading it. */
    for (int i = 0; i < 3; i++) {
        REQUIRE(authored_function(i)->argument_sharing == BEHAVIOR_SHARING_SAFE);
    }
    return true;
}

/* Changing fields of a record the helper owns, even to text from its
 * arguments, writes only the helper's storage, so the table still shares. */
TEST_CASE(borrow_argument_sharing_allows_own_record_field_writes)
{
    REQUIRE(authored(
        "Share :: library { Row :: struct { name: string; count: int; }"
        "Root :: (input: string[]) -> int { total: int = 0; i: int = 0;"
        "while i < 8 { total += Label(input, i); i += 1; } return total; }"
        "Label :: (input: string[], at: int) -> int {"
        "row: Row = {name: \"\", count: 0}; row.count = at; row.name = "
        "input[at % 2];"
        "if row.name == \"a\" { return row.count; } return 0; } }"
    ));
    for (int i = 0; i < 2; i++) {
        REQUIRE(authored_function(i)->argument_sharing == BEHAVIOR_SHARING_SAFE);
    }
    const char *texts[] = { "a", "b" };
    pin_value items[2], output = { .type = PIN_I32 };
    pin_array array;
    pin_value input = strings(&array, items, texts, 2);
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(begin(&input, 1, &output, NULL, backend) == FLOW_OK);
        REQUIRE(
            output.i == 12 && !strcmp(pin_text(items[0]).bytes, "a") &&
            !strcmp(pin_text(items[1]).bytes, "b")
        );
    }
    return true;
}

/* A callee that writes through a reference argument, and every caller of it,
 * keeps copying: its value argument stays the caller's original. */
TEST_CASE(borrow_argument_sharing_copies_around_reference_writes)
{
    REQUIRE(authored(
        "Share :: library {"
        "Root :: (input: string[]) -> int { local := input; return "
        "Write(local, ref local); }"
        "Write :: (values: string[], target: ref string[]) -> int { target[0] "
        "= \"z\";"
        "if values[0] == \"a\" { return 1; } return 0; } }"
    ));
    REQUIRE(authored_function(1)->argument_sharing == BEHAVIOR_SHARING_COPY);
    REQUIRE(authored_function(0)->argument_sharing == BEHAVIOR_SHARING_COPY);
    const char *texts[] = { "a" };
    pin_value items[1], output = { .type = PIN_I32 };
    pin_array array;
    pin_value input = strings(&array, items, texts, 1);
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(begin(&input, 1, &output, NULL, backend) == FLOW_OK);
        REQUIRE(output.i == 1 && !strcmp(pin_text(items[0]).bytes, "a"));
    }
    return true;
}

/* Pushes 2.0 onto the local array `local`, in the caller's storage when
 * `built`. */
static void push_item(int local, bool built)
{
    int number = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64, .d = 2 } },
        false
    );
    pin_value headers[] = { { .type = BF_POINTER_TYPE }, { .type = PIN_F64 } },
              done = { .type = PIN_BOOL };
    int push = behavior_function_builder_host(
        builder,
        &EXECUTION_array_push_call,
        &EXECUTION_assignment_access,
        headers,
        2,
        &done,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_array_push_call) |
                  (built ? EXECUTION_INTRINSIC_IN_RESULT : 0);
    assert(behavior_function_builder_host_identity(builder, push, id, execution_intrinsic_abi(id)));
    int first = behavior_function_builder_host_first(builder, push);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, first, BEHAVIOR_ADDRESS));
    put_address(first, host_address(local));
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_F64,
                                          .left = number,
                                          .right = host_address(first + 1),
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = push,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
}
/* A function builds a value in its caller's storage, and may move it there
 * at return, only when nothing but the updates that run there changes it: a
 * plain push onto it or a raw write through it would leave it reaching the
 * callee's own storage. */
TEST_CASE(borrow_built_results_move_only_when_unchanged_elsewhere)
{
    enum
    {
        BUILT_PUSH,
        PLAIN_PUSH,
        RAW_WRITE,
        GROWN_THEN_PLAIN,
        VALUE_ARGUMENT,
        RAW_ARGUMENT
    };
    for (int kind = BUILT_PUSH; kind <= RAW_ARGUMENT; kind++) {
        cleanup();
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, NULL, 0, NULL, 0, NULL, 0);
        int local = behavior_function_builder_values(builder, &array_shape, 1);
        if (kind == GROWN_THEN_PLAIN) {
            /* A built push puts the empty local's array in the caller's
             * storage; a plain push would then grow it here. */
            push_item(local, true);
            push_item(local, false);
            emit((behavior_function_instruction){ .op = BF_END });
            char grown[256];
            function = behavior_function_builder_finish(builder, grown, sizeof grown);
            REQUIRE(function && !function->builds_results);
            continue;
        }
        if (kind == VALUE_ARGUMENT || kind == RAW_ARGUMENT) {
            /* A helper takes the built array as a copied value, or as a raw
             * address it could write through. */
            behavior_function_builder *outer = builder;
            builder = behavior_function_builder_new();
            pin_value raw = { .type = BF_POINTER_TYPE };
            behavior_function_builder_signature(
                builder,
                kind == RAW_ARGUMENT ? &raw : &array_shape,
                1,
                NULL,
                0,
                NULL,
                0
            );
            if (kind == RAW_ARGUMENT) {
                assert(
                    behavior_function_builder_kind(builder, BEHAVIOR_BANK_ARGUMENT, 0, BEHAVIOR_ADDRESS)
                );
            }
            emit((behavior_function_instruction){ .op = BF_END });
            char helper[256];
            behavior_function_free(helper_function);
            helper_function = behavior_function_builder_finish(builder, helper, sizeof helper);
            behavior_function_builder_free(builder);
            builder = outer;
            REQUIRE(helper_function);
        }
        int make = behavior_function_builder_host(
            builder,
            &EXECUTION_construct_call,
            &EXECUTION_assignment_access,
            &array_shape,
            1,
            &array_shape,
            1
        );
        uint64_t built = execution_intrinsic_id(&EXECUTION_construct_call) |
                         EXECUTION_INTRINSIC_IN_RESULT;
        assert(
            behavior_function_builder_host_identity(builder, make, built, execution_intrinsic_abi(built))
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = make,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        int made = value(
            (behavior_function_instruction){
                .op = BF_LOAD,
                .type = BF_POINTER_TYPE,
                .left = host_address(behavior_function_builder_host_results(builder, make)),
                .offset = offsetof(pin_value, d) },
            true
        );
        put_address(local, made);
        int number = value(
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = PIN_F64,
                                             .value = { .type = PIN_F64, .d = 2 } },
            false
        );
        if (kind == VALUE_ARGUMENT || kind == RAW_ARGUMENT) {
            pin_value raw = { .type = BF_POINTER_TYPE };
            int call = behavior_function_builder_call(
                builder,
                helper_function,
                kind == RAW_ARGUMENT ? &raw : &array_shape,
                1,
                NULL,
                0
            );
            int argument = behavior_function_builder_call_first(builder, call);
            if (kind == RAW_ARGUMENT) {
                assert(
                    behavior_function_builder_kind(builder, BEHAVIOR_BANK_DATA, argument, BEHAVIOR_ADDRESS)
                );
            }
            int array = value(
                (behavior_function_instruction){ .op = BF_LOAD,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = host_address(local),
                                                 .offset = offsetof(pin_value, d) },
                true
            );
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = BF_POINTER_TYPE,
                                                  .left = array,
                                                  .right = host_address(argument),
                                                  .offset = offsetof(pin_value, d) });
            emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
        } else if (kind == RAW_WRITE) {
            int array = value(
                (behavior_function_instruction){ .op = BF_LOAD,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = host_address(local),
                                                 .offset = offsetof(pin_value, d) },
                true
            );
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = PIN_F64,
                                                  .left = number,
                                                  .right = first_item(array) });
        } else {
            push_item(local, kind == BUILT_PUSH);
        }
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        if (!function) {
            fprintf(stderr, "built results %d: %s\n", kind, error);
        }
        REQUIRE(
            function && function->builds_results ==
                            (kind == BUILT_PUSH || kind == VALUE_ARGUMENT)
        );
        if (kind == RAW_ARGUMENT) {
            /* Once argument sharing is known, a raw address may reach a callee
             * that changes no argument, and not one that may. */
            for (int copying = 1; copying >= 0; copying--) {
                function->argument_sharing = BEHAVIOR_SHARING_UNKNOWN;
                helper_function->argument_sharing = copying ? BEHAVIOR_SHARING_COPY
                                                            : BEHAVIOR_SHARING_SAFE;
                behavior_function_prepare_borrows(function);
                REQUIRE(function->builds_results == !copying);
            }
        }
    }
    return true;
}
/* A function that returns an argument the caller shared rather than copied
 * builds no results in the caller's storage, so its result is a copy: the
 * caller's later push leaves the result as it was. */
TEST_CASE(borrow_shared_argument_returned_is_copied)
{
    cleanup();
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &array_shape, 1, &array_shape, 1, NULL, 0);
    int unused = behavior_function_builder_host(
        builder,
        &EXECUTION_construct_call,
        &EXECUTION_assignment_access,
        &array_shape,
        1,
        &array_shape,
        1
    );
    uint64_t built = execution_intrinsic_id(&EXECUTION_construct_call) |
                     EXECUTION_INTRINSIC_IN_RESULT;
    assert(
        behavior_function_builder_host_identity(builder, unused, built, execution_intrinsic_abi(built))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = unused,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    int original = value(
        (behavior_function_instruction){ .op = BF_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .target = BF_ARGUMENT,
                                         .value = array_shape },
        true
    );
    int name = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = BF_POINTER_TYPE,
                                         .value = { .type = BF_POINTER_TYPE } },
        true
    );
    emit((behavior_function_instruction){ .op = BF_RESULT_ARRAY,
                                          .type = PIN_F64,
                                          .left = original,
                                          .right = name,
                                          .value = array_shape });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    helper_function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!helper_function) {
        fprintf(stderr, "shared argument helper: %s\n", error);
    }
    REQUIRE(helper_function);
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    pin_value count = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, NULL, 0, &count, 1, NULL, 0);
    int local = behavior_function_builder_values(builder, &array_shape, 1);
    int make = behavior_function_builder_host(
        builder,
        &EXECUTION_construct_call,
        &EXECUTION_value_access,
        &array_shape,
        1,
        &array_shape,
        1
    );
    uint64_t plain = execution_intrinsic_id(&EXECUTION_construct_call);
    assert(
        behavior_function_builder_host_identity(builder, make, plain, execution_intrinsic_abi(plain))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = make,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    put_address(
        local,
        value(
            (behavior_function_instruction){
                .op = BF_LOAD,
                .type = BF_POINTER_TYPE,
                .left = host_address(behavior_function_builder_host_results(builder, make)),
                .offset = offsetof(pin_value, d) },
            true
        )
    );
    push_item(local, false);
    int call = behavior_function_builder_call(builder, helper_function, &array_shape, 1, &array_shape, 1);
    put_address(
        behavior_function_builder_call_first(builder, call),
        value(
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = BF_POINTER_TYPE,
                                             .left = host_address(local),
                                             .offset = offsetof(pin_value, d) },
            true
        )
    );
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    push_item(local, false);
    int returned = value(
        (behavior_function_instruction){
            .op = BF_LOAD,
            .type = BF_POINTER_TYPE,
            .left = host_address(behavior_function_builder_call_results(builder, call)),
            .offset = offsetof(pin_value, d) },
        true
    );
    int length = value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = PIN_I32,
                                         .left = returned,
                                         .offset = offsetof(pin_array, count) },
        false
    );
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = length });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "shared argument caller: %s\n", error);
    }
    REQUIRE(function);
    behavior_function_prepare_borrows(function);
    behavior_function_prepare_borrows(helper_function);
    REQUIRE(
        !helper_function->builds_results && helper_function->argument_sharing == BEHAVIOR_SHARING_SAFE
    );
    pin_value output = { .type = PIN_I32 };
    REQUIRE(begin(NULL, 0, &output, NULL, 0) == FLOW_OK);
    REQUIRE(output.i == 1);
    return true;
}
/* A result read through an argument may be part of the caller's data, as an
 * inner array of a shared nested array is, so moving it would let the caller's
 * result share it: such a function builds no results. */
TEST_CASE(borrow_result_reached_through_argument_builds_nothing)
{
    enum
    {
        BUILT,
        ARGUMENT,
        THROUGH_ARGUMENT,
        WRITTEN_ARGUMENT,
        KINDS
    };
    for (int kind = 0; kind < KINDS; kind++) {
        cleanup();
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, &array_shape, 1, &array_shape, 1, NULL, 0);
        int make = behavior_function_builder_host(
            builder,
            &EXECUTION_construct_call,
            &EXECUTION_assignment_access,
            &array_shape,
            1,
            &array_shape,
            1
        );
        uint64_t built = execution_intrinsic_id(&EXECUTION_construct_call) |
                         EXECUTION_INTRINSIC_IN_RESULT;
        assert(
            behavior_function_builder_host_identity(builder, make, built, execution_intrinsic_abi(built))
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = make,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        int returned = value(
            (behavior_function_instruction){
                .op = BF_LOAD,
                .type = BF_POINTER_TYPE,
                .left = host_address(behavior_function_builder_host_results(builder, make)),
                .offset = offsetof(pin_value, d) },
            true
        );
        if (kind != BUILT) {
            returned = value(
                (behavior_function_instruction){ .op = BF_ARRAY,
                                                 .type = BF_POINTER_TYPE,
                                                 .target = BF_ARGUMENT,
                                                 .value = array_shape },
                true
            );
        }
        if (kind == THROUGH_ARGUMENT) {
            returned = value(
                (behavior_function_instruction){ .op = BF_LOAD,
                                                 .type = BF_POINTER_TYPE,
                                                 .left = returned,
                                                 .offset = offsetof(pin_array, data) },
                true
            );
        }
        if (kind == WRITTEN_ARGUMENT) {
            /* Written into the result buffer, as stage one publishes. */
            int results = value(
                (behavior_function_instruction){
                    .op = BF_CONTEXT,
                    .type = BF_POINTER_TYPE,
                    .offset = offsetof(behavior_function_context, results) },
                true
            );
            emit((behavior_function_instruction){ .op = BF_WRITE,
                                                  .type = BF_POINTER_TYPE,
                                                  .left = returned,
                                                  .right = results,
                                                  .offset = offsetof(pin_value, d) });
        } else {
            int name = value(
                (behavior_function_instruction){ .op = BF_CONSTANT,
                                                 .type = BF_POINTER_TYPE,
                                                 .value = { .type = BF_POINTER_TYPE } },
                true
            );
            emit((behavior_function_instruction){ .op = BF_RESULT_ARRAY,
                                                  .type = PIN_F64,
                                                  .left = returned,
                                                  .right = name,
                                                  .value = array_shape });
        }
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        if (!function) {
            fprintf(stderr, "reached result %d: %s\n", kind, error);
        }
        REQUIRE(function);
        REQUIRE(function->builds_results == (kind == BUILT));
    }
    return true;
}
/* union_keep shares a value it keeps with the local that holds it, so a
 * result-storage update after it would change the union too: such a function
 * builds no results. Updates before it, or after one whose mask keeps
 * nothing and no instruction writes, leave the union as it was built. */
TEST_CASE(borrow_update_after_union_keep_builds_nothing)
{
    enum
    {
        UPDATE_BEFORE,
        UPDATE_AFTER,
        KEEPS_NOTHING,
        MASK_WRITTEN,
        KINDS
    };
    for (int kind = 0; kind < KINDS; kind++) {
        cleanup();
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, NULL, 0, NULL, 0, NULL, 0);
        int local = behavior_function_builder_values(builder, &array_shape, 1);
        int make = behavior_function_builder_host(
            builder,
            &EXECUTION_construct_call,
            &EXECUTION_assignment_access,
            &array_shape,
            1,
            &array_shape,
            1
        );
        uint64_t built = execution_intrinsic_id(&EXECUTION_construct_call) |
                         EXECUTION_INTRINSIC_IN_RESULT;
        assert(
            behavior_function_builder_host_identity(builder, make, built, execution_intrinsic_abi(built))
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = make,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        put_address(
            local,
            value(
                (behavior_function_instruction){
                    .op = BF_LOAD,
                    .type = BF_POINTER_TYPE,
                    .left = host_address(behavior_function_builder_host_results(builder, make)),
                    .offset = offsetof(pin_value, d) },
                true
            )
        );
        if (kind == UPDATE_BEFORE) {
            push_item(local, true);
        }
        pin_value inputs[] = { { .type = PIN_U64, .u64 = 7 },
                               { .type = PIN_U64,
                                 .u64 = kind == KEEPS_NOTHING || kind == MASK_WRITTEN
                                          ? 0
                                          : 1 },
                               array_shape },
                  made = { .type = PIN_UNION };
        int keep = behavior_function_builder_host(
            builder,
            &EXECUTION_union_keep_call,
            &EXECUTION_assignment_access,
            inputs,
            3,
            &made,
            1
        );
        uint64_t id = execution_intrinsic_id(&EXECUTION_union_keep_call) |
                      EXECUTION_INTRINSIC_IN_RESULT;
        assert(
            behavior_function_builder_host_identity(builder, keep, id, execution_intrinsic_abi(id))
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = keep,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        if (kind == MASK_WRITTEN) {
            /* A later write, here after the keep, makes its mask unknown. */
            int all = value(
                (behavior_function_instruction){ .op = BF_CONSTANT,
                                                 .type = PIN_U64,
                                                 .value = { .type = PIN_U64, .u64 = 1 } },
                false
            );
            emit((behavior_function_instruction){
                .op = BF_WRITE,
                .type = PIN_U64,
                .left = all,
                .right = host_address(behavior_function_builder_host_first(builder, keep) + 1),
                .offset = offsetof(pin_value, d) });
        }
        if (kind != UPDATE_BEFORE) {
            push_item(local, true);
        }
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        if (!function) {
            fprintf(stderr, "update after keep %d: %s\n", kind, error);
        }
        REQUIRE(function);
        REQUIRE(function->builds_results == (kind == UPDATE_BEFORE || kind == KEEPS_NOTHING));
    }
    return true;
}
/* A function with two results builds neither in its caller's storage, since
 * both could reach one value: returning one built value twice gives the
 * caller two independent copies. */
TEST_CASE(borrow_aliased_results_stay_independent)
{
    cleanup();
    builder = behavior_function_builder_new();
    pin_value pair[] = { array_shape, array_shape };
    behavior_function_builder_signature(builder, NULL, 0, pair, 2, NULL, 0);
    int make = behavior_function_builder_host(
        builder,
        &EXECUTION_construct_call,
        &EXECUTION_assignment_access,
        &array_shape,
        1,
        &array_shape,
        1
    );
    uint64_t built = execution_intrinsic_id(&EXECUTION_construct_call) |
                     EXECUTION_INTRINSIC_IN_RESULT;
    assert(
        behavior_function_builder_host_identity(builder, make, built, execution_intrinsic_abi(built))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = make,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    int made = value(
        (behavior_function_instruction){
            .op = BF_LOAD,
            .type = BF_POINTER_TYPE,
            .left = host_address(behavior_function_builder_host_results(builder, make)),
            .offset = offsetof(pin_value, d) },
        true
    );
    int name = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = BF_POINTER_TYPE,
                                         .value = { .type = BF_POINTER_TYPE } },
        true
    );
    for (int result = 0; result < 2; result++) {
        emit((behavior_function_instruction){ .op = BF_RESULT_ARRAY,
                                              .type = PIN_F64,
                                              .left = made,
                                              .right = name,
                                              .target = result,
                                              .value = array_shape });
    }
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    helper_function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!helper_function) {
        fprintf(stderr, "aliased results helper: %s\n", error);
    }
    REQUIRE(helper_function);
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    pin_value same = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, NULL, 0, &same, 1, NULL, 0);
    int call = behavior_function_builder_call(builder, helper_function, NULL, 0, pair, 2);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    int results = behavior_function_builder_call_results(builder, call), arrays[2];
    for (int result = 0; result < 2; result++) {
        arrays[result] = value(
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = BF_POINTER_TYPE,
                                             .left = host_address(results + result),
                                             .offset = offsetof(pin_value, d) },
            true
        );
    }
    int equal = value(
        (behavior_function_instruction){ .op = BF_EQ,
                                         .type = BF_POINTER_TYPE,
                                         .left = arrays[0],
                                         .right = arrays[1] },
        false
    );
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = equal });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "aliased results caller: %s\n", error);
    }
    REQUIRE(function);
    behavior_function_prepare_borrows(function);
    behavior_function_prepare_borrows(helper_function);
    REQUIRE(!helper_function->builds_results);
    pin_value output = { .type = PIN_BOOL };
    REQUIRE(begin(NULL, 0, &output, NULL, 0) == FLOW_OK);
    REQUIRE(!output.b);
    return true;
}
/* A finished function of `slots` scalar slots that returns 1 after calling
 * `callee`, when there is one. */
static behavior_function *nested_link(const behavior_function *callee, int slots)
{
    pin_value number = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, &number, 1, NULL, 0);
    while (acount(behavior_function_builder_definition(builder)->slots) < slots - 1) {
        behavior_function_builder_slot(builder, PIN_I32);
    }
    if (callee) {
        int call = behavior_function_builder_call(builder, callee, NULL, 0, &number, 1);
        emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    }
    int one = value(
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .value = { .type = PIN_I32, .i = 1 } },
        false
    );
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = one });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *f = behavior_function_builder_finish(builder, error, sizeof error);
    if (!f) {
        fprintf(stderr, "nested link: %s\n", error);
    }
    behavior_function_builder_free(builder);
    builder = NULL;
    return f;
}
TEST_CASE(borrow_proof_bounds_nested_callee_facts)
{
    /* links[i] calls links[i - 1]. Each proof keeps a kind and bank per slot
     * and a marker per local live in every frame it has entered, so a walk
     * from links[i] holds the facts of links[0] to links[i - 1] besides its
     * own, which its preparation scratch already counts. */
    size_t nested[NESTED_LINKS + 1] = { 0 };
    for (int i = 0; i <= NESTED_LINKS; i++) {
        links[i] = nested_link(i ? links[i - 1] : NULL, NESTED_LINK_SLOTS);
        REQUIRE(links[i] && behavior_function_compile(links[i]));
        behavior_function_prepare_borrows(links[i]);
        size_t frame = ((size_t)acount(links[i]->slots) + 1) * 2 * sizeof(int) +
                       ((size_t)acount(links[i]->locals) + 1) * sizeof(bool);
        REQUIRE(behavior_function_borrow_bytes(links[i]) == frame);
        if (i < NESTED_LINKS) {
            nested[i + 1] = nested[i] + frame;
        }
    }
    /* Three callees' facts fit the nested bound and four exceed it. */
    REQUIRE(nested[NESTED_LINKS - 1] <= BEHAVIOR_BORROW_NESTED_BYTES);
    REQUIRE(nested[NESTED_LINKS] > BEHAVIOR_BORROW_NESTED_BYTES);
    for (int i = 0; i < NESTED_LINKS; i++) {
        REQUIRE(links[i]->readonly_collections);
    }
    REQUIRE(!links[NESTED_LINKS]->readonly_collections);
    /* Whatever an accepted walk keeps live fits the scratch it reserves. */
    size_t scratch = behavior_function_preparation_bytes(links[NESTED_LINKS - 1])
                         .scratch;
    REQUIRE(scratch != SIZE_MAX);
    REQUIRE(
        scratch >= behavior_function_borrow_bytes(links[NESTED_LINKS - 1]) + BEHAVIOR_BORROW_NESTED_BYTES
    );
    return true;
}
void test_language_borrow()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(borrow_infers_call_scoped_views_through_forwarded_helpers);
    RUN_TEST_CASE(borrow_call_callbacks_keep_their_owned_argument_snapshots);
    RUN_TEST_CASE(borrow_call_contract_rejects_escape_mutation_suspend_and_recursion);
    RUN_TEST_CASE(borrow_helper_pause_and_wait_retain_arguments_before_resuming);
    RUN_TEST_CASE(borrow_helper_mutation_and_opaque_calls_remain_owning);
    RUN_TEST_CASE(borrow_call_guards_program_state_headers_before_commit);
    RUN_TEST_CASE(borrow_authored_readonly_loop_keeps_scalar_writes);
    RUN_TEST_CASE(borrow_guard_preserves_null_empty_mixed_headers_and_errors);
    RUN_TEST_CASE(borrow_guards_scalar_banks_aliasing_array_storage);
    RUN_TEST_CASE(borrow_rejects_mutation_identity_headers_escape_and_opaque_calls);
    RUN_TEST_CASE(borrow_preserves_ownership_across_debug_and_real_suspension);
    RUN_TEST_CASE(borrow_flags_cannot_enter_canonical_or_prepared_code);
    RUN_TEST_CASE(borrow_argument_sharing_rejects_writes_through_caller_storage);
    RUN_TEST_CASE(borrow_argument_sharing_tracks_derived_and_selected_addresses);
    RUN_TEST_CASE(borrow_verifier_rejects_cell_header_rewrites);
    RUN_TEST_CASE(borrow_argument_sharing_copies_after_unplaced_header_writes);
    RUN_TEST_CASE(borrow_call_rejects_result_header_rewrites);
    RUN_TEST_CASE(borrow_argument_sharing_tracks_reference_kinds);
    RUN_TEST_CASE(borrow_union_constructor_tracks_payload_aliases);
    RUN_TEST_CASE(borrow_argument_sharing_passes_string_tables_without_copies);
    RUN_TEST_CASE(borrow_argument_sharing_allows_own_record_field_writes);
    RUN_TEST_CASE(borrow_argument_sharing_copies_around_reference_writes);
    RUN_TEST_CASE(borrow_built_results_move_only_when_unchanged_elsewhere);
    RUN_TEST_CASE(borrow_shared_argument_returned_is_copied);
    RUN_TEST_CASE(borrow_result_reached_through_argument_builds_nothing);
    RUN_TEST_CASE(borrow_aliased_results_stay_independent);
    RUN_TEST_CASE(borrow_update_after_union_keep_builds_nothing);
    RUN_TEST_CASE(borrow_proof_bounds_nested_callee_facts);
    pu_clear_setup();
}
