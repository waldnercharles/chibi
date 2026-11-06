#include "runtime/fields/definition.h"
#include "runtime/fields/native.h"
#include "runtime/fields/prepared.h"
#include "runtime/material_field.h"
#include "runtime/material_field_prepared.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

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
    char name[] = "radius", type[] = "number";
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
    char error[192], name[] = "radius", type[] = "number", key[] = "preset/large";
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
TEST_CASE(field_z_is_slice_depth_and_zero_for_planar_bakes)
{
    char error[192];
    /* A sphere of radius 3 around the origin: X, Y and depth Z. */
    field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_Z },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 0, 0 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 1, 1 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 2, 2 } },
        { .op = FIELD_ADD, .count = 2, .inputs = { 3, 4 } },
        { .op = FIELD_ADD, .count = 2, .inputs = { 6, 5 } },
        { .op = FIELD_CONSTANT, .value = 9 },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 7, 8 } },
    };
    program = field_program_new(code, 10, 9, NULL, 0, error, sizeof error);
    REQUIRE(program);
    float value;
    REQUIRE(field_program_sample(program, (field_context){ 0 }, 1, 2, &value, error, sizeof error));
    REQUIRE(value == -4);
    float planar[6], slice[6];
    field_grid grid = { 3, 2, 1, 0, 0, 0 };
    REQUIRE(
        field_program_bake(program, (field_context){ 0 }, grid, planar, NULL, error, sizeof error)
    );
    grid.depth = 2;
    REQUIRE(
        field_program_bake(program, (field_context){ 0 }, grid, slice, NULL, error, sizeof error)
    );
    for (int i = 0; i < 6; i++) {
        REQUIRE(slice[i] == planar[i] + 4);
    }
    /* Prepared bytes keep the appended opcode. */
    size_t length;
    REQUIRE(field_prepared_write(program, &bytes, &length, error, sizeof error));
    size_t remaining = 32768;
    decoded = field_prepared_read_budget(bytes, length, &remaining, error, sizeof error);
    REQUIRE(decoded && field_program_instruction_at(decoded, 2).op == FIELD_Z);
    grid.depth = NAN;
    REQUIRE(
        !field_program_bake(program, (field_context){ 0 }, grid, slice, NULL, error, sizeof error)
    );
    return true;
}
TEST_CASE(point_bake_matches_grid_bake_at_the_same_positions)
{
    char error[192];
    /* Noise over X and Y plus depth Z, then a sqrt that fails on negatives. */
    field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_Z },
        { .op = FIELD_CONSTANT, .value = 0 },
        { .op = FIELD_NOISE, .count = 3, .inputs = { 0, 1, 3 } },
        { .op = FIELD_ADD, .count = 2, .inputs = { 4, 2 } },
        { .op = FIELD_DIVIDE, .count = 2, .inputs = { 5, 2 } },
    };
    program = field_program_new(code, 7, 6, NULL, 0, error, sizeof error);
    REQUIRE(program);
    baker = field_program_baker_new(program, (field_context){ .seed = 7 }, 1, error, sizeof error);
    REQUIRE(baker);
    enum
    {
        WIDTH = 23,
        HEIGHT = 31,
        COUNT = WIDTH * HEIGHT
    };
    field_grid grid = { WIDTH, HEIGHT, .5f, 3, -2, 1.5f };
    static float rows[COUNT], points[COUNT], x[COUNT], y[COUNT], z[COUNT];
    REQUIRE(field_program_baker_bake_rows(baker, grid, 0, HEIGHT, rows, NULL, error, sizeof error));
    for (int i = 0; i < COUNT; i++) {
        x[i] = grid.center_x + (i % WIDTH + .5f - WIDTH * .5f) * grid.cell_size;
        y[i] = grid.center_y + (i / WIDTH + .5f - HEIGHT * .5f) * grid.cell_size;
        z[i] = grid.depth;
    }
    REQUIRE(
        field_program_baker_bake_points(baker, x, y, z, COUNT, points, NULL, error, sizeof error)
    );
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(points[i] == rows[i]);
    }
    /* Division by a zero depth anywhere fails the whole call. */
    z[COUNT - 1] = 0;
    REQUIRE(
        !field_program_baker_bake_points(baker, x, y, z, COUNT, points, NULL, error, sizeof error)
    );
    REQUIRE(field_program_baker_bake_points(baker, x, y, z, 0, points, NULL, error, sizeof error));
    return true;
}
/* Cell op of (X, Y) with constant jitter and seed offset 3. */
static field_program *cell_program(field_op op, float jitter, char *error, size_t size)
{
    field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_CONSTANT, .value = jitter },
        { .op = FIELD_CONSTANT, .value = 3 },
        { .op = op, .count = 4, .inputs = { 0, 1, 2, 3 } },
    };
    return field_program_new(code, 5, 4, NULL, 0, error, size);
}
TEST_CASE(cells_are_the_voronoi_diagram_of_jittered_lattice_sites)
{
    char error[192];
    field_context context = { .seed = 11 };
    float value;
    /* Unjittered cells are hexagons of inradius one half. */
    program = cell_program(FIELD_CELL_EDGE, 0, error, sizeof error);
    REQUIRE(program);
    REQUIRE(field_program_sample(program, context, 0, 0, &value, error, sizeof error));
    REQUIRE(fabsf(value - .5f) < 1e-5f);
    REQUIRE(field_program_sample(program, context, .5f, 0, &value, error, sizeof error));
    REQUIRE(fabsf(value) < 1e-5f);
    field_program_free(program);
    program = cell_program(FIELD_CELL_Y, 0, error, sizeof error);
    REQUIRE(program);
    REQUIRE(field_program_sample(program, context, .6f, .8f, &value, error, sizeof error));
    REQUIRE(fabsf(value - .8660254f) < 1e-5f);
    field_program_free(program);
    /* Jittered sites: each lattice point lies in its own site's cell. */
    enum
    {
        SPAN = 7,
        SITES = (2 * SPAN + 1) * (2 * SPAN + 1)
    };
    static float sites[SITES][2];
    field_program *x = cell_program(FIELD_CELL_X, .25f, error, sizeof error);
    field_program *y = cell_program(FIELD_CELL_Y, .25f, error, sizeof error);
    program = cell_program(FIELD_CELL_EDGE, .25f, error, sizeof error);
    REQUIRE(x && y && program);
    int n = 0;
    for (int j = -SPAN; j <= SPAN; j++) {
        for (int i = -SPAN; i <= SPAN; i++, n++) {
            float px = i + .5f * j, py = j * .8660254f;
            REQUIRE(field_program_sample(x, context, px, py, &sites[n][0], error, sizeof error));
            REQUIRE(field_program_sample(y, context, px, py, &sites[n][1], error, sizeof error));
            float dx = sites[n][0] - px, dy = sites[n][1] - py;
            REQUIRE(dx * dx + dy * dy <= .0625f + 1e-6f);
        }
    }
    /* Brute force agrees on the site and the distance to the nearest
     * bisector across the middle of the lattice. */
    unsigned state = 1;
    for (int t = 0; t < 2000; t++) {
        state = state * 1664525u + 1013904223u;
        float px = (state >> 8) / 16777216.f * 6 - 3;
        state = state * 1664525u + 1013904223u;
        float py = (state >> 8) / 16777216.f * 6 - 3;
        int nearest = 0;
        float best = INFINITY;
        for (int k = 0; k < SITES; k++) {
            float dx = sites[k][0] - px, dy = sites[k][1] - py;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                nearest = k;
            }
        }
        float edge = INFINITY;
        for (int k = 0; k < SITES; k++) {
            if (k == nearest) {
                continue;
            }
            float dx = sites[k][0] - sites[nearest][0],
                  dy = sites[k][1] - sites[nearest][1];
            float mx = .5f * (sites[k][0] + sites[nearest][0]) - px,
                  my = .5f * (sites[k][1] + sites[nearest][1]) - py;
            edge = fminf(edge, (mx * dx + my * dy) / sqrtf(dx * dx + dy * dy));
        }
        float site_x, site_y;
        REQUIRE(field_program_sample(x, context, px, py, &site_x, error, sizeof error));
        REQUIRE(field_program_sample(y, context, px, py, &site_y, error, sizeof error));
        REQUIRE(field_program_sample(program, context, px, py, &value, error, sizeof error));
        REQUIRE(site_x == sites[nearest][0] && site_y == sites[nearest][1]);
        REQUIRE(fabsf(value - edge) < 1e-4f);
    }
    /* Row bakes agree with the scalar reference. */
    float row[12];
    field_grid grid = { 4, 3, .37f, .2f, -.4f, 0 };
    REQUIRE(field_program_bake(program, context, grid, row, NULL, error, sizeof error));
    for (int i = 0; i < 12; i++) {
        float px = grid.center_x + (i % 4 + .5f - 2) * grid.cell_size;
        float py = grid.center_y + (i / 4 + .5f - 1.5f) * grid.cell_size;
        REQUIRE(field_program_sample(program, context, px, py, &value, error, sizeof error));
        REQUIRE(row[i] == value);
    }
    field_program_free(x);
    field_program_free(y);
    field_program_free(program);
    /* Jitter past a quarter would need a wider search. */
    program = cell_program(FIELD_CELL_EDGE, .3f, error, sizeof error);
    REQUIRE(program);
    REQUIRE(!field_program_sample(program, context, 0, 0, &value, error, sizeof error));
    return true;
}
typedef struct
{
    field_op op;
    const char *name;
    const node_host_call *call;
} library_operation;
#define OPERATION(OP)                                                          \
    { FIELD_##OP, FIELD_NODE_##OP##_NAME, &FIELD_##OP##_CALL },
static const library_operation OPERATIONS[] = { FIELD_NATIVE_OPERATIONS(OPERATION) };
#undef OPERATION

/* The host call the definition pack binds to a field node, or NULL. */
static const node_host_call *bound_call(const char *name)
{
    for (int i = 0; i < FIELD_DEFINITION_PACK.binding_count; i++) {
        if (!strcmp(FIELD_DEFINITION_PACK.bindings[i].canonical_name, name)) {
            return FIELD_DEFINITION_PACK.bindings[i].implementation.call;
        }
    }
    return NULL;
}
/* Operands across and past each op's domain: zero divisors, inverted ranges,
 * negative radii and out-of-range octave counts and jitters. */
static float operand(unsigned *state)
{
    static const float SPECIAL[] = { 0, .25f, .5f, 1, 2, 3, 16, 17, -1 };
    *state = *state * 1664525u + 1013904223u;
    unsigned bits = *state >> 8;
    if (bits % 4 == 0) {
        return SPECIAL[(bits >> 2) % (sizeof SPECIAL / sizeof *SPECIAL)];
    }
    return (bits >> 2) / 4194304.f * 8 - 4;
}
static bool same_bits(float a, float b)
{
    return !memcmp(&a, &b, sizeof a);
}
TEST_CASE(library_calls_evaluate_every_field_operation_as_programs_do)
{
    enum
    {
        SAMPLES = 2000
    };
    char error[192];
    /* Every node but Field Output, the field's own, has a library call; all
     * but Constant, the four sample reads and Output take operands. */
    int definitions = FIELD_DEFINITION_PACK.definition_count;
    int operations = (int)(sizeof OPERATIONS / sizeof *OPERATIONS);
    REQUIRE(FIELD_DEFINITION_PACK.binding_count == definitions - 1);
    REQUIRE(operations == FIELD_OP_COUNT - 6);
    for (int i = 0; i < definitions; i++) {
        const char *name = FIELD_DEFINITION_PACK.definitions[i].canonical_name;
        REQUIRE(!bound_call(name) == !strcmp(name, FIELD_NODE_OUTPUT_NAME));
    }
    unsigned state = 1;
    for (int o = 0; o < operations; o++) {
        const library_operation *operation = &OPERATIONS[o];
        REQUIRE(bound_call(operation->name) == operation->call);
        int arity = field_op_arity(operation->op), valid = 0;
        for (int s = 0; s < SAMPLES; s++) {
            pin_value in[FIELD_INPUT_MAX];
            field_ir code[FIELD_INPUT_MAX + 1] = { 0 };
            code[arity] = (field_ir){ .op = operation->op, .count = arity };
            for (int i = 0; i < arity; i++) {
                float value = operand(&state);
                in[i] = (pin_value){ .type = PIN_F32, .f = value };
                code[i] = (field_ir){ .op = FIELD_CONSTANT, .value = value };
                code[arity].inputs[i] = i;
            }
            program = field_program_new(code, arity + 1, arity, NULL, 0, error, sizeof error);
            REQUIRE(program);
            float sampled, baked;
            bool sample = field_program_sample(program, (field_context){ 0 }, 0, 0, &sampled, error, sizeof error);
            field_grid cell = { 1, 1, 1, 0, 0, 0 };
            bool bake = field_program_bake(program, (field_context){ 0 }, cell, &baked, NULL, error, sizeof error);
            pin_value out = { .type = PIN_F32 };
            const char *failure = operation->call->invoke(NULL, in, &out);
            REQUIRE(sample == bake && sample == !failure);
            REQUIRE(!sample || (same_bits(out.f, sampled) && same_bits(out.f, baked)));
            REQUIRE(!failure || strstr(failure, operation->name));
            valid += sample;
            field_program_free(program);
            program = NULL;
        }
        REQUIRE(valid > 0);
    }
    /* A field never carries a nonfinite scalar, so neither do calls. */
    pin_value in[] = { { .type = PIN_F32, .f = NAN }, { .type = PIN_F32, .f = 1 } };
    pin_value out = { .type = PIN_F32 };
    REQUIRE(FIELD_MIN_CALL.invoke(NULL, in, &out));
    in[0].f = INFINITY;
    REQUIRE(FIELD_ADD_CALL.invoke(NULL, in, &out));
    REQUIRE(FIELD_CONSTANT_CALL.invoke(NULL, in, &out));
    REQUIRE(!FIELD_CONSTANT_CALL.invoke(NULL, &in[1], &out) && out.f == 1);
    /* Sample reads have no value outside a field program. */
    pin_value name = { .type = PIN_STR,
                       .text = &(const pin_string){ .bytes = "Bias",
                                                    .length = sizeof("Bias") - 1 } };
    REQUIRE(FIELD_X_CALL.invoke(NULL, NULL, &out));
    REQUIRE(FIELD_Y_CALL.invoke(NULL, NULL, &out));
    REQUIRE(FIELD_Z_CALL.invoke(NULL, NULL, &out));
    REQUIRE(FIELD_PARAMETER_CALL.invoke(NULL, &name, &out));
    return true;
}
/* A call's Seed Offset is its whole seed; a field program adds its own. */
TEST_CASE(library_calls_take_seed_offset_as_the_whole_seed)
{
    char error[192];
    field_ir code[] = {
        { .op = FIELD_CONSTANT, .value = .3f },
        { .op = FIELD_CONSTANT, .value = .6f },
        { .op = FIELD_CONSTANT, .value = 3 },
        { .op = FIELD_CONSTANT, .value = .2f },
        { .op = FIELD_NOISE, .count = 3, .inputs = { 0, 1, 2 } },
        { .op = FIELD_CELL_EDGE, .count = 4, .inputs = { 0, 1, 3, 2 } },
        { .op = FIELD_ADD, .count = 2, .inputs = { 4, 5 } },
    };
    program = field_program_new(code, 7, 6, NULL, 0, error, sizeof error);
    REQUIRE(program);
    float seeded, unseeded;
    REQUIRE(
        field_program_sample(program, (field_context){ .seed = 7 }, 0, 0, &seeded, error, sizeof error)
    );
    REQUIRE(
        field_program_sample(program, (field_context){ 0 }, 0, 0, &unseeded, error, sizeof error)
    );
    REQUIRE(seeded != unseeded);
    pin_value noise_in[] = { { .type = PIN_F32, .f = .3f },
                             { .type = PIN_F32, .f = .6f },
                             { .type = PIN_F32, .f = 10 } };
    pin_value cell_in[] = { noise_in[0],
                            noise_in[1],
                            { .type = PIN_F32, .f = .2f },
                            noise_in[2] };
    pin_value noise = { .type = PIN_F32 }, edge = { .type = PIN_F32 };
    REQUIRE(!FIELD_NOISE_CALL.invoke(NULL, noise_in, &noise));
    REQUIRE(!FIELD_CELL_EDGE_CALL.invoke(NULL, cell_in, &edge));
    REQUIRE(same_bits(noise.f + edge.f, seeded));
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
    RUN_TEST_CASE(field_z_is_slice_depth_and_zero_for_planar_bakes);
    RUN_TEST_CASE(point_bake_matches_grid_bake_at_the_same_positions);
    RUN_TEST_CASE(cells_are_the_voronoi_diagram_of_jittered_lattice_sites);
    RUN_TEST_CASE(library_calls_evaluate_every_field_operation_as_programs_do);
    RUN_TEST_CASE(library_calls_take_seed_offset_as_the_whole_seed);
    pu_clear_setup();
}
