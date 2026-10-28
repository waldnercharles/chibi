#include "ecs/ecs.h"
#include "execution/function.h"
#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/entities_native.h"
#include "runtime/ui.h"
#include "runtime/ui_native.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
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
            const char *error = pin_value_text(&producer, &field, copy, strlen(copy));
            if (error) {
                abort();
            }
        } else if (field.type == PIN_I32) {
            field.i = number + i;
        } else {
            field.b = (number + i) % 2;
        }
        {
            const char *
                error = pin_write_field(&producer, value.ptr, &record->type.fields[i], field);
            if (error) {
                abort();
            }
        };
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
        REQUIRE(!pin_read_field(&reads, value->ptr, &value->record->type.fields[i], &field));
        if (i < 5) {
            char text[32];
            snprintf(text, sizeof text, "row-%d-field-%d", number, i);
            REQUIRE(pin_text(field).bytes && !strcmp(pin_text(field).bytes, text));
            REQUIRE(arena_contains(owner, field.text));
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
        REQUIRE(pin_array_get(array, i).record == array->record);
        pin_value item = pin_array_get(array, i);
        REQUIRE(check_row(&item, i, owner));
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
    pin_value arguments[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "row", .length = sizeof("row") - 1 } },
        scalar
    };
    pin_value get = result_header(scalar), get_array = result_header(array);
    void *get_backing = get.ptr;
    REQUIRE(!host(&ENTITY_GET_GLOBAL_CALL, arguments, &get));
    REQUIRE(get.ptr == get_backing);
    arguments[0].text = &(const pin_string){ .bytes = "rows",
                                             .length = (uint32_t)strlen("rows") };
    REQUIRE(!host(&ENTITY_GET_GLOBAL_CALL, arguments, &get_array));
    REQUIRE(check_row(&get, 7, &invocation));
    REQUIRE(check_rows(&get_array, 3, &invocation));
    arguments[0].text = &(const pin_string){ .bytes = "row",
                                             .length = (uint32_t)strlen("row") };
    arguments[1] = row(record, 9);
    pin_value set = result_header(scalar), set_array = result_header(array);
    void *backing = set.ptr;
    REQUIRE(!host(&ENTITY_SET_GLOBAL_CALL, arguments, &set));
    REQUIRE(set.ptr == backing);
    /* Text is immutable and shared, so overwrite a scalar field: the stored
     * row must be its own copy. */
    const pin_value overwrite = { .type = PIN_I32, .i = -1 };
    REQUIRE(!pin_write_field(&producer, arguments[1].ptr, &record->type.fields[5], overwrite));
    REQUIRE(check_row(&set, 9, &invocation));
    arguments[0].text = &(const pin_string){ .bytes = "rows",
                                             .length = (uint32_t)strlen("rows") };
    arguments[1] = array;
    REQUIRE(!host(&ENTITY_SET_GLOBAL_CALL, arguments, &set_array));
    REQUIRE(
        !pin_write_field(&producer, pin_array_data(array.ptr, 0), &record->type.fields[5], overwrite)
    );
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
    /* Each row must carry the array's layout, not only the first. */
    pin_array *items = array.ptr;
    items->items[1].record = different;
    REQUIRE(entities_set_global_value(globals, "rows", &array));
    REQUIRE(unchanged_rows());
    REQUIRE(entities_set_global_value(globals, "invalid_first", &array));
    REQUIRE(unchanged_rows());
    items->items[1].record = record;
    pin_value arguments[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "rows", .length = sizeof("rows") - 1 } },
        rows(record, 0)
    };
    pin_value output = result_header(other), before = output;
    REQUIRE(host(&ENTITY_SET_GLOBAL_CALL, arguments, &output));
    REQUIRE(!memcmp(&output, &before, sizeof output));
    REQUIRE(unchanged_rows());
    output = result_header(array);
    output.record = different;
    before = output;
    REQUIRE(host(&ENTITY_SET_GLOBAL_CALL, arguments, &output));
    REQUIRE(!memcmp(&output, &before, sizeof output));
    REQUIRE(unchanged_rows());
    REQUIRE(host(&ENTITY_GET_GLOBAL_CALL, arguments, &output));
    REQUIRE(!memcmp(&output, &before, sizeof output));
    /* A new key avoids the existing-value type mismatch rejecting first. */
    pin_value pointer = { .type = PIN_POINTER, .type_name = "^int" }, variant;
    REQUIRE(!pin_union_value(&producer, 1, &pointer, 1, &variant));
    pin_array *pointer_array = pin_array_snapshot_allocate(&producer, PIN_POINTER, "^int", 1);
    REQUIRE(pointer_array);
    pointer_array->items[0] = pointer;
    const type_field pointer_field = { .name = "value",
                                       .type = PIN_POINTER,
                                       .type_name = "^int" };
    const pin_record *pointer_record = pin_record_create(&producer, "PointerCarrier", 93, &pointer_field, 1);
    REQUIRE(pointer_record);
    void *absent = NULL;
    const pin_value rejected[] = {
        variant,
        { .type = PIN_POINTER,
          .container = PIN_CONTAINER_ARRAY,
          .type_name = "^int",
          .ptr = pointer_array },
        { .type = PIN_STRUCT,
          .type_name = pointer_record->type.name,
          .record = pointer_record,
          .ptr = &absent },
    };
    for (int i = 0; i < 3; i++) {
        REQUIRE(entities_set_global_value(globals, "rejected", &rejected[i]) == -1);
        REQUIRE(unchanged_rows());
    }
    /* Queued event payloads outlive every managed heap, so a pointer, or a
     * union or record carrying one, is refused before anything is queued. */
    const pin_value payloads[] = { pointer, variant, rejected[2] };
    for (int i = 0; i < 3; i++) {
        execution_arguments inputs = { .values = &payloads[i], .count = 1 };
        REQUIRE(
            entities_request_event(globals, ecs_entity_from_u64(1), "Hit", inputs) == FLOW_ERR_ARGUMENT
        );
        REQUIRE(entities_pending_events(globals) == 0);
    }
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
    behavior_function_context context = { .value_storage = &invocation };
    model = ui_new();
    const pin_record *record = pin_record_create(&producer, "Rows.Row", 91, fields, 12);
    REQUIRE(record);
    pin_value array = rows(record, 3);
    pin_value arguments[] = {
        { .type = PIN_U64, .u64 = 17 },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "rows", .length = sizeof("rows") - 1 } },
        array
    };
    REQUIRE(!UI_SET_DATA_CALL.context(&context, model, arguments, NULL));
    REQUIRE(ui_data_count(model) == 1);
    const ui_datum *datum = ui_data_at(model, 0);
    REQUIRE(datum->entity == 17 && !strcmp(datum->name, "rows"));
    REQUIRE(av_arr_size(datum->value) == 3);
    uint64_t revision = ui_revision(model);
    const av_value *before = datum->value;
    const pin_string *valid_name = arguments[1].text;
    const char nul_name[] = "rows\0other";
    const pin_string invalid_name = { nul_name, sizeof nul_name - 1 };
    arguments[1].text = &invalid_name;
    const char *invalid_text = UI_SET_DATA_CALL.context(&context, model, arguments, NULL);
    REQUIRE(invalid_text && !strcmp(invalid_text, "String contains embedded NUL"));
    REQUIRE(ui_revision(model) == revision && ui_data_at(model, 0)->value == before);
    arguments[1].text = valid_name;
    pin_record malformed = *record;
    malformed.type.size++;
    ((pin_array *)array.ptr)->items[1].record = &malformed;
    REQUIRE(UI_SET_DATA_CALL.context(&context, model, arguments, NULL));
    REQUIRE(ui_revision(model) == revision && ui_data_at(model, 0)->value == before);
    arena_free(&producer);
    for (int i = 0; i < 3; i++) {
        REQUIRE(check_document_row(av_arr_get(before, i), i));
    }
    record = pin_record_create(&producer, "Rows.Row", 91, fields, 12);
    REQUIRE(record);
    arguments[2] = rows(record, 0);
    REQUIRE(!UI_SET_DATA_CALL.context(&context, model, arguments, NULL));
    REQUIRE(ui_revision(model) == revision + 1);
    REQUIRE(av_arr_size(ui_data_at(model, 0)->value) == 0);
    REQUIRE(!UI_SET_DATA_CALL.context(&context, model, arguments, NULL));
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

/* Separate compatibility selector: nineteen value/host entries, one entity
 * created then destroyed synchronously, no update/renderer/worker. */
TEST_CASE(record_globals_native_compatibility)
{
    setup_globals();
    const double precise = 16777217.0;
    REQUIRE((double)(float)precise != precise);
    pin_value initial = { .type = PIN_F32, .f = 0.5f };
    REQUIRE(!entities_set_global_value(globals, "narrow", &initial));
    pin_value args[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "narrow",
                                       .length = sizeof("narrow") - 1 } },
        { .type = PIN_F64, .d = precise }
    };
    pin_value out = { .type = PIN_F64 }, saved;
    REQUIRE(!host(&ENTITY_SET_GLOBAL_CALL, args, &out));
    REQUIRE(out.type == PIN_F64 && out.d == precise && !out.record);
    REQUIRE(!entities_get_global_value(globals, "narrow", &reads, &saved));
    REQUIRE(saved.type == PIN_F32 && saved.f == (float)precise);
    REQUIRE((double)saved.f != out.d);

    initial = (pin_value){ .type = PIN_F64, .d = 0.25 };
    REQUIRE(!entities_set_global_value(globals, "wide", &initial));
    args[0].text = &(const pin_string){ .bytes = "wide",
                                        .length = (uint32_t)strlen("wide") };
    args[1] = (pin_value){ .type = PIN_F32, .f = 1.25f };
    out = (pin_value){ .type = PIN_F64 };
    REQUIRE(!host(&ENTITY_SET_GLOBAL_CALL, args, &out));
    REQUIRE(out.type == PIN_F64 && out.d == 1.25 && !out.record);
    REQUIRE(!entities_get_global_value(globals, "wide", &reads, &saved));
    REQUIRE(saved.type == PIN_F64 && saved.d == 1.25 && !saved.record);
    out = (pin_value){ .type = PIN_F32 };
    REQUIRE(!host(&ENTITY_GET_GLOBAL_CALL, args, &out));
    REQUIRE(out.type == PIN_F32 && out.f == 1.25f && !out.record);

    char *text = arena_alloc(&producer, 16);
    strcpy(text, "native-text");
    const pin_string expected_text = { .bytes = "native-text",
                                       .length = sizeof("native-text") - 1 };
    pin_value values[] = {
        { .type = PIN_BOOL, .b = true },
        { .type = PIN_I32, .i = -37 },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = text, .length = (uint32_t)strlen(text) } }
    };
    const char *names[] = { "boolean", "integer", "text" };
    for (int i = 0; i < 3; i++) {
        args[0].text = &(const pin_string){ .bytes = names[i],
                                            .length = (uint32_t)strlen(names[i]) };
        args[1] = values[i];
        out = (pin_value){ .type = values[i].type };
        REQUIRE(!host(&ENTITY_SET_GLOBAL_CALL, args, &out));
        REQUIRE(out.type == values[i].type && !out.record);
        if (i == 2) {
            REQUIRE(arena_contains(&invocation, out.text));
            text[0] = 'X';
            arena_free(&producer);
            values[i].text = &expected_text;
            args[1] = values[i];
        }
        REQUIRE(pin_value_equal(&out, &values[i]));
        REQUIRE(!entities_get_global_value(globals, names[i], &reads, &saved));
        REQUIRE(pin_value_equal(&saved, &values[i]) && !saved.record);
        out = (pin_value){ .type = values[i].type };
        REQUIRE(!host(&ENTITY_GET_GLOBAL_CALL, args, &out));
        REQUIRE(pin_value_equal(&out, &values[i]) && !out.record);
        if (i == 2) {
            REQUIRE(arena_contains(&reads, saved.text));
            REQUIRE(arena_contains(&invocation, out.text));
        }
    }

    ecs_entity entity = ecs_world_create_entity(world);
    REQUIRE(entity.slot && ecs_entity_is_alive(world, entity));
    REQUIRE(!ecs_world_destroy_entity(world, entity));
    REQUIRE(!ecs_entity_is_alive(world, entity));
    args[0].text = &(const pin_string){ .bytes = "dead",
                                        .length = (uint32_t)strlen("dead") };
    args[1] = (pin_value){ .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .u64 = ecs_entity_to_u64(entity) };
    out = (pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity" };
    REQUIRE(!host(&ENTITY_SET_GLOBAL_CALL, args, &out));
    REQUIRE(out.u64 == args[1].u64 && out.u64 && !out.record);
    REQUIRE(!entities_get_global_value(globals, "dead", &reads, &saved));
    REQUIRE(saved.type == PIN_HANDLE && saved.u64 == 0 && !saved.record);
    REQUIRE(!host(&ENTITY_GET_GLOBAL_CALL, args, &out));
    REQUIRE(out.type == PIN_HANDLE && out.u64 == 0 && !out.record);
    return true;
}
void test_language_record_global_compatibility()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_globals_native_compatibility);
    pu_setup(NULL, NULL);
}
