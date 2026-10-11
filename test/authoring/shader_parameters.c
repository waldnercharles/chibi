#include "authoring/shader_prepare.h"
#include "authoring/visual_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"
#include "render/shader_catalog.h"
#include "render/shader_publish.h"
#include "render/shader_values.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static behavior_asset *asset;
static game_player_asset *visual_assets;
static game_player_resource_host resource_host;
static void *resource_pending;
static unsigned char *payload;
static const registry_pack **previous_packs;
static bool registry_changed;
static void cleanup()
{
    if (resource_pending) {
        resource_host.discard(resource_host.context, resource_pending);
        resource_pending = NULL;
    }
    authoring_visual_prepared_free(visual_assets);
    visual_assets = NULL;
    free(payload);
    payload = NULL;
    behavior_asset_free(asset);
    asset = NULL;
    authoring_shader_catalog_clear();
    if (registry_changed) {
        registry_set_packs(previous_packs, acount(previous_packs));
        registry_changed = false;
    }
    afree(previous_packs);
}

TEST_CASE(shader_parameters_persist_and_reject_stale_generations)
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &SHADER_PACK };
    registry_set_packs(packs, 1);
    registry_changed = true;
    asset = behavior_asset_new("Preview parameters");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *output = shader_graph_add_output(g, false);
    const node_desc *parameter = registry_node_by_name("Shader Parameter");
    REQUIRE(parameter);
    node *color = graph_add_node(g, parameter);
    node *opacity = graph_add_node(g, parameter);
    REQUIRE(graph_set_node_reference(g, color, 0, "Tint") == 0);
    REQUIRE(graph_set_node_reference(g, opacity, 1, "Opacity") == 0);
    REQUIRE(graph_set_pin_type(g, color, 1, 0, PIN_STRUCT, "ShaderVec3") == 0);
    float tint[3] = { 1, 1, 1 };
    pin_value color_default = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = tint };
    REQUIRE(graph_set_pin_default(g, color, 1, 0, &color_default) == 0);
    pin_value opacity_default = { .type = PIN_F32, .f = 1 };
    REQUIRE(graph_set_pin_default(g, opacity, 1, 0, &opacity_default) == 0);
    REQUIRE(graph_connect_data(g, color, 0, output, 0) == 0);
    REQUIRE(graph_connect_data(g, opacity, 0, output, 5) == 0);
    shader_error error = { 0 };
    REQUIRE(render_shader_upsert(asset, &error));
    int shader = render_shader_find(asset->name);
    REQUIRE(shader);
    REQUIRE(render_shader_parameters_validate(
        shader,
        "(array)Tint 1 -2 0.5\n"
        "Opacity 4\n",
        &error
    ));
    const char *parameter_key = "(array)Tint 1 -2 0.5\nOpacity 4\n";
    float prepared[32][4];
    REQUIRE(shader_values_plain(shader, parameter_key, prepared, &error));
    int tint_slot = -1, opacity_slot = -1;
    for (int i = 0; i < 32; i++) {
        const char *name = render_shader_parameter_name(shader, i);
        if (name && !strcmp(name, "Tint")) {
            tint_slot = i;
        }
        if (name && !strcmp(name, "Opacity")) {
            opacity_slot = i;
        }
    }
    REQUIRE(tint_slot >= 0 && opacity_slot >= 0);
    REQUIRE(fabsf(prepared[tint_slot][1] - (-2)) < 0.0001f);
    REQUIRE(fabsf(prepared[opacity_slot][0] - (4)) < 0.0001f);
    size_t payload_size;
    payload = shader_values_save(&payload_size);
    REQUIRE(payload && payload_size);
    shader_values_clear();
    REQUIRE(!shader_values_plain(shader, parameter_key, prepared, &error));
    REQUIRE(shader_values_load(payload, payload_size, &error));
    REQUIRE(shader_values_plain(shader, parameter_key, prepared, &error));
    REQUIRE(fabsf(prepared[tint_slot][2] - 0.5f) < 0.0001f);
    payload[4] ^= 1;
    REQUIRE(!shader_values_load(payload, payload_size, &error));
    REQUIRE(shader_values_plain(shader, parameter_key, prepared, &error));
    payload[4] ^= 1;
    float changed_tint[3] = { 2, 2, 2 };
    color_default.ptr = changed_tint;
    REQUIRE(graph_set_pin_default(g, color, 1, 0, &color_default) == 0);
    REQUIRE(render_shader_upsert(asset, &error));
    REQUIRE(!shader_values_load(payload, payload_size, &error));
    free(payload);
    payload = NULL;
    REQUIRE(!render_shader_parameters_validate(shader, "(array)Tint 1\n", &error));
    REQUIRE(strstr(error.message, "array of 3"));
    REQUIRE(!render_shader_parameters_validate(shader, "(array)Opacity 4\n", &error));
    REQUIRE(strstr(error.message, "finite number or boolean"));
    REQUIRE(!render_shader_parameters_validate(shader, "Missing 1\n", &error));
    REQUIRE(strstr(error.message, "Unknown shader preview parameter"));
    return true;
}

TEST_CASE(model_parameters_match_reconstructed_prepared_generation)
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &MODEL_PACK };
    registry_set_packs(packs, 1);
    registry_changed = true;
    asset = behavior_asset_new("Prepared model parameters");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *radius = graph_add_node(g, &NODE_ModelParameter);
    REQUIRE(!graph_set_node_reference(g, radius, 0, "Radius"));
    const float values[] = { 2, 1, 4 };
    for (int i = 0; i < 3; i++) {
        pin_value value = { .type = PIN_F32, .f = values[i] };
        REQUIRE(!graph_set_pin_default(g, radius, false, i, &value));
    }
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    REQUIRE(!graph_connect_data(g, radius, 0, sphere, 0));
    REQUIRE(!graph_connect_data(g, sphere, 0, model_graph_add_output(g), 0));
    shader_error diagnostic = { 0 };
    const behavior_asset *models[] = { asset };
    REQUIRE(render_shader_models_upsert(models, 1, &diagnostic));
    int shader = render_shader_find(asset->name);
    REQUIRE(shader);
    REQUIRE(render_shader_parameters_validate(shader, "Radius 3\n", &diagnostic));
    char error[512];
    REQUIRE(authoring_visual_prepare(NULL, 0, &visual_assets, error, sizeof error));
    REQUIRE(acount(visual_assets) == 4);
    size_t bytes = 0;
    for (int i = 0; i < acount(visual_assets); i++) {
        bytes += visual_assets[i].length;
    }
    REQUIRE(bytes < 65536);
    resource_host = render_prepared_resources(NULL);
    size_t remaining = 2 * 1024 * 1024;
    resource_pending = resource_host.stage(
        resource_host.context,
        visual_assets,
        acount(visual_assets),
        &remaining,
        error,
        sizeof error
    );
    if (!resource_pending) {
        printf("Prepared model parameter stage: %s\n", error);
    }
    REQUIRE(resource_pending);
    REQUIRE(remaining < 2 * 1024 * 1024);
    return true;
}

type_catalog_pack test_shader_parameters_types()
{
    return (type_catalog_pack){ .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
}

void test_shader_parameters_prepared()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(shader_parameters_persist_and_reject_stale_generations);
    RUN_TEST_CASE(model_parameters_match_reconstructed_prepared_generation);
}
