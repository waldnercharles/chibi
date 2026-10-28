#include "execution/intrinsics.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/text.h"
#include "types/union.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LARGE_TEXT_BYTES 8192
#define CAPTURE_TEST_LIMIT ((size_t)128 * 1024)
/* A box holding short text: one arena block and the table that charges the
 * shared text backing it retains. */
#define SMALL_STORAGE_BYTES 1024

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
    /* A box commits a snapshot of a value that already carries its record,
     * so the first box holds what its later writes would. */
    arena staged = { 0 };
    pin_value carried;
    REQUIRE(!pin_value_snapshot_to_arena(&staged, &carried, &value));
    REQUIRE(!pin_value_snapshot_to_arena(&box.storage, &box.kept, &carried));
    arena_free(&staged);
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
            previous_storage.current == box.storage.current &&
            previous_storage.slabs == box.storage.slabs
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
    REQUIRE(begin((pin_value){
        .type = PIN_STR,
        .text = &(const pin_string){ .bytes = large_text, .length = LARGE_TEXT_BYTES },
        .type_name = "CaptureText" }));
    pin_value saved;
    REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved, &box.kept));
    REQUIRE(write_value(root_reference, box.kept, true));
    REQUIRE(strlen(pin_text(box.kept).bytes) == LARGE_TEXT_BYTES);
    REQUIRE(write_value(
        root_reference,
        (pin_value){ .type = PIN_STR,
                     .text = &(const pin_string){ .bytes = "small",
                                                  .length = sizeof("small") - 1 },
                     .type_name = "CaptureText" },
        true
    ));
    REQUIRE(
        !strcmp(pin_text(box.kept).bytes, "small") &&
        !strcmp(box.kept.type_name, "CaptureText")
    );
    REQUIRE(arena_storage_bytes(&box.storage) < SMALL_STORAGE_BYTES);
    REQUIRE(write_value(
        root_reference,
        (pin_value){ .type = PIN_STR,
                     .text = &(const pin_string){ .bytes = "", .length = sizeof("") - 1 } },
        true
    ));
    REQUIRE(
        !strcmp(pin_text(box.kept).bytes, "") &&
        arena_storage_bytes(&box.storage) < SMALL_STORAGE_BYTES
    );
    size_t idle = arena_storage_bytes(&box.storage);
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            !strcmp(pin_text(box.kept).bytes, "") &&
            arena_storage_bytes(&box.storage) == idle
        );
        REQUIRE(
            !strcmp(pin_text(saved).bytes, large_text) &&
            !strcmp(saved.type_name, "CaptureText")
        );
    }
    REQUIRE(write_value(
        root_reference,
        (pin_value){ .type = PIN_STRUCT, .type_name = "MissingCaptureType", .ptr = large_text },
        false
    ));
    REQUIRE(!strcmp(pin_text(box.kept).bytes, ""));
    return true;
}
TEST_CASE(capture_paths_survive_root_replacement_and_fail_atomically)
{
    pin_value items[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = large_text, .length = LARGE_TEXT_BYTES } },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "tail", .length = sizeof("tail") - 1 } }
    };
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
    REQUIRE(!strcmp(pin_text(pin_array_get(box.kept.ptr, 0)).bytes, "tail"));
    REQUIRE(write_value(second, pin_array_get(box.kept.ptr, 1), true));
    values.count = 1;
    REQUIRE(write_value(root_reference, root, true));
    REQUIRE(write_value(
        first,
        (pin_value){ .type = PIN_STR,
                     .text = &(const pin_string){ .bytes = "short",
                                                  .length = sizeof("short") - 1 } },
        true
    ));
    REQUIRE(!strcmp(pin_text(pin_array_get(box.kept.ptr, 0)).bytes, "short"));
    REQUIRE(write_value(
        second,
        (pin_value){ .type = PIN_STR,
                     .text = &(const pin_string){ .bytes = "bad",
                                                  .length = sizeof("bad") - 1 } },
        false
    ));
    values.count = 0;
    REQUIRE(write_value(root_reference, root, true));
    REQUIRE(write_value(
        first,
        (pin_value){ .type = PIN_STR,
                     .text = &(const pin_string){ .bytes = "bad",
                                                  .length = sizeof("bad") - 1 } },
        false
    ));
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
    REQUIRE(!strcmp(pin_text(pin_array_get(box.kept.ptr, 0)).bytes, large_text));
    REQUIRE(!strcmp(pin_text(pin_array_get(saved.ptr, 0)).bytes, large_text));
    REQUIRE(!strcmp(pin_text(pin_array_get(saved.ptr, 1)).bytes, "tail"));
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
/* Reads through reference with result declared as the read's result header:
 * fails with error, or with no error succeeds into *out. */
static bool read_value(pin_value reference, pin_value declared, const char *error, pin_value *out)
{
    *out = declared;
    const char *failure = EXECUTION_reference_read_call.value(&references, &reference, 1, out);
    if (error && (!failure || strcmp(failure, error))) {
        fprintf(stderr, "reference read: %s\n", failure ? failure : "succeeded");
    }
    REQUIRE(error ? failure && !strcmp(failure, error) : !failure);
    return true;
}
/* A reference copies by the type it was made with, and a read's result goes to
 * code checked against its declared header, so a reference whose type is not
 * its root's, a read declared as another type, a field path naming no such
 * field and a write of another type all fail and leave the root as it was. */
TEST_CASE(capture_references_refuse_another_type)
{
    type_catalog_set_project(&record_pack);
    project_types = true;
    capture_record value = { .inner = { .number = 2, .text = "inner" },
                             .label = "label" };
    pin_value record = { .type = PIN_STRUCT, .type_name = "CaptureRecord" };
    REQUIRE(begin(
        (pin_value){ .type = PIN_STRUCT, .type_name = "CaptureRecord", .ptr = &value }
    ));
    pin_value read;
    REQUIRE(read_value(root_reference, record, NULL, &read));
    REQUIRE(((const capture_record *)read.ptr)->inner.number == 2);
    REQUIRE(read_value(
        root_reference,
        (pin_value){ .type = PIN_F64 },
        "Reference read differs from its declared result",
        &read
    ));
    pin_value inner = { .type = PIN_STRUCT, .type_name = "CaptureInner" };
    pin_value claimed = execution_reference_value(&references, &origin, &box.kept, inner);
    REQUIRE(read_value(claimed, inner, "Reference type differs from its storage", &read));
    pin_value label;
    REQUIRE(
        path(root_reference, (pin_value){ .type = PIN_F64 }, offsetof(capture_record, label), false, &label)
    );
    REQUIRE(
        read_value(label, (pin_value){ .type = PIN_F64 }, "Referenced storage is unavailable", &read)
    );
    REQUIRE(
        path(root_reference, (pin_value){ .type = PIN_STR }, offsetof(capture_record, label), false, &label)
    );
    REQUIRE(read_value(label, (pin_value){ .type = PIN_STR }, NULL, &read));
    REQUIRE(!strcmp(pin_text(read).bytes, "label"));
    REQUIRE(write_value(root_reference, (pin_value){ .type = PIN_F64, .d = 1 }, false));
    REQUIRE(
        box.kept.type == PIN_STRUCT &&
        !strcmp(((const capture_record *)box.kept.ptr)->label, "label")
    );
    return true;
}
/* Lowered union headers carry no declaration name: an unnamed union reference
 * reads and writes unnamed unions, and a named one still refuses another. */
TEST_CASE(capture_unnamed_union_references_read_and_write)
{
    pin_value payload = { .type = PIN_I32, .i = 4 }, first, second;
    REQUIRE(!pin_union_value(&escaped, 11, &payload, 1, &first));
    REQUIRE(!pin_union_value(&escaped, 12, NULL, 0, &second));
    REQUIRE(begin(first));
    pin_value read;
    REQUIRE(read_value(root_reference, (pin_value){ .type = PIN_UNION }, NULL, &read));
    REQUIRE(((const pin_union *)read.ptr)->tag == 11);
    REQUIRE(write_value(root_reference, second, true));
    REQUIRE(((const pin_union *)box.kept.ptr)->tag == 12);
    pin_value named = first;
    named.type_name = "Shape";
    REQUIRE(write_value(root_reference, named, false));
    return true;
}
/* An element write must match both the element type its reference claims and
 * the array's own item: a string array's element stays a string. */
TEST_CASE(capture_element_writes_keep_the_item_type)
{
    pin_value items[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "first", .length = sizeof("first") - 1 } }
    };
    pin_array values = { .element_type = PIN_STR, .count = 1, .capacity = 1, .items = items };
    REQUIRE(begin((pin_value){ .type = PIN_STR,
                               .container = PIN_CONTAINER_ARRAY,
                               .ptr = &values }));
    pin_value text, number;
    REQUIRE(path(root_reference, (pin_value){ .type = PIN_STR }, 0, true, &text));
    REQUIRE(write_value(text, (pin_value){ .type = PIN_F64, .d = 1 }, false));
    REQUIRE(path(root_reference, (pin_value){ .type = PIN_F64 }, 0, true, &number));
    REQUIRE(write_value(number, (pin_value){ .type = PIN_F64, .d = 1 }, false));
    REQUIRE(((const pin_array *)box.kept.ptr)->items[0].type == PIN_STR);
    REQUIRE(write_value(
        text,
        (pin_value){ .type = PIN_STR,
                     .text = &(const pin_string){ .bytes = "second",
                                                  .length = sizeof("second") - 1 } },
        true
    ));
    REQUIRE(!strcmp(pin_text(((const pin_array *)box.kept.ptr)->items[0]).bytes, "second"));
    return true;
}
void test_language_capture()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(capture_whole_value_shrink_preserves_escaped_and_self_values);
    RUN_TEST_CASE(capture_paths_survive_root_replacement_and_fail_atomically);
    RUN_TEST_CASE(capture_nested_scalar_writes_retain_only_current_managed_root);
    RUN_TEST_CASE(capture_references_refuse_another_type);
    RUN_TEST_CASE(capture_element_writes_keep_the_item_type);
    RUN_TEST_CASE(capture_unnamed_union_references_read_and_write);
    pu_clear_setup();
}
