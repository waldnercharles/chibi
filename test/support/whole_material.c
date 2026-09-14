#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "test/whole_material_fixture.h"

#include <assert.h>
#include <math.h>

pin_value whole_material_value(int kind, bool nonfinite)
{
    static const float finite[] = { .25f, -.5f, .75f, 1.f },
                       invalid[] = { .25f, INFINITY, .75f, 1.f };
    pin_value values[] = { { .type = PIN_F32, .f = nonfinite ? INFINITY : .25f },
                           { .type = PIN_F64, .d = nonfinite ? 1e300 : .5 },
                           { .type = PIN_BOOL, .b = true },
                           { .type = PIN_STRUCT, .type_name = "ShaderVec2" },
                           { .type = PIN_STRUCT, .type_name = "ShaderVec3" },
                           { .type = PIN_STRUCT, .type_name = "ShaderVec4" },
                           { .type = PIN_STRUCT, .type_name = "CF_V2" },
                           { .type = PIN_I32, .i = 7 } };
    assert(kind >= 0 && kind < WHOLE_MATERIAL_KINDS);
    pin_value value = values[kind];
    if (value.type == PIN_STRUCT) {
        value.ptr = (void *)(nonfinite ? invalid : finite);
    }
    return value;
}
behavior_asset *whole_material_fixture(int kind, bool waits, bool arguments)
{
    behavior_asset *a = behavior_asset_new("Typed material writes");
    graph *g = behavior_asset_add_function(a, "Paint", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    node *first = graph_add_node(g, registry_node_by_name("Set Material Parameter")),
         *second = graph_add_node(g, first->desc);
    pin_value value = whole_material_value(kind, false);
    assert(!graph_set_pin_type(g, first, 0, 3, value.type, value.type_name));
    assert(!graph_set_pin_type(g, second, 0, 3, value.type, value.type_name));
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, event, "Run"));
    pin_value defaults[] = { { .type = PIN_HANDLE,
                               .type_name = "ecs.entity",
                               .u64 = UINT64_C(4294967297) },
                             { .type = PIN_STR, .str = "Value" },
                             value };
    for (int i = 1; i < 4; i++) {
        pin_spec spec = first->input_pins[i];
        spec.flags = spec.group = 0;
        spec.id = 0;
        spec.default_value = defaults[i - 1];
        assert(graph_add_pin(g, entry, 1, &spec) == i);
        assert(!graph_connect_data(g, entry, i, first, i));
        if (i != 2) {
            assert(!graph_connect_data(g, entry, i, second, i));
        }
        if (arguments) {
            assert(graph_add_pin(a->graph, event, 1, &spec) == i);
        }
    }
    assert(
        !graph_set_pin_default(g, second, 0, 2, &(pin_value){ .type = PIN_STR, .str = "Second" })
    );
    assert(!graph_connect_exec(g, entry, 0, first, 0, 0));
    if (waits) {
        node *delay = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(g, first, 0, delay, 0, 0));
        assert(!graph_connect_exec(g, delay, 0, second, 0, 0));
    } else {
        assert(!graph_connect_exec(g, first, 0, second, 0, 0));
    }
    assert(!graph_connect_exec(g, second, 0, ret, 0, 0));
    node *call = behavior_asset_add_function_call_node(a, g);
    assert(!graph_connect_exec(a->graph, event, 0, call, 0, 0));
    for (int i = 1; i < 4; i++) {
        assert(!graph_set_pin_default(a->graph, call, 0, i, &defaults[i - 1]));
        if (arguments) {
            assert(!graph_connect_data(a->graph, event, i, call, i));
        }
    }
    return a;
}
int whole_material_publish(int width)
{
    behavior_asset *a = behavior_asset_new("Typed host material");
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = a->graph;
    node *output = shader_graph_add_output(g, false);
    for (int i = 0; i < 2; i++) {
        node *parameter = graph_add_node(g, registry_node_by_name("Shader Parameter"));
        assert(!graph_set_node_reference(g, parameter, i, i ? "Second" : "Value"));
        node *source = parameter;
        if (width > 1) {
            const char *type = (const char *[]){ "ShaderVec2",
                                                 "ShaderVec3",
                                                 "ShaderVec4" }[width - 2];
            assert(!graph_set_pin_type(g, parameter, 1, 0, PIN_STRUCT, type));
            source = graph_add_node(g, registry_node_by_name("Shader Length"));
            assert(!graph_set_pin_type(g, source, 0, 0, PIN_STRUCT, type));
            assert(!graph_connect_data(g, parameter, 0, source, 0));
        }
        assert(!graph_connect_data(g, source, 0, output, i + 2));
    }
    shader_error error;
    bool ok = render_shader_upsert(a, &error);
    if (!ok) {
        fprintf(stderr, "%s\n", error.message);
    }
    assert(ok);
    int id = render_shader_find(a->name);
    assert(id && render_shader_parameter_width(id, 0) == width);
    behavior_asset_free(a);
    return id;
}
ecs_world *whole_material_world(ecs_entity *entity)
{
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameShader, NULL));
    *entity = ecs_world_create_entity(world);
    GameShader shader = { .material = "Typed host material" };
    assert(!ecs_world_record_add(world, *entity, GAME_COMPONENT_GameShader.id, &shader, 0));
    assert(!ecs_world_apply_commands(world));
    return world;
}
