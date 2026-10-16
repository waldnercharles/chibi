#include "ecs/ecs.h"
#include "execution/function.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/entities_native.h"
#include "runtime/ui.h"
#include "runtime/ui_native.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"

#include <stdio.h>
#include <string.h>

/* Three fixed service cases, no world step, entity spawn, renderer or worker. */
static arena producer, reads, invocation;
static ecs_world *world;
static entities *globals;
static ui *model;
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
static void cleanup()
{
    if (globals) {
        entities_free(globals);
        globals = NULL;
    }
    if (world) {
        ecs_world_free(world);
        world = NULL;
    }
    if (model) {
        ui_free(model);
        model = NULL;
    }
    arena_free(&producer);
    arena_free(&reads);
    arena_free(&invocation);
}
static void setup_globals()
{
    world = ecs_world_new();
    globals = entities_new(world, NULL);
}
static pin_value row(const pin_record *record, int number)
{
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = arena_alloc(&producer, record->type.size) };
    memset(value.ptr, 0, record->type.size);
    for (int i = 0; i < 12; i++) {
        pin_value field = { .type = record->type.fields[i].type };
        if (field.type == PIN_STR) {
            char text[32];
            snprintf(text, sizeof text, "row-%d-field-%d", number, i);
            char *copy = arena_alloc(&producer, strlen(text) + 1);
            strcpy(copy, text);
            field.str = copy;
        } else if (field.type == PIN_I32) {
            field.i = number + i;
        } else {
            field.b = (number + i) % 2;
        }
        pin_write_field(value.ptr, &record->type.fields[i], field);
    }
    return value;
}
static pin_value rows(const pin_record *record, int count)
{
    pin_array *array = arena_alloc(&producer, sizeof *array);
    *array = (pin_array){
        .element_type = PIN_STRUCT,
        .type_name = record->type.name,
        .record = record,
        .count = count,
        .capacity = count,
        .items = count ? arena_alloc(&producer, count * sizeof(pin_value)) : NULL
    };
    for (int i = 0; i < count; i++) {
        array->items[i] = row(record, i);
    }
    return (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = array };
}
static bool check_row(const pin_value *value, int number, const arena *owner)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE);
    REQUIRE(pin_record_valid(value->record));
    REQUIRE(value->type_name == value->record->type.name);
    REQUIRE(arena_contains(owner, value->record));
    REQUIRE(arena_contains(owner, value->ptr));
    for (int i = 0; i < 12; i++) {
        pin_value field;
        pin_read_field(value->ptr, &value->record->type.fields[i], &field);
        if (i < 5) {
            char text[32];
            snprintf(text, sizeof text, "row-%d-field-%d", number, i);
            REQUIRE(field.str && !strcmp(field.str, text));
            REQUIRE(arena_contains(owner, field.str));
        } else if (i < 7) {
            REQUIRE(field.i == number + i);
        } else {
            REQUIRE(field.b == (bool)((number + i) % 2));
        }
    }
    return true;
}
static bool check_rows(const pin_value *value, int count, const arena *owner)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    REQUIRE(pin_record_valid(value->record) && arena_contains(owner, value->record));
    const pin_array *array = value->ptr;
    REQUIRE(array && array->count == count && arena_contains(owner, array));
    REQUIRE(array->record == value->record);
    REQUIRE(
        array->type_name == value->type_name &&
        value->type_name == value->record->type.name
    );
    for (int i = 0; i < count; i++) {
        REQUIRE(array->items[i].record == array->record);
        REQUIRE(check_row(&array->items[i], i, owner));
    }
    return true;
}
static pin_value result_header(pin_value value)
{
    value.ptr = value.container == PIN_CONTAINER_SINGLE
                  ? arena_alloc(&invocation, value.record->type.size)
                  : NULL;
    return value;
}
static const char *host(const node_host_call *call, const pin_value *arguments, pin_value *out)
{
    behavior_function_context context = { .value_storage = &invocation };
    return call->context ? call->context(&context, globals, arguments, out)
                         : call->invoke(globals, arguments, out);
}
TEST_CASE(record_globals_owned_values)
{
    setup_globals();
    const pin_record *record = pin_record_create(&producer, "Rows.Row", 91, fields, 12);
    REQUIRE(record);
    pin_value scalar = row(record, 7), array = rows(record, 3), saved, saved_array;
    /* Expected red: scalar authored values currently require native registration. */
    REQUIRE(!entities_set_global_value(globals, "row", &scalar));
    REQUIRE(!entities_set_global_value(globals, "rows", &array));
    REQUIRE(!entities_get_global_value(globals, "row", &reads, &saved));
    REQUIRE(!entities_get_global_value(globals, "rows", &reads, &saved_array));
    pin_value arguments[] = { { .type = PIN_STR, .str = "row" }, scalar };
    pin_value get = result_header(scalar), get_array = result_header(array);
    void *get_backing = get.ptr;
    REQUIRE(!host(&ENTITY_GET_GLOBAL_CALL, arguments, &get));
    REQUIRE(get.ptr == get_backing);
    arguments[0].str = "rows";
    REQUIRE(!host(&ENTITY_GET_GLOBAL_CALL, arguments, &get_array));
    REQUIRE(check_row(&get, 7, &invocation));
    REQUIRE(check_rows(&get_array, 3, &invocation));
    arguments[0].str = "row";
    arguments[1] = row(record, 9);
    pin_value set = result_header(scalar), set_array = result_header(array);
    void *backing = set.ptr;
    REQUIRE(!host(&ENTITY_SET_GLOBAL_CALL, arguments, &set));
    REQUIRE(set.ptr == backing);
    pin_value text;
    pin_read_field(arguments[1].ptr, &record->type.fields[0], &text);
    ((char *)text.str)[0] = 'X';
    REQUIRE(check_row(&set, 9, &invocation));
    arguments[0].str = "rows";
    arguments[1] = array;
    REQUIRE(!host(&ENTITY_SET_GLOBAL_CALL, arguments, &set_array));
    pin_read_field(((pin_array *)array.ptr)->items[0].ptr, &record->type.fields[0], &text);
    ((char *)text.str)[0] = 'X';
    REQUIRE(check_rows(&set_array, 3, &invocation));
    arena_free(&producer);
    pin_value stored, stored_array;
    REQUIRE(!entities_get_global_value(globals, "row", &reads, &stored));
    REQUIRE(!entities_get_global_value(globals, "rows", &reads, &stored_array));
    REQUIRE(check_row(&stored, 9, &reads));
    REQUIRE(check_rows(&stored_array, 3, &reads));
    entities_free(globals);
    globals = NULL;
    ecs_world_free(world);
    world = NULL;
    REQUIRE(check_row(&saved, 7, &reads));
    REQUIRE(check_rows(&saved_array, 3, &reads));
    REQUIRE(check_row(&get, 7, &invocation));
    REQUIRE(check_rows(&get_array, 3, &invocation));
    REQUIRE(check_row(&set, 9, &invocation));
    REQUIRE(check_rows(&set_array, 3, &invocation));
    return true;
}
static bool unchanged_rows()
{
    pin_value value;
    REQUIRE(entities_global_name_count(globals) == 1);
    REQUIRE(!strcmp(entities_global_name_at(globals, 0), "rows"));
    REQUIRE(!entities_get_global_value(globals, "rows", &reads, &value));
    REQUIRE(check_rows(&value, 3, &reads));
    return true;
}
TEST_CASE(record_globals_atomic_rejections)
{
    setup_globals();
    const pin_record *record = pin_record_create(&producer, "Rows.Row", 91, fields, 12);
    const pin_record *different = pin_record_create(&producer, "Rows.Row", 92, fields, 12);
    REQUIRE(record && different);
    pin_value array = rows(record, 3), other = rows(different, 3);
    REQUIRE(!entities_set_global_value(globals, "rows", &array));
    /* Expected red: same spelling currently hides different nominal identities. */
    REQUIRE(entities_set_global_value(globals, "rows", &other));
    REQUIRE(unchanged_rows());
    type_field changed[12];
    memcpy(changed, fields, sizeof changed);
    changed[6].type = PIN_BOOL;
    const pin_record *layout = pin_record_create(&producer, "Rows.Row", 91, changed, 12);
    REQUIRE(layout && !pin_record_same(layout, record));
    pin_value changed_array = rows(layout, 3);
    REQUIRE(entities_set_global_value(globals, "rows", &changed_array));
    REQUIRE(unchanged_rows());
    pin_value native = array;
    native.record = NULL;
    REQUIRE(entities_set_global_value(globals, "rows", &native));
    REQUIRE(unchanged_rows());
    pin_record malformed = *record;
    malformed.type.size++;
    pin_array *items = array.ptr;
    items->items[1].record = &malformed;
    REQUIRE(entities_set_global_value(globals, "rows", &array));
    REQUIRE(unchanged_rows());
    REQUIRE(entities_set_global_value(globals, "invalid_first", &array));
    REQUIRE(unchanged_rows());
    items->items[1].record = record;
    pin_value arguments[] = { { .type = PIN_STR, .str = "rows" }, rows(record, 0) };
    pin_value output = result_header(other), before = output;
    REQUIRE(host(&ENTITY_SET_GLOBAL_CALL, arguments, &output));
    REQUIRE(!memcmp(&output, &before, sizeof output));
    REQUIRE(unchanged_rows());
    output = result_header(array);
    output.record = &malformed;
    before = output;
    REQUIRE(host(&ENTITY_SET_GLOBAL_CALL, arguments, &output));
    REQUIRE(!memcmp(&output, &before, sizeof output));
    REQUIRE(unchanged_rows());
    REQUIRE(host(&ENTITY_GET_GLOBAL_CALL, arguments, &output));
    REQUIRE(!memcmp(&output, &before, sizeof output));
    return true;
}
static bool check_document_row(const av_value *value, int number)
{
    REQUIRE(value && av_is_obj(value));
    for (int i = 0; i < 12; i++) {
        const av_value *field = av_obj_get(value, fields[i].name);
        REQUIRE(field);
        if (i < 5) {
            char text[32];
            snprintf(text, sizeof text, "row-%d-field-%d", number, i);
            REQUIRE(av_is_str(field) && !strcmp(av_get_str(field), text));
        } else if (i < 7) {
            REQUIRE(av_is_sint(field) && av_get_sint(field) == number + i);
        } else {
            REQUIRE(av_is_bool(field) && av_get_bool(field) == (bool)((number + i) % 2));
        }
    }
    return true;
}
TEST_CASE(record_headless_ui_owned_data)
{
    model = ui_new();
    const pin_record *record = pin_record_create(&producer, "Rows.Row", 91, fields, 12);
    REQUIRE(record);
    pin_value array = rows(record, 3);
    pin_value arguments[] = { { .type = PIN_U64, .u64 = 17 },
                              { .type = PIN_STR, .str = "rows" },
                              array };
    REQUIRE(!UI_SET_DATA_CALL.invoke(model, arguments, NULL));
    REQUIRE(ui_data_count(model) == 1);
    const ui_datum *datum = ui_data_at(model, 0);
    REQUIRE(datum->entity == 17 && !strcmp(datum->name, "rows"));
    REQUIRE(av_arr_size(datum->value) == 3);
    uint64_t revision = ui_revision(model);
    const av_value *before = datum->value;
    pin_record malformed = *record;
    malformed.type.size++;
    ((pin_array *)array.ptr)->items[1].record = &malformed;
    REQUIRE(UI_SET_DATA_CALL.invoke(model, arguments, NULL));
    REQUIRE(ui_revision(model) == revision && ui_data_at(model, 0)->value == before);
    arena_free(&producer);
    for (int i = 0; i < 3; i++) {
        REQUIRE(check_document_row(av_arr_get(before, i), i));
    }
    record = pin_record_create(&producer, "Rows.Row", 91, fields, 12);
    REQUIRE(record);
    arguments[2] = rows(record, 0);
    REQUIRE(!UI_SET_DATA_CALL.invoke(model, arguments, NULL));
    REQUIRE(ui_revision(model) == revision + 1);
    REQUIRE(av_arr_size(ui_data_at(model, 0)->value) == 0);
    REQUIRE(!UI_SET_DATA_CALL.invoke(model, arguments, NULL));
    REQUIRE(ui_revision(model) == revision + 1);
    arena_free(&producer);
    REQUIRE(av_arr_size(ui_data_at(model, 0)->value) == 0);
    return true;
}
void test_language_record_services()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_globals_owned_values);
    RUN_TEST_CASE(record_globals_atomic_rejections);
    RUN_TEST_CASE(record_headless_ui_owned_data);
    pu_setup(NULL, NULL);
}
