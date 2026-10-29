#include "authoring/native_bindings.h"
#include "backend.h"
#include "compiler_fixture.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/managed.h"
#include "execution/module.h"
#include "execution/module_effects.h"
#include "execution/status.h"
#include "execution/wasm_jit.h"
#include "language/identity.h"
#include "language/lower.h"
#include "runtime/arrays.h"
#include "runtime/native_capabilities.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/copy.h"
#include "types/record.h"
#include "types/text.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Proposed authored construction path, not the current one-pass Studio C
 * producer. Six fixed cases: 84 direct callbacks, four reference snapshots,
 * eight rejects, four collectable callbacks/two collections, six module
 * preparations and twenty authored invocations, plus one two-backend package
 * preparation with 48 in-place local array calls in plain and collectable storage.
 * Maximum sixteen rows in the measured cases and 200,000 appended items in the
 * in-place case; no I/O/workers/frames or timing loop. */
static arena producer, storage;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static native_capabilities *native;
static uint64_t function_id, expected_id, expected_layout;
static behavior_function_context context;
static behavior_function_activation activation;
static behavior_managed_heap heap;
static behavior_run_scope runs;
static pin_copy_scope copies;
static pin_value result;
static const int sizes[] = { 0, 1, 4, 16 };
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static const type_field fields[] = {
    { .name = "key", .type = PIN_STR },
    { .name = "label", .type = PIN_STR },
    { .name = "icon", .type = PIN_STR },
    { .name = "prototype", .type = PIN_STR },
    { .name = "drop", .type = PIN_STR },
    { .name = "depth", .type = PIN_I32 },
    { .name = "behaviors", .type = PIN_I32 },
    { .name = "active", .type = PIN_BOOL },
    { .name = "marked", .type = PIN_BOOL },
    { .name = "hidden", .type = PIN_BOOL },
    { .name = "locked", .type = PIN_BOOL },
    { .name = "renaming", .type = PIN_BOOL },
};
static void retire_module()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    execution_module_free(module);
    module = NULL;
    execution_module_plan_free(plan);
    plan = NULL;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    native_capabilities_free(native);
    native = NULL;
}
static void cleanup()
{
    if (copies.active) {
        pin_copy_scope_end(&copies);
    }
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    retire_module();
    behavior_managed_heap_free(&heap);
    arena_free(&producer);
    arena_free(&storage);
    result = (pin_value){ 0 };
}
static const pin_record *layout(uint64_t id)
{
    return pin_record_create(&producer, "Rows.Row", id, fields, 12);
}
static pin_value member(const pin_value *value, int field)
{
    pin_value out;
    {
        const char *error = pin_read_field(
            &producer,
            value->ptr,
            &value->record->type.fields[field],
            &out
        );
        if (error) {
            abort();
        }
    };
    return out;
}
static pin_value row(const pin_record *record, int number)
{
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = arena_alloc(&producer, record->type.size) };
    memset(value.ptr, 0, record->type.size);
    for (int f = 0; f < 12; f++) {
        pin_value v = { .type = fields[f].type };
        if (v.type == PIN_STR) {
            char buffer[20];
            snprintf(buffer, sizeof buffer, "row-%02d-%d", number, f);
            const char *error = pin_value_text(&producer, &v, buffer, strlen(buffer));
            if (error) {
                abort();
            }
        } else if (v.type == PIN_I32) {
            v.i = number + f;
        } else {
            v.b = (number + f) % 2;
        }
        {
            const char *
                error = pin_write_field(&producer, value.ptr, &record->type.fields[f], v);
            if (error) {
                abort();
            }
        };
    }
    return value;
}
static pin_value array_header(pin_array *a, const pin_record *record, pin_value *items, int count)
{
    *a = (pin_array){ .element_type = PIN_STRUCT,
                      .type_name = record->type.name,
                      .record = record,
                      .count = count,
                      .capacity = count,
                      .items = items };
    return (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = a };
}
static bool check_row(const pin_value *value, int number, const char *shared_text, const arena *owner)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE);
    REQUIRE(value->record && pin_record_valid(value->record));
    REQUIRE(value->record->id == expected_id && value->record->layout == expected_layout);
    REQUIRE(value->type_name == value->record->type.name);
    REQUIRE(!strcmp(value->type_name, "Rows.Row"));
    REQUIRE(arena_contains(owner, value->record) && arena_contains(owner, value->ptr));
    for (int f = 0; f < 12; f++) {
        pin_value v = member(value, f);
        REQUIRE(v.type == fields[f].type);
        if (v.type == PIN_STR) {
            char buffer[20];
            snprintf(buffer, sizeof buffer, "row-%02d-%d", number, f);
            REQUIRE(
                pin_text(v).bytes &&
                !strcmp(pin_text(v).bytes, shared_text ? shared_text : buffer)
            );
            /* Text shares its immutable bytes; the owner holds the descriptor. */
            REQUIRE(arena_contains(owner, v.text));
        } else if (v.type == PIN_I32) {
            REQUIRE(v.i == number + f);
        } else {
            REQUIRE(v.b == (bool)((number + f) % 2));
        }
    }
    return true;
}
/* A result lives in the caller's storage, or, when the function shares an
 * array it built, on the caller's managed heap. */
static bool caller_owns(const void *p)
{
    return arena_contains(&storage, p) || arena_contains(&heap.storage, p);
}
static bool check_array(const pin_value *value, int count, const char *shared_text)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    REQUIRE(value->record && pin_record_valid(value->record));
    REQUIRE(value->record->id == expected_id && value->record->layout == expected_layout);
    REQUIRE(value->type_name == value->record->type.name);
    REQUIRE(!strcmp(value->type_name, "Rows.Row"));
    REQUIRE(caller_owns(value->record));
    const pin_array *a = value->ptr;
    REQUIRE(a && caller_owns(a));
    REQUIRE(a->count == count && a->element_type == PIN_STRUCT);
    REQUIRE(a->record == value->record && a->type_name == value->type_name);
    for (int i = 0; i < count; i++) {
        REQUIRE(pin_array_get(a, i).record == a->record);
        pin_value item = pin_array_get(a, i);
        REQUIRE(
            check_row(&item, i, shared_text, arena_contains(&storage, a) ? &storage : &heap.storage)
        );
    }
    return true;
}
static void expect_layout(const pin_record *record)
{
    expected_id = record->id;
    expected_layout = record->layout;
}
static void report(const char *path, int wrapper, int count, uint64_t blocks)
{
    size_t used, capacity;
    arena_usage(&storage, &used, &capacity);
    printf(
        "row-array path=%s wrapper=%d rows=%d record_bytes=%" PRIu64
        " text_bytes=%" PRIu64 " nominal_bytes=%" PRIu64 " headers=%" PRIu64
        " value_buffer=%" PRIu64 " all_arena_block_delta=%" PRIu64
        " arena_used_total=%zu arena_capacity_total=%zu"
        " arena_storage_total=%zu final_one_shape_bytes=%zu\n",
        path,
        wrapper,
        count,
        copies.counts.record_bytes,
        copies.counts.text_bytes,
        copies.counts.nominal_bytes,
        copies.counts.value_header_moves,
        copies.counts.value_buffer_bytes,
        arena_block_allocations() - blocks,
        used,
        capacity,
        arena_storage_bytes(&storage),
        result.record->bytes
    );
}
TEST_CASE(row_array_direct_cost_and_aliases)
{
    static const node_host_call *const calls[2][2] = {
        { &NATIVE_ARRAY_add_HOST, &NATIVE_ARRAY_extend_HOST },
        { &NATIVE_ARRAY_add_CALL, &NATIVE_ARRAY_extend_CALL },
    };
    for (int wrapper = 0; wrapper < 2; wrapper++) {
        for (int operation = 0; operation < 2; operation++) {
            for (int n = 0; n < 4; n++) {
                cleanup();
                int count = sizes[n];
                const pin_record *record = layout(91);
                REQUIRE(record);
                expect_layout(record);
                size_t payload = record->type.size;
                pin_value items[16];
                for (int i = 0; i < count; i++) {
                    items[i] = row(record, i);
                }
                pin_array empty;
                pin_value initial = array_header(&empty, record, NULL, 0);
                REQUIRE(!pin_value_snapshot_to_arena(&storage, &result, &initial));
                pin_value prefix = result;
                uint64_t blocks = arena_block_allocations();
                pin_copy_scope_reset(&copies);
                pin_copy_scope_begin(&copies);
                for (int i = 0; i < count; i++) {
                    pin_array singleton;
                    pin_value tail = array_header(&singleton, record, &items[i], 1);
                    pin_value args[] = { result, operation ? tail : items[i] };
                    REQUIRE(!calls[wrapper][operation]->value(&storage, args, 2, &result));
                    if (i == 0) {
                        prefix = result;
                    }
                }
                pin_copy_scope_end(&copies);
                uint64_t copies_expected = (uint64_t)count * (count + 1) / 2;
                REQUIRE(!copies.counts.overflow);
                REQUIRE(copies.counts.record_bytes == copies_expected * payload);
                REQUIRE(!copies.counts.text_bytes);
                REQUIRE(copies.counts.value_header_moves == copies_expected);
                REQUIRE(!copies.counts.nominal_bytes && !copies.counts.value_buffer_bytes);
                report(operation ? "append" : "add", wrapper, count, blocks);
                REQUIRE(check_array(&prefix, count ? 1 : 0, NULL));
                REQUIRE(check_array(&result, count, NULL));
                arena_free(&producer);
                REQUIRE(check_array(&prefix, count ? 1 : 0, NULL));
                REQUIRE(check_array(&result, count, NULL));
            }
        }
    }
    for (int n = 0; n < 4; n++) {
        cleanup();
        int count = sizes[n];
        const pin_record *record = layout(91);
        REQUIRE(record);
        expect_layout(record);
        size_t payload = record->type.size;
        pin_value items[16];
        for (int i = 0; i < count; i++) {
            items[i] = row(record, i);
        }
        pin_array a;
        pin_value input = array_header(&a, record, items, count);
        uint64_t blocks = arena_block_allocations();
        pin_copy_scope_reset(&copies);
        pin_copy_scope_begin(&copies);
        REQUIRE(!pin_value_snapshot_to_arena(&storage, &result, &input));
        pin_copy_scope_end(&copies);
        REQUIRE(!copies.counts.overflow);
        REQUIRE(copies.counts.record_bytes == (uint64_t)count * payload);
        REQUIRE(!copies.counts.text_bytes);
        REQUIRE(copies.counts.value_header_moves == (uint64_t)count + 1);
        REQUIRE(!copies.counts.nominal_bytes && !copies.counts.value_buffer_bytes);
        report("one-pass-snapshot", 0, count, blocks);
        arena_free(&producer);
        REQUIRE(check_array(&result, count, NULL));
    }
    return true;
}
TEST_CASE(row_array_rejection_atomicity)
{
    static const node_host_call *const calls[2][2] = {
        { &NATIVE_ARRAY_add_HOST, &NATIVE_ARRAY_extend_HOST },
        { &NATIVE_ARRAY_add_CALL, &NATIVE_ARRAY_extend_CALL },
    };
    for (int wrapper = 0; wrapper < 2; wrapper++) {
        for (int operation = 0; operation < 2; operation++) {
            cleanup();
            const pin_record *record = layout(91);
            const pin_record *different = layout(92);
            REQUIRE(record && different);
            expect_layout(record);
            pin_value item = row(record, 0);
            pin_array left;
            pin_value left_value = array_header(&left, record, &item, 1);
            REQUIRE(!pin_value_snapshot_to_arena(&storage, &result, &left_value));
            pin_value original = result;
            pin_value bad = item;
            bad.record = different;
            bad.type_name = bad.record->type.name;
            pin_array right;
            pin_value tail = array_header(&right, bad.record, &bad, 1);
            pin_value args[] = { result, operation ? tail : bad };
            pin_value output = { .type = PIN_I32, .i = 73 };
            pin_value before = output;
            pin_copy_scope_reset(&copies);
            pin_copy_scope_begin(&copies);
            const char *error = calls[wrapper][operation]->value(&storage, args, 2, &output);
            pin_copy_scope_end(&copies);
            REQUIRE(error);
            REQUIRE(
                !strcmp(error, operation ? "data function produced an invalid result" : "arena allocation failed")
            );
            REQUIRE(output.type == before.type && output.container == before.container);
            REQUIRE(output.i == before.i && !output.record && !output.type_name);
            REQUIRE(result.ptr == original.ptr && result.record == original.record);
            REQUIRE(check_array(&result, 1, NULL));
            REQUIRE(item.record == record);
            REQUIRE(check_row(&item, 0, NULL, &producer));
            REQUIRE(left.count == 1 && left.items == &item && right.count == 1);
            REQUIRE(!copies.counts.overflow);
        }
    }
    return true;
}

TEST_CASE(row_array_call_collectable_results)
{
    arena_init_collectable(&storage);
    const pin_record *record = layout(91);
    REQUIRE(record);
    expect_layout(record);
    pin_value item = row(record, 0);
    pin_array empty_array, one_array;
    pin_value empty = array_header(&empty_array, record, NULL, 0);
    pin_value one = array_header(&one_array, record, &item, 1);
    pin_value arguments[][2] = { { empty, item },
                                 { empty, empty },
                                 { one, empty },
                                 { empty, one } };
    const int counts[] = { 1, 0, 1, 1 };
    pin_value retained[4];
    for (int i = 0; i < 4; i++) {
        retained[i] = (pin_value){ .type = PIN_I32,
                                   .type_name = "poison",
                                   .record = record,
                                   .i = 73 };
        const node_host_call *call = i ? &NATIVE_ARRAY_extend_CALL
                                       : &NATIVE_ARRAY_add_CALL;
        REQUIRE(!call->value(&storage, arguments[i], 2, &retained[i]));
        REQUIRE(check_array(&retained[i], counts[i], NULL));
    }
    /* Text bytes are immutable and shared, so freeing their producer is the
     * change the results must survive. */
    arena_free(&producer);
    arena_collection collection;
    arena *arenas[] = { &storage };
    arena_collect_begin(&collection, arenas, 1);
    for (int i = 0; i < 4; i++) {
        /* Raw array roots must retain their layout, payloads and strings. */
        arena_collect_mark(&collection, retained[i].ptr);
    }
    arena_collect_end(&collection);
    for (int i = 0; i < 4; i++) {
        REQUIRE(check_array(&retained[i], counts[i], NULL));
    }
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_end(&collection);
    size_t bytes, capacity;
    arena_usage(&storage, &bytes, &capacity);
    REQUIRE(bytes == 0);
    return true;
}

#define ROW                                                                    \
    "Row :: struct { key:string; label:string; icon:string=\"node\"; "         \
    "prototype:string=\"\"; drop:string=\"\"; depth:int=1; behaviors:int=0; "  \
    "active:bool=true; marked:bool=false; hidden:bool=false; "                 \
    "locked:bool=false; renaming:bool=false; } "
#define MAKE_ROW                                                               \
    "row:Row={key:text,label:text,icon:text,prototype:text,drop:text,"         \
    "depth:i+5,behaviors:i+6,active:i%2==0,marked:i%2==1,hidden:i%2==0,"       \
    "locked:i%2==1,renaming:i%2==0}; "
static const behavior_function *body()
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info f = execution_module_function_at(module, i);
        if (f.id == function_id) {
            return f.body;
        }
    }
    return NULL;
}
static bool compile(const char *source, behavior_backend backend)
{
    char error[256];
    native = native_capabilities_from_definition_packs(
        NULL,
        0,
        &NATIVE_ARRAY_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    if (!native) {
        fprintf(stderr, "row-array catalog: %s\n", error);
    }
    REQUIRE(native);
    const script_catalog *catalog = native_capabilities_catalog(native);
    script_lower_bindings bindings = authoring_native_bindings(native);
    syntax = script_parse(source);
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        const script_diagnostic *d = &syntax->diagnostics[i];
        fprintf(
            stderr,
            "row-array parse %d:%d [%d,%d): %s: %.64s\n",
            d->range.line,
            d->range.column,
            d->range.start,
            d->range.end,
            d->message,
            source + d->range.start
        );
    }
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze(syntax, catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "row-array semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid && analysis->function_count == 1);
    REQUIRE(analysis->functions[0].symbol >= 0);
    function_id = analysis->symbols[analysis->functions[0].symbol].id;
    plan = script_lower(analysis, catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "row-array lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "row-array publish: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    REQUIRE(body() && body()->result_count == 1);
    REQUIRE(body()->result_defaults[0].record);
    expect_layout(body()->result_defaults[0].record);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    return true;
}
static bool run(behavior_backend backend, int count, const char *source_text, const char *path)
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    activation = (behavior_function_activation){ 0 };
    context = (behavior_function_context){ 0 };
    arena_free(&storage);
    behavior_managed_heap_free(&heap);
    behavior_managed_heap_init(&heap);
    const behavior_function *function = body();
    REQUIRE(function);
    result = function->result_defaults[0];
    behavior_function_prepare_results(function, &result, &storage);
    pin_value args[] = { { .type = PIN_I32, .i = count },
                         { .type = PIN_STR,
                           .text = &(const pin_string){
                               .bytes = source_text,
                               .length = (uint32_t)strlen(source_text) } } };
    context = (behavior_function_context){ .activation = &activation,
                                           .arguments = args,
                                           .argument_count = 2,
                                           .results = &result,
                                           .managed_heap = &heap,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
    behavior_run_scope_reset(&runs);
    pin_copy_scope_reset(&copies);
    uint64_t blocks = arena_block_allocations();
    /* Caller result preparation/bind is excluded. The whole authored body is
     * included: constructors, local/argument retention, callbacks and return
     * snapshots. No estimated overhead is subtracted. */
    behavior_run_scope_begin(&runs);
    pin_copy_scope_begin(&copies);
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    pin_copy_scope_end(&copies);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "row-array run: %d %s\n",
            status,
            context.error_message ? context.error_message : ""
        );
    }
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(!copies.counts.overflow && !runs.counts.overflow);
    REQUIRE(!runs.counts.inspection_bytecode_fallbacks);
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
    execution_wasm_jit_get_stats(&after);
    REQUIRE(
        backend != BEHAVIOR_WASM || (after.executions > before.executions &&
                                     after.compile_failures == before.compile_failures)
    );
    REQUIRE(result.record && pin_record_valid(result.record));
    report(path, backend, count, blocks);
    printf(
        "row-array backend=%d bytecode=%" PRIu64 " sljit=%" PRIu64
        " wasm=%" PRIu64 " fallback=%" PRIu64 "\n",
        backend,
        runs.counts.bytecode_entries,
        runs.counts.sljit_entries,
        runs.counts.wasm_entries,
        runs.counts.inspection_bytecode_fallbacks
    );
    /* Arguments are a synchronous borrow; do not retain this stack pointer. */
    context.arguments = NULL;
    context.argument_count = 0;
    return true;
}
TEST_CASE(row_array_authored_construction)
{
    /* Exact selected-copy predictions for an in-place local add and an
     * in-place append of one-row arrays: both are linear in the row count.
     * Constructor, argument and return work remains in these totals. */
    static const uint64_t row_copies[][4] = { { 0, 3, 9, 33 }, { 0, 6, 21, 81 } };
    /* Text shares its immutable bytes: the call copies its borrowed text
     * argument, "producer" and its terminator, once, and every row shares it. */
    static const uint64_t text_bytes = 9;
    static const uint64_t headers[][4] = { { 13, 29, 74, 254 }, { 13, 33, 90, 318 } };
    static const char *sources[] = {
        "Rows :: library { " ROW
        "Build::(count:int,text:string)->Row[] { rows:Row[]=[]; i:int=0; "
        "while i<count { " MAKE_ROW
        "rows=append(rows,row); i=i+1; } return rows; } }",
        "Rows :: library { " ROW
        "Build::(count:int,text:string)->Row[] { rows:Row[]=[]; i:int=0; "
        "while i<count { " MAKE_ROW
        "rows=extend(rows,[row]); i=i+1; } return rows; } }",
    };
    for (int operation = 0; operation < 2; operation++) {
        for (int b = 0; b < 2; b++) {
            cleanup();
            REQUIRE(compile(sources[operation], backends[b]));
            for (int n = 0; n < 4; n++) {
                char input[] = "producer";
                REQUIRE(
                    run(backends[b], sizes[n], input, operation ? "authored-append" : "authored-add")
                );
                REQUIRE(
                    copies.counts.record_bytes ==
                    row_copies[operation][n] * result.record->type.size
                );
                REQUIRE(copies.counts.text_bytes == text_bytes);
                REQUIRE(copies.counts.value_header_moves == headers[operation][n]);
                REQUIRE(!copies.counts.nominal_bytes && !copies.counts.value_buffer_bytes);
                memset(input, 'x', sizeof input - 1);
                REQUIRE(check_array(&result, sizes[n], "producer"));
            }
            retire_module();
            REQUIRE(check_array(&result, 16, "producer"));
        }
    }
    return true;
}
TEST_CASE(row_array_authored_retained_prefix)
{
    static const char source
        [] = "Rows :: library { " ROW "Keep::(count:int,text:string)->Row[] { "
             "rows:Row[]=[]; prefix:Row[]=[]; i:int=0; "
             "while i<count { " MAKE_ROW
             "rows=append(rows,row); if i==0 { prefix=rows; } i=i+1; } "
             "if length(prefix)!=1 or length(rows)!=count or "
             "rows[count-1].depth!=count+4 { "
             "prefix[0].depth=-99; } rows[0].key=\"changed\"; return prefix; } "
             "}";
    static const int counts[] = { 2, 16 };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(source, backends[b]));
        for (int n = 0; n < 2; n++) {
            char input[] = "producer";
            REQUIRE(run(backends[b], counts[n], input, "authored-retained-prefix"));
            memset(input, 'x', sizeof input - 1);
            REQUIRE(check_array(&result, 1, "producer"));
        }
        retire_module();
        REQUIRE(check_array(&result, 1, "producer"));
    }
    return true;
}
/* A local's own array grows and changes in place; every other value keeps its
 * snapshot. 200,000 appends finish only because each one is amortized O(1). */
/* The pc of the first watch at a source offset, or -1. */
static int watch_pc(const behavior_function *f, int start, bool output, int ordinal)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        const behavior_source_watch *w = i->op == BF_SOURCE_WATCH
                                           ? &f->source_watches[i->target]
                                           : NULL;
        if (w && w->origin.start == start && w->output == output &&
            w->ordinal == ordinal) {
            return pc;
        }
    }
    return -1;
}
/* The pc of the first assignment point at a source offset, or -1. */
static int symbol_point_pc(const behavior_function *f, int start)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_SOURCE_POINT &&
            f->source_points[i->left].kind == SCRIPT_RECORD_SYMBOL &&
            f->source_points[i->left].start == start) {
            return pc;
        }
    }
    return -1;
}
static const char *call_intrinsic(const node_host_call *call, arena *storage, pin_value *args, int count)
{
    pin_value out = { .type = PIN_BOOL };
    return call->value(storage, args, count, &out);
}
/* A local array of plain scalars changes in place without boxing: a byte
 * costs a byte, and extending one packed array by another copies bytes. */
TEST_CASE(row_array_owned_scalars_stay_packed)
{
    /* Local arrays live on a collectable managed heap. */
    arena storage;
    arena_init_collectable(&storage);
    pin_value bytes = { .type = PIN_U8, .container = PIN_CONTAINER_ARRAY };
    for (int i = 0; i < 20; i++) {
        pin_value args[] = { { .u64 = (uint64_t)(uintptr_t)&bytes },
                             { .type = PIN_U8, .u8 = (uint8_t)i } };
        REQUIRE(!call_intrinsic(&EXECUTION_array_push_call, &storage, args, 2));
    }
    pin_array *array = bytes.ptr;
    REQUIRE(array->stride == 1 && array->count == 20 && !array->items);
    pin_value store[] = { { .u64 = (uint64_t)(uintptr_t)&bytes },
                          { .type = PIN_I32, .i = 3 },
                          { .type = PIN_U8, .u8 = 200 } };
    REQUIRE(!call_intrinsic(&EXECUTION_array_store_call, &storage, store, 3));
    pin_value extend[] = { { .u64 = (uint64_t)(uintptr_t)&bytes }, bytes };
    REQUIRE(!call_intrinsic(&EXECUTION_array_extend_call, &storage, extend, 2));
    REQUIRE(array->stride == 1 && array->count == 40);
    for (int i = 0; i < 40; i++) {
        REQUIRE(pin_array_get(array, i).u8 == (i % 20 == 3 ? 200 : i % 20));
    }
    /* Boxed items of the same type still join a packed array inline. */
    pin_array *boxed = pin_array_snapshot_allocate(&storage, PIN_U8, NULL, 2);
    boxed->items[0] = (pin_value){ .type = PIN_U8, .u8 = 7 };
    boxed->items[1] = (pin_value){ .type = PIN_U8, .u8 = 8 };
    pin_value more[] = { { .u64 = (uint64_t)(uintptr_t)&bytes },
                         { .type = PIN_U8, .container = PIN_CONTAINER_ARRAY, .ptr = boxed } };
    REQUIRE(!call_intrinsic(&EXECUTION_array_extend_call, &storage, more, 2));
    REQUIRE(array->stride == 1 && array->count == 42 && pin_array_get(array, 41).u8 == 8);
    /* An item of another type is rejected and leaves the array as it was,
     * as is a store of one. */
    pin_value wide[] = { { .u64 = (uint64_t)(uintptr_t)&bytes },
                         { .type = PIN_I32, .i = 1000 } };
    REQUIRE(call_intrinsic(&EXECUTION_array_push_call, &storage, wide, 2));
    pin_value replace[] = { { .u64 = (uint64_t)(uintptr_t)&bytes },
                            { .type = PIN_I32, .i = 0 },
                            { .type = PIN_I32, .i = 9 } };
    REQUIRE(call_intrinsic(&EXECUTION_array_store_call, &storage, replace, 3));
    REQUIRE(array->stride == 1 && array->count == 42);
    REQUIRE(pin_array_get(array, 0).u8 == 0 && pin_array_get(array, 3).u8 == 200);
    arena_free(&storage);
    return true;
}
TEST_CASE(row_array_local_arrays_update_in_place)
{
    static const char source
        [] = "package arrays\n"
             "Row :: struct { a: int; b: string; }\n"
             "Holder :: union { Some(items: int[]) }\n"
             "pub Build :: (n: int) -> int {\n"
             "    items: int[] = [];\n"
             "    i: int = 0;\n"
             "    while (i < n) { items = append(items, i); i += 1; }\n"
             "    items[n - 1] = 7;\n"
             "    return length(items) + items[n - 1] + items[n - 2];\n"
             "}\n"
             "pub Alias :: () -> int {\n"
             "    a: int[] = [1, 2];\n"
             "    b := a;\n"
             "    a = append(a, int(3));\n"
             "    a[0] = 9;\n"
             "    return b[0] * 1000 + length(b) * 100 + a[0] * 10 + "
             "length(a);\n"
             "}\n"
             "Count :: (xs: int[]) -> int { return length(xs); }\n"
             "pub Borrow :: () -> int {\n"
             "    xs: int[] = [];\n"
             "    xs = append(xs, int(1));\n"
             "    n := Count(xs);\n"
             "    xs = append(xs, int(2));\n"
             "    return n * 10 + Count(xs);\n"
             "}\n"
             "pub Iterate :: () -> int {\n"
             "    xs: int[] = [1, 2, 3];\n"
             "    total: int = 0;\n"
             "    for (x in xs) { xs = append(xs, x); total += x; }\n"
             "    return total * 10 + length(xs);\n"
             "}\n"
             "pub Records :: () -> int {\n"
             "    rows: Row[] = [];\n"
             "    row: Row = {a: 1, b: \"x\"};\n"
             "    rows = append(rows, row);\n"
             "    row.a = 5;\n"
             "    rows[0] = {a: rows[0].a + 1, b: \"y\"};\n"
             "    return rows[0].a * 10 + row.a;\n"
             "}\n"
             "pub Extend :: () -> int {\n"
             "    xs: int[] = [1];\n"
             "    ys: int[] = [2, 3];\n"
             "    xs = extend(xs, ys);\n"
             "    ys[0] = 9;\n"
             "    xs = extend(xs, xs);\n"
             "    rows: Row[] = [];\n"
             "    row: Row = {a: 4, b: \"z\"};\n"
             "    rows = append(rows, row);\n"
             "    rows = append(rows, rows[0]);\n"
             "    return rows[1].a * 1000 + length(xs) * 100 + xs[1] * 10 + "
             "xs[5];\n"
             "}\n"
             "pub Named :: () -> int {\n"
             "    xs: int[] = [1];\n"
             "    ys: int[] = [2];\n"
             "    xs = extend(other_array: xs, array: ys);\n"
             "    return xs[0] * 10 + xs[1];\n"
             "}\n"
             "Bump :: (values: ^(int[])) -> int { values^ = "
             "append(values^, int(9)); return 5; }\n"
             "pub Order :: () -> int {\n"
             "    xs: int[] = [1];\n"
             "    xs = append(xs, Bump(&xs));\n"
             "    return length(xs) * 10 + xs[length(xs) - 1];\n"
             "}\n"
             "pub Rows :: (n: int) -> int {\n"
             "    rows: Row[] = [];\n"
             "    i: int = 0;\n"
             "    while (i < n) {\n"
             "        row: Row = {a: i, b: \"r\"};\n"
             "        rows = append(rows, row);\n"
             "        i += 1;\n"
             "    }\n"
             "    return rows[n - 1].a + length(rows);\n"
             "}\n"
             "pub Branch :: () -> bool {\n"
             "    xs: bool[] = [];\n"
             "    a := length(xs) == 0;\n"
             "    xs = append(xs, a && !a);\n"
             "    xs[0] = a || !a;\n"
             "    return length(xs) == 1;\n"
             "}\n"
             "pub Ready :: () -> bool {\n"
             "    xs: int[] = [];\n"
             "    xs = append(xs, int(1));\n"
             "    xs = extend(xs, xs);\n"
             "    xs[0] = 2;\n"
             "    return length(xs) == 2;\n"
             "}\n"
             "pub Payload :: () -> int {\n"
             "    xs: int[] = [1];\n"
             "    h := Holder.Some(xs);\n"
             "    xs = append(xs, int(2));\n"
             "    xs[0] = 7;\n"
             "    switch h { case .Some(items): return items[0] * 10 + "
             "length(items); }\n"
             "}\n";
    static const struct
    {
        const char *name;
        int argument, expected;
    } calls[] = { { "Build", 200000, 400005 }, { "Alias", 0, 1293 },
                  { "Borrow", 0, 12 },         { "Iterate", 0, 66 },
                  { "Records", 0, 25 },        { "Extend", 0, 4623 },
                  { "Named", 0, 21 },          { "Order", 0, 25 },
                  { "Rows", 100000, 199999 },  { "Payload", 0, 11 } };
    source_package_input inputs[] = { { "arrays/Arrays.chibi", source, sizeof source - 1 } };
    compiler_fixture fixture = { 0 };
    REQUIRE(compiler_fixture_prepare(&fixture, inputs, 1));
    for (int b = 0; b < 2; b++) {
        /* The append keeps the call's debugger point and watches and stays a
         * valid query predicate, even with branches in its value: it writes
         * only the function's own local array. */
        const execution_module
            *module = compiler_fixture_package(&fixture, b, "project:arrays")->module;
        const behavior_function *build = NULL, *ready = NULL, *branch = NULL;
        for (int f = 0; f < execution_module_function_count(module); f++) {
            execution_module_function_info info = execution_module_function_at(module, f);
            if (info.id ==
                compiler_fixture_export(&fixture, b, "project:arrays", "Build")) {
                build = info.body;
            }
            if (info.id ==
                compiler_fixture_export(&fixture, b, "project:arrays", "Ready")) {
                ready = info.body;
            }
            if (info.id ==
                compiler_fixture_export(&fixture, b, "project:arrays", "Branch")) {
                branch = info.body;
            }
        }
        REQUIRE(build && ready && branch);
        REQUIRE(execution_query_predicate_valid(ready, UINT_MAX));
        REQUIRE(execution_query_predicate_valid(branch, UINT_MAX));
        int call = (int)(strstr(source, "append(items, i)") - source);
        bool pointed = false;
        for (int p = 0; p < acount(build->source_points); p++) {
            pointed |= build->source_points[p].start == call;
        }
        REQUIRE(pointed);
        /* The array argument, both inputs and the result keep their watches,
         * and the assignment point follows the value. */
        int statement = (int)(strstr(source, "items = append(items, i)") - source);
        int argument = watch_pc(build, call + (int)strlen("append("), true, 0),
            array = watch_pc(build, call, false, 0),
            item = watch_pc(build, call, false, 1),
            assigned = symbol_point_pc(build, statement),
            result = watch_pc(build, call, true, 0);
        REQUIRE(argument >= 0 && array > argument && item > array);
        REQUIRE(assigned > item && result > assigned);
    }
    /* Program activations collect their storage; record items must not walk it. */
    for (int run = 0; run < 4; run++) {
        int b = run % 2;
        fixture.collectable = run >= 2;
        for (size_t i = 0; i < sizeof calls / sizeof *calls; i++) {
            arena detached = { 0 };
            pin_value argument = { .type = PIN_I32, .i = calls[i].argument },
                      out = { 0 };
            bool takes = !strcmp(calls[i].name, "Build") ||
                         !strcmp(calls[i].name, "Rows");
            bool ok = compiler_fixture_call(
                &fixture,
                b,
                "project:arrays",
                calls[i].name,
                takes ? &argument : NULL,
                takes ? 1 : 0,
                100000000,
                &detached,
                &out
            );
            REQUIRE(ok && out.type == PIN_I32 && out.i == calls[i].expected);
            arena_free(&detached);
        }
    }
    compiler_fixture_free(&fixture);
    return true;
}
void test_language_row_array()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(row_array_direct_cost_and_aliases);
    RUN_TEST_CASE(row_array_rejection_atomicity);
    RUN_TEST_CASE(row_array_call_collectable_results);
    RUN_TEST_CASE(row_array_authored_construction);
    RUN_TEST_CASE(row_array_authored_retained_prefix);
    RUN_TEST_CASE(row_array_owned_scalars_stay_packed);
    RUN_TEST_CASE(row_array_local_arrays_update_in_place);
}
