#include "execution/intrinsics.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LARGE_TEXT_BYTES 8192
#define CAPTURE_TEST_LIMIT ((size_t)128 * 1024)

static execution_box box;
static arena references, escaped;
static behavior_function_context origin;
static pin_value root_reference;
static char large_text[LARGE_TEXT_BYTES + 1];
static bool project_types;

static void cleanup()
{
    arena_free(&box.storage);
    arena_free(&references);
    arena_free(&escaped);
    box = (execution_box){ 0 };
    origin = (behavior_function_context){ 0 };
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
}
static bool begin(pin_value value)
{
    memset(large_text, 'L', LARGE_TEXT_BYTES);
    large_text[LARGE_TEXT_BYTES] = 0;
    REQUIRE(!pin_value_snapshot_to_arena(&box.storage, &box.kept, &value));
    box.holders = 1;
    box.owner = &box.detached;
    box.value = &box.kept;
    box.detached.value_storage = &box.storage;
    root_reference = execution_reference_value(&references, &origin, &box.kept, value);
    execution_reference_capture(&references, &origin, &root_reference, &box);
    return true;
}
static bool path(pin_value base, pin_value type, size_t offset, bool element, pin_value *out)
{
    const pin_value args[] = { base,
                               type,
                               { .type = PIN_U64, .u64 = offset },
                               { .type = PIN_BOOL, .b = element } };
    REQUIRE(!EXECUTION_reference_field_call.value(&references, args, 4, out));
    return true;
}
static bool compact_storage()
{
    arena fresh = { 0 };
    pin_value current;
    REQUIRE(!pin_value_snapshot_to_arena(&fresh, &current, &box.kept));
    size_t used, capacity, current_used, current_capacity;
    arena_usage(&box.storage, &used, &capacity);
    arena_usage(&fresh, &current_used, &current_capacity);
    arena_free(&fresh);
    REQUIRE(used == current_used && capacity == current_capacity);
    REQUIRE(arena_storage_bytes(&box.storage) < CAPTURE_TEST_LIMIT);
    return true;
}
static bool write_value(pin_value reference, pin_value value, bool success)
{
    const pin_value args[] = { reference, value };
    pin_value result, previous = box.kept;
    arena previous_storage = box.storage;
    size_t bytes = arena_storage_bytes(&box.storage);
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    const char *error = EXECUTION_reference_write_call.value(NULL, args, 2, &result);
    xalloc_stats_get(&after);
    REQUIRE((error == NULL) == success && result.type == PIN_BOOL && result.b == success);
    REQUIRE(box.holders == 1 && box.value == &box.kept && box.owner == &box.detached);
    REQUIRE(box.detached.value_storage == &box.storage);
    if (!success) {
        REQUIRE(
            previous.type == box.kept.type && previous.container == box.kept.container &&
            previous.type_name == box.kept.type_name && previous.u64 == box.kept.u64
        );
        REQUIRE(
            previous_storage.first == box.storage.first &&
            previous_storage.current == box.storage.current
        );
        REQUIRE(
            previous_storage.allocations == box.storage.allocations &&
            previous_storage.unused == box.storage.unused
        );
        REQUIRE(
            previous_storage.allocated == box.storage.allocated &&
            previous_storage.kept == box.storage.kept &&
            previous_storage.collectable == box.storage.collectable
        );
        REQUIRE(arena_storage_bytes(&box.storage) == bytes);
    }
    REQUIRE(compact_storage());
    printf(
        "capture-write success=%d previous_backing=%zu final_backing=%zu "
        "xalloc_calls=%llu xalloc_requested=%llu\n",
        success,
        bytes,
        arena_storage_bytes(&box.storage),
        (unsigned long long)(after.calls - before.calls),
        (unsigned long long)(after.requested_bytes - before.requested_bytes)
    );
    return true;
}
TEST_CASE(capture_whole_value_shrink_preserves_escaped_and_self_values)
{
    REQUIRE(begin((pin_value){ .type = PIN_STR, .str = large_text, .type_name = "CaptureText" }));
    pin_value saved;
    REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved, &box.kept));
    REQUIRE(write_value(root_reference, box.kept, true));
    REQUIRE(strlen(box.kept.str) == LARGE_TEXT_BYTES);
    REQUIRE(write_value(
        root_reference,
        (pin_value){ .type = PIN_STR, .str = "small", .type_name = "CaptureText" },
        true
    ));
    REQUIRE(!strcmp(box.kept.str, "small") && !strcmp(box.kept.type_name, "CaptureText"));
    REQUIRE(arena_storage_bytes(&box.storage) < 512);
    REQUIRE(write_value(root_reference, (pin_value){ .type = PIN_STR, .str = "" }, true));
    REQUIRE(!strcmp(box.kept.str, "") && arena_storage_bytes(&box.storage) < 512);
    size_t idle = arena_storage_bytes(&box.storage);
    for (int i = 0; i < 4; i++) {
        REQUIRE(!strcmp(box.kept.str, "") && arena_storage_bytes(&box.storage) == idle);
        REQUIRE(!strcmp(saved.str, large_text) && !strcmp(saved.type_name, "CaptureText"));
    }
    REQUIRE(write_value(
        root_reference,
        (pin_value){ .type = PIN_STRUCT, .type_name = "MissingCaptureType", .ptr = large_text },
        false
    ));
    REQUIRE(!strcmp(box.kept.str, ""));
    return true;
}
TEST_CASE(capture_paths_survive_root_replacement_and_fail_atomically)
{
    pin_value items[] = { { .type = PIN_STR, .str = large_text },
                          { .type = PIN_STR, .str = "tail" } };
    pin_array values = { .element_type = PIN_STR, .count = 2, .capacity = 2, .items = items };
    pin_value root = { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = &values };
    REQUIRE(begin(root));
    pin_value first, second;
    REQUIRE(path(root_reference, (pin_value){ .type = PIN_STR }, 0, true, &first));
    REQUIRE(path(root_reference, (pin_value){ .type = PIN_STR }, 1, true, &second));
    /* Capture a pre-existing element path again, as a descendant does. */
    execution_reference_capture(&references, &origin, &first, &box);
    pin_value saved;
    REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved, &box.kept));
    REQUIRE(write_value(first, pin_array_get(box.kept.ptr, 1), true));
    REQUIRE(!strcmp(pin_array_get(box.kept.ptr, 0).str, "tail"));
    REQUIRE(write_value(second, pin_array_get(box.kept.ptr, 1), true));
    values.count = 1;
    REQUIRE(write_value(root_reference, root, true));
    REQUIRE(write_value(first, (pin_value){ .type = PIN_STR, .str = "short" }, true));
    REQUIRE(!strcmp(pin_array_get(box.kept.ptr, 0).str, "short"));
    REQUIRE(write_value(second, (pin_value){ .type = PIN_STR, .str = "bad" }, false));
    values.count = 0;
    REQUIRE(write_value(root_reference, root, true));
    REQUIRE(write_value(first, (pin_value){ .type = PIN_STR, .str = "bad" }, false));
    size_t idle = arena_storage_bytes(&box.storage);
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            ((pin_array *)box.kept.ptr)->count == 0 &&
            arena_storage_bytes(&box.storage) == idle
        );
    }
    values.count = 2;
    REQUIRE(write_value(root_reference, root, true));
    REQUIRE(write_value(
        first,
        (pin_value){ .type = PIN_STRUCT, .type_name = "MissingCaptureType", .ptr = large_text },
        false
    ));
    REQUIRE(!strcmp(pin_array_get(box.kept.ptr, 0).str, large_text));
    REQUIRE(!strcmp(pin_array_get(saved.ptr, 0).str, large_text));
    REQUIRE(!strcmp(pin_array_get(saved.ptr, 1).str, "tail"));
    return true;
}

typedef struct
{
    double number;
    const char *text;
} capture_inner;
typedef struct
{
    capture_inner inner;
    const char *label;
} capture_record;
static const type_field inner_fields[] = {
    { .name = "number", .type = PIN_F64, .size = sizeof(double) },
    { .name = "text",
      .type = PIN_STR,
      .offset = offsetof(capture_inner, text),
      .size = sizeof(const char *) },
};
static const type_desc inner_type = { .name = "CaptureInner",
                                      .size = sizeof(capture_inner),
                                      .fields = inner_fields,
                                      .field_count = 2 };
static const type_field record_fields[] = {
    { .name = "inner",
      .type = PIN_STRUCT,
      .type_name = "CaptureInner",
      .size = sizeof(capture_inner) },
    { .name = "label",
      .type = PIN_STR,
      .offset = offsetof(capture_record, label),
      .size = sizeof(const char *) },
};
static const type_desc record_type = { .name = "CaptureRecord",
                                       .size = sizeof(capture_record),
                                       .fields = record_fields,
                                       .field_count = 2 };
static const type_desc *const record_types[] = { &inner_type, &record_type };
static const type_catalog_pack record_pack = { .types = record_types, .type_count = 2 };
TEST_CASE(capture_nested_scalar_writes_retain_only_current_managed_root)
{
    type_catalog_set_project(&record_pack);
    project_types = true;
    capture_record value = { .inner = { .number = 2, .text = large_text },
                             .label = "sibling" };
    REQUIRE(begin(
        (pin_value){ .type = PIN_STRUCT, .type_name = "CaptureRecord", .ptr = &value }
    ));
    pin_value inner, number, saved;
    REQUIRE(path(
        root_reference,
        (pin_value){ .type = PIN_STRUCT, .type_name = "CaptureInner" },
        offsetof(capture_record, inner),
        false,
        &inner
    ));
    REQUIRE(
        path(inner, (pin_value){ .type = PIN_F64 }, offsetof(capture_inner, number), false, &number)
    );
    REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved, &box.kept));
    for (int i = 0; i < 8; i++) {
        REQUIRE(write_value(number, (pin_value){ .type = PIN_F64, .d = i }, true));
        const capture_record *current = box.kept.ptr;
        REQUIRE(current->inner.number == i && !strcmp(current->inner.text, large_text));
        REQUIRE(!strcmp(current->label, "sibling"));
    }
    const capture_record *original = saved.ptr;
    REQUIRE(original->inner.number == 2 && !strcmp(original->inner.text, large_text));
    REQUIRE(!strcmp(original->label, "sibling"));
    return true;
}
void test_language_capture()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(capture_whole_value_shrink_preserves_escaped_and_self_values);
    RUN_TEST_CASE(capture_paths_survive_root_replacement_and_fail_atomically);
    RUN_TEST_CASE(capture_nested_scalar_writes_retain_only_current_managed_root);
    pu_clear_setup();
}
