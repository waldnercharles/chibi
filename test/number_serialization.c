#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "support/arena.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void integer_spelling(char *json, const char *decimal)
{
    size_t length = strlen(decimal);
    char *value = strstr(json, decimal);
    assert(value);
    while (value) {
        memmove(value + length - 2, value + length, strlen(value + length) + 1);
        value = strstr(value, decimal);
    }
}

void test_number_serialization()
{
    graph original;
    graph_init(&original);
    node *single = graph_add_node(&original, registry_node_by_name("Add"));
    node *real = graph_add_node(&original, registry_node_by_name("Real"));
    single->x = 123;
    single->y = -456;
    pin_value f = { .type = PIN_F32, .f = 123 };
    pin_value d = { .type = PIN_F64, .d = -456 };
    assert(graph_set_pin_default(&original, single, 0, 0, &f) == 0);
    assert(graph_set_pin_default(&original, real, 0, 0, &d) == 0);
    char *json = graph_save(&original);
    assert(json);
    integer_spelling(json, "123.0");
    integer_spelling(json, "-456.0");
    graph *loaded = graph_load(json);
    assert(loaded && acount(loaded->nodes) == 2);
    assert(loaded->nodes[0]->x == 123 && loaded->nodes[0]->y == -456);
    arena memory;
    arena_init(&memory, 256);
    eval_context context;
    eval_context_init(&context);
    pin_value result;
    assert(graph_eval_output(loaded, loaded->nodes[0], 0, &memory, &result, &context) == 0);
    assert(result.type == PIN_F32 && result.f == 123);
    assert(graph_eval_output(loaded, loaded->nodes[1], 0, &memory, &result, &context) == 0);
    assert(result.type == PIN_F64 && result.d == -456);
    eval_context_free(&context);
    arena_free(&memory);
    graph_free(loaded);
    free(loaded);
    graph_free(&original);
    free(json);
    puts("PASS: integer KDL numbers retain float defaults and graph coordinates");
}
