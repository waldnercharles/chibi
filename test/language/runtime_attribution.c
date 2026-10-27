#include "backend.h"
#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/status.h"
#include "execution/wasm_jit.h"
#include "language/lower.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* This dispatcher has a fixed source-reviewed envelope. No environment-driven
 * sizes, worker pool, files, frames, sleeps or repeated-until-fast loops. */
typedef struct
{
    const char *name;
    int32_t number;
} ProbeInner;
typedef struct
{
    const char *label;
    ProbeInner inner;
} ProbeRecord;
typedef struct
{
    pin_union *value;
} ProbeEnvelope;
typedef struct
{
    const char *text;
} BrokenRecord;
static const type_field inner_fields[] = {
    { .name = "name",
      .type = PIN_NAME,
      .offset = offsetof(ProbeInner, name),
      .size = sizeof(const char *) },
    { .name = "number",
      .type = PIN_I32,
      .offset = offsetof(ProbeInner, number),
      .size = sizeof(int32_t) },
};
static const type_desc inner_type = { .name = "ProbeInner",
                                      .size = sizeof(ProbeInner),
                                      .fields = inner_fields,
                                      .field_count = 2 };
static const type_field record_fields[] = {
    { .name = "label",
      .type = PIN_STR,
      .offset = offsetof(ProbeRecord, label),
      .size = sizeof(const char *) },
    { .name = "inner",
      .type = PIN_STRUCT,
      .type_name = "ProbeInner",
      .offset = offsetof(ProbeRecord, inner),
      .size = sizeof(ProbeInner) },
};
static const type_desc record_type = { .name = "ProbeRecord",
                                       .size = sizeof(ProbeRecord),
                                       .fields = record_fields,
                                       .field_count = 2 };
static const type_field envelope_field = { .name = "value",
                                           .type = PIN_UNION,
                                           .offset = offsetof(ProbeEnvelope, value),
                                           .size = sizeof(void *) };
static const type_desc envelope_type = { .name = "ProbeEnvelope",
                                         .size = sizeof(ProbeEnvelope),
                                         .fields = &envelope_field,
                                         .field_count = 1 };
static const type_field broken_fields[] = {
    { .name = "text",
      .type = PIN_STR,
      .offset = offsetof(BrokenRecord, text),
      .size = sizeof(const char *) },
    { .name = "invalid", .type = PIN_I8, .offset = sizeof(BrokenRecord) + 1, .size = 1 },
};
static const type_desc broken_type = { .name = "BrokenRecord",
                                       .size = sizeof(BrokenRecord),
                                       .fields = broken_fields,
                                       .field_count = 2 };
static const type_desc *const probe_types[] = { &inner_type,
                                                &record_type,
                                                &envelope_type,
                                                &broken_type };
static const type_catalog_pack probe_pack = { .types = probe_types, .type_count = 4 };
static struct
{
    pin_copy_scope copy[2];
    behavior_run_scope run[2];
    arena input, output;
    pin_value heap[5];
    pin_array *array;
    void *producer[3];
    ecs_world *world[2];
    behavior_function_builder *builder;
    behavior_function *function[2];
    behavior_function_context context;
    pin_value argument, result;
    execution_artifact *saved_artifact;
    unsigned saved_inspection_state;
    bool project_types, measured;
    uint64_t header_sum;
    int operations;
} fixture;

static void workload_cleanup();
static void cleanup_attribution()
{
    /* Every active object below outlives REQUIRE's early return. */
    for (int i = 1; i >= 0; i--) {
        if (fixture.copy[i].active) {
            pin_copy_scope_end(&fixture.copy[i]);
        }
        if (fixture.run[i].active) {
            behavior_run_scope_end(&fixture.run[i]);
        }
    }
    if (fixture.saved_artifact) {
        atomic_store_explicit(
            &fixture.saved_artifact->inspection_state,
            fixture.saved_inspection_state,
            memory_order_release
        );
        fixture.saved_artifact = NULL;
    }
    workload_cleanup();
    behavior_function_release_host(&fixture.context);
    if (fixture.builder) {
        behavior_function_builder_free(fixture.builder);
    }
    for (int i = 0; i < 2; i++) {
        behavior_function_free(fixture.function[i]);
        ecs_world_free(fixture.world[i]);
    }
    for (int i = 0; i < 5; i++) {
        pin_value_destroy(&fixture.heap[i]);
    }
    pin_array_free(fixture.array);
    for (int i = 0; i < 3; i++) {
        free(fixture.producer[i]);
    }
    arena_free(&fixture.input);
    arena_free(&fixture.output);
    if (fixture.project_types) {
        type_catalog_set_project(NULL);
    }
    memset(&fixture, 0, sizeof fixture);
}

static void copy_begin(bool enabled)
{
    pin_copy_scope_reset(&fixture.copy[0]);
    fixture.measured = enabled;
    if (enabled) {
        pin_copy_scope_begin(&fixture.copy[0]);
    }
}
static bool copy_buffer_end(uint64_t headers, uint64_t text, uint64_t nominal, uint64_t record, uint64_t buffer)
{
    if (fixture.measured) {
        pin_copy_scope_end(&fixture.copy[0]);
    } else {
        headers = text = nominal = record = buffer = 0;
    }
    pin_copy_counts counts = fixture.copy[0].counts;
    if (counts.overflow || counts.value_header_moves != headers ||
        counts.text_bytes != text || counts.nominal_bytes != nominal ||
        counts.record_bytes != record || counts.value_buffer_bytes != buffer) {
        fprintf(
            stderr,
            "copy operation=%d measured=%d actual H=%" PRIu64 " T=%" PRIu64
            " N=%" PRIu64 " R=%" PRIu64 " B=%" PRIu64 " overflow=%d; "
            "expected H=%" PRIu64 " T=%" PRIu64 " N=%" PRIu64 " R=%" PRIu64
            " B=%" PRIu64 "\n",
            fixture.operations + 1,
            fixture.measured,
            counts.value_header_moves,
            counts.text_bytes,
            counts.nominal_bytes,
            counts.record_bytes,
            counts.value_buffer_bytes,
            counts.overflow,
            headers,
            text,
            nominal,
            record,
            buffer
        );
    }
    REQUIRE(!counts.overflow);
    REQUIRE(counts.value_header_moves == headers);
    REQUIRE(counts.text_bytes == text);
    REQUIRE(counts.nominal_bytes == nominal);
    REQUIRE(counts.record_bytes == record);
    REQUIRE(counts.value_buffer_bytes == buffer);
    fixture.header_sum += counts.value_header_moves;
    fixture.operations++;
    return true;
}
static bool copy_end(uint64_t headers, uint64_t text, uint64_t nominal, uint64_t record)
{
    return copy_buffer_end(headers, text, nominal, record, 0);
}
static void producer_record()
{
    fixture.producer[0] = xmalloc(sizeof(ProbeRecord));
    fixture.producer[1] = xstrdup("one");
    fixture.producer[2] = xstrdup("two");
    *(ProbeRecord *)fixture.producer[0] = (ProbeRecord){
        .label = fixture.producer[1],
        .inner = { .name = fixture.producer[2], .number = 17 }
    };
}
static void release_producer()
{
    ((char *)fixture.producer[1])[0] = 'X';
    ((char *)fixture.producer[2])[0] = 'Y';
    ((ProbeRecord *)fixture.producer[0])->inner.number = 99;
    for (int i = 0; i < 3; i++) {
        free(fixture.producer[i]);
        fixture.producer[i] = NULL;
    }
}
static bool record_survives(const pin_value *value)
{
    REQUIRE(value->type == PIN_STRUCT && !strcmp(value->type_name, "ProbeRecord"));
    const ProbeRecord *record = value->ptr;
    REQUIRE(record && !strcmp(record->label, "one"));
    REQUIRE(!strcmp(record->inner.name, "two") && record->inner.number == 17);
    return true;
}

TEST_CASE(runtime_attribution_copy_sites)
{
    type_catalog_set_project(&probe_pack);
    fixture.project_types = true;
    for (int mode = 0; mode < 2; mode++) {
        fixture.operations = 0;
        fixture.header_sum = 0;
        bool enabled = mode != 0;
        pin_value scalar = { .type = PIN_I32, .i = 37 }, result = { 0 };
        /* S1: H3; H3+H1. */
        copy_begin(enabled);
        REQUIRE(pin_value_copy(&fixture.heap[0], &scalar) == 0);
        REQUIRE(copy_end(1, 0, 0, 0));
        REQUIRE(fixture.heap[0].i == 37);
        pin_value_destroy(&fixture.heap[0]);
        fixture.array = pin_array_new(PIN_I32, NULL, 0);
        copy_begin(enabled);
        REQUIRE(pin_array_push(fixture.array, &scalar) == 0);
        REQUIRE(copy_end(2, 0, 0, 0));
        REQUIRE(fixture.array->count == 1 && pin_array_get(fixture.array, 0).i == 37);
        pin_array_free(fixture.array);
        fixture.array = NULL;
        /* S2: null, empty, heap text, ordinary arena text, owning empty. */
        const char *texts[] = { NULL, "", "abc" };
        for (int i = 0; i < 3; i++) {
            const pin_string borrowed = { texts[i] ? texts[i] : "",
                                          texts[i] ? (uint32_t)strlen(texts[i]) : 0 };
            pin_value source = { .type = PIN_STR, .text = texts[i] ? &borrowed : NULL };
            copy_begin(enabled);
            REQUIRE(pin_value_copy(&fixture.heap[0], &source) == 0);
            /* Owning copies materialize a terminator for both empty forms. */
            REQUIRE(copy_end(1, i < 2 ? 1 : 4, 0, 0));
            REQUIRE(pin_value_equal(&source, &fixture.heap[0]));
            pin_value_destroy(&fixture.heap[0]);
        }
        pin_value text = { .type = PIN_STR,
                           .text = &(const pin_string){ .bytes = "abc",
                                                        .length = sizeof("abc") - 1 } };
        copy_begin(enabled);
        REQUIRE(pin_value_copy_to_arena(&fixture.output, &result, &text) == 0);
        REQUIRE(copy_end(1, 4, 0, 0));
        REQUIRE(pin_value_equal(&text, &result));
        text.text = &(const pin_string){ .bytes = "", .length = (uint32_t)strlen("") };
        copy_begin(enabled);
        REQUIRE(pin_value_snapshot_to_arena(&fixture.output, &result, &text) == 0);
        REQUIRE(copy_end(1, 1, 0, 0));
        REQUIRE(pin_value_equal(&text, &result));
        /* S3: two distinct borrowed C arrays, three scalar elements each. */
        pin_value numbers[] = { scalar, scalar, scalar };
        pin_array source_array = { .element_type = PIN_I32,
                                   .count = 3,
                                   .capacity = 3,
                                   .items = numbers };
        pin_value array_value = { .type = PIN_I32,
                                  .container = PIN_CONTAINER_ARRAY,
                                  .ptr = &source_array };
        copy_begin(enabled);
        REQUIRE(pin_value_copy(&fixture.heap[0], &array_value) == 0);
        REQUIRE(copy_end(4, 0, 0, 0));
        REQUIRE(pin_value_equal(&array_value, &fixture.heap[0]));
        pin_value_destroy(&fixture.heap[0]);
        pin_value handles[3];
        for (int i = 0; i < 3; i++) {
            handles[i] = (pin_value){ .type = PIN_HANDLE,
                                      .type_name = "Elem",
                                      .u64 = (uint64_t)i + 1 };
        }
        source_array = (pin_array){ .element_type = PIN_HANDLE,
                                    .type_name = "Elem",
                                    .count = 3,
                                    .capacity = 3,
                                    .items = handles };
        array_value = (pin_value){ .type = PIN_HANDLE,
                                   .type_name = "Elem",
                                   .container = PIN_CONTAINER_ARRAY,
                                   .ptr = &source_array };
        copy_begin(enabled);
        pin_array *cloned = pin_array_clone_to_arena(&fixture.output, &source_array);
        REQUIRE(copy_end(3, 0, 5, 0));
        REQUIRE(cloned && pin_array_equal(cloned, &source_array));
        copy_begin(enabled);
        REQUIRE(pin_value_copy_to_arena(&fixture.output, &result, &array_value) == 0);
        REQUIRE(copy_end(4, 0, 5, 0));
        REQUIRE(pin_value_equal(&result, &array_value));
        copy_begin(enabled);
        REQUIRE(pin_value_snapshot_to_arena(&fixture.output, &result, &array_value) == 0);
        /* The owning snapshot packs three uint64 handles. Only the outer
         * value moves a header; outer and array each own one "Elem" name.
         * Three scalar payload writes account for exactly 3*sizeof(uint64_t). */
        REQUIRE(copy_buffer_end(1, 0, 2 * sizeof("Elem"), 0, 3 * sizeof(uint64_t)));
        const pin_array *packed = result.ptr;
        REQUIRE(
            packed && packed->stride == sizeof(uint64_t) && packed->data &&
            !packed->items
        );
        REQUIRE(packed->count == 3);
        REQUIRE(pin_value_equal(&result, &array_value));
        /* S4: actual delegated descriptor assignments remain distinct. */
        pin_value children[] = { scalar, scalar }, union_value;
        REQUIRE(pin_union_value(&fixture.input, 7, children, 2, &union_value) == 0);
        union_value.type_name = "U";
        copy_begin(enabled);
        REQUIRE(pin_union_copy(&fixture.heap[0], &union_value) == 0);
        REQUIRE(copy_end(3, 0, 0, 0));
        REQUIRE(pin_value_equal(&fixture.heap[0], &union_value));
        pin_value_destroy(&fixture.heap[0]);
        copy_begin(enabled);
        REQUIRE(pin_value_copy(&fixture.heap[0], &union_value) == 0);
        REQUIRE(copy_end(4, 0, 0, 0));
        REQUIRE(pin_value_equal(&fixture.heap[0], &union_value));
        pin_value_destroy(&fixture.heap[0]);
        copy_begin(enabled);
        REQUIRE(pin_value_copy_to_arena(&fixture.output, &result, &union_value) == 0);
        REQUIRE(copy_end(4, 0, 2, 0));
        REQUIRE(pin_value_equal(&result, &union_value));
        copy_begin(enabled);
        REQUIRE(pin_value_snapshot_to_arena(&fixture.output, &result, &union_value) == 0);
        REQUIRE(copy_end(3, 0, 2, 0));
        REQUIRE(pin_value_equal(&result, &union_value));
        /* S5: a borrowed record descriptor versus an owning snapshot. */
        producer_record();
        pin_value record = { .type = PIN_STRUCT,
                             .type_name = "ProbeRecord",
                             .ptr = fixture.producer[0] };
        copy_begin(enabled);
        REQUIRE(pin_value_copy(&fixture.heap[0], &record) == 0);
        REQUIRE(copy_end(1, 0, 0, 0));
        REQUIRE(fixture.heap[0].ptr == record.ptr);
        pin_value_destroy(&fixture.heap[0]);
        copy_begin(enabled);
        REQUIRE(pin_value_snapshot_to_arena(&fixture.output, &result, &record) == 0);
        REQUIRE(copy_end(1, 8, 12, sizeof(ProbeRecord)));
        REQUIRE(result.ptr != record.ptr);
        release_producer();
        REQUIRE(record_survives(&result));
        /* S6: existing whole-component reference selects the captured layout. */
        producer_record();
        fixture.world[0] = ecs_world_new();
        const ecs_component_desc component = { .id = 9711,
                                               .key = "probe.record",
                                               .name = "ProbeRecord",
                                               .element_size = sizeof(ProbeRecord),
                                               .type = &record_type,
                                               .layout_hash = type_layout_hash(&record_type),
                                               .index = ECS_INDEX_DENSE };
        REQUIRE(ecs_world_register_component(fixture.world[0], &component, NULL) == ECS_OK);
        ecs_entity owner = ecs_world_create_entity(fixture.world[0]);
        REQUIRE(ecs_world_record_add(fixture.world[0], owner, component.id, NULL, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(fixture.world[0]) == ECS_OK);
        *(ProbeRecord *)ecs_component_data(fixture.world[0], owner, component.id) = *(
                                                                                         ProbeRecord *
        )fixture.producer[0];
        fixture.context.world = ecs_world_execution(fixture.world[0]);
        pin_value arguments[9] = {
            { .type = BF_POINTER_TYPE, .u64 = (uintptr_t)&fixture.context },
            { .type = PIN_STRUCT, .type_name = "ProbeRecord" },
            { .type = PIN_I32, .i = LANGUAGE_REFERENCE_COMPONENT },
            { .type = PIN_I32, .i = -1 },
            { .type = BF_POINTER_TYPE },
            { .type = PIN_HANDLE, .u64 = ecs_entity_to_u64(owner) },
            { .type = PIN_U64, .u64 = component.id },
            { .type = PIN_U64, .u64 = component.layout_hash },
            { .type = PIN_U64, .u64 = sizeof(ProbeRecord) },
        }, reference;
        REQUIRE(!EXECUTION_reference_make_call.value(&fixture.input, arguments, 9, &reference));
        copy_begin(enabled);
        REQUIRE(!EXECUTION_reference_read_call.value(&fixture.output, &reference, 1, &result));
        REQUIRE(copy_end(1, 8, 12, sizeof(ProbeRecord)));
        ((ProbeRecord *)ecs_component_data(fixture.world[0], owner, component.id))
            ->inner.number = 99;
        REQUIRE(record_survives(&result));
        ecs_world_free(fixture.world[0]);
        fixture.world[0] = NULL;
        fixture.context.world = NULL;
        release_producer();
        REQUIRE(record_survives(&result));
        /* S7: one packed handle shares the owned set's sole nominal name.
         * No outer pin_value or stored item descriptor is copied. */
        pin_array *set = NULL;
        pin_value handle = { .type = PIN_HANDLE, .type_name = "ProbeHandle", .u64 = 1 };
        copy_begin(enabled);
        REQUIRE(pin_set_add_owned(&fixture.output, &set, &handle) == 1);
        REQUIRE(copy_buffer_end(0, 0, sizeof("ProbeHandle"), 0, sizeof(uint64_t)));
        REQUIRE(set && set->count == 1 && set->stride == sizeof(uint64_t));
        REQUIRE(set->data && !set->items);
        pin_value item = pin_array_get(set, 0);
        REQUIRE(pin_value_equal(&item, &handle));
        copy_begin(enabled);
        REQUIRE(pin_set_add_owned(&fixture.output, &set, &handle) == 0);
        REQUIRE(copy_end(0, 0, 0, 0));
        REQUIRE(set->count == 1);
        /* S8: root record and union child snapshots, with no duplicate bytes. */
        children[1] = (pin_value){
            .type = PIN_STR,
            .text = &(const pin_string){ .bytes = "kid", .length = sizeof("kid") - 1 }
        };
        REQUIRE(pin_union_value(&fixture.input, 7, children, 2, &union_value) == 0);
        ProbeEnvelope envelope = { .value = union_value.ptr };
        record = (pin_value){ .type = PIN_STRUCT,
                              .type_name = "ProbeEnvelope",
                              .ptr = &envelope };
        copy_begin(enabled);
        REQUIRE(pin_value_snapshot_to_arena(&fixture.output, &result, &record) == 0);
        REQUIRE(copy_end(4, 4, 14, sizeof(ProbeEnvelope)));
        REQUIRE(pin_value_equal(&record, &result));
        /* F1: fail before reading the invalid field, retain previous traffic. */
        BrokenRecord broken = { .text = "ok" };
        record = (pin_value){ .type = PIN_STRUCT,
                              .type_name = "BrokenRecord",
                              .ptr = &broken };
        copy_begin(enabled);
        REQUIRE(pin_value_snapshot_to_arena(&fixture.output, &result, &record) == -1);
        REQUIRE(copy_end(1, 3, 13, sizeof(BrokenRecord)));
        REQUIRE(fixture.operations == 22 && fixture.header_sum == (enabled ? 42 : 0));
        arena_free(&fixture.input);
        arena_free(&fixture.output);
    }
    return true;
}

/* Reporting arithmetic is deliberately local to this diagnostic consumer. */
typedef struct
{
    uint64_t value;
    bool overflow;
} attribution_total;
static void total_add(attribution_total *total, uint64_t value, bool overflow)
{
    total->overflow |= overflow;
    if (UINT64_MAX - total->value < value) {
        total->value = UINT64_MAX;
        total->overflow = true;
        return;
    }
    total->value += value;
}
static attribution_total copy_payload_total(pin_copy_counts counts)
{
    attribution_total total = { .overflow = counts.overflow };
    total_add(&total, counts.text_bytes, false);
    total_add(&total, counts.nominal_bytes, false);
    total_add(&total, counts.record_bytes, false);
    return total;
}
static attribution_total backend_entry_total(behavior_run_counts counts)
{
    attribution_total total = { .overflow = counts.overflow };
    total_add(&total, counts.bytecode_entries, false);
    total_add(&total, counts.sljit_entries, false);
    total_add(&total, counts.wasm_entries, false);
    return total;
}
static bool exact_delta(attribution_total before, attribution_total after, uint64_t *delta)
{
    if (before.overflow || after.overflow || before.value > after.value) {
        return false;
    }
    *delta = after.value - before.value;
    return true;
}
static bool build_scalar(int index)
{
    fixture.builder = behavior_function_builder_new();
    pin_value scalar = { .type = PIN_I32 };
    behavior_function_builder_signature(fixture.builder, &scalar, 1, &scalar, 1, NULL, 0);
    int slot = behavior_function_builder_slot(fixture.builder, PIN_I32);
    behavior_function_builder_emit(
        fixture.builder,
        (
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = slot, .value = scalar }
    );
    behavior_function_builder_emit(
        fixture.builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = slot }
    );
    behavior_function_builder_emit(fixture.builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(fixture.builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    fixture.function[index] = behavior_function_builder_finish(fixture.builder, error, sizeof error);
    if (!fixture.function[index]) {
        fprintf(stderr, "attribution scalar builder: %s\n", error);
    }
    behavior_function_builder_free(fixture.builder);
    fixture.builder = NULL;
    REQUIRE(fixture.function[index]);
    REQUIRE(acount(fixture.function[index]->instructions) <= 8);
    return true;
}
static int inspect_continue(void *, const behavior_function_point *)
{
    return 0;
}
static int scalar_entry(int index, bool compiled, bool inspected)
{
    behavior_function_release_host(&fixture.context);
    fixture.argument = (pin_value){ .type = PIN_I32, .i = 37 };
    fixture.result = (pin_value){ .type = PIN_I32 };
    fixture.context = (behavior_function_context){ .arguments = &fixture.argument,
                                                   .argument_count = 1,
                                                   .results = &fixture.result,
                                                   .budget = 32,
                                                   .inspect = inspected ? inspect_continue
                                                                        : NULL };
    behavior_function_bind_storage(&fixture.context, &fixture.output);
    if (!behavior_function_bind(fixture.function[index], &fixture.context, NULL, 0)) {
        return FLOW_ERR_COMPILE;
    }
    return behavior_function_run(fixture.function[index], &fixture.context, compiled);
}
static bool scalar_helper()
{
    REQUIRE(scalar_entry(0, false, false) == FLOW_OK);
    REQUIRE(fixture.result.i == 37);
    return true;
}
static bool copy_scalar_helper()
{
    pin_value input = { .type = PIN_I32, .i = 37 }, result;
    REQUIRE(pin_value_copy(&result, &input) == 0 && result.i == 37);
    return true;
}

TEST_CASE(runtime_attribution_scopes)
{
    pin_copy_scope *outer = &fixture.copy[0], *inner = &fixture.copy[1];
    REQUIRE(copy_scalar_helper()); /* 1: disabled */
    REQUIRE(outer->counts.value_header_moves == 0);
    pin_copy_scope_begin(outer);
    REQUIRE(copy_scalar_helper()); /* 2 */
    pin_copy_scope_begin(inner);
    pin_value text = { .type = PIN_STR,
                       .text = &(const pin_string){ .bytes = "abc",
                                                    .length = sizeof("abc") - 1 } };
    REQUIRE(pin_value_copy(&fixture.heap[0], &text) == 0); /* 3 */
    pin_copy_scope_end(inner);
    REQUIRE(inner->counts.value_header_moves == 1 && inner->counts.text_bytes == 4);
    pin_value_destroy(&fixture.heap[0]);
    REQUIRE(copy_scalar_helper()); /* 4 */
    REQUIRE(copy_scalar_helper()); /* 5: ordinary C reentry */
    pin_copy_scope_end(outer);
    REQUIRE(outer->counts.value_header_moves == 3 && outer->counts.text_bytes == 0);
    REQUIRE(copy_scalar_helper()); /* 6: disabled */
    REQUIRE(outer->counts.value_header_moves == 3);
    pin_copy_scope_begin(outer);
    REQUIRE(copy_scalar_helper()); /* 7: accumulate */
    pin_copy_scope_end(outer);
    REQUIRE(outer->counts.value_header_moves == 4);
    pin_copy_scope_reset(outer);
    pin_copy_scope_begin(outer);
    REQUIRE(copy_scalar_helper()); /* 8: reset */
    pin_copy_scope_end(outer);
    REQUIRE(outer->counts.value_header_moves == 1 && !outer->counts.overflow);
    outer->counts.value_header_moves = UINT64_MAX - 1;
    pin_copy_scope_begin(outer);
    REQUIRE(copy_scalar_helper()); /* 9 */
    REQUIRE(outer->counts.value_header_moves == UINT64_MAX && !outer->counts.overflow);
    REQUIRE(copy_scalar_helper()); /* 10 */
    pin_copy_scope_end(outer);
    REQUIRE(outer->counts.value_header_moves == UINT64_MAX && outer->counts.overflow);
    pin_copy_scope_reset(outer);
    REQUIRE(!outer->counts.overflow && outer->counts.value_header_moves == 0);

    REQUIRE(build_scalar(0));
    behavior_run_scope *run_outer = &fixture.run[0], *run_inner = &fixture.run[1];
    REQUIRE(scalar_helper()); /* 1: disabled */
    REQUIRE(run_outer->counts.bytecode_entries == 0);
    behavior_run_scope_begin(run_outer);
    REQUIRE(scalar_helper()); /* 2 */
    behavior_run_scope_begin(run_inner);
    REQUIRE(scalar_helper()); /* 3 */
    behavior_run_scope_end(run_inner);
    REQUIRE(run_inner->counts.bytecode_entries == 1);
    REQUIRE(scalar_helper()); /* 4 */
    REQUIRE(scalar_helper()); /* 5: ordinary C reentry */
    behavior_run_scope_end(run_outer);
    REQUIRE(run_outer->counts.bytecode_entries == 3);
    REQUIRE(scalar_helper()); /* 6: disabled */
    REQUIRE(run_outer->counts.bytecode_entries == 3);
    behavior_run_scope_begin(run_outer);
    REQUIRE(scalar_helper()); /* 7: accumulate */
    behavior_run_scope_end(run_outer);
    REQUIRE(run_outer->counts.bytecode_entries == 4);
    behavior_run_scope_reset(run_outer);
    behavior_run_scope_begin(run_outer);
    REQUIRE(scalar_helper()); /* 8: reset */
    behavior_run_scope_end(run_outer);
    REQUIRE(run_outer->counts.bytecode_entries == 1 && !run_outer->counts.overflow);
    run_outer->counts.bytecode_entries = UINT64_MAX - 1;
    behavior_run_scope_begin(run_outer);
    REQUIRE(scalar_helper()); /* 9 */
    REQUIRE(
        run_outer->counts.bytecode_entries == UINT64_MAX && !run_outer->counts.overflow
    );
    REQUIRE(scalar_helper()); /* 10 */
    behavior_run_scope_end(run_outer);
    REQUIRE(
        run_outer->counts.bytecode_entries == UINT64_MAX && run_outer->counts.overflow
    );
    REQUIRE(run_outer->counts.sljit_entries == 0 && run_outer->counts.wasm_entries == 0);
    behavior_run_scope_reset(run_outer);
    REQUIRE(run_outer->counts.bytecode_entries == 0 && !run_outer->counts.overflow);

    pin_copy_counts copies = { .text_bytes = UINT64_MAX - 2, .nominal_bytes = 2 };
    attribution_total total = copy_payload_total(copies);
    REQUIRE(total.value == UINT64_MAX && !total.overflow);
    copies.record_bytes = 1;
    total = copy_payload_total(copies);
    REQUIRE(total.value == UINT64_MAX && total.overflow);
    behavior_run_counts runs = { .bytecode_entries = UINT64_MAX - 2,
                                 .sljit_entries = 2 };
    total = backend_entry_total(runs);
    REQUIRE(total.value == UINT64_MAX && !total.overflow);
    runs.wasm_entries = 1;
    total = backend_entry_total(runs);
    REQUIRE(total.value == UINT64_MAX && total.overflow);
    attribution_total merged = { .value = UINT64_MAX - 1 };
    total_add(&merged, 2, false);
    REQUIRE(merged.value == UINT64_MAX && merged.overflow);
    merged = (attribution_total){ 0 };
    total_add(&merged, 1, true);
    total_add(&merged, 1, false);
    REQUIRE(merged.value == 2 && merged.overflow);
    uint64_t delta = 99;
    REQUIRE(!exact_delta(merged, total, &delta) && delta == 99);
    REQUIRE(!exact_delta((attribution_total){ 0 }, total, &delta));
    REQUIRE(!exact_delta(total, (attribution_total){ .value = UINT64_MAX }, &delta));
    REQUIRE(
        exact_delta((attribution_total){ .value = 1 }, (attribution_total){ .value = 3 }, &delta) &&
        delta == 2
    );
    return true;
}

static const type_field text_field = { .name = "text",
                                       .type = PIN_STR,
                                       .size = sizeof(const char *) };
static const type_desc text_type = { .name = "ProbeText",
                                     .size = sizeof(const char *),
                                     .fields = &text_field,
                                     .field_count = 1 };
static bool string_usage(ecs_world *world, uint64_t entries, uint64_t bytes, int capacity)
{
    ecs_string_usage usage = ecs_world_string_usage(world);
    REQUIRE(!usage.overflow && usage.entries == entries && usage.payload_bytes == bytes);
    REQUIRE(
        usage.table_backing_bytes ==
        (capacity ? sizeof(dyna_hdr) + (uint64_t)capacity * sizeof(char *) : 0)
    );
    return true;
}
TEST_CASE(runtime_attribution_world_strings)
{
    ecs_entity owner[2];
    const ecs_component_desc component = { .id = 9712,
                                           .key = "probe.text",
                                           .name = "ProbeText",
                                           .element_size = sizeof(const char *),
                                           .type = &text_type,
                                           .layout_hash = type_layout_hash(&text_type),
                                           .index = ECS_INDEX_DENSE };
    for (int i = 0; i < 2; i++) {
        fixture.world[i] = ecs_world_new();
        REQUIRE(string_usage(fixture.world[i], 0, 0, 0));
        REQUIRE(ecs_world_register_component(fixture.world[i], &component, NULL) == ECS_OK);
        owner[i] = ecs_world_create_entity(fixture.world[i]);
        REQUIRE(
            ecs_world_record_add(fixture.world[i], owner[i], component.id, NULL, 0) == ECS_OK
        );
        REQUIRE(ecs_world_apply_commands(fixture.world[i]) == ECS_OK);
        REQUIRE(
            ecs_component_write_field(
                fixture.world[i],
                owner[i],
                component.id,
                0,
                (pin_value){ .type = PIN_STR,
                             .text = &(const pin_string){
                                 .bytes = i ? "other" : "base",
                                 .length = (uint32_t)strlen(i ? "other" : "base") } }
            ) == ECS_OK
        );
    }
    const char *base = *(
        const char **
    )ecs_component_data(fixture.world[0], owner[0], component.id);
    REQUIRE(string_usage(fixture.world[0], 1, 5, 16));
    REQUIRE(string_usage(fixture.world[1], 1, 6, 16));
    ecs_string_usage before = ecs_world_string_usage(fixture.world[0]);
    attribution_total initial = { .value = before.payload_bytes,
                                  .overflow = before.overflow };
    total_add(&initial, before.table_backing_bytes, false);
    REQUIRE(!initial.overflow);
    uint64_t sampled_retained_max = initial.value;
    char text[40];
    for (int i = 0; i < 32; i++) {
        snprintf(text, sizeof text, "edit%02d", i);
        REQUIRE(
            ecs_component_write_field(
                fixture.world[0],
                owner[0],
                component.id,
                0,
                (pin_value){ .type = PIN_STR,
                             .text = &(const pin_string){ .bytes = text,
                                                          .length = (uint32_t)strlen(text) } }
            ) == ECS_OK
        );
        int entries = i + 2;
        REQUIRE(string_usage(
            fixture.world[0],
            entries,
            5 + (uint64_t)(i + 1) * 7,
            entries <= 12   ? 16
            : entries <= 24 ? 32
                            : 64
        ));
        ecs_string_usage sample = ecs_world_string_usage(fixture.world[0]);
        attribution_total retained = { .value = sample.payload_bytes,
                                       .overflow = sample.overflow };
        total_add(&retained, sample.table_backing_bytes, false);
        REQUIRE(!retained.overflow);
        if (retained.value > sampled_retained_max) {
            sampled_retained_max = retained.value;
        }
    }
    const char *last = *(
        const char **
    )ecs_component_data(fixture.world[0], owner[0], component.id);
    for (int i = 0; i < 32; i++) {
        REQUIRE(
            ecs_component_write_field(
                fixture.world[0],
                owner[0],
                component.id,
                0,
                (pin_value){
                    .type = PIN_STR,
                    .text = &(const pin_string){ .bytes = "edit31",
                                                 .length = sizeof("edit31") - 1 } }
            ) == ECS_OK
        );
        REQUIRE(
            *(const char **)
                ecs_component_data(fixture.world[0], owner[0], component.id) == last
        );
        REQUIRE(string_usage(fixture.world[0], 33, 229, 64));
    }
    REQUIRE(
        ecs_component_write_field(
            fixture.world[0],
            owner[0],
            component.id,
            0,
            (pin_value){ .type = PIN_STR,
                         .text = &(const pin_string){ .bytes = "base",
                                                      .length = sizeof("base") - 1 } }
        ) == ECS_OK
    );
    REQUIRE(
        *(const char **)ecs_component_data(fixture.world[0], owner[0], component.id) == base
    );
    REQUIRE(string_usage(fixture.world[0], 33, 229, 64));
    REQUIRE(string_usage(fixture.world[1], 1, 6, 16));
    ecs_string_usage after = ecs_world_string_usage(fixture.world[0]);
    uint64_t copied;
    REQUIRE(exact_delta(
        (attribution_total){ before.payload_bytes, before.overflow },
        (attribution_total){ after.payload_bytes, after.overflow },
        &copied
    ));
    REQUIRE(copied == 224);
    printf(
        "attribution world=A generation=1 intern_text_copy_bytes=%" PRIu64
        " retained_text=%" PRIu64 " table_backing=%" PRIu64
        " sampled_retained_max=%" PRIu64 "\n",
        copied,
        after.payload_bytes,
        after.table_backing_bytes,
        sampled_retained_max
    );
    ecs_world_free(fixture.world[0]);
    fixture.world[0] = ecs_world_new();
    REQUIRE(string_usage(fixture.world[0], 0, 0, 0));
    REQUIRE(string_usage(fixture.world[1], 1, 6, 16));
    return true;
}

static bool observed_entry(bool compiled, bool inspected, bool fallback)
{
    behavior_run_counts c = fixture.run[0].counts;
    REQUIRE(!c.overflow);
    REQUIRE(c.bytecode_entries == (compiled && !fallback ? 0 : 1));
#ifdef __EMSCRIPTEN__
    REQUIRE(c.wasm_entries == (compiled ? 1 : 0) && c.sljit_entries == 0);
    REQUIRE(c.inspected_wasm_entries == (compiled && inspected ? 1 : 0));
    REQUIRE(c.inspected_sljit_entries == 0 && c.inspection_bytecode_fallbacks == 0);
#else
    REQUIRE(c.sljit_entries == (compiled && !fallback ? 1 : 0) && c.wasm_entries == 0);
    REQUIRE(c.inspected_sljit_entries == (compiled && inspected && !fallback ? 1 : 0));
    REQUIRE(
        c.inspected_wasm_entries == 0 &&
        c.inspection_bytecode_fallbacks == (fallback ? 1 : 0)
    );
#endif
    return true;
}
static bool counted_entry(int index, bool compiled, bool inspected, bool fallback)
{
    behavior_run_scope_reset(&fixture.run[0]);
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
#endif
    behavior_run_scope_begin(&fixture.run[0]);
    REQUIRE(scalar_entry(index, compiled, inspected) == FLOW_OK);
    behavior_run_scope_end(&fixture.run[0]);
    REQUIRE(fixture.result.i == 37);
    REQUIRE(observed_entry(compiled, inspected, fallback));
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_get_stats(&after);
    REQUIRE(after.executions >= before.executions);
    REQUIRE(after.executions - before.executions == fixture.run[0].counts.wasm_entries);
#endif
    return true;
}
TEST_CASE(runtime_attribution_backend_entries)
{
    REQUIRE(build_scalar(0));
    REQUIRE(build_scalar(1));
    REQUIRE(behavior_function_compile(fixture.function[0]));
    REQUIRE(counted_entry(0, false, false, false)); /* A1 VM */
    REQUIRE(counted_entry(0, true, false, false));  /* A2 compiled */
    REQUIRE(counted_entry(0, true, true, false));   /* A3 inspected compiled */
    behavior_run_scope_reset(&fixture.run[0]);
    REQUIRE(scalar_entry(0, true, false) == FLOW_OK && fixture.result.i == 37); /* A4 off */
    REQUIRE(backend_entry_total(fixture.run[0].counts).value == 0);
#ifdef __EMSCRIPTEN__
    REQUIRE(!fixture.function[1]->artifact->wasm_jit);
    behavior_run_scope_begin(&fixture.run[0]);
    REQUIRE(scalar_entry(1, true, false) == FLOW_ERR_COMPILE); /* B1 uncompiled */
    behavior_run_scope_end(&fixture.run[0]);
    REQUIRE(backend_entry_total(fixture.run[0].counts).value == 0);
#else
    REQUIRE(behavior_function_compile(fixture.function[1]));
    execution_artifact *artifact = fixture.function[1]->artifact;
    REQUIRE(artifact->code && !artifact->inspection_code && !artifact->inspection_addresses);
    fixture.saved_inspection_state = atomic_load_explicit(&artifact->inspection_state, memory_order_acquire);
    REQUIRE(fixture.saved_inspection_state == EXECUTION_INSPECTION_UNCOMPILED);
    fixture.saved_artifact = artifact;
    atomic_store_explicit(&artifact->inspection_state, EXECUTION_INSPECTION_COMPILING, memory_order_release);
    REQUIRE(counted_entry(1, true, true, true)); /* B1 inspected VM fallback */
    atomic_store_explicit(&artifact->inspection_state, fixture.saved_inspection_state, memory_order_release);
    fixture.saved_artifact = NULL;
    REQUIRE(counted_entry(1, true, true, false)); /* B2 real inspected native */
    REQUIRE(artifact->inspection_code);
#endif
    return true;
}

enum
{
    WORKLOAD_START = 10,
    WORKLOAD_SAVE = 11,
    WORKLOAD_CYCLES = 8,
    WORKLOAD_TRIALS = 5,
    WORKLOAD_CALLS = 128,
    WORKLOAD_BUDGET = 1000,
    WORKLOAD_TEXT_BYTES = 64,
    WORKLOAD_OUTPUT_LIMIT = 512 * 1024,
};
static const script_catalog_parameter workload_start_parameter = {
    .name = "input",
    .type = { .storage = PIN_STR }
};
static const script_catalog_capability workload_events[] = {
    { .id = WORKLOAD_START,
      .name = "Start",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &workload_start_parameter,
      .parameter_count = 1 },
    { .id = WORKLOAD_SAVE, .name = "Save", .kind = SCRIPT_CAPABILITY_EVENT },
};
static const script_catalog workload_catalog = { .capabilities = workload_events,
                                                 .capability_count = 2 };
static const char
    workload_task_source[] = "Attribution :: behavior { worker: task(string); "
                             "text: string = \"\";Work "
                             ":: (input: string) -> string { yield; return "
                             "input; }on Start :: (input: string) { "
                             "worker = Work(input); }on Save :: () { result := "
                             "await worker; if result is "
                             "string value { text = value; } worker = nil; } }";
static script_syntax *workload_syntax[2];
static script_analysis *workload_analysis[2];
static execution_module_plan *workload_plan[2];
static execution_module *workload_module[2];
static execution_program *workload_program[4];
static execution_instance *workload_instance[4];
static ecs_world *workload_world[4];
static uint64_t workload_owner[4];
static uint64_t workload_text_state[2], workload_task_state[2];
static char *workload_producer;
static char workload_final_text[4][WORKLOAD_TEXT_BYTES];
static behavior_function_builder *workload_builder;
static behavior_function *workload_function[2];
static behavior_function_context workload_context[WORKLOAD_CALLS];
static pin_value workload_result[WORKLOAD_CALLS];
static arena workload_storage;
static pin_copy_scope workload_copy_scope;
static behavior_run_scope workload_run_scope;
static pin_value workload_input_items[] = {
    { .type = PIN_STR,
      .text = &(const pin_string){ .bytes = "one", .length = sizeof("one") - 1 } },
    { .type = PIN_STR,
      .text = &(const pin_string){ .bytes = "two", .length = sizeof("two") - 1 } },
    { .type = PIN_STR,
      .text = &(const pin_string){ .bytes = "three", .length = sizeof("three") - 1 } },
};
static pin_array workload_input_array = {
    .element_type = PIN_STR,
    .count = 3,
    .capacity = 3,
    .items = workload_input_items,
};
static const pin_value workload_input[2] = {
    { .type = PIN_I32, .i = 41 },
    { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = &workload_input_array },
};

static void workload_end_scopes()
{
    if (workload_run_scope.active) {
        behavior_run_scope_end(&workload_run_scope);
    }
    if (workload_copy_scope.active) {
        pin_copy_scope_end(&workload_copy_scope);
    }
}

static void workload_release_trial()
{
    for (int i = 0; i < WORKLOAD_CALLS; i++) {
        behavior_function_release_host(&workload_context[i]);
        workload_context[i] = (behavior_function_context){ 0 };
        workload_result[i] = (pin_value){ 0 };
    }
    arena_free(&workload_storage);
}

static void workload_cleanup()
{
    workload_end_scopes();
    workload_release_trial();
    free(workload_producer);
    workload_producer = NULL;
    for (int i = 0; i < 4; i++) {
        execution_program_free(workload_program[i]);
        workload_program[i] = NULL;
        workload_instance[i] = NULL;
        ecs_world_free(workload_world[i]);
        workload_world[i] = NULL;
        workload_owner[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        behavior_function_free(workload_function[i]);
        workload_function[i] = NULL;
        execution_module_free(workload_module[i]);
        workload_module[i] = NULL;
        execution_module_plan_free(workload_plan[i]);
        workload_plan[i] = NULL;
        script_analysis_free(workload_analysis[i]);
        workload_analysis[i] = NULL;
        script_syntax_free(workload_syntax[i]);
        workload_syntax[i] = NULL;
        workload_text_state[i] = workload_task_state[i] = 0;
    }
    if (workload_builder) {
        behavior_function_builder_free(workload_builder);
        workload_builder = NULL;
    }
    behavior_run_scope_reset(&workload_run_scope);
    pin_copy_scope_reset(&workload_copy_scope);
    memset(workload_final_text, 0, sizeof workload_final_text);
}

static bool workload_counts_empty(pin_copy_counts counts)
{
    return !counts.value_header_moves && !counts.value_buffer_bytes &&
           !counts.text_bytes && !counts.nominal_bytes &&
           !counts.record_bytes && !counts.overflow;
}

static bool workload_route(behavior_run_counts counts, behavior_backend backend, uint64_t expected)
{
    REQUIRE(!counts.overflow);
    REQUIRE(!counts.inspected_sljit_entries && !counts.inspected_wasm_entries);
    REQUIRE(!counts.inspection_bytecode_fallbacks);
    uint64_t entries = backend == BEHAVIOR_BYTECODE ? counts.bytecode_entries
                     : backend == BEHAVIOR_SLJIT    ? counts.sljit_entries
                                                    : counts.wasm_entries;
    REQUIRE(entries > 0);
    if (expected) {
        REQUIRE(entries == expected);
    }
    REQUIRE(backend == BEHAVIOR_BYTECODE || counts.bytecode_entries == 0);
    REQUIRE(backend == BEHAVIOR_SLJIT || counts.sljit_entries == 0);
    REQUIRE(backend == BEHAVIOR_WASM || counts.wasm_entries == 0);
    return true;
}

static void workload_print_counts(bool copy_observed, bool run_observed)
{
    pin_copy_counts copy = workload_copy_scope.counts;
    behavior_run_counts run = workload_run_scope.counts;
    printf(
        " copy_observed=%d H=%" PRIu64 " B=%" PRIu64 " T=%" PRIu64 " N=%" PRIu64
        " R=%" PRIu64 " copy_overflow=%d"
        " backend_observed=%d VM=%" PRIu64 " SLJIT=%" PRIu64 " WASM=%" PRIu64
        " inspected_SLJIT=%" PRIu64 " inspected_WASM=%" PRIu64
        " inspection_VM_fallback=%" PRIu64 " run_overflow=%d\n",
        copy_observed,
        copy.value_header_moves,
        copy.value_buffer_bytes,
        copy.text_bytes,
        copy.nominal_bytes,
        copy.record_bytes,
        copy.overflow,
        run_observed,
        run.bytecode_entries,
        run.sljit_entries,
        run.wasm_entries,
        run.inspected_sljit_entries,
        run.inspected_wasm_entries,
        run.inspection_bytecode_fallbacks,
        run.overflow
    );
}

static bool workload_prepare_module(int index, behavior_backend backend)
{
    workload_syntax[index] = script_parse(workload_task_source);
    workload_analysis[index] = script_analyze(workload_syntax[index], &workload_catalog);
    script_analysis *analysis = workload_analysis[index];
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "attribution task semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
    }
    REQUIRE(analysis->valid);
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, "text")) {
            workload_text_state[index] = symbol->id;
        }
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, "worker")) {
            workload_task_state[index] = symbol->id;
        }
    }
    REQUIRE(workload_text_state[index] && workload_task_state[index]);
    const script_lower_bindings bindings = { 0 };
    workload_plan[index] = script_lower(analysis, &workload_catalog, &bindings);
    execution_module_plan *plan = workload_plan[index];
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(stderr, "attribution task lower: %s\n", plan->diagnostics[i].message);
        }
    }
    REQUIRE(plan && plan->valid);
    char error[256];
    workload_module[index] = execution_module_finish(&workload_plan[index], backend, error, sizeof error);
    if (!workload_module[index]) {
        fprintf(stderr, "attribution task publish: %s\n", error);
    }
    REQUIRE(workload_module[index]);
    REQUIRE(execution_module_backend(workload_module[index]) == backend);
    return true;
}

TEST_CASE(runtime_attribution_tasks)
{
    REQUIRE(sizeof workload_task_source <= 1024);
    REQUIRE(behavior_backend_available(TEST_COMPILED_BACKEND));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    int boundaries = 0;
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(workload_prepare_module(backend, backends[backend]));
        for (int enabled = 0; enabled < 2; enabled++) {
            int index = 2 * backend + enabled;
            workload_world[index] = ecs_world_new();
            workload_program[index] = execution_program_new(
                workload_module[backend],
                workload_world[index],
                NULL
            );
            REQUIRE(workload_program[index]);
            workload_owner[index] = ecs_entity_to_u64(
                ecs_world_create_entity(workload_world[index])
            );
            workload_instance[index] = execution_program_attach(
                workload_program[index],
                workload_owner[index]
            );
            REQUIRE(workload_instance[index]);
            pin_copy_scope_reset(&workload_copy_scope);
            behavior_run_scope_reset(&workload_run_scope);
            execution_wasm_jit_stats before, after;
            execution_wasm_jit_get_stats(&before);
            behavior_run_scope_begin(&workload_run_scope);
            if (enabled) {
                pin_copy_scope_begin(&workload_copy_scope);
            }
            for (int cycle = 0; cycle < WORKLOAD_CYCLES; cycle++) {
                char expected[WORKLOAD_TEXT_BYTES];
                memset(expected, 'A' + cycle, sizeof expected - 1);
                expected[sizeof expected - 1] = '\0';
                workload_producer = xmalloc(sizeof expected);
                memcpy(workload_producer, expected, sizeof expected);
                pin_value argument = { .type = PIN_STR,
                                       .text = &(const pin_string){
                                           .bytes = workload_producer,
                                           .length = (uint32_t)strlen(workload_producer) } };
                double now = 3 * cycle;
                int status = execution_instance_dispatch(
                    workload_instance[index],
                    WORKLOAD_START,
                    &argument,
                    1,
                    now,
                    0,
                    WORKLOAD_BUDGET
                );
                boundaries++;
                REQUIRE(status == FLOW_OK);
                const pin_value *handle = execution_instance_state(
                    workload_instance[index],
                    workload_task_state[backend]
                );
                REQUIRE(handle && handle->type == PIN_TASK && handle->u64);
                memset(workload_producer, '!', sizeof expected - 1);
                free(workload_producer);
                workload_producer = NULL;
                argument.text = NULL;
                status = execution_program_update(workload_program[index], now + 1, 0, WORKLOAD_BUDGET);
                boundaries++;
                REQUIRE(status == FLOW_OK);
                execution_memory_usage held = execution_program_memory_usage(
                    workload_program[index]
                );
                REQUIRE(held.retained_tasks == 1 && held.task_slots == 1);
                REQUIRE(held.task_result_bytes >= sizeof expected);
                status = execution_instance_dispatch(
                    workload_instance[index],
                    WORKLOAD_SAVE,
                    NULL,
                    0,
                    now + 1,
                    0,
                    WORKLOAD_BUDGET
                );
                boundaries++;
                REQUIRE(status == FLOW_OK);
                const pin_value *text = execution_instance_state(
                    workload_instance[index],
                    workload_text_state[backend]
                );
                REQUIRE(text && text->type == PIN_STR && pin_text(*text).bytes);
                REQUIRE(!strcmp(pin_text(*text).bytes, expected));
                handle = execution_instance_state(workload_instance[index], workload_task_state[backend]);
                REQUIRE(handle && !handle->u64);
                status = execution_program_update(workload_program[index], now + 2, 0, WORKLOAD_BUDGET);
                boundaries++;
                REQUIRE(status == FLOW_OK);
                execution_memory_usage released = execution_program_memory_usage(
                    workload_program[index]
                );
                REQUIRE(released.retained_tasks == 0 && released.task_result_bytes == 0);
                REQUIRE(released.task_slots == 1);
                text = execution_instance_state(workload_instance[index], workload_text_state[backend]);
                REQUIRE(text && pin_text(*text).bytes && !strcmp(pin_text(*text).bytes, expected));
                memcpy(workload_final_text[index], pin_text(*text).bytes, sizeof expected);
            }
            workload_end_scopes();
            execution_wasm_jit_get_stats(&after);
            REQUIRE(after.executions >= before.executions);
            REQUIRE(workload_route(workload_run_scope.counts, backends[backend], 0));
            REQUIRE(
                after.executions - before.executions ==
                workload_run_scope.counts.wasm_entries
            );
            REQUIRE(!workload_copy_scope.counts.overflow);
            if (enabled) {
                REQUIRE(
                    workload_copy_scope.counts.value_buffer_bytes ==
                    WORKLOAD_CYCLES * sizeof(pin_value)
                );
                REQUIRE(workload_copy_scope.counts.text_bytes > 0);
            } else {
                REQUIRE(workload_counts_empty(workload_copy_scope.counts));
            }
            printf(
                "attribution case=tasks backend=%s copy_mode=%s module=%" PRIu64
                " generation_ordinal=%d owner=%" PRIu64
                " phase=eight-cycles boundaries=32",
                behavior_backend_name(backends[backend]),
                enabled ? "on" : "off",
                execution_module_id(workload_module[backend]),
                backend + 1,
                workload_owner[index]
            );
            workload_print_counts(enabled, true);
        }
    }
    REQUIRE(boundaries == 128);
    for (int i = 1; i < 4; i++) {
        REQUIRE(!memcmp(workload_final_text[0], workload_final_text[i], WORKLOAD_TEXT_BYTES));
    }
    return true;
}

static int workload_emit(behavior_function_instruction instruction)
{
    return behavior_function_builder_emit(workload_builder, instruction);
}

static int workload_value(behavior_function_instruction instruction, behavior_value_kind kind)
{
    instruction
        .output = behavior_function_builder_slot(workload_builder, instruction.type);
    bool valid = behavior_function_builder_kind(
        workload_builder,
        BEHAVIOR_BANK_SLOT,
        instruction.output,
        kind
    );
    assert(valid);
    (void)valid;
    workload_emit(instruction);
    return instruction.output;
}

static int workload_host_address(int bank)
{
    return workload_value(
        (behavior_function_instruction){ .op = BF_HOST_SLOT,
                                         .type = BF_POINTER_TYPE,
                                         .left = bank },
        BEHAVIOR_ADDRESS
    );
}

static bool workload_build(int managed)
{
    workload_builder = behavior_function_builder_new();
    pin_value shape = workload_input[managed];
    memset(&shape.d, 0, sizeof shape.d);
    behavior_function_builder_signature(workload_builder, &shape, 1, &shape, 1, NULL, 0);
    if (!managed) {
        int argument = workload_value(
            (
                behavior_function_instruction
            ){ .op = BF_ARGUMENT, .type = PIN_I32, .left = 0, .value = shape },
            BEHAVIOR_VALUE
        );
        int one = workload_value(
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = PIN_I32,
                                             .value = { .type = PIN_I32, .i = 1 } },
            BEHAVIOR_VALUE
        );
        int sum = workload_value(
            (
                behavior_function_instruction
            ){ .op = BF_ADD, .type = PIN_I32, .left = argument, .right = one },
            BEHAVIOR_VALUE
        );
        workload_emit((
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = PIN_I32, .left = sum, .target = 0 });
    } else {
        int argument = workload_value(
            (behavior_function_instruction){ .op = BF_ARRAY,
                                             .type = BF_POINTER_TYPE,
                                             .left = 0,
                                             .target = BF_ARGUMENT,
                                             .value = shape },
            BEHAVIOR_ADDRESS
        );
        int host = behavior_function_builder_host(
            workload_builder,
            &EXECUTION_snapshot_call,
            &EXECUTION_value_access,
            &shape,
            1,
            &shape,
            1
        );
        uint64_t id = execution_intrinsic_id(&EXECUTION_snapshot_call);
        REQUIRE(
            behavior_function_builder_host_identity(workload_builder, host, id, execution_intrinsic_abi(id))
        );
        int address = workload_host_address(
            behavior_function_builder_host_first(workload_builder, host)
        );
        workload_emit((behavior_function_instruction){
            .op = BF_WRITE,
            .type = BF_POINTER_TYPE,
            .left = argument,
            .right = address,
            .offset = offsetof(pin_value, d) });
        workload_emit((
            behavior_function_instruction
        ){ .op = BF_HOST_CALL, .target = host, .left = BEHAVIOR_FUNCTION_HOST_DATA });
        address = workload_host_address(
            behavior_function_builder_host_results(workload_builder, host)
        );
        int snapshot = workload_value(
            (behavior_function_instruction){ .op = BF_LOAD,
                                             .type = BF_POINTER_TYPE,
                                             .left = address,
                                             .offset = offsetof(pin_value, d) },
            BEHAVIOR_ADDRESS
        );
        int name = workload_value(
            (behavior_function_instruction){ .op = BF_TYPE_NAME,
                                             .type = BF_POINTER_TYPE,
                                             .target = BF_CONSTANT,
                                             .value = { .type = PIN_NAME } },
            BEHAVIOR_ADDRESS
        );
        workload_emit((behavior_function_instruction){ .op = BF_RESULT_ARRAY,
                                                       .type = PIN_STR,
                                                       .left = snapshot,
                                                       .right = name,
                                                       .target = 0,
                                                       .value = shape });
    }
    workload_emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    workload_function[managed] = behavior_function_builder_finish(workload_builder, error, sizeof error);
    if (!workload_function[managed]) {
        fprintf(stderr, "attribution overhead builder: %s\n", error);
    }
    REQUIRE(workload_function[managed]);
    behavior_function_builder_free(workload_builder);
    workload_builder = NULL;
    REQUIRE(acount(workload_function[managed]->instructions) <= 32);
    REQUIRE(acount(workload_function[managed]->calls) == 0);
    REQUIRE(!behavior_function_resumable(workload_function[managed]));
    REQUIRE(behavior_function_compile(workload_function[managed]));
#ifdef __EMSCRIPTEN__
    REQUIRE(workload_function[managed]->artifact->wasm_jit);
#else
    REQUIRE(behavior_function_entry(workload_function[managed]));
#endif
    return true;
}

static bool workload_prepare_trial(int managed, int count)
{
    workload_release_trial();
    /* Retained output storage is fresh for every warmup/trial. Eager backing
     * and host/result banks are prepared outside both clocks and scopes. */
    if (managed) {
        arena_init(&workload_storage, 64 * 1024);
    }
    for (int i = 0; i < count; i++) {
        workload_result[i] = workload_input[managed];
        memset(&workload_result[i].d, 0, sizeof workload_result[i].d);
        workload_context[i] = (behavior_function_context){
            .arguments = &workload_input[managed],
            .argument_count = 1,
            .results = &workload_result[i],
            .budget = WORKLOAD_BUDGET
        };
        behavior_function_bind_storage(&workload_context[i], &workload_storage);
        REQUIRE(
            behavior_function_bind(workload_function[managed], &workload_context[i], NULL, 0)
        );
        behavior_function_prepare_host(workload_function[managed], &workload_context[i]);
        behavior_function_prepare_results(workload_function[managed], &workload_result[i], &workload_storage);
        REQUIRE(
            !behavior_function_inspection_required(workload_function[managed], &workload_context[i])
        );
    }
    pin_copy_scope_reset(&workload_copy_scope);
    behavior_run_scope_reset(&workload_run_scope);
    return true;
}

static uint64_t workload_checksum(int managed, const pin_value *result)
{
    if (!managed) {
        return (uint64_t)result->i;
    }
    const pin_array *array = result->ptr;
    if (!array || array->count != 3) {
        return 0;
    }
    uint64_t sum = (uint64_t)array->count;
    for (int i = 0; i < 3; i++) {
        sum += pin_text(pin_array_get(array, i)).bytes
                 ? (unsigned char)pin_text(pin_array_get(array, i)).bytes[0]
                 : 0;
    }
    return sum;
}

static bool workload_check_results(int managed, int count)
{
    for (int i = 0; i < count; i++) {
        REQUIRE(
            !behavior_function_inspection_required(workload_function[managed], &workload_context[i])
        );
        if (!managed) {
            REQUIRE(workload_result[i].type == PIN_I32 && workload_result[i].i == 42);
            continue;
        }
        REQUIRE(
            workload_result[i].type == PIN_STR && workload_result[i].container == PIN_CONTAINER_ARRAY
        );
        const pin_array *array = workload_result[i].ptr;
        REQUIRE(array && array != &workload_input_array);
        REQUIRE(array->count == 3 && array->element_type == PIN_STR && !array->type_name);
        REQUIRE(arena_contains(&workload_storage, array));
        REQUIRE(arena_contains(&workload_storage, array->items));
        for (int j = 0; j < 3; j++) {
            REQUIRE(
                pin_array_get(array, j).type == PIN_STR &&
                pin_text(pin_array_get(array, j)).bytes
            );
            REQUIRE(
                pin_text(pin_array_get(array, j)).bytes !=
                pin_text(workload_input_items[j]).bytes
            );
            REQUIRE(arena_contains(
                &workload_storage,
                pin_text(pin_array_get(array, j)).bytes
            ));
            REQUIRE(!strcmp(
                pin_text(pin_array_get(array, j)).bytes,
                pin_text(workload_input_items[j]).bytes
            ));
        }
    }
    size_t used, capacity;
    arena_usage(&workload_storage, &used, &capacity);
    REQUIRE(used <= WORKLOAD_OUTPUT_LIMIT);
    REQUIRE(managed ? used > 0 : used == 0);
    return true;
}

static bool workload_check_copy(int managed, int count, bool enabled)
{
    pin_copy_counts counts = workload_copy_scope.counts;
    REQUIRE(!counts.overflow);
    if (!enabled || !managed) {
        REQUIRE(workload_counts_empty(counts));
        return true;
    }
    REQUIRE(counts.value_header_moves == (uint64_t)count * 4);
    REQUIRE(counts.text_bytes == (uint64_t)count * 14);
    REQUIRE(!counts.nominal_bytes && !counts.record_bytes && !counts.value_buffer_bytes);
    return true;
}

static double workload_seconds()
{
    struct timespec now;
    int status = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(!status);
    (void)status;
    return (double)now.tv_sec + now.tv_nsec * 1e-9;
}

static bool workload_warm(int managed, behavior_backend backend, bool enabled)
{
    REQUIRE(workload_prepare_trial(managed, 1));
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
    /* Even the OFF warmup temporarily observes its actual backend. */
    behavior_run_scope_begin(&workload_run_scope);
    if (enabled) {
        pin_copy_scope_begin(&workload_copy_scope);
    }
    int status = behavior_function_run(
        workload_function[managed],
        &workload_context[0],
        backend != BEHAVIOR_BYTECODE
    );
    workload_end_scopes();
    execution_wasm_jit_get_stats(&after);
    REQUIRE(after.executions >= before.executions);
    REQUIRE(status == FLOW_OK);
    REQUIRE(workload_route(workload_run_scope.counts, backend, 1));
    REQUIRE(after.executions - before.executions == workload_run_scope.counts.wasm_entries);
    REQUIRE(workload_check_results(managed, 1));
    REQUIRE(workload_check_copy(managed, 1, enabled));
    printf(
        "attribution case=overhead phase=warmup workload=%s backend=%s "
        "copy_mode=%s module=n/a generation_ordinal=n/a owner=n/a",
        managed ? "managed-array-snapshot" : "scalar",
        behavior_backend_name(backend),
        enabled ? "on" : "off"
    );
    workload_print_counts(enabled, true);
    workload_release_trial();
    behavior_run_scope_reset(&workload_run_scope);
    pin_copy_scope_reset(&workload_copy_scope);
    return true;
}

static bool workload_trial(int managed, behavior_backend backend, bool enabled, int trial)
{
    REQUIRE(workload_prepare_trial(managed, WORKLOAD_CALLS));
    xalloc_stats before, after;
    execution_wasm_jit_stats wasm_before, wasm_after;
    xalloc_stats_get(&before);
    execution_wasm_jit_get_stats(&wasm_before);
    uint64_t blocks = arena_block_allocations();
    uint64_t checksum = 0;
    int status[WORKLOAD_CALLS];
    if (enabled) {
        pin_copy_scope_begin(&workload_copy_scope);
        behavior_run_scope_begin(&workload_run_scope);
    }
    double start = workload_seconds();
    for (int i = 0; i < WORKLOAD_CALLS; i++) {
        status[i] = behavior_function_run(
            workload_function[managed],
            &workload_context[i],
            backend != BEHAVIOR_BYTECODE
        );
        checksum += workload_checksum(managed, &workload_result[i]);
    }
    double elapsed = workload_seconds() - start;
    workload_end_scopes();
    xalloc_stats_get(&after);
    execution_wasm_jit_get_stats(&wasm_after);
    REQUIRE(wasm_after.executions >= wasm_before.executions);
    uint64_t final_blocks = arena_block_allocations();
    REQUIRE(
        final_blocks >= blocks && after.calls >= before.calls &&
        after.requested_bytes >= before.requested_bytes
    );
    uint64_t block_delta = final_blocks - blocks;
    size_t used, capacity;
    arena_usage(&workload_storage, &used, &capacity);
    size_t backing = arena_storage_bytes(&workload_storage);
    REQUIRE(elapsed >= 0);
    for (int i = 0; i < WORKLOAD_CALLS; i++) {
        REQUIRE(status[i] == FLOW_OK);
    }
    REQUIRE(checksum == (uint64_t)WORKLOAD_CALLS * (managed ? 346 : 42));
    REQUIRE(workload_check_results(managed, WORKLOAD_CALLS));
    REQUIRE(workload_check_copy(managed, WORKLOAD_CALLS, enabled));
    if (enabled) {
        REQUIRE(workload_route(workload_run_scope.counts, backend, WORKLOAD_CALLS));
        REQUIRE(
            wasm_after.executions - wasm_before.executions ==
            workload_run_scope.counts.wasm_entries
        );
    } else {
        behavior_run_counts counts = workload_run_scope.counts;
        REQUIRE(!counts.bytecode_entries && !counts.sljit_entries && !counts.wasm_entries);
        REQUIRE(!counts.inspected_sljit_entries && !counts.inspected_wasm_entries);
        REQUIRE(!counts.inspection_bytecode_fallbacks && !counts.overflow);
    }
    printf(
        "attribution case=overhead phase=trial workload=%s backend=%s "
        "scopes=%s trial=%d"
        " module=n/a generation_ordinal=n/a owner=n/a calls=%d seconds=%.9f "
        "checksum=%" PRIu64 " xalloc_calls=%" PRIu64
        " xalloc_requested_bytes=%" PRIu64 " arena_block_delta=%" PRIu64
        " arena_used=%zu arena_capacity=%zu arena_backing=%zu",
        managed ? "managed-array-snapshot" : "scalar",
        behavior_backend_name(backend),
        enabled ? "on" : "off",
        trial,
        WORKLOAD_CALLS,
        elapsed,
        checksum,
        after.calls - before.calls,
        after.requested_bytes - before.requested_bytes,
        block_delta,
        used,
        capacity,
        backing
    );
    workload_print_counts(enabled, enabled);
    workload_release_trial();
    return true;
}

static bool attribution_workload(bool benchmark)
{
    REQUIRE(behavior_backend_available(TEST_COMPILED_BACKEND));
    REQUIRE(workload_build(0));
    REQUIRE(workload_build(1));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    int calls = 0;
    for (int managed = 0; managed < 2; managed++) {
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(workload_warm(managed, backends[backend], false));
            REQUIRE(workload_warm(managed, backends[backend], true));
            calls += 2;
            if (!benchmark) {
                continue;
            }
            for (int trial = 0; trial < WORKLOAD_TRIALS; trial++) {
                /* OFF/ON, ON/OFF, OFF/ON, ON/OFF, OFF/ON. */
                for (int pair = 0; pair < 2; pair++) {
                    bool enabled = (trial % 2) != pair;
                    REQUIRE(workload_trial(managed, backends[backend], enabled, trial + 1));
                    calls += WORKLOAD_CALLS;
                }
            }
        }
    }
    REQUIRE(calls == (benchmark ? 5128 : 8));
    return true;
}

TEST_CASE(runtime_attribution_scalar_and_managed_copy_backends)
{
    return attribution_workload(false);
}

TEST_CASE(runtime_attribution_overhead)
{
    return attribution_workload(true);
}

void test_language_runtime_attribution()
{
    printf(
        "runtime-attribution engine_base=%s fixture_sha256=%s build=%s "
        "coverage=correctness timings=none\n",
        CHIBI_ATTRIBUTION_BASE,
        CHIBI_ATTRIBUTION_FIXTURE_SHA,
        CHIBI_ATTRIBUTION_BUILD
    );
    pu_setup(NULL, cleanup_attribution);
    RUN_TEST_CASE(runtime_attribution_copy_sites);
    RUN_TEST_CASE(runtime_attribution_scopes);
    RUN_TEST_CASE(runtime_attribution_world_strings);
    RUN_TEST_CASE(runtime_attribution_tasks);
    RUN_TEST_CASE(runtime_attribution_backend_entries);
    RUN_TEST_CASE(runtime_attribution_scalar_and_managed_copy_backends);
    pu_clear_setup();
}

void test_language_runtime_attribution_benchmark()
{
    printf(
        "runtime-attribution engine_base=%s fixture_sha256=%s build=%s "
        "coverage=overhead timings=raw-noop-hooks-control-not-run\n",
        CHIBI_ATTRIBUTION_BASE,
        CHIBI_ATTRIBUTION_FIXTURE_SHA,
        CHIBI_ATTRIBUTION_BUILD
    );
    pu_setup(NULL, cleanup_attribution);
    RUN_TEST_CASE(runtime_attribution_overhead);
    pu_clear_setup();
}
