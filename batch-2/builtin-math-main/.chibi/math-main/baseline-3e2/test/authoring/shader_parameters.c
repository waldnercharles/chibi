#include "authoring/shader_prepare.h"
#include "authoring/visual_prepare.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/library.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/shader/shader_graph.h"
#include "render/scene3d_parts.h"
#include "render/shader_catalog.h"
#include "render/shader_catalog_records.h"
#include "render/shader_publish.h"
#include "render/shader_values.h"
#include "runtime/shaders/prepared.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static behavior_asset *asset;
static behavior_asset *helper, *linked_asset;
static player_asset *visual_assets;
static player_resource_host resource_host;
static void *resource_pending;
static unsigned char *payload;
static const registry_pack **previous_packs;
static bool registry_changed;
static shader_prepared_asset decoded;
static shader_graph lowered;
static char *generated;
static render_shader_parameter_cache *parameter_cache;
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
    free(generated);
    generated = NULL;
    shader_prepared_asset_free(&decoded);
    shader_graph_free(&lowered);
    render_shader_parameter_cache_free(parameter_cache);
    parameter_cache = NULL;
    behavior_asset_free(asset);
    asset = NULL;
    behavior_asset_free(helper);
    helper = NULL;
    behavior_asset_free(linked_asset);
    linked_asset = NULL;
    authoring_shader_catalog_clear();
    if (registry_changed) {
        registry_set_packs(previous_packs, acount(previous_packs));
        registry_changed = false;
    }
    afree(previous_packs);
}

static void shader_registry()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &CORE_FLOW_PACK, &SHADER_PACK };
    registry_set_packs(packs, 2);
    registry_changed = true;
}

/* A material whose Tint (white) colors it and whose Opacity (1) sets its
 * opacity. Returns the Tint node. */
static node *tint_material()
{
    shader_registry();
    asset = behavior_asset_new("Preview parameters");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *output = shader_graph_add_output(g, false);
    const node_desc *parameter = registry_node_by_name("Shader Parameter");
    assert(parameter);
    node *color = graph_add_node(g, parameter);
    node *opacity = graph_add_node(g, parameter);
    float tint[3] = { 1, 1, 1 };
    pin_value color_default = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = tint };
    pin_value opacity_default = { .type = PIN_F32, .f = 1 };
    bool built = !graph_set_node_reference(g, color, 0, "Tint") &&
                 !graph_set_node_reference(g, opacity, 1, "Opacity") &&
                 !graph_set_pin_type(g, color, 1, 0, PIN_STRUCT, "ShaderVec3") &&
                 !graph_set_pin_default(g, color, 1, 0, &color_default) &&
                 !graph_set_pin_default(g, opacity, 1, 0, &opacity_default) &&
                 !graph_connect_data(g, color, 0, output, 0) &&
                 !graph_connect_data(g, opacity, 0, output, 5);
    assert(built);
    (void)built;
    return color;
}

static int parameter_slot(int shader, const char *name)
{
    for (int i = 0; i < 32; i++) {
        const char *slot = render_shader_parameter_name(shader, i);
        if (slot && !strcmp(slot, name)) {
            return i;
        }
    }
    return -1;
}

TEST_CASE(shader_parameters_persist_and_reject_stale_generations)
{
    node *color = tint_material();
    graph *g = asset->graph;
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
    int tint_slot = parameter_slot(shader, "Tint"),
        opacity_slot = parameter_slot(shader, "Opacity");
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
    pin_value color_default = { .type = PIN_STRUCT,
                                .type_name = "ShaderVec3",
                                .ptr = changed_tint };
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

/* 3D solids and 2D sprites read a Shader component's parameters as their
 * material's own values, so preparing the component publishes those too. */
TEST_CASE(shader_bindings_prepare_their_material_values)
{
    tint_material();
    shader_error error = { 0 };
    REQUIRE(render_shader_upsert(asset, &error));
    int shader = render_shader_find(asset->name);
    REQUIRE(shader);
    Shader binding = { .material = asset->name,
                       .parameters = "(array)Tint 0.25 0.5 0.75\n" };
    REQUIRE(authoring_shader_binding_prepare(&binding, &error));
    float values[32][4];
    REQUIRE(shader_values_plain(shader, binding.parameters, values, &error));
    int tint = parameter_slot(shader, "Tint"),
        opacity = parameter_slot(shader, "Opacity");
    REQUIRE(tint >= 0 && opacity >= 0);
    REQUIRE(values[tint][0] == 0.25f && values[tint][1] == 0.5f && values[tint][2] == 0.75f);
    REQUIRE(values[opacity][0] == 1);
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

static bool particle_fixture()
{
    shader_registry();
    asset = behavior_asset_new("Particles");
    asset->domain = BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    graph *g = behavior_asset_add_function(asset, "Cloud", true);
    REQUIRE(g);
    node *entry = graph_node_by_id(g, g->entry_id);
    node *ret = graph_node_by_id(g, g->return_id);
    const char *names[] = { "Index", "Count", "Seed", "Radius", "Offset", "Tint" };
    for (int i = 0; i < 6; i++) {
        pin_spec pin = { .name = names[i],
                         .kind = PIN_DATA,
                         .type = i < 4 ? PIN_F32 : PIN_STRUCT,
                         .type_name = i < 4  ? "number"
                                    : i == 4 ? "ShaderVec3"
                                             : "ShaderVec4" };
        REQUIRE(graph_add_pin(g, entry, true, &pin) == i);
    }
    float offset[3] = { 4, 5, 6 }, tint[4] = { 0.2f, 0.4f, 0.8f, 0.5f };
    REQUIRE(
        !graph_set_pin_default(g, entry, true, 3, &(pin_value){ .type = PIN_F32, .f = 0.75f })
    );
    REQUIRE(!graph_set_pin_default(
        g,
        entry,
        true,
        4,
        &(pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = offset }
    ));
    REQUIRE(!graph_set_pin_default(
        g,
        entry,
        true,
        5,
        &(pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec4", .ptr = tint }
    ));
    pin_spec results[] = {
        { .name = "Position", .type = PIN_STRUCT, .type_name = "ShaderVec3", .kind = PIN_DATA },
        { .name = "Size", .type = PIN_F32, .type_name = "number", .kind = PIN_DATA },
        { .name = "Color", .type = PIN_STRUCT, .type_name = "ShaderVec4", .kind = PIN_DATA },
    };
    for (int i = 0; i < 3; i++) {
        REQUIRE(graph_add_pin(g, ret, false, &results[i]) == i);
    }
    node *position = graph_add_node(g, registry_node_by_name("Shader Vector3"));
    node *time = graph_add_node(g, registry_node_by_name("Shader Time"));
    node *size = graph_add_node(g, registry_node_by_name("Shader Multiply"));
    REQUIRE(position && time && size);
    REQUIRE(!graph_connect_data(g, entry, 0, position, 0));
    REQUIRE(!graph_connect_data(g, time, 0, position, 1));
    REQUIRE(!graph_connect_data(g, entry, 2, position, 2));
    REQUIRE(!graph_connect_data(g, position, 0, ret, 0));
    REQUIRE(!graph_connect_data(g, entry, 3, size, 0));
    REQUIRE(!graph_connect_data(g, entry, 1, size, 1));
    REQUIRE(!graph_connect_data(g, size, 0, ret, 1));
    REQUIRE(!graph_connect_data(g, entry, 5, ret, 2));
    return true;
}

TEST_CASE(particle_function_parameters_survive_preparation)
{
    REQUIRE(particle_fixture());
    graph *g = asset->graphs[1];
    shader_error error = { 0 };
    REQUIRE(shader_graph_from_function(g, &lowered, &error));
    REQUIRE(lowered.output_count == 3);
    REQUIRE(lowered.nodes[3].source_id == g->entry_id);
    REQUIRE(lowered.nodes[3].value[0] == 0.75f);
    REQUIRE(lowered.nodes[4].value[2] == 6);
    REQUIRE(render_shader_library_upsert(asset, &error));
    int id = render_shader_find("Particles.Cloud");
    REQUIRE(id && render_shader_is_particle(id));
    REQUIRE(!render_shader_is_shape(id));
    REQUIRE(render_shader_may_be_transparent(id));
    REQUIRE(!strcmp(render_shader_parameter_name(id, 3), "Radius"));
    REQUIRE(render_shader_parameter_width(id, 4) == 3);
    REQUIRE(render_shader_parameter_width(id, 5) == 4);
    generated = render_shader_particle_glsl(id, &error);
    REQUIRE(generated && strstr(generated, "void sg_particle("));
    free(generated);
    render_shader_set set = { 0 };
    generated = render_shader_glsl(&set, 1, 0);
    REQUIRE(generated && !strstr(generated, "void sg_asset_"));
    REQUIRE(!render_shader_set_add(&set, id, &error));
    const shader_asset *record = &shader_catalog_assets[id - 1];
    shader_prepared_asset prepared = { .name = record->name, .graph = record->graph };
    memcpy(prepared.names, record->names, sizeof prepared.names);
    memcpy(prepared.widths, record->widths, sizeof prepared.widths);
    memcpy(prepared.defaults, record->defaults, sizeof prepared.defaults);
    size_t length = 0;
    REQUIRE(shader_asset_prepared_write(&prepared, &payload, &length, &error));
    size_t budget = 64 * 1024 * 1024;
    REQUIRE(shader_asset_prepared_read_budget(payload, length, &budget, &decoded, &error));
    REQUIRE(!strcmp(decoded.name, "Particles.Cloud"));
    REQUIRE(!strcmp(decoded.names[4], "Offset"));
    REQUIRE(decoded.widths[4] == 3 && decoded.widths[5] == 4);
    REQUIRE(decoded.defaults[3][0] == 0.75f && decoded.defaults[4][2] == 6);
    REQUIRE(decoded.graph.nodes[3].source_id == g->entry_id);
    REQUIRE(
        decoded.graph.nodes[decoded.graph.output[0]].source_id ==
        lowered.nodes[lowered.output[0]].source_id
    );
    float parameters[32][4], result[SHADER_OUTPUT_MAX][4];
    memcpy(parameters, decoded.defaults, sizeof parameters);
    parameters[0][0] = 2;
    parameters[1][0] = 8;
    parameters[2][0] = 7;
    REQUIRE(shader_graph_eval_outputs(
        &decoded.graph,
        &(shader_sample_context){ .time = 3 },
        parameters,
        result,
        &error
    ));
    REQUIRE(result[0][0] == 2 && result[0][1] == 3 && result[0][2] == 7);
    REQUIRE(result[1][0] == 6 && fabsf(result[2][2] - 0.8f) < 0.0001f);
    Shader binding = { .material = "Particles.Cloud",
                       .parameters = "Radius 2\n(array)Tint 1 0 0 0.5\n" };
    const behavior_asset *sources[] = { asset };
    REQUIRE(
        render_shader_binding_sources_validate(&binding, binding.parameters, sources, 1, &error)
    );
    REQUIRE(authoring_shader_binding_prepare(&binding, &error));
    REQUIRE(render_shader_material_parameters_scoped_cached(
        id,
        NULL,
        (ecs_entity){ 0 },
        binding.parameters,
        "",
        parameters,
        &parameter_cache,
        &error
    ));
    REQUIRE(parameters[3][0] == 2 && parameters[5][0] == 1);
    REQUIRE(
        !render_shader_binding_sources_validate(&binding, "(array)Tint 1 2 3\n", sources, 1, &error)
    );
    REQUIRE(strstr(error.message, "4 finite"));
    REQUIRE(
        !render_shader_binding_sources_validate(&binding, "Missing 2\n", sources, 1, &error)
    );
    REQUIRE(strstr(error.message, "Unknown"));
    char *index_name = decoded.names[0];
    decoded.names[0] = "Wrong";
    unsigned char *invalid = NULL;
    size_t invalid_length = 0;
    bool accepted = shader_asset_prepared_write(&decoded, &invalid, &invalid_length, &error);
    decoded.names[0] = index_name;
    free(invalid);
    REQUIRE(!accepted);
    return true;
}

TEST_CASE(particle_function_stages_and_rejects_mesh_material_use)
{
    REQUIRE(particle_fixture());
    shader_error diagnostic = { 0 };
    REQUIRE(render_shader_library_upsert(asset, &diagnostic));
    char error[256] = { 0 };
    REQUIRE(authoring_visual_prepare(NULL, 0, &visual_assets, error, sizeof error));
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
    REQUIRE(resource_pending);
    resource_host.discard(resource_host.context, resource_pending);
    resource_pending = NULL;
    authoring_visual_prepared_free(visual_assets);
    visual_assets = NULL;

    scene3d_parts parts = { .count = 1 };
    parts.parts[0] = (scene3d_part){ .primitive = PRIMITIVE_BOX,
                                     .dimensions = { 1, 1, 1 },
                                     .material = "Particles.Cloud" };
    REQUIRE(render_scene3d_parts_publish("particle-as-material", &parts, error, sizeof error));
    REQUIRE(authoring_visual_prepare(NULL, 0, &visual_assets, error, sizeof error));
    remaining = 2 * 1024 * 1024;
    resource_pending = resource_host.stage(
        resource_host.context,
        visual_assets,
        acount(visual_assets),
        &remaining,
        error,
        sizeof error
    );
    REQUIRE(!resource_pending);
    REQUIRE(strstr(error, "material dependency"));
    return true;
}

TEST_CASE(particle_function_signature_and_context_are_checked)
{
    REQUIRE(particle_fixture());
    graph *g = asset->graphs[1];
    node *entry = graph_node_by_id(g, g->entry_id);
    node *ret = graph_node_by_id(g, g->return_id);
    shader_error error = { 0 };
    REQUIRE(render_shader_library_upsert(asset, &error));
    int id = render_shader_find("Particles.Cloud");
    unsigned revision = render_shader_revision();
    REQUIRE(!graph_set_pin_type(g, entry, true, 0, PIN_I32, "int"));
    REQUIRE(!render_shader_library_upsert(asset, &error));
    REQUIRE(strstr(error.message, "number/vector"));
    REQUIRE(render_shader_revision() == revision && render_shader_is_particle(id));
    REQUIRE(!graph_set_pin_type(g, entry, true, 0, PIN_F32, "number"));
    const shader_graph *applied = &shader_catalog_assets[id - 1].graph;
    node *position = graph_node_by_id(g, applied->nodes[applied->output[0]].source_id);
    REQUIRE(!graph_connect_data(g, entry, 0, position, 0));
    const char *name = entry->output_pins[0].name;
    entry->output_pins[0].name = "Wrong";
    REQUIRE(!render_shader_library_upsert(asset, &error));
    REQUIRE(strstr(error.message, "Index:number"));
    entry->output_pins[0].name = name;
    for (int i = entry->output_count; i <= SHADER_PARAMETER_MAX; i++) {
        char extra[32];
        snprintf(extra, sizeof extra, "Extra%d", i);
        REQUIRE(
            graph_add_pin(g, entry, true, &(pin_spec){ .name = extra, .type = PIN_F32, .kind = PIN_DATA }) == i
        );
    }
    REQUIRE(!render_shader_library_upsert(asset, &error));
    REQUIRE(strstr(error.message, "3..32"));
    while (entry->output_count > 6) {
        REQUIRE(!graph_remove_pin(g, entry, true, entry->output_count - 1));
    }
    REQUIRE(!graph_set_pin_type(g, ret, false, 2, PIN_STRUCT, "ShaderVec3"));
    REQUIRE(!render_shader_library_upsert(asset, &error));
    REQUIRE(strstr(error.message, "Color:ShaderVec4"));
    REQUIRE(!graph_set_pin_type(g, ret, false, 2, PIN_STRUCT, "ShaderVec4"));
    REQUIRE(!graph_connect_data(g, entry, 5, ret, 2));
    node *world = graph_add_node(g, registry_node_by_name("Shader WorldPosition"));
    REQUIRE(world);
    REQUIRE(!graph_connect_data(g, world, 0, ret, 0));
    REQUIRE(!render_shader_library_upsert(asset, &error));
    REQUIRE(error.node == world->id && strstr(error.message, "WorldPosition"));
    REQUIRE(render_shader_revision() == revision && render_shader_is_particle(id));
    /* Removing the complete particle signature makes it an ordinary helper and
     * retires its old catalog record rather than keeping a stale program. */
    ret->input_pins[2].name = "Other";
    REQUIRE(render_shader_library_upsert(asset, &error));
    REQUIRE(!render_shader_find("Particles.Cloud"));
    int vector = shader_graph_add(&lowered, SHADER_CONSTANT, 3);
    int scalar = shader_graph_add(&lowered, SHADER_CONSTANT, 1);
    int color = shader_graph_add(&lowered, SHADER_CONSTANT, 4);
    int bump = shader_graph_add(&lowered, SHADER_BUMP, 3);
    lowered.nodes[scalar].value[0] = 1;
    for (int i = 0; i < 3; i++) {
        lowered.nodes[bump].input[i] = scalar;
    }
    lowered.nodes[bump].input[3] = vector;
    lowered.nodes[bump].source_id = 99;
    lowered.output_count = 3;
    lowered.output[0] = bump;
    lowered.output[1] = scalar;
    lowered.output[2] = color;
    REQUIRE(shader_graph_validate(&lowered, &error));
    REQUIRE(!shader_graph_particle_validate(&lowered, &error));
    REQUIRE(error.node == 99 && strstr(error.message, "Bump"));
    return true;
}

TEST_CASE(particle_function_links_library_helpers)
{
    REQUIRE(particle_fixture());
    helper = behavior_asset_new("ParticleMath");
    helper->domain = BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    graph *function = behavior_asset_add_function(helper, "Identity", true);
    node *entry = graph_node_by_id(function, function->entry_id);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec value = { .name = "Value", .type = PIN_F32, .kind = PIN_DATA };
    REQUIRE(graph_add_pin(function, entry, true, &value) == 0);
    REQUIRE(graph_add_pin(function, ret, false, &value) == 0);
    REQUIRE(!graph_connect_data(function, entry, 0, ret, 0));
    graph *particle = asset->graphs[1];
    node *call = graph_add_node(particle, &NODE_CallPureFunction);
    REQUIRE(!graph_library_refresh_call(particle, call, function));
    REQUIRE(!graph_set_node_reference_asset(particle, call, helper->name));
    REQUIRE(
        !graph_connect_data(particle, graph_node_by_id(particle, particle->entry_id), 3, call, 0)
    );
    REQUIRE(!graph_connect_data(
        particle,
        call,
        0,
        graph_node_by_id(particle, particle->return_id),
        1
    ));
    const behavior_asset *catalog[] = { asset, helper };
    shader_error error = { 0 };
    Shader binding = { .material = "Particles.Cloud" };
    REQUIRE(render_shader_binding_sources_validate(&binding, "Radius 2\n", catalog, 2, &error));
    REQUIRE(
        !render_shader_binding_sources_validate(&binding, "Radius 2\n", catalog, 1, &error)
    );
    REQUIRE(strstr(error.message, "missing function library"));
    linked_asset = behavior_asset_link_libraries(
        asset,
        catalog,
        2,
        error.message,
        sizeof error.message
    );
    REQUIRE(linked_asset);
    REQUIRE(render_shader_library_upsert(linked_asset, &error));
    REQUIRE(render_shader_library_upsert(helper, &error));
    REQUIRE(render_shader_find("Particles.Cloud"));
    REQUIRE(!render_shader_find("Particles.Identity"));
    REQUIRE(!render_shader_find("ParticleMath.Identity"));
    float parameters[32][4] = { 0 }, result[SHADER_OUTPUT_MAX][4];
    parameters[3][0] = 2;
    REQUIRE(render_shader_sample(
        render_shader_find("Particles.Cloud"),
        &(shader_sample_context){ 0 },
        parameters,
        result,
        &error
    ));
    REQUIRE(result[1][0] == 2);
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
    RUN_TEST_CASE(shader_bindings_prepare_their_material_values);
    RUN_TEST_CASE(particle_function_parameters_survive_preparation);
    RUN_TEST_CASE(particle_function_stages_and_rejects_mesh_material_use);
    RUN_TEST_CASE(particle_function_signature_and_context_are_checked);
    RUN_TEST_CASE(particle_function_links_library_helpers);
    RUN_TEST_CASE(model_parameters_match_reconstructed_prepared_generation);
}
