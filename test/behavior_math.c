#include "authoring/preview.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "graph/nodes/vector_nodes.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/instance.h"
#include "runtime/component_catalog.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/support/code_fixture.h"
#include "test/support/event_fixture.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static behavior_asset *load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 0);
    rewind(file);
    char *json = xmalloc((size_t)size + 1);
    assert(fread(json, 1, (size_t)size, file) == (size_t)size);
    json[size] = 0;
    fclose(file);
    behavior_asset *asset = test_project_asset(path, json);
    if (asset) {
        asset = link_test_libraries(asset);
    }
    free(json);
    assert(asset);
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0);
    compile_report_free(&report);
    return asset;
}

static float evaluate(behavior_asset *asset, graph *g, node *call, float x, float y)
{
    assert(call->desc == &NODE_CallPureFunction);
    int argument = 0;
    for (int i = 0; i < call->input_count; i++) {
        if (call->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        pin_type original_type = call->input_pins[i].type;
        node *source = NULL;
        int output = -1;
        if (graph_input_source(g, call, i, &source, &output)) {
            assert(graph_remove_edge(g, source, output, call, i) == 0);
        }
        assert(graph_set_pin_binding(g, call, false, i, NULL) == 0);
        assert(original_type == PIN_F32 || original_type == PIN_F64);
        float number = argument++ ? y : x;
        pin_value value = { .type = original_type };
        if (value.type == PIN_F32) {
            value.f = number;
        } else {
            value.d = number;
        }
        assert(graph_set_pin_default(g, call, false, i, &value) == 0);
    }
    assert(argument >= 1 && argument <= 2);
    /* Mirror the authored consumer's fixed float boundary. */
    node *sink = graph_add_node(g, registry_node_by_name("Add"));
    assert(sink);
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(g, sink, false, i, PIN_F32, "float") == 0);
        sink->input_pins[i].flags &= ~PIN_FLAG_WILDCARD;
    }
    assert(graph_set_pin_type(g, sink, true, 0, PIN_F32, "float") == 0);
    sink->output_pins[0].flags &= ~PIN_FLAG_WILDCARD;
    assert(graph_connect_data(g, call, 0, sink, 0) == 0);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    arena memory;
    arena_init(&memory, 4096);
    pin_value result;
    assert(graph_eval_output(g, sink, 0, &memory, &result, &context) == 0);
    assert(result.type == PIN_F32);
    float value = result.f;
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    assert(graph_remove_node(g, sink) == 0);
    return value;
}

static void health_damage()
{
    behavior_asset *asset = load("test/fixtures/language/content/behaviors/HealthBehavior.chibi");
    node *setter = NULL;
    graph *setter_graph = NULL;
    node *expression = NULL;
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (strcmp(n->desc->name, "Set_GameHealth_current") == 0) {
                setter = n;
                setter_graph = g;
            }
        }
    }
    assert(setter && setter->input_pins[1].type == PIN_I32);
    int expression_output = -1;
    assert(graph_input_source(setter_graph, setter, 1, &expression, &expression_output));
    assert(expression && expression->desc == &NODE_BehaviorExpression);
    assert(expression_output >= 0 && expression->output_pins[expression_output].type == PIN_F64);
    const node_desc *conversion = graph_input_conversion(setter_graph, setter, 1);
    assert(conversion && conversion == conversion_for(PIN_F64, PIN_I32));
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_preview_world *live = game_preview_world_new(world);
    register_damage_event(live);
    assert(
        game_preview_add_definition(live, game_preview_definition_new("HealthBehavior", asset)) == 0
    );
    const struct
    {
        int current, maximum, amount, expected;
        bool destroyed;
    } cases[] = {
        { 100, 100, 25, 75, false },
        { 100, 100, -25, 100, false },
        { 100, 100, INT_MIN, 100, false },
        { 100, 100, INT_MAX, 0, true },
        { 100, 100, 100, 0, true },
        { 150, 100, 25, 100, false },
        { INT_MAX, INT_MAX, 1, INT_MAX - 1, false },
        { INT_MAX, INT_MAX, INT_MAX - 1, 1, false },
        { 1, INT_MAX, 0, 1, false },
        { 10, 0, 0, 10, false },
        { 10, 0, 1, 0, true },
        { 0, 100, 10, 0, false },
        { -1, 100, 10, -1, false },
    };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        ecs_entity entity = ecs_world_create_entity(world);
        assert(GAME_GameHealth_add(world, entity, 0) == ECS_OK);
        assert(ecs_world_apply_commands(world) == ECS_OK);
        *GAME_GameHealth_get(world, entity) = (GameHealth){ cases[i].current,
                                                            cases[i].maximum };
        assert(game_behavior_attach(live->runtime->behaviors, entity, "HealthBehavior") == 0);
        behavior_instance *instance = game_behavior_instance(live->runtime->behaviors, entity, "HealthBehavior");
        assert(instance);
        pin_value values[] = {
            { .type = PIN_I32, .type_name = "int", .i = cases[i].amount },
            { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 0 },
        };
        flow_error error;
        flow_run *run = behavior_instance_dispatch(instance, "Damage", (flow_inputs){ values, 2 }, &error);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        assert(GAME_GameHealth_get(world, entity)->current == cases[i].expected);
        assert(GAME_GameHealth_get(world, entity)->maximum == cases[i].maximum);
        assert(game_entities_dispatch_events(live->runtime->entities) >= 0);
        assert(game_entities_flush(live->runtime->entities) >= 0);
        assert(ecs_world_apply_commands(world) == ECS_OK);
        assert(ecs_entity_is_alive(world, entity) == !cases[i].destroyed);
    }
    game_preview_world_free(live);
    ecs_world_free(world);
    puts("PASS: Health expression preserves integer limits, rejects nonpositive damage, and delivers death before destruction");
}

static void integer_division()
{
    const int64_t numerators[] = { -17, -8, -1, 0, 7, 8, 17 };
    const int64_t divisors[] = { -8, -3, 3, 8 };
    arena storage;
    arena_init(&storage, 4096);
    eval_context eval;
    eval_context_init(&eval);
    for (int wide = 0; wide < 2; wide++) {
        graph g;
        graph_init(&g);
        node *divide = graph_add_node(&g, registry_node_by_name("Divide"));
        pin_type type = wide ? PIN_I64 : PIN_I32;
        const char *name = wide ? "int64" : "int";
        assert(graph_set_pin_type(&g, divide, false, 0, type, name) >= 0);
        assert(graph_set_pin_type(&g, divide, false, 1, type, name) >= 0);
        assert(graph_set_pin_type(&g, divide, true, 0, type, name) >= 0);
        for (int i = 0; i < 7; i++) {
            for (int j = 0; j < 4; j++) {
                pin_value a = { .type = type, .type_name = name }, b = a, result;
                if (wide) {
                    a.i64 = numerators[i];
                    b.i64 = divisors[j];
                } else {
                    a.i = (int)numerators[i];
                    b.i = (int)divisors[j];
                }
                assert(graph_set_pin_default(&g, divide, false, 0, &a) == 0);
                assert(graph_set_pin_default(&g, divide, false, 1, &b) == 0);
                eval_context_reset_runtime(&eval);
                assert(graph_eval_output(&g, divide, 0, &storage, &result, &eval) == 0);
                assert(result.type == type);
                assert((wide ? result.i64 : result.i) == numerators[i] / divisors[j]);
            }
        }
        for (int overflow = 0; overflow < 2; overflow++) {
            pin_value a = { .type = type, .type_name = name }, b = a, result;
            if (wide) {
                a.i64 = INT64_MIN;
                b.i64 = overflow ? -1 : 0;
            } else {
                a.i = INT_MIN;
                b.i = overflow ? -1 : 0;
            }
            assert(graph_set_pin_default(&g, divide, false, 0, &a) == 0);
            assert(graph_set_pin_default(&g, divide, false, 1, &b) == 0);
            eval_context_reset_runtime(&eval);
            assert(graph_eval_output(&g, divide, 0, &storage, &result, &eval) < 0);
        }
        graph_free(&g);
    }
    eval_context_free(&eval);
    arena_free(&storage);
}

static void random_ranges()
{
    arena storage;
    arena_init(&storage, 4096);
    eval_context eval;
    eval_context_init(&eval);
    srand(1234);
    pin_value real_inputs[] = {
        { .type = PIN_F64, .d = 8 },
        { .type = PIN_F64, .d = -2 },
    };
    for (int i = 0; i < 128; i++) {
        pin_value result = NODE_RandomRangeReal.fn(real_inputs, 2, &storage);
        assert(result.type == PIN_F64 && result.d >= -2 && result.d <= 8);
    }
    real_inputs[0].d = 4;
    real_inputs[1].d = 4;
    pin_value equal = NODE_RandomRangeReal.fn(real_inputs, 2, &storage);
    assert(equal.type == PIN_F64 && equal.d == 4);
    real_inputs[0].d = DBL_MAX;
    real_inputs[1].d = DBL_MAX;
    equal = NODE_RandomRangeReal.fn(real_inputs, 2, &storage);
    assert(equal.type == PIN_F64 && equal.d == DBL_MAX);
    real_inputs[0].d = NAN;
    pin_value invalid = NODE_RandomRangeReal.fn(real_inputs, 2, &storage);
    assert(invalid.type == PIN_FLOW);

    CF_V3 min = { 4, -FLT_MAX, 2 };
    CF_V3 max = { -4, FLT_MAX, 2 };
    pin_value vector_inputs[] = {
        { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &min },
        { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &max },
    };
    for (int i = 0; i < 128; i++) {
        pin_value result = NODE_Random_Range_Vector.fn(vector_inputs, 2, &storage);
        assert(result.type == PIN_STRUCT && result.ptr);
        const CF_V3 *value = result.ptr;
        assert(value->x >= -4 && value->x <= 4);
        assert(isfinite(value->y) && value->y >= -FLT_MAX && value->y <= FLT_MAX);
        assert(value->z == 2);
    }
    min = (CF_V3){ 3, 5, -1 };
    max = min;
    equal = NODE_Random_Range_Vector.fn(vector_inputs, 2, &storage);
    assert(equal.type == PIN_STRUCT && equal.ptr);
    assert(!memcmp(equal.ptr, &min, sizeof min));
    min.x = NAN;
    invalid = NODE_Random_Range_Vector.fn(vector_inputs, 2, &storage);
    assert(invalid.type == PIN_FLOW);

    graph scalar_graph;
    graph_init(&scalar_graph);
    node *scalar = graph_add_node(&scalar_graph, &NODE_RandomRangeReal);
    assert(scalar);
    pin_value scalar_min = { .type = PIN_F64, .d = -2 };
    pin_value scalar_max = { .type = PIN_F64, .d = 8 };
    assert(graph_set_pin_default(&scalar_graph, scalar, false, 0, &scalar_min) == 0);
    assert(graph_set_pin_default(&scalar_graph, scalar, false, 1, &scalar_max) == 0);
    pin_value graph_result;
    eval_context_reset_runtime(&eval);
    assert(graph_eval_output(&scalar_graph, scalar, 0, &storage, &graph_result, &eval) == 0);
    assert(graph_result.type == PIN_F64 && graph_result.d >= -2 && graph_result.d <= 8);
    scalar_min.d = NAN;
    assert(graph_set_pin_default(&scalar_graph, scalar, false, 0, &scalar_min) == 0);
    eval_context_reset_runtime(&eval);
    assert(graph_eval_output(&scalar_graph, scalar, 0, &storage, &graph_result, &eval) < 0);
    graph_free(&scalar_graph);

    graph vector_graph;
    graph_init(&vector_graph);
    node *range = graph_add_node(&vector_graph, &NODE_Random_Range_Vector);
    assert(range);
    CF_V3 graph_min = { 4, -3, 2 };
    CF_V3 graph_max = { -4, 7, 2 };
    pin_value vector_min = { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &graph_min };
    pin_value vector_max = { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &graph_max };
    assert(graph_set_pin_default(&vector_graph, range, false, 0, &vector_min) == 0);
    assert(graph_set_pin_default(&vector_graph, range, false, 1, &vector_max) == 0);
    eval_context_reset_runtime(&eval);
    assert(graph_eval_output(&vector_graph, range, 0, &storage, &graph_result, &eval) == 0);
    assert(graph_result.type == PIN_STRUCT && graph_result.ptr);
    const CF_V3 *graph_value = graph_result.ptr;
    assert(graph_value->x >= -4 && graph_value->x <= 4);
    assert(graph_value->y >= -3 && graph_value->y <= 7);
    assert(graph_value->z == 2);
    graph_min.x = NAN;
    assert(graph_set_pin_default(&vector_graph, range, false, 0, &vector_min) == 0);
    eval_context_reset_runtime(&eval);
    assert(graph_eval_output(&vector_graph, range, 0, &storage, &graph_result, &eval) < 0);
    graph_free(&vector_graph);
    eval_context_free(&eval);
    arena_free(&storage);
    puts("PASS: random range nodes preserve authored bounds through direct and graph evaluation, including reversed/equal endpoints and invalid input");
}

static void damped_spring()
{
    pin_value inputs[] = {
        { .type = PIN_F32, .f = 0 }, { .type = PIN_F32, .f = 0 },
        { .type = PIN_F32, .f = 1 }, { .type = PIN_F32, .f = 1 },
        { .type = PIN_F32, .f = 1 }, { .type = PIN_F32, .f = 1.0f / 60 },
    };
    pin_value outputs[2];
    arena storage;
    arena_init(&storage, 4096);
    assert(!NODE_Advance_Damped_Spring.read_inputs(NULL, inputs, &storage, outputs));
    double delta = inputs[5].f;
    double omega = 2 * 3.14159265358979323846;
    double damping_term = 2 * delta * omega;
    double delta_term = delta * delta * omega * omega;
    double inverse = 1 / (1 + damping_term + delta_term);
    assert(outputs[0].type == PIN_F32 && outputs[1].type == PIN_F32);
    assert(fabsf(outputs[0].f - (float)(delta_term * inverse)) < 1e-6f);
    assert(fabsf(outputs[1].f - (float)(delta * omega * omega * inverse)) < 1e-6f);
    for (int i = 0; i < 240; i++) {
        inputs[0].f = outputs[0].f;
        inputs[1].f = outputs[1].f;
        assert(!NODE_Advance_Damped_Spring.read_inputs(NULL, inputs, &storage, outputs));
    }
    assert(fabsf(outputs[0].f - inputs[2].f) < .001f && fabsf(outputs[1].f) < .01f);
    inputs[5].f = -1;
    inputs[0].f = 0;
    inputs[1].f = 0;
    assert(!NODE_Advance_Damped_Spring.read_inputs(NULL, inputs, &storage, outputs));
    assert(outputs[0].f == 0 && outputs[1].f == 0);
    inputs[0].f = NAN;
    assert(NODE_Advance_Damped_Spring.read_inputs(NULL, inputs, &storage, outputs));
    arena_free(&storage);
    puts("PASS: generic damped spring preserves the authored implicit response, clamps negative delta, and rejects nonfinite input");
}

static void angle_helpers()
{
    pin_value inputs[] = {
        { .type = PIN_F64, .d = 3 * 3.14159265358979323846 / 4 },
        { .type = PIN_F64, .d = 3.14159265358979323846 / 2 },
    };
    pin_value result = NODE_Quantize_Angle.fn(inputs, 2, NULL);
    assert(result.type == PIN_F64 && result.d == 3.14159265358979323846 / 2 * 2);
    inputs[1].d = 0;
    assert(NODE_Quantize_Angle.fn(inputs, 2, NULL).type == PIN_FLOW);
    puts("PASS: generic angle quantization snaps finite radians and rejects a zero step");
}

static void easing_helpers()
{
    pin_value input = { .type = PIN_F64, .d = -.5 };
    pin_value result = NODE_Smooth_Step.fn(&input, 1, NULL);
    assert(result.type == PIN_F64 && result.d == 0);
    input.d = .5;
    result = NODE_Smooth_Step.fn(&input, 1, NULL);
    assert(result.type == PIN_F64 && result.d == .5);
    input.d = 1.5;
    result = NODE_Smooth_Step.fn(&input, 1, NULL);
    assert(result.type == PIN_F64 && result.d == 1);
    input.d = NAN;
    assert(NODE_Smooth_Step.fn(&input, 1, NULL).type == PIN_FLOW);
    puts("PASS: generic Smooth Step clamps normalized easing and rejects nonfinite input");
}

void test_behavior_math()
{
    integer_division();
    random_ranges();
    damped_spring();
    angle_helpers();
    easing_helpers();
    health_damage();
    behavior_asset *projectile = load("test/fixtures/content/behaviors/ProjectileBehavior.chibi");
    behavior_asset *trail = load("test/fixtures/content/behaviors/MovementTrailBehavior.chibi");
    graph *direction = projectile->graphs[1];
    graph *emission = trail->graphs[1];
    node *magnitude = graph_node_by_id(direction, 150);
    node *rate = graph_node_by_id(emission, 153);
    assert(magnitude && rate);
    assert(
        magnitude->desc == &NODE_CallPureFunction &&
        !strcmp(magnitude->reference_name, "Direction Magnitude")
    );
    assert(rate->desc == &NODE_CallPureFunction && !strcmp(rate->reference_name, "Emission Budget"));
    assert(
        !graph_node_by_id(direction, 95) && !graph_node_by_id(direction, 98) &&
        !graph_node_by_id(direction, 102)
    );
    assert(!graph_node_by_id(emission, 53) && !graph_node_by_id(emission, 58));
    assert(graph_node_by_id(direction, 106)->input_pins[1].type == PIN_F32);
    assert(graph_node_by_id(emission, 64)->input_pins[1].type == PIN_F32);
    const float values[] = { 0,       -0.0f,    FLT_TRUE_MIN, -FLT_TRUE_MIN,
                             0.0001f, -0.0001f, 0.1f,         -0.1f,
                             1,       -1,       12345.67f,    FLT_MAX / 4 };
    for (int i = 0; i < (int)(sizeof values / sizeof *values); i++) {
        for (int j = 0; j < (int)(sizeof values / sizeof *values); j++) {
            float expected = fabsf(values[i]) + fabsf(values[j]);
            assert(evaluate(projectile, direction, magnitude, values[i], values[j]) == expected);
        }
        float bounded = fminf(fmaxf(values[i], 0), 0.1f);
        assert(evaluate(trail, emission, rate, values[i], 0) == bounded * 64.0f);
    }
    float boundary[] = { nextafterf(0.1f, 0), 0.1f, nextafterf(0.1f, 1) };
    for (int i = 0; i < 3; i++) {
        assert(
            evaluate(trail, emission, rate, boundary[i], 0) ==
            fminf(boundary[i], 0.1f) * 64.0f
        );
    }
    behavior_asset_free(projectile);
    behavior_asset_free(trail);
    puts("PASS: authored functions preserve projectile and trail float boundaries");
}
