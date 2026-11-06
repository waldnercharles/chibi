#include "runtime/arrays.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixed legacy controls for the array kernels changed by owned records. */
static arena source, results;
static bool catalog_set;
typedef struct
{
    const char *text;
    int number;
} native_row;
static const type_field fields[] = {
    { .name = "text",
      .type = PIN_STR,
      .offset = offsetof(native_row, text),
      .size = sizeof(char *) },
    { .name = "number",
      .type = PIN_I32,
      .offset = offsetof(native_row, number),
      .size = sizeof(int) },
};
static const type_desc native_type = {
    .name = "RecordCompatibilityNative",
    .size = sizeof(native_row),
    .fields = fields,
    .field_count = 2,
};
static const type_desc *const types[] = { &native_type };
static const type_catalog_pack pack = { .types = types, .type_count = 1 };
static void cleanup()
{
    if (catalog_set) {
        type_catalog_set_project(NULL);
        catalog_set = false;
    }
    arena_free(&source);
    arena_free(&results);
}
static pin_value item(pin_type type, int number)
{
    pin_value value = { .type = type };
    if (type == PIN_I32) {
        value.i = number;
        return value;
    }
    char *text = arena_alloc(&source, 24);
    snprintf(text, 24, "native-%d", number);
    if (type == PIN_STR) {
        const char *error = pin_value_text(&source, &value, text, strlen(text));
        if (error) {
            abort();
        }
        return value;
    }
    native_row *row = arena_alloc(&source, sizeof *row);
    *row = (native_row){ .text = text, .number = number };
    value.type_name = native_type.name;
    value.ptr = row;
    return value;
}
/* Legacy kernels never give a value an authored record; a native row may
 * carry its catalog type's native record. */
static bool native_or_none(pin_type type, const pin_record *record)
{
    return !record || (type == PIN_STRUCT && record->native &&
                       !strcmp(record->type.name, native_type.name));
}
static bool check_item(const pin_value *value, pin_type type, int number)
{
    REQUIRE(value->type == type && value->container == PIN_CONTAINER_SINGLE);
    REQUIRE(native_or_none(type, value->record));
    if (type == PIN_I32) {
        REQUIRE(value->i == number);
        return true;
    }
    /* Text is immutable and shared: a result holds a reference to its
     * owner, which outlives the source arena. */
    const char *text = type == PIN_STR ? pin_text(*value).bytes : NULL;
    REQUIRE(type != PIN_STR || !number || pin_text(*value).owner);
    if (type == PIN_STRUCT) {
        REQUIRE(value->type_name && !strcmp(value->type_name, native_type.name));
        REQUIRE(arena_contains(&results, value->type_name));
        if (!number) {
            REQUIRE(!value->ptr);
            return true;
        }
        const native_row *row = value->ptr;
        REQUIRE(row && arena_contains(&results, row));
        REQUIRE(row->number == number);
        text = row->text;
    }
    if (!number) {
        REQUIRE(type == PIN_STR && !pin_text(*value).length);
        return true;
    }
    char expected[24];
    snprintf(expected, sizeof expected, "native-%d", number);
    REQUIRE(text && !strcmp(text, expected));
    REQUIRE(type == PIN_STR || arena_contains(&results, text));
    return true;
}
TEST_CASE(record_legacy_array_callbacks)
{
    type_catalog_set_project(&pack);
    catalog_set = true;
    static const pin_type kinds[] = { PIN_I32, PIN_STR, PIN_STRUCT };
    static const node_host_call *const calls[2][8] = {
        { &NATIVE_ARRAY_add_HOST,
          &NATIVE_ARRAY_extend_HOST,
          &NATIVE_ARRAY_set_elem_HOST,
          &NATIVE_ARRAY_insert_HOST,
          &NATIVE_ARRAY_remove_index_HOST,
          &NATIVE_ARRAY_resize_HOST,
          &NATIVE_ARRAY_resize_HOST,
          &NATIVE_ARRAY_slice_HOST },
        { &NATIVE_ARRAY_add_CALL,
          &NATIVE_ARRAY_extend_CALL,
          &NATIVE_ARRAY_set_elem_CALL,
          &NATIVE_ARRAY_insert_CALL,
          &NATIVE_ARRAY_remove_index_CALL,
          &NATIVE_ARRAY_resize_CALL,
          &NATIVE_ARRAY_resize_CALL,
          &NATIVE_ARRAY_slice_CALL },
    };
    static const int counts[] = { 4, 4, 3, 4, 2, 5, 1, 2 };
    static const int expected[8][5] = {
        { 10, 20, 30, 40 },
        { 10, 20, 30, 40 },
        { 10, 40, 30 },
        { 10, 40, 20, 30 },
        { 10, 30 },
        { 10, 20, 30, 0, 0 },
        { 10 },
        { 20, 30 },
    };
    for (int kind = 0; kind < 3; kind++) {
        pin_value items[4];
        for (int i = 0; i < 4; i++) {
            items[i] = item(kinds[kind], (i + 1) * 10);
        }
        pin_array base = { .element_type = kinds[kind],
                           .type_name = items[0].type_name,
                           .count = 3,
                           .capacity = 3,
                           .items = items };
        pin_array tail = { .element_type = kinds[kind],
                           .type_name = items[0].type_name,
                           .count = 1,
                           .capacity = 1,
                           .items = &items[3] };
        pin_value input = { .type = kinds[kind],
                            .container = PIN_CONTAINER_ARRAY,
                            .type_name = items[0].type_name,
                            .ptr = &base };
        pin_value other = input;
        other.ptr = &tail;
        pin_value out[2][8];
        for (int wrapper = 0; wrapper < 2; wrapper++) {
            for (int operation = 0; operation < 8; operation++) {
                pin_value args[3] = { input, { .type = PIN_I32, .i = 1 }, items[3] };
                int count = 3;
                if (operation == 0) {
                    args[1] = items[3];
                    count = 2;
                }
                if (operation == 1) {
                    args[1] = other;
                    count = 2;
                }
                if (operation == 4) {
                    count = 2;
                }
                if (operation == 5 || operation == 6) {
                    args[1].i = operation == 5 ? 5 : 1;
                    count = 2;
                }
                if (operation == 7) {
                    args[2] = (pin_value){ .type = PIN_I32, .i = 2 };
                }
                REQUIRE(calls[wrapper][operation]->value);
                REQUIRE(!calls[wrapper][operation]
                             ->value(&results, args, count, &out[wrapper][operation]));
                REQUIRE(base.count == 3 && base.items == items && tail.count == 1);
            }
        }
        /* A native row's text is a mutable buffer the source owns, so the
         * result must already hold its own copy. */
        if (kinds[kind] == PIN_STRUCT) {
            for (int i = 0; i < 4; i++) {
                ((char *)((native_row *)items[i].ptr)->text)[0] = 'X';
            }
        }
        arena_free(&source);
        for (int wrapper = 0; wrapper < 2; wrapper++) {
            for (int operation = 0; operation < 8; operation++) {
                const pin_value *value = &out[wrapper][operation];
                REQUIRE(value->type == kinds[kind] && value->container == PIN_CONTAINER_ARRAY);
                REQUIRE(native_or_none(kinds[kind], value->record));
                const pin_array *array = value->ptr;
                REQUIRE(array && arena_contains(&results, array));
                REQUIRE(!array->count || arena_contains(&results, pin_array_data(array, 0)));
                REQUIRE(array->count == counts[operation] && array->element_type == kinds[kind]);
                REQUIRE(native_or_none(kinds[kind], array->record));
                if (kinds[kind] == PIN_STRUCT) {
                    REQUIRE(!strcmp(value->type_name, native_type.name));
                    REQUIRE(!strcmp(array->type_name, native_type.name));
                    REQUIRE(arena_contains(&results, value->type_name));
                    REQUIRE(arena_contains(&results, array->type_name));
                }
                for (int i = 0; i < array->count; i++) {
                    pin_value item = pin_array_get(array, i);
                    REQUIRE(check_item(&item, kinds[kind], expected[operation][i]));
                }
            }
        }
        arena_free(&results);
    }
    return true;
}
void test_language_record_compatibility()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_legacy_array_callbacks);
    pu_setup(NULL, NULL);
}
