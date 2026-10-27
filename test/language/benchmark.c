#include "backend.h"
#include "ecs/ecs.h"
#include "execution/function.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "runtime/text_format.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include "../../src/execution/wasm_jit.h"
#endif

#define LOOP_COUNT 100
#define DISPATCH_COUNT 200
#define TRIAL_COUNT 5
#define INSTRUCTION_BUDGET 100000
#define EXPECTED_TOTAL 14950
#define RUN_EVENT 10
#define RECORD_FIELD_COUNT 24
#define RECORD_ROW_COUNT 256
#define RECORD_COPY_COUNT 200
#define TEXT_BYTES (128 * 1024)
#define TEXT_READS 4096
#define BYTE_PIECES 4096
#define BYTE_PIECE 16
#define RESET_CELLS 200
#define RESET_COUNT 200000

static const script_catalog_capability run_event = {
    .id = RUN_EVENT,
    .name = "Run",
    .kind = SCRIPT_CAPABILITY_EVENT,
};
static const script_catalog catalog = {
    .capabilities = &run_event,
    .capability_count = 1,
};
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static ecs_world *world;
static double publish_seconds;
#ifdef __EMSCRIPTEN__
static execution_wasm_jit_stats publish_stats;
#endif
static double monotonic_seconds();

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

static bool compile(const char *source, behavior_backend backend)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(stderr, "benchmark semantic: %s\n", analysis->diagnostics[i].message);
        }
        return false;
    }
    const script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(stderr, "benchmark lower: %s\n", plan->diagnostics[i].message);
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
#endif
    double start = monotonic_seconds();
    module = execution_module_finish(&plan, backend, error, sizeof error);
    publish_seconds = monotonic_seconds() - start;
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_get_stats(&after);
    publish_stats = (execution_wasm_jit_stats){
        .compilations = after.compilations - before.compilations,
        .executions = after.executions - before.executions,
        .compile_failures = after.compile_failures - before.compile_failures,
    };
#endif
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "benchmark publish: %s\n", error);
        return false;
    }
    world = ecs_world_new();
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}

static uint64_t total_id()
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, "total")) {
            return analysis->symbols[i].id;
        }
    }
    return 0;
}

static double monotonic_seconds()
{
    struct timespec time;
    int status = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(status == 0);
    return time.tv_sec + time.tv_nsec * 1e-9;
}

TEST_CASE(scalar_authored_call_benchmark)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    const char *expressions[] = { "i * 3 + 1", "Step(i)" };
    for (int b = 0; b < 2; b++) {
        for (int helper = 0; helper < 2; helper++) {
            char source[1024];
            int size = snprintf(
                source,
                sizeof source,
                "Bench :: behavior { total: f64 = 0; "
                "Step :: (x: f64) -> (value: f64) { return(x * 3 + "
                "1); } "
                "on Run :: () { sum: f64 = 0; i := 0.0; "
                "while i < %d { sum += %s; i += 1; } total = sum; } }",
                LOOP_COUNT,
                expressions[helper]
            );
            REQUIRE(size > 0 && (size_t)size < sizeof source);
            REQUIRE(compile(source, backends[b]));
            ecs_entity owner = ecs_world_create_entity(world);
            execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
            REQUIRE(instance != NULL);
            uint64_t state = total_id();
            REQUIRE(state != 0);
            REQUIRE(
                execution_instance_dispatch(instance, RUN_EVENT, NULL, 0, 0, 0, INSTRUCTION_BUDGET) == FLOW_OK
            );
            const pin_value *total = execution_instance_state(instance, state);
            REQUIRE(total && total->type == PIN_F64 && total->d == EXPECTED_TOTAL);
            printf(
                "BENCH backend=%s helper=%d loop=%d dispatches=%d trials=%d "
                "module_bytes=%zu publish_ms=%.6f\n",
                behavior_backend_name(backends[b]),
                helper,
                LOOP_COUNT,
                DISPATCH_COUNT,
                TRIAL_COUNT,
                execution_module_storage_bytes(module),
                publish_seconds * 1e3
            );
#ifdef __EMSCRIPTEN__
            printf(
                "PUBLISH installations=%llu executions=%llu "
                "compile_failures=%llu\n",
                (unsigned long long)publish_stats.compilations,
                (unsigned long long)publish_stats.executions,
                (unsigned long long)publish_stats.compile_failures
            );
#endif
            for (int trial = 0; trial < TRIAL_COUNT; trial++) {
                int status = FLOW_OK;
                xalloc_stats before, after;
                xalloc_stats_get(&before);
                uint64_t blocks = arena_block_allocations();
                double start = monotonic_seconds();
                for (int n = 0; n < DISPATCH_COUNT; n++) {
                    status |= execution_instance_dispatch(instance, RUN_EVENT, NULL, 0, 0, 0, INSTRUCTION_BUDGET);
                }
                double elapsed = monotonic_seconds() - start;
                xalloc_stats_get(&after);
                blocks = arena_block_allocations() - blocks;
                REQUIRE(status == FLOW_OK);
                REQUIRE(execution_instance_state(instance, state)->d == EXPECTED_TOTAL);
                printf(
                    "BENCH trial=%d seconds=%.9f ns_per_loop=%.3f "
                    "us_per_dispatch=%.3f\n",
                    trial,
                    elapsed,
                    elapsed * 1e9 / (LOOP_COUNT * DISPATCH_COUNT),
                    elapsed * 1e6 / DISPATCH_COUNT
                );
                printf(
                    "ALLOC calls=%llu requested_bytes=%llu arena_blocks=%llu\n",
                    (unsigned long long)(after.calls - before.calls),
                    (unsigned long long)(after.requested_bytes - before.requested_bytes),
                    (unsigned long long)blocks
                );
            }
            cleanup();
        }
    }
    return true;
}

/* Copies rows of a compiler-sized record between arenas, as stage one does
 * when it passes and returns its syntax rows. */
TEST_CASE(record_row_snapshot_benchmark)
{
    arena source = { 0 }, copies = { 0 };
    char names[RECORD_FIELD_COUNT][16];
    type_field fields[RECORD_FIELD_COUNT];
    for (int i = 0; i < RECORD_FIELD_COUNT; i++) {
        snprintf(names[i], sizeof names[i], "field_%d", i);
        const pin_type types[] = { PIN_STR, PIN_I64, PIN_BOOL };
        fields[i] = (type_field){ .name = names[i], .type = types[i % 3] };
    }
    const pin_record *record = pin_record_create(&source, "Syntax.Row", 7, fields, RECORD_FIELD_COUNT);
    REQUIRE(record);
    pin_value *rows = arena_alloc(&source, RECORD_ROW_COUNT * sizeof *rows);
    for (int i = 0; i < RECORD_ROW_COUNT; i++) {
        rows[i] = (pin_value){ .type = PIN_STRUCT,
                               .type_name = record->type.name,
                               .record = record,
                               .ptr = arena_alloc(&source, record->type.size) };
        memset(rows[i].ptr, 0, record->type.size);
        for (int f = 0; f < RECORD_FIELD_COUNT; f += 3) {
            pin_value text = { .type = PIN_STR,
                               .text = &(const pin_string){
                                   .bytes = "identifier",
                                   .length = sizeof("identifier") - 1 } };
            {
                const char *error = pin_write_field(
                    &source,
                    rows[i].ptr,
                    &record->type.fields[f],
                    text
                );
                if (error) {
                    abort();
                }
            };
        }
    }
    pin_value *out = xmalloc(RECORD_ROW_COUNT * sizeof *out);
    for (int trial = 0; trial < TRIAL_COUNT; trial++) {
        double start = monotonic_seconds();
        for (int n = 0; n < RECORD_COPY_COUNT; n++) {
            arena_free(&copies);
            const pin_record *owned = pin_record_snapshot(&copies, record);
            /* Rows holding strings are not packed, so they have no stride. */
            REQUIRE(owned && !pin_array_element_stride(PIN_STRUCT, owned->type.name, owned));
            REQUIRE(!pin_array_snapshot_items(&copies, owned, out, rows, RECORD_ROW_COUNT));
            for (int i = 0; i < RECORD_ROW_COUNT; i++) {
                REQUIRE(!pin_value_snapshot_to_arena(&copies, &out[i], &rows[i]));
            }
        }
        double elapsed = monotonic_seconds() - start;
        printf(
            "BENCH record_rows fields=%d rows=%d copies=%d trial=%d "
            "ns_per_row=%.3f\n",
            RECORD_FIELD_COUNT,
            RECORD_ROW_COUNT,
            RECORD_COPY_COUNT,
            trial,
            elapsed * 1e9 / (2.0 * RECORD_ROW_COUNT * RECORD_COPY_COUNT)
        );
    }
    free(out);
    arena_free(&copies);
    arena_free(&source);
    return true;
}

/* Reads node-sized slices and bytes near the start of a source-sized text, as
 * a compiler does while it walks a file's syntax. */
TEST_CASE(text_slice_benchmark)
{
    arena storage = { 0 };
    char *text = xmalloc(TEXT_BYTES + 1);
    memset(text, 'a', TEXT_BYTES);
    text[TEXT_BYTES] = 0;
    for (int trial = 0; trial < TRIAL_COUNT; trial++) {
        double start = monotonic_seconds();
        for (int i = 0; i < TEXT_READS; i++) {
            pin_value slice[] = { { .type = PIN_STR,
                                    .text = &(
                                        const pin_string
                                    ){ .bytes = text, .length = (uint32_t)strlen(text) } },
                                  { .type = PIN_I32, .i = i * 4 },
                                  { .type = PIN_I32, .i = 8 } },
                      out;
            REQUIRE(
                !NATIVE_TEXT_SLICE_CALL.value(&storage, slice, 3, &out) &&
                strlen(pin_text(out).bytes) == 8
            );
            REQUIRE(!NATIVE_TEXT_BYTE_AT_CALL.value(&storage, slice, 2, &out) && out.i == 'a');
        }
        double elapsed = monotonic_seconds() - start;
        arena_free(&storage);
        printf(
            "BENCH text bytes=%d reads=%d trial=%d ns_per_read=%.3f\n",
            TEXT_BYTES,
            TEXT_READS,
            trial,
            elapsed * 1e9 / (2.0 * TEXT_READS)
        );
    }
    free(text);
    return true;
}

/* Builds a byte array from small pieces as the prepared writer does: each
 * piece is pushed a byte at a time, then appended to the whole, which is
 * then copied out as a call result. */
TEST_CASE(byte_array_build_benchmark)
{
    arena storage = { 0 }, result = { 0 };
    for (int trial = 0; trial < TRIAL_COUNT; trial++) {
        arena_init_collectable(&storage);
        double start = monotonic_seconds();
        pin_value whole = { .type = PIN_U8, .container = PIN_CONTAINER_ARRAY }, out;
        for (int i = 0; i < BYTE_PIECES; i++) {
            pin_value piece = { .type = PIN_U8, .container = PIN_CONTAINER_ARRAY };
            for (int b = 0; b < BYTE_PIECE; b++) {
                pin_value push[] = { { .u64 = (uint64_t)(uintptr_t)&piece },
                                     { .type = PIN_U8, .u8 = (uint8_t)b } };
                REQUIRE(!EXECUTION_array_push_call.value(&storage, push, 2, &out));
            }
            pin_value extend[] = { { .u64 = (uint64_t)(uintptr_t)&whole }, piece };
            REQUIRE(!EXECUTION_array_extend_call.value(&storage, extend, 2, &out));
        }
        pin_value copy;
        REQUIRE(!pin_value_snapshot_to_arena(&result, &copy, &whole));
        double elapsed = monotonic_seconds() - start;
        REQUIRE(((const pin_array *)copy.ptr)->count == BYTE_PIECES * BYTE_PIECE);
        arena_free(&storage);
        arena_free(&result);
        printf(
            "BENCH bytes pieces=%d piece=%d trial=%d ns_per_byte=%.3f\n",
            BYTE_PIECES,
            BYTE_PIECE,
            trial,
            elapsed * 1e9 / (BYTE_PIECES * BYTE_PIECE)
        );
    }
    return true;
}

/* Restores the data of a function with as many cells as the compiler's
 * largest helpers, as retiring each of its calls does. */
TEST_CASE(data_reset_benchmark)
{
    behavior_function_builder *builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, NULL, 0, NULL, 0);
    pin_value cells[RESET_CELLS];
    for (int i = 0; i < RESET_CELLS; i++) {
        cells[i] = (pin_value){ .type = PIN_I32, .i = i };
    }
    behavior_function_builder_values(builder, cells, RESET_CELLS);
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    REQUIRE(function);
    behavior_data_storage *data = NULL;
    behavior_function_prepare_data(function, NULL, &data);
    REQUIRE(data);
    for (int trial = 0; trial < TRIAL_COUNT; trial++) {
        double start = monotonic_seconds();
        for (int n = 0; n < RESET_COUNT; n++) {
            data->values[n % RESET_CELLS].i = -1;
            behavior_function_reset_data(data);
        }
        double elapsed = monotonic_seconds() - start;
        REQUIRE(data->values[RESET_CELLS - 1].i == RESET_CELLS - 1);
        printf(
            "BENCH reset cells=%d trial=%d ns_per_reset=%.3f\n",
            RESET_CELLS,
            trial,
            elapsed * 1e9 / RESET_COUNT
        );
    }
    behavior_function_free_data(data);
    behavior_function_free(function);
    return true;
}
void test_language_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scalar_authored_call_benchmark);
    RUN_TEST_CASE(record_row_snapshot_benchmark);
    RUN_TEST_CASE(text_slice_benchmark);
    RUN_TEST_CASE(byte_array_build_benchmark);
    RUN_TEST_CASE(data_reset_benchmark);
    pu_clear_setup();
}
