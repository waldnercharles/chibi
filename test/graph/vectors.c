#include "runtime/vectors.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/arithmetic.h"
#include "runtime/components.h"
#include "support/arena.h"
#include "support/array.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static bool direction_to()
{
    const node_desc *direction = graph_palette_node("Direction To");
    REQUIRE(direction->execution_call == &NATIVE_direction_to_CALL);
    const CF_V2 origins[] = { { 1, 2 }, { 3, 4 }, { -2, 5 }, { -FLT_MAX, 0 }, { 0, 0 } };
    const CF_V2 targets[] = { { 4, 6 },
                              { 3, 4 },
                              { -5, 1 },
                              { FLT_MAX, 0 },
                              { FLT_TRUE_MIN, 0 } };
    arena memory;
    arena_init(&memory, 256);
    for (int i = 0; i < (int)(sizeof origins / sizeof origins[0]); i++) {
        pin_value inputs[] = {
            { .type = PIN_STRUCT, .type_name = "vec2", .ptr = (void *)&origins[i] },
            { .type = PIN_STRUCT, .type_name = "vec2", .ptr = (void *)&targets[i] },
        };
        pin_value subtraction[] = { inputs[1], inputs[0] };
        pin_value delta = native_arithmetic_subtract(subtraction, 2, &memory);
        pin_value expected = native_normalize(&delta, 1, &memory);
        pin_value actual = native_direction_to(inputs, 2, &memory);
        REQUIRE(((const CF_V2 *)actual.ptr)->x == ((const CF_V2 *)expected.ptr)->x);
        REQUIRE(((const CF_V2 *)actual.ptr)->y == ((const CF_V2 *)expected.ptr)->y);
        arena_reset(&memory);
    }
    graph g;
    graph_init(&g);
    node *n = graph_add_node(&g, direction);
    pin_value origin = { .type = PIN_STRUCT,
                         .type_name = "vec2",
                         .ptr = (void *)&origins[0] };
    pin_value target = { .type = PIN_STRUCT,
                         .type_name = "vec2",
                         .ptr = (void *)&targets[0] };
    REQUIRE(graph_set_pin_default(&g, n, 0, 0, &origin) == 0);
    REQUIRE(graph_set_pin_default(&g, n, 0, 1, &target) == 0);
    char *json = graph_save(&g);
    graph *loaded = graph_load(json);
    free(json);
    REQUIRE(loaded);
    node *restored = graph_node_by_id(loaded, n->id);
    REQUIRE(restored && restored->desc == direction);
    pin_value restored_inputs[] = { restored->input_pins[0].default_value,
                                    restored->input_pins[1].default_value };
    pin_value value = native_direction_to(restored_inputs, 2, &memory);
    REQUIRE(((const CF_V2 *)value.ptr)->x == 0.6f && ((const CF_V2 *)value.ptr)->y == 0.8f);
    graph_free(loaded);
    free(loaded);
    graph_free(&g);
    arena_free(&memory);
    return true;
}

static bool test_zero_scale()
{
    arena memory;
    arena_init(&memory, 256);
    CF_V2 source = { 3, -4 };
    pin_value inputs[] = {
        { .type = PIN_STRUCT, .type_name = "vec2", .ptr = &source },
        { .type = PIN_F32, .type_name = "number", .f = 0 },
    };
    pin_value result = native_arithmetic_multiply(inputs, 2, &memory);
    REQUIRE(result.type == PIN_STRUCT && result.ptr);
    CF_V2 *vector = result.ptr;
    REQUIRE(vector->x == 0 && vector->y == 0);
    graph graph = { 0 };
    node *make = graph_add_node(&graph, graph_palette_node("Make_vec2"));
    pin_value x = { .type = PIN_F32, .f = 3 }, y = { .type = PIN_F32, .f = -4 };
    REQUIRE(graph_set_pin_default(&graph, make, 0, 0, &x) == 0);
    REQUIRE(graph_set_pin_default(&graph, make, 0, 1, &y) == 0);
    node *multiply = graph_add_node(&graph, &NODE_Multiply);
    REQUIRE(graph_connect_data(&graph, make, 0, multiply, 0) == 0);
    REQUIRE(graph_set_pin_default(&graph, multiply, 0, 1, &inputs[1]) == 0);
    REQUIRE(graph.edges && acount(graph.edges) == 1);
    CF_V2 composed = { make->input_pins[0].default_value.f,
                       make->input_pins[1].default_value.f };
    pin_value composed_inputs[] = {
        { .type = PIN_STRUCT, .type_name = "vec2", .ptr = &composed },
        multiply->input_pins[1].default_value
    };
    result = native_arithmetic_multiply(composed_inputs, 2, &memory);
    REQUIRE(result.type == PIN_STRUCT && result.ptr);
    vector = result.ptr;
    REQUIRE(vector->x == 0 && vector->y == 0);
    graph_free(&graph);
    arena_free(&memory);
    return true;
}

TEST_CASE(vector_extremes_defaults_and_zero_scale)
{
    const CF_V2 values[] = {
        { 0, 0 },
        { 3, 4 },
        { -3, 4 },
        { FLT_MAX, FLT_MAX },
        { FLT_TRUE_MIN, 0 },
        { INFINITY, 1 },
        { NAN, 1 },
    };
    const CF_V2 expected[] = {
        { 0, 0 }, { 0.6f, 0.8f }, { -0.6f, 0.8f }, { 0.70710678f, 0.70710678f },
        { 1, 0 }, { 0, 0 },       { 0, 0 },
    };
    arena memory;
    arena_init(&memory, 256);
    for (int i = 0; i < (int)(sizeof values / sizeof values[0]); i++) {
        pin_value input = { .type = PIN_STRUCT,
                            .type_name = "vec2",
                            .ptr = (void *)&values[i] };
        pin_value output = native_normalize(&input, 1, &memory);
        const CF_V2 *result = output.ptr;
        REQUIRE(fabsf(result->x - expected[i].x) < 0.00001f);
        REQUIRE(fabsf(result->y - expected[i].y) < 0.00001f);
        arena_reset(&memory);
    }
    arena_free(&memory);
    arena_init(&memory, sizeof(CF_V2));
    CF_V2 *stored = arena_alloc(&memory, sizeof *stored);
    *stored = (CF_V2){ 3, 4 };
    pin_value input = { .type = PIN_STRUCT, .type_name = "vec2", .ptr = stored };
    pin_value output = native_normalize(&input, 1, &memory);
    const CF_V2 *result = output.ptr;
    REQUIRE(fabsf(result->x - 0.6f) < 0.00001f && fabsf(result->y - 0.8f) < 0.00001f);
    arena_free(&memory);
    REQUIRE(test_zero_scale());
    REQUIRE(direction_to());
    return true;
}
/* A vector of floats scaled by an integer, as `vec2(x, y) * 40` passes an
 * untyped integer literal, scales each field by the integer's value, on
 * either side and at each integer width. */
TEST_CASE(vectors_scale_by_integers)
{
    arena memory;
    arena_init(&memory, 256);
    CF_V2 source = { 0.5f, -2 };
    const pin_value scales[] = {
        { .type = PIN_I32, .i = 40 },
        { .type = PIN_I64, .i64 = 40 },
        { .type = PIN_U64, .u64 = 40 },
    };
    for (int i = 0; i < (int)(sizeof scales / sizeof *scales); i++) {
        pin_value vector = { .type = PIN_STRUCT, .type_name = "vec2", .ptr = &source };
        pin_value right[] = { vector, scales[i] }, left[] = { scales[i], vector };
        pin_value scaled = native_arithmetic_multiply(right, 2, &memory);
        REQUIRE(scaled.type == PIN_STRUCT && scaled.ptr);
        REQUIRE(((CF_V2 *)scaled.ptr)->x == 20 && ((CF_V2 *)scaled.ptr)->y == -80);
        scaled = native_arithmetic_multiply(left, 2, &memory);
        REQUIRE(scaled.type == PIN_STRUCT && scaled.ptr);
        REQUIRE(((CF_V2 *)scaled.ptr)->x == 20 && ((CF_V2 *)scaled.ptr)->y == -80);
    }
    arena_free(&memory);
    return true;
}
void test_vector_nodes()
{
    graph_palette_open();
    RUN_TEST_CASE(vector_extremes_defaults_and_zero_scale);
    RUN_TEST_CASE(vectors_scale_by_integers);
    graph_palette_close();
}
