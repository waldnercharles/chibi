#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/status.h"
#include "export_compiler.h"
#include "language/lower.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/scalar.h"
#include "types/text.h"

#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
    ALIAS_FIXTURES = 12,
    BORROW_FIXTURE = ALIAS_FIXTURES + 1,
    BORROW_CALL_FIXTURE = BORROW_FIXTURE + 1,
    QUERY_FIXTURE = BORROW_CALL_FIXTURE + 1,
    QUERY_FIXTURES = 2,
    READ_FIXTURE = QUERY_FIXTURE + QUERY_FIXTURES,
    READ_FIXTURES = 3,
    WRITE_FIXTURE = READ_FIXTURE + READ_FIXTURES,
    WRITE_FIXTURES = 3,
    CAST_FIXTURE = WRITE_FIXTURE + WRITE_FIXTURES,
    CAST_FIXTURES = 17,
    INTEGER_FIXTURE = CAST_FIXTURE + CAST_FIXTURES,
    INTEGER_FIXTURES = 8,
    TEXT_FIXTURE = INTEGER_FIXTURE + INTEGER_FIXTURES,
    FIXTURES = TEXT_FIXTURE + 1
};
/* Representative BF_CAST pairs; generated C must match the VM per sample. */
static const struct
{
    pin_type from, to;
    behavior_cast_mode mode;
} casts[CAST_FIXTURES] = {
    { PIN_I64, PIN_I32, BEHAVIOR_CAST_EXACT },
    { PIN_I64, PIN_I32, BEHAVIOR_CAST_WRAP },
    { PIN_I64, PIN_I32, BEHAVIOR_CAST_CLAMP },
    { PIN_U64, PIN_U8, BEHAVIOR_CAST_EXACT },
    { PIN_U64, PIN_U8, BEHAVIOR_CAST_WRAP },
    { PIN_U64, PIN_U8, BEHAVIOR_CAST_CLAMP },
    { PIN_I32, PIN_U64, BEHAVIOR_CAST_EXACT },
    { PIN_I32, PIN_U64, BEHAVIOR_CAST_CLAMP },
    { PIN_U64, PIN_I64, BEHAVIOR_CAST_EXACT },
    { PIN_U64, PIN_I64, BEHAVIOR_CAST_CLAMP },
    { PIN_I8, PIN_U16, BEHAVIOR_CAST_EXACT },
    { PIN_U8, PIN_I32, BEHAVIOR_CAST_EXACT },
    { PIN_F64, PIN_U64, BEHAVIOR_CAST_EXACT },
    { PIN_F64, PIN_I16, BEHAVIOR_CAST_EXACT },
    { PIN_F64, PIN_I64, BEHAVIOR_CAST_EXACT },
    { PIN_U64, PIN_F64, BEHAVIOR_CAST_EXACT },
    { PIN_I64, PIN_F64, BEHAVIOR_CAST_EXACT },
};
static const int query_variables[QUERY_FIXTURES] = { 1, ECS_QUERY_VARS };
static const pin_string managed_text = { .bytes = "managed",
                                         .length = sizeof("managed") - 1 };
static behavior_function *functions[FIXTURES];
static behavior_function_builder *builder;
static char directory[128];
static FILE *source;
static void *library;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static behavior_function_context pause_context;
static behavior_function_activation pause_activation;
static arena pause_storage;

static void release_pause()
{
    behavior_function_release_host(&pause_context);
    behavior_function_activation_free(&pause_activation);
    arena_free(&pause_storage);
    pause_context = (behavior_function_context){ 0 };
}

static void cleanup()
{
    release_pause();
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    if (source) {
        fclose(source);
    }
    source = NULL;
    if (library) {
        dlclose(library);
    }
    library = NULL;
    if (builder) {
        behavior_function_builder_free(builder);
    }
    builder = NULL;
    for (int i = 0; i < FIXTURES; i++) {
        behavior_function_free(functions[i]);
        functions[i] = NULL;
    }
    if (directory[0]) {
        tool_remove_tree(directory);
    }
    directory[0] = 0;
}
static pin_value integer(pin_type type, int64_t number)
{
    pin_value value = { .type = type };
    if (type == PIN_I32) {
        value.i = (int32_t)number;
    } else {
        value.i64 = number;
    }
    return value;
}
static int64_t number(pin_value value)
{
    return value.type == PIN_I32 ? value.i : value.i64;
}
static bool cast_fixture(int index)
{
    pin_value from = { .type = casts[index].from }, to = { .type = casts[index].to };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &from, 1, &to, 1, NULL, 0);
    int input = behavior_function_builder_slot(builder, from.type);
    int output = behavior_function_builder_slot(builder, to.type);
    behavior_function_instruction code[] = {
        { .op = BF_ARGUMENT, .type = from.type, .output = input, .value = from },
        { .op = BF_CAST,
          .type = to.type,
          .output = output,
          .left = input,
          .target = casts[index].mode,
          .pin = 5 },
        { .op = BF_RESULT, .type = to.type, .left = output },
        { .op = BF_RETURN },
        { .op = BF_END },
    };
    for (size_t i = 0; i < sizeof code / sizeof *code; i++) {
        behavior_function_builder_emit(builder, code[i]);
    }
    char error[256];
    functions[CAST_FIXTURE + index] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[CAST_FIXTURE + index]) {
        fprintf(stderr, "C export cast fixture: %s\n", error);
    }
    REQUIRE(functions[CAST_FIXTURE + index]);
    behavior_function_builder_free(builder);
    builder = NULL;
    char name[40];
    snprintf(name, sizeof name, "export_cast_%d", index);
    behavior_function_write_c(functions[CAST_FIXTURE + index], source, name);
    return true;
}
static uint64_t cast_bits(pin_value value)
{
    if (value.type != PIN_F64) {
        return behavior_scalar_bits(value);
    }
    uint64_t bits;
    memcpy(&bits, &value.d, sizeof bits);
    return bits;
}
static bool exported_casts_match_the_vm()
{
    const uint64_t integers[] = { 0,
                                  1,
                                  200,
                                  300,
                                  UINT64_MAX,
                                  UINT64_C(1) << 40,
                                  UINT64_C(1) << 63,
                                  UINT64_C(0x80000000) };
    const double reals[] = { -5.5, 300.7,         1e30,    -1e30,
                             NAN,  0x1p63 + 4096, -40000.5 };
    for (int i = 0; i < CAST_FIXTURES; i++) {
        char name[40];
        snprintf(name, sizeof name, "export_cast_%d", i);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        bool real = casts[i].from == PIN_F64;
        int samples = real ? (int)(sizeof reals / sizeof *reals)
                           : (int)(sizeof integers / sizeof *integers);
        for (int sample = 0; sample < samples; sample++) {
            pin_value input = real
                                ? (pin_value){ .type = PIN_F64, .d = reals[sample] }
                                : behavior_scalar_from_bits(casts[i].from, integers[sample]);
            pin_value expected = { .type = casts[i].to }, actual = expected;
            behavior_function_context vm = { .arguments = &input,
                                             .argument_count = 1,
                                             .results = &expected,
                                             .budget = 16 };
            behavior_function_context compiled = { .arguments = &input,
                                                   .argument_count = 1,
                                                   .results = &actual,
                                                   .budget = 16 };
            REQUIRE(behavior_function_bind(functions[CAST_FIXTURE + i], &vm, NULL, 0));
            int reference = behavior_function_run(functions[CAST_FIXTURE + i], &vm, false);
            int status = generated(&compiled);
            /* No cast fails: EXACT clamps a value that does not fit. */
            REQUIRE(status == FLOW_OK && reference == FLOW_OK);
            REQUIRE(cast_bits(actual) == cast_bits(expected));
        }
    }
    return true;
}
static bool fixture(int index)
{
    pin_type type = index < 6 ? PIN_I32 : PIN_I64;
    bool modulo = index % 6 >= 3;
    int alias = index % 3;
    pin_value values[] = { { .type = type }, { .type = type } };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, values, 2, values, 2, NULL, 0);
    int operands[2];
    for (int i = 0; i < 2; i++) {
        operands[i] = behavior_function_builder_slot(builder, type);
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_ARGUMENT,
                                             .type = type,
                                             .output = operands[i],
                                             .left = i,
                                             .value = values[i] }
        );
    }
    int output = alias == 2 ? behavior_function_builder_slot(builder, type)
                            : operands[1 - alias];
    int secondary = alias == 2 ? output : operands[alias];
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = modulo ? BF_MOD : BF_DIV,
                                         .type = type,
                                         .output = output,
                                         .left = operands[0],
                                         .right = operands[1],
                                         .target = secondary }
    );
    int results[] = { output, secondary };
    for (int i = 0; i < 2; i++) {
        behavior_function_builder_emit(
            builder,
            (
                behavior_function_instruction
            ){ .op = BF_RESULT, .type = type, .left = results[i], .target = i }
        );
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    functions[index] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[index]) {
        fprintf(stderr, "C export fixture: %s\n", error);
    }
    REQUIRE(functions[index]);
    behavior_function_builder_free(builder);
    builder = NULL;
    char name[40];
    snprintf(name, sizeof name, "export_alias_%d", index);
    behavior_function_write_c(functions[index], source, name);
    return true;
}
static const pin_type integer_types[INTEGER_FIXTURES] = {
    PIN_I8, PIN_U8, PIN_I16, PIN_U16, PIN_I32, PIN_U32, PIN_I64, PIN_U64
};
static bool integer_fixture(int index)
{
    pin_type type = integer_types[index];
    pin_value inputs[] = { { .type = type }, { .type = type } };
    pin_value outputs[7];
    for (int i = 0; i < 7; i++) {
        outputs[i] = (pin_value){ .type = i < 5 ? type : PIN_BOOL };
    }
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, inputs, 2, outputs, 7, NULL, 0);
    int arguments[2], results[7];
    for (int i = 0; i < 2; i++) {
        arguments[i] = behavior_function_builder_slot(builder, type);
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_ARGUMENT,
                                             .type = type,
                                             .output = arguments[i],
                                             .left = i,
                                             .value = inputs[i] }
        );
    }
    const behavior_function_op operations[] = { BF_ADD, BF_SUB, BF_MUL, BF_DIV,
                                                BF_MOD, BF_LT,  BF_GT };
    for (int i = 0; i < 7; i++) {
        results[i] = behavior_function_builder_slot(builder, outputs[i].type);
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = operations[i],
                                             .type = type,
                                             .output = results[i],
                                             .left = arguments[0],
                                             .right = arguments[1],
                                             .target = -1 }
        );
    }
    for (int i = 0; i < 7; i++) {
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_RESULT,
                                             .type = outputs[i].type,
                                             .left = results[i],
                                             .target = i }
        );
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256], name[40];
    functions[INTEGER_FIXTURE + index] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[INTEGER_FIXTURE + index]) {
        fprintf(stderr, "C integer fixture: %s\n", error);
    }
    REQUIRE(functions[INTEGER_FIXTURE + index]);
    behavior_function_builder_free(builder);
    builder = NULL;
    snprintf(name, sizeof name, "export_integer_%d", index);
    behavior_function_write_c(functions[INTEGER_FIXTURE + index], source, name);
    return true;
}
static bool exported_integer_width_edges()
{
    /* Eight functions, six input pairs, two engines: 96 bounded calls. */
    for (int i = 0; i < INTEGER_FIXTURES; i++) {
        pin_type type = integer_types[i];
        unsigned bits = (unsigned)behavior_scalar_size(type) * 8;
        uint64_t mask = UINT64_MAX >> (64 - bits), high = UINT64_C(1) << (bits - 1);
        bool sign = behavior_scalar_signed(type);
        const uint64_t samples[][2] = { { mask, 1 },     { 0, 1 },
                                        { high + 1, 3 }, { mask, 2 },
                                        { high, mask },  { high, 0 } };
        char name[40];
        snprintf(name, sizeof name, "export_integer_%d", i);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        for (int sample = 0; sample < 6; sample++) {
            pin_value arguments[] = {
                behavior_scalar_from_bits(type, samples[sample][0]),
                behavior_scalar_from_bits(type, samples[sample][1])
            };
            uint64_t x = behavior_scalar_bits(arguments[0]),
                     y = behavior_scalar_bits(arguments[1]);
            /* x / 0 is 0 with remainder x; x / -1 negates with remainder 0. */
            int64_t sx = (int64_t)x, sy = (int64_t)y;
            bool negate = sign && sy == -1;
            uint64_t expected[7] = {
                x + y,
                x - y,
                x * y,
                !y       ? 0
                : negate ? 0 - x
                : sign   ? (uint64_t)(sx / sy)
                         : x / y,
                !y       ? x
                : negate ? 0
                : sign   ? (uint64_t)(sx % sy)
                         : x % y,
                sign ? sx < sy : x < y,
                sign ? sx > sy : x > y,
            };
            for (int backend = 0; backend < 2; backend++) {
                pin_value results[7];
                for (int r = 0; r < 7; r++) {
                    results[r] = (pin_value){ .type = r < 5 ? type : PIN_BOOL };
                }
                behavior_function_context context = { .arguments = arguments,
                                                      .argument_count = 2,
                                                      .results = results,
                                                      .budget = 64 };
                REQUIRE(
                    behavior_function_bind(functions[INTEGER_FIXTURE + i], &context, NULL, 0)
                );
                int status = backend
                               ? generated(&context)
                               : behavior_function_run(functions[INTEGER_FIXTURE + i], &context, false);
                REQUIRE(status == FLOW_OK);
                for (int r = 0; r < 7; r++) {
                    REQUIRE(
                        r < 5 ? behavior_scalar_bits(results[r]) ==
                                    behavior_scalar_bits(
                                        behavior_scalar_from_bits(type, expected[r])
                                    )
                              : results[r].b == (expected[r] != 0)
                    );
                }
                behavior_function_release_host(&context);
            }
        }
    }
    return true;
}
static bool shift_fixture(int index)
{
    pin_type type = integer_types[index];
    pin_value inputs[] = { { .type = type }, { .type = type }, { .type = PIN_BOOL } };
    pin_value result = { .type = type };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, inputs, 3, &result, 1, NULL, 0);
    int left = behavior_function_builder_slot(builder, type);
    int right = behavior_function_builder_slot(builder, type);
    int direction = behavior_function_builder_slot(builder, PIN_BOOL);
    int output = behavior_function_builder_slot(builder, type);
    int operands[] = { left, right, direction };
    for (int a = 0; a < 3; a++) {
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_ARGUMENT,
                                             .type = inputs[a].type,
                                             .output = operands[a],
                                             .left = a,
                                             .value = inputs[a] }
        );
    }
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = direction, .target = 6 }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_SHL,
                                         .type = type,
                                         .output = output,
                                         .left = left,
                                         .right = right,
                                         .node_id = 73,
                                         .pin = 3 }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_JUMP, .target = 7 }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_SHR,
                                         .type = type,
                                         .output = output,
                                         .left = left,
                                         .right = right,
                                         .node_id = 73,
                                         .pin = 3 }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = type, .left = output }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256], name[40];
    functions[INTEGER_FIXTURE + index] = behavior_function_builder_finish(builder, error, sizeof error);
    REQUIRE(functions[INTEGER_FIXTURE + index]);
    behavior_function_builder_free(builder);
    builder = NULL;
    snprintf(name, sizeof name, "export_shift_%d", index);
    behavior_function_write_c(functions[INTEGER_FIXTURE + index], source, name);
    return true;
}
static bool exported_shifts_past_the_width_give_zero_or_sign()
{
    /* Six count boundaries, eight widths, both directions, VM and generated C. */
    for (int i = 0; i < INTEGER_FIXTURES; i++) {
        pin_type type = integer_types[i];
        unsigned width = (unsigned)behavior_scalar_size(type) * 8;
        uint64_t mask = UINT64_MAX >> (64 - width);
        const uint64_t counts[] = { 0, 1, width - 1, width, width + 1, mask };
        char name[40];
        snprintf(name, sizeof name, "export_shift_%d", i);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        for (int direction = 0; direction < 2; direction++) {
            for (int c = 0; c < 6; c++) {
                pin_value arguments[] = { behavior_scalar_from_bits(type, mask),
                                          behavior_scalar_from_bits(type, counts[c]),
                                          { .type = PIN_BOOL, .b = direction != 0 } };
                /* A count at or past the width shifts every bit out; the
                 * signed right shift of -1 keeps filling with ones. */
                bool past = c >= 3;
                uint64_t expected = direction ? (behavior_scalar_signed(type) ? mask
                                                 : past ? 0
                                                        : mask >> counts[c])
                                  : past ? 0
                                         : (mask << counts[c]) & mask;
                for (int backend = 0; backend < 2; backend++) {
                    pin_value result = { .type = type };
                    behavior_function_context context = { .arguments = arguments,
                                                          .argument_count = 3,
                                                          .results = &result,
                                                          .budget = 64 };
                    REQUIRE(
                        behavior_function_bind(functions[INTEGER_FIXTURE + i], &context, NULL, 0)
                    );
                    int status = backend ? generated(&context)
                                         : behavior_function_run(
                                               functions[INTEGER_FIXTURE + i],
                                               &context,
                                               false
                                           );
                    REQUIRE(status == FLOW_OK);
                    REQUIRE((behavior_scalar_bits(result) & mask) == expected);
                    behavior_function_release_host(&context);
                }
            }
        }
    }
    return true;
}
static int inspect(void *user, const behavior_function_point *point)
{
    (void)user;
    (void)point;
    return 0;
}

static bool pause_fixture()
{
    const script_catalog catalog = { 0 };
    const script_lower_bindings bindings = { 0 };
    syntax = script_parse(
        "Scalar :: library {\n"
        "Eval :: (amount: number) -> number {\n"
        "value: number | Cancelled = amount;\n"
        "if value is number found { return found; }\n"
        "return 0; } }"
    );
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis && analysis->valid);
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    for (int i = 0; i < plan->function_count; i++) {
        behavior_function *body = plan->functions[i].body;
        if (body && body->argument_count == 1 && body->result_count == 1) {
            functions[ALIAS_FIXTURES] = body;
            plan->functions[i].body = NULL;
            break;
        }
    }
    REQUIRE(functions[ALIAS_FIXTURES]);
    behavior_function_write_c(functions[ALIAS_FIXTURES], source, "export_pause");
    return true;
}

static int pause_line(void *, behavior_function_context *, const behavior_source_point *point)
{
    return point->line == 4 && point->kind == BEHAVIOR_SOURCE_STATEMENT;
}

static bool resumes_exported_pause()
{
    typedef int (*entry)(behavior_function_context *);
    entry generated = (entry)dlsym(library, "export_pause");
    REQUIRE(generated);
    behavior_function *f = functions[ALIAS_FIXTURES];
    const double samples[] = { 3.75, -0.0 };
    for (int i = 0; i < 2; i++) {
        release_pause();
        pin_value input = { .type = PIN_F64, .d = samples[i] };
        pin_value output = { .type = PIN_F64, .d = 99 };
        behavior_function_activation_init(f, &pause_activation);
        pause_context = (behavior_function_context){
            .activation = &pause_activation,
            .arguments = &input,
            .argument_count = 1,
            .results = &output,
            .budget = 100,
            .source_pause = pause_line,
            .source_function = f,
            .source_points = f->source_points,
            .source_watches = f->source_watches,
        };
        behavior_function_bind_storage(&pause_context, &pause_storage);
        REQUIRE(behavior_function_bind(f, &pause_context, NULL, 0));
        behavior_function_prepare_host(f, &pause_context);
        REQUIRE(generated(&pause_context) == FLOW_PAUSED_RESULT);
        pause_context.source_pause = NULL;
        REQUIRE(generated(&pause_context) == FLOW_OK);
        REQUIRE(!memcmp(&output.d, &input.d, sizeof output.d));
    }
    return true;
}

static int borrow_value(behavior_function_instruction instruction, bool address)
{
    instruction.output = behavior_function_builder_slot(builder, instruction.type);
    if (address) {
        bool typed = behavior_function_builder_kind(
            builder,
            BEHAVIOR_BANK_SLOT,
            instruction.output,
            BEHAVIOR_ADDRESS
        );
        assert(typed);
        (void)typed;
    }
    behavior_function_builder_emit(builder, instruction);
    return instruction.output;
}

static bool borrow_fixture(bool caller)
{
    /* Retain the protocol bank for ownership assertions. Authored lowering
     * correctly clears consumed banks after loading the result into a slot. */
    const pin_value shape = { .type = PIN_F64, .container = PIN_CONTAINER_ARRAY };
    const pin_value output = { .type = PIN_F64 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &shape, 1, &output, 1, NULL, 0);
    int original = borrow_value(
        (behavior_function_instruction){ .op = BF_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .left = 0,
                                         .target = BF_ARGUMENT,
                                         .value = shape },
        true
    );
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_snapshot_call,
        &EXECUTION_value_access,
        &shape,
        1,
        &shape,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_snapshot_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    int input = borrow_value(
        (behavior_function_instruction){
            .op = BF_HOST_SLOT,
            .type = BF_POINTER_TYPE,
            .left = behavior_function_builder_host_first(builder, host) },
        true
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = BF_POINTER_TYPE,
                                         .left = original,
                                         .right = input,
                                         .offset = offsetof(pin_value, d) }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .target = host,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA }
    );
    int result = borrow_value(
        (behavior_function_instruction){
            .op = BF_HOST_SLOT,
            .type = BF_POINTER_TYPE,
            .left = behavior_function_builder_host_results(builder, host) },
        true
    );
    int held = borrow_value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = result,
                                         .offset = offsetof(pin_value, d) },
        true
    );
    behavior_function_builder_root(builder, BEHAVIOR_BANK_SLOT, held, shape);
    if (caller) {
        int call = behavior_function_builder_call(builder, functions[BORROW_FIXTURE], &shape, 1, &output, 1);
        int argument = borrow_value(
            (behavior_function_instruction){
                .op = BF_HOST_SLOT,
                .type = BF_POINTER_TYPE,
                .left = behavior_function_builder_call_first(builder, call) },
            true
        );
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = BF_POINTER_TYPE,
                                             .left = held,
                                             .right = argument,
                                             .offset = offsetof(pin_value, d) }
        );
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_INVOKE, .left = call }
        );
    }
    int count = borrow_value(
        (behavior_function_instruction){ .op = BF_READ,
                                         .type = PIN_I32,
                                         .left = held,
                                         .offset = offsetof(pin_array, count) },
        false
    );
    int converted = borrow_value(
        (behavior_function_instruction){ .op = BF_CAST, .type = PIN_F64, .left = count },
        false
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = converted }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    int index = caller ? BORROW_CALL_FIXTURE : BORROW_FIXTURE;
    functions[index] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[index]) {
        fprintf(stderr, "C borrow fixture: %s\n", error);
    }
    REQUIRE(functions[index]);
    behavior_function_builder_free(builder);
    builder = NULL;
    REQUIRE(functions[index]->runtime_map);
    int guarded = 0;
    for (int i = 0; i < acount(functions[index]->runtime_body); i++) {
        const behavior_function_instruction *instruction = &functions[index]->runtime_body[i];
        guarded += instruction->op == BF_HOST_CALL &&
                   instruction->right == BF_HOST_BORROW_ARRAY;
    }
    REQUIRE(guarded == 1);
    behavior_function_write_c(functions[index], source, caller ? "export_borrow_call" : "export_borrow");
    return true;
}

static void export_call_leave(behavior_function_context *, int) {}

static bool borrows_exported_plain_arrays()
{
    typedef int (*entry)(behavior_function_context *);
    entry generated = (entry)dlsym(library, "export_borrow");
    REQUIRE(generated);
    behavior_function *f = functions[BORROW_FIXTURE];
    int result = -1, guarded = 0;
    for (int i = 0; i < acount(f->runtime_body); i++) {
        const behavior_function_instruction *instruction = &f->runtime_body[i];
        if (instruction->op == BF_HOST_CALL &&
            instruction->right == BF_HOST_BORROW_ARRAY) {
            REQUIRE(!f->instructions[i].right);
            result = f->host_calls[instruction->target].result_first;
            guarded++;
        }
    }
    REQUIRE(guarded == 1 && result >= 0);
    for (int sample = 0; sample < 6; sample++) {
        for (int inspection = 0; inspection < 2; inspection++) {
            release_pause();
            pin_value items[] = { { .type = PIN_F64, .d = 1 },
                                  { .type = PIN_F64, .d = 2 },
                                  { .type = PIN_F64, .d = 3 } };
            pin_array values = { .element_type = PIN_F64,
                                 .count = 3,
                                 .capacity = 3,
                                 .items = items };
            double packed[] = { 1, 2, 3 };
            if (sample == 1) {
                values.element_type = PIN_STR;
                for (int item = 0; item < 3; item++) {
                    items[item] = (pin_value){ .type = PIN_STR, .text = &managed_text };
                }
            } else if (sample == 2) {
                /* A plain header cannot prove that each payload is plain. */
                items[1] = (pin_value){ .type = PIN_STR, .text = &managed_text };
            } else if (sample == 3) {
                /* Native inputs can alias the public saved-value bank even
                 * when this function itself never suspends. */
                int saved = acount(f->slots) + acount(f->locals) + acount(f->frames);
                REQUIRE((size_t)saved * sizeof *pause_activation.values >= sizeof(pin_value));
                pause_activation
                    .values = xcalloc((size_t)saved, sizeof *pause_activation.values);
                values.items = (pin_value *)pause_activation.values;
                values.items[0] = items[0];
                values.count = values.capacity = 1;
            } else if (sample == 5) {
                values.items = NULL;
                values.data = packed;
                values.stride = sizeof *packed;
            }
            pin_value input = { .type = PIN_F64,
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = &values };
            pin_value output = { .type = PIN_F64, .d = -1 };
            pause_context = (behavior_function_context){
                .arguments = &input,
                .argument_count = 1,
                .results = &output,
                .activation = sample == 3 ? &pause_activation : NULL,
                .budget = 1000,
                .call_leave = sample == 4 ? export_call_leave : NULL,
                .inspect = inspection ? inspect : NULL
            };
            behavior_function_bind_storage(&pause_context, &pause_storage);
            REQUIRE(behavior_function_bind(f, &pause_context, NULL, 0));
            behavior_function_prepare_host(f, &pause_context);
            REQUIRE(generated(&pause_context) == FLOW_OK);
            REQUIRE(output.d == values.count);
            const pin_array *snapshot = pause_context.data->values[result].ptr;
            if ((!sample || sample == 5) && !inspection) {
                REQUIRE(snapshot == &values);
                REQUIRE(!arena_storage_bytes(&pause_storage));
            } else {
                REQUIRE(
                    snapshot && snapshot != &values && snapshot->count == values.count
                );
                REQUIRE(
                    values.stride ? snapshot->data != values.data
                                  : snapshot->items != values.items
                );
                REQUIRE(arena_storage_bytes(&pause_storage) > 0);
                if (sample == 1 || sample == 2) {
                    REQUIRE(
                        pin_text(pin_array_get(snapshot, 1)).bytes !=
                        pin_text(items[1]).bytes
                    );
                    REQUIRE(!strcmp(pin_text(pin_array_get(snapshot, 1)).bytes, "managed"));
                } else {
                    REQUIRE(pin_array_get(snapshot, 0).type == PIN_F64);
                    REQUIRE(pin_array_get(snapshot, 0).d == 1);
                }
            }
        }
    }
    return true;
}

static bool exported_call_snapshots_keep_the_standalone_abi()
{
    typedef int (*entry)(behavior_function_context *);
    entry generated = (entry)dlsym(library, "export_borrow_call");
    REQUIRE(generated);
    behavior_function *f = functions[BORROW_CALL_FIXTURE];
    REQUIRE(f->readonly_collections);
    release_pause();
    pin_value item = { .type = PIN_F64, .d = 7 }, output = { .type = PIN_F64 };
    pin_array array = { .element_type = PIN_F64, .count = 1, .capacity = 1, .items = &item };
    pin_value input = { .type = PIN_F64, .container = PIN_CONTAINER_ARRAY, .ptr = &array };
    behavior_function_activation_init(f, &pause_activation);
    pause_context = (behavior_function_context){ .arguments = &input,
                                                 .argument_count = 1,
                                                 .results = &output,
                                                 .activation = &pause_activation,
                                                 .budget = 1000 };
    behavior_function_bind_storage(&pause_context, &pause_storage);
    REQUIRE(behavior_function_bind(f, &pause_context, NULL, 0));
    behavior_function_prepare_host(f, &pause_context);
    REQUIRE(generated(&pause_context) == BEHAVIOR_FUNCTION_CALL_RESULT);
    const pin_array *held = pause_context.data->values[f->calls[0].first].ptr;
    REQUIRE(held && held != &array && held->items != &item);
    item.d = 99;
    REQUIRE(pin_array_get(held, 0).d == 7);
    return true;
}

static bool query_fixture(int index)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_HANDLE };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    int value = behavior_function_builder_slot(builder, PIN_HANDLE);
    int origin = behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .graph_id = 8310,
                                    .call_id = 8311,
                                    .parent = -1,
                                    .asset_name = "QueryBinding.chibi",
                                    .start = 12,
                                    .end = 18,
                                    .line = 2,
                                    .column = 5 }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_QUERY_VARIABLE,
                                         .type = PIN_HANDLE,
                                         .output = value,
                                         .target = query_variables[index],
                                         .source = origin,
                                         .node_id = 8312,
                                         .pin = 7 }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_HANDLE, .left = value }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *f = behavior_function_builder_finish(builder, error, sizeof error);
    functions[QUERY_FIXTURE + index] = f;
    if (!f) {
        fprintf(stderr, "C query fixture: %s\n", error);
    }
    REQUIRE(f);
    behavior_function_builder_free(builder);
    builder = NULL;
    REQUIRE(!acount(f->host_calls) && !acount(f->data_values));
    char name[40];
    snprintf(name, sizeof name, "export_query_%d", index);
    behavior_function_write_c(f, source, name);
    return true;
}

static bool exported_query_variables_preserve_tuple_and_failure_contract()
{
    const uint64_t samples[] = { UINT64_C(0x87654321abcdef01), 0, 37 };
    for (int index = 0; index < QUERY_FIXTURES; index++) {
        char name[40];
        snprintf(name, sizeof name, "export_query_%d", index);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        behavior_function *f = functions[QUERY_FIXTURE + index];
        uint64_t tuple[ECS_QUERY_VARS];
        for (int i = 0; i < ECS_QUERY_VARS; i++) {
            tuple[i] = (uint64_t)i + 101;
        }
        for (int sample = 0; sample < 3; sample++) {
            tuple[query_variables[index] - 1] = samples[sample];
            bool missing = sample == 2;
            for (int inspection = 0; inspection < 2; inspection++) {
                pin_value output = { .type = PIN_HANDLE, .u64 = UINT64_MAX };
                behavior_function_context context = {
                    .query_variables = missing ? NULL : tuple,
                    .results = &output,
                    .budget = 32,
                    .inspect = inspection ? inspect : NULL
                };
                REQUIRE(generated(&context) == (missing ? FLOW_ERR_INVALID_GRAPH : FLOW_OK));
                REQUIRE(output.type == PIN_HANDLE);
                REQUIRE(output.u64 == (missing ? UINT64_MAX : samples[sample]));
                if (!missing) {
                    REQUIRE(!context.error_message);
                    continue;
                }
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, "Query variable is not bound")
                );
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(context.node_id == 8312 && context.error_pin == 7);
                REQUIRE(context.error_frame == 0 && context.error_source == 1);
                REQUIRE(context.error_stack == -1);
                REQUIRE(context.error_steps == 0 && context.steps == 0);
                REQUIRE(acount(f->sources) == 2);
                const behavior_function_source *origin = &f->sources[context.error_source];
                REQUIRE(origin->graph_id == 8310 && origin->call_id == 8311);
                REQUIRE(origin->start == 12 && origin->end == 18);
                REQUIRE(origin->line == 2 && origin->column == 5);
                REQUIRE(!strcmp(origin->asset_name, "QueryBinding.chibi"));
                context.query_variables = tuple;
                REQUIRE(generated(&context) == FLOW_OK);
                REQUIRE(output.type == PIN_HANDLE && output.u64 == samples[sample]);
                REQUIRE(!context.error_message);
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(context.node_id == 8312 && context.error_pin == 7);
                REQUIRE(context.error_frame == 0 && context.error_source == 1);
                REQUIRE(context.error_stack == -1);
                REQUIRE(context.error_steps == 0 && context.steps == 0);
            }
        }
    }
    return true;
}

#define READ_COMPONENT UINT64_C(0x123456789abcdef0)
#define READ_LAYOUT UINT64_C(0x23456789abcdef01)
#define READ_ENTITY ((UINT64_C(7) << 32) | 1)

typedef struct
{
    int prefix;
    bool enabled;
    uint64_t wide;
    double real;
} export_read_row;
static const struct
{
    pin_type type;
    size_t offset, size;
} read_fields[READ_FIXTURES] = {
    { PIN_BOOL, offsetof(export_read_row, enabled), sizeof(bool) },
    { PIN_U64, offsetof(export_read_row, wide), sizeof(uint64_t) },
    { PIN_F64, offsetof(export_read_row, real), sizeof(double) }
};

typedef struct
{
    export_read_row rows[2];
    uint32_t generations[2];
    unsigned char occupied[2];
    uint64_t membership;
    int lookup[2];
    ecs_execution_column column;
    ecs_execution_view view;
} export_read_world;

static void read_world(export_read_world *world, int sample)
{
    *world = (export_read_world){ 0 };
    world->rows[1].enabled = !sample;
    world->rows[1].wide = sample ? UINT64_C(0x8000000000000001)
                                 : UINT64_C(0xfedcba9876543210);
    uint64_t bits = sample ? UINT64_C(0x7ff8123456789abc)
                           : UINT64_C(0x8000000000000000);
    memcpy(&world->rows[1].real, &bits, sizeof bits);
    world->generations[1] = 7;
    world->occupied[1] = 1;
    world->membership = 2;
    world->column = (ecs_execution_column){ .id = READ_COMPONENT,
                                            .layout = READ_LAYOUT,
                                            .stride = sizeof(export_read_row),
                                            .data = world->rows,
                                            .occupancy = &world->membership,
                                            .count = 1,
                                            .singleton = 1 };
    uint64_t hash = READ_COMPONENT * UINT64_C(11400714819323198485);
    world->lookup[(hash ^ (hash >> 32)) & 1] = 1;
    world->view = (ecs_execution_view){ .generations = world->generations,
                                        .occupied = world->occupied,
                                        .slot_count = 2,
                                        .columns = &world->column,
                                        .lookup = world->lookup,
                                        .capacity = 2 };
}

static bool read_fixture(int field)
{
    pin_value input = { .type = PIN_HANDLE },
              output = { .type = read_fields[field].type };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int entity = behavior_function_builder_slot(builder, PIN_HANDLE);
    int result = behavior_function_builder_slot(builder, output.type);
    int origin = behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .asset_name = "ExportRead",
                                    .parent = -1,
                                    .start = 7,
                                    .end = 19,
                                    .line = 2,
                                    .column = 3 }
    );
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = PIN_HANDLE, .output = entity, .value = input }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_CHECK });
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){
            .op = BF_COMPONENT_READ,
            .type = output.type,
            .output = result,
            .left = entity,
            .component = behavior_function_builder_component(builder, READ_COMPONENT, false, false),
            .value = { .type = PIN_U64, .u64 = READ_COMPONENT },
            .layout = READ_LAYOUT,
            .offset = sizeof(export_read_row),
            .target = (int)read_fields[field].offset,
            .source = origin,
            .pin = 23 + field,
            .node_id = UINT64_C(0x3456789abcdef012) + (uint64_t)field,
            .message = "must use the component read diagnostic" }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = output.type, .left = result }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    functions[READ_FIXTURE + field] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[READ_FIXTURE + field]) {
        fprintf(stderr, "C component read fixture: %s\n", error);
    }
    REQUIRE(functions[READ_FIXTURE + field]);
    behavior_function_builder_free(builder);
    builder = NULL;
    char name[40];
    snprintf(name, sizeof name, "export_read_%d", field);
    behavior_function_write_c(functions[READ_FIXTURE + field], source, name);
    return true;
}

static bool read_metadata(const behavior_function_context *context, const behavior_function_instruction *read)
{
    REQUIRE(context->failure == BEHAVIOR_FUNCTION_HOST_DATA);
    REQUIRE(context->node_id == read->node_id);
    REQUIRE(context->error_frame == read->frame && context->error_source == read->source);
    REQUIRE(context->error_stack == read->stack && context->error_pin == read->pin);
    REQUIRE(context->error_steps == 8 && context->steps == 8);
    REQUIRE(context->error_status == -87);
    return true;
}

static bool reads_exported_current_world_scalars()
{
    for (int field = 0; field < READ_FIXTURES; field++) {
        char name[40];
        snprintf(name, sizeof name, "export_read_%d", field);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        const behavior_function *f = functions[READ_FIXTURE + field];
        const behavior_function_instruction *read = NULL;
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            if (f->instructions[pc].op == BF_COMPONENT_READ) {
                REQUIRE(!read);
                read = &f->instructions[pc];
            }
        }
        REQUIRE(read && read->source > 0 && !f->root_steps);
        for (int inspection = 0; inspection < 2; inspection++) {
            export_read_world worlds[2];
            read_world(&worlds[0], 0);
            read_world(&worlds[1], 1);
            ecs_execution_column stale = worlds[0].column;
            stale.layout = 0;
            const ecs_execution_column *columns[] = { &stale };
            ecs_execution_view binding = worlds[0].view;
            binding.columns = &stale;
            pin_value input = { .type = PIN_HANDLE, .u64 = READ_ENTITY };
            pin_value output = { .type = read_fields[field].type };
            behavior_function_context context = { .arguments = &input,
                                                  .argument_count = 1,
                                                  .results = &output,
                                                  .budget = 32,
                                                  .error_status = -87,
                                                  .columns = columns,
                                                  .binding_world = &binding,
                                                  .inspect = inspection ? inspect
                                                                        : NULL };
            for (int sample = 0; sample < 2; sample++) {
                context.world = &worlds[sample].view;
                context.steps = 7;
                context.error_message = "stale component read error";
                REQUIRE(generated(&context) == FLOW_OK);
                REQUIRE(
                    !context.error_message && output.type == read_fields[field].type
                );
                REQUIRE(!memcmp(
                    &output.d,
                    (const char *)&worlds[sample].rows[1] + read_fields[field].offset,
                    read_fields[field].size
                ));
            }
            /* The same context recovers against a different current world.
             * Neither the stale column table nor binding world is rebound. */
            for (int failure = 0; failure < 10; failure++) {
                read_world(&worlds[0], 0);
                read_world(&worlds[1], 1);
                export_read_world *world = &worlds[failure % 2];
                context.world = &world->view;
                input.u64 = READ_ENTITY;
                switch (failure) {
                    case 0: context.world = NULL; break;
                    case 1: input.u64 = UINT64_C(7) << 32; break;
                    case 2: input.u64 = (UINT64_C(7) << 32) | 2; break;
                    case 3: world->occupied[1] = 0; break;
                    case 4: world->generations[1]++; break;
                    case 5: world->membership = 0; break;
                    case 6: world->view.capacity = 0; break;
                    case 7: world->column.id++; break;
                    case 8: world->column.layout ^= 1; break;
                    case 9: world->column.stride++; break;
                }
                const pin_value sentinel = { .type = read_fields[field].type,
                                             .u64 = UINT64_C(0xa5a5a5a5a5a5a5a5) };
                output = sentinel;
                context.steps = 7;
                context.error_message = "stale component read error";
                context.failure = BEHAVIOR_FUNCTION_ARGUMENT;
                context.node_id = 99;
                context.error_frame = context.error_source = 99;
                context.error_stack = context.error_pin = 99;
                context.error_steps = 99;
                REQUIRE(generated(&context) == FLOW_ERR_INVALID_GRAPH);
                REQUIRE(
                    output.type == sentinel.type &&
                    !memcmp(&output.d, &sentinel.d, sizeof output.d)
                );
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, "Referenced storage is unavailable")
                );
                REQUIRE(read_metadata(&context, read));
                world = &worlds[1 - failure % 2];
                context.world = &world->view;
                input.u64 = READ_ENTITY;
                context.steps = 7;
                REQUIRE(generated(&context) == FLOW_OK);
                REQUIRE(
                    !context.error_message && output.type == read_fields[field].type
                );
                REQUIRE(!memcmp(
                    &output.d,
                    (const char *)&world->rows[1] + read_fields[field].offset,
                    read_fields[field].size
                ));
                REQUIRE(read_metadata(&context, read));
            }
        }
    }
    return true;
}

enum
{
    WRITE_SAMPLE_0,
    WRITE_SAMPLE_1,
    WRITE_CLAMP,
    WRITE_RELOCATE,
    WRITE_NO_WORLD,
    WRITE_ZERO_SLOT,
    WRITE_INVALID_SLOT,
    WRITE_UNOCCUPIED,
    WRITE_GENERATION,
    WRITE_NO_MEMBERSHIP,
    WRITE_NO_COLUMN,
    WRITE_WRONG_COMPONENT,
    WRITE_LAYOUT,
    WRITE_STRIDE,
    WRITE_NO_CALLBACK,
    WRITE_REJECT,
    WRITE_MUTATE_REJECT,
    WRITE_CASES
};

static bool write_fixture(int field)
{
    pin_value inputs[] = { { .type = field == 1 ? PIN_U64 : PIN_HANDLE },
                           { .type = read_fields[field].type } };
    pin_value outputs[] = { inputs[1], inputs[1] };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, inputs, 2, outputs, 2, NULL, 0);
    int operands[2];
    for (int i = 0; i < 2; i++) {
        operands[i] = behavior_function_builder_slot(builder, inputs[i].type);
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_ARGUMENT,
                                             .type = inputs[i].type,
                                             .output = operands[i],
                                             .left = i,
                                             .value = inputs[i] }
        );
    }
    int result = behavior_function_builder_slot(builder, outputs[0].type);
    int origin = behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .asset_name = "ExportWrite",
                                    .parent = -1,
                                    .start = 11,
                                    .end = 29,
                                    .line = 3,
                                    .column = 5 }
    );
    behavior_function_instruction write = {
        .op = BF_COMPONENT_WRITE,
        .type = outputs[0].type,
        .left = operands[0],
        .right = operands[1],
        .component = behavior_function_builder_component(builder, READ_COMPONENT, false, true),
        .value = { .type = PIN_U64, .u64 = READ_COMPONENT },
        .layout = READ_LAYOUT,
        .offset = sizeof(export_read_row),
        .target = (int)read_fields[field].offset,
        .source = origin,
        .pin = 31 + field,
        .node_id = UINT64_C(0x456789abcdef0123) + (uint64_t)field,
        .message = "must use the component write diagnostic"
    };
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_CHECK });
    behavior_function_builder_emit(builder, write);
    behavior_function_instruction read = write;
    read.op = BF_COMPONENT_READ;
    read.output = result;
    read.right = 0;
    read.node_id += 10;
    behavior_function_builder_emit(builder, read);
    int results[] = { operands[1], result };
    for (int i = 0; i < 2; i++) {
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_RESULT,
                                             .type = outputs[i].type,
                                             .left = results[i],
                                             .target = i }
        );
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    functions[WRITE_FIXTURE + field] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[WRITE_FIXTURE + field]) {
        fprintf(stderr, "C component write fixture: %s\n", error);
    }
    REQUIRE(functions[WRITE_FIXTURE + field]);
    behavior_function_builder_free(builder);
    builder = NULL;
    char name[40];
    snprintf(name, sizeof name, "export_write_%d", field);
    behavior_function_write_c(functions[WRITE_FIXTURE + field], source, name);
    return true;
}

typedef struct
{
    export_read_world *world, *replacement;
    export_read_row before;
    pin_value input, stored;
    int field, sample, calls;
    bool valid;
} export_write_state;

static int export_store_component(
    behavior_function_context *context,
    uint64_t entity,
    uint64_t component,
    uint64_t layout,
    size_t offset,
    pin_value declared,
    pin_value value
)
{
    export_write_state *state = context->inspection_user;
    int field = state->field;
    state->calls++;
    state->valid = state->calls == 1 && context->world == &state->world->view &&
                   !context->value_storage && entity == READ_ENTITY &&
                   component == READ_COMPONENT && layout == READ_LAYOUT &&
                   offset == read_fields[field].offset &&
                   declared.type == read_fields[field].type &&
                   declared.container == PIN_CONTAINER_SINGLE && !declared.type_name &&
                   !declared.u64 && value.type == state->input.type &&
                   value.container == PIN_CONTAINER_SINGLE && !value.type_name &&
                   !memcmp(&value.d, &state->input.d, read_fields[field].size) &&
                   !memcmp(&state->world->rows[1], &state->before, sizeof state->before);
    if (!state->valid || state->sample == WRITE_REJECT) {
        return -1;
    }
    export_read_world *world = state->world;
    if (state->sample == WRITE_RELOCATE) {
        world = state->replacement;
        context->world = &world->view;
    }
    memcpy(
        (char *)&world->rows[1] + offset,
        &state->stored.d,
        read_fields[field].size
    );
    return state->sample == WRITE_MUTATE_REJECT ? -1 : 0;
}

static bool writes_exported_current_world_scalars()
{
    /* Three scalar fixtures, seventeen cases, two inspection modes and two
     * backends: 102 generated calls paired with 102 bytecode calls. */
    for (int field = 0; field < WRITE_FIXTURES; field++) {
        char name[40];
        snprintf(name, sizeof name, "export_write_%d", field);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        const behavior_function *f = functions[WRITE_FIXTURE + field];
        const behavior_function_instruction *write = NULL;
        int reads = 0;
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            if (f->instructions[pc].op == BF_COMPONENT_WRITE) {
                REQUIRE(!write);
                write = &f->instructions[pc];
            }
            reads += f->instructions[pc].op == BF_COMPONENT_READ;
        }
        REQUIRE(write && write->source > 0 && reads == 1 && !f->root_steps);
        REQUIRE(!acount(f->host_calls) && !acount(f->data_values));
        for (int inspection = 0; inspection < 2; inspection++) {
            for (int sample = 0; sample < WRITE_CASES; sample++) {
                pin_value reference[2];
                int reference_status = 0;
                for (int backend = 0; backend < 2; backend++) {
                    export_read_world worlds[2];
                    read_world(&worlds[0], 1);
                    read_world(&worlds[1], 1);
                    worlds[0].rows[1].prefix = 41;
                    worlds[1].rows[1].prefix = 42;
                    export_read_row expected[2][2];
                    memcpy(expected[0], worlds[0].rows, sizeof expected[0]);
                    memcpy(expected[1], worlds[1].rows, sizeof expected[1]);
                    ecs_execution_column stale = worlds[0].column;
                    stale.layout = 0;
                    const ecs_execution_column *columns[] = { &stale };
                    ecs_execution_view binding = worlds[0].view;
                    binding.columns = &stale;
                    pin_value inputs[] = { { .type = field == 1 ? PIN_U64 : PIN_HANDLE,
                                             .u64 = READ_ENTITY },
                                           { .type = read_fields[field].type } };
                    export_read_world values;
                    read_world(&values, sample == WRITE_SAMPLE_1);
                    memcpy(
                        &inputs[1].d,
                        (const char *)&values.rows[1] + read_fields[field].offset,
                        read_fields[field].size
                    );
                    if (sample == WRITE_CLAMP && field == 2) {
                        inputs[1].d = 200;
                    }
                    export_write_state state = { .world = &worlds[0],
                                                 .replacement = &worlds[1],
                                                 .input = inputs[1],
                                                 .stored = inputs[1],
                                                 .field = field,
                                                 .sample = sample };
                    memcpy(&state.before, &worlds[0].rows[1], sizeof state.before);
                    if (sample == WRITE_CLAMP) {
                        switch (state.stored.type) {
                            case PIN_BOOL: state.stored.b = false; break;
                            case PIN_U64: state.stored.u64 = 500; break;
                            case PIN_F64: state.stored.d = 12.5; break;
                            default: REQUIRE(false);
                        }
                    }
                    const pin_value sentinel = { .type = read_fields[field].type,
                                                 .u64 = UINT64_C(0xa5a5a5a5a5a5a5a5) };
                    pin_value outputs[] = { sentinel, sentinel };
                    behavior_function_context context = {
                        .arguments = inputs,
                        .argument_count = 2,
                        .results = outputs,
                        .world = &worlds[0].view,
                        .columns = columns,
                        .binding_world = &binding,
                        .store_component = export_store_component,
                        .inspection_user = &state,
                        .inspect = inspection ? inspect : NULL,
                        .budget = 32,
                        .steps = 7,
                        .error_status = -87,
                        .error_message = "stale component write error",
                        .failure = BEHAVIOR_FUNCTION_ARGUMENT,
                        .node_id = 99,
                        .error_frame = 99,
                        .error_source = 99,
                        .error_stack = 99,
                        .error_pin = 99,
                        .error_steps = 99
                    };
                    switch (sample) {
                        case WRITE_NO_WORLD: context.world = NULL; break;
                        case WRITE_ZERO_SLOT:
                            inputs[0].u64 = UINT64_C(7) << 32;
                            break;
                        case WRITE_INVALID_SLOT:
                            inputs[0].u64 = (UINT64_C(7) << 32) | 2;
                            break;
                        case WRITE_UNOCCUPIED: worlds[0].occupied[1] = 0; break;
                        case WRITE_GENERATION:
                            worlds[0].generations[1]++;
                            break;
                        case WRITE_NO_MEMBERSHIP:
                            worlds[0].membership = 0;
                            break;
                        case WRITE_NO_COLUMN:
                            worlds[0].view.capacity = 0;
                            break;
                        case WRITE_WRONG_COMPONENT:
                            worlds[0].column.id++;
                            break;
                        case WRITE_LAYOUT: worlds[0].column.layout ^= 1; break;
                        case WRITE_STRIDE: worlds[0].column.stride++; break;
                        case WRITE_NO_CALLBACK:
                            context.store_component = NULL;
                            break;
                    }
                    REQUIRE(behavior_function_bind(f, &context, NULL, 0));
                    int status = backend
                                   ? generated(&context)
                                   : behavior_function_run(f, &context, false);
                    bool success = sample <= WRITE_RELOCATE;
                    bool callback = success || sample >= WRITE_REJECT;
                    REQUIRE(status == (success ? FLOW_OK : FLOW_ERR_INVALID_GRAPH));
                    REQUIRE(state.calls == (callback ? 1 : 0));
                    REQUIRE(!callback || state.valid);
                    REQUIRE(!context.value_storage);
                    if (success || sample == WRITE_MUTATE_REJECT) {
                        int destination = sample == WRITE_RELOCATE;
                        memcpy(
                            (char *)&expected[destination][1] +
                                read_fields[field].offset,
                            &state.stored.d,
                            read_fields[field].size
                        );
                    }
                    for (int world = 0; world < 2; world++) {
                        REQUIRE(
                            !memcmp(worlds[world].rows, expected[world], sizeof expected[world])
                        );
                    }
                    REQUIRE(
                        outputs[0].type == sentinel.type &&
                        outputs[1].type == sentinel.type
                    );
                    if (success) {
                        REQUIRE(!context.error_message);
                        REQUIRE(!memcmp(
                            &outputs[0].d,
                            &inputs[1].d,
                            read_fields[field].size
                        ));
                        REQUIRE(!memcmp(
                            &outputs[1].d,
                            &state.stored.d,
                            read_fields[field].size
                        ));
                    } else {
                        REQUIRE(
                            !memcmp(&outputs[0].d, &sentinel.d, sizeof sentinel.d)
                        );
                        REQUIRE(
                            !memcmp(&outputs[1].d, &sentinel.d, sizeof sentinel.d)
                        );
                        REQUIRE(
                            context.error_message &&
                            !strcmp(
                                context.error_message,
                                sample >= WRITE_NO_CALLBACK
                                    ? "Cannot retain referenced component write"
                                    : "Referenced storage is unavailable"
                            )
                        );
                        REQUIRE(read_metadata(&context, write));
                    }
                    if (!backend) {
                        reference[0] = outputs[0];
                        reference[1] = outputs[1];
                        reference_status = status;
                    } else {
                        REQUIRE(status == reference_status);
                        for (int result = 0; result < 2; result++) {
                            REQUIRE(!memcmp(
                                &outputs[result].d,
                                &reference[result].d,
                                read_fields[field].size
                            ));
                        }
                    }
                }
            }
        }
    }
    return true;
}

static const char binary_text_bytes[] = { 'A', 0, 'B', (char)0xff, 0 };
static const char other_text_bytes[] = { 'A', 0, 'C', (char)0xff, 0 };
static const pin_string binary_text = { .bytes = binary_text_bytes,
                                        .length = sizeof binary_text_bytes - 1 };
static const pin_string other_text = { .bytes = other_text_bytes,
                                       .length = sizeof other_text_bytes - 1 };
static bool binary_text_fixture()
{
    pin_value argument = { .type = PIN_STR };
    pin_value outputs[] = { argument, { .type = PIN_BOOL }, { .type = PIN_BOOL } };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &argument, 1, outputs, 3, NULL, 0);
    int input = behavior_function_builder_slot(builder, PIN_STR);
    int first = behavior_function_builder_slot(builder, PIN_STR);
    int second = behavior_function_builder_slot(builder, PIN_STR);
    int equal = behavior_function_builder_slot(builder, PIN_BOOL);
    int different = behavior_function_builder_slot(builder, PIN_BOOL);
    const behavior_function_instruction code[] = {
        { .op = BF_ARGUMENT, .type = PIN_STR, .output = input, .value = argument },
        { .op = BF_CONSTANT,
          .type = PIN_STR,
          .output = first,
          .value = { .type = PIN_STR, .text = &binary_text } },
        { .op = BF_CONSTANT,
          .type = PIN_STR,
          .output = second,
          .value = { .type = PIN_STR, .text = &other_text } },
        { .op = BF_STRING_EQUAL, .type = PIN_BOOL, .output = equal, .left = input, .right = first },
        { .op = BF_STRING_EQUAL, .type = PIN_BOOL, .output = different, .left = first, .right = second },
        { .op = BF_RESULT, .type = PIN_STR, .left = first, .target = 0 },
        { .op = BF_RESULT, .type = PIN_BOOL, .left = equal, .target = 1 },
        { .op = BF_RESULT, .type = PIN_BOOL, .left = different, .target = 2 },
        { .op = BF_RETURN },
        { .op = BF_END },
    };
    for (int i = 0; i < (int)(sizeof code / sizeof *code); i++) {
        behavior_function_builder_emit(builder, code[i]);
    }
    char error[256];
    functions[TEXT_FIXTURE] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[TEXT_FIXTURE]) {
        fprintf(stderr, "C export text fixture: %s\n", error);
    }
    REQUIRE(functions[TEXT_FIXTURE]);
    behavior_function_builder_free(builder);
    builder = NULL;
    behavior_function_write_c(functions[TEXT_FIXTURE], source, "export_binary_text");
    return true;
}
/* An unrelated call after the export returns exercises descriptor lifetime. */
__attribute__((noinline)) static unsigned disturb_export_stack()
{
    volatile unsigned char bytes[4096];
    unsigned sum = 0;
    for (int i = 0; i < (int)sizeof bytes; i++) {
        bytes[i] = (unsigned char)i;
        sum += bytes[i];
    }
    return sum;
}
static bool exported_binary_text_matches_the_vm()
{
    typedef int (*entry)(behavior_function_context *);
    entry generated = (entry)dlsym(library, "export_binary_text");
    REQUIRE(generated);
    const pin_string *samples[] = { &binary_text, &other_text };
    for (int sample = 0; sample < 2; sample++) {
        pin_value argument = { .type = PIN_STR, .text = samples[sample] };
        pin_value expected[] = { { .type = PIN_STR },
                                 { .type = PIN_BOOL },
                                 { .type = PIN_BOOL } };
        pin_value actual[] = { { .type = PIN_STR },
                               { .type = PIN_BOOL },
                               { .type = PIN_BOOL } };
        arena vm_storage = { 0 }, generated_storage = { 0 };
        behavior_function_context vm = { .arguments = &argument,
                                         .argument_count = 1,
                                         .results = expected,
                                         .budget = 64 };
        behavior_function_context compiled = { .arguments = &argument,
                                               .argument_count = 1,
                                               .results = actual,
                                               .budget = 64 };
        behavior_function_bind_storage(&vm, &vm_storage);
        behavior_function_bind_storage(&compiled, &generated_storage);
        REQUIRE(behavior_function_bind(functions[TEXT_FIXTURE], &vm, NULL, 0));
        REQUIRE(behavior_function_run(functions[TEXT_FIXTURE], &vm, false) == FLOW_OK);
        REQUIRE(generated(&compiled) == FLOW_OK);
        REQUIRE(disturb_export_stack() == 522240);
        for (int backend = 0; backend < 2; backend++) {
            const pin_value *result = backend ? actual : expected;
            REQUIRE(result[0].type == PIN_STR && result[0].text);
            REQUIRE(pin_text(result[0]).length == sizeof binary_text_bytes - 1);
            REQUIRE(
                !memcmp(pin_text(result[0]).bytes, binary_text_bytes, sizeof binary_text_bytes - 1)
            );
            REQUIRE(result[1].type == PIN_BOOL && result[1].b == (sample == 0));
            REQUIRE(result[2].type == PIN_BOOL && !result[2].b);
        }
        arena_free(&vm_storage);
        arena_free(&generated_storage);
    }
    return true;
}

static bool begin_export()
{
    snprintf(directory, sizeof directory, "/tmp/chibi-c-export-XXXXXX");
    char *created = mkdtemp(directory);
    if (!created) {
        directory[0] = 0;
    }
    REQUIRE(created);
    char input[256];
    snprintf(input, sizeof input, "%s/aliases.c", directory);
    source = fopen(input, "w");
    REQUIRE(source);
    return true;
}
static bool compile_export(long source_limit)
{
    char input[256], output[256], log[256], includes[PATH_MAX], debug[PATH_MAX];
    snprintf(input, sizeof input, "%s/aliases.c", directory);
    snprintf(output, sizeof output, "%s/aliases.so", directory);
    snprintf(log, sizeof log, "%s/compiler.log", directory);
    snprintf(includes, sizeof includes, "%s/src", tool_root());
    snprintf(debug, sizeof debug, "%s/src/execution/source_debug.c", tool_root());
    long bytes = ftell(source);
    if (bytes <= 0 || bytes >= source_limit) {
        fprintf(stderr, "Generated C fixture source is %ld bytes (limit: %ld)\n", bytes, source_limit);
    }
    REQUIRE(bytes > 0 && bytes < source_limit);
    int closed = fclose(source);
    source = NULL;
    REQUIRE(!closed);
    const char **argv = NULL;
    for (int i = 0; export_compiler[i]; i++) {
        apush(argv, export_compiler[i]);
    }
    const char *arguments[] = { "-std=c11", "-Werror", "-shared", "-fPIC",
                                "-I",       includes,  input,     debug,
                                "-o",       output,    NULL };
    for (int i = 0; i < (int)(sizeof arguments / sizeof *arguments); i++) {
        apush(argv, arguments[i]);
    }
    int compiled = tool_run(argv, directory, log);
    afree(argv);
    REQUIRE(compiled == 0);
    library = dlopen(output, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "C export load: %s\n", dlerror());
    }
    REQUIRE(library);
    return true;
}
TEST_CASE(generated_c_preserves_aliased_operands_and_debug_continuations)
{
    REQUIRE(begin_export());
    for (int i = 0; i < ALIAS_FIXTURES; i++) {
        REQUIRE(fixture(i));
    }
    REQUIRE(pause_fixture());
    REQUIRE(borrow_fixture(false));
    REQUIRE(borrow_fixture(true));
    for (int i = 0; i < QUERY_FIXTURES; i++) {
        REQUIRE(query_fixture(i));
    }
    for (int field = 0; field < READ_FIXTURES; field++) {
        REQUIRE(read_fixture(field));
    }
    for (int field = 0; field < WRITE_FIXTURES; field++) {
        REQUIRE(write_fixture(field));
    }
    for (int i = 0; i < CAST_FIXTURES; i++) {
        REQUIRE(cast_fixture(i));
    }
    for (int i = 0; i < INTEGER_FIXTURES; i++) {
        REQUIRE(integer_fixture(i));
    }
    REQUIRE(compile_export(128 * 1024));
    for (int i = 0; i < ALIAS_FIXTURES; i++) {
        char name[40];
        snprintf(name, sizeof name, "export_alias_%d", i);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        pin_type type = i < 6 ? PIN_I32 : PIN_I64;
        int64_t minimum = type == PIN_I32 ? INT32_MIN : INT64_MIN;
        const int64_t samples[][2] = { { 10, 3 },
                                       { -10, 3 },
                                       { 10, -3 },
                                       { minimum, -1 },
                                       { 1, 0 } };
        for (int sample = 0; sample < 5; sample++) {
            for (int inspection = 0; inspection < 2; inspection++) {
                pin_value inputs[] = { integer(type, samples[sample][0]),
                                       integer(type, samples[sample][1]) };
                pin_value actual[] = { integer(type, 77), integer(type, 88) };
                pin_value expected[] = { integer(type, 77), integer(type, 88) };
                behavior_function_context vm = { .arguments = inputs,
                                                 .argument_count = 2,
                                                 .results = expected,
                                                 .budget = 32,
                                                 .inspect = inspect };
                behavior_function_context compiled_context = {
                    .arguments = inputs,
                    .argument_count = 2,
                    .results = actual,
                    .budget = 32,
                    .inspect = inspection ? inspect : NULL
                };
                REQUIRE(behavior_function_bind(functions[i], &vm, NULL, 0));
                int reference = behavior_function_run(functions[i], &vm, false);
                int status = generated(&compiled_context);
                REQUIRE(status == reference);
                REQUIRE(status == FLOW_OK);
                REQUIRE(number(actual[0]) == number(expected[0]));
                REQUIRE(number(actual[1]) == number(expected[1]));
                /* The minimum / -1 wraps to the minimum with remainder 0;
                 * x / 0 is 0 with remainder x. */
                int64_t x = samples[sample][0], y = samples[sample][1];
                int64_t quotient = !y      ? 0
                                 : y == -1 ? (x == minimum ? minimum : -x)
                                           : x / y;
                int64_t remainder = !y ? x : y == -1 ? 0 : x % y;
                bool modulo = i % 6 >= 3;
                int64_t primary = modulo ? remainder : quotient;
                int64_t secondary = i % 3 == 2 ? primary
                                               : (modulo ? quotient : remainder);
                REQUIRE(number(actual[0]) == primary);
                REQUIRE(number(actual[1]) == secondary);
            }
        }
    }
    REQUIRE(resumes_exported_pause());
    REQUIRE(borrows_exported_plain_arrays());
    REQUIRE(exported_call_snapshots_keep_the_standalone_abi());
    REQUIRE(exported_query_variables_preserve_tuple_and_failure_contract());
    REQUIRE(reads_exported_current_world_scalars());
    REQUIRE(writes_exported_current_world_scalars());
    REQUIRE(exported_casts_match_the_vm());
    REQUIRE(exported_integer_width_edges());
    return true;
}
TEST_CASE(generated_c_shifts_every_bit_out_past_the_width)
{
    REQUIRE(begin_export());
    for (int i = 0; i < INTEGER_FIXTURES; i++) {
        REQUIRE(shift_fixture(i));
    }
    /* Nine fixed functions share this compiler child. The binary fixture adds
     * two VM and two generated-C calls, each with a 64-instruction budget. */
    REQUIRE(binary_text_fixture());
    REQUIRE(compile_export(64 * 1024));
    REQUIRE(exported_shifts_past_the_width_give_zero_or_sign());
    REQUIRE(exported_binary_text_matches_the_vm());
    return true;
}
void test_tool_c_export()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(generated_c_preserves_aliased_operands_and_debug_continuations);
    RUN_TEST_CASE(generated_c_shifts_every_bit_out_past_the_width);
    pu_clear_setup();
}
