#include "../core/arena.h"
#include "../core/eval.h"
#include "../core/registry.h"
#include "../game/components.h"
#include "../game/vector_nodes.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static void test_zero_scale()
{
    arena memory;
    arena_init(&memory, 256);
    CF_V2 source = { 3, -4 };
    pin_value inputs[] = {
        { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &source },
        { .type = PIN_F32, .type_name = "float", .f = 0 },
    };
    pin_value result = NODE_Multiply.fn(inputs, 2, &memory);
    assert(result.type == PIN_STRUCT && result.ptr);
    CF_V2 *vector = result.ptr;
    assert(vector->x == 0 && vector->y == 0);
    graph graph = { 0 };
    node *make = graph_add_node(&graph, registry_node_by_name("Make_CF_V2"));
    pin_value x = { .type = PIN_F32, .f = 3 }, y = { .type = PIN_F32, .f = -4 };
    assert(graph_set_pin_default(&graph, make, 0, 0, &x) == 0);
    assert(graph_set_pin_default(&graph, make, 0, 1, &y) == 0);
    node *multiply = graph_add_node(&graph, &NODE_Multiply);
    assert(graph_connect_data(&graph, make, 0, multiply, 0) == 0);
    assert(graph_set_pin_default(&graph, multiply, 0, 1, &inputs[1]) == 0);
    eval_context context;
    eval_context_init(&context);
    assert(graph_eval_output(&graph, multiply, 0, &memory, &result, &context) == 0);
    assert(result.type == PIN_STRUCT && result.ptr);
    vector = result.ptr;
    assert(vector->x == 0 && vector->y == 0);
    eval_context_free(&context);
    graph_free(&graph);
    arena_free(&memory);
}

void test_vector()
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
                            .type_name = "CF_V2",
                            .ptr = (void *)&values[i] };
        pin_value output = NODE_Normalize_Vector_2.fn(&input, 1, &memory);
        const CF_V2 *result = output.ptr;
        assert(fabsf(result->x - expected[i].x) < 0.00001f);
        assert(fabsf(result->y - expected[i].y) < 0.00001f);
        arena_reset(&memory);
    }
    arena_free(&memory);
    arena_init(&memory, sizeof(CF_V2));
    CF_V2 *stored = arena_alloc(&memory, sizeof *stored);
    *stored = (CF_V2){ 3, 4 };
    pin_value input = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = stored };
    pin_value output = NODE_Normalize_Vector_2.fn(&input, 1, &memory);
    const CF_V2 *result = output.ptr;
    assert(fabsf(result->x - 0.6f) < 0.00001f && fabsf(result->y - 0.8f) < 0.00001f);
    arena_free(&memory);
    test_zero_scale();
    puts("[vector] normalization handles zero and extreme magnitudes; zero scaling preserves vector output");
}
