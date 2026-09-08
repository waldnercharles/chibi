#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/eval.h"
#include "runtime/components.h"
#include "support/arena.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
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

static void manual_integer64_conversion()
{
    behavior_asset *asset = behavior_asset_new("Manual Integer64");
    node *source = add(asset->graph, "Real");
    node *cast = add(asset->graph, "To Integer64");
    assert(cast->input_pins[0].type == PIN_F64);
    pin_value input = { .type = PIN_F64, .d = 1000000000000.75 };
    assert(graph_set_pin_default(asset->graph, source, 0, 0, &input) == 0);
    assert(graph_connect_data(asset->graph, source, 0, cast, 0) == 0);
    uint64_t id = cast->id;
    char *json = behavior_asset_save_json(asset);
    behavior_asset *loaded = behavior_asset_load_json(json);
    assert(loaded);
    free(json);
    behavior_asset_free(asset);
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(loaded, &report) == 0);
    compile_report_free(&report);
    arena memory;
    arena_init(&memory, 64);
    eval_context context;
    eval_context_init(&context);
    pin_value result;
    assert(
        graph_eval_output(loaded->graph, graph_node_by_id(loaded->graph, id), 0, &memory, &result, &context) ==
        0
    );
    assert(result.type == PIN_I64 && result.i64 == INT64_C(1000000000000));
    eval_context_free(&context);
    arena_free(&memory);
    behavior_asset_free(loaded);
}

static void real_integer_conversions()
{
    const pin_type integers[] = { PIN_U8, PIN_I32, PIN_I64 };
    const pin_type reals[] = { PIN_F32, PIN_F64 };
    const double values[] = { 0,
                              -0.0,
                              1.9,
                              -1.9,
                              255.9,
                              -255.9,
                              2147483647.0,
                              -2147483648.0,
                              0x1p63,
                              -0x1p63,
                              nextafter(0x1p63, 0),
                              INFINITY,
                              -INFINITY,
                              NAN };
    for (int r = 0; r < 2; r++) {
        for (int t = 0; t < 3; t++) {
            behavior_asset *asset = behavior_asset_new("Real narrowing");
            graph *g = asset->graph;
            node *source = add(g, "Real");
            assert(graph_set_pin_type(g, source, 0, 0, reals[r], pin_type_name(reals[r])) == 0);
            assert(graph_set_pin_type(g, source, 1, 0, reals[r], pin_type_name(reals[r])) == 0);
            const char *literal[] = { "Byte", "Integer", "Integer64" };
            node *sink = add(g, literal[t]);
            const node_desc *conversion = conversion_for(reals[r], integers[t]);
            assert(conversion && registry_node_by_name(conversion->name) == conversion);
            assert(graph_connect_data_conversion(g, source, 0, sink, 0, conversion) == 0);
            uint64_t source_id = source->id, sink_id = sink->id;
            char *json = behavior_asset_save_json(asset);
            assert(json);
            behavior_asset *loaded = behavior_asset_load_json(json);
            assert(loaded);
            free(json);
            behavior_asset_free(asset);
            asset = loaded;
            g = asset->graph;
            source = graph_node_by_id(g, source_id);
            sink = graph_node_by_id(g, sink_id);
            assert(graph_input_conversion(g, sink, 0) == conversion);
            for (int i = 0; i < (int)(sizeof values / sizeof *values); i++) {
                pin_value input = { .type = reals[r] };
                if (reals[r] == PIN_F32) {
                    input.f = (float)values[i];
                } else {
                    input.d = values[i];
                }
                assert(graph_set_pin_default(g, source, 0, 0, &input) == 0);
                compile_report report;
                compile_report_init(&report);
                assert(behavior_asset_compile(asset, &report) == 0);
                compile_report_free(&report);
                arena memory;
                arena_init(&memory, 64);
                eval_context context;
                eval_context_init(&context);
                pin_value result;
                assert(graph_eval_output(g, sink, 0, &memory, &result, &context) == 0);
                double value = reals[r] == PIN_F32 ? input.f : input.d;
                long double low = t == 0 ? 0 : t == 1 ? INT32_MIN : INT64_MIN;
                long double high = t == 0 ? UINT8_MAX
                                 : t == 1 ? INT32_MAX
                                          : (long double)INT64_MAX;
                long double expected = isnan(value) ? 0 : truncl(value);
                expected = fmaxl(low, fminl(high, expected));
                assert(result.type == integers[t]);
                int64_t actual = t == 0 ? result.u8
                               : t == 1 ? result.i
                                        : result.i64;
                /* On platforms where long double equals double, compare the
                 * saturating endpoints before converting the reference. */
                int64_t wanted = expected >= high ? (t == 0   ? UINT8_MAX
                                                     : t == 1 ? INT32_MAX
                                                              : INT64_MAX)
                               : expected <= low  ? (t == 0   ? 0
                                                     : t == 1 ? INT32_MIN
                                                              : INT64_MIN)
                                                  : (int64_t)expected;
                assert(actual == wanted);
                eval_context_free(&context);
                arena_free(&memory);
            }
            behavior_asset_free(asset);
        }
    }
    puts("PASS: Real narrowing truncates and saturates through graph edges and serialization");
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
    real_integer_conversions();
    manual_integer64_conversion();
    puts("PASS: one-field component values connect without Break nodes and survive compile, evaluation and reload");
}
