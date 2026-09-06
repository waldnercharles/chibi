#include "../core/arena.h"
#include "../core/array.h"
#include "../core/compile.h"
#include "../core/conv_nodes.h"
#include "../core/eval.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../game/components.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static node *add(graph *graph, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    node *node = graph_add_node(graph, desc);
    assert(node);
    return node;
}

static void set_float(graph *graph, node *node, int input, float number)
{
    pin_value value = { .type = PIN_F32, .type_name = "float", .f = number };
    assert(graph_set_pin_default(graph, node, 0, input, &value) == 0);
}

static void connect_unwrapped(graph *graph, node *source, node *target, const char *name)
{
    const node_desc *conversion = conversion_for_pins(
        &source->output_pins[0],
        &target->input_pins[0]
    );
    assert(conversion && strcmp(conversion->name, name) == 0);
    assert(conversion->pure_kind == NODE_PURE_CALL && conversion->fn);
    assert(graph_connect_data_conversion(graph, source, 0, target, 0, conversion) == 0);
    assert(graph_input_conversion(graph, target, 0) == conversion);
}

static void verify(behavior_asset *asset, uint64_t comparison, uint64_t normalized, uint64_t product)
{
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    for (int i = 0; i < acount(report.items); i++) {
        if (report.items[i].severity == CMPL_ERROR) {
            fprintf(stderr, "component conversion: %s\n", report.items[i].message);
        }
    }
    assert(errors == 0);
    compile_report_free(&report);
    arena memory;
    arena_init(&memory, 16);
    eval_context context;
    eval_context_init(&context);
    pin_value value;
    assert(
        graph_eval_output(asset->graph, graph_node_by_id(asset->graph, comparison), 0, &memory, &value, &context) ==
        0
    );
    assert(value.type == PIN_BOOL && value.b);
    assert(
        graph_eval_output(asset->graph, graph_node_by_id(asset->graph, normalized), 0, &memory, &value, &context) ==
        0
    );
    assert(value.type == PIN_STRUCT && strcmp(value.type_name, "CF_V2") == 0);
    CF_V2 vector = *(const CF_V2 *)value.ptr;
    assert(fabsf(vector.x - 0.6f) < 0.0001f && fabsf(vector.y - 0.8f) < 0.0001f);
    assert(
        graph_eval_output(asset->graph, graph_node_by_id(asset->graph, product), 0, &memory, &value, &context) ==
        0
    );
    assert(value.type == PIN_F32 && value.f == 270.0f);
    eval_context_free(&context);
    arena_free(&memory);
}

static void rejected_conversions()
{
    pin_spec movement = { .type = PIN_STRUCT, .type_name = "GameMovement" };
    pin_spec scalar = { .type = PIN_F32, .type_name = "float" };
    pin_spec input = { .type = PIN_STRUCT, .type_name = "GameInput" };
    pin_spec vector = { .type = PIN_STRUCT, .type_name = "CF_V2" };
    pin_spec health = { .type = PIN_STRUCT, .type_name = "GameHealth" };
    pin_spec integer = { .type = PIN_I32, .type_name = "int" };
    pin_spec entity = { .type = PIN_HANDLE, .type_name = "ecs.entity" };
    pin_spec foreign = { .type = PIN_HANDLE, .type_name = "other.entity" };
    assert(!conversion_for_pins(&health, &integer));
    assert(!conversion_for_pins(&vector, &scalar));
    assert(!conversion_for_pins(&scalar, &movement));
    assert(!conversion_for_pins(&input, &movement));
    assert(!conversion_for_pins(&entity, &foreign));
    assert(!conversion_for_pins(&entity, &scalar));
    movement.container = PIN_CONTAINER_ARRAY;
    assert(!conversion_for_pins(&movement, &scalar));
    movement.container = PIN_CONTAINER_SINGLE;
    scalar.container = PIN_CONTAINER_ARRAY;
    assert(!conversion_for_pins(&movement, &scalar));
    input.container = vector.container = PIN_CONTAINER_ARRAY;
    assert(!conversion_for_pins(&input, &vector));

    graph graph;
    graph_init(&graph);
    node *wrong = add(&graph, "Make_GameHealth");
    node *target = add(&graph, "Greater Or Equal");
    assert(
        graph_connect_data_conversion(&graph, wrong, 0, target, 0, registry_node_by_name("Unwrap_GameMovement")) ==
        -1
    );
    assert(acount(graph.edges) == 0);
    graph_free(&graph);
}

void test_component_conversion()
{
    behavior_asset *asset = behavior_asset_new("Component values");
    graph *graph = asset->graph;
    node *movement = add(graph, "Make_GameMovement");
    node *comparison = add(graph, "Greater Or Equal");
    set_float(graph, movement, 0, 90.0f);
    set_float(graph, comparison, 1, 85.0f);
    connect_unwrapped(graph, movement, comparison, "Unwrap_GameMovement");

    node *vector = add(graph, "Make_CF_V2");
    node *input = add(graph, "Make_GameInput");
    node *normalized = add(graph, "Normalize Vector 2");
    set_float(graph, vector, 0, 3.0f);
    set_float(graph, vector, 1, 4.0f);
    assert(graph_connect_data(graph, vector, 0, input, 0) == 0);
    connect_unwrapped(graph, input, normalized, "Unwrap_GameInput");

    node *product = add(graph, "Multiply");
    assert(graph_connect_data(graph, movement, 0, product, 0) == 0);
    assert(product->input_pins[0].type == PIN_F32 && product->output_pins[0].type == PIN_F32);
    const node_desc *conversion = graph_input_conversion(graph, product, 0);
    assert(conversion && strcmp(conversion->name, "Unwrap_GameMovement") == 0);
    set_float(graph, product, 1, 3.0f);
    uint64_t comparison_id = comparison->id, normalized_id = normalized->id,
             product_id = product->id;
    verify(asset, comparison_id, normalized_id, product_id);
    char *json = behavior_asset_save_json(asset);
    assert(json);
    behavior_asset *loaded = behavior_asset_load_json(json);
    if (!loaded) {
        fprintf(stderr, "component conversion reload: %s\n", graph_last_error());
    }
    assert(loaded);
    verify(loaded, comparison_id, normalized_id, product_id);
    assert(
        strcmp(
            graph_input_conversion(loaded->graph, graph_node_by_id(loaded->graph, comparison_id), 0)
                ->name,
            "Unwrap_GameMovement"
        ) == 0
    );
    assert(
        strcmp(
            graph_input_conversion(loaded->graph, graph_node_by_id(loaded->graph, normalized_id), 0)
                ->name,
            "Unwrap_GameInput"
        ) == 0
    );
    free(json);
    behavior_asset_free(loaded);
    behavior_asset_free(asset);
    rejected_conversions();
    puts("PASS: one-field component values connect without Break nodes and survive compile, evaluation and reload");
}
