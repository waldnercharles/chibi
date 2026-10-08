#include "runtime/fields/prepared.h"
#include "runtime/material_field.h"
#include "runtime/material_field_prepared.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static field_program *program, *decoded;
static field_program_baker *baker;
static material_fields *fields;
static material_field_session *session;
static unsigned char *bytes, *changed;
static void cleanup()
{
    material_field_session_free(session);
    if (fields) {
        material_fields_free(fields);
    }
    field_program_baker_free(baker);
    field_program_free(decoded);
    field_program_free(program);
    free(bytes);
    free(changed);
    session = NULL;
    fields = NULL;
    baker = NULL;
    decoded = program = NULL;
    bytes = changed = NULL;
}
static field_program *circle(char *error, size_t size)
{
    char name[] = "radius", type[] = "float";
    field_parameter parameter = { name, { .type = PIN_F32, .type_name = type, .f = 2 } };
    field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_PARAMETER, .parameter = 0 },
        { .op = FIELD_CIRCLE, .count = 3, .inputs = { 0, 1, 2 } },
    };
    field_program *p = field_program_new(code, 4, 3, &parameter, 1, error, size);
    memset(name, 'x', sizeof name - 1);
    memset(type, 'x', sizeof type - 1);
    memset(code, 0, sizeof code);
    parameter.value.f = 999;
    return p;
}
TEST_CASE(prepared_field_survives_authoring_storage_and_serialized_bytes)
{
    char error[192];
    program = circle(error, sizeof error);
    REQUIRE(program);
    float value;
    REQUIRE(field_program_sample(program, (field_context){ 0 }, 3, 4, &value, error, sizeof error));
    REQUIRE(value == 3);
    size_t length;
    REQUIRE(field_prepared_write(program, &bytes, &length, error, sizeof error));
    size_t remaining = 1;
    REQUIRE(!field_prepared_read_budget(bytes, length, &remaining, error, sizeof error));
    REQUIRE(remaining == 1);
    remaining = 32768;
    decoded = field_prepared_read_budget(bytes, length, &remaining, error, sizeof error);
    REQUIRE(decoded);
    REQUIRE(remaining == 32768 - field_program_storage_bytes(decoded));
    field_program_free(program);
    program = NULL;
    memset(bytes, 0, length);
    free(bytes);
    bytes = NULL;
    field_parameter parameter = { "radius", { .type = PIN_F32, .f = 1 } };
    field_context context = { .parameters = &parameter, .parameter_count = 1 };
    REQUIRE(field_program_sample(decoded, context, 3, 4, &value, error, sizeof error));
    REQUIRE(value == 4);
    REQUIRE(!strcmp(field_program_parameter_at(decoded, 0).name, "radius"));
    return true;
}
TEST_CASE(prepared_field_baker_slices_match_scalar_geometry)
{
    char error[192];
    program = circle(error, sizeof error);
    REQUIRE(program);
    baker = field_program_baker_new(program, (field_context){ 0 }, 1, error, sizeof error);
    REQUIRE(baker);
    field_grid grid = { .width = 3, .height = 3, .cell_size = 1 };
    float output[9];
    for (int i = 0; i < 9; i++) {
        output[i] = 99;
    }
    REQUIRE(field_program_baker_bake_rows(baker, grid, 1, 1, output, NULL, error, sizeof error));
    REQUIRE(output[0] == 99 && output[8] == 99);
    REQUIRE(output[3] == -1 && output[4] == -2 && output[5] == -1);
    REQUIRE(field_program_baker_bake_rows(baker, grid, 0, 3, output, NULL, error, sizeof error));
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            float dx = (float)(x - 1), dy = (float)(y - 1);
            REQUIRE(output[y * 3 + x] == sqrtf(dx * dx + dy * dy) - 2);
        }
    }
    return true;
}
TEST_CASE(prepared_field_rejects_invalid_topology_and_truncated_records)
{
    char error[192];
    field_ir invalid[] = { { .op = FIELD_X },
                           { .op = FIELD_ABS, .count = 1, .inputs = { 1 } } };
    REQUIRE(!field_program_new(invalid, 2, 1, NULL, 0, error, sizeof error));
    invalid[1].inputs[0] = 0;
    invalid[1].count = 0;
    REQUIRE(!field_program_new(invalid, 2, 1, NULL, 0, error, sizeof error));
    program = circle(error, sizeof error);
    REQUIRE(program);
    size_t length;
    REQUIRE(field_prepared_write(program, &bytes, &length, error, sizeof error));
    REQUIRE(length < 512);
    for (size_t cut = 0; cut < length; cut++) {
        REQUIRE(!field_prepared_read(bytes, cut, error, sizeof error));
    }
    changed = malloc(length + 1);
    REQUIRE(changed);
    memcpy(changed, bytes, length);
    changed[length] = 0;
    REQUIRE(!field_prepared_read(changed, length + 1, error, sizeof error));
    changed[0] ^= 1;
    REQUIRE(!field_prepared_read(changed, length, error, sizeof error));
    return true;
}
TEST_CASE(typed_field_catalog_preserves_overrides_and_invalidates_old_sessions)
{
    char error[192];
    fields = material_fields_new();
    program = circle(error, sizeof error);
    REQUIRE(program);
    REQUIRE(material_field_register(fields, "Circle", program));
    program = NULL;
    field_parameter radius = { "radius", { .type = PIN_F32, .f = 1 } };
    field_context context = { .parameters = &radius, .parameter_count = 1 };
    field_grid grid = { .width = 3, .height = 3, .cell_size = 1 };
    float output[9];
    REQUIRE(
        material_field_bake(fields, "Circle", context, grid, output, NULL, error, sizeof error)
    );
    REQUIRE(output[4] == -1);
    session = material_field_session_new(fields, "Circle", context, NULL, error, sizeof error);
    REQUIRE(session);
    REQUIRE(material_field_session_matches(session, fields));
    program = circle(error, sizeof error);
    REQUIRE(program);
    REQUIRE(material_field_register(fields, "Circle", program));
    program = NULL;
    REQUIRE(!material_field_session_matches(session, fields));
    REQUIRE(
        !material_field_session_bake_rows(session, fields, grid, 0, 3, output, NULL, error, sizeof error)
    );
    REQUIRE(
        material_field_bake(fields, "Circle", (field_context){ 0 }, grid, output, NULL, error, sizeof error)
    );
    REQUIRE(output[4] == -2);
    radius.value = (pin_value){ .type = PIN_I32, .i = 1 };
    REQUIRE(
        !material_field_bake(fields, "Circle", context, grid, output, NULL, error, sizeof error)
    );
    return true;
}
TEST_CASE(typed_argument_catalog_survives_prepared_handoff)
{
    char error[192], name[] = "radius", type[] = "float", key[] = "preset/large";
    fields = material_fields_new();
    program = circle(error, sizeof error);
    REQUIRE(program);
    REQUIRE(material_field_register(fields, "Circle", program));
    program = NULL;
    field_parameter parameter = { name, { .type = PIN_F32, .type_name = type, .f = 3 } };
    REQUIRE(material_field_register_arguments(
        fields,
        "Circle",
        key,
        (field_context){ .seed = 71, .parameters = &parameter, .parameter_count = 1 },
        error,
        sizeof error
    ));
    memset(name, 'x', sizeof name - 1);
    memset(type, 'x', sizeof type - 1);
    memset(key, 'x', sizeof key - 1);
    parameter.value.f = 999;
    field_context context;
    REQUIRE(
        material_field_context(fields, "Circle", "preset/large", 1729, &context, error, sizeof error)
    );
    REQUIRE(context.seed == 1729 && context.parameters[0].value.f == 3);
    REQUIRE(!strcmp(context.parameters[0].name, "radius"));
    parameter = (field_parameter){ "radius", { .type = PIN_I32, .i = 99 } };
    REQUIRE(!material_field_register_arguments(
        fields,
        "Circle",
        "preset/large",
        (field_context){ .parameters = &parameter, .parameter_count = 1 },
        error,
        sizeof error
    ));
    REQUIRE(material_field_argument_count(fields) == 1);
    REQUIRE(!material_field_context(fields, "Circle", "missing", 0, &context, error, sizeof error));
    size_t length;
    REQUIRE(material_fields_prepared_write(fields, &bytes, &length, error, sizeof error));
    size_t remaining = 32768;
    REQUIRE(
        !material_fields_prepared_read_budget(bytes, length - 1, &remaining, error, sizeof error)
    );
    REQUIRE(remaining == 32768 && material_field_count(fields) == 1);
    remaining = 1;
    REQUIRE(!material_fields_prepared_read_budget(bytes, length, &remaining, error, sizeof error));
    REQUIRE(remaining == 1 && material_field_argument_count(fields) == 1);
    material_fields_free(fields);
    remaining = 32768;
    fields = material_fields_prepared_read_budget(bytes, length, &remaining, error, sizeof error);
    REQUIRE(fields);
    REQUIRE(remaining == 32768 - material_fields_storage_bytes(fields));
    memset(bytes, 0, length);
    free(bytes);
    bytes = NULL;
    REQUIRE(material_field_count(fields) == 1 && material_field_argument_count(fields) == 1);
    REQUIRE(
        material_field_context(fields, "Circle", "preset/large", 42, &context, error, sizeof error)
    );
    REQUIRE(context.seed == 42 && context.parameters[0].value.f == 3);
    float output[9];
    REQUIRE(material_field_bake(
        fields,
        "Circle",
        context,
        (field_grid){ .width = 3, .height = 3, .cell_size = 1 },
        output,
        NULL,
        error,
        sizeof error
    ));
    REQUIRE(output[4] == -3 && output[3] == -2);
    REQUIRE(!strcmp(material_field_argument_at(fields, 0).key, "preset/large"));
    return true;
}
void test_field_runtime()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_field_survives_authoring_storage_and_serialized_bytes);
    RUN_TEST_CASE(prepared_field_baker_slices_match_scalar_geometry);
    RUN_TEST_CASE(prepared_field_rejects_invalid_topology_and_truncated_records);
    RUN_TEST_CASE(typed_field_catalog_preserves_overrides_and_invalidates_old_sessions);
    RUN_TEST_CASE(typed_argument_catalog_survives_prepared_handoff);
    pu_clear_setup();
}
