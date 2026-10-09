#include "material_graphs.h"

#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/shader/shader_graph.h"
#include "types/catalog.h"

#include <assert.h>
#include <string.h>

type_catalog_pack test_material_graph_types()
{
    return (type_catalog_pack){ .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
}

static const node_desc *pack_node(const char *name)
{
    for (int i = 0; i < SHADER_PACK.node_count; i++) {
        if (!strcmp(SHADER_PACK.nodes[i]->name, name)) {
            return SHADER_PACK.nodes[i];
        }
    }
    return NULL;
}

static behavior_asset *new_material(const char *name)
{
    behavior_asset *material = behavior_asset_new(name);
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    return material;
}

static bool publish(behavior_asset *material, bool built)
{
    shader_error error = { 0 };
    built = built && render_shader_upsert(material, &error);
    behavior_asset_free(material);
    return built;
}

bool test_register_surface(const char *name, float roughness, float metallic)
{
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    pin_value values[] = { { .type = PIN_F32, .f = roughness },
                           { .type = PIN_F32, .f = metallic } };
    return publish(
        material,
        !graph_set_pin_default(g, output, 0, 2, &values[0]) &&
            !graph_set_pin_default(g, output, 0, 3, &values[1])
    );
}

bool test_register_ramp(const char *name, int count, const float from[4], const float to[4])
{
    assert(count >= 2 && count <= SHADER_RAMP_MAX);
    behavior_asset *material = new_material(name);
    graph *g = material->graph;
    node *output = shader_graph_add_output(g, false);
    node *ramp = graph_add_node(g, pack_node("Shader Color Ramp"));
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    for (int i = 0; i < count; i++) {
        float t = (float)i / (count - 1);
        stops[i].position = t;
        for (int c = 0; c < 4; c++) {
            stops[i].color[c] = from[c] + (to[c] - from[c]) * t;
        }
    }
    pin_value factor = { .type = PIN_F32, .f = .25f };
    pin_value unlit = { .type = PIN_BOOL, .b = true };
    return publish(
        material,
        ramp && shader_node_ramp_set(g, ramp, stops, count) &&
            !graph_set_pin_default(g, ramp, 0, 0, &factor) &&
            !graph_connect_data(g, ramp, 0, output, 0) &&
            !graph_set_pin_default(g, output, 0, 6, &unlit)
    );
}
