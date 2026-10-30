#include "package_compiler_chibi.h"

#include "authoring/native_bindings.h"
#include "authoring/source_collection.h"
#include "backend.h"
#include "compiler_fixture.h"
#include "compositional_records.h"
#include "compositional_records_source.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/module_internal.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "float_conversion_cases.h"
#include "language/lower.h"
#include "language/semantic.h"
#include "package_compiler_chibi_source.h"
#include "runtime/arithmetic_definition.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/math_definition.h"
#include "runtime/native_capabilities.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "runtime/text_format.h"
#include "runtime/vectors_definition.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "toolchain/compiler_target.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy.h"
#include "types/map.h"
#include "types/text.h"
#include "types/union.h"
#include "types/value_document.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__) && defined(__x86_64__)
#include "host/native_libraries.h"
#include "host/process.h"
#include "host/stack_overflow.h"
#include "native_calls.h"

#include <dlfcn.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
extern char **environ;
#endif

#ifndef CHIBI_CORE_MATH_ROOT
#define CHIBI_CORE_MATH_ROOT "packages/core/math"
#endif

/* The authored package compiler, prepared by stage zero, compiles each sample
 * package, with the packages it imports, to a closure against the natives the
 * compiler host links. A fresh runtime host loads it and calls every function
 * on bytecode and SLJIT, and stage zero's own closure of the same packages
 * must give the same JSON result. Rejections report their exact message and
 * position. */
static const char PACKAGE[] = "project:package_compiler";
enum
{
    CALLS = 14,
    SAMPLE_INPUTS = 4,
    ARGUMENTS = 12,
    /* Compile's debug flag, then the six that describe the target. */
    DEBUG_ARGUMENT = 5,
    TARGET_ARGUMENT = 6,
    COMPILE_BUDGET = 20000000,
    SELF_BUDGET = 2000000000,
    TARGET_BUDGET = 100000
};
/* A NULL expected result means the call must fail. */
typedef struct
{
    const char *function, *arguments, *expected;
    const char *diagnostic;
    /* Instructions the call may run; TARGET_BUDGET when zero. */
    int budget;
} sample_call;
/* Focused selectors group unions, constants, patterns and integer types
 * (TYPE_SAMPLES); imports, core-pack calls, moves, reads in place and array
 * methods (IMPORT_SAMPLES, run in Release); and maps, references, arrays and
 * ranges (COLLECTION_SAMPLES). */
typedef enum
{
    BASE_SAMPLES,
    TYPE_SAMPLES,
    IMPORT_SAMPLES,
    COLLECTION_SAMPLES,
    FALLIBLE_SAMPLES,
    MATH_SAMPLES
} sample_group;
/* `source` is package facts, and `more` another file of it when present;
 * `library` is package lib when present, and `core` adds core:strings.
 * `collection_source` and `collection_dependency` add core:host_only and
 * core:host_dependency; `project_modules` checks that the emitted closure
 * contains exactly that many project modules and no collection rows. A
 * `game` sample's target links the core pack a game links, which declares
 * assert; preparing every sample with it would cost each selector seconds. An
 * `updates` sample's `Build(count)` updates a local array in place: doubling
 * its count at most doubles the values it copies, where copying the array on
 * each update would quadruple them. A `boxed` sample's Boxed functions read
 * only arrays that are never packed, and its Packed functions arrays that may
 * be. A `moves` sample calls no native: its Moved functions take fresh values
 * into locals and returns without copying them, and its Copied function copies
 * an array's element. An `aliases` sample's Aliased functions read records and
 * arrays nothing changes in place, and its CopiedWalk copies the array its loop
 * changes. A `variants` sample calls no native: its Kept functions return
 * variants that keep their payload values, and its Copied function builds one
 * it keeps after a change. A `built` sample calls no native: its Built
 * functions build the arrays they return in their caller's storage, and its
 * Copied function, which assigns its array, does not. A `lends` sample's Lent
 * functions pass locals to natives and keep or return natives' results
 * without a copy,
 * and its Copied function copies the element it passes. A `drops` sample's
 * Plain functions pass temporaries that hold no collection and drop none, and
 * its Held functions drop the temporary arrays they pass. A `debug_metadata`
 * sample compares source points and watches with stage zero on both compiler
 * backends. A `traced` sample's calls take no arguments and deliver the
 * watches stage zero's do, in its order and with its values; a traced
 * `c_string_probe` sample's calls run only that way, because the compiler
 * host does not link the probe natives, and return int. */
typedef struct
{
    const char *name, *source, *more, *library;
    const char *collection_source, *collection_dependency;
    sample_group group;
    bool core, math, updates, boxed, game, moves, aliases, variants, built,
        lends, drops, same_rejection, c_string_probe, host_effects,
        update_order, debug_metadata, traced, char_checks;
    /* Its calls run with the probe natives linked, take no arguments and
     * return one int. */
    bool probe_calls;
    sample_call calls[CALLS];
    const char *message;
    const char *oracle_message, *oracle_package;
    int line, column, project_modules;
    int oracle_line, oracle_column;
    int specializations;
} sample;
static const char *cstring_probe(arena *storage, const pin_value *arguments, int, pin_value *result)
{
    const char *value = NULL;
    size_t nul = 0;
    const char *error = pin_text_cstring(storage, arguments[0], &value, &nul);
    if (error) {
        return error;
    }
    result->i = (int)strlen(value);
    return NULL;
}
static const char *integer_probe(arena *, const pin_value *arguments, int, pin_value *result)
{
    result->i = arguments[0].i;
    return NULL;
}
static const native_parameter_definition INTEGER_INPUT[] = {
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
};
static const native_parameter_definition CSTRING_INPUT[] = {
    { .name = "value",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .c_string = true },
};
/* The second parameter deliberately carries the C-string boundary. */
static const native_parameter_definition CSTRING_PAIR_INPUT[] = {
    { .name = "prefix", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
    { .name = "value",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .c_string = true },
};
static const char *cstring_pair(arena *storage, const pin_value *arguments, int count, pin_value *result)
{
    return cstring_probe(storage, arguments + 1, count - 1, result);
}
/* Overloads that report which one ran: number or f64, and an int and an f64
 * in either order. */
static const char *first_kernel(arena *, const pin_value *, int, pin_value *result)
{
    result->i = 1;
    return NULL;
}
static const char *second_kernel(arena *, const pin_value *, int, pin_value *result)
{
    result->i = 2;
    return NULL;
}
static const native_parameter_definition NUMBER_INPUT[] = {
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_F32, .type_name = "number" },
};
static const native_parameter_definition DOUBLE_INPUT[] = {
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_F64, .type_name = "f64" },
};
static const native_parameter_definition INTEGER_DOUBLE_INPUT[] = {
    { .name = "first", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
    { .name = "second", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_F64, .type_name = "f64" },
};
static const native_parameter_definition DOUBLE_INTEGER_INPUT[] = {
    { .name = "first", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_F64, .type_name = "f64" },
    { .name = "second", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
};
static const native_parameter_definition CSTRING_OUTPUT[] = {
    { .name = "length", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
};
/* Borrowed native result banks exercise tuple copies without authored named
 * collection results, which stage one does not support. */
static const char *sequence_pair_probe(void *, const pin_value *, pin_value *results)
{
    static int items[] = { 3, 4 };
    static pin_array values = {
        .element_type = PIN_I32,
        .count = 2,
        .capacity = 2,
        .data = items,
        .stride = sizeof *items,
    };
    results[0].ptr = &values;
    results[1].i = 9;
    return NULL;
}
static const native_parameter_definition SEQUENCE_PAIR_OUTPUTS[] = {
    { .name = "values",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .container = PIN_CONTAINER_ARRAY,
      .type_name = "int" },
    { .name = "stamp", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
};
#define PROBE_DEFINITION(CANONICAL, SOURCE, INPUT, PURE)                       \
    { .canonical_name = CANONICAL,                                             \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .inputs = INPUT,                                                         \
      .input_count = sizeof INPUT / sizeof *INPUT,                             \
      .outputs = CSTRING_OUTPUT,                                               \
      .output_count = 1,                                                       \
      .completion_output = -1,                                                 \
      .pure = PURE }
static const native_capability_definition CSTRING_DEFINITIONS[] = {
    PROBE_DEFINITION("CString Probe", "cstring_probe", CSTRING_INPUT, true),
    { .canonical_name = "CString Pair",
      .source_name = "cstring_pair",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = CSTRING_PAIR_INPUT,
      .input_count = 2,
      .outputs = CSTRING_OUTPUT,
      .output_count = 1,
      .completion_output = -1,
      .pure = true },
    PROBE_DEFINITION("Qualified Probe", "native.probe", CSTRING_INPUT, false),
    PROBE_DEFINITION("Qualified Length", "native.length", CSTRING_INPUT, true),
    PROBE_DEFINITION("Qualified Text Overload", "native.overload", CSTRING_INPUT, true),
    PROBE_DEFINITION("Qualified Integer Overload", "native.overload", INTEGER_INPUT, true),
    PROBE_DEFINITION("Qualified Ambiguous One", "native.ProbeOne", CSTRING_INPUT, true),
    PROBE_DEFINITION("Qualified Ambiguous Two", "native.probe_one", CSTRING_INPUT, true),
    { .canonical_name = "Sequence Pair Probe",
      .source_name = "Multi",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .outputs = SEQUENCE_PAIR_OUTPUTS,
      .output_count = 2,
      .completion_output = -1,
      .pure = true },
    PROBE_DEFINITION("Number Kernel", "native.kernel", NUMBER_INPUT, true),
    PROBE_DEFINITION("Double Kernel", "native.kernel", DOUBLE_INPUT, true),
    PROBE_DEFINITION("Integer Double Pair", "native.pair", INTEGER_DOUBLE_INPUT, true),
    PROBE_DEFINITION("Double Integer Pair", "native.pair", DOUBLE_INTEGER_INPUT, true),
};
#undef PROBE_DEFINITION
static const node_host_call CSTRING_CALL = { .value = cstring_probe };
static const node_host_call CSTRING_PAIR_CALL = { .value = cstring_pair };
static const node_host_call INTEGER_CALL = { .value = integer_probe };
static const node_host_call SEQUENCE_PAIR_CALL = { .invoke = sequence_pair_probe };
static const node_host_call FIRST_CALL = { .value = first_kernel };
static const node_host_call SECOND_CALL = { .value = second_kernel };
static const node_access CSTRING_ACCESS = { .flags = NODE_EFFECT_PURE };
static const node_access HOST_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define PROBE_BINDING(CANONICAL, CALL, ACCESS)                                 \
    {                                                                          \
        .canonical_name = CANONICAL, .implementation = {                       \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding CSTRING_BINDINGS[] = {
    PROBE_BINDING("CString Probe", CSTRING_CALL, CSTRING_ACCESS),
    PROBE_BINDING("CString Pair", CSTRING_PAIR_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Probe", CSTRING_CALL, HOST_ACCESS),
    PROBE_BINDING("Qualified Length", CSTRING_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Text Overload", CSTRING_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Integer Overload", INTEGER_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Ambiguous One", CSTRING_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Qualified Ambiguous Two", CSTRING_CALL, CSTRING_ACCESS),
    { .canonical_name = "Sequence Pair Probe",
      .implementation = { .call = &SEQUENCE_PAIR_CALL, .access = &CSTRING_ACCESS } },
    PROBE_BINDING("Number Kernel", FIRST_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Double Kernel", SECOND_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Integer Double Pair", FIRST_CALL, CSTRING_ACCESS),
    PROBE_BINDING("Double Integer Pair", SECOND_CALL, CSTRING_ACCESS),
};
#undef PROBE_BINDING
static const native_definition_pack CSTRING_PACK = {
    .definitions = CSTRING_DEFINITIONS,
    .definition_count = sizeof CSTRING_DEFINITIONS / sizeof *CSTRING_DEFINITIONS,
    .bindings = CSTRING_BINDINGS,
    .binding_count = sizeof CSTRING_BINDINGS / sizeof *CSTRING_BINDINGS,
};
typedef struct
{
    int first, second;
    double gain;
    bool enabled;
    uint64_t count, target;
    char message[8], ctext[8], name[8];
    size_t message_length, ctext_length, name_length;
} action_record;
static action_record action_records[8];
static int action_trace[32], action_record_count, action_trace_count;
static int action_kind_calls[5];
static int action_number_calls[2], action_default_calls[2];
static const pin_string ACTION_TEXT = { .bytes = "a\0\xff"
                                                 "b",
                                        .length = 4 },
                        ACTION_CTEXT = { .bytes = "okay", .length = 4 },
                        ACTION_NAME = { .bytes = "named", .length = 5 };
static const char *action_probe(void *, const pin_value *arguments, pin_value *results)
{
    pin_string message = pin_text(arguments[5]), ctext = pin_text(arguments[6]),
               name = pin_text(arguments[8]);
    if (arguments[0].type != PIN_I32 || arguments[1].type != PIN_I32 ||
        arguments[2].type != PIN_F64 || arguments[3].type != PIN_BOOL ||
        arguments[4].type != PIN_U64 || arguments[5].type != PIN_STR ||
        arguments[6].type != PIN_STR || arguments[7].type != PIN_HANDLE ||
        !arguments[7].type_name || strcmp(arguments[7].type_name, "fixture.owner") ||
        arguments[8].type != PIN_NAME || results[0].type != PIN_I32 ||
        results[0].container != PIN_CONTAINER_SINGLE ||
        message.length > sizeof action_records[0].message ||
        ctext.length > sizeof action_records[0].ctext ||
        name.length > sizeof action_records[0].name) {
        fprintf(
            stderr,
            "action headers: inputs %d/%d/%d/%d/%d/%d/%d/%d/%d, target %s, "
            "result %d/%d, bytes %u/%u/%u\n",
            arguments[0].type,
            arguments[1].type,
            arguments[2].type,
            arguments[3].type,
            arguments[4].type,
            arguments[5].type,
            arguments[6].type,
            arguments[7].type,
            arguments[8].type,
            arguments[7].type_name ? arguments[7].type_name : "none",
            results[0].type,
            results[0].container,
            message.length,
            ctext.length,
            name.length
        );
        return "action argument or result header differs";
    }
    assert(action_record_count < NATIVE_COUNT(action_records));
    assert(action_trace_count < NATIVE_COUNT(action_trace));
    action_record *record = &action_records[action_record_count++];
    *record = (action_record){ .first = arguments[0].i,
                               .second = arguments[1].i,
                               .gain = arguments[2].d,
                               .enabled = arguments[3].b,
                               .count = arguments[4].u64,
                               .target = arguments[7].u64,
                               .message_length = message.length,
                               .ctext_length = ctext.length,
                               .name_length = name.length };
    memcpy(record->message, message.bytes, message.length);
    memcpy(record->ctext, ctext.bytes, ctext.length);
    memcpy(record->name, name.bytes, name.length);
    action_trace[action_trace_count++] = 1000 + record->first;
    results[0].i = record->first * 100 + record->second;
    return NULL;
}
static const char *action_mark(void *, const pin_value *arguments, pin_value *results)
{
    assert(action_trace_count < NATIVE_COUNT(action_trace));
    action_trace[action_trace_count++] = arguments[0].i;
    results[0].i = arguments[0].i;
    return NULL;
}
static const char *action_zero(void *, const pin_value *, pin_value *results)
{
    assert(action_trace_count < NATIVE_COUNT(action_trace));
    action_trace[action_trace_count++] = 3000;
    results[0].u64 = 0;
    return NULL;
}
static const char *action_kind_function(void *, const pin_value *arguments, pin_value *results)
{
    action_kind_calls[0]++;
    results[0].i = arguments[0].i + 40;
    return NULL;
}
static const char *action_kind_action(void *, const pin_value *arguments, pin_value *results)
{
    if (arguments[0].type != PIN_STR ||
        !pin_text_equal(pin_text(arguments[0]), (pin_string){ .bytes = "text", .length = 4 }) ||
        results[0].type != PIN_I32) {
        return "qualified action argument or result differs";
    }
    action_kind_calls[1]++;
    results[0].i = 99;
    return NULL;
}
static const char *action_duplicate_int(void *, const pin_value *arguments, pin_value *results)
{
    if (arguments[0].type != PIN_I32 || arguments[0].i != 3 ||
        results[0].type != PIN_I32) {
        return "qualified integer action argument or result differs";
    }
    action_kind_calls[3]++;
    results[0].i = 99;
    return NULL;
}
static const char *action_duplicate_text(void *service, const pin_value *arguments, pin_value *results)
{
    const char *error = action_kind_action(service, arguments, results);
    if (error) {
        return error;
    }
    action_kind_calls[1]--;
    action_kind_calls[4]++;
    return NULL;
}
static const char *action_assert(void *, const pin_value *, pin_value *)
{
    action_kind_calls[2]++;
    return NULL;
}
static const char *action_tiny(void *, const pin_value *arguments, pin_value *results)
{
    if (arguments[0].type != PIN_I8 || results[0].type != PIN_I32) {
        return "tiny numeric overload header differs";
    }
    action_number_calls[0]++;
    results[0].i = arguments[0].i8;
    return NULL;
}
static const char *action_real(void *, const pin_value *arguments, pin_value *results)
{
    if (arguments[0].type != PIN_F32 || arguments[0].f != 256 ||
        results[0].type != PIN_I32) {
        return "real numeric overload value or header differs";
    }
    action_number_calls[1]++;
    results[0].i = (int)arguments[0].f;
    return NULL;
}
/* default_pair's overloads: an int then an f64 that defaults to 5, and the
 * reverse. Each reports which one ran. */
static const char *action_default_first(void *, const pin_value *arguments, pin_value *results)
{
    if (arguments[0].type != PIN_I32 || arguments[1].type != PIN_F64 ||
        arguments[1].d != 5) {
        return "default pair's first overload value or header differs";
    }
    action_default_calls[0]++;
    results[0].i = 1;
    return NULL;
}
static const char *action_default_second(void *, const pin_value *, pin_value *results)
{
    action_default_calls[1]++;
    results[0].i = 2;
    return NULL;
}
static const char *action_ambiguous(void *, const pin_value *arguments, pin_value *)
{
    assert(action_trace_count < NATIVE_COUNT(action_trace));
    action_trace[action_trace_count++] = 4000 + 10 * arguments[0].i +
                                         arguments[1].i;
    return NULL;
}
static const char *action_aggregate(void *, const pin_value *arguments, pin_value *)
{
    const pin_array *array = arguments[0].ptr;
    if (arguments[0].type != PIN_I32 || arguments[0].container != PIN_CONTAINER_ARRAY ||
        !array || array->count != 2) {
        return "action aggregate input differs";
    }
    assert(action_trace_count < NATIVE_COUNT(action_trace));
    action_trace[action_trace_count++] = 5000 + pin_array_get(array, 0).i +
                                         pin_array_get(array, 1).i;
    return NULL;
}
static const char *action_fail(const behavior_function_context *context, void *, const pin_value *, pin_value *)
{
    if (!context || !context->value_storage || !context->owner) {
        return "action context storage or owner is missing";
    }
    assert(action_trace_count < NATIVE_COUNT(action_trace));
    action_trace[action_trace_count++] = 6000;
    return "fixture action failed";
}
static int action_command(void *, const char *, const pin_value *, int)
{
    return 0;
}
static const node_host_call ACTION_CALL = { .invoke = action_probe },
                            ACTION_MARK = { .invoke = action_mark },
                            ACTION_ZERO = { .invoke = action_zero },
                            ACTION_KIND_FUNCTION = { .invoke = action_kind_function },
                            ACTION_KIND_ACTION = { .invoke = action_kind_action },
                            ACTION_DUPLICATE_INT = { .invoke = action_duplicate_int },
                            ACTION_DUPLICATE_TEXT = { .invoke = action_duplicate_text },
                            ACTION_ASSERT = { .invoke = action_assert },
                            ACTION_TINY = { .invoke = action_tiny },
                            ACTION_REAL = { .invoke = action_real },
                            ACTION_DEFAULT_FIRST = { .invoke = action_default_first },
                            ACTION_DEFAULT_SECOND = { .invoke = action_default_second },
                            ACTION_AMBIGUOUS = { .invoke = action_ambiguous },
                            ACTION_AGGREGATE = { .invoke = action_aggregate },
                            ACTION_FAIL = { .context = action_fail };
static const node_command ACTION_COMMAND = { .invoke = action_command };
static const native_parameter_definition ACTION_INPUT[] = {
    CORE_EXEC_INPUTS(NATIVE_PORT){ .name = "First",
                                   .kind = NATIVE_PARAMETER_DATA,
                                   .storage = PIN_I32,
                                   .type_name = "int" },
    NATIVE_PORT(
        "Second",
        NATIVE_PARAMETER_DATA,
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        0,
        0,
        SCRIPT_REFERENCE_NONE,
        false,
        ((pin_value){ .i = 7 })
    )
        NATIVE_PORT(
            "Gain",
            NATIVE_PARAMETER_DATA,
            PIN_F64,
            "f64",
            PIN_CONTAINER_SINGLE,
            0,
            0,
            SCRIPT_REFERENCE_NONE,
            false,
            ((pin_value){ .d = -2.5 })
        )
            NATIVE_PORT(
                "Enabled",
                NATIVE_PARAMETER_DATA,
                PIN_BOOL,
                "bool",
                PIN_CONTAINER_SINGLE,
                0,
                0,
                SCRIPT_REFERENCE_NONE,
                false,
                ((pin_value){ .b = true })
            )
                NATIVE_PORT(
                    "Count",
                    NATIVE_PARAMETER_DATA,
                    PIN_U64,
                    "u64",
                    PIN_CONTAINER_SINGLE,
                    0,
                    0,
                    SCRIPT_REFERENCE_NONE,
                    false,
                    ((pin_value){ .u64 = UINT64_C(0xfedcba9876543210) })
                )
                    NATIVE_PORT(
                        "Message",
                        NATIVE_PARAMETER_DATA,
                        PIN_STR,
                        "string",
                        PIN_CONTAINER_SINGLE,
                        0,
                        0,
                        SCRIPT_REFERENCE_NONE,
                        false,
                        ((pin_value){ .text = &ACTION_TEXT })
                    )
                        NATIVE_CSTRING_PORT(
                            "CText",
                            NATIVE_PARAMETER_DATA,
                            PIN_STR,
                            "string",
                            PIN_CONTAINER_SINGLE,
                            0,
                            0,
                            SCRIPT_REFERENCE_NONE,
                            false,
                            ((pin_value){ .text = &ACTION_CTEXT })
                        )
                            NATIVE_PORT(
                                "Target",
                                NATIVE_PARAMETER_DATA,
                                PIN_HANDLE,
                                "fixture.owner",
                                PIN_CONTAINER_SINGLE,
                                0,
                                0,
                                SCRIPT_REFERENCE_NONE,
                                true,
                                ((pin_value){ .u64 = 0 })
                            )
                                NATIVE_PORT(
                                    "Name",
                                    NATIVE_PARAMETER_DATA,
                                    PIN_NAME,
                                    "name",
                                    PIN_CONTAINER_SINGLE,
                                    0,
                                    0,
                                    SCRIPT_REFERENCE_NONE,
                                    false,
                                    ((pin_value){ .str = "named" })
                                )
};
static const native_parameter_definition ACTION_OUTPUT[] = {
    CORE_THEN_OUTPUTS(NATIVE_PORT){ .name = "value",
                                    .kind = NATIVE_PARAMETER_DATA,
                                    .storage = PIN_I32,
                                    .type_name = "int" },
};
static const native_parameter_definition ACTION_EXEC[] = { CORE_EXEC_INPUTS(NATIVE_PORT) },
                                         ACTION_THEN[] = { CORE_THEN_OUTPUTS(NATIVE_PORT) },
                                         ACTION_ENTITY[] = { CORE_DATA_PORT(NATIVE_PORT, "Value", PIN_HANDLE, "ecs.entity") },
                                         ACTION_AMBIGUOUS_INPUT[] = {
    CORE_EXEC_INPUTS(NATIVE_PORT)
    { .name = "FirstValue", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
    { .name = "first_value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
}, ACTION_AGGREGATE_INPUT[] = {
    CORE_EXEC_INPUTS(NATIVE_PORT)
    NATIVE_PORT("Values", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_ARRAY, 0, 0,
        SCRIPT_REFERENCE_NONE, false, ((pin_value){ .container = PIN_CONTAINER_ARRAY }))
}, ACTION_TEXT_INPUT[] = {
    CORE_EXEC_INPUTS(NATIVE_PORT)
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_STR, .type_name = "string" },
}, ACTION_INTEGER_INPUT[] = {
    CORE_EXEC_INPUTS(NATIVE_PORT)
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
}, ACTION_ASSERT_INPUT[] = {
    CORE_EXEC_INPUTS(NATIVE_PORT)
    { .name = "Condition", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_BOOL, .type_name = "bool" },
}, ACTION_TINY_INPUT[] = {
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I8, .type_name = "i8" },
}, ACTION_REAL_INPUT[] = {
    { .name = "value", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_F32, .type_name = "number" },
}, ACTION_DEFAULT_FIRST_INPUT[] = {
    { .name = "first", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_I32, .type_name = "int" },
    { .name = "second",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_F64,
      .type_name = "f64",
      .has_default = true,
      .default_value = { .type = PIN_F64, .d = 5 } },
}, ACTION_DEFAULT_SECOND_INPUT[] = {
    { .name = "first", .kind = NATIVE_PARAMETER_DATA, .storage = PIN_F64, .type_name = "f64" },
    { .name = "second",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .has_default = true,
      .default_value = { .type = PIN_I32, .i = 5 } },
};
static const native_capability_definition ACTION_DEFINITIONS[] = {
    { .canonical_name = "Fixture Synchronous Action",
      .source_name = "sync_probe",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_INPUT,
      .input_count = NATIVE_COUNT(ACTION_INPUT),
      .outputs = ACTION_OUTPUT,
      .output_count = NATIVE_COUNT(ACTION_OUTPUT),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "Fixture Action Tick",
      .source_name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .completion_output = -1 },
    { .canonical_name = "Fixture Action Mark",
      .source_name = "action_mark",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = INTEGER_INPUT,
      .input_count = NATIVE_COUNT(INTEGER_INPUT),
      .outputs = CSTRING_OUTPUT,
      .output_count = NATIVE_COUNT(CSTRING_OUTPUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "Fixture Action Zero",
      .source_name = "zero_entity",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .outputs = ACTION_ENTITY,
      .output_count = NATIVE_COUNT(ACTION_ENTITY),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "Fixture Action Ambiguous",
      .source_name = "ambiguous_probe",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_AMBIGUOUS_INPUT,
      .input_count = NATIVE_COUNT(ACTION_AMBIGUOUS_INPUT),
      .outputs = ACTION_THEN,
      .output_count = NATIVE_COUNT(ACTION_THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "Fixture Action Aggregate",
      .source_name = "aggregate_probe",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_AGGREGATE_INPUT,
      .input_count = NATIVE_COUNT(ACTION_AGGREGATE_INPUT),
      .outputs = ACTION_THEN,
      .output_count = NATIVE_COUNT(ACTION_THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "Fixture Action Failure",
      .source_name = "fail_probe",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_EXEC,
      .input_count = NATIVE_COUNT(ACTION_EXEC),
      .outputs = ACTION_THEN,
      .output_count = NATIVE_COUNT(ACTION_THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "Fixture Unsupported Action",
      .source_name = "unsupported_probe",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_EXEC,
      .input_count = NATIVE_COUNT(ACTION_EXEC),
      .outputs = ACTION_THEN,
      .output_count = NATIVE_COUNT(ACTION_THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "Fixture Suspending Action",
      .source_name = "suspending_probe",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_EXEC,
      .input_count = NATIVE_COUNT(ACTION_EXEC),
      .outputs = ACTION_THEN,
      .output_count = NATIVE_COUNT(ACTION_THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND },
    { .canonical_name = "fixture.kind_function",
      .source_name = "kind_probe",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = INTEGER_INPUT,
      .input_count = NATIVE_COUNT(INTEGER_INPUT),
      .outputs = CSTRING_OUTPUT,
      .output_count = NATIVE_COUNT(CSTRING_OUTPUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.kind_action",
      .source_name = "kind_probe",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_TEXT_INPUT,
      .input_count = NATIVE_COUNT(ACTION_TEXT_INPUT),
      .outputs = ACTION_OUTPUT,
      .output_count = NATIVE_COUNT(ACTION_OUTPUT),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.duplicate_int",
      .source_name = "duplicate_action",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_INTEGER_INPUT,
      .input_count = NATIVE_COUNT(ACTION_INTEGER_INPUT),
      .outputs = ACTION_OUTPUT,
      .output_count = NATIVE_COUNT(ACTION_OUTPUT),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.duplicate_text",
      .source_name = "duplicate_action",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_TEXT_INPUT,
      .input_count = NATIVE_COUNT(ACTION_TEXT_INPUT),
      .outputs = ACTION_OUTPUT,
      .output_count = NATIVE_COUNT(ACTION_OUTPUT),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.assert_action",
      .source_name = "assert",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_ASSERT_INPUT,
      .input_count = NATIVE_COUNT(ACTION_ASSERT_INPUT),
      .outputs = ACTION_THEN,
      .output_count = NATIVE_COUNT(ACTION_THEN),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.unavailable_function",
      .source_name = "held_probe",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = INTEGER_INPUT,
      .input_count = NATIVE_COUNT(INTEGER_INPUT),
      .outputs = CSTRING_OUTPUT,
      .output_count = NATIVE_COUNT(CSTRING_OUTPUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND },
    { .canonical_name = "fixture.available_action",
      .source_name = "held_probe",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = ACTION_TEXT_INPUT,
      .input_count = NATIVE_COUNT(ACTION_TEXT_INPUT),
      .outputs = ACTION_OUTPUT,
      .output_count = NATIVE_COUNT(ACTION_OUTPUT),
      .completion_output = 0,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.numeric_tiny",
      .source_name = "numeric_choice",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = ACTION_TINY_INPUT,
      .input_count = NATIVE_COUNT(ACTION_TINY_INPUT),
      .outputs = CSTRING_OUTPUT,
      .output_count = NATIVE_COUNT(CSTRING_OUTPUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.numeric_real",
      .source_name = "numeric_choice",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = ACTION_REAL_INPUT,
      .input_count = NATIVE_COUNT(ACTION_REAL_INPUT),
      .outputs = CSTRING_OUTPUT,
      .output_count = NATIVE_COUNT(CSTRING_OUTPUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.default_first",
      .source_name = "default_pair",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = ACTION_DEFAULT_FIRST_INPUT,
      .input_count = NATIVE_COUNT(ACTION_DEFAULT_FIRST_INPUT),
      .outputs = CSTRING_OUTPUT,
      .output_count = NATIVE_COUNT(CSTRING_OUTPUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_HOST },
    { .canonical_name = "fixture.default_second",
      .source_name = "default_pair",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = ACTION_DEFAULT_SECOND_INPUT,
      .input_count = NATIVE_COUNT(ACTION_DEFAULT_SECOND_INPUT),
      .outputs = CSTRING_OUTPUT,
      .output_count = NATIVE_COUNT(CSTRING_OUTPUT),
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_HOST },
};
static const native_definition_binding ACTION_BINDINGS[] = {
    { .canonical_name = "Fixture Synchronous Action",
      .implementation = { .call = &ACTION_CALL, .access = &HOST_ACCESS } },
    { .canonical_name = "Fixture Action Mark",
      .implementation = { .call = &ACTION_MARK, .access = &HOST_ACCESS } },
    { .canonical_name = "Fixture Action Zero",
      .implementation = { .call = &ACTION_ZERO, .access = &HOST_ACCESS } },
    { .canonical_name = "Fixture Action Ambiguous",
      .implementation = { .call = &ACTION_AMBIGUOUS, .access = &HOST_ACCESS } },
    { .canonical_name = "Fixture Action Aggregate",
      .implementation = { .call = &ACTION_AGGREGATE, .access = &HOST_ACCESS } },
    { .canonical_name = "Fixture Action Failure",
      .implementation = { .call = &ACTION_FAIL, .access = &HOST_ACCESS } },
    { .canonical_name = "Fixture Unsupported Action",
      .implementation = { .command = &ACTION_COMMAND, .access = &HOST_ACCESS } },
    { .canonical_name = "Fixture Suspending Action",
      .implementation = { .call = &ACTION_FAIL, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.kind_function",
      .implementation = { .call = &ACTION_KIND_FUNCTION, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.kind_action",
      .implementation = { .call = &ACTION_KIND_ACTION, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.duplicate_int",
      .implementation = { .call = &ACTION_DUPLICATE_INT, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.duplicate_text",
      .implementation = { .call = &ACTION_DUPLICATE_TEXT, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.assert_action",
      .implementation = { .call = &ACTION_ASSERT, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.unavailable_function",
      .implementation = { .call = &ACTION_KIND_FUNCTION, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.available_action",
      .implementation = { .call = &ACTION_KIND_ACTION, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.numeric_tiny",
      .implementation = { .call = &ACTION_TINY, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.numeric_real",
      .implementation = { .call = &ACTION_REAL, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.default_first",
      .implementation = { .call = &ACTION_DEFAULT_FIRST, .access = &HOST_ACCESS } },
    { .canonical_name = "fixture.default_second",
      .implementation = { .call = &ACTION_DEFAULT_SECOND, .access = &HOST_ACCESS } },
};
static const native_global_definition ACTION_GLOBALS[] = {
    { .canonical_name = "Fixture Action Self", .source_name = "self", .type = ACTION_ENTITY },
};
static const native_definition_global_binding ACTION_GLOBAL_BINDINGS[] = {
    { .canonical_name = "Fixture Action Self",
      .instruction = { .op = BF_OWNER, .type = PIN_HANDLE } },
};
static const native_definition_pack ACTION_PACK = {
    .definitions = ACTION_DEFINITIONS,
    .definition_count = NATIVE_COUNT(ACTION_DEFINITIONS),
    .bindings = ACTION_BINDINGS,
    .binding_count = NATIVE_COUNT(ACTION_BINDINGS),
    .global_definitions = ACTION_GLOBALS,
    .global_count = NATIVE_COUNT(ACTION_GLOBALS),
    .global_bindings = ACTION_GLOBAL_BINDINGS,
    .global_binding_count = NATIVE_COUNT(ACTION_GLOBAL_BINDINGS),
};
static const char HOST_ONLY_SOURCE[] = "package host_only\n"
                                       "pub Probe :: () -> int {\n"
                                       "    return host_only_probe();\n"
                                       "}\n";
static const char HOST_DEPENDENCY_SOURCE[] = "package host_dependency\n"
                                             "pub Probe :: () -> int {\n"
                                             "    return host_only_probe();\n"
                                             "}\n";
#define UNCHECKED_COLLECTION_ERROR_BODY                                        \
    "@error pub Failure :: struct { code: int; }\n"                            \
    "pub Read :: () -> Failure? { return {code: 3}; }\n"                       \
    "pub Broken :: () { Read(); }\n"
static const char UNCHECKED_COLLECTION_ERROR_SOURCE
    [] = "package host_only\n" UNCHECKED_COLLECTION_ERROR_BODY;
static const char UNCHECKED_DEPENDENCY_ERROR_SOURCE
    [] = "package host_dependency\n" UNCHECKED_COLLECTION_ERROR_BODY;
static const char UNCHECKED_PROJECT_ERROR_SOURCE
    [] = "package lib\n" UNCHECKED_COLLECTION_ERROR_BODY;
#undef UNCHECKED_COLLECTION_ERROR_BODY
static const sample samples[] = {
    { .name = "unused collection host capability is not required",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .collection_source = HOST_ONLY_SOURCE,
      .project_modules = 1,
      .calls = { { .function = "Value", .arguments = "[]", .expected = "[7]" } } },
    { .name = "unused collection ill-typed body is not checked",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .collection_source = "package host_only\npub Broken :: () -> int { "
                           "return true; }\n",
      .project_modules = 1,
      .calls = { { .function = "Value", .arguments = "[]", .expected = "[7]" } } },
    { .name = "unused collection declaration is not checked",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .collection_source = "package host_only\npub Broken :: (value: Missing) "
                           "-> int { return 0; }\n",
      .project_modules = 1,
      .calls = { { .function = "Value", .arguments = "[]", .expected = "[7]" } } },
    { .name = "unused collection dependencies stay outside closure",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .collection_source = "package host_only\nimport dep "
                           "\"core:host_dependency\"\n"
                           "pub Value :: () -> int { return 2; }\n",
      .collection_dependency = HOST_DEPENDENCY_SOURCE,
      .project_modules = 1,
      .calls = { { .function = "Value", .arguments = "[]", .expected = "[7]" } } },
    { .name = "direct collection import requires its host capability",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport host \"core:host_only\"\n"
                "pub Value :: () -> int { return 7; }\n",
      .collection_source = HOST_ONLY_SOURCE,
      .message = "Unknown function 'host_only_probe'",
      .line = 3,
      .column = 12 },
    { .name = "transitive collection import requires its host capability",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport host \"core:host_only\"\n"
                "pub Value :: () -> int { return 7; }\n",
      .collection_source = "package host_only\nimport dep "
                           "\"core:host_dependency\"\n"
                           "pub Value :: () -> int { return 2; }\n",
      .collection_dependency = HOST_DEPENDENCY_SOURCE,
      .message = "Unknown function 'host_only_probe'",
      .line = 3,
      .column = 12 },
    { .name = "unrelated project package remains checked",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .library = "package lib\npub Probe :: () -> int {\n"
                 "    return host_only_probe();\n}\n",
      .collection_source = "package host_only\npub Value :: () -> int { return "
                           "2; }\n",
      .message = "Unknown function 'host_only_probe'",
      .line = 3,
      .column = 12 },
    { .name = "unrelated project imports retain collection checking",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .library = "package lib\nimport host \"core:host_only\"\n"
                 "pub Value :: () -> int { return 2; }\n",
      .collection_source = HOST_ONLY_SOURCE,
      .message = "Unknown function 'host_only_probe'",
      .line = 3,
      .column = 12 },
    { .name = "independent project package remains in emitted closure",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .library = "package lib\npub Independent :: () -> int { return 9; }\n",
      .collection_source = HOST_ONLY_SOURCE,
      .project_modules = 2,
      .calls = { { .function = "Value", .arguments = "[]", .expected = "[7]" } } },
    { .name = "unused collection unchecked error stays outside closure",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .collection_source = UNCHECKED_COLLECTION_ERROR_SOURCE,
      .project_modules = 1,
      .calls = { { .function = "Value", .arguments = "[]", .expected = "[7]" } } },
    { .name = "direct collection import checks error obligations",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport host \"core:host_only\"\n"
                "pub Value :: () -> int { return 7; }\n",
      .collection_source = UNCHECKED_COLLECTION_ERROR_SOURCE,
      .message = "Error result is ignored on a reachable path; check, report, store or return it",
      .oracle_package = "core:host_only",
      .same_rejection = true },
    { .name = "transitive collection import checks error obligations",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport host \"core:host_only\"\n"
                "pub Value :: () -> int { return 7; }\n",
      .collection_source = "package host_only\nimport dep \"core:host_dependency\"\n"
                           "pub Value :: () -> int { return 2; }\n",
      .collection_dependency = UNCHECKED_DEPENDENCY_ERROR_SOURCE,
      .message = "Error result is ignored on a reachable path; check, report, store or return it",
      .oracle_package = "core:host_dependency",
      .same_rejection = true },
    { .name = "independent project package checks error obligations",
      .group = IMPORT_SAMPLES,
      .source = "package facts\npub Value :: () -> int { return 7; }\n",
      .library = UNCHECKED_PROJECT_ERROR_SOURCE,
      .message = "Error result is ignored on a reachable path; check, report, store or return it",
      .oracle_package = "project:lib",
      .same_rejection = true },
    { .name = "active collection executes checked error results",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport host \"core:host_only\"\n"
                "pub Value :: () -> int { return host.Value(); }\n",
      .collection_source = "package host_only\n@error pub Failure :: struct { code: int; }\n"
                           "Read :: () -> (value: int, error: Failure?) { return(7, {code: 3}); }\n"
                           "pub Value :: () -> int { read := Read(); if read.error { return read.value + read.error.code; } return read.value; }\n",
      .calls = { { .function = "Value", .arguments = "[]", .expected = "[10]" } } },
#include "fallible_chibi_samples.h"
    { .name = "char parameters locals and named results keep their type",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Echo :: (value: char) -> char { return value; }\n"
                "Named :: (n: int) -> (scalar: char) { scalar = char(n); "
                "return; }\n"
                "RoundTrip :: (n: int) -> int { value: char = char(n); "
                "kept := Echo(value); return int(kept); }\n"
                "NamedRead :: (n: int) -> i64 { return Named(n).scalar; }\n"
                "ImplicitWide :: (n: int) -> u32 { return Echo(char(n)); }\n"
                "ExactDecimal :: () -> int { return int(char(65.0)); }\n"
                "ExactInteger :: () -> int { return int(char((+(65)))); }\n"
                "PlainInt :: (n: int) -> int { return n; }",
      .calls = { { .function = "RoundTrip", .arguments = "[0]", .expected = "[0]" },
                 { .function = "RoundTrip", .arguments = "[55295]", .expected = "[55295]" },
                 { .function = "RoundTrip", .arguments = "[57344]", .expected = "[57344]" },
                 { .function = "RoundTrip", .arguments = "[1114111]", .expected = "[1114111]" },
                 { .function = "NamedRead", .arguments = "[128008]", .expected = "[128008]" },
                 { .function = "ImplicitWide", .arguments = "[1114111]", .expected = "[1114111]" },
                 { .function = "ExactDecimal", .arguments = "[]", .expected = "[65]" },
                 { .function = "ExactInteger", .arguments = "[]", .expected = "[65]" },
                 { .function = "PlainInt", .arguments = "[-1]", .expected = "[-1]" } } },
    { .name = "char validates dynamic scalars before narrowing",
      .group = TYPE_SAMPLES,
      .char_checks = true,
      .source = "package facts\n"
                "Signed :: (n: i64) -> int { return int(char(n)); }\n"
                "Unsigned :: (n: u64) -> int { return int(char(n)); }\n"
                "Narrow :: (n: int) -> int { return int(char(n)); }",
      .calls = { { .function = "Narrow", .arguments = "[-1]", .diagnostic = "Value does not fit in char" },
                 { .function = "Narrow", .arguments = "[55296]", .diagnostic = "Value does not fit in char" },
                 { .function = "Narrow", .arguments = "[57343]", .diagnostic = "Value does not fit in char" },
                 { .function = "Narrow", .arguments = "[1114112]", .diagnostic = "Value does not fit in char" },
                 { .function = "Signed", .arguments = "[4294967296]", .diagnostic = "Value does not fit in char" },
                 { .function = "Signed",
                   .arguments = "[-9223372036854775808]",
                   .diagnostic = "Value does not fit in char" },
                 { .function = "Unsigned",
                   .arguments = "[18446744073709551615]",
                   .diagnostic = "Value does not fit in char" },
                 { .function = "Unsigned",
                   .arguments = "[4294967296]",
                   .diagnostic = "Value does not fit in char" },
                 { .function = "Signed", .arguments = "[1114111]", .expected = "[1114111]" },
                 { .function = "Unsigned", .arguments = "[0]", .expected = "[0]" } } },
    { .name = "public char calls and UTF8 encode",
      .group = TYPE_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Encode :: (n: int) -> u8[] { return lib.Encode(char(n)); }\n"
                "NamedRead :: (n: int) -> int { return "
                "int(lib.Result(char(n)).scalar); }",
      .library = "package lib\n"
                 "pub Result :: (value: char) -> (scalar: char) { scalar = "
                 "value; return; }\n"
                 "pub Encode :: (value: char) -> u8[] { scalar := int(value); "
                 "if (scalar <= 127) { return [u8(scalar)]; } "
                 "if (scalar <= 2047) { return [u8(192 | (scalar >> 6)), "
                 "u8(128 | (scalar & 63))]; } "
                 "if (scalar <= 65535) { return [u8(224 | (scalar >> 12)), "
                 "u8(128 | ((scalar >> 6) & 63)), "
                 "u8(128 | (scalar & 63))]; } "
                 "return [u8(240 | (scalar >> 18)), u8(128 | ((scalar >> 12) & "
                 "63)), "
                 "u8(128 | ((scalar >> 6) & 63)), u8(128 | (scalar & 63))]; }",
      .calls = { { .function = "Encode", .arguments = "[0]", .expected = "[[0]]" },
                 { .function = "Encode", .arguments = "[127]", .expected = "[[127]]" },
                 { .function = "Encode", .arguments = "[128]", .expected = "[[194,128]]" },
                 { .function = "Encode", .arguments = "[2047]", .expected = "[[223,191]]" },
                 { .function = "Encode", .arguments = "[2048]", .expected = "[[224,160,128]]" },
                 { .function = "Encode", .arguments = "[55295]", .expected = "[[237,159,191]]" },
                 { .function = "Encode", .arguments = "[57344]", .expected = "[[238,128,128]]" },
                 { .function = "Encode", .arguments = "[65535]", .expected = "[[239,191,191]]" },
                 { .function = "Encode", .arguments = "[65536]", .expected = "[[240,144,128,128]]" },
                 { .function = "Encode", .arguments = "[1114111]", .expected = "[[244,143,191,191]]" },
                 { .function = "NamedRead", .arguments = "[128008]", .expected = "[128008]" } } },
    { .name = "ordinary native numeric overload remains available",
      .group = TYPE_SAMPLES,
      .c_string_probe = true,
      .traced = true,
      .source = "package facts\nRun :: () -> int { value: int = 65; return "
                "native.kernel(value); }",
      .calls = { { .function = "Run", .arguments = "[]", .expected = "[1]" } } },
    { .name = "collection length beside the vector natives",
      .group = TYPE_SAMPLES,
      .game = true,
      .source = "package facts\nRun :: () -> int {\n"
                "    parts: string[] = [\"a\", \"b\"];\n"
                "    n: int = 0;\n"
                "    while (length(parts) < 4 - 1) { parts.append(\"c\"); n += 1; }\n"
                "    return n + length(parts);\n}",
      .calls = { { .function = "Run", .arguments = "[]", .expected = "[4]" } } },
    { .name = "core-math-imported-values",
      .group = IMPORT_SAMPLES,
      .math = true,
      .game = true,
      .source =
          "package facts\nimport math \"core:math\"\nimport ease "
          "\"core:math/ease\"\n"
          "pub Check :: () -> bool { x: number = 16777215; "
          "return math.InverseLerp(-1, 16777216, x) == 1 && "
          "math.InverseLerp(6, 2, 3) == 0.75 && math.Lerp(2, 6, 0.25) == 3 && "
          "ease.QuadraticInOut(0.5) == 0.5; }\n"
          "pub Spring :: () -> bool { spring: math.Spring = {position: 1, "
          "velocity: 0}; "
          "next := math.AdvanceSpring(spring, 0, 1, 1, 0.125); "
          "return next.position > 0.8140309 && next.position < 0.8140313 && "
          "next.velocity > -2.2499655 && next.velocity < -2.2499635; }\n"
          "pub Narrow :: () -> bool { wide: f64 = 16777217; "
          "return math.InverseLerp(16777216, 16777218, number(wide)) == 0; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" },
                 { .function = "Spring", .arguments = "[]", .expected = "[true]" },
                 { .function = "Narrow", .arguments = "[]", .expected = "[true]" } } },
    { .name = "core-math-rejects-implicit-f64-narrowing",
      .group = IMPORT_SAMPLES,
      .math = true,
      .game = true,
      .source = "package facts\nimport math \"core:math\"\n"
                "pub Narrow :: (value: f64) -> number { return "
                "math.InverseLerp(16777216, 16777218, value); }\n",
      .message = "argument differs from its parameter type",
      .line = 3,
      .column = 47 },
    { .name = "array update debug order",
      .group = COLLECTION_SAMPLES,
      .update_order = true,
      .source = "package facts\n"
                "Item :: () -> int { return 7; }\n"
                "Items :: () -> int[] { return [7, 8]; }\n"
                "AppendLocal :: () -> int[] { xs: int[] = [1]; xs = append(xs, "
                "Item()); return xs; }\n"
                "AppendParameter :: (xs: int[]) -> int[] { xs = append(xs, "
                "Item()); return xs; }\n"
                "AppendMethod :: () -> int[] { xs: int[] = [1]; "
                "xs.append(Item()); return xs; }\n"
                "AppendParameterMethod :: (xs: int[]) -> int[] { "
                "xs.append(Item()); return xs; }\n"
                "ExtendLocal :: () -> int[] { xs: int[] = [1]; xs = extend(xs, "
                "Items()); return xs; }\n"
                "ExtendParameter :: (xs: int[]) -> int[] { xs = extend(xs, "
                "Items()); return xs; }\n"
                "ExtendMethod :: () -> int[] { xs: int[] = [1]; "
                "xs.extend(Items()); return xs; }\n"
                "ExtendParameterMethod :: (xs: int[]) -> int[] { "
                "xs.extend(Items()); return xs; }",
      .calls = { { .function = "AppendLocal", .arguments = "[]", .expected = "[[1,7]]" },
                 { .function = "AppendParameter", .arguments = "[[1]]", .expected = "[[1,7]]" },
                 { .function = "AppendMethod", .arguments = "[]", .expected = "[[1,7]]" },
                 { .function = "AppendParameterMethod", .arguments = "[[1]]", .expected = "[[1,7]]" },
                 { .function = "ExtendLocal", .arguments = "[]", .expected = "[[1,7,8]]" },
                 { .function = "ExtendParameter", .arguments = "[[1]]", .expected = "[[1,7,8]]" },
                 { .function = "ExtendMethod", .arguments = "[]", .expected = "[[1,7,8]]" },
                 { .function = "ExtendParameterMethod",
                   .arguments = "[[1]]",
                   .expected = "[[1,7,8]]" } } },
    { .name = "typed constants fold exactly",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Sign: int : -1 << 31;\n"
                "Small: i8 : (64 << 1) - 1;\n"
                "Unsigned: u32 : 4294967295 >> 1;\n"
                "Floor: int : -3 >> 1;\n"
                "Wide: i64 : -1 << 63;\n"
                "Mask: u64 : ~0;\n"
                "Low: u8 : ~u8(15);\n"
                "Big: int : (1 << 100) >> 98;\n"
                "Half: i8 : (127 + 1) / 2;\n"
                "Wrapped: u8 : u8(wrap: 300);\n"
                "ReadSign :: () -> int { return Sign; }\n"
                "ReadSmall :: () -> i8 { return Small; }\n"
                "ReadUnsigned :: () -> u32 { return Unsigned; }\n"
                "ReadFloor :: () -> int { return Floor; }\n"
                "ReadWide :: () -> i64 { return Wide; }\n"
                "ReadMask :: () -> u64 { return Mask; }\n"
                "ReadLow :: () -> u8 { return Low; }\n"
                "ReadBig :: () -> int { return Big; }\n"
                "ReadHalf :: () -> i8 { return Half; }\n"
                "ReadWrapped :: () -> u8 { return Wrapped; }",
      .calls = { { .function = "ReadSign", .arguments = "[]", .expected = "[-2147483648]" },
                 { .function = "ReadSmall", .arguments = "[]", .expected = "[127]" },
                 { .function = "ReadUnsigned", .arguments = "[]", .expected = "[2147483647]" },
                 { .function = "ReadFloor", .arguments = "[]", .expected = "[-2]" },
                 { .function = "ReadWide", .arguments = "[]", .expected = "[-9223372036854775808]" },
                 { .function = "ReadMask", .arguments = "[]", .expected = "[18446744073709551615]" },
                 { .function = "ReadLow", .arguments = "[]", .expected = "[240]" },
                 { .function = "ReadBig", .arguments = "[]", .expected = "[4]" },
                 { .function = "ReadHalf", .arguments = "[]", .expected = "[64]" },
                 { .function = "ReadWrapped", .arguments = "[]", .expected = "[44]" } } },
    { .name = "constant outside its type",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: int : 1 << 31;",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 12 },
    { .name = "constant negative shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: int : 1 << -1;",
      .message = "Shift count must not be negative",
      .line = 2,
      .column = 17 },
    { .name = "constant shift count too large",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: int : 1 >> 4097;",
      .message = "Constant shift count is too large",
      .line = 2,
      .column = 17 },
    { .name = "parenthesized negative shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nShift :: (a: int) -> int { return a << (2 - "
                "3); }\n",
      .message = "Shift count must not be negative",
      .line = 2,
      .column = 41 },
    { .name = "constant signed shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nCount: int : 3;\nBad: int : 1 << Count;",
      .message = "Shift count must be unsigned; convert it with u32(value)",
      .line = 3,
      .column = 17 },
    { .name = "comparisons of untyped literals",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Guard :: () -> int {\n"
                "    if (7 < 3) { return 1; }\n"
                "    return 2;\n"
                "}\n"
                "Wide :: () -> bool { return 3000000000 > 2; }\n"
                "Mixed :: () -> bool { return 2.5 > 2; }\n"
                "Negative :: () -> bool { return -1 < 0; }\n"
                "Same :: () -> bool { return 7 == 7; }",
      .calls = { { .function = "Guard", .arguments = "[]", .expected = "[2]" },
                 { .function = "Wide", .arguments = "[]", .expected = "[true]" },
                 { .function = "Mixed", .arguments = "[]", .expected = "[true]" },
                 { .function = "Negative", .arguments = "[]", .expected = "[true]" },
                 { .function = "Same", .arguments = "[]", .expected = "[true]" } } },
    { .name = "import alias cannot fall through to assertion intrinsic",
      .group = IMPORT_SAMPLES,
      .game = true,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { lib.assert(false); return 1; }",
      .library = "package lib\npub Other :: () -> int { return 2; }",
      .message = "Unknown function 'assert'",
      .oracle_message = "Unknown callable 'lib.assert'",
      .same_rejection = true,
      .line = 3,
      .column = 20 },
    { .name = "private imported function",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { return lib.Hidden(); }",
      .library = "package lib\nHidden :: () -> int { return 7; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported constant",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { return lib.Hidden; }",
      .library = "package lib\nHidden :: 7;",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported constant expression",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\nValue :: "
                "lib.Hidden + 1;",
      .library = "package lib\nHidden :: 7;",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported record local",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { value: lib.Hidden = lib.Make(); return "
                "value.x; }",
      .library = "package lib\nHidden :: struct { x: int; }\n"
                 "pub Make :: () -> Hidden { return {x: 7}; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported record parameter",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: (value: lib.Hidden) -> int { return value.x; }",
      .library = "package lib\nHidden :: struct { x: int; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported record result",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> lib.Hidden { return lib.Make(); }",
      .library = "package lib\nHidden :: struct { x: int; }\n"
                 "pub Make :: () -> Hidden { return {x: 7}; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported union payload type",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Wrapper :: union { Some(value: lib.Hidden) }",
      .library = "package lib\nHidden :: struct { x: int; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported map value type",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { values: [int: lib.Hidden] = [:]; return 0; "
                "}",
      .library = "package lib\nHidden :: struct { x: int; }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private imported union constructor",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { value := lib.Hidden.Some(7); return 0; }",
      .library = "package lib\nHidden :: union { Some(value: int) | None }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .oracle_message = "Unknown callable 'lib.Hidden.Some'",
      .same_rejection = true },
    { .name = "private imported union value",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { value := lib.Hidden.None; return 0; }",
      .library = "package lib\nHidden :: union { Some(value: int) | None }",
      .message = "'lib.Hidden' is private: declare it pub to use it from "
                 "another package",
      .same_rejection = true },
    { .name = "private package implementation",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { return Helper(3) + lib.Value(3) + "
                "lib.Make(3).x; }\n"
                "Shadow :: (lib: Local) -> int { return lib.Hidden; }\n"
                "Local :: struct { Hidden: int; }",
      .more = "package facts\nHelper :: (value: int) -> int { return value + "
              "1; }",
      .library = "package lib\nHidden :: 7;\n"
                 "Secret :: struct { x: int; }\n"
                 "Twice :: (value: int) -> int { return value * 2; }\n"
                 "pub Value :: (value: int) -> int { return Twice(value) + "
                 "Hidden; }\n"
                 "pub Make :: (value: int) -> Secret { return {x: value}; }",
      .calls = { { .function = "One", .arguments = "[]", .expected = "[20]" },
                 { .function = "Shadow", .arguments = "[{\"Hidden\":11}]", .expected = "[11]" } } },
    { .name = "constant",
      .source = "package facts\nAnswer :: () -> int { return 42; }",
      .calls = { { .function = "Answer", .arguments = "[]", .expected = "[42]" } } },
    { .name = "named results",
      .source = "package facts\nPair :: (x: int) -> (kind: int, end: int) {\n  "
                "  end = x + 1;\n"
                "    return (x, end);\n}\nRest :: (x: int) -> (low: int, high: "
                "int) {\n    low = x - 1;\n    return;\n}\n"
                "Use :: (x: int) -> int {\n    p := Pair(x);\n    q := "
                "Rest(x);\n"
                "    return p.kind * 10 + p.end + Pair(x).end * 100 + q.low * "
                "1000 + q.high;\n}",
      .calls = { { .function = "Use", .arguments = "[3]", .expected = "[2434]" } } },
    { .name = "single named result",
      .source = "package facts\n"
                "One :: (n: int) -> (value: int) { return n + 1; }\n"
                "Flag :: () -> (value: bool = false) { value = true; return; "
                "}\n"
                "Sequential :: () -> (a: int, b: int) { a = 1; b = 2; return "
                "(b, a); }\n"
                "Use :: (n: int) -> int {\n"
                "    if Flag() { return One(n) * 10 + One(n).value; }\n"
                "    return 0;\n}\n",
      .calls = { { .function = "One", .arguments = "[3]", .expected = "[4]" },
                 { .function = "Sequential", .arguments = "[]", .expected = "[2,2]" },
                 { .function = "Use", .arguments = "[3]", .expected = "[44]" } } },
    { .name = "widened named projection",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Tiny :: () -> (value: i8) { return 7; }\n"
                "Narrow :: (n: i8) -> int { return int(n); }\n"
                "Use :: () -> int { n: int = Tiny(); return Narrow(n.value); "
                "}\n"
                "Alias :: () -> int { n: int = Tiny(); q := n; "
                "return Narrow(q.value); }\n"
                "Assigned :: (value: int) -> int { n: int = Tiny(); "
                "n = value; return Narrow(n.value); }\n"
                "Converted :: (value: int) -> int { n: int = Tiny(); "
                "n = value; return int(n.value); }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[7]" },
                 { .function = "Alias", .arguments = "[]", .expected = "[7]" },
                 { .function = "Assigned", .arguments = "[127]", .expected = "[127]" },
                 { .function = "Assigned", .arguments = "[-128]", .expected = "[-128]" },
                 { .function = "Assigned", .arguments = "[300]", .expected = "[127]" },
                 { .function = "Assigned", .arguments = "[-129]", .expected = "[-128]" },
                 { .function = "Converted", .arguments = "[300]", .expected = "[127]" },
                 { .function = "Converted", .arguments = "[-129]", .expected = "[-128]" } } },
    { .name = "optional errors beside named struct results",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Instant :: struct { ticks: i64; domain: u64; }\n"
                "Failure :: union { Denied | Platform(code: int) }\n"
                "Now :: (fail: bool) -> (instant: Instant, error: Failure?) {\n"
                "    if fail { return({ticks: 3, domain: 8}, "
                "Failure.Platform(17)); }\n"
                "    return({ticks: 12, domain: 8}, nil);\n}\n"
                "Use :: (fail: bool) -> int {\n"
                "    result := Now(fail);\n"
                "    error := result.error;\n"
                "    if error {\n"
                "        switch error { case .Denied: return -1; case "
                ".Platform(code): return int(result.instant.ticks) + code; }\n"
                "    }\n"
                "    return int(result.instant.ticks);\n}\n"
                "Empty :: () -> int { error: Failure? = nil; if error == nil { "
                "return 1; } return 0; }\n",
      .calls = { { .function = "Use", .arguments = "[false]", .expected = "[12]" },
                 { .function = "Use", .arguments = "[true]", .expected = "[20]" },
                 { .function = "Empty", .arguments = "[]", .expected = "[1]" } } },
    { .name = "partial text and bytes beside optional record errors",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Failure :: struct { code: int; message: string; }\n"
                "Read :: (fail: bool) -> (text: string, bytes: u8[], error: "
                "Failure?) {\n"
                "    if fail { return(\"partial\", [u8(3), u8(4)], {code: 7, "
                "message: \"bad input\"}); }\n"
                "    return(\"done\", [u8(8)], nil);\n}\n"
                "Use :: (fail: bool) -> int {\n"
                "    result := Read(fail);\n"
                "    bytes := result.bytes;\n"
                "    copy := bytes;\n"
                "    copy[0] = 9;\n"
                "    error := result.error;\n"
                "    if error {\n"
                "        if error.message != \"bad input\" { return -1; }\n"
                "        if result.text != \"partial\" { return -2; }\n"
                "        return int(bytes[0]) * 100 + int(bytes[1]) * 10 + "
                "error.code;\n"
                "    }\n"
                "    if result.text != \"done\" { return -3; }\n"
                "    return int(bytes[0]);\n}\n"
                "Defaults :: () -> (text: string, bytes: u8[], error: "
                "Failure?) { return; }\n"
                "UseDefaults :: () -> int { result := Defaults(); if "
                "result.error { return -1; }\n"
                "    if result.text != \"\" { return -2; } return "
                "length(result.bytes); }\n"
                "Code :: (error: Failure?) -> int { if error == nil { return "
                "0; } return error.code; }\n"
                "Guarded :: (fail: bool) -> int { result := Read(fail); return "
                "Code(result.error); }\n",
      .calls = { { .function = "Use", .arguments = "[false]", .expected = "[8]" },
                 { .function = "Use", .arguments = "[true]", .expected = "[347]" },
                 { .function = "UseDefaults", .arguments = "[]", .expected = "[0]" },
                 { .function = "Guarded", .arguments = "[false]", .expected = "[0]" },
                 { .function = "Guarded", .arguments = "[true]", .expected = "[7]" } } },
    { .name = "constant and dynamic byte array debugger origins",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Folded :: () -> u8[] { return [u8(1) + u8(2), u8(4)]; }\n"
                "Mixed :: (value: u8) -> u8[] { return [u8(3), value]; }\n"
                "Inferred :: () -> int { values := [u8(3), u8(4)]; "
                "return int(values[0]) + int(values[1]); }\n",
      .calls = { { .function = "Folded", .arguments = "[]", .expected = "[[3,4]]" },
                 { .function = "Mixed", .arguments = "[7]", .expected = "[[3,7]]" },
                 { .function = "Inferred", .arguments = "[]", .expected = "[7]" } } },
    { .name = "array conversion names preserve authored calls",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "u8 :: (value: int) -> int { return value + 1; }\n"
                "Use :: () -> int[] { return [u8(3), u8(4)]; }\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[[4,5]]" } } },
    { .name = "named syntax fact arrays survive forwarding and copies",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Node :: struct { kind: string; start: int; end: int; }\n"
                "ReadSyntax :: () -> (nodes: Node[], fields: string[], named: "
                "bool[], clean: bool) {\n"
                "    node: Node = {kind: \"word\", start: 3, end: 7};\n"
                "    return([node], [\"body\"], [true], true);\n}\n"
                "Forward :: () -> (nodes: Node[], fields: string[], named: "
                "bool[], clean: bool) {\n"
                "    result := ReadSyntax();\n"
                "    return(result.nodes, result.fields, result.named, "
                "result.clean);\n}\n"
                "Use :: () -> int {\n"
                "    original := Forward();\n"
                "    nodes := original.nodes; fields := original.fields; named "
                ":= original.named;\n"
                "    changed := nodes[0]; changed.kind = \"changed\"; "
                "changed.start = 99;\n"
                "    nodes[0] = changed; fields[0] = \"changed\"; named[0] = "
                "false;\n"
                "    if !original.clean { return -1; }\n"
                "    if original.nodes[0].kind != \"word\" { return -2; }\n"
                "    if original.fields[0] != \"body\" { return -3; }\n"
                "    if !original.named[0] { return -4; }\n"
                "    if nodes[0].kind != \"changed\" || nodes[0].start != 99 { "
                "return -5; }\n"
                "    if fields[0] != \"changed\" || named[0] { return -6; }\n"
                "    return original.nodes[0].start * 100 + "
                "original.nodes[0].end * 10 + length(original.fields);\n}\n"
                "Defaults :: () -> (nodes: Node[], fields: string[], named: "
                "bool[], clean: bool) { return; }\n"
                "DefaultSize :: () -> int {\n"
                "    result := Defaults(); if result.clean { return -1; }\n"
                "    return length(result.nodes) + length(result.fields) + "
                "length(result.named);\n}\n",
      .calls = { { .function = "ReadSyntax", .arguments = "[]", .expected = "[[{\"kind\":\"word\",\"start\":3,\"end\":7}],[\"body\"],[true],true]" },
                 { .function = "Use", .arguments = "[]", .expected = "[371]" },
                 { .function = "DefaultSize", .arguments = "[]", .expected = "[0]" } } },
    { .name = "optional presence preserves false zero and empty text",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Flag :: () -> bool? { return false; }\n"
                "Count :: () -> int? { return 0; }\n"
                "Text :: () -> string? { return \"\"; }\n"
                "Absent :: () -> int? { return nil; }\n"
                "Use :: () -> int {\n"
                "    score: int = 0;\n"
                "    if Flag() { score += 1; }\n"
                "    if Count() { score += 10; }\n"
                "    if Text() { score += 100; }\n"
                "    if Absent() { score += 1000; }\n"
                "    return score;\n}\n",
      .calls = { { .function = "Use", .arguments = "[]", .expected = "[111]" } } },
    { .name = "optional error payload requires a presence guard",
      .group = TYPE_SAMPLES,
      .same_rejection = true,
      .source = "package facts\n"
                "Failure :: struct { code: int; }\n"
                "Use :: (error: Failure?) -> int { return error.code; }\n",
      .message = "Optional component requires a presence guard before member "
                 "access" },
    { .name = "named struct results",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Collision :: struct { point: int; }\n"
                "MakeCollision :: () -> (point: Collision) { return {point: "
                "7}; }\n"
                "ReadCollision :: () -> int { return "
                "MakeCollision().point.point; }\n"
                "Direct :: (n: int) -> (point: Point) { return {x: n, y: 2}; "
                "}\n"
                "Empty :: () -> (point: Point) { return; }\n"
                "Mutate :: (n: int) -> (point: Point) { point.x = n; point.y = "
                "2; return; }\n"
                "Project :: () -> int { p := Direct(7); q := p; return "
                "q.point.x; }\n"
                "Other :: () -> (other: Point) { return {x: 9, y: 3}; }\n"
                "Reassigned :: () -> int { p := Direct(7); p = Other(); "
                "q := p; return q.point.x; }\n"
                "LaterShadow :: () -> int { p := Direct(7); q := p; "
                "if true { p: Point = {x: 99, y: 1}; return q.point.x + "
                "p.x; } return 0; }\n"
                "Bump :: (p: ^Point) { p.x += 1; }\n"
                "ByRef :: () -> int { p := Direct(7); Bump(&p); return "
                "p.point.x; }\n"
                "Counter :: () -> (value: int) { return 7; }\n"
                "Increment :: (n: ^int) { n^ += 1; }\n"
                "Boxed :: () -> int { n := Counter(); Increment(&n); "
                "return n.value; }\n"
                "Use :: (n: int) -> int {\n"
                "    p := Direct(n);\n"
                "    q := p;\n"
                "    q.x += 5;\n"
                "    return p.x * 100 + q.x * 10 + q.y + Empty().x;\n}\n",
      .calls = { { .function = "Direct", .arguments = "[7]", .expected = "[{\"x\":7,\"y\":2}]" },
                 { .function = "Empty", .arguments = "[]", .expected = "[{\"x\":0,\"y\":0}]" },
                 { .function = "Mutate", .arguments = "[7]", .expected = "[{\"x\":7,\"y\":2}]" },
                 { .function = "Project", .arguments = "[]", .expected = "[7]" },
                 { .function = "Reassigned", .arguments = "[]", .expected = "[9]" },
                 { .function = "LaterShadow", .arguments = "[]", .expected = "[106]" },
                 { .function = "ByRef", .arguments = "[]", .expected = "[8]" },
                 { .function = "Boxed", .arguments = "[]", .expected = "[8]" },
                 { .function = "ReadCollision", .arguments = "[]", .expected = "[7]" },
                 { .function = "Use", .arguments = "[7]", .expected = "[822]" } } },
    { .name = "named result ownership",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "Keep :: (value: string) -> (note: Note) {\n"
                "    note.text = value; note.count = 2; return;\n}\n"
                "Split :: (value: string) -> (note: Note, count: int) {\n"
                "    note = Keep(value); return (note, note.count);\n}\n"
                "Use :: () -> string {\n"
                "    first := Keep(\"a\\u0000b\");\n"
                "    second := Keep(\"changed\");\n"
                "    second.text = \"other\";\n"
                "    return first.text;\n}\n"
                "Projected :: () -> string {\n"
                "    pair := Split(\"a\\u0000b\");\n"
                "    return pair.note.text;\n}\n",
      .calls = { { .function = "Keep",
                   .arguments = "[\"a\\u0000b\"]",
                   .expected = "[{\"text\":\"a\\u0000b\",\"count\":2}]" },
                 { .function = "Split",
                   .arguments = "[\"kept\"]",
                   .expected = "[{\"text\":\"kept\",\"count\":2},2]" },
                 { .function = "Use", .arguments = "[]", .expected = "[\"a\\u0000b\"]" },
                 { .function = "Projected", .arguments = "[]", .expected = "[\"a\\u0000b\"]" } } },
    { .name = "parameters",
      .source = "package facts\nAdd :: (a: int, b: int) -> int { return "
                "a + b * 2 - (a - b) % 3; }",
      .calls = { { .function = "Add", .arguments = "[3,4]", .expected = "[12]" },
                 { .function = "Add", .arguments = "[-5,2]", .expected = "[0]" } } },
    { .name = "loops",
      .source = "package facts\nSum :: (n: int) -> int {\n"
                "    total: int = 0;\n    i: int = 0;\n"
                "    while (i < n) { total += i; i += 1; }\n    return "
                "total;\n}",
      .calls = { { .function = "Sum", .arguments = "[10]", .expected = "[45]" },
                 { .function = "Sum", .arguments = "[0]", .expected = "[0]" } } },
    { .name = "recursion",
      .source = "package facts\nFib :: (n: int) -> int {\n"
                "    if (n < 2) { return n; }\n    return Fib(n - 1) + Fib(n - "
                "2);\n}",
      .calls = { { .function = "Fib", .arguments = "[15]", .expected = "[610]" } } },
    { .name = "control",
      .source = "package facts\nCount :: (n: int) -> int {\n"
                "    found: int = 0;\n    i: int = 0;\n"
                "    while (true) {\n        i += 1;\n        if (i > n) { "
                "break; }\n"
                "        if (i % 3 == 0 || i % 5 == 0) { continue; } else { "
                "found += 1; }\n    }\n"
                "    return found;\n}",
      .calls = { { .function = "Count", .arguments = "[15]", .expected = "[8]" } } },
    { .name = "logic",
      .source = "package facts\nEven :: (n: int) -> bool { return !(n % "
                "2 != 0) && n >= 0; }\n"
                "Both :: (a: int, b: int) -> bool { return Even(a) && "
                "Even(b) || a == b; }",
      .calls = { { .function = "Even", .arguments = "[4]", .expected = "[true]" },
                 { .function = "Even", .arguments = "[-4]", .expected = "[false]" },
                 { .function = "Both", .arguments = "[2,4]", .expected = "[true]" },
                 { .function = "Both", .arguments = "[3,3]", .expected = "[true]" } } },
    { .name = "records",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; title: string; open: bool; "
                "}\n"
                "Make :: (x: int, y: int) -> Point {\n"
                "    p: Point = {x: x, y: y, title: \"p\", open: x < y};\n"
                "    return p;\n}\n"
                "Sum :: (p: Point) -> int { return p.x + p.y; }\n"
                "Moved :: (p: Point, dx: int) -> Point {\n"
                "    return {title: p.title, open: p.open, y: p.y, x: p.x + "
                "dx};\n}\n"
                "Total :: (n: int) -> int {\n"
                "    acc: Point = {x: 0, y: 0, title: \"\", open: false};\n"
                "    i: int = 0;\n"
                "    while (i < n) { acc = Moved(acc, i); i += 1; }\n"
                "    first := acc;\n    acc = Moved(acc, 100);\n"
                "    return Sum(acc) + Sum(Make(1, 2)) + first.x;\n}",
      .calls = { { .function = "Make", .arguments = "[1,2]", .expected = "[{\"x\":1,\"y\":2,\"title\":\"p\",\"open\":true}]" },
                 { .function = "Moved", .arguments = "[{\"x\":1,\"y\":2,\"title\":\"q\",\"open\":false},5]", .expected = "[{\"x\":6,\"y\":2,\"title\":\"q\",\"open\":false}]" },
                 { .function = "Total", .arguments = "[5]", .expected = "[123]" } } },
    { .name = "view element lending",
      .group = COLLECTION_SAMPLES,
      .source =
                "package facts\n"
                "Pair :: union { Row(xs: int[]) | None }\n"
                "Hold :: (window: view int[][], owner: ^(int[][])) -> int {\n"
                "    row := window[0];\n"
                "    owner^[0] = [9];\n"
                "    return row[0] * 10 + length(row);\n"
                "}\n"
                "Viewed :: () -> int {\n"
                "    values: int[][] = [[1, 2], [3]];\n"
                "    pointer := &values;\n"
                "    selected := pointer.view(0..<2);\n"
                "    if selected {\n"
                "        return Hold(selected, pointer);\n"
                "    }\n"
                "    return -1;\n"
                "}\n"
                "HoldPair :: (window: view Pair[], owner: ^(Pair[])) -> int {\n"
                "    pair := window[0];\n"
                "    owner^[0] = Pair.None;\n"
                "    switch pair {\n"
                "        case .Row(xs):\n"
                "            return length(xs);\n"
                "        case:\n"
                "            return -2;\n"
                "    }\n"
                "}\n"
                "ViewedPair :: () -> int {\n"
                "    values: Pair[] = [Pair.Row([1, 2]), Pair.None];\n"
                "    pointer := &values;\n"
                "    selected := pointer.view(0..<2);\n"
                "    if selected {\n"
                "        return HoldPair(selected, pointer);\n"
                "    }\n"
                "    return -1;\n"
                "}\n",
      .calls = {
          { .function = "Viewed", .arguments = "[]",
            .expected = "[12]" },
          { .function = "ViewedPair", .arguments = "[]",
            .expected = "[2]" },
      } },
    { .name = "scoped lent locals",
      .group = COLLECTION_SAMPLES,
      .source =
                "package facts\n"
                "Pair :: union { Row(xs: int[]) | None }\n"
                "Make :: (n: int) -> int[] {\n"
                "    xs: int[] = [];\n"
                "    for i in 0..<n {\n"
                "        xs.append(i + 1);\n"
                "    }\n"
                "    return xs;\n"
                "}\n"
                "Shape :: (xs: int[]) -> int {\n"
                "    return length(xs) * 10 + xs[length(xs) - 1];\n"
                "}\n"
                "Width :: (p: Pair) -> int {\n"
                "    switch p {\n"
                "        case .Row(xs):\n"
                "            return Shape(xs);\n"
                "        case:\n"
                "            return -1;\n"
                "    }\n"
                "}\n"
                "Param :: (xs: int[], flag: bool) -> int {\n"
                "    if flag {\n"
                "        alias := xs;\n"
                "        if length(alias) == 0 {\n"
                "            return -1;\n"
                "        }\n"
                "    }\n"
                "    return Shape(xs);\n"
                "}\n"
                "Scoped :: () -> int {\n"
                "    xs := Make(2);\n"
                "    if length(xs) > 0 {\n"
                "        alias := xs;\n"
                "        if length(alias) == 0 {\n"
                "            return -1;\n"
                "        }\n"
                "    }\n"
                "    return Shape(xs);\n"
                "}\n"
                "Passed :: () -> int {\n"
                "    xs := Make(4);\n"
                "    n := Param(xs, true);\n"
                "    return n * 100 + Shape(xs);\n"
                "}\n"
                "PassedFalse :: () -> int {\n"
                "    xs := Make(4);\n"
                "    n := Param(xs, false);\n"
                "    return n * 100 + Shape(xs);\n"
                "}\n"
                "Broken :: () -> int {\n"
                "    xs := Make(3);\n"
                "    total := 0;\n"
                "    for i in 0..<3 {\n"
                "        alias := xs;\n"
                "        total += length(alias);\n"
                "        if i == 1 {\n"
                "            break;\n"
                "        }\n"
                "    }\n"
                "    return total * 100 + Shape(xs);\n"
                "}\n"
                "Continued :: () -> int {\n"
                "    xs := Make(3);\n"
                "    total := 0;\n"
                "    for i in 0..<3 {\n"
                "        alias := xs;\n"
                "        if i == 1 {\n"
                "            continue;\n"
                "        }\n"
                "        total += length(alias);\n"
                "    }\n"
                "    return total * 100 + Shape(xs);\n"
                "}\n"
                "Returned :: () -> int {\n"
                "    xs := Make(3);\n"
                "    total := 0;\n"
                "    for i in 0..<3 {\n"
                "        alias := xs;\n"
                "        total += length(alias);\n"
                "        if i == 1 {\n"
                "            return total * 100 + Shape(xs);\n"
                "        }\n"
                "    }\n"
                "    return -1;\n"
                "}\n"
                "Whole :: () -> int {\n"
                "    held := Pair.Row(Make(3));\n"
                "    if Width(held) > 0 {\n"
                "        alias := held;\n"
                "        if Width(alias) < 0 {\n"
                "            return -1;\n"
                "        }\n"
                "    }\n"
                "    return Width(held);\n"
                "}\n"
                "Looped :: () -> int {\n"
                "    rows: int[][] = [Make(3), Make(1)];\n"
                "    total := 0;\n"
                "    for i in 0..<3 {\n"
                "        row := rows[0];\n"
                "        total += length(row);\n"
                "    }\n"
                "    return total * 100 + Shape(rows[0]);\n"
                "}\n"
                "RowBroken :: () -> int {\n"
                "    rows: int[][] = [Make(3), Make(1)];\n"
                "    total := 0;\n"
                "    for i in 0..<3 {\n"
                "        row := rows[0];\n"
                "        total += length(row);\n"
                "        if i == 1 {\n"
                "            break;\n"
                "        }\n"
                "    }\n"
                "    return total * 100 + Shape(rows[0]);\n"
                "}\n"
                "RowContinued :: () -> int {\n"
                "    rows: int[][] = [Make(3), Make(1)];\n"
                "    total := 0;\n"
                "    for i in 0..<3 {\n"
                "        row := rows[0];\n"
                "        if i == 1 {\n"
                "            continue;\n"
                "        }\n"
                "        total += length(row);\n"
                "    }\n"
                "    return total * 100 + Shape(rows[0]);\n"
                "}\n"
                "RowReturned :: () -> int {\n"
                "    rows: int[][] = [Make(3), Make(1)];\n"
                "    total := 0;\n"
                "    for i in 0..<3 {\n"
                "        row := rows[0];\n"
                "        total += length(row);\n"
                "        if i == 1 {\n"
                "            return total * 100 + Shape(rows[0]);\n"
                "        }\n"
                "    }\n"
                "    return -1;\n"
                "}\n"
                "Element :: () -> int {\n"
                "    pairs: Pair[] = [Pair.Row(Make(2)), Pair.None];\n"
                "    if length(pairs) > 0 {\n"
                "        pair := pairs[0];\n"
                "        if Width(pair) < 0 {\n"
                "            return -1;\n"
                "        }\n"
                "    }\n"
                "    return Width(pairs[0]);\n"
                "}\n"
                "Items :: () -> int {\n"
                "    rows: int[][] = [Make(3), Make(1)];\n"
                "    total := 0;\n"
                "    for row in rows {\n"
                "        total += length(row);\n"
                "    }\n"
                "    return total * 10000 + Shape(rows[0]) * 100 + Shape(rows[1]);\n"
                "}\n"
                "ItemsBroken :: () -> int {\n"
                "    rows: int[][] = [Make(3), Make(1)];\n"
                "    total := 0;\n"
                "    for row in rows {\n"
                "        total += length(row);\n"
                "        if length(row) == 3 {\n"
                "            break;\n"
                "        }\n"
                "    }\n"
                "    return total * 10000 + Shape(rows[0]) * 100 + Shape(rows[1]);\n"
                "}\n",
      .calls = {
          { .function = "Scoped", .arguments = "[]",
            .expected = "[22]" },
          { .function = "Passed", .arguments = "[]",
            .expected = "[4444]" },
          { .function = "PassedFalse", .arguments = "[]",
            .expected = "[4444]" },
          { .function = "Broken", .arguments = "[]",
            .expected = "[633]" },
          { .function = "Continued", .arguments = "[]",
            .expected = "[633]" },
          { .function = "Returned", .arguments = "[]",
            .expected = "[633]" },
          { .function = "Whole", .arguments = "[]",
            .expected = "[33]" },
          { .function = "Looped", .arguments = "[]",
            .expected = "[933]" },
          { .function = "RowBroken", .arguments = "[]",
            .expected = "[633]" },
          { .function = "RowContinued", .arguments = "[]",
            .expected = "[633]" },
          { .function = "RowReturned", .arguments = "[]",
            .expected = "[633]" },
          { .function = "Element", .arguments = "[]",
            .expected = "[22]" },
          { .function = "Items", .arguments = "[]",
            .expected = "[43311]" },
          { .function = "ItemsBroken", .arguments = "[]",
            .expected = "[33311]" },
      } },
    { .name = "removed union elements",
      .group = COLLECTION_SAMPLES,
      .source =
                "package facts\n"
                "Holder :: union { Has(xs: int[]) | Empty }\n"
                "Work :: union { Visit(h: Holder) | Token(n: int) }\n"
                "Removed :: () -> int {\n"
                "    outer: Holder[] = [Holder.Empty, Holder.Has([7])];\n"
                "    pending: Work[] = [];\n"
                "    pending.append(Work.Visit(outer[1]));\n"
                "    work := pending[0];\n"
                "    pending.remove_index(0);\n"
                "    switch work {\n"
                "        case .Visit(h):\n"
                "            switch h {\n"
                "                case .Has(xs):\n"
                "                    return length(xs);\n"
                "                case:\n"
                "                    return -2;\n"
                "            }\n"
                "        case:\n"
                "            return -3;\n"
                "    }\n"
                "}\n"
                "Replaced :: () -> int {\n"
                "    pending: Work[] = [Work.Visit(Holder.Has([1, 2]))];\n"
                "    work := pending[0];\n"
                "    work = pending[0];\n"
                "    pending[0] = Work.Token(0);\n"
                "    switch work {\n"
                "        case .Visit(h):\n"
                "            switch h {\n"
                "                case .Has(xs):\n"
                "                    return length(xs);\n"
                "                case:\n"
                "                    return -2;\n"
                "            }\n"
                "        case:\n"
                "            return -3;\n"
                "    }\n"
                "}\n"
                "Walked :: () -> int {\n"
                "    outer: Holder[] = [Holder.Has([5, 6, 7])];\n"
                "    pending: Work[] = [Work.Visit(outer[0]), Work.Token(1)];\n"
                "    total: int = 0;\n"
                "    while (length(pending) > 0) {\n"
                "        work := pending[length(pending) - 1];\n"
                "        pending.remove_index(length(pending) - 1);\n"
                "        switch work {\n"
                "            case .Visit(h):\n"
                "                switch h {\n"
                "                    case .Has(xs):\n"
                "                        total += length(xs);\n"
                "                        pending.append(Work.Token(10));\n"
                "                    case:\n"
                "                        total = -2;\n"
                "                }\n"
                "            case .Token(n):\n"
                "                total += n;\n"
                "        }\n"
                "    }\n"
                "    return total;\n"
                "}\n",
      .calls = { { .function = "Removed", .arguments = "[]", .expected = "[1]" },
                 { .function = "Replaced", .arguments = "[]", .expected = "[2]" },
                 { .function = "Walked", .arguments = "[]", .expected = "[14]" } } },
    { .name = "lent arguments",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "Change :: (note: Note, xs: int[]) -> int {\n"
                "    note.count = 9;\n    xs.append(7);\n    xs[0] = 9;\n"
                "    return length(xs) * 10 + note.count;\n}\n"
                "Same :: (xs: int[]) -> int[] { return xs; }\n"
                "Read :: (note: Note, xs: int[]) -> int { return note.count * 10 + "
                "length(xs); }\n"
                "Grow :: (xs: ^(int[])) -> int { xs^[0] = 5; return 0; }\n"
                "Observe :: (xs: int[], ignored: int) -> int { return xs[0]; }\n"
                "Peek :: (p: ^(int[]), xs: int[]) -> int { p^[0] = 5; return xs[0]; }\n"
                "Writes :: () -> int {\n"
                "    xs: int[] = [1];\n    note: Note = {text: \"a\", count: 1};\n"
                "    n := Change(note, xs);\n"
                "    if (note.count != 1 || xs[0] != 1) { return -1; }\n"
                "    return n * 100 + note.count * 10 + length(xs);\n}\n"
                "Returns :: () -> int {\n"
                "    xs: int[] = [1, 2];\n    kept := Same(xs);\n"
                "    xs.append(3);\n    xs[0] = 5;\n"
                "    return kept[0] * 100 + length(kept) * 10 + length(xs);\n}\n"
                "Temporaries :: () -> int { return Read({text: \"t\", count: 3}, "
                "Same([4, 5])); }\n"
                "Referenced :: () -> int {\n"
                "    a: int[] = [1];\n    n := Observe(a, Grow(&a));\n"
                "    return n * 10 + a[0];\n}\n"
                "ReferencedEarlier :: () -> int {\n"
                "    a: int[] = [1];\n    n := Peek(&a, a);\n"
                "    return n * 10 + a[0];\n}",
      .calls = { { .function = "Writes", .arguments = "[]", .expected = "[2911]" },
                 { .function = "Returns", .arguments = "[]", .expected = "[123]" },
                 { .function = "Temporaries", .arguments = "[]", .expected = "[32]" },
                 { .function = "Referenced", .arguments = "[]", .expected = "[15]" },
                 { .function = "ReferencedEarlier", .arguments = "[]", .expected = "[15]" } } },
    { .name = "moved values",
      .group = IMPORT_SAMPLES,
      .moves = true,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "MovedNote :: (n: int) -> Note { return {text: \"a\", count: "
                "n}; }\n"
                "MovedResults :: () -> int {\n"
                "    a := MovedNote(1);\n    b := MovedNote(1);\n"
                "    a.count = 10;\n    b = MovedNote(2);\n"
                "    return a.count + b.count;\n}\n"
                "MovedList :: (n: int) -> int[] { return [n, n + 1]; }\n"
                "MovedSecond :: (n: int) -> int { xs := MovedList(n); return "
                "xs[1]; }\n"
                "Copied :: (notes: Note[]) -> int {\n"
                "    note := notes[0];\n    note.count += 5;\n"
                "    return note.count + notes[0].count;\n}",
      .calls = { { .function = "MovedNote", .arguments = "[3]", .expected = "[{\"text\":\"a\",\"count\":3}]" },
                 { .function = "MovedResults", .arguments = "[]", .expected = "[12]" },
                 { .function = "MovedSecond", .arguments = "[4]", .expected = "[5]" },
                 { .function = "Copied",
                   .arguments = "[[{\"text\":\"x\",\"count\":1}]]",
                   .expected = "[7]" } } },
    { .name = "returned variants",
      .group = IMPORT_SAMPLES,
      .variants = true,
      .source =
          "package facts\n"
          "Holder :: union { Some(items: int[], name: string) }\n"
          "KeptMake :: (n: int) -> Holder {\n"
          "    xs: int[] = [];\n    i: int = 0;\n"
          "    while (i < n) { xs.append(i); i += 1; }\n"
          "    return (Holder.Some(xs, \"made\"));\n}\n"
          "KeptAgain :: (h: Holder) -> Holder {\n"
          "    switch h { case .Some(items, name): return Holder.Some(items, "
          "name); }\n}\n"
          "KeptParam :: (xs: int[]) -> Holder { return Holder.Some(xs, \"p\"); "
          "}\n"
          "Total :: () -> int {\n"
          "    a := KeptMake(3);\n    b := KeptMake(5);\n    c := "
          "KeptAgain(a);\n"
          "    total: int = 0;\n"
          "    switch a { case .Some(items, name): total += items[2] * 100; }\n"
          "    switch b { case .Some(items, name): total += items[4] * 10; }\n"
          "    switch c { case .Some(items, name): total += items[1]; }\n"
          "    return total;\n}\n"
          "Changed :: () -> int {\n"
          "    ys: int[] = [1, 2];\n    h := KeptParam(ys);\n    ys[0] = 7;\n"
          "    switch h { case .Some(items, name): return items[0] * 10 + "
          "ys[0]; }\n}\n"
          "CopiedHold :: (xs: int[]) -> int {\n"
          "    h := Holder.Some(xs, \"x\");\n    xs[0] = 9;\n"
          "    switch h { case .Some(items, name): return items[0]; }\n}",
      .calls = { { .function = "Total", .arguments = "[]", .expected = "[241]" },
                 { .function = "Changed", .arguments = "[]", .expected = "[17]" },
                 { .function = "CopiedHold", .arguments = "[[5,6]]", .expected = "[5]" } } },
    { .name = "built results",
      .group = IMPORT_SAMPLES,
      .built = true,
      .source = "package facts\n"
                "Holder :: union { Some(items: int[], name: string) }\n"
                "Counted :: (n: int) -> int[] {\n"
                "    xs: int[] = [];\n    i: int = 0;\n"
                "    while (i < n) { xs.append(i); i += 1; }\n"
                "    return xs;\n}\n"
                "Paired :: (n: int, ys: int[]) -> Holder {\n"
                "    xs: int[] = [7];\n    xs.extend(Counted(n));\n    "
                "xs[0] = 9;\n"
                "    if (n == 0) { return Holder.Some(ys, \"none\"); }\n"
                "    return Holder.Some(xs, \"pair\");\n}\n"
                "BuiltPair :: (n: int) -> Holder { return Holder.Some([n, n + 1], \"pair\"); }\n"
                "BuiltOne :: () -> Holder { return Holder.Some([1], \"one\"); }\n"
                "CopiedCount :: (n: int) -> int[] {\n"
                "    xs: int[] = [];\n    xs = Counted(n);\n    return "
                "xs;\n}\n"
                "Total :: () -> int {\n"
                "    a := Counted(3);\n    ys: int[] = [4, 5];\n"
                "    b := Paired(2, ys);\n    c := Paired(0, ys);\n    "
                "ys[0] = 6;\n    d := BuiltPair(3);\n    e := BuiltOne();\n"
                "    total: int = a[2] * 1000 + CopiedCount(4)[3] * 100;\n"
                "    switch b { case .Some(items, name): total += items[0] * "
                "10 + items[2]; }\n"
                "    switch c { case .Some(items, name): total += items[0] * "
                "10000; }\n"
                "    switch d { case .Some(items, name): total += items[1] * 100000; }\n"
                "    switch e { case .Some(items, name): total += items[0] * 1000000; }\n"
                "    return total;\n}",
      .calls = { { .function = "Total", .arguments = "[]", .expected = "[1442391]" } } },
    { .name = "read in place",
      .group = IMPORT_SAMPLES,
      .aliases = true,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "AliasedSum :: (notes: Note[]) -> int {\n"
                "    total: int = 0;\n"
                "    for (note in notes) { total += note.count; }\n"
                "    first := notes[0];\n    return total + first.count * "
                "100;\n}\n"
                "CopiedWalk :: (notes: Note[]) -> int {\n"
                "    xs := notes;\n    total: int = 0;\n"
                "    for (note in xs) { xs[1] = {text: \"z\", count: 100}; "
                "total += note.count; }\n"
                "    return total;\n}\n"
                "LentFind :: (notes: Note[], text: string) -> int {\n"
                "    index: int = 0;\n"
                "    while (index < notes.length()) {\n"
                "        note := notes[index];\n"
                "        if (note.text == text && note.count > 0) { return index; }\n"
                "        index += 1;\n    }\n    return -1;\n}\n"
                "CopiedFind :: (notes: Note[]) -> int {\n"
                "    note := notes[1];\n    notes[1] = {text: \"z\", count: 100};\n"
                "    return note.count;\n}\n"
                "WalkedSum :: (notes: Note[]) -> int {\n"
                "    total: int = 0;\n    more: Note[] = [{text: \"c\", count: 3}];\n"
                "    for (note in notes) { total += note.count; }\n"
                "    for (note in more) { total += note.count; }\n    return total;\n}",
      .calls = { { .function = "WalkedSum",
                   .arguments = "[[{\"text\":\"a\",\"count\":1},{\"text\":"
                                "\"b\",\"count\":2}]]",
                   .expected = "[6]" },
                 { .function = "AliasedSum",
                   .arguments = "[[{\"text\":\"a\",\"count\":1},{\"text\":"
                                "\"b\",\"count\":2}]]",
                   .expected = "[103]" },
                 { .function = "CopiedWalk",
                   .arguments = "[[{\"text\":\"x\",\"count\":1},{\"text\":"
                                "\"y\",\"count\":2}]]",
                   .expected = "[3]" },
                 { .function = "LentFind",
                   .arguments = "[[{\"text\":\"a\",\"count\":1},{\"text\":"
                                "\"b\",\"count\":2}],\"b\"]",
                   .expected = "[1]" },
                 { .function = "CopiedFind",
                   .arguments = "[[{\"text\":\"x\",\"count\":1},{\"text\":"
                                "\"y\",\"count\":2}]]",
                   .expected = "[2]" } } },
    { .name = "native values",
      .group = IMPORT_SAMPLES,
      .lends = true,
      .source = "package facts\n"
                "LentLength :: (xs: int[]) -> int { return length(xs) + xs.length(); }\n"
                "LentSlice :: (xs: int[]) -> int {\n"
                "    part := slice(xs, 1, 2);\n    part.append(5);\n"
                "    rest := xs.slice(0, 1);\n    rest = append(rest, 7);\n"
                "    return length(part) * 10 + rest[1] + xs.length() * 100;\n}\n"
                "LentReturn :: (xs: int[]) -> int[] { return slice(xs, 1, 1); }\n"
                "CopiedElement :: () -> int { xss: int[][] = [[1, 2], [3]]; return length(xss[0]); }",
      .calls = { { .function = "LentLength", .arguments = "[[1,2,3]]", .expected = "[6]" },
                 { .function = "LentLength", .arguments = "[[]]", .expected = "[0]" },
                 { .function = "LentSlice", .arguments = "[[1,2,3]]", .expected = "[337]" },
                 { .function = "LentReturn", .arguments = "[[1,2,3]]", .expected = "[[2]]" },
                 { .function = "CopiedElement", .arguments = "[]", .expected = "[2]" } } },
    { .name = "plain drops",
      .group = IMPORT_SAMPLES,
      .drops = true,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Shape :: union { Dot(at: Point) | Line(a: int, b: int) }\n"
                "Bag :: union { Items(xs: int[]) | Size(n: int) }\n"
                "Make :: (x: int) -> Point { return {x: x, y: x + 1}; }\n"
                "Sum :: (p: Point) -> int { return p.x + p.y; }\n"
                "Width :: (s: Shape) -> int {\n"
                "    switch s { case .Dot(at): return at.x; case .Line(a, b): return b - a; }\n}\n"
                "Count :: (b: Bag) -> int {\n"
                "    switch b { case .Items(xs): return length(xs); case .Size(n): return n; }\n}\n"
                "PlainPoint :: (x: int) -> int { return Sum(Make(x)); }\n"
                "PlainLine :: (x: int) -> int { return Width(Shape.Line(x, x * 3)); }\n"
                "PlainDot :: (x: int) -> int { return Width(Shape.Dot(Make(x))); }\n"
                "HeldBag :: (n: int) -> int { return Count(Bag.Items([n, n])); }\n"
                "HeldArray :: (n: int) -> int { return length(Counted(n)); }\n"
                "HeldExtend :: (n: int) -> int { xs: int[] = [1]; xs.extend(Counted(n)); xs.append(n); return xs[1] + xs[2]; }\n"
                "HeldPush :: (n: int) -> int { bags: Bag[] = []; bags.append(Bag.Items(Counted(n))); bags.append(Bag.Size(n)); return Count(bags[0]) + Count(bags[1]); }\n"
                "Counted :: (n: int) -> int[] { xs: int[] = []; xs.append(n); return xs; }",
      .calls = { { .function = "PlainPoint", .arguments = "[3]", .expected = "[7]" },
                 { .function = "PlainLine", .arguments = "[2]", .expected = "[4]" },
                 { .function = "PlainDot", .arguments = "[2]", .expected = "[2]" },
                 { .function = "HeldBag", .arguments = "[4]", .expected = "[2]" },
                 { .function = "HeldArray", .arguments = "[4]", .expected = "[1]" },
                 { .function = "HeldExtend", .arguments = "[4]", .expected = "[8]" },
                 { .function = "HeldPush", .arguments = "[4]", .expected = "[5]" } } },
    { .name = "array methods",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Build :: (n: int) -> int[] {\n"
                "    xs: int[] = [];\n    i: int = 0;\n"
                "    while (i < n) { xs.append(Item: i); i += 1; }\n"
                "    ys: int[] = [100, 200];\n"
                "    xs.extend(other_array: ys);\n    seven: int = 7;\n    "
                "xs.insert(0, "
                "seven);\n    xs.reverse();\n"
                "    return xs;\n}\n"
                "Count :: (xs: int[]) -> int { return xs.length() * 10 + "
                "Build(2).length(); }\n"
                "Has :: (xs: int[], x: int) -> bool { return xs.contains(x); "
                "}\n"
                "Sliced :: (xs: int[]) -> int[] { part := xs.slice(1, 2); "
                "return part; }\n"
                "Separate :: () -> int { xs: int[] = [1, 2]; ys := xs; "
                "ys.append(3); return xs.length(); }\n"
                "Grow :: (xs: int[]) -> int[] { xs.append(1); return xs; }\n"
                "Named :: () -> int[] { xs: int[] = []; "
                "xs = append(Array: xs, Item: 7); return xs; }",
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[[200,100,2,1,0,7]]" },
                 { .function = "Count", .arguments = "[[1,2,3]]", .expected = "[35]" },
                 { .function = "Has", .arguments = "[[1,2],2]", .expected = "[true]" },
                 { .function = "Sliced", .arguments = "[[5,6,7,8]]", .expected = "[[6,7]]" },
                 { .function = "Separate", .arguments = "[]", .expected = "[2]" },
                 { .function = "Grow", .arguments = "[[5]]", .expected = "[[5,1]]" },
                 { .function = "Named", .arguments = "[]", .expected = "[[7]]" } } },
    { .name = "array method in an expression",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nBuild :: () -> int[] { xs: int[] = []; xs = "
                "xs.append(1); return xs; }",
      .message = "append changes the array; call it as a statement on a name",
      .line = 2,
      .column = 45 },
    { .name = "strings",
      .source = "package facts\n"
                "Name :: (name: string) -> string {\n"
                "    if (name == \"\") { return \"nobody\"; }\n    return "
                "name;\n}\n"
                "Same :: (a: string, b: string) -> bool { return a == b "
                "&& !(a != b); }\n"
                "Differ :: (a: string, b: string) -> bool { return a != "
                "b; }\n"
                "Quoted :: () -> string { text: string = "
                "\"a\\\"b\\u0041\"; "
                "return text; }",
      .calls = { { .function = "Name", .arguments = "[\"\"]", .expected = "[\"nobody\"]" },
                 { .function = "Name", .arguments = "[\"ada\"]", .expected = "[\"ada\"]" },
                 { .function = "Same", .arguments = "[\"x\",\"x\"]", .expected = "[true]" },
                 { .function = "Same", .arguments = "[\"x\",\"y\"]", .expected = "[false]" },
                 { .function = "Differ", .arguments = "[\"x\",\"y\"]", .expected = "[true]" },
                 { .function = "Quoted", .arguments = "[]", .expected = "[\"a\\\"bA\"]" } } },
    /* Records and union payloads hold float and double values; an integer
     * stored there converts as stage zero converts it. */
    { .name = "floating fields",
      .source = "package facts\n"
                "V :: struct { x: number; y: f64; n: int; }\n"
                "Shape :: union { Circle(radius: f64) | Dot }\n"
                "Make :: (x: number, n: int) -> f64 {\n"
                "    v: V = {x: x, y: 2.5, n: n};\n    v.y += v.x;\n    v.x = "
                "n;\n"
                "    return v.y + v.x;\n}\n"
                "Area :: (r: f64) -> f64 {\n"
                "    switch Shape.Circle(r) { case .Circle(radius): return "
                "radius * radius; case .Dot: return 0.0; }\n"
                "}\n",
      .calls = { { .function = "Make", .arguments = "[1.5,3]", .expected = "[7.0]" },
                 { .function = "Area", .arguments = "[1.5]", .expected = "[2.25]" } } },
    /* int8, int16, uint16 and uint as stage zero computes them: mixed
     * operands widen to the type that holds both, and records hold them. */
    { .name = "narrow integers",
      .source = "package facts\n"
                "P :: struct { a: i8; b: u16; }\n"
                "Narrow :: (a: i8, b: u16, c: u32, d: i16) -> i64 {\n"
                "    x: i8 = a - 1;\n    y: u32 = c / 2;\n"
                "    return i64(x) + i64(b) * 2 + i64(y) - i64(d);\n}\n"
                "Mixed :: (a: i8, b: u8) -> int { return int(a) + int(b); "
                "}\n"
                "Big :: (a: u32, b: int) -> i64 { return i64(a) + "
                "i64(b); }\n"
                "Store :: () -> u16 { v: u16 = 65535; return v; }\n"
                "Make :: (a: i8) -> int { p: P = {a: a, b: 7}; return "
                "int(p.a) + int(p.b); }\n",
      .calls = { { .function = "Narrow", .arguments = "[-5,300,9,-7]", .expected = "[605]" },
                 { .function = "Mixed", .arguments = "[-3,200]", .expected = "[197]" },
                 { .function = "Big", .arguments = "[4000000000,-1]", .expected = "[3999999999]" },
                 { .function = "Store", .arguments = "[]", .expected = "[65535]" },
                 { .function = "Make", .arguments = "[-2]", .expected = "[5]" } } },
    { .name = "clamp label",
      .group = TYPE_SAMPLES,
      .source = "package facts\nClamp :: (v: int) -> u8 { return u8(clamp: "
                "v); }\n",
      .message = "A conversion argument may only be named wrap",
      .line = 2,
      .column = 37 },
    { .name = "mixed integer types",
      .source = "package facts\nMixed :: (a: i8, b: u8) -> int { return a "
                "+ b; }\n",
      .message = "Mismatched integer types i8 and u8; convert one operand",
      .line = 2,
      .column = 41 },
    /* Compound arithmetic rejects differing typed integers before assignment
     * widening, both for a local and for a stored field. */
    { .name = "mixed compound assignment",
      .source = "package facts\nGrow :: (b: u8) -> int { a: int = 1; a += b; "
                "return a; }\n",
      .message = "Mismatched integer types int and u8; convert one operand",
      .line = 2,
      .column = 38 },
    { .name = "mixed compound field assignment",
      .source = "package facts\nNote :: struct { count: int; }\n"
                "Grow :: (b: u8) -> int { n: Note = {count: 1}; n.count += "
                "b; return n.count; }\n",
      .message = "Mismatched integer types int and u8; convert one operand",
      .line = 3,
      .column = 48 },
    { .name = "compound assignment preserves contextual and explicit operands",
      .debug_metadata = true,
      .source = "package facts\nNote :: struct { count: int; }\n"
                "Plain :: (b: u8) -> int { a: int = 1; a = b; return a; }\n"
                "Local :: (b: u8) -> int { a: int = 1; a += int(b); return "
                "a; }\n"
                "Literal :: () -> int { a: int = 1; a += 2; return a; }\n"
                "Field :: (b: u8) -> int { n: Note = {count: 1}; n.count += "
                "int(b); return n.count; }\n"
                "FieldLiteral :: () -> int { n: Note = {count: 1}; n.count += "
                "2; return n.count; }\n"
                "Shift :: (a: int, count: u8) -> int { return a << count; "
                "}\n",
      .calls = { { .function = "Plain", .arguments = "[2]", .expected = "[2]" },
                 { .function = "Local", .arguments = "[2]", .expected = "[3]" },
                 { .function = "Literal", .arguments = "[]", .expected = "[3]" },
                 { .function = "Field", .arguments = "[2]", .expected = "[3]" },
                 { .function = "FieldLiteral", .arguments = "[]", .expected = "[3]" },
                 { .function = "Shift", .arguments = "[3,1]", .expected = "[6]" } } },
    { .name = "mixed compound reference assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Grow :: (a: ^int, b: u8) { a^ += b; }\n",
      .message = "Mismatched integer types int and u8; convert one operand",
      .line = 2,
      .column = 28 },
    { .name = "mixed compound reference field assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Note :: struct { count: int; }\n"
                "Grow :: (n: ^Note, b: u8) { n.count += b; }\n",
      .message = "Mismatched integer types int and u8; convert one operand",
      .line = 3,
      .column = 29 },
    { .name = "mixed compound boxed local assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Touch :: (v: ^int) { v^ += 0; }\n"
                "Grow :: (b: u8) -> int { a: int = 1; Touch(&a); a += b; "
                "return a; }\n",
      .message = "Mismatched integer types int and u8; convert one operand",
      .line = 3,
      .column = 49 },
    { .name = "mixed compound boxed parameter assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Touch :: (v: ^int) { v^ += 0; }\n"
                "Grow :: (a: int, b: u8) -> int { Touch(&a); a += b; "
                "return a; }\n",
      .message = "Mismatched integer types int and u8; convert one operand",
      .line = 3,
      .column = 45 },
    { .name = "compound reference and boxed operands preserve effects",
      .debug_metadata = true,
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Note :: struct { count: int; }\n"
                "Touch :: (v: ^int) { v^ += 0; }\n"
                "Bump :: (v: ^int, b: u8) { v^ += int(b); }\n"
                "BumpField :: (n: ^Note, b: u8) { n.count += int(b); }\n"
                "Source :: (calls: ^int) -> int { calls^ += 1; return 2; }\n"
                "Ref :: () -> int { n: int = 1; Bump(&n, u8(2)); return n; "
                "}\n"
                "RefField :: () -> int { n: Note = {count: 1}; BumpField(&n, "
                "u8(2)); return n.count; }\n"
                "Box :: () -> int { n: int = 1; Touch(&n); n += int(u8(2)); "
                "return n; }\n"
                "BoxParam :: (n: int) -> int { Touch(&n); n += 2; return n; }\n"
                "Once :: () -> int { n: Note = {count: 1}; calls: int = 0; "
                "n.count += Source(&calls); return n.count * 10 + calls; }\n"
                "Bits :: (v: int) -> int { n: int = 6; n &= v; n |= 8; n ^= 1; "
                "return n; }\n"
                "Clamp :: (v: int) -> u8 { return u8(v); }\n",
      .calls = { { .function = "Ref", .arguments = "[]", .expected = "[3]" },
                 { .function = "RefField", .arguments = "[]", .expected = "[3]" },
                 { .function = "Box", .arguments = "[]", .expected = "[3]" },
                 { .function = "BoxParam", .arguments = "[1]", .expected = "[3]" },
                 { .function = "Once", .arguments = "[]", .expected = "[31]" },
                 { .function = "Bits", .arguments = "[3]", .expected = "[11]" },
                 { .function = "Clamp", .arguments = "[300]", .expected = "[255]" } } },
    { .name = "mixed compound bitwise assignment",
      .source = "package facts\nGrow :: (b: u8) -> int { a: int = 1; a &= b; "
                "return a; }\n",
      .message = "Mismatched integer types int and u8; convert one operand",
      .line = 2,
      .column = 38 },
    { .name = "constant division by zero",
      .group = TYPE_SAMPLES,
      .source = "package facts\nRest :: (a: int) -> int { return a % (2 - 2); "
                "}\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 34 },
    { .name = "compound division by zero",
      .group = TYPE_SAMPLES,
      .source = "package facts\nHalve :: (a: int) -> int { b := a; b /= 0; "
                "return b; }\n",
      .message = "Constant division by zero",
      .line = 2,
      .column = 36 },
    { .name = "folded constant outside its type",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBig :: () -> int { return 1 << 32; }\n",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 27 },
    { .name = "conversion outside its type",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: u8 : u8(300);\n",
      .message = "Constant does not fit in u8; use u8(wrap: value)",
      .line = 2,
      .column = 11 },
    { .name = "negated unsigned constant",
      .group = TYPE_SAMPLES,
      .source = "package facts\nBad: u8 : -u8(1);\n",
      .message = "Cannot negate an unsigned constant",
      .line = 2,
      .column = 11 },
    { .name = "signed shift count",
      .group = TYPE_SAMPLES,
      .source = "package facts\nShift :: (a: int, n: int) -> int { return a << "
                "n; }\n",
      .message = "Shift count must be unsigned; convert it with u32(value)",
      .line = 2,
      .column = 48 },
    /* Integer operations never fail at run time: unsigned negation wraps, a
     * count at or past the width gives 0 and a zero divisor gives 0. */
    { .name = "total integer operations",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Negate :: (a: u8) -> u8 { return -a; }\n"
                "Wide :: (a: u64) -> u64 { return -a; }\n"
                "Far :: (a: int) -> int { return a << 40; }\n"
                "Fill :: (a: int) -> int { return a >> 40; }\n"
                "Divide :: (a: int, b: int) -> int { return a / b + a % b; }\n",
      .calls = { { .function = "Negate", .arguments = "[1]", .expected = "[255]" },
                 { .function = "Wide", .arguments = "[1]", .expected = "[18446744073709551615]" },
                 { .function = "Far", .arguments = "[3]", .expected = "[0]" },
                 { .function = "Fill", .arguments = "[-5]", .expected = "[-1]" },
                 { .function = "Fill", .arguments = "[5]", .expected = "[0]" },
                 { .function = "Divide", .arguments = "[7,0]", .expected = "[7]" } } },
    /* An untyped constant expression that names a constant takes its type
     * from its use, and only integer division by zero is an error. */
    { .name = "untyped constant expressions",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Count: u32 : 3;\n"
                "Eight :: (x: i64) -> bool { return x == (1 << Count); }\n"
                "Huge :: (f: f64) -> bool { return f / 0 > 1000000.0; }\n",
      .calls = { { .function = "Eight", .arguments = "[8]", .expected = "[true]" },
                 { .function = "Huge", .arguments = "[1]", .expected = "[true]" } } },
    /* A shift has its left operand's type, and a count past that type's
     * maximum clamps to it. Exact constant shifts retain the written count. */
    { .name = "shift counts clamp",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Literal :: (a: u8) -> u8 { return a << 300; }\n"
                "Counted :: (a: u8, n: u32) -> u8 { return a << n; }\n"
                "Signed :: (a: i8, n: u64) -> i8 { return a >> n; }\n",
      .calls = { { .function = "Literal", .arguments = "[1]", .expected = "[0]" },
                 { .function = "Counted", .arguments = "[1,300]", .expected = "[0]" },
                 { .function = "Counted", .arguments = "[1,7]", .expected = "[128]" },
                 { .function = "Signed", .arguments = "[-5,4096]", .expected = "[-1]" } } },
    /* uint64 and byte have every operator, unsigned ordering and division
     * included, as stage zero computes them. */
    { .name = "unsigned arithmetic",
      .source = "package facts\n"
                "Diff :: (a: u64, b: u64) -> u64 { return a - b; }\n"
                "Less :: (a: u64, b: u64) -> bool { return a < b; }\n"
                "Div :: (a: u64, b: u64) -> u64 { return a / b + a % b; }\n"
                "Bytes :: (a: u8, b: u8) -> u8 { return a + b * 2 - 1; }\n"
                "Order :: (a: u8, b: u8) -> bool { return a > b && a >= 3; }\n"
                "Shift :: (a: u8) -> u8 { return (a << 1) >> 2; }\n",
      .calls = { { .function = "Diff", .arguments = "[10,3]", .expected = "[7]" },
                 { .function = "Less", .arguments = "[9223372036854775808,1]", .expected = "[false]" },
                 { .function = "Less", .arguments = "[1,9223372036854775808]", .expected = "[true]" },
                 { .function = "Div",
                   .arguments = "[18446744073709551615,10]",
                   .expected = "[1844674407370955166]" },
                 { .function = "Bytes", .arguments = "[20,30]", .expected = "[79]" },
                 { .function = "Order", .arguments = "[5,3]", .expected = "[true]" },
                 { .function = "Shift", .arguments = "[12]", .expected = "[6]" } } },
    /* Constants fold integer expressions of literals and other constants,
     * declared in any order and in an imported package. */
    { .name = "constant expressions",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "import lib \"project:lib\"\n"
                "Area :: Width * Height;\n"
                "Width :: (Unit << 2) + 1;\n"
                "Height: int : -(Width - 7) / 2 % 3;\n"
                "Mask: i64 : ~lib.Bits & 0xFFFF;\n"
                "Small: u8 : 0x10 | 2;\n"
                "Low: u8 : ~0x0F;\n"
                "Unit :: 2;\n"
                "Folded :: () -> i64 { return i64(Area) * 1000 + Mask + "
                "i64(Small) + i64(Low); }\n",
      .library = "package lib\npub Bits: i64 : 1 << 40 | 0xF0;\n",
      .calls = { { .function = "Folded", .arguments = "[]", .expected = "[56553]" } } },
    /* An omitted argument takes its parameter's literal default. */
    { .name = "defaults",
      .source = "package facts\n"
                "Add :: (a: int, b: int = -10, c: i64 = 0x10) -> i64 { "
                "return i64(a + b) + c; }\n"
                "Greet :: (name: string = \"you\", loud: bool = true) -> "
                "string {\n"
                "    if (loud) { return format_text(\"HI {}\", name); }\n"
                "    return name;\n}\n"
                "Use :: () -> i64 { return Add(1) + Add(1, 2) + Add(1, 2, "
                "3); }\n"
                "Hello :: () -> string { return Greet(); }\n",
      .calls = { { .function = "Add", .arguments = "[5,6,7]", .expected = "[18]" },
                 { .function = "Use", .arguments = "[]", .expected = "[32]" },
                 { .function = "Hello", .arguments = "[]", .expected = "[\"HI you\"]" },
                 { .function = "Greet", .arguments = "[\"ada\",false]", .expected = "[\"ada\"]" } } },
    /* An integer switch tests its arms in order and runs the default arm
     * after them all, wherever it is written. */
    { .name = "integer switch",
      .source = "package facts\n"
                "Kind :: (c: int) -> int {\n"
                "    switch c {\n"
                "        case 61: return 1;\n"
                "        case: return 0;\n"
                "        case -62: return 2;\n"
                "    }\n"
                "}\n"
                "Spaces :: (xs: int[]) -> int {\n"
                "    total: int = 0;\n"
                "    for x in xs {\n"
                "        b := u8(x);\n"
                "        switch b { case 32: total += 1; case 10: total += "
                "100; }\n"
                "    }\n"
                "    return total;\n}\n",
      .calls = { { .function = "Kind", .arguments = "[61]", .expected = "[1]" },
                 { .function = "Kind", .arguments = "[-62]", .expected = "[2]" },
                 { .function = "Kind", .arguments = "[7]", .expected = "[0]" },
                 { .function = "Spaces", .arguments = "[[32,10,32,5]]", .expected = "[102]" } } },
    /* Range loops count as stage zero's do: inclusive and half-open,
     * forward and reversed, ending at a bound without wrapping; `for x, i`
     * names an array's item and its index, and `for <` walks it backwards. */
    { .name = "ranges",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Sum :: (first: int, last: int) -> int {\n"
                "    total: int = 0;\n    for i in first..last { total += i; "
                "}\n"
                "    return total;\n}\n"
                "Below :: (n: int) -> int {\n"
                "    total: int = 0;\n"
                "    for i in 0..<n { if (i == 2) { continue; } total += i; }\n"
                "    return total;\n}\n"
                "Down :: (n: int) -> int {\n"
                "    total: int = 0;\n"
                "    for < i in 0..n { total = total * 10 + i; if (i == 1) { "
                "break; } }\n"
                "    return total;\n}\n"
                "DownBelow :: (n: int) -> int {\n"
                "    total: int = 0;\n    for < i in 0..<n { total = total * "
                "10 + i; }\n"
                "    return total;\n}\n"
                "Count :: (first: int, last: int) -> int {\n"
                "    count: int = 0;\n    for i in first..last { count += 1; "
                "}\n"
                "    for < i in first..last { count += 1; }\n"
                "    return count;\n}\n"
                "Small :: (b: u8) -> int {\n"
                "    total: int = 0;\n    for i in 0..b { total += 1; }\n"
                "    return total;\n}\n"
                "Keys :: (xs: int[]) -> int {\n"
                "    total: int = 0;\n    for x, i in xs { total += i * x; }\n"
                "    return total;\n}\n"
                "Back :: (xs: int[]) -> int {\n"
                "    total: int = 0;\n"
                "    for < x, i in xs { total = total * 100 + x * 10 + i; }\n"
                "    return total;\n}\n",
      .calls = { { .function = "Sum", .arguments = "[1,4]", .expected = "[10]" },
                 { .function = "Below", .arguments = "[5]", .expected = "[8]" },
                 { .function = "Down", .arguments = "[3]", .expected = "[321]" },
                 { .function = "DownBelow", .arguments = "[3]", .expected = "[210]" },
                 { .function = "DownBelow", .arguments = "[0]", .expected = "[0]" },
                 { .function = "Count", .arguments = "[2147483646,2147483647]", .expected = "[4]" },
                 { .function = "Count", .arguments = "[-2147483648,-2147483647]", .expected = "[4]" },
                 { .function = "Small", .arguments = "[3]", .expected = "[4]" },
                 { .function = "Keys", .arguments = "[[5,6,7]]", .expected = "[20]" },
                 { .function = "Back", .arguments = "[[1,2,3]]", .expected = "[322110]" } } },
    /* Observable sequence semantics use the supported package subset: ordinary
     * parameters, unnamed results, scalar record fields and inferred optionals.
     * Multi() below supplies a native result bank; authored named collection
     * results and nonnumeric state remain outside this fixture's subset. */
    { .name = "value sequence copies",
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Point :: struct { x: int; y: int; }\n"
          "Change :: (values: int[]) -> int { values[0] = 8; values.append(9); return values.length() * 10 + values[0]; }\n"
          "Identity :: (values: int[]) -> int[] { return values; }\n"
          "Assignment :: () -> int { original: int[] = [1, 2]; copied := original; copied[0] = 7; copied.append(3); return original[0] * 1000 + original.length() * 100 + copied[0] * 10 + copied.length(); }\n"
          "Parameter :: () -> int { original: int[] = [1, 2]; changed := Change(original); return original[0] * 100 + original.length() * 10 + changed; }\n"
          "Returned :: () -> int { original: int[] = [1, 2]; copied := Identity(original); copied[1] = 9; return original[1] * 10 + copied[1]; }\n"
          "Nested :: () -> int { original: int[][] = [[1, 2], [3]]; copied := original; copied[0].append(7); copied[1][0] = 8; return original[0].length() * 1000 + original[1][0] * 100 + copied[0].length() * 10 + copied[1][0]; }\n"
          "Maps :: () -> int { original: [int: int[]] = [1: [2, 3]]; copied := original; copied[1].append(4); copied[2] = [9]; delete copied[1]; return original[1].length() * 100 + original[1][0] * 10 + copied[2][0]; }\n"
          "Records :: () -> int { original: Point[] = [{x: 2, y: 3}]; copied := original; copied[0].x = 9; return original[0].x * 100 + copied[0].x * 10 + copied[0].y; }\n"
          "Pointers :: () -> int { value: int = 1; other: int = 9; original: ^int[] = [&value]; copied := original; copied[0]^ = 5; copied[0] = &other; return original[0]^ * 100 + copied[0]^ * 10 + value; }\n"
          "Cloned :: () -> int { original: int[] = [4, 5]; copied := original.clone(); copied[0] = 8; return original[0] * 10 + copied[0]; }\n"
          "Empty :: () -> int { original: int[] = []; copied := original; copied.append(7); return original.length() * 100 + copied.length() * 10 + copied[0]; }\n",
      .calls = {
          { .function = "Assignment", .arguments = "[]",
            .expected = "[1273]" },
          { .function = "Parameter", .arguments = "[]",
            .expected = "[158]" },
          { .function = "Returned", .arguments = "[]",
            .expected = "[29]" },
          { .function = "Nested", .arguments = "[]",
            .expected = "[2338]" },
          { .function = "Maps", .arguments = "[]",
            .expected = "[229]" },
          { .function = "Records", .arguments = "[]",
            .expected = "[293]" },
          { .function = "Pointers", .arguments = "[]",
            .expected = "[595]" },
          { .function = "Cloned", .arguments = "[]",
            .expected = "[48]" },
          { .function = "Empty", .arguments = "[]",
            .expected = "[17]" },
      } },
    { .name = "strict sequence ranges",
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Bracket :: () -> int { original: int[] = [1, 2, 3, 4]; selected := original[1..<3]; if selected { selected[0] = 9; return original[1] * 100 + selected[0] * 10 + selected.length(); } return -1; }\n"
          "Method :: () -> int { original: int[] = [1, 2, 3]; selected := original.slice(0..<2); if selected { selected[1] = 8; return original[1] * 10 + selected[1]; } return -1; }\n"
          "Empty :: () -> int { original: int[] = [1, 2]; selected := original[2..<2]; if selected { return selected.length() + 1; } return -1; }\n"
          "EmptySource :: () -> int { original: int[] = []; selected := original[0..<0]; if selected { return selected.length() + 1; } return -1; }\n"
          "Bounds :: (first: int, last: int) -> int { original: int[] = [1, 2]; selected := original.slice(first..<last); if selected { return selected.length() + 1; } return 0; }\n"
          "Text :: () -> string { original: string = \"abcd\"; selected := original[1..<3]; if selected { copied := selected.clone(); return format_text(\"{}|{}\", selected, copied); } return \"invalid\"; }\n"
          "Bytes :: () -> int { original: string = \"a\\u03bbz\"; selected := original[1..<2]; if selected { return selected.length(); } return -1; }\n"
          "EmptyText :: () -> int { original: string = \"abc\"; selected := original[3..<3]; if selected { return selected.length() + 1; } return -1; }\n",
      .calls = {
          { .function = "Bracket", .arguments = "[]",
            .expected = "[292]" },
          { .function = "Method", .arguments = "[]",
            .expected = "[28]" },
          { .function = "Empty", .arguments = "[]",
            .expected = "[1]" },
          { .function = "EmptySource", .arguments = "[]",
            .expected = "[1]" },
          { .function = "Bounds", .arguments = "[-1,1]",
            .expected = "[0]" },
          { .function = "Bounds", .arguments = "[1,0]",
            .expected = "[0]" },
          { .function = "Bounds", .arguments = "[0,3]",
            .expected = "[0]" },
          { .function = "Bounds", .arguments = "[2,2]",
            .expected = "[1]" },
          { .function = "Bounds", .arguments = "[0,2]",
            .expected = "[3]" },
          { .function = "Text", .arguments = "[]",
            .expected = "[\"bc|bc\"]" },
          { .function = "Bytes", .arguments = "[]",
            .expected = "[1]" },
          { .function = "EmptyText", .arguments = "[]",
            .expected = "[1]" },
      } },
    { .name = "pointer sequence views",
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Owner :: () -> ^(int[]) { values: int[] = [4, 5]; return &values; }\n"
          "Touch :: (window: view int[]) { window[0] = 7; }\n"
          "ReadWindow :: (window: view int[]) -> int { return window.length() * 100 + window[0]; }\n"
          "Follow :: () -> int { values: int[] = [1, 2, 3]; pointer := &values; selected := pointer.view(1..<3); if selected { selected[0] = 8; previous := values[1]; pointer^ = [4, 5, 6, 7]; selected[1] = 9; return previous * 1000 + values[1] * 100 + values[2] * 10 + selected.length(); } return -1; }\n"
          "OrdinaryCopy :: () -> int { values: int[] = [1, 2]; copied := values; pointer := &values; selected := pointer.view(0..<2); if selected { selected[1] = 7; return values[1] * 100 + copied[1] * 10 + selected[1]; } return -1; }\n"
          "Grow :: () -> int { values: int[] = [1, 2, 3]; pointer := &values; selected := pointer.view(1..<3); if selected { pointer.append(4); } if selected { selected[0] = 8; return values[1] * 100 + selected.length() * 10 + values.length(); } return -1; }\n"
          "Cloned :: () -> int { values: int[] = [1, 2, 3]; pointer := &values; selected := pointer.view(0..<2); if selected { copied := selected.clone(); copied[0] = 9; return values[0] * 100 + selected[0] * 10 + copied[0]; } return -1; }\n"
          "Sliced :: () -> int { values: int[] = [1, 2, 3]; pointer := &values; selected := pointer.view(0..<2); if selected { copied := selected[0..<1]; if copied { copied[0] = 7; return values[0] * 100 + selected[0] * 10 + copied[0]; } } return -1; }\n"
          "Empty :: () -> int { values: int[] = []; pointer := &values; selected := pointer.view(0..<0); if selected { return selected.length() + 1; } return -1; }\n"
          "Escaped :: () -> int { pointer := Owner(); selected := pointer.view(0..<2); if selected { selected[0] = 8; return pointer^[0] * 10 + selected[1]; } return -1; }\n"
          "InvalidRange :: () -> int { values: int[] = [1]; pointer := &values; selected := pointer.view(2..<2); if selected { return 1; } return 0; }\n"
          "Shrunk :: () -> int { values: int[] = [1, 2, 3]; pointer := &values; selected := pointer.view(1..<3); if selected { pointer^ = [9]; return selected.length(); } return -1; }\n"
          "Parameter :: () -> int { values: int[] = [1, 2, 3]; pointer := &values; selected := pointer.view(1..<3); if selected { Touch(selected); } if selected { return values[1] * 100 + ReadWindow(selected); } return -1; }\n",
      .calls = {
          { .function = "Follow", .arguments = "[]",
            .expected = "[8592]" },
          { .function = "OrdinaryCopy", .arguments = "[]",
            .expected = "[727]" },
          { .function = "Grow", .arguments = "[]",
            .expected = "[824]" },
          { .function = "Cloned", .arguments = "[]",
            .expected = "[119]" },
          { .function = "Sliced", .arguments = "[]",
            .expected = "[117]" },
          { .function = "Empty", .arguments = "[]",
            .expected = "[1]" },
          { .function = "Escaped", .arguments = "[]",
            .expected = "[85]" },
          { .function = "InvalidRange", .arguments = "[]",
            .expected = "[0]" },
          { .function = "Shrunk", .arguments = "[]",
            .expected = NULL },
          { .function = "Parameter", .arguments = "[]",
            .expected = "[907]" },
      } },
    { .name = "value arguments and result banks",
      .group = COLLECTION_SAMPLES,
      .c_string_probe = true,
      .probe_calls = true,
      .source =
          "package facts\n"
          "Change :: (pointer: ^(int[])) -> int { pointer^[0] = 9; pointer.append(3); return 7; }\n"
          "Observe :: (values: int[], changed: int) -> int { return values[0] * 100 + values.length() * 10 + changed; }\n"
          "ObservePointer :: (pointer: ^(int[]), changed: int) -> int { return pointer^[0] + pointer^.length() * 10 + changed; }\n"
          "ChangeRows :: (pointer: ^(int[][])) -> int { pointer^[0].append(7); return 0; }\n"
          "ObserveRows :: (values: int[][], changed: int) -> int { return values[0][0] * 10 + values[0].length() + changed; }\n"
          "ChangeMap :: (pointer: ^([int: int])) -> int { pointer^[1] = 8; return 2; }\n"
          "ObserveMap :: (values: [int: int], changed: int) -> int { return values[1] * 10 + changed; }\n"
          "EarlierArray :: () -> int { values: int[] = [1, 2]; observed := Observe(values, Change(&values)); return observed * 1000 + values[0] * 10 + values.length(); }\n"
          "EarlierNested :: () -> int { values: int[][] = [[1]]; observed := ObserveRows(values, ChangeRows(&values)); return observed * 10 + values[0].length(); }\n"
          "EarlierPointer :: () -> int { values: int[] = [1]; return ObservePointer(&values, Change(&values)); }\n"
          "EarlierMap :: () -> int { values: [int: int] = [1: 3]; observed := ObserveMap(values, ChangeMap(&values)); return observed * 10 + values[1]; }\n"
          "TupleLifetime :: () -> int { p := Multi(); total: int = 0; if total == 0 { q := p; extracted := q.values; extracted[0] = 8; total = extracted[0] + q.values[0]; } return total * 100 + p.values[0] * 10 + p.stamp; }\n"
          "KeepTupleValue :: () -> int[] { p := Multi(); if p.stamp > 0 { q := p; return q.values; } return []; }\n"
          "TupleReturned :: () -> int { values := KeepTupleValue(); values.append(5); return values[0] * 100 + values[2] * 10 + values.length(); }\n",
      .calls = {
          { .function = "EarlierArray", .arguments = "[]",
            .expected = "[127093]" },
          { .function = "EarlierNested", .arguments = "[]",
            .expected = "[112]" },
          { .function = "EarlierPointer", .arguments = "[]",
            .expected = "[36]" },
          { .function = "EarlierMap", .arguments = "[]",
            .expected = "[328]" },
          { .function = "TupleLifetime", .arguments = "[]",
            .expected = "[1139]" },
          { .function = "TupleReturned", .arguments = "[]",
            .expected = "[353]" },
      } },
    { .name = "arrays",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Squares :: (n: int) -> int[] {\n"
                "    out: int[] = [];\n    i: int = 0;\n"
                "    while (i < n) { out = append(out, i * i); i += 1; }\n"
                "    return out;\n}\n"
                "Total :: (xs: int[]) -> int {\n"
                "    sum: int = 0;\n"
                "    for (x in xs) { if (x == 4) { continue; } sum += x; "
                "}\n"
                "    return sum;\n}\n"
                "Edit :: () -> int {\n"
                "    xs: int[] = [1, 2, 3];\n    xs[1] = 20;\n    xs[2] += 5;\n"
                "    copy := xs;\n    more: int[] = [7, 8];\n    xs = "
                "extend(xs, more);\n"
                "    return xs[1] + xs[2] + length(xs) * 100 + length(copy) * "
                "1000;\n}\n"
                "Points :: (n: int) -> Point[] {\n"
                "    ps: Point[] = [];\n    i: int = 0;\n"
                "    while (i < n) { p: Point = {x: i, y: i * 2}; ps = "
                "append(ps, p); i += 1; }\n"
                "    return ps;\n}\n"
                "SumY :: (ps: Point[]) -> int {\n"
                "    total: int = 0;\n"
                "    for (p in ps) { total += p.y; if (total > 100) { "
                "break; } }\n"
                "    return total + ps[0].x;\n}\n"
                "Words :: () -> string[] {\n"
                "    ws: string[] = [\"a\", \"b\", \"c\"];\n    return "
                "slice(ws, 1, 2);\n}\n"
                "Counts :: (n: int) -> int { return length(Squares(n)) "
                "+ Total(Squares(n)); }\n"
                "Pick :: (xs: int[], i: int) -> int { return xs[i]; }\n"
                "Index :: (i: int) -> int { return i; }\n"
                "Nested :: (ps: Point[]) -> int {\n"
                "    total: int = 0;\n    ws: string[] = [\"a\", \"bb\"];\n"
                "    for (p in ps) {\n        if (p.x == 0) { continue; }\n"
                "        for (w in ws) { if (w == \"a\") { continue; } "
                "total += p.x * 10; }\n    }\n"
                "    counts: int[] = [0, 0, 0];\n    counts[Index(2)] += 7;\n"
                "    return total + counts[2];\n}",
      .calls = { { .function = "Squares", .arguments = "[4]", .expected = "[[0,1,4,9]]" },
                 { .function = "Total", .arguments = "[[0,1,4,9]]", .expected = "[10]" },
                 { .function = "Edit", .arguments = "[]", .expected = "[3528]" },
                 { .function = "Points", .arguments = "[3]", .expected = "[[{\"x\":0,\"y\":0},{\"x\":1,\"y\":2},{\"x\":2,\"y\":4}]]" },
                 { .function = "SumY",
                   .arguments = "[[{\"x\":5,\"y\":1},{\"x\":0,\"y\":2}]]",
                   .expected = "[8]" },
                 { .function = "Words", .arguments = "[]", .expected = "[[\"b\",\"c\"]]" },
                 { .function = "Counts", .arguments = "[3]", .expected = "[4]" },
                 { .function = "Pick", .arguments = "[[1,2],2]", .expected = NULL },
                 { .function = "Nested",
                   .arguments = "[[{\"x\":0,\"y\":1},{\"x\":2,\"y\":0},{\"x\":"
                                "3,\"y\":0}]]",
                   .expected = "[57]" } } },
    { .name = "maps",
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Point :: struct { x: int; y: int; }\n"
          "Ages :: () -> int {\n"
          "    ages: [string: int] = [\"Ada\": 36, \"Bea\": 41];\n"
          "    return ages[\"Ada\"] + ages[\"Cid\"] * 1000 + ages[\"Bea\"] * "
          "100;\n}\n"
          "Name :: (key: int) -> string {\n"
          "    names: [int: string] = [1: \"one\", 2: \"two\"];\n    return "
          "names[key];\n}\n"
          "Y :: (key: string) -> int {\n"
          "    ps: [string: Point] = [\"a\": {x: 1, y: 2}];\n    return "
          "ps[key].y;\n}\n"
          "Flag :: () -> bool { flags: [int: bool] = [:]; return flags[3]; }\n"
          "Count :: (n: int) -> int {\n"
          "    squares: [int: int] = [:];\n    i: int = 0;\n"
          "    while (i < n) { squares[i] = i * i; i += 1; }\n"
          "    delete squares[1];\n    squares.delete(2);\n    "
          "squares.delete(99);\n"
          "    found: int = 0;\n"
          "    if (0 in squares) { found += 1; }\n"
          "    if (1 not in squares) { found += 10; }\n"
          "    if (squares.has(3)) { found += 100; }\n"
          "    if (!squares.has(2)) { found += 1000; }\n"
          "    return found * 100000 + squares[3] * 1000 + squares[1];\n}\n"
          "Points :: () -> int {\n"
          "    ps: [string: Point] = [:];\n    ps[\"a\"] = {x: 3, y: 4};\n"
          "    p: Point = {x: 5, y: 6};\n    ps[\"b\"] = p;\n    p.x = 7;\n"
          "    return ps[\"a\"].y * 100 + ps[\"b\"].x;\n}\n"
          "Walk :: () -> string {\n"
          "    ages: [string: int] = [\"Ada\": 3, \"Bea\": 1, \"Cid\": 2];\n"
          "    ages[\"Ada\"] = 4;\n    delete ages[\"Bea\"];\n    "
          "ages[\"Bea\"] = 5;\n"
          "    out: string = \"\";\n    total: int = 0;\n"
          "    for (name, age in ages) { out = format_text(\"{}{}{}\", out, "
          "name, age); total += age; }\n"
          "    for (age in ages) { if (age == 2) { continue; } total += age * "
          "100; }\n"
          "    ps: [int: Point] = [1: {x: 2, y: 3}, 4: {x: 5, y: 6}];\n"
          "    for (key, p in ps) { total += key * 1000 + p.y * 10000; }\n"
          "    return format_text(\"{} {}\", out, total);\n}\n"
          "Make :: (n: int) -> [int: string] {\n"
          "    made: [int: string] = [:];\n    i: int = 0;\n"
          "    while (i < n) { made[i] = format_text(\"v{}\", i); i += 1; }\n  "
          "  return made;\n}\n"
          "Join :: (m: [int: string]) -> string {\n"
          "    out: string = \"\";\n    for (k, v in m) { out = "
          "format_text(\"{}{}={};\", out, k, v); }\n"
          "    return out;\n}\n"
          "Copies :: () -> string {\n"
          "    a := Make(3);\n    b := a;\n    b[0] = \"changed\";\n    delete "
          "b[2];\n"
          "    return format_text(\"{}|{}\", Join(a), Join(b));\n}",
      .calls = { { .function = "Ages", .arguments = "[]", .expected = "[4136]" },
                 { .function = "Name", .arguments = "[2]", .expected = "[\"two\"]" },
                 { .function = "Name", .arguments = "[3]", .expected = "[\"\"]" },
                 { .function = "Y", .arguments = "[\"a\"]", .expected = "[2]" },
                 { .function = "Y", .arguments = "[\"b\"]", .expected = NULL },
                 { .function = "Flag", .arguments = "[]", .expected = "[false]" },
                 { .function = "Count", .arguments = "[5]", .expected = "[111109000]" },
                 { .function = "Points", .arguments = "[]", .expected = "[405]" },
                 { .function = "Walk", .arguments = "[]", .expected = "[\"Ada4Cid2Bea5 95911\"]" },
                 { .function = "Copies",
                   .arguments = "[]",
                   .expected = "[\"0=v0;1=v1;2=v2;|0=changed;1=v1;\"]" } } },
    { .name = "references",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Bump :: (v: ^int) { v^ += 1; }\n"
                "Set :: (v: ^int, to: int) { v^ = to; }\n"
                "Twice :: (v: ^int) { Bump(v); Bump(v); }\n"
                "Rename :: (s: ^string) { s^ = format_text(\"{}!\", s^); }\n"
                "Swap :: (p: ^Point) { p^ = {x: p.y, y: p.x}; }\n"
                "Grow :: (xs: ^(int[])) { item: int = 7; xs^ = append(xs^, "
                "item); "
                "}\n"
                "Inc :: (k: int) -> int { Bump(&k); return k; }\n"
                "Nudge :: (p: ^Point) { p.x = 9; p.y += 5; Bump(&p.x); }\n"
                "Count :: () -> int { c: int = 2; c += 3; Bump(&c); c = c * 2; "
                "return c; }\n"
                "Run :: () -> int {\n"
                "    n: int = 1;\n    Bump(&n);\n    Twice(&n);\n    Set(&n, n "
                "* "
                "10);\n"
                "    name: string = \"a\";\n    Rename(&name);\n"
                "    p: Point = {x: 1, y: 2};\n    Swap(&p);\n    Bump(&p.x);\n"
                "    xs: int[] = [1];\n    Grow(&xs);\n"
                "    total := n + p.x * 100 + p.y * 1000 + length(xs) * 10000 "
                "+ Inc(5) * 1000000;\n"
                "    if (name == \"a!\") { total += 100000; }\n"
                "    return total;\n}\n"
                "Moved :: () -> int { p: Point = {x: 1, y: 2}; Nudge(&p); "
                "return p.x * 100 + p.y; }",
      .calls = { { .function = "Run", .arguments = "[]", .expected = "[6121340]" },
                 { .function = "Inc", .arguments = "[1]", .expected = "[2]" },
                 { .function = "Count", .arguments = "[]", .expected = "[12]" },
                 { .function = "Moved", .arguments = "[]", .expected = "[1007]" } } },
    /* Engine array kernels take arrays of managed pointers without retaining
     * them, so the native argument boundary does not reject or walk them. */
    { .name = "managed pointer arrays",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Count :: () -> int { x: int = 1; y: int = 2; "
                "ps: ^int[] = [&x]; ps.append(&y); ps[1]^ = 7; "
                "return length(ps) * 10 + y; }\n",
      .calls = { { .function = "Count", .arguments = "[]", .expected = "[27]" } } },
    /* Fixed scalar-result calls exercise aliases, escaping storage,
     * grouped types, widening, aggregate replacement, and one two-node cycle. */
    { .name = "managed pointers",
      .group = COLLECTION_SAMPLES,
      .source =
          "package facts\n"
          "Point :: struct { x: int; y: int; }\n"
          "Holder :: struct { value: int; alias: ^int; }\n"
          "Link :: struct { value: int; next: ^Link?; }\n"
          "Bump :: (value: ^int) { value^ += 1; }\n"
          "Make :: () -> ^Point { value: Point = {x: 3, y: 4}; return &value; "
          "}\n"
          "Scalar :: () -> ^int { value: int = 2; return &value; }\n"
          "EscapeCopy :: (n: int) -> ^int { p := &n; p^ += 1; return p; }\n"
          "Parameter :: () -> int { value: int = 3; p := EscapeCopy(value); "
          "return value * 10 + p^; }\n"
          "Alias :: () -> int { value: int = 1; p: ^int = &value; q := p; "
          "q^ = 7; Bump(p); return value * 100 + p^ * 10 + q^; }\n"
          "Escaped :: () -> int { p := Make(); p.x = 9; return p^.x * 10 + "
          "p.y; }\n"
          "Field :: () -> int { value: Point = {x: 2, y: 4}; p := &value; "
          "x := &value.x; x^ = 5; return p.x * 10 + p^.y; }\n"
          "Copies :: () -> int { value: int = 2; original: Holder = "
          "{value: 3, alias: &value}; copied := original; copied.value = 7; "
          "copied.alias^ = 9; return original.value * 100 + copied.value * 10 "
          "+ value; }\n"
          "Distinct :: () -> int { a := Scalar(); b := Scalar(); a^ = 11; "
          "return a^ * 100 + b^; }\n"
          "Nullable :: () -> int { p: ^int? = nil; value: int = 3; p = &value; "
          "if p != nil { return p^; } return 0; }\n"
          "ArrayCopies :: () -> int { x: int = 1; y: int = 2; "
          "original: ^int[] = [&x, &y]; copied := original; copied[0]^ = 5; "
          "return original[0]^ * 100 + original[1]^ * 10 + "
          "copied[1]^; }\n"
          "ReadGrouped :: (values: (^int)[]) -> int { return values[0]^; }\n"
          "Grouped :: () -> int { value: int = 4; return "
          "ReadGrouped([&value]); }\n"
          "Widened :: () -> i64 { small: int = 3; wide: i64 = small; "
          "p := &wide; p^ += 2; return p^; }\n"
          "WholeArray :: () -> int { values: int[] = [1, 2]; "
          "p: ^(int[]) = &values; p^ = [8, 9, 10]; "
          "return length(values) * 100 + values[0] * 10 + values[2]; }\n"
          "Cycle :: () -> int { a: Link = {value: 1, next: nil}; "
          "b: Link = {value: 2, next: nil}; p := &a; q := &b; "
          "p.next = q; q.next = p; return p.value * 10 + q.value; }",
      .calls = { { .function = "Alias", .arguments = "[]", .expected = "[888]" },
                 { .function = "Escaped", .arguments = "[]", .expected = "[94]" },
                 { .function = "Field", .arguments = "[]", .expected = "[54]" },
                 { .function = "Copies", .arguments = "[]", .expected = "[379]" },
                 { .function = "Distinct", .arguments = "[]", .expected = "[1102]" },
                 { .function = "Nullable", .arguments = "[]", .expected = "[3]" },
                 { .function = "ArrayCopies", .arguments = "[]", .expected = "[522]" },
                 { .function = "Parameter", .arguments = "[]", .expected = "[34]" },
                 { .function = "Cycle", .arguments = "[]", .expected = "[12]" },
                 { .function = "Grouped", .arguments = "[]", .expected = "[4]" },
                 { .function = "Widened", .arguments = "[]", .expected = "[5]" },
                 { .function = "WholeArray", .arguments = "[]", .expected = "[390]" } } },
    /* A compound write reads its pointer target once, before its value, as
     * stage zero caches the target. Each call adds to `calls`, so receivers
     * and values evaluate exactly once. */
    { .name = "managed pointer compound order",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Set :: (q: ^int, calls: ^int) -> int { calls^ += 10; "
                "q^ = 10; return 2; }\n"
                "SetX :: (q: ^Point, calls: ^int) -> int { calls^ += 10; "
                "q.x = 10; return 2; }\n"
                "Pick :: (p: ^int, calls: ^int) -> ^int { calls^ += 1; "
                "return p; }\n"
                "PickPoint :: (p: ^Point, calls: ^int) -> ^Point { "
                "calls^ += 1; return p; }\n"
                "Through :: () -> int { value: int = 1; calls: int = 0; "
                "p := &value; Pick(p, &calls)^ += Set(p, &calls); "
                "return value * 100 + calls; }\n"
                "Local :: () -> int { value: int = 1; calls: int = 0; "
                "p := &value; value += Set(p, &calls); "
                "return value * 100 + calls; }\n"
                "Member :: () -> int { value: Point = {x: 1, y: 0}; "
                "calls: int = 0; p := &value; "
                "PickPoint(p, &calls).x += SetX(p, &calls); "
                "return value.x * 100 + calls; }\n"
                "Owned :: () -> int { value: Point = {x: 1, y: 0}; "
                "calls: int = 0; p := &value; value.x += SetX(p, &calls); "
                "return value.x * 100 + calls; }\n",
      .calls = { { .function = "Through", .arguments = "[]", .expected = "[311]" },
                 { .function = "Local", .arguments = "[]", .expected = "[310]" },
                 { .function = "Member", .arguments = "[]", .expected = "[311]" },
                 { .function = "Owned", .arguments = "[]", .expected = "[310]" } } },
    /* A narrower integer widens when it is stored through a pointer or into
     * an addressed local, as ordinary assignment widens it. */
    { .name = "managed pointer widening",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Widened :: () -> i64 { x: i64 = 0; y: int = 1; p := &x; "
                "x = y; p^ = y + 1; return x; }\n"
                "Floated :: () -> f64 { x: f64 = 0; f: number = 1.5; p := "
                "&x; "
                "p^ = f; return x; }\n",
      .calls = { { .function = "Widened", .arguments = "[]", .expected = "[2]" },
                 { .function = "Floated", .arguments = "[]", .expected = "[1.5]" } } },
    /* A float operand widens in a compound through a pointer, an addressed
     * local and a pointer's field, as it does for an ordinary local. */
    { .name = "managed pointer compound float widening",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "R :: struct { v: f64; }\n"
                "Local :: () -> f64 { x: f64 = 1.0; y: number = 2.0; "
                "x += y; return x; }\n"
                "Through :: () -> f64 { x: f64 = 1.0; y: number = 2.0; "
                "p := &x; p^ += y; return x; }\n"
                "Addressed :: () -> f64 { x: f64 = 1.0; y: number = 2.0; "
                "p := &x; x += y; return p^; }\n"
                "Member :: () -> f64 { r: R = {v: 1.0}; y: number = 2.0; "
                "p := &r; p.v += y; return r.v; }\n",
      .calls = { { .function = "Local", .arguments = "[]", .expected = "[3.0]" },
                 { .function = "Through", .arguments = "[]", .expected = "[3.0]" },
                 { .function = "Addressed", .arguments = "[]", .expected = "[3.0]" },
                 { .function = "Member", .arguments = "[]", .expected = "[3.0]" } } },
    { .name = "managed pointer compound narrowing",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nRun :: () -> number {\n    x: number = 1.0;\n"
                "    d: f64 = 2.0;\n    p := &x;\n    p^ += d;\n    return "
                "x;\n}",
      .message = "compound assignment needs number operands of its type",
      .line = 6,
      .column = 5 },
    /* Mixed integer compounds reject through a pointer, as they do for an
     * ordinary local. */
    { .name = "managed pointer compound mixed integers",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nRun :: () -> i64 {\n    x: i64 = 0;\n"
                "    y: int = 1;\n    p := &x;\n    p^ += y;\n    return x;\n}",
      .message = "Mismatched integer types i64 and int; convert one operand",
      .line = 6,
      .column = 5 },
    { .name = "nullable pointer condition observes its original type",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nNullTest :: () -> int { p: ^int? = nil; "
                "if p == nil { return 1; } return 0; }",
      .calls = { { .function = "NullTest", .arguments = "[]", .expected = "[1]" } } },
    /* A member of a dereferenced pointer is the pointee's field, as in stage
     * zero, even when the native call that returned the pointer names its
     * output the same, directly or through a local. */
    { .name = "managed pointer named result member",
      .group = COLLECTION_SAMPLES,
      .debug_metadata = true,
      .source = "package facts\n"
                "Node :: struct { item: int; }\n"
                "Direct :: () -> int { node: Node = {item: 7}; "
                "nodes: ^Node[] = [&node]; return get(nodes, 0).item; }\n"
                "Inferred :: () -> int { node: Node = {item: 7}; "
                "nodes: ^Node[] = [&node]; p := get(nodes, 0); "
                "return p.item; }\n",
      .calls = { { .function = "Direct", .arguments = "[]", .expected = "[7]" },
                 { .function = "Inferred", .arguments = "[]", .expected = "[7]" } } },
    /* A callable's named result cannot be a pointer in stage one, so its name
     * never meets a pointee's field there. */
    { .name = "managed pointer named result",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; }\n"
                "Id :: (v: ^Point) -> (x: ^Point) { return v; }\n",
      .message = "named result type is not supported yet",
      .line = 3,
      .column = 23 },
    { .name = "imported references",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Forward :: (value: ^int) { lib.Bump(value); }\n"
                "Run :: () -> int { value: int = 4; Forward(&value); "
                "lib.Bump(&value); return value; }",
      .library = "package lib\npub Bump :: (value: ^int) { value^ += 3; }",
      .calls = { { .function = "Run", .arguments = "[]", .expected = "[10]" } } },
    /* A pointer to a pointer passed on dereferenced lends the target it
     * holds, and an address of a parenthesized dereference escapes. */
    { .name = "scoped pointer relays",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Bump :: (v: ^int) { v^ += 1; }\n"
                "Relay :: (p: ^^int) { Bump(p^); }\n"
                "Keep :: (p: ^int) -> ^int { return &(p^); }\n"
                "Relayed :: () -> int { x: int = 1; q := &x; Relay(&q); "
                "return x; }\n"
                "Kept :: () -> int { x: int = 1; k := Keep(&x); k^ = 5; "
                "return x; }\n"
                "Through :: (p: ^^int) -> int { p^^ = 5; return (p^)^ + 1; }\n"
                "Thrown :: () -> int { x: int = 1; q := &x; return Through(&q) "
                "* 10 + x; }\n"
                "Copy :: (p: ^^int) -> int { q := p^; q^ += 1; return q^; }\n"
                "Copied :: () -> int { x: int = 1; q := &x; return Copy(&q) * "
                "10 + x; }\n"
                "Wrapped :: (p: ^int) { Bump((p)); }\n"
                "Unwrapped :: (p: ^^int) { Bump((p^)); }\n"
                "Parenthesized :: () -> int { x: int = 1; Wrapped(&x); q := "
                "&x; "
                "Unwrapped(&q); return x; }\n",
      .calls = { { .function = "Relayed", .arguments = "[]", .expected = "[2]" },
                 { .function = "Kept", .arguments = "[]", .expected = "[5]" },
                 { .function = "Thrown", .arguments = "[]", .expected = "[65]" },
                 { .function = "Copied", .arguments = "[]", .expected = "[22]" },
                 { .function = "Parenthesized", .arguments = "[]", .expected = "[3]" } } },
    /* A scoped `n^` keeps its own source span. */
    { .name = "scoped pointer spans",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Read :: (n: ^int) -> int { return n^; }\n"
                "Reading :: () -> int { x: int = 4; return Read(&x); }\n",
      .calls = { { .function = "Reading", .arguments = "[]", .expected = "[4]" } } },
    { .name = "pointer argument missing",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ^int) { v^ += 1; }\nRun :: () "
                "{\n    n: int = 1;\n    Bump(n);\n}",
      .message = "Pointer parameter requires &value or a pointer",
      .line = 5,
      .column = 10 },
    { .name = "pointer imported argument missing",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\nRun :: () {\n    "
                "n: int = 1;\n    lib.Bump(n);\n}",
      .library = "package lib\npub Bump :: (v: ^int) { v^ += 1; }",
      .message = "Pointer parameter requires &value or a pointer",
      .line = 5,
      .column = 14 },
    { .name = "pointer literal",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ^int) { v^ += 1; }\nRun :: () { "
                "Bump(&3); }",
      .message = "Pointer argument requires a writable location",
      .line = 3,
      .column = 19 },
    { .name = "pointer default",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ^int = 1) { v^ += 1; }",
      .message = "a scoped pointer parameter takes no default",
      .line = 2,
      .column = 10 },
    { .name = "pointer type",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBump :: (v: ^int) { v^ += 1; }\nRun :: () { "
                "n: i64 = 1; Bump(&n); }",
      .message = "pointer argument differs from the parameter's type",
      .line = 3,
      .column = 31 },
    { .name = "unions",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Shape :: union { Circle(radius: int) | Box(width: int, "
                "height: int) | Spot(at: Point)\n"
                "    | Named(name: string, sides: int[]) | Empty }\n"
                "Make :: (kind: int) -> Shape {\n"
                "    if (kind == 0) { return Shape.Circle(3); }\n"
                "    if (kind == 1) { return Shape.Box(2, 5); }\n"
                "    if (kind == 2) { p: Point = {x: 4, y: 6}; return "
                "Shape.Spot(p); }\n"
                "    if (kind == 3) { sides: int[] = [1, 2, 3]; return "
                "Shape.Named(\"tri\", sides); }\n"
                "    return Shape.Empty;\n}\n"
                "Sum :: (p: Point) -> int { return p.x + p.y; }\n"
                "Area :: (shape: Shape) -> int {\n"
                "    switch shape {\n"
                "        case .Circle(radius): return radius * radius * 3;\n"
                "        case .Box(width, height): return width * height;\n"
                "        case .Spot(at): total := Sum(at); return total + "
                "at.x;\n"
                "        case .Named(name, sides):\n"
                "            total: int = 0;\n"
                "            for (side in sides) { total += side; }\n"
                "            if (name == \"tri\") { total += 100; }\n"
                "            return total;\n"
                "        case .Empty: return -1;\n"
                "    }\n    return -2;\n}\n"
                "Score :: (n: int) -> int {\n"
                "    total: int = 0;\n    i: int = 0;\n"
                "    while (i < n) { total += Area(Make(i)); i += 1; }\n"
                "    return total;\n}\n"
                "Kinds :: () -> int {\n"
                "    shapes: Shape[] = [Shape.Circle(1), Shape.Empty, "
                "Shape.Box(1, 1)];\n"
                "    count: int = 0;\n"
                "    for (shape in shapes) {\n"
                "        switch shape {\n            case .Empty: count += "
                "10;\n            case .Box: count += 100;\n"
                "            case: count += 1;\n        }\n"
                "    }\n    return count;\n}\n"
                "Pair :: (a: Shape, b: Shape) -> int {\n"
                "    switch a {\n"
                "        case .Empty:\n"
                "            switch b {\n                case .Empty: return "
                "2;\n                case: return 1;\n            }\n"
                "        case .Circle(radius): return radius;\n"
                "        case .Box(width, height): return width;\n"
                "        case .Spot(at): return at.x;\n"
                "        case .Named(name, sides): return length(sides);\n"
                "    }\n    return -1;\n}\n"
                "Nested :: () -> int {\n"
                "    return Pair(Shape.Empty, Shape.Empty) * 10 + "
                "Pair(Shape.Empty, Shape.Circle(1)) * 100 + Pair(Shape.Box(3, "
                "4), Shape.Empty);\n}",
      .calls = { { .function = "Score", .arguments = "[5]", .expected = "[156]" },
                 { .function = "Kinds", .arguments = "[]", .expected = "[111]" },
                 { .function = "Nested", .arguments = "[]", .expected = "[123]" },
                 { .function = "Make", .arguments = "[1]", .expected = "[{\"tag\":6300533083688230396,\"values\":[2,5]}]" },
                 { .function = "Make", .arguments = "[2]", .expected = "[{\"tag\":10337135207335990755,\"values\":[{\"x\":4,\"y\":6}]}]" },
                 { .function = "Make", .arguments = "[3]", .expected = "[{\"tag\":16893218631787042844,\"values\":[\"tri\",[1,2,3]]}]" },
                 { .function = "Make", .arguments = "[4]", .expected = "[{\"tag\":16895435817925979816,\"values\":[]}]" } } },
    { .name = "exported generic functions",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Identity :: (value: int) -> int { return value + 100; }\n"
                "Main :: (value: int) -> int { return lib.Repeated(value) + "
                "lib.Identity(value); }\n"
                "Wide :: (value: i64) -> i64 { return lib.Identity(value); }\n"
                "First :: () -> int { values: int[] = [7, 2]; return "
                "lib.First(values); }\n"
                "Scaled :: () -> int { return lib.Scale(5, 1 + 2) + "
                "lib.Scale(2, 4); }\n"
                "Enabled :: () -> int { return lib.Maybe(7, true) + "
                "lib.Maybe(7, false); }\n",
      .library = "package lib\n"
                 "pub Identity :: (value: $T) -> T { local: T = value; return "
                 "local; }\n"
                 "pub Repeated :: (value: $T) -> T { return "
                 "Hidden(Identity(value)); }\n"
                 "Hidden :: (value: $T) -> T { return value + value; }\n"
                 "pub First :: (values: $T[]) -> T { return values[0]; }\n"
                 "pub Scale :: (value: $T, $N: int) -> T { return value * N; "
                 "}\n"
                 "pub Maybe :: (value: int, $Enabled: bool) -> int { "
                 "if Enabled { return value; } return 0; }\n",
      .calls = { { .function = "Main", .arguments = "[7]", .expected = "[21]" },
                 { .function = "Wide", .arguments = "[9007199254740993]", .expected = "[9007199254740993]" },
                 { .function = "First", .arguments = "[]", .expected = "[7]" },
                 { .function = "Scaled", .arguments = "[]", .expected = "[23]" },
                 { .function = "Enabled", .arguments = "[]", .expected = "[7]" } } },
    { .name = "returned structs with array fields survive collections",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Pair :: struct { a: int; b: int; }\n"
                "Bundle :: struct { codes: int[]; pairs: Pair[]; tail: Pair; }\n"
                "Make :: (n: int) -> Bundle { p: Pair = {a: n, b: n + 1}; "
                "return {codes: [n, n + 1, n + 2], pairs: [p, p], tail: p}; }\n"
                "Main :: () -> int { total := 0; i := 0; while i < 400000 { "
                "b := Make(i); total += b.codes[2] - b.pairs[1].b + b.tail.a - i; "
                "i += 1; } return total; }\n",
      .calls = { { .function = "Main", .arguments = "[]", .expected = "[400000]", .budget = 200000000 } } },
    { .name = "optional locals start absent and become present",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Main :: () -> int { absent: int? = nil; present: int? = 4; r := 0; if absent { r += 100; } if present { r += present - 1; } return r; }" "\n",
      .calls = { { .function = "Main", .arguments = "[]", .expected = "[3]" } } },
    { .name = "optional results wrap values and return nil",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Find :: (n: int) -> int? { if n > 2 { return n; } return nil; }" "\n"
                "Main :: () -> int { a := Find(1); b := Find(5); r := 0; if a { r += 100; } if b { r += b; } return r; }" "\n",
      .calls = { { .function = "Main", .arguments = "[]", .expected = "[5]" } } },
    { .name = "optional parameters take optionals and nil",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Pick :: (v: int?) -> int { if v { return v; } return 9; }" "\n"
                "Main :: () -> int { some: int? = 3; none: int? = nil; return Pick(some) * 10 + Pick(none) + Pick(nil); }" "\n",
      .calls = { { .function = "Main", .arguments = "[]", .expected = "[48]" } } },
    { .name = "optional coalescing and nil comparisons",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Main :: () -> int { x: int? = nil; y: int? = 7; r := (x ?? 3) * 10 + (y ?? 1); if y != nil { r += 100; } if x == nil { r += 1000; } return r + (x ?? y ?? 8); }" "\n",
      .calls = { { .function = "Main", .arguments = "[]", .expected = "[1144]" } } },
    { .name = "generic collection and optional values preserve ownership",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Nested :: () -> int { rows: int[][] = [[1, 2], [3]]; "
                "head := lib.First(rows); head.append(7); head[0] = 8; "
                "return rows[0][0] * 1000 + rows[0].length() * 100 + "
                "head[0] * 10 + head.length(); }\n"
                "Whole :: () -> int { rows: int[][] = [[1, 2], [3]]; "
                "copied := lib.Identity(rows); copied[1].append(4); "
                "return rows[1].length() * 100 + copied[1].length() * 10 + "
                "copied[1][1]; }\n"
                "Present :: () -> int { values: int[] = [3, 4]; "
                "selected := lib.Identity(values[0..<2]); if selected { "
                "selected[0] = 8; return values[0] * 100 + selected[0] * 10 + "
                "selected.length(); } return -1; }\n"
                "Absent :: () -> int { values: int[] = [3, 4]; "
                "selected := lib.Identity(values[0..<3]); if selected { "
                "return -1; } return values[0]; }\n"
                "Empty :: () -> int { values: int[] = [3, 4]; "
                "selected := lib.Identity(values[2..<2]); if selected { "
                "return selected.length() + 1; } return -1; }\n"
                "Fallback :: () -> int { values: int[] = [3, 4]; "
                "fallback: int[] = [9]; present := lib.OrElse(values[0..<1], "
                "fallback); absent := lib.OrElse(values[0..<3], fallback); "
                "absent[0] = 7; return present[0] * 100 + fallback[0] * 10 + "
                "absent[0]; }\n",
      .library = "package lib\n"
                 "pub First :: (values: $T[]) -> T { return values[0]; }\n"
                 "pub Identity :: (value: $T) -> T { return value; }\n"
                 "pub OrElse :: (value: $T?, fallback: T) -> T { return "
                 "value ?? fallback; }\n",
      .calls = { { .function = "Nested", .arguments = "[]", .expected = "[1283]" },
                 { .function = "Whole", .arguments = "[]", .expected = "[124]" },
                 { .function = "Present", .arguments = "[]", .expected = "[382]" },
                 { .function = "Absent", .arguments = "[]", .expected = "[3]" },
                 { .function = "Empty", .arguments = "[]", .expected = "[1]" },
                 { .function = "Fallback", .arguments = "[]", .expected = "[397]" } } },
    { .name = "generic specialization identities are reused across documents",
      .group = IMPORT_SAMPLES,
      .specializations = 2,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Flat :: () -> int { values: int[] = [3, 5]; return "
                "lib.First(values) + Again(values) + lib.First(values); }\n"
                "Nested :: () -> int { rows: int[][] = [[4], [6]]; "
                "head := lib.First(rows); again := NestedAgain(rows); "
                "return head[0] * 10 + again[0]; }\n",
      .more = "package facts\nimport tool \"project:lib\"\n"
              "Again :: (values: int[]) -> int { return tool.First(values); }\n"
              "NestedAgain :: (rows: int[][]) -> int[] { return "
              "tool.First(rows); }\n",
      .library = "package lib\npub First :: (values: $T[]) -> T { return "
                 "values[0]; }\n",
      .calls = { { .function = "Flat", .arguments = "[]", .expected = "[9]" },
                 { .function = "Nested", .arguments = "[]", .expected = "[44]" } } },
    { .name = "forwarded callbacks preserve sequence values",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Count :: (input: int[]) -> int { return input.length(); }\n"
                "Change :: (input: int[]) -> int[] { input.append(9); "
                "input[0] = 7; return input; }\n"
                "ReadNamed :: (input: int[]) -> int { return "
                "lib.ReadForward(input, Count); }\n"
                "ReadInline :: (input: int[]) -> int { return "
                "lib.ReadForward(input, (row: int[]) => row.length()); }\n"
                "CopiedNamed :: () -> int { original: int[] = [1, 2]; "
                "copied := lib.TransformForward(original, Change); return "
                "original[0] * 1000 + original.length() * 100 + copied[0] * "
                "10 + copied.length(); }\n"
                "CopiedInline :: () -> int { original: int[] = [1, 2]; "
                "copied := lib.TransformForward(original, (row: int[]) { "
                "row.append(9); row[0] = 8; return row; }); return "
                "original[0] * 1000 + original.length() * 100 + copied[0] * "
                "10 + copied.length(); }\n",
      .library = "package lib\n"
                 "pub ReadForward :: (input: $T, operation: fn(data: T) -> "
                 "int) -> int { return Read(input, operation); }\n"
                 "Read :: (input: $T, operation: fn(data: T) -> int) -> int { "
                 "return operation(data: input); }\n"
                 "pub TransformForward :: (input: $T, operation: fn(data: T) "
                 "-> T) -> T { return Transform(input, operation); }\n"
                 "Transform :: (input: $T, operation: fn(data: T) -> T) -> T { "
                 "return operation(data: input); }\n",
      .calls = { { .function = "ReadNamed", .arguments = "[[1,2,3]]", .expected = "[3]" },
                 { .function = "ReadNamed", .arguments = "[[]]", .expected = "[0]" },
                 { .function = "ReadInline", .arguments = "[[1,2]]", .expected = "[2]" },
                 { .function = "ReadInline", .arguments = "[[]]", .expected = "[0]" },
                 { .function = "CopiedNamed", .arguments = "[]", .expected = "[1273]" },
                 { .function = "CopiedInline", .arguments = "[]", .expected = "[1283]" } } },
    { .name = "forwarded callback labels survive erased and reordered arguments",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Join :: (left: int, right: int) -> int { return left * 100 + "
                "right; }\n"
                "Mark :: (order: ^int, digit: int, value: int) -> int { "
                "order^ = order^ * 10 + digit; return value; }\n"
                "Positional :: () -> int { return lib.Forward(2, 3, Join, "
                "4); }\n"
                "Named :: () -> int { order := 0; result := "
                "lib.Forward(tail: Mark(&order, 1, 4), operation: Join, "
                "input: Mark(&order, 2, 3), Count: 2); return order * 1000 + "
                "result; }\n"
                "Inline :: () -> int { return lib.Forward(2, 3, "
                "(a: int, b: int) => a * 10 + b, 4); }\n",
      .library = "package lib\n"
                 "pub Forward :: ($Count: int, input: $T, operation: "
                 "fn(first: T, second: T) -> T, tail: T) -> T { return "
                 "Apply(operation, Count, input, tail); }\n"
                 "Apply :: (operation: fn(first: int, second: int) -> int, "
                 "$Offset: int, input: int, tail: int) -> int { return "
                 "operation(second: tail + Offset, first: input); }\n",
      .calls = { { .function = "Positional", .arguments = "[]", .expected = "[306]" },
                 { .function = "Named", .arguments = "[]", .expected = "[12306]" },
                 { .function = "Inline", .arguments = "[]", .expected = "[36]" } } },
    { .name = "inline pointer parameter shadows enclosing pointer parameter",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Bump :: (value: ^int) -> int { value^ += 1; return value^; }\n"
                "Held :: (value: ^int) -> int { pointer := value; pointer^ += "
                "2; return pointer^; }\n"
                "Observe :: (value: ^int) -> int { other := 7; observed := "
                "lib.Apply(&other, (value: ^int) -> int { value^ += 2; return "
                "value^; }); return value^ * 100 + observed * 10 + other; }\n"
                "Main :: () -> int { original := 3; observed := "
                "Observe(&original); return observed * 10 + original; }\n"
                "Scoped :: () -> int { original := 3; observed := "
                "lib.Apply(&original, Bump); return observed * 10 + original; }\n"
                "Managed :: () -> int { original := 3; observed := "
                "lib.Apply(&original, Held); return observed * 10 + original; }\n",
      .library = "package lib\npub Apply :: (input: ^int, function: "
                 "fn(value: ^int) -> int) -> int { return function(input); }\n",
      .calls = { { .function = "Main", .arguments = "[]", .expected = "[3993]" },
                 { .function = "Scoped", .arguments = "[]", .expected = "[44]" },
                 { .function = "Managed", .arguments = "[]", .expected = "[55]" } } },
    { .name = "non-escaping callable algorithms",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Less :: (a: int, b: int) -> bool { return a < b; }\n"
                "Positive :: (value: int) -> bool { return value > 0; }\n"
                "Named :: () -> int { values: int[] = [3, 1, 2]; sorted := "
                "lib.Sorted(values, Less); "
                "if lib.Any(sorted, Positive) { return values[0] * 1000 + "
                "sorted[0] * 100 + sorted[1] * 10 + sorted[2]; } return 0; }\n"
                "Inline :: () -> int { values: int[] = [3, 1, 2]; sorted := "
                "lib.Sorted(values, (a: int, b: int) => a > b); "
                "if lib.Any(sorted, (value: int) => value == 2) { return "
                "sorted[0] * 100 + sorted[1] * 10 + sorted[2]; } return 0; }\n"
                "Gain :: (input: number) -> number { return input * 2.0; }\n"
                "ShaderHelper :: () -> number { return lib.Remap(0.25, Gain); "
                "}\n"
                "LoopShadow :: () -> int { item := 9; return lib.Remap(3, "
                "(value: int) { total := 0; for item in 0..<value { total += "
                "item; } return total; }) + item; }\n",
      .library = "package lib\n"
                 "pub Sorted :: (values: $T[], less: fn(a: T, b: T) -> bool) "
                 "-> T[] {\n"
                 "  result := values; i := 1; while i < length(result) { j := "
                 "i; "
                 "while j > 0 && less(result[j], result[j - 1]) { item: T = "
                 "result[j]; result[j] = result[j - 1]; result[j - 1] = item; "
                 "j -= 1; } i += 1; } return result; }\n"
                 "pub Any :: (values: $T[], predicate: fn(value: T) -> bool) "
                 "-> bool { return AnyInternal(values, predicate); }\n"
                 "AnyInternal :: (values: $T[], predicate: fn(value: T) -> "
                 "bool) -> bool { for value in values { if predicate(value) { "
                 "return true; } } return false; }\n"
                 "pub Remap :: (value: $T, transform: (fn(value: T) -> T)) -> "
                 "T { return transform(value: value); }\n",
      .calls = { { .function = "Named", .arguments = "[]", .expected = "[3123]" },
                 { .function = "Inline", .arguments = "[]", .expected = "[321]" },
                 { .function = "ShaderHelper", .arguments = "[]", .expected = "[0.5]" },
                 { .function = "LoopShadow", .arguments = "[]", .expected = "[12]" } } },
    /* No declaration needs specialization. Ordinary body typing must reject
     * the lambda at its source location instead of treating its parameter as
     * a parameter of Run. */
    { .name = "inline function cannot satisfy scalar parameter without templates",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Take :: (value: int) -> int { return value; }\n"
                "Run :: () -> int { return Take((value: int) => value); }\n",
      .message = "Inline functions may only be passed to a callable parameter",
      .line = 3,
      .column = 32 },
    { .name = "callable capture rejection",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Main :: (limit: int) -> bool { return lib.Apply(3, (value: "
                "int) => value > limit); }\n",
      .library = "package lib\npub Apply :: (value: int, predicate: fn(value: "
                 "int) -> bool) -> bool { return predicate(value); }\n",
      .message = "Inline functions cannot capture enclosing locals",
      .line = 3,
      .column = 76 },
    { .name = "callable pattern bindings shadow enclosing locals",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Choice :: union { Some(amount: int) | None }\n"
                "IfPattern :: () -> int { payload := 90; return lib.Remap(3, "
                "(value: int) { choice := Choice.Some(value); if choice is "
                ".Some(payload) && payload > 0 { return payload + 1; } return "
                "0; }) + payload; }\n"
                "WhilePattern :: () -> int { payload := 90; return "
                "lib.Remap(3, "
                "(value: int) { choice := Choice.Some(value); total := 0; "
                "while "
                "choice is .Some(payload) && payload > 0 { total += payload; "
                "choice = Choice.Some(payload - 1); } return total; }) + "
                "payload; }\n",
      .library = "package lib\npub Remap :: (value: int, transform: fn(value: "
                 "int) -> int) -> int { return transform(value); }\n",
      .calls = { { .function = "IfPattern", .arguments = "[]", .expected = "[94]" },
                 { .function = "WhilePattern", .arguments = "[]", .expected = "[96]" } } },
    { .name = "callable single result names do not change signature",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Named :: () -> (answer: int) { return 7; }\n"
                "Unnamed :: () -> int { return 11; }\n"
                "Main :: () -> int { return lib.UnnamedContract(Named) * 100 + "
                "lib.NamedContract(Unnamed); }\n"
                "Inline :: () -> int { answer := 90; return "
                "lib.UnnamedContract(() -> (answer: int) { answer = 13; "
                "return; }) + "
                "lib.NamedContract(() => 17) + answer; }\n",
      .library = "package lib\n"
                 "pub UnnamedContract :: (function: fn() -> int) -> int { "
                 "return function(); }\n"
                 "pub NamedContract :: (function: fn() -> (expected: int)) -> "
                 "int { return function(); }\n",
      .calls = { { .function = "Main", .arguments = "[]", .expected = "[711]" },
                 { .function = "Inline", .arguments = "[]", .expected = "[120]" } } },
    { .name = "compile-time arguments retain lexical constant scope",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Factor :: 3;\n"
                "Scale :: (value: int, $N: int) -> int { return value * N; }\n"
                "Maybe :: (value: int, $Flag: bool) -> int { if Flag { return "
                "value; } return 0; }\n"
                "Forward :: ($Flag: bool) -> int { return Maybe(1, Flag); }\n"
                "Main :: () -> int { return Scale(2, Factor) + Maybe(4, "
                "true) + Scale(1, lib.Factor) + Forward(true); }\n",
      .library = "package lib\npub Factor :: 5;\n",
      .calls = { { .function = "Main", .arguments = "[]", .expected = "[16]" } } },
    { .name = "compile-time argument cannot use shadowing parameter",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nFactor :: 3;\n"
                "Scale :: (value: int, $N: int) -> int { return value * N; }\n"
                "Main :: (Factor: int) -> int { return Scale(2, Factor); }\n",
      .message = "constant value must be an integer expression",
      .oracle_message = "Compile-time evaluation: only constant values and "
                        "types are available",
      .same_rejection = true },
    { .name = "compile-time argument cannot use shadowing local",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nFactor :: 3;\n"
                "Scale :: (value: int, $N: int) -> int { return value * N; }\n"
                "Main :: () -> int { Factor := 5; return Scale(2, Factor); }\n",
      .message = "constant value must be an integer expression",
      .oracle_message = "Compile-time evaluation: only constant values and "
                        "types are available",
      .same_rejection = true },
    { .name = "compile-time argument cannot use shadowed import alias",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Options :: struct { Factor: int; }\n"
                "Scale :: (value: int, $N: int) -> int { return value * N; }\n"
                "Main :: (lib: Options) -> int { return Scale(2, lib.Factor); "
                "}\n",
      .library = "package lib\npub Factor :: 3;\n",
      .message = "constant value must be an integer expression",
      .oracle_message = "Compile-time evaluation: only constant values and "
                        "types are available",
      .same_rejection = true },
    { .name = "boolean compile-time argument cannot use shadowing parameter",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Maybe :: (value: int, $Flag: bool) -> int { if Flag { return "
                "value; } return 0; }\n"
                "Main :: (Enabled: bool) -> int { return Maybe(2, Enabled); "
                "}\n",
      .message = "A boolean compile-time argument requires a constant boolean "
                 "value",
      .oracle_message = "Compile-time evaluation: only constant values and "
                        "types are available",
      .same_rejection = true },
    { .name = "boolean compile-time argument cannot use shadowing local",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "Maybe :: (value: int, $Flag: bool) -> int { if Flag { return "
                "value; } return 0; }\n"
                "Forward :: ($Enabled: bool) -> int { if true { Enabled := "
                "false; return Maybe(2, Enabled); } return 0; }\n"
                "Main :: () -> int { return Forward(true); }\n",
      .message = "A boolean compile-time argument requires a constant boolean "
                 "value",
      .oracle_message = "Compile-time evaluation: only constant values and "
                        "types are available",
      .same_rejection = true },
    { .name = "callable pattern binding cannot escape consequence",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Choice :: union { Some(amount: int) | None }\n"
                "Main :: (payload: int) -> int { return lib.Remap(3, (value: "
                "int) { choice := Choice.Some(value); if choice is "
                ".Some(payload) "
                "{ return value; } return payload; }); }\n",
      .library = "package lib\npub Remap :: (value: int, transform: fn(value: "
                 "int) -> int) -> int { return transform(value); }\n",
      .message = "Inline functions cannot capture enclosing locals",
      .line = 4,
      .column = 151 },
    { .name = "callable capture respects lexical scopes",
      .group = IMPORT_SAMPLES,
      .source = "package facts\noffset :: 1;\nimport lib \"project:lib\"\n"
                "Main :: (offset: int) -> int { return lib.Remap(3, (value: "
                "int) { if false { offset := 9; } return value + offset; }); "
                "}\n",
      .library = "package lib\npub Remap :: (value: int, transform: fn(value: "
                 "int) -> int) -> int { return transform(value); }\n",
      .message = "Inline functions cannot capture enclosing locals",
      .line = 4,
      .column = 108 },
    { .name = "callable signature rejection",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Wrong :: (value: int) -> int { return value; }\n"
                "Main :: () -> bool { return lib.Apply(3, Wrong); }\n",
      .library = "package lib\npub Apply :: (value: int, predicate: fn(value: "
                 "int) -> bool) -> bool { return predicate(value); }\n",
      .message = "Function argument does not match the callable parameter "
                 "signature",
      .line = 4,
      .column = 42 },
    { .name = "callable inferred result rejects contextual widening",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Main :: () -> number { return lib.Apply(2.0, (value: number) "
                "=> 1); }\n",
      .library = "package lib\npub Apply :: (value: number, transform: "
                 "fn(value: number) -> number) -> number { return "
                 "transform(value); }\n",
      .message = "Function argument does not match the callable parameter "
                 "signature",
      .line = 3,
      .column = 46 },
    { .name = "callable consumer cannot suspend",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "One :: () -> int { return 1; }\nMain :: () { pending := "
                "lib.Apply(One); }\n",
      .library = "package lib\npub Apply :: (function: fn() -> int) -> int { "
                 "yield; return function(); }\n",
      .message = "Non-escaping callable parameters require synchronous "
                 "function bodies",
      .line = 2,
      .column = 1 },
    { .name = "callable invocation cannot inherit target defaults",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Pair :: (a: int = 1, b: int = 2) -> int { return a + b; "
                "}\nMain :: () -> int { return lib.Apply(Pair); }\n",
      .library = "package lib\npub Apply :: (function: fn(a: int, b: int) -> "
                 "int) -> int { return function(b: 5); }\n",
      .message = "call has the wrong number of arguments",
      .line = 2,
      .column = 68 },
    { .name = "imports",
      .group = IMPORT_SAMPLES,
      .source = "package facts\n"
                "import lib \"project:lib\"\n"
                "import strings \"core:strings\"\n"
                "Total :: (n: int) -> int { return lib.Twice(n) + "
                "lib.Sum(lib.Make(n, 3)); }\n"
                "Local :: () -> int { p: lib.Point = {x: 2, y: 5}; "
                "return lib.Sum(p) + p.x; }\n"
                "Shapes :: () -> int {\n"
                "    return lib.Size(lib.Shape.Dot(lib.Make(1, 2))) + "
                "lib.Size(lib.Shape.Line(7)) * 10 +\n"
                "        lib.Size(lib.Shape.Nothing);\n}\n"
                "Match :: (n: int) -> int {\n"
                "    shape := lib.Shape.Line(n);\n"
                "    switch shape { case .Line(size): return size; case: "
                "return 0; }\n"
                "    return -1;\n}\n"
                "Measure :: (s: string) -> int {\n"
                "    return strings.ByteLength(s) * 10 + strings.Compare(s, "
                "\"m\") + lib.Width(s) * 100;\n}\n"
                "Echo :: (p: lib.Point) -> lib.Point { return p; }\n"
                "Points :: (n: int) -> lib.Point[] { points: "
                "lib.Point[] = [lib.Make(n, n)]; return points; }",
      .library = "package lib\n"
                 "import text \"core:strings\"\n"
                 "pub Point :: struct { x: int; y: int; }\n"
                 "pub Shape :: union { Dot(at: Point) | Line(size: int) | "
                 "Nothing "
                 "}\n"
                 "pub Make :: (x: int, y: int) -> Point { p: Point = {x: x, "
                 "y: y}; return p; }\n"
                 "pub Sum :: (p: Point) -> int { return p.x + p.y; }\n"
                 "pub Twice :: (n: int) -> int { return Helper(n) * 2; }\n"
                 "Helper :: (n: int) -> int { return n + 1; }\n"
                 "pub Width :: (s: string) -> int { return "
                 "text.ByteLength(s); }\n"
                 "pub Size :: (shape: Shape) -> int {\n"
                 "    switch shape {\n"
                 "        case .Dot(at): return Sum(at);\n"
                 "        case .Line(size): return size;\n"
                 "        case .Nothing: return 0;\n"
                 "    }\n    return -1;\n}",
      .core = true,
      .calls = { { .function = "Total", .arguments = "[4]", .expected = "[17]" },
                 { .function = "Local", .arguments = "[]", .expected = "[9]" },
                 { .function = "Shapes", .arguments = "[]", .expected = "[73]" },
                 { .function = "Match", .arguments = "[6]", .expected = "[6]" },
                 { .function = "Measure", .arguments = "[\"zebra\"]", .expected = "[551]" },
                 { .function = "Echo", .arguments = "[{\"x\":1,\"y\":2}]", .expected = "[{\"x\":1,\"y\":2}]" },
                 { .function = "Points", .arguments = "[3]", .expected = "[[{\"x\":3,\"y\":3}]]" } } },
    /* An assert fails the call when its condition is false, with its
     * message or its condition's source; it names its arguments as the core
     * catalog does. */
    { .name = "assertions",
      .group = IMPORT_SAMPLES,
      .game = true,
      .source = "package facts\n"
                "Double :: (n: int) -> int {\n"
                "    assert(n > 0, \"n is positive\");\n"
                "    assert(n < 100);\n"
                "    assert(condition: n != 7, message: \"\");\n"
                "    assert((n != 8), (\"not eight\"));\n"
                "    assert(n != 9, \"a\\u0000b\");\n"
                "    return n * 2;\n}",
      .calls = { { .function = "Double", .arguments = "[3]", .expected = "[6]" },
                 { .function = "Double", .arguments = "[-1]", .expected = NULL },
                 { .function = "Double", .arguments = "[200]", .expected = NULL },
                 { .function = "Double", .arguments = "[7]", .expected = NULL },
                 { .function = "Double", .arguments = "[8]", .expected = NULL },
                 { .function = "Double",
                   .arguments = "[9]",
                   .expected = NULL,
                   .diagnostic = "Assertion failed: a\\0b" } } },
    /* Package and imported constants. `lib.Scaled(2)` takes a default that
     * stage zero reads from lib's catalog, at the call; `Triple(2)` one that
     * the package's other file declares, whose occurrence is that file's. */
    { .name = "constants",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "import lib \"project:lib\"\n"
                "Base: int : 40;\n"
                "Down: int : -2;\n"
                "Plain: int : 7;\n"
                "Sum :: (n: int) -> int { return n + Base + Down + "
                "Plain + lib.Step; }\n"
                "Hidden :: () -> int { Base: int = 1; return Base; }\n"
                "Limits :: () -> int {\n"
                "    high := lib.High;\n"
                "    if (lib.Low < 0 && high > 0) { return high + lib.Low; }\n"
                "    return 0;\n}\n"
                "Scale :: () -> int { return lib.Scaled(2); }\n"
                "Tripled :: () -> int { return Triple(2); }",
      .more = "package facts\nTriple :: (n: int, by: int = 3) -> int { return "
              "n * by; }\n",
      .library = "package lib\n"
                 "pub Step: int : 100;\n"
                 "pub Low: int : -2147483648;\n"
                 "pub High: int : 2147483647;\n"
                 "pub Scaled :: (n: int, by: int = 1) -> int { return n * "
                 "Step * by; }",
      .calls = { { .function = "Sum", .arguments = "[3]", .expected = "[148]" },
                 { .function = "Hidden", .arguments = "[]", .expected = "[1]" },
                 { .function = "Limits", .arguments = "[]", .expected = "[-1]" },
                 { .function = "Scale", .arguments = "[]", .expected = "[200]" },
                 { .function = "Tripled", .arguments = "[]", .expected = "[6]" } } },
    { .name = "patterns",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Value :: union { Int(n: int) | Text(s: string) | Pair(a: int, "
                "b: int) | Spot(at: Point) | Nothing }\n"
                "Make :: (k: int) -> Value {\n"
                "    if (k == 0) { return Value.Int(5); }\n"
                "    if (k == 1) { return Value.Text(\"hey\"); }\n"
                "    if (k == 2) { return Value.Pair(3, 4); }\n"
                "    if (k == 3) { return Value.Pair(4, 3); }\n"
                "    return Value.Nothing;\n}\n"
                "Score :: (k: int) -> int {\n"
                "    v := Make(k);\n"
                "    if (v is .Int(n)) { return n; }\n"
                "    if (v is .Pair(a, b) && a < b) { return a * 10 + b; }\n"
                "    if (v is not .Nothing && !(v is .Text)) { return -1; }\n"
                "    if ((v is .Text(s)) && s == \"hey\") { return 100; } else "
                "{ return 0; }\n"
                "    return -2;\n}\n"
                "Kind :: (k: int) -> int {\n"
                "    v := Make(k);\n"
                "    if (v is .Int(n)) { return n; } else if (v is .Pair(a, "
                "b)) { return a + b; } else { return -5; }\n"
                "    return 0;\n}\n"
                "Count :: () -> int {\n"
                "    v := Make(2);\n"
                "    total: int = 0;\n"
                "    while (v is .Pair(a, b) && a < 10) { total += a; v = "
                "Value.Pair(a + b, b); }\n"
                "    return total;\n}\n"
                "Walk :: () -> int {\n"
                "    start: Point = {x: 0, y: 1};\n"
                "    v := Value.Spot(start);\n"
                "    total: int = 0;\n"
                "    while (v is .Spot(at) and at.x < 3) {\n"
                "        total += at.x * 10 + at.y;\n"
                "        next: Point = {x: at.x + 1, y: at.y};\n"
                "        v = Value.Spot(next);\n"
                "    }\n"
                "    return total;\n}\n"
                "Empty :: (k: int) -> bool { return Make(k) is "
                ".Nothing; }",
      .calls = { { .function = "Score", .arguments = "[0]", .expected = "[5]" },
                 { .function = "Score", .arguments = "[1]", .expected = "[100]" },
                 { .function = "Score", .arguments = "[2]", .expected = "[34]" },
                 { .function = "Score", .arguments = "[3]", .expected = "[-1]" },
                 { .function = "Score", .arguments = "[4]", .expected = "[0]" },
                 { .function = "Kind", .arguments = "[2]", .expected = "[7]" },
                 { .function = "Kind", .arguments = "[4]", .expected = "[-5]" },
                 { .function = "Count", .arguments = "[]", .expected = "[10]" },
                 { .function = "Walk", .arguments = "[]", .expected = "[33]" },
                 { .function = "Empty", .arguments = "[4]", .expected = "[true]" } } },
    { .name = "shadowing",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Shadow :: (n: int) -> int {\n"
                "    m := n + 1;\n"
                "    total: int = m;\n"
                "    if (m > 0) { total: int = 100; total += 1; }\n"
                "    if (m > 1) { m: int = 5; total += m; }\n"
                "    return total + m;\n}\n"
                "U :: union { A(x: int) | B }\n"
                "Arm :: () -> int { u := U.A(7); switch u { case .A(n): "
                "n: int = 2; return n; case .B: return 0; } return -1; }",
      .calls = { { .function = "Shadow", .arguments = "[3]", .expected = "[13]" },
                 { .function = "Arm", .arguments = "[]", .expected = "[2]" } } },
    { .name = "integers",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Mask: u64 : 0xFF00000000000000;\n"
                "Small: u8 : 200;\n"
                "Hash :: (bytes: u8[]) -> u64 {\n"
                "    hash: u64 = 1469598103934665603;\n"
                "    for (item in bytes) { hash = (hash ^ u64(item)) * "
                "1099511628211; }\n"
                "    return hash;\n}\n"
                "Bytes :: (n: int) -> u8[] {\n"
                "    out: u8[] = [1, 2, 255];\n"
                "    out = append(out, u8(wrap: n));\n"
                "    out = append(out, Small);\n"
                "    return out;\n}\n"
                "High :: (n: int) -> int { mask := Mask; return int(wrap: "
                "(mask >> 56) + u64(n)); }\n"
                "Mixed :: (a: int, b: i64) -> i64 { return i64(a) + b; "
                "}\n"
                "Store :: (a: int) -> i64 { x: i64 = a; x += 10; x "
                "*= 3; return x; }\n"
                "Narrow :: (v: i64) -> int { return int(v); }\n"
                "Clamp :: (v: int) -> u8 { return u8(v); }",
      .calls = { { .function = "Hash", .arguments = "[[1,2,3]]", .expected = "[11570874782335668893]" },
                 { .function = "Bytes", .arguments = "[300]", .expected = "[[1,2,255,44,200]]" },
                 { .function = "High", .arguments = "[1]", .expected = "[256]" },
                 { .function = "Mixed", .arguments = "[2,5000000000]", .expected = "[5000000002]" },
                 { .function = "Store", .arguments = "[4]", .expected = "[42]" },
                 { .function = "Narrow", .arguments = "[7]", .expected = "[7]" },
                 { .function = "Narrow", .arguments = "[5000000000]", .expected = "[2147483647]" },
                 { .function = "Clamp", .arguments = "[300]", .expected = "[255]" },
                 { .function = "Clamp", .arguments = "[-5]", .expected = "[0]" } } },
    { .name = "wide values",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Wide: i64 : -5000000000;\n"
                "Tag :: union { Bits(value: u64) | None }\n"
                "Shifts :: (n: int) -> u64 { v := u64(n); return "
                "(v << 4) | (v >> 60) & 0xF; }\n"
                "Equal :: (a: int, b: int) -> bool { return u64(a) "
                "== u64(b) && u64(a) != 0; }\n"
                "Twice :: () -> i64 { wide := Wide; return wide * 2; }\n"
                "Negative :: (n: i64) -> bool { return n < 0; }\n"
                "Mk :: (v: u64) -> Tag { return Tag.Bits(v); }\n"
                "Pick :: (n: int) -> u64 {\n"
                "    t := Mk(u64(n) * 1000000007);\n"
                "    switch t { case .Bits(value): return value; case .None: "
                "return 0; }\n"
                "    return 0;\n}",
      .calls = { { .function = "Shifts", .arguments = "[1]", .expected = "[16]" },
                 { .function = "Equal", .arguments = "[3,3]", .expected = "[true]" },
                 { .function = "Twice", .arguments = "[]", .expected = "[-10000000000]" },
                 { .function = "Negative", .arguments = "[-1]", .expected = "[true]" },
                 { .function = "Pick", .arguments = "[5]", .expected = "[5000000035]" } } },
    { .name = "wide records",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Entry :: struct { flag: bool; id: u64; small: u8; "
                "count: int; delta: i64; name: string; }\n"
                "Make :: (n: int) -> Entry {\n"
                "    e: Entry = {flag: n > 0, id: u64(n) * 1000000007, "
                "small: u8(wrap: n), count: n, delta: i64(n) - "
                "5000000000, name: \"e\"};\n"
                "    return e;\n}\n"
                "Mix :: (e: Entry) -> u64 { return (e.id ^ "
                "u64(e.small)) + u64(e.count); }\n"
                "Bump :: (e: Entry) -> Entry {\n"
                "    out := e;\n    out.id += 1;\n    out.small = 7;\n"
                "    out.delta *= 2;\n    return out;\n}\n"
                "Total :: (n: int) -> i64 {\n"
                "    es: Entry[] = [];\n    i: int = 0;\n"
                "    while (i < n) { es = append(es, Make(i)); i += 1; }\n"
                "    total: i64 = 0;\n"
                "    for (e in es) { total += e.delta + i64(e.small); }\n"
                "    return total;\n}",
      .calls = { { .function = "Make",
                   .arguments = "[3]",
                   .expected = "[{\"flag\":true,\"id\":3000000021,\"small\":3,"
                               "\"count\":3,"
                               "\"delta\":-4999999997,\"name\":\"e\"}]" },
                 { .function = "Mix",
                   .arguments = "[{\"flag\":true,\"id\":3000000021,\"small\":3,"
                                "\"count\":3,"
                                "\"delta\":-4999999997,\"name\":\"e\"}]",
                   .expected = "[3000000025]" },
                 { .function = "Bump",
                   .arguments = "[{\"flag\":false,\"id\":18446744073709551614,"
                                "\"small\":200,"
                                "\"count\":-1,\"delta\":-4999999997,\"name\":"
                                "\"x\"}]",
                   .expected = "[{\"flag\":false,\"id\":18446744073709551615,"
                               "\"small\":7,"
                               "\"count\":-1,\"delta\":-9999999994,\"name\":"
                               "\"x\"}]" },
                 { .function = "Total", .arguments = "[3]", .expected = "[-14999999994]" } } },
    { .name = "inferred arrays",
      .group = TYPE_SAMPLES,
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Sizes :: (n: int) -> int {\n"
                "    flags := resize([false], n);\n"
                "    zero: int = 0;\n"
                "    counts := resize([zero], n);\n"
                "    counts[0] = 7;\n"
                "    total: int = 0;\n"
                "    for (count in counts) { total += count; }\n"
                "    if (flags[1]) { return -1; }\n"
                "    return total * 10 + length(flags);\n}\n"
                "Words :: () -> int { first := \"x\"; words := [first, "
                "\"yy\", \"zzz\"]; return length(words); }\n"
                "Points :: () -> int { p: Point = {x: 1, y: 2}; points "
                ":= [p, p]; return points[1].y + length(points); }\n"
                "Wide :: () -> u64 { one: u64 = 1; bits := [one, 2, "
                "0xFF]; "
                "return bits[1] + bits[2]; }",
      .calls = { { .function = "Sizes", .arguments = "[3]", .expected = "[73]" },
                 { .function = "Words", .arguments = "[]", .expected = "[3]" },
                 { .function = "Points", .arguments = "[]", .expected = "[4]" },
                 { .function = "Wide", .arguments = "[]", .expected = "[257]" } } },
    { .name = "boxed elements",
      .group = TYPE_SAMPLES,
      .boxed = true,
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; }\n"
                "Point :: struct { x: int; y: int; }\n"
                "Shape :: union { Circle(radius: int) | Empty }\n"
                "BoxedWord :: (words: string[], i: int) -> string { return "
                "words[i]; }\n"
                "BoxedWords :: (words: string[]) -> int {\n"
                "    total: int = 0;\n"
                "    for (word in words) { if (word == \"b\") { total += 1; } "
                "}\n"
                "    return total;\n}\n"
                "BoxedNotes :: (notes: Note[], i: int) -> int {\n"
                "    total: int = 0;\n"
                "    for (note in notes) { total += note.count; }\n"
                "    return total + notes[i].count * 100;\n}\n"
                "BoxedShapes :: (i: int) -> int {\n"
                "    shapes: Shape[] = [Shape.Circle(2), Shape.Empty, "
                "Shape.Circle(5)];\n"
                "    total: int = 0;\n"
                "    for (shape in shapes) {\n"
                "        switch shape { case .Circle(radius): total += radius; "
                "case .Empty: total += 10; }\n"
                "    }\n"
                "    picked := shapes[i];\n"
                "    switch picked { case .Circle(radius): return total + "
                "radius * 100; case .Empty: return total; }\n"
                "    return -1;\n}\n"
                "PackedPoints :: (points: Point[], i: int) -> int {\n"
                "    total: int = 0;\n"
                "    for (point in points) { total += point.y; }\n"
                "    return total + points[i].x * 100;\n}",
      .calls = { { .function = "BoxedWord",
                   .arguments = "[[\"a\",\"b\",\"c\"],1]",
                   .expected = "[\"b\"]" },
                 { .function = "BoxedWord", .arguments = "[[\"a\"],1]", .expected = NULL },
                 { .function = "BoxedWord", .arguments = "[[\"a\"],-1]", .expected = NULL },
                 { .function = "BoxedWords", .arguments = "[[\"a\",\"b\",\"b\"]]", .expected = "[2]" },
                 { .function = "BoxedNotes",
                   .arguments = "[[{\"text\":\"x\",\"count\":1},{\"text\":"
                                "\"y\",\"count\":2}],1]",
                   .expected = "[203]" },
                 { .function = "BoxedNotes",
                   .arguments = "[[{\"text\":\"x\",\"count\":1}],1]",
                   .expected = NULL },
                 { .function = "BoxedShapes", .arguments = "[2]", .expected = "[517]" },
                 { .function = "BoxedShapes", .arguments = "[1]", .expected = "[17]" },
                 { .function = "BoxedShapes", .arguments = "[3]", .expected = NULL },
                 { .function = "PackedPoints",
                   .arguments = "[[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}],1]",
                   .expected = "[306]" } } },
    { .name = "formatting",
      .source = "package facts\n"
                "Label :: (n: int, name: string) -> string { return "
                "format_text(\"{}:{}\", n, name); }\n"
                "Plain :: () -> string { return format_text(\"plain\"); "
                "}\n"
                "Mixed :: (flag: bool) -> string { big: u64 = "
                "0xFFFFFFFFFFFFFFFF; small: i64 = -3; return "
                "format_text(\"{} {} {}\", flag, big, small); }",
      .calls = { { .function = "Label", .arguments = "[3,\"x\"]", .expected = "[\"3:x\"]" },
                 { .function = "Plain", .arguments = "[]", .expected = "[\"plain\"]" },
                 { .function = "Mixed",
                   .arguments = "[true]",
                   .expected = "[\"true 18446744073709551615 -3\"]" } } },
    { .name = "native results",
      .source = "package facts\n"
                "Decode :: (literal: string) -> string {\n"
                "    decoded := compiler_json_string(literal);\n"
                "    if (!decoded.valid) { return \"?\"; }\n"
                "    return format_text(\"{}/{}\", decoded.value, "
                "decoded.byte_length);\n}\n"
                "Valid :: (literal: string) -> bool { return "
                "compiler_json_string(literal).Valid; }\n"
                "Length :: (literal: string) -> int { return "
                "compiler_json_string(literal).ByteLength; }",
      .calls = { { .function = "Decode",
                   .arguments = "[\"\\\"a\\\\u00e9\\\"\"]",
                   .expected = "[\"a\303\251/3\"]" },
                 { .function = "Decode", .arguments = "[\"bad\"]", .expected = "[\"?\"]" },
                 { .function = "Valid", .arguments = "[\"\\\"x\\\"\"]", .expected = "[true]" },
                 { .function = "Length", .arguments = "[\"\\\"abc\\\"\"]", .expected = "[3]" } } },
    { .name = "written parameters",
      .source = "package facts\n"
                "Point :: struct { x: int; y: int; }\n"
                "Grow :: (xs: int[], n: int) -> int[] { xs = "
                "append(xs, n); xs[0] = 9; return xs; }\n"
                "Swap :: (p: Point, q: Point) -> int { p = q; return "
                "p.x * 10 + q.y; }\n"
                "Caller :: () -> int {\n"
                "    xs: int[] = [1, 2];\n"
                "    ys := Grow(xs, 5);\n"
                "    a: Point = {x: 1, y: 2};\n"
                "    b: Point = {x: 3, y: 4};\n"
                "    return xs[0] * 1000 + length(ys) * 100 + ys[0] * 10 + "
                "Swap(a, b) - a.x * 34;\n}",
      .calls = { { .function = "Caller", .arguments = "[]", .expected = "[1390]" } } },
    { .name = "field assignment",
      .source = "package facts\n"
                "Note :: struct { text: string; count: int; open: bool; }\n"
                "Stamp :: (note: Note, count: int) -> Note { note.count "
                "= count; note.text = format_text(\"#{}\", count); return "
                "note; }\n"
                "Fields :: (n: int) -> string {\n"
                "    note: Note = {text: \"a\", count: 1, open: false};\n"
                "    copy := note;\n"
                "    note.count += n;\n"
                "    note.count *= 2;\n"
                "    note.open = true;\n"
                "    stamped := Stamp(note, 7);\n"
                "    b: u8 = 3;\n"
                "    copy.count = b;\n"
                "    copy.count += int(b);\n"
                "    return format_text(\"{} {} {} {} {} {}\", note.text, "
                "note.count, note.open, copy.count, stamped.text, "
                "stamped.count);\n}",
      .calls = { { .function = "Fields", .arguments = "[4]", .expected = "[\"a 10 true 6 #7 7\"]" } } },
    { .name = "record push",
      .source = "package facts\nNode :: struct { kind: string; start: int; }\n"
                "Build :: (count: int) -> int {\n"
                "    nodes: Node[] = [];\n"
                "    index: int = 0;\n"
                "    while (index < count) {\n"
                "        node: Node = {kind: \"a\", start: index};\n"
                "        nodes = append(nodes, node);\n"
                "        index += 1;\n"
                "    }\n"
                "    return length(nodes);\n}",
      .updates = true,
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[3]" } } },
    { .name = "extension",
      .source = "package facts\nBuild :: (count: int) -> int {\n"
                "    xs: int[] = [];\n"
                "    index: int = 0;\n"
                "    while (index < count) {\n"
                "        xs = extend(xs, [index, index]);\n"
                "        index += 1;\n"
                "    }\n"
                "    return length(xs);\n}",
      .updates = true,
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[6]" } } },
    { .name = "array updates keep aliases",
      .source = "package facts\nBuild :: (count: int) -> int {\n"
                "    xs: int[] = [count, count];\n"
                "    ys := xs;\n"
                "    xs = append(xs, count);\n"
                "    ys = append(ys, count + 1);\n"
                "    zs := xs;\n"
                "    xs = extend(xs, ys);\n"
                "    return xs[2] * 100 + ys[2] * 10 + length(zs);\n}",
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[343]" } } },
    { .name = "map updates",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nBuild :: (count: int) -> int {\n"
                "    squares: [int: int] = [:];\n"
                "    index: int = 0;\n"
                "    while (index < count) { squares[index] = index; index += "
                "1; }\n"
                "    index = 0;\n"
                "    while (index < count) { delete squares[index]; index += "
                "2; }\n"
                "    total: int = 0;\n"
                "    for (value in squares) { total += 1; }\n"
                "    return total;\n}",
      .updates = true,
      .calls = { { .function = "Build", .arguments = "[3]", .expected = "[1]" } } },
    { .name = "map loop names",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nAnswer :: () -> int {\n"
                "    m: [int: int] = [1: 2];\n    total: int = 0;\n"
                "    for (k, v in m) { total += k * 10 + v; }\n"
                "    v: int = 3;\n    k: int = 4;\n"
                "    return total * 100 + v + k;\n}",
      .calls = { { .function = "Answer", .arguments = "[]", .expected = "[1207]" } } },
    { .name = "duplicate map loop names",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nAnswer :: () { m: [int: int] = [:]; for (x, x "
                "in m) {} }",
      .message = "Duplicate declaration 'x'",
      .line = 2,
      .column = 45 },
    { .name = "inferred map literal",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nAnswer :: () -> int { m := [\"a\": 1]; return "
                "0; }",
      .message = "map literal needs a [K: V] type",
      .line = 2,
      .column = 28 },
    { .name = "compound map assignment",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\nAdd :: () -> int { m: [int: int] = [:]; m[1] "
                "+= 2; return m[1]; }",
      .message = "compound map assignment is not supported yet",
      .line = 2,
      .column = 41 },
    /* A map parameter is the function's own value: removing a key leaves
     * the caller's map as it was. */
    { .name = "map parameter removal",
      .group = COLLECTION_SAMPLES,
      .source = "package facts\n"
                "Drop :: (m: [int: int]) -> int { delete m[1]; if 1 in m { return 1; } return 0; }\n"
                "Keep :: () -> int { m: [int: int] = [1: 2]; gone := Drop(m); return gone * 100 + m[1]; }",
      .calls = { { .function = "Keep", .arguments = "[]", .expected = "[2]" } } },
    { .name = "bounds",
      .source = "package facts\nPick :: (xs: int[], i: int) -> int { "
                "return xs[i]; }",
      .calls = { { .function = "Pick", .arguments = "[[1,2],1]", .expected = "[2]" },
                 { .function = "Pick", .arguments = "[[1,2],-1]", .expected = NULL },
                 { .function = "Pick", .arguments = "[[],0]", .expected = NULL } } },
    { .name = "unknown name",
      .source = "package facts\nAnswer :: () -> int { return missing; }",
      .message = "Unknown name 'missing'",
      .line = 2,
      .column = 30 },
    { .name = "arity",
      .source = "package facts\nOne :: (a: int) -> int { return a; "
                "}\nTwo :: () -> int { return One(); }",
      .message = "call has the wrong number of arguments",
      .line = 3,
      .column = 27 },
    { .name = "types",
      .source = "package facts\nAnswer :: () -> int { return true; }",
      .message = "returned value differs from the result type",
      .line = 2,
      .column = 23 },
    { .name = "untyped literal",
      .source = "package facts\nPair :: struct { a: int; }\n"
                "One :: () -> int { p := {a: 1}; return p.a; }",
      .message = "Struct literal needs an explicit record type",
      .line = 3,
      .column = 25 },
    { .name = "unknown field",
      .source = "package facts\nPair :: struct { a: int; }\n"
                "One :: (p: Pair) -> int { return p.b; }",
      .message = "Unknown field 'b'",
      .line = 3,
      .column = 34 },
    { .name = "missing field",
      .source = "package facts\nPair :: struct { a: int; b: int; }\n"
                "One :: () -> Pair { return {a: 1}; }",
      .message = "Struct literal needs every field",
      .line = 3,
      .column = 28 },
    { .name = "duplicate member",
      .source = "package facts\nPair :: struct { a: int; b: int; }\n"
                "One :: () -> Pair { return {a: 1, a: 2}; }",
      .message = "Duplicate literal member 'a'",
      .line = 3,
      .column = 35 },
    { .name = "shared scope",
      .source = "package facts\nPoint :: struct { x: int; }\n"
                "Point :: () -> int { return 1; }",
      .message = "Duplicate declaration 'Point'",
      .line = 3,
      .column = 1 },
    { .name = "no value",
      .source = "package facts\nNothing :: () { }\n"
                "One :: () -> int { x := Nothing(); return 0; }",
      .message = "call has no value",
      .line = 3,
      .column = 20 },
    { .name = "untyped array",
      .source = "package facts\nOne :: () -> int { xs := [1]; return 0; "
                "}",
      .message = "an untyped number needs a type; give the array an "
                 "integer type",
      .line = 2,
      .column = 27 },
    { .name = "unknown function",
      .source = "package facts\nOne :: () -> int { return Missing(1); }",
      .message = "Unknown function 'Missing'",
      .line = 2,
      .column = 27 },
    { .name = "index type",
      .source = "package facts\nOne :: (xs: int[]) -> int { return "
                "xs[true]; }",
      .message = "index needs an int",
      .line = 2,
      .column = 39 },
    { .name = "reference for",
      .source = "package facts\nOne :: (xs: int[]) -> int { for &x in xs { } "
                "return 0; }",
      .message = "`for &item` loops are not supported yet",
      .line = 2,
      .column = 29 },
    { .name = "descending range",
      .source = "package facts\nTop :: 3;\nOne :: () -> int { for i in Top..1 "
                "{ } "
                "return 0; }",
      .message = "Range bounds descend; count down with for <",
      .line = 3,
      .column = 20 },
    { .name = "descending reverse range",
      .source = "package facts\nOne :: () -> int { for < i in 10..0 { } "
                "return 0; }",
      .message = "Range bounds descend; for < takes the low bound first, as in "
                 "for < i in 0..10",
      .line = 2,
      .column = 20 },
    { .name = "duplicate keyed binding",
      .source = "package facts\nOne :: (xs: int[]) -> int { for x, x in xs { } "
                "return 0; }",
      .message = "Duplicate declaration 'x'",
      .line = 2,
      .column = 36 },
    { .name = "missing default",
      .source = "package facts\nF :: (a: int = 1, b: int) -> int { return a + "
                "b; "
                "}\nOne :: () -> int { return F(); }",
      .message = "call has the wrong number of arguments",
      .line = 3,
      .column = 27 },
    { .name = "multiplication overflow",
      .source = "package facts\nA: i64 : -1 * -9223372036854775808;",
      .message = "Constant value is outside the range of i64",
      .line = 2,
      .column = 10 },
    { .name = "range assignment",
      .source = "package facts\nOne :: () -> int { for i in 0..3 { i = 1; } "
                "return 0; }",
      .message = "Assignment requires a writable value",
      .line = 2,
      .column = 36 },
    { .name = "for scalar",
      .source = "package facts\nOne :: (n: int) -> int { for (x in "
                "n) { } return 0; }",
      .message = "for needs an array",
      .line = 2,
      .column = 36 },
    { .name = "untyped wildcard argument",
      .source = "package facts\nOne :: () -> string { return "
                "format_text(\"{}\", 1); }",
      .message = "a number needs an integer type here",
      .line = 2,
      .column = 48 },
    { .name = "inferred mismatch",
      .source = "package facts\nOne :: (xs: int[]) -> int { ys := "
                "append(xs, true); return length(ys); }",
      .message = "argument differs from its parameter type",
      .line = 2,
      .column = 35 },
    { .name = "switch scalar pattern",
      .source = "package facts\nU :: union { A | B }\nOne :: (n: int) "
                "-> int { switch n { case .A: return 1; } return 0; }",
      .message = "an integer switch's arms need values",
      .line = 3,
      .column = 42 },
    { .name = "switch string",
      .source = "package facts\nOne :: (s: string) -> int { switch s { "
                "case \"a\": return 1; } return 0; }",
      .message = "switch needs a union or integer value",
      .line = 2,
      .column = 36 },
    { .name = "unknown variant",
      .source = "package facts\nU :: union { A | B }\nOne :: () -> U { "
                "return U.C; }",
      .message = "Unknown variant 'C'",
      .line = 3,
      .column = 25 },
    { .name = "partial bindings",
      .source = "package facts\nU :: union { A(x: int, y: int) }\n"
                "One :: (u: U) -> int { switch u { case .A(x): return "
                "x; } return 0; }",
      .message = "A union payload pattern must bind every field",
      .line = 3,
      .column = 40 },
    { .name = "assigned binding",
      .source = "package facts\nU :: union { A(x: int) }\n"
                "One :: (u: U) -> int { switch u { case .A(x): x = 2; "
                "return x; } return 0; }",
      .message = "Assignment requires a writable value",
      .line = 3,
      .column = 47 },
    { .name = "missing payload",
      .source = "package facts\nU :: union { A(x: int) }\nOne :: () -> "
                "U { return U.A; }",
      .message = "variant 'A' needs its payload",
      .line = 3,
      .column = 25 },
    { .name = "assert message",
      .group = IMPORT_SAMPLES,
      .game = true,
      .source = "package facts\nCheck :: (n: int, m: string) -> int {\n"
                "    assert(n > 0, m);\n    return n;\n}",
      .message = "assert's message is a string constant",
      .line = 3,
      .column = 19 },
    { .name = "unknown imported function",
      .source = "package facts\nimport lib \"project:lib\"\nOne :: () "
                "-> int { return lib.Missing(1); }",
      .library = "package lib\npub Two :: () -> int { return 2; }",
      .message = "Unknown function 'Missing'",
      .oracle_message = "Unknown callable 'lib.Missing'",
      .same_rejection = true,
      .line = 3,
      .column = 27 },
    { .name = "alias conflict",
      .source = "package facts\nimport lib \"project:lib\"\nlib :: () "
                "-> int { return 1; }",
      .library = "package lib\npub Two :: () -> int { return 2; }",
      .message = "Package import alias conflicts with a package declaration",
      .line = 2,
      .column = 1 },
    { .name = "alias value",
      .source = "package facts\nimport lib \"project:lib\"\nOne :: () "
                "-> int { return lib; }",
      .library = "package lib\npub Two :: () -> int { return 2; }",
      .message = "Unknown name 'lib'",
      .line = 3,
      .column = 27 },
    { .name = "constant type",
      .source = "package facts\nOn: bool : true;",
      .message = "constant type is not supported yet",
      .line = 2,
      .column = 5 },
    { .name = "constant cycle",
      .source = "package facts\nA: int : B + 1;\nB: int : A;",
      .message = "Constant 'A' depends on itself",
      .line = 2,
      .column = 10 },
    { .name = "constant overflow",
      .source = "package facts\nA: int : 65536 * 65536;",
      .message = "Constant value is outside the range of int",
      .line = 2,
      .column = 10 },
    { .name = "constant clash",
      .source = "package facts\nA: int : 1;\nA :: () -> int { return 1; "
                "}",
      .message = "Duplicate declaration 'A'",
      .line = 3,
      .column = 1 },
    { .name = "constant assignment",
      .source = "package facts\nA: int : 1;\nOne :: () -> int { A = 2; "
                "return A; }",
      .message = "assignment needs a local name",
      .line = 3,
      .column = 20 },
    { .name = "binding outside condition",
      .source = "package facts\nU :: union { A(x: int) | B }\nOne :: "
                "(u: U) -> bool { return u is .A(x); }",
      .message = "Pattern bindings require a matching if, while or switch arm",
      .line = 3,
      .column = 40 },
    { .name = "negated binding",
      .source = "package facts\nU :: union { A(x: int) | B }\n"
                "One :: (u: U) -> int { if (u is not .A(x)) { return 1; "
                "} return 0; }",
      .message = "Pattern bindings require a matching if, while or switch arm",
      .line = 3,
      .column = 40 },
    { .name = "alternative binding",
      .source = "package facts\nU :: union { A(x: int) | B }\n"
                "One :: (u: U) -> int { if (u is .A(x) || false) { "
                "return 1; } return 0; }",
      .message = "Pattern bindings require a matching if, while or switch arm",
      .line = 3,
      .column = 36 },
    { .name = "binding in else",
      .source = "package facts\nU :: union { A(x: int) | B }\n"
                "One :: (u: U) -> int { if (u is .A(x)) { return x; } "
                "else { return x; } }",
      .message = "Unknown name 'x'",
      .line = 3,
      .column = 68 },
    { .name = "is scalar",
      .source = "package facts\nU :: union { A | B }\nOne :: (n: int) "
                "-> bool { return n is .A; }",
      .message = "is needs a union value",
      .line = 3,
      .column = 34 },
    { .name = "is unknown variant",
      .source = "package facts\nU :: union { A | B }\nOne :: (u: U) -> "
                "bool { return u is .C; }",
      .message = "Unknown union variant 'C'",
      .line = 3,
      .column = 37 },
    { .name = "duplicate local",
      .source = "package facts\nOne :: () -> int { x: int = 1; x: int = "
                "2; "
                "return x; }",
      .message = "Duplicate declaration 'x'",
      .line = 2,
      .column = 32 },
    { .name = "duplicate binding",
      .source = "package facts\nU :: union { A(x: int, y: int) | B }\n"
                "One :: (u: U) -> int { if (u is .A(n, n)) { return n; "
                "} return 0; }",
      .message = "Duplicate declaration 'n'",
      .line = 3,
      .column = 39 },
    { .name = "duplicate condition binding",
      .source = "package facts\nU :: union { A(x: int) | B }\n"
                "One :: (u: U, v: U) -> int { if (u is .A(n) && v is "
                ".A(n)) { return n; } return 0; }",
      .message = "Duplicate declaration 'n'",
      .line = 3,
      .column = 56 },
    { .name = "signed with u64",
      .source = "package facts\nOne :: (a: int, b: u64) -> u64 { "
                "return a + b; }",
      .message = "Mismatched integer types int and u64; convert one operand",
      .line = 2,
      .column = 41 },
    { .name = "u8 literal range",
      .source = "package facts\nOne :: () -> u8 { return 256; }",
      .message = "Numeric literal is outside the range of u8",
      .line = 2,
      .column = 26 },
    { .name = "negative u64",
      .source = "package facts\nOne :: () -> u64 { x: u64 = -1; "
                "return x; }",
      .message = "Numeric literal is outside the range of u64",
      .line = 2,
      .column = 29 },
    { .name = "inferred numeric locals",
      .source = "package facts\n"
                "Bump :: (x: ^int) { x^ += 1; }\n"
                "ByReference :: () -> int { x := 0; Bump(&x); return x; }\n"
                "One :: () -> int { x := 5; return x; }\n"
                "Index :: () -> int { x := 0; xs: int[] = [4, 7]; x += 1; "
                "xs[x] = 9; return xs[x] + x; }\n"
                "Negative :: () -> int { x := -1; x += 2; return x; }\n"
                "Wide :: () -> i64 { x := 2147483648; x += 1; return x; }\n"
                "Maximum :: () -> u64 { x := 18446744073709551615; return "
                "x; }\n"
                "Decimal :: () -> number { x := 0.5; x += 0.25; return x; }\n"
                "Exponent :: () -> number { x := 1e3; return x; }",
      .calls = { { .function = "ByReference", .arguments = "[]", .expected = "[1]" },
                 { .function = "One", .arguments = "[]", .expected = "[5]" },
                 { .function = "Index", .arguments = "[]", .expected = "[10]" },
                 { .function = "Negative", .arguments = "[]", .expected = "[1]" },
                 { .function = "Wide", .arguments = "[]", .expected = "[2147483649]" },
                 { .function = "Maximum", .arguments = "[]", .expected = "[18446744073709551615]" },
                 { .function = "Decimal", .arguments = "[]", .expected = "[0.75]" },
                 { .function = "Exponent", .arguments = "[]", .expected = "[1000.0]" } } },
    { .name = "inferred integer refuses number assignment",
      .source = "package facts\nOne :: () -> int { x := 0; fraction := 0.5; x "
                "= fraction; return x; }",
      .message = "assigned value differs from its local",
      .line = 2,
      .column = 45 },
    { .name = "conversion arity",
      .source = "package facts\nOne :: () -> u8 { return u8(1, 2); "
                "}",
      .message = "Conversion takes one value",
      .line = 2,
      .column = 26 },
    { .name = "conversion name",
      .source = "package facts\nOne :: () -> u8 { return u8(round: "
                "1); }",
      .message = "A conversion argument may only be named wrap",
      .line = 2,
      .column = 29 },
    { .name = "floating constant",
      .source = "package facts\nHalf :: 0.5;",
      .message = "only integer constants are supported yet",
      .line = 2,
      .column = 9 },
    { .name = "negative zero u8",
      .source = "package facts\nOne :: () -> u8 { x: u8 = -0; "
                "return x; }",
      .message = "Numeric literal is outside the range of u8",
      .line = 2,
      .column = 27 },
    /* Unary complements are classified as literal-shaped for debug metadata.
     * Their folded value still needs one watch, including through grouping. */
    { .name = "folded complement retains its watch",
      .source = "package facts\nOne :: () -> int { return ~0; }\n"
                "Grouped :: () -> int { return (~0); }",
      .calls = { { .function = "One", .arguments = "[]", .expected = "[-1]" },
                 { .function = "Grouped", .arguments = "[]", .expected = "[-1]" } } },
    /* An untyped operand of a conversion is int64, or uint64 beyond it,
     * in parentheses too. */
    { .name = "wrapped literal in parentheses",
      .source = "package facts\nOne :: () -> int { return int(wrap: "
                "(0xFFFFFFFFFFFFFFFF)); }",
      .calls = { { .function = "One", .arguments = "[]", .expected = "[-1]" } } },
    { .name = "untyped empty array",
      .source = "package facts\nOne :: () -> int { xs := []; return "
                "length(xs); }",
      .message = "Empty arrays need an explicit element type",
      .line = 2,
      .column = 26 },
    { .name = "results in an array",
      .source = "package facts\nOne :: (s: string) -> bool { xs := "
                "[compiler_json_string(s)]; return true; }",
      .message = "array item has no value type",
      .line = 2,
      .column = 37 },
    { .name = "results assignment",
      .source = "package facts\nOne :: (s: string) -> bool { d := "
                "compiler_json_string(s); d = "
                "compiler_json_string(s); return d.valid; }",
      .message = "assigning a native's several results is not supported yet",
      .line = 2,
      .column = 60 },
    { .name = "binding field assignment",
      .source = "package facts\nP :: struct { x: int; }\nU :: union { A(p: P) "
                "| B }\n"
                "One :: (u: U) -> int { switch u { case .A(p): p.x = 1; "
                "return p.x; case .B: return 0; } return 0; }",
      .message = "Assignment requires a writable value",
      .line = 4,
      .column = 47 },
    { .name = "for field assignment",
      .source = "package facts\nP :: struct { x: int; }\nOne :: (ps: "
                "P[]) -> int { for (p in ps) { p.x = 1; } return 0; }",
      .message = "Assignment requires a writable value",
      .line = 3,
      .column = 43 },
    { .name = "for assignment",
      .source = "package facts\nOne :: (xs: int[]) -> "
                "int { for (x in xs) { x = 1; } return 0; }",
      .message = "Assignment requires a writable value",
      .line = 2,
      .column = 45 },
    { .name = "element field assignment",
      .source = "package facts\nP :: struct { x: int; }\nOne :: () -> "
                "int { ps: P[] = []; ps.append({x: 0}); ps[0].x = 1; return ps[0].x; }",
      .calls = { { .function = "One", .arguments = "[]", .expected = "[1]" } } },
    { .name = "non-exhaustive switch",
      .source = "package facts\nU :: union { A | B }\nOne :: (u: U) -> "
                "int { switch u { case .A: return 2; } return 1; }",
      .message = "Non-exhaustive switch: a union alternative is missing",
      .line = 3,
      .column = 24 },
    { .name = "duplicate arm",
      .source = "package facts\nU :: union { A | B }\n"
                "One :: (u: U) -> int { switch u { case .A: return 2; "
                "case .A: return 3; case .B: return 4; } return 1; }",
      .message = "Unreachable duplicate union pattern",
      .line = 3,
      .column = 59 },
    { .name = "duplicate default",
      .source = "package facts\nU :: union { A | B }\n"
                "One :: (u: U) -> int { switch u { case: return 2; "
                "case: return 3; } return 1; }",
      .message = "Duplicate default arm",
      .line = 3,
      .column = 51 },
    { .name = "duplicate field",
      .source = "package facts\nPair :: struct { a: int; a: bool; }",
      .message = "Duplicate field 'a'",
      .line = 2,
      .column = 26 },
    { .name = "byte-preserving strings",
      .source = "package facts\nBinary :: () -> string { return \"a\\u0000b\"; "
                "}\nDefault :: (value: string = \"a\\u0000b\") -> string { "
                "return value; }\nReadDefault :: () -> string { return "
                "Default(); }\nEqual :: () -> bool { return \"a\\u0000b\" == "
                "\"a\\u0000c\"; }",
      .calls = { { .function = "Binary", .arguments = "[]", .expected = "[\"a\\u0000b\"]" },
                 { .function = "ReadDefault", .arguments = "[]", .expected = "[\"a\\u0000b\"]" },
                 { .function = "Equal", .arguments = "[]", .expected = "[false]" } } },
    { .name = "qualified native calls",
      .c_string_probe = true,
      .host_effects = true,
      .source = "package facts\nimport lib \"project:lib\"\n"
                "Probe :: () -> int { return native.probe(\"seven..\"); }\n"
                "Caller :: () -> int { return Probe(); }\n"
                "Recursive :: (n: int) -> int { if (n <= 0) { return Probe(); "
                "} return Recursive(n - 1); }\n"
                "ImportedHost :: () -> int { return lib.Through(); }\n"
                "Length :: () -> int { return native.length(\"four\"); }\n"
                "Text :: () -> int { return native . overload(\"three\"); }\n"
                "Integer :: (value: int) -> int { return native . "
                "overload(value); }",
      .library = "package lib\npub Through :: () -> int { return "
                 "native.probe(\"lib\"); }" },
    { .name = "qualified native import and local precedence",
      .c_string_probe = true,
      .source = "package facts\nimport native \"project:lib\"\n"
                "Imported :: () -> int { return native.probe(\"seven..\"); }\n"
                "Local :: () -> int { native: int[] = [1, 2]; "
                "native.append(3); return length(native); }",
      .library = "package lib\npub probe :: (value: string) -> int { return "
                 "99; }",
      .calls = { { .function = "Imported", .arguments = "[]", .expected = "[99]" },
                 { .function = "Local", .arguments = "[]", .expected = "[3]" } } },
    { .name = "qualified native missing",
      .c_string_probe = true,
      .source = "package facts\nCall :: () -> int { return native.absent(); }",
      .message = "Unknown function 'native.absent'",
      .line = 2,
      .column = 35 },
    { .name = "qualified native ambiguous",
      .c_string_probe = true,
      .source = "package facts\nCall :: () -> int { return "
                "native.PROBE_ONE(\"x\"); }",
      .message = "Ambiguous function 'native.PROBE_ONE'",
      .line = 2,
      .column = 35 },
    { .name = "qualified native import missing",
      .c_string_probe = true,
      .source = "package facts\nimport native \"project:lib\"\nCall :: () -> "
                "int { return native.probe(\"x\"); }",
      .library = "package lib\npub Other :: () -> int { return 0; }",
      .message = "Unknown function 'probe'",
      .oracle_message = "Unknown callable 'native.probe'",
      .same_rejection = true,
      .line = 3,
      .column = 28 },
    { .name = "qualified native local shadow",
      .c_string_probe = true,
      .source = "package facts\nCall :: () -> int { native: int = 1; return "
                "native.probe(\"x\"); }",
      .message = "call needs a function name",
      .line = 2,
      .column = 45 },
    { .name = "C-string dynamic boundary compiles", .c_string_probe = true, .source = "package facts\nDynamic :: (value: string) -> int { return cstring_probe(value); }" },
    { .name = "C-string literal boundary",
      .c_string_probe = true,
      .source = "package facts\nOne :: () -> int { return "
                "cstring_probe(\"a\\u0000b\"); }",
      .message = "C-string boundary 'value' cannot accept NUL at byte 1",
      .same_rejection = true },
    { .name = "C-string parenthesized boundary",
      .c_string_probe = true,
      .source = "package facts\nOne :: () -> int { return "
                "cstring_probe((\"\\u0000b\")); }",
      .message = "C-string boundary 'value' cannot accept NUL at byte 0",
      .same_rejection = true },
    /* A grouped or signed literal takes a floating type as a literal does,
     * and each sign around it is watched. */
    { .name = "grouped signed floating literals",
      .source = "package facts\n"
                "Scale :: (x: f64) -> f64 { return x * -(1.5) + x * (-(-2.5)); "
                "}\n"
                "Half :: (x: number) -> number { return x * -(+(-(-(0.5)))); "
                "}\n",
      .calls = { { .function = "Scale", .arguments = "[2]", .expected = "[2.0]" },
                 { .function = "Half", .arguments = "[4]", .expected = "[-2.0]" } } },
    /* A floating literal's outer signs are each watched once; stage zero
     * folds an integer one, a whole decimal too, to one constant. */
    { .name = "grouped signed literal results",
      .traced = true,
      .source = "package facts\n"
                "Half :: () -> f64 { return -(1.5); }\n"
                "Back :: () -> f64 { return -(-(1.5)); }\n"
                "Two :: () -> int { return -(2); }\n"
                "Three :: () -> int { return -(+(-(3))); }\n"
                "Plus :: () -> int { return +(-(4)); }\n"
                "Whole :: () -> int { return -(+(2.0)); }\n"
                "Unsigned :: () -> u8 { return +(2e0); }\n",
      .calls = { { .function = "Half", .arguments = "[]", .expected = "[-1.5]" },
                 { .function = "Back", .arguments = "[]", .expected = "[1.5]" },
                 { .function = "Two", .arguments = "[]", .expected = "[-2]" },
                 { .function = "Three", .arguments = "[]", .expected = "[3]" },
                 { .function = "Plus", .arguments = "[]", .expected = "[-4]" },
                 { .function = "Whole", .arguments = "[]", .expected = "[-2]" },
                 { .function = "Unsigned", .arguments = "[]", .expected = "[2]" } } },
    /* An overloaded native's untyped literal argument takes the overload
     * stage zero picks (native_overload_reference_contract) and that
     * overload's type, grouped, signed or a whole decimal. Stage zero folds a
     * grouped one and its signs to one constant. */
    { .name = "untyped literal overload argument",
      .c_string_probe = true,
      .traced = true,
      .source = "package facts\n"
                "Run :: () -> int { return native.overload(2); }\n"
                "Grouped :: () -> int { return native.overload((-(2))); }\n"
                "Whole :: () -> int { return native.overload(2.0); }\n"
                "Exponent :: () -> int { return native.overload(1e0); }\n"
                "Zero :: () -> int { return native.overload(0.0); }\n"
                "Signed :: () -> int { return native.overload(-0.0); }\n"
                "Signs :: () -> int { return native.overload(+(-(-(2)))); }\n"
                "Decimal :: () -> int { return native.overload(-(2.0)); }\n",
      .calls = { { .function = "Run", .arguments = "[]", .expected = "[2]" },
                 { .function = "Grouped", .arguments = "[]", .expected = "[-2]" },
                 { .function = "Signs", .arguments = "[]", .expected = "[2]" },
                 { .function = "Decimal", .arguments = "[]", .expected = "[-2]" },
                 { .function = "Whole", .arguments = "[]", .expected = "[2]" },
                 { .function = "Exponent", .arguments = "[]", .expected = "[1]" },
                 { .function = "Zero", .arguments = "[]", .expected = "[0]" },
                 { .function = "Signed", .arguments = "[]", .expected = "[0]" } } },
    /* Typed arguments choose the kernel their width prefers, i64 the f64
     * one and the rest number, and literals take the number one. */
    { .name = "typed overload kernels",
      .c_string_probe = true,
      .traced = true,
      .source =
          "package facts\n"
          "Literal :: () -> int { return native.kernel(2); }\n"
          "Decimal :: () -> int { return native.kernel(2.5); }\n"
          "Integer :: () -> int { v: int = 3; return native.kernel(v * 2); }\n"
          "Wide :: () -> int { v: i64 = 3; return native.kernel(v * 2); }\n"
          "Single :: () -> int { v: number = 1.5; return native.kernel(v); }\n"
          "Double :: () -> int { v: f64 = 1.5; return native.kernel(v); }\n",
      .calls = { { .function = "Literal", .arguments = "[]", .expected = "[1]" },
                 { .function = "Decimal", .arguments = "[]", .expected = "[1]" },
                 { .function = "Integer", .arguments = "[]", .expected = "[1]" },
                 { .function = "Wide", .arguments = "[]", .expected = "[2]" },
                 { .function = "Single", .arguments = "[]", .expected = "[1]" },
                 { .function = "Double", .arguments = "[]", .expected = "[2]" } } },
    /* An argument before a call the overload depends on is still typed,
     * converted and watched first, when the call has an effect and when it
     * fails. */
    { .name = "overload argument order",
      .c_string_probe = true,
      .traced = true,
      .source = "package facts\n"
                "Later :: () -> int { return native.probe(\"ab\"); }\n"
                "Fails :: () -> int { xs: int[] = []; return xs[0]; }\n"
                "Before :: () -> int { return native.pair(2, Later()); }\n"
                "After :: () -> int { return native.pair(Later(), 2); }\n"
                "Stops :: () -> int { return native.pair(-(2), Fails()); }\n"
                "Floating :: () -> int { return native.pair(+(-(2.5)), "
                "Later()); }\n"
                "Local :: () -> int { v: f64 = 2.5; return native.pair(v, "
                "Later()); }\n"
                "Widened :: () -> int { v: number = 1.5; return native.pair(v, "
                "Later()); }\n",
      .calls = { { .function = "Before", .arguments = "[]", .expected = "[2]" },
                 { .function = "After", .arguments = "[]", .expected = "[1]" },
                 { .function = "Stops", .arguments = "[]" },
                 { .function = "Floating", .arguments = "[]", .expected = "[2]" },
                 { .function = "Local", .arguments = "[]", .expected = "[2]" },
                 { .function = "Widened", .arguments = "[]", .expected = "[2]" } } },
    /* Parentheses produce no value: a grouped argument is typed, converted
     * and watched once the expression inside has produced it. */
    { .name = "grouped overload arguments",
      .c_string_probe = true,
      .traced = true,
      .source =
          "package facts\n"
          "Later :: () -> int { return native.probe(\"ab\"); }\n"
          "Fails :: () -> int { xs: int[] = []; return xs[0]; }\n"
          "Typed :: () -> int { v: number = 1.5; return "
          "native.kernel((v)); }\n"
          "Converted :: () -> int { v: int = 3; return native.kernel((v)); "
          "}\n"
          "Wide :: () -> int { v: i64 = 3; return native.kernel(((v))); }\n"
          "Effect :: () -> int { return native.pair(2, (Later())); }\n"
          "First :: () -> int { return native.pair((Later()), (2)); }\n"
          "Stops :: () -> int { return native.pair((-(2)), (Fails())); }\n"
          "Local :: () -> int { v: f64 = 2.5; return native.pair((v), "
          "(Later())); }\n",
      .calls = { { .function = "Typed", .arguments = "[]", .expected = "[1]" },
                 { .function = "Converted", .arguments = "[]", .expected = "[1]" },
                 { .function = "Wide", .arguments = "[]", .expected = "[2]" },
                 { .function = "Effect", .arguments = "[]", .expected = "[2]" },
                 { .function = "First", .arguments = "[]", .expected = "[1]" },
                 { .function = "Stops", .arguments = "[]" },
                 { .function = "Local", .arguments = "[]", .expected = "[2]" } } },
    /* Both stages read a decimal at an integer type exactly, digit by digit:
     * no floating rounding drops a fraction, an integer unit or the range. */
    { .name = "exact whole decimals",
      .traced = true,
      .source = "package facts\n"
                "Sum :: () -> i64 { x: i64 = 0; return x + 9007199254740993.0; "
                "}\n"
                "Negative :: () -> i64 { return -9007199254740993.0; }\n"
                "Largest :: () -> u64 { return 18446744073709551615.0; }\n"
                "Scaled :: () -> i64 { return 9007199254740993e0; }\n"
                "Least :: () -> i64 { return -9223372036854775808.0; }\n",
      .calls = { { .function = "Sum", .arguments = "[]", .expected = "[9007199254740993]" },
                 { .function = "Negative", .arguments = "[]", .expected = "[-9007199254740993]" },
                 { .function = "Largest", .arguments = "[]", .expected = "[18446744073709551615]" },
                 { .function = "Scaled", .arguments = "[]", .expected = "[9007199254740993]" },
                 { .function = "Least", .arguments = "[]", .expected = "[-9223372036854775808]" } } },
    /* Zero is whole however it is spelled. */
    { .name = "zero decimals",
      .traced = true,
      .source = "package facts\n"
                "Zero :: () -> int { return 0.0; }\n"
                "Negative :: () -> int { return -0.0; }\n"
                "Scaled :: () -> int { return 0e-1; }\n"
                "Places :: () -> i64 { return 0.000; }\n",
      .calls = { { .function = "Zero", .arguments = "[]", .expected = "[0]" },
                 { .function = "Negative", .arguments = "[]", .expected = "[0]" },
                 { .function = "Scaled", .arguments = "[]", .expected = "[0]" },
                 { .function = "Places", .arguments = "[]", .expected = "[0]" } } },
    { .name = "decimal past the least integer",
      .source = "package facts\nRun :: () -> i64 { return "
                "-9223372036854775809.0; }",
      .message = "Numeric literal is outside the range of i64",
      .same_rejection = true },
    { .name = "decimal past the largest integer",
      .source = "package facts\nRun :: () -> u64 { return "
                "18446744073709551616.0; }",
      .message = "Numeric literal is outside the range of u64",
      .same_rejection = true },
    { .name = "small fraction overload argument",
      .c_string_probe = true,
      .source = "package facts\nRun :: () -> int { return "
                "native.overload(2.0000000000000001); }",
      .message = "Numeric literal has a fraction, which int cannot hold",
      .same_rejection = true },
    { .name = "smaller fraction overload argument",
      .c_string_probe = true,
      .source = "package facts\nRun :: () -> int { return "
                "native.overload(2.0000000000000000001); }",
      .message = "Numeric literal has a fraction, which int cannot hold",
      .same_rejection = true },
    { .name = "small fraction integer",
      .source = "package facts\nRun :: () -> i64 { x: i64 = 0; return x + "
                "9007199254740992.5; }",
      .message = "Numeric literal has a fraction, which i64 cannot hold",
      .same_rejection = true },
    { .name = "ambiguous literal overload",
      .c_string_probe = true,
      .source = "package facts\nRun :: () -> int { return native.pair(2, 3); }",
      .message = "Ambiguous function 'native.pair'",
      .line = 2,
      .column = 34 },
    { .name = "decimal overload argument",
      .c_string_probe = true,
      .source = "package facts\nRun :: () -> int { return "
                "native.overload(2.5); }",
      .message = "Numeric literal has a fraction, which int cannot hold",
      .same_rejection = true },
    { .name = "wide overload argument",
      .c_string_probe = true,
      .source = "package facts\nRun :: () -> int { return "
                "native.overload(4294967296); }",
      .message = "Numeric literal is outside the range of int",
      .same_rejection = true },
    /* Authored constant support boundaries. Stage zero accepts some of these
     * forms; require the authored diagnostic at the operand, not parity with
     * stage zero or an earlier unsupported-declaration failure. */
    { .name = "floating constant cannot become integer bits",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nWide: f64 : 1.5;\nBad: u64 : "
                "u64(Wide);\n",
      .message = "floating-to-integer constant conversion is not supported yet",
      .line = 3,
      .column = 16 },
    { .name = "floating constant cannot wrap into integer bits",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nWide: f64 : 1.5;\nBad: int : int(wrap: "
                "Wide);\n",
      .message = "floating-to-integer constant conversion is not supported yet",
      .line = 3,
      .column = 22 },
    { .name = "floating conversion cannot become integer bits",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nBad: u64 : u64(f64(1.5));\n",
      .message = "floating-to-integer constant conversion is not supported yet",
      .line = 2,
      .column = 16 },
    { .name = "floating shift count cannot become integer bits",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nWide: f64 : 1.5;\nBad: int : 1 << Wide;\n",
      .message = "constant value must be an integer expression",
      .line = 3,
      .column = 17 },
    { .name = "floating binary constant initializer remains unsupported",
      .group = IMPORT_SAMPLES,
      .source = "package facts\nBad: f64 : 1.0 + 2.0;\n",
      .message = "floating constant initializer is not supported yet",
      .line = 2,
      .column = 12 },
};
enum
{
    SAMPLES = sizeof samples / sizeof *samples
};
/* Source-level contracts use literal answers. They do not ask the constant
 * evaluator or either compiler to calculate the expected result. */
static const sample math_samples[] = {
    { .name = "math unary and matrix arithmetic retain their shapes",
      .source = "package facts\nCheck :: () -> bool { v := vec2(1, -2); "
                "return -v == vec2(-1, 2) && v != vec2(0) && mat2(1) != "
                "mat2(0) && "
                "(mat2(1) + mat2(2)) * v == vec2(3, -6) && "
                "((mat2(4) - mat2(2)) / 2) * v == v && "
                "(-mat2(1)) * v == -v && (2 * mat2(1)) * v == v * 2; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math sign saturate and round use scalar component rules",
      .source = "package facts\nCheck :: () -> bool { "
                "return sign(vec3(-2, 0, 3)) == vec3(-1, 0, 1) && "
                "saturate(vec3(-1, 0.5, 2)) == vec3(0, 0.5, 1) && "
                "round(vec3(0.5, 1.5, -2.5)) == vec3(1, 2, -3); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math modulo follows divisor sign and powers preserve order",
      .source = "package facts\nCheck :: () -> bool { "
                "return mod(vec2(-3, 3), 2) == vec2(1, 1) && "
                "mod(vec2(-3, 3), -2) == vec2(-1, -1) && "
                "pow(vec3(2, 3, 4), 2) == vec3(4, 9, 16) && "
                "sqrt(vec3(4, 9, 16)) == vec3(2, 3, 4); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math exponential and logarithm functions keep f32 results",
      .source = "package facts\nCheck :: () -> bool { "
                "a := exp(vec2(0, 1)); b := log(vec2(1, 2.7182818)); "
                "return a.x == 1.0 && abs(a.y - 2.7182818) < 0.000001 && "
                "b.x == 0.0 && abs(b.y - 1.0) < 0.000001 && "
                "exp2(vec3(-1, 0, 1)) == vec3(0.5, 1, 2) && "
                "log2(vec3(0.5, 1, 4)) == vec3(-1, 0, 2); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math trigonometric functions use radians and atan2 y x order",
      .source = "package facts\nCheck :: () -> bool { "
                "a := sin(vec2(0, 1.5707963)); b := cos(vec2(0, 1.5707963)); "
                "c := atan2(vec2(0, 1), vec2(1, 0)); "
                "return a.x == 0.0 && abs(a.y - 1.0) < 0.000001 && "
                "b.x == 1.0 && abs(b.y) < 0.000001 && c.x == 0.0 && "
                "abs(c.y - 1.5707963) < 0.000001 && "
                "tan(vec2(0)) == vec2(0) && asin(vec2(0)) == vec2(0) && "
                "acos(vec2(1)) == vec2(0) && atan(vec2(0)) == vec2(0); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math pointer receiver calls run once before the rhs",
      .source = "package facts\n"
                "Pick :: (v: ^vec3, calls: ^int) -> ^vec3 { "
                "calls^ = calls^ * 10 + 1; return v; }\n"
                "Right :: (calls: ^int) -> vec2 { calls^ = calls^ * 10 + 2; "
                "return vec2(7, 8); }\n"
                "Check :: () -> (calls: int, x: number, y: number, z: number) "
                "{ "
                "v := vec3(1, 2, 3); total: int = 0; Pick(&v, &total).yx = "
                "Right(&total); "
                "return (total, v.x, v.y, v.z); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[12,8.0,7.0,3.0]" } } },
    { .name = "math swizzles preserve copied map values",
      .source = "package facts\n"
                "Check :: () -> (x: number, y: number, z: number, sx: number, "
                "sy: number, sz: number) { "
                "rows: [int: vec3] = [1: vec3(1, 2, 3)]; "
                "before := rows; rows[1].yx = rows[1].xy; "
                "return (rows[1].x, rows[1].y, rows[1].z, before[1].x, "
                "before[1].y, before[1].z); }\n",
      .calls = { { .function = "Check",
                   .arguments = "[]",
                   .expected = "[2.0,1.0,3.0,1.0,2.0,3.0]" } } },
    { .name = "math constructor splats concatenation and function returns",
      .source = "package facts\n"
                "Foo :: () -> vec3 { return vec3(2, 4, 8); }\n"
                "Check :: () -> bool { v := Foo(); a := vec4(v.xy, v.z, 16); "
                "b := vec3(3); c := vec4(vec2(1, 2), vec2(3, 4)); "
                "return v == vec3(2, 4, 8) && a == vec4(2, 4, 8, 16) && "
                "b == vec3(3, 3, 3) && c == vec4(1, 2, 3, 4); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math explicit f64 narrowing rounds at f32 width",
      .source = "package facts\n"
                "Check :: () -> bool { wide: f64 = 16777217.0; "
                "v := vec3(f32(wide), 2, 3); return v.x == 16777216.0 && "
                "v.y == 2.0 && v.z == 3.0; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math broadcasts preserve subtraction and division order",
      .source = "package facts\n"
                "Check :: () -> bool { v := vec3(2, 4, 8); "
                "return 2 - v == vec3(0, -2, -6) && v - 2 == vec3(0, 2, 6) && "
                "16 / v == vec3(8, 4, 2) && v / 2 == vec3(1, 2, 4) && "
                "2 + v == vec3(4, 6, 10) && v * 2 == vec3(4, 8, 16) && "
                "v * vec3(3, 2, 1) == vec3(6, 8, 8); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math swizzle writes snapshot aliased values",
      .source = "package facts\n"
                "Check :: () -> bool { v := vec4(1, 2, 3, 4); "
                "v.xy = v.yx; a := v.xxy; v.ba = v.rb; "
                "return v == vec4(2, 1, 2, 3) && a == vec3(2, 2, 1); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math compound swizzles preserve untouched components",
      .source = "package facts\n"
                "Check :: () -> bool { v := vec4(1, 2, 3, 4); "
                "v.yx += vec2(5, 6); v.zw *= 2; "
                "return v == vec4(7, 7, 6, 8); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math swizzles write through pointers and array elements",
      .source = "package facts\n"
                "Set :: (p: ^vec3) { p.yx = vec2(7, 8); }\n"
                "Check :: () -> bool { v := vec3(1, 2, 3); Set(&v); "
                "rows: vec3[] = [vec3(2, 4, 6), vec3(8, 10, 12)]; "
                "rows[0].yx = rows[0].xy; return v == vec3(8, 7, 3) && "
                "rows[0] == vec3(4, 2, 6) && rows[1] == vec3(8, 10, 12); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math swizzle receiver and rhs execute once in source order",
      .source = "package facts\n"
                "Index :: (calls: ^int) -> int { calls^ = calls^ * 10 + 1; "
                "return 0; }\n"
                "Right :: (rows: ^(vec4[]), calls: ^int) -> vec2 { "
                "calls^ = calls^ * 10 + 2; rows^[0].w = 9; return rows^[0].yx; "
                "}\n"
                "Check :: () -> bool { calls: int = 0; "
                "rows: vec4[] = [vec4(1, 2, 3, 4), vec4(5, 6, 7, 8)]; "
                "rows[Index(&calls)].xy = Right(&rows, &calls); return calls "
                "== 12 && "
                "rows[0] == vec4(2, 1, 3, 9) && rows[1] == vec4(5, 6, 7, 8); "
                "}\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math column matrix products do not commute",
      .source = "package facts\n"
                "Check :: () -> bool { a := mat2(vec2(1, 2), vec2(0, 1)); "
                "b := mat2(2, 0, 0, 3); v := vec2(1, 1); "
                "left := (a * b) * v; right := (b * a) * v; "
                "return left == vec2(2, 7) && right == vec2(2, 9) && "
                "a * (b * v) == left && mat2(1) * v == v; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math homogeneous matrix columns carry translations",
      .source = "package facts\n"
                "Check :: () -> bool { translate := mat3(1, 0, 0, 0, 1, 0, 5, "
                "7, 1); "
                "scale := mat3(2, 0, 0, 0, 3, 0, 0, 0, 1); v := vec3(2, 3, 1); "
                "return (translate * scale) * v == vec3(9, 16, 1) && "
                "(scale * translate) * v == vec3(14, 30, 1) && "
                "mat4(1) * vec4(v, 1) == vec4(2, 3, 1, 1); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math quaternion Hamilton products retain their order",
      .source = "package facts\n"
                "Check :: () -> bool { x := quat(1, 0, 0, 0); y := quat(0, 1, "
                "0, 0); "
                "return x * y == quat(0, 0, 1, 0) && y * x == quat(0, 0, -1, "
                "0) && "
                "x * x == quat(0, 0, 0, -1) && quat(0, 0, 0, 1) * y == y; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math constructor refuses excess components",
      .source = "package facts\nBad :: () -> vec2 { return vec2(1, 2, 3); }\n",
      .message = "vec2 needs 2 components; these arguments supply 3",
      .line = 2,
      .column = 28,
      .oracle_message = "vec2 needs 2 components; these arguments supply 3",
      .oracle_line = 2,
      .oracle_column = 32 },
    { .name = "math constructor refuses implicit f64 narrowing",
      .source = "package facts\nBad :: (wide: f64) -> vec3 { return "
                "vec3(wide); }\n",
      .message = "math components require explicit f32(value) narrowing from "
                 "f64",
      .line = 2,
      .column = 37,
      .oracle_message = "Math components require explicit f32(value) narrowing "
                        "from f64",
      .oracle_line = 2,
      .oracle_column = 42 },
    { .name = "math writable swizzles refuse duplicate destinations",
      .source = "package facts\nBad :: () { v := vec3(1, 2, 3); v.xx = vec2(4, "
                "5); }\n",
      .message = "a writable swizzle cannot repeat a destination component",
      .line = 2,
      .column = 33,
      .oracle_message = "A writable swizzle cannot repeat a destination "
                        "component",
      .oracle_line = 2,
      .oracle_column = 33 },
    { .name = "math read swizzles refuse mixed component alphabets",
      .source = "package facts\nBad :: () -> vec2 { v := vec3(1, 2, 3); return "
                "v.xr; }\n",
      .message = "a swizzle cannot mix xyzw and rgba components",
      .line = 2,
      .column = 48,
      .oracle_message = "A swizzle uses one of xyzw or rgba without mixing "
                        "them",
      .oracle_line = 2,
      .oracle_column = 50 },
    { .name = "math vector operands require the same width",
      .source = "package facts\nBad :: () -> vec2 { return vec2(1) + vec3(1); "
                "}\n",
      .message = "cannot apply + to vec2 and vec3",
      .line = 2,
      .column = 28,
      .oracle_message = "Cannot combine vec2 and vec3 with this math operator",
      .oracle_line = 2,
      .oracle_column = 28 },
    { .name = "math vector matrix multiplication states its convention",
      .source = "package facts\nBad :: () -> vec2 { return vec2(1) * mat2(1); "
                "}\n",
      .message = "a matrix multiplies a column vector on its right; write "
                 "matrix * vector",
      .line = 2,
      .column = 28,
      .oracle_message = "A matrix multiplies a column vector on its right; "
                        "write matrix * vector",
      .oracle_line = 2,
      .oracle_column = 28 },
    { .name = "math quaternion rotation waits for recoverable results",
      .source = "package facts\nBad :: () -> vec3 { return quat(0, 0, 0, 1) * "
                "vec3(1, 0, 0); }\n",
      .message = "quaternion rotation requires the recoverable math result "
                 "contract",
      .line = 2,
      .column = 28,
      .oracle_message = "Quaternion rotation requires the recoverable math "
                        "result adapter",
      .oracle_line = 2,
      .oracle_column = 28 },
    { .name = "math matrix representation stays private",
      .source = "package facts\nBad :: () -> number { m := mat2(1); return "
                "m.c0r0; }\n",
      .message = "matrix storage fields are internal; use matrix operations",
      .line = 2,
      .column = 44,
      .oracle_message = "Matrix storage fields are internal; use matrix "
                        "operations",
      .oracle_line = 2,
      .oracle_column = 46 },
    { .name = "math dot and cross have independent scalar and handedness "
              "answers",
      .source = "package facts\n"
                "Check :: () -> bool { a := vec3(1, 2, 3); b := vec3(4, 5, 6); "
                "return dot(a, b) == 32.0 && cross(a, b) == vec3(-3, 6, -3) && "
                "cross(b, a) == vec3(3, -6, 3); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math length and distance use three four five triangles",
      .source = "package facts\n"
                "Check :: () -> bool { return length(vec2(3, 4)) == 5.0 && "
                "length(vec3(0, 3, 4)) == 5.0 && length(vec4(0, 0, 3, 4)) == "
                "5.0 && "
                "distance(vec3(1, 2, 3), vec3(4, 6, 3)) == 5.0; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math absolute bounds and clamp broadcast scalar arguments",
      .source = "package facts\n"
                "Check :: () -> bool { v := vec3(-2, 0.5, 3); "
                "return abs(v) == vec3(2, 0.5, 3) && min(v, 1) == vec3(-2, "
                "0.5, 1) && "
                "max(0, v) == vec3(0, 0.5, 3) && clamp(v, 0, 1) == vec3(0, "
                "0.5, 1); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math mix accepts scalar and component weights",
      .source = "package facts\n"
                "Check :: () -> bool { return "
                "mix(vec3(0, 2, 4), vec3(8, 10, 12), 0.25) == vec3(2, 4, 6) && "
                "mix(vec3(0), vec3(8), vec3(0, 0.5, 1)) == vec3(0, 4, 8); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math step takes edge before value and includes the edge",
      .source = "package facts\n"
                "Check :: () -> bool { return step(2.0, 1.0) == 0.0 && "
                "step(2.0, 2.0) == 1.0 && "
                "step(0.5, vec3(0.25, 0.5, 0.75)) == vec3(0, 1, 1) && "
                "step(vec3(0.25, 0.5, 0.75), 0.5) == vec3(1, 1, 0); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math smoothstep clamps values outside its edges",
      .source = "package facts\n"
                "Check :: () -> bool { return "
                "smoothstep(0, 1, vec4(-1, 0, 0.5, 2)) == vec4(0, 0, 0.5, 1); "
                "}\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math floor ceil and fract agree on negative fractions",
      .source = "package facts\n"
                "Check :: () -> bool { v := vec3(-1.25, 1.75, 2); "
                "return floor(v) == vec3(-2, 1, 2) && ceil(v) == vec3(-1, 2, "
                "2) && "
                "fract(v) == vec3(0.75, 0.75, 0); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math reflect and refract distinguish total internal reflection",
      .source = "package facts\n"
                "Check :: () -> bool { normal := vec3(0, 1, 0); "
                "return reflect(vec3(1, -1, 0), normal) == vec3(1, 1, 0) && "
                "refract(vec3(0, -1, 0), normal, 0.5) == vec3(0, -1, 0) && "
                "refract(vec3(1, 0, 0), normal, 2) == vec3(0); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math transpose and determinant use column order",
      .source = "package facts\n"
                "Check :: () -> bool { a := mat2(1, 2, 3, 4); "
                "b := mat3(1, 0, 0, 2, 3, 0, 4, 5, 6); "
                "return transpose(a) == mat2(1, 3, 2, 4) && determinant(a) == "
                "-2.0 && "
                "transpose(b) == mat3(1, 2, 4, 0, 3, 5, 0, 0, 6) && "
                "determinant(b) == 18.0; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math matrix component multiplication is distinct from "
              "composition",
      .source = "package facts\n"
                "Check :: () -> bool { a := mat2(1, 2, 3, 4); b := mat2(5, 6, "
                "7, 8); "
                "return matrix_comp_mult(a, b) == mat2(5, 12, 21, 32) && "
                "a * b == mat2(23, 34, 31, 46); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math quaternion conjugation negates the imaginary components",
      .source = "package facts\n"
                "Check :: () -> bool { q := quat(1, 2, 3, 4); "
                "return quat_conjugate(q) == quat(-1, -2, -3, 4) && "
                "quat_conjugate(quat_conjugate(q)) == q; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math normalization handles zero and large finite directions",
      .source = "package facts\n"
                "Check :: () -> bool { zero := normalize_or_zero(vec3(0)); "
                "unit := normalize_or_zero(vec3(3e30, 4e30, 0)); "
                "return zero == vec3(0) && unit.x > 0.599999 && unit.x < "
                "0.600001 && "
                "unit.y > 0.799999 && unit.y < 0.800001 && unit.z == 0.0; }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math binary left operand is captured before a mutating rhs",
      .source = "package facts\n"
                "Mutate :: (p: ^vec3) -> vec3 { p^ = vec3(10, 20, 30); return "
                "vec3(4, 5, 6); }\n"
                "Check :: () -> bool { v := vec3(1, 2, 3); sum := v + "
                "Mutate(&v); "
                "return sum == vec3(5, 7, 9) && v == vec3(10, 20, 30); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math constructor arguments are captured before later mutation",
      .source = "package facts\n"
                "Mutate :: (p: ^vec4) -> vec2 { p^ = vec4(10, 20, 30, 40); "
                "return vec2(5, 6); }\n"
                "Check :: () -> bool { v := vec4(1, 2, 3, 4); made := "
                "vec4(v.xy, Mutate(&v)); "
                "return made == vec4(1, 2, 5, 6) && v == vec4(10, 20, 30, 40); "
                "}\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math function arguments are captured before later mutation",
      .source = "package facts\n"
                "Mutate :: (p: ^vec3) -> vec3 { p^ = vec3(10, 20, 30); return "
                "vec3(4, 5, 6); }\n"
                "Check :: () -> bool { v := vec3(1, 2, 3); product := dot(v, "
                "Mutate(&v)); "
                "return product == 32.0 && v == vec3(10, 20, 30); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math nested swizzles compose their destination components",
      .source = "package facts\n"
                "Check :: () -> bool { v := vec4(1, 2, 3, 4); v.yx.xy = "
                "vec2(7, 8); "
                "picked := v.wzyx.yx; v.wzyx.yx = v.xy; "
                "return picked == vec2(3, 4) && v == vec4(8, 7, 8, 7); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },
    { .name = "math compound swizzles snapshot selected fields and retain rhs "
              "side effects",
      .source = "package facts\n"
                "Right :: (p: ^vec4) -> vec2 { p^ = vec4(100, 200, 30, 40); "
                "return vec2(5, 6); }\n"
                "Check :: () -> bool { v := vec4(1, 2, 3, 4); v.xy += "
                "Right(&v); "
                "return v == vec4(6, 8, 30, 40); }\n",
      .calls = { { .function = "Check", .arguments = "[]", .expected = "[true]" } } },

};

static compiler_fixture fixture, oracle, consumer, corpus;
/* The compiler host's natives, and those with the core pack a game links,
 * which declares assert. */
static native_capabilities *linked[2][2];
static native_capabilities *actions_native;
/* Compile's native tables for the host and for a game, per compiler backend,
 * built once from `linked`. */
static pin_value linked_tables[2][2][2][3];
static pin_value linked_effects[2][2][2];
static arena input, detached;
static compiler_host *host;
static compiler_host_result result;

/* Whether a test published its project's types, which cleanup withdraws
 * after a test that returned early. */
static bool project_published;
static void publish_project(const type_catalog_pack *pack)
{
    type_catalog_set_project(pack);
    project_published = pack;
}
static void cleanup()
{
    if (project_published) {
        publish_project(NULL);
    }
    compiler_fixture_free(&fixture);
    compiler_fixture_free(&oracle);
    compiler_fixture_free(&consumer);
    compiler_fixture_free(&corpus);
    native_capabilities_free(actions_native);
    actions_native = NULL;
    for (int game = 0; game < 2; game++) {
        for (int probe = 0; probe < 2; probe++) {
            native_capabilities_free(linked[game][probe]);
        }
    }
    memset(linked, 0, sizeof linked);
    memset(linked_tables, 0, sizeof linked_tables);
    memset(linked_effects, 0, sizeof linked_effects);
    arena_free(&input);
    arena_free(&detached);
    compiler_host_free(host);
    host = NULL;
    free(result.json);
    result = (compiler_host_result){ 0 };
}
static pin_value text(const char *s)
{
    pin_value value = { 0 };
    assert(!pin_value_text(&input, &value, s, strlen(s)));
    return value;
}
static pin_value strings(const char *const *items, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(items[i]);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
/* Writes an int or int64 field. */
static bool put(pin_value *row, const char *name, int64_t value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && (field->type == PIN_I32 || field->type == PIN_I64));
    pin_value v = { .type = PIN_I64, .i64 = value };
    if (field->type == PIN_I32) {
        REQUIRE(value >= INT32_MIN && value <= INT32_MAX);
        v = (pin_value){ .type = PIN_I32, .i = (int32_t)value };
    }
    REQUIRE(!pin_write_field(&input, row->ptr, field, v));
    return true;
}
static bool put_value(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static pin_value record_row(const compiler_fixture *f, int b, const char *identity, const char *name)
{
    const pin_record *shape = compiler_fixture_record(f, b, identity, name, &input);
    assert(shape);
    pin_value v = { .type = PIN_STRUCT,
                    .ptr = arena_alloc(&input, shape->type.size),
                    .record = shape,
                    .type_name = shape->type.name };
    memset(v.ptr, 0, shape->type.size);
    return v;
}
static pin_value row(int b, const char *name)
{
    return record_row(&fixture, b, PACKAGE, name);
}
static pin_value rows(const pin_value *items, int count, pin_type type, const pin_record *shape)
{
    pin_array *
        array = pin_array_snapshot_allocate(&input, type, shape ? shape->type.name : NULL, count);
    array->record = shape;
    for (int i = 0; i < count; i++) {
        array->items[i] = items[i];
    }
    return (pin_value){ .type = type,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array,
                        .record = shape,
                        .type_name = shape ? shape->type.name : NULL };
}
/* The data functions the compiler host links: no flow, outcomes or
 * suspension. It links the compiler's text profile, which the compiler is
 * compiled for; a game links the public text pack, which also converts bytes
 * to text. */
static bool natives(int b, bool game, bool probe, pin_value *out)
{
    if (linked_tables[game][probe][b][0].ptr) {
        memcpy(out, linked_tables[game][probe][b], sizeof linked_tables[game][probe][b]);
        return true;
    }
    if (!linked[game][probe]) {
        const native_definition_pack packs[] = { game ? NATIVE_TEXT_DEFINITION_PACK
                                                      : NATIVE_COMPILER_TEXT_DEFINITION_PACK,
                                                 NATIVE_ARRAY_DEFINITION_PACK,
                                                 COMPILER_JSON_DEFINITION_PACK,
                                                 COMPILER_SYNTAX_DEFINITION_PACK };
        char error[256];
        native_definition_pack selected[8];
        int count = (int)(sizeof packs / sizeof *packs);
        memcpy(selected, packs, sizeof packs);
        if (game) {
            selected[count++] = CORE_DEFINITION_PACK;
            selected[count++] = NATIVE_MATH_DEFINITION_PACK;
            /* With the arrays', its length overload makes a call of length
             * ambiguous among natives; a collection argument still takes the
             * collection's. */
            selected[count++] = NATIVE_VECTOR_DEFINITION_PACK;
        }
        if (probe) {
            selected[count++] = CSTRING_PACK;
        }
        linked[game][probe] = native_capabilities_from_definition_packs(
            NULL,
            0,
            selected,
            count,
            error,
            sizeof error
        );
        REQUIRE(linked[game][probe]);
    }
    REQUIRE(compiler_fixture_natives(
        &fixture,
        b,
        PACKAGE,
        native_capabilities_catalog(linked[game][probe]),
        &input,
        out,
        &linked_effects[game][probe][b]
    ));
    memcpy(linked_tables[game][probe][b], out, sizeof linked_tables[game][probe][b]);
    return true;
}
static bool get(const pin_value *row, const char *name, pin_value *out)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, out));
    return true;
}
static uint64_t fixture_variant_tag(
    const compiler_fixture *f,
    int b,
    const char *identity,
    const char *union_name,
    const char *variant
)
{
    const authoring_package *owner = compiler_fixture_package(f, b, identity);
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(f, b, identity, union_name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, variant)) {
            return type->variants[i].tag;
        }
    }
    return 0;
}
static uint64_t variant_tag(int b, const char *union_name, const char *variant)
{
    return fixture_variant_tag(&fixture, b, PACKAGE, union_name, variant);
}
static bool prepare_backends(unsigned backends)
{
    const source_package_input sources[] = { PACKAGE_COMPILER_SOURCE_INPUTS };
    native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                       COMPILER_SYNTAX_DEFINITION_PACK,
                                       CSTRING_PACK };
    REQUIRE(compiler_fixture_prepare_backends(
        &fixture,
        backends,
        sources,
        (int)(sizeof sources / sizeof *sources),
        packs,
        3
    ));
    return true;
}
static bool prepare()
{
    return prepare_backends(COMPILER_FIXTURE_BOTH);
}
/* The sample's files: package facts, then lib and core:strings when it
 * uses them. */
static int sample_inputs(const sample *s, source_package_input inputs[SAMPLE_INPUTS])
{
    int count = 0;
    inputs[count++] = (source_package_input){ "facts/Main.chibi",
                                              s->source,
                                              strlen(s->source) };
    if (s->more) {
        inputs[count++] = (source_package_input){ "facts/More.chibi",
                                                  s->more,
                                                  strlen(s->more) };
    }
    if (s->library) {
        inputs[count++] = (source_package_input){ "lib/Lib.chibi",
                                                  s->library,
                                                  strlen(s->library) };
    }
    if (s->core) {
        inputs[count++] = (source_package_input){
            "/sdk/core/strings/Strings.chibi",
            CORE_STRINGS_SOURCE,
            sizeof CORE_STRINGS_SOURCE - 1
        };
    }
    if (s->collection_source) {
        assert(count < SAMPLE_INPUTS);
        inputs[count++] = (source_package_input){
            "/sdk/core/host_only/HostOnly.chibi",
            s->collection_source,
            strlen(s->collection_source)
        };
    }
    if (s->collection_dependency) {
        assert(count < SAMPLE_INPUTS);
        inputs[count++] = (source_package_input){
            "/sdk/core/host_dependency/Dependency.chibi",
            s->collection_dependency,
            strlen(s->collection_dependency)
        };
    }
    if (s->math) {
        static char sources[3][32768];
        static const char *const names[] = { "Math.chibi", "Spring.chibi", "ease/Ease.chibi" };
        static const char *const paths[] = { "/sdk/core/math/Math.chibi",
                                             "/sdk/core/math/Spring.chibi",
                                             "/sdk/core/math/ease/Ease.chibi" };
        const char *root = getenv("CHIBI_CORE_MATH_ROOT");
        if (!root || !root[0]) {
            root = CHIBI_CORE_MATH_ROOT;
        }
        for (int i = 0; i < 3; i++) {
            char path[4096];
            int n = snprintf(path, sizeof path, "%s/%s", root, names[i]);
            assert(n > 0 && (size_t)n < sizeof path && count < SAMPLE_INPUTS);
            FILE *file = fopen(path, "rb");
            assert(file);
            size_t length = fread(sources[i], 1, sizeof sources[i] - 1, file);
            bool read = !ferror(file) && feof(file);
            fclose(file);
            assert(read);
            sources[i][length] = 0;
            inputs[count++] = (source_package_input){ paths[i], sources[i], length };
        }
    }
    return count;
}
static pin_value files(const source_package_input *inputs, int count, bool sources)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(sources ? inputs[i].text : inputs[i].path);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
/* This host's target, as Compile and CollectScalar take it. */
static bool target(int b, pin_value *out)
{
    const pin_record *record = compiler_fixture_record(&fixture, b, PACKAGE, "Target", &input);
    REQUIRE(record);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .ptr = arena_alloc(&input, record->type.size),
                        .record = record,
                        .type_name = record->type.name };
    memset(out->ptr, 0, record->type.size);
    REQUIRE(put(out, "pointer_storage", BF_POINTER_TYPE));
    REQUIRE(put(out, "pointer_size", (int)sizeof(void *)));
    REQUIRE(put(out, "value_size", (int)sizeof(pin_value)));
    REQUIRE(put(out, "payload_offset", (int)offsetof(pin_value, d)));
    REQUIRE(put(out, "arguments_offset", (int)offsetof(behavior_function_context, arguments)));
    REQUIRE(
        put(out, "delta_seconds_offset", (int)offsetof(behavior_function_context, delta_seconds))
    );
    REQUIRE(put(out, "results_offset", (int)offsetof(behavior_function_context, results)));
    REQUIRE(
        put(out, "error_status_offset", (int)offsetof(behavior_function_context, error_status))
    );
    REQUIRE(put(out, "type_offset", (int)offsetof(pin_value, type)));
    REQUIRE(put(out, "container_offset", (int)offsetof(pin_value, container)));
    REQUIRE(put(out, "name_offset", (int)offsetof(pin_value, type_name)));
    REQUIRE(put(out, "record_offset", (int)offsetof(pin_value, record)));
    REQUIRE(put(out, "schema_offset", (int)offsetof(pin_value, schema)));
    REQUIRE(put(out, "count_offset", (int)offsetof(pin_array, count)));
    REQUIRE(put(out, "items_offset", (int)offsetof(pin_array, items)));
    REQUIRE(put(out, "data_offset", (int)offsetof(pin_array, data)));
    REQUIRE(put(out, "stride_offset", (int)offsetof(pin_array, stride)));
    REQUIRE(put(out, "map_count_offset", (int)offsetof(pin_map, count)));
    REQUIRE(put(out, "data_failure", BEHAVIOR_FUNCTION_HOST_DATA));
    REQUIRE(put(out, "argument_failure", BEHAVIOR_FUNCTION_ARGUMENT));
    REQUIRE(put(out, "assert_failure", BEHAVIOR_FUNCTION_DATA));
    return true;
}
/* The names of this host's prepared instructions, in their order. */
static pin_value instruction_names()
{
    int instructions = execution_prepared_instruction_count();
    pin_array *names = pin_array_snapshot_allocate(&input, PIN_STR, NULL, instructions);
    for (int i = 0; i < instructions; i++) {
        names->items[i] = text(execution_prepared_instruction_name(i));
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = names };
}
enum
{
    SCALAR_ARGUMENTS = 8
};
/* CollectScalar's arguments for `source` at `path` on VM b: the project, the
 * path and source, this host's target and instruction names, and the
 * production natives it links. */
static bool scalar_arguments(int b, const char *path, pin_value source, pin_value args[SCALAR_ARGUMENTS])
{
    args[0] = text("/project");
    args[1] = text(path);
    args[2] = source;
    REQUIRE(target(b, &args[3]));
    args[4] = instruction_names();
    REQUIRE(natives(b, false, false, &args[5]));
    return true;
}
/* Compile's arguments for `inputs`: the project, the core collection, the
 * files, debugger points and watches, this host's target and the natives it
 * links: with `game`, also the core pack a game links. */
static bool arguments_for(
    int b,
    bool game,
    bool probe,
    const source_package_input *inputs,
    int count,
    pin_value args[ARGUMENTS]
)
{
    const char *collection_names[] = { "core" };
    const char *collection_paths[] = { "/sdk/core" };
    args[0] = text("/project");
    args[1] = strings(collection_names, 1);
    args[2] = strings(collection_paths, 1);
    args[3] = files(inputs, count, false);
    args[4] = files(inputs, count, true);
    args[DEBUG_ARGUMENT] = (pin_value){ .type = PIN_BOOL, .b = true };
    REQUIRE(target(b, &args[6]));
    args[7] = (pin_value){ .type = PIN_U64, .u64 = execution_prepared_abi() };
    args[8] = instruction_names();
    REQUIRE(natives(b, game, probe, &args[9]));
    return true;
}
/* Ordinary compilation and bootstrap use only production host natives. */
static bool arguments(int b, bool game, const source_package_input *inputs, int count, pin_value args[ARGUMENTS])
{
    return arguments_for(b, game, false, inputs, count, args);
}
typedef struct
{
    pin_value module, bytes, error;
} compiler_result;
static bool compiler_succeeded(const compiler_result *result)
{
    return !result->error.ptr;
}
static const pin_value *compiler_error(const compiler_result *result)
{
    const pin_union *error = result->error.ptr;
    assert(error && error->count == 1);
    return &error->values[0];
}
static bool compiler_call(
    int b,
    const char *entry,
    const pin_value *arguments,
    int count,
    uintptr_t budget,
    compiler_result *out
)
{
    pin_value values[3];
    REQUIRE(
        compiler_fixture_call_results(&fixture, b, PACKAGE, entry, arguments, count, budget, &detached, values, 3)
    );
    REQUIRE(values[0].type == PIN_U64);
    REQUIRE(values[1].type == PIN_U8 && values[1].container == PIN_CONTAINER_ARRAY);
    REQUIRE(values[2].type == PIN_UNION && values[2].container == PIN_CONTAINER_SINGLE);
    const pin_union *error = values[2].ptr;
    REQUIRE(!error || (error->count == 1 && error->values[0].type == PIN_STRUCT));
    if (error) {
        const pin_array *bytes = values[1].ptr;
        REQUIRE(values[0].u64 == 0);
        REQUIRE(!bytes || bytes->count == 0);
    }
    *out = (compiler_result){ .module = values[0],
                              .bytes = values[1],
                              .error = values[2] };
    return true;
}
static bool compile(
    int b,
    bool game,
    bool probe,
    bool effects,
    const source_package_input *inputs,
    int count,
    uintptr_t budget,
    compiler_result *out
)
{
    pin_value args[ARGUMENTS + 1];
    REQUIRE(arguments_for(b, game, probe, inputs, count, args));
    args[ARGUMENTS] = linked_effects[game][probe][b];
    REQUIRE(
        compiler_call(b, effects ? "CompileWithEffects" : "Compile", args, ARGUMENTS + effects, budget, out)
    );
    return true;
}
static uint64_t function_id(const char *name)
{
    char text[256];
    snprintf(text, sizeof text, "project:facts:1:%s:callable", name);
    return script_catalog_id("package-symbol", text);
}
/* Math packages link the ordinary native libm pack, outside the compiler
 * host's intentionally restricted JSON/text/array execution profile. */
static bool run_math(const unsigned char *bytes, size_t length, const sample_call *call, int backend)
{
    REQUIRE(!strcmp(call->arguments, "[]") && !strcmp(call->expected, "[true]"));
    native_prepared_context native = { .native = linked[1][0] };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&native);
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root;
    char error[256];
    behavior_backend mode = backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE;
    package_collection *collection = compiler_artifact_read(
        bytes,
        length,
        &linker,
        mode,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!collection) {
        fprintf(stderr, "math artifact: %s\n", error);
    }
    REQUIRE(collection);
    const behavior_function *body = NULL;
    for (int i = 0; i < package_collection_count(collection); i++) {
        execution_module *module = package_collection_module(collection, i);
        if (execution_module_id(module) != root) {
            continue;
        }
        for (int j = 0; j < execution_module_function_count(module); j++) {
            execution_module_function_info info = execution_module_function_at(module, j);
            if (info.id == function_id(call->function)) {
                body = info.body;
            }
        }
    }
    REQUIRE(body);
    arena memory = { 0 };
    behavior_managed_heap heap = { 0 };
    behavior_managed_heap_init(&heap);
    behavior_function_activation activation = { 0 };
    if (behavior_function_resumable(body)) {
        behavior_function_activation_init(body, &activation);
    }
    pin_value answer = { .type = PIN_BOOL };
    behavior_function_context context = {
        .results = &answer,
        .activation = behavior_function_resumable(body) ? &activation : NULL,
        .managed_heap = &heap,
        .budget = TARGET_BUDGET
    };
    behavior_function_bind_storage(&context, &memory);
    REQUIRE(behavior_function_bind(body, &context, NULL, 0));
    behavior_run_scope counts = { 0 };
    behavior_run_scope_begin(&counts);
    int status = behavior_function_run(body, &context, backend != 0);
    behavior_run_scope_end(&counts);
    bool passed = status == FLOW_OK && context.returned && answer.type == PIN_BOOL &&
                  answer.b && !counts.counts.inspection_bytecode_fallbacks &&
                  (!backend || ((TEST_COMPILED_BACKEND == BEHAVIOR_WASM
                                     ? counts.counts.wasm_entries > 0
                                     : counts.counts.sljit_entries > 0) &&
                                !counts.counts.bytecode_entries));
    if (!passed) {
        fprintf(
            stderr,
            "math artifact %s backend=%d status=%d: %s\n",
            call->function,
            backend,
            status,
            context.error_message ? context.error_message : "wrong result"
        );
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&heap);
    arena_free(&memory);
    package_collection_free(collection);
    REQUIRE(passed);
    return true;
}
static bool run(const unsigned char *bytes, size_t length, const sample_call *call, int backend)
{
    char error[256];
    host = compiler_host_load(
        bytes,
        length,
        backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    if (!host) {
        fprintf(stderr, "package compiler load: %s\n", error);
    }
    REQUIRE(host);
    bool valid = compiler_host_invoke(
        host,
        function_id(call->function),
        call->arguments,
        strlen(call->arguments),
        call->budget ? call->budget : TARGET_BUDGET,
        &result
    );
    bool passed = valid && result.flow_status == FLOW_OK;
    bool expected = call->expected ? passed && !strcmp(result.json, call->expected)
                                   : !passed;
    if (call->diagnostic) {
        expected = expected && strstr(result.diagnostic, call->diagnostic);
    }
    if (!expected) {
        fprintf(
            stderr,
            "package compiler run %s%s: %d %s %s\n",
            call->function,
            call->arguments,
            result.flow_status,
            valid && result.json ? result.json : "",
            result.diagnostic
        );
    }
    REQUIRE(expected);
    compiler_host_free(host);
    host = NULL;
    free(result.json);
    result = (compiler_host_result){ 0 };
    return true;
}
/* Whether a point is one this compiler places as stage zero does: a
 * function's entry and completions, each statement's, each evaluated
 * expression's and each symbol's it reads or stores. */
static bool placed(const behavior_source_point *point)
{
    return point->kind == BEHAVIOR_SOURCE_FUNCTION ||
           point->kind == BEHAVIOR_SOURCE_STATEMENT ||
           point->kind == BEHAVIOR_SOURCE_EXPRESSION ||
           point->kind == BEHAVIOR_SOURCE_SYMBOL;
}
static void print_points(const char *compiler, const behavior_function *f)
{
    for (int k = 0; k < acount(f->source_points); k++) {
        const behavior_source_point *p = &f->source_points[k];
        fprintf(
            stderr,
            "  %s point kind %d at %d..%d occurrence %016llx completion %d\n",
            compiler,
            p->kind,
            p->start,
            p->end,
            (unsigned long long)p->occurrence,
            p->completion
        );
    }
}
static bool same_point(const behavior_source_point *x, const behavior_source_point *y)
{
    return x->kind == y->kind && x->occurrence == y->occurrence &&
           x->start == y->start && x->end == y->end && x->line == y->line &&
           x->column == y->column && x->completion == y->completion &&
           (x->kind != BEHAVIOR_SOURCE_FUNCTION || x->meaning == y->meaning);
}
/* Each function of stage one's module has stage zero's function and
 * statement points, as many of each, in any order. */
static bool same_points(const execution_module *expected, const execution_module *actual)
{
    if (expected->plan.function_count != actual->plan.function_count) {
        fprintf(
            stderr,
            "function count module %016llx: stage zero=%d stage one=%d\n",
            (unsigned long long)execution_module_id(actual),
            expected->plan.function_count,
            actual->plan.function_count
        );
        for (int i = 0; i < expected->plan.function_count; i++) {
            fprintf(
                stderr,
                "  stage zero function %016llx\n",
                (unsigned long long)expected->plan.functions[i].id
            );
        }
        for (int i = 0; i < actual->plan.function_count; i++) {
            fprintf(
                stderr,
                "  stage one function %016llx\n",
                (unsigned long long)actual->plan.functions[i].id
            );
        }
    }
    REQUIRE(expected->plan.function_count == actual->plan.function_count);
    bool all_alike = true;
    for (int i = 0; i < expected->plan.function_count; i++) {
        const behavior_function *x = expected->plan.functions[i].body, *y = NULL;
        for (int j = 0; !y && j < actual->plan.function_count; j++) {
            if (actual->plan.functions[j].id == expected->plan.functions[i].id) {
                y = actual->plan.functions[j].body;
            }
        }
        if (!y) {
            fprintf(
                stderr,
                "stage one lacks function %016llx\n",
                (unsigned long long)expected->plan.functions[i].id
            );
        }
        REQUIRE(y);
        int count = acount(y->source_points);
        bool *used = xcalloc((size_t)count + 1, sizeof *used);
        bool alike = true;
        int placed_count = 0;
        for (int j = 0; alike && j < acount(x->source_points); j++) {
            if (!placed(&x->source_points[j])) {
                continue;
            }
            placed_count++;
            int found = -1;
            for (int k = 0; found < 0 && k < count; k++) {
                found = !used[k] && same_point(&x->source_points[j], &y->source_points[k])
                          ? k
                          : -1;
            }
            alike = found >= 0;
            if (alike) {
                used[found] = true;
            } else {
                fprintf(
                    stderr,
                    "function %d lacks stage zero's point kind %d at %d..%d\n",
                    i,
                    x->source_points[j].kind,
                    x->source_points[j].start,
                    x->source_points[j].end
                );
            }
        }
        for (int k = 0; alike && k < count; k++) {
            alike = !placed(&y->source_points[k]) || used[k];
            placed_count -= placed(&y->source_points[k]);
            if (!alike) {
                fprintf(
                    stderr,
                    "function %d has an extra point at %d..%d\n",
                    i,
                    y->source_points[k].start,
                    y->source_points[k].end
                );
            }
        }
        free(used);
        if (!alike || placed_count != 0) {
            print_points("stage zero", x);
            print_points("stage one", y);
            all_alike = false;
        }
    }
    return all_alike;
}
static bool same_watch(const behavior_source_watch *x, const behavior_source_watch *y)
{
    const char *a = x->initial.type_name ? x->initial.type_name : "",
               *b = y->initial.type_name ? y->initial.type_name : "";
    return x->origin.kind == y->origin.kind &&
           x->origin.occurrence == y->origin.occurrence &&
           x->origin.start == y->origin.start && x->origin.end == y->origin.end &&
           x->origin.line == y->origin.line && x->origin.column == y->origin.column &&
           x->initial.type == y->initial.type &&
           x->initial.container == y->initial.container && !strcmp(a, b) &&
           x->ordinal == y->ordinal && x->output == y->output &&
           x->reference == y->reference;
}
static void print_watches(const char *compiler, const behavior_function *f)
{
    for (int k = 0; k < acount(f->source_watches); k++) {
        const behavior_source_watch *w = &f->source_watches[k];
        fprintf(
            stderr,
            "  %s watch at %d..%d occurrence %016llx type %d container %d name "
            "%s ordinal %d output %d "
            "reference %d\n",
            compiler,
            w->origin.start,
            w->origin.end,
            (unsigned long long)w->origin.occurrence,
            w->initial.type,
            w->initial.container,
            w->initial.type_name ? w->initial.type_name : "-",
            w->ordinal,
            w->output,
            w->reference
        );
    }
}
/* Each function of stage one's module has stage zero's watches, as many of
 * each, in any order. */
static bool same_watches(const execution_module *expected, const execution_module *actual)
{
    REQUIRE(expected->plan.function_count == actual->plan.function_count);
    bool all_alike = true;
    for (int i = 0; i < expected->plan.function_count; i++) {
        const behavior_function *x = expected->plan.functions[i].body, *y = NULL;
        for (int j = 0; !y && j < actual->plan.function_count; j++) {
            if (actual->plan.functions[j].id == expected->plan.functions[i].id) {
                y = actual->plan.functions[j].body;
            }
        }
        if (!y) {
            fprintf(
                stderr,
                "stage one lacks function %016llx\n",
                (unsigned long long)expected->plan.functions[i].id
            );
        }
        REQUIRE(y);
        int count = acount(y->source_watches);
        bool *used = xcalloc((size_t)count + 1, sizeof *used);
        bool alike = count == acount(x->source_watches);
        for (int j = 0; alike && j < acount(x->source_watches); j++) {
            int found = -1;
            for (int k = 0; found < 0 && k < count; k++) {
                found = !used[k] && same_watch(&x->source_watches[j], &y->source_watches[k])
                          ? k
                          : -1;
            }
            alike = found >= 0;
            if (alike) {
                used[found] = true;
            }
        }
        free(used);
        if (!alike) {
            fprintf(stderr, "WATCHES %d\n", i);
            print_watches("stage zero", x);
            print_watches("stage one", y);
            all_alike = false;
        }
    }
    return all_alike;
}
/* Stage zero's closure of the same packages, whose calls must give the same
 * results. */
static bool oracle_closure(const sample *s, unsigned char **closure, size_t *length)
{
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(s, inputs);
    compiler_fixture_free(&oracle);
    /* The natives the stage-one target links. */
    native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                       COMPILER_SYNTAX_DEFINITION_PACK,
                                       CSTRING_PACK,
                                       CORE_DEFINITION_PACK,
                                       NATIVE_MATH_DEFINITION_PACK,
                                       NATIVE_VECTOR_DEFINITION_PACK };
    /* Only this bytecode owner is serialized and compared below. Each
     * caller still chooses its original target execution backends. */
    REQUIRE(compiler_fixture_prepare_backends(
        &oracle,
        COMPILER_FIXTURE_BYTECODE,
        inputs,
        count,
        packs,
        s->math   ? 5
        : s->game ? 4
                  : 3
    ));
    const authoring_packages *owner = oracle.owners[0];
    package_module rows[3];
    REQUIRE(authoring_packages_count(owner) <= 3);
    int written = 0;
    char error[256];
    bool valid = true;
    for (int p = 0; valid && p < authoring_packages_count(owner); p++) {
        const authoring_package *package = authoring_packages_at(owner, p);
        unsigned char *prepared = NULL;
        size_t size = 0;
        valid = package->module &&
                execution_prepared_write(package->module, &prepared, &size, error, sizeof error);
        rows[written++] = (package_module){ package->identity,
                                            valid ? execution_module_id(package->module)
                                                  : 0,
                                            prepared,
                                            size };
    }
    const authoring_package *root = compiler_fixture_package(&oracle, 0, "project:facts");
    valid = valid && root &&
            compiler_artifact_write(
                rows,
                written,
                execution_module_id(root->module),
                closure,
                length,
                error,
                sizeof error
            );
    for (int i = 0; i < written; i++) {
        free((void *)rows[i].data);
    }
    REQUIRE(valid);
    return true;
}
/* Own the compiler's detached byte output after its result arena is retired. */
static unsigned char *closure_bytes(const compiler_result *result, size_t *length)
{
    const pin_array *array = result->bytes.ptr;
    *length = (size_t)array->count;
    unsigned char *bytes = xmalloc(*length);
    for (size_t i = 0; i < *length; i++) {
        bytes[i] = array->items[i].u8;
    }
    return bytes;
}

bool package_compiler_closure(
    int compiler_backend,
    const source_package_input *sources,
    int count,
    uintptr_t budget,
    unsigned char **bytes,
    size_t *length
)
{
    assert(compiler_backend == 0 || compiler_backend == 1);
    *bytes = NULL;
    *length = 0;
    compiler_result output;
    bool valid = prepare_backends(1u << compiler_backend) &&
                 compile(compiler_backend, true, false, false, sources, count, budget, &output);
    if (valid && !compiler_succeeded(&output)) {
        pin_value message, line, column;
        if (get(compiler_error(&output), "message", &message) &&
            get(compiler_error(&output), "line", &line) &&
            get(compiler_error(&output), "column", &column)) {
            fprintf(
                pu_errors(),
                "Authored closure compilation: %s at %d:%d\n",
                pin_text(message).bytes,
                line.i,
                column.i
            );
        }
        valid = false;
    }
    if (valid) {
        *bytes = closure_bytes(&output, length);
    }
    cleanup();
    return valid;
}

/* Compile the discovered test package with each compiler backend, then run
 * the same fixed assertions on each independently loaded target backend. */
static bool authored_tests(
    const compiler_fixture *metadata,
    const source_package_input *sources,
    int count,
    uintptr_t budget
)
{
    bool all_passed = true;
    for (int compiler = 0; compiler < 2; compiler++) {
        REQUIRE(prepare_backends(1u << compiler));
        compiler_result output;
        REQUIRE(compile(compiler, false, false, true, sources, count, COMPILE_BUDGET, &output));
        if (!compiler_succeeded(&output)) {
            pin_value message, line, column;
            REQUIRE(get(compiler_error(&output), "message", &message));
            REQUIRE(get(compiler_error(&output), "line", &line));
            REQUIRE(get(compiler_error(&output), "column", &column));
            fprintf(
                pu_errors(),
                "Authored test compilation: %s at %d:%d\n",
                pin_text(message).bytes,
                line.i,
                column.i
            );
            return false;
        }
        size_t length;
        unsigned char *bytes = closure_bytes(&output, &length);
        bool passed = true;
        for (int backend = 0; backend < 2; backend++) {
            char error[256];
            host = compiler_host_load(
                bytes,
                length,
                backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
                error,
                sizeof error
            );
            if (!host) {
                fprintf(pu_errors(), "Authored test closure: %s\n", error);
                passed = false;
                break;
            }
            fprintf(
                pu_output(),
                "Stage-one compiler %s\n",
                compiler ? behavior_backend_name(TEST_COMPILED_BACKEND) : "BYTECODE"
            );
            passed &= compiler_fixture_module_tests(metadata, backend, compiler_host_root(host), budget);
            compiler_host_free(host);
            host = NULL;
        }
        free(bytes);
        all_passed &= passed;
        cleanup();
    }
    return all_passed;
}
bool package_compiler_authored_tests(
    const compiler_fixture *metadata,
    const source_package_input *sources,
    int count,
    uintptr_t budget
)
{
    bool passed = authored_tests(metadata, sources, count, budget);
    cleanup();
    return passed;
}
/* The values `Build(count)` copies in `host`. */
static uint64_t copies(int count)
{
    char arguments[32];
    snprintf(arguments, sizeof arguments, "[%d]", count);
    pin_copy_scope scope = { 0 };
    pin_copy_scope_begin(&scope);
    bool called = compiler_host_invoke(
        host,
        function_id("Build"),
        arguments,
        strlen(arguments),
        TARGET_BUDGET,
        &result
    );
    pin_copy_scope_end(&scope);
    free(result.json);
    result = (compiler_host_result){ 0 };
    return called ? scope.counts.value_header_moves : UINT64_MAX;
}
static bool updates_in_place(const unsigned char *bytes, size_t length, const char *name)
{
    char error[256];
    host = compiler_host_load(bytes, length, TEST_COMPILED_BACKEND, error, sizeof error);
    if (!host) {
        fprintf(stderr, "package compiler %s load: %s\n", name, error);
    }
    REQUIRE(host);
    uint64_t fewer = copies(100), more = copies(200);
    if (!fewer || fewer == UINT64_MAX || more > 2 * fewer) {
        fprintf(
            stderr,
            "package compiler %s copies %llu then %llu\n",
            name,
            (unsigned long long)fewer,
            (unsigned long long)more
        );
    }
    REQUIRE(fewer && fewer != UINT64_MAX && more <= 2 * fewer);
    compiler_host_free(host);
    host = NULL;
    return true;
}
/* An intrinsic's capability ID is "CHII" and its number. */
static const uint64_t SEQUENCE_GET_CAPABILITY = UINT64_C(0x4348494900000000) |
                                                EXECUTION_INTRINSIC_SEQUENCE_GET;
/* How many of `f`'s instructions load an array's stride. */
static int stride_loads(const behavior_function *f)
{
    int loads = 0;
    for (int i = 0; i < acount(f->instructions); i++) {
        loads += f->instructions[i].op == BF_LOAD &&
                 f->instructions[i].offset == offsetof(pin_array, stride);
    }
    return loads;
}
static bool reads_elements(const behavior_function *f)
{
    for (int i = 0; i < acount(f->host_calls); i++) {
        if (f->host_calls[i].capability_id == SEQUENCE_GET_CAPABILITY) {
            return true;
        }
    }
    return false;
}
/* An array of strings, unions or records with a string field is never packed,
 * so stage one reads its elements from `items` alone, or through the sequence
 * intrinsic; any other array may be, so its reads in place still choose the
 * representation by its stride. In the root module, only the Packed functions
 * load a stride. */
static bool reads_boxed(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler boxed load: %s\n", error);
    }
    REQUIRE(packages);
    int boxed = 0, packed = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool box = !strncmp(f.entry, "Boxed", 5),
                 pack = !strncmp(f.entry, "Packed", 6);
            int loads = stride_loads(f.body);
            boxed += box;
            packed += pack;
            if ((box && loads) || (pack && !loads)) {
                fprintf(stderr, "package compiler %s loads a stride %d times\n", f.entry, loads);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(boxed == 4 && packed == 1 && !wrong);
    return true;
}
static const uint64_t SNAPSHOT_CAPABILITY = UINT64_C(0x4348494900000000) |
                                            EXECUTION_INTRINSIC_SNAPSHOT;
static int snapshots(const behavior_function *f)
{
    int count = 0;
    for (int i = 0; i < acount(f->host_calls); i++) {
        count += f->host_calls[i].capability_id == SNAPSHOT_CAPABILITY;
    }
    return count;
}
/* A literal or an authored function's result is new in the function's
 * storage, so stage one moves it into a local or a return without a snapshot;
 * an element read from an array is still copied, by the sequence intrinsic
 * that reads it. */
static bool moves_fresh_values(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler moves load: %s\n", error);
    }
    REQUIRE(packages);
    int moved = 0, copied = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool move = !strncmp(f.entry, "Moved", 5),
                 copy = !strcmp(f.entry, "Copied");
            int count = snapshots(f.body);
            moved += move;
            copied += copy;
            if ((move && count) || (copy && !count && !reads_elements(f.body))) {
                fprintf(stderr, "package compiler %s snapshots %d values\n", f.entry, count);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(moved == 4 && copied == 1 && !wrong);
    return true;
}
static int union_calls(const behavior_function *f, const node_host_call *call)
{
    int count = 0;
    for (int i = 0; i < acount(f->host_calls); i++) {
        count += f->host_calls[i].capability_id == execution_intrinsic_id(call);
    }
    return count;
}
/* Variants built in the caller's storage, keeping the locals built there. */
static int kept_unions(const behavior_function *f)
{
    int count = 0;
    for (int i = 0; i < acount(f->host_calls); i++) {
        count += f->host_calls[i].capability_id ==
                 (execution_intrinsic_id(&EXECUTION_union_keep_call) |
                  EXECUTION_INTRINSIC_IN_RESULT);
    }
    return count;
}
/* A returned variant keeps its payload values, which the return copies to the
 * caller; one kept in a local copies them, since the local's array changes. */
static bool moves_returned_variants(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler variants load: %s\n", error);
    }
    REQUIRE(packages);
    int kept = 0, copied = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool keep = !strncmp(f.entry, "Kept", 4),
                 copy = !strncmp(f.entry, "Copied", 6);
            int moves = kept_unions(f.body),
                makes = union_calls(f.body, &EXECUTION_union_make_call);
            kept += keep;
            copied += copy;
            if ((keep && (moves != 1 || makes || snapshots(f.body))) ||
                (copy && (moves || makes != 1))) {
                fprintf(stderr, "package compiler %s moves %d and makes %d variants\n", f.entry, moves, makes);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(kept == 3 && copied == 1 && !wrong);
    return true;
}
/* A function builds the variant literal it returns in its caller's storage;
 * one that returns a local it assigns does not. Arrays live on the managed
 * heap, so a returned one is never copied, and a function that changes its
 * arrays through their places, as Counted and Paired do, builds nothing
 * there. */
static bool builds_results(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler built load: %s\n", error);
    }
    REQUIRE(packages);
    int built = 0, copied = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool build = !strncmp(f.entry, "Built", 5),
                 copy = !strncmp(f.entry, "Copied", 6);
            built += build;
            copied += copy;
            if ((build && !f.body->builds_results) || (copy && f.body->builds_results)) {
                fprintf(
                    stderr,
                    "package compiler %s builds results: %d\n",
                    f.entry,
                    f.body->builds_results
                );
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(built == 2 && copied == 1 && !wrong);
    return true;
}
/* Whether f's first push or extension drops its item at once: the next host
 * call drops a value. */
static bool drops_pushed_item(const behavior_function *f)
{
    for (int i = 0; i + 1 < acount(f->host_calls); i++) {
        execution_intrinsic kind = execution_intrinsic_kind(f->host_calls[i].capability_id);
        if (kind == EXECUTION_INTRINSIC_ARRAY_PUSH ||
            kind == EXECUTION_INTRINSIC_ARRAY_EXTEND) {
            return f->host_calls[i + 1].capability_id ==
                   (UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_VALUE_DROP);
        }
    }
    return false;
}
static int intrinsic_calls(const behavior_function *f, execution_intrinsic kind)
{
    int count = 0;
    for (int i = 0; i < acount(f->host_calls); i++) {
        count += f->host_calls[i].capability_id ==
                 (UINT64_C(0x4348494900000000) | kind);
    }
    return count;
}
/* Arrays are values a loop retains, so neither a loop over an array nothing
 * changes nor one whose body changes its array snapshots it; CopiedWalk's
 * result shows the walk still sees the array as it was. A local nothing
 * changes reads its element in place without a host call, as stage zero lends
 * it, unless its array changes. WalkedSum walks a parameter and a local
 * nothing changes as they are, without copying either. */
static bool reads_in_place(const unsigned char *bytes, size_t length)
{
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256];
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler aliases load: %s\n", error);
    }
    REQUIRE(packages);
    int walks = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; execution_module_id(module) == root &&
                        i < execution_module_function_count(module);
             i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            int count = snapshots(f.body);
            walks += !strcmp(f.entry, "AliasedSum") || !strcmp(f.entry, "CopiedWalk");
            if (count) {
                fprintf(stderr, "package compiler %s snapshots %d values\n", f.entry, count);
                wrong++;
            }
            int gets = intrinsic_calls(f.body, EXECUTION_INTRINSIC_SEQUENCE_GET),
                drops = intrinsic_calls(f.body, EXECUTION_INTRINSIC_VALUE_DROP),
                borrows = intrinsic_calls(f.body, EXECUTION_INTRINSIC_SEQUENCE_BORROW);
            bool lent = !strcmp(f.entry, "LentFind"),
                 copied = !strcmp(f.entry, "CopiedFind"),
                 walked = !strcmp(f.entry, "WalkedSum");
            int copies = intrinsic_calls(f.body, EXECUTION_INTRINSIC_VALUE_COPY);
            walks += lent || copied || walked;
            if ((lent && (gets || drops || borrows)) ||
                (copied && (!gets || borrows)) || (walked && copies)) {
                fprintf(stderr, "package compiler %s reads %d, borrows %d, drops %d elements\n", f.entry, gets, borrows, drops);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    REQUIRE(walks == 5 && !wrong);
    return true;
}
/* Literals whose binary64 and binary32 readings LiteralBits must agree on:
 * exact and halfway values, the ends of both ranges, long and hexadecimal
 * spellings. */
static const char *const literal_spellings[] = {
    "0",
    "0.0",
    "000",
    "1",
    "007.5",
    "0.5",
    "0.1",
    "0.2",
    "0.3",
    "0.001",
    "384.0",
    "1e10",
    "1e22",
    "1e23",
    "3.14159265358979323846264338327950288",
    "123456789012345678901234567890",
    "9007199254740992",
    "9007199254740993",
    "9007199254740995",
    "18446744073709551615",
    "1.00000000000000011102230246251565404236316680908203125",
    "1.00000000000000011102230246251565404236316680908203124",
    "1.00000000000000011102230246251565404236316680908203126",
    "1.000000059604644775390625",
    "1.000000059604644775390624",
    "1.7976931348623157e308",
    "1.7976931348623158e308",
    "1.7976931348623159e308",
    "1e308",
    "1e309",
    "2.2250738585072014e-308",
    "2.2250738585072011e-308",
    "2.2250738585072012e-308",
    "2.2250738585072013e-308",
    "1.000000178813934326171875",
    "4.9e-324",
    "1e-400",
    "3.4028234663852886e38",
    "3.4028235e38",
    "3.4028235677973366e38",
    "3.4028236e38",
    "1.17549435e-38",
    "1.4e-45",
    "1e-46",
    "1e-30",
    "0x0",
    "0x1F",
    "0x10000000000001",
    "0x1FFFFFFFFFFFFF",
    "0x20000000000001",
    "0x8000000000000000",
    "0xFFFFFFFFFFFFFFFF",
    "1e0",
    "1E+2",
    "25e-1",
    "0.000000000000000000001",
    "100000000000000000000000000000000000000000000000000",
    "1.5e300",
    "6.02214076e23",
};
/* Whether a literal's significant digits are nonzero. */
static bool digits_nonzero(const char *text)
{
    text += *text == '-';
    bool hexadecimal = text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    for (const char *at = hexadecimal ? text + 2 : text; *at; at++) {
        if (!hexadecimal && (*at == 'e' || *at == 'E')) {
            return false;
        }
        if (*at != '0' && *at != '.') {
            return true;
        }
    }
    return false;
}
/* The reading LiteralBits implements, which is stage zero's where long double
 * is double: the binary64 nearest the literal and the binary32 nearest that;
 * a nonzero literal whose binary64 is infinite, zero or subnormal is out of
 * range. */
static void literal_reading(const char *text, bool *finite, uint64_t *wide, bool *fits, uint64_t *narrow)
{
    char *end;
    double value = strtod(text, &end);
    *finite = !*end && isfinite(value) && !(value != 0 && fabs(value) < DBL_MIN) &&
              !(value == 0 && digits_nonzero(text));
    memcpy(wide, &value, sizeof *wide);
    *fits = fabs(value) <= FLT_MAX;
    float single = (float)value;
    uint32_t bits;
    memcpy(&bits, &single, sizeof bits);
    *narrow = bits;
}
static bool literal_agrees(int b, const char *spelling, bool negative)
{
    char spelling_text[2100];
    snprintf(spelling_text, sizeof spelling_text, "%s%s", negative ? "-" : "", spelling);
    bool finite, fits;
    uint64_t wide, narrow;
    literal_reading(spelling_text, &finite, &wide, &fits, &narrow);
    pin_value args[] = { text(spelling),
                         (pin_value){ .type = PIN_BOOL, .b = negative } };
    pin_value out;
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "LiteralBits", args, 2, COMPILE_BUDGET, &detached, &out)
    );
    const pin_union *value = out.ptr;
    bool read = value->tag == variant_tag(b, "FloatBits", "Finite");
    bool same = read == finite;
    if (same && read) {
        same = value->values[0].u64 == wide && value->values[1].b == fits &&
               (!fits || value->values[2].u64 == narrow);
    }
    if (!same) {
        fprintf(
            stderr,
            "package compiler literal %s: expected %d %016llx %d %08llx, read "
            "%d %016llx %d %08llx\n",
            spelling_text,
            finite,
            (unsigned long long)wide,
            fits,
            (unsigned long long)narrow,
            read,
            read ? (unsigned long long)value->values[0].u64 : 0ull,
            read && value->values[1].b,
            read ? (unsigned long long)value->values[2].u64 : 0ull
        );
    }
    arena_free(&detached);
    return same;
}
/* Spellings longer than the listed ones: the exact decimals of binary64
 * values that are binary32 ties, and 800 digits followed by one that decides
 * the rounding, or by an exponent that moves the point back. */
static int long_spellings(char spellings[][2048], int capacity)
{
    static const double ties[] = { 0x3p-150, 0x5p-150, 0x1.000001p0, 0x1.000003p0, 0x1.fffffe8p127 };
    int count = 0;
    for (int i = 0; i < (int)(sizeof ties / sizeof *ties) && count < capacity; i++) {
        snprintf(spellings[count++], sizeof spellings[0], "%.200e", ties[i]);
    }
    char *sticky = spellings[count++];
    int length = snprintf(sticky, sizeof spellings[0], "%s", "1.00000000000000011102230246251565404236316680908203125");
    for (int i = 0; i < 800; i++) {
        sticky[length++] = '0';
    }
    sticky[length++] = '1';
    sticky[length] = 0;
    char *shifted = spellings[count++];
    shifted[0] = '1';
    for (int i = 1; i <= 800; i++) {
        shifted[i] = '0';
    }
    snprintf(shifted + 801, sizeof spellings[0] - 801, "%s", "e-800");
    return count;
}
/* LiteralBits reads number literals of floating types by that reading:
 * the listed spellings on both compiler backends, with both signs on the
 * compiled one, and there also the long spellings and generated ones across
 * the binary64 range. */
static bool literals_agree()
{
    static char spellings[8][2048];
    int long_count = long_spellings(spellings, 8);
    for (int i = 0; i < (int)(sizeof literal_spellings / sizeof *literal_spellings);
         i++) {
        REQUIRE(literal_agrees(0, literal_spellings[i], false));
        REQUIRE(literal_agrees(1, literal_spellings[i], false));
        REQUIRE(literal_agrees(1, literal_spellings[i], true));
    }
    for (int i = 0; i < long_count; i++) {
        REQUIRE(literal_agrees(1, spellings[i], false));
    }
    uint32_t state = 12345;
    for (int i = 0; i < 120; i++) {
        char spelling[96];
        int length = 0;
        state = state * 1103515245u + 12345u;
        int digits = 1 + (int)(state >> 16) % 19;
        for (int d = 0; d < digits; d++) {
            state = state * 1103515245u + 12345u;
            spelling[length++] = (char)('0' + (state >> 16) % 10);
            if (d == digits / 2 && d + 1 < digits && (state & 1)) {
                spelling[length++] = '.';
            }
        }
        state = state * 1103515245u + 12345u;
        int exponent = (int)((state >> 8) % 640) - 330;
        length += snprintf(spelling + length, sizeof spelling - (size_t)length, "e%d", exponent);
        REQUIRE(literal_agrees(1, spelling, false));
    }
    return true;
}
/* The packages of a closure, linked against the compiler host's natives,
 * which `native` holds; NULL when it does not load. */
static package_collection *read_packages(
    const unsigned char *bytes,
    size_t length,
    native_capabilities **native,
    behavior_backend backend
)
{
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK,
                                             CSTRING_PACK };
    char error[256];
    *native = native_capabilities_from_definition_packs(NULL, 0, packs, 5, error, sizeof error);
    assert(*native);
    native_prepared_context context = { .native = *native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    linker.decoded_bytes = COMPILER_HOST_MAX_CLOSURE_BYTES;
    linker.decoded_records = COMPILER_HOST_MAX_CLOSURE_RECORDS;
    linker.layout_work = COMPILER_HOST_MAX_LAYOUT_WORK;
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        &linker,
        backend,
        &remaining,
        &root,
        error,
        sizeof error
    );
    if (!packages) {
        fprintf(stderr, "package compiler load: %s\n", error);
    }
    return packages;
}
/* Repeated equivalent bindings share one emitted specialization, while
 * distinct bindings have different IDs. Source points separately compare
 * those IDs with stage zero's independently prepared modules. */
static bool reuses_specializations(const unsigned char *bytes, size_t length, int expected)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    uint64_t *ids = NULL;
    bool distinct = true;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info function = execution_module_function_at(module, i);
            if (strncmp(function.entry, "__generic_", 10)) {
                continue;
            }
            for (int j = 0; j < acount(ids); j++) {
                distinct = distinct && ids[j] != function.id;
            }
            apush(ids, function.id);
        }
    }
    int count = acount(ids);
    afree(ids);
    package_collection_free(packages);
    native_capabilities_free(native);
    if (!distinct || count != expected) {
        fprintf(stderr, "package compiler specialization reuse: %d distinct=%d, expected %d\n", count, distinct, expected);
    }
    REQUIRE(distinct && count == expected);
    return true;
}
/* A native never changes a value it is passed, so a call lends it a local,
 * and the result it returns is a temporary a local takes over. An element it
 * is passed is copied. */
static bool lends_native_values(const unsigned char *bytes, size_t length)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    int lent = 0, copied = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool lend = !strncmp(f.entry, "Lent", 4),
                 copy = !strncmp(f.entry, "Copied", 6);
            int copies = intrinsic_calls(f.body, EXECUTION_INTRINSIC_VALUE_COPY);
            lent += lend;
            copied += copy;
            if ((lend && copies) || (copy && !copies)) {
                fprintf(stderr, "package compiler %s copies %d values\n", f.entry, copies);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    REQUIRE(lent == 3 && copied == 1 && !wrong);
    return true;
}
/* A drop releases only collections, so a temporary that holds none is not
 * dropped. */
static bool drops_only_collections(const unsigned char *bytes, size_t length)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    int plain = 0, held = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            bool keep = !strncmp(f.entry, "Plain", 5),
                 drop = !strncmp(f.entry, "Held", 4);
            int drops = intrinsic_calls(f.body, EXECUTION_INTRINSIC_VALUE_DROP);
            plain += keep;
            held += drop;
            if ((keep && drops) || (drop && !drops)) {
                fprintf(stderr, "package compiler %s drops %d values\n", f.entry, drops);
                wrong++;
            }
        }
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    REQUIRE(plain == 3 && held == 4 && !wrong);
    return true;
}
/* Runs a call with the probe natives: the compiler host links only its own,
 * so the closure is read with them and its function run directly. */
static bool run_probe(const unsigned char *bytes, size_t length, const sample_call *call, int backend)
{
    REQUIRE(!strcmp(call->arguments, "[]") && call->expected);
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    const behavior_function *body = NULL;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int f = 0; f < execution_module_function_count(module); f++) {
            execution_module_function_info info = execution_module_function_at(module, f);
            if (info.id == function_id(call->function)) {
                body = info.body;
            }
        }
    }
    REQUIRE(body);
    arena storage = { 0 };
    pin_value result = { .type = PIN_I32 };
    behavior_function_activation activation = { 0 };
    bool resumable = behavior_function_resumable(body);
    if (resumable) {
        behavior_function_activation_init(body, &activation);
    }
    behavior_function_prepare_results(body, &result, &storage);
    behavior_managed_heap heap;
    behavior_managed_heap_init(&heap);
    behavior_function_context context = { .activation = resumable ? &activation : NULL,
                                          .managed_heap = &heap,
                                          .results = &result,
                                          .budget = TARGET_BUDGET };
    behavior_function_bind_storage(&context, &storage);
    bool bound = behavior_function_bind(body, &context, NULL, 0);
    int status = bound ? behavior_function_run(body, &context, backend != 0)
                       : FLOW_ERR_ARGUMENT;
    char text[32];
    snprintf(text, sizeof text, "[%d]", result.i);
    bool expected = status == FLOW_OK && context.returned &&
                    !strcmp(text, call->expected);
    if (!expected) {
        fprintf(stderr, "package compiler probe run %s: %d %s\n", call->function, status, text);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&heap);
    arena_free(&storage);
    package_collection_free(packages);
    native_capabilities_free(native);
    REQUIRE(expected);
    return true;
}
static bool call_sample(
    const sample *s,
    const unsigned char *bytes,
    size_t length,
    const sample_call *call,
    int backend
)
{
    if (s->math) {
        return run_math(bytes, length, call, backend);
    }
    return s->probe_calls ? run_probe(bytes, length, call, backend)
                          : run(bytes, length, call, backend);
}
/* The receiver input must be captured before the authored item call. Comparing
 * watch sets alone cannot detect a watch moved across that call. Each fixture
 * function has exactly one update and one authored invocation. */
static bool array_update_order(const unsigned char *bytes, size_t length, const sample *s)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    for (int c = 0; c < CALLS && s->calls[c].function; c++) {
        const behavior_function *body = NULL;
        for (int p = 0; p < package_collection_count(packages); p++) {
            const execution_module *module = package_collection_module(packages, p);
            for (int f = 0; f < module->plan.function_count; f++) {
                if (module->plan.functions[f].id == function_id(s->calls[c].function)) {
                    body = module->plan.functions[f].body;
                }
            }
        }
        REQUIRE(body);
        int receiver = -1, item = -1, invocation = -1;
        for (int pc = 0; pc < acount(body->instructions); pc++) {
            const behavior_function_instruction *op = &body->instructions[pc];
            if (op->op == BF_INVOKE) {
                REQUIRE(invocation < 0);
                invocation = pc;
            }
            if (op->op != BF_SOURCE_WATCH) {
                continue;
            }
            const behavior_source_watch *watch = &body->source_watches[op->target];
            if (watch->output) {
                continue;
            }
            if (watch->ordinal == 0 && watch->initial.container == PIN_CONTAINER_ARRAY) {
                REQUIRE(receiver < 0);
                receiver = pc;
            }
            if (watch->ordinal == 1) {
                REQUIRE(item < 0);
                item = pc;
            }
        }
        if (!(receiver >= 0 && receiver < invocation && invocation < item)) {
            fprintf(
                stderr,
                "%s debug order: receiver %d, invocation %d, item %d\n",
                s->calls[c].function,
                receiver,
                invocation,
                item
            );
        }
        REQUIRE(receiver >= 0 && receiver < invocation && invocation < item);
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    return true;
}
typedef struct
{
    const behavior_function *item;
    int start, end, step;
    bool valid;
} array_update_trace;
static int array_update_pause(void *user, behavior_function_context *context, const behavior_source_point *point)
{
    array_update_trace *trace = user;
    if (context->source_function == trace->item &&
        point->kind == BEHAVIOR_SOURCE_FUNCTION && !point->completion) {
        trace->valid &= trace->step == 1;
        trace->step++;
    }
    return 0;
}
static void array_update_capture(
    void *user,
    behavior_function_context *,
    const behavior_source_watch *watch,
    const pin_value *value
)
{
    array_update_trace *trace = user;
    if (watch->origin.start != trace->start || watch->origin.end != trace->end) {
        return;
    }
    if (!value) {
        trace->valid = false;
        return;
    }
    if (!watch->output && watch->ordinal == 1) {
        trace->valid &= trace->step == 2 && value->type == PIN_I32 &&
                        value->container == PIN_CONTAINER_SINGLE && value->i == 7;
        trace->step++;
        return;
    }
    const pin_array *array = value->ptr;
    bool valid = value->type == PIN_I32 && value->container == PIN_CONTAINER_ARRAY &&
                 array && array->count == (watch->output ? 2 : 1);
    if (valid) {
        valid = pin_array_get(array, 0).i == 1 &&
                (!watch->output || pin_array_get(array, 1).i == 7);
    }
    trace->valid &= valid && watch->ordinal == 0 &&
                    trace->step == (watch->output ? 3 : 0);
    trace->step++;
}
/* Observe actual delivery as well as emitted order. Payload pointers belong
 * to the invocation; the callbacks retain only their checked literal values. */
static bool observes_array_update(const unsigned char *bytes, size_t length, const sample *s)
{
    const char *call = "append(xs, Item())";
    const char *start = strstr(s->source, call);
    REQUIRE(start);
    for (int backend = 0; backend < 2; backend++) {
        native_capabilities *native;
        package_collection *packages = read_packages(
            bytes,
            length,
            &native,
            backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE
        );
        REQUIRE(packages);
        const behavior_function *body = NULL, *item = NULL;
        for (int p = 0; p < package_collection_count(packages); p++) {
            const execution_module *module = package_collection_module(packages, p);
            for (int f = 0; f < execution_module_function_count(module); f++) {
                execution_module_function_info info = execution_module_function_at(module, f);
                if (info.id == function_id("AppendLocal")) {
                    body = info.body;
                }
                if (info.id == function_id("Item")) {
                    item = info.body;
                }
            }
        }
        REQUIRE(body && item);
        array_update_trace trace = { .item = item,
                                     .start = (int)(start - s->source),
                                     .end = (int)(start - s->source + strlen(call)),
                                     .valid = true };
        arena storage = { 0 };
        pin_value result = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY };
        behavior_function_activation activation = { 0 };
        bool resumable = behavior_function_resumable(body);
        if (resumable) {
            behavior_function_activation_init(body, &activation);
        }
        behavior_function_prepare_results(body, &result, &storage);
        /* The array values live in the invocation's managed heap. */
        behavior_managed_heap heap;
        behavior_managed_heap_init(&heap);
        behavior_function_context context = { .activation = resumable ? &activation : NULL,
                                              .managed_heap = &heap,
                                              .results = &result,
                                              .budget = TARGET_BUDGET,
                                              .source_debug_user = &trace,
                                              .source_pause = array_update_pause,
                                              .source_capture = array_update_capture };
        behavior_function_bind_storage(&context, &storage);
        bool bound = behavior_function_bind(body, &context, NULL, 0);
        behavior_run_scope runs = { 0 };
        behavior_run_scope_begin(&runs);
        int status = bound ? behavior_function_run(body, &context, backend != 0)
                           : FLOW_ERR_ARGUMENT;
        behavior_run_scope_end(&runs);
        const pin_array *array = result.ptr;
        bool valid = status == FLOW_OK && context.returned && trace.valid &&
                     trace.step == 4 && array && array->count == 2 &&
                     pin_array_get(array, 0).i == 1 &&
                     pin_array_get(array, 1).i == 7;
        behavior_function_release_host(&context);
        behavior_function_activation_free(&activation);
        behavior_managed_heap_free(&heap);
        arena_free(&storage);
        package_collection_free(packages);
        native_capabilities_free(native);
        REQUIRE(valid);
        REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
        REQUIRE(backend != 0 || runs.counts.bytecode_entries > 0);
        if (backend && TEST_COMPILED_BACKEND == BEHAVIOR_SLJIT) {
            REQUIRE(runs.counts.inspected_sljit_entries > 0 && !runs.counts.bytecode_entries);
        }
        if (backend && TEST_COMPILED_BACKEND == BEHAVIOR_WASM) {
            REQUIRE(runs.counts.inspected_wasm_entries > 0);
        }
    }
    return true;
}
/* Compare serialized function flags, independently of the runtime's derived
 * effect summary. Direct, recursive and imported HOST callers must retain it. */
static bool same_host_effects(const execution_module *expected, const execution_module *actual)
{
    bool same = true;
    int observed = 0;
    for (int i = 0; i < expected->plan.function_count; i++) {
        const execution_module_function *x = &expected->plan.functions[i];
        const execution_module_function *y = NULL;
        for (int j = 0; !y && j < actual->plan.function_count; j++) {
            if (actual->plan.functions[j].id == x->id) {
                y = &actual->plan.functions[j];
            }
        }
        bool host_call = x->id == function_id("Probe") ||
                         x->id == function_id("Caller") ||
                         x->id == function_id("Recursive") ||
                         x->id == function_id("ImportedHost") ||
                         x->id == script_catalog_id("package-symbol", "project:lib:1:Through:callable");
        REQUIRE(!host_call || (x->effects & SCRIPT_EFFECT_HOST));
        observed += host_call;
        REQUIRE(y);
        if (x->effects != y->effects) {
            fprintf(
                stderr,
                "stored function effects %016llx: stage zero=%u stage one=%u\n",
                (unsigned long long)x->id,
                x->effects,
                y->effects
            );
        }
        same &= x->effects == y->effects;
    }
    REQUIRE(observed > 0);
    return same;
}
/* Each module retains stage zero's source points, watches and, when supplied,
 * effects for the target's natives. */
static bool points_like_oracle(const unsigned char *bytes, size_t length, bool host_effects)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    const authoring_packages *owner = oracle.owners[0];
    bool alike = package_collection_count(packages) == authoring_packages_count(owner);
    for (int p = 0; alike && p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        const execution_module *expected = NULL;
        for (int q = 0; !expected && q < authoring_packages_count(owner); q++) {
            const execution_module *candidate = authoring_packages_at(owner, q)->module;
            expected = execution_module_id(candidate) == execution_module_id(module)
                         ? candidate
                         : NULL;
        }
        alike = expected && same_points(expected, module) &&
                same_watches(expected, module) &&
                (!host_effects || same_host_effects(expected, module));
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    return alike;
}
/* Each compiler backend has a focused selector. Bytecode compares source
 * points with stage zero; both compare explicit target effects. */
/* Stage zero's packages for the sample, linked against the fixture's
 * natives, and its selected diagnostic package (facts by default). */
static authoring_packages *oracle_packages(const sample *s, const authoring_package **root)
{
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(s, inputs);
    source_package_collection collections[] = { { "core", "/sdk/core" } };
    source_package_snapshot_options options = { .project_root = "/compiler",
                                                .files = inputs,
                                                .file_count = count,
                                                .collections = collections,
                                                .collection_count = 1 };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    assert(snapshot && !source_package_snapshot_error_at(snapshot));
    script_lower_bindings bindings = authoring_native_bindings(fixture.native);
    authoring_packages *packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(fixture.native),
        &bindings,
        BEHAVIOR_BYTECODE,
        NULL
    );
    assert(packages);
    const char *identity = s->oracle_package ? s->oracle_package : "project:facts";
    *root = NULL;
    for (int p = 0; p < authoring_packages_count(packages); p++) {
        const authoring_package *candidate = authoring_packages_at(packages, p);
        if (!strcmp(candidate->identity, identity)) {
            *root = candidate;
        }
    }
    assert(*root);
    return packages;
}
static bool rejects_like_oracle(const sample *s, const char *message, int line, int column)
{
    const authoring_package *root;
    authoring_packages *packages = oracle_packages(s, &root);
    REQUIRE(!authoring_packages_valid(packages));
    REQUIRE(root->diagnostic_count);
    const script_diagnostic *expected = &root->diagnostics[0];
    bool same = !strcmp(expected->message, s->oracle_message ? s->oracle_message : message) &&
                expected->range.line == line && expected->range.column == column;
    if (!same) {
        fprintf(
            stderr,
            "stage zero %s: %s at %d:%d; stage one at %d:%d\n",
            s->name,
            expected->message,
            expected->range.line,
            expected->range.column,
            line,
            column
        );
    }
    authoring_packages_free(packages);
    REQUIRE(same);
    return true;
}
static bool rejects_malformed_effects()
{
    static const source_package_input bad_source = { "facts/Main.chibi", "not source", 10 };
    pin_value args[ARGUMENTS + 1];
    REQUIRE(arguments_for(0, false, false, &bad_source, 1, args));
    const pin_array *declared = linked_effects[0][0][0].ptr;
    REQUIRE(declared && declared->count > 0);
    for (int i = 0; i < 3; i++) {
        int count = i ? declared->count : 0;
        pin_array *masks = pin_array_snapshot_allocate(&input, PIN_I32, NULL, count);
        for (int j = 0; j < count; j++) {
            masks->items[j] = (pin_value){ .type = PIN_I32,
                                           .i = j      ? 0
                                              : i == 1 ? -1
                                                       : 128 };
        }
        args[ARGUMENTS] = (pin_value){ .type = PIN_I32,
                                       .container = PIN_CONTAINER_ARRAY,
                                       .ptr = masks };
        compiler_result out;
        pin_value message, document;
        REQUIRE(compiler_call(0, "CompileWithEffects", args, ARGUMENTS + 1, COMPILE_BUDGET, &out));
        REQUIRE(!compiler_succeeded(&out));
        REQUIRE(get(compiler_error(&out), "message", &message));
        REQUIRE(get(compiler_error(&out), "document", &document));
        REQUIRE(
            !strcmp(pin_text(message).bytes, "unsupported compiler input or target") &&
            document.i == -1
        );
        arena_free(&detached);
    }
    return true;
}
enum
{
    TRACED_WATCHES = 64
};
typedef struct
{
    int start, end, ordinal;
    bool output;
    pin_type type;
    uint64_t bits;
} traced_watch;
/* The watches one call delivers, in order, with each scalar payload's bits. */
typedef struct
{
    traced_watch watches[TRACED_WATCHES];
    int count;
    bool overflow;
} watch_trace;
static uint64_t payload_bits(const pin_value *value)
{
    if (value->container != PIN_CONTAINER_SINGLE) {
        return 0;
    }
    uint32_t single;
    switch (value->type) {
        case PIN_BOOL: return value->b;
        case PIN_I32: return (uint32_t)value->i;
        case PIN_F32: memcpy(&single, &value->f, sizeof single); return single;
        case PIN_I64:
        case PIN_U64:
        case PIN_F64: return value->u64;
        default: return 0;
    }
}
static void trace_watch(
    void *user,
    behavior_function_context *,
    const behavior_source_watch *watch,
    const pin_value *value
)
{
    watch_trace *trace = user;
    if (trace->count == TRACED_WATCHES) {
        trace->overflow = true;
        return;
    }
    trace->watches[trace->count++] = (traced_watch){
        .start = watch->origin.start,
        .end = watch->origin.end,
        .ordinal = watch->ordinal,
        .output = watch->output,
        .type = value ? value->type : PIN_FLOW,
        .bits = value ? payload_bits(value) : 0,
    };
}
/* Runs `function`, which takes no arguments and returns one value, from a
 * closure linked with the probe natives, tracing the watches it delivers. */
static bool traced_call(
    const unsigned char *bytes,
    size_t length,
    const char *function,
    int backend,
    watch_trace *trace,
    int *status,
    pin_value *output
)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    const behavior_function *body = NULL;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int f = 0; f < execution_module_function_count(module); f++) {
            execution_module_function_info info = execution_module_function_at(module, f);
            if (info.id == function_id(function)) {
                body = info.body;
            }
        }
    }
    REQUIRE(body && body->argument_count == 0 && body->result_count == 1);
    arena storage = { 0 };
    behavior_function_activation activation = { 0 };
    bool resumable = behavior_function_resumable(body);
    if (resumable) {
        behavior_function_activation_init(body, &activation);
    }
    *output = body->result_defaults[0];
    behavior_function_prepare_results(body, output, &storage);
    behavior_function_context context = { .activation = resumable ? &activation : NULL,
                                          .results = output,
                                          .budget = TARGET_BUDGET,
                                          .source_debug_user = trace,
                                          .source_capture = trace_watch };
    behavior_function_bind_storage(&context, &storage);
    bool bound = behavior_function_bind(body, &context, NULL, 0);
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    *status = bound ? behavior_function_run(body, &context, backend != 0)
                    : FLOW_ERR_ARGUMENT;
    behavior_run_scope_end(&runs);
    if (*status == FLOW_OK && !context.returned) {
        *status = FLOW_ERR_ARGUMENT;
    }
    *output = (pin_value){ .type = output->type, .u64 = payload_bits(output) };
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    package_collection_free(packages);
    native_capabilities_free(native);
    REQUIRE(bound && !trace->overflow);
    /* The call ran on the backend it selected, inspected, with no fallback. */
    const behavior_run_counts *n = &runs.counts;
    uint64_t compiled = n->sljit_entries + n->wasm_entries;
    REQUIRE(!n->overflow && !n->inspection_bytecode_fallbacks && !n->size_bytecode_fallbacks);
    REQUIRE(
        backend ? compiled > 0 && !n->bytecode_entries &&
                      n->inspected_sljit_entries + n->inspected_wasm_entries == compiled
                : n->bytecode_entries > 0 && !compiled
    );
    return true;
}
static bool same_traced(const traced_watch *x, const traced_watch *y)
{
    return x->start == y->start && x->end == y->end && x->ordinal == y->ordinal &&
           x->output == y->output && x->type == y->type && x->bits == y->bits;
}
static void print_traced(const char *compiler, int index, const traced_watch *w)
{
    fprintf(
        stderr,
        "  %s watch %d at %d..%d ordinal %d%s type %d bits %llx\n",
        compiler,
        index,
        w->start,
        w->end,
        w->ordinal,
        w->output ? " output" : "",
        (int)w->type,
        (unsigned long long)w->bits
    );
}
static bool same_trace(const watch_trace *expected, const watch_trace *actual)
{
    bool same = expected->count == actual->count;
    for (int i = 0; same && i < expected->count; i++) {
        same = same_traced(&expected->watches[i], &actual->watches[i]);
    }
    for (int i = 0; !same && i < expected->count; i++) {
        print_traced("stage zero", i, &expected->watches[i]);
    }
    for (int i = 0; !same && i < actual->count; i++) {
        print_traced("stage one", i, &actual->watches[i]);
    }
    return same;
}
/* Each traced call returns what stage zero's does and delivers its watches,
 * on both backends. */
static bool traces_like_oracle(const unsigned char *bytes, size_t length, const sample *s)
{
    unsigned char *expected = NULL;
    size_t expected_length = 0;
    REQUIRE(oracle_closure(s, &expected, &expected_length));
    bool same = true;
    for (int c = 0; same && c < CALLS && s->calls[c].function; c++) {
        const sample_call *call = &s->calls[c];
        REQUIRE(!strcmp(call->arguments, "[]"));
        for (int backend = 0; same && backend < 2; backend++) {
            watch_trace traces[2] = { 0 };
            int statuses[2];
            pin_value results[2];
            REQUIRE(traced_call(
                expected,
                expected_length,
                call->function,
                backend,
                &traces[0],
                &statuses[0],
                &results[0]
            ));
            REQUIRE(
                traced_call(bytes, length, call->function, backend, &traces[1], &statuses[1], &results[1])
            );
            char returned[32] = "";
            if (results[1].type == PIN_I32) {
                snprintf(returned, sizeof returned, "[%d]", results[1].i);
            }
            bool result = call->expected ? statuses[1] == FLOW_OK &&
                                               results[0].type == results[1].type &&
                                               results[0].u64 == results[1].u64 &&
                                               (!s->c_string_probe ||
                                                !strcmp(returned, call->expected))
                                         : statuses[1] != FLOW_OK;
            same = traces[0].count > 0 && statuses[0] == statuses[1] &&
                   result && same_trace(&traces[0], &traces[1]);
            if (!same) {
                fprintf(
                    stderr,
                    "package compiler traced %s on backend %d: status %d and "
                    "%d, %s\n",
                    call->function,
                    backend,
                    statuses[0],
                    statuses[1],
                    returned
                );
            }
        }
    }
    free(expected);
    return same;
}
/* Unused collection packages have no emitted row, even an empty one. All
 * independent project packages retain nonempty module rows. */
static bool project_closure_only(const unsigned char *bytes, size_t length, int expected)
{
    char error[256];
    int count = 0;
    uint64_t root = 0;
    package_module *rows = compiler_artifact_rows(bytes, length, &count, &root, error, sizeof error);
    if (!rows) {
        fprintf(stderr, "project-only closure: %s\n", error);
    }
    REQUIRE(rows);
    bool project_only = count == expected &&
                        root == script_catalog_id("module", "project:facts");
    for (int i = 0; i < count; i++) {
        project_only &= !strncmp(rows[i].name, "project:", 8) && rows[i].length > 0;
    }
    compiler_artifact_rows_free(rows, count);
    REQUIRE(project_only);
    return true;
}
/* Invalid Unicode scalars are authored data failures, as stage zero's
 * char_check emits, rather than failures attributed to a native host call. */
static bool char_failures_use_data_status(const unsigned char *bytes, size_t length)
{
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    int checked = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module_plan *plan = &package_collection_module(packages, p)
                                                 ->plan;
        for (int f = 0; f < plan->function_count; f++) {
            const behavior_function *body = plan->functions[f].body;
            for (int op = 0; op < acount(body->instructions); op++) {
                const behavior_function_instruction *instruction = &body->instructions[op];
                if (instruction->op == BF_ASSERT && instruction->message &&
                    !strcmp(instruction->message, "Value does not fit in char")) {
                    REQUIRE(instruction->target == BEHAVIOR_FUNCTION_DATA);
                    checked++;
                }
            }
        }
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    REQUIRE(checked > 0);
    return true;
}
static bool math_rejects_at_source(const sample *s)
{
    const authoring_package *root;
    authoring_packages *packages = oracle_packages(s, &root);
    bool same = !authoring_packages_valid(packages) && root->diagnostic_count > 0;
    if (same) {
        const script_diagnostic *diagnostic = &root->diagnostics[0];
        same = !strcmp(diagnostic->message, s->oracle_message) &&
               diagnostic->range.line == s->oracle_line &&
               diagnostic->range.column == s->oracle_column;
        if (!same) {
            fprintf(
                stderr,
                "stage zero math %s: %s at %d:%d\n",
                s->name,
                diagnostic->message,
                diagnostic->range.line,
                diagnostic->range.column
            );
        }
    }
    authoring_packages_free(packages);
    REQUIRE(same);
    return true;
}
static bool run_sample_table(sample_group group, int b, const sample *table, int table_count)
{
    REQUIRE(prepare_backends(1u << b));
    if (group == BASE_SAMPLES && b == 0) {
        REQUIRE(rejects_malformed_effects());
    }
    for (int i = 0; i < table_count; i++) {
        const sample *s = &table[i];
        if ((group != MATH_SAMPLES && s->group != group)) {
            continue;
        }
        source_package_input inputs[SAMPLE_INPUTS];
        int count = sample_inputs(s, inputs);
        compiler_result out;
        bool compiled = compile(b, s->game, s->c_string_probe, s->host_effects, inputs, count, COMPILE_BUDGET, &out);
        if (!compiled) {
            fprintf(
                stderr,
                "package compiler sample failed: name=%s index=%d group=%d "
                "backend=%s inputs=%d source_bytes=%zu budget=%d\n",
                s->name,
                i,
                group,
                b ? "compiled" : "bytecode",
                count,
                strlen(s->source),
                COMPILE_BUDGET
            );
        }
        REQUIRE(compiled);
        if (s->message) {
            if (compiler_succeeded(&out)) {
                fprintf(stderr, "package compiler %s: accepted\n", s->name);
                if (s->same_rejection && s->oracle_message && s->line > 0 &&
                    s->column > 0) {
                    REQUIRE(rejects_like_oracle(s, s->message, s->line, s->column));
                    fprintf(
                        stderr,
                        "stage zero rejected it: %s at %d:%d\n",
                        s->oracle_message,
                        s->line,
                        s->column
                    );
                }
            }
            REQUIRE(!compiler_succeeded(&out));
            pin_value message, line, column;
            REQUIRE(get(compiler_error(&out), "message", &message));
            REQUIRE(
                get(compiler_error(&out), "line", &line) &&
                get(compiler_error(&out), "column", &column)
            );
            bool position = s->same_rejection
                              ? rejects_like_oracle(
                                    s,
                                    pin_text(message).bytes,
                                    line.i,
                                    column.i
                                )
                              : line.i == s->line && column.i == s->column;
            if (strcmp(pin_text(message).bytes, s->message) || !position) {
                fprintf(
                    stderr,
                    "package compiler %s: %s at %d:%d\n",
                    s->name,
                    pin_text(message).bytes,
                    line.i,
                    column.i
                );
            }
            REQUIRE(!strcmp(pin_text(message).bytes, s->message) && position);
            if (group == MATH_SAMPLES) {
                REQUIRE(math_rejects_at_source(s));
            }
            arena_free(&detached);
            continue;
        }
        if (!compiler_succeeded(&out)) {
            pin_value message, line, column;
            get(compiler_error(&out), "message", &message);
            get(compiler_error(&out), "line", &line);
            get(compiler_error(&out), "column", &column);
            fprintf(
                stderr,
                "package compiler %s rejected: %s at %d:%d\n",
                s->name,
                pin_text(message).bytes,
                line.i,
                column.i
            );
        }
        REQUIRE(compiler_succeeded(&out));
        size_t length;
        unsigned char *bytes = closure_bytes(&out, &length);
        arena_free(&detached);
        if (s->project_modules) {
            REQUIRE(project_closure_only(bytes, length, s->project_modules));
        }
        unsigned char *expected = NULL;
        size_t expected_length = 0;
        bool ran = (!s->char_checks || char_failures_use_data_status(bytes, length)) &&
                   (!s->updates || updates_in_place(bytes, length, s->name)) &&
                   (!s->specializations ||
                    reuses_specializations(bytes, length, s->specializations)) &&
                   (!s->boxed || reads_boxed(bytes, length)) &&
                   (!s->moves || moves_fresh_values(bytes, length)) &&
                   (!s->aliases || reads_in_place(bytes, length)) &&
                   (!s->variants || moves_returned_variants(bytes, length)) &&
                   (!s->built || builds_results(bytes, length)) &&
                   (!s->lends || lends_native_values(bytes, length)) &&
                   (!s->drops || drops_only_collections(bytes, length)) &&
                   ((b && !s->host_effects && !s->debug_metadata) ||
                    (oracle_closure(s, &expected, &expected_length) &&
                     (s->math || points_like_oracle(bytes, length, s->host_effects))));
        bool hosted = !s->traced || !s->c_string_probe;
        for (int c = 0; ran && hosted && c < CALLS && s->calls[c].function; c++) {
            for (int backend = 0; ran && backend < 2; backend++) {
                ran = call_sample(s, bytes, length, &s->calls[c], backend);
                if (!ran && group == MATH_SAMPLES) {
                    fprintf(stderr, "math stage-one compiler=%d target-vm=%d\n", b, backend);
                }
            }
            if (ran && !b) {
                ran = call_sample(s, expected, expected_length, &s->calls[c], 0);
                if (!ran && group == MATH_SAMPLES) {
                    fprintf(stderr, "math stage-zero oracle target-vm=0\n");
                }
            }
            if (ran && !b && group == MATH_SAMPLES) {
                ran = call_sample(s, expected, expected_length, &s->calls[c], 1);
                if (!ran) {
                    fprintf(stderr, "math stage-zero oracle target-vm=1\n");
                }
            }
        }
        ran = ran && (!s->traced || traces_like_oracle(bytes, length, s));
        if (ran && s->update_order) {
            ran = (b || array_update_order(expected, expected_length, s)) &&
                  array_update_order(bytes, length, s) &&
                  observes_array_update(bytes, length, s);
            pin_value args[ARGUMENTS], closure;
            REQUIRE(arguments(b, false, inputs, count, args));
            REQUIRE(compiler_fixture_call(
                &fixture,
                b,
                PACKAGE,
                "CompileClosure",
                args,
                ARGUMENTS,
                COMPILE_BUDGET,
                &detached,
                &closure
            ));
            const pin_array *array = closure.ptr;
            REQUIRE(closure.type == PIN_U8 && closure.container == PIN_CONTAINER_ARRAY && array);
            REQUIRE(length > 0 && (size_t)array->count == length);
            bool same = true;
            for (size_t i = 0; i < length; i++) {
                same &= array->items[i].u8 == bytes[i];
            }
            arena_free(&detached);
            REQUIRE(same);
        }
        free(bytes);
        free(expected);
        if (!ran) {
            fprintf(stderr, "package compiler %s: differs\n", s->name);
        }
        REQUIRE(ran);
    }
    return true;
}
static bool run_samples(sample_group group, int b)
{
    return run_sample_table(group, b, samples, (int)SAMPLES);
}
/* Without debugger points and watches, a closure's functions have none, and
 * its loops and branches still run as they do with them. */
/* The closure the compiler builds for sample `s` without debug information,
 * or NULL when it does not compile. */
static unsigned char *debug_free_closure(const sample *s, size_t *length)
{
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(s, inputs);
    pin_value args[ARGUMENTS];
    compiler_result out;
    if (!arguments(1, false, inputs, count, args)) {
        return NULL;
    }
    args[DEBUG_ARGUMENT].b = false;
    if (!compiler_call(1, "Compile", args, ARGUMENTS, COMPILE_BUDGET, &out)) {
        return NULL;
    }
    unsigned char *bytes = compiler_succeeded(&out) ? closure_bytes(&out, length)
                                                    : NULL;
    arena_free(&detached);
    return bytes;
}
TEST_CASE(package_compiler_omits_debug_info)
{
    REQUIRE(prepare());
    const char *const names[] = { "loops", "control", "array update debug order" };
    for (size_t n = 0; n < sizeof names / sizeof *names; n++) {
        const sample *s = samples;
        while (strcmp(s->name, names[n])) {
            s++;
        }
        size_t length;
        unsigned char *bytes = debug_free_closure(s, &length);
        REQUIRE(bytes);
        native_capabilities *native;
        package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
        REQUIRE(packages);
        int debug = 0;
        for (int p = 0; p < package_collection_count(packages); p++) {
            const execution_module_plan *plan = &package_collection_module(packages, p)
                                                     ->plan;
            for (int i = 0; i < plan->function_count; i++) {
                debug += acount(plan->functions[i].body->source_points) +
                         acount(plan->functions[i].body->source_watches);
            }
        }
        package_collection_free(packages);
        native_capabilities_free(native);
        bool ran = !debug;
        for (int c = 0; ran && c < CALLS && s->calls[c].function; c++) {
            for (int backend = 0; ran && backend < 2; backend++) {
                ran = run(bytes, length, &s->calls[c], backend);
            }
        }
        free(bytes);
        REQUIRE(ran);
    }
    return true;
}
/* Without debug information, a push or extension of a local array updates it
 * in place and drops a temporary item at once rather than holding it until
 * the function returns. */
TEST_CASE(package_compiler_drops_pushed_items)
{
    REQUIRE(prepare());
    const sample *s = samples;
    while (strcmp(s->name, "plain drops")) {
        s++;
    }
    size_t length;
    unsigned char *bytes = debug_free_closure(s, &length);
    REQUIRE(bytes);
    native_capabilities *native;
    package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
    REQUIRE(packages);
    int pushes = 0, wrong = 0;
    for (int p = 0; p < package_collection_count(packages); p++) {
        const execution_module *module = package_collection_module(packages, p);
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info f = execution_module_function_at(module, i);
            if (strcmp(f.entry, "HeldExtend") && strcmp(f.entry, "HeldPush")) {
                continue;
            }
            pushes++;
            wrong += !drops_pushed_item(f.body);
        }
    }
    package_collection_free(packages);
    native_capabilities_free(native);
    bool ran = pushes == 2 && !wrong;
    for (int c = 0; ran && c < CALLS && s->calls[c].function; c++) {
        for (int backend = 0; ran && backend < 2; backend++) {
            ran = run(bytes, length, &s->calls[c], backend);
        }
    }
    free(bytes);
    REQUIRE(ran);
    return true;
}
/* The instructions compiling a package of `records` records takes, counted
 * across every call, or zero when it does not compile. */
static uint64_t declaring_steps(int records)
{
    static char source[64 * 1024];
    int used = snprintf(source, sizeof source, "package facts\n");
    for (int i = 0; i < records; i++) {
        used += snprintf(source + used, sizeof source - (size_t)used, "R%d :: struct { v: int; }\n", i);
    }
    used += snprintf(source + used, sizeof source - (size_t)used, "Read :: (r: R0) -> int { return r.v; }\n");
    REQUIRE((size_t)used < sizeof source);
    source_package_input input = { "facts/Main.chibi", source, (size_t)used };
    behavior_execution_scope scope = { .remaining = UINT64_MAX };
    behavior_execution_scope_begin(&scope);
    compiler_result out;
    bool called = compile(0, false, false, false, &input, 1, COMPILE_BUDGET, &out);
    behavior_execution_scope_end(&scope);
    bool compiled = called && compiler_succeeded(&out);
    arena_free(&detached);
    return compiled ? UINT64_MAX - scope.remaining : 0;
}
/* Declaring each type's array and view optionals takes one pass over the
 * type table, not a search of it for each type: twice the records take about
 * twice the steps, where the search took three times as many. */
TEST_CASE(package_compiler_declares_records_in_linear_steps)
{
    REQUIRE(prepare());
    uint64_t fewer = declaring_steps(100), more = declaring_steps(200);
    REQUIRE(fewer && 2 * more <= 5 * fewer);
    return true;
}
/* Stage zero currently resolves assert through its native catalog, so its
 * ordinary pure-target oracle cannot model this authored intrinsic contract.
 * Compile directly with no game/native assertion pack and inspect the emitted
 * checked instruction before running both execution backends. Existing oracle
 * samples remain unchanged. */
TEST_CASE(package_compiler_asserts_without_native_capability)
{
    REQUIRE(prepare());
    const sample cases[] = {
        { .source = "package facts\n"
                    "Double :: (n: int) -> int {\n"
                    "    assert(condition: n > 0, message: \"n is "
                    "positive\");\n"
                    "    return n * 2;\n}\n",
          .calls = { { .function = "Double", .arguments = "[3]", .expected = "[6]" },
                     { .function = "Double",
                       .arguments = "[-1]",
                       .expected = NULL,
                       .diagnostic = "Assertion failed: n is positive" } } },
        { .source = "package facts\n"
                    "assert :: (n: int) -> int { return n + 7; }\n"
                    "Main :: () -> int { return assert(3); }\n",
          .calls = { { .function = "Main", .arguments = "[]", .expected = "[10]" } } },
        { .source = "package facts\n"
                    "import lib \"project:lib\"\n"
                    "Main :: () -> int { return lib.assert(3); }\n",
          .library = "package lib\npub assert :: (n: int) -> int { return n + "
                     "9; }\n",
          .calls = { { .function = "Main", .arguments = "[]", .expected = "[12]" } } }
    };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        source_package_input inputs[SAMPLE_INPUTS];
        int count = sample_inputs(&cases[i], inputs);
        for (int b = 0; b < 2; b++) {
            compiler_result out;
            REQUIRE(compile(b, false, false, false, inputs, count, COMPILE_BUDGET, &out));
            REQUIRE(compiler_succeeded(&out));
            size_t length;
            unsigned char *bytes = closure_bytes(&out, &length);
            arena_free(&detached);
            native_capabilities *native;
            package_collection *packages = read_packages(bytes, length, &native, BEHAVIOR_BYTECODE);
            REQUIRE(packages);
            int checked = 0;
            for (int p = 0; p < package_collection_count(packages); p++) {
                const execution_module_plan
                    *plan = &package_collection_module(packages, p)->plan;
                for (int f = 0; f < plan->function_count; f++) {
                    const behavior_function *body = plan->functions[f].body;
                    for (int op = 0; op < acount(body->instructions); op++) {
                        if (body->instructions[op].op == BF_ASSERT) {
                            REQUIRE(body->instructions[op].target == BEHAVIOR_FUNCTION_DATA);
                            REQUIRE(!acount(body->host_calls));
                            checked++;
                        }
                    }
                }
            }
            package_collection_free(packages);
            native_capabilities_free(native);
            REQUIRE(checked == (i == 0));
            bool ran = true;
            for (int c = 0; ran && c < CALLS && cases[i].calls[c].function; c++) {
                for (int backend = 0; ran && backend < 2; backend++) {
                    ran = run(bytes, length, &cases[i].calls[c], backend);
                }
            }
            free(bytes);
            REQUIRE(ran);
        }
    }
    return true;
}
static bool approved_float_conversion_admission(int compiler, bool constants)
{
    bool agreed = true;
    for (size_t i = 0; i < sizeof FLOAT_CONVERSION_CASES / sizeof *FLOAT_CONVERSION_CASES;
         i++) {
        const float_conversion_case *item = &FLOAT_CONVERSION_CASES[i];
        if (item->constant != constants) {
            continue;
        }
        source_package_input source = { "facts/Main.chibi",
                                        item->source,
                                        strlen(item->source) };
        compiler_result out;
        REQUIRE(compile(compiler, false, false, false, &source, 1, COMPILE_BUDGET, &out));
        bool accepted = compiler_succeeded(&out);
        if (!accepted) {
            pin_value message, line, column;
            REQUIRE(get(compiler_error(&out), "message", &message));
            REQUIRE(get(compiler_error(&out), "line", &line));
            REQUIRE(get(compiler_error(&out), "column", &column));
            REQUIRE(line.type == PIN_I32 && column.type == PIN_I32);
            fprintf(
                stderr,
                "FLOAT_REJECTION stage1 %s %d:%d %s\n",
                item->name,
                line.i,
                column.i,
                pin_text(message).bytes
            );
            if (!item->accepted) {
                if (!item->stage_one.message) {
                    REQUIRE(item->constant);
                    fprintf(
                        stderr,
                        "FLOAT_DEPENDENCY_BLOCKED %s: typed-constant narrowing "
                        "category is pending; rejection is not policy proof\n",
                        item->name
                    );
                    agreed = false;
                } else if (
                    strcmp(pin_text(message).bytes, item->stage_one.message) ||
                    line.i != item->stage_one.line ||
                    column.i != item->stage_one.column
                ) {
                    fprintf(
                        stderr,
                        "FLOAT_REJECTION_MISMATCH stage1 %s expected %d:%d "
                        "%s\n",
                        item->name,
                        item->stage_one.line,
                        item->stage_one.column,
                        item->stage_one.message
                    );
                    agreed = false;
                }
            }
        }
        if (accepted != item->accepted) {
            fprintf(
                stderr,
                "stage-one floating admission %s: expected %s, got %s\n",
                item->name,
                item->accepted ? "accepted" : "rejected",
                accepted ? "accepted" : "rejected"
            );
            if (!accepted) {
                pin_value message;
                REQUIRE(get(compiler_error(&out), "message", &message));
                fprintf(stderr, "  %s\n", pin_text(message).bytes);
            }
            agreed = false;
        }
        if (accepted && item->accepted) {
            size_t length;
            unsigned char *bytes = closure_bytes(&out, &length);
            arena_free(&detached);
            sample_call call = { .function = "Fold",
                                 .arguments = item->arguments,
                                 .expected = item->expected };
            bool ran = true;
            for (int backend = 0; backend < 2; backend++) {
                ran = run(bytes, length, &call, backend) && ran;
                compiler_host_free(host);
                host = NULL;
                free(result.json);
                result = (compiler_host_result){ 0 };
            }
            free(bytes);
            agreed = ran && agreed;
        }
        arena_free(&detached);
    }
    REQUIRE(agreed);
    return true;
}

/* The literal diagnostics also hold in the independent stage-zero frontend,
 * including reference and boxed targets and record fields. */
TEST_CASE(package_compiler_compound_diagnostics_match_stage_zero)
{
    REQUIRE(prepare());
    int checked = 0;
    for (int i = 0; i < (int)SAMPLES; i++) {
        const sample *s = &samples[i];
        if (!strncmp(s->name, "mixed compound ", strlen("mixed compound "))) {
            REQUIRE(s->message);
            REQUIRE(rejects_like_oracle(s, s->message, s->line, s->column));
            checked++;
        }
    }
    REQUIRE(checked == 7);
    return true;
}
TEST_CASE(package_compiler_builds_runnable_packages)
{
    return run_samples(BASE_SAMPLES, 0);
}
TEST_CASE(package_compiler_builds_runnable_packages_compiled)
{
    return run_samples(BASE_SAMPLES, 1);
}
TEST_CASE(package_compiler_builtin_math_source_contracts)
{
    enum
    {
        COUNT = sizeof math_samples / sizeof *math_samples
    };
    REQUIRE(run_sample_table(MATH_SAMPLES, 0, math_samples, COUNT));
    cleanup();
    return run_sample_table(MATH_SAMPLES, 1, math_samples, COUNT);
}
TEST_CASE(package_compiler_builds_runnable_typed_packages)
{
    return run_samples(TYPE_SAMPLES, 0);
}
TEST_CASE(package_compiler_builds_runnable_typed_packages_compiled)
{
    return run_samples(TYPE_SAMPLES, 1);
}
TEST_CASE(package_compiler_checks_fallible_source_results)
{
    return run_samples(FALLIBLE_SAMPLES, 0);
}
TEST_CASE(package_compiler_checks_fallible_source_results_compiled)
{
    return run_samples(FALLIBLE_SAMPLES, 1);
}
TEST_CASE(package_compiler_builds_runnable_collection_packages)
{
    return run_samples(COLLECTION_SAMPLES, 0);
}
TEST_CASE(package_compiler_builds_runnable_collection_packages_compiled)
{
    return run_samples(COLLECTION_SAMPLES, 1);
}
TEST_CASE(package_compiler_builds_runnable_importing_packages)
{
    REQUIRE(run_samples(IMPORT_SAMPLES, 0));
    return approved_float_conversion_admission(0, false);
}
TEST_CASE(package_compiler_builds_runnable_importing_packages_compiled)
{
    REQUIRE(run_samples(IMPORT_SAMPLES, 1));
    return approved_float_conversion_admission(1, false);
}
TEST_CASE(package_compiler_float_constant_dependency)
{
    REQUIRE(prepare_backends(COMPILER_FIXTURE_BYTECODE));
    return approved_float_conversion_admission(0, true);
}
TEST_CASE(package_compiler_float_constant_dependency_compiled)
{
    REQUIRE(prepare_backends(COMPILER_FIXTURE_COMPILED));
    return approved_float_conversion_admission(1, true);
}
/* Values as the compiler host takes arguments and projects results: a JSON
 * array; malloc-owned. */
static char *document_json(const pin_value *values, int count, size_t *length)
{
    av_doc *doc = av_doc_new();
    av_value *array = av_arr(doc);
    for (int i = 0; i < count; i++) {
        av_value *value = pin_value_to_document(doc, &values[i]);
        assert(value);
        av_arr_append(array, value);
    }
    yyjson_mut_doc *encoded = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(encoded, av_to_json(encoded, array));
    char *json = yyjson_mut_write(encoded, YYJSON_WRITE_ALLOW_INVALID_UNICODE, length);
    yyjson_mut_doc_free(encoded);
    av_doc_free(doc);
    return json;
}
/* Stage two's rejection of the compiler's sources, from Compile's result. */
static void stage_two_rejection(const char *json, size_t length)
{
    bool called = compiler_host_invoke(
        host,
        script_catalog_id("package-symbol", "project:package_compiler:1:Compile:callable"),
        json,
        length,
        SELF_BUDGET,
        &result
    );
    fprintf(
        stderr,
        "package compiler stage two rejected: %s\n",
        called ? result.json : result.diagnostic
    );
}
/* Writes bytes to folder/name. */
static bool write_seed_file(const char *folder, const char *name, const void *bytes, size_t length)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", folder, name);
    FILE *file = fopen(path, "wb");
    bool written = file && fwrite(bytes, 1, length, file) == length;
    if (file) {
        written = !fclose(file) && written;
    }
    return written;
}
/* With CHIBI_BOOTSTRAP_SEED naming a folder, leaves stage two and Compile's
 * arguments there, the seed make bootstrap-artifact writes the bootstrap
 * artifact from. */
static bool write_seed(const unsigned char *stage_two, size_t length, const char *json, size_t json_length)
{
    const char *seed = getenv("CHIBI_BOOTSTRAP_SEED");
    return !seed || (write_seed_file(seed, "compiler.chibip", stage_two, length) &&
                     write_seed_file(seed, "compile.json", json, json_length));
}
/* Stage two reads the listed floating literals as stage one does: each
 * LiteralBits result projects to the same JSON. */
static bool stage_two_literals()
{
    uint64_t entry = script_catalog_id("package-symbol", "project:package_compiler:1:LiteralBits:callable");
    for (int i = 0; i < (int)(sizeof literal_spellings / sizeof *literal_spellings);
         i++) {
        for (int sign = 0; sign < 2; sign++) {
            pin_value args[] = { text(literal_spellings[i]),
                                 (pin_value){ .type = PIN_BOOL, .b = sign } },
                      out;
            REQUIRE(
                compiler_fixture_call(&fixture, 1, PACKAGE, "LiteralBits", args, 2, COMPILE_BUDGET, &detached, &out)
            );
            size_t length, expected_length;
            char *json = document_json(args, 2, &length);
            char *expected = document_json(&out, 1, &expected_length);
            arena_free(&detached);
            bool called = compiler_host_invoke(host, entry, json, length, COMPILE_BUDGET, &result);
            bool same = called && result.flow_status == FLOW_OK &&
                        result.length == expected_length &&
                        !memcmp(result.json, expected, expected_length);
            if (!same) {
                fprintf(
                    stderr,
                    "package compiler stage two literal %s%s: %s\n",
                    sign ? "-" : "",
                    literal_spellings[i],
                    called ? result.json : result.diagnostic
                );
            }
            free(json);
            free(expected);
            free(result.json);
            result = (compiler_host_result){ 0 };
            REQUIRE(same);
        }
    }
    return true;
}
/* Whether stage two, loaded in `host`, compiles sample `s` as stage one does:
 * to the same closure, or to the same rejection. A closure is compared as
 * bytes, since its JSON output outgrows the host's projection limits. */
static bool stage_two_sample(const sample *s)
{
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(s, inputs);
    pin_value args[ARGUMENTS + 1];
    compiler_result out;
    REQUIRE(arguments_for(1, s->game, s->c_string_probe, inputs, count, args));
    args[ARGUMENTS] = linked_effects[s->game][s->c_string_probe][1];
    int argument_count = ARGUMENTS + s->host_effects;
    const char *entry_name = s->host_effects ? "CompileWithEffects" : "Compile";
    REQUIRE(compiler_call(1, entry_name, args, argument_count, COMPILE_BUDGET, &out));
    size_t length, expected_length;
    char *json = document_json(args, argument_count, &length);
    bool compiled = compiler_succeeded(&out);
    const pin_value outputs[] = { out.module, out.bytes, out.error };
    unsigned char *expected = compiled
                                ? closure_bytes(&out, &expected_length)
                                : (unsigned char *)document_json(outputs, 3, &expected_length);
    arena_free(&detached);
    bool same;
    if (compiled) {
        compiler_host_bytes again = { 0 };
        same = compiler_host_invoke_bytes(
                   host,
                   script_catalog_id("package-symbol", s->host_effects ? "project:package_compiler:1:CompileClosureWithEffects:callable" : "project:package_compiler:1:CompileClosure:callable"),
                   json,
                   length,
                   SELF_BUDGET,
                   COMPILER_HOST_MAX_OUTPUT_BYTES,
                   &again
               ) &&
               again.flow_status == FLOW_OK && again.length == expected_length &&
               !memcmp(again.bytes, expected, expected_length);
        if (!same) {
            fprintf(
                stderr,
                "package compiler stage two %s: %s\n",
                s->name,
                again.diagnostic
            );
        }
        free(again.bytes);
    } else {
        uint64_t entry = script_catalog_id(
            "package-symbol",
            s->host_effects
                ? "project:package_compiler:1:CompileWithEffects:callable"
                : "project:package_compiler:1:Compile:callable"
        );
        bool called = compiler_host_invoke(host, entry, json, length, SELF_BUDGET, &result);
        same = called && result.flow_status == FLOW_OK &&
               result.length == expected_length &&
               !memcmp(result.json, expected, expected_length);
        if (!same) {
            fprintf(
                stderr,
                "package compiler stage two %s: %s\n",
                s->name,
                called ? result.json : result.diagnostic
            );
        }
        free(result.json);
        result = (compiler_host_result){ 0 };
    }
    free(json);
    free(expected);
    return same;
}
static bool stage_two_samples()
{
    for (int i = 0; i < (int)SAMPLES; i++) {
        REQUIRE(stage_two_sample(&samples[i]));
    }
    return true;
}
/* Stage zero prepares stage one, which compiles its own packages, without
 * debugger points and watches, to a closure: stage two. Stage two, loaded by
 * the compiler host, compiles the same packages to the same bytes, every
 * sample as stage one does, and reads the listed floating literals as stage
 * one does. */
/* The functions of the package compiler's module `p` that copy their
 * arguments, other than those that run once per compile or per source, each
 * reported. */
static int copying_functions(const execution_module_plan *p)
{
    const char *const once[] = { "Compile",
                                 "CompileClosure",
                                 "CompileWithEffects",
                                 "CompileInput",
                                 "CompileClosureWithEffects",
                                 "CompileBehavior",
                                 "CompileBehaviorResolved",
                                 "CompileSources",
                                 "PromoteRecords",
                                 "DeclareSource",
                                 "CompileBehaviorsResolved",
                                 "BehaviorRejectionResolved",
                                 "CompileBehaviorsWithCatalog",
                                 "BehaviorRejectionWithCatalog",
                                 "CollectScalar",
                                 "Declare",
                                 "DeclarePackage" };
    int copying = 0;
    for (int i = 0; i < p->function_count; i++) {
        const behavior_function *body = p->functions[i].body;
        bool allowed = false;
        for (size_t j = 0; j < sizeof once / sizeof *once; j++) {
            char name[128];
            snprintf(name, sizeof name, "%s:1:%s:callable", PACKAGE, once[j]);
            allowed |= p->functions[i].id == script_catalog_id("package-symbol", name);
        }
        if (body && body->argument_sharing != BEHAVIOR_SHARING_SAFE && !allowed) {
            fprintf(
                stderr,
                "package compiler function %016llx copies its arguments\n",
                (unsigned long long)p->functions[i].id
            );
            copying++;
        }
    }
    return copying;
}
static const source_package_input bootstrap_files[] = { PACKAGE_COMPILER_SOURCE_INPUTS };
enum
{
    BOOTSTRAP_FILES = sizeof bootstrap_files / sizeof *bootstrap_files
};
static int bootstrap_inputs(source_package_input inputs[BOOTSTRAP_FILES])
{
    /* Compile's root package is its first file's: the package compiler's. */
    int count = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < BOOTSTRAP_FILES; i++) {
            bool own = !strncmp(bootstrap_files[i].path, "package_compiler/", 17);
            if (own == !pass) {
                inputs[count++] = bootstrap_files[i];
            }
        }
    }
    return count;
}
bool package_compiler_bootstrap_source_order(const char *inventory, size_t length)
{
    source_package_input inputs[BOOTSTRAP_FILES];
    int count = bootstrap_inputs(inputs);
    for (int i = 0; i < count; i++) {
        const char *end = memchr(inventory, '\n', length);
        size_t line_length = end ? (size_t)(end - inventory) : length;
        const char *path = inputs[i].path;
        if (line_length != strlen(path) || memcmp(inventory, path, line_length)) {
            return false;
        }
        size_t consumed = line_length + (end != NULL);
        inventory += consumed;
        length -= consumed;
    }
    return length == 0;
}
TEST_CASE(package_compiler_compiles_itself)
{
    REQUIRE(prepare());
    source_package_input inputs[BOOTSTRAP_FILES];
    int count = bootstrap_inputs(inputs);
    pin_value args[ARGUMENTS];
    compiler_result out;
    REQUIRE(arguments(1, false, inputs, count, args));
    /* The compiler is compiled without debugger points and watches, which
     * no one debugging a game steps through. */
    args[DEBUG_ARGUMENT].b = false;
    REQUIRE(compiler_call(1, "Compile", args, ARGUMENTS, SELF_BUDGET, &out));
    if (!compiler_succeeded(&out)) {
        pin_value message, line, column;
        get(compiler_error(&out), "message", &message);
        get(compiler_error(&out), "line", &line);
        get(compiler_error(&out), "column", &column);
        fprintf(
            stderr,
            "package compiler stage one rejected: %s at %d:%d\n",
            pin_text(message).bytes,
            line.i,
            column.i
        );
    }
    REQUIRE(compiler_succeeded(&out));
    size_t length, json_length;
    unsigned char *stage_two = closure_bytes(&out, &length);
    char *json = document_json(args, ARGUMENTS, &json_length);
    arena_free(&detached);
    char error[256];
    host = compiler_host_load(stage_two, length, TEST_COMPILED_BACKEND, error, sizeof error);
    if (!host) {
        fprintf(stderr, "self-hosted compiler load: %s\n", error);
    }
    REQUIRE(host);
    size_t target_length;
    char *target_json = compiler_host_target_json(host, &target_length);
    yyjson_doc *target = yyjson_read(target_json, target_length, 0);
    yyjson_doc *compiled_arguments = yyjson_read(json, json_length, 0);
    REQUIRE(target && compiled_arguments);
    bool target_equal = true;
    for (size_t i = 0; i < 6; i++) {
        yyjson_val *host_part = yyjson_arr_get(yyjson_doc_get_root(target), i);
        yyjson_val *argument_part = yyjson_arr_get(
            yyjson_doc_get_root(compiled_arguments),
            TARGET_ARGUMENT + i
        );
        bool same = yyjson_equals(host_part, argument_part);
        if (!same) {
            char *host_text = yyjson_val_write(host_part, 0, NULL);
            char *argument_text = yyjson_val_write(argument_part, 0, NULL);
            size_t at = 0;
            while (host_text[at] && argument_text[at] &&
                   host_text[at] == argument_text[at]) {
                at++;
            }
            size_t from = at > 120 ? at - 120 : 0;
            fprintf(
                stderr,
                "self-hosted target part %zu differs at byte %zu:\n host: "
                "%.240s\n args: %.240s\n",
                i,
                at,
                host_text + from,
                argument_text + from
            );
            free(host_text);
            free(argument_text);
        }
        target_equal &= same;
    }
    yyjson_doc_free(compiled_arguments);
    yyjson_doc_free(target);
    free(target_json);
    REQUIRE(target_equal);
    compiler_host_bytes again = { 0 };
    bool invoked = host &&
                   compiler_host_invoke_bytes(
                       host,
                       script_catalog_id("package-symbol", "project:package_compiler:1:CompileClosure:callable"),
                       json,
                       json_length,
                       SELF_BUDGET,
                       COMPILER_HOST_MAX_ARTIFACT_BYTES,
                       &again
                   );
    bool same = invoked && again.flow_status == FLOW_OK &&
                again.length == length && !memcmp(again.bytes, stage_two, length);
    if (!invoked) {
        fprintf(
            stderr,
            "package compiler stage two: %s%s\n",
            host ? "" : error,
            again.diagnostic
        );
    } else if (!again.length) {
        stage_two_rejection(json, json_length);
    } else if (!same) {
        size_t at = 0;
        while (at < length && at < again.length && again.bytes[at] == stage_two[at]) {
            at++;
        }
        fprintf(
            stderr,
            "package compiler stage two: %zu bytes, stage one %zu; first "
            "difference at %zu\n",
            again.length,
            length,
            at
        );
    }
    /* Stage one's build of the compiler shares arguments as stage zero's
     * does: a function that copies them made stage two compile itself 20
     * times slower. */
    int copying = copying_functions(&compiler_host_root(host)->plan);
    bool seeded = same && write_seed(stage_two, length, json, json_length);
    free(again.bytes);
    free(stage_two);
    free(json);
    REQUIRE(same);
    REQUIRE(copying == 0);
    REQUIRE(seeded);
    return stage_two_samples() && stage_two_literals();
}
TEST_CASE(package_compiler_reads_float_literals)
{
    REQUIRE(prepare());
    return literals_agree();
}
/* Behavior assets. Stage one compiles each against a game this test declares,
 * on both compiler backends; its module declares stage zero's identities and
 * plays as stage zero's does. Counter is the Counter story's behavior: a
 * retained state, and a Tick handler that increments it and writes it,
 * converted to a float, to a field of the owner's component. */
typedef struct
{
    const char *name, *source;
} behavior_sample;
;
static const char
    TEXT_STATE[] = "TextState :: behavior {\n"
                   "    label: string = \"seed\"\n"
                   "    Copy :: (value: string) -> string { return value; }\n"
                   "    on Tick :: () {\n"
                   "        if label == \"seed\" {\n"
                   "            label = Copy(\"a\\u0000b\")\n"
                   "            self.Spot.x = 1\n"
                   "        } else {\n"
                   "            label = Copy(\"seed\")\n"
                   "            self.Spot.x = 2\n"
                   "        }\n"
                   "    }\n}\n";
;
/* Sources stage one rejects, with its message. Where stage zero rejects one
 * too, stage one gives its message at its position. */
typedef struct
{
    const char *source, *message;
    bool stage_zero;
} behavior_rejection;
;
enum
{
    SPOT = 101,
    SPOT_COMPONENT = 901,
    QUERY_TARGETS = 902,
    TICK = 11,
    MOVE = 12,
    OWNER = 30,
    ENTITY = 700,
    OTHER_HANDLE = 701,
    SCENE_GLOBALS = 702,
    GET_TERRAIN = 800,
    GET_OTHER = 801,
    GET_RESOLVED = 802,
    GET_TARGET = 803,
    BEHAVIOR_ARGUMENTS = 17,
    TICKS = 5
};
typedef struct
{
    float x, y, w;
    uint64_t owner;
} spot;
static const type_field spot_fields[] = {
    { .name = "x",
      .type = PIN_F32,
      .type_name = "number",
      .offset = offsetof(spot, x),
      .size = sizeof(float) },
    { .name = "y",
      .type = PIN_F32,
      .type_name = "number",
      .offset = offsetof(spot, y),
      .size = sizeof(float) },
    { .name = "w",
      .type = PIN_F32,
      .type_name = "number",
      .offset = offsetof(spot, w),
      .size = sizeof(float) },
    { .name = "owner",
      .type = PIN_HANDLE,
      .type_name = "ecs.entity",
      .offset = offsetof(spot, owner),
      .size = sizeof(uint64_t) },
};
static const type_desc spot_type = { .name = "Spot",
                                     .size = sizeof(spot),
                                     .fields = spot_fields,
                                     .field_count = sizeof spot_fields /
                                                    sizeof *spot_fields };
static const script_catalog_field spot_members[] = {
    { .id = 811,
      .name = "x",
      .type = { .storage = PIN_F32 },
      .offset = offsetof(spot, x),
      .writable = true },
    { .id = 812,
      .name = "y",
      .type = { .storage = PIN_F32 },
      .offset = offsetof(spot, y),
      .writable = true },
    { .id = 813, .name = "w", .type = { .storage = PIN_F32 }, .offset = offsetof(spot, w) },
    { .id = 814,
      .name = "owner",
      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
      .offset = offsetof(spot, owner) },
    { .id = 815,
      .name = "resolved",
      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
      .read = GET_RESOLVED },
};
static const script_catalog_parameter delta_seconds = { .name = "DeltaSeconds",
                                                        .type = { .storage = PIN_F32 } };
static const script_catalog_parameter move_payload[] = {
    { .name = "x", .type = { .storage = PIN_F32 } },
    { .name = "y", .type = { .storage = PIN_F32 } },
};
static const script_catalog_parameter global_key = { .name = "key",
                                                     .type = { .storage = PIN_STR } };
static const script_catalog_parameter spot_argument = {
    .name = "spot",
    .type = { .storage = PIN_STRUCT, .nominal = SPOT }
};
static const script_catalog_parameter terrain_result = {
    .name = "Value",
    .type = { .storage = PIN_HANDLE, .nominal = ENTITY }
};
static const script_catalog_parameter other_result = {
    .name = "Value",
    .type = { .storage = PIN_HANDLE, .nominal = OTHER_HANDLE }
};
static const script_catalog_capability game_capabilities[] = {
    { .id = TICK,
      .name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &delta_seconds,
      .parameter_count = 1,
      .completion_outcome = -1 },
    { .id = MOVE,
      .name = "Move",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = move_payload,
      .parameter_count = 2,
      .completion_outcome = -1 },
    { .id = GET_TERRAIN,
      .name = "Fixture Terrain",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &global_key,
      .parameter_count = 1,
      .results = &terrain_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_READ,
      .completion_outcome = -1 },
    { .id = GET_OTHER,
      .name = "Fixture Other",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &global_key,
      .parameter_count = 1,
      .results = &other_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_READ,
      .completion_outcome = -1 },
    { .id = GET_TARGET,
      .name = "Fixture Target",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &global_key,
      .parameter_count = 1,
      .results = &terrain_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_READ,
      .completion_outcome = -1 },
    { .id = GET_RESOLVED,
      .name = "Fixture Resolved",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &spot_argument,
      .parameter_count = 1,
      .results = &terrain_result,
      .result_count = 1,
      .effects = SCRIPT_EFFECT_READ,
      .completion_outcome = -1 },
};
static const script_catalog_field scene_globals[] = {
    { .id = 820,
      .name = "Terrain",
      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
      .read = GET_TERRAIN,
      .read_key = "Terrain" },
    { .id = 821,
      .name = "Other",
      .type = { .storage = PIN_HANDLE, .nominal = OTHER_HANDLE },
      .read = GET_OTHER,
      .read_key = "Other" },
    { .id = 822,
      .name = "Target",
      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
      .read = GET_TARGET,
      .read_key = "Target" },
};
static const script_catalog_global game_globals[] = {
    { .id = OWNER, .name = "self", .type = { .storage = PIN_HANDLE, .nominal = ENTITY } },
    { .id = OWNER + 1, .name = "pace", .type = { .storage = PIN_F32 } },
    { .id = OWNER + 2,
      .name = "globals",
      .type = { .storage = PIN_FLOW, .nominal = SCENE_GLOBALS, .flags = SCRIPT_TYPE_NAMESPACE } },
};
/* The game: Spot, two nominal handle types, and keyed scene globals, with
 * Tick, Move, `self`, and the unsupported scalar global `pace`. */
static script_catalog_type game_types[4];
static script_catalog game_catalog()
{
    script_catalog_type *types = game_types;
    types[0] = (script_catalog_type){
        .id = SPOT,
        .name = spot_type.name,
        .type = { .storage = PIN_STRUCT, .nominal = SPOT },
        .fields = spot_members,
        .field_count = sizeof spot_members / sizeof *spot_members,
        .size = spot_type.size,
        .layout = type_layout_hash(&spot_type),
        .component_id = SPOT_COMPONENT,
        .component = true
    };
    types[1] = (script_catalog_type){ .id = ENTITY,
                                      .name = "ecs.entity",
                                      .type = { .storage = PIN_HANDLE, .nominal = ENTITY },
                                      .size = sizeof(uint64_t) };
    types[2] = (script_catalog_type){ .id = OTHER_HANDLE,
                                      .name = "test.other",
                                      .type = { .storage = PIN_HANDLE,
                                                .nominal = OTHER_HANDLE },
                                      .size = sizeof(uint64_t) };
    types[3] = (script_catalog_type){ .id = SCENE_GLOBALS,
                                      .name = "SceneGlobals",
                                      .type = { .storage = PIN_FLOW,
                                                .nominal = SCENE_GLOBALS,
                                                .flags = SCRIPT_TYPE_NAMESPACE },
                                      .fields = scene_globals,
                                      .field_count = sizeof scene_globals /
                                                     sizeof *scene_globals };
    return (script_catalog){ .types = types,
                             .type_count = sizeof game_types / sizeof *game_types,
                             .capabilities = game_capabilities,
                             .capability_count = sizeof game_capabilities /
                                                 sizeof *game_capabilities,
                             .globals = game_globals,
                             .global_count = sizeof game_globals / sizeof *game_globals,
                             .entity_type = ENTITY,
                             .tick_event = TICK };
}
/* The entity `globals.Target` names: the playing world's target. Fresh worlds
 * number their entities alike, so both players of a comparison share it. */
static uint64_t play_target;
/* The getters preserve the caller's typed result header. Equal payload bits
 * for the two handle types must never make them assignment-compatible. */
static const char *keyed_handle(
    const pin_value *arguments,
    pin_value *out,
    const char *name,
    const char *type_name,
    uint64_t handle
)
{
    pin_string key = pin_text(arguments[0]);
    if (key.length != strlen(name) || memcmp(key.bytes, name, key.length)) {
        return "Unexpected keyed global name";
    }
    if (out->type != PIN_HANDLE || out->container != PIN_CONTAINER_SINGLE ||
        !out->type_name || strcmp(out->type_name, type_name)) {
        return "Keyed global result lost its nominal type";
    }
    out->u64 = handle;
    return NULL;
}
static const char *terrain_global(
    const behavior_function_context *context,
    void *,
    const pin_value *arguments,
    pin_value *out
)
{
    return keyed_handle(arguments, out, "Terrain", "ecs.entity", context->owner);
}
static const char *target_global(
    const behavior_function_context *,
    void *,
    const pin_value *arguments,
    pin_value *out
)
{
    if (!play_target) {
        return "No world is playing a target";
    }
    return keyed_handle(arguments, out, "Target", "ecs.entity", play_target);
}
static const char *other_global(
    const behavior_function_context *context,
    void *,
    const pin_value *arguments,
    pin_value *out
)
{
    return keyed_handle(arguments, out, "Other", "test.other", context->owner);
}
static const char *resolved_owner(
    const behavior_function_context *,
    void *,
    const pin_value *arguments,
    pin_value *out
)
{
    const pin_value *receiver = &arguments[0];
    if (receiver->type != PIN_STRUCT ||
        receiver->container != PIN_CONTAINER_SINGLE || !receiver->ptr ||
        !receiver->type_name || strcmp(receiver->type_name, "Spot")) {
        return "Resolved getter requires a Spot";
    }
    if (out->type != PIN_HANDLE || out->container != PIN_CONTAINER_SINGLE ||
        !out->type_name || strcmp(out->type_name, "ecs.entity")) {
        return "Resolved getter result lost its nominal type";
    }
    out->u64 = ((const spot *)receiver->ptr)->owner;
    return NULL;
}
static const node_host_call terrain_call = { .context = terrain_global };
static const node_host_call other_call = { .context = other_global };
static const node_host_call target_call = { .context = target_global };
static const node_host_call resolved_call = { .context = resolved_owner };
static const node_access global_access = { .flags = NODE_EFFECT_PURE };
static const node_host_call *global_call(uint64_t id)
{
    return id == GET_TERRAIN  ? &terrain_call
         : id == GET_OTHER    ? &other_call
         : id == GET_TARGET   ? &target_call
         : id == GET_RESOLVED ? &resolved_call
                              : NULL;
}
static bool game_host(void *, uint64_t id, script_lower_host *out)
{
    const node_host_call *call = global_call(id);
    if (!call) {
        return false;
    }
    *out = (script_lower_host){ .call = call, .access = &global_access };
    return true;
}
static bool game_global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != OWNER) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
/* Stage zero's module of `source`. */
static pin_value u64(uint64_t value)
{
    return (pin_value){ .type = PIN_U64, .u64 = value };
}
static pin_value flag(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
/* The index of the type `id` among the catalog's, -1 for none. */
static int type_index(const script_catalog *catalog, uint64_t id)
{
    for (int i = 0; id && i < catalog->type_count; i++) {
        if (catalog->types[i].id == id) {
            return i;
        }
    }
    return -1;
}
/* Behavior targets append these fixture getters after the production host
 * natives. Package compilation and bootstrap keep their original tables. */
static bool behavior_natives(int b, pin_value tables[3])
{
    script_catalog catalog = game_catalog();
    pin_value *functions = NULL, *ports = NULL, *identities = NULL;
    pin_array *before[] = { tables[0].ptr, tables[1].ptr, tables[2].ptr };
    for (int i = 0; i < before[0]->count; i++) {
        apush(functions, pin_array_get(before[0], i));
    }
    for (int i = 0; i < before[1]->count; i++) {
        apush(ports, pin_array_get(before[1], i));
    }
    for (int i = 0; i < before[2]->count; i++) {
        apush(identities, pin_array_get(before[2], i));
    }
    for (int i = 0; i < catalog.capability_count; i++) {
        const script_catalog_capability *c = &catalog.capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_FUNCTION) {
            continue;
        }
        pin_value function = row(b, "Native");
        REQUIRE(put_value(&function, "name", text(c->name)));
        REQUIRE(put(&function, "first", acount(ports)));
        REQUIRE(put(&function, "parameters", c->parameter_count));
        REQUIRE(put(&function, "results", c->result_count));
        REQUIRE(put_value(&function, "variadic", flag(false)));
        apush(functions, function);
        for (int j = 0; j < c->parameter_count + c->result_count; j++) {
            const script_catalog_parameter *declared = j < c->parameter_count
                                                         ? &c->parameters[j]
                                                         : &c->results[j - c->parameter_count];
            pin_value port = row(b, "Port");
            REQUIRE(put_value(&port, "name", text(declared->name)));
            REQUIRE(put(&port, "storage", compiler_fixture_port_storage(declared->type)));
            REQUIRE(put(&port, "container", (int)declared->type.container));
            REQUIRE(put_value(&port, "c_string", flag(false)));
            apush(ports, port);
        }
        apush(identities, u64(c->id));
        apush(identities, u64(script_catalog_capability_abi(&catalog, c)));
    }
    tables[0] = rows(functions, acount(functions), PIN_STRUCT, tables[0].record);
    tables[1] = rows(ports, acount(ports), PIN_STRUCT, tables[1].record);
    tables[2] = rows(identities, acount(identities), PIN_U64, NULL);
    afree(functions);
    afree(ports);
    afree(identities);
    return true;
}
static int game_native_index(const script_catalog *catalog, uint64_t id, int first)
{
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_FUNCTION) {
            continue;
        }
        if (c->id == id) {
            return first;
        }
        first++;
    }
    return -1;
}
/* CompileBehavior's game arguments, as the game target exports them: each
 * event and its payload fields, each type and its fields, the entity type,
 * no operators that combine records, the globals, and no record makes. */
static bool game_arguments(int b, int first_native, pin_value *out)
{
    script_catalog catalog = game_catalog();
    pin_value *declared = NULL, *payloads = NULL, *types = NULL, *fields = NULL;
    for (int i = 0; i < catalog.capability_count; i++) {
        const script_catalog_capability *c = &catalog.capabilities[i];
        if (c->kind != SCRIPT_CAPABILITY_EVENT) {
            continue;
        }
        pin_value event = row(b, "GameEvent");
        REQUIRE(put_value(&event, "name", text(c->name)));
        REQUIRE(put_value(&event, "id", u64(c->id)));
        REQUIRE(put_value(&event, "abi", u64(script_catalog_capability_abi(&catalog, c))));
        REQUIRE(put(&event, "first", acount(payloads)));
        REQUIRE(put(&event, "count", c->parameter_count));
        apush(declared, event);
        for (int j = 0; j < c->parameter_count; j++) {
            pin_value payload = row(b, "GameEventField");
            REQUIRE(put_value(&payload, "name", text(c->parameters[j].name)));
            REQUIRE(put(&payload, "storage", (int)c->parameters[j].type.storage));
            REQUIRE(put_value(&payload, "nominal", u64(c->parameters[j].type.nominal)));
            REQUIRE(put_value(&payload, "has_default", flag(c->parameters[j].has_default)));
            REQUIRE(put_value(&payload, "default_owner", flag(c->parameters[j].default_owner)));
            apush(payloads, payload);
        }
    }
    for (int i = 0; i < catalog.type_count; i++) {
        const script_catalog_type *t = &catalog.types[i];
        pin_value type = row(b, "GameType");
        REQUIRE(put_value(&type, "required", flag(!(t->type.flags & SCRIPT_TYPE_NAMESPACE))));
        REQUIRE(put(&type, "underlying", type_index(&catalog, t->underlying)));
        REQUIRE(put_value(&type, "name", text(t->name)));
        REQUIRE(put_value(&type, "id", u64(t->id)));
        REQUIRE(put_value(&type, "layout", u64(t->layout)));
        REQUIRE(put(&type, "storage", (int)t->type.storage));
        REQUIRE(put(&type, "size", (int)t->size));
        REQUIRE(put_value(&type, "component", u64(t->component ? t->component_id : 0)));
        REQUIRE(put(&type, "first", acount(fields)));
        REQUIRE(put(&type, "count", t->field_count));
        REQUIRE(put_value(&type, "relationship", flag(false)));
        REQUIRE(put(&type, "traits", 0));
        REQUIRE(
            put_value(&type, "error", flag(script_catalog_type_contains_error(&catalog, t->type)))
        );
        apush(types, type);
        for (int j = 0; j < t->field_count; j++) {
            pin_value field = row(b, "GameField");
            REQUIRE(put_value(&field, "name", text(t->fields[j].name)));
            REQUIRE(put(&field, "storage", (int)t->fields[j].type.storage));
            REQUIRE(put(&field, "offset", (int)t->fields[j].offset));
            REQUIRE(put_value(&field, "writable", flag(t->fields[j].writable)));
            REQUIRE(put_value(&field, "c_string", flag(t->fields[j].c_string)));
            REQUIRE(put(&field, "record", type_index(&catalog, t->fields[j].type.nominal)));
            REQUIRE(put_value(&field, "constant", flag(t->fields[j].constant)));
            REQUIRE(
                put(&field, "read", game_native_index(&catalog, t->fields[j].read, first_native))
            );
            REQUIRE(put_value(
                &field,
                "read_key",
                text(t->fields[j].read_key ? t->fields[j].read_key : "")
            ));
            apush(fields, field);
        }
    }
    out[0] = rows(declared, acount(declared), PIN_STRUCT, declared[0].record);
    out[1] = rows(payloads, acount(payloads), PIN_STRUCT, payloads[0].record);
    out[2] = rows(types, acount(types), PIN_STRUCT, types[0].record);
    out[3] = rows(fields, acount(fields), PIN_STRUCT, fields[0].record);
    out[4] = u64(catalog.entity_type);
    out[5] = rows(
        NULL,
        0,
        PIN_STRUCT,
        compiler_fixture_record(&fixture, b, PACKAGE, "NumericOperator", &input)
    );
    pin_value *globals = NULL;
    for (int i = 0; i < catalog.global_count; i++) {
        const script_catalog_global *g = &catalog.globals[i];
        pin_value global = row(b, "GameGlobal");
        REQUIRE(put_value(&global, "name", text(g->name)));
        REQUIRE(put(&global, "storage", (int)g->type.storage));
        REQUIRE(put(&global, "record", type_index(&catalog, g->type.nominal)));
        apush(globals, global);
    }
    out[6] = rows(globals, acount(globals), PIN_STRUCT, globals[0].record);
    out[7] = rows(NULL, 0, PIN_STRUCT, compiler_fixture_record(&fixture, b, PACKAGE, "NativeMake", &input));
    out[8] = rows(NULL, 0, PIN_I32, NULL);
    afree(declared);
    afree(payloads);
    afree(types);
    afree(fields);
    afree(globals);
    return true;
}
static bool catalog_compile(
    int b,
    const script_catalog *catalog,
    const char *identity,
    const char *source,
    const char *entry,
    pin_value *out
);
/* Stage one's prepared closure for `source`, using the production catalog export. */
static bool compile_behavior(int b, const char *identity, const char *source, pin_value *out)
{
    script_catalog catalog = game_catalog();
    return catalog_compile(b, &catalog, identity, source, "CompileBehaviorsWithCatalog", out);
}
/* The game's types and events, as the player links them. */
static bool link_game_type(void *user, uint64_t id, execution_prepared_type *out)
{
    const script_catalog *catalog = user;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        if (t->id == id) {
            *out = (execution_prepared_type){ .name = t->name,
                                              .type = t->id == SPOT ? &spot_type : NULL,
                                              .storage = t->type.storage,
                                              .size = t->size,
                                              .layout = t->layout };
            return true;
        }
    }
    return false;
}
static bool link_game_event(void *user, uint64_t id, uint64_t abi)
{
    const script_catalog *catalog = user;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        if (c->kind == SCRIPT_CAPABILITY_EVENT && c->id == id) {
            return script_catalog_capability_abi(catalog, c) == abi;
        }
    }
    return false;
}
static bool link_game_host(void *user, uint64_t id, execution_prepared_host *out)
{
    const node_host_call *call = global_call(id);
    const script_catalog *catalog = user;
    const script_catalog_capability *capability = script_catalog_capability_by_id(catalog, id);
    if (!call || !capability) {
        return false;
    }
    *out = (execution_prepared_host){
        .call = call,
        .access = &global_access,
        .abi = script_catalog_capability_abi(catalog, capability)
    };
    return true;
}
/* The module stage one compiles `source` to on backend `b`, linked against
 * the game. */
static bool stage_one_behavior(int b, const char *identity, const char *source, execution_module **module)
{
    *module = NULL;
    script_catalog catalog = game_catalog();
    pin_value out;
    REQUIRE(compile_behavior(b, identity, source, &out));
    REQUIRE(out.type == PIN_U8 && out.container == PIN_CONTAINER_ARRAY);
    const pin_array *array = out.ptr;
    if (!array || !array->count) {
        arena_free(&detached);
        REQUIRE(
            catalog_compile(b, &catalog, identity, source, "BehaviorRejectionWithCatalog", &out)
        );
        REQUIRE(out.type == PIN_STR);
        fprintf(stderr, "stage one behavior rejected: %s\n", pin_text(out).bytes);
        arena_free(&detached);
        return false;
    }
    size_t length = (size_t)array->count;
    unsigned char *bytes = xmalloc(length);
    for (size_t i = 0; i < length; i++) {
        bytes[i] = array->items[i].u8;
    }
    arena_free(&detached);
    char error[256] = { 0 };
    int count = 0;
    uint64_t root = 0;
    package_module *modules = compiler_artifact_rows(bytes, length, &count, &root, error, sizeof error);
    if (!modules || count != 1 || root != modules[0].id) {
        fprintf(stderr, "stage one behavior %s closure: %s\n", identity, error);
        compiler_artifact_rows_free(modules, count);
        free(bytes);
        return false;
    }
    execution_prepared_linker linker = { .user = &catalog,
                                         .host = link_game_host,
                                         .type = link_game_type,
                                         .event = link_game_event };
    *module = execution_prepared_read(
        modules[0].data,
        modules[0].length,
        &linker,
        b ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    if (!*module) {
        fprintf(stderr, "stage one behavior %s load: %s\n", identity, error);
    }
    compiler_artifact_rows_free(modules, count);
    free(bytes);
    return *module != NULL;
}
/* A behavior module playing in its own world, on one owner holding a Spot,
 * beside the target holding another. */
typedef struct
{
    ecs_world *world;
    execution_program *program;
    execution_instance *instance;
    ecs_entity owner, target;
} player;
static bool play(execution_module *module, player *out)
{
    out->world = ecs_world_new();
    const ecs_component_desc component = { .id = SPOT_COMPONENT,
                                           .key = "test.spot",
                                           .name = spot_type.name,
                                           .element_size = sizeof(spot),
                                           .type = &spot_type };
    REQUIRE(ecs_world_register_component(out->world, &component, NULL) == ECS_OK);
    for (int i = 0; i < module->plan.query_count; i++) {
        if (module->plan.queries[i].kind == EXECUTION_QUERY_GENERATOR &&
            module->plan.queries[i].component == QUERY_TARGETS) {
            const ecs_relationship_desc targets = { .id = QUERY_TARGETS,
                                                    .key = "test.query-targets",
                                                    .name = "Targets" };
            REQUIRE(ecs_world_register_relationship(out->world, &targets, NULL) == ECS_OK);
            break;
        }
    }
    out->program = execution_program_new(module, out->world, NULL);
    REQUIRE(out->program);
    out->owner = ecs_world_create_entity(out->world);
    spot initial;
    memset(&initial, 0, sizeof initial);
    initial.w = 9;
    initial.owner = ecs_entity_to_u64(out->owner);
    REQUIRE(ecs_world_record_add(out->world, out->owner, SPOT_COMPONENT, &initial, 0) == ECS_OK);
    out->target = ecs_world_create_entity(out->world);
    uint64_t target = ecs_entity_to_u64(out->target);
    REQUIRE(target != initial.owner && (!play_target || play_target == target));
    play_target = target;
    spot aimed;
    memset(&aimed, 0, sizeof aimed);
    aimed.x = 4;
    aimed.y = 6;
    aimed.w = 5;
    aimed.owner = target;
    REQUIRE(ecs_world_record_add(out->world, out->target, SPOT_COMPONENT, &aimed, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(out->world) == ECS_OK);
    if (!module->plan.query_variable_count) {
        out->instance = execution_program_attach(out->program, ecs_entity_to_u64(out->owner));
        REQUIRE(out->instance);
    }
    return true;
}
static void stop(player *p)
{
    play_target = 0;
    execution_program_free(p->program);
    ecs_world_free(p->world);
    *p = (player){ 0 };
}
/* Stage one's module declares what stage zero's does: the module, its owner,
 * types, services, states, events and handlers, with the same identities,
 * ABIs, kinds and effects. */
static bool same_declarations(const execution_module *expected, const execution_module *actual)
{
    const execution_module_plan *e = &expected->plan, *a = &actual->plan;
    REQUIRE(a->id == e->id && a->owner_type == e->owner_type);
    REQUIRE(a->type_count == e->type_count);
    for (int i = 0; i < e->type_count; i++) {
        REQUIRE(
            a->types[i].id == e->types[i].id &&
            a->types[i].layout == e->types[i].layout
        );
        REQUIRE(
            a->types[i].storage == e->types[i].storage &&
            a->types[i].size == e->types[i].size
        );
        REQUIRE(
            !strcmp(a->types[i].name, e->types[i].name) &&
            !a->types[i].record == !e->types[i].record
        );
    }
    REQUIRE(a->service_count == e->service_count);
    for (int i = 0; i < e->service_count; i++) {
        REQUIRE(!strcmp(a->services[i], e->services[i]));
    }
    REQUIRE(a->state_count == e->state_count);
    for (int i = 0; i < e->state_count; i++) {
        REQUIRE(a->states[i].id == e->states[i].id);
        REQUIRE(pin_value_equal(&a->states[i].initial, &e->states[i].initial));
        REQUIRE(!strcmp(
            a->states[i].initial.type_name ? a->states[i].initial.type_name : "",
            e->states[i].initial.type_name ? e->states[i].initial.type_name : ""
        ));
    }
    REQUIRE(a->event_count == e->event_count);
    for (int i = 0; i < e->event_count; i++) {
        const execution_module_event_declaration *x = &e->events[i],
                                                 *y = &a->events[i];
        REQUIRE(y->id == x->id && y->abi == x->abi && !strcmp(y->name, x->name));
        REQUIRE(y->field_count == x->field_count);
        for (int j = 0; j < x->field_count; j++) {
            REQUIRE(
                !strcmp(y->fields[j].name, x->fields[j].name) &&
                y->fields[j].nominal == x->fields[j].nominal
            );
            REQUIRE(y->fields[j].has_default == x->fields[j].has_default);
            REQUIRE(y->fields[j].default_owner == x->fields[j].default_owner);
            REQUIRE(y->fields[j].initial.type == x->fields[j].initial.type);
        }
    }
    REQUIRE(a->function_count == e->function_count);
    for (int i = 0; i < e->function_count; i++) {
        const execution_module_function *x = &e->functions[i], *y = &a->functions[i];
        REQUIRE(y->id == x->id && y->event == x->event && y->event_abi == x->event_abi);
        if (y->kind != x->kind || y->effects != x->effects) {
            fprintf(
                stderr,
                "function %d (%s): native kind/effects %d/%u, authored %d/%u\n",
                i,
                x->entry ? x->entry : "handler",
                x->kind,
                (unsigned)x->effects,
                y->kind,
                (unsigned)y->effects
            );
        }
        REQUIRE(y->kind == x->kind && y->effects == x->effects);
        REQUIRE(!y->event_name == !x->event_name && !y->entry == !x->entry);
        REQUIRE(!x->event_name || !strcmp(y->event_name, x->event_name));
        REQUIRE(y->body->argument_count == x->body->argument_count);
        REQUIRE(y->body->result_count == x->body->result_count);
        for (int j = 0; j < x->body->result_count; j++) {
            const pin_value *wanted = &x->body->result_defaults[j];
            const pin_value *got = &y->body->result_defaults[j];
            REQUIRE(got->type == wanted->type && got->container == wanted->container);
            REQUIRE(!strcmp(
                got->type_name ? got->type_name : "",
                wanted->type_name ? wanted->type_name : ""
            ));
            /* A native record frozen into one side matches the catalog
             * layout the other names. */
            REQUIRE(pin_value_layout_same(got, wanted));
        }
        REQUIRE(y->body->variable_count == x->body->variable_count);
        REQUIRE(y->body->component_access == x->body->component_access);
        REQUIRE(acount(y->body->components) == acount(x->body->components));
        for (int j = 0; j < acount(x->body->components); j++) {
            REQUIRE(y->body->components[j].id == x->body->components[j].id);
        }
    }
    return true;
}
}
}
}
/* Production catalog export must carry nominal native ports all the way to
 * prepared host banks. SpotAlias shares Spot's layout; OtherSpot does not
 * share its identity. An unsupported native remains harmless until selected. */
enum
{
    CATALOG_ALIAS = 710,
    CATALOG_OTHER = 711,
    CATALOG_ECHO = 5100,
    CATALOG_ENTITY,
    CATALOG_PAIR,
    CATALOG_ALIASED,
    CATALOG_SEED,
    CATALOG_CHECK,
    CATALOG_OTHER_RESULT,
    CATALOG_UNSUPPORTED
};
;
;
;
;
;
;
;
;
;
}
;
}
}
}
}
}
}
}
;
}
}
}
/* Decode the production JSON against the actual authored entry signature.
 * No test-side table reconstructs or repairs its port metadata. */
static bool catalog_compile_sources_native(
    int b,
    const script_catalog *catalog,
    const native_capabilities *native,
    const char *const *identities,
    const char *const *sources,
    int source_count,
    const char *entry,
    pin_value *out
)
{
    size_t length;
    char *json = compiler_behavior_catalog_input_json(catalog, native, identities, sources, source_count, &length);
    yyjson_doc *parsed = yyjson_read(json, length, YYJSON_READ_ALLOW_INVALID_UNICODE);
    REQUIRE(parsed);
    av_doc *doc = av_doc_new();
    av_value *values = av_from_json(doc, yyjson_doc_get_root(parsed));
    const authoring_package *package = compiler_fixture_package(&fixture, b, PACKAGE);
    uint64_t id = compiler_fixture_export(&fixture, b, PACKAGE, entry);
    const behavior_function *function = NULL;
    for (int i = 0; i < package->module->plan.function_count; i++) {
        if (package->module->plan.functions[i].id == id) {
            function = package->module->plan.functions[i].body;
        }
    }
    REQUIRE(function);
    int count;
    const pin_value *declarations = behavior_function_arguments(function, &count);
    REQUIRE(count == 18 && yyjson_arr_size(yyjson_doc_get_root(parsed)) == 18);
    pin_value args[18];
    char error[256];
    for (int i = 0; i < count; i++) {
        int status = pin_value_read_document(
            &input,
            declarations[i],
            av_arr_get(values, (size_t)i),
            &args[i],
            error,
            sizeof error
        );
        if (status) {
            fprintf(stderr, "catalog compiler argument %d: %s\n", i, error);
        }
        REQUIRE(!status);
    }
    yyjson_doc_free(parsed);
    av_doc_free(doc);
    free(json);
    return compiler_fixture_call(&fixture, b, PACKAGE, entry, args, count, COMPILE_BUDGET, &detached, out);
}
static bool catalog_compile_sources(
    int b,
    const script_catalog *catalog,
    const char *const *identities,
    const char *const *sources,
    int source_count,
    const char *entry,
    pin_value *out
)
{
    return catalog_compile_sources_native(b, catalog, NULL, identities, sources, source_count, entry, out);
}
static bool catalog_compile_native(
    int b,
    const script_catalog *catalog,
    const native_capabilities *native,
    const char *identity,
    const char *source,
    const char *entry,
    pin_value *out
)
{
    return catalog_compile_sources_native(b, catalog, native, &identity, &source, 1, entry, out);
}
static bool catalog_compile(
    int b,
    const script_catalog *catalog,
    const char *identity,
    const char *source,
    const char *entry,
    pin_value *out
)
{
    return catalog_compile_native(b, catalog, NULL, identity, source, entry, out);
}
typedef struct
{
    float x, y, z;
} native_math_vector;
;
;
;
}
;
typedef struct
{
    native_capabilities *native;
    script_syntax *syntax;
    script_analysis *analysis;
    execution_module_plan *plan;
    execution_module *expected[2], *actual;
    unsigned char *bytes;
    package_module *modules;
    int module_count;
} native_math_fixture;
}
}
}
enum
{
    CATALOG_SELECT_SPOT = 6100,
    CATALOG_SELECT_ENTITY,
    CATALOG_COLLISION,
    CATALOG_INFERRED,
    CATALOG_MAKE
};
;
}
}
}
}
;
}
}
}
/* Same native result type, different output names and input types. The x
 * output hides a field once; its next x is the ordinary float field. */
static const char *library_alias_native(void *, const pin_value *arguments, pin_value *results)
{
    if (arguments[0].type != PIN_I32 || results[0].type != PIN_I32) {
        return "canonical library native headers differ";
    }
    library_alias_calls[0]++;
    results[0].i = arguments[0].i + 100;
    return NULL;
}
static const char *library_alias_source(void *, const pin_value *arguments, pin_value *results)
{
    if (arguments[0].type != PIN_I32 || results[0].type != PIN_I32) {
        return "source alias native headers differ";
    }
    library_alias_calls[1]++;
    results[0].i = arguments[0].i + 200;
    return NULL;
}
static const node_host_call LIBRARY_ALIAS_CALLS[] = {
    { .invoke = library_alias_native },
    { .invoke = library_alias_source }
};
static bool library_host(void *user, uint64_t id, script_lower_host *out)
{
    if (game_host(user, id, out)) {
        return true;
    }
    for (int i = 0; i < NATIVE_COUNT(LIBRARY_ALIAS_DEFINITIONS); i++) {
        if (id == native_capability_definition_id(&LIBRARY_ALIAS_DEFINITIONS[i])) {
            *out = (script_lower_host){ .call = &LIBRARY_ALIAS_CALLS[i],
                                        .access = &CSTRING_ACCESS };
            return true;
        }
    }
    return false;
}
static bool link_library_host(void *user, uint64_t id, execution_prepared_host *out)
{
    const script_catalog *catalog = user;
    script_lower_host linked;
    const script_catalog_capability *capability = script_catalog_capability_by_id(catalog, id);
    if (!capability || !library_host(user, id, &linked)) {
        return false;
    }
    *out = (execution_prepared_host){
        .call = linked.call,
        .access = linked.access,
        .abi = script_catalog_capability_abi(catalog, capability)
    };
    return true;
}
static bool library_plays(execution_module *expected, execution_module *actual, bool compiled)
{
    player e = { 0 }, a = { 0 };
    REQUIRE(play(expected, &e) && play(actual, &a));
    const uint64_t label = script_catalog_id("symbol", "behaviors/Caller.chibi:1:label");
    pin_value delta = { .type = PIN_F32, .f = 1.0f / 60.0f };
    for (int tick = 1; tick <= TICKS; tick++) {
        REQUIRE(execution_instance_dispatch(e.instance, TICK, &delta, 1, 0, 0, 10000) == FLOW_OK);
        behavior_run_scope runs = { 0 };
        behavior_run_scope_begin(&runs);
        int status = execution_instance_dispatch(a.instance, TICK, &delta, 1, 0, 0, 10000);
        behavior_run_scope_end(&runs);
        REQUIRE(status == FLOW_OK && !runs.counts.overflow);
        if (compiled) {
            REQUIRE(
                TEST_COMPILED_BACKEND == BEHAVIOR_WASM ? runs.counts.wasm_entries > 0
                                                       : runs.counts.sljit_entries > 0
            );
            REQUIRE(!runs.counts.bytecode_entries);
        }
        const spot *left = ecs_component_data_const(e.world, e.owner, SPOT_COMPONENT);
        const spot *right = ecs_component_data_const(a.world, a.owner, SPOT_COMPONENT);
        REQUIRE(left && right && left->x == 8 * tick && right->x == left->x);
        REQUIRE(left->y == 3.5f * tick && right->y == left->y);
        const pin_value *x = execution_instance_state(e.instance, label);
        const pin_value *y = execution_instance_state(a.instance, label);
        REQUIRE(
            x && y &&
            pin_text_equal(pin_text(*x), (pin_string){ .bytes = "a\0b", .length = 3 })
        );
        REQUIRE(pin_text_equal(pin_text(*x), pin_text(*y)));
    }
    stop(&e);
    stop(&a);
    return true;
}
/* A caller retains exactly the imported generation. Replacing a called
 * library rejects; replacing an unused library remains valid. */
static bool library_generations(
    const script_catalog *catalog,
    const char *const *identities,
    const char *const *sources,
    const unsigned char *bytes,
    size_t length
)
{
    const char *amount = strstr(sources[2], "return n;");
    REQUIRE(amount);
    char *leaf = xmalloc(strlen(sources[2]) + 16);
    snprintf(
        leaf,
        strlen(sources[2]) + 16,
        "%.*sreturn n + 1.0;%s",
        (int)(amount - sources[2]),
        sources[2],
        amount + strlen("return n;")
    );
    const char *changed[] = { sources[0], sources[1], leaf, "Unused :: library { Answer :: () -> int { return 20; } }" };
    pin_value out;
    REQUIRE(
        catalog_compile_sources(1, catalog, identities, changed, 4, "CompileBehaviorsWithCatalog", &out)
    );
    free(leaf);
    const pin_array *array = out.ptr;
    REQUIRE(array && array->count);
    size_t next_length = (size_t)array->count;
    unsigned char *next = xmalloc(next_length);
    for (size_t i = 0; i < next_length; i++) {
        next[i] = array->items[i].u8;
    }
    arena_free(&detached);
    char error[256];
    int before_count, after_count;
    uint64_t before_root, after_root;
    package_module *before = compiler_artifact_rows(bytes, length, &before_count, &before_root, error, sizeof error);
    package_module *after = compiler_artifact_rows(next, next_length, &after_count, &after_root, error, sizeof error);
    REQUIRE(before && after && before_count == 4 && after_count == 4 && before_root == after_root);
    execution_prepared_linker linker = { .user = (void *)catalog,
                                         .type = link_game_type,
                                         .host = link_library_host,
                                         .event = link_game_event };
    for (int replaced = 2; replaced <= 3; replaced++) {
        package_module mixed[4];
        memcpy(mixed, before, sizeof mixed);
        bool swapped = false;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (!strcmp(mixed[i].name, identities[replaced]) &&
                    mixed[i].id == after[j].id) {
                    mixed[i].data = after[j].data;
                    mixed[i].length = after[j].length;
                    swapped = true;
                }
            }
        }
        REQUIRE(swapped);
        size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
        package_collection *loaded = package_read_budget(
            mixed,
            4,
            &linker,
            NULL,
            BEHAVIOR_BYTECODE,
            &remaining,
            error,
            sizeof error
        );
        REQUIRE((loaded != NULL) == (replaced == 3));
        if (loaded) {
            package_collection_free(loaded);
        }
    }
    package_module missing[3];
    int kept = 0;
    for (int i = 0; i < 4; i++) {
        if (strcmp(before[i].name, identities[2])) {
            missing[kept++] = before[i];
        }
    }
    REQUIRE(kept == 3);
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    package_collection *loaded = package_read_budget(
        missing,
        3,
        &linker,
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    REQUIRE(!loaded);
    compiler_artifact_rows_free(before, before_count);
    compiler_artifact_rows_free(after, after_count);
    free(next);
    return true;
}
typedef struct
{
    const char *identity;
    int observed, first_calls, second_calls;
} library_result;
static bool library_result_plays(
    execution_module *expected,
    execution_module *actual,
    bool compiled,
    const library_result *result
)
{
    execution_module *modules[] = { expected, actual };
    char state[256];
    snprintf(state, sizeof state, "%s:1:observed", result->identity);
    uint64_t id = script_catalog_id("symbol", state);
    for (int i = 0; i < 2; i++) {
        memset(library_alias_calls, 0, sizeof library_alias_calls);
        player p = { 0 };
        REQUIRE(play(modules[i], &p));
        pin_value delta = { .type = PIN_F32, .f = 1.0f / 60.0f };
        behavior_run_scope runs = { 0 };
        behavior_run_scope_begin(&runs);
        int status = execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, 0, 10000);
        behavior_run_scope_end(&runs);
        const pin_value *value = execution_instance_state(p.instance, id);
        bool valid = status == FLOW_OK && value && value->type == PIN_I32 &&
                     value->i == result->observed &&
                     library_alias_calls[0] == result->first_calls &&
                     library_alias_calls[1] == result->second_calls;
        if (!valid) {
            const execution_program_failure *failure = execution_program_error(p.program);
            fprintf(
                stderr,
                "library runtime %s %s: status%d value%d calls%d/%d error%s\n",
                i ? "selfhost" : "C",
                result->identity,
                status,
                value && value->type == PIN_I32 ? value->i : -1,
                library_alias_calls[0],
                library_alias_calls[1],
                failure ? failure->message : "none"
            );
        }
        if (i && compiled) {
            REQUIRE(
                TEST_COMPILED_BACKEND == BEHAVIOR_WASM ? runs.counts.wasm_entries > 0
                                                       : runs.counts.sljit_entries > 0
            );
            REQUIRE(!runs.counts.bytecode_entries);
        }
        stop(&p);
        REQUIRE(valid);
    }
    return true;
}
static bool libraries_with_results_catalog(
    const script_catalog *provided,
    const char *const *identities,
    const char *const *sources,
    int count,
    bool playing,
    bool namespace_native,
    const library_result *results,
    int result_count
)
{
    char root[] = "/tmp/chibi-compiler-libraries-XXXXXX";
    REQUIRE(mkdtemp(root));
    source_catalog_edit *edits = NULL;
    char (*paths)[256] = xmalloc((size_t)count * sizeof *paths);
    for (int i = 0; i < count; i++) {
        snprintf(paths[i], sizeof paths[i], "%s/%s", root, identities[i]);
        apush(edits, ((source_catalog_edit){ .path = paths[i], .text = sources[i] }));
    }
    script_catalog native = provided ? *provided : game_catalog();
    script_catalog_capability
        capabilities[sizeof game_capabilities / sizeof *game_capabilities + NATIVE_COUNT(LIBRARY_ALIAS_DEFINITIONS)];
    char definition_error[256];
    const script_catalog definition_base = { 0 };
    native_definition_catalog *definitions = native_definition_catalog_new(
        &definition_base,
        LIBRARY_ALIAS_DEFINITIONS,
        NATIVE_COUNT(LIBRARY_ALIAS_DEFINITIONS),
        NULL,
        0,
        definition_error,
        sizeof definition_error
    );
    REQUIRE(definitions);
    const script_catalog *aliases = native_definition_catalog_view(definitions);
    if (namespace_native) {
        memcpy(capabilities, game_capabilities, sizeof game_capabilities);
        for (int i = 0; i < native.capability_count; i++) {
            if (capabilities[i].id == GET_TERRAIN) {
                capabilities[i].name = "Leaf.Native";
                capabilities[i].qualified_name = "Leaf.Native";
            }
        }
        for (int i = 0; i < aliases->capability_count; i++) {
            capabilities[native.capability_count++] = aliases->capabilities[i];
        }
        native.capabilities = capabilities;
    }
    source_catalog *snapshot = source_catalog_replaced(root, &native, edits, count);
    afree(edits);
    free(paths);
    for (int i = 0; i < source_catalog_diagnostic_count(snapshot); i++) {
        fprintf(
            stderr,
            "library catalog: %s\n",
            source_catalog_diagnostic_at(snapshot, i)->message
        );
    }
    REQUIRE(!source_catalog_diagnostic_count(snapshot));
    script_lower_bindings bindings = { .host = library_host, .global = game_global };
    authoring_sources *oracle = authoring_sources_prepare_catalog(snapshot, &bindings, BEHAVIOR_BYTECODE);
    for (int i = 0; i < authoring_sources_count(oracle); i++) {
        const authoring_source *source = authoring_sources_at(oracle, i);
        for (int j = 0; j < source->diagnostic_count; j++) {
            fprintf(
                stderr,
                "library oracle %s: %s\n",
                source->path,
                source->diagnostics[j].message
            );
        }
    }
    REQUIRE(authoring_sources_valid(oracle));
    const script_catalog *catalog = authoring_sources_catalog(oracle);
    for (int b = 0; b < 2; b++) {
        pin_value out;
        REQUIRE(
            catalog_compile_sources(b, catalog, identities, sources, count, "CompileBehaviorsWithCatalog", &out)
        );
        const pin_array *bytes = out.ptr;
        if (!bytes || !bytes->count) {
            arena_free(&detached);
            REQUIRE(
                catalog_compile_sources(b, catalog, identities, sources, count, "BehaviorRejectionWithCatalog", &out)
            );
            fprintf(
                stderr,
                "executable library rejected: %.*s\n",
                (int)pin_text(out).length,
                pin_text(out).bytes
            );
            REQUIRE(false);
        }
        size_t length = (size_t)bytes->count;
        unsigned char *copied = xmalloc(length);
        for (size_t i = 0; i < length; i++) {
            copied[i] = bytes->items[i].u8;
        }
        arena_free(&detached);
        for (int backend = 0; backend < 2; backend++) {
            execution_prepared_linker linker = { .user = (void *)catalog,
                                                 .type = link_game_type,
                                                 .host = link_library_host,
                                                 .event = link_game_event };
            size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
            uint64_t root_id = 0;
            char error[256];
            package_collection *actual = compiler_artifact_read(
                copied,
                length,
                &linker,
                backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
                &remaining,
                &root_id,
                error,
                sizeof error
            );
            if (!actual) {
                fprintf(stderr, "executable library load: %s\n", error);
            }
            REQUIRE(actual && package_collection_count(actual) == count);
            int checked_results = 0;
            for (int i = 0; i < count; i++) {
                execution_module *got = package_collection_module(actual, i);
                const execution_module *expected = NULL;
                const char *identity = NULL;
                for (int j = 0; j < count; j++) {
                    const execution_module *candidate = authoring_sources_at(oracle, j)
                                                            ->module;
                    if (execution_module_id(candidate) == execution_module_id(got)) {
                        REQUIRE(!expected);
                        expected = candidate;
                        identity = authoring_sources_at(oracle, j)->path +
                                   strlen(root) + 1;
                    }
                }
                REQUIRE(expected);
                REQUIRE(got->plan.import_count == expected->plan.import_count);
                REQUIRE(same_declarations(expected, got));
                REQUIRE(same_points(expected, got));
                REQUIRE(same_watches(expected, got));
                for (int r = 0; r < result_count; r++) {
                    if (!strcmp(identity, results[r].identity)) {
                        REQUIRE(
                            library_result_plays((execution_module *)expected, got, backend, &results[r])
                        );
                        checked_results++;
                    }
                }
                if (playing && execution_module_id(got) == root_id) {
                    int calls = 0;
                    for (int j = 0; j < got->plan.function_count; j++) {
                        calls += acount(got->plan.functions[j].body->calls);
                    }
                    REQUIRE(calls > 0);
                }
                for (int j = 0; j < execution_module_function_count(got); j++) {
                    execution_module_function_info x = execution_module_function_at(expected, j);
                    execution_module_function_info y = execution_module_function_at(got, j);
                    REQUIRE(x.continuation_wait == y.continuation_wait);
                }
                if (playing && execution_module_id(got) == root_id) {
                    REQUIRE(library_plays((execution_module *)expected, got, backend));
                }
            }
            REQUIRE(checked_results == result_count);
            package_collection_free(actual);
        }
        if (playing && b == 1) {
            REQUIRE(library_generations(catalog, identities, sources, copied, length));
        }
        free(copied);
    }
    authoring_sources_free(oracle);
    native_definition_catalog_free(definitions);
    REQUIRE(!rmdir(root));
    return true;
}
static bool libraries_with_results(
    const char *const *identities,
    const char *const *sources,
    int count,
    bool playing,
    bool namespace_native,
    const library_result *results,
    int result_count
)
{
    return libraries_with_results_catalog(NULL, identities, sources, count, playing, namespace_native, results, result_count);
}
static bool libraries_alike(
    const char *const *identities,
    const char *const *sources,
    int count,
    bool playing,
    bool namespace_native
)
{
    return libraries_with_results(identities, sources, count, playing, namespace_native, NULL, 0);
}
TEST_CASE(package_compiler_compiles_executable_libraries)
{
    REQUIRE(prepare());
    const char *identities[] = { "libraries/Leaf.chibi" };
    const char *sources[] = { "Leaf :: library { Answer :: (value: int = 7) -> "
                              "(result: int) { return value; } }" };
    REQUIRE(libraries_alike(identities, sources, 1, false, false));
    return true;
}
TEST_CASE(package_compiler_links_executable_libraries)
{
    REQUIRE(prepare());
    const type_desc *reflected[] = { &spot_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    publish_project(&pack);
    const char *identities[] = { "behaviors/Caller.chibi",
                                 "libraries/Middle.chibi",
                                 "libraries/Leaf.chibi",
                                 "libraries/Unused.chibi" };
    const char *sources[] = {
        "Caller :: behavior { label: string = \"seed\"; on Tick :: () { "
        "p := Middle.Make().point; q := p; q.count += 5; later := "
        "Leaf.Make(19).value; label = p.text; "
        "if p.count == 7 && q.count == 12 && later.count == 19 { self.Spot.x "
        "+= 7; } "
        "self.Spot.y += Middle.Amount(); "
        "if Middle.OwnerMatches() { self.Spot.x += 1; } "
        "if Middle.CheckText(label) { self.Spot.y += 1; } } }",
        "Middle :: library { Make :: () -> (point: Leaf.Row) { return "
        "Leaf.Make(); } "
        "Amount :: () -> number { return Leaf.Amount(); } "
        "OwnerMatches :: () -> bool { return Leaf.OwnerMatches(); } "
        "CheckText :: (value: string) -> bool { return value == \"a\\u0000b\"; "
        "} }",
        "Leaf :: library { Row :: struct { text: string; count: int; } "
        "Make :: (n: int = 7) -> (value: Row) { return {text: \"a\\u0000b\", "
        "count: n}; } "
        "Amount :: (n: number = 2.5) -> number { return n; } "
        "OwnerMatches :: () -> bool { return self == globals.Terrain; } }",
        "Unused :: library { Answer :: () -> int { return 19; } }"
    };
    REQUIRE(libraries_alike(identities, sources, 4, true, false));
    publish_project(NULL);
    return true;
}
TEST_CASE(package_compiler_resolves_unqualified_libraries)
{
    REQUIRE(prepare());
    const char *identities[] = { "behaviors/Probe.chibi", "libraries/Leaf.chibi" };
    const char *sources[] = {
        "Probe :: behavior { observed: int = 0; Read :: () -> int { return "
        "Answer().value; } "
        "on Tick :: () { observed = Read(); } }",
        "Leaf :: library { Answer :: () -> (value: int) { return 7; } }"
    };
    const library_result result = { identities[0], 7, 0, 0 };
    REQUIRE(libraries_with_results(identities, sources, 2, false, false, &result, 1));
    return true;
}
TEST_CASE(package_compiler_resolves_library_primitive_names)
{
    REQUIRE(prepare());
    const char *identities[] = { "behaviors/Primitive.chibi",
                                 "libraries/Leaf.chibi",
                                 "behaviors/Qualified.chibi",
                                 "behaviors/Lexical.chibi" };
    const char *sources[] = {
        "Primitive :: behavior { observed: int = 0; Read :: () -> int { return "
        "int(7); } "
        "on Tick :: () { observed = Read(); } }",
        "Leaf :: library { int :: (value: int) -> int { return value + 5; } "
        "char :: (value: int) -> int { return value + 6; } "
        "number :: (value: int) -> int { return value + 7; } }",
        "Qualified :: behavior { observed: int = 0; Read :: () -> int { return "
        "Leaf.int(7) + "
        "Leaf.char(7) + Leaf.number(7); } on Tick :: () { observed = Read(); } "
        "}",
        "Lexical :: behavior { observed: int = 0; int :: (value: int) -> int { "
        "return value + 9; "
        "} "
        "char :: (value: int) -> int { return value + 10; } "
        "number :: (value: int) -> int { return value + 11; } "
        "Read :: () -> int { return int(7) + char(7) + number(7); } "
        "on Tick :: () { observed = Read(); } }"
    };
    const library_result results[] = { { identities[0], 7, 0, 0 },
                                       { identities[2], 39, 0, 0 },
                                       { identities[3], 51, 0, 0 } };
    REQUIRE(
        libraries_with_results(identities, sources, 4, false, false, results, NATIVE_COUNT(results))
    );
    return true;
}
TEST_CASE(package_compiler_resolves_native_canonical_names)
{
    REQUIRE(prepare());
    const char *identities[] = { "behaviors/Probe.chibi", "libraries/Alias.chibi" };
    const char *sources[] = {
        "Probe :: behavior { observed: int = 0; Read :: () -> int { return "
        "Alias.Native(7).length; } "
        "ReadAlias :: () -> int { return leaf_alias(8).length; } "
        "ReadSource :: () -> bool { return Alias.Native_(9).length; } "
        "on Tick :: () { observed = Read() + ReadAlias(); if !ReadSource() { "
        "observed += 1000; } } }",
        "Alias :: library { Native_ :: (value: int) -> (length: bool) { return "
        "false; } }"
    };
    const library_result result = { identities[0], 1215, 2, 0 };
    REQUIRE(libraries_with_results(identities, sources, 2, false, true, &result, 1));
    return true;
}
TEST_CASE(package_compiler_resolves_library_names)
{
    REQUIRE(prepare());
    const char *identities[] = { "behaviors/Probe.chibi", "libraries/Leaf.chibi" };
    const char *sources[] = {
        "Probe :: behavior { observed: int = 0; Read :: () -> int { return "
        "leaf.value(); } "
        "Owned :: () -> bool { return Leaf.Native(\"Terrain\").Value == self; "
        "} on Tick :: () { observed = Read(); if Owned() { observed += 1000; } "
        "} }",
        "Leaf :: library { Value :: () -> int { return Twice(2); } "
        "Native_ :: (key: string) -> (Value: int) { return 0; } "
        "Twice :: (n: int) -> int { if n == 0 { return 0; } return Twice(n - "
        "1) + 1; } }"
    };
    const library_result result = { identities[0], 1002, 0, 0 };
    REQUIRE(libraries_with_results(identities, sources, 2, false, true, &result, 1));
    return true;
}
TEST_CASE(package_compiler_pure_provider_has_no_write)
{
    const native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    char error[256];
    native_capabilities *native = native_capabilities_from_definition_packs(
        NULL,
        0,
        packs,
        NATIVE_COUNT(packs),
        error,
        sizeof error
    );
    REQUIRE(native);
    const script_catalog *catalog = native_capabilities_catalog(native);
    for (int i = 0; i < catalog->capability_count; i++) {
        REQUIRE(!(catalog->capabilities[i].effects & SCRIPT_EFFECT_WRITE));
    }
    size_t length;
    char *json = compiler_target_effects_json(catalog, &length);
    yyjson_doc *effects = yyjson_read(json, length, 0);
    REQUIRE(effects && yyjson_is_arr(yyjson_doc_get_root(effects)));
    yyjson_val *rows = yyjson_doc_get_root(effects);
    REQUIRE(yyjson_arr_size(rows) > 0);
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        REQUIRE(yyjson_is_uint(yyjson_arr_get(rows, i)));
        REQUIRE(!(yyjson_get_uint(yyjson_arr_get(rows, i)) & SCRIPT_EFFECT_WRITE));
    }
    fprintf(stderr, "pure12 provider exports %zu native effect masks with no WRITE\n", yyjson_arr_size(rows));
    yyjson_doc_free(effects);
    free(json);
    native_capabilities_free(native);
    return true;
}
TEST_CASE(package_compiler_folds_constants_as_stage_zero_does)
{
    REQUIRE(prepare());
    const char *identity = "behaviors/Nested.chibi";
    const char
        *source = "Nested :: behavior { observed: int = 0; Read :: () -> int { "
                  "x: int = int(u8(9)) + int(u8(wrap: 300)); "
                  "y: int = (4 - 1); return x + y + int(-3) + (2 * 3); } "
                  "on Tick :: () { observed = Read(); } }";
    const library_result result = { identity, 59, 0, 0 };
    REQUIRE(libraries_with_results(&identity, &source, 1, false, false, &result, 1));
    return true;
}
static bool library_rejects_catalog(
    const script_catalog *native,
    const char *const *identities,
    const char *const *sources,
    int count,
    const char *message,
    library_oracle_expectation expectation
)
{
    char root[] = "/tmp/chibi-compiler-libraries-XXXXXX";
    REQUIRE(mkdtemp(root));
    source_catalog_edit *edits = NULL;
    char (*paths)[256] = xmalloc((size_t)count * sizeof *paths);
    for (int i = 0; i < count; i++) {
        snprintf(paths[i], sizeof paths[i], "%s/%s", root, identities[i]);
        apush(edits, ((source_catalog_edit){ .path = paths[i], .text = sources[i] }));
    }
    source_catalog *snapshot = source_catalog_replaced(root, native, edits, count);
    afree(edits);
    free(paths);
    const script_catalog *catalog = source_catalog_view(snapshot);
    authoring_sources *oracle = NULL;
    if (expectation != LIBRARY_ORACLE_UNCHECKED) {
        script_lower_bindings bindings = { .host = library_host, .global = game_global };
        oracle = authoring_sources_prepare_catalog(snapshot, &bindings, BEHAVIOR_BYTECODE);
        REQUIRE(authoring_sources_valid(oracle) == (expectation == LIBRARY_ORACLE_VALID));
    }
    for (int b = 0; b < 2; b++) {
        pin_value out;
        REQUIRE(
            catalog_compile_sources(b, catalog, identities, sources, count, "CompileBehaviorsWithCatalog", &out)
        );
        const pin_array *bytes = out.ptr;
        REQUIRE(!bytes || !bytes->count);
        arena_free(&detached);
        REQUIRE(
            catalog_compile_sources(b, catalog, identities, sources, count, "BehaviorRejectionWithCatalog", &out)
        );
        if (!strstr(pin_text(out).bytes, message)) {
            fprintf(
                stderr,
                "library rejection wanted '%s', got '%s'\n",
                message,
                pin_text(out).bytes
            );
        }
        REQUIRE(strstr(pin_text(out).bytes, message));
        arena_free(&detached);
    }
    if (expectation != LIBRARY_ORACLE_UNCHECKED) {
        authoring_sources_free(oracle);
    } else {
        source_catalog_free(snapshot);
    }
    REQUIRE(!rmdir(root));
    return true;
}
static bool library_rejects(
    const char *const *identities,
    const char *const *sources,
    int count,
    const char *message,
    bool native_invalid
)
{
    script_catalog native = game_catalog();
    return library_rejects_catalog(
        &native,
        identities,
        sources,
        count,
        message,
        native_invalid ? LIBRARY_ORACLE_INVALID : LIBRARY_ORACLE_UNCHECKED
    );
}
TEST_CASE(package_compiler_resolves_library_array_updates)
{
    REQUIRE(prepare());
    const native_definition_pack packs[] = { NATIVE_ARRAY_DEFINITION_PACK };
    script_catalog base = game_catalog();
    char error[256];
    native_capabilities *arrays = native_capabilities_from_definition_packs(
        NULL,
        0,
        packs,
        NATIVE_COUNT(packs),
        error,
        sizeof error
    );
    REQUIRE(arrays);
    const script_catalog *array_catalog = native_capabilities_catalog(arrays);
    script_catalog_capability *capabilities = NULL;
    for (int i = 0; i < base.capability_count; i++) {
        apush(capabilities, base.capabilities[i]);
    }
    for (int i = 0; i < array_catalog->capability_count; i++) {
        apush(capabilities, array_catalog->capabilities[i]);
    }
    base.capabilities = capabilities;
    base.capability_count = acount(capabilities);
    const char *identities[] = { "libraries/ArrayTools.chibi", "behaviors/Use.chibi" };
    const char *sources[] = {
        "ArrayTools :: library { append :: (array: int[], item: int) -> int[] "
        "{ return array; } }",
        "Use :: behavior { Read :: () -> int { xs: int[] = []; xs = append(xs, "
        "7); return length(xs); } }"
    };
    REQUIRE(
        library_rejects_catalog(&base, identities, sources, 2, "Ambiguous function", LIBRARY_ORACLE_INVALID)
    );
    afree(capabilities);
    native_capabilities_free(arrays);
    return true;
}
/* These sources are independently rejected by stage zero. The stage-one
 * checks exercise nominal scalar identity and constant scalar validation,
 * rather than trusting an integer storage code or the UTF-8 library itself. */
TEST_CASE(package_compiler_rejects_invalid_char_conversions)
{
    static const char *const sources[] = {
        "package facts\nBad :: () -> char { return char(-1); }",
        "package facts\nBad :: () -> char { return char(0xD800); }",
        "package facts\nBad :: () -> char { return char(0xDFFF); }",
        "package facts\nBad :: () -> char { return char(0x110000); }",
        "package facts\nBad :: () -> char { return char(4294967296); }",
        "package facts\nBad :: () -> char { return char(18446744073709551615); "
        "}",
        "package facts\nBad :: () -> char { return char(wrap: 65); }",
        "package facts\nBad :: (value: number) -> char { return char(value); }",
        "package facts\nBad :: (value: int) -> char { return value; }",
        "package facts\nTake :: (value: char) -> int { return int(value); }\n"
        "Bad :: (value: int) -> int { return Take(value); }",
        "package facts\nBad :: (n: int) -> int { value: char = char(65); "
        "value = n; return int(value); }",
        "package facts\nBad :: (value: char) -> u8 { return value; }",
        "package facts\nBad :: (n: int) -> (value: char) { value = n; return; "
        "}",
        "package facts\nBad :: (n: int) -> (value: char) { return(n); }",
        "package facts\nBad :: () -> char { return char(65.5); }",
        "package facts\nFloat: number : 0;\nBad :: () -> char { return "
        "char(Float); }",
        "package facts\nFloat: f64 : 0;\nBad :: () -> char { return "
        "char(Float); }",
        "package facts\nRow :: struct { x: number; }\n"
        "Bad :: () -> number { row: Row = {x: 0.0}; row.x = char(65); return "
        "row.x; }",
        "package facts\nRow :: struct { x: f64; }\n"
        "Bad :: () -> f64 { row: Row = {x: 0.0}; row.x = char(65); return "
        "row.x; }",
        "package facts\nBad :: (value: char) -> int { return "
        "native.kernel(value); }"
    };
    REQUIRE(prepare());
    for (int i = 0; i < NATIVE_COUNT(sources); i++) {
        sample s = { .source = sources[i] };
        const authoring_package *root;
        authoring_packages *native = oracle_packages(&s, &root);
        REQUIRE(!authoring_packages_valid(native));
        REQUIRE(root->diagnostic_count > 0);
        authoring_packages_free(native);
        source_package_input inputs[SAMPLE_INPUTS];
        int count = sample_inputs(&s, inputs);
        for (int b = 0; b < 2; b++) {
            compiler_result out;
            pin_value message;
            bool probe = strstr(sources[i], "native.kernel") != NULL;
            REQUIRE(compile(b, false, probe, false, inputs, count, COMPILE_BUDGET, &out));
            if (compiler_succeeded(&out)) {
                fprintf(stderr, "stage one accepted invalid char source: %s\n", sources[i]);
            }
            REQUIRE(!compiler_succeeded(&out));
            REQUIRE(get(compiler_error(&out), "message", &message));
            if (i < 6) {
                REQUIRE(strstr(pin_text(message).bytes, "Constant does not fit in char"));
            }
            arena_free(&detached);
        }
    }
    return true;
}
/* The bounded scalar-char prerequisite deliberately does not enable its
 * container, pointer, field or arithmetic forms. Native acceptance proves
 * these failures describe stage-one coverage rather than invalid Chibi. */
TEST_CASE(package_compiler_rejects_deferred_char_forms)
{
    static const struct
    {
        const char *name, *source;
    } cases[] = {
        { "explicit array",
          "package facts\nRun :: () -> int { values: char[] = [char(65)]; "
          "return int(values[0]); }" },
        { "grouped array", "package facts\nRun :: () -> int { values: (char)[] = [char(65)]; return int(values[0]); }" },
        { "nested grouped array", "package facts\nRun :: () -> int { values: ((char))[] = [char(65)]; return int(values[0]); }" },
        { "grouped array type", "package facts\nRun :: () -> int { values: (char[]) = [char(65)]; return int(values[0]); }" },
        { "inferred array",
          "package facts\nRun :: () -> int { values := [char(65)]; "
          "return int(values[0]); }" },
        { "scoped pointer",
          "package facts\nRead :: (value: ^char) -> int { return int(value^); "
          "}\n"
          "Run :: () -> int { value: char = char(65); return Read(&value); }" },
        { "addressed local",
          "package facts\nRun :: () -> int { value: char = char(65); "
          "pointer := &value; return int(pointer^); }" },
        { "map key after int key",
          "package facts\nKnown :: (n: int) -> int { known: [int: int] = [:]; "
          "known[n] = 1; return known[n]; }\n"
          "Run :: () -> int { values: [char: int] = [:]; "
          "values[char(65)] = 7; return values[char(65)]; }" },
        { "map value",
          "package facts\nRun :: () -> int { values: [int: char] = [:]; "
          "values[0] = char(65); return int(values[0]); }" },
        { "unary arithmetic",
          "package facts\nRun :: (value: char) -> char { return -value; }" },
        { "binary arithmetic",
          "package facts\nRun :: (value: char) -> char { return value + 1; }" },
        { "compound arithmetic", "package facts\nRun :: (value: char) -> int { value += 1; return int(value); }" },
        { "record field",
          "package facts\nRow :: struct { value: char; }\n"
          "Run :: () -> int { row: Row = {value: char(65)}; return "
          "int(row.value); }" },
        { "optional", "package facts\nRun :: () -> char? { return char(65); }" },
        { "union payload",
          "package facts\nValue :: union { Rune(value: char) | Empty }\n"
          "Run :: () -> Value { return Value.Rune(char(65)); }" },
        { "character literal", "package facts\nRun :: () -> int { return int('A'); }" },
        { "contextual decimal expression",
          "package facts\nRun :: () -> int { return int(char(64.0 + 1.0)); }" },
        { "nested contextual decimal expression", "package facts\nRun :: () -> int { return int(char((32.0 * 2.0) + 1.0)); }" },
        { "widened named char result projection",
          "package facts\nNamed :: () -> (scalar: char) { return char(65); }\n"
          "Run :: () -> char { value: int = Named(); value = -1; return "
          "value.scalar; }" },
        { "wide named char result projection",
          "package facts\nNamed :: () -> (scalar: char) { return char(65); }\n"
          "Run :: () -> char { value: i64 = Named(); value = 4294967296; "
          "return value.scalar; }" },
        { "compound int field with char",
          "package facts\nRow :: struct { x: int; }\n"
          "Run :: () -> int { row: Row = {x: 0}; row.x += char(65); return "
          "row.x; }" },
        { "compound int array with char",
          "package facts\nRun :: () -> int { values: int[] = [0]; "
          "values[0] += char(65); return values[0]; }" },
        { "inferred generic array",
          "package facts\nRun :: () -> int { values := make_array(char(65)); "
          "return int(values[0]); }" }
    };
    REQUIRE(prepare());
    bool rejected_all = true;
    for (int i = 0; i < NATIVE_COUNT(cases); i++) {
        sample s = { .source = cases[i].source };
        const authoring_package *root;
        authoring_packages *native = oracle_packages(&s, &root);
        if (!authoring_packages_valid(native)) {
            for (int j = 0; j < root->diagnostic_count; j++) {
                fprintf(
                    stderr,
                    "native deferred char %s: %s\n",
                    cases[i].name,
                    root->diagnostics[j].message
                );
            }
        }
        REQUIRE(authoring_packages_valid(native));
        authoring_packages_free(native);
        source_package_input inputs[SAMPLE_INPUTS];
        int count = sample_inputs(&s, inputs);
        for (int b = 0; b < 2; b++) {
            compiler_result out;
            pin_value message;
            REQUIRE(compile(b, false, false, false, inputs, count, COMPILE_BUDGET, &out));
            if (compiler_succeeded(&out)) {
                fprintf(
                    stderr,
                    "stage one accepted deferred char form: %s\n",
                    cases[i].name
                );
            }
            bool rejected = !compiler_succeeded(&out);
            rejected_all &= rejected;
            if (rejected) {
                REQUIRE(get(compiler_error(&out), "message", &message));
                REQUIRE(pin_text(message).length > 0);
            }
            arena_free(&detached);
        }
    }
    REQUIRE(rejected_all);
    return true;
}
/* A state's resolved nominal type must be checked after grouping, before
 * its integer storage or default can enter the behavior state table. */
TEST_CASE(package_compiler_rejects_deferred_char_states)
{
    static const struct
    {
        const char *name, *source;
        bool native_valid;
    } cases[] = {
        { "grouped negative", "Bad :: behavior { value: (char) = -1; Read :: () -> char { return value; } }", false },
        { "nested surrogate", "Bad :: behavior { value: ((char)) = 0xD800; Read :: () -> char { return value; } }", false },
        { "grouped valid", "Bad :: behavior { value: (char) = 65; Read :: () -> char { return value; } }", true },
        { "nested valid", "Bad :: behavior { value: ((char)) = 65; Read :: () -> char { return value; } }", true }
    };
    REQUIRE(prepare());
    script_catalog native = game_catalog();
    const char *identities[] = { "behaviors/CharState.chibi" };
    bool native_matches = true, rejected_all = true;
    for (int i = 0; i < NATIVE_COUNT(cases); i++) {
        char root[] = "/tmp/chibi-char-states-XXXXXX", path[256];
        REQUIRE(mkdtemp(root));
        snprintf(path, sizeof path, "%s/%s", root, identities[0]);
        source_catalog_edit edit = { .path = path, .text = cases[i].source };
        source_catalog *snapshot = source_catalog_replaced(root, &native, &edit, 1);
        script_lower_bindings bindings = { .host = library_host, .global = game_global };
        authoring_sources *oracle = authoring_sources_prepare_catalog(snapshot, &bindings, BEHAVIOR_BYTECODE);
        native_matches &= authoring_sources_valid(oracle) == cases[i].native_valid;
        for (int b = 0; b < 2; b++) {
            const char *sources[] = { cases[i].source };
            pin_value out;
            REQUIRE(
                catalog_compile_sources(b, &native, identities, sources, 1, "CompileBehaviorsWithCatalog", &out)
            );
            const pin_array *bytes = out.ptr;
            bool rejected = !bytes || !bytes->count;
            if (!rejected) {
                fprintf(
                    stderr,
                    "stage one accepted deferred char state %s on backend %d\n",
                    cases[i].name,
                    b
                );
            }
            rejected_all &= rejected;
            arena_free(&detached);
            if (rejected) {
                REQUIRE(
                    catalog_compile_sources(b, &native, identities, sources, 1, "BehaviorRejectionWithCatalog", &out)
                );
                REQUIRE(strstr(pin_text(out).bytes, "state type is not supported yet"));
                arena_free(&detached);
            }
        }
        authoring_sources_free(oracle);
        REQUIRE(!rmdir(root));
    }
    REQUIRE(native_matches);
    REQUIRE(rejected_all);
    return true;
}
/* The production generic arithmetic native retains its input type. Its
 * char form is deferred until every such result preserves Unicode validity. */
TEST_CASE(package_compiler_defers_generic_char_native_results)
{
    REQUIRE(prepare());
    char error[256];
    const native_definition_pack pack = NATIVE_ARITHMETIC_DEFINITION_PACK;
    native_capabilities *
        arithmetic = native_capabilities_from_definition_packs(NULL, 0, &pack, 1, error, sizeof error);
    REQUIRE(arithmetic);
    const script_catalog *catalog = native_capabilities_catalog(arithmetic);
    const char *sources[] = {
        "package facts\nRun :: () -> char { return negate(char(65)); }",
        "package facts\nRun :: (value: int) -> int { return negate(value); }"
    };
    for (int i = 0; i < NATIVE_COUNT(sources); i++) {
        source_package_input source = { "facts/Main.chibi",
                                        sources[i],
                                        strlen(sources[i]) };
        source_package_snapshot_options options = { .project_root = "/compiler",
                                                    .files = &source,
                                                    .file_count = 1 };
        source_package_snapshot *snapshot = source_package_snapshot_new(&options);
        REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
        script_lower_bindings bindings = authoring_native_bindings(arithmetic);
        authoring_packages *native = authoring_packages_prepare(snapshot, catalog, &bindings, BEHAVIOR_BYTECODE, NULL);
        REQUIRE(native && authoring_packages_valid(native));
        authoring_packages_free(native);
        for (int b = 0; b < 2; b++) {
            pin_value args[ARGUMENTS + 1];
            compiler_result out;
            REQUIRE(arguments(b, false, &source, 1, args));
            REQUIRE(
                compiler_fixture_natives(&fixture, b, PACKAGE, catalog, &input, &args[9], &args[ARGUMENTS])
            );
            REQUIRE(
                compiler_call(b, "CompileWithEffects", args, ARGUMENTS + 1, COMPILE_BUDGET, &out)
            );
            REQUIRE(compiler_succeeded(&out) == (i != 0));
            if (i == 0) {
                pin_value message;
                REQUIRE(get(compiler_error(&out), "message", &message));
                REQUIRE(
                    strstr(pin_text(message).bytes, "generic char native results are not supported yet")
                );
            }
            arena_free(&detached);
        }
    }
    native_capabilities_free(arithmetic);
    return true;
}
TEST_CASE(package_compiler_rejects_unsupported_numeric_conversions)
{
    REQUIRE(prepare());
    const char *identities[] = { "behaviors/Unsupported.chibi", "libraries/Leaf.chibi" };
    /* A primitive conversion wins over a library or native export with the
     * same name, including char's checked scalar conversion. */
    const char *behaviors[] = {
        "Unsupported :: behavior { observed: int = 0; "
        "on Tick :: () { observed = int(char(65)); } }"
    };
    const char
        *library = "Leaf :: library { char :: (value: int) -> int { return "
                   "value + 5; } "
                   "number :: (value: int) -> int { return value + 6; } "
                   "f32 :: (value: int) -> int { return value + 7; } "
                   "f64 :: (value: int) -> int { return value + 8; } }";
    native_capability_definition primitive_definitions[2] = {
        LIBRARY_ALIAS_DEFINITIONS[0],
        LIBRARY_ALIAS_DEFINITIONS[1]
    };
    primitive_definitions[0].canonical_name = "char";
    primitive_definitions[0].source_name = "character_function";
    primitive_definitions[1].canonical_name = "number";
    primitive_definitions[1].source_name = "number_function";
    const script_catalog empty = { 0 };
    char error[256];
    native_definition_catalog *definitions = native_definition_catalog_new(
        &empty,
        primitive_definitions,
        NATIVE_COUNT(primitive_definitions),
        NULL,
        0,
        error,
        sizeof error
    );
    REQUIRE(definitions);
    const script_catalog *declared = native_definition_catalog_view(definitions);
    script_catalog native = game_catalog();
    script_catalog_capability *capabilities = NULL;
    for (int i = 0; i < native.capability_count; i++) {
        apush(capabilities, native.capabilities[i]);
    }
    for (int i = 0; i < declared->capability_count; i++) {
        apush(capabilities, declared->capabilities[i]);
    }
    script_catalog with_native = native;
    with_native.capabilities = capabilities;
    with_native.capability_count = acount(capabilities);
    for (int i = 0; i < NATIVE_COUNT(behaviors); i++) {
        const char *sources[] = { behaviors[i], library };
        const library_result result = { identities[0], 65, 0, 0 };
        REQUIRE(libraries_with_results(identities, sources, 2, false, false, &result, 1));
        REQUIRE(
            libraries_with_results_catalog(&with_native, identities, sources, 1, false, false, &result, 1)
        );
        const char *invalid[] = {
            "Unsupported :: behavior { Read :: () -> "
            "char { return char(wrap: 65); } }",
            "Unsupported :: behavior { Read :: (value: number) -> "
            "char { return char(value); } }",
            "Unsupported :: behavior { Read :: () -> int { "
            "value := char(65); value = -1; return int(value); } }"
        };
        REQUIRE(
            library_rejects_catalog(&with_native, identities, invalid, 1, "char converts only with char(value)", LIBRARY_ORACLE_INVALID)
        );
        REQUIRE(
            library_rejects_catalog(&with_native, identities, invalid + 1, 1, "char converts from an integer", LIBRARY_ORACLE_INVALID)
        );
        REQUIRE(
            library_rejects_catalog(&with_native, identities, invalid + 2, 1, "assigned value differs from its local", LIBRARY_ORACLE_INVALID)
        );
    }
    afree(capabilities);
    native_definition_catalog_free(definitions);
    /* A floating conversion converts, ahead of a library's function. */
    const char *converted[] = {
        "Unsupported :: behavior { observed: int = 0; "
        "on Tick :: () { observed = int(f64(7)) + int(number(1)); } }",
        library
    };
    const library_result result = { identities[0], 8, 0, 0 };
    REQUIRE(libraries_with_results(identities, converted, 2, false, false, &result, 1));
    return true;
}
TEST_CASE(package_compiler_rejects_invalid_library_closures)
{
    REQUIRE(prepare());
    const char *identities[] = { "libraries/A.chibi",
                                 "libraries/B.chibi",
                                 "behaviors/Use.chibi" };
    const char *ambiguous[] = {
        "A :: library { Value :: () -> int { return 1; } }",
        "B :: library { Value :: () -> int { return 2; } }",
        "Use :: behavior { Read :: () -> int { return Value(); } }"
    };
    REQUIRE(library_rejects(identities, ambiguous, 3, "Ambiguous function", true));
    const char *cycle[] = {
        "A :: library { Value :: () -> int { return B.Value(); } }",
        "B :: library { Value :: () -> int { return A.Value(); } }"
    };
    REQUIRE(library_rejects(identities, cycle, 2, "cross-library import cycle", true));
    const char *duplicates[] = { "libraries/A.chibi", "libraries/A.chibi" };
    REQUIRE(
        library_rejects(duplicates, ambiguous, 2, "duplicate executable source identity", false)
    );
    const char *roots[] = {
        "A :: library { Value :: () -> int { return 1; } }",
        "A :: library { Value :: () -> int { return 2; } }"
    };
    REQUIRE(library_rejects(identities, roots, 2, "duplicate library declaration", true));
    const char *non_declaration[] = {
        "A :: library { Value :: () -> int { return 1; } }",
        "Batch :: struct { value: int; }"
    };
    REQUIRE(library_rejects(identities, non_declaration, 2, "Unexpected syntax", false));
    return true;
}
/* Stage zero's build of stage one passes arguments without copying them to
 * every function but those that run once per compile. One that pushes onto an
 * array argument, or stores a uint64 into a field of a local record, copies
 * every argument on each call, and so does every function that calls it,
 * slowing stage one compiling itself. */
TEST_CASE(package_compiler_shares_its_arguments)
{
    REQUIRE(prepare());
    REQUIRE(
        copying_functions(
            &compiler_fixture_package(&fixture, 1, PACKAGE)->module->plan
        ) == 0
    );
    return true;
}
}
static bool action_table()
{
    if (actions_native) {
        return true;
    }
    uint64_t entity_id = script_catalog_id("type", "ecs.entity"),
             alias_id = script_catalog_id("type", "fixture.owner");
    const script_catalog_type types[] = {
        { .id = entity_id,
          .name = "ecs.entity",
          .type = { .storage = PIN_HANDLE, .nominal = entity_id },
          .size = sizeof(uint64_t) },
        { .id = alias_id,
          .name = "fixture.owner",
          .type = { .storage = PIN_HANDLE, .nominal = alias_id },
          .size = sizeof(uint64_t),
          .underlying = entity_id }
    };
    native_definition_pack pack = ACTION_PACK;
    pack.base = (script_catalog){
        .types = types,
        .type_count = NATIVE_COUNT(types),
        .entity_type = entity_id,
        .tick_event = native_capability_definition_id(&ACTION_DEFINITIONS[1])
    };
    char error[256];
    actions_native = native_capabilities_from_definition_packs(NULL, 0, &pack, 1, error, sizeof error);
    if (!actions_native) {
        fprintf(stderr, "action native definitions: %s\n", error);
    }
    REQUIRE(actions_native);
    return true;
}
static yyjson_val *action_exported(yyjson_val *functions, const char *name)
{
    size_t index, count;
    yyjson_val *function;
    yyjson_arr_foreach(functions, index, count, function)
    {
        if (!strcmp(yyjson_get_str(yyjson_obj_get(function, "name")), name)) {
            return function;
        }
    }
    return NULL;
}
static yyjson_val *action_exported_id(yyjson_val *root, uint64_t id)
{
    yyjson_val *functions = yyjson_arr_get(root, 5),
               *identities = yyjson_arr_get(root, 7);
    for (size_t i = 0; i < yyjson_arr_size(functions); i++) {
        if (yyjson_get_uint(yyjson_arr_get(identities, 2 * i)) == id) {
            return yyjson_arr_get(functions, i);
        }
    }
    return NULL;
}
TEST_CASE(package_compiler_keeps_unavailable_native_functions)
{
    REQUIRE(action_table());
    const script_catalog *catalog = native_capabilities_catalog(actions_native);
    uint64_t id = 0;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *capability = &catalog->capabilities[i];
        if (capability->qualified_name &&
            !strcmp(capability->qualified_name, "fixture.unavailable_function")) {
            REQUIRE(
                capability->kind == SCRIPT_CAPABILITY_FUNCTION &&
                (capability->effects & SCRIPT_EFFECT_SUSPEND)
            );
            id = capability->id;
        }
    }
    REQUIRE(id);
    const char *identity = "Unavailable",
               *source = "Unavailable :: behavior {}";
    size_t length;
    char *json = compiler_behavior_catalog_input_json(catalog, actions_native, &identity, &source, 1, &length);
    yyjson_doc *parsed = yyjson_read(json, length, YYJSON_READ_ALLOW_INVALID_UNICODE);
    REQUIRE(parsed);
    yyjson_val *row = action_exported_id(yyjson_doc_get_root(parsed), id);
    bool retained = row && yyjson_is_false(yyjson_obj_get(row, "direct"));
    if (!retained) {
        fprintf(stderr, "rich exporter omitted unavailable FUNCTION fixture.unavailable_function/held_probe; same-source ACTION remains eligible\n");
    }
    yyjson_doc_free(parsed);
    free(json);
    REQUIRE(retained);
    return true;
}
TEST_CASE(package_compiler_exports_native_actions)
{
    REQUIRE(action_table());
    const script_catalog *catalog = native_capabilities_catalog(actions_native);
    const script_catalog_capability *action = script_catalog_capability_by_id(
        catalog,
        native_capability_definition_id(&ACTION_DEFINITIONS[0])
    );
    REQUIRE(action);
    const native_capability_binding *binding = native_capabilities_find(
        actions_native,
        action->id,
        script_catalog_capability_abi(catalog, action)
    );
    REQUIRE(
        action->kind == SCRIPT_CAPABILITY_ACTION && action->entry_count == 1 &&
        action->outcome_count == 1 && action->completion_outcome == 0
    );
    REQUIRE(binding && binding->call && binding->call->invoke == action_probe);
    const char *identity = "NativeActions";
    const char *source = "NativeActions :: behavior {}";
    size_t length;
    char *json = compiler_behavior_catalog_input_json(catalog, actions_native, &identity, &source, 1, &length);
    yyjson_doc *parsed = yyjson_read(json, length, YYJSON_READ_ALLOW_INVALID_UNICODE);
    REQUIRE(parsed);
    yyjson_val *root = yyjson_doc_get_root(parsed),
               *functions = yyjson_arr_get(root, 5),
               *ports = yyjson_arr_get(root, 6),
               *native = action_exported(functions, action->name);
    bool exported = native && yyjson_is_true(yyjson_obj_get(native, "direct"));
    if (!exported) {
        fprintf(
            stderr,
            "rich exporter omitted synchronous ACTION '%s': %zu callable rows, "
            "real invoke binding, one entry and completion\n",
            action->name,
            yyjson_arr_size(functions)
        );
    }
    REQUIRE(exported && yyjson_arr_size(functions) == NATIVE_COUNT(ACTION_DEFINITIONS) - 1);
    REQUIRE(yyjson_arr_size(yyjson_arr_get(root, 17)) == yyjson_arr_size(functions));
    REQUIRE(yyjson_obj_size(native) == 10);
    static const char *const eligible[] = { "fixture.kind_action",
                                            "fixture.duplicate_int",
                                            "fixture.duplicate_text",
                                            "fixture.assert_action" };
    for (int i = 0; i < NATIVE_COUNT(eligible); i++) {
        for (int c = 0; c < catalog->capability_count; c++) {
            const script_catalog_capability *capability = &catalog->capabilities[c];
            if (!capability->qualified_name ||
                strcmp(capability->qualified_name, eligible[i])) {
                continue;
            }
            yyjson_val *row = action_exported_id(root, capability->id);
            bool direct = row && yyjson_is_true(yyjson_obj_get(row, "direct"));
            if (!direct) {
                fprintf(
                    stderr,
                    "rich exporter lost eligible action binding '%s' (%s)\n",
                    capability->qualified_name,
                    capability->name
                );
            }
            REQUIRE(direct);
        }
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *capability = &catalog->capabilities[i];
        if (capability->kind != SCRIPT_CAPABILITY_FUNCTION &&
            capability->kind != SCRIPT_CAPABILITY_ACTION) {
            continue;
        }
        yyjson_val *row = action_exported_id(root, capability->id);
        REQUIRE(row && yyjson_obj_size(row) == 10);
        REQUIRE(yyjson_get_int(yyjson_obj_get(row, "kind")) == (int)capability->kind);
        const native_capability_binding *binding = native_capabilities_find(
            actions_native,
            capability->id,
            script_catalog_capability_abi(catalog, capability)
        );
        REQUIRE(
            yyjson_get_int(yyjson_obj_get(row, "intrinsic")) ==
            (binding ? (int)binding->intrinsic : 0)
        );
        REQUIRE(!strcmp(
            yyjson_get_str(yyjson_obj_get(row, "qualified_name")),
            capability->qualified_name ? capability->qualified_name : capability->name
        ));
    }
    const char *const unsupported[] = { "unsupported_probe", "suspending_probe" };
    for (int i = 0; i < NATIVE_COUNT(unsupported); i++) {
        yyjson_val *row = action_exported(functions, unsupported[i]);
        REQUIRE(row && yyjson_is_false(yyjson_obj_get(row, "direct")));
        REQUIRE(
            !yyjson_get_int(yyjson_obj_get(row, "parameters")) &&
            !yyjson_get_int(yyjson_obj_get(row, "results"))
        );
    }
    int first = yyjson_get_int(yyjson_obj_get(native, "first"));
    for (int i = 0; i < action->parameter_count + action->result_count; i++) {
        yyjson_val *port = yyjson_arr_get(ports, (size_t)(first + i));
        REQUIRE(yyjson_obj_size(port) == 10);
        bool has_default = i > 0 && i < action->parameter_count;
        REQUIRE(yyjson_get_bool(yyjson_obj_get(port, "has_default")) == has_default);
        REQUIRE(yyjson_get_bool(yyjson_obj_get(port, "default_supported")) == has_default);
        REQUIRE(yyjson_get_bool(yyjson_obj_get(port, "default_owner")) == (i == 7));
    }
    yyjson_val *gain = yyjson_arr_get(ports, (size_t)(first + 2)),
               *large = yyjson_arr_get(ports, (size_t)(first + 4)),
               *message = yyjson_arr_get(ports, (size_t)(first + 5)),
               *name = yyjson_arr_get(ports, (size_t)(first + 8));
    uint64_t gain_bits;
    double gain_value = -2.5;
    memcpy(&gain_bits, &gain_value, sizeof gain_bits);
    REQUIRE(yyjson_get_uint(yyjson_obj_get(gain, "default_bits")) == gain_bits);
    REQUIRE(yyjson_get_uint(yyjson_obj_get(large, "default_bits")) == UINT64_C(0xfedcba9876543210));
    yyjson_val *message_text = yyjson_obj_get(message, "default_text");
    REQUIRE(
        yyjson_get_len(message_text) == ACTION_TEXT.length &&
        !memcmp(yyjson_get_str(message_text), ACTION_TEXT.bytes, ACTION_TEXT.length)
    );
    REQUIRE(
        yyjson_get_int(yyjson_obj_get(name, "storage")) == PIN_NAME &&
        !strcmp(yyjson_get_str(yyjson_obj_get(name, "default_text")), "named")
    );
    yyjson_val *aggregate = action_exported(functions, "aggregate_probe");
    yyjson_val *aggregate_port = yyjson_arr_get(
        ports,
        (size_t)yyjson_get_int(yyjson_obj_get(aggregate, "first"))
    );
    REQUIRE(
        yyjson_is_true(yyjson_obj_get(aggregate_port, "has_default")) &&
        yyjson_is_false(yyjson_obj_get(aggregate_port, "default_supported"))
    );
    yyjson_doc_free(parsed);
    free(json);
    return true;
}
}
}
}
}
}
}
static void action_default_setup(action_default_fixture *out, bool wrong_header)
{
    const script_catalog *base = native_capabilities_catalog(actions_native);
    assert(base->type_count == NATIVE_COUNT(out->types) - 1);
    memcpy(out->types, base->types, (size_t)base->type_count * sizeof *out->types);
    out->types[2] = base->types[0];
    out->types[2].id = script_catalog_id("type", "different.entity");
    out->types[2].name = "different.entity";
    out->types[2].type.nominal = out->types[2].id;
    const script_catalog_capability *probe = script_catalog_capability_by_id(
        base,
        native_capability_definition_id(&ACTION_DEFINITIONS[0])
    );
    memcpy(out->parameters, probe->parameters, sizeof out->parameters);
    out->parameters[7].type = out->types[0].type;
    out->parameters[7].default_owner = false;
    out->parameters[7]
        .default_value.type_name = out->types[wrong_header ? 2 : 0].name;
    out->parameters[7].default_value.u64 = 7;
    out->capabilities[0] = *probe;
    out->capabilities[1] = *script_catalog_capability_by_id(base, base->tick_event);
    out->capabilities[0].kind = SCRIPT_CAPABILITY_FUNCTION;
    out->capabilities[0].parameters = out->parameters;
    out->capabilities[0].entries = out->capabilities[0].outcomes = NULL;
    out->capabilities[0].entry_count = out->capabilities[0].outcome_count = 0;
    out->capabilities[0].completion_outcome = -1;
    out->catalog = *base;
    out->catalog.types = out->types;
    out->catalog.type_count = NATIVE_COUNT(out->types);
    out->catalog.capabilities = out->capabilities;
    out->catalog.capability_count = NATIVE_COUNT(out->capabilities);
}
static bool action_default_unsupported(const action_default_fixture *values)
{
    const char *identity = "Malformed", *source = "Malformed :: behavior {}";
    size_t length;
    char *json = compiler_behavior_catalog_input_json(&values->catalog, NULL, &identity, &source, 1, &length);
    yyjson_doc *parsed = yyjson_read(json, length, YYJSON_READ_ALLOW_INVALID_UNICODE);
    REQUIRE(parsed);
    yyjson_val *port = yyjson_arr_get(yyjson_arr_get(yyjson_doc_get_root(parsed), 6), 7);
    bool unsupported = yyjson_get_int(yyjson_obj_get(port, "storage")) == PIN_HANDLE &&
                       yyjson_is_true(yyjson_obj_get(port, "has_default")) &&
                       yyjson_is_false(yyjson_obj_get(port, "default_supported"));
    if (!unsupported) {
        fprintf(
            stderr,
            "rich exporter admitted non-owner HANDLE default bits7 with header "
            "%s for declared ecs.entity\n",
            values->parameters[7].default_value.type_name
        );
    }
    yyjson_doc_free(parsed);
    free(json);
    REQUIRE(unsupported);
    return true;
}
TEST_CASE(package_compiler_rejects_nonzero_handle_defaults)
{
    REQUIRE(action_table());
    action_default_fixture values;
    action_default_setup(&values, false);
    REQUIRE(action_default_unsupported(&values));
    return true;
}
}
}
}
}
}
TEST_CASE(package_compiler_keeps_text_state_per_owner)
{
    REQUIRE(prepare());
    const type_desc *const reflected[] = { &spot_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    publish_project(&pack);
    execution_module *module = NULL;
    REQUIRE(stage_one_behavior(1, "TextState", TEXT_STATE, &module));
    player p = { 0 };
    REQUIRE(play(module, &p));
    ecs_entity other = ecs_world_create_entity(p.world);
    spot initial = { .w = 9 };
    REQUIRE(ecs_world_record_add(p.world, other, SPOT_COMPONENT, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(p.world) == ECS_OK);
    execution_instance *second = execution_program_attach(p.program, ecs_entity_to_u64(other));
    REQUIRE(second);
    uint64_t state = script_catalog_id("symbol", "TextState:1:label");
    pin_value delta = { .type = PIN_F32, .f = 1.0f / 60.0f };
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int first = execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, 0, 1000);
    behavior_run_scope_end(&runs);
    REQUIRE(first == FLOW_OK);
    const pin_value *first_value = execution_instance_state(p.instance, state);
    const pin_value *second_value = execution_instance_state(second, state);
    REQUIRE(first_value && second_value);
    REQUIRE(
        pin_text_equal(pin_text(*first_value), (pin_string){ .bytes = "a\0b", .length = 3 })
    );
    REQUIRE(
        pin_text_equal(pin_text(*second_value), (pin_string){ .bytes = "seed", .length = 4 })
    );
    behavior_run_scope_begin(&runs);
    int repeated = execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, 0, 1000);
    int other_result = execution_instance_dispatch(second, TICK, &delta, 1, 0, 0, 1000);
    behavior_run_scope_end(&runs);
    REQUIRE(repeated == FLOW_OK && other_result == FLOW_OK);
    first_value = execution_instance_state(p.instance, state);
    second_value = execution_instance_state(second, state);
    REQUIRE(first_value && second_value);
    REQUIRE(
        pin_text_equal(pin_text(*first_value), (pin_string){ .bytes = "seed", .length = 4 })
    );
    REQUIRE(
        pin_text_equal(pin_text(*second_value), (pin_string){ .bytes = "a\0b", .length = 3 })
    );
    REQUIRE(
        TEST_COMPILED_BACKEND == BEHAVIOR_WASM ? runs.counts.wasm_entries > 0
                                               : runs.counts.sljit_entries > 0
    );
    REQUIRE(!runs.counts.overflow && !runs.counts.bytecode_entries);
    stop(&p);
    execution_module_free(module);
    publish_project(NULL);
    return true;
}
/* Task results keep cancellation distinct from valid zero/false/empty values.
 * Start runs up to its first yield; joining a completed/cancelled task continues
 * synchronously, and direct await preserves the ordinary return type. */
TEST_CASE(package_compiler_runs_task_sequencing_and_cancellation)
{
    REQUIRE(prepare());
    const type_desc *const reflected[] = { &spot_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    type_catalog_set_project(&pack);
    const char *source =
        "Tasks :: behavior { started: int = 0; initial: int = 0; zero: int = "
        "99; "
        "flags: int = 0; cancelled: int = 0; done: int = 0; failures: int = 0; "
        "Number :: (value: int) -> int { started += 1; yield; return value } "
        "Flag :: () -> bool { yield; return false } "
        "Text :: () -> string { yield; return \"\" } "
        "Label :: () -> string { yield; return \"ret\\u0000ained\" } "
        "ReadSpot :: () -> Spot { yield; return self.Spot } "
        "Void :: () { yield } "
        "Child :: () { yield; failures += 1000 } "
        "Parent :: () { Child(); await Delay(100) } "
        "Run :: () { n := Number(0); initial = started; r := await n; "
        "if r is Cancelled { failures += 1 } else { "
        "if (r) is not Cancelled { yield; if r is not Cancelled { zero = r } } "
        "} "
        "b := Flag(); br := await b; if br is not Cancelled { if !br { flags "
        "+= 1 } } "
        "t := Text(); tr := await t; if tr is not Cancelled { if tr == \"\" { "
        "flags += 2 } } "
        "label := Label(); lr := await label; if lr is not Cancelled { "
        "if lr == \"ret\\u0000ained\" { flags += 32 } } "
        "s := ReadSpot(); sr := await s; if sr is not Cancelled { "
        "if sr.w == 9 { flags += 16 } } "
        "bad := Number(9); cancel bad; cr := await bad; "
        "if cr is Cancelled { cancelled += 1 } else { failures += cr } "
        "late := Number(8); cancel late after 0; dr := await late; "
        "if dr is not Cancelled { failures += dr } else { cancelled += 1 } "
        "parent := Parent(); cancel parent; pr := await parent; "
        "if pr is Cancelled { cancelled += 1 } "
        "v := Void(); vr := await v; if vr is Cancelled { failures += 1 } else "
        "{ if vr is not Cancelled { flags += 4 } } "
        "direct := await Number(3); flags += direct; await Delay(0); done = 1 "
        "} "
        "on Tick :: () { Run() } }";
    const char *const states[] = { "initial", "zero",     "flags",  "cancelled",
                                   "done",    "failures", "started" };
    const int expected[] = { 1, 0, 58, 3, 1, 0, 4 };
    for (int b = 0; b < 2; b++) {
        execution_module *module = NULL;
        REQUIRE(stage_one_behavior(b, "Tasks", source, &module));
        player p = { 0 };
        REQUIRE(play(module, &p));
        const pin_value delta = { .type = PIN_F32, .f = 1.0f };
        REQUIRE(execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, 0, 10000) == FLOW_OK);
        const pin_value *initial = execution_instance_state(
            p.instance,
            script_catalog_id("symbol", "Tasks:1:initial")
        );
        REQUIRE(initial && initial->type == PIN_I32 && initial->i == 1);
        for (int frame = 1; frame <= 32 && execution_instance_pending(p.instance);
             frame++) {
            REQUIRE(execution_program_update(p.program, frame, 1, 10000) == FLOW_OK);
        }
        for (size_t i = 0; i < sizeof expected / sizeof *expected; i++) {
            char identity[96];
            snprintf(identity, sizeof identity, "Tasks:1:%s", states[i]);
            const pin_value *value = execution_instance_state(
                p.instance,
                script_catalog_id("symbol", identity)
            );
            if (!value || value->type != PIN_I32 || value->i != expected[i]) {
                fprintf(
                    stderr,
                    "task backend %d state %s: type %d value %d expected %d\n",
                    b,
                    states[i],
                    value ? value->type : -1,
                    value ? value->i : -1,
                    expected[i]
                );
            }
            REQUIRE(value && value->type == PIN_I32 && value->i == expected[i]);
        }
        REQUIRE(execution_instance_pending(p.instance) == 0);
        stop(&p);
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    return true;
}

static pin_value scalar_field(const pin_value *row, const char *name)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    assert(field);
    pin_value value;
    assert(!pin_read_field(&detached, row->ptr, field, &value));
    return value;
}
static bool retained_text(const pin_value *row, const char *name, const char *expected)
{
    pin_value value = scalar_field(row, name);
    REQUIRE(value.type == PIN_STR && arena_contains(&detached, value.text));
    REQUIRE(
        pin_text_equal(pin_text(value), (pin_string){ .bytes = expected, .length = strlen(expected) })
    );
    return true;
}
static const char
    SCALAR_SOURCE[] = "package scalar\n"
                      "Main :: () -> int { return Pick(7, true); }\n"
                      "Pick :: (x: int, b: bool) -> int { return x; }\n";
/* Two concrete Remap instances, one with a named function and one with an
 * inline function. Main's independent arithmetic oracle is 50 + 150 = 200. */
static const char CASES_PACKAGE[] = "project:scalar_cases";
static const char GENERIC_SCALAR_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return int(Named(0.25) * 100) + int(Inline(0.5) "
         "* 100); }\n"
         "Named :: (sample: number) -> number { return Remap(sample, Gain); }\n"
         "Inline :: (sample: number) -> number { return Remap(sample, (value: "
         "number) => value * 3); }\n"
         "Remap :: (sample: $T, transform: fn(value: T) -> T) -> T { return "
         "transform(value: sample); }\n"
         "Gain :: (input: number) -> number { return input * 2; }\n";
static const char GENERIC_INTEGER_SCALAR_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return Named(25) + Inline(50); }\n"
         "Named :: (sample: int) -> int { return Remap(sample, Gain); }\n"
         "Inline :: (sample: int) -> int { return Remap(sample, (value: int) "
         "=> value * 3); }\n"
         "Remap :: (sample: $T, transform: fn(value: T) -> T) -> T { return "
         "transform(value: sample); }\n"
         "Gain :: (input: int) -> int { return input * 2; }\n";
/* A guarded module as CollectScalar gives it: Pick's guard compares its
 * parameters and its arm calls Id. */
static const char GUARD_SOURCE[] = "package scalar\n"
                                   "Main :: () -> int { return Pick(1, 2); }\n"
                                   "Pick :: (a: int, b: int) -> int {\n"
                                   "    if (a < b) { return Id(a); }\n"
                                   "    return b;\n"
                                   "}\n"
                                   "Id :: (x: int) -> int { return x; }\n";
/* Low(298) is 298's low byte, 42, shifted left once; Main widens it to int. */
static const char
    WIDTH_SOURCE[] = "package scalar\n"
                     "Main :: () -> int { return int(Low(298)); }\n"
                     "Low :: (x: int) -> u8 { return u8(wrap: x) << 1; }\n";
/* Calc(3, 4) is -(3 + 4) * ~4, which is 35. */
static const char ARITHMETIC_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return Calc(3, 4); }\n"
         "Calc :: (a: int, b: int) -> int { return -(a + b) * ~b; }\n";
/* scalar_facts row kinds and types. */
enum
{
    SCALAR_SOURCE_ROW = 0,
    SCALAR_REJECTION_ROW = 1,
    SCALAR_DOCUMENT_ROW = 2,
    SCALAR_FUNCTION_ROW = 3,
    SCALAR_PARAMETER_ROW = 4,
    SCALAR_RETURN_ROW = 5,
    SCALAR_LITERAL_ROW = 6,
    SCALAR_READ_ROW = 7,
    SCALAR_CALL_ROW = 8,
    SCALAR_IF_ROW = 9,
    SCALAR_COMPARE_ROW = 10,
    SCALAR_ARITHMETIC_ROW = 11,
    SCALAR_UNARY_ROW = 12,
    SCALAR_CONVERT_ROW = 13,
    SCALAR_LOCAL_ROW = 14,
    SCALAR_ASSIGN_ROW = 15,
    SCALAR_ELSE_ROW = 16,
    SCALAR_END_ROW = 17,
    SCALAR_LOGICAL_ROW = 18,
    SCALAR_WHILE_ROW = 19,
    SCALAR_FOR_ROW = 20,
    SCALAR_BREAK_ROW = 21,
    SCALAR_CONTINUE_ROW = 22,
    SCALAR_SWITCH_ROW = 23,
    SCALAR_CASE_ROW = 24,
    SCALAR_RECORD_ROW = 25,
    SCALAR_MEMBER_ROW = 26,
    SCALAR_ZERO_ROW = 27,
    SCALAR_UPDATE_ROW = 28,
    SCALAR_FIELD_ROW = 29,
    SCALAR_UNION_ROW = 30,
    SCALAR_VARIANT_ROW = 31,
    SCALAR_MAKE_ROW = 32,
    SCALAR_TAG_ROW = 33,
    SCALAR_PAYLOAD_ROW = 34,
    SCALAR_TEXT_ROW = 35,
    SCALAR_ASSERT_ROW = 36,
    SCALAR_TEXT_LENGTH = 1,
    SCALAR_TEXT_BYTE = 2,
    SCALAR_TEXT_SLICE = 3,
    SCALAR_TEXT_FORMAT = 4,
    SCALAR_RANGE_EXCLUSIVE = 1,
    SCALAR_INT = 1,
    SCALAR_BOOL = 2,
    SCALAR_INT8 = 3,
    SCALAR_BYTE = 4,
    SCALAR_INT16 = 5,
    SCALAR_UINT16 = 6,
    SCALAR_UINT = 7,
    SCALAR_INT64 = 8,
    SCALAR_WORD = 9,
    SCALAR_FLOAT = 10,
    SCALAR_DOUBLE = 11,
    SCALAR_STRING = 12,
    SCALAR_RECORD = 32
};
/* The kind of each of GUARD_SOURCE's rows, and the rows edited. */
static const int GUARD_KINDS[] = {
    SCALAR_SOURCE_ROW,    SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_CALL_ROW,      SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_IF_ROW,       SCALAR_READ_ROW,
    SCALAR_READ_ROW,      SCALAR_COMPARE_ROW,  SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,      SCALAR_CALL_ROW,     SCALAR_END_ROW,
    SCALAR_RETURN_ROW,    SCALAR_READ_ROW,     SCALAR_FUNCTION_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_RETURN_ROW,   SCALAR_READ_ROW,
};
enum
{
    GUARD_CALLEE = 7,
    GUARD_ROW = 10,
    GUARD_CONDITION = 11,
    GUARD_COMPARE = 13,
    GUARD_ARM = 14,
    GUARD_ARM_READ = 15,
    GUARD_END = 17,
    GUARD_FINAL = 18,
    GUARD_ID_READ = 23,
    GUARD_ROWS = (int)(sizeof GUARD_KINDS / sizeof *GUARD_KINDS),
    GUARD_EDITS = 5
};
/* Pick declares x, then in an if statement whose condition is a logical
 * and its consequence's own local y, adds y to x, and otherwise returns b.
 * Pick(1, 2) is 3. */
static const char
    CONTROL_ROWS_SOURCE[] = "package scalar\n"
                            "Main :: () -> int { return Pick(1, 2); }\n"
                            "Pick :: (a: int, b: int) -> int {\n"
                            "    x := a;\n"
                            "    if (a < b && !(b < 0)) {\n"
                            "        y := b;\n"
                            "        x += y;\n"
                            "    } else {\n"
                            "        return b;\n"
                            "    }\n"
                            "    return x;\n"
                            "}\n";
/* The kind of each of CONTROL_ROWS_SOURCE's rows, and the rows edited. */
static const int CONTROL_KINDS[] = {
    SCALAR_SOURCE_ROW,    SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_CALL_ROW,      SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_LOCAL_ROW,    SCALAR_READ_ROW,
    SCALAR_IF_ROW,        SCALAR_READ_ROW,     SCALAR_READ_ROW,
    SCALAR_COMPARE_ROW,   SCALAR_LOGICAL_ROW,  SCALAR_READ_ROW,
    SCALAR_LITERAL_ROW,   SCALAR_COMPARE_ROW,  SCALAR_UNARY_ROW,
    SCALAR_LOCAL_ROW,     SCALAR_READ_ROW,     SCALAR_ASSIGN_ROW,
    SCALAR_READ_ROW,      SCALAR_READ_ROW,     SCALAR_ARITHMETIC_ROW,
    SCALAR_ELSE_ROW,      SCALAR_RETURN_ROW,   SCALAR_READ_ROW,
    SCALAR_END_ROW,       SCALAR_RETURN_ROW,   SCALAR_READ_ROW,
};
enum
{
    CONTROL_X = 10,
    CONTROL_X_VALUE = 11,
    CONTROL_IF = 12,
    CONTROL_LOGICAL = 16,
    CONTROL_NOT = 20,
    CONTROL_Y = 21,
    CONTROL_ASSIGN = 23,
    CONTROL_ELSE = 27,
    CONTROL_ELSE_READ = 29,
    CONTROL_END = 30,
    CONTROL_FINAL = 31,
    CONTROL_FINAL_READ = 32,
    CONTROL_ROWS = (int)(sizeof CONTROL_KINDS / sizeof *CONTROL_KINDS)
};
/* Sum sums a for loop's indices until a break, then counts up in an endless
 * while loop whose continue returns to its head. Sum(3) is 2. */
static const char
    LOOP_ROWS_SOURCE[] = "package scalar\n"
                         "Main :: () -> int { return Sum(3); }\n"
                         "Sum :: (n: int) -> int {\n"
                         "    total := 0;\n"
                         "    for (i in 0..<n) {\n"
                         "        if (i == 1) { break; }\n"
                         "        total += i;\n"
                         "    }\n"
                         "    while (true) {\n"
                         "        if (total > 1) { return total; }\n"
                         "        total += 1;\n"
                         "        if (total < 0) { continue; }\n"
                         "    }\n"
                         "}\n";
/* The kind of each of LOOP_ROWS_SOURCE's rows, and the rows edited. */
static const int LOOP_KINDS[] = {
    SCALAR_SOURCE_ROW,     SCALAR_DOCUMENT_ROW,   SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,     SCALAR_LITERAL_ROW,    SCALAR_CALL_ROW,
    SCALAR_FUNCTION_ROW,   SCALAR_PARAMETER_ROW,  SCALAR_LOCAL_ROW,
    SCALAR_LITERAL_ROW,    SCALAR_FOR_ROW,        SCALAR_LITERAL_ROW,
    SCALAR_READ_ROW,       SCALAR_IF_ROW,         SCALAR_READ_ROW,
    SCALAR_LITERAL_ROW,    SCALAR_COMPARE_ROW,    SCALAR_BREAK_ROW,
    SCALAR_END_ROW,        SCALAR_ASSIGN_ROW,     SCALAR_READ_ROW,
    SCALAR_READ_ROW,       SCALAR_ARITHMETIC_ROW, SCALAR_END_ROW,
    SCALAR_WHILE_ROW,      SCALAR_LITERAL_ROW,    SCALAR_IF_ROW,
    SCALAR_READ_ROW,       SCALAR_LITERAL_ROW,    SCALAR_COMPARE_ROW,
    SCALAR_RETURN_ROW,     SCALAR_READ_ROW,       SCALAR_END_ROW,
    SCALAR_ASSIGN_ROW,     SCALAR_READ_ROW,       SCALAR_LITERAL_ROW,
    SCALAR_ARITHMETIC_ROW, SCALAR_IF_ROW,         SCALAR_READ_ROW,
    SCALAR_LITERAL_ROW,    SCALAR_COMPARE_ROW,    SCALAR_CONTINUE_ROW,
    SCALAR_END_ROW,        SCALAR_END_ROW,
};
enum
{
    LOOP_FOR = 10,
    LOOP_FIRST = 11,
    LOOP_LAST = 12,
    LOOP_GUARD = 13,
    LOOP_BREAK = 17,
    LOOP_ADD = 19,
    LOOP_FOR_END = 23,
    LOOP_WHILE = 24,
    LOOP_TRUE = 25,
    LOOP_COUNT_READ = 34,
    LOOP_CONTINUE = 41,
    LOOP_WHILE_END = 43,
    LOOP_ROWS = (int)(sizeof LOOP_KINDS / sizeof *LOOP_KINDS)
};
/* Pick switches on x: its default arm, in the middle, declares y. Every arm
 * returns, so the switch ends Pick. Pick(2) is 20. */
static const char
    SWITCH_ROWS_SOURCE[] = "package scalar\n"
                           "Main :: () -> int { return Pick(2); }\n"
                           "Pick :: (x: int) -> int {\n"
                           "    switch x {\n"
                           "        case 1: return 10;\n"
                           "        case: y := x; return y;\n"
                           "        case 2: return 20;\n"
                           "    }\n"
                           "}\n";
/* The kind of each of SWITCH_ROWS_SOURCE's rows, and the rows edited. */
static const int SWITCH_KINDS[] = {
    SCALAR_SOURCE_ROW,   SCALAR_DOCUMENT_ROW,  SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,   SCALAR_LITERAL_ROW,   SCALAR_CALL_ROW,
    SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW, SCALAR_SWITCH_ROW,
    SCALAR_READ_ROW,     SCALAR_CASE_ROW,      SCALAR_LITERAL_ROW,
    SCALAR_RETURN_ROW,   SCALAR_LITERAL_ROW,   SCALAR_CASE_ROW,
    SCALAR_LOCAL_ROW,    SCALAR_READ_ROW,      SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,     SCALAR_CASE_ROW,      SCALAR_LITERAL_ROW,
    SCALAR_RETURN_ROW,   SCALAR_LITERAL_ROW,   SCALAR_END_ROW,
};
enum
{
    SWITCH_FUNCTION = 6,
    SWITCH_ROW = 8,
    SWITCH_VALUE = 9,
    SWITCH_FIRST = 10,
    SWITCH_FIRST_VALUE = 11,
    SWITCH_FIRST_RETURN = 12,
    SWITCH_DEFAULT = 14,
    SWITCH_LAST = 19,
    SWITCH_LAST_RETURN = 21,
    SWITCH_LAST_VALUE = 22,
    SWITCH_END = 23,
    SWITCH_ROWS = (int)(sizeof SWITCH_KINDS / sizeof *SWITCH_KINDS)
};
/* Make builds a Point from its zero, its fields in another order than
 * declared, and Main adds 1 to the x field of the copy it returns, then
 * returns that field: Make(3) gives 4. */
static const char RECORD_ROWS_SOURCE
    [] = "package scalar\n"
         "Point :: struct { x: int; y: number; }\n"
         "Main :: () -> int { p := Make(3); p.x += 1; return p.x; }\n"
         "Make :: (x: int) -> Point { return {y: 0.5, x: x}; }\n";
/* The kind of each of RECORD_ROWS_SOURCE's rows, and the rows edited. */
static const int RECORD_KINDS[] = {
    SCALAR_SOURCE_ROW, SCALAR_DOCUMENT_ROW, SCALAR_RECORD_ROW,
    SCALAR_MEMBER_ROW, SCALAR_MEMBER_ROW,   SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,  SCALAR_LITERAL_ROW,  SCALAR_CALL_ROW,
    SCALAR_ASSIGN_ROW, SCALAR_READ_ROW,     SCALAR_READ_ROW,
    SCALAR_FIELD_ROW,  SCALAR_LITERAL_ROW,  SCALAR_ARITHMETIC_ROW,
    SCALAR_UPDATE_ROW, SCALAR_RETURN_ROW,   SCALAR_READ_ROW,
    SCALAR_FIELD_ROW,  SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW,
    SCALAR_RETURN_ROW, SCALAR_ZERO_ROW,     SCALAR_LITERAL_ROW,
    SCALAR_UPDATE_ROW, SCALAR_READ_ROW,     SCALAR_UPDATE_ROW,
};
enum
{
    RECORD_DECLARATION = 2,
    RECORD_X = 3,
    RECORD_Y = 4,
    RECORD_LOCAL = 6,
    RECORD_ASSIGN = 9,
    RECORD_COMPOUND_FIELD = 12,
    RECORD_COMPOUND_UPDATE = 15,
    RECORD_READ = 17,
    RECORD_FIELD = 18,
    RECORD_MAKE = 19,
    RECORD_ZERO = 22,
    RECORD_Y_UPDATE = 24,
    RECORD_ROWS = (int)(sizeof RECORD_KINDS / sizeof *RECORD_KINDS)
};
/* Make builds the Box variant of Shape, and Main tests it with `is`, then
 * switches on it, binding the Box arm's payloads: Make(3) gives 3. The last
 * arm is the switch's default, since it has none. */
static const char UNION_ROWS_SOURCE
    [] = "package scalar\n"
         "Shape :: union { Dot | Box(w: int, h: number) }\n"
         "Main :: () -> int {\n"
         "    s := Make(3);\n"
         "    if (s is .Dot) { return 0; }\n"
         "    switch s { case .Dot: return 1; case .Box(w, h): return w; }\n"
         "}\n"
         "Make :: (w: int) -> Shape { return Shape.Box(w, 0.5); }\n";
/* The kind of each of UNION_ROWS_SOURCE's rows, and the rows edited. */
static const int UNION_KINDS[] = {
    SCALAR_SOURCE_ROW,   SCALAR_DOCUMENT_ROW,  SCALAR_UNION_ROW,
    SCALAR_VARIANT_ROW,  SCALAR_VARIANT_ROW,   SCALAR_MEMBER_ROW,
    SCALAR_MEMBER_ROW,   SCALAR_FUNCTION_ROW,  SCALAR_LOCAL_ROW,
    SCALAR_LITERAL_ROW,  SCALAR_CALL_ROW,      SCALAR_IF_ROW,
    SCALAR_READ_ROW,     SCALAR_TAG_ROW,       SCALAR_LITERAL_ROW,
    SCALAR_COMPARE_ROW,  SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,
    SCALAR_END_ROW,      SCALAR_SWITCH_ROW,    SCALAR_READ_ROW,
    SCALAR_TAG_ROW,      SCALAR_CASE_ROW,      SCALAR_LITERAL_ROW,
    SCALAR_RETURN_ROW,   SCALAR_LITERAL_ROW,   SCALAR_CASE_ROW,
    SCALAR_LOCAL_ROW,    SCALAR_READ_ROW,      SCALAR_PAYLOAD_ROW,
    SCALAR_LOCAL_ROW,    SCALAR_READ_ROW,      SCALAR_PAYLOAD_ROW,
    SCALAR_RETURN_ROW,   SCALAR_READ_ROW,      SCALAR_END_ROW,
    SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW, SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,     SCALAR_LITERAL_ROW,   SCALAR_MAKE_ROW,
};
enum
{
    UNION_DECLARATION = 2,
    UNION_DOT = 3,
    UNION_BOX = 4,
    UNION_W = 5,
    UNION_H = 6,
    UNION_MAIN = 7,
    UNION_LOCAL = 8,
    UNION_IS_TAG = 13,
    UNION_IS_VARIANT = 14,
    UNION_DOT_RETURN = 17,
    UNION_SWITCH = 19,
    UNION_SWITCH_TAG = 21,
    UNION_DOT_ARM = 22,
    UNION_DOT_VARIANT = 23,
    UNION_BOX_ARM = 26,
    UNION_W_LOCAL = 27,
    UNION_W_PAYLOAD = 29,
    UNION_H_LOCAL = 30,
    UNION_H_PAYLOAD = 32,
    UNION_MAKE = 41,
    UNION_ROWS = (int)(sizeof UNION_KINDS / sizeof *UNION_KINDS)
};
/* Main passes Name a string and compares its result with an equal literal,
 * so the module's one text serves both: Main returns 1. */
static const char
    STRING_ROWS_SOURCE[] = "package scalar\n"
                           "Main :: () -> int {\n"
                           "    s := Name(\"one\");\n"
                           "    if (s != \"one\") { return 0; }\n"
                           "    return 1;\n"
                           "}\n"
                           "Name :: (n: string) -> string { return n; }\n";
/* The kind of each of STRING_ROWS_SOURCE's rows, and the rows edited. */
static const int STRING_KINDS[] = {
    SCALAR_SOURCE_ROW,   SCALAR_DOCUMENT_ROW,  SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,    SCALAR_LITERAL_ROW,   SCALAR_CALL_ROW,
    SCALAR_IF_ROW,       SCALAR_READ_ROW,      SCALAR_LITERAL_ROW,
    SCALAR_COMPARE_ROW,  SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,
    SCALAR_END_ROW,      SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,
    SCALAR_FUNCTION_ROW, SCALAR_PARAMETER_ROW, SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,
};
enum
{
    STRING_LOCAL = 3,
    STRING_ARGUMENT = 4,
    STRING_READ = 7,
    STRING_LITERAL = 8,
    STRING_COMPARE = 9,
    STRING_ZERO = 11,
    STRING_NAME = 15,
    STRING_PARAMETER = 16,
    STRING_PARAMETER_READ = 18,
    STRING_ROWS = (int)(sizeof STRING_KINDS / sizeof *STRING_KINDS)
};
/* Main slices a literal, then reads its view's byte count and first byte:
 * 2 + 'b'. */
static const char TEXT_ROWS_SOURCE[] = "package scalar\n"
                                       "Main :: () -> int {\n"
                                       "    s := text_slice(\"abcd\", 1, 2);\n"
                                       "    return text_byte_length(s) + "
                                       "text_byte_at(s, 0);\n"
                                       "}\n";
/* The kind of each of TEXT_ROWS_SOURCE's rows, and the rows edited. */
static const int TEXT_KINDS[] = {
    SCALAR_SOURCE_ROW,  SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,   SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_LITERAL_ROW, SCALAR_TEXT_ROW,     SCALAR_RETURN_ROW,
    SCALAR_READ_ROW,    SCALAR_TEXT_ROW,     SCALAR_READ_ROW,
    SCALAR_LITERAL_ROW, SCALAR_TEXT_ROW,     SCALAR_ARITHMETIC_ROW,
};
enum
{
    TEXT_SOURCE = 4,
    TEXT_START = 5,
    TEXT_SLICE = 7,
    TEXT_LENGTH = 10,
    TEXT_INDEX = 12,
    TEXT_BYTE = 13,
    TEXT_ROWS = (int)(sizeof TEXT_KINDS / sizeof *TEXT_KINDS)
};
/* Main formats an i64, a bool and a string and returns the byte count of
 * what it gives. */
static const char FORMAT_ROWS_SOURCE[] = "package scalar\n"
                                         "Main :: () -> int {\n"
                                         "    s := format_text(\"{}{}{}\", "
                                         "i64(7), true, \"x\");\n"
                                         "    return text_byte_length(s);\n"
                                         "}\n";
/* The kind of each of FORMAT_ROWS_SOURCE's rows, and the rows edited. */
static const int FORMAT_KINDS[] = {
    SCALAR_SOURCE_ROW,  SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,   SCALAR_LITERAL_ROW,  SCALAR_LITERAL_ROW,
    SCALAR_LITERAL_ROW, SCALAR_LITERAL_ROW,  SCALAR_TEXT_ROW,
    SCALAR_RETURN_ROW,  SCALAR_READ_ROW,     SCALAR_TEXT_ROW,
};
enum
{
    FORMAT_FORMAT = 4,
    FORMAT_INT = 5,
    FORMAT_TEXT = 8,
    FORMAT_ROWS = (int)(sizeof FORMAT_KINDS / sizeof *FORMAT_KINDS)
};
/* Main asserts a comparison before it returns. */
static const char ASSERT_ROWS_SOURCE[] = "package scalar\n"
                                         "Main :: () -> int {\n"
                                         "    x: int = 1;\n"
                                         "    assert(x < 2, \"x is small\");\n"
                                         "    return x;\n"
                                         "}\n";
/* The kind of each of ASSERT_ROWS_SOURCE's rows, and the rows edited. */
static const int ASSERT_KINDS[] = {
    SCALAR_SOURCE_ROW, SCALAR_DOCUMENT_ROW, SCALAR_FUNCTION_ROW,
    SCALAR_LOCAL_ROW,  SCALAR_LITERAL_ROW,  SCALAR_ASSERT_ROW,
    SCALAR_READ_ROW,   SCALAR_LITERAL_ROW,  SCALAR_COMPARE_ROW,
    SCALAR_RETURN_ROW, SCALAR_READ_ROW,
};
enum
{
    ASSERT_STATEMENT = 5,
    ASSERT_COMPARE = 8,
    ASSERT_ROWS = (int)(sizeof ASSERT_KINDS / sizeof *ASSERT_KINDS)
};
/* One field of one row set to `value`, to `text` when it is set, or to the
 * same field of row `from` when it is set. */
typedef struct row_edit
{
    int row;
    const char *field;
    int value;
    const char *text;
    int from;
} row_edit;
typedef struct malformed_guard
{
    row_edit edits[GUARD_EDITS];
    const char *message;
} malformed_guard;
#define EDIT(r, f, v) { .row = (r), .field = (f), .value = (v), .from = -1 }
#define COPY(r, f, source) { .row = (r), .field = (f), .from = (source) }
static const char LAYOUT[] = "invalid scalar module layout",
                  COMPARISON[] = "scalar comparison differs from its operands",
                  SEGMENT[] = "invalid scalar statement row";
/* The kind of each of ARITHMETIC_SOURCE's rows, and the rows edited. */
static const int ARITHMETIC_KINDS[] = {
    SCALAR_SOURCE_ROW,    SCALAR_DOCUMENT_ROW,   SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,    SCALAR_LITERAL_ROW,    SCALAR_LITERAL_ROW,
    SCALAR_CALL_ROW,      SCALAR_FUNCTION_ROW,   SCALAR_PARAMETER_ROW,
    SCALAR_PARAMETER_ROW, SCALAR_RETURN_ROW,     SCALAR_READ_ROW,
    SCALAR_READ_ROW,      SCALAR_ARITHMETIC_ROW, SCALAR_UNARY_ROW,
    SCALAR_READ_ROW,      SCALAR_UNARY_ROW,      SCALAR_ARITHMETIC_ROW,
};
enum
{
    ARITHMETIC_SECOND_READ = 12,
    ARITHMETIC_ADD = 13,
    ARITHMETIC_NEGATE = 14,
    ARITHMETIC_LAST_READ = 15,
    ARITHMETIC_ROWS = (int)(sizeof ARITHMETIC_KINDS / sizeof *ARITHMETIC_KINDS)
};
static const char
    ARITHMETIC[] = "scalar arithmetic differs from its number operands";
static const malformed_guard MALFORMED_GUARDS[] = {
    /* An if statement with no condition, one that never ends, and an else
     * with no end. */
    { { EDIT(GUARD_ROW, "count", 0) }, LAYOUT },
    { { EDIT(GUARD_FINAL, "kind", SCALAR_IF_ROW) }, LAYOUT },
    { { EDIT(GUARD_END, "kind", SCALAR_ELSE_ROW) }, LAYOUT },
    /* A function whose parameter count would wrap past the rows. */
    { { EDIT(GUARD_CALLEE, "count", INT32_MAX) }, LAYOUT },
    /* A comparison's operator, arity and result. */
    { { EDIT(GUARD_COMPARE, "value", 0) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "value", 7) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "count", 1) }, COMPARISON },
    { { EDIT(GUARD_COMPARE, "type", SCALAR_INT) }, COMPARISON },
    /* A comparison of an int with a bool, and an ordering of two bools. */
    { { EDIT(GUARD_CONDITION + 1, "kind", SCALAR_LITERAL_ROW),
        EDIT(GUARD_CONDITION + 1, "type", SCALAR_BOOL),
        EDIT(GUARD_CONDITION + 1, "value", 0),
        EDIT(GUARD_CONDITION + 1, "literal", 1) },
      COMPARISON },
    { { EDIT(GUARD_CONDITION, "kind", SCALAR_LITERAL_ROW),
        EDIT(GUARD_CONDITION, "type", SCALAR_BOOL),
        EDIT(GUARD_CONDITION + 1, "kind", SCALAR_LITERAL_ROW),
        EDIT(GUARD_CONDITION + 1, "type", SCALAR_BOOL),
        EDIT(GUARD_CONDITION + 1, "value", 0) },
      COMPARISON },
    /* A condition that leaves three ints rather than one bool: the
     * comparison becomes a read of b. */
    { { EDIT(GUARD_COMPARE, "kind", SCALAR_READ_ROW),
        EDIT(GUARD_COMPARE, "type", SCALAR_INT),
        EDIT(GUARD_COMPARE, "count", 0) },
      "scalar condition is not one bool" },
    /* An if row that carries a type, a consequence that ends past its if
     * statement, a condition operand inside the consequence, a final return
     * that starts inside the if statement, and an end with another range. */
    { { EDIT(GUARD_ROW, "type", SCALAR_INT) }, SEGMENT },
    { { COPY(GUARD_ARM, "end", GUARD_FINAL) }, SEGMENT },
    { { COPY(GUARD_CONDITION, "start", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "end", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "line", GUARD_ARM_READ),
        COPY(GUARD_CONDITION, "column", GUARD_ARM_READ) },
      SEGMENT },
    { { COPY(GUARD_FINAL, "start", GUARD_ROW),
        COPY(GUARD_FINAL, "line", GUARD_ROW),
        COPY(GUARD_FINAL, "column", GUARD_ROW) },
      SEGMENT },
    { { COPY(GUARD_END, "end", GUARD_FINAL) }, SEGMENT },
    /* A read past the variables, and a body that ends in an assignment. */
    { { EDIT(GUARD_ARM_READ, "value", 2) }, "scalar read differs from its variable" },
    { { EDIT(GUARD_FINAL, "kind", SCALAR_ASSIGN_ROW),
        EDIT(GUARD_FINAL, "type", SCALAR_INT),
        EDIT(GUARD_FINAL, "value", 1) },
      "scalar function can end without returning" },
    /* Id calls Main, so the arm's call recurses, under another name. */
    { { EDIT(GUARD_ID_READ, "kind", SCALAR_CALL_ROW),
        EDIT(GUARD_ID_READ, "value", 0),
        { .row = GUARD_ID_READ, .field = "name", .text = "Other", .from = -1 } },
      "scalar call differs from its callee's signature" },
};
static const char LOGIC[] = "scalar logic differs from its bool operands",
                  READ[] = "scalar read differs from its variable";
static const malformed_guard MALFORMED_CONTROL[] = {
    /* A logical with no right rows, with right rows past its condition, whose
     * right side is an int, or with another operator. */
    { { EDIT(CONTROL_LOGICAL, "count", 0) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "count", 5) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "count", 1) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "count", INT32_MAX) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "value", 3) }, LOGIC },
    { { EDIT(CONTROL_LOGICAL, "type", SCALAR_INT) }, LOGIC },
    /* A not of an int. */
    { { EDIT(CONTROL_NOT, "type", SCALAR_INT) }, ARITHMETIC },
    /* x read in its own initializer, y after its consequence and in the
     * alternative. */
    { { EDIT(CONTROL_X_VALUE, "value", 2) }, READ },
    { { EDIT(CONTROL_FINAL_READ, "value", 3) }, READ },
    { { EDIT(CONTROL_ELSE_READ, "value", 3) }, READ },
    /* A local that is not the next variable, has no name or no scalar type. */
    { { EDIT(CONTROL_X, "value", 3) }, "invalid scalar local row" },
    { { { .row = CONTROL_X, .field = "name", .text = "", .from = -1 } },
      "invalid scalar local row" },
    { { EDIT(CONTROL_X, "type", 0) }, "invalid scalar local row" },
    /* An assignment to no visible variable, or of another type. */
    { { EDIT(CONTROL_ASSIGN, "value", 5) }, "scalar assignment differs from its variable" },
    { { EDIT(CONTROL_ASSIGN, "type", SCALAR_BOOL) },
      "scalar assignment differs from its variable" },
    /* A second else, an else outside its if statement, and an end of
     * another range. */
    { { EDIT(CONTROL_END, "kind", SCALAR_ELSE_ROW) }, LAYOUT },
    { { COPY(CONTROL_ELSE, "end", CONTROL_FINAL) }, SEGMENT },
    { { COPY(CONTROL_END, "start", CONTROL_ELSE),
        COPY(CONTROL_END, "line", CONTROL_ELSE),
        COPY(CONTROL_END, "column", CONTROL_ELSE) },
      SEGMENT },
    /* An if statement with an else whose consequence falls through ends the
     * body only when its alternative does too: the final return becomes an
     * assignment. */
    { { EDIT(CONTROL_FINAL, "kind", SCALAR_ASSIGN_ROW),
        EDIT(CONTROL_FINAL, "type", SCALAR_INT),
        EDIT(CONTROL_FINAL, "value", 2) },
      "scalar function can end without returning" },
};
static const char LOCAL[] = "invalid scalar local row",
                  NO_RETURN[] = "scalar function can end without returning";
static const malformed_guard MALFORMED_LOOPS[] = {
    /* A continue that breaks out of the endless loop, an endless loop on
     * false, and a while row that becomes an if statement, which leaves the
     * continue outside every loop. */
    { { EDIT(LOOP_CONTINUE, "kind", SCALAR_BREAK_ROW) }, NO_RETURN },
    { { EDIT(LOOP_TRUE, "literal", 0) }, NO_RETURN },
    { { EDIT(LOOP_WHILE, "kind", SCALAR_IF_ROW) }, LAYOUT },
    /* A break with an operand, a type or a value. */
    { { EDIT(LOOP_BREAK, "count", 1) }, LAYOUT },
    { { EDIT(LOOP_BREAK, "type", SCALAR_INT) }, SEGMENT },
    { { EDIT(LOOP_CONTINUE, "value", 1) }, SEGMENT },
    /* A while condition that is an int and a range bound that is a bool. */
    { { EDIT(LOOP_TRUE, "type", SCALAR_INT) }, "scalar condition is not one bool" },
    { { EDIT(LOOP_FIRST, "type", SCALAR_BOOL) }, "scalar range differs from its int bounds" },
    /* An index that is not the next variable, is not an int, has unknown
     * flags or no name. */
    { { EDIT(LOOP_FOR, "value", 5) }, LOCAL },
    { { EDIT(LOOP_FOR, "type", SCALAR_BOOL) }, LOCAL },
    { { EDIT(LOOP_FOR, "literal", 4) }, LOCAL },
    { { { .row = LOOP_FOR, .field = "name", .text = "", .from = -1 } }, LOCAL },
    /* An assignment to the index, and reads of the index in its own bound
     * and after its loop. */
    { { EDIT(LOOP_ADD, "value", 2) }, "scalar assignment differs from its variable" },
    { { EDIT(LOOP_LAST, "value", 2) }, READ },
    { { EDIT(LOOP_COUNT_READ, "value", 2) }, READ },
    /* A loop end of another range, and a body statement that starts inside
     * the bounds. */
    { { COPY(LOOP_FOR_END, "end", LOOP_WHILE_END) }, SEGMENT },
    { { COPY(LOOP_GUARD, "start", LOOP_LAST),
        COPY(LOOP_GUARD, "line", LOOP_LAST),
        COPY(LOOP_GUARD, "column", LOOP_LAST) },
      SEGMENT },
};
static const char
    SWITCH_VALUE_TYPE[] = "scalar switch value is not one integer";
static const malformed_guard MALFORMED_SWITCHES[] = {
    /* A statement before the first arm, an arm outside a switch, and a
     * second default arm. */
    { { EDIT(SWITCH_FIRST, "kind", SCALAR_RETURN_ROW) }, LAYOUT },
    { { EDIT(SWITCH_ROW, "kind", SCALAR_IF_ROW) }, LAYOUT },
    { { EDIT(SWITCH_FIRST, "count", 0),
        EDIT(SWITCH_FIRST_VALUE, "kind", SCALAR_CASE_ROW),
        EDIT(SWITCH_FIRST_VALUE, "literal", 0) },
      SEGMENT },
    /* A switch on a bool, on a float, and of a type its value is not. */
    { { EDIT(SWITCH_ROW, "type", SCALAR_BOOL) }, SWITCH_VALUE_TYPE },
    { { EDIT(SWITCH_ROW, "type", SCALAR_FLOAT) }, SWITCH_VALUE_TYPE },
    { { EDIT(SWITCH_ROW, "type", SCALAR_INT64) }, SWITCH_VALUE_TYPE },
    /* An arm of another type than its switch, and an arm whose value is. */
    { { EDIT(SWITCH_FIRST, "type", SCALAR_BYTE) }, SEGMENT },
    { { EDIT(SWITCH_FIRST_VALUE, "type", SCALAR_BOOL) },
      "scalar case value differs from its switch value" },
    /* An arm with a value or a name, one that ends past its switch, an arm
     * statement inside its arm's value, and an end of another range. */
    { { EDIT(SWITCH_LAST, "value", 1) }, SEGMENT },
    { { { .row = SWITCH_LAST, .field = "name", .text = "x", .from = -1 } }, SEGMENT },
    { { COPY(SWITCH_LAST, "end", SWITCH_FUNCTION) }, SEGMENT },
    { { COPY(SWITCH_FIRST_RETURN, "start", SWITCH_FIRST_VALUE),
        COPY(SWITCH_FIRST_RETURN, "line", SWITCH_FIRST_VALUE),
        COPY(SWITCH_FIRST_RETURN, "column", SWITCH_FIRST_VALUE) },
      SEGMENT },
    { { COPY(SWITCH_END, "end", SWITCH_FUNCTION) }, SEGMENT },
    /* The default arm's y read in the arm after it. */
    { { EDIT(SWITCH_LAST_VALUE, "kind", SCALAR_READ_ROW),
        EDIT(SWITCH_LAST_VALUE, "value", 1),
        EDIT(SWITCH_LAST_VALUE, "literal", 0) },
      READ },
    /* An arm that does not return: its return becomes an assignment. */
    { { EDIT(SWITCH_LAST_RETURN, "kind", SCALAR_ASSIGN_ROW),
        EDIT(SWITCH_LAST_RETURN, "type", SCALAR_INT),
        EDIT(SWITCH_LAST_RETURN, "value", 0) },
      NO_RETURN },
};
static const char RECORD_ROW[] = "invalid scalar record row",
                  MEMBER_ROW[] = "invalid scalar record member row",
                  FIELD_READ[] = "scalar field read differs from its record",
                  FIELD_WRITE[] = "scalar field write differs from its record";
static const malformed_guard MALFORMED_RECORDS[] = {
    /* A record whose members overrun into the function after them, one
     * with no name, and a record that does not lie where it is declared. */
    { { EDIT(RECORD_DECLARATION, "count", 3) }, MEMBER_ROW },
    { { EDIT(RECORD_DECLARATION, "count", INT32_MAX) }, RECORD_ROW },
    { { { .row = RECORD_DECLARATION, .field = "name", .text = "", .from = -1 } },
      RECORD_ROW },
    { { EDIT(RECORD_DECLARATION, "type", SCALAR_INT) }, RECORD_ROW },
    /* A member of a record type, out of order, named as another, or
     * outside its record. */
    { { EDIT(RECORD_Y, "type", SCALAR_RECORD) }, MEMBER_ROW },
    { { EDIT(RECORD_Y, "type", SCALAR_STRING) }, MEMBER_ROW },
    { { EDIT(RECORD_X, "value", 1) }, MEMBER_ROW },
    { { { .row = RECORD_Y, .field = "name", .text = "x", .from = -1 } }, MEMBER_ROW },
    { { COPY(RECORD_Y, "end", RECORD_MAKE) }, MEMBER_ROW },
    /* A function returning a record the module does not declare, which
     * its caller's call reaches first, and a local or read of another type
     * than its value or variable. */
    { { EDIT(RECORD_MAKE, "type", SCALAR_RECORD + 1) },
      "scalar call differs from its callee's signature" },
    { { EDIT(RECORD_LOCAL, "type", SCALAR_INT) }, "scalar value differs from its variable" },
    { { EDIT(RECORD_ASSIGN, "type", SCALAR_INT) },
      "scalar assignment differs from its variable" },
    { { EDIT(RECORD_READ, "type", SCALAR_INT) }, READ },
    /* A zero of a scalar type; a field read of the float field, as a float
     * or of a scalar; a write of the int to the float field or to a
     * scalar. */
    { { EDIT(RECORD_ZERO, "type", SCALAR_INT) }, "scalar record zero names no record" },
    { { EDIT(RECORD_FIELD, "value", 1) }, FIELD_READ },
    { { EDIT(RECORD_FIELD, "value", 2) }, FIELD_READ },
    { { EDIT(RECORD_FIELD, "type", SCALAR_FLOAT) }, FIELD_READ },
    { { EDIT(RECORD_FIELD, "count", 2) }, FIELD_READ },
    { { EDIT(RECORD_COMPOUND_FIELD, "value", -1) }, FIELD_READ },
    { { EDIT(RECORD_Y_UPDATE, "value", 0) }, FIELD_WRITE },
    { { EDIT(RECORD_COMPOUND_UPDATE, "value", 1) }, FIELD_WRITE },
    { { EDIT(RECORD_COMPOUND_UPDATE, "type", SCALAR_INT) }, FIELD_WRITE },
    { { EDIT(RECORD_COMPOUND_UPDATE, "count", 1) }, FIELD_WRITE },
};
static const char UNION_ROW[] = "invalid scalar union row",
                  VARIANT_ROW[] = "invalid scalar union variant row",
                  PAYLOAD_ROW[] = "invalid scalar union member row",
                  VARIANT[] = "scalar variant differs from its union",
                  TAG[] = "scalar tag reads no union",
                  PAYLOAD[] = "scalar payload read differs from its union";
static const malformed_guard MALFORMED_UNIONS[] = {
    /* A union whose variants overrun into its payloads, one with no name,
     * one that does not lie where it is declared, and one named as a
     * record would be. */
    { { EDIT(UNION_DECLARATION, "count", 3) }, UNION_ROW },
    { { { .row = UNION_DECLARATION, .field = "name", .text = "", .from = -1 } }, UNION_ROW },
    { { EDIT(UNION_DECLARATION, "type", SCALAR_INT) }, UNION_ROW },
    { { EDIT(UNION_DECLARATION, "value", 1) }, UNION_ROW },
    /* A variant out of order, named as another, outside its union, or with
     * fewer payloads than follow it, which leaves a member where a function
     * must start. */
    { { EDIT(UNION_BOX, "value", 0) }, VARIANT_ROW },
    { { { .row = UNION_BOX, .field = "name", .text = "Dot", .from = -1 } }, VARIANT_ROW },
    { { COPY(UNION_BOX, "end", UNION_MAIN) }, VARIANT_ROW },
    { { EDIT(UNION_BOX, "count", 1) }, LAYOUT },
    /* Payload counts that would wrap the next variant's row negative. */
    { { EDIT(UNION_DOT, "count", INT32_MAX) }, UNION_ROW },
    { { EDIT(UNION_BOX, "count", INT32_MAX) }, UNION_ROW },
    { { EDIT(UNION_DECLARATION, "count", INT32_MAX) }, UNION_ROW },
    /* A payload numbered as another, of a record type, named as another in
     * its variant, or outside its variant. */
    { { EDIT(UNION_H, "value", 0) }, PAYLOAD_ROW },
    { { EDIT(UNION_H, "type", SCALAR_RECORD) }, PAYLOAD_ROW },
    { { EDIT(UNION_H, "type", SCALAR_STRING) }, PAYLOAD_ROW },
    { { { .row = UNION_H, .field = "name", .text = "w", .from = -1 } }, PAYLOAD_ROW },
    { { COPY(UNION_H, "end", UNION_MAIN) }, PAYLOAD_ROW },
    /* A variant made with another's payloads, too few of them or as a
     * scalar. */
    { { EDIT(UNION_MAKE, "value", 0) }, VARIANT },
    { { EDIT(UNION_MAKE, "count", 1) }, VARIANT },
    { { EDIT(UNION_MAKE, "type", SCALAR_INT) }, VARIANT },
    /* A tag of another type or arity, a payload of another type than its
     * binding or none of its union, and a zero or field read of a union,
     * which only a record has. */
    { { EDIT(UNION_IS_TAG, "type", SCALAR_BOOL) }, TAG },
    { { EDIT(UNION_IS_TAG, "count", 2) }, TAG },
    { { EDIT(UNION_IS_TAG, "value", 1) }, TAG },
    { { EDIT(UNION_H_PAYLOAD, "value", 0) }, PAYLOAD },
    { { EDIT(UNION_H_PAYLOAD, "value", 2) }, PAYLOAD },
    { { EDIT(UNION_W_PAYLOAD, "type", SCALAR_FLOAT) }, PAYLOAD },
    { { EDIT(UNION_DOT_RETURN, "kind", SCALAR_ZERO_ROW),
        EDIT(UNION_DOT_RETURN, "type", SCALAR_RECORD) },
      "scalar record zero names no record" },
    { { EDIT(UNION_IS_TAG, "kind", SCALAR_FIELD_ROW) }, FIELD_READ },
    /* The tag as a bool, which its variant's literal no longer matches. */
    { { EDIT(UNION_IS_VARIANT, "type", SCALAR_BOOL),
        EDIT(UNION_IS_VARIANT, "literal", 0) },
      "scalar comparison differs from its operands" },
};
static const char OPERAND_ROW[] = "invalid scalar operand row",
                  NONCANONICAL[] = "noncanonical scalar literal";
static const malformed_guard MALFORMED_STRINGS[] = {
    /* Text on an int literal or a read, and a string literal whose
     * `literal` is not 0. */
    { { { .row = STRING_ZERO, .field = "text", .text = "x", .from = -1 } }, OPERAND_ROW },
    { { { .row = STRING_READ, .field = "text", .text = "one", .from = -1 } }, OPERAND_ROW },
    { { EDIT(STRING_LITERAL, "literal", 1) }, NONCANONICAL },
    /* Strings ordered, and a string compared with an int. */
    { { EDIT(STRING_COMPARE, "value", 1) }, COMPARISON },
    { { EDIT(STRING_COMPARE, "value", 4) }, COMPARISON },
    { { EDIT(STRING_LITERAL, "type", SCALAR_INT),
        { .row = STRING_LITERAL, .field = "text", .text = "", .from = -1 } },
      COMPARISON },
    /* A string local or read of another type, and a call whose string
     * argument meets an int parameter. */
    { { EDIT(STRING_LOCAL, "type", SCALAR_INT) }, "scalar value differs from its variable" },
    { { EDIT(STRING_PARAMETER_READ, "type", SCALAR_BOOL) }, READ },
    { { EDIT(STRING_PARAMETER, "type", SCALAR_INT) },
      "scalar call argument differs from its parameter" },
};
static const char TEXT_READ[] = "scalar text read differs from its operands";
/* A Text row's operator, arity, name and result, and its string or int
 * operand of another type. */
static const malformed_guard MALFORMED_TEXTS[] = {
    { { EDIT(TEXT_SLICE, "value", 0) }, TEXT_READ },
    { { EDIT(TEXT_SLICE, "value", 5) }, TEXT_READ },
    { { EDIT(TEXT_SLICE, "count", 2) }, TEXT_READ },
    { { EDIT(TEXT_SLICE, "type", SCALAR_INT) }, TEXT_READ },
    { { EDIT(TEXT_LENGTH, "type", SCALAR_STRING) }, TEXT_READ },
    { { { .row = TEXT_LENGTH, .field = "name", .text = "x", .from = -1 } }, TEXT_READ },
    { { EDIT(TEXT_BYTE, "value", SCALAR_TEXT_LENGTH) }, TEXT_READ },
    { { EDIT(TEXT_BYTE, "value", SCALAR_TEXT_SLICE), EDIT(TEXT_BYTE, "count", 3) },
      TEXT_READ },
    { { EDIT(TEXT_START, "type", SCALAR_BOOL) }, TEXT_READ },
    { { EDIT(TEXT_INDEX, "type", SCALAR_INT64) }, TEXT_READ },
    { { EDIT(TEXT_SOURCE, "type", SCALAR_INT),
        { .row = TEXT_SOURCE, .field = "text", .text = "", .from = -1 } },
      TEXT_READ },
};
/* A format of more operands than the stack holds, of none, an int result
 * and an int format. */
static const malformed_guard MALFORMED_FORMATS[] = {
    { { EDIT(FORMAT_TEXT, "count", 5) }, TEXT_READ },
    { { EDIT(FORMAT_TEXT, "count", 0) }, TEXT_READ },
    { { EDIT(FORMAT_TEXT, "type", SCALAR_INT) }, TEXT_READ },
    { { EDIT(FORMAT_FORMAT, "type", SCALAR_INT),
        { .row = FORMAT_FORMAT, .field = "text", .text = "", .from = -1 } },
      TEXT_READ },
};
/* An assert's type, value, name and literal, and a condition that is an
 * int. */
static const malformed_guard MALFORMED_ASSERTS[] = {
    { { EDIT(ASSERT_STATEMENT, "type", SCALAR_BOOL) }, SEGMENT },
    { { EDIT(ASSERT_STATEMENT, "value", 1) }, SEGMENT },
    { { { .row = ASSERT_STATEMENT, .field = "name", .text = "x", .from = -1 } }, SEGMENT },
    { { { .row = ASSERT_STATEMENT, .field = "literal", .value = 1, .from = -1 } }, SEGMENT },
    { { EDIT(ASSERT_COMPARE, "kind", SCALAR_ARITHMETIC_ROW),
        EDIT(ASSERT_COMPARE, "type", SCALAR_INT) },
      "scalar condition is not one bool" },
};
/* Arithmetic's operator, arity and result, and arithmetic on a bool. */
static const malformed_guard MALFORMED_ARITHMETIC[] = {
    { { EDIT(ARITHMETIC_ADD, "value", 0) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "value", 11) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "count", 1) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_ADD, "type", SCALAR_BOOL) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "value", 0) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "value", 3) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_NEGATE, "count", 2) }, ARITHMETIC },
    { { EDIT(ARITHMETIC_SECOND_READ, "kind", SCALAR_LITERAL_ROW),
        EDIT(ARITHMETIC_SECOND_READ, "type", SCALAR_BOOL),
        EDIT(ARITHMETIC_SECOND_READ, "value", 0),
        EDIT(ARITHMETIC_SECOND_READ, "literal", 1) },
      ARITHMETIC },
    { { EDIT(ARITHMETIC_LAST_READ, "kind", SCALAR_LITERAL_ROW),
        EDIT(ARITHMETIC_LAST_READ, "type", SCALAR_BOOL),
        EDIT(ARITHMETIC_LAST_READ, "value", 0) },
      ARITHMETIC },
};
/* The kind of each of WIDTH_SOURCE's rows, and the rows edited. */
static const int WIDTH_KINDS[] = {
    SCALAR_SOURCE_ROW,  SCALAR_DOCUMENT_ROW,   SCALAR_FUNCTION_ROW,
    SCALAR_RETURN_ROW,  SCALAR_LITERAL_ROW,    SCALAR_CALL_ROW,
    SCALAR_CONVERT_ROW, SCALAR_FUNCTION_ROW,   SCALAR_PARAMETER_ROW,
    SCALAR_RETURN_ROW,  SCALAR_READ_ROW,       SCALAR_CONVERT_ROW,
    SCALAR_LITERAL_ROW, SCALAR_ARITHMETIC_ROW,
};
enum
{
    WIDTH_WIDENED = 6,
    WIDTH_READ = 10,
    WIDTH_WRAPPED = 11,
    WIDTH_SHIFT_COUNT = 12,
    WIDTH_SHIFT = 13,
    WIDTH_ROWS = (int)(sizeof WIDTH_KINDS / sizeof *WIDTH_KINDS)
};
/* A conversion's mode, arity, type and operand, a literal outside its type,
 * a literal on another row, mixed shift operands, and a return that does not
 * widen to its result. */
static const char
    CONVERSION[] = "scalar conversion is not one the runtime implements";
static const malformed_guard MALFORMED_WIDTHS[] = {
    { { EDIT(WIDTH_WIDENED, "value", 2) }, CONVERSION },
    { { EDIT(WIDTH_WIDENED, "count", 2) }, CONVERSION },
    { { EDIT(WIDTH_WRAPPED, "type", SCALAR_BOOL) }, CONVERSION },
    { { EDIT(WIDTH_READ, "kind", SCALAR_LITERAL_ROW),
        EDIT(WIDTH_READ, "type", SCALAR_BOOL),
        EDIT(WIDTH_READ, "value", 0) },
      CONVERSION },
    { { EDIT(WIDTH_SHIFT_COUNT, "literal", 256) }, "noncanonical scalar literal" },
    { { EDIT(WIDTH_SHIFT_COUNT, "literal", -1) }, "noncanonical scalar literal" },
    { { EDIT(WIDTH_READ, "literal", 1) }, "invalid scalar operand row" },
    { { EDIT(WIDTH_SHIFT_COUNT, "type", SCALAR_INT) }, ARITHMETIC },
    { { EDIT(WIDTH_SHIFT, "value", 11) }, ARITHMETIC },
    { { EDIT(WIDTH_WIDENED, "type", SCALAR_BYTE) },
      "scalar return differs from its function's result" },
};
#undef EDIT
#undef COPY
static pin_value *guard_row(const pin_value *rows, int index)
{
    return &((const pin_array *)rows->ptr)->items[index];
}
static void write_row(const pin_value *rows, int index, const char *name, pin_value value)
{
    pin_value *row = guard_row(rows, index);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    assert(field);
    assert(!pin_write_field(&detached, row->ptr, field, value));
}
/* native_abi's target `function`, such as "LinuxX64", on VM b. */
static bool native_target(int b, const char *function, pin_value *out)
{
    REQUIRE(
        compiler_fixture_call(&consumer, b, "project:native_abi", function, NULL, 0, COMPILE_BUDGET, &input, out)
    );
    return true;
}
/* scalar_llvm's EmitBytesFor of `rows` and native_abi's target `function`. */
static bool emit_bytes(int b, const char *function, const pin_value *rows, pin_value *emitted)
{
    pin_value args[2] = { { 0 }, *rows };
    REQUIRE(native_target(b, function, &args[0]));
    REQUIRE(
        compiler_fixture_call(&consumer, b, "project:scalar_llvm", "EmitBytesFor", args, 2, COMPILE_BUDGET, &input, emitted)
    );
    return true;
}
static bool validated(int b, pin_value *rows, const char *message)
{
    pin_value error, emitted;
    REQUIRE(
        compiler_fixture_call(&consumer, b, "project:scalar_llvm", "Validate", rows, 1, COMPILE_BUDGET, &input, &error)
    );
    REQUIRE(emit_bytes(b, "LinuxX64", rows, &emitted));
    bool valid = pin_text_equal(
        pin_text(error),
        (pin_string){ .bytes = message, .length = strlen(message) }
    );
    if (!valid) {
        fprintf(
            stderr,
            "scalar_llvm Validate gave \"%.*s\", not \"%s\"\n",
            (int)pin_text(error).length,
            pin_text(error).bytes,
            message
        );
    }
    REQUIRE(valid && !((const pin_array *)emitted.ptr)->count == !!*message);
    return true;
}
/* Each edit applied to the producer's rows, the message Validate gives, and
 * the rows restored. */
static bool rejects_guard(int b, pin_value *rows, const malformed_guard *malformed)
{
    pin_value saved[GUARD_EDITS];
    int count = 0;
    while (count < GUARD_EDITS && malformed->edits[count].field) {
        const row_edit *edit = &malformed->edits[count];
        saved[count++] = scalar_field(guard_row(rows, edit->row), edit->field);
    }
    for (int i = 0; i < count; i++) {
        const row_edit *edit = &malformed->edits[i];
        pin_value value = { .type = PIN_I32, .i = edit->value };
        if (saved[i].type == PIN_I64) {
            value = (pin_value){ .type = PIN_I64, .i64 = edit->value };
        }
        if (edit->text) {
            value = text(edit->text);
        }
        if (edit->from >= 0) {
            value = scalar_field(guard_row(rows, edit->from), edit->field);
        }
        write_row(rows, edit->row, edit->field, value);
    }
    REQUIRE(validated(b, rows, malformed->message));
    for (int i = count - 1; i >= 0; i--) {
        write_row(rows, malformed->edits[i].row, malformed->edits[i].field, saved[i]);
    }
    return true;
}
/* scalar_llvm accepts the producer's rows, of the `kinds` it gives, rejects
 * each malformed variant of them when its layout, operators, operand types,
 * ranges or calls are not what a producer gives, and emits nothing for it. */
static bool rejects_malformed(
    int b,
    pin_value *rows,
    const int *kinds,
    int count,
    const malformed_guard *malformed,
    int variants
)
{
    REQUIRE(((const pin_array *)rows->ptr)->count == count);
    for (int i = 0; i < count; i++) {
        REQUIRE(scalar_field(guard_row(rows, i), "kind").i == kinds[i]);
    }
    REQUIRE(validated(b, rows, ""));
    for (int i = 0; i < variants; i++) {
        REQUIRE(rejects_guard(b, rows, &malformed[i]));
    }
    REQUIRE(validated(b, rows, ""));
    return true;
}
/* WIDTH_SOURCE's typed rows: 298 is an int argument that Low wraps to a byte,
 * its shift count is a byte literal, and Main widens Low's byte to int. */
static bool widened_rows(const pin_value *rows)
{
    static const struct
    {
        int row, type, value;
        int64_t literal;
    } EXPECTED[] = {
        { 4, SCALAR_INT, 0, 298 },
        { 5, SCALAR_BYTE, 1, 0 },
        { WIDTH_WIDENED, SCALAR_INT, 0, 0 },
        { WIDTH_READ, SCALAR_INT, 0, 0 },
        { WIDTH_WRAPPED, SCALAR_BYTE, 1, 0 },
        { WIDTH_SHIFT_COUNT, SCALAR_BYTE, 0, 1 },
        { WIDTH_SHIFT, SCALAR_BYTE, 9, 0 },
    };
    for (int i = 0; i < (int)(sizeof EXPECTED / sizeof *EXPECTED); i++) {
        pin_value *row = guard_row(rows, EXPECTED[i].row);
        REQUIRE(scalar_field(row, "type").i == EXPECTED[i].type);
        REQUIRE(scalar_field(row, "value").i == EXPECTED[i].value);
        REQUIRE(scalar_field(row, "literal").i64 == EXPECTED[i].literal);
    }
    return true;
}
/* LOOP_ROWS_SOURCE's for row: an exclusive range over int bounds that
 * declares i, Sum's third variable. */
static bool looped_rows(const pin_value *rows)
{
    pin_value *row = guard_row(rows, LOOP_FOR);
    REQUIRE(scalar_field(row, "type").i == SCALAR_INT);
    REQUIRE(scalar_field(row, "value").i == 2);
    REQUIRE(scalar_field(row, "count").i == 2);
    REQUIRE(scalar_field(row, "literal").i64 == SCALAR_RANGE_EXCLUSIVE);
    REQUIRE(retained_text(row, "name", "i"));
    REQUIRE(scalar_field(guard_row(rows, LOOP_TRUE), "literal").i64 == 1);
    return true;
}
/* SWITCH_ROWS_SOURCE's switch and arms: each of x's type, a value arm with
 * its one literal and the default arm with none. */
static bool switched_rows(const pin_value *rows)
{
    static const struct
    {
        int row, count;
    } EXPECTED[] = { { SWITCH_ROW, 1 },
                     { SWITCH_FIRST, 1 },
                     { SWITCH_DEFAULT, 0 },
                     { SWITCH_LAST, 1 } };
    for (int i = 0; i < (int)(sizeof EXPECTED / sizeof *EXPECTED); i++) {
        pin_value *row = guard_row(rows, EXPECTED[i].row);
        REQUIRE(scalar_field(row, "type").i == SCALAR_INT);
        REQUIRE(scalar_field(row, "count").i == EXPECTED[i].count);
    }
    REQUIRE(scalar_field(guard_row(rows, SWITCH_LAST_VALUE), "literal").i64 == 20);
    return true;
}
/* RECORD_ROWS_SOURCE's record, its fields in declared order, and the record
 * type of Make's result, Main's local and their rows; the literal's int
 * field is updated last, as written. */
static bool recorded_rows(const pin_value *rows)
{
    pin_value *record = guard_row(rows, RECORD_DECLARATION);
    REQUIRE(
        retained_text(record, "name", "Point") && scalar_field(record, "count").i == 2
    );
    REQUIRE(retained_text(guard_row(rows, RECORD_X), "name", "x"));
    REQUIRE(scalar_field(guard_row(rows, RECORD_X), "type").i == SCALAR_INT);
    REQUIRE(retained_text(guard_row(rows, RECORD_Y), "name", "y"));
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y), "type").i == SCALAR_FLOAT);
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y), "value").i == 1);
    static const int RECORD_TYPED[] = { RECORD_LOCAL, RECORD_ASSIGN,
                                        RECORD_READ,  RECORD_MAKE,
                                        RECORD_ZERO,  RECORD_Y_UPDATE };
    for (int i = 0; i < (int)(sizeof RECORD_TYPED / sizeof *RECORD_TYPED); i++) {
        REQUIRE(scalar_field(guard_row(rows, RECORD_TYPED[i]), "type").i == SCALAR_RECORD);
    }
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y_UPDATE), "value").i == 1);
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y_UPDATE + 2), "value").i == 0);
    REQUIRE(scalar_field(guard_row(rows, RECORD_Y_UPDATE - 1), "type").i == SCALAR_FLOAT);
    REQUIRE(scalar_field(guard_row(rows, RECORD_FIELD), "type").i == SCALAR_INT);
    return true;
}
/* STRING_ROWS_SOURCE's strings: each literal holds its decoded bytes and
 * literal 0, its variables, Name's result and its read are strings, and its
 * comparison gives a bool. Its IR holds one text, which both literals name.
 * A literal longer than any source is noncanonical. */
static bool stringed_rows(int b, pin_value *rows)
{
    static const int STRING_TYPED[] = { STRING_LOCAL,         STRING_ARGUMENT,
                                        STRING_READ,          STRING_LITERAL,
                                        STRING_NAME,          STRING_PARAMETER,
                                        STRING_PARAMETER_READ };
    for (int i = 0; i < (int)(sizeof STRING_TYPED / sizeof *STRING_TYPED); i++) {
        REQUIRE(scalar_field(guard_row(rows, STRING_TYPED[i]), "type").i == SCALAR_STRING);
    }
    static const int STRING_LITERALS[] = { STRING_ARGUMENT, STRING_LITERAL };
    for (int i = 0; i < 2; i++) {
        pin_value *row = guard_row(rows, STRING_LITERALS[i]);
        REQUIRE(
            retained_text(row, "text", "one") && scalar_field(row, "literal").i64 == 0
        );
    }
    REQUIRE(scalar_field(guard_row(rows, STRING_COMPARE), "type").i == SCALAR_BOOL);
    pin_value emitted;
    REQUIRE(
        compiler_fixture_call(&consumer, b, "project:scalar_llvm", "EmitBytes", rows, 1, COMPILE_BUDGET, &input, &emitted)
    );
    const pin_array *bytes = emitted.ptr;
    char *ir = xmalloc((size_t)bytes->count + 1);
    for (int i = 0; i < bytes->count; i++) {
        ir[i] = (char)bytes->items[i].u8;
    }
    ir[bytes->count] = 0;
    bool shared = strstr(ir, "@chibi_scalar.text.0 = private unnamed_addr constant [4 x i8] c\"one\\00\"") &&
                  !strstr(ir, "@chibi_scalar.text.1");
    free(ir);
    REQUIRE(shared);
    pin_value *literal = guard_row(rows, STRING_LITERAL);
    pin_value saved = scalar_field(literal, "text");
    /* A literal has no length bound. */
    enum
    {
        LONG_TEXT = 40000
    };
    char *long_text = xmalloc(LONG_TEXT + 1);
    memset(long_text, 'a', LONG_TEXT);
    long_text[LONG_TEXT] = 0;
    write_row(rows, STRING_LITERAL, "text", text(long_text));
    free(long_text);
    REQUIRE(validated(b, rows, ""));
    write_row(rows, STRING_LITERAL, "text", saved);
    return true;
}
/* TEXT_ROWS_SOURCE's Text rows: each names its operator and the operands it
 * consumes, a slice gives a string and a byte count or byte an int. */
static bool texted_rows(pin_value *rows)
{
    static const struct
    {
        int row, value, count, type;
    } TEXTS[] = {
        { TEXT_SLICE, SCALAR_TEXT_SLICE, 3, SCALAR_STRING },
        { TEXT_LENGTH, SCALAR_TEXT_LENGTH, 1, SCALAR_INT },
        { TEXT_BYTE, SCALAR_TEXT_BYTE, 2, SCALAR_INT },
    };
    for (int i = 0; i < (int)(sizeof TEXTS / sizeof *TEXTS); i++) {
        pin_value *row = guard_row(rows, TEXTS[i].row);
        REQUIRE(scalar_field(row, "value").i == TEXTS[i].value);
        REQUIRE(scalar_field(row, "count").i == TEXTS[i].count);
        REQUIRE(scalar_field(row, "type").i == TEXTS[i].type);
    }
    REQUIRE(retained_text(guard_row(rows, TEXT_SOURCE), "text", "abcd"));
    return true;
}
/* ASSERT_ROWS_SOURCE's assert has its call's range and its condition's
 * three operands, and lowers to a branch past a Fail that names the range in
 * a failure constant, which the context carries. */
static bool asserted_rows(int b, pin_value *rows)
{
    static const char call[] = "assert(x < 2, \"x is small\")";
    const pin_value *row = guard_row(rows, ASSERT_STATEMENT);
    int start = (int)(strstr(ASSERT_ROWS_SOURCE, call) - ASSERT_ROWS_SOURCE);
    REQUIRE(scalar_field(row, "count").i == 3 && scalar_field(row, "type").i == 0);
    REQUIRE(scalar_field(row, "start").i == start);
    REQUIRE(scalar_field(row, "end").i == start + (int)strlen(call));
    REQUIRE(scalar_field(row, "line").i == 4 && scalar_field(row, "column").i == 5);
    pin_value emitted;
    REQUIRE(emit_bytes(b, "LinuxX64", rows, &emitted));
    const pin_array *bytes = emitted.ptr;
    REQUIRE(emitted.container == PIN_CONTAINER_ARRAY && bytes->count > 0);
    char *ir = xmalloc((size_t)bytes->count + 1), constant[160];
    for (int i = 0; i < bytes->count; i++) {
        ir[i] = (char)bytes->items[i].u8;
    }
    ir[bytes->count] = 0;
    snprintf(
        constant,
        sizeof constant,
        "@chibi_scalar.failure.0.7 = private unnamed_addr constant { i64, "
        "i32, i32 } { i64 0, i32 %d, i32 %d }\n",
        start,
        start + (int)strlen(call)
    );
    bool lowered = strstr(ir, constant) &&
                   strstr(ir, "define i32 @chibi_scalar(ptr %ctx) #0 {\n") &&
                   strstr(ir, "  br i1 %a6, label %p8, label %p7\n") &&
                   strstr(
                       ir,
                       "p7:\n  %fail7.at = getelementptr inbounds { ptr, ptr "
                       "}, ptr %ctx, i32 0, i32 1\n"
                       "  store ptr @chibi_scalar.failure.0.7, ptr "
                       "%fail7.at\n  br label %failed\n"
                   ) &&
                   strstr(ir, "failed:\n  ret i32 0\n");
    if (!lowered) {
        fprintf(stderr, "%s\n", ir);
    }
    free(ir);
    REQUIRE(lowered);
    return true;
}
/* FORMAT_ROWS_SOURCE's format consumes its format and each argument as it
 * is to give a string. */
static bool formatted_rows(pin_value *rows)
{
    pin_value *row = guard_row(rows, FORMAT_TEXT);
    REQUIRE(scalar_field(row, "value").i == SCALAR_TEXT_FORMAT);
    REQUIRE(scalar_field(row, "count").i == 4 && scalar_field(row, "type").i == SCALAR_STRING);
    REQUIRE(scalar_field(guard_row(rows, FORMAT_INT), "type").i == SCALAR_INT64);
    REQUIRE(retained_text(guard_row(rows, FORMAT_FORMAT), "text", "{}{}{}"));
    return true;
}
/* UNION_ROWS_SOURCE's union, its variants and their payloads numbered across
 * it, the union type of Make's result, Main's local and the rows that read or
 * make one, the Tag and variant ordinals its `is` and arms test, its last
 * arm as the default, and its bindings' locals. */
static bool unioned_rows(const pin_value *rows)
{
    pin_value *declared = guard_row(rows, UNION_DECLARATION);
    REQUIRE(
        retained_text(declared, "name", "Shape") &&
        scalar_field(declared, "count").i == 2
    );
    REQUIRE(retained_text(guard_row(rows, UNION_DOT), "name", "Dot"));
    REQUIRE(scalar_field(guard_row(rows, UNION_DOT), "count").i == 0);
    REQUIRE(retained_text(guard_row(rows, UNION_BOX), "name", "Box"));
    REQUIRE(scalar_field(guard_row(rows, UNION_BOX), "value").i == 1);
    REQUIRE(scalar_field(guard_row(rows, UNION_BOX), "count").i == 2);
    REQUIRE(retained_text(guard_row(rows, UNION_W), "name", "w"));
    REQUIRE(scalar_field(guard_row(rows, UNION_W), "type").i == SCALAR_INT);
    REQUIRE(retained_text(guard_row(rows, UNION_H), "name", "h"));
    REQUIRE(scalar_field(guard_row(rows, UNION_H), "type").i == SCALAR_FLOAT);
    REQUIRE(scalar_field(guard_row(rows, UNION_H), "value").i == 1);
    static const int UNION_TYPED[] = { UNION_LOCAL,
                                       UNION_LOCAL + 2,
                                       UNION_IS_TAG - 1,
                                       UNION_MAKE };
    for (int i = 0; i < (int)(sizeof UNION_TYPED / sizeof *UNION_TYPED); i++) {
        REQUIRE(scalar_field(guard_row(rows, UNION_TYPED[i]), "type").i == SCALAR_RECORD);
    }
    REQUIRE(scalar_field(guard_row(rows, UNION_MAKE), "value").i == 1);
    REQUIRE(scalar_field(guard_row(rows, UNION_MAKE), "count").i == 2);
    static const int INT_TYPED[] = { UNION_IS_TAG,  UNION_IS_VARIANT,
                                     UNION_SWITCH,  UNION_SWITCH_TAG,
                                     UNION_DOT_ARM, UNION_DOT_VARIANT,
                                     UNION_BOX_ARM, UNION_W_LOCAL };
    for (int i = 0; i < (int)(sizeof INT_TYPED / sizeof *INT_TYPED); i++) {
        REQUIRE(scalar_field(guard_row(rows, INT_TYPED[i]), "type").i == SCALAR_INT);
    }
    REQUIRE(scalar_field(guard_row(rows, UNION_IS_VARIANT), "literal").i64 == 0);
    REQUIRE(scalar_field(guard_row(rows, UNION_DOT_ARM), "count").i == 1);
    REQUIRE(scalar_field(guard_row(rows, UNION_BOX_ARM), "count").i == 0);
    REQUIRE(retained_text(guard_row(rows, UNION_W_LOCAL), "name", "w"));
    REQUIRE(retained_text(guard_row(rows, UNION_H_LOCAL), "name", "h"));
    REQUIRE(scalar_field(guard_row(rows, UNION_H_LOCAL), "type").i == SCALAR_FLOAT);
    REQUIRE(scalar_field(guard_row(rows, UNION_H_LOCAL), "value").i == 2);
    REQUIRE(scalar_field(guard_row(rows, UNION_W_PAYLOAD), "value").i == 0);
    REQUIRE(scalar_field(guard_row(rows, UNION_H_PAYLOAD), "value").i == 1);
    return true;
}
/* Every produced call names a concrete numeric function, and erased
 * callable arguments never become runtime parameters or operand rows. */
static bool generic_scalar_rows(const pin_value *rows)
{
    const pin_array *array = rows->ptr;
    REQUIRE(array->count > 1);
    pin_value *document = guard_row(rows, 1);
    if (scalar_field(document, "kind").i == SCALAR_REJECTION_ROW) {
        fprintf(
            stderr,
            "generic scalar: %s\n",
            pin_text(scalar_field(document, "text")).bytes
        );
    }
    REQUIRE(array->count > 2);
    pin_value *first = guard_row(rows, 2);
    REQUIRE(scalar_field(first, "kind").i == SCALAR_FUNCTION_ROW);
    pin_value *functions[8];
    int count = 0, calls = 0;
    for (int i = 2; i < array->count; i++) {
        pin_value *row = guard_row(rows, i);
        if (scalar_field(row, "kind").i != SCALAR_FUNCTION_ROW) {
            continue;
        }
        REQUIRE(count < 8);
        functions[count++] = row;
        REQUIRE(scalar_field(row, "literal").i64 == 0);
        REQUIRE(scalar_field(row, "count").i <= 1);
    }
    REQUIRE(count == 7);
    for (int i = 2; i < array->count; i++) {
        pin_value *row = guard_row(rows, i);
        if (scalar_field(row, "kind").i != SCALAR_CALL_ROW) {
            continue;
        }
        int target = scalar_field(row, "value").i;
        REQUIRE(target >= 0 && target < count);
        REQUIRE(
            scalar_field(row, "count").i ==
            scalar_field(functions[target], "count").i
        );
        REQUIRE(pin_text_equal(
            pin_text(scalar_field(row, "name")),
            pin_text(scalar_field(functions[target], "name"))
        ));
        calls++;
    }
    REQUIRE(calls == 6);
    return true;
}
/* CollectScalar's rows own their shape and strings: they outlive the
 * producer's packages and inputs, and a consumer prepared afterwards emits
 * them as LLVM IR that keeps the call, and validates guarded and arithmetic
 * rows. */
TEST_CASE(package_compiler_scalar_rows_outlive_the_producer)
{
    REQUIRE(prepare());
    pin_value produced[2], guarded[2], computed[2], widened[2], controlled[2],
        looped[2], switched[2], recorded[2], unioned[2], stringed[2], texted[2],
        formatted[2], asserted[2], specialized[2], specialized_integer[2];
    const char *const texts[] = {
        SCALAR_SOURCE,      GUARD_SOURCE,          ARITHMETIC_SOURCE,
        WIDTH_SOURCE,       CONTROL_ROWS_SOURCE,   LOOP_ROWS_SOURCE,
        SWITCH_ROWS_SOURCE, RECORD_ROWS_SOURCE,    UNION_ROWS_SOURCE,
        STRING_ROWS_SOURCE, TEXT_ROWS_SOURCE,      FORMAT_ROWS_SOURCE,
        ASSERT_ROWS_SOURCE, GENERIC_SCALAR_SOURCE, GENERIC_INTEGER_SCALAR_SOURCE
    };
    pin_value *outputs[] = { produced, guarded,     computed,
                             widened,  controlled,  looped,
                             switched, recorded,    unioned,
                             stringed, texted,      formatted,
                             asserted, specialized, specialized_integer };
    for (int b = 0; b < 2; b++) {
        pin_value args[SCALAR_ARGUMENTS];
        REQUIRE(scalar_arguments(b, "Scalar.chibi", text(SCALAR_SOURCE), args));
        for (size_t i = 0; i < sizeof texts / sizeof *texts; i++) {
            args[2] = text(texts[i]);
            REQUIRE(compiler_fixture_call(
                &fixture,
                b,
                PACKAGE,
                "CollectScalar",
                args,
                SCALAR_ARGUMENTS,
                COMPILE_BUDGET,
                &detached,
                &outputs[i][b]
            ));
        }
    }
    compiler_fixture_free(&fixture);
    arena_free(&input);
    const source_package_input sources[] = { SCALAR_CONSUMER_SOURCE_INPUTS };
    REQUIRE(compiler_fixture_prepare(&consumer, sources, (int)(sizeof sources / sizeof *sources)));
    const source_package_input oracle[] = { SCALAR_CASES_SOURCE_INPUTS };
    REQUIRE(compiler_fixture_prepare(&corpus, oracle, (int)(sizeof oracle / sizeof *oracle)));
    for (int b = 0; b < 2; b++) {
        REQUIRE(generic_scalar_rows(&specialized[b]));
        REQUIRE(validated(b, &specialized[b], ""));
        REQUIRE(generic_scalar_rows(&specialized_integer[b]));
        REQUIRE(validated(b, &specialized_integer[b], ""));
        pin_value expected;
        REQUIRE(compiler_fixture_call(
            &corpus,
            b,
            CASES_PACKAGE,
            "Value",
            &specialized_integer[b],
            1,
            COMPILE_BUDGET,
            &input,
            &expected
        ));
        REQUIRE(expected.type == PIN_I64 && expected.i64 == 200);
        const pin_array *rows = produced[b].ptr;
        REQUIRE(produced[b].container == PIN_CONTAINER_ARRAY && arena_contains(&detached, rows));
        REQUIRE(rows->count == 12 && arena_contains(&detached, rows->record));
        REQUIRE(retained_text(&rows->items[0], "name", "Scalar.chibi"));
        REQUIRE(retained_text(&rows->items[0], "text", SCALAR_SOURCE));
        REQUIRE(retained_text(&rows->items[1], "name", "project:Scalar.chibi"));
        REQUIRE(retained_text(&rows->items[2], "name", "Main"));
        REQUIRE(retained_text(&rows->items[6], "name", "Pick"));
        REQUIRE(scalar_field(&rows->items[6], "value").i == 1);
        REQUIRE(scalar_field(&rows->items[6], "count").i == 2);
        REQUIRE(retained_text(&rows->items[7], "name", "Pick"));
        pin_value emitted;
        REQUIRE(emit_bytes(b, "LinuxX64", &produced[b], &emitted));
        const pin_array *bytes = emitted.ptr;
        REQUIRE(emitted.container == PIN_CONTAINER_ARRAY && bytes->count > 0);
        char *ir = xmalloc((size_t)bytes->count + 1);
        for (int i = 0; i < bytes->count; i++) {
            ir[i] = (char)bytes->items[i].u8;
        }
        ir[bytes->count] = 0;
        bool called = strstr(ir, "define internal i32 @chibi_scalar.1(i32 %arg0, i1 %arg1)") &&
                      strstr(ir, "call i32 @chibi_scalar.1(i32 %c3_0, i1 %c3_1)");
        free(ir);
        REQUIRE(called);
        REQUIRE(rejects_malformed(
            b,
            &guarded[b],
            GUARD_KINDS,
            GUARD_ROWS,
            MALFORMED_GUARDS,
            (int)(sizeof MALFORMED_GUARDS / sizeof *MALFORMED_GUARDS)
        ));
        REQUIRE(rejects_malformed(
            b,
            &computed[b],
            ARITHMETIC_KINDS,
            ARITHMETIC_ROWS,
            MALFORMED_ARITHMETIC,
            (int)(sizeof MALFORMED_ARITHMETIC / sizeof *MALFORMED_ARITHMETIC)
        ));
        REQUIRE(widened_rows(&widened[b]));
        REQUIRE(rejects_malformed(
            b,
            &widened[b],
            WIDTH_KINDS,
            WIDTH_ROWS,
            MALFORMED_WIDTHS,
            (int)(sizeof MALFORMED_WIDTHS / sizeof *MALFORMED_WIDTHS)
        ));
        REQUIRE(rejects_malformed(
            b,
            &controlled[b],
            CONTROL_KINDS,
            CONTROL_ROWS,
            MALFORMED_CONTROL,
            (int)(sizeof MALFORMED_CONTROL / sizeof *MALFORMED_CONTROL)
        ));
        REQUIRE(looped_rows(&looped[b]));
        REQUIRE(rejects_malformed(
            b,
            &looped[b],
            LOOP_KINDS,
            LOOP_ROWS,
            MALFORMED_LOOPS,
            (int)(sizeof MALFORMED_LOOPS / sizeof *MALFORMED_LOOPS)
        ));
        REQUIRE(switched_rows(&switched[b]));
        REQUIRE(rejects_malformed(
            b,
            &switched[b],
            SWITCH_KINDS,
            SWITCH_ROWS,
            MALFORMED_SWITCHES,
            (int)(sizeof MALFORMED_SWITCHES / sizeof *MALFORMED_SWITCHES)
        ));
        REQUIRE(rejects_malformed(
            b,
            &recorded[b],
            RECORD_KINDS,
            RECORD_ROWS,
            MALFORMED_RECORDS,
            (int)(sizeof MALFORMED_RECORDS / sizeof *MALFORMED_RECORDS)
        ));
        REQUIRE(recorded_rows(&recorded[b]));
        REQUIRE(unioned_rows(&unioned[b]));
        REQUIRE(rejects_malformed(
            b,
            &unioned[b],
            UNION_KINDS,
            UNION_ROWS,
            MALFORMED_UNIONS,
            (int)(sizeof MALFORMED_UNIONS / sizeof *MALFORMED_UNIONS)
        ));
        REQUIRE(rejects_malformed(
            b,
            &stringed[b],
            STRING_KINDS,
            STRING_ROWS,
            MALFORMED_STRINGS,
            (int)(sizeof MALFORMED_STRINGS / sizeof *MALFORMED_STRINGS)
        ));
        REQUIRE(stringed_rows(b, &stringed[b]));
        REQUIRE(texted_rows(&texted[b]));
        REQUIRE(rejects_malformed(
            b,
            &texted[b],
            TEXT_KINDS,
            TEXT_ROWS,
            MALFORMED_TEXTS,
            (int)(sizeof MALFORMED_TEXTS / sizeof *MALFORMED_TEXTS)
        ));
        REQUIRE(formatted_rows(&formatted[b]));
        REQUIRE(rejects_malformed(
            b,
            &formatted[b],
            FORMAT_KINDS,
            FORMAT_ROWS,
            MALFORMED_FORMATS,
            (int)(sizeof MALFORMED_FORMATS / sizeof *MALFORMED_FORMATS)
        ));
        REQUIRE(rejects_malformed(
            b,
            &asserted[b],
            ASSERT_KINDS,
            ASSERT_ROWS,
            MALFORMED_ASSERTS,
            (int)(sizeof MALFORMED_ASSERTS / sizeof *MALFORMED_ASSERTS)
        ));
        REQUIRE(asserted_rows(b, &asserted[b]));
    }
    return true;
}
/* llvm_native's op and type ordinals, and its tables as Emit takes them.
 * Functions own no locals. */
enum
{
    NATIVE_BOOL = 1,
    NATIVE_INT = 6,
    NATIVE_FLOAT = 10,
    NATIVE_CONSTANT = 0,
    NATIVE_ARGUMENT = 1,
    NATIVE_PUBLISH = 4,
    NATIVE_RETURN = 5,
    NATIVE_NE = 24,
    NATIVE_CHECK = 27,
    NATIVE_CALL = 28,
    NATIVE_INSERT = 30,
    NATIVE_EXTRACT = 31,
    NATIVE_TEXT = 13,
    NATIVE_RECORD = 32,
    NATIVE_ADD = 7,
    NATIVE_TEXT_BYTE = 33,
    NATIVE_TEXT_DROP = 34,
    NATIVE_TEXT_TAKE = 35
};
typedef struct native_code
{
    int op, type, output, left, right, target;
    int64_t literal;
} native_code;
typedef struct native_function
{
    int result, first_parameter, parameters, first_slot, slots, first_code,
        code, first_argument, arguments;
} native_function;
typedef struct native_record
{
    int first, count;
} native_record;
typedef struct native_module
{
    const native_function *functions;
    int function_count;
    const int *parameters;
    int parameter_count;
    const int *slots;
    int slot_count;
    const native_code *code;
    int code_count;
    const int *arguments;
    int argument_count;
    const native_record *records;
    int record_count;
    const int *fields;
    int field_count;
    const char *const *texts;
    int text_count;
    /* Line tables: the source file, or NULL for none, its directory, each
     * function's name and each code row's line and column. */
    const char *file, *directory;
    const char *const *names;
    const int *lines, *columns;
} native_module;
static const char NATIVE_PACKAGE[] = "project:llvm_native";
static pin_value native_ints(const int *values, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_I32, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){ .type = PIN_I32, .i = values[i] };
    }
    return (pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
static const char *const CODE_FIELDS[] = { "op",    "type",   "output", "left",
                                           "right", "target", "literal" };
static const char *const FUNCTION_FIELDS[] = { "result",     "first_parameter",
                                               "parameters", "first_slot",
                                               "slots",      "first_code",
                                               "code",       "first_argument",
                                               "arguments" };
static const char *const RECORD_FIELDS[] = { "first", "count" };
enum
{
    CODE_WIDTH = (int)(sizeof CODE_FIELDS / sizeof *CODE_FIELDS),
    FUNCTION_WIDTH = (int)(sizeof FUNCTION_FIELDS / sizeof *FUNCTION_FIELDS),
    RECORD_WIDTH = (int)(sizeof RECORD_FIELDS / sizeof *RECORD_FIELDS)
};
/* count Instruction, Function or Record records whose fields, in declaration
 * order, take consecutive values. No records is an empty array. */
static bool native_records(int b, const char *name, const int64_t *values, int count, pin_value *out)
{
    const char *const *fields = RECORD_FIELDS;
    int width = RECORD_WIDTH;
    if (!strcmp(name, "Instruction")) {
        fields = CODE_FIELDS;
        width = CODE_WIDTH;
    } else if (!strcmp(name, "Function")) {
        fields = FUNCTION_FIELDS;
        width = FUNCTION_WIDTH;
    }
    if (!count) {
        *out = rows(
            NULL,
            0,
            PIN_STRUCT,
            compiler_fixture_record(&consumer, b, NATIVE_PACKAGE, name, &input)
        );
        return true;
    }
    pin_value *items = xmalloc(sizeof *items * (size_t)count);
    for (int i = 0; i < count; i++) {
        items[i] = record_row(&consumer, b, NATIVE_PACKAGE, name);
        for (int j = 0; j < width; j++) {
            REQUIRE(put(&items[i], fields[j], values[i * width + j]));
        }
    }
    *out = rows(items, count, PIN_STRUCT, items[0].record);
    free(items);
    return true;
}
static bool native_codes(int b, const native_code *code, int count, pin_value *out)
{
    int64_t *values = xmalloc(sizeof *values * CODE_WIDTH * (size_t)count);
    for (int i = 0; i < count; i++) {
        const native_code *c = &code[i];
        const int64_t fields[CODE_WIDTH] = { c->op,     c->type,  c->output,
                                             c->left,   c->right, c->target,
                                             c->literal };
        memcpy(&values[i * CODE_WIDTH], fields, sizeof fields);
    }
    bool built = native_records(b, "Instruction", values, count, out);
    free(values);
    return built;
}
static bool native_functions(int b, const native_function *functions, int count, pin_value *out)
{
    int64_t *values = xmalloc(sizeof *values * FUNCTION_WIDTH * (size_t)count);
    for (int i = 0; i < count; i++) {
        const native_function *f = &functions[i];
        const int64_t fields[FUNCTION_WIDTH] = {
            f->result,     f->first_parameter, f->parameters,
            f->first_slot, f->slots,           f->first_code,
            f->code,       f->first_argument,  f->arguments
        };
        memcpy(&values[i * FUNCTION_WIDTH], fields, sizeof fields);
    }
    bool built = native_records(b, "Function", values, count, out);
    free(values);
    return built;
}
static bool native_record_table(int b, const native_record *records, int count, pin_value *out)
{
    int64_t *values = xmalloc(sizeof *values * RECORD_WIDTH * (size_t)count);
    for (int i = 0; i < count; i++) {
        values[i * RECORD_WIDTH] = records[i].first;
        values[i * RECORD_WIDTH + 1] = records[i].count;
    }
    bool built = native_records(b, "Record", values, count, out);
    free(values);
    return built;
}
static pin_value native_ids(const int64_t *values, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_I64, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){ .type = PIN_I64, .i64 = values[i] };
    }
    return (pin_value){ .type = PIN_I64, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
/* native_abi's target `function` and m's tables, the arguments Emit and
 * EmitLibrary share. */
static bool native_tables(int b, const char *function, const native_module *m, pin_value args[10])
{
    REQUIRE(native_target(b, function, &args[0]));
    REQUIRE(native_functions(b, m->functions, m->function_count, &args[1]));
    args[2] = native_ints(m->parameters, m->parameter_count);
    args[3] = native_ints(m->slots, m->slot_count);
    args[4] = native_ints(NULL, 0);
    REQUIRE(native_codes(b, m->code, m->code_count, &args[5]));
    args[6] = native_ints(m->arguments, m->argument_count);
    REQUIRE(native_record_table(b, m->records, m->record_count, &args[7]));
    args[8] = native_ints(m->fields, m->field_count);
    args[9] = strings(m->texts, m->text_count);
    return true;
}
static bool native_result(int b, const char *function, pin_value *args, int count, const pin_union **out)
{
    pin_value result;
    REQUIRE(
        compiler_fixture_call(&consumer, b, NATIVE_PACKAGE, function, args, count, COMPILE_BUDGET, &input, &result)
    );
    REQUIRE(result.type == PIN_UNION && result.ptr);
    *out = result.ptr;
    return true;
}
/* Emits m for native_abi's target `function` under budget 1, its first
 * function the entry, or `main` for a program. */
static bool native_entry_emit(
    int b,
    const char *function,
    const native_module *m,
    bool main,
    const pin_union **out
)
{
    pin_value args[16];
    REQUIRE(native_tables(b, function, m, args));
    args[10] = record_row(&consumer, b, NATIVE_PACKAGE, "Source");
    REQUIRE(put_value(&args[10], "file", text(m->file ? m->file : "")));
    REQUIRE(put_value(&args[10], "directory", text(m->directory ? m->directory : "")));
    int placed = m->file ? m->code_count : 0;
    args[11] = strings(m->names, m->file ? m->function_count : 0);
    args[12] = native_ints(m->lines, placed);
    args[13] = native_ints(m->columns, placed);
    args[14] = (pin_value){ .type = PIN_BOOL, .b = main };
    args[15] = (pin_value){ .type = PIN_I32, .i = 1 };
    return native_result(b, "Emit", args, 16, out);
}
/* Emits m for native_abi's target `function` with its entry chibi_scalar. */
static bool native_target_emit(int b, const char *function, const native_module *m, const pin_union **out)
{
    return native_entry_emit(b, function, m, false, out);
}
/* Emits m for native_abi's target `function` as the library `name` under
 * budget 1, its functions named and identified by `names` and `ids`, `count`
 * of each. */
static bool native_emit_library(
    int b,
    const char *function,
    const native_module *m,
    const char *name,
    const char *const *names,
    const int64_t *ids,
    int count,
    const pin_union **out
)
{
    pin_value tables[10], args[14];
    REQUIRE(native_tables(b, function, m, tables));
    args[0] = tables[0];
    args[1] = text(name);
    args[2] = strings(names, count);
    args[3] = native_ids(ids, count);
    memcpy(&args[4], &tables[1], sizeof tables - sizeof *tables);
    args[13] = (pin_value){ .type = PIN_I32, .i = 1 };
    return native_result(b, "EmitLibrary", args, 14, out);
}
/* Emits m for Linux x86-64. */
static bool native_emit(int b, const native_module *m, const pin_union **out)
{
    return native_target_emit(b, "LinuxX64", m, out);
}
static bool native_rejects(int b, const native_module *m, const char *message)
{
    const pin_union *value;
    REQUIRE(native_emit(b, m, &value));
    REQUIRE(value->tag == fixture_variant_tag(&consumer, b, NATIVE_PACKAGE, "Result", "Rejected"));
    pin_string text = pin_text(value->values[1]);
    REQUIRE(pin_text_equal(text, (pin_string){ .bytes = message, .length = strlen(message) }));
    return true;
}
/* main() returns Pick(7, true, 9), and Pick :: (int, bool, int) -> int
 * returns its third argument. Slots s0..s2 hold the arguments in call order. */
static const native_code CALLER_AND_CALLEE[] = {
    { .op = NATIVE_CHECK },
    { .op = NATIVE_CONSTANT, .type = NATIVE_INT, .literal = 7 },
    { .op = NATIVE_CONSTANT, .type = NATIVE_BOOL, .output = 1, .literal = 1 },
    { .op = NATIVE_CONSTANT, .type = NATIVE_INT, .output = 2, .literal = 9 },
    { .op = NATIVE_CALL, .type = NATIVE_INT, .output = 3, .left = 1, .target = 3 },
    { .op = NATIVE_PUBLISH, .type = NATIVE_INT, .left = 3 },
    { .op = NATIVE_RETURN },
    { .op = NATIVE_CHECK },
    { .op = NATIVE_ARGUMENT, .type = NATIVE_INT, .left = 2 },
    { .op = NATIVE_PUBLISH, .type = NATIVE_INT },
    { .op = NATIVE_RETURN },
};
enum
{
    CALL_CODE = (int)(sizeof CALLER_AND_CALLEE / sizeof *CALLER_AND_CALLEE)
};
static const native_function CALL_FUNCTIONS[] = {
    { NATIVE_INT, 0, 0, 0, 4, 0, 7, 0, 3 },
    { NATIVE_INT, 0, 3, 4, 1, 7, 4, 3, 0 },
};
static const int CALL_PARAMETERS[] = { NATIVE_INT, NATIVE_BOOL, NATIVE_INT };
static const int CALL_SLOTS[] = { NATIVE_INT, NATIVE_BOOL, NATIVE_INT, NATIVE_INT, NATIVE_INT };
static const int CALL_ARGUMENTS[] = { 0, 1, 2 };
static native_module call_module(const native_code *code, const int *arguments, int argument_count)
{
    return (native_module){ .functions = CALL_FUNCTIONS,
                            .function_count = 2,
                            .parameters = CALL_PARAMETERS,
                            .parameter_count = 3,
                            .slots = CALL_SLOTS,
                            .slot_count = 5,
                            .code = code,
                            .code_count = CALL_CODE,
                            .arguments = arguments,
                            .argument_count = argument_count };
}
static bool contains(pin_string haystack, const char *needle)
{
    char *text = xmalloc(haystack.length + 1);
    memcpy(text, haystack.bytes, haystack.length);
    text[haystack.length] = 0;
    bool found = strstr(text, needle);
    free(text);
    return found;
}
/* The call module as the library Lib: every function internal, one wrapper
 * per word signature, and the manifest the only exported symbol. */
static bool native_libraries_export_words(int b)
{
    native_module m = call_module(CALLER_AND_CALLEE, CALL_ARGUMENTS, 3);
    static const char *const names[] = { "Main", "Pick" };
    static const int64_t ids[] = { 11, -5 };
    const pin_union *value;
    REQUIRE(native_emit_library(b, "LinuxX64", &m, "Lib", names, ids, 2, &value));
    REQUIRE(value->tag == fixture_variant_tag(&consumer, b, NATIVE_PACKAGE, "Result", "Emitted"));
    pin_string text = pin_text(value->values[0]);
    REQUIRE(!contains(text, "define i32"));
    REQUIRE(contains(text, "define internal i32 @chibi_scalar.0() #0 {"));
    REQUIRE(
        contains(text, "define internal void @chibi_scalar.export.1(ptr %ctx, ptr %in, ptr %out) #0 {")
    );
    REQUIRE(
        contains(text, "@chibi_scalar.export.types.1 = private unnamed_addr constant [4 x i8] [i8 1, i8 2, i8 1, i8 1]")
    );
    REQUIRE(
        contains(text, "@chibi_scalar.export.name.1 = private unnamed_addr constant [5 x i8] c\"Pick\\00\"")
    );
    REQUIRE(contains(
        text,
        "%chibi.export { ptr @chibi_scalar.export.name.1, i64 -5, ptr "
        "@chibi_scalar.export.types.1, i32 3, "
        "i32 1, ptr @chibi_scalar.export.1 }"
    ));
    REQUIRE(
        contains(text, "@chibi_natives_Lib = constant %chibi.manifest { i32 3, i32 2, ptr @chibi_scalar.exports }")
    );
    static const char malformed[] = "native library needs an identifier and a "
                                    "name and ID for each function";
    static const char *const repeated[] = { "Main", "Main" };
    static const int64_t shared[] = { 11, 11 };
    /* Libraries are Linux-only: other targets reject them rather than export
     * Linux's word ABI. */
    static const char linux_only[] = "native libraries are Linux x86-64 only";
    static const struct
    {
        const char *function;
        const char *name;
        const char *const *names;
        const int64_t *ids;
        int count;
        const char *message;
    } rejected[] = {
        { "LinuxX64", "1Lib", names, ids, 2, malformed },
        { "LinuxX64", "", names, ids, 2, malformed },
        { "LinuxX64", "Lib", names, ids, 1, malformed },
        { "LinuxX64", "Lib", repeated, ids, 2, "native library names each function once" },
        { "LinuxX64", "Lib", names, shared, 2, "native library names each function once" },
        { "MacOSArm64", "Lib", names, ids, 2, linux_only },
        { "WindowsX64", "Lib", names, ids, 2, linux_only },
    };
    for (size_t i = 0; i < sizeof rejected / sizeof *rejected; i++) {
        REQUIRE(native_emit_library(
            b,
            rejected[i].function,
            &m,
            rejected[i].name,
            rejected[i].names,
            rejected[i].ids,
            rejected[i].count,
            &value
        ));
        REQUIRE(
            value->tag ==
            fixture_variant_tag(&consumer, b, NATIVE_PACKAGE, "Result", "Rejected")
        );
        pin_string message = pin_text(value->values[1]);
        REQUIRE(pin_text_equal(
            message,
            (pin_string){ .bytes = rejected[i].message,
                          .length = strlen(rejected[i].message) }
        ));
    }
    return true;
}
typedef struct native_change
{
    int index;
    size_t field;
    int value;
} native_change;
/* Sets one field of code[change.index], at the width of that field. */
static void change_code(native_code *code, native_change change)
{
    native_code *item = &code[change.index];
    if (change.field == offsetof(native_code, literal)) {
        item->literal = change.value;
        return;
    }
    memcpy((char *)item + change.field, &change.value, sizeof change.value);
}
static const char
    TABLES[] = "unsupported native scalar signature or table size";
static const char OUTPUT[] = "native scalar output has an invalid type or "
                             "multiple definitions";
/* The IR native_emit gives for m. */
static bool native_text(int b, const native_module *m, pin_string *ir)
{
    const pin_union *value;
    REQUIRE(native_emit(b, m, &value));
    REQUIRE(value->tag == fixture_variant_tag(&consumer, b, NATIVE_PACKAGE, "Result", "Emitted"));
    *ir = pin_text(value->values[0]);
    return true;
}
/* The native LLVM fixtures target 64-bit layouts. Portable hosts still store
 * pin_string as their own bytes pointer, 32-bit length and owner pointer. */
_Static_assert(
    offsetof(pin_string, bytes) == 0 &&
        offsetof(pin_string, length) == sizeof(void *) &&
        offsetof(pin_string, owner) == 2 * sizeof(void *) &&
        sizeof(((pin_string *)0)->length) == 4,
    "pin_string is { host pointer, i32, host pointer }"
);
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(pin_string) == 24, "64-bit native text layout");
#else
_Static_assert(sizeof(void *) == 4 && sizeof(pin_string) == 12, "32-bit portable text layout");
#endif
/* Main returns whether its two texts differ: each constant is a private
 * global of its bytes and a NUL, named by its index in the text table, and
 * `!=` negates the module's one byte comparison. */
static const native_code TEXT_CODE[] = {
    { .op = NATIVE_CHECK },
    { .op = NATIVE_CONSTANT, .type = NATIVE_TEXT },
    { .op = NATIVE_CONSTANT, .type = NATIVE_TEXT, .output = 1, .literal = 1 },
    { .op = NATIVE_NE, .type = NATIVE_TEXT, .output = 2, .right = 1 },
    { .op = NATIVE_PUBLISH, .type = NATIVE_BOOL, .left = 2 },
    { .op = NATIVE_RETURN },
};
enum
{
    TEXT_CODE_COUNT = (int)(sizeof TEXT_CODE / sizeof *TEXT_CODE)
};
static const native_function TEXT_FUNCTIONS[] = {
    { NATIVE_BOOL, 0, 0, 0, 3, 0, TEXT_CODE_COUNT, 0, 0 }
};
static const int TEXT_SLOTS[] = { NATIVE_TEXT, NATIVE_TEXT, NATIVE_BOOL };
static const char *const TEXTS[] = { "a\"\\\n\xc3\xa9", "" };
static native_module text_module(const native_code *code)
{
    return (native_module){ .functions = TEXT_FUNCTIONS,
                            .function_count = 1,
                            .slots = TEXT_SLOTS,
                            .slot_count = 3,
                            .code = code,
                            .code_count = TEXT_CODE_COUNT,
                            .texts = TEXTS,
                            .text_count = 2 };
}
/* The text module with one instruction field changed. */
static bool native_rejects_text_change(int b, native_change change, const char *message)
{
    native_code code[TEXT_CODE_COUNT];
    memcpy(code, TEXT_CODE, sizeof code);
    change_code(code, change);
    native_module m = text_module(code);
    return native_rejects(b, &m, message);
}
static bool native_strings_emit(int b)
{
    pin_value type;
    REQUIRE(
        compiler_fixture_call(&consumer, b, "project:native_abi", "TextType", NULL, 0, COMPILE_BUDGET, &input, &type)
    );
    REQUIRE(
        pin_text_equal(pin_text(type), (pin_string){ .bytes = "{ ptr, i32, ptr }", .length = 17 })
    );
    native_module m = text_module(TEXT_CODE);
    pin_string ir;
    REQUIRE(native_text(b, &m, &ir));
    REQUIRE(contains(
        ir,
        "@chibi_scalar.text.0 = private unnamed_addr constant [7 x i8] "
        "c\"a\\22\\5C\\0A\\C3\\A9\\00\"\n"
    ));
    REQUIRE(contains(ir, "@chibi_scalar.text.1 = private unnamed_addr constant [1 x i8] c\"\\00\"\n"));
    REQUIRE(
        contains(ir, "store { ptr, i32, ptr } { ptr @chibi_scalar.text.0, i32 6, ptr null }, ptr %s0")
    );
    REQUIRE(
        contains(ir, "store { ptr, i32, ptr } { ptr @chibi_scalar.text.1, i32 0, ptr null }, ptr %s1")
    );
    REQUIRE(
        contains(ir, "call i1 @chibi_scalar.text_equal({ ptr, i32, ptr } %a3, { ptr, i32, ptr } %b3)")
    );
    REQUIRE(contains(ir, "xor i1 %e3, true"));
    REQUIRE(
        contains(ir, "define internal i1 @chibi_scalar.text_equal({ ptr, i32, ptr } %a, { ptr, i32, ptr } %b) #0 {")
    );
    static const char outside[] = "native constant is outside its type",
                      operands[] = "native scalar operand type mismatch";
    static const struct
    {
        native_change change;
        const char *message;
    } changes[] = {
        /* A text past the table, or before it. */
        { { 2, offsetof(native_code, literal), 2 }, outside },
        { { 2, offsetof(native_code, literal), -1 }, outside },
        /* Strings ordered, a sum of strings, which no bool slot holds, and a
         * string compared with a bool. */
        { { 3, offsetof(native_code, op), 19 }, operands },
        { { 3, offsetof(native_code, op), 7 }, OUTPUT },
        { { 3, offsetof(native_code, right), 2 }, operands },
    };
    for (size_t i = 0; i < sizeof changes / sizeof *changes; i++) {
        REQUIRE(native_rejects_text_change(b, changes[i].change, changes[i].message));
    }
    /* No string crosses the exported entry. */
    static const native_code returned[] = {
        { .op = NATIVE_CHECK },
        { .op = NATIVE_CONSTANT, .type = NATIVE_TEXT },
        { .op = NATIVE_PUBLISH, .type = NATIVE_TEXT },
        { .op = NATIVE_RETURN },
    };
    static const native_function exported[] = { { NATIVE_TEXT, 0, 0, 0, 1, 0, 4, 0, 0 } },
                                 taking[] = { { NATIVE_BOOL, 0, 1, 0, 1, 0, 4, 0, 0 } };
    static const int text_parameter[] = { NATIVE_TEXT };
    native_module alone = { .functions = exported,
                            .function_count = 1,
                            .slots = TEXT_SLOTS,
                            .slot_count = 1,
                            .code = returned,
                            .code_count = 4,
                            .texts = TEXTS,
                            .text_count = 1 };
    REQUIRE(native_rejects(b, &alone, TABLES));
    /* A library keeps such a function internal and exports no wrapper. */
    static const char *const name[] = { "Named" };
    static const int64_t id[] = { 3 };
    const pin_union *library;
    REQUIRE(native_emit_library(b, "LinuxX64", &alone, "Texts", name, id, 1, &library));
    REQUIRE(library->tag == fixture_variant_tag(&consumer, b, NATIVE_PACKAGE, "Result", "Emitted"));
    pin_string kept = pin_text(library->values[0]);
    REQUIRE(contains(kept, "define internal { ptr, i32, ptr } @chibi_scalar.0() #0 {"));
    REQUIRE(!contains(kept, "@chibi_scalar.export.0"));
    REQUIRE(contains(kept, "@chibi_scalar.exports = private constant [0 x %chibi.export] []"));
    REQUIRE(
        contains(kept, "@chibi_natives_Texts = constant %chibi.manifest { i32 3, i32 0, ptr @chibi_scalar.exports }")
    );
    static const native_code published[] = {
        { .op = NATIVE_CHECK },
        { .op = NATIVE_CONSTANT, .type = NATIVE_BOOL },
        { .op = NATIVE_PUBLISH, .type = NATIVE_BOOL },
        { .op = NATIVE_RETURN },
    };
    alone.functions = taking;
    alone.parameters = text_parameter;
    alone.parameter_count = 1;
    alone.slots = TEXT_SLOTS + 2;
    alone.code = published;
    REQUIRE(native_rejects(b, &alone, "unsupported native scalar parameter"));
    /* A module that compares no strings defines no comparison. */
    native_code equal[TEXT_CODE_COUNT];
    memcpy(equal, TEXT_CODE, sizeof equal);
    equal[3] = (native_code){ .op = NATIVE_CONSTANT, .type = NATIVE_BOOL, .output = 2 };
    m = text_module(equal);
    REQUIRE(native_text(b, &m, &ir));
    REQUIRE(!contains(ir, "text_equal"));
    return true;
}
/* llvm_native's call and float contracts: definitions, argument order,
 * floating encodings and conversions, and the malformed or cyclic plans it
 * rejects before writing IR. */
TEST_CASE(package_compiler_native_emitter_checks_calls)
{
    const source_package_input sources[] = { SCALAR_CONSUMER_SOURCE_INPUTS,
                                             EMITTER_TEST_SOURCE_INPUTS };
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    REQUIRE(
        compiler_fixture_prepare_packs(&consumer, sources, (int)(sizeof sources / sizeof *sources), core, 1)
    );
    for (int b = 0; b < 2; b++) {
        REQUIRE(native_libraries_export_words(b));
        REQUIRE(native_strings_emit(b));
    }
    REQUIRE(compiler_fixture_tests(&consumer));
    return true;
}
/* A shared wrong result is not an oracle. Each executed value must match
 * the fixture's literal, including Main's value. */
static bool scalar_results_match(const int64_t values[4], int64_t expected)
{
    return values[0] == expected && values[1] == expected &&
           values[2] == expected && values[3] == expected;
}
/* Forms the scalar subset cannot express, each with its message and the
 * source its diagnostic covers: a switch on a union call, an `is` that
 * binds, string and record payloads, a text native given a named argument,
 * a record formatted and a call of another native. */
static const struct
{
    const char *source, *message, *excerpt;
} UNSUPPORTED[] = {
    { "package scalar\nShape :: union { Dot | Box(w: int) }\n"
      "Main :: () -> int { switch Make() { case .Dot: return 0; case .Box(w): "
      "return w; } }\n"
      "Make :: () -> Shape { return Shape.Dot; }\n",
      "scalar proof switches only on union variables",
      "Make()" },
    { "package scalar\nShape :: union { Dot | Box(w: int) }\n"
      "Main :: () -> int { s := Shape.Box(1); if (s is .Box(w)) { return w; } "
      "return 0; }\n",
      "scalar proof binds payloads only in switch arms",
      ".Box(w)" },
    { "package scalar\nNamed :: union { Tag(text: string) }\nMain :: () -> int "
      "{ return 1; }\n",
      "scalar proof requires number and bool union payloads",
      "text: string" },
    { "package scalar\nPoint :: struct { x: int; }\nWrapped :: union { Of(p: "
      "Point) }\n"
      "Main :: () -> int { return 1; }\n",
      "scalar proof requires number and bool union payloads",
      "p: Point" },
    { "package scalar\nMain :: () -> int { return text_byte_length(Value: "
      "\"ab\"); }\n",
      "scalar proof passes a text native's arguments in order",
      "Value: \"ab\"" },
    /* A record among several named results. */
    { "package scalar\nPoint :: struct { x: int; }\n"
      "Main :: () -> int { return 1; }\n"
      "Pair :: () -> (p: Point, n: int) { return; }\n",
      "scalar proof requires number and bool results where a function has "
      "several",
      "p: Point" },
    { "package scalar\nPoint :: struct { x: int; }\n"
      "Main :: () -> int { p: Point = {x: 1}; return "
      "text_byte_length(format_text(\"{}\", p)); }\n",
      "scalar proof formats only numbers, bools and strings",
      "p" },
    /* The call step types format_text's arguments first, as the VM's. */
    { "package scalar\nMain :: () -> int { return "
      "text_byte_length(format_text(\"{}\", 7)); }\n",
      "a number needs an integer type here",
      "7" },
    /* No native but a text read is known. */
    { "package scalar\nMain :: () -> int { if (text_equal(\"a\", \"b\")) { "
      "return 1; } return 0; }\n",
      "Unknown function 'text_equal'",
      "text_equal" },
};
/* Each of UNSUPPORTED is a located rejection on both VMs, its source and
 * diagnostic and nothing else. */
static bool scalar_rejects_unsupported()
{
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < (int)(sizeof UNSUPPORTED / sizeof *UNSUPPORTED); i++) {
            const char *source = UNSUPPORTED[i].source;
            pin_value args[SCALAR_ARGUMENTS];
            REQUIRE(scalar_arguments(b, "Scalar.chibi", text(source), args));
            pin_value rows;
            REQUIRE(compiler_fixture_call(
                &fixture,
                b,
                PACKAGE,
                "CollectScalar",
                args,
                SCALAR_ARGUMENTS,
                COMPILE_BUDGET,
                &detached,
                &rows
            ));
            const pin_array *array = rows.ptr;
            REQUIRE(array->count == 2);
            pin_value *rejection = guard_row(&rows, 1);
            REQUIRE(scalar_field(rejection, "kind").i == SCALAR_REJECTION_ROW);
            int start = scalar_field(rejection, "start").i,
                end = scalar_field(rejection, "end").i;
            pin_string message = pin_text(scalar_field(rejection, "text"));
            bool expected = !strcmp(message.bytes, UNSUPPORTED[i].message) &&
                            end - start == (int)strlen(UNSUPPORTED[i].excerpt) &&
                            !strncmp(source + start, UNSUPPORTED[i].excerpt, (size_t)(end - start));
            if (!expected) {
                fprintf(
                    stderr,
                    "unsupported rejection %d: %s at %.*s\n",
                    i,
                    message.bytes,
                    end - start,
                    source + start
                );
            }
            REQUIRE(expected);
            arena_free(&detached);
        }
    }
    return true;
}
/* CollectScalar's rows of `source` at libraries/Tools.chibi on VM b. */
static bool library_rows(int b, const char *source, pin_value *rows)
{
    pin_value args[SCALAR_ARGUMENTS];
    REQUIRE(scalar_arguments(b, "libraries/Tools.chibi", text(source), args));
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "CollectScalar", args, SCALAR_ARGUMENTS, COMPILE_BUDGET, &detached, rows)
    );
    return true;
}
/* Library sources that are not one library of plain functions, each with
 * its message. A source holds at most one library, so the grammar rejects
 * anything after it. */
static const struct
{
    const char *source, *message;
} LIBRARY_REJECTIONS[] = {
    { "Tools :: library {\n    Point :: struct { x: int; }\n}\n",
      "a native library holds only functions without attributes" },
    { "Tools :: library {\n}\n", "scalar proof requires at least one function" },
    { "Tools :: library {\n    One :: () -> int { return 1; }\n}\n"
      "More :: library {\n    Two :: () -> int { return 2; }\n}\n",
      "Unexpected syntax" },
    { "Tools :: library {\n    One :: () -> int { return 1; }\n"
      "    One :: () -> int { return 2; }\n}\n",
      "Duplicate declaration 'One'" },
};
/* A library's Document row names it, and each Function row's literal is the
 * stable symbol ID the runtime binds the function by. */
static bool scalar_collects_libraries()
{
    static const char
        source[] = "Tools :: library {\n"
                   "    Add :: (a: int, b: int) -> int { return a + b; }\n"
                   "    Name :: () -> string { return \"tools\"; }\n"
                   "}\n";
    const int64_t ids[] = {
        (int64_t)script_catalog_id("symbol", "libraries/Tools.chibi:1:Add:callable"),
        (int64_t)script_catalog_id("symbol", "libraries/Tools.chibi:1:Name:callable"),
    };
    for (int b = 0; b < 2; b++) {
        pin_value rows;
        REQUIRE(library_rows(b, source, &rows));
        const pin_array *array = rows.ptr;
        REQUIRE(scalar_field(guard_row(&rows, 1), "kind").i == SCALAR_DOCUMENT_ROW);
        REQUIRE(retained_text(guard_row(&rows, 1), "text", "Tools"));
        int functions = 0;
        for (int i = 2; i < array->count; i++) {
            pin_value *row = guard_row(&rows, i);
            if (scalar_field(row, "kind").i != SCALAR_FUNCTION_ROW) {
                continue;
            }
            REQUIRE(functions < 2 && scalar_field(row, "literal").i64 == ids[functions]);
            functions++;
        }
        REQUIRE(functions == 2);
        arena_free(&detached);
        for (size_t i = 0; i < sizeof LIBRARY_REJECTIONS / sizeof *LIBRARY_REJECTIONS;
             i++) {
            REQUIRE(library_rows(b, LIBRARY_REJECTIONS[i].source, &rows));
            array = rows.ptr;
            REQUIRE(array->count == 2);
            pin_value *rejection = guard_row(&rows, 1);
            REQUIRE(scalar_field(rejection, "kind").i == SCALAR_REJECTION_ROW);
            pin_string message = pin_text(scalar_field(rejection, "text"));
            if (strcmp(message.bytes, LIBRARY_REJECTIONS[i].message)) {
                fprintf(stderr, "library rejection %zu: %s\n", i, message.bytes);
            }
            REQUIRE(!strcmp(message.bytes, LIBRARY_REJECTIONS[i].message));
            arena_free(&detached);
        }
    }
    return true;
}
/* One fixture load serves both, since each load takes minutes. */
TEST_CASE(package_compiler_scalar_rejects_unions_and_reads_libraries)
{
    REQUIRE(prepare());
    REQUIRE(scalar_rejects_unsupported());
    REQUIRE(scalar_collects_libraries());
    return true;
}
TEST_CASE(package_compiler_scalar_results_require_the_fixture_value)
{
    const source_package_input cases[] = { SCALAR_CASES_SOURCE_INPUTS };
    REQUIRE(compiler_fixture_prepare(&corpus, cases, (int)(sizeof cases / sizeof *cases)));
    static const int32_t expected[] = { 42, 0, INT32_MAX };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 3; i++) {
            pin_value ordinal = { .type = PIN_I32, .i = i }, literal;
            REQUIRE(compiler_fixture_call(
                &corpus,
                b,
                CASES_PACKAGE,
                "ExpectedValue",
                &ordinal,
                1,
                COMPILE_BUDGET,
                &detached,
                &literal
            ));
            REQUIRE(literal.type == PIN_I32 && literal.i == expected[i]);
            int64_t values[4] = { expected[i], expected[i], expected[i], expected[i] };
            REQUIRE(scalar_results_match(values, literal.i));
            int64_t wrong = expected[i] == 42 ? 41 : 1;
            const int64_t consensus[4] = { wrong, wrong, wrong, wrong };
            REQUIRE(!scalar_results_match(consensus, literal.i));
            for (int column = 0; column < 4; column++) {
                values[column] = wrong;
                REQUIRE(!scalar_results_match(values, literal.i));
                values[column] = expected[i];
            }
            arena_free(&detached);
        }
    }
    return true;
}
#if defined(__linux__) && defined(__x86_64__)
/* Native execution of the authored scalar corpus. Each case's source goes
 * through CollectScalar on one producer VM, alternating by case, and must be
 * the result Cases.Check expects. An accepted module's IR becomes O0 and O2
 * objects through the native driver's fixed opt and llc argument vectors
 * (test/native_aot/driver/README.md), with no undefined symbol. Each object,
 * linked as a validation-only shared library, runs in a child process, so a
 * trap is a status rather than the end of this test. Its entry's value on
 * both VMs and both native levels must match the fixture's literal expected
 * value, and so must each probe's. A probe reaches an internal function
 * through a validation-only IR adapter that passes it int64 arguments and
 * returns an int64, which is how every integer type and bool reaches it; the
 * adapter is not a foreign ABI. The modules run on Linux x86-64 here, and
 * the other targets the emitter admits run them from a bundle (see
 * test/native_aot/targets). CHIBI_LLVM_BIN names the directory of opt, llc,
 * ld.lld and llvm-nm; nothing is found through PATH. */
/* The corpus's accepted cases. */
enum
{
    SCALAR_ACCEPTED_CASES = 53
};
/* Cases whose 64-deep operand nesting takes CollectScalar past the stage-zero
 * fixture's call depth limit (SCRIPT_CALL_DEPTH_LIMIT). Run.ps1 runs them on
 * the recovered compiler, which has no such limit. */
static const char *const DEEP_CASES[] = { "deep_operands" };
enum
{
    DEEP_CASE_COUNT = (int)(sizeof DEEP_CASES / sizeof *DEEP_CASES)
};
static bool deep_case(pin_string name)
{
    for (int i = 0; i < DEEP_CASE_COUNT; i++) {
        if (pin_text_equal(
                name,
                (pin_string){ .bytes = DEEP_CASES[i], .length = strlen(DEEP_CASES[i]) }
            )) {
            return true;
        }
    }
    return false;
}
enum
{
    MAX_PROBE_ARGUMENTS = 3
};
typedef struct scalar_probe
{
    const char *source_case, *function;
    int count;
    int64_t arguments[MAX_PROBE_ARGUMENTS];
    int64_t expected;
} scalar_probe;
static const scalar_probe SCALAR_PROBES[] = {
    { "recursion", "Sum", 1, { 0 }, 0 },
    { "recursion", "Sum", 1, { 10 }, 55 },
    { "mutual_recursion", "A", 1, { 4 }, 1 },
    { "mutual_recursion", "B", 1, { 4 }, 2 },
    { "recursive_guard", "Down", 1, { -1 }, -1 },
    { "recursive_guard", "Down", 1, { 7 }, 0 },
    { "min", "Min", 2, { INT32_MIN, INT32_MAX }, INT32_MIN },
    { "min", "Min", 2, { INT32_MAX, INT32_MIN }, INT32_MIN },
    { "min", "Min", 2, { -1, -1 }, -1 },
    { "max", "Max", 2, { INT32_MIN, INT32_MAX }, INT32_MAX },
    { "max", "Max", 2, { INT32_MAX, INT32_MIN }, INT32_MAX },
    { "max", "Max", 2, { 0, -1 }, 0 },
    { "clamp", "Clamp", 3, { -5, 0, 10 }, 0 },
    { "clamp", "Clamp", 3, { 5, 0, 10 }, 5 },
    { "clamp", "Clamp", 3, { 15, 0, 10 }, 10 },
    { "clamp", "Clamp", 3, { INT32_MIN, INT32_MIN + 1, INT32_MAX }, INT32_MIN + 1 },
    { "operators", "If", 3, { 1, 4, 9 }, 4 },
    { "operators", "If", 3, { 0, 4, 9 }, 9 },
    { "call_condition", "Small", 1, { 9 }, 1 },
    { "call_condition", "Small", 1, { 10 }, 0 },
    { "lazy_arm", "Choose", 1, { 1 }, 1 },
    { "lazy_arm", "Choose", 1, { 0 }, 2 },
    { "bare_condition", "Positive", 1, { INT32_MIN }, 0 },
    { "bare_condition", "Positive", 1, { 0 }, 0 },
    { "bare_condition", "Positive", 1, { INT32_MAX }, INT32_MAX },
    { "int_operators", "Mix", 3, { 3, 4, 5 }, 37 },
    { "int_operators", "Mix", 3, { 4, 3, 5 }, 4 },
    /* (a + b) * c wraps to -6, and -c % 3 is -2. */
    { "int_operators", "Mix", 3, { INT32_MAX - 1, INT32_MAX, 2 }, -4 },
    /* The folded constant is 2^30; only the sum with x wraps. */
    { "arithmetic", "Half", 1, { 0 }, 1073741824 },
    { "arithmetic", "Half", 1, { INT32_MAX }, -1073741825 },
    { "folded_operand", "Add", 1, { INT32_MAX }, -1073741825 },
    /* The guard compares with 2^29, not the -2^29 wrapping rows give. */
    { "folded_guard", "Pick", 1, { 0 }, 0 },
    { "folded_guard", "Pick", 1, { 536870912 }, 0 },
    { "folded_guard", "Pick", 1, { 536870913 }, INT32_MAX },
    { "wrapped_operand", "Half", 1, { 1 }, 1 },
    { "wrapped_operand", "Half", 1, { -3 }, -1 },
    /* The low byte of an int, widened back. */
    { "conversion", "Low", 1, { -1 }, 255 },
    { "conversion", "Low", 1, { 256 }, 0 },
    { "conversion", "Low", 1, { INT32_MIN }, 0 },
    { "conversion", "Low", 1, { INT32_MAX }, 255 },
    { "shift", "Shl", 1, { -1 }, -4 },
    { "shift", "Shl", 1, { INT32_MIN }, 0 },
    { "shift", "Shl", 1, { INT32_MAX }, -4 },
    /* Each path of a && b's short circuit. */
    { "logical_and", "Mid", 3, { 2, 1, 3 }, 2 },
    { "logical_and", "Mid", 3, { 1, 3, 2 }, 1 },
    { "logical_and", "Mid", 3, { 1, 2, 3 }, 2 },
    { "bool_comparison", "Same", 2, { 1, 1 }, 1 },
    { "bool_comparison", "Same", 2, { 0, 1 }, 0 },
    { "helper_statement", "Id", 1, { INT32_MIN }, INT32_MIN },
};
/* The six comparisons of the operators case, in the order of their source
 * functions, each probed with every pair. */
static const char *const COMPARISONS[] = { "Lt", "Le", "Gt", "Ge", "Eq", "Ne" };
static const int32_t COMPARED[][2] = { { INT32_MIN, INT32_MAX },
                                       { INT32_MAX, INT32_MIN },
                                       { 0, 0 },
                                       { -1, 0 },
                                       { INT32_MAX, INT32_MAX } };
enum
{
    PROBES = (int)(sizeof SCALAR_PROBES / sizeof *SCALAR_PROBES),
    COMPARISON_COUNT = (int)(sizeof COMPARISONS / sizeof *COMPARISONS),
    PAIRS = (int)(sizeof COMPARED / sizeof *COMPARED),
    MAX_CASE_PROBES = 160
};
static int32_t compared(int comparison, int32_t a, int32_t b)
{
    switch (comparison) {
        case 0: return a < b;
        case 1: return a <= b;
        case 2: return a > b;
        case 3: return a >= b;
        case 4: return a == b;
        default: return a != b;
    }
}
/* llvm_native's integer operations, by the name the corpus's int_operators
 * case gives each one's function. */
static const struct
{
    const char *name;
    int op, parameters;
} TOTAL_OPERATIONS[] = {
    { "Add", 7, 2 },     { "Sub", 8, 2 },    { "Mul", 9, 2 },
    { "Div", 10, 2 },    { "Mod", 11, 2 },   { "Neg", 12, 1 },
    { "BitAnd", 13, 2 }, { "BitOr", 14, 2 }, { "BitXor", 15, 2 },
    { "BitNot", 16, 1 }, { "Shl", 17, 2 },   { "Shr", 18, 2 },
};
static const int32_t TOTAL_VALUES[] = { INT32_MIN, -7, -1, 0,
                                        1,         31, 32, INT32_MAX };
enum
{
    TOTAL_OPERATION_COUNT = (int)(sizeof TOTAL_OPERATIONS / sizeof *TOTAL_OPERATIONS),
    TOTAL_VALUE_COUNT = (int)(sizeof TOTAL_VALUES / sizeof *TOTAL_VALUES),
    /* INT32_MIN, -7, -1 and 0. */
    INT_OPERANDS = 4
};
/* The language's integer contract for operation `op`: nothing fails. */
static int32_t total(int op, int32_t a, int32_t b)
{
    uint32_t x = (uint32_t)a, y = (uint32_t)b;
    switch (op) {
        case 7: return (int32_t)(x + y);
        case 8: return (int32_t)(x - y);
        case 9: return (int32_t)(x * y);
        case 10: return !b ? 0 : b == -1 ? (int32_t)(0u - x) : a / b;
        case 11: return !b ? a : b == -1 ? 0 : a % b;
        case 12: return (int32_t)(0u - x);
        case 13: return a & b;
        case 14: return a | b;
        case 15: return a ^ b;
        case 16: return ~a;
        case 17: return y >= 32 ? 0 : (int32_t)(x << y);
        default: return y >= 32 ? (a < 0 ? -1 : 0) : a >> y;
    }
}
/* The probes of one corpus case. */
static int case_probes(const char *name, scalar_probe probes[MAX_CASE_PROBES])
{
    int count = 0;
    for (int i = 0; i < PROBES; i++) {
        if (!strcmp(SCALAR_PROBES[i].source_case, name)) {
            probes[count++] = SCALAR_PROBES[i];
        }
    }
    /* Each int operator but the shifts, which the int_operators case does
     * not declare, on the first values of TOTAL_VALUES. */
    for (int k = 0; !strcmp(name, "int_operators") && k < TOTAL_OPERATION_COUNT - 2;
         k++) {
        int n = TOTAL_OPERATIONS[k].parameters, rights = n == 2 ? INT_OPERANDS : 1;
        for (int i = 0; i < INT_OPERANDS; i++) {
            for (int j = 0; j < rights; j++) {
                int32_t a = TOTAL_VALUES[i], b = n == 2 ? TOTAL_VALUES[j] : 0;
                probes[count++] = (scalar_probe){ "int_operators",
                                                  TOTAL_OPERATIONS[k].name,
                                                  n,
                                                  { a, b },
                                                  total(TOTAL_OPERATIONS[k].op, a, b) };
            }
        }
    }
    for (int c = 0; !strcmp(name, "operators") && c < COMPARISON_COUNT; c++) {
        for (int p = 0; p < PAIRS; p++) {
            probes[count++] = (scalar_probe){
                "operators",
                COMPARISONS[c],
                2,
                { COMPARED[p][0], COMPARED[p][1] },
                compared(c, COMPARED[p][0], COMPARED[p][1])
            };
        }
    }
    assert(count <= MAX_CASE_PROBES);
    return count;
}
static bool llvm_tool(const char *name, char path[PATH_MAX])
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "CHIBI_LLVM_BIN must name the directory of opt, llc, ld.lld and llvm-nm\n");
    }
    REQUIRE(bin && *bin);
    REQUIRE(snprintf(path, PATH_MAX, "%s/%s", bin, name) < PATH_MAX);
    REQUIRE(!access(path, X_OK));
    return true;
}
/* Set by SIGINT or SIGTERM while the native tests run: the running child's
 * process group is killed, and the run reports cancellation, not a timeout. */
static volatile sig_atomic_t native_cancelled;
static void cancel_native(int)
{
    native_cancelled = 1;
}
/* The deadlines of an LLVM tool and of a library's calls, and the output
 * kept of each. */
static const double TOOL_SECONDS = 60, CALLS_SECONDS = 20, SPIN_SECONDS = 1;
enum
{
    NATIVE_OUTPUT = 1 << 16
};
/* The standard error of the last native run. */
static char native_errors[NATIVE_OUTPUT];
static const char *native_outcome(chibi_process_capture_result result)
{
    switch (result.error) {
        case CHIBI_PROCESS_OK: return result.status ? "failed" : "passed";
        case CHIBI_PROCESS_TIMEOUT: return "timed out";
        case CHIBI_PROCESS_CANCELLED: return "was cancelled";
        case CHIBI_PROCESS_SPAWN_FAILED: return "could not start";
        default: return "could not be supervised";
    }
}
/* Runs argv, an absolute executable with a fixed argument vector, in
 * `directory` under chibi_process_capture: its process group is killed at
 * the deadline, `seconds` from now, or on cancellation. `out` receives its
 * standard output; a run that does not pass reports its error output. */
static chibi_process_capture_result run_native(
    const char *const argv[],
    const char *directory,
    double seconds,
    char out[NATIVE_OUTPUT]
)
{
    chibi_process_output output = { out, NATIVE_OUTPUT, 0, false },
                         errors = { native_errors, sizeof native_errors, 0, false };
    chibi_process_capture_result result = chibi_process_capture(
        argv[0],
        argv,
        directory,
        (const char *const *)environ,
        chibi_process_now() + seconds,
        &native_cancelled,
        &output,
        &errors
    );
    if (result.error != CHIBI_PROCESS_OK || result.status) {
        fprintf(
            stderr,
            "%s %s (status %d): %s%s\n",
            argv[0],
            native_outcome(result),
            result.status,
            out,
            native_errors
        );
    }
    return result;
}
static bool native_passed(chibi_process_capture_result result)
{
    return result.error == CHIBI_PROCESS_OK && !result.status;
}
/* An LLVM tool's run, which must pass. */
static bool run_tool(const char *const argv[], const char *directory, char out[NATIVE_OUTPUT])
{
    REQUIRE(native_passed(run_native(argv, directory, TOOL_SECONDS, out)));
    return true;
}
static bool write_text(const char *path, const char *first, const char *second)
{
    FILE *file = fopen(path, "w");
    REQUIRE(file);
    bool written = fputs(first, file) >= 0 && fputs(second, file) >= 0;
    REQUIRE(!fclose(file) && written);
    return true;
}
/* The files the native builds wrote, which the test removes. */
static char **native_files;
static void native_file(const char *path)
{
    for (int i = 0; i < acount(native_files); i++) {
        if (!strcmp(native_files[i], path)) {
            return;
        }
    }
    char *copy = xmalloc(strlen(path) + 1);
    strcpy(copy, path);
    apush(native_files, copy);
}
/* `ir` and then `adapter`, built at `level` with the driver's exact opt and
 * llc arguments into `object`. `module` receives the path of the optimized
 * module in `directory`, and `undefined` the object's undefined symbols,
 * one per line. */
static bool native_object(
    const char *directory,
    const char *name,
    const char *ir,
    const char *adapter,
    int level,
    const char *object,
    char module[PATH_MAX],
    char undefined[NATIVE_OUTPUT]
)
{
    char opt[PATH_MAX], llc[PATH_MAX], nm[PATH_MAX];
    REQUIRE(llvm_tool("opt", opt) && llvm_tool("llc", llc) && llvm_tool("llvm-nm", nm));
    char source[PATH_MAX];
    snprintf(source, sizeof source, "%s/%s-%d.ll", directory, name, level);
    snprintf(module, PATH_MAX, "%s/%s-%d.opt.ll", directory, name, level);
    native_file(source);
    native_file(module);
    REQUIRE(write_text(source, ir, adapter));
    const char *passes = level ? "-passes=verify,default<O2>,verify" : "-passes=verify";
    const char *const optimize[] = { opt,  "-S",   passes, source,
                                     "-o", module, NULL };
    const char *const emit[] = { llc,
                                 "-filetype=obj",
                                 level ? "-O=2" : "-O=0",
                                 "-relocation-model=pic",
                                 module,
                                 "-o",
                                 object,
                                 NULL };
    const char *const symbols[] = { nm, "--undefined-only", "--just-symbol-name", object, NULL };
    REQUIRE(run_tool(optimize, directory, undefined) && run_tool(emit, directory, undefined));
    REQUIRE(run_tool(symbols, directory, undefined));
    return true;
}
/* `ir` and then `adapter`, built at `level` with the driver's exact object
 * arguments, then linked as `directory`/`name`.so, which `library` names. */
static bool native_library(
    const char *directory,
    const char *name,
    const char *ir,
    const char *adapter,
    int level,
    char library[PATH_MAX]
)
{
    char lld[PATH_MAX], object[PATH_MAX], module[PATH_MAX], out[NATIVE_OUTPUT];
    REQUIRE(llvm_tool("ld.lld", lld));
    snprintf(object, sizeof object, "%s/%s-%d.o", directory, name, level);
    snprintf(library, PATH_MAX, "%s/%s-%d.so", directory, name, level);
    native_file(object);
    native_file(library);
    REQUIRE(native_object(directory, name, ir, adapter, level, object, module, out));
    if (*out) {
        fprintf(stderr, "%s has undefined symbols: %s\n", object, out);
    }
    REQUIRE(!*out);
    const char *const link[] = { lld, "-shared", object, "-o", library, NULL };
    REQUIRE(run_tool(link, directory, out));
    return true;
}
/* A row field of a retained module's row `index`. */
static int row_int(const pin_value *rows, int index, const char *name)
{
    const pin_array *array = rows->ptr;
    return scalar_field(&array->items[index], name).i;
}
static const char *row_name(const pin_value *rows, int index)
{
    const pin_array *array = rows->ptr;
    return pin_text(scalar_field(&array->items[index], "name")).bytes;
}
static int row_count(const pin_value *rows)
{
    return ((const pin_array *)rows->ptr)->count;
}
/* The ordinal of function `name` among the module's functions and its row,
 * or -1. */
static int function_row(const pin_value *rows, const char *name, int *ordinal)
{
    *ordinal = -1;
    for (int i = 0; i < row_count(rows); i++) {
        if (row_int(rows, i, "kind") != SCALAR_FUNCTION_ROW) {
            continue;
        }
        ++*ordinal;
        if (!strcmp(row_name(rows, i), name)) {
            return i;
        }
    }
    return -1;
}
/* A scalar_facts type's bits, 1 for bool, whether it is unsigned, and
 * whether it is a float or double. */
static int scalar_bits(int type)
{
    static const int BITS[] = { [SCALAR_INT] = 32,   [SCALAR_BOOL] = 1,
                                [SCALAR_INT8] = 8,   [SCALAR_BYTE] = 8,
                                [SCALAR_INT16] = 16, [SCALAR_UINT16] = 16,
                                [SCALAR_UINT] = 32,  [SCALAR_INT64] = 64,
                                [SCALAR_WORD] = 64,  [SCALAR_FLOAT] = 32,
                                [SCALAR_DOUBLE] = 64 };
    assert(type > 0 && type < (int)(sizeof BITS / sizeof *BITS) && BITS[type]);
    return BITS[type];
}
static bool scalar_unsigned(int type)
{
    return type == SCALAR_BOOL || type == SCALAR_BYTE ||
           type == SCALAR_UINT16 || type == SCALAR_UINT || type == SCALAR_WORD;
}
static bool scalar_floating(int type)
{
    return type == SCALAR_FLOAT || type == SCALAR_DOUBLE;
}
static const char *llvm_type(int type)
{
    if (scalar_floating(type)) {
        return type == SCALAR_FLOAT ? "float" : "double";
    }
    static const char *const NAMES[] = { [1] = "i1",
                                         [8] = "i8",
                                         [16] = "i16",
                                         [32] = "i32",
                                         [64] = "i64" };
    return NAMES[scalar_bits(type)];
}
/* Appends to `adapter` the validation-only probe @probe_<name>(ptr): it reads
 * each parameter as an int64 of its argument array, truncated to the
 * parameter's type, and returns the function's result extended to int64 as
 * its type's value: sign-extended when signed, zero-extended otherwise. A
 * float or double travels as its encoding's bits, zero-extended, and a NaN
 * result as the canonical quiet NaN, since IEEE leaves a NaN's sign and
 * payload open. */
static bool append_adapter(const pin_value *rows, const char *name, char **adapter)
{
    int ordinal;
    int head = function_row(rows, name, &ordinal);
    REQUIRE(head > 0 && ordinal > 0);
    int count = row_int(rows, head, "count"), result = row_int(rows, head, "type");
    char text[4096], call[1024] = "";
    int n = snprintf(text, sizeof text, "\ndefine i64 @probe_%s(ptr %%args) {\n", name);
    for (int i = 0; i < count; i++) {
        int type = row_int(rows, head + 1 + i, "type");
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%p%d = getelementptr i64, ptr %%args, i64 %d\n  %%w%d = load "
            "i64, ptr %%p%d\n",
            i,
            i,
            i,
            i
        );
        if (type == SCALAR_FLOAT) {
            n += snprintf(
                text + n,
                sizeof text - (size_t)n,
                "  %%t%d = trunc i64 %%w%d to i32\n  %%a%d = bitcast i32 %%t%d "
                "to float\n",
                i,
                i,
                i,
                i
            );
        } else if (type == SCALAR_DOUBLE) {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%a%d = bitcast i64 %%w%d to double\n", i, i);
        } else if (scalar_bits(type) == 64) {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%a%d = add i64 %%w%d, 0\n", i, i);
        } else {
            n += snprintf(
                text + n,
                sizeof text - (size_t)n,
                "  %%a%d = trunc i64 %%w%d to %s\n",
                i,
                i,
                llvm_type(type)
            );
        }
        size_t used = strlen(call);
        snprintf(call + used, sizeof call - used, "%s%s %%a%d", i ? ", " : "", llvm_type(type), i);
    }
    n += snprintf(
        text + n,
        sizeof text - (size_t)n,
        "  %%r = call %s @chibi_scalar.%d(%s)\n",
        llvm_type(result),
        ordinal,
        call
    );
    if (scalar_floating(result)) {
        const char *type = llvm_type(result);
        int bits = scalar_bits(result);
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%n = fcmp uno %s %%r, 0.0\n  %%c = select i1 %%n, %s "
            "0x7FF8000000000000, %s %%r\n  %%b = bitcast %s %%c to i%d\n",
            type,
            type,
            type,
            type,
            bits
        );
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            bits == 64 ? "  ret i64 %%b\n}\n" : "  %%z = zext i32 %%b to i64\n  ret i64 %%z\n}\n"
        );
    } else if (scalar_bits(result) == 64) {
        n += snprintf(text + n, sizeof text - (size_t)n, "  ret i64 %%r\n}\n");
    } else {
        n += snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%z = %s %s %%r to i64\n  ret i64 %%z\n}\n",
            scalar_unsigned(result) ? "zext" : "sext",
            llvm_type(result)
        );
    }
    REQUIRE(n < (int)sizeof text);
    size_t old = strlen(*adapter);
    *adapter = xrealloc(*adapter, old + (size_t)n + 1);
    memcpy(*adapter + old, text, (size_t)n + 1);
    return true;
}
/* The canonical quiet NaN of a float or double, which adapters return for
 * every NaN. */
static const int64_t FLOAT_NAN = 0x7fc00000, DOUBLE_NAN = INT64_C(0x7ff8000000000000);
/* The bits a float or double `value` of `type` travels as: a float's
 * zero-extended, and a NaN canonical. */
static int64_t floating_bits(int type, double value)
{
    if (isnan(value)) {
        return type == SCALAR_FLOAT ? FLOAT_NAN : DOUBLE_NAN;
    }
    if (type == SCALAR_FLOAT) {
        float narrow = (float)value;
        uint32_t bits;
        memcpy(&bits, &narrow, sizeof bits);
        return bits;
    }
    int64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}
static double floating_value(int type, int64_t bits)
{
    if (type == SCALAR_FLOAT) {
        uint32_t narrow_bits = (uint32_t)bits;
        float narrow;
        memcpy(&narrow, &narrow_bits, sizeof narrow);
        return narrow;
    }
    double value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
/* A probe's arguments as the JSON array the compiler host takes. A float or
 * double is a JSON real, NaN or an infinity, never an integer, so -0 keeps
 * its sign. */
static void probe_json(const pin_value *rows, const scalar_probe *probe, char *json, size_t size)
{
    int ordinal;
    int head = function_row(rows, probe->function, &ordinal);
    int n = snprintf(json, size, "[");
    for (int i = 0; i < probe->count; i++) {
        int type = row_int(rows, head + 1 + i, "type");
        const char *separator = i ? "," : "";
        double value = scalar_floating(type)
                         ? floating_value(type, probe->arguments[i])
                         : 0;
        if (type == SCALAR_BOOL) {
            n += snprintf(json + n, size - (size_t)n, "%s%s", separator, probe->arguments[i] ? "true" : "false");
        } else if (scalar_floating(type) && (isnan(value) || isinf(value))) {
            const char *special = isnan(value) ? "NaN"
                                : value > 0    ? "Infinity"
                                               : "-Infinity";
            n += snprintf(json + n, size - (size_t)n, "%s%s", separator, special);
        } else if (scalar_floating(type)) {
            n += snprintf(json + n, size - (size_t)n, "%s%.17e", separator, value);
        } else if (type == SCALAR_WORD) {
            n += snprintf(
                json + n,
                size - (size_t)n,
                "%s%" PRIu64,
                separator,
                (uint64_t)probe->arguments[i]
            );
        } else {
            n += snprintf(
                json + n,
                size - (size_t)n,
                "%s%" PRId64,
                separator,
                probe->arguments[i]
            );
        }
    }
    snprintf(json + n, size - (size_t)n, "]");
}
/* Loads `bytes`, a closure, as the test's host on `backend`. */
static bool vm_load(const unsigned char *bytes, size_t length, int backend)
{
    char error[256];
    host = compiler_host_load(
        bytes,
        length,
        backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    if (!host) {
        fprintf(stderr, "scalar closure load: %s\n", error);
    }
    REQUIRE(host);
    return true;
}
/* The value of `type` the loaded closure's `function` of package scalar
 * returns for `json`, as the int64 its probe adapter gives: a bool as 0 or 1,
 * a uint64 as its bits, and a float or double as floating_bits. */
static bool vm_value(const char *function, const char *json, int type, int64_t *out)
{
    char name[256];
    snprintf(name, sizeof name, "project:scalar:1:%s:callable", function);
    bool valid = compiler_host_invoke(
        host,
        script_catalog_id("package-symbol", name),
        json,
        strlen(json),
        TARGET_BUDGET,
        &result
    );
    if (!valid || result.flow_status != FLOW_OK) {
        fprintf(
            stderr,
            "scalar closure %s%s failed with status %d\n",
            function,
            json,
            result.flow_status
        );
    }
    REQUIRE(valid && result.flow_status == FLOW_OK);
    /* The host's result is the JSON array of the function's one result. */
    if (!strcmp(result.json, "[true]") || !strcmp(result.json, "[false]")) {
        *out = !strcmp(result.json, "[true]");
    } else if (scalar_floating(type)) {
        /* The host writes a float as the shortest text that reads back as
         * it, so a float reads as one, not through a double. */
        char *end = result.json;
        double value = 0;
        if (result.json[0] == '[') {
            value = type == SCALAR_FLOAT ? strtof(result.json + 1, &end)
                                         : strtod(result.json + 1, &end);
        }
        bool read = end != result.json + 1 && !strcmp(end, "]");
        if (!read) {
            fprintf(
                stderr,
                "scalar closure %s%s gave %s\n",
                function,
                json,
                result.json
            );
        }
        REQUIRE(read);
        *out = floating_bits(type, value);
    } else {
        char *end = result.json;
        errno = 0;
        if (result.json[0] == '[') {
            *out = type == SCALAR_WORD ? (int64_t)strtoull(result.json + 1, &end, 10)
                                       : strtoll(result.json + 1, &end, 10);
        }
        bool read = end != result.json && !errno && !strcmp(end, "]") &&
                    (type != SCALAR_WORD || result.json[1] != '-');
        if (read && scalar_bits(type) < 64) {
            int64_t limit = INT64_C(1)
                         << (scalar_bits(type) - !scalar_unsigned(type));
            read = *out < limit && *out >= (scalar_unsigned(type) ? 0 : -limit);
        }
        if (!read) {
            fprintf(
                stderr,
                "scalar closure %s%s gave %s\n",
                function,
                json,
                result.json
            );
        }
        REQUIRE(read);
    }
    free(result.json);
    result = (compiler_host_result){ 0 };
    return true;
}
/* The authored compiler's closure of `source` as package scalar. */
static bool vm_closure(int b, pin_string source, unsigned char **bytes, size_t *length)
{
    const source_package_input inputs[] = {
        { "scalar/Main.chibi", source.bytes, source.length }
    };
    compiler_result out;
    REQUIRE(compile(b, false, false, false, inputs, 1, COMPILE_BUDGET, &out));
    REQUIRE(compiler_succeeded(&out));
    *bytes = closure_bytes(&out, length);
    return true;
}
/* The text of `ir` from `start` to the end of the definition it begins. */
static char *definition(const char *ir, const char *start)
{
    const char *at = strstr(ir, start);
    assert(at);
    const char *end = strstr(at, "\n}\n");
    assert(end);
    size_t length = (size_t)(end - at);
    char *text = xmalloc(length + 1);
    memcpy(text, at, length);
    text[length] = 0;
    return text;
}
/* The text of `ir` from label `p<pc>` on, or NULL. */
static const char *labelled(const char *ir, int pc)
{
    char label[32];
    snprintf(label, sizeof label, "\np%d:\n", pc);
    return strstr(ir, label);
}
/* Unoptimized IR keeps what the source decides at run time. Min moves its
 * parameters to locals, compares them and branches on the result. Choose's
 * branch goes to its consequence, which calls Taken, and otherwise jumps past
 * it to the call of Skipped, so each runs only on its own path. Mid compares
 * b with c only after branching on a < b. */
static bool keeps_guards(const char *name, const char *ir)
{
    if (!strcmp(name, "min")) {
        char *min = definition(ir, "define internal i32 @chibi_scalar.1(i32 %arg0, i32 %arg1)");
        bool kept = strstr(min, "store i32 %arg0, ptr %s0") &&
                    strstr(min, "store i32 %arg1, ptr %s1") &&
                    strstr(min, "%a5 = load i32, ptr %l0\n  store i32 %a5, ptr %s2") &&
                    strstr(min, "%a7 = load i32, ptr %s2\n  %b7 = load i32, ptr %s3\n  %v7 = icmp slt i32 %a7, %b7") &&
                    strstr(min, "store i1 %v7, ptr %s4") &&
                    strstr(min, "br i1 %a8, label %p");
        free(min);
        REQUIRE(kept);
    }
    if (!strcmp(name, "lazy_arm")) {
        char *choose = definition(ir, "define internal i32 @chibi_scalar.1(i1 %arg0)");
        /* The if statement's branch, not a budget check's `br i1 %available`. */
        const char *branch = strstr(choose, "  br i1 %a");
        while (branch && !isdigit((unsigned char)branch[strlen("  br i1 %a")])) {
            branch = strstr(branch + 1, "  br i1 %a");
        }
        int consequence = -1, otherwise = -1, past = -1;
        REQUIRE(
            branch &&
            sscanf(branch, "  br i1 %%a%*d, label %%p%d, label %%p%d", &consequence, &otherwise) == 2
        );
        const char *jump = labelled(choose, otherwise);
        REQUIRE(jump && sscanf(strchr(jump + 1, '\n'), "\n  br label %%p%d", &past) == 1);
        const char *arm = labelled(choose, consequence),
                   *rest = labelled(choose, past);
        const char *taken = strstr(choose, "call i32 @chibi_scalar.2()");
        const char *skipped = strstr(choose, "call i32 @chibi_scalar.3()");
        bool lazy = arm && rest && taken && skipped && branch < arm &&
                    arm < taken && taken < rest && rest < skipped &&
                    !strstr(taken + 1, "call i32 @chibi_scalar.2()") &&
                    !strstr(skipped + 1, "call i32 @chibi_scalar.3()");
        free(choose);
        REQUIRE(lazy);
    }
    if (!strcmp(name, "logical_and")) {
        char *mid = definition(ir, "define internal i32 @chibi_scalar.1(i32 %arg0, i32 %arg1, i32 %arg2)");
        const char *first = strstr(mid, "icmp slt");
        const char *branch = first ? strstr(first, "  br i1 %a") : NULL;
        const char *second = branch ? strstr(branch, "icmp slt") : NULL;
        bool lazy = second && !strstr(second + 1, "icmp slt");
        free(mid);
        REQUIRE(lazy);
    }
    return true;
}
/* A call a case checks, its entry or a probe, and what it gave on the
 * bytecode and compiled VMs and natively at O0 and O2. An entry of a module
 * that calls the runtime takes a context. */
typedef struct scalar_call
{
    scalar_probe probe;
    char json[128], symbol[64];
    int64_t values[4];
    bool context;
} scalar_call;
enum
{
    MAX_CASE_CALLS = MAX_CASE_PROBES + 1,
    /* A width or floating module's entry and probes, the most of any
     * module. */
    MAX_WIDTH_CALLS = 1024
};
/* Case `name`'s calls: its entry, then each of its probes. */
static int case_calls(const char *name, const pin_value *rows, int64_t expected, scalar_call calls[MAX_CASE_CALLS])
{
    scalar_probe probes[MAX_CASE_PROBES];
    int count = case_probes(name, probes);
    calls[0] = (scalar_call){ .probe = { name, "Main", 0, { 0 }, expected },
                              .json = "[]",
                              .symbol = "chibi_scalar" };
    for (int i = 0; i < count; i++) {
        scalar_call *call = &calls[i + 1];
        *call = (scalar_call){ .probe = probes[i] };
        probe_json(rows, &probes[i], call->json, sizeof call->json);
        snprintf(call->symbol, sizeof call->symbol, "probe_%s", probes[i].function);
    }
    return count + 1;
}
/* Each call's value from the authored compiler's closure on both VMs. */
static bool vm_values(
    const unsigned char *closure,
    size_t length,
    const pin_value *rows,
    scalar_call *calls,
    int count
)
{
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(vm_load(closure, length, backend));
        for (int i = 0; i < count; i++) {
            int ordinal;
            int type = row_int(rows, function_row(rows, calls[i].probe.function, &ordinal), "type");
            REQUIRE(
                vm_value(calls[i].probe.function, calls[i].json, type, &calls[i].values[backend])
            );
        }
        compiler_host_free(host);
        host = NULL;
    }
    return true;
}
typedef int32_t (*scalar_entry)();
typedef int32_t (*scalar_runtime_entry)(execution_native_context *);
typedef int64_t (*scalar_adapter)(const int64_t *);
enum
{
    CALL_SPEC = 160
};
/* Runs each call of `library` in order, in a child run of this binary (see
 * test_language_package_compiler_chibi_native_call) that must finish within
 * `seconds`, and stores each value in its call's `column`. A call of a
 * probe_ symbol is a probe adapter, with or without arguments; any other,
 * such as the entry, takes none, or only a context, and returns an int32. */
static chibi_process_capture_result library_calls(
    const char *directory,
    const char *library,
    scalar_call *calls,
    int count,
    int column,
    double seconds
)
{
    char self[PATH_MAX], out[NATIVE_OUTPUT];
    ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
    assert(length > 0 && count <= MAX_WIDTH_CALLS);
    self[length] = 0;
    char (*specs)[CALL_SPEC] = xmalloc(sizeof *specs * (size_t)count);
    const char **argv = xmalloc(sizeof *argv * (size_t)(count + 4));
    argv[0] = self;
    argv[1] = "--package-compiler-chibi-native-call";
    argv[2] = library;
    for (int i = 0; i < count; i++) {
        bool adapter = !strncmp(calls[i].symbol, "probe_", strlen("probe_"));
        assert(!adapter || !calls[i].context);
        int n = snprintf(
            specs[i],
            CALL_SPEC,
            "%s%s%s",
            calls[i].context ? "+" : "",
            calls[i].symbol,
            adapter ? ":" : ""
        );
        for (int a = 0; adapter && a < calls[i].probe.count; a++) {
            n += snprintf(
                specs[i] + n,
                CALL_SPEC - (size_t)n,
                "%s%" PRId64,
                a ? "," : "",
                calls[i].probe.arguments[a]
            );
        }
        assert(n < CALL_SPEC);
        argv[3 + i] = specs[i];
    }
    argv[3 + count] = NULL;
    chibi_process_capture_result result = run_native(argv, directory, seconds, out);
    free(argv);
    free(specs);
    const char *at = out;
    for (int i = 0; native_passed(result) && i < count; i++) {
        char *end;
        errno = 0;
        calls[i].values[column] = strtoll(at, &end, 10);
        assert(end != at && *end == '\n' && !errno);
        at = end + 1;
    }
    return result;
}
/* The child of library_calls: opens `library`, then calls each spec in order,
 * `symbol` for the entry, `+symbol` for one that takes a context, or
 * `symbol:a,b` for an adapter, `symbol:` for one of no arguments, and prints
 * each value on its own line as it returns. A context's heap is freed after
 * its call, as the VM frees it. */
static int library_child(const char *library, int count, char **specs)
{
    stack_overflow_init();
    void *opened = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!opened) {
        fprintf(stderr, "native scalar load: %s\n", dlerror());
        return 1;
    }
    for (int i = 0; i < count; i++) {
        char symbol[CALL_SPEC];
        int64_t arguments[MAX_PROBE_ARGUMENTS];
        bool context = specs[i][0] == '+';
        const char *spec = specs[i] + context;
        const char *at = spec + strcspn(spec, ":");
        size_t length = (size_t)(at - spec);
        assert(length < sizeof symbol);
        memcpy(symbol, spec, length);
        symbol[length] = 0;
        bool adapter = *at;
        int n = 0;
        for (char *end; adapter && *at && at[1]; at = end, n++) {
            assert(n < MAX_PROBE_ARGUMENTS);
            arguments[n] = strtoll(at + 1, &end, 10);
        }
        void *function = dlsym(opened, symbol);
        if (!function) {
            fprintf(stderr, "native scalar symbol %s: %s\n", symbol, dlerror());
            return 1;
        }
        int64_t value;
        if (context) {
            execution_native_frame frame;
            execution_native_frame_init(
                &frame,
                (execution_native_runtime){ NATIVE_FORMAT_TEXT_HOST.value }
            );
            value = ((scalar_runtime_entry)function)(&frame.context);
            execution_native_frame_free(&frame);
        } else {
            value = adapter ? ((scalar_adapter)function)(arguments)
                            : ((scalar_entry)function)();
        }
        printf("%" PRId64 "\n", value);
        fflush(stdout);
    }
    return 0;
}
/* The other targets native_abi admits. When CHIBI_NATIVE_BUNDLE names a
 * directory, each module the native tests run is also emitted for each of
 * them and built with the driver's opt and llc arguments into the bundle,
 * whose runner (test/native_aot/targets) links and runs it on that target.
 * Each has native_abi's function, the bundle's subdirectory, the triple and
 * the data layout LLVM 18 gives it, which the driver admits, and the
 * symbols its objects may leave undefined, which the bundle's runtime.o
 * defines from `runtime`, or none. Windows code that uses floating point
 * references _fltused, and a frame larger than a page calls __chkstk; the C
 * runtime would define both. This __chkstk is compiler-rt's x86-64 probe, as
 * tools/native_aot/Driver.chibi's start object defines it: it touches each
 * page from the stack pointer down by RAX bytes and changes no register. */
enum
{
    BUNDLE_RUNTIME_SYMBOLS = 2
};
static const struct
{
    const char *function, *directory, *triple, *layout, *runtime;
    const char *defines[BUNDLE_RUNTIME_SYMBOLS];
} BUNDLE_TARGETS[] = {
    { "MacOSArm64",
      "macos-arm64",
      "arm64-apple-macosx11.0.0",
      "e-m:o-i64:64-i128:128-n32:64-S128",
      NULL,
      { NULL } },
    { "WindowsX64",
      "windows-x64",
      "x86_64-pc-windows-msvc",
      "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:"
      "64-S128",
      "@_fltused = dso_local global i32 0\n"
      "module asm \".text\"\n"
      "module asm \".globl __chkstk\"\n"
      "module asm \"__chkstk:\"\n"
      "module asm \"pushq %rcx\"\n"
      "module asm \"pushq %rax\"\n"
      "module asm \"cmpq $0x1000, %rax\"\n"
      "module asm \"leaq 24(%rsp), %rcx\"\n"
      "module asm \"jb 1f\"\n"
      "module asm \"2:\"\n"
      "module asm \"subq $0x1000, %rcx\"\n"
      "module asm \"testq %rcx, (%rcx)\"\n"
      "module asm \"subq $0x1000, %rax\"\n"
      "module asm \"cmpq $0x1000, %rax\"\n"
      "module asm \"ja 2b\"\n"
      "module asm \"1:\"\n"
      "module asm \"subq %rax, %rcx\"\n"
      "module asm \"testq %rcx, (%rcx)\"\n"
      "module asm \"popq %rax\"\n"
      "module asm \"popq %rcx\"\n"
      "module asm \"retq\"\n",
      { "__chkstk", "_fltused" } },
};
enum
{
    BUNDLE_TARGET_COUNT = (int)(sizeof BUNDLE_TARGETS / sizeof *BUNDLE_TARGETS)
};
/* Whether each line of `undefined` names a symbol target `t`'s runtime
 * defines. */
static bool bundle_runtime_defines(int t, const char *undefined)
{
    for (const char *at = undefined; *at;) {
        size_t length = strcspn(at, "\n");
        bool defined = false;
        for (int i = 0; i < BUNDLE_RUNTIME_SYMBOLS && BUNDLE_TARGETS[t].defines[i];
             i++) {
            const char *symbol = BUNDLE_TARGETS[t].defines[i];
            defined = defined ||
                      (strlen(symbol) == length && !strncmp(at, symbol, length));
        }
        if (!defined) {
            return false;
        }
        at += length + (at[length] == '\n');
    }
    return true;
}
/* The modules bundled so far, which number each one's objects: generated
 * modules share some corpus cases' names. */
static int bundled_modules;
static const char *bundle_root()
{
    const char *root = getenv("CHIBI_NATIVE_BUNDLE");
    return root && *root ? root : NULL;
}
/* Whether the module at `path` declares exactly target t's triple and
 * layout. */
static bool bundle_module_targets(const char *path, int t)
{
    char *text = file_read_text(path);
    REQUIRE(text);
    char triple[128], layout[256];
    snprintf(
        triple,
        sizeof triple,
        "\ntarget triple = \"%s\"\n",
        BUNDLE_TARGETS[t].triple
    );
    snprintf(
        layout,
        sizeof layout,
        "\ntarget datalayout = \"%s\"\n",
        BUNDLE_TARGETS[t].layout
    );
    bool declared = strstr(text, triple) && strstr(text, layout) &&
                    !strstr(strstr(text, triple) + 1, "\ntarget triple");
    free(text);
    if (!declared) {
        fprintf(
            stderr,
            "%s does not declare %s's triple and layout\n",
            path,
            BUNDLE_TARGETS[t].directory
        );
    }
    return declared;
}
/* Builds `ir`, emitted for bundle target t, and `adapter` at O0 and O2 into
 * the bundle, and lists each of `calls` after its object with the value
 * both VMs and the native Linux runs gave. */
static bool bundle_module(
    const char *directory,
    int t,
    const char *name,
    const char *ir,
    const char *adapter,
    const scalar_call *calls,
    int count
)
{
    char path[PATH_MAX], label[128];
    snprintf(
        path,
        sizeof path,
        "%s/%s/modules.txt",
        bundle_root(),
        BUNDLE_TARGETS[t].directory
    );
    snprintf(label, sizeof label, "%s-%s", BUNDLE_TARGETS[t].directory, name);
    FILE *list = fopen(path, "a");
    REQUIRE(list);
    bool built = true;
    for (int level = 0; level < 3; level += 2) {
        char file[128], object[PATH_MAX], module[PATH_MAX], undefined[NATIVE_OUTPUT];
        snprintf(file, sizeof file, "%d-%s-%d.o", bundled_modules, name, level);
        snprintf(
            object,
            sizeof object,
            "%s/%s/%s",
            bundle_root(),
            BUNDLE_TARGETS[t].directory,
            file
        );
        built = native_object(directory, label, ir, adapter, level, object, module, undefined) &&
                bundle_module_targets(module, t);
        if (built && !bundle_runtime_defines(t, undefined)) {
            fprintf(stderr, "%s has undefined symbols: %s\n", object, undefined);
            built = false;
        }
        if (!built) {
            break;
        }
        fprintf(list, "module %s\n", file);
        for (int i = 0; i < count; i++) {
            const scalar_probe *probe = &calls[i].probe;
            fprintf(
                list,
                "call %s %" PRId64 " %d",
                calls[i].symbol,
                probe->expected,
                probe->count
            );
            for (int a = 0; a < probe->count; a++) {
                fprintf(list, " %" PRId64, probe->arguments[a]);
            }
            fputc('\n', list);
        }
    }
    bundled_modules++;
    REQUIRE(!fclose(list) && built);
    return true;
}
/* An adapter for each function `calls` probe after the entry. */
static bool call_adapters(const pin_value *rows, const scalar_call *calls, int count, char **adapter)
{
    *adapter = xmalloc(1);
    **adapter = 0;
    for (int i = 1; i < count; i++) {
        if (i > 1 && !strcmp(calls[i].probe.function, calls[i - 1].probe.function)) {
            continue;
        }
        REQUIRE(append_adapter(rows, calls[i].probe.function, adapter));
    }
    return true;
}
/* Whether module `ir` calls the runtime: its entry takes a context. */
static bool calls_runtime(const char *ir)
{
    return strstr(ir, "@chibi_scalar(ptr %ctx)");
}
/* Each call's value natively: `ir` with an adapter for each probed function,
 * built at O0 and at O2. */
static bool native_values(
    const char *directory,
    const char *name,
    const pin_value *rows,
    const char *ir,
    scalar_call *calls,
    int count
)
{
    /* No adapter passes a context, so a module that calls the runtime is
     * checked through its entry alone. */
    calls[0].context = calls_runtime(ir);
    REQUIRE(!calls[0].context || count == 1);
    char *adapter;
    REQUIRE(call_adapters(rows, calls, count, &adapter));
    for (int level = 0; level < 2; level++) {
        char library[PATH_MAX];
        REQUIRE(native_library(directory, name, ir, adapter, level * 2, library));
        REQUIRE(native_passed(
            library_calls(directory, library, calls, count, 2 + level, CALLS_SECONDS)
        ));
    }
    free(adapter);
    return true;
}
/* Rename only one generated callee definition. Its call sites keep the old
 * symbol, which an observed wrapper supplies. No branch body is rewritten. */
static bool observed_body(char **ir, int ordinal, const char *name)
{
    char before[128], after[128];
    snprintf(before, sizeof before, "define internal i32 @chibi_scalar.%d()", ordinal);
    snprintf(after, sizeof after, "define internal i32 @chibi_test_%s_body()", name);
    const char *at = strstr(*ir, before);
    REQUIRE(at && !strstr(at + 1, before));
    size_t prefix = (size_t)(at - *ir), old = strlen(before),
           replacement = strlen(after);
    char *renamed = xmalloc(strlen(*ir) - old + replacement + 1);
    memcpy(renamed, *ir, prefix);
    memcpy(renamed + prefix, after, replacement);
    strcpy(renamed + prefix + replacement, at + old);
    free(*ir);
    *ir = renamed;
    return true;
}
/* Reset, Choose's return, Taken's entry count and Skipped's entry count. */
static bool lazy_trace_matches(const scalar_call calls[4], int column, int condition)
{
    return calls[0].values[column] == 0 &&
           calls[1].values[column] == (condition ? 1 : 2) &&
           calls[2].values[column] == condition &&
           calls[3].values[column] == !condition;
}
/* An additional validation-only module observes generated callee entries.
 * Normal optimization/result checks above still use the unmodified IR.
 * The extra-call variant keeps Choose's value but must fail the same trace
 * oracle on the true path, even after O2 inlines the pure original bodies. */
static bool observes_lazy_arm(const char *directory, const pin_value *rows, const char *ir)
{
    int taken, skipped;
    int taken_row = function_row(rows, "Taken", &taken);
    int skipped_row = function_row(rows, "Skipped", &skipped);
    REQUIRE(taken_row > 0 && skipped_row > 0);
    REQUIRE(row_int(rows, taken_row, "count") == 0 && row_int(rows, skipped_row, "count") == 0);
    REQUIRE(
        row_int(rows, taken_row, "type") == SCALAR_INT &&
        row_int(rows, skipped_row, "type") == SCALAR_INT
    );
    char *observed = xmalloc(strlen(ir) + 1), *probe = xmalloc(1);
    strcpy(observed, ir);
    *probe = 0;
    bool valid = observed_body(&observed, taken, "taken") &&
                 observed_body(&observed, skipped, "skipped") &&
                 append_adapter(rows, "Choose", &probe);
    for (int extra = 0; valid && extra < 2; extra++) {
        char discarded[128] = "", wrappers[2048];
        if (extra) {
            snprintf(discarded, sizeof discarded, "  %%discarded = call i32 @chibi_scalar.%d()\n", skipped);
        }
        int length = snprintf(
            wrappers,
            sizeof wrappers,
            "\n@chibi_test_taken = internal global i32 0\n"
            "@chibi_test_skipped = internal global i32 0\n"
            "define internal i32 @chibi_scalar.%d() {\nentry:\n"
            "  %%n = atomicrmw add ptr @chibi_test_taken, i32 1 monotonic\n"
            "%s"
            "  %%r = call i32 @chibi_test_taken_body()\n  ret i32 %%r\n}\n"
            "define internal i32 @chibi_scalar.%d() {\nentry:\n"
            "  %%n = atomicrmw add ptr @chibi_test_skipped, i32 1 monotonic\n"
            "  %%r = call i32 @chibi_test_skipped_body()\n  ret i32 %%r\n}\n"
            "define i32 @chibi_trace_reset() {\nentry:\n"
            "  store volatile i32 0, ptr @chibi_test_taken\n"
            "  store volatile i32 0, ptr @chibi_test_skipped\n  ret i32 0\n}\n"
            "define i32 @chibi_trace_taken() {\nentry:\n"
            "  %%n = load volatile i32, ptr @chibi_test_taken\n  ret i32 "
            "%%n\n}\n"
            "define i32 @chibi_trace_skipped() {\nentry:\n"
            "  %%n = load volatile i32, ptr @chibi_test_skipped\n  ret i32 "
            "%%n\n}\n",
            taken,
            discarded,
            skipped
        );
        assert(length > 0 && length < (int)sizeof wrappers);
        size_t probe_length = strlen(probe);
        char *adapter = xmalloc(probe_length + (size_t)length + 1);
        memcpy(adapter, probe, probe_length);
        memcpy(adapter + probe_length, wrappers, (size_t)length + 1);
        for (int level = 0; valid && level < 2; level++) {
            char library[PATH_MAX];
            valid = native_library(
                directory,
                extra ? "lazy_trace_extra" : "lazy_trace",
                observed,
                adapter,
                level * 2,
                library
            );
            for (int condition = 0; valid && condition < 2; condition++) {
                scalar_call calls[4] = {
                    { .symbol = "chibi_trace_reset" },
                    { .symbol = "probe_Choose",
                      .probe = { .count = 1, .arguments = { condition } } },
                    { .symbol = "chibi_trace_taken" },
                    { .symbol = "chibi_trace_skipped" }
                };
                int column = 2 + level;
                valid = native_passed(
                    library_calls(directory, library, calls, 4, column, CALLS_SECONDS)
                );
                bool matches = lazy_trace_matches(calls, column, condition);
                valid = valid && matches == (!extra || !condition) &&
                        calls[0].values[column] == 0 &&
                        calls[1].values[column] == (condition ? 1 : 2) &&
                        calls[2].values[column] == condition &&
                        calls[3].values[column] == (!condition || extra);
                fprintf(
                    stderr,
                    "lazy trace O%d extra %d condition %d: return %" PRId64 ", "
                    "Taken %" PRId64 ", Skipped %" PRId64 ", oracle %s\n",
                    level * 2,
                    extra,
                    condition,
                    calls[1].values[column],
                    calls[2].values[column],
                    calls[3].values[column],
                    matches ? "accept" : "reject"
                );
            }
        }
        free(adapter);
    }
    free(observed);
    free(probe);
    REQUIRE(valid);
    return true;
}
/* Every executed result matches its literal fixture or probe oracle. */
static bool calls_agree(const char *name, const scalar_call *calls, int count)
{
    for (int i = 0; i < count; i++) {
        const int64_t *values = calls[i].values;
        int64_t expected = calls[i].probe.expected;
        bool agree = scalar_results_match(values, expected);
        if (!agree) {
            fprintf(
                stderr,
                "scalar case %s %s%s: expected %" PRId64 ", VM %" PRId64
                " %" PRId64 ", native O0 %" PRId64 " O2 %" PRId64 "\n",
                name,
                calls[i].probe.function,
                calls[i].json,
                expected,
                values[0],
                values[1],
                values[2],
                values[3]
            );
        }
        REQUIRE(agree);
    }
    return true;
}
/* The IR EmitBytesFor gives for `rows` and native_abi's target `function`,
 * NUL-terminated, or NULL for none. */
static bool target_ir(int b, const char *function, const pin_value *rows, char **ir)
{
    pin_value emitted;
    REQUIRE(emit_bytes(b, function, rows, &emitted));
    const pin_array *bytes = emitted.ptr;
    REQUIRE(emitted.container == PIN_CONTAINER_ARRAY);
    *ir = NULL;
    if (!bytes->count) {
        return true;
    }
    *ir = xmalloc((size_t)bytes->count + 1);
    for (int i = 0; i < bytes->count; i++) {
        (*ir)[i] = (char)bytes->items[i].u8;
    }
    (*ir)[bytes->count] = 0;
    return true;
}
/* The Linux x86-64 IR EmitBytesFor gives for `rows`, or NULL for none. */
static bool emitted_ir(int b, pin_value *rows, char **ir)
{
    return target_ir(b, "LinuxX64", rows, ir);
}
/* `source`'s producer rows at `path` from CollectScalar on VM b. */
static bool collected_at(int b, const char *path, const char *case_name, pin_value source, pin_value *rows)
{
    pin_value args[SCALAR_ARGUMENTS];
    REQUIRE(scalar_arguments(b, path, source, args));
    bool collected = compiler_fixture_call(
        &fixture,
        b,
        PACKAGE,
        "CollectScalar",
        args,
        SCALAR_ARGUMENTS,
        COMPILE_BUDGET,
        &detached,
        rows
    );
    if (!collected) {
        fprintf(stderr, "scalar case %s: CollectScalar failed\n", case_name);
    }
    REQUIRE(collected);
    return true;
}
/* With a bundle, the module of `rows` for each other target, with `calls`,
 * whose values every run agreed on. */
static bool bundled(
    const char *directory,
    int b,
    const char *name,
    const pin_value *rows,
    const scalar_call *calls,
    int count
)
{
    /* A bundle's runner has no runtime to give a context. */
    if (!bundle_root() || calls[0].context) {
        return true;
    }
    char *adapter;
    REQUIRE(call_adapters(rows, calls, count, &adapter));
    bool built = true;
    for (int t = 0; built && t < BUNDLE_TARGET_COUNT; t++) {
        char *ir = NULL;
        built = target_ir(b, BUNDLE_TARGETS[t].function, rows, &ir) && ir &&
                bundle_module(directory, t, name, ir, adapter, calls, count);
        free(ir);
    }
    free(adapter);
    REQUIRE(built);
    return true;
}
static bool collected_rows(int b, const char *case_name, pin_value source, pin_value *rows)
{
    return collected_at(b, "Scalar.chibi", case_name, source, rows);
}
/* Each of `calls` of an accepted module, `ir` its IR, gives its expected
 * value on both VMs, from the authored compiler's closure of `source` on VM
 * b, and natively at O0 and O2. */
static bool runs_everywhere(
    const char *directory,
    int b,
    const char *name,
    pin_string source,
    const pin_value *rows,
    const char *ir,
    scalar_call *calls,
    int count
)
{
    unsigned char *closure;
    size_t length;
    REQUIRE(vm_closure(b, source, &closure, &length));
    bool agree = vm_values(closure, length, rows, calls, count) &&
                 native_values(directory, name, rows, ir, calls, count) &&
                 calls_agree(name, calls, count) &&
                 bundled(directory, b, name, rows, calls, count);
    free(closure);
    REQUIRE(agree);
    return true;
}
/* An accepted module, `ir` its IR, which this frees: each of its calls must
 * give one value on both VMs and natively at O0 and O2. */
static bool module_agrees(
    const char *directory,
    int b,
    const char *case_name,
    pin_string source,
    const pin_value *rows,
    char *ir,
    int64_t expected,
    scalar_call calls[MAX_CASE_CALLS]
)
{
    REQUIRE(keeps_guards(case_name, ir));
    int count = case_calls(case_name, rows, expected, calls);
    bool agree = runs_everywhere(directory, b, case_name, source, rows, ir, calls, count);
    if (agree && !strcmp(case_name, "lazy_arm")) {
        agree = observes_lazy_arm(directory, rows, ir);
    }
    free(ir);
    REQUIRE(agree);
    return true;
}
/* Case `index` of the corpus: its producer rows on VM b must be what the case
 * expects, and an accepted module must run natively as it does on both VMs. */
static bool scalar_case_agrees(const char *directory, int index, int *accepted, int *deep)
{
    int b = index % 2;
    pin_value ordinal = { .type = PIN_I32, .i = index }, name, source, rows, checked;
    REQUIRE(
        compiler_fixture_call(&corpus, b, CASES_PACKAGE, "CaseName", &ordinal, 1, COMPILE_BUDGET, &detached, &name)
    );
    if (deep_case(pin_text(name))) {
        ++*deep;
        return true;
    }
    const char *case_name = pin_text(name).bytes;
    REQUIRE(
        compiler_fixture_call(&corpus, b, CASES_PACKAGE, "Source", &ordinal, 1, COMPILE_BUDGET, &detached, &source)
    );
    REQUIRE(collected_rows(b, case_name, source, &rows));
    REQUIRE(
        compiler_fixture_call(&corpus, b, CASES_PACKAGE, "Check", &rows, 1, COMPILE_BUDGET, &detached, &checked)
    );
    if (checked.i != 1) {
        bool rejection = row_int(&rows, 1, "kind") == SCALAR_REJECTION_ROW;
        fprintf(
            stderr,
            "scalar case %s: Check gave %d%s%s\n",
            case_name,
            checked.i,
            rejection ? ", rejected: " : "",
            rejection
                ? pin_text(scalar_field(&((const pin_array *)rows.ptr)->items[1], "text"))
                      .bytes
                : ""
        );
    }
    REQUIRE(checked.i == 1);
    char *ir;
    REQUIRE(emitted_ir(b, &rows, &ir));
    bool rejected = row_int(&rows, 1, "kind") == SCALAR_REJECTION_ROW;
    REQUIRE(rejected == !ir);
    if (rejected) {
        return true;
    }
    pin_value expected;
    REQUIRE(
        compiler_fixture_call(&corpus, b, CASES_PACKAGE, "ExpectedValue", &ordinal, 1, COMPILE_BUDGET, &detached, &expected)
    );
    REQUIRE(expected.type == PIN_I32);
    ++*accepted;
    scalar_call calls[MAX_CASE_CALLS];
    return module_agrees(
        directory,
        b,
        case_name,
        pin_text(source),
        &rows,
        ir,
        expected.i,
        calls
    );
}
#define IDS_10 "Id(Id(Id(Id(Id(Id(Id(Id(Id(Id("
#define CLOSES_10 "))))))))))"
#define NESTED_IDS IDS_10 IDS_10 IDS_10 IDS_10 IDS_10
#define NESTED_CLOSES CLOSES_10 CLOSES_10 CLOSES_10 CLOSES_10 CLOSES_10
/* A scalar module with a constant expression the ordinary compiler folds
 * exactly, a conversion it must call instead, calls that omit defaulted
 * arguments or records, and the value its Main gives. They stay out of the 96-source
 * corpus, which Run.ps1 bounds. Fixed-width rows would wrap each constant:
 * `(2147483647 + 1) / 2` to -2^30, and the square to 1. */
static const struct
{
    const char *name, *source;
    int32_t entry;
} FOLDED_SOURCES[] = {
    { "folded_return",
      "package scalar\nMain :: () -> int { return (2147483647 + 1) / 2; }\n",
      1073741824 },
    { "folded_square", "package scalar\nMain :: () -> int { return (2147483647 * 2147483647) / 2147483647; }\n", INT32_MAX },
    { "folded_operand",
      "package scalar\nMain :: () -> int { return Add(1); }\n"
      "Add :: (x: int) -> int { return x + ((2147483647 + 1) / 2); }\n",
      1073741825 },
    /* In an argument, a guard's condition and its arm. */
    { "folded_guard",
      "package scalar\nMain :: () -> int { return Pick(Id((2147483647 + 1) "
      "/ "
      "2)); }\n"
      "Id :: (x: int) -> int { return x; }\n"
      "Pick :: (x: int) -> int {\n"
      "    if (x > (2147483647 + 1) / 4) { return (2147483647 * "
      "2147483647) / "
      "2147483647; }\n"
      "    return 0;\n"
      "}\n",
      INT32_MAX },
    /* A conversion an authored function shadows is a call to it, with that
     * function's result type, also inside an expression and as a built-in
     * conversion's argument. */
    { "shadowed_conversion",
      "package scalar\nMain :: () -> int { return i8(1); }\n"
      "i8 :: (v: int) -> int { return v + 1; }\n",
      2 },
    { "shadowed_nested_conversion",
      "package scalar\nMain :: () -> int { return int(i8(127)) + i8(100) * "
      "2; }\n"
      "i8 :: (v: int) -> int { return v + 1; }\n",
      330 },
    /* A default of every kind of type, and a call that gives some
     * arguments and omits the rest: 6 + 20 + 10 + (-1 + 255 + 15 + 2 - 9). */
    { "defaults",
      "package scalar\nMain :: () -> int { return Mix() + Mix(10) + "
      "Mix(10, "
      "false) + Small(); }\n"
      "Mix :: (a: int = 3, twice: bool = true) -> int { if (twice) { "
      "return a "
      "* 2; } "
      "return a; }\n"
      "Small :: (b: i8 = -1, u: u8 = 255, w: u64 = "
      "18446744073709551615, f: number = 0.5,\n"
      "          d: f64 = -2.25) -> int {\n"
      "    return int(b) + int(u) + int(w >> 60) + int(f * 4.0) + int(d * "
      "4.0);\n"
      "}\n",
      298 },
    /* Records: literals in any field order, copies that leave their source
     * alone, field writes and compound writes, record parameters and
     * results, nested calls, and a u8 field that wraps:
     * 50 + (206 + 8 - 9 + 1 + 1000) + 5. */
    { "records",
      "package scalar\n"
      "Vec :: struct { x: number; y: number; }\n"
      "Cell :: struct { alive: bool; count: u8; weight: f64; id: i64; "
      "}\n"
      "Main :: () -> int { return Vectors() + Cells() + Walk(); }\n"
      "Add :: (p: Vec, q: Vec) -> Vec { return {x: p.x + q.x, y: p.y + "
      "q.y}; "
      "}\n"
      "Vectors :: () -> int {\n"
      "    a: Vec = {x: 1.5, y: -2.0};\n"
      "    b := a;\n"
      "    b.x = 4.0;\n"
      "    b.y *= 3.0;\n"
      "    s := Add(a, b);\n"
      "    return int(s.x * 10.0) + int(s.y) + int(a.x * 2.0);\n"
      "}\n"
      "Grow :: (c: Cell) -> Cell {\n"
      "    next := c;\n"
      "    next.count += 3;\n"
      "    next.weight = next.weight * 2.0;\n"
      "    next.id -= 1;\n"
      "    next.alive = next.count > 2;\n"
      "    return next;\n"
      "}\n"
      "Cells :: () -> int {\n"
      "    c := Grow(Grow({alive: true, count: 200, weight: 0.25, id: "
      "-7}));\n"
      "    total: int = 0;\n"
      "    if (c.alive) { total += int(c.count) + int(c.weight * 8.0) + "
      "int(c.id); }\n"
      "    w := Grow({id: 0, weight: 1.0, count: 254, alive: true});\n"
      "    total += int(w.count);\n"
      "    if (!w.alive) { total += 1000; }\n"
      "    return total;\n"
      "}\n"
      "Walk :: () -> int {\n"
      "    a: Vec = {y: -2.0, x: 1.5};\n"
      "    for (i in 0..<3) { a = Add(a, {y: 1.0, x: number(i)}); }\n"
      "    return int(a.x) + int(a.y);\n"
      "}\n",
      1261 },
    /* Named results: two to four of mixed types, assigned, returned in
     * order, each value assigned before the next is evaluated, by a bare
     * return or at the end, read by name from a call or a local; and one
     * named result, a number or a record, which is its call's value and
     * also projected by name, returned and read through:
     * 132 + 146 + 10012 + 22 + 108 + 30 + 37649. */
    { "named_results",
      "package scalar\n"
      "Vec :: struct { x: int; y: int; }\n"
      "Main :: () -> int { return Pairs() + Mixed() + Quads() + Orders() + "
      "Clamps() + Single() + Records(); }\n"
      "DivMod :: (a: int, b: int) -> (q: int, r: int) { return (a / b, a % b); "
      "}\n"
      "Pairs :: () -> int { d := DivMod(17, 5); return d.q * 10 + d.r + "
      "DivMod(9, 4).r * 100; }\n"
      "Split :: (v: f64) -> (whole: int, half: bool, rest: f64) {\n"
      "    whole = int(v);\n"
      "    rest = v - f64(whole);\n"
      "    half = rest >= 0.5;\n"
      "    return;\n"
      "}\n"
      "Mixed :: () -> int {\n"
      "    s := Split(7.75);\n"
      "    total := s.whole * 10;\n"
      "    if (s.half) { total += 1; }\n"
      "    return total + int(s.rest * 100.0);\n"
      "}\n"
      "Quad :: (seed: u8) -> (a: u8, b: i64, c: bool, d: number) {\n"
      "    a = seed;\n"
      "    a += 250;\n"
      "    b = i64(seed) * 1000;\n"
      "    c = a < seed;\n"
      "    d = 0.25;\n"
      "}\n"
      "Quads :: () -> int {\n"
      "    q := Quad(10);\n"
      "    n := int(q.a) + int(q.b);\n"
      "    if (q.c) { n += 7; }\n"
      "    return n + int(q.d * 4.0);\n"
      "}\n"
      "Order :: () -> (p: int, q: int) { p = 1; q = 2; return (q, p); }\n"
      "Orders :: () -> int { o := Order(); return o.p * 10 + o.q; }\n"
      "Clamp :: (v: int) -> (low: bool, value: int) {\n"
      "    value = v;\n"
      "    if (v < 0) { low = true; value = 0; return; }\n"
      "    return (false, v * 2);\n"
      "}\n"
      "Clamps :: () -> int {\n"
      "    c := Clamp(-5);\n"
      "    n := Clamp(4).value;\n"
      "    if (c.low) { n += 100; }\n"
      "    return n + c.value;\n"
      "}\n"
      "Twice :: (v: int) -> (doubled: int) { doubled = v * 2; }\n"
      "Single :: () -> int {\n"
      "    t := Twice(4);\n"
      "    w: i64 = Twice(3);\n"
      "    return t + t.doubled + Twice(1).doubled + int(w) + w.doubled;\n"
      "}\n"
      "Make :: (v: int) -> (p: Vec) { p.x = v; p.y = v + 1; return; }\n"
      "Forward :: () -> Vec { return Make(7).p; }\n"
      "Records :: () -> int {\n"
      "    m := Make(3);\n"
      "    q := Make(2).p;\n"
      "    return m.x + m.y * 10 + Make(5).y + Make(5).p.y * 100 + Forward().x "
      "* 1000 + q.y * 10000;\n"
      "}\n",
      48099 },
    /* A record through 50 nested calls. Each call takes one compiler frame,
     * as an int call does, so this stays within the 64 frames stage zero
     * gives the compiler; two frames per call would exceed them. */
    { "nested_records",
      "package scalar\nVec :: struct { x: int; }\n"
      "Main :: () -> int { v: Vec = {x: 7}; return " NESTED_IDS "v" NESTED_CLOSES ".x; }\n"
      "Id :: (p: Vec) -> Vec { return p; }\n",
      7 },
    /* Unions: variants with and without payloads, made, passed, returned,
     * assigned and tested with `is`, switches whose arms bind payloads, with
     * a default arm or covering every variant without one, and a switch in
     * a loop that breaks and continues: 123 + 111 + (250 + 251 + 252 + 253). */
    { "unions",
      "package scalar\n"
      "Shape :: union { Empty | Circle(r: number) | Rect(w: int, h: int) | "
      "Flag(lit: bool, weight: f64) }\n"
      "Step :: union { Stop | Go(n: u8) }\n"
      "Main :: () -> int { return Areas() + Tests() + Steps(); }\n"
      "Area :: (s: Shape) -> int {\n"
      "    switch s {\n"
      "        case .Empty: return 0;\n"
      "        case .Circle(r): return int(r * r * 3.0);\n"
      "        case .Rect(w, h): return w * h;\n"
      "        case .Flag(lit, weight):\n"
      "            if (lit) { return int(weight * 2.0); }\n"
      "            return -1;\n"
      "    }\n"
      "}\n"
      "Grow :: (s: Shape) -> Shape {\n"
      "    switch s {\n"
      "        case .Rect(w, h): return Shape.Rect(w + 1, h * 2);\n"
      "        case .Circle(r): return Shape.Circle(r + 0.5);\n"
      "        case: return s;\n"
      "    }\n"
      "}\n"
      "Areas :: () -> int {\n"
      "    a := Shape.Rect(3, 4);\n"
      "    b: Shape = Shape.Circle(2.0);\n"
      "    c := Grow(Grow(a));\n"
      "    total := Area(a) + Area(b) + Area(c) + Area(Shape.Empty) + "
      "Area(Shape.Flag(true, 10.25));\n"
      "    a = Grow(Shape.Flag(false, 1.0));\n"
      "    return total + Area(a);\n"
      "}\n"
      "Tests :: () -> int {\n"
      "    s := Grow(Shape.Circle(1.0));\n"
      "    n: int = 0;\n"
      "    if (s is .Circle) { n += 1; }\n"
      "    if (s is not .Rect) { n += 10; }\n"
      "    if (Shape.Empty is .Empty && !(s is .Empty)) { n += 100; }\n"
      "    return n;\n"
      "}\n"
      "Steps :: () -> int {\n"
      "    s := Step.Go(250);\n"
      "    total: int = 0;\n"
      "    for (i in 0..<10) {\n"
      "        switch s {\n"
      "            case .Stop: break;\n"
      "            case .Go(n):\n"
      "                total += int(n);\n"
      "                if (n > 252) { s = Step.Stop; continue; }\n"
      "                s = Step.Go(n + 1);\n"
      "        }\n"
      "    }\n"
      "    return total;\n"
      "}\n",
      1240 },
    /* Past the bounds the facts once had: a union of 18 variants, one of
     * 20 payloads all bound in one arm, a record of 20 fields and 17 more
     * records: 210 + 210 + 1000 + 5. */
    { "widest_union",
      "package scalar\n"
      "Wide :: union { Empty | All(a: int, b: int, c: int, d: int, e: int, f: "
      "int, g: int, h: int, i: int, j: int, k: int, l: int, m: int, n: int, o: "
      "int, p: int, q: int, r: int, s: int, t: int) |\n"
      "    V2 | V3 | V4 | V5 | V6 | V7 | V8 | V9 | V10 | V11 | V12 | V13 | V14 "
      "| V15 | V16 | V17 }\n"
      "Big :: struct { a: int; b: int; c: int; d: int; e: int; f: int; g: int; "
      "h: int; i: int; j: int; k: int; l: int; m: int; n: int; o: int; p: int; "
      "q: int; r: int; s: int; t: int; }\n"
      "R1 :: struct { x: int; }\nR2 :: struct { x: int; }\nR3 :: struct { x: "
      "int; }\nR4 :: struct { x: int; }\nR5 :: struct { x: int; }\nR6 :: "
      "struct { x: int; }\nR7 :: struct { x: int; }\nR8 :: struct { x: int; "
      "}\nR9 :: struct { x: int; }\nR10 :: struct { x: int; }\nR11 :: struct { "
      "x: int; }\nR12 :: struct { x: int; }\nR13 :: struct { x: int; }\nR14 :: "
      "struct { x: int; }\nR15 :: struct { x: int; }\nR16 :: struct { x: int; "
      "}\nR17 :: struct { x: int; }\n"
      "Main :: () -> int { r: R17 = {x: 5}; return Sum(Wide.All(1, 2, 3, 4, 5, "
      "6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20)) + Fields() + "
      "Sum(Wide.V17) + r.x; }\n"
      "Sum :: (w: Wide) -> int {\n"
      "    switch w {\n"
      "        case .Empty: return 0;\n"
      "        case .All(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, "
      "s, t):\n"
      "            return (a + b + c + d + e) + (f + g + h + i + j) + (k + l + "
      "m + n + o) + (p + q + r + s + t);\n"
      "        case .V17: return 1000;\n"
      "        case: return -1;\n"
      "    }\n"
      "}\n"
      "Fields :: () -> int {\n"
      "    b: Big = {a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7, h: 8, i: 9, j: "
      "10, k: 11, l: 12, m: 13, n: 14, o: 15, p: 16, q: 17, r: 18, s: 19, t: "
      "20};\n"
      "    return (b.a + b.b + b.c + b.d + b.e) + (b.f + b.g + b.h + b.i + "
      "b.j) + (b.k + b.l + b.m + b.n + b.o) + (b.p + b.q + b.r + b.s + b.t);\n"
      "}\n",
      1425 },
    /* Strings: literals, parameters, results, locals, assignments,
     * defaults and arguments, compared by their bytes with == and !=,
     * including the empty string, a prefix, a last byte that differs,
     * escapes, UTF-8 and an embedded NUL, and in a loop. Each check is one
     * bit: Names() 61, Escapes() 127 and Loops() 3. */
    { "strings",
      "package scalar\n"
      "Main :: () -> int { return Names() + Escapes() * 1000 + Loops() * "
      "1000000; }\n"
      "Name :: (n: int) -> string {\n"
      "    if (n == 0) { return \"zero\"; }\n"
      "    if (n == 1) { return \"one\"; }\n"
      "    return \"many\";\n"
      "}\n"
      "Same :: (a: string, b: string) -> bool { return a == b; }\n"
      "Greet :: (name: string = \"world\") -> string { return name; }\n"
      "Bit :: (b: bool, weight: int) -> int {\n"
      "    if (b) { return weight; }\n"
      "    return 0;\n"
      "}\n"
      "Names :: () -> int {\n"
      "    s := Name(1);\n"
      "    t: string = \"\";\n"
      "    empty := t == \"\";\n"
      "    t = s;\n"
      "    return Bit(s == \"one\", 1) + Bit(s != \"one\", 2) + Bit(empty, "
      "4) "
      "+ Bit(Same(t, s), 8) +\n"
      "        Bit(Same(Greet(), \"world\"), 16) + Bit(Greet(\"x\") == "
      "\"x\", "
      "32);\n"
      "}\n"
      "Escapes :: () -> int {\n"
      "    quote := \"say \\\"hi\\\"\\\\\\n\";\n"
      "    return Bit(quote == \"say \\\"hi\\\"\\\\\\n\", 1) + Bit(quote "
      "!= "
      "\"say \\\"hi\\\"\\\\\\t\", 2) +\n"
      "        Bit(\"é\U0001F408\" == \"é\U0001F408\", 4) + Bit(\"on\" "
      "!= \"one\", 8) + Bit(\"onf\" != \"one\", 16) +\n"
      "        Bit(\"\" != \"a\", 32) + Bit(\"a\\u0000b\" == \"a\\u0000b\" "
      "&& "
      "\"a\\u0000b\" != \"a\", 64);\n"
      "}\n"
      "Loops :: () -> int {\n"
      "    n: int = 0;\n"
      "    for (i in 0..<5) {\n"
      "        if (Name(i) == \"many\") { n += 1; }\n"
      "    }\n"
      "    return n;\n"
      "}\n",
      3127061 },
    /* Package constants are their folded values: alone, in folded
     * expressions, as a shift count and typed number, f64 and u64 values,
     * declared after a constant that names them.
     * Add(40, 2) + 8 + 100 + 1000 + Limit() (-40 + 2). */
    { "constants",
      "package scalar\n"
      "M: int : N / 20;\n"
      "N: int : 40;\n"
      "Half: number : 0.5;\n"
      "Big: f64 : 1e300;\n"
      "Wide: u64 : 18446744073709551615;\n"
      "Shift: u32 : 3;\n"
      "Main :: () -> int {\n"
      "    total := Add(N, M) + (1 << Shift);\n"
      "    if (Half < 1.0 && Big > 1e299) { total = total + 100; }\n"
      "    if (Wide == 18446744073709551615) { total = total + 1000; }\n"
      "    return total + Limit();\n"
      "}\n"
      "Add :: (a: int, b: int) -> int { return a + b; }\n"
      "Limit :: () -> int { return Add(-N, M); }\n",
      1112 },
    /* Not constant: the sum wraps at run time. */
    { "wrapped_operand",
      "package scalar\nMain :: () -> int { return Half(2147483647); }\n"
      "Half :: (x: int) -> int { return (x + 1) / 2; }\n",
      -1073741824 },
};
#undef NESTED_CLOSES
#undef NESTED_IDS
#undef CLOSES_10
#undef IDS_10
enum
{
    FOLDED_SOURCE_COUNT = (int)(sizeof FOLDED_SOURCES / sizeof *FOLDED_SOURCES)
};
/* Text natives through core:strings functions copied as they are:
 * byte counts, bytes inside and outside a string, slices clamped at
 * each end and slices of slices, compared and searched. Each value is
 * mixed into Main's result, which wraps. The source is joined from
 * pieces, each under C99's 4095-byte bound on a string literal. */
static const char *const TEXT_NATIVES[] = {
    "package scalar\n"
    "Main :: () -> int {\n"
    "    t: int = 0;\n"
    "    t = Mix(t, Compare(\"abc\", \"abd\"));\n"
    "    t = Mix(t, Compare(\"abd\", \"abc\"));\n"
    "    t = Mix(t, Compare(\"\", \"\"));\n"
    "    t = Mix(t, Compare(\"ab\", \"abc\"));\n"
    "    t = Mix(t, Bit(HasPrefix(\"prefix\", \"pre\")));\n"
    "    t = Mix(t, Bit(HasPrefix(\"pr\", \"pre\")));\n"
    "    t = Mix(t, Bit(HasSuffix(\"suffix\", \"fix\")));\n"
    "    t = Mix(t, Bit(HasSuffix(\"x\", \"fix\")));\n"
    "    t = Mix(t, Bit(EqualFold(\"Chibi\", \"cHIBI\")));\n"
    "    t = Mix(t, Bit(EqualFold(\"ab\", \"ac\")));\n"
    "    t = Mix(t, Bit(IsIdentifier(\"_a1\")));\n"
    "    t = Mix(t, Bit(IsIdentifier(\"1a\")));\n"
    "    t = Mix(t, Index(\"chicken\", \"ken\"));\n"
    "    t = Mix(t, Index(\"chicken\", \"dmc\"));\n"
    "    t = Mix(t, Index(\"abcabc\", \"c\"));\n"
    "    t = Mix(t, Index(\"abc\", \"\"));\n"
    "    t = Mix(t, LastIndex(\"abcabc\", \"bc\"));\n"
    "    t = Mix(t, LastIndex(\"abc\", \"\"));\n"
    "    t = Mix(t, LastIndex(\"abc\", \"abc\"));\n"
    "    t = Mix(t, Count(\"cheese\", \"e\"));\n"
    "    t = Mix(t, Count(\"five\", \"\"));\n"
    "    t = Mix(t, Count(\"banana\", \"ana\"));\n"
    "    t = Mix(t, IndexByte(\"hello\", 108));\n"
    "    t = Mix(t, LastIndexByte(\"hello\", 108));\n"
    "    t = Mix(t, ByteLength(BaseName(\"/a/b/file.txt\")));\n"
    "    t = Mix(t, Bit(BaseName(\"/a/b/file.txt\") == \"file.txt\"));\n"
    "    t = Mix(t, Bit(TruncateToByte(\"key=value\", 61) == \"key\"));\n"
    "    t = Mix(t, Bit(TruncateToByte(\"plain\", 61) == \"plain\"));\n"
    "    t = Mix(t, Bit(Slice(\"abc\", -5, 2) == \"ab\"));\n"
    "    t = Mix(t, Bit(Slice(\"abc\", 2, 99) == \"c\"));\n"
    "    t = Mix(t, Bit(Slice(\"abc\", 9, 1) == \"\"));\n"
    "    t = Mix(t, Bit(Slice(\"abc\", 1, -1) == \"\"));\n"
    "    t = Mix(t, Bit(Slice(Slice(\"abcdef\", 1, 4), 1, 2) == \"cd\"));\n"
    "    t = Mix(t, ByteAt(\"\xc3\xa9"
    "\", 1));\n"
    "    t = Mix(t, ByteAt(\"a\", 5));\n"
    "    t = Mix(t, ByteAt(\"a\", -1));\n"
    "    t = Mix(t, ByteLength(\"h\xc3\xa9"
    "llo\"));\n"
    "    t = Mix(t, ByteAt(Slice(\"xyz\", 1, 5), 1));\n"
    "    return t;\n"
    "}\n"
    "Mix :: (t: int, value: int) -> int { return t * 31 + value; }\n"
    "Bit :: (b: bool) -> int {\n"
    "    if (b) { return 1; }\n"
    "    return 0;\n"
    "}\n"
    "ByteLength :: (value: string) -> int {\n"
    "    return text_byte_length(value);\n"
    "}\n"
    "ByteAt :: (value: string, index: int) -> int {\n"
    "    return text_byte_at(value, index);\n"
    "}\n"
    "Slice :: (value: string, start: int, count: int) -> string {\n"
    "    return text_slice(value, start, count);\n"
    "}\n",
    "Compare :: (left: string, right: string) -> int {\n"
    "    leftLength := ByteLength(left);\n"
    "    rightLength := ByteLength(right);\n"
    "    index: int = 0;\n"
    "    while (index < leftLength && index < rightLength) {\n"
    "        a := ByteAt(left, index);\n"
    "        b := ByteAt(right, index);\n"
    "        if (a < b) {\n"
    "            return -1;\n"
    "        }\n"
    "        if (a > b) {\n"
    "            return 1;\n"
    "        }\n"
    "        index += 1;\n"
    "    }\n"
    "    if (leftLength < rightLength) {\n"
    "        return -1;\n"
    "    }\n"
    "    if (leftLength > rightLength) {\n"
    "        return 1;\n"
    "    }\n"
    "    return 0;\n"
    "}\n"
    "IsIdentifier :: (value: string) -> bool {\n"
    "    size := ByteLength(value);\n"
    "    if (size == 0) {\n"
    "        return false;\n"
    "    }\n"
    "    index: int = 0;\n"
    "    while (index < size) {\n"
    "        byte := ByteAt(value, index);\n"
    "        letter := byte == 95 || (byte >= 65 && byte <= 90) || (byte >= 97 "
    "&& byte <= 122);\n"
    "        if (!letter && !(index > 0 && byte >= 48 && byte <= 57)) {\n"
    "            return false;\n"
    "        }\n"
    "        index += 1;\n"
    "    }\n"
    "    return true;\n"
    "}\n"
    "BaseName :: (path: string) -> string {\n"
    "    size := ByteLength(path);\n"
    "    start: int = 0;\n"
    "    index: int = 0;\n"
    "    while (index < size) {\n"
    "        if (ByteAt(path, index) == 47) {\n"
    "            start = index + 1;\n"
    "        }\n"
    "        index += 1;\n"
    "    }\n"
    "    return Slice(path, start, size - start);\n"
    "}\n"
    "HasPrefix :: (s: string, prefix: string) -> bool {\n"
    "    size := ByteLength(prefix);\n"
    "    return ByteLength(s) >= size && Slice(s, 0, size) == prefix;\n"
    "}\n"
    "HasSuffix :: (s: string, suffix: string) -> bool {\n"
    "    size := ByteLength(s);\n"
    "    count := ByteLength(suffix);\n"
    "    return size >= count && Slice(s, size - count, count) == suffix;\n"
    "}\n",
    "IndexByte :: (s: string, c: int) -> int {\n"
    "    size := ByteLength(s);\n"
    "    i: int = 0;\n"
    "    while (i < size) {\n"
    "        if (ByteAt(s, i) == c) {\n"
    "            return i;\n"
    "        }\n"
    "        i += 1;\n"
    "    }\n"
    "    return -1;\n"
    "}\n"
    "LastIndexByte :: (s: string, c: int) -> int {\n"
    "    i: int = ByteLength(s) - 1;\n"
    "    while (i >= 0) {\n"
    "        if (ByteAt(s, i) == c) {\n"
    "            return i;\n"
    "        }\n"
    "        i -= 1;\n"
    "    }\n"
    "    return -1;\n"
    "}\n"
    "MatchesAt :: (s: string, substr: string, start: int) -> bool {\n"
    "    i: int = 0;\n"
    "    while (i < ByteLength(substr)) {\n"
    "        if (ByteAt(s, start + i) != ByteAt(substr, i)) {\n"
    "            return false;\n"
    "        }\n"
    "        i += 1;\n"
    "    }\n"
    "    return true;\n"
    "}\n"
    "IndexFrom :: (s: string, substr: string, start: int) -> int {\n"
    "    n := ByteLength(substr);\n"
    "    size := ByteLength(s);\n"
    "    if (n == 0) {\n"
    "        return start;\n"
    "    }\n"
    "    if (n > size - start) {\n"
    "        return -1;\n"
    "    }\n"
    "    if (n == 1) {\n"
    "        c := ByteAt(substr, 0);\n"
    "        i := start;\n"
    "        while (i < size) {\n"
    "            if (ByteAt(s, i) == c) {\n"
    "                return i;\n"
    "            }\n"
    "            i += 1;\n"
    "        }\n"
    "        return -1;\n"
    "    }\n"
    "    hash: i64 = 0;\n"
    "    pow: i64 = 1;\n"
    "    h: i64 = 0;\n"
    "    i: int = 0;\n"
    "    while (i < n) {\n"
    "        hash = (hash * 16777619 + i64(ByteAt(substr, i))) & 0xFFFFFFFF;\n"
    "        pow = (pow * 16777619) & 0xFFFFFFFF;\n"
    "        h = (h * 16777619 + i64(ByteAt(s, start + i))) & 0xFFFFFFFF;\n"
    "        i += 1;\n"
    "    }\n"
    "    i += start;\n"
    "    if (h == hash && MatchesAt(s, substr, start)) {\n"
    "        return start;\n"
    "    }\n"
    "    while (i < size) {\n"
    "        h = (h * 16777619 + i64(ByteAt(s, i)) - pow * i64(ByteAt(s, i - "
    "n))) & 0xFFFFFFFF;\n"
    "        i += 1;\n"
    "        if (h == hash && MatchesAt(s, substr, i - n)) {\n"
    "            return i - n;\n"
    "        }\n"
    "    }\n"
    "    return -1;\n"
    "}\n",
    "Index :: (s: string, substr: string) -> int {\n"
    "    return IndexFrom(s, substr, 0);\n"
    "}\n"
    "LastIndex :: (s: string, substr: string) -> int {\n"
    "    n := ByteLength(substr);\n"
    "    size := ByteLength(s);\n"
    "    if (n == 0) {\n"
    "        return size;\n"
    "    }\n"
    "    if (n == 1) {\n"
    "        return LastIndexByte(s, ByteAt(substr, 0));\n"
    "    }\n"
    "    if (n == size) {\n"
    "        if (s == substr) {\n"
    "            return 0;\n"
    "        }\n"
    "        return -1;\n"
    "    }\n"
    "    if (n > size) {\n"
    "        return -1;\n"
    "    }\n"
    "    hash: i64 = 0;\n"
    "    pow: i64 = 1;\n"
    "    h: i64 = 0;\n"
    "    i: int = 0;\n"
    "    while (i < n) {\n"
    "        hash = (hash * 16777619 + i64(ByteAt(substr, n - 1 - i))) & "
    "0xFFFFFFFF;\n"
    "        pow = (pow * 16777619) & 0xFFFFFFFF;\n"
    "        h = (h * 16777619 + i64(ByteAt(s, size - 1 - i))) & 0xFFFFFFFF;\n"
    "        i += 1;\n"
    "    }\n"
    "    i = size - n;\n"
    "    if (h == hash && Slice(s, i, n) == substr) {\n"
    "        return i;\n"
    "    }\n"
    "    i -= 1;\n"
    "    while (i >= 0) {\n"
    "        h = (h * 16777619 + i64(ByteAt(s, i)) - pow * i64(ByteAt(s, i + "
    "n))) & 0xFFFFFFFF;\n"
    "        if (h == hash && Slice(s, i, n) == substr) {\n"
    "            return i;\n"
    "        }\n"
    "        i -= 1;\n"
    "    }\n"
    "    return -1;\n"
    "}\n",
    "Count :: (s: string, substr: string) -> int {\n"
    "    n := ByteLength(substr);\n"
    "    if (n == 0) {\n"
    "        return ByteLength(s) + 1;\n"
    "    }\n"
    "    total: int = 0;\n"
    "    if (n == 1) {\n"
    "        c := ByteAt(substr, 0);\n"
    "        size := ByteLength(s);\n"
    "        i: int = 0;\n"
    "        while (i < size) {\n"
    "            if (ByteAt(s, i) == c) {\n"
    "                total += 1;\n"
    "            }\n"
    "            i += 1;\n"
    "        }\n"
    "        return total;\n"
    "    }\n"
    "    start: int = 0;\n"
    "    while (true) {\n"
    "        i := IndexFrom(s, substr, start);\n"
    "        if (i < 0) {\n"
    "            break;\n"
    "        }\n"
    "        total += 1;\n"
    "        start = i + n;\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "TruncateToByte :: (s: string, c: int) -> string {\n"
    "    n := IndexByte(s, c);\n"
    "    if (n < 0) {\n"
    "        return s;\n"
    "    }\n"
    "    return Slice(s, 0, n);\n"
    "}\n"
    "EqualFold :: (u: string, v: string) -> bool {\n"
    "    size := ByteLength(u);\n"
    "    if (size != ByteLength(v)) {\n"
    "        return false;\n"
    "    }\n"
    "    i: int = 0;\n"
    "    while (i < size) {\n"
    "        sr := ByteAt(u, i);\n"
    "        tr := ByteAt(v, i);\n"
    "        if (tr < sr) {\n"
    "            swap := sr;\n"
    "            sr = tr;\n"
    "            tr = swap;\n"
    "        }\n"
    "        if (tr != sr && !(sr >= 65 && sr <= 90 && tr == sr + 32)) {\n"
    "            return false;\n"
    "        }\n"
    "        i += 1;\n"
    "    }\n"
    "    return true;\n"
    "}\n",
};
enum
{
    TEXT_NATIVES_ENTRY = -386181014
};
/* Allocating text through core:strings functions copied as they are, and
 * format_text of each scalar type, widths, precision, hex, explicit indexes,
 * escaped braces, an embedded NUL and no arguments. Each string's bytes are
 * mixed into Main's result, which wraps. The core:strings functions they
 * search with are TEXT_NATIVES'. */
static const char *const TEXT_FORMATS[] = {
    "package scalar\n"
    "Main :: () -> int {\n"
    "    t: int = 0;\n"
    "    t = Mix(t, Sum(ToLower(\"Hello, World!\")));\n"
    "    t = Mix(t, Sum(ToLower(\"already\")));\n"
    "    t = Mix(t, Sum(ToUpper(\"chibi 2D/3D \xc3\xa9"
    "\")));\n"
    "    t = Mix(t, Sum(Repeat(\"ab\", 5)));\n"
    "    t = Mix(t, Sum(Repeat(\"x\", 0)));\n"
    "    t = Mix(t, Sum(Replace(\"a-b-c-d\", \"-\", \"+\", 2)));\n"
    "    t = Mix(t, Sum(Replace(\"aaa\", \"\", \"|\", -1)));\n"
    "    t = Mix(t, Sum(ReplaceAll(\"banana\", \"an\", \"AN\")));\n"
    "    n: number = 2.5;\n"
    "    d: f64 = -0.125;\n"
    "    k: i64 = -9000000000;\n"
    "    u: u8 = 255;\n"
    "    flag := k < 0;\n"
    "    t = Mix(t, Sum(format_text(\"{} {} {} {} {}\", n, d, k, u, flag)));\n"
    "    t = Mix(t, Sum(format_text(\"[{:6.2}] [{:4}] {{}} {:x}\", n, \"ab\", "
    "int(255))));\n"
    "    t = Mix(t, Sum(format_text(\"{1}{0}{}\", \"a\", \"b\\u0000c\")));\n"
    "    t = Mix(t, Sum(format_text(\"no arguments\")));\n"
    "    return t;\n"
    "}\n"
    "Mix :: (t: int, value: int) -> int { return t * 31 + value; }\n"
    "Sum :: (s: string) -> int {\n"
    "    total := ByteLength(s);\n"
    "    i: int = 0;\n"
    "    while (i < ByteLength(s)) {\n"
    "        total = total * 7 + ByteAt(s, i);\n"
    "        i += 1;\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "ByteLength :: (value: string) -> int {\n"
    "    return text_byte_length(value);\n"
    "}\n"
    "ByteAt :: (value: string, index: int) -> int {\n"
    "    return text_byte_at(value, index);\n"
    "}\n"
    "Slice :: (value: string, start: int, count: int) -> string {\n"
    "    return text_slice(value, start, count);\n"
    "}\n",
    "ToLower :: (s: string) -> string {\n"
    "    size := ByteLength(s);\n"
    "    result: string = \"\";\n"
    "    start: int = 0;\n"
    "    i: int = 0;\n"
    "    while (i < size) {\n"
    "        c := ByteAt(s, i);\n"
    "        if (c >= 65 && c <= 90) {\n"
    "            letter := Slice(\"abcdefghijklmnopqrstuvwxyz\", c - 65, 1);\n"
    "            result = format_text(\"{}{}{}\", result, Slice(s, start, i - "
    "start), letter);\n"
    "            start = i + 1;\n"
    "        }\n"
    "        i += 1;\n"
    "    }\n"
    "    if (start == 0) {\n"
    "        return s;\n"
    "    }\n"
    "    return format_text(\"{}{}\", result, Slice(s, start, size - start));\n"
    "}\n"
    "ToUpper :: (s: string) -> string {\n"
    "    size := ByteLength(s);\n"
    "    result: string = \"\";\n"
    "    start: int = 0;\n"
    "    i: int = 0;\n"
    "    while (i < size) {\n"
    "        c := ByteAt(s, i);\n"
    "        if (c >= 97 && c <= 122) {\n"
    "            letter := Slice(\"ABCDEFGHIJKLMNOPQRSTUVWXYZ\", c - 97, 1);\n"
    "            result = format_text(\"{}{}{}\", result, Slice(s, start, i - "
    "start), letter);\n"
    "            start = i + 1;\n"
    "        }\n"
    "        i += 1;\n"
    "    }\n"
    "    if (start == 0) {\n"
    "        return s;\n"
    "    }\n"
    "    return format_text(\"{}{}\", result, Slice(s, start, size - start));\n"
    "}\n"
    "Repeat :: (s: string, count: int) -> string {\n"
    "    if (count < 1) {\n"
    "        return \"\";\n"
    "    }\n"
    "    total: i64 = i64(ByteLength(s)) * i64(count);\n"
    "    result: string = s;\n"
    "    while (i64(ByteLength(result)) < total) {\n"
    "        rest: i64 = total - i64(ByteLength(result));\n"
    "        result = format_text(\"{}{}\", result, Slice(result, 0, "
    "int(rest)));\n"
    "    }\n"
    "    return result;\n"
    "}\n"
    "Replace :: (s: string, old: string, new: string, n: int) -> string {\n"
    "    if (old == new || n == 0) {\n"
    "        return s;\n"
    "    }\n"
    "    matches := Count(s, old);\n"
    "    if (matches == 0) {\n"
    "        return s;\n"
    "    }\n"
    "    if (n > 0 && n < matches) {\n"
    "        matches = n;\n"
    "    }\n"
    "    size := ByteLength(s);\n"
    "    width := ByteLength(old);\n"
    "    result: string = \"\";\n"
    "    start: int = 0;\n"
    "    i: int = 0;\n"
    "    while (i < matches) {\n"
    "        j := start;\n"
    "        if (width == 0) {\n"
    "            if (i > 0) {\n"
    "                j += 1;\n"
    "            }\n"
    "        } else {\n"
    "            j += Index(Slice(s, start, size - start), old);\n"
    "        }\n"
    "        result = format_text(\"{}{}{}\", result, Slice(s, start, j - "
    "start), new);\n"
    "        start = j + width;\n"
    "        i += 1;\n"
    "    }\n"
    "    return format_text(\"{}{}\", result, Slice(s, start, size - start));\n"
    "}\n"
    "ReplaceAll :: (s: string, old: string, new: string) -> string {\n"
    "    return Replace(s, old, new, -1);\n"
    "}\n",
};
enum
{
    TEXT_FORMATS_ENTRY = 1366853637
};
/* Why module `name`'s rows on VM b have no IR: the producer's rejection or
 * the consumer's validation error. */
static void report_rejection(int b, const char *name, pin_value *rows)
{
    pin_value error;
    if (row_int(rows, 1, "kind") == SCALAR_REJECTION_ROW) {
        const pin_array *array = rows->ptr;
        error = scalar_field(&array->items[1], "text");
    } else if (!compiler_fixture_call(
                   &consumer,
                   b,
                   "project:scalar_llvm",
                   "Validate",
                   rows,
                   1,
                   COMPILE_BUDGET,
                   &input,
                   &error
               )) {
        error = text("Validate failed");
    }
    fprintf(
        stderr,
        "scalar case %s: rejected: %.*s\n",
        name,
        (int)pin_text(error).length,
        pin_text(error).bytes
    );
}
/* Module `name` is accepted, and its Main gives `entry` on VM b and
 * natively at O0 and O2. */
static bool module_gives(const char *directory, int b, const char *name, const char *source, int32_t entry)
{
    pin_value rows;
    REQUIRE(collected_rows(b, name, text(source), &rows));
    char *ir;
    REQUIRE(emitted_ir(b, &rows, &ir));
    if (!ir) {
        report_rejection(b, name, &rows);
    }
    REQUIRE(ir);
    scalar_call calls[MAX_CASE_CALLS];
    REQUIRE(module_agrees(
        directory,
        b,
        name,
        (pin_string){ .bytes = source, .length = strlen(source) },
        &rows,
        ir,
        entry,
        calls
    ));
    if (calls[0].values[0] != entry) {
        fprintf(
            stderr,
            "scalar case %s: Main gave %" PRId64 ", not %d\n",
            name,
            calls[0].values[0],
            entry
        );
    }
    REQUIRE(calls[0].values[0] == entry);
    arena_free(&detached);
    return true;
}
/* TEXT_FORMATS, with TEXT_NATIVES' functions but its Main, gives its value
 * on both VMs and natively at O0 and O2, each native call formatting in a
 * context whose heap is freed after it. */
static bool text_formats_agree(const char *directory)
{
    size_t length = 0;
    for (size_t i = 0; i < sizeof TEXT_FORMATS / sizeof *TEXT_FORMATS; i++) {
        length += strlen(TEXT_FORMATS[i]);
    }
    for (size_t i = 1; i < sizeof TEXT_NATIVES / sizeof *TEXT_NATIVES; i++) {
        length += strlen(TEXT_NATIVES[i]);
    }
    char *source = xmalloc(length + 1);
    source[0] = 0;
    for (size_t i = 0; i < sizeof TEXT_FORMATS / sizeof *TEXT_FORMATS; i++) {
        strcat(source, TEXT_FORMATS[i]);
    }
    for (size_t i = 1; i < sizeof TEXT_NATIVES / sizeof *TEXT_NATIVES; i++) {
        strcat(source, TEXT_NATIVES[i]);
    }
    bool gives = module_gives(directory, 0, "text_formats", source, TEXT_FORMATS_ENTRY);
    free(source);
    return gives;
}
/* Each folded source and the text natives' source is accepted, and its Main
 * gives its value on both VMs and natively at O0 and O2. */
static bool folded_sources_agree(const char *directory)
{
    for (int i = 0; i < FOLDED_SOURCE_COUNT && !native_cancelled; i++) {
        REQUIRE(module_gives(
            directory,
            i % 2,
            FOLDED_SOURCES[i].name,
            FOLDED_SOURCES[i].source,
            FOLDED_SOURCES[i].entry
        ));
    }
    size_t length = 0;
    for (size_t i = 0; i < sizeof TEXT_NATIVES / sizeof *TEXT_NATIVES; i++) {
        length += strlen(TEXT_NATIVES[i]);
    }
    char *source = xmalloc(length + 1);
    source[0] = 0;
    for (size_t i = 0; i < sizeof TEXT_NATIVES / sizeof *TEXT_NATIVES; i++) {
        strcat(source, TEXT_NATIVES[i]);
    }
    bool gives = native_cancelled ||
                 module_gives(directory, FOLDED_SOURCE_COUNT % 2, "text_natives", source, TEXT_NATIVES_ENTRY);
    free(source);
    return gives && (native_cancelled || text_formats_agree(directory));
}
/* A constant that folds outside its type is still a located error. */
static bool folded_overflow_rejected()
{
    static const char SOURCE[] = "package scalar\nMain :: () -> int { return "
                                 "2147483647 + 1; }\n";
    pin_value rows;
    REQUIRE(collected_rows(0, "folded_overflow", text(SOURCE), &rows));
    const pin_array *array = rows.ptr;
    REQUIRE(row_count(&rows) == 2 && row_int(&rows, 1, "kind") == SCALAR_REJECTION_ROW);
    const char *message = pin_text(scalar_field(&array->items[1], "text")).bytes;
    if (!strstr(message, "outside the range of int")) {
        fprintf(stderr, "scalar case folded_overflow: %s\n", message);
    }
    REQUIRE(strstr(message, "outside the range of int"));
    arena_free(&detached);
    return true;
}
/* The integer types, as the width modules declare them: each one's name, the
 * name its functions take, its scalar_facts type, bits and signedness. */
typedef struct scalar_width
{
    const char *name, *title;
    int type, bits;
    bool is_unsigned;
} scalar_width;
static const scalar_width WIDTHS[] = {
    { "i8", "Int8", SCALAR_INT8, 8, false },
    { "u8", "Byte", SCALAR_BYTE, 8, true },
    { "i16", "Int16", SCALAR_INT16, 16, false },
    { "u16", "Uint16", SCALAR_UINT16, 16, true },
    { "int", "Int", SCALAR_INT, 32, false },
    { "u32", "Uint", SCALAR_UINT, 32, true },
    { "i64", "Int64", SCALAR_INT64, 64, false },
    { "u64", "Uint64", SCALAR_WORD, 64, true },
};
enum
{
    SCALAR_WIDTHS = (int)(sizeof WIDTHS / sizeof *WIDTHS),
    WIDTH_INT = 4,
    WIDTH_INT64 = 6,
    WIDTH_WORD = 7,
    WIDTH_SOURCE_BYTES = 4096
};
/* `bits` as a value of `w`: its low bits, sign-extended when `w` is signed,
 * so a uint64 is its bits. */
static int64_t width_value(const scalar_width *w, uint64_t bits)
{
    if (w->bits < 64) {
        uint64_t mask = (UINT64_C(1) << w->bits) - 1;
        bits &= mask;
        if (!w->is_unsigned && bits >> (w->bits - 1)) {
            bits |= ~mask;
        }
    }
    return (int64_t)bits;
}
static int64_t width_least(const scalar_width *w)
{
    return w->is_unsigned ? 0 : width_value(w, UINT64_C(1) << (w->bits - 1));
}
static int64_t width_greatest(const scalar_width *w)
{
    return width_value(w, w->is_unsigned ? UINT64_MAX : (UINT64_C(1) << (w->bits - 1)) - 1);
}
/* Whether value `a` of `t` is less than value `b` of `u`. */
static bool width_below(const scalar_width *t, int64_t a, const scalar_width *u, int64_t b)
{
    bool a_negative = !t->is_unsigned && a < 0, b_negative = !u->is_unsigned && b < 0;
    if (a_negative != b_negative) {
        return a_negative;
    }
    return a_negative ? a < b : (uint64_t)a < (uint64_t)b;
}
/* The operations of a width module: the function, its expression over `a`
 * and `b` of the module's type or the uint shift count `n`, and its arity. */
enum
{
    WIDTH_ADD,
    WIDTH_SUB,
    WIDTH_MUL,
    WIDTH_DIV,
    WIDTH_MOD,
    WIDTH_AND,
    WIDTH_OR,
    WIDTH_XOR,
    WIDTH_LT,
    WIDTH_LE,
    WIDTH_GT,
    WIDTH_GE,
    WIDTH_EQ,
    WIDTH_NE,
    WIDTH_SHL,
    WIDTH_SHR,
    WIDTH_SHL_FAR,
    WIDTH_SHR_FAR,
    WIDTH_NEG,
    WIDTH_NOT,
    WIDTH_OPERATIONS
};
static const struct
{
    const char *name, *expression;
    int parameters;
} WIDTH_OPERATION[WIDTH_OPERATIONS] = {
    [WIDTH_ADD] = { "Add", "a + b", 2 },
    [WIDTH_SUB] = { "Sub", "a - b", 2 },
    [WIDTH_MUL] = { "Mul", "a * b", 2 },
    [WIDTH_DIV] = { "Div", "a / b", 2 },
    [WIDTH_MOD] = { "Mod", "a % b", 2 },
    [WIDTH_AND] = { "And", "a & b", 2 },
    [WIDTH_OR] = { "Or", "a | b", 2 },
    [WIDTH_XOR] = { "Xor", "a ^ b", 2 },
    [WIDTH_LT] = { "Lt", "a < b", 2 },
    [WIDTH_LE] = { "Le", "a <= b", 2 },
    [WIDTH_GT] = { "Gt", "a > b", 2 },
    [WIDTH_GE] = { "Ge", "a >= b", 2 },
    [WIDTH_EQ] = { "Eq", "a == b", 2 },
    [WIDTH_NE] = { "Ne", "a != b", 2 },
    [WIDTH_SHL] = { "Shl", "a << n", 2 },
    [WIDTH_SHR] = { "Shr", "a >> n", 2 },
    [WIDTH_SHL_FAR] = { "ShlFar", "a << 300", 1 },
    [WIDTH_SHR_FAR] = { "ShrFar", "a >> 300", 1 },
    [WIDTH_NEG] = { "Neg", "-a", 1 },
    [WIDTH_NOT] = { "Not", "~a", 1 },
};
/* The far shifts' constant count, which ShiftStep clamps to the type. */
static const int64_t FAR_COUNT = 300;
/* The language's integer contract for operation `op` on values of `w`:
 * nothing fails, and a shift count is a uint at or past the width. */
static int64_t width_total(const scalar_width *w, int op, int64_t a, int64_t b)
{
    uint64_t x = (uint64_t)a, y = (uint64_t)b;
    bool sign = !w->is_unsigned;
    switch (op) {
        case WIDTH_ADD: return width_value(w, x + y);
        case WIDTH_SUB: return width_value(w, x - y);
        case WIDTH_MUL: return width_value(w, x * y);
        case WIDTH_DIV:
            return !b              ? 0
                 : sign && b == -1 ? width_value(w, 0 - x)
                 : sign            ? a / b
                                   : (int64_t)(x / y);
        case WIDTH_MOD:
            return !b              ? a
                 : sign && b == -1 ? 0
                 : sign            ? a % b
                                   : (int64_t)(x % y);
        case WIDTH_AND: return a & b;
        case WIDTH_OR: return a | b;
        case WIDTH_XOR: return a ^ b;
        case WIDTH_LT: return width_below(w, a, w, b);
        case WIDTH_LE: return !width_below(w, b, w, a);
        case WIDTH_GT: return width_below(w, b, w, a);
        case WIDTH_GE: return !width_below(w, a, w, b);
        case WIDTH_EQ: return a == b;
        case WIDTH_NE: return a != b;
        case WIDTH_SHL_FAR: y = FAR_COUNT; /* fall through */
        case WIDTH_SHL:
            return y >= (uint64_t)w->bits ? 0 : width_value(w, x << y);
        case WIDTH_SHR_FAR: y = FAR_COUNT; /* fall through */
        case WIDTH_SHR:
            if (y >= (uint64_t)w->bits) {
                return sign && a < 0 ? -1 : 0;
            }
            return !sign ? (int64_t)(x >> y) : a < 0 ? ~(~a >> y) : a >> y;
        case WIDTH_NEG: return width_value(w, 0 - x);
        default: return width_value(w, ~x);
    }
}
/* The boundary values a width module's operations take: the least and
 * greatest, those next to them, 0, 1 and -1, or the top bit unsigned. */
static int width_samples(const scalar_width *w, int64_t samples[6])
{
    uint64_t top = UINT64_C(1) << (w->bits - 1);
    if (w->is_unsigned) {
        const int64_t values[6] = { 0,
                                    1,
                                    2,
                                    width_value(w, top),
                                    width_greatest(w) - 1,
                                    width_greatest(w) };
        memcpy(samples, values, sizeof values);
    } else {
        const int64_t values[6] = {
            width_least(w), width_least(w) + 1, -1, 0, 1, width_greatest(w)
        };
        memcpy(samples, values, sizeof values);
    }
    return 6;
}
/* The uint shift counts: in range, the width's edges and past it, and past
 * every narrower type's range, which the count's conversion clamps. */
static int shift_counts(const scalar_width *w, int64_t counts[7])
{
    const int64_t values[7] = { 0,           1,   w->bits - 1, w->bits,
                                w->bits + 1, 255, UINT32_MAX };
    memcpy(counts, values, sizeof values);
    return 7;
}
/* The operations module of `w`: Main, then one function per operation. */
static void width_operations_source(const scalar_width *w, char source[WIDTH_SOURCE_BYTES])
{
    int n = snprintf(source, WIDTH_SOURCE_BYTES, "package scalar\nMain :: () -> int { return int(wrap: Mul(3, 5)); }\n");
    for (int op = 0; op < WIDTH_OPERATIONS; op++) {
        const char *result = op >= WIDTH_LT && op <= WIDTH_NE ? "bool" : w->name;
        char parameters[64];
        if (WIDTH_OPERATION[op].parameters == 1) {
            snprintf(parameters, sizeof parameters, "a: %s", w->name);
        } else if (op == WIDTH_SHL || op == WIDTH_SHR) {
            snprintf(parameters, sizeof parameters, "a: %s, n: u32", w->name);
        } else {
            snprintf(parameters, sizeof parameters, "a: %s, b: %s", w->name, w->name);
        }
        n += snprintf(
            source + n,
            WIDTH_SOURCE_BYTES - (size_t)n,
            "%s :: (%s) -> %s { return %s; }\n",
            WIDTH_OPERATION[op].name,
            parameters,
            result,
            WIDTH_OPERATION[op].expression
        );
    }
    assert(n < WIDTH_SOURCE_BYTES);
}
/* A call of `function` with `count` arguments that must give `expected`. */
static scalar_call width_call(const char *function, int count, int64_t a, int64_t b, int64_t expected)
{
    return (scalar_call){ .probe = { "widths", function, count, { a, b }, expected } };
}
/* The entry, then each operation of `w` on every pair of its samples, or
 * each sample with every shift count, or each sample alone. */
static int width_operation_calls(const scalar_width *w, scalar_call calls[MAX_WIDTH_CALLS])
{
    int64_t samples[6], counts[7];
    int sample_count = width_samples(w, samples),
        count_count = shift_counts(w, counts), count = 0;
    calls[count++] = width_call("Main", 0, 0, 0, 15);
    for (int op = 0; op < WIDTH_OPERATIONS; op++) {
        bool shift = op == WIDTH_SHL || op == WIDTH_SHR,
             unary = WIDTH_OPERATION[op].parameters == 1;
        const int64_t *rights = shift ? counts : samples;
        int right_count = unary ? 1 : shift ? count_count : sample_count;
        for (int i = 0; i < sample_count; i++) {
            for (int j = 0; j < right_count; j++) {
                int64_t a = samples[i], b = unary ? 0 : rights[j];
                assert(count < MAX_WIDTH_CALLS);
                calls[count++] = width_call(
                    WIDTH_OPERATION[op].name,
                    WIDTH_OPERATION[op].parameters,
                    a,
                    b,
                    width_total(w, op, a, b)
                );
            }
        }
    }
    return count;
}
/* The values conversions take: each type's edges and those next to them, as
 * an int64 sample or, past it, a uint64 one. */
static const int64_t CONVERTED[] = { 0,
                                     1,
                                     -1,
                                     42,
                                     127,
                                     128,
                                     -128,
                                     -129,
                                     255,
                                     256,
                                     298,
                                     32767,
                                     32768,
                                     -32768,
                                     -32769,
                                     65535,
                                     65536,
                                     INT32_MAX,
                                     INT32_MAX + INT64_C(1),
                                     INT32_MIN,
                                     INT32_MIN - INT64_C(1),
                                     UINT32_MAX,
                                     UINT32_MAX + INT64_C(1),
                                     INT64_MAX,
                                     INT64_MIN };
static const int64_t CONVERTED_WORDS[] = { INT64_MIN, -1 };
/* `T(value)` and `T(wrap: value)` for a value of `from` and T `to`. */
static int64_t clamped(const scalar_width *from, int64_t value, const scalar_width *to)
{
    if (width_below(from, value, to, width_least(to))) {
        return width_least(to);
    }
    if (width_below(to, width_greatest(to), from, value)) {
        return width_greatest(to);
    }
    return width_value(to, (uint64_t)value);
}
/* The conversions module of `to`: Main, then ClampT and WrapT for each type
 * T, which convert their T to `to`. */
static void width_conversions_source(const scalar_width *to, char source[WIDTH_SOURCE_BYTES])
{
    int n = snprintf(source, WIDTH_SOURCE_BYTES, "package scalar\nMain :: () -> int { return int(wrap: WrapInt(298)); }\n");
    for (int i = 0; i < SCALAR_WIDTHS; i++) {
        const scalar_width *from = &WIDTHS[i];
        n += snprintf(
            source + n,
            WIDTH_SOURCE_BYTES - (size_t)n,
            "Clamp%s :: (a: %s) -> %s { return %s(a); }\nWrap%s :: (a: %s) "
            "-> "
            "%s { return %s(wrap: a); }\n",
            from->title,
            from->name,
            to->name,
            to->name,
            from->title,
            from->name,
            to->name,
            to->name
        );
    }
    assert(n < WIDTH_SOURCE_BYTES);
}
/* The entry, then both conversions of each type's every sample it holds. */
static int width_conversion_calls(const scalar_width *to, scalar_call *calls, char names[][32])
{
    int count = 0;
    calls[count++] = width_call(
        "Main",
        0,
        0,
        0,
        width_value(&WIDTHS[WIDTH_INT], (uint64_t)width_value(to, 298))
    );
    for (int i = 0; i < SCALAR_WIDTHS; i++) {
        const scalar_width *from = &WIDTHS[i];
        for (int wrap = 0; wrap < 2; wrap++) {
            int name = 2 * i + wrap;
            snprintf(names[name], sizeof names[name], "%s%s", wrap ? "Wrap" : "Clamp", from->title);
            for (int k = 0; k < (int)(sizeof CONVERTED / sizeof *CONVERTED) + 2; k++) {
                bool word = k >= (int)(sizeof CONVERTED / sizeof *CONVERTED);
                const scalar_width *sampled = word ? &WIDTHS[WIDTH_WORD]
                                                   : &WIDTHS[WIDTH_INT64];
                int64_t value = word
                                  ? CONVERTED_WORDS[k - (int)(sizeof CONVERTED / sizeof *CONVERTED)]
                                  : CONVERTED[k];
                if (width_below(sampled, value, from, width_least(from)) ||
                    width_below(from, width_greatest(from), sampled, value)) {
                    continue;
                }
                int64_t expected = wrap ? width_value(to, (uint64_t)value)
                                        : clamped(from, value, to);
                assert(count < MAX_WIDTH_CALLS);
                calls[count++] = width_call(names[name], 1, value, 0, expected);
            }
        }
    }
    return count;
}
/* `source`, a width module, is accepted on VM b, and each of its `calls`
 * gives its oracle's value on both VMs and natively at O0 and O2. */
static bool width_module_agrees(
    const char *directory,
    int b,
    const char *name,
    const char *source,
    scalar_call *calls,
    int count
)
{
    pin_value rows;
    REQUIRE(collected_rows(b, name, text(source), &rows));
    char *ir;
    REQUIRE(emitted_ir(b, &rows, &ir));
    if (!ir) {
        const pin_array *array = rows.ptr;
        fprintf(
            stderr,
            "scalar case %s: %s at %d:%d\n",
            name,
            pin_text(scalar_field(&array->items[1], "text")).bytes,
            scalar_field(&array->items[1], "line").i,
            scalar_field(&array->items[1], "column").i
        );
    }
    REQUIRE(ir);
    snprintf(calls[0].json, sizeof calls[0].json, "[]");
    snprintf(calls[0].symbol, sizeof calls[0].symbol, "chibi_scalar");
    for (int i = 1; i < count; i++) {
        probe_json(&rows, &calls[i].probe, calls[i].json, sizeof calls[i].json);
        snprintf(
            calls[i].symbol,
            sizeof calls[i].symbol,
            "probe_%s",
            calls[i].probe.function
        );
    }
    bool agree = runs_everywhere(
        directory,
        b,
        name,
        (pin_string){ .bytes = source, .length = strlen(source) },
        &rows,
        ir,
        calls,
        count
    );
    free(ir);
    arena_free(&detached);
    REQUIRE(agree);
    return true;
}
/* Every integer type's operations and its conversions from every type, at
 * their boundaries, run natively as the integer contract and both VMs give
 * them. */
static bool widths_agree(const char *directory)
{
    scalar_call *calls = xmalloc(sizeof *calls * MAX_WIDTH_CALLS);
    char source[WIDTH_SOURCE_BYTES], name[64], names[2 * SCALAR_WIDTHS][32];
    bool agreed = true;
    for (int i = 0; agreed && !native_cancelled && i < SCALAR_WIDTHS; i++) {
        snprintf(name, sizeof name, "%s_operations", WIDTHS[i].name);
        width_operations_source(&WIDTHS[i], source);
        int count = width_operation_calls(&WIDTHS[i], calls);
        agreed = width_module_agrees(directory, i % 2, name, source, calls, count);
        snprintf(name, sizeof name, "%s_conversions", WIDTHS[i].name);
        width_conversions_source(&WIDTHS[i], source);
        count = width_conversion_calls(&WIDTHS[i], calls, names);
        agreed = agreed &&
                 width_module_agrees(directory, (i + 1) % 2, name, source, calls, count);
    }
    free(calls);
    REQUIRE(agreed);
    return true;
}

/* The operations float and double take, of WIDTH_OPERATION. */
static const int FLOATING_OPERATIONS[] = { WIDTH_ADD, WIDTH_SUB, WIDTH_MUL,
                                           WIDTH_DIV, WIDTH_LT,  WIDTH_LE,
                                           WIDTH_GT,  WIDTH_GE,  WIDTH_EQ,
                                           WIDTH_NE };
enum
{
    FLOATING_OPERATION_COUNT = (int)(sizeof FLOATING_OPERATIONS / sizeof *FLOATING_OPERATIONS),
    FLOATING_SAMPLES = 9
};
static const char *floating_name(int type)
{
    return type == SCALAR_FLOAT ? "number" : "f64";
}
/* The values a floating module's operations take: both zeros, a finite value
 * of each sign, one that rounds, the extremes, both infinities and NaN. */
static void floating_samples(int type, double samples[FLOATING_SAMPLES])
{
    bool narrow = type == SCALAR_FLOAT;
    const double values[FLOATING_SAMPLES] = { 0.0,
                                              -0.0,
                                              1.0,
                                              narrow ? (double)-0.1f : -0.1,
                                              narrow ? FLT_MAX : DBL_MAX,
                                              narrow ? FLT_TRUE_MIN : DBL_TRUE_MIN,
                                              INFINITY,
                                              -INFINITY,
                                              NAN };
    memcpy(samples, values, sizeof values);
}
/* IEEE operation `op` on `a` and `b` at the width of `type`, rounding to
 * nearest. Every ordered comparison is false for NaN, and != true. */
static int64_t floating_total(int type, int op, double a, double b)
{
    if (type == SCALAR_FLOAT) {
        float x = (float)a, y = (float)b;
        switch (op) {
            case WIDTH_ADD: return floating_bits(type, x + y);
            case WIDTH_SUB: return floating_bits(type, x - y);
            case WIDTH_MUL: return floating_bits(type, x * y);
            case WIDTH_DIV: return floating_bits(type, x / y);
            case WIDTH_LT: return x < y;
            case WIDTH_LE: return x <= y;
            case WIDTH_GT: return x > y;
            case WIDTH_GE: return x >= y;
            case WIDTH_EQ: return x == y;
            default: return x != y;
        }
    }
    switch (op) {
        case WIDTH_ADD: return floating_bits(type, a + b);
        case WIDTH_SUB: return floating_bits(type, a - b);
        case WIDTH_MUL: return floating_bits(type, a * b);
        case WIDTH_DIV: return floating_bits(type, a / b);
        case WIDTH_LT: return a < b;
        case WIDTH_LE: return a <= b;
        case WIDTH_GT: return a > b;
        case WIDTH_GE: return a >= b;
        case WIDTH_EQ: return a == b;
        default: return a != b;
    }
}
/* The ints a floating module adds to its type: an int becomes a float
 * through double, so 2^24 + 1 rounds to even. */
static const int64_t MIXED_INTS[] = { 0, -1, 16777217, INT32_MAX, INT32_MIN };
/* The operations module of floating `type`: Main, each operation, then the
 * typing rules around it: literals, an int operand, and for float its
 * widening to double. */
static void floating_operations_source(int type, char source[WIDTH_SOURCE_BYTES])
{
    const char *name = floating_name(type);
    int n = snprintf(source, WIDTH_SOURCE_BYTES, "package scalar\nMain :: () -> int { return int(Mul(2.5, 3.0)); }\n");
    for (int i = 0; i < FLOATING_OPERATION_COUNT; i++) {
        int op = FLOATING_OPERATIONS[i];
        n += snprintf(
            source + n,
            WIDTH_SOURCE_BYTES - (size_t)n,
            "%s :: (a: %s, b: %s) -> %s { return %s; }\n",
            WIDTH_OPERATION[op].name,
            name,
            name,
            op >= WIDTH_LT ? "bool" : name,
            WIDTH_OPERATION[op].expression
        );
    }
    n += snprintf(
        source + n,
        WIDTH_SOURCE_BYTES - (size_t)n,
        "Mixed :: (a: %s, n: int) -> %s { return a + n; }\nSigned :: () -> "
        "%s "
        "{ return -1.5; }\nPlus :: () -> %s { return +1.5; }\nRounded :: "
        "() -> "
        "%s { return %s; }\nHalf :: (a: %s) -> bool { return a != 0.5; }\n",
        name,
        name,
        name,
        name,
        name,
        type == SCALAR_FLOAT ? "1.0000000596046447753906250000001" : "0.1",
        name
    );
    if (type == SCALAR_FLOAT) {
        n += snprintf(
            source + n,
            WIDTH_SOURCE_BYTES - (size_t)n,
            "Widen :: (a: number) -> f64 { return a; }\nAddDouble :: (a: "
            "number, b: f64) -> f64 { return a + b; }\n"
        );
    }
    assert(n < WIDTH_SOURCE_BYTES);
}
static scalar_call floating_call(const char *function, int count, int64_t a, int64_t b, int64_t expected)
{
    return (scalar_call){ .probe = { "floats", function, count, { a, b }, expected } };
}
/* The entry, each operation on every pair of samples, and the typing rules'
 * calls. A float literal is the binary32 nearest its binary64, so the
 * literal just past halfway between 1 and the next float rounds to 1. */
static int floating_operation_calls(int type, scalar_call calls[MAX_WIDTH_CALLS])
{
    double samples[FLOATING_SAMPLES];
    floating_samples(type, samples);
    int count = 0;
    assert(count < MAX_WIDTH_CALLS);
    calls[count++] = floating_call("Main", 0, 0, 0, 7);
    for (int k = 0; k < FLOATING_OPERATION_COUNT; k++) {
        int op = FLOATING_OPERATIONS[k];
        for (int i = 0; i < FLOATING_SAMPLES; i++) {
            for (int j = 0; j < FLOATING_SAMPLES; j++) {
                assert(count < MAX_WIDTH_CALLS);
                calls[count++] = floating_call(
                    WIDTH_OPERATION[op].name,
                    2,
                    floating_bits(type, samples[i]),
                    floating_bits(type, samples[j]),
                    floating_total(type, op, samples[i], samples[j])
                );
            }
        }
    }
    /* Each function's calls are consecutive, so each gets one adapter. */
    for (int i = 0; i < FLOATING_SAMPLES; i++) {
        for (size_t j = 0; j < sizeof MIXED_INTS / sizeof *MIXED_INTS; j++) {
            double n = (double)MIXED_INTS[j];
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = floating_call(
                "Mixed",
                2,
                floating_bits(type, samples[i]),
                MIXED_INTS[j],
                floating_total(type, WIDTH_ADD, samples[i], type == SCALAR_FLOAT ? (float)n : n)
            );
        }
    }
    for (int i = 0; i < FLOATING_SAMPLES; i++) {
        assert(count < MAX_WIDTH_CALLS);
        calls[count++] = floating_call("Half", 1, floating_bits(type, samples[i]), 0, samples[i] != 0.5);
    }
    for (int i = 0; type == SCALAR_FLOAT && i < FLOATING_SAMPLES; i++) {
        int64_t a = floating_bits(type, samples[i]);
        assert(count < MAX_WIDTH_CALLS);
        calls[count++] = floating_call("Widen", 1, a, 0, floating_bits(SCALAR_DOUBLE, samples[i]));
    }
    for (int i = 0; type == SCALAR_FLOAT && i < FLOATING_SAMPLES; i++) {
        assert(count < MAX_WIDTH_CALLS);
        calls[count++] = floating_call(
            "AddDouble",
            2,
            floating_bits(type, samples[i]),
            floating_bits(SCALAR_DOUBLE, 0.1),
            floating_bits(SCALAR_DOUBLE, samples[i] + 0.1)
        );
    }
    assert(count < MAX_WIDTH_CALLS);
    calls[count++] = floating_call("Signed", 0, 0, 0, floating_bits(type, -1.5));
    assert(count < MAX_WIDTH_CALLS);
    calls[count++] = floating_call("Plus", 0, 0, 0, floating_bits(type, 1.5));
    assert(count < MAX_WIDTH_CALLS);
    calls[count++] = floating_call("Rounded", 0, 0, 0, floating_bits(type, type == SCALAR_FLOAT ? 1.0 : 0.1));
    return count;
}
/* The values floats convert to integers from: each integer type's edges,
 * with fractions on either side, past every type, the infinities, NaN and
 * the least subnormals. */
static const double TRUNCATED[] = { 0.0,
                                    -0.0,
                                    0.9,
                                    -0.9,
                                    1.5,
                                    -1.5,
                                    127.5,
                                    128.0,
                                    -128.5,
                                    -129.0,
                                    255.5,
                                    256.0,
                                    32767.5,
                                    32768.0,
                                    -32768.5,
                                    -32769.0,
                                    65535.5,
                                    65536.0,
                                    2147483647.5,
                                    2147483648.0,
                                    -2147483648.5,
                                    -2147483649.0,
                                    4294967295.5,
                                    4294967296.0,
                                    9223372036854774784.0,
                                    9223372036854775808.0,
                                    -9223372036854775808.0,
                                    -9223372036854777856.0,
                                    18446744073709549568.0,
                                    18446744073709551616.0,
                                    1e30,
                                    -1e30,
                                    INFINITY,
                                    -INFINITY,
                                    NAN,
                                    DBL_TRUE_MIN,
                                    -DBL_TRUE_MIN };
/* `T(value)` for a float or double `value` and integer type `to`: truncated
 * toward zero, saturated at the type's bounds, and 0 for NaN. */
static int64_t truncated(const scalar_width *to, double value)
{
    if (isnan(value)) {
        return 0;
    }
    double whole = trunc(value), limit = ldexp(1, to->bits - !to->is_unsigned);
    if (whole >= limit) {
        return width_greatest(to);
    }
    if (whole < (to->is_unsigned ? 0 : -limit)) {
        return width_least(to);
    }
    return to->is_unsigned ? (int64_t)(uint64_t)whole : (int64_t)whole;
}
/* The ints converted to float through double rounding: below 2^53 and 2^24
 * they are exact, and past them each rounds to nearest, with 2^60 + 2^36 +
 * 1 rounding twice. */
static const int64_t FLOATED[] = { INT64_C(16777217),
                                   INT64_C(9007199254740993),
                                   INT64_C(0x1000001000000001) };
/* The integer `value` of `from` as floating `type`: an int becomes a
 * double, then a float. */
static int64_t floated(int type, const scalar_width *from, int64_t value)
{
    double wide = from->is_unsigned ? (double)(uint64_t)value : (double)value;
    return floating_bits(type, type == SCALAR_FLOAT ? (float)wide : wide);
}
/* The conversions module of floating `type`: Main, To<T> and From<T>
 * between it and each integer type T, and Narrow from double to float or
 * Widen from float to double. */
static void floating_conversions_source(int type, char source[WIDTH_SOURCE_BYTES])
{
    const char *name = floating_name(type);
    int n = snprintf(source, WIDTH_SOURCE_BYTES, "package scalar\nMain :: () -> int { return ToInt(-7.5); }\n");
    for (int i = 0; i < SCALAR_WIDTHS; i++) {
        const scalar_width *w = &WIDTHS[i];
        n += snprintf(
            source + n,
            WIDTH_SOURCE_BYTES - (size_t)n,
            "To%s :: (a: %s) -> %s { return %s(a); }\nFrom%s :: (a: %s) -> "
            "%s "
            "{ "
            "return %s(a); }\n",
            w->title,
            name,
            w->name,
            w->name,
            w->title,
            w->name,
            name,
            name
        );
    }
    n += snprintf(
        source + n,
        WIDTH_SOURCE_BYTES - (size_t)n,
        type == SCALAR_FLOAT
            ? "Narrow :: (a: f64) -> number { return number(a); }\n"
            : "Widen :: (a: number) -> f64 { return f64(a); }\n"
    );
    assert(n < WIDTH_SOURCE_BYTES);
}
/* The entry, each To<T> of every sample as `type` holds it, each From<T> of
 * every integer sample T holds, and Narrow or Widen of every sample. */
static int floating_conversion_calls(int type, scalar_call *calls, char names[][32])
{
    int count = 0, samples = (int)(sizeof TRUNCATED / sizeof *TRUNCATED);
    assert(count < MAX_WIDTH_CALLS);
    calls[count++] = floating_call("Main", 0, 0, 0, -7);
    for (int i = 0; i < SCALAR_WIDTHS; i++) {
        const scalar_width *w = &WIDTHS[i];
        snprintf(names[2 * i], sizeof names[2 * i], "To%s", w->title);
        snprintf(names[2 * i + 1], sizeof names[2 * i + 1], "From%s", w->title);
        for (int k = 0; k < samples; k++) {
            int64_t bits = floating_bits(type, TRUNCATED[k]);
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = floating_call(
                names[2 * i],
                1,
                bits,
                0,
                truncated(w, floating_value(type, bits))
            );
        }
        int converted = (int)(sizeof CONVERTED / sizeof *CONVERTED),
            words = (int)(sizeof CONVERTED_WORDS / sizeof *CONVERTED_WORDS),
            floated_count = (int)(sizeof FLOATED / sizeof *FLOATED);
        for (int k = 0; k < converted + words + floated_count; k++) {
            bool word = k >= converted && k < converted + words;
            const scalar_width *sampled = word ? &WIDTHS[WIDTH_WORD]
                                               : &WIDTHS[WIDTH_INT64];
            int64_t value = k < converted ? CONVERTED[k]
                          : word          ? CONVERTED_WORDS[k - converted]
                                          : FLOATED[k - converted - words];
            if (width_below(sampled, value, w, width_least(w)) ||
                width_below(w, width_greatest(w), sampled, value)) {
                continue;
            }
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = floating_call(names[2 * i + 1], 1, value, 0, floated(type, w, value));
        }
    }
    int other = type == SCALAR_FLOAT ? SCALAR_DOUBLE : SCALAR_FLOAT;
    for (int k = 0; k < samples; k++) {
        double value = floating_value(other, floating_bits(other, TRUNCATED[k]));
        assert(count < MAX_WIDTH_CALLS);
        calls[count++] = floating_call(
            type == SCALAR_FLOAT ? "Narrow" : "Widen",
            1,
            floating_bits(other, value),
            0,
            floating_bits(type, value)
        );
    }
    return count;
}
/* Float and double operations, conversions to and from every integer type
 * and between the two, at their boundaries and IEEE's special values, run
 * natively as C's IEEE arithmetic and both VMs give them. */
static bool floats_agree(const char *directory)
{
    static const int TYPES[] = { SCALAR_FLOAT, SCALAR_DOUBLE };
    scalar_call *calls = xmalloc(sizeof *calls * MAX_WIDTH_CALLS);
    char source[WIDTH_SOURCE_BYTES], name[64], names[2 * SCALAR_WIDTHS][32];
    bool agreed = true;
    for (int i = 0; agreed && !native_cancelled && i < 2; i++) {
        snprintf(name, sizeof name, "%s_operations", floating_name(TYPES[i]));
        floating_operations_source(TYPES[i], source);
        int count = floating_operation_calls(TYPES[i], calls);
        agreed = width_module_agrees(directory, i, name, source, calls, count);
        snprintf(name, sizeof name, "%s_conversions", floating_name(TYPES[i]));
        floating_conversions_source(TYPES[i], source);
        count = floating_conversion_calls(TYPES[i], calls, names);
        agreed = agreed &&
                 width_module_agrees(directory, 1 - i, name, source, calls, count);
    }
    free(calls);
    REQUIRE(agreed);
    return true;
}

/* Control flow over every statement form: else-if chains that end or fall
 * through, nested consequences with locals of their own, compound
 * assignments to parameters and to a byte local, every logical form and
 * bool equality. */
static const char CONTROL_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return Flow(30, 5) + Grade(85); }\n"
         "Sign :: (x: int) -> int {\n"
         "    if (x < 0) {\n"
         "        return -1;\n"
         "    } else if (x == 0) {\n"
         "        return 0;\n"
         "    } else {\n"
         "        return 1;\n"
         "    }\n"
         "}\n"
         "Grade :: (x: int) -> int {\n"
         "    if (x >= 90) {\n"
         "        return 4;\n"
         "    } else if (x >= 80) {\n"
         "        return 3;\n"
         "    } else if (x >= 70) {\n"
         "        return 2;\n"
         "    } else if (x >= 60) {\n"
         "        return 1;\n"
         "    }\n"
         "    return 0;\n"
         "}\n"
         "Parity :: (x: int) -> int {\n"
         "    kind := 0;\n"
         "    if (x > 0) {\n"
         "        if (x % 2 == 0) {\n"
         "            kind = 2;\n"
         "        } else {\n"
         "            kind = 1;\n"
         "        }\n"
         "    } else {\n"
         "        offset := 3;\n"
         "        kind = kind + offset;\n"
         "    }\n"
         "    return kind;\n"
         "}\n"
         "Flow :: (a: int, b: int) -> int {\n"
         "    r := 0;\n"
         "    if (a > b) {\n"
         "        r = a - b;\n"
         "    }\n"
         "    if (a == b) {\n"
         "        return 100;\n"
         "    }\n"
         "    if (r > 10 && a > 0) {\n"
         "        r = 10;\n"
         "    } else if (!(r > 0)) {\n"
         "        r = -1;\n"
         "    }\n"
         "    return r;\n"
         "}\n"
         "Steps :: (a: int, b: int) -> int {\n"
         "    a += b;\n"
         "    a *= 3;\n"
         "    a -= 1;\n"
         "    a = a << 2;\n"
         "    a = a >> 1;\n"
         "    a /= b;\n"
         "    a %= 7;\n"
         "    a ^= b;\n"
         "    a |= 1;\n"
         "    a &= 255;\n"
         "    return a;\n"
         "}\n"
         "Low :: (a: u8, b: u8) -> u8 {\n"
         "    x := a;\n"
         "    x += b;\n"
         "    x *= 3;\n"
         "    x = x >> 1;\n"
         "    return x;\n"
         "}\n"
         "Far :: (a: u8, n: u32) -> u8 {\n"
         "    x := a;\n"
         "    x = x << n;\n"
         "    y := a;\n"
         "    y = y >> 300;\n"
         "    return x + y;\n"
         "}\n"
         "Shadow :: (x: int) -> int {\n"
         "    y := x + 1;\n"
         "    if (x > 0) {\n"
         "        y := x * 2;\n"
         "        return y;\n"
         "    }\n"
         "    return y;\n"
         "}\n"
         "Choose :: (c: bool, a: int, b: int) -> int {\n"
         "    picked: int = b;\n"
         "    if (c && a > b || !c && a < b) {\n"
         "        picked = a;\n"
         "    }\n"
         "    return picked;\n"
         "}\n"
         "Both :: (a: bool, b: bool) -> bool { return a && b; }\n"
         "Either :: (a: bool, b: bool) -> bool { return a || b; }\n"
         "Not :: (a: bool) -> bool { return !a; }\n"
         "Same :: (a: bool, b: bool) -> bool { return a == b; }\n"
         "Differ :: (a: bool, b: bool) -> bool { return a != b; }\n"
         "Mixed :: (a: bool, b: bool, c: bool) -> bool { return a && !b || c; "
         "}\n"
         "Nested :: (a: bool, b: bool, c: bool) -> bool { return a || (b && "
         "!c) || !(a || c); }\n";
static scalar_call control_call(const char *function, int count, int64_t a, int64_t b, int64_t c, int64_t expected)
{
    return (scalar_call){ .probe = { "control", function, count, { a, b, c }, expected } };
}
/* What CONTROL_SOURCE's integer functions give, as C computes them with the
 * integer contract: nothing fails, and a byte keeps its low bits. */
static int64_t control_int(const char *function, int64_t a, int64_t b, int64_t c)
{
    int32_t x = (int32_t)a, y = (int32_t)b;
    if (!strcmp(function, "Sign")) {
        return x < 0 ? -1 : x == 0 ? 0 : 1;
    }
    if (!strcmp(function, "Grade")) {
        return x >= 90 ? 4 : x >= 80 ? 3 : x >= 70 ? 2 : x >= 60 ? 1 : 0;
    }
    if (!strcmp(function, "Parity")) {
        return x > 0 ? (total(11, x, 2) == 0 ? 2 : 1) : 3;
    }
    if (!strcmp(function, "Flow")) {
        int32_t r = x > y ? total(8, x, y) : 0;
        return x == y ? 100 : r > 10 && x > 0 ? 10 : !(r > 0) ? -1 : r;
    }
    if (!strcmp(function, "Steps")) {
        x = total(7, x, y);
        x = total(9, x, 3);
        x = total(8, x, 1);
        x = total(17, x, 2);
        x = total(18, x, 1);
        x = total(10, x, y);
        x = total(11, x, 7);
        return ((x ^ y) | 1) & 255;
    }
    if (!strcmp(function, "Low")) {
        uint8_t v = (uint8_t)((uint8_t)(a + b) * 3);
        return v >> 1;
    }
    if (!strcmp(function, "Far")) {
        /* The count clamps to the byte, and 300 to its largest, 255. */
        int64_t count = b > 255 ? 255 : b;
        return count >= 8 ? 0 : (uint8_t)(a << count);
    }
    if (!strcmp(function, "Shadow")) {
        return x > 0 ? total(9, x, 2) : total(7, x, 1);
    }
    assert(!strcmp(function, "Choose"));
    return (a && y > (int32_t)c) || (!a && y < (int32_t)c) ? y : (int32_t)c;
}
static int64_t control_bool(const char *function, bool a, bool b, bool c)
{
    if (!strcmp(function, "Both")) {
        return a && b;
    }
    if (!strcmp(function, "Either")) {
        return a || b;
    }
    if (!strcmp(function, "Not")) {
        return !a;
    }
    if (!strcmp(function, "Same")) {
        return a == b;
    }
    if (!strcmp(function, "Differ")) {
        return a != b;
    }
    if (!strcmp(function, "Mixed")) {
        return (a && !b) || c;
    }
    assert(!strcmp(function, "Nested"));
    return a || (b && !c) || !(a || c);
}
/* The entry, then each integer function on its samples, Far with each byte
 * and count, Choose with each
 * condition and ordered pair, and each bool function with every
 * combination of its arguments. */
static int control_calls(scalar_call calls[MAX_WIDTH_CALLS])
{
    static const char *const INTS[] = { "Sign", "Grade", "Parity", "Shadow" };
    static const char *const PAIRS_OF[] = { "Flow", "Steps" };
    static const char *const BOOLS[] = { "Both",   "Either", "Not",   "Same",
                                         "Differ", "Mixed",  "Nested" };
    static const int BOOL_ARITY[] = { 2, 2, 1, 2, 2, 3, 3 };
    static const int64_t SAMPLES[] = { INT32_MIN, -7, -1, 0,   1,
                                       2,         59, 60, 69,  70,
                                       80,        89, 90, 100, INT32_MAX };
    static const int64_t PAIRED[] = { INT32_MIN, -11, -1, 0,        1,
                                      5,         16,  30, INT32_MAX };
    static const int64_t BYTES[] = { 0, 1, 85, 127, 128, 200, 255 };
    static const int64_t COUNTS[] = { 0, 1, 7, 8, 9, 255, 256, UINT32_MAX };
    int count = 0;
    calls[count++] = control_call("Main", 0, 0, 0, 0, 13);
    for (int f = 0; f < 4; f++) {
        for (int i = 0; i < (int)(sizeof SAMPLES / sizeof *SAMPLES); i++) {
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = control_call(
                INTS[f],
                1,
                SAMPLES[i],
                0,
                0,
                control_int(INTS[f], SAMPLES[i], 0, 0)
            );
        }
    }
    for (int f = 0; f < 2; f++) {
        for (int i = 0; i < (int)(sizeof PAIRED / sizeof *PAIRED); i++) {
            for (int j = 0; j < (int)(sizeof PAIRED / sizeof *PAIRED); j++) {
                assert(count < MAX_WIDTH_CALLS);
                calls[count++] = control_call(
                    PAIRS_OF[f],
                    2,
                    PAIRED[i],
                    PAIRED[j],
                    0,
                    control_int(PAIRS_OF[f], PAIRED[i], PAIRED[j], 0)
                );
            }
        }
    }
    for (int i = 0; i < (int)(sizeof BYTES / sizeof *BYTES); i++) {
        for (int j = 0; j < (int)(sizeof BYTES / sizeof *BYTES); j++) {
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = control_call(
                "Low",
                2,
                BYTES[i],
                BYTES[j],
                0,
                control_int("Low", BYTES[i], BYTES[j], 0)
            );
        }
    }
    for (int i = 0; i < (int)(sizeof BYTES / sizeof *BYTES); i++) {
        for (int j = 0; j < (int)(sizeof COUNTS / sizeof *COUNTS); j++) {
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = control_call(
                "Far",
                2,
                BYTES[i],
                COUNTS[j],
                0,
                control_int("Far", BYTES[i], COUNTS[j], 0)
            );
        }
    }
    for (int condition = 0; condition < 2; condition++) {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                assert(count < MAX_WIDTH_CALLS);
                calls[count++] = control_call(
                    "Choose",
                    3,
                    condition,
                    PAIRED[i + 2],
                    PAIRED[j + 2],
                    control_int("Choose", condition, PAIRED[i + 2], PAIRED[j + 2])
                );
            }
        }
    }
    for (int f = 0; f < (int)(sizeof BOOLS / sizeof *BOOLS); f++) {
        for (int bits = 0; bits < 1 << BOOL_ARITY[f]; bits++) {
            bool a = bits & 1, b = bits >> 1 & 1, c = bits >> 2 & 1;
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = control_call(
                BOOLS[f],
                BOOL_ARITY[f],
                a,
                b,
                c,
                control_bool(BOOLS[f], a, b, c)
            );
        }
    }
    return count;
}
/* Locals, assignments, nested if statements and logic run natively as C and
 * both VMs give them. */
static bool control_agrees(const char *directory)
{
    scalar_call *calls = xmalloc(sizeof *calls * MAX_WIDTH_CALLS);
    int count = control_calls(calls);
    bool agreed = width_module_agrees(directory, 0, "control", CONTROL_SOURCE, calls, count);
    free(calls);
    REQUIRE(agreed);
    return true;
}

/* Every loop form: ranges up and down, inclusive and exclusive, to the ends
 * of int and over byte bounds; a while loop with a condition, an endless one
 * that only a return leaves, nested loops whose break and continue leave or
 * restart the innermost, and an outer break and continue that pass over an
 * inner loop. Each range folds its indices into a hash, so
 * their order and count both show. */
static const char LOOP_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return Up(0, 5) + Nested(3); }\n"
         "Up :: (a: int, b: int) -> int {\n"
         "    h := 7;\n"
         "    for i in a..<b { h = h * 31 + i; }\n"
         "    return h;\n"
         "}\n"
         "Through :: (a: int, b: int) -> int {\n"
         "    h := 7;\n"
         "    for i in a..b { h = h * 31 + i; }\n"
         "    return h;\n"
         "}\n"
         "Down :: (a: int, b: int) -> int {\n"
         "    h := 7;\n"
         "    for < i in a..<b { h = h * 31 + i; }\n"
         "    return h;\n"
         "}\n"
         "DownThrough :: (a: int, b: int) -> int {\n"
         "    h := 7;\n"
         "    for < i in a..b { h = h * 31 + i; }\n"
         "    return h;\n"
         "}\n"
         "Bytes :: (a: u8, b: u8) -> int {\n"
         "    sum := 0;\n"
         "    for i in a..<b { sum += i; }\n"
         "    return sum;\n"
         "}\n"
         "Halve :: (n: int) -> int {\n"
         "    steps := 0;\n"
         "    while (n > 1) {\n"
         "        n /= 2;\n"
         "        steps += 1;\n"
         "    }\n"
         "    return steps;\n"
         "}\n"
         "Collatz :: (n: int) -> int {\n"
         "    steps := 0;\n"
         "    while (true) {\n"
         "        if (n <= 1) { return steps; }\n"
         "        if (n % 2 == 0) { n /= 2; } else { n = 3 * n + 1; }\n"
         "        steps += 1;\n"
         "    }\n"
         "}\n"
         "Outer :: (n: int) -> int {\n"
         "    h := 0;\n"
         "    for i in 0..<n {\n"
         "        if (i % 3 == 0) { continue; }\n"
         "        if (i > 7) { break; }\n"
         "        j := 0;\n"
         "        while (j < i) { j += 1; }\n"
         "        h = h * 5 + j;\n"
         "    }\n"
         "    return h;\n"
         "}\n"
         "Nested :: (n: int) -> int {\n"
         "    h := 0;\n"
         "    for i in 0..<n {\n"
         "        if (i == 2) { continue; }\n"
         "        j := 0;\n"
         "        while (j < n) {\n"
         "            j += 1;\n"
         "            if (j == 3) { continue; }\n"
         "            if (j > i + 2) { break; }\n"
         "            h = h * 7 + i * 10 + j;\n"
         "        }\n"
         "        if (h > 100000) { break; }\n"
         "    }\n"
         "    return h;\n"
         "}\n";
static int32_t loop_hash(int32_t h, int64_t i)
{
    return (int32_t)((uint32_t)h * 31u + (uint32_t)i);
}
/* What LOOP_SOURCE's functions give, as C computes them with the integer
 * contract. Indices are int64, so no C loop wraps at an end of int. */
static int64_t loop_int(const char *function, int64_t a, int64_t b)
{
    int32_t x = (int32_t)a, y = (int32_t)b, h = 7;
    if (!strcmp(function, "Up")) {
        for (int64_t i = x; i < y; i++) {
            h = loop_hash(h, i);
        }
        return h;
    }
    if (!strcmp(function, "Through")) {
        for (int64_t i = x; i <= y; i++) {
            h = loop_hash(h, i);
        }
        return h;
    }
    if (!strcmp(function, "Down")) {
        for (int64_t i = (int64_t)y - 1; i >= x; i--) {
            h = loop_hash(h, i);
        }
        return h;
    }
    if (!strcmp(function, "DownThrough")) {
        for (int64_t i = y; i >= x; i--) {
            h = loop_hash(h, i);
        }
        return h;
    }
    if (!strcmp(function, "Bytes")) {
        return a < b ? (b - a) * (a + b - 1) / 2 : 0;
    }
    int32_t steps = 0;
    if (!strcmp(function, "Halve")) {
        for (; x > 1; x /= 2) {
            steps++;
        }
        return steps;
    }
    if (!strcmp(function, "Collatz")) {
        for (; x > 1; steps++) {
            x = x % 2 == 0 ? x / 2 : total(7, total(9, 3, x), 1);
        }
        return steps;
    }
    uint32_t hash = 0;
    if (!strcmp(function, "Outer")) {
        for (int64_t i = 0; i < x; i++) {
            if (i % 3 == 0) {
                continue;
            }
            if (i > 7) {
                break;
            }
            hash = hash * 5u + (uint32_t)i;
        }
        return (int32_t)hash;
    }
    assert(!strcmp(function, "Nested"));
    for (int64_t i = 0; i < x; i++) {
        if (i == 2) {
            continue;
        }
        for (int64_t j = 1; j <= x; j++) {
            if (j == 3) {
                continue;
            }
            if (j > i + 2) {
                break;
            }
            hash = hash * 7u + (uint32_t)(i * 10 + j);
        }
        if ((int32_t)hash > 100000) {
            break;
        }
    }
    return (int32_t)hash;
}
/* The entry, each range function on every pair of RANGE_ENDS at most 16
 * apart, which includes every empty range, Bytes on every pair of bytes,
 * and the while loops on their samples. */
static int loop_calls(scalar_call calls[MAX_WIDTH_CALLS])
{
    static const char *const RANGES[] = { "Up", "Through", "Down", "DownThrough" };
    static const int64_t RANGE_ENDS[] = {
        INT32_MIN, INT32_MIN + 1, INT32_MIN + 3, -3,       -1, 0, 1,
        5,         INT32_MAX - 3, INT32_MAX - 1, INT32_MAX
    };
    static const int64_t BYTES[] = { 0, 1, 85, 127, 128, 200, 255 };
    static const struct
    {
        const char *function;
        int64_t samples[11];
    } WHILES[] = {
        { "Halve", { INT32_MIN, -1, 0, 1, 2, 3, 1024, 1025, INT32_MAX } },
        { "Collatz", { INT32_MIN, -5, 0, 1, 2, 3, 6, 7, 27, 97, 871 } },
        { "Nested", { INT32_MIN, -1, 0, 1, 2, 3, 4, 5, 8, 20 } },
        { "Outer", { INT32_MIN, 0, 1, 2, 4, 5, 8, 9, 12, 100 } },
    };
    static const int WHILE_SAMPLES[] = { 9, 11, 10, 10 };
    int count = 0, ends = (int)(sizeof RANGE_ENDS / sizeof *RANGE_ENDS);
    calls[count++] = (scalar_call){
        .probe = { "loop", "Main", 0, { 0 }, loop_int("Up", 0, 5) + loop_int("Nested", 3, 0) }
    };
    for (int f = 0; f < 4; f++) {
        for (int i = 0; i < ends; i++) {
            for (int j = 0; j < ends; j++) {
                if (RANGE_ENDS[j] - RANGE_ENDS[i] > 16) {
                    continue;
                }
                int64_t a = RANGE_ENDS[i], b = RANGE_ENDS[j];
                assert(count < MAX_WIDTH_CALLS);
                calls[count++] = (scalar_call){
                    .probe = { "loop", RANGES[f], 2, { a, b }, loop_int(RANGES[f], a, b) }
                };
            }
        }
    }
    for (int i = 0; i < (int)(sizeof BYTES / sizeof *BYTES); i++) {
        for (int j = 0; j < (int)(sizeof BYTES / sizeof *BYTES); j++) {
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = (scalar_call){
                .probe = { "loop",
                           "Bytes",
                           2,
                           { BYTES[i], BYTES[j] },
                           loop_int("Bytes", BYTES[i], BYTES[j]) }
            };
        }
    }
    for (int f = 0; f < (int)(sizeof WHILES / sizeof *WHILES); f++) {
        for (int i = 0; i < WHILE_SAMPLES[f]; i++) {
            int64_t n = WHILES[f].samples[i];
            assert(count < MAX_WIDTH_CALLS);
            calls[count++] = (scalar_call){
                .probe = { "loop",
                           WHILES[f].function,
                           1,
                           { n },
                           loop_int(WHILES[f].function, n, 0) }
            };
        }
    }
    return count;
}
/* Loops, break and continue run natively as C and both VMs give them. */
static bool loops_agree(const char *directory)
{
    scalar_call *calls = xmalloc(sizeof *calls * MAX_WIDTH_CALLS);
    int count = loop_calls(calls);
    bool agreed = width_module_agrees(directory, 1, "loop", LOOP_SOURCE, calls, count);
    free(calls);
    REQUIRE(agreed);
    return true;
}
/* Switches: arms that all return, a default arm written first that runs
 * last, a byte and an int64 value, arm values that are calls and
 * expressions, one repeated so only its first arm can run, break and
 * continue through a switch to its loop, and a switch nested in an arm. */
static const char
    SWITCH_SOURCE[] = "package scalar\n"
                      "Main :: () -> int { return Grade(85) + Late(2); }\n"
                      "Grade :: (x: int) -> int {\n"
                      "    switch x / 10 {\n"
                      "        case 10: return 5;\n"
                      "        case 9: return 4;\n"
                      "        case 8: return 3;\n"
                      "        case: return 0;\n"
                      "    }\n"
                      "}\n"
                      "Late :: (x: int) -> int {\n"
                      "    r := 0;\n"
                      "    switch x {\n"
                      "        case: r = 100;\n"
                      "        case 1: r = 1;\n"
                      "        case 2:\n"
                      "            y := x * 3;\n"
                      "            r = y;\n"
                      "    }\n"
                      "    return r;\n"
                      "}\n"
                      "Narrow :: (b: u8) -> int {\n"
                      "    switch b {\n"
                      "        case 0: return 1;\n"
                      "        case 255: return 2;\n"
                      "    }\n"
                      "    return 3;\n"
                      "}\n"
                      "Long :: (v: i64) -> int {\n"
                      "    switch v {\n"
                      "        case -1: return 1;\n"
                      "        case 4294967296: return 2;\n"
                      "    }\n"
                      "    return 3;\n"
                      "}\n"
                      "Twice :: (v: int) -> int { return v * 2; }\n"
                      "Order :: (x: int) -> int {\n"
                      "    switch x {\n"
                      "        case Twice(1): return 10;\n"
                      "        case 2: return 20;\n"
                      "        case -x: return 30;\n"
                      "    }\n"
                      "    return 40;\n"
                      "}\n"
                      "Steps :: (n: int) -> int {\n"
                      "    h := 0;\n"
                      "    for i in 0..<n {\n"
                      "        switch i % 4 {\n"
                      "            case 0: continue;\n"
                      "            case 3:\n"
                      "                if (i > 9) { break; }\n"
                      "                h += 100;\n"
                      "            case: h = h * 3 + i;\n"
                      "        }\n"
                      "        h += 1;\n"
                      "    }\n"
                      "    return h;\n"
                      "}\n"
                      "Pair :: (a: int, b: int) -> int {\n"
                      "    switch a {\n"
                      "        case 0:\n"
                      "            switch b {\n"
                      "                case 0: return 1;\n"
                      "                case: return 2;\n"
                      "            }\n"
                      "        case 1:\n"
                      "            x := b * 2;\n"
                      "            return x;\n"
                      "    }\n"
                      "    return -1;\n"
                      "}\n";
/* What SWITCH_SOURCE's functions give, as C computes them with the integer
 * contract. */
static int64_t switch_int(const char *function, int64_t a, int64_t b)
{
    int32_t x = (int32_t)a, y = (int32_t)b;
    if (!strcmp(function, "Grade")) {
        int32_t tens = total(10, x, 10);
        return tens == 10 ? 5 : tens == 9 ? 4 : tens == 8 ? 3 : 0;
    }
    if (!strcmp(function, "Late")) {
        return x == 1 ? 1 : x == 2 ? 6 : 100;
    }
    if (!strcmp(function, "Narrow")) {
        return a == 0 ? 1 : a == 255 ? 2 : 3;
    }
    if (!strcmp(function, "Long")) {
        return a == -1 ? 1 : a == INT64_C(4294967296) ? 2 : 3;
    }
    if (!strcmp(function, "Order")) {
        return x == 2 ? 10 : x == total(12, x, 0) ? 30 : 40;
    }
    if (!strcmp(function, "Steps")) {
        uint32_t h = 0;
        for (int64_t i = 0; i < x; i++) {
            if (i % 4 == 0) {
                continue;
            }
            if (i % 4 == 3) {
                if (i > 9) {
                    break;
                }
                h += 100;
            } else {
                h = h * 3u + (uint32_t)i;
            }
            h += 1;
        }
        return (int32_t)h;
    }
    assert(!strcmp(function, "Pair"));
    return x == 0 ? (y == 0 ? 1 : 2) : x == 1 ? total(9, y, 2) : -1;
}
/* The entry, then each function on its samples: the int ones at and around
 * their arms' values and at the ends of int, Narrow on every byte, Long at
 * its arms and the ends of int64, and Pair on every pair of its samples. */
static int switch_calls(scalar_call calls[MAX_WIDTH_CALLS])
{
    static const char *const INTS[] = { "Grade", "Late", "Order", "Steps" };
    static const int64_t SAMPLES[] = { INT32_MIN, -1, 0,   1,   2,
                                       3,         12, 20,  79,  80,
                                       89,        99, 100, 109, INT32_MAX };
    static const int64_t LONGS[] = {
        INT64_MIN,           -2,       -1, 0, INT64_C(4294967295),
        INT64_C(4294967296), INT64_MAX
    };
    static const int64_t PAIRS[] = { INT32_MIN, -1, 0, 1, 2, INT32_MAX };
    int count = 0;
    calls[count++] = (scalar_call){ .probe = { "switch",
                                               "Main",
                                               0,
                                               { 0 },
                                               switch_int("Grade", 85, 0) +
                                                   switch_int("Late", 2, 0) } };
    for (int f = 0; f < 4; f++) {
        for (int i = 0; i < (int)(sizeof SAMPLES / sizeof *SAMPLES); i++) {
            /* Steps loops its argument's times, so it takes none past 20. */
            if (f == 3 && SAMPLES[i] > 20) {
                continue;
            }
            calls[count++] = (scalar_call){
                .probe = { "switch",
                           INTS[f],
                           1,
                           { SAMPLES[i] },
                           switch_int(INTS[f], SAMPLES[i], 0) }
            };
        }
    }
    for (int value = 0; value < 256; value++) {
        calls[count++] = (scalar_call){
            .probe = { "switch", "Narrow", 1, { value }, switch_int("Narrow", value, 0) }
        };
    }
    for (int i = 0; i < (int)(sizeof LONGS / sizeof *LONGS); i++) {
        calls[count++] = (scalar_call){
            .probe = { "switch", "Long", 1, { LONGS[i] }, switch_int("Long", LONGS[i], 0) }
        };
    }
    for (int i = 0; i < (int)(sizeof PAIRS / sizeof *PAIRS); i++) {
        for (int j = 0; j < (int)(sizeof PAIRS / sizeof *PAIRS); j++) {
            calls[count++] = (scalar_call){
                .probe = { "switch",
                           "Pair",
                           2,
                           { PAIRS[i], PAIRS[j] },
                           switch_int("Pair", PAIRS[i], PAIRS[j]) }
            };
        }
    }
    assert(count <= MAX_WIDTH_CALLS);
    return count;
}
/* Switches run natively as C and both VMs give them. */
static bool switches_agree(const char *directory)
{
    scalar_call *calls = xmalloc(sizeof *calls * MAX_WIDTH_CALLS);
    int count = switch_calls(calls);
    bool agreed = width_module_agrees(directory, 0, "switch", SWITCH_SOURCE, calls, count);
    free(calls);
    REQUIRE(agreed);
    return true;
}
/* A function well past the bounds the facts once had: twelve parameters,
 * WIDE_LOOPS reversed exclusive ranges, then WIDE_CHAINS assignments of a
 * logical chain of five reads, then a return. Short chains keep stage zero's
 * operand recursion within its 64 calls. */
enum
{
    WIDE_LOOPS = 40,
    WIDE_CHAINS = 20
};
static bool widest_loops_agree(const char *directory)
{
    char source[4096];
    int at = snprintf(
        source,
        sizeof source,
        "package scalar\nMain :: () -> int { return Wide(1, 3, true, 0, 0, 0, "
        "0, 0, 0, 0, 0, 12); }\n"
        "Wide :: (a: int, b: int, c: bool, d: int, e: int, f: int, g: int, h: "
        "int, i: int, j: int, k: int, l: int) -> int {\n"
    );
    for (int i = 0; i < WIDE_LOOPS; i++) {
        at += snprintf(source + at, sizeof source - (size_t)at, "    for < n in a..<b {}\n");
    }
    for (int i = 0; i < WIDE_CHAINS; i++) {
        at += snprintf(source + at, sizeof source - (size_t)at, "    c = c && c && c && c && c;\n");
    }
    snprintf(source + at, sizeof source - (size_t)at, "    return a + l;\n}\n");
    assert(strlen(source) < sizeof source - 1);
    scalar_call call = { .probe = { "widest", "Main", 0, { 0 }, 13 } };
    REQUIRE(width_module_agrees(directory, 0, "widest", source, &call, 1));
    return true;
}
/* `function`(`argument`) of `source` fails on both VMs with `status`. */
static bool vms_fail(const char *source, const char *function, int64_t argument, int status)
{
    unsigned char *closure;
    size_t length;
    REQUIRE(
        vm_closure(0, (pin_string){ .bytes = source, .length = strlen(source) }, &closure, &length)
    );
    char id[128], arguments[32];
    snprintf(id, sizeof id, "project:scalar:1:%s:callable", function);
    int n = snprintf(arguments, sizeof arguments, "[%" PRId64 "]", argument);
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(vm_load(closure, length, backend));
        compiler_host_invoke(
            host,
            script_catalog_id("package-symbol", id),
            arguments,
            (size_t)n,
            TARGET_BUDGET,
            &result
        );
        int given = result.flow_status;
        free(result.json);
        result = (compiler_host_result){ 0 };
        compiler_host_free(host);
        host = NULL;
        REQUIRE(given == status);
    }
    free(closure);
    arena_free(&detached);
    return true;
}
/* How a native call of a function taking one int ends, at O0 and O2, within
 * `seconds`: with `value`, when `error` is CHIBI_PROCESS_OK and `signal` 0;
 * otherwise with `error`, and for CHIBI_PROCESS_OK with `signal`'s status
 * and `report` in its standard error. */
typedef struct
{
    const char *function;
    int64_t argument, value;
    chibi_process_error error;
    int signal;
    const char *report;
    double seconds;
} native_ending;
/* Each of `calls`, of functions of `source`, ends natively as it says. */
static bool natively_end(
    const char *directory,
    const char *name,
    const char *source,
    const native_ending *calls,
    int count
)
{
    pin_value rows;
    REQUIRE(collected_rows(0, name, text(source), &rows));
    char *ir, *adapter = xmalloc(1);
    *adapter = 0;
    REQUIRE(emitted_ir(0, &rows, &ir));
    REQUIRE(ir);
    for (int i = 0; i < count; i++) {
        char probe[128];
        snprintf(probe, sizeof probe, "@probe_%s(", calls[i].function);
        REQUIRE(strstr(adapter, probe) || append_adapter(&rows, calls[i].function, &adapter));
    }
    for (int level = 0; level < 2; level++) {
        char library[PATH_MAX];
        REQUIRE(native_library(directory, name, ir, adapter, level * 2, library));
        for (int i = 0; i < count; i++) {
            const native_ending *expected = &calls[i];
            scalar_call call = {
                .probe = { name, expected->function, 1, { expected->argument }, 0 }
            };
            snprintf(call.symbol, sizeof call.symbol, "probe_%s", expected->function);
            chibi_process_capture_result
                ended = library_calls(directory, library, &call, 1, 2, expected->seconds);
            REQUIRE(ended.error == expected->error);
            if (expected->error != CHIBI_PROCESS_OK) {
                continue;
            }
            REQUIRE(ended.status == -expected->signal);
            REQUIRE(expected->signal || call.values[2] == expected->value);
            REQUIRE(!expected->report || strstr(native_errors, expected->report));
        }
    }
    free(adapter);
    free(ir);
    arena_free(&detached);
    return true;
}
/* Spin never returns. Both VMs fail its call at their instruction budget.
 * Natively its loop runs unchecked, as Odin's does, and LLVM keeps it at O0
 * and O2, so the call runs until its deadline stops it. */
static const char SPIN_SOURCE[] = "package scalar\n"
                                  "Main :: () -> int { return 0; }\n"
                                  "Spin :: (n: int) -> int {\n"
                                  "    while (true) { n += 1; }\n"
                                  "}\n";
static bool endless_loops_hang(const char *directory)
{
    static const native_ending spin = { .function = "Spin",
                                        .error = CHIBI_PROCESS_TIMEOUT,
                                        .seconds = SPIN_SECONDS };
    REQUIRE(vms_fail(SPIN_SOURCE, "Spin", 0, FLOW_ERR_STEP_BUDGET));
    REQUIRE(natively_end(directory, "spin", SPIN_SOURCE, &spin, 1));
    return true;
}
/* Direct, mutual and two-way recursion, recursion through a loop, and a
 * function that only ever calls itself. Forever's subtraction keeps its
 * call from being a tail call, which O2 would turn into an endless loop. */
static const char RECURSION_SOURCE
    [] = "package scalar\n"
         "Main :: () -> int { return Sum(10) + Even(7); }\n"
         "Sum :: (n: int) -> int {\n"
         "    if (n > 0) { return n + Sum(n - 1); }\n"
         "    return 0;\n"
         "}\n"
         "Fib :: (n: int) -> int {\n"
         "    if (n < 2) { return n; }\n"
         "    return Fib(n - 1) + Fib(n - 2);\n"
         "}\n"
         "Even :: (n: int) -> int {\n"
         "    if (n == 0) { return 1; }\n"
         "    return Odd(n - 1);\n"
         "}\n"
         "Odd :: (n: int) -> int {\n"
         "    if (n == 0) { return 0; }\n"
         "    return Even(n - 1);\n"
         "}\n"
         "Gcd :: (a: int, b: int) -> int {\n"
         "    if (b == 0) { return a; }\n"
         "    return Gcd(b, a % b);\n"
         "}\n"
         "Nodes :: (n: int) -> int {\n"
         "    c := 1;\n"
         "    for i in 0..<n { c += Nodes(i); }\n"
         "    return c;\n"
         "}\n"
         "Forever :: (n: int) -> int { return Forever(n + 1) - n; }\n";
/* What RECURSION_SOURCE's functions give, as C computes them with the
 * integer contract. */
static int64_t recursion_int(const char *function, int64_t a, int64_t b)
{
    int32_t x = (int32_t)a, y = (int32_t)b;
    if (!strcmp(function, "Sum")) {
        return x > 0 ? total(7, x, (int32_t)recursion_int("Sum", x - 1, 0)) : 0;
    }
    if (!strcmp(function, "Fib")) {
        return x < 2 ? x
                     : total(
                           7,
                           (int32_t)recursion_int("Fib", x - 1, 0),
                           (int32_t)recursion_int("Fib", x - 2, 0)
                       );
    }
    if (!strcmp(function, "Even") || !strcmp(function, "Odd")) {
        bool even = !strcmp(function, "Even");
        return x == 0 ? even : recursion_int(even ? "Odd" : "Even", x - 1, 0);
    }
    if (!strcmp(function, "Nodes")) {
        return x > 0 ? INT64_C(1) << x : 1;
    }
    assert(!strcmp(function, "Gcd"));
    return y == 0 ? x : recursion_int("Gcd", y, total(11, x, y));
}
/* The entry, then each function at the depths every backend runs: Sum, Even
 * and Odd at each depth below stage zero's call depth limit, Fib and Nodes on
 * their first 12, within the VMs' instruction budget, and Gcd on pairs of
 * zero, signs, the ends of int and consecutive Fibonacci numbers, its
 * deepest. */
static int recursion_calls(scalar_call calls[MAX_WIDTH_CALLS])
{
    static const int64_t PAIRS[][2] = { { 0, 0 },
                                        { 12, 0 },
                                        { 0, 12 },
                                        { 12, 18 },
                                        { -12, 18 },
                                        { 12, -18 },
                                        { INT32_MIN, -1 },
                                        { INT32_MAX, INT32_MIN },
                                        { 1836311903, 1134903170 } };
    int count = 0;
    calls[count++] = (scalar_call){ .probe = { "recursion",
                                               "Main",
                                               0,
                                               { 0 },
                                               recursion_int("Sum", 10, 0) +
                                                   recursion_int("Even", 7, 0) } };
    static const char *const DEEP[] = { "Sum", "Even", "Odd" };
    for (int f = 0; f < 3; f++) {
        /* Even and Odd recurse without end below 0, so only Sum takes -1. */
        for (int n = f ? 0 : -1; n < SCRIPT_CALL_DEPTH_LIMIT; n++) {
            calls[count++] = (scalar_call){
                .probe = { "recursion", DEEP[f], 1, { n }, recursion_int(DEEP[f], n, 0) }
            };
        }
    }
    static const char *const WIDE[] = { "Fib", "Nodes" };
    for (int f = 0; f < 2; f++) {
        for (int n = 0; n < 12; n++) {
            calls[count++] = (scalar_call){
                .probe = { "recursion", WIDE[f], 1, { n }, recursion_int(WIDE[f], n, 0) }
            };
        }
    }
    for (int i = 0; i < (int)(sizeof PAIRS / sizeof *PAIRS); i++) {
        calls[count++] = (scalar_call){
            .probe = { "recursion",
                       "Gcd",
                       2,
                       { PAIRS[i][0], PAIRS[i][1] },
                       recursion_int("Gcd", PAIRS[i][0], PAIRS[i][1]) }
        };
    }
    assert(count <= MAX_WIDTH_CALLS);
    return count;
}
/* Recursion runs natively as C and both VMs give it. */
static bool recursion_agrees(const char *directory)
{
    scalar_call *calls = xmalloc(sizeof *calls * MAX_WIDTH_CALLS);
    int count = recursion_calls(calls);
    bool agreed = width_module_agrees(directory, 1, "recursion", RECURSION_SOURCE, calls, count);
    free(calls);
    REQUIRE(agreed);
    return true;
}
/* Natively recursion is unchecked, as Rust's is: Sum, Even and Odd run 10,000
 * calls deep, far past any VM's limit, and Forever's endless recursion
 * faults in the stack's guard page, which the host reports as a stack
 * overflow before it aborts, at O0 and O2, well before the deadline. */
static bool recursion_overflows_by_name(const char *directory)
{
    enum
    {
        DEEP = 10000
    };
    /* Sum(DEEP) wraps as the sum of its terms does. */
    int32_t sum = (int32_t)(uint32_t)((uint64_t)DEEP * (DEEP + 1) / 2);
    const native_ending calls[] = {
        { "Sum", DEEP, sum, CHIBI_PROCESS_OK, 0, NULL, CALLS_SECONDS },
        { "Even", DEEP, 1, CHIBI_PROCESS_OK, 0, NULL, CALLS_SECONDS },
        { "Odd", DEEP, 0, CHIBI_PROCESS_OK, 0, NULL, CALLS_SECONDS },
        { "Forever", 0, 0, CHIBI_PROCESS_OK, SIGABRT, "stack overflow", CALLS_SECONDS },
    };
    REQUIRE(
        natively_end(directory, "recursion", RECURSION_SOURCE, calls, (int)(sizeof calls / sizeof *calls))
    );
    return true;
}

/* A function of LONG_LOCALS locals, then LONG_ASSIGNMENTS assignments, then a
 * return, one statement each, well past the bounds the facts once had. */
enum
{
    LONG_LOCALS = 40,
    LONG_ASSIGNMENTS = 100
};
static bool long_functions_agree(const char *directory)
{
    char source[4096];
    int at = snprintf(source, sizeof source, "package scalar\nMain :: () -> int {\n");
    for (int i = 0; i < LONG_LOCALS; i++) {
        at += snprintf(source + at, sizeof source - (size_t)at, "    x%d := %d;\n", i, i);
    }
    for (int i = 0; i < LONG_ASSIGNMENTS; i++) {
        at += snprintf(source + at, sizeof source - (size_t)at, "    x0 = x%d + %d;\n", i % LONG_LOCALS, i);
    }
    snprintf(source + at, sizeof source - (size_t)at, "    return x0;\n}\n");
    assert(strlen(source) < sizeof source - 1);
    /* The last assignment reads x19 and adds 99. */
    scalar_call call = { .probe = { "long", "Main", 0, { 0 }, 118 } };
    REQUIRE(width_module_agrees(directory, 0, "long", source, &call, 1));
    return true;
}
/* A new directory for the native builds' files. */
static bool native_directory(char directory[PATH_MAX])
{
    const char *temporary = getenv("TMPDIR");
    snprintf(directory, PATH_MAX, "%s/chibi-native-scalar-XXXXXX", temporary && *temporary ? temporary : "/tmp");
    REQUIRE(mkdtemp(directory));
    return true;
}
/* Removes the files the native builds registered, then `directory`. A build
 * that failed may not have written all its files. */
static bool remove_native_files(const char *directory)
{
    bool removed = true;
    for (int i = 0; i < acount(native_files); i++) {
        removed = (!remove(native_files[i]) || errno == ENOENT) && removed;
        free(native_files[i]);
    }
    afree(native_files);
    REQUIRE(!rmdir(directory) && removed);
    return true;
}
/* Whether process `pid` has exited: it is gone, or a zombie whose parent
 * has not reaped it yet. Waits up to `seconds` for its kill to land. */
static bool process_ended(pid_t pid, double seconds)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    for (double deadline = chibi_process_now() + seconds;
         chibi_process_now() < deadline;) {
        FILE *file = fopen(path, "r");
        if (!file) {
            return true;
        }
        char state = 0;
        int fields = fscanf(file, "%*d (%*[^)]) %c", &state);
        fclose(file);
        if (fields == 1 && (state == 'Z' || state == 'X')) {
            return true;
        }
        usleep(10000);
    }
    return false;
}
/* How long a run that should end at its deadline or cancellation may take,
 * which allows for a slow sanitized host. */
static const double PROMPT_SECONDS = 10;

/* Starts the bundle, creating its directory if needed: an empty list of
 * modules for each target, and for a target whose objects reference a
 * runtime symbol, an object that defines it, which its runner links into
 * each library. */
TEST_CASE(native_bundle_starts_empty)
{
    if (!bundle_root()) {
        return true;
    }
    char directory[PATH_MAX];
    REQUIRE(native_directory(directory));
    bool started = !mkdir(bundle_root(), 0700) || errno == EEXIST;
    for (int t = 0; started && t < BUNDLE_TARGET_COUNT; t++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", bundle_root(), BUNDLE_TARGETS[t].directory);
        started = !mkdir(path, 0700) || errno == EEXIST;
        snprintf(
            path,
            sizeof path,
            "%s/%s/modules.txt",
            bundle_root(),
            BUNDLE_TARGETS[t].directory
        );
        started = started && write_text(path, "", "");
        if (!started || !BUNDLE_TARGETS[t].runtime) {
            continue;
        }
        char ir[128], object[PATH_MAX], module[PATH_MAX], undefined[NATIVE_OUTPUT];
        snprintf(ir, sizeof ir, "target triple = \"%s\"\n", BUNDLE_TARGETS[t].triple);
        snprintf(
            object,
            sizeof object,
            "%s/%s/runtime.o",
            bundle_root(),
            BUNDLE_TARGETS[t].directory
        );
        started = native_object(directory, "runtime", ir, BUNDLE_TARGETS[t].runtime, 0, object, module, undefined) &&
                  !*undefined && bundle_module_targets(module, t);
    }
    REQUIRE(remove_native_files(directory) && started);
    return true;
}
/* A dense switch, which O2 lowers through a table, links into a shared
 * library and runs: the driver's objects are position independent. */
static bool lookup_tables_link(const char *directory)
{
    static const char
        TABLE[] = "@index = internal global i32 3\n"
                  "define i32 @table() {\n"
                  "entry:\n"
                  "  %x = load volatile i32, ptr @index\n"
                  "  switch i32 %x, label %other [ i32 0, label %a\n"
                  "    i32 1, label %b\n    i32 2, label %c\n"
                  "    i32 3, label %d\n    i32 4, label %e ]\n"
                  "a:\n  ret i32 11\nb:\n  ret i32 23\nc:\n  ret i32 5\n"
                  "d:\n  ret i32 47\ne:\n  ret i32 9\nother:\n  ret i32 0\n"
                  "}\n";
    char library[PATH_MAX];
    REQUIRE(native_library(directory, "table", TABLE, "", 2, library));
    scalar_call call = { .symbol = "table" };
    REQUIRE(native_passed(library_calls(directory, library, &call, 1, 2, CALLS_SECONDS)));
    REQUIRE(call.values[2] == 47);
    return true;
}
/* The SIGSEGV handler a stack_overflow child installs before the report,
 * with SIGUSR1 in its mask: it exits 7 when that mask is in force. */
static void handled_fault(int)
{
    sigset_t blocked;
    sigprocmask(SIG_SETMASK, NULL, &blocked);
    _exit(sigismember(&blocked, SIGUSR1) ? 7 : 8);
}
/* The child of stack_overflow_leaves_other_faults. `mode` is "handled" or
 * "default", the action SIGSEGV has before the report is installed, then
 * "-fault" for a write to an unmapped page or "-sent" for raise(SIGSEGV). */
int test_language_stack_overflow_child(const char *mode)
{
    struct sigaction before = { .sa_handler = SIG_DFL };
    sigemptyset(&before.sa_mask);
    if (!strncmp(mode, "handled", strlen("handled"))) {
        before.sa_handler = handled_fault;
        sigaddset(&before.sa_mask, SIGUSR1);
    }
    sigaction(SIGSEGV, &before, NULL);
    stack_overflow_init();
    if (strstr(mode, "-fault")) {
        *(volatile int *)(uintptr_t)16 = 0;
    } else {
        raise(SIGSEGV);
    }
    return 1;
}
/* A SIGSEGV that is not a stack overflow, a fault or a sent signal, goes to
 * the action installed before the report, with that action's mask. */
TEST_CASE(stack_overflow_leaves_other_faults)
{
    static const struct
    {
        const char *mode;
        int status;
    } cases[] = { { "handled-fault", 7 },
                  { "handled-sent", 7 },
                  { "default-fault", -SIGSEGV },
                  { "default-sent", -SIGSEGV } };
    char self[PATH_MAX], out[NATIVE_OUTPUT];
    ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
    REQUIRE(length > 0);
    self[length] = 0;
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        const char *const argv[] = { self, "--stack-overflow-child", cases[i].mode, NULL };
        chibi_process_capture_result ended = run_native(argv, "/", CALLS_SECONDS, out);
        REQUIRE(ended.error == CHIBI_PROCESS_OK && ended.status == cases[i].status);
        REQUIRE(!strstr(native_errors, "stack overflow"));
    }
    return true;
}
TEST_CASE(package_compiler_native_runs_are_bounded)
{
    char out[NATIVE_OUTPUT];
    /* A tool that hangs, with a descendant in its group, times out at its
     * deadline, and the descendant is killed with it. */
    const char *const hung[] = { "/bin/sh", "-c", "sleep 60 & echo $!; wait", NULL };
    double started = chibi_process_now();
    chibi_process_capture_result result = run_native(hung, "/", 0.5, out);
    REQUIRE(result.error == CHIBI_PROCESS_TIMEOUT);
    REQUIRE(chibi_process_now() - started < PROMPT_SECONDS);
    char *end;
    long descendant = strtol(out, &end, 10);
    REQUIRE(end != out && descendant > 0);
    REQUIRE(process_ended((pid_t)descendant, PROMPT_SECONDS));
    /* SIGTERM while a tool runs cancels it, and is not a timeout. */
    const char *const terminating[] = { "/bin/sh", "-c", "kill -TERM $PPID; sleep 60", NULL };
    started = chibi_process_now();
    result = run_native(terminating, "/", 60, out);
    bool cancelled = native_cancelled;
    native_cancelled = 0;
    REQUIRE(cancelled && result.error == CHIBI_PROCESS_CANCELLED);
    REQUIRE(chibi_process_now() - started < PROMPT_SECONDS);
    /* Generated code that does not return in time times out in its child.
     * It loops 2^35 times, far past the deadline, so a deadline that is not
     * enforced fails the test instead of hanging it. */
    char directory[PATH_MAX], library[PATH_MAX];
    REQUIRE(native_directory(directory));
    const char *loop = "define i32 @hang() {\n"
                       "entry:\n"
                       "  br label %loop\n"
                       "loop:\n"
                       "  %i = phi i64 [ 0, %entry ], [ %next, %loop ]\n"
                       "  %next = add i64 %i, 1\n"
                       "  %done = icmp eq i64 %next, 34359738368\n"
                       "  br i1 %done, label %exit, label %loop\n"
                       "exit:\n"
                       "  ret i32 0\n"
                       "}\n";
    scalar_call call = { .symbol = "hang" };
    bool linked = lookup_tables_link(directory);
    bool built = native_library(directory, "hang", loop, "", 0, library);
    started = chibi_process_now();
    result = built ? library_calls(directory, library, &call, 1, 2, 1)
                   : (chibi_process_capture_result){ 0 };
    double elapsed = chibi_process_now() - started;
    REQUIRE(remove_native_files(directory) && linked && built);
    REQUIRE(result.error == CHIBI_PROCESS_TIMEOUT && elapsed < PROMPT_SECONDS);
    return true;
}
/* A module whose entry returns 0 and whose function k + 1 applies operation
 * k to its parameters. */
static bool total_ir(const char *function, char **ir)
{
    native_code code[4 + TOTAL_OPERATION_COUNT * 6];
    native_function functions[1 + TOTAL_OPERATION_COUNT];
    int parameters[TOTAL_OPERATION_COUNT * 2], slots[1 + TOTAL_OPERATION_COUNT * 3];
    int c = 0, p = 0, s = 0;
    code[c++] = (native_code){ .op = NATIVE_CHECK };
    code[c++] = (native_code){ .op = NATIVE_CONSTANT, .type = NATIVE_INT };
    code[c++] = (native_code){ .op = NATIVE_PUBLISH, .type = NATIVE_INT };
    code[c++] = (native_code){ .op = NATIVE_RETURN };
    slots[s++] = NATIVE_INT;
    functions[0] = (native_function){ NATIVE_INT, 0, 0, 0, 1, 0, c, 0, 0 };
    for (int k = 0; k < TOTAL_OPERATION_COUNT; k++) {
        int n = TOTAL_OPERATIONS[k].parameters, first = c;
        functions[k + 1] = (native_function){ NATIVE_INT, p, n, s, n + 1,
                                              c,          0, 0, 0 };
        code[c++] = (native_code){ .op = NATIVE_CHECK };
        for (int i = 0; i < n; i++) {
            code[c++] = (native_code){ .op = NATIVE_ARGUMENT,
                                       .type = NATIVE_INT,
                                       .output = i,
                                       .left = i };
            parameters[p++] = NATIVE_INT;
            slots[s++] = NATIVE_INT;
        }
        code[c++] = (native_code){ .op = TOTAL_OPERATIONS[k].op,
                                   .type = NATIVE_INT,
                                   .output = n,
                                   .left = 0,
                                   .right = n - 1 };
        slots[s++] = NATIVE_INT;
        code[c++] = (native_code){ .op = NATIVE_PUBLISH, .type = NATIVE_INT, .left = n };
        code[c++] = (native_code){ .op = NATIVE_RETURN };
        functions[k + 1].code = c - first;
    }
    native_module m = { functions,  1 + TOTAL_OPERATION_COUNT,
                        parameters, p,
                        slots,      s,
                        code,       c,
                        NULL,       0 };
    const pin_union *value;
    REQUIRE(native_target_emit(0, function, &m, &value));
    REQUIRE(value->tag == fixture_variant_tag(&consumer, 0, NATIVE_PACKAGE, "Result", "Emitted"));
    pin_string text = pin_text(value->values[0]);
    REQUIRE(
        !contains(text, "label %trap\n  br") && !contains(text, " nsw ") &&
        !contains(text, " nuw ")
    );
    *ir = xmalloc(text.length + 1);
    memcpy(*ir, text.bytes, text.length);
    (*ir)[text.length] = 0;
    return true;
}
/* The validation-only adapter probe_NAME(ptr) of each operation, which
 * takes and returns int64 as append_adapter's do. */
static char *total_adapters()
{
    char *adapters = xmalloc(1);
    *adapters = 0;
    for (int k = 0; k < TOTAL_OPERATION_COUNT; k++) {
        bool binary = TOTAL_OPERATIONS[k].parameters == 2;
        char text[512];
        int n = snprintf(
            text,
            sizeof text,
            "\ndefine i64 @probe_%s(ptr %%args) {\n  %%w0 = load i64, ptr "
            "%%args\n  %%a0 = trunc i64 %%w0 to i32\n",
            TOTAL_OPERATIONS[k].name
        );
        if (binary) {
            n += snprintf(text + n, sizeof text - (size_t)n, "  %%p1 = getelementptr i64, ptr %%args, i64 1\n  %%w1 = load i64, ptr %%p1\n  %%a1 = trunc i64 %%w1 to i32\n");
        }
        snprintf(
            text + n,
            sizeof text - (size_t)n,
            "  %%r = call i32 @chibi_scalar.%d(i32 %%a0%s)\n  %%z = sext "
            "i32 "
            "%%r to i64\n  ret i64 %%z\n}\n",
            k + 1,
            binary ? ", i32 %a1" : ""
        );
        size_t used = strlen(adapters);
        adapters = xrealloc(adapters, used + strlen(text) + 1);
        strcpy(adapters + used, text);
    }
    return adapters;
}
/* The entry, then operation k on every pair of TOTAL_VALUES, or every value
 * for a unary one. */
static int total_calls(int k, scalar_call calls[MAX_CASE_CALLS])
{
    int count = 0;
    calls[count++] = (scalar_call){ .probe = { "total", "Main", 0, { 0 }, 0 },
                                    .symbol = "chibi_scalar" };
    int n = TOTAL_OPERATIONS[k].parameters, rights = n == 2 ? TOTAL_VALUE_COUNT : 1;
    for (int i = 0; i < TOTAL_VALUE_COUNT; i++) {
        for (int j = 0; j < rights; j++) {
            int32_t a = TOTAL_VALUES[i], b = n == 2 ? TOTAL_VALUES[j] : 0;
            scalar_call *call = &calls[count++];
            *call = (scalar_call){ .probe = { "total",
                                              TOTAL_OPERATIONS[k].name,
                                              n,
                                              { a, b },
                                              total(TOTAL_OPERATIONS[k].op, a, b) } };
            snprintf(
                call->symbol,
                sizeof call->symbol,
                "probe_%s",
                TOTAL_OPERATIONS[k].name
            );
        }
    }
    assert(count <= MAX_CASE_CALLS);
    return count;
}

/* With a bundle, the total module for each other target, with every
 * operation's calls. */
static bool total_bundled(const char *directory, const char *adapters)
{
    if (!bundle_root()) {
        return true;
    }
    scalar_call *calls = xmalloc(sizeof *calls * MAX_WIDTH_CALLS);
    int count = 0;
    for (int k = 0; k < TOTAL_OPERATION_COUNT; k++) {
        count += total_calls(k, calls + count);
    }
    assert(count <= MAX_WIDTH_CALLS);
    bool built = true;
    for (int t = 0; built && t < BUNDLE_TARGET_COUNT; t++) {
        char *ir = NULL;
        built = total_ir(BUNDLE_TARGETS[t].function, &ir) &&
                bundle_module(directory, t, "total", ir, adapters, calls, count);
        free(ir);
    }
    free(calls);
    REQUIRE(built);
    return true;
}
TEST_CASE(package_compiler_native_arithmetic_is_total)
{
    const source_package_input consumers[] = { SCALAR_CONSUMER_SOURCE_INPUTS };
    REQUIRE(
        compiler_fixture_prepare(&consumer, consumers, (int)(sizeof consumers / sizeof *consumers))
    );
    char *ir;
    REQUIRE(total_ir("LinuxX64", &ir));
    char *adapters = total_adapters(), directory[PATH_MAX];
    REQUIRE(native_directory(directory));
    bool agreed = true;
    for (int level = 0; agreed && level < 2; level++) {
        char library[PATH_MAX];
        agreed = native_library(directory, "total", ir, adapters, level * 2, library);
        for (int k = 0; agreed && k < TOTAL_OPERATION_COUNT; k++) {
            scalar_call calls[MAX_CASE_CALLS];
            int count = total_calls(k, calls);
            agreed = native_passed(
                library_calls(directory, library, calls, count, 2 + level, CALLS_SECONDS)
            );
            for (int i = 1; agreed && i < count; i++) {
                const scalar_probe *probe = &calls[i].probe;
                agreed = calls[i].values[2 + level] == probe->expected;
                if (!agreed) {
                    fprintf(
                        stderr,
                        "native %s(%" PRId64 ", %" PRId64
                        ") at O%d gave %" PRId64 ", expected %" PRId64 "\n",
                        probe->function,
                        probe->arguments[0],
                        probe->arguments[1],
                        level * 2,
                        calls[i].values[2 + level],
                        probe->expected
                    );
                }
            }
        }
    }
    free(ir);
    agreed = agreed && total_bundled(directory, adapters);
    free(adapters);
    REQUIRE(remove_native_files(directory) && agreed);
    return true;
}
/* Whether `manifest` exports Small, whose entry gives 19 through Wide, and
 * not Wide, whose parameters no entry takes. */
static bool exports_small_alone(const execution_native_manifest *manifest)
{
    const execution_native_export *small = NULL;
    for (int i = 0; i < manifest->count; i++) {
        REQUIRE(strcmp(manifest->exports[i].name, "Wide"));
        if (!strcmp(manifest->exports[i].name, "Small")) {
            small = &manifest->exports[i];
        }
    }
    REQUIRE(small && small->parameter_count == 0);
    uint64_t result = 0;
    small->entry(NULL, NULL, &result);
    REQUIRE(result == 19);
    return true;
}
/* `library` loaded as the only library of a game's natives folder: its
 * manifest names every function an entry can take, and bound to the
 * library's module it gives a running Probe the VM's states. A missing
 * folder holds no libraries, and a file not named for its manifest fails. */
static bool library_loads_and_agrees(const char *directory, const char *library)
{
    char folder[PATH_MAX], misnamed[PATH_MAX], shipped[PATH_MAX],
        reason[PATH_MAX + 64], error[PATH_MAX + 64] = "";
    snprintf(folder, sizeof folder, "%s/natives", directory);
    snprintf(misnamed, sizeof misnamed, "%s/Other.so", folder);
    snprintf(shipped, sizeof shipped, "%s/Native.so", folder);
    snprintf(reason, sizeof reason, "native library %s has no chibi_natives_Other", misnamed);
    native_file(misnamed);
    native_file(shipped);
    native_file(folder);
    native_libraries *libraries = native_libraries_open(folder, error, sizeof error);
    REQUIRE(libraries);
    int count = 1;
    native_libraries_manifests(libraries, &count);
    REQUIRE(!count);
    native_libraries_close(libraries);
    REQUIRE(!mkdir(folder, 0700) && !rename(library, misnamed));
    REQUIRE(!native_libraries_open(folder, error, sizeof error) && !strcmp(error, reason));
    REQUIRE(!rename(misnamed, shipped));
    libraries = native_libraries_open(folder, error, sizeof error);
    if (!libraries) {
        fprintf(stderr, "%s\n", error);
    }
    count = 0;
    const execution_native_manifest *const
        *manifests = libraries ? native_libraries_manifests(libraries, &count) : NULL;
    bool agreed = count == 1 && manifests[0]->abi == EXECUTION_NATIVE_ABI &&
                  manifests[0]->count == 8 && exports_small_alone(manifests[0]) &&
                  native_calls_agree(manifests[0]);
    native_libraries_close(libraries);
    REQUIRE(!remove(shipped) && !rmdir(folder) && agreed);
    return true;
}
/* NATIVE_CALLS_LIBRARY compiled natively at O0 and O2 runs in a scene. */
static bool library_runs_in_a_scene(const char *directory)
{
    pin_value rows;
    char *ir;
    REQUIRE(
        collected_at(1, "libraries/Native.chibi", "library", text(NATIVE_CALLS_LIBRARY), &rows)
    );
    if (row_int(&rows, 1, "kind") != 2) {
        report_rejection(1, "library", &rows);
    }
    REQUIRE(row_int(&rows, 1, "kind") == 2 && emitted_ir(1, &rows, &ir) && ir);
    bool agreed = true;
    for (int level = 0; agreed && level <= 2; level += 2) {
        char library[PATH_MAX];
        agreed = native_library(directory, "library", ir, "", level, library) &&
                 library_loads_and_agrees(directory, library);
    }
    free(ir);
    REQUIRE(agreed);
    return true;
}
TEST_CASE(package_compiler_scalar_runs_natively_like_the_vm)
{
    REQUIRE(prepare());
    const source_package_input consumers[] = { SCALAR_CONSUMER_SOURCE_INPUTS };
    REQUIRE(
        compiler_fixture_prepare(&consumer, consumers, (int)(sizeof consumers / sizeof *consumers))
    );
    const source_package_input cases[] = { SCALAR_CASES_SOURCE_INPUTS };
    REQUIRE(compiler_fixture_prepare(&corpus, cases, (int)(sizeof cases / sizeof *cases)));
    char directory[PATH_MAX];
    REQUIRE(native_directory(directory));
    pin_value count;
    REQUIRE(
        compiler_fixture_call(&corpus, 0, CASES_PACKAGE, "Count", NULL, 0, COMPILE_BUDGET, &detached, &count)
    );
    int accepted = 0, deep = 0;
    bool agreed = true;
    for (int i = 0; agreed && !native_cancelled && i < count.i; i++) {
        agreed = scalar_case_agrees(directory, i, &accepted, &deep);
        arena_free(&detached);
    }
    agreed = agreed && folded_sources_agree(directory) &&
             folded_overflow_rejected() && widths_agree(directory) &&
             floats_agree(directory) && control_agrees(directory) &&
             long_functions_agree(directory) && loops_agree(directory) &&
             widest_loops_agree(directory) && switches_agree(directory) &&
             endless_loops_hang(directory) && recursion_agrees(directory) &&
             recursion_overflows_by_name(directory) &&
             library_runs_in_a_scene(directory);
    REQUIRE(remove_native_files(directory));
    if (native_cancelled) {
        fprintf(stderr, "native scalar run was cancelled\n");
    }
    REQUIRE(agreed && !native_cancelled);
    REQUIRE(accepted == SCALAR_ACCEPTED_CASES && deep == DEEP_CASE_COUNT);
    return true;
}
#endif
/* Query tests use the target's canonical Tick(dt: number), while the older
 * behavior fixtures above exercise an event named DeltaSeconds. */
static script_catalog query_game_catalog()
{
    static const script_catalog_parameter dt = { .name = "dt",
                                                 .type = { .storage = PIN_F32 } };
    static script_catalog_capability events[sizeof game_capabilities / sizeof *game_capabilities];
    memcpy(events, game_capabilities, sizeof events);
    events[0].parameters = &dt;
    static const script_catalog_relationship targets = { .id = QUERY_TARGETS,
                                                         .name = "Targets" };
    script_catalog catalog = game_catalog();
    catalog.capabilities = events;
    catalog.relationships = &targets;
    catalog.relationship_count = 1;
    return catalog;
}
static bool query_compile_result_category(
    int b,
    const char *identity,
    const char *source,
    bool error_type,
    compiler_result *out
)
{
    fixture.observe_failures = true;
    pin_value compile[ARGUMENTS], args[BEHAVIOR_ARGUMENTS];
    REQUIRE(arguments(b, false, NULL, 0, compile));
    args[0] = text(identity);
    args[1] = text(source);
    memcpy(&args[2], &compile[TARGET_ARGUMENT], 6 * sizeof *args);
    int first_native = ((pin_array *)args[5].ptr)->count;
    REQUIRE(behavior_natives(b, &args[5]));
    REQUIRE(game_arguments(b, first_native, &args[8]));
    pin_array *events = args[8].ptr, *payloads = args[9].ptr;
    script_catalog catalog = query_game_catalog();
    script_catalog_type descriptions[sizeof game_types / sizeof *game_types];
    memcpy(descriptions, catalog.types, sizeof descriptions);
    for (int i = 0; i < catalog.type_count; i++) {
        if (descriptions[i].id == SPOT) {
            descriptions[i].error = error_type;
        }
    }
    catalog.types = descriptions;
    pin_value *types = NULL;
    const pin_array *existing = args[10].ptr;
    for (int i = 0; i < existing->count; i++) {
        pin_value type = existing->items[i], id;
        REQUIRE(get(&type, "id", &id));
        const script_catalog_type *definition = script_catalog_type_by_id(&catalog, id.u64);
        REQUIRE(definition);
        REQUIRE(put_value(
            &type,
            "error",
            flag(script_catalog_type_contains_error(&catalog, definition->type))
        ));
        apush(types, type);
    }
    pin_value relationship = row(b, "GameType");
    REQUIRE(put_value(&relationship, "required", flag(false)));
    REQUIRE(put(&relationship, "underlying", -1));
    REQUIRE(put_value(&relationship, "name", text("Targets")));
    REQUIRE(put_value(&relationship, "id", u64(QUERY_TARGETS)));
    REQUIRE(put_value(&relationship, "layout", u64(0)));
    REQUIRE(put(&relationship, "storage", PIN_HANDLE));
    REQUIRE(put(&relationship, "size", sizeof(uint64_t)));
    REQUIRE(put_value(&relationship, "component", u64(QUERY_TARGETS)));
    REQUIRE(put(&relationship, "first", 0));
    REQUIRE(put(&relationship, "count", 0));
    REQUIRE(put_value(&relationship, "relationship", flag(true)));
    REQUIRE(put(&relationship, "traits", 0));
    REQUIRE(put_value(&relationship, "error", flag(false)));
    apush(types, relationship);
    args[10] = rows(types, acount(types), PIN_STRUCT, relationship.record);
    afree(types);
    REQUIRE(put_value(&payloads->items[0], "name", text("dt")));
    REQUIRE(put(&payloads->items[0], "storage", PIN_F32));
    REQUIRE(put_value(
        &events->items[0],
        "abi",
        u64(script_catalog_capability_abi(&catalog, &catalog.capabilities[0]))
    ));
    REQUIRE(compiler_call(b, "CompileBehavior", args, BEHAVIOR_ARGUMENTS, COMPILE_BUDGET, out));
    return true;
}
static bool query_compile_result(int b, const char *identity, const char *source, compiler_result *out)
{
    return query_compile_result_category(b, identity, source, false, out);
}
TEST_CASE(package_compiler_fallible_system_query_keeps_unsupported_boundary)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        for (int marked = 0; marked < 2; marked++) {
            const char
                source[] = "Fixed :: system(point: Spot, if Ready().value) {\n"
                           "    Ready :: () -> (value: bool, error: Spot?) { "
                           "return(true, nil); }\n"
                           "}\n";
            compiler_result out;
            REQUIRE(query_compile_result_category(b, "Fixed", source, marked != 0, &out));
            if (marked) {
                if (compiler_succeeded(&out)) {
                    fprintf(pu_errors(), "Marked system query accepted: %s\n", source);
                }
                REQUIRE(!compiler_succeeded(&out));
                pin_value message, document, line, column;
                REQUIRE(get(compiler_error(&out), "message", &message));
                REQUIRE(get(compiler_error(&out), "document", &document));
                REQUIRE(get(compiler_error(&out), "line", &line));
                REQUIRE(get(compiler_error(&out), "column", &column));
                REQUIRE(
                    !strcmp(pin_text(message).bytes, "Error result checking requires an authored function body in stage one")
                );
                REQUIRE(document.i == 0 && line.i == 1 && column.i == 33);
            } else {
                if (!compiler_succeeded(&out)) {
                    pin_value message, line, column;
                    REQUIRE(get(compiler_error(&out), "message", &message));
                    REQUIRE(get(compiler_error(&out), "line", &line));
                    REQUIRE(get(compiler_error(&out), "column", &column));
                    fprintf(
                        pu_errors(),
                        "Ordinary system query rejected: %s at %d:%d\n",
                        pin_text(message).bytes,
                        line.i,
                        column.i
                    );
                }
                REQUIRE(compiler_succeeded(&out));
            }
            arena_free(&detached);
        }
    }
    return true;
}
static bool query_stage_one_module(int b, const char *identity, const char *source, execution_module **module)
{
    *module = NULL;
    compiler_result out;
    REQUIRE(query_compile_result(b, identity, source, &out));
    if (!compiler_succeeded(&out)) {
        pin_value message = { 0 }, line = { 0 }, column = { 0 };
        get(compiler_error(&out), "message", &message);
        get(compiler_error(&out), "line", &line);
        get(compiler_error(&out), "column", &column);
        fprintf(
            stderr,
            "stage one query %s rejected: %s at %d:%d\n",
            identity,
            pin_text(message).bytes,
            line.i,
            column.i
        );
        arena_free(&detached);
        return false;
    }
    size_t length = 0;
    unsigned char *bytes = closure_bytes(&out, &length);
    arena_free(&detached);
    script_catalog catalog = query_game_catalog();
    const execution_prepared_linker linker = { .user = &catalog,
                                               .type = link_game_type,
                                               .event = link_game_event,
                                               .host = link_game_host };
    char error[256] = { 0 };
    *module = execution_prepared_read(
        bytes,
        length,
        &linker,
        b ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    if (!*module) {
        fprintf(stderr, "stage one query %s load: %s\n", identity, error);
    }
    free(bytes);
    return *module != NULL;
}
/* Play without the target entity, so a query's rows are the owner and the
 * entities the test adds. */
static bool query_play(execution_module *module, player *out)
{
    REQUIRE(play(module, out));
    REQUIRE(ecs_world_destroy_entity(out->world, out->target) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(out->world) == ECS_OK);
    return true;
}
static ecs_entity query_add_spot(player *p, float x)
{
    ecs_entity entity = ecs_world_create_entity(p->world);
    const spot initial = { .x = x, .w = 9 };
    if (ecs_world_record_add(p->world, entity, SPOT_COMPONENT, &initial, 0) != ECS_OK ||
        ecs_world_apply_commands(p->world) != ECS_OK) {
        return (ecs_entity){ 0 };
    }
    return entity;
}
static bool query_spot_equals(const player *p, ecs_entity entity, float x, float y)
{
    const spot *value = ecs_component_data_const(p->world, entity, SPOT_COMPONENT);
    if (!value || value->x != x || value->y != y) {
        fprintf(
            stderr,
            "query Spot: expected %g,%g, got %g,%g\n",
            x,
            y,
            value ? value->x : -1.0,
            value ? value->y : -1.0
        );
        return false;
    }
    return true;
}
static bool query_step(player *p, double now, double delta)
{
    int status = execution_program_step(p->program, now, delta, 100000);
    if (status != FLOW_OK) {
        const execution_program_failure *failure = execution_program_error(p->program);
        fprintf(
            stderr,
            "query step at %g: status %d, %s at %d:%d\n",
            now,
            status,
            failure ? failure->message : "no failure recorded",
            failure ? failure->line : 0,
            failure ? failure->column : 0
        );
    }
    return status == FLOW_OK;
}
static void query_register_types()
{
    static const type_desc *const reflected[] = { &spot_type };
    static const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    type_catalog_set_project(&pack);
}
TEST_CASE(package_compiler_query_rows_capture_and_restart)
{
    static const char source
        [] = "Rows :: behavior {\n"
             "    total: int = 0\n"
             "    Bump :: (value: number) -> number { total := value; "
             "total += 1; return total }\n"
             "    on Tick :: () {\n"
             "        threshold: number = 3\n"
             "        matches := query(before: Spot, live: ^Spot, if "
             "before.x > threshold, amount := Bump(before.x + threshold - 1))\n"
             "        threshold = 100\n"
             "        for row in matches {\n"
             "            total += 1\n"
             "            row.live.y = row.amount\n"
             "            copy := row.before\n"
             "            copy.x += 1\n"
             "            self.Spot.y += copy.x - row.before.x\n"
             "            if row.before.x == 5 { continue }\n"
             "            row.live.x += 10\n"
             "            self.Spot.y += row.before.x\n"
             "        }\n"
             "        for row in matches { total += 100; break }\n"
             "        for row in matches { total += 1000 }\n"
             "    }\n"
             "}\n";
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        execution_module *module = NULL;
        player p = { 0 };
        passed = query_stage_one_module(b, "Rows", source, &module) &&
                 query_play(module, &p);
        if (passed) {
            ecs_entity skipped = query_add_spot(&p, 1);
            ecs_entity continued = query_add_spot(&p, 5);
            ecs_entity changed = query_add_spot(&p, 9);
            const pin_value delta = { .type = PIN_F32, .f = .25 };
            passed = ecs_entity_to_u64(skipped) &&
                     ecs_entity_to_u64(continued) && ecs_entity_to_u64(changed) &&
                     execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, .25, 100000) ==
                         FLOW_OK &&
                     query_spot_equals(&p, p.owner, 0, 11) &&
                     query_spot_equals(&p, skipped, 1, 0) &&
                     query_spot_equals(&p, continued, 5, 8) &&
                     query_spot_equals(&p, changed, 19, 12);
            const pin_value *total = execution_instance_state(
                p.instance,
                script_catalog_id("symbol", "Rows:1:total")
            );
            passed = passed && total && total->type == PIN_I32 && total->i == 2102;
        }
        stop(&p);
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
TEST_CASE(package_compiler_query_nested_return_and_empty_iteration)
{
    static const char
        source[] = "Nested :: behavior {\n"
                   "    First :: () -> number {\n"
                   "        matches := query(point: Spot, if point.x > 0)\n"
                   "        for outer in matches {\n"
                   "            for inner in matches { return outer.point.x + "
                   "inner.point.x }\n"
                   "        }\n"
                   "        return -1\n"
                   "    }\n"
                   "    on Tick :: () {\n"
                   "        self.Spot.y = First()\n"
                   "        for row in query(point: Spot, if point.x < 0) { "
                   "self.Spot.y = -100 }\n"
                   "        for row in query(point: Spot, if point.x > 0) { "
                   "self.Spot.x += row.point.x }\n"
                   "    }\n"
                   "}\n";
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        execution_module *module = NULL;
        player p = { 0 };
        passed = query_stage_one_module(b, "Nested", source, &module) &&
                 query_play(module, &p);
        if (passed) {
            ecs_entity other = query_add_spot(&p, 7);
            const pin_value delta = { .type = PIN_F32, .f = .25 };
            passed = ecs_entity_to_u64(other) &&
                     execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, .25, 100000) ==
                         FLOW_OK &&
                     query_spot_equals(&p, p.owner, 7, 14) &&
                     query_spot_equals(&p, other, 7, 0);
        }
        stop(&p);
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
TEST_CASE(package_compiler_query_header_constructs_captured_description)
{
    static const char source[] =
        "NestedHeader :: behavior {\n"
        "    total: int = 0\n"
        "    on Tick :: () {\n"
        "        for outer in query(point: Spot, if point.x > 0, "
        "inner := query(other: Spot, if other.x > point.x)) {\n"
        "            for row in outer.inner { self.Spot.y += row.other.x }\n"
        "            for row in outer.inner { total += int(row.other.x) }\n"
        "        }\n"
        "    }\n"
        "}\n";
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        execution_module *module = NULL;
        player p = { 0 };
        passed = query_stage_one_module(b, "NestedHeader", source, &module) &&
                 query_play(module, &p);
        if (passed) {
            ecs_entity first = query_add_spot(&p, 2);
            ecs_entity second = query_add_spot(&p, 7);
            const pin_value delta = { .type = PIN_F32, .f = .25 };
            passed = ecs_entity_to_u64(first) && ecs_entity_to_u64(second) &&
                     execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, .25, 100000) ==
                         FLOW_OK &&
                     query_spot_equals(&p, p.owner, 0, 7);
            const pin_value *total = execution_instance_state(
                p.instance,
                script_catalog_id("symbol", "NestedHeader:1:total")
            );
            passed = passed && total && total->type == PIN_I32 && total->i == 7;
        }
        stop(&p);
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
TEST_CASE(package_compiler_system_query_shares_snapshot_and_implicit_dt)
{
    static const char
        source[] = "Apply :: system(before: Spot, live: ^Spot, if before.x > "
                   "0, amount := number(10 / int(before.x))) {\n"
                   "    live.x += amount\n"
                   "    live.y = before.x + number(dt)\n"
                   "}\n";
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        execution_module *module = NULL;
        player p = { 0 };
        passed = query_stage_one_module(b, "Apply", source, &module) &&
                 query_play(module, &p);
        if (passed) {
            ecs_entity first = query_add_spot(&p, 2);
            ecs_entity second = query_add_spot(&p, 5);
            passed = module->plan.system_description == 1 &&
                     ecs_entity_to_u64(first) && ecs_entity_to_u64(second) &&
                     query_step(&p, 0, .25) && query_spot_equals(&p, p.owner, 0, 0) &&
                     query_spot_equals(&p, first, 7, 2.25f) &&
                     query_spot_equals(&p, second, 7, 5.25f);
        }
        stop(&p);
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
/* Each source's queries and system header compile into its own module, in
 * whatever position the project lists it: each module matches the one its
 * source compiles to alone, and its queries run. */
/* Compile `sources` together, then play each query-bearing module alone. A
 * system adds 2 to every row's y; a behavior's rows end at (x, y). */
static bool query_sources_compose(
    int b,
    const char *const *identities,
    const char *const *sources,
    int count,
    float x,
    float y
)
{
    script_catalog catalog = query_game_catalog();
    pin_value out;
    REQUIRE(
        catalog_compile_sources(b, &catalog, identities, sources, count, "CompileBehaviorsWithCatalog", &out)
    );
    const pin_array *array = out.ptr;
    if (!array || !array->count) {
        arena_free(&detached);
        REQUIRE(
            catalog_compile_sources(b, &catalog, identities, sources, count, "BehaviorRejectionWithCatalog", &out)
        );
        fprintf(stderr, "query sources rejected: %s\n", pin_text(out).bytes);
        arena_free(&detached);
        return false;
    }
    size_t length = (size_t)array->count;
    unsigned char *bytes = xmalloc(length);
    for (size_t i = 0; i < length; i++) {
        bytes[i] = array->items[i].u8;
    }
    arena_free(&detached);
    const execution_prepared_linker linker = { .user = &catalog,
                                               .type = link_game_type,
                                               .event = link_game_event,
                                               .host = link_game_host };
    size_t remaining = COMPILER_HOST_MAX_CLOSURE_BYTES;
    uint64_t root = 0;
    char error[256] = { 0 };
    package_collection *packages = compiler_artifact_read(
        bytes,
        length,
        &linker,
        b ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
        &remaining,
        &root,
        error,
        sizeof error
    );
    free(bytes);
    if (!packages) {
        fprintf(stderr, "query sources load: %s\n", error);
    }
    REQUIRE(packages && package_collection_count(packages) == count);
    bool passed = true;
    for (int i = 0; passed && i < count; i++) {
        execution_module *alone = NULL;
        passed = query_stage_one_module(b, identities[i], sources[i], &alone);
        execution_module *composed = NULL;
        for (int j = 0; passed && j < count; j++) {
            execution_module *candidate = package_collection_module(packages, j);
            if (execution_module_id(candidate) == execution_module_id(alone)) {
                composed = candidate;
            }
        }
        passed = passed && composed &&
                 composed->plan.function_count == alone->plan.function_count &&
                 composed->plan.description_count == alone->plan.description_count &&
                 composed->plan.system_description == alone->plan.system_description;
        execution_module_free(alone);
        if (!passed || !composed->plan.description_count) {
            continue;
        }
        player p = { 0 };
        passed = query_play(composed, &p);
        ecs_entity other = passed ? query_add_spot(&p, 4) : (ecs_entity){ 0 };
        const pin_value delta = { .type = PIN_F32, .f = .25 };
        passed = passed && ecs_entity_to_u64(other) &&
                 (composed->plan.system_description
                      ? query_step(&p, 0, .25) && query_spot_equals(&p, other, 4, 2)
                      : execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, .25, 100000) ==
                                FLOW_OK &&
                            query_spot_equals(&p, other, x, y));
        stop(&p);
    }
    package_collection_free(packages);
    return passed;
}
TEST_CASE(package_compiler_queries_compose_in_any_source_order)
{
    static const char *const plain = "Plain :: behavior {}\n";
    static const char *const
        rows = "Rows :: behavior {\n"
               "    on Tick :: () {\n"
               "        matches := query(live: ^Spot)\n"
               "        for row in matches { row.live.x += 1 }\n"
               "    }\n"
               "}\n";
    static const char *const apply = "Apply :: system(live: ^Spot) {\n"
                                     "    live.y += 2\n"
                                     "}\n";
    static const char *const orders[][2] = {
        { "Plain", "Rows" },
        { "Rows", "Plain" },
        { "Rows", "Apply" },
        { "Apply", "Rows" },
    };
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        for (size_t o = 0; passed && o < sizeof orders / sizeof *orders; o++) {
            const char *sources[2];
            for (int i = 0; i < 2; i++) {
                const char *name = orders[o][i];
                sources[i] = !strcmp(name, "Plain") ? plain
                           : !strcmp(name, "Rows")  ? rows
                                                    : apply;
            }
            passed = query_sources_compose(b, orders[o], sources, 2, 5, 0);
        }
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
/* A query captures a local and calls a helper whose loop changes a local
 * that shadows a state. An earlier source whose offsets run past the query's
 * must hide neither from the later one: each scan stays in its own source. */
TEST_CASE(package_compiler_query_captures_resolve_in_any_source_order)
{
    char padded[600];
    snprintf(padded, sizeof padded, "Padded :: behavior {%512s\n    pad: int = 0\n}\n", "");
    static const char *const
        captured = "Captured :: behavior {\n"
                   "    sum: number = 0\n"
                   "    Twice :: (value: number) -> number {\n"
                   "        sum: number = 0\n"
                   "        count: int = 0\n"
                   "        while count < 2 { sum += value; count += 1 }\n"
                   "        return sum\n"
                   "    }\n"
                   "    on Tick :: () {\n"
                   "        threshold: number = 3\n"
                   "        matches := query(before: Spot, live: ^Spot, if "
                   "before.x > threshold, "
                   "amount := Twice(before.x))\n"
                   "        for row in matches { row.live.y = row.amount }\n"
                   "    }\n"
                   "}\n";
    static const char *const orders[][2] = {
        { "Padded", "Captured" },
        { "Captured", "Padded" },
    };
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        for (size_t o = 0; passed && o < sizeof orders / sizeof *orders; o++) {
            const char *sources[2];
            for (int i = 0; i < 2; i++) {
                sources[i] = !strcmp(orders[o][i], "Padded") ? padded : captured;
            }
            passed = query_sources_compose(b, orders[o], sources, 2, 4, 8);
        }
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
TEST_CASE(package_compiler_system_keeps_state_and_calls_pure_header_helper)
{
    static const char source
        [] = "Accumulate :: system(before: Spot, live: ^Spot, if before.x > 0, "
             "amount := Bump(before.x)) {\n"
             "    total: retain int = 0\n"
             "    Bump :: (value: number) -> number { return value + 1 }\n"
             "    Add :: (step: number) -> number { total += 1; return step }\n"
             "    step := Add(number(dt))\n"
             "    live.y += amount + step\n"
             "}\n";
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        execution_module *module = NULL;
        player p = { 0 };
        passed = query_stage_one_module(b, "Accumulate", source, &module) &&
                 query_play(module, &p);
        if (passed) {
            ecs_entity first = query_add_spot(&p, 2);
            ecs_entity second = query_add_spot(&p, 5);
            passed = ecs_entity_to_u64(first) && ecs_entity_to_u64(second) &&
                     query_step(&p, 0, .25) && query_step(&p, .25, .25) &&
                     query_spot_equals(&p, first, 2, 6.5f) &&
                     query_spot_equals(&p, second, 5, 12.5f);
            const pin_value *total = execution_instance_state(
                p.instance,
                script_catalog_id("symbol", "Accumulate:1:total")
            );
            passed = passed && total && total->type == PIN_I32 && total->i == 4;
        }
        stop(&p);
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
TEST_CASE(package_compiler_empty_system_runs_only_attached_owner)
{
    static const char
        source[] = "Empty :: system(/* empty */) { self.Spot.x += 1; "
                   "self.Spot.y += number(dt) }";
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        execution_module *module = NULL;
        player p = { 0 };
        passed = query_stage_one_module(b, "Empty", source, &module) &&
                 module->plan.system_description == 0 &&
                 module->plan.query == -1 && query_play(module, &p);
        if (passed) {
            ecs_entity other = query_add_spot(&p, 7);
            passed = ecs_entity_to_u64(other) && query_step(&p, 0, .25) &&
                     query_spot_equals(&p, p.owner, 1, .25f) &&
                     query_spot_equals(&p, other, 7, 0);
        }
        stop(&p);
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
TEST_CASE(package_compiler_query_generators_bind_target_components)
{
    static const char *const sources[] = {
        "Targets :: system(target: entity in self.Targets, before: Spot from "
        "target, "
        "live: ^Spot from target, if before.x > 0) { live.y += before.x }",
        "Targets :: behavior { on Tick :: () { for row in query("
        "target: entity in self.Targets, before: Spot from target, "
        "live: ^Spot from target, if before.x > 0) { row.live.y += "
        "row.before.x } } }",
    };
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        for (int sample = 0; passed && sample < 2; sample++) {
            execution_module *module = NULL;
            player p = { 0 };
            passed = query_stage_one_module(b, "Targets", sources[sample], &module) &&
                     query_play(module, &p);
            if (passed) {
                ecs_entity first = query_add_spot(&p, 2);
                ecs_entity second = query_add_spot(&p, 5);
                ecs_entity missing = ecs_world_create_entity(p.world);
                passed = ecs_entity_to_u64(first) && ecs_entity_to_u64(second) &&
                         ecs_entity_to_u64(missing) &&
                         ecs_world_record_add_pair(p.world, p.owner, QUERY_TARGETS, first, 0) ==
                             ECS_OK &&
                         ecs_world_record_add_pair(p.world, p.owner, QUERY_TARGETS, second, 0) ==
                             ECS_OK &&
                         ecs_world_record_add_pair(p.world, p.owner, QUERY_TARGETS, missing, 0) ==
                             ECS_OK &&
                         ecs_world_apply_commands(p.world) == ECS_OK;
                const pin_value delta = { .type = PIN_F32, .f = .25 };
                int status = FLOW_ERR_UNKNOWN_EVENT;
                if (passed && sample == 0) {
                    status = query_step(&p, 0, .25) ? FLOW_OK : FLOW_ERR_STATE;
                } else if (passed) {
                    status = execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, .25, 100000);
                }
                passed = passed && status == FLOW_OK &&
                         query_spot_equals(&p, p.owner, 0, 0) &&
                         query_spot_equals(&p, first, 2, 2) &&
                         query_spot_equals(&p, second, 5, 5);
            }
            stop(&p);
            execution_module_free(module);
        }
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
TEST_CASE(package_compiler_query_optional_guards_and_inferred_values)
{
    static const char source
        [] = "Optional :: behavior {\n"
             "    total: int = 0\n"
             "    on Tick :: () {\n"
             "        for row in query(point: ^Spot?, alias := point, if "
             "all(alias, "
             "any(alias.x > 0, false)), "
             "if any(true, 10 / int(alias.x - alias.x) > 0), "
             "copy := alias^, first := [int(copy.x)][0]) {\n"
             "            total += row.first\n"
             "            row.alias.y = row.copy.x\n"
             "        }\n"
             "        for row in query(point: Spot?, if point) { total += 100 "
             "}\n"
             "    }\n"
             "}\n";
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        execution_module *module = NULL;
        player p = { 0 };
        passed = query_stage_one_module(b, "Optional", source, &module) &&
                 query_play(module, &p);
        if (passed) {
            ecs_entity present = query_add_spot(&p, 7);
            ecs_entity absent = ecs_world_create_entity(p.world);
            const pin_value delta = { .type = PIN_F32, .f = .25 };
            passed = ecs_entity_to_u64(present) && ecs_entity_to_u64(absent) &&
                     ecs_world_apply_commands(p.world) == ECS_OK &&
                     execution_instance_dispatch(p.instance, TICK, &delta, 1, 0, .25, 100000) ==
                         FLOW_OK &&
                     query_spot_equals(&p, present, 7, 7);
            const pin_value *total = execution_instance_state(
                p.instance,
                script_catalog_id("symbol", "Optional:1:total")
            );
            passed = passed && total && total->type == PIN_I32 && total->i == 207;
        }
        stop(&p);
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
TEST_CASE(package_compiler_query_rejects_hidden_mutation_and_old_headers)
{
    static const char *const sources[] = {
        "Fixed :: system(point: ^Spot) { Read :: () -> number { return "
        "number(dt) }; "
        "point.y = Read() }",
        "Fixed :: system(target: Missing in self.Targets) {}",
        "Fixed :: system(amount: Missing = 1) {}",
        "Fixed :: system(point: Spot, amount := total) { total: retain int = 1 "
        "}",
        "Fixed :: system(point: Spot, amount := Read()) { total: retain int = "
        "1; "
        "Read :: () -> int { return total } }",
        "Fixed :: system(inner := query(point: Spot, if point.x > dt)) {}",
        "Fixed :: behavior { total: int = 1; on Tick :: () { "
        "matches := query(point: Spot, inner := query(other: Spot, "
        "if other.x > Read())) } Read :: () -> number { return number(total) } "
        "}",
        "Fixed :: behavior { total: int = 1; "
        "Read :: () -> int { for row in query(p: Spot, "
        "if p.x > number(total)) { return 1 }; return 0 } "
        "on Tick :: () { matches := query(point: Spot, amount := Read()) } }",
        "Fixed :: behavior { on Tick :: () { value := 1; "
        "matches := query(point: Spot, pointer := &value) } }",
        "Fixed :: behavior { total: int = 0; Read :: () -> int { return total "
        "}; "
        "on Tick :: () { matches := query(point: Spot, amount := Read()) } }",
        "Fixed :: system(point: Spot, amount := dt) {}",
        "Fixed :: system(point: Spot, dt := 1) {}",
        "Fixed :: system(point: ^Spot?, if point.x > 0 && point) {}",
        "Fixed :: behavior { on Tick :: () { matches := query(point: Spot); "
        "self.Spot.x = matches.point.x } }",
        "Fixed :: behavior { on Tick :: () { for row in query(point: Spot) { "
        "for other in row {} } } }",
        "Fixed :: system(point: Spot) on Tick {}",
        "Fixed :: system(point: Spot) without Frozen {}",
        "Fixed :: system(point: Spot) where point.x > 0 {}",
        "Fixed :: system(point: Spot) join target in self.Targets {}",
        "Fixed :: behavior { on Tick :: () { matches := "
        "query(point: Spot = self) } }",
    };
    REQUIRE(prepare());
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        for (size_t i = 0; passed && i < sizeof sources / sizeof *sources; i++) {
            compiler_result out;
            passed = query_compile_result(b, "Fixed", sources[i], &out);
            if (passed) {
                passed = !compiler_succeeded(&out);
                if (!passed) {
                    fprintf(stderr, "stage one accepted invalid query: %s\n", sources[i]);
                }
            }
            arena_free(&detached);
        }
    }
    REQUIRE(passed);
    return true;
}
/* This test-only source joins the compiler fixture's package, so its probe
 * can exercise private scope collection without adding a shipped export. */
static const char QUERY_SCOPE_PROBE[] =
    "package package_compiler\n"
    "import snapshot \"project:source_snapshot\"\n"
    "import raw \"project:compiler_syntax\"\n"
    "pub ScopeCaptures :: (source: string, instruction_names: string[], "
    "target: Target) -> string[] {\n"
    "    forest: snapshot.SnapshotNode[] = []; root: int = -1;\n"
    "    switch raw.Parse(source) {\n"
    "        case .Parsed(nodes, fields, is_named, is_extra, top):\n"
    "            root = top; index: int = 0;\n"
    "            while index < length(nodes) {\n"
    "                forest.append(snapshot.Node(nodes[index], fields[index], "
    "is_named[index], is_extra[index], 0));\n"
    "                index += 1;\n"
    "            }\n"
    "    }\n"
    "    document: snapshot.Document = {original_path: \"scope/Main.chibi\", "
    "path: \"scope/Main.chibi\",\n"
    "        directory: \"scope\", key: \"scope/Main.chibi\", identity: "
    "\"project:scope\", text: source, root: 0,\n"
    "        syntax_root: root, header_start: 0, header_end: 0, header_line: "
    "0, header_column: 0, header_document: 0,\n"
    "        node_start: 0, node_count: length(forest), first_import: 0, "
    "import_count: 0, has_key: true,\n"
    "        has_syntax: true, has_header: false};\n"
    "    switch DeclarePackage(forest, [document], [0], 0, \"scope\", [], "
    "target, [], [], [], [], []) {\n"
    "        case .Failed(diagnostic): return [format_text(\"ERROR {}\", "
    "diagnostic.message)];\n"
    "        case .Collected(callables, params, types, members, constants):\n"
    "            switch CollectQueries(forest, source, \"project:scope\", 0, "
    "0, length(forest), types, members, callables, params, [], [], [],\n"
    "                                  [], 0, target, "
    "ResolveOps(instruction_names, false)) {\n"
    "                case .Failed(diagnostic): return [format_text(\"ERROR "
    "{}\", diagnostic.message)];\n"
    "                case .Collected(more_types, more_members, more_callables, "
    "more_params, descriptions):\n"
    "                    captures: string[] = [];\n"
    "                    for description in descriptions {\n"
    "                        index: int = 0;\n"
    "                        while index < description.capture_count {\n"
    "                            param := "
    "more_params[description.first_capture + index];\n"
    "                            captures.append(format_text(\"{}:{}:{}\", "
    "param.name, param.storage, param.container));\n"
    "                            index += 1;\n"
    "                        }\n"
    "                        captures.append(\"|\");\n"
    "                    }\n"
    "                    return captures;\n"
    "            }\n"
    "    }\n"
    "}\n";
TEST_CASE(package_compiler_query_captures_lexical_bindings)
{
    static const char source
        [] = "package scope\n"
             "Choice :: union { Some(amount: int) | None }\n"
             "Probe :: () -> int {\n"
             "    threshold: f64 = 20; payload: f64 = 40;\n"
             "    for threshold in 0..<2 { captured := query(value := "
             "threshold); }\n"
             "    values: f64[] = [3, 7]; alias := values;\n"
             "    for value, index in alias { captured := query(v := value, n "
             ":= index); }\n"
             "    pairs: [string: int] = [\"a\": 3]; pair_alias := pairs;\n"
             "    for key, value in pair_alias { captured := query(k := key, v "
             ":= value); }\n"
             "    matches := query(amount := 1);\n"
             "    for row in matches { alias_row := row; captured := query(x "
             ":= row.amount, y := alias_row.amount); }\n"
             "    choice := Choice.Some(5);\n"
             "    if choice is .Some(payload) { captured := query(x := "
             "payload); }\n"
             "    while choice is .Some(loop_payload) { captured := query(x := "
             "loop_payload); break; }\n"
             "    switch choice { case .Some(payload): captured := query(x := "
             "payload); case .None: {} }\n"
             "    after := query(x := threshold, y := payload);\n"
             "    return 0;\n"
             "}\n";
    static const char *const expected[] = {
        "threshold:6:0",
        "|",
        "value:11:0",
        "index:6:0",
        "|",
        "key:13:0",
        "value:6:0",
        "|",
        "|",
        "row:20:0",
        "alias_row:20:0",
        "|",
        "payload:6:0",
        "|",
        "loop_payload:6:0",
        "|",
        "payload:6:0",
        "|",
        "threshold:11:0",
        "payload:11:0",
        "|",
    };
    const source_package_input sources[] = {
        PACKAGE_COMPILER_SOURCE_INPUTS,
        { "package_compiler/ScopeTest.chibi", QUERY_SCOPE_PROBE, sizeof QUERY_SCOPE_PROBE - 1 },
    };
    const native_definition_pack packs[] = {
        COMPILER_JSON_DEFINITION_PACK,
        COMPILER_SYNTAX_DEFINITION_PACK,
        CSTRING_PACK,
    };
    REQUIRE(
        compiler_fixture_prepare_packs(&fixture, sources, sizeof sources / sizeof *sources, packs, 3)
    );
    for (int b = 0; b < 2; b++) {
        pin_value compile[ARGUMENTS];
        REQUIRE(arguments(b, false, NULL, 0, compile));
        pin_value args[] = { text(source),
                             compile[TARGET_ARGUMENT + 2],
                             compile[TARGET_ARGUMENT] };
        pin_value out = { 0 };
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "ScopeCaptures", args, 3, COMPILE_BUDGET, &detached, &out)
        );
        const pin_array *captures = out.ptr;
        if (!captures || captures->count != sizeof expected / sizeof *expected) {
            fprintf(
                stderr,
                "query scope returned %d entries\n",
                captures ? captures->count : -1
            );
            for (int i = 0; captures && i < captures->count; i++) {
                fprintf(stderr, "  %s\n", pin_text(pin_array_get(captures, i)).bytes);
            }
        }
        REQUIRE(captures && captures->count == sizeof expected / sizeof *expected);
        for (int i = 0; i < captures->count; i++) {
            const pin_string found = pin_text(pin_array_get(captures, i));
            if (found.length != strlen(expected[i]) ||
                memcmp(found.bytes, expected[i], found.length)) {
                fprintf(
                    stderr,
                    "query scope %d: expected %s, got %s\n",
                    i,
                    expected[i],
                    found.bytes
                );
            }
            REQUIRE(
                found.length == strlen(expected[i]) &&
                !memcmp(found.bytes, expected[i], found.length)
            );
        }
        arena_free(&detached);
    }
    return true;
}
TEST_CASE(package_compiler_query_values_are_readonly)
{
    static const char *const sources[] = {
        "Fixed :: system(point: Spot) { point.x = 2 }",
        ("Fixed :: behavior { on Tick :: () { for row in query(point: Spot) { "
         "row.point.x = 2 } } }"),
        "Fixed :: system(point: ^Spot, amount := 2) { amount = 3 }",
        "Fixed :: system(point: ^Spot) { point = null }",
        "Fixed :: system(point: ^Spot) { point.w = 3 }",
    };
    REQUIRE(prepare());
    query_register_types();
    bool passed = true;
    for (int b = 0; passed && b < 2; b++) {
        for (size_t i = 0; passed && i < sizeof sources / sizeof *sources; i++) {
            compiler_result out;
            passed = query_compile_result(b, "Fixed", sources[i], &out);
            if (passed) {
                pin_value message = { 0 };
                passed = !compiler_succeeded(&out) &&
                         get(compiler_error(&out), "message", &message) &&
                         (strstr(pin_text(message).bytes, "writable") ||
                          strstr(pin_text(message).bytes, "read-only"));
                if (!passed) {
                    fprintf(stderr, "query readonly binding accepted: %s\n", sources[i]);
                }
            }
            arena_free(&detached);
        }
    }
    type_catalog_set_project(NULL);
    REQUIRE(passed);
    return true;
}
void test_language_package_compiler_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_compound_diagnostics_match_stage_zero);
    RUN_TEST_CASE(package_compiler_builds_runnable_packages);
    RUN_TEST_CASE(package_compiler_asserts_without_native_capability);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_types()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builtin_math_source_contracts);
    RUN_TEST_CASE(package_compiler_builds_runnable_typed_packages);
    RUN_TEST_CASE(package_compiler_rejects_invalid_char_conversions);
    RUN_TEST_CASE(package_compiler_rejects_deferred_char_forms);
    RUN_TEST_CASE(package_compiler_rejects_deferred_char_states);
    RUN_TEST_CASE(package_compiler_defers_generic_char_native_results);
    RUN_TEST_CASE(package_compiler_rejects_unsupported_numeric_conversions);
    RUN_TEST_CASE(package_compiler_resolves_library_primitive_names);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_math_native()
{
    pu_setup(NULL, cleanup);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_fallible()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_checks_fallible_source_results);
    RUN_TEST_CASE(package_compiler_checks_fallible_source_results_compiled);
    RUN_TEST_CASE(package_compiler_fallible_system_query_keeps_unsupported_boundary);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_imports()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_importing_packages);
    RUN_TEST_CASE(package_compiler_float_constant_dependency);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_compiled()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_packages_compiled);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_types_compiled()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_typed_packages_compiled);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_collections()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_collection_packages);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_collections_compiled()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_collection_packages_compiled);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_imports_compiled()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_importing_packages_compiled);
    RUN_TEST_CASE(package_compiler_float_constant_dependency_compiled);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_bootstrap()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_compiles_itself);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_literals()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_reads_float_literals);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_behaviors()
{
    pu_setup(NULL, cleanup);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_actions()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_exports_native_actions);
    RUN_TEST_CASE(package_compiler_keeps_unavailable_native_functions);
    RUN_TEST_CASE(package_compiler_rejects_nonzero_handle_defaults);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_behaviors_rest()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_keeps_text_state_per_owner);
    RUN_TEST_CASE(package_compiler_runs_task_sequencing_and_cancellation);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_queries()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_query_rows_capture_and_restart);
    RUN_TEST_CASE(package_compiler_query_nested_return_and_empty_iteration);
    RUN_TEST_CASE(package_compiler_query_header_constructs_captured_description);
    RUN_TEST_CASE(package_compiler_system_query_shares_snapshot_and_implicit_dt);
    RUN_TEST_CASE(package_compiler_queries_compose_in_any_source_order);
    RUN_TEST_CASE(package_compiler_query_captures_resolve_in_any_source_order);
    RUN_TEST_CASE(package_compiler_system_keeps_state_and_calls_pure_header_helper);
    RUN_TEST_CASE(package_compiler_empty_system_runs_only_attached_owner);
    RUN_TEST_CASE(package_compiler_query_optional_guards_and_inferred_values);
    RUN_TEST_CASE(package_compiler_query_generators_bind_target_components);
    RUN_TEST_CASE(package_compiler_query_rejects_hidden_mutation_and_old_headers);
    RUN_TEST_CASE(package_compiler_query_values_are_readonly);
    RUN_TEST_CASE(package_compiler_query_captures_lexical_bindings);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_sharing()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_shares_its_arguments);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_debug_free()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_omits_debug_info);
    RUN_TEST_CASE(package_compiler_drops_pushed_items);
    RUN_TEST_CASE(package_compiler_declares_records_in_linear_steps);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_scalar()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_scalar_rows_outlive_the_producer);
    RUN_TEST_CASE(package_compiler_native_emitter_checks_calls);
    RUN_TEST_CASE(package_compiler_scalar_results_require_the_fixture_value);
    RUN_TEST_CASE(package_compiler_scalar_rejects_unions_and_reads_libraries);
    pu_clear_setup();
}
#if defined(__linux__) && defined(__x86_64__)
int test_language_package_compiler_chibi_native_call(int count, char **arguments)
{
    return library_child(arguments[0], count - 1, arguments + 1);
}
#else
int test_language_package_compiler_chibi_native_call(int, char **)
{
    abort();
}
int test_language_stack_overflow_child(const char *)
{
    abort();
}
#endif
void test_language_package_compiler_chibi_native()
{
#if defined(__linux__) && defined(__x86_64__)
    /* The supervised children run in their own process groups, so a signal
     * to this one cancels them instead of leaving them running. */
    struct sigaction cancel = { .sa_handler = cancel_native }, interrupt, terminate;
    sigemptyset(&cancel.sa_mask);
    sigaction(SIGINT, &cancel, &interrupt);
    sigaction(SIGTERM, &cancel, &terminate);
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_bundle_starts_empty);
    RUN_TEST_CASE(stack_overflow_leaves_other_faults);
    RUN_TEST_CASE(package_compiler_native_runs_are_bounded);
    RUN_TEST_CASE(package_compiler_native_arithmetic_is_total);
    RUN_TEST_CASE(package_compiler_scalar_runs_natively_like_the_vm);
    pu_clear_setup();
    sigaction(SIGINT, &interrupt, NULL);
    sigaction(SIGTERM, &terminate, NULL);
#else
    fprintf(stderr, "native scalar execution runs on Linux x86-64; other targets run its bundle\n");
    abort();
#endif
}

void test_language_package_compiler_chibi_libraries()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_pure_provider_has_no_write);
    RUN_TEST_CASE(package_compiler_compiles_executable_libraries);
    RUN_TEST_CASE(package_compiler_links_executable_libraries);
    RUN_TEST_CASE(package_compiler_resolves_unqualified_libraries);
    RUN_TEST_CASE(package_compiler_resolves_native_canonical_names);
    RUN_TEST_CASE(package_compiler_resolves_library_names);
    RUN_TEST_CASE(package_compiler_resolves_library_array_updates);
    RUN_TEST_CASE(package_compiler_rejects_invalid_library_closures);
    RUN_TEST_CASE(package_compiler_folds_constants_as_stage_zero_does);
    pu_clear_setup();
}

/* Fixed primitive-native argument association, checked against literal
 * results and the independent stage-zero reference. */
static const sample native_argument_samples[] = {
    { .name = "native positional control",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(\"abcd\", 1, 2); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native names in declaration order",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(Value: "
                "\"abcd\", Start: 1, Count: 2); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native reordered names",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(Count: "
                "2, Value: \"abcd\", Start: 1); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native mixed arguments",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(\"abcd\", Count: 2, Start: 1); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native unary name",
      .source = "package facts\nRun :: () -> int { return "
                "text_byte_length(Value: \"abcd\"); }",
      .calls = { { "Run", "[]", "[4]", NULL } } },
    { .name = "native loose label match",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(count: "
                "2, value: \"abcd\", start: 1); }",
      .calls = { { "Run", "[]", "[\"bc\"]", NULL } } },
    { .name = "native arguments evaluate in source order",
      .source = "package facts\n"
                "Number :: (order: ^int, digit: int, value: int) -> int { "
                "order^ = order^ * 10 + digit; return value; }\n"
                "Text :: (order: ^int) -> string { order^ = order^ * 10 + 2; "
                "return \"abcd\"; }\n"
                "Run :: () -> int {\n"
                "  order: int = 0;\n"
                "  sliced := text_slice(Count: Number(&order, 3, 2), Value: "
                "Text(&order), Start: Number(&order, 1, 1));\n"
                "  if (!text_equal(sliced, \"bc\")) { return -1; }\n"
                "  return order;\n}",
      .calls = { { "Run", "[]", "[321]", NULL } } },
    { .name = "native unknown name",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(Value: "
                "\"abcd\", Start: 1, Missing: 2); }",
      .message = "Unknown argument 'Missing'",
      .line = 2,
      .column = 66 },
    { .name = "native duplicate name",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(Value: "
                "\"abcd\", Start: 1, Start: 2, Count: 2); }",
      .message = "Argument supplied more than once",
      .line = 2,
      .column = 66 },
    { .name = "native positional named duplicate",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(\"abcd\", Value: \"efgh\", Count: 2); }",
      .message = "Argument supplied more than once",
      .line = 2,
      .column = 49 },
    { .name = "native named CString expression location",
      .source = "package facts\nRun :: () -> int { return "
                "cstring_probe(value: "
                "(\"a\\u0000b\")); }",
      .c_string_probe = true,
      .message = "C-string boundary 'value' cannot accept NUL at byte 1",
      .line = 2,
      .column = 48 },
    { .name = "native named positional duplicate",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(Start: "
                "1, 2, Value: \"abcd\"); }",
      .message = "Argument supplied more than once",
      .line = 2,
      .column = 51 },
    { .name = "native reordered CString expression location",
      .source = "package facts\nRun :: () -> int { return "
                "cstring_pair(value: "
                "(\"a\\u0000b\"), prefix: 1); }",
      .c_string_probe = true,
      .message = "C-string boundary 'value' cannot accept NUL at byte 1",
      .line = 2,
      .column = 47 },
    { .name = "native extra argument",
      .source = "package facts\nRun :: () -> string { return "
                "text_slice(\"abcd\", 1, 2, 3); }",
      .message = "Too many arguments",
      .line = 2,
      .column = 55 },
};

TEST_CASE(native_arguments_reference_contract)
{
    /* Every valid call supplies all parameters of the existing text natives. */
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(&native_argument_samples[0], inputs);
    native_definition_pack packs[] = { CSTRING_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, inputs, count, packs, 1));
    for (int i = 0;
         i < (int)(sizeof native_argument_samples / sizeof *native_argument_samples);
         i++) {
        const sample *s = &native_argument_samples[i];
        fprintf(stderr, "native arguments reference: %s\n", s->name);
        if (s->message) {
            REQUIRE(rejects_like_oracle(s, s->message, s->line, s->column));
            continue;
        }
        unsigned char *bytes = NULL;
        size_t length = 0;
        REQUIRE(oracle_closure(s, &bytes, &length));
        bool passed = run(bytes, length, &s->calls[0], 0) &&
                      run(bytes, length, &s->calls[0], 1);
        free(bytes);
        REQUIRE(passed);
    }
    return true;
}

/* Stage zero picks native.overload's integer or text overload by the
 * argument; an untyped number never converts to text, so it cannot pick the
 * text one. native.kernel's number overload takes literals and typed values
 * other than i64, which takes the f64 one. */
TEST_CASE(native_overload_reference_contract)
{
    static const struct
    {
        const char *source;
        pin_type parameter;
        const char *message;
        int column;
    } cases[] = {
        { "package facts\nRun :: () -> int { return native.overload(2); }", PIN_I32, NULL, 0 },
        { "package facts\nRun :: () -> int { return "
          "native.overload((-(2))); }",
          PIN_I32,
          NULL,
          0 },
        { "package facts\nRun :: () -> int { return native.overload(2.0); "
          "}",
          PIN_I32,
          NULL,
          0 },
        { "package facts\nRun :: () -> int { return native.overload(1e0); "
          "}",
          PIN_I32,
          NULL,
          0 },
        { "package facts\nRun :: () -> int { return native.kernel(2); }", PIN_F32, NULL, 0 },
        { "package facts\nRun :: (v: int) -> int { return native.kernel(v "
          "* "
          "2); }",
          PIN_F32,
          NULL,
          0 },
        { "package facts\nRun :: (v: i64) -> int { return native.kernel(v "
          "* "
          "2); }",
          PIN_F64,
          NULL,
          0 },
        { "package facts\nRun :: (v: f64) -> int { return "
          "native.kernel(v); }",
          PIN_F64,
          NULL,
          0 },
        { "package facts\nRun :: (v: int) -> int { return "
          "native.overload(v); "
          "}",
          PIN_I32,
          NULL,
          0 },
        { "package facts\nRun :: () -> int { return "
          "native.overload(\"three\"); }",
          PIN_STR,
          NULL,
          0 },
        { "package facts\nRun :: () -> int { return native.overload(2.5); "
          "}",
          PIN_FLOW,
          "Numeric literal has a fraction, which int cannot hold",
          43 },
        { "package facts\nRun :: () -> int { return "
          "native.overload(4294967296); }",
          PIN_FLOW,
          "Numeric literal is outside the range of int",
          43 },
    };
    native_definition_pack packs[] = { CSTRING_PACK };
    source_package_input inputs[SAMPLE_INPUTS];
    sample first = { .source = cases[0].source };
    int count = sample_inputs(&first, inputs);
    REQUIRE(compiler_fixture_prepare_packs(&fixture, inputs, count, packs, 1));
    const script_catalog *catalog = native_capabilities_catalog(fixture.native);
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        sample s = { .source = cases[i].source };
        const authoring_package *root;
        authoring_packages *packages = oracle_packages(&s, &root);
        if (cases[i].message) {
            const script_diagnostic *d = root->diagnostic_count
                                           ? &root->diagnostics[0]
                                           : NULL;
            if (d) {
                fprintf(
                    stderr,
                    "native overload %zu: %s at %d:%d\n",
                    i,
                    d->message,
                    d->range.line,
                    d->range.column
                );
            }
            bool same = d && !strcmp(d->message, cases[i].message) &&
                        d->range.line == 2 && d->range.column == cases[i].column;
            authoring_packages_free(packages);
            REQUIRE(same);
            continue;
        }
        const script_analysis *analysis = root->analysis;
        int chosen = 0;
        for (int e = 0; e < analysis->expression_count; e++) {
            const script_expression *expression = &analysis->expressions[e];
            const script_catalog_capability *capability = expression->kind == SCRIPT_EXPR_CALL
                                                            ? script_catalog_capability_by_id(
                                                                  catalog,
                                                                  expression->capability
                                                              )
                                                            : NULL;
            if (capability && capability->parameter_count == 1 &&
                capability->parameters[0].type.storage == cases[i].parameter) {
                chosen++;
            }
        }
        bool valid = root->valid && chosen == 1;
        if (!valid) {
            fprintf(
                stderr,
                "native overload %zu: %s\n",
                i,
                root->diagnostic_count ? root->diagnostics[0].message : "other overload"
            );
        }
        authoring_packages_free(packages);
        REQUIRE(valid);
    }
    return true;
}
TEST_CASE(native_arguments_authored_contract)
{
    REQUIRE(prepare());
    bool all = true;
    for (int b = 0; b < 2; b++) {
        for (int i = 0;
             i < (int)(sizeof native_argument_samples / sizeof *native_argument_samples);
             i++) {
            const sample *s = &native_argument_samples[i];
            source_package_input inputs[SAMPLE_INPUTS];
            int count = sample_inputs(s, inputs);
            compiler_result out;
            REQUIRE(
                compile(b, false, s->c_string_probe, false, inputs, count, COMPILE_BUDGET, &out)
            );
            bool passed = false;
            if (!compiler_succeeded(&out)) {
                pin_value message, line, column;
                REQUIRE(get(compiler_error(&out), "message", &message));
                REQUIRE(get(compiler_error(&out), "line", &line));
                REQUIRE(get(compiler_error(&out), "column", &column));
                passed = s->message && !strcmp(s->message, pin_text(message).bytes) &&
                         line.i == s->line && column.i == s->column;
                fprintf(
                    stderr,
                    "native arguments authored %d: %s: %s at %d:%d (%s)\n",
                    b,
                    s->name,
                    pin_text(message).bytes,
                    line.i,
                    column.i,
                    passed ? "expected" : "MISMATCH"
                );
            } else {
                size_t length = 0;
                unsigned char *bytes = closure_bytes(&out, &length);
                unsigned char *expected = NULL;
                size_t expected_length = 0;
                passed = !s->message &&
                         (b || (oracle_closure(s, &expected, &expected_length) &&
                                points_like_oracle(bytes, length, false))) &&
                         run(bytes, length, &s->calls[0], 0) &&
                         run(bytes, length, &s->calls[0], 1);
                free(expected);
                free(bytes);
                /* run() leaves a failed invocation for cleanup; continue the
                 * remaining fixtures without overwriting that ownership. */
                compiler_host_free(host);
                host = NULL;
                free(result.json);
                result = (compiler_host_result){ 0 };
                fprintf(
                    stderr,
                    "native arguments authored %d: %s: compiled (%s)\n",
                    b,
                    s->name,
                    passed ? "expected" : "MISMATCH"
                );
            }
            arena_free(&detached);
            all = passed && all;
        }
    }
    REQUIRE(all);
    return true;
}

void test_language_package_compiler_chibi_native_arguments_oracle()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_arguments_reference_contract);
    RUN_TEST_CASE(native_overload_reference_contract);
    pu_clear_setup();
}
void test_language_package_compiler_chibi_native_arguments()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_arguments_authored_contract);
    pu_clear_setup();
}

TEST_CASE(package_compiler_compositional_records)
{
    REQUIRE(prepare());
    /* The sample host names its root package facts. Change only the package
     * declaration so both stages execute the authored fixture's assertions. */
    const char *body = strchr(COMPOSITIONAL_RECORDS_SOURCE, '\n');
    REQUIRE(body);
    char source[sizeof COMPOSITIONAL_RECORDS_SOURCE + 16];
    int used = snprintf(source, sizeof source, "package facts\n%s", body + 1);
    REQUIRE(used > 0 && (size_t)used < sizeof source);
    const sample s = { .name = "compositional records", .source = source, .game = true };
    source_package_input inputs[SAMPLE_INPUTS];
    int count = sample_inputs(&s, inputs);
    const sample_call call = { .function = "Main", .arguments = "[]", .expected = "[0]" };
    for (int b = 0; b < 2; b++) {
        compiler_result out;
        REQUIRE(compile(b, true, false, false, inputs, count, COMPILE_BUDGET, &out));
        if (!compiler_succeeded(&out)) {
            pin_value message, line, column;
            REQUIRE(get(compiler_error(&out), "message", &message));
            REQUIRE(get(compiler_error(&out), "line", &line));
            REQUIRE(get(compiler_error(&out), "column", &column));
            fprintf(
                stderr,
                "compositional stage one: %s at %d:%d\n",
                pin_text(message).bytes,
                line.i,
                column.i
            );
        }
        REQUIRE(compiler_succeeded(&out));
        size_t length;
        unsigned char *bytes = closure_bytes(&out, &length);
        arena_free(&detached);
        bool passed = true;
        for (int backend = 0; passed && backend < 2; backend++) {
            passed = run(bytes, length, &call, backend);
        }
        free(bytes);
        REQUIRE(passed);
        const source_package_input rejected = {
            "facts/Main.chibi",
            COMPOSITIONAL_RECORD_ALIAS_REJECTION,
            strlen(COMPOSITIONAL_RECORD_ALIAS_REJECTION)
        };
        REQUIRE(compile(b, false, false, false, &rejected, 1, COMPILE_BUDGET, &out));
        REQUIRE(!compiler_succeeded(&out));
        pin_value message;
        REQUIRE(get(compiler_error(&out), "message", &message));
        if (!strstr(pin_text(message).bytes, "Nullable pointer requires a presence guard")) {
            fprintf(
                stderr,
                "compositional stage-one alias rejection: %s\n",
                pin_text(message).bytes
            );
        }
        REQUIRE(strstr(pin_text(message).bytes, "Nullable pointer requires a presence guard"));
        arena_free(&detached);
        char depth_source[4096];
        for (int arrays = 0; arrays < 2; arrays++) {
            for (int depth = 16; depth <= 17; depth++) {
                REQUIRE(
                    compositional_records_depth_source(depth_source, sizeof depth_source, depth, arrays)
                );
                const source_package_input input = { "facts/Main.chibi",
                                                     depth_source,
                                                     strlen(depth_source) };
                REQUIRE(compile(b, false, false, false, &input, 1, COMPILE_BUDGET, &out));
                bool expected = depth == 16;
                if (compiler_succeeded(&out) != expected) {
                    fprintf(stderr, "compositional stage-one depth: backend=%d arrays=%d depth=%d\n", b, arrays, depth);
                    if (!compiler_succeeded(&out)) {
                        REQUIRE(get(compiler_error(&out), "message", &message));
                        fprintf(stderr, "%s\n", pin_text(message).bytes);
                    }
                }
                REQUIRE(compiler_succeeded(&out) == expected);
                arena_free(&detached);
            }
        }
        REQUIRE(compositional_records_fanout_source(depth_source, sizeof depth_source));
        const source_package_input fanout = { "facts/Main.chibi",
                                              depth_source,
                                              strlen(depth_source) };
        REQUIRE(compile(b, false, false, false, &fanout, 1, COMPILE_BUDGET, &out));
        REQUIRE(!compiler_succeeded(&out));
        REQUIRE(get(compiler_error(&out), "message", &message));
        REQUIRE(strstr(pin_text(message).bytes, "metadata work"));
        arena_free(&detached);
    }
    return true;
}

void test_language_package_compiler_chibi_records()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_compositional_records);
    pu_clear_setup();
}
