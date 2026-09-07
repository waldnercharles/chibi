#include "core/arena.h"
#include "core/array.h"
#include "core/compile.h"
#include "core/conv_nodes.h"
#include "core/eval.h"
#include "core/instance.h"
#include "core/math_expression.h"
#include "core/registry.h"
#include "core/serialize.h"
#include "core/xalloc.h"
#include "game/generated.h"
#include "game/live.h"

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
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    assert(asset);
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0);
    compile_report_free(&report);
    return asset;
}

static float evaluate(const char *source, float x, float y)
{
    behavior_asset *asset = behavior_asset_new("Math boundary");
    math_expression_error error;
    node *call = math_expression_add(asset, asset->graph, source, &error);
    assert(call);
    for (int i = 0; i < call->input_count; i++) {
        pin_value value = { .type = PIN_F64, .d = i ? y : x };
        assert(graph_set_pin_default(asset->graph, call, 0, i, &value) == 0);
    }
    /* The authored consumers use fixed float pins. Exercise that real graph
     * boundary instead of comparing only a C cast of the expression result. */
    node *sink = graph_add_node(asset->graph, registry_node_by_name("Add"));
    assert(sink);
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(asset->graph, sink, 0, i, PIN_F32, "float") == 0);
        sink->input_pins[i].flags &= ~PIN_FLAG_WILDCARD;
    }
    assert(graph_set_pin_type(asset->graph, sink, 1, 0, PIN_F32, "float") == 0);
    sink->output_pins[0].flags &= ~PIN_FLAG_WILDCARD;
    assert(graph_connect_data(asset->graph, call, 0, sink, 0) == 0);
    compile_report report;
    compile_report_init(&report);
    assert(behavior_asset_compile(asset, &report) == 0);
    compile_report_free(&report);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    arena memory;
    arena_init(&memory, 4096);
    pin_value result;
    assert(graph_eval_output(asset->graph, sink, 0, &memory, &result, &context) == 0);
    assert(result.type == PIN_F32);
    float value = result.f;
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    return value;
}

static void health_damage()
{
    behavior_asset *asset = load("content/behaviors/HealthBehavior.json");
    graph *g = asset->graph;
    assert(!graph_node_by_id(g, 20) && !graph_node_by_id(g, 25) && !graph_node_by_id(g, 29));
    node *setter = graph_node_by_id(g, 38);
    assert(setter && setter->input_pins[1].type == PIN_I32);
    const node_desc *conversion = graph_input_conversion(g, setter, 1);
    assert(conversion && conversion == conversion_for(PIN_F64, PIN_I32));
    node *expression = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        const char *source = math_expression_source(asset, g->nodes[i]);
        if (source &&
            strcmp(source, "clamp(current - clamp(amount, 0, current), 0, maximum)") == 0) {
            expression = g->nodes[i];
        }
    }
    assert(expression);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    assert(game_live_add_definition(live, game_live_definition_new("HealthBehavior", asset)) == 0);
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
        { 10, 0, 0, 0, true },
        { 0, 100, 10, 0, false },
        { -1, 100, 10, -1, false },
    };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        ecs_entity entity = ecs_world_create_entity(world);
        assert(GAME_GameHealth_add(world, entity, 0) == ECS_OK);
        assert(ecs_world_apply_commands(world) == ECS_OK);
        *GAME_GameHealth_get(world, entity) = (GameHealth){ cases[i].current,
                                                            cases[i].maximum };
        assert(game_behavior_attach(live->behaviors, entity, "HealthBehavior") == 0);
        behavior_instance *instance = game_behavior_instance(live->behaviors, entity, "HealthBehavior");
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
        assert(game_entities_flush(live->entities) >= 0);
        assert(ecs_world_apply_commands(world) == ECS_OK);
        assert(ecs_entity_is_alive(world, entity) == !cases[i].destroyed);
    }
    game_live_world_free(live);
    ecs_world_free(world);
    puts("PASS: Health expression Damage dispatch preserves integer limits, living guard and zero-health destruction");
}

void test_behavior_math()
{
    health_damage();
    behavior_asset *projectile = load("content/behaviors/ProjectileBehavior.json");
    behavior_asset *trail = load("content/behaviors/MovementTrailBehavior.json");
    graph *direction = projectile->graphs[1];
    graph *emission = trail->graphs[1];
    node *magnitude = graph_node_by_id(direction, 150);
    node *rate = graph_node_by_id(emission, 153);
    assert(magnitude && rate);
    const char *magnitude_source = math_expression_source(projectile, magnitude);
    const char *rate_source = math_expression_source(trail, rate);
    assert(magnitude_source && rate_source);
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
            assert(evaluate(magnitude_source, values[i], values[j]) == expected);
        }
        float bounded = fminf(fmaxf(values[i], 0), 0.1f);
        assert(evaluate(rate_source, values[i], 0) == bounded * 64.0f);
    }
    float boundary[] = { nextafterf(0.1f, 0), 0.1f, nextafterf(0.1f, 1) };
    for (int i = 0; i < 3; i++) {
        assert(evaluate(rate_source, boundary[i], 0) == fminf(boundary[i], 0.1f) * 64.0f);
    }
    behavior_asset_free(projectile);
    behavior_asset_free(trail);
    puts("PASS: authored Math expressions preserve projectile and trail float boundaries");
}
