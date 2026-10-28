#include "authoring/native_bindings.h"
#include "backend.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "runtime/arrays.h"
#include "runtime/math_native.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/text.h"
#include "types/trace.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
    ELEMENTS = 64,
    TRIALS = 16
};
static arena producer, storage;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static native_capabilities *natives;
static behavior_function_context context;
static behavior_function_activation activation;

static void cleanup()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    execution_module_free(module);
    native_capabilities_free(natives);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    arena_free(&producer);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    module = NULL;
    natives = NULL;
    analysis = NULL;
    syntax = NULL;
}
static pin_value scalar(pin_type type, int n)
{
    pin_value value = { .type = type };
    switch (type) {
        case PIN_BOOL: value.b = n != 0; break;
        case PIN_I8: value.i8 = n; break;
        case PIN_U8: value.u8 = n; break;
        case PIN_I16: value.i16 = n; break;
        case PIN_U16: value.u16 = n; break;
        case PIN_I32: value.i = n; break;
        case PIN_U32: value.u = n; break;
        case PIN_I64: value.i64 = n; break;
        case PIN_U64:
        case PIN_HANDLE: value.u64 = n; break;
        case PIN_F32: value.f = n; break;
        case PIN_F64: value.d = n; break;
        case PIN_ENUM: value.e = n; break;
        default: assert(false);
    }
    return value;
}
TEST_CASE(packed_scalars_preserve_values_widths_and_snapshot_ownership)
{
    const pin_type types[] = { PIN_BOOL, PIN_I8,     PIN_U8,  PIN_I16, PIN_U16,
                               PIN_I32,  PIN_U32,    PIN_I64, PIN_U64, PIN_F32,
                               PIN_F64,  PIN_HANDLE, PIN_ENUM };
    arena_init_collectable(&storage);
    for (size_t t = 0; t < sizeof types / sizeof *types; t++) {
        pin_value values[] = { scalar(types[t], 1),
                               scalar(types[t], 2),
                               scalar(types[t], 3) };
        const char *name = types[t] == PIN_HANDLE ? "ecs.entity"
                         : types[t] == PIN_ENUM   ? "PackedChoice"
                                                  : NULL;
        for (int i = 0; i < 3; i++) {
            values[i].type_name = name;
        }
        pin_array source = { .element_type = types[t],
                             .type_name = name,
                             .count = 3,
                             .capacity = 3,
                             .items = values };
        pin_array *copy = pin_array_snapshot_to_arena(&storage, &source);
        REQUIRE(copy && pin_array_valid(copy) && !copy->items);
        REQUIRE(copy->stride == behavior_scalar_size(types[t]));
        REQUIRE(pin_array_storage_bytes(copy) == 3 * behavior_scalar_size(types[t]));
        pin_value expected = values[0];
        values[0] = scalar(types[t], 9);
        pin_value first = pin_array_get(copy, 0);
        REQUIRE(pin_value_equal(&first, &expected));
        pin_array_swap(copy, 0, 2);
        first = pin_array_get(copy, 2);
        REQUIRE(pin_value_equal(&first, &expected));
        arena_collection collection;
        arena *arenas[] = { &storage };
        arena_collect_begin(&collection, arenas, 1);
        arena_collect_mark(&collection, copy);
        arena_collect_end(&collection);
        first = pin_array_get(copy, 2);
        REQUIRE(pin_value_equal(&first, &expected));
    }
    return true;
}
TEST_CASE(packed_record_updates_box_compatible_null_elements)
{
    const type_field field = { .name = "count", .type = PIN_I32 };
    const pin_record *record = pin_record_create(&producer, "NullableRow", 419, &field, 1);
    REQUIRE(record);
    int32_t count = 23;
    pin_value row = { .type = PIN_STRUCT,
                      .type_name = record->type.name,
                      .record = record,
                      .ptr = &count };
    pin_array source = { .element_type = PIN_STRUCT,
                         .type_name = record->type.name,
                         .record = record,
                         .count = 1,
                         .capacity = 1,
                         .items = &row };
    arena_init_collectable(&storage);
    pin_array *packed = pin_array_snapshot_to_arena(&storage, &source);
    REQUIRE(packed && packed->stride);
    pin_value array = { .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = packed->type_name,
                        .record = packed->record,
                        .ptr = packed };
    pin_value null = row;
    null.ptr = NULL;
    pin_value add[] = { array, null };
    pin_value update[] = { array, { .type = PIN_I32 }, null };
    pin_value results[] = { native_array_add(add, 2, &storage),
                            native_array_insert(update, 3, &storage),
                            native_array_set_elem(update, 3, &storage) };
    arena_free(&producer);
    arena_collection collection;
    arena *arenas[] = { &storage };
    arena_collect_begin(&collection, arenas, 1);
    for (int i = 0; i < 3; i++) {
        pin_value_trace(&results[i], arena_collect_mark, &collection);
    }
    arena_collect_end(&collection);
    for (int i = 0; i < 3; i++) {
        const pin_array *result = results[i].ptr;
        REQUIRE(result && pin_array_valid(result) && !result->stride);
        REQUIRE(result->count == (i == 2 ? 1 : 2));
        pin_value item = pin_array_get(result, i == 0 ? 1 : 0);
        REQUIRE(item.type == PIN_STRUCT && !item.ptr);
        if (i != 2) {
            item = pin_array_get(result, i == 0 ? 0 : 1);
            pin_value value;
            {
                const char *error = pin_read_field(
                    &producer,
                    item.ptr,
                    &item.record->type.fields[0],
                    &value
                );
                if (error) {
                    abort();
                }
            };
            REQUIRE(value.i == 23);
        }
    }
    return true;
}
/* A slice's copied elements alone decide its representation, read from the
 * right offset of a packed or boxed source. */
TEST_CASE(packed_slices_take_their_representation_from_their_range)
{
    pin_value items[8];
    for (int i = 0; i < 8; i++) {
        items[i] = scalar(PIN_I32, i + 1);
    }
    items[0] = scalar(PIN_I64, 1); /* Outside the sliced range. */
    pin_array boxed = { .element_type = PIN_I32, .count = 8, .capacity = 8, .items = items };
    pin_value header = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = &boxed };
    arena_init_collectable(&storage);
    pin_value inside[] = { header, scalar(PIN_I32, 2), scalar(PIN_I32, 3) };
    pin_value slice = native_array_slice(inside, 3, &storage);
    const pin_array *copy = slice.ptr;
    REQUIRE(copy && copy->stride == sizeof(int32_t) && copy->count == 3);
    for (int i = 0; i < 3; i++) {
        REQUIRE(pin_array_get(copy, i).i == i + 3);
    }
    pin_value packed_slice[] = { slice, scalar(PIN_I32, 1), scalar(PIN_I32, 5) };
    const pin_array *tail = native_array_slice(packed_slice, 3, &storage).ptr;
    REQUIRE(tail && tail->stride && tail->count == 2);
    REQUIRE(pin_array_get(tail, 0).i == 4 && pin_array_get(tail, 1).i == 5);
    pin_value across[] = { header, scalar(PIN_I32, 0), scalar(PIN_I32, 2) };
    copy = native_array_slice(across, 3, &storage).ptr;
    REQUIRE(copy && !copy->stride && copy->count == 2);
    REQUIRE(copy->items[0].type == PIN_I64 && copy->items[1].i == 2);
    return true;
}
TEST_CASE(packed_records_own_layouts_and_keep_nullable_and_managed_fallbacks)
{
    const type_field fields[] = {
        { .name = "x", .type = PIN_F32 },
        { .name = "y", .type = PIN_F32 },
        { .name = "count", .type = PIN_I64 },
        { .name = "owner", .type = PIN_HANDLE, .type_name = "ecs.entity" }
    };
    const pin_record *record = pin_record_create(&producer, "PackedRow", 401, fields, 4);
    REQUIRE(record && pin_record_valid(record));
    pin_value values[2];
    for (int i = 0; i < 2; i++) {
        values[i] = (pin_value){ .type = PIN_STRUCT,
                                 .type_name = record->type.name,
                                 .record = record,
                                 .ptr = arena_alloc(&producer, record->type.size) };
        memset(values[i].ptr, 0, record->type.size);
        for (int f = 0; f < 4; f++) {
            pin_value value = scalar(fields[f].type, i + f + 1);
            value.type_name = fields[f].type_name;
            {
                const char *error = pin_write_field(
                    &producer,
                    values[i].ptr,
                    &record->type.fields[f],
                    value
                );
                if (error) {
                    abort();
                }
            };
        }
    }
    pin_array source = { .element_type = PIN_STRUCT,
                         .type_name = record->type.name,
                         .record = record,
                         .count = 2,
                         .capacity = 2,
                         .items = values };
    arena_init_collectable(&storage);
    pin_array *packed = pin_array_snapshot_to_arena(&storage, &source);
    REQUIRE(packed && packed->stride == record->type.size && !packed->items);
    values[1].ptr = NULL;
    pin_array *nullable = pin_array_snapshot_to_arena(&storage, &source);
    REQUIRE(nullable && !nullable->stride && nullable->items);
    const type_field text_field = { .name = "text", .type = PIN_STR };
    const pin_record *managed = pin_record_create(&producer, "ManagedRow", 402, &text_field, 1);
    REQUIRE(managed);
    pin_value text = { .type = PIN_STRUCT,
                       .type_name = managed->type.name,
                       .record = managed,
                       .ptr = arena_alloc(&producer, managed->type.size) };
    {
        const char *error = pin_write_field(
            &producer,
            text.ptr,
            &managed->type.fields[0],
            (pin_value){ .type = PIN_STR,
                         .text = &(const pin_string){ .bytes = "owned",
                                                      .length = sizeof("owned") - 1 } }
        );
        if (error) {
            abort();
        }
    };
    pin_array texts = { .element_type = PIN_STRUCT,
                        .type_name = managed->type.name,
                        .record = managed,
                        .count = 1,
                        .capacity = 1,
                        .items = &text };
    pin_array *kept = pin_array_snapshot_to_arena(&storage, &texts);
    REQUIRE(kept && !kept->stride);
    arena_free(&producer);
    arena_collection collection;
    arena *arenas[] = { &storage };
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_mark(&collection, packed);
    arena_collect_mark(&collection, nullable);
    arena_collect_mark(&collection, kept);
    arena_collect_end(&collection);
    pin_value row = pin_array_get(packed, 1), member;
    {
        const char *error = pin_read_field(&producer, row.ptr, &row.record->type.fields[2], &member);
        if (error) {
            abort();
        }
    };
    REQUIRE(member.type == PIN_I64 && member.i64 == 4);
    row = pin_array_get(nullable, 1);
    REQUIRE(!row.ptr && pin_record_valid(row.record));
    row = pin_array_get(kept, 0);
    {
        const char *error = pin_read_field(&producer, row.ptr, &row.record->type.fields[0], &member);
        if (error) {
            abort();
        }
    };
    REQUIRE(member.type == PIN_STR && !strcmp(pin_text(member).bytes, "owned"));
    return true;
}
static double now()
{
    struct timespec time;
    int status = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(!status);
    return time.tv_sec + time.tv_nsec * 1e-9;
}
static const behavior_function *compile_source(const char *source, behavior_backend backend)
{
    char error[256];
    const script_catalog empty = { 0 };
    const script_catalog *catalog = natives ? native_capabilities_catalog(natives)
                                            : &empty;
    syntax = script_parse(source);
    analysis = script_analyze(syntax, catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "packed semantic: %s\n", analysis->diagnostics[i].message);
    }
    if (!analysis->valid) {
        return NULL;
    }
    const script_lower_bindings bindings = natives
                                             ? authoring_native_bindings(natives)
                                             : (script_lower_bindings){ 0 };
    execution_module_plan *plan = script_lower(analysis, catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "packed lower: %s\n", plan->diagnostics[i].message);
    }
    if (!plan || !plan->valid) {
        execution_module_plan_free(plan);
        return NULL;
    }
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "packed publish: %s\n", error);
        return NULL;
    }
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info function = execution_module_function_at(module, i);
        if (execution_module_function_count(module) == 1) {
            return function.body;
        }
        for (int j = 0; j < analysis->function_count; j++) {
            int symbol = analysis->functions[j].symbol;
            if (symbol >= 0 && analysis->symbols[symbol].id == function.id &&
                !strcmp(analysis->symbols[symbol].name, "Fold")) {
                return function.body;
            }
        }
    }
    return NULL;
}
static const behavior_function *compile(pin_type type, behavior_backend backend)
{
    char source[384];
    const char *name = script_scalar_name(type);
    snprintf(
        source,
        sizeof source,
        "Packed :: library { Sum :: (values:%s[]) -> %s { total:%s=0; "
        "for value in values { total += value; } return total; } }",
        name,
        name,
        name
    );
    return compile_source(source, backend);
}
static bool integer_case(
    pin_type type,
    behavior_backend backend,
    const char *operation,
    uint64_t left,
    uint64_t right,
    uint64_t expected,
    bool comparison
)
{
    char source[384];
    const char *name = script_scalar_name(type);
    snprintf(
        source,
        sizeof source,
        "Packed :: library { Edge :: (a:%s,b:%s) -> %s { return a %s b; } }",
        name,
        name,
        comparison ? "bool" : name,
        operation
    );
    const behavior_function *function = compile_source(source, backend);
    if (!function) {
        return false;
    }
    pin_value arguments[] = { behavior_scalar_from_bits(type, left),
                              behavior_scalar_from_bits(type, right) };
    pin_value result = { .type = comparison ? PIN_BOOL : type };
    context = (behavior_function_context){ .arguments = arguments,
                                           .argument_count = 2,
                                           .results = &result,
                                           .activation = &activation,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    if (!behavior_function_bind(function, &context, NULL, 0)) {
        return false;
    }
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    bool valid = status == FLOW_OK &&
                 (comparison
                      ? result.b == (expected != 0)
                      : behavior_scalar_bits(result) ==
                            behavior_scalar_bits(behavior_scalar_from_bits(type, expected)));
    if (!valid) {
        fprintf(
            stderr,
            "integer edge backend=%s type=%s op=%s status=%d\n",
            behavior_backend_name(backend),
            name,
            operation,
            status
        );
    }
    cleanup();
    return valid;
}
TEST_CASE(typed_integer_edges_match_vm_and_compiled_backends)
{
    const pin_type types[] = { PIN_I8,  PIN_U8,  PIN_I16, PIN_U16,
                               PIN_I32, PIN_U32, PIN_I64, PIN_U64 };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (size_t t = 0; t < sizeof types / sizeof *types; t++) {
            pin_type type = types[t];
            unsigned bits = (unsigned)behavior_scalar_size(type) * 8;
            uint64_t mask = UINT64_MAX >> (64 - bits);
            uint64_t high = UINT64_C(1) << (bits - 1);
            bool sign = behavior_scalar_signed(type);
            REQUIRE(integer_case(type, backends[b], "+", mask, 1, 0, false));
            REQUIRE(integer_case(type, backends[b], "-", 0, 1, mask, false));
            REQUIRE(integer_case(type, backends[b], "*", high + 1, 3, high + 3, false));
            REQUIRE(integer_case(type, backends[b], "<", high, 1, sign, true));
            REQUIRE(
                integer_case(type, backends[b], "/", mask - 1, 2, sign ? mask : (mask - 1) / 2, false)
            );
            REQUIRE(integer_case(type, backends[b], "%", mask, 2, sign ? mask : 1, false));
            /* x / 0 is 0 and x % 0 is x. */
            REQUIRE(integer_case(type, backends[b], "/", high, 0, 0, false));
            REQUIRE(integer_case(type, backends[b], "%", high, 0, high, false));
            if (sign) {
                /* The minimum / -1 wraps to the minimum, with remainder 0. */
                REQUIRE(integer_case(type, backends[b], "/", high, mask, high, false));
                REQUIRE(integer_case(type, backends[b], "%", high, mask, 0, false));
            }
        }
    }
    return true;
}
TEST_CASE(typed_integer_large_constant_folding_is_exact)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        const behavior_function *function = compile_source(
            "Packed :: library { Inner :: (a:u64,b:u64) -> u64 { "
            "return (a+b)*3; } Fold :: () -> u64 { "
            "return Inner(9007199254740993,9007199254740991); } }",
            backends[b]
        );
        REQUIRE(function);
        /* Authored calls suspend the parent while the callee executes. Their
         * public invocation contract requires saved activation values. */
        behavior_function_activation_init(function, &activation);
        pin_value result = { .type = PIN_U64 };
        context = (behavior_function_context){ .results = &result,
                                               .activation = &activation,
                                               .budget = 10000 };
        behavior_function_bind_storage(&context, &storage);
        REQUIRE(behavior_function_bind(function, &context, NULL, 0));
        REQUIRE(behavior_function_run(function, &context, b != 0) == FLOW_OK);
        REQUIRE(result.u64 == UINT64_C(54043195528445952));
        cleanup();
    }
    return true;
}
TEST_CASE(typed_compile_time_values_preserve_width_and_float_remainder)
{
    const struct
    {
        pin_type type;
        const char *maximum;
        uint64_t expected;
    } cases[] = { { PIN_I8, "127", UINT64_C(128) },
                  { PIN_U8, "255", 0 },
                  { PIN_I16, "32767", UINT64_C(32768) },
                  { PIN_U16, "65535", 0 },
                  { PIN_I32, "2147483647", UINT64_C(2147483648) },
                  { PIN_U32, "4294967295", 0 },
                  { PIN_I64, "9223372036854775807", UINT64_C(9223372036854775808) },
                  { PIN_U64, "18446744073709551615", 0 },
                  { PIN_F32, NULL, 0 },
                  { PIN_F64, NULL, 0 } };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
            char source[384];
            pin_type type = cases[c].type;
            const char *name = script_scalar_name(type);
            if (type == PIN_F32 || type == PIN_F64) {
                snprintf(
                    source,
                    sizeof source,
                    "Packed :: library { Wrap :: () -> %s { return "
                    "%s(5.5) %% %s(2); } Value :: Wrap(); "
                    "Fold :: () -> %s { return Value; } }",
                    name,
                    name,
                    name,
                    name
                );
            } else {
                snprintf(
                    source,
                    sizeof source,
                    "Packed :: library { Wrap :: () -> %s { x: %s = %s; "
                    "return x + %s(1); } Value :: Wrap(); "
                    "Fold :: () -> %s { return Value; } }",
                    name,
                    name,
                    cases[c].maximum,
                    name,
                    name
                );
            }
            const behavior_function *function = compile_source(source, backends[b]);
            REQUIRE(function);
            pin_value expected = type == PIN_F32
                                   ? (pin_value){ .type = PIN_F32, .f = 1.5f }
                               : type == PIN_F64
                                   ? (pin_value){ .type = PIN_F64, .d = 1.5 }
                                   : behavior_scalar_from_bits(type, cases[c].expected);
            bool found = false;
            for (int i = 0; i < analysis->symbol_count; i++) {
                const script_symbol *symbol = &analysis->symbols[i];
                if (symbol->kind == SCRIPT_CONSTANT &&
                    symbol->initializer >= 0 && !strcmp(symbol->name, "Value")) {
                    REQUIRE(analysis->expressions[symbol->initializer].kind == SCRIPT_EXPR_CONSTANT);
                    REQUIRE(pin_value_equal(
                        &analysis->expressions[symbol->initializer].constant,
                        &expected
                    ));
                    found = true;
                }
            }
            REQUIRE(found);
            uint64_t wrap = 0;
            for (int i = 0; i < analysis->function_count; i++) {
                int symbol = analysis->functions[i].symbol;
                if (symbol >= 0 && !strcmp(analysis->symbols[symbol].name, "Wrap")) {
                    wrap = analysis->symbols[symbol].id;
                }
            }
            REQUIRE(wrap);
            pin_value result = { .type = type };
            context = (behavior_function_context){ .results = &result,
                                                   .activation = &activation,
                                                   .budget = 10000 };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(function, &context, NULL, 0));
            REQUIRE(behavior_function_run(function, &context, b != 0) == FLOW_OK);
            REQUIRE(pin_value_equal(&result, &expected));
            behavior_function_release_host(&context);
            behavior_function_activation_free(&activation);
            arena_free(&storage);
            function = NULL;
            for (int i = 0; i < execution_module_function_count(module); i++) {
                execution_module_function_info candidate = execution_module_function_at(module, i);
                if (candidate.id == wrap) {
                    function = candidate.body;
                }
            }
            REQUIRE(function);
            result = (pin_value){ .type = type };
            context = (behavior_function_context){ .results = &result,
                                                   .activation = &activation,
                                                   .budget = 10000 };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(function, &context, NULL, 0));
            REQUIRE(behavior_function_run(function, &context, b != 0) == FLOW_OK);
            REQUIRE(pin_value_equal(&result, &expected));
            cleanup();
        }
    }
    return true;
}
/* Counts the native calls of the last compile whose kernel returns `storage`,
 * or -1 when one returns another type. */
static int kernel_calls(pin_type storage)
{
    int calls = 0;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *expression = &analysis->expressions[i];
        if (expression->kind != SCRIPT_EXPR_CALL || !expression->capability) {
            continue;
        }
        const script_catalog_capability *capability = script_catalog_capability_by_id(
            native_capabilities_catalog(natives),
            expression->capability
        );
        assert(capability && capability->result_count == 1);
        if (capability->results[0].type.storage != storage) {
            return -1;
        }
        calls++;
    }
    return calls;
}
/* Literal-only math picks the number kernel; an f64 operand picks f64. */
TEST_CASE(float_math_stays_float_and_f64_math_stays_f64)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (int narrow = 0; narrow < 2; narrow++) {
            char error[256];
            natives = native_capabilities_from_definition_packs(
                NULL,
                0,
                &NATIVE_MATH_DEFINITION_PACK,
                1,
                error,
                sizeof error
            );
            REQUIRE(natives);
            const behavior_function *function = compile_source(
                narrow
                    ? "Packed :: library { Math :: (value:number) -> number { "
                      "return "
                      "sqrt(value)+sin(value)+pow(value,2); } }"
                    : "Packed :: library { Math :: () -> f64 { return "
                      "sqrt(f64(2)); } }",
                backends[b]
            );
            REQUIRE(function);
            REQUIRE(kernel_calls(narrow ? PIN_F32 : PIN_F64) == (narrow ? 3 : 1));
            pin_value value = { .type = PIN_F32, .f = 2 },
                      result = { .type = narrow ? PIN_F32 : PIN_F64 };
            context = (behavior_function_context){ .arguments = narrow ? &value : NULL,
                                                   .argument_count = narrow,
                                                   .results = &result,
                                                   .activation = &activation,
                                                   .budget = 10000 };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(function, &context, NULL, 0));
            REQUIRE(behavior_function_run(function, &context, b != 0) == FLOW_OK);
            if (narrow) {
                float expected = sqrtf(2) + cos_sin_compute(2).s;
                expected += powf(2, 2);
                REQUIRE(result.type == PIN_F32 && result.f == expected);
            } else {
                REQUIRE(result.type == PIN_F64 && result.d == sqrt(2));
            }
            cleanup();
        }
    }
    /* A 64-bit integer keeps its digits in the f64 kernel; int picks number. */
    static const struct
    {
        const char *source;
        pin_type kernel;
    } integers[] = {
        { "Packed :: library { Math :: (value:i64) -> f64 { return "
          "sqrt(value); } }",
          PIN_F64 },
        { "Packed :: library { Math :: (value:u64) -> f64 { return "
          "sqrt(value); } }",
          PIN_F64 },
        { "Packed :: library { Math :: (value:int) -> number { return "
          "sqrt(value); } }",
          PIN_F32 },
        /* Literals rank by their default: number, unless a typed operand
         * chooses. */
        { "Packed :: library { Math :: () -> number { return sqrt(2.0); } }", PIN_F32 },
        { "Packed :: library { Math :: () -> number { return sqrt(-(2)); } }", PIN_F32 },
        { "Packed :: library { Math :: () -> number { return pow(2, 3); } }", PIN_F32 },
        { "Packed :: library { Math :: (x:f64) -> f64 { return pow(x, 3); } }", PIN_F64 },
        { "Packed :: library { Math :: (x:f64) -> f64 { return pow(Exponent: "
          "x, "
          "Base: (2.5)); } }",
          PIN_F64 },
    };
    for (size_t i = 0; i < sizeof integers / sizeof *integers; i++) {
        char error[256];
        natives = native_capabilities_from_definition_packs(
            NULL,
            0,
            &NATIVE_MATH_DEFINITION_PACK,
            1,
            error,
            sizeof error
        );
        REQUIRE(natives);
        REQUIRE(compile_source(integers[i].source, BEHAVIOR_BYTECODE));
        REQUIRE(kernel_calls(integers[i].kernel) == 1);
        cleanup();
    }
    return true;
}
static bool packed_authored_loops(bool benchmark)
{
    const pin_type types[] = { PIN_I8,  PIN_U8,  PIN_I16, PIN_U16, PIN_I32,
                               PIN_U32, PIN_I64, PIN_U64, PIN_F32, PIN_F64 };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        for (size_t t = 0; t < sizeof types / sizeof *types; t++) {
            const behavior_function *function = compile(types[t], backends[b]);
            REQUIRE(function);
            pin_value values[ELEMENTS];
            for (int i = 0; i < ELEMENTS; i++) {
                values[i] = scalar(types[t], 1);
            }
            pin_array boxed = { .element_type = types[t],
                                .count = ELEMENTS,
                                .capacity = ELEMENTS,
                                .items = values };
            pin_array *packed = pin_array_snapshot_to_arena(&producer, &boxed);
            REQUIRE(packed && packed->stride);
            double seconds[2];
            for (int representation = 0; representation < 2; representation++) {
                pin_value argument = { .type = types[t],
                                       .container = PIN_CONTAINER_ARRAY,
                                       .ptr = representation ? packed : &boxed };
                pin_value result = scalar(types[t], 0),
                          expected = scalar(types[t], ELEMENTS);
                context = (behavior_function_context){ .arguments = &argument,
                                                       .argument_count = 1,
                                                       .results = &result,
                                                       .activation = &activation,
                                                       .budget = 100000 };
                behavior_function_bind_storage(&context, &storage);
                REQUIRE(behavior_function_bind(function, &context, NULL, 0));
                double begin = benchmark ? now() : 0;
                /* Initial execution and warm reuse are correctness checks. */
                for (int trial = 0; trial < (benchmark ? TRIALS : 2); trial++) {
                    REQUIRE(behavior_function_run(function, &context, b != 0) == FLOW_OK);
                    REQUIRE(pin_value_equal(&result, &expected));
                }
                seconds[representation] = benchmark ? now() - begin : 0;
                behavior_function_release_host(&context);
                behavior_function_activation_free(&activation);
                context = (behavior_function_context){ 0 };
                activation = (behavior_function_activation){ 0 };
                arena_free(&storage);
            }
            if (benchmark) {
                printf(
                    "packed-loop backend=%s type=%s elements=%d trials=%d "
                    "boxed_bytes=%zu packed_bytes=%zu boxed_seconds=%.6f "
                    "packed_seconds=%.6f\n",
                    behavior_backend_name(backends[b]),
                    script_scalar_name(types[t]),
                    ELEMENTS,
                    TRIALS,
                    pin_array_storage_bytes(&boxed),
                    pin_array_storage_bytes(packed),
                    seconds[0],
                    seconds[1]
                );
            }
            cleanup();
        }
    }
    return true;
}
TEST_CASE(packed_authored_loops_match_boxed_input_across_numeric_widths)
{
    return packed_authored_loops(false);
}

TEST_CASE(packed_authored_loop_benchmark)
{
    return packed_authored_loops(true);
}

static bool float_conversion_call(
    const behavior_function *function,
    pin_value input,
    pin_value expected,
    behavior_backend backend
)
{
    pin_value result = { .type = expected.type };
    context = (behavior_function_context){ .arguments = &input,
                                           .argument_count = 1,
                                           .results = &result,
                                           .activation = &activation,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    behavior_run_scope_end(&runs);
    REQUIRE(status == FLOW_OK && context.returned && result.type == expected.type);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(backend != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backend != BEHAVIOR_BYTECODE || runs.counts.bytecode_entries > 0);
    double actual = result.type == PIN_F32 ? result.f : result.d;
    double wanted = expected.type == PIN_F32 ? expected.f : expected.d;
    if (isnan(wanted)) {
        REQUIRE(isnan(actual));
    } else {
        REQUIRE(actual == wanted);
        REQUIRE(!!signbit(actual) == !!signbit(wanted));
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    return true;
}

/* A literal past number's range needs an f64 context: a typed f64 argument
 * that selects a native's f64 overload, a typed f64 operand, or an f64
 * declaration or conversion, through parentheses and signs and with
 * arguments in any order. Without one it is a number and fails where it is
 * written, so a number context and an inferred local reject it. */
TEST_CASE(wide_literals_need_an_f64_context)
{
#define WIDE(BODY)                                                             \
    "Packed :: library { Math :: () -> f64 { x: f64 = 1; " BODY " } }"
    static const struct
    {
        const char *source;
        double expected;
        int kernels;
    } chosen[] = {
        { WIDE("return pow(Base: -1e300, Exponent: x);"), -1e300, 1 },
        { WIDE("return pow(Exponent: x, Base: -1e300);"), -1e300, 1 },
        { WIDE("return pow(Base: (-1e300), Exponent: x);"), -1e300, 1 },
        { WIDE("return pow(Exponent: x, Base: -(1e300));"), -1e300, 1 },
        { WIDE("return pow(Base: -(-1e300), Exponent: x);"), 1e300, 1 },
        { WIDE("return pow(Exponent: x, Base: +((1e300)));"), 1e300, 1 },
        { WIDE("return x * 1e300;"), 1e300, 0 },
        { WIDE("return (-1e300) * x;"), -1e300, 0 },
        { WIDE("return x * -(1e300 * 2);"), -2e300, 0 },
        { WIDE("y: f64 = -(1e300) * 2; return y;"), -2e300, 0 },
        { WIDE("return x * -18446744073709551616;"), -0x1p64, 0 },
        { WIDE("return x * 18446744073709551616;"), 0x1p64, 0 },
        { WIDE("y: f64 = -18446744073709551616; return y;"), -0x1p64, 0 },
        { WIDE("y: f64 = 18446744073709551616; return y;"), 0x1p64, 0 },
        { WIDE("return (-18446744073709551616) * x;"), -0x1p64, 0 },
        { WIDE("return x * -(-18446744073709551616);"), 0x1p64, 0 },
        { WIDE("return pow(Exponent: x, Base: -18446744073709551616);"), -0x1p64, 1 },
        { WIDE("x = 1e300; return sqrt(x);"), 1e150, 1 },
        { WIDE("return sqrt(f64(1e300));"), 1e150, 1 },
    };
#undef WIDE
    static const struct
    {
        const char *source, *message;
        int column;
    } rejected[] = {
        { "Packed :: library { Math :: () -> number { x: number = 1e300; "
          "return x; } }",
          "Numeric literal is outside the range of number",
          56 },
        { "Packed :: library { Math :: () -> f64 { x := 1e300; return x; } }",
          "Numeric literal is outside the range of number",
          46 },
        { "Packed :: library { Math :: () -> f64 { return sqrt(1e300); } }",
          "Numeric literal is outside the range of number",
          53 },
        { "Packed :: library { Math :: () -> f64 { x := -(1e300); return x; } "
          "}",
          "Numeric literal is outside the range of number",
          48 },
        { "Packed :: library { Math :: () -> f64 { return sqrt((-1e300)); } }",
          "Numeric literal is outside the range of number",
          54 },
        { "Packed :: library { Math :: () -> f64 { y := 1e300 * 2; return y; } "
          "}",
          "Numeric literal is outside the range of number",
          46 },
        { "Packed :: library { Math :: () -> f64 { y := -18446744073709551616; "
          "return y; } }",
          "Numeric literal is outside the range of u64",
          46 },
        { "Packed :: library { Math :: (x: f64) -> number { return sqrt(x); } "
          "}",
          "Incompatible types: expected number, got f64; convert with "
          "number(value)",
          57 },
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    char error[256];
    for (int b = 0; b < 2; b++) {
        for (size_t i = 0; i < sizeof chosen / sizeof *chosen; i++) {
            natives = native_capabilities_from_definition_packs(
                NULL,
                0,
                &NATIVE_MATH_DEFINITION_PACK,
                1,
                error,
                sizeof error
            );
            REQUIRE(natives);
            const behavior_function *function = compile_source(chosen[i].source, backends[b]);
            if (!function || analysis->diagnostic_count) {
                fprintf(
                    stderr,
                    "wide literal %zu: %s\n",
                    i,
                    analysis->diagnostic_count ? analysis->diagnostics[0].message : "rejected"
                );
            }
            REQUIRE(function && analysis->diagnostic_count == 0);
            REQUIRE(kernel_calls(PIN_F64) == chosen[i].kernels);
            pin_value result = { .type = PIN_F64 };
            context = (behavior_function_context){ .results = &result,
                                                   .activation = &activation,
                                                   .budget = 10000 };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(function, &context, NULL, 0));
            REQUIRE(behavior_function_run(function, &context, b != 0) == FLOW_OK);
            REQUIRE(result.type == PIN_F64 && result.d == chosen[i].expected);
            cleanup();
        }
    }
    for (size_t i = 0; i < sizeof rejected / sizeof *rejected; i++) {
        natives = native_capabilities_from_definition_packs(
            NULL,
            0,
            &NATIVE_MATH_DEFINITION_PACK,
            1,
            error,
            sizeof error
        );
        REQUIRE(natives);
        REQUIRE(!compile_source(rejected[i].source, BEHAVIOR_BYTECODE));
        REQUIRE(analysis->diagnostic_count == 1);
        const script_diagnostic *d = &analysis->diagnostics[0];
        if (strcmp(d->message, rejected[i].message) ||
            d->range.column != rejected[i].column) {
            fprintf(stderr, "wide literal rejection %zu: %d %s\n", i, d->range.column, d->message);
        }
        REQUIRE(!strcmp(d->message, rejected[i].message));
        REQUIRE(d->range.line == 1 && d->range.column == rejected[i].column);
        cleanup();
    }
    return true;
}
TEST_CASE(float_explicit_narrowing_uses_literal_rounding_oracles)
{
    /* Answers are exact literals, never a production cast helper or host cast
     * of the input. Cases include both halfway parities and their neighbours. */
    static const struct
    {
        double input;
        float expected;
    } cases[] = {
        { 16777217.0, 16777216.0f },
        { 16777219.0, 16777220.0f },
        { -16777217.0, -16777216.0f },
        { -16777219.0, -16777220.0f },
        { 0x1.000000fffffffp24, 16777216.0f },
        { 0x1.0000010000001p24, 16777218.0f },
        { 0x1.fffffep127, FLT_MAX },
        { 0x1.fffffefffffffp127, FLT_MAX },
        { 0x1.ffffffp127, INFINITY },
        { -0x1.ffffffp127, -INFINITY },
        { 0x1p-149, 0x1p-149f },
        { 0x1p-150, 0.0f },
        { 0x1.0000000000001p-150, 0x1p-149f },
        { -0x1p-150, -0.0f },
        { 0.0, 0.0f },
        { -0.0, -0.0f },
        { INFINITY, INFINITY },
        { -INFINITY, -INFINITY },
        { NAN, NAN },
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        const behavior_function *function = compile_source(
            "Packed :: library { Fold :: (value: f64) -> number { return "
            "number(value); } }",
            backends[b]
        );
        REQUIRE(function);
        for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
            REQUIRE(float_conversion_call(
                function,
                (pin_value){ .type = PIN_F64, .d = cases[i].input },
                (pin_value){ .type = PIN_F32, .f = cases[i].expected },
                backends[b]
            ));
        }
        cleanup();
    }
    return true;
}

TEST_CASE(float_implicit_widening_preserves_literal_values)
{
    static const struct
    {
        float input;
        double expected;
    } cases[] = {
        { 0.0f, 0.0 },
        { -0.0f, -0.0 },
        { 1.5f, 1.5 },
        { 16777216.0f, 16777216.0 },
        { FLT_MAX, 0x1.fffffep127 },
        { -FLT_MAX, -0x1.fffffep127 },
        { FLT_MIN, 0x1p-126 },
        { 0x1p-149f, 0x1p-149 },
        { -0x1p-149f, -0x1p-149 },
        { INFINITY, INFINITY },
        { -INFINITY, -INFINITY },
        { NAN, NAN },
    };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        const behavior_function *function = compile_source(
            "Packed :: library { Fold :: (value: number) -> f64 { return "
            "value; } }",
            backends[b]
        );
        REQUIRE(function);
        for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
            REQUIRE(float_conversion_call(
                function,
                (pin_value){ .type = PIN_F32, .f = cases[i].input },
                (pin_value){ .type = PIN_F64, .d = cases[i].expected },
                backends[b]
            ));
        }
        cleanup();
    }
    return true;
}

void test_language_packed()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(packed_record_updates_box_compatible_null_elements);
    RUN_TEST_CASE(packed_scalars_preserve_values_widths_and_snapshot_ownership);
    RUN_TEST_CASE(packed_slices_take_their_representation_from_their_range);
    RUN_TEST_CASE(packed_records_own_layouts_and_keep_nullable_and_managed_fallbacks);
    RUN_TEST_CASE(packed_authored_loops_match_boxed_input_across_numeric_widths);
    RUN_TEST_CASE(typed_integer_edges_match_vm_and_compiled_backends);
    RUN_TEST_CASE(typed_integer_large_constant_folding_is_exact);
    RUN_TEST_CASE(typed_compile_time_values_preserve_width_and_float_remainder);
    RUN_TEST_CASE(float_math_stays_float_and_f64_math_stays_f64);
    RUN_TEST_CASE(wide_literals_need_an_f64_context);
    RUN_TEST_CASE(float_explicit_narrowing_uses_literal_rounding_oracles);
    RUN_TEST_CASE(float_implicit_widening_preserves_literal_values);
    pu_clear_setup();
}

void test_language_packed_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(packed_authored_loop_benchmark);
    pu_clear_setup();
}
