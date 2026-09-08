#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/vector_nodes.h"
#include "graph/runtime/eval.h"
#include "runtime/components.h"
#include "support/arena.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void direction_to()
{
    const node_desc *direction = registry_node_by_name("Direction To");
    assert(direction == &NODE_Direction_To);
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
            { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = (void *)&origins[i] },
            { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = (void *)&targets[i] },
        };
        pin_value subtraction[] = { inputs[1], inputs[0] };
        pin_value delta = NODE_Subtract.fn(subtraction, 2, &memory);
        pin_value expected = NODE_Normalize_Vector_2.fn(&delta, 1, &memory);
        pin_value actual = direction->fn(inputs, 2, &memory);
        assert(((const CF_V2 *)actual.ptr)->x == ((const CF_V2 *)expected.ptr)->x);
        assert(((const CF_V2 *)actual.ptr)->y == ((const CF_V2 *)expected.ptr)->y);
        arena_reset(&memory);
    }
    graph g;
    graph_init(&g);
    node *n = graph_add_node(&g, direction);
    pin_value origin = { .type = PIN_STRUCT,
                         .type_name = "CF_V2",
                         .ptr = (void *)&origins[0] };
    pin_value target = { .type = PIN_STRUCT,
                         .type_name = "CF_V2",
                         .ptr = (void *)&targets[0] };
    assert(graph_set_pin_default(&g, n, 0, 0, &origin) == 0);
    assert(graph_set_pin_default(&g, n, 0, 1, &target) == 0);
    char *json = graph_save_json(&g);
    graph *loaded = graph_load_json(json);
    free(json);
    assert(loaded);
    eval_context context;
    eval_context_init(&context);
    pin_value value;
    assert(
        graph_eval_output(loaded, graph_node_by_id(loaded, n->id), 0, &memory, &value, &context) == 0
    );
    assert(((const CF_V2 *)value.ptr)->x == 0.6f && ((const CF_V2 *)value.ptr)->y == 0.8f);
    eval_context_free(&context);
    graph_free(loaded);
    free(loaded);
    graph_free(&g);
    arena_free(&memory);
}

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
    direction_to();
    puts("[vector] normalization handles zero and extreme magnitudes; zero scaling preserves vector output");
}
