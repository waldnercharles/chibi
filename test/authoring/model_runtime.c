#include "graph/authoring/graph_edit.h"
#include "graph/modeling/model_compile.h"
#include "runtime/geometry/mesh_prepared.h"
#include "runtime/models/model_program.h"
#include "runtime/models/prepared.h"
#include "runtime/shaders/prepared.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/value_wire.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Three tiny real compiler preparations. Runtime assertions happen after the
 * authored graphs retire; no files, world, renderer, services or process work. */
static behavior_asset *source;
static model_program program;
static shader_graph distance, hit;
static model_material_bindings bindings;
static unsigned char *wire, *copy;
static size_t wire_length, copy_length;
static shader_prepared_asset asset, decoded;
static render_mesh_asset mesh, decoded_mesh;
static render_mesh_table *prior_meshes, *staged_meshes, *replacement_meshes;
static void cleanup()
{
    behavior_asset_free(source);
    source = NULL;
    model_program_free(&program);
    shader_graph_free(&distance);
    shader_graph_free(&hit);
    model_material_bindings_free(&bindings);
    shader_prepared_asset_free(&asset);
    shader_prepared_asset_free(&decoded);
    free(wire);
    free(copy);
    wire = copy = NULL;
    render_mesh_asset_free(&mesh);
    render_mesh_asset_free(&decoded_mesh);
    if (prior_meshes) {
        render_mesh_table_exchange(prior_meshes);
    }
    render_mesh_table_free(prior_meshes);
    render_mesh_table_free(staged_meshes);
    render_mesh_table_free(replacement_meshes);
    prior_meshes = staged_meshes = replacement_meshes = NULL;
}
static void setup()
{
    model_program_init(&program);
    shader_graph_init(&distance);
    shader_graph_init(&hit);
    model_material_bindings_init(&bindings);
    shader_prepared_asset_init(&asset);
    shader_prepared_asset_init(&decoded);
}
static bool scalar(node *n, int pin, float f)
{
    pin_value value = { .type = PIN_F32, .f = f };
    return !graph_set_pin_default(source->graph, n, false, pin, &value);
}
static bool vector(node *n, int pin, float x, float y, float z)
{
    float v[3] = { x, y, z };
    pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = v };
    return !graph_set_pin_default(source->graph, n, false, pin, &value);
}
static bool connect(node *from, node *to, int pin)
{
    return !graph_connect_data(source->graph, from, 0, to, pin);
}
static bool near(float a, float b)
{
    return fabsf(a - b) < .0001f;
}
/* Decode only after both graph and compiled producer storage have retired. */
static bool model_roundtrip()
{
    shader_error error = { 0 };
    REQUIRE(model_program_prepared_write(&program, &wire, &wire_length, &error));
    REQUIRE(wire_length > 16 && wire_length < 65536);
    REQUIRE(model_program_prepared_write(&program, &copy, &copy_length, &error));
    REQUIRE(copy_length == wire_length && !memcmp(copy, wire, wire_length));
    model_program_free(&program);
    REQUIRE(model_program_prepared_read(wire, wire_length, &program, &error));
    shader_node *nodes = program.shader.nodes;
    size_t budget = 65536;
    wire[8] = 2; /* Retired model format embeds the old shader opcode numbering. */
    REQUIRE(!model_program_prepared_read_budget(wire, wire_length, &budget, &program, &error));
    REQUIRE(program.shader.nodes == nodes && budget == 65536 && error.message[0]);
    wire[8] = (unsigned char)MODEL_PREPARED_VERSION;
    REQUIRE(!model_program_prepared_read_budget(wire, wire_length - 1, &budget, &program, &error));
    REQUIRE(program.shader.nodes == nodes && budget == 65536);
    copy = xrealloc(copy, copy_length + 1);
    copy[copy_length] = 0;
    REQUIRE(!model_program_prepared_read_budget(copy, copy_length + 1, &budget, &program, &error));
    REQUIRE(program.shader.nodes == nodes && budget == 65536);
    budget = 1;
    REQUIRE(!model_program_prepared_read_budget(wire, wire_length, &budget, &program, &error));
    REQUIRE(program.shader.nodes == nodes && budget == 1);
    free(wire);
    free(copy);
    wire = copy = NULL;
    return true;
}
TEST_CASE(model_runtime_retains_numeric_parts_and_shader_snapshots)
{
    source = behavior_asset_new("NumericModel");
    source->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = source->graph;
    node *radius = graph_add_node(g, &NODE_ModelParameter);
    REQUIRE(!graph_set_node_reference(g, radius, 0, "Radius"));
    REQUIRE(scalar(radius, 0, 2) && scalar(radius, 1, 1) && scalar(radius, 2, 4));
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    node *part = graph_add_node(g, &NODE_ModelPart);
    node *translate = graph_add_node(g, &NODE_ModelTranslate);
    REQUIRE(!graph_set_node_reference(g, part, 0, "Surface"));
    REQUIRE(vector(translate, 1, 5, 0, 0));
    REQUIRE(connect(radius, sphere, 0) && connect(sphere, part, 0));
    REQUIRE(connect(part, translate, 0) && connect(translate, model_graph_add_output(g), 0));
    shader_error error = { 0 };
    REQUIRE(model_compile(source, &program, &error));
    REQUIRE(acount(program.parts) == 1);
    int slot = -1;
    for (int i = 0; i < acount(program.parameters); i++) {
        if (!program.parameters[i].hidden &&
            program.parameters[i].node == radius->id) {
            slot = program.parameters[i].slot;
        }
    }
    REQUIRE(slot >= 0);
    behavior_asset_free(source);
    source = NULL;
    REQUIRE(model_roundtrip());
    REQUIRE(!strcmp(program.parts[0].name, "Surface"));
    float incoming[SHADER_PARAMETER_MAX][4], prepared[SHADER_PARAMETER_MAX][4];
    model_bounds bounds;
    REQUIRE(model_program_parameters(&program, incoming, &error));
    incoming[slot][0] = 3;
    REQUIRE(model_program_prepare(&program, incoming, prepared, &bounds, &error));
    REQUIRE(near(bounds.min[0], 2) && near(bounds.max[0], 8));
    REQUIRE(near(bounds.min[1], -3) && near(bounds.max[2], 3));
    model_bounds selected;
    REQUIRE(model_program_part_bounds(&program, prepared, 0, &selected, &error));
    REQUIRE(near(selected.min[0], 2) && near(selected.max[0], 8));
    float point[3] = { 5, 0, 0 };
    model_hit owner;
    REQUIRE(model_program_hit(&program, prepared, point, &owner, &error));
    REQUIRE(owner.part == 0 && near(owner.position[0], 0));
    REQUIRE(near(owner.part_from_object[0][0], 1) && near(owner.part_from_object[0][1], 0));
    float preserved[SHADER_PARAMETER_MAX][4];
    memcpy(preserved, prepared, sizeof preserved);
    model_bounds before = bounds;
    incoming[slot][0] = 5;
    REQUIRE(!model_program_prepare(&program, incoming, prepared, &bounds, &error));
    REQUIRE(
        !memcmp(preserved, prepared, sizeof preserved) &&
        !memcmp(&before, &bounds, sizeof bounds)
    );
    REQUIRE(model_program_part_shaders(&program, 0, &distance, &hit, &error));
    shader_node *old_nodes = distance.nodes;
    REQUIRE(!model_program_part_shaders(&program, 0, &distance, &distance, &error));
    REQUIRE(distance.nodes == old_nodes);
    model_program_free(&program);
    shader_sample_context context = { .object_position = { 5, 0, 0 } };
    float values[SHADER_OUTPUT_MAX][4];
    REQUIRE(shader_graph_eval_outputs(&distance, &context, preserved, values, &error));
    REQUIRE(near(values[0][0], -3));
    return true;
}
TEST_CASE(model_runtime_owns_structural_profile_and_path_points)
{
    source = behavior_asset_new("StructuralModel");
    source->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = source->graph;
    node *polygon = graph_add_node(g, &NODE_ModelProfilePolygon);
    node *extrude = graph_add_node(g, &NODE_ModelExtrude);
    node *shell = graph_add_node(g, &NODE_ModelShell);
    node *tube = graph_add_node(g, &NODE_ModelTubeSweep);
    node *join = graph_add_node(g, &NODE_ModelUnion);
    float triangle[3][2] = { { -2, -1 }, { 2, -1 }, { 0, 3 } };
    pin_value vertices[3];
    for (int i = 0; i < 3; i++) {
        vertices[i] = (pin_value){ .type = PIN_STRUCT,
                                   .type_name = "ShaderVec2",
                                   .ptr = triangle[i] };
    }
    pin_array profile = { .element_type = PIN_STRUCT,
                          .type_name = "ShaderVec2",
                          .items = vertices,
                          .count = 3,
                          .capacity = 3 };
    pin_value input = { .type = PIN_STRUCT,
                        .type_name = "ShaderVec2",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = &profile };
    REQUIRE(!graph_set_pin_default(g, polygon, false, 0, &input));
    float path[2][3] = { { -4, -1, 0 }, { -4, 1, 0 } };
    pin_value points[2];
    for (int i = 0; i < 2; i++) {
        points[i] = (pin_value){ .type = PIN_STRUCT,
                                 .type_name = "ShaderVec3",
                                 .ptr = path[i] };
    }
    pin_array line = { .element_type = PIN_STRUCT,
                       .type_name = "ShaderVec3",
                       .items = points,
                       .count = 2,
                       .capacity = 2 };
    input = (pin_value){ .type = PIN_STRUCT,
                         .type_name = "ShaderVec3",
                         .container = PIN_CONTAINER_ARRAY,
                         .ptr = &line };
    REQUIRE(!graph_set_pin_default(g, tube, false, 0, &input));
    REQUIRE(scalar(extrude, 1, 2) && scalar(shell, 1, .5f) && scalar(tube, 1, .25f));
    REQUIRE(connect(polygon, extrude, 0) && connect(extrude, shell, 0));
    REQUIRE(connect(shell, join, 0) && connect(tube, join, 1));
    REQUIRE(connect(join, model_graph_add_output(g), 0));
    shader_error error = { 0 };
    REQUIRE(model_compile(source, &program, &error));
    memset(triangle, 0, sizeof triangle);
    memset(path, 0, sizeof path);
    behavior_asset_free(source);
    source = NULL;
    REQUIRE(model_roundtrip());
    model_bounds bounds;
    REQUIRE(model_program_bounds(&program, NULL, &bounds, &error));
    REQUIRE(near(bounds.min[0], -4.25f) && near(bounds.max[0], 2.25f));
    REQUIRE(near(bounds.min[1], -1.25f) && near(bounds.max[1], 3.25f));
    REQUIRE(near(bounds.min[2], -2.25f) && near(bounds.max[2], 2.25f));
    shader_sample_context context = { .object_position = { -4, 0, 0 } };
    float values[SHADER_OUTPUT_MAX][4];
    REQUIRE(shader_graph_eval_outputs(&program.shader, &context, NULL, values, &error));
    REQUIRE(near(values[0][0], -.25f));
    return true;
}
TEST_CASE(model_resource_changes_require_geometry_preparation)
{
    source = behavior_asset_new("SampledModel");
    source->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = source->graph;
    node *sampled = graph_add_node(g, &NODE_ModelSampledSdf);
    pin_value key = { .type = PIN_STR,
                      .text = &(
                          const pin_string
                      ){ .bytes = "content/sdf/0000000000000000.sdf.png",
                         .length = sizeof("content/sdf/0000000000000000.sdf.png") - 1 } };
    REQUIRE(!graph_set_pin_default(g, sampled, false, 0, &key));
    REQUIRE(connect(sampled, model_graph_add_output(g), 0));
    shader_error error = { 0 };
    REQUIRE(model_compile(source, &program, &error));
    REQUIRE(
        model_program_compile_bindings(&program, source, NULL, 0, &bindings, &error) == MODEL_BINDINGS_READY
    );
    key.text = &(const pin_string){
        .bytes = "content/sdf/1111111111111111.sdf.png",
        .length = (uint32_t)strlen("content/sdf/1111111111111111.sdf.png")
    };
    REQUIRE(!graph_set_pin_default(g, sampled, false, 0, &key));
    REQUIRE(
        model_program_compile_bindings(&program, source, NULL, 0, &bindings, &error) ==
        MODEL_BINDINGS_GEOMETRY_CHANGED
    );
    behavior_asset_free(source);
    source = NULL;
    model_bounds bounds;
    REQUIRE(model_program_bounds(&program, NULL, &bounds, &error));
    REQUIRE(near(bounds.min[0], -1) && near(bounds.max[2], 1));
    return true;
}
static bool resident_image(void *userdata, const shader_image_source *image, shader_image_view *view)
{
    int *calls = userdata;
    if (strcmp(image->key, "textures/owned.png") ||
        image->color_space != SHADER_IMAGE_LINEAR ||
        image->filter != SHADER_IMAGE_NEAREST || image->wrap != SHADER_IMAGE_CLAMP) {
        return false;
    }
    static const unsigned char pixels[4] = { 255, 0, 255, 255 };
    (*calls)++;
    *view = (shader_image_view){ .pixels = pixels, .width = 1, .height = 1 };
    return true;
}
TEST_CASE(shader_persistence_owns_uniforms_ramps_and_resident_resource_keys)
{
    asset.name = xstrdup("OwnedShader");
    asset.names[0] = xstrdup("Mix");
    asset.widths[0] = 1;
    asset.defaults[0][0] = .25f;
    asset.bounds[0] = 2;
    asset.bounds[1] = 3;
    asset.shape = true;
    shader_graph *g = &asset.graph;
    int factor = shader_graph_add(g, SHADER_PARAMETER, 1);
    g->nodes[factor].parameter = 0;
    int mode = shader_graph_add(g, SHADER_CONSTANT, 1);
    g->nodes[mode].value[0] = 0; /* Linear interpolation. */
    int ramp = shader_graph_add(g, SHADER_COLOR_RAMP, 3);
    g->nodes[ramp].input[0] = factor;
    g->nodes[ramp].input[1] = mode;
    g->nodes[ramp].source_id = 71;
    apush(g->nodes[ramp].ramp, ((shader_ramp_stop){ .position = 0, .color = { 1, 0, 0, 1 } }));
    apush(g->nodes[ramp].ramp, ((shader_ramp_stop){ .position = 1, .color = { 0, 0, 1, 1 } }));
    int uv = shader_graph_add(g, SHADER_CONSTANT, 2);
    int image = shader_graph_add(g, SHADER_IMAGE_TEXTURE, 3);
    char key[] = "textures/owned.png";
    shader_image_source resource = { .key = key,
                                     .color_space = SHADER_IMAGE_LINEAR,
                                     .filter = SHADER_IMAGE_NEAREST,
                                     .wrap = SHADER_IMAGE_CLAMP };
    g->nodes[image].image = shader_graph_add_image(g, &resource);
    g->nodes[image].input[0] = uv;
    g->output[0] = ramp;
    g->output[1] = image;
    g->output_count = 2;
    memset(key, 'x', sizeof key - 1);
    shader_error error = { 0 };
    REQUIRE(shader_asset_prepared_write(&asset, &wire, &wire_length, &error));
    REQUIRE(wire_length > 16 && wire_length < 8192);
    REQUIRE(shader_asset_prepared_write(&asset, &copy, &copy_length, &error));
    REQUIRE(copy_length == wire_length && !memcmp(copy, wire, wire_length));
    shader_prepared_asset_free(&asset);
    size_t budget = 65536;
    REQUIRE(shader_asset_prepared_read_budget(wire, wire_length, &budget, &decoded, &error));
    REQUIRE(budget < 65536 && !strcmp(decoded.name, "OwnedShader"));
    REQUIRE(!strcmp(decoded.names[0], "Mix") && decoded.widths[0] == 1);
    REQUIRE(decoded.shape && near(decoded.bounds[0], 2) && near(decoded.bounds[1], 3));
    REQUIRE(
        decoded.graph.nodes[ramp].source_id == 71 &&
        acount(decoded.graph.nodes[ramp].ramp) == 2
    );
    shader_node *nodes = decoded.graph.nodes;
    budget = 65536;
    wire[8] = 3; /* Retired shader format includes the removed beat operations. */
    REQUIRE(!shader_asset_prepared_read_budget(wire, wire_length, &budget, &decoded, &error));
    REQUIRE(decoded.graph.nodes == nodes && budget == 65536 && error.message[0]);
    wire[8] = (unsigned char)SHADER_PREPARED_VERSION;
    REQUIRE(!shader_asset_prepared_read_budget(wire, wire_length - 1, &budget, &decoded, &error));
    REQUIRE(decoded.graph.nodes == nodes && budget == 65536);
    copy = xrealloc(copy, copy_length + 1);
    copy[copy_length] = 0;
    REQUIRE(!shader_asset_prepared_read_budget(copy, copy_length + 1, &budget, &decoded, &error));
    REQUIRE(decoded.graph.nodes == nodes && budget == 65536);
    budget = 1;
    REQUIRE(!shader_asset_prepared_read_budget(wire, wire_length, &budget, &decoded, &error));
    REQUIRE(decoded.graph.nodes == nodes && budget == 1);
    /* Standalone IR codec also owns its stops/keys after the asset retires. */
    free(wire);
    wire = NULL;
    REQUIRE(shader_graph_prepared_write(&decoded.graph, &wire, &wire_length, &error));
    float parameters[SHADER_PARAMETER_MAX][4];
    memcpy(parameters, decoded.defaults, sizeof parameters);
    shader_prepared_asset_free(&decoded);
    REQUIRE(shader_graph_prepared_read(wire, wire_length, &distance, &error));
    free(wire);
    wire = NULL;
    shader_sample_context context = { 0 };
    float values[SHADER_OUTPUT_MAX][4];
    REQUIRE(!shader_graph_eval_outputs(&distance, &context, parameters, values, &error));
    int calls = 0;
    context.resolve_image = resident_image;
    context.image_userdata = &calls;
    REQUIRE(shader_graph_eval_outputs(&distance, &context, parameters, values, &error));
    REQUIRE(calls == 1 && near(values[0][0], .75f) && near(values[0][2], .25f));
    REQUIRE(near(values[1][0], 1) && near(values[1][1], 0) && near(values[1][2], 1));
    return true;
}
/* Locate a serialized group through the owning field-wise primitives, never
 * through native struct layout. This fixture has exactly three vertices. */
static bool corrupt_mesh_group()
{
    execution_wire reader = { .data = copy, .size = copy_length, .reading = true };
    uint64_t header;
    bool flag;
    float value;
    int count, first;
    execution_wire_u64(&reader, &header);
    execution_wire_u64(&reader, &header);
    execution_wire_bool(&reader, &flag);
    execution_wire_bool(&reader, &flag);
    for (int i = 0; i < 12; i++) {
        execution_wire_float(&reader, &value);
    }
    execution_wire_count(&reader, &count, 1);
    REQUIRE(!reader.error && count == 3);
    for (int i = 0; i < 72; i++) {
        execution_wire_float(&reader, &value);
    }
    execution_wire_count(&reader, &count, 1);
    REQUIRE(!reader.error && count == 3);
    for (int i = 0; i < 3; i++) {
        execution_wire_int(&reader, &first);
    }
    execution_wire_count(&reader, &count, 1);
    execution_wire_int(&reader, &first);
    REQUIRE(!reader.error && count == 1);
    size_t at = reader.offset;
    execution_wire_int(&reader, &count);
    REQUIRE(!reader.error && first == 0 && count == 3);
    execution_wire writer = { 0 };
    count = 4; /* Invalid: a material group must contain whole triangles. */
    execution_wire_int(&writer, &count);
    bool encoded = !writer.error && writer.size == reader.offset - at;
    if (encoded) {
        memcpy(copy + at, writer.data, writer.size);
    }
    afree(writer.data);
    REQUIRE(encoded);
    return true;
}
TEST_CASE(shader_prepared_preserves_time_math_and_rejects_retired_opcodes)
{
    shader_graph *g = &asset.graph;
    int time = shader_graph_add(g, SHADER_TIME, 1);
    int floor = shader_graph_add(g, SHADER_FLOOR, 1);
    g->nodes[floor].input[0] = time;
    g->output[0] = floor;
    g->output_count = 1;
    shader_error error = { 0 };
    REQUIRE(shader_graph_prepared_write(g, &wire, &wire_length, &error));
    size_t budget = 65536;
    REQUIRE(shader_graph_prepared_read_budget(wire, wire_length, &budget, &distance, &error));
    float parameters[SHADER_PARAMETER_MAX][4] = { 0 };
    float values[SHADER_OUTPUT_MAX][4];
    shader_sample_context sample = { .time = 2.75f };
    REQUIRE(shader_graph_eval_outputs(&distance, &sample, parameters, values, &error));
    REQUIRE(values[0][0] == 2);
    sample.time = -0.25f;
    REQUIRE(shader_graph_eval_outputs(&distance, &sample, parameters, values, &error));
    REQUIRE(values[0][0] == -1);
    shader_node *nodes = distance.nodes;
    budget = 65536;
    wire[8] = 3; /* Old numeric opcodes must never decode as their new neighbors. */
    REQUIRE(!shader_graph_prepared_read_budget(wire, wire_length, &budget, &distance, &error));
    REQUIRE(distance.nodes == nodes && budget == 65536);
    REQUIRE(strstr(error.message, "Unknown prepared shader format"));
    REQUIRE(shader_graph_eval_outputs(&distance, &sample, parameters, values, &error));
    REQUIRE(values[0][0] == -1);
    return true;
}
TEST_CASE(mesh_persistence_transfers_detached_generations)
{
    mesh.has_part_coordinates = mesh.has_uv_bounds = true;
    mesh.max = mesh.uv_max = (CF_V3){ 1, 1, 1 };
    for (int i = 0; i < 3; i++) {
        render_mesh_surface_vertex vertex = {
            .normal = { 0, 0, 1 },
            .tangent = { 1, 0, 0, 1 },
            .part_from_object = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }
        };
        vertex.position.x = i == 1;
        vertex.position.y = i == 2;
        vertex.uv = (CF_V2){ vertex.position.x, vertex.position.y };
        vertex.part_position = vertex.position;
        apush(mesh.vertices, vertex);
        apush(mesh.indices, i);
    }
    render_mesh_group group = { .count = 3,
                                .part = 0,
                                .node = 42,
                                .asset = xstrdup("Triangle"),
                                .name = xstrdup("Face"),
                                .path = xstrdup("Triangle/Face"),
                                .material = xstrdup("OldMaterial"),
                                .material_scope = xstrdup("materials") };
    apush(mesh.groups, group);
    mesh.identity = 999; /* Resident identity is not portable metadata. */
    char error[128] = { 0 };
    REQUIRE(render_mesh_asset_prepared_write(&mesh, &wire, &wire_length, error, sizeof error));
    REQUIRE(wire_length > 16 && wire_length < 4096);
    mesh.identity = 1000;
    REQUIRE(render_mesh_asset_prepared_write(&mesh, &copy, &copy_length, error, sizeof error));
    REQUIRE(copy_length == wire_length && !memcmp(copy, wire, wire_length));
    render_mesh_asset_free(&mesh);
    size_t budget = 8192;
    REQUIRE(
        render_mesh_asset_prepared_read_budget(wire, wire_length, &budget, &decoded_mesh, error, sizeof error)
    );
    REQUIRE(budget < 8192 && !decoded_mesh.identity && acount(decoded_mesh.vertices) == 3);
    REQUIRE(
        decoded_mesh.has_uv_bounds && decoded_mesh.has_part_coordinates &&
        acount(decoded_mesh.groups) == 1
    );
    REQUIRE(
        near(decoded_mesh.vertices[1].uv.x, 1) &&
        near(decoded_mesh.vertices[2].part_position.y, 1)
    );
    REQUIRE(near(decoded_mesh.vertices[1].part_from_object[0][0], 1));
    REQUIRE(acount(decoded_mesh.indices) == 3 && decoded_mesh.indices[2] == 2);
    REQUIRE(
        near(decoded_mesh.vertices[2].tangent.x, 1) &&
        near(decoded_mesh.vertices[2].tangent.w, 1)
    );
    REQUIRE(
        !strcmp(decoded_mesh.groups[0].material, "OldMaterial") &&
        decoded_mesh.groups[0].node == 42
    );
    render_mesh_surface_vertex *vertices = decoded_mesh.vertices;
    budget = 8192;
    REQUIRE(corrupt_mesh_group());
    REQUIRE(
        !render_mesh_asset_prepared_read_budget(copy, copy_length, &budget, &decoded_mesh, error, sizeof error)
    );
    REQUIRE(decoded_mesh.vertices == vertices && budget == 8192 && error[0]);
    REQUIRE(
        !render_mesh_asset_prepared_read_budget(wire, wire_length - 1, &budget, &decoded_mesh, error, sizeof error)
    );
    REQUIRE(decoded_mesh.vertices == vertices && budget == 8192);
    memcpy(copy, wire, wire_length);
    copy = xrealloc(copy, copy_length + 1);
    copy[copy_length] = 0;
    REQUIRE(
        !render_mesh_asset_prepared_read_budget(copy, copy_length + 1, &budget, &decoded_mesh, error, sizeof error)
    );
    REQUIRE(decoded_mesh.vertices == vertices && budget == 8192);
    budget = 1;
    REQUIRE(
        !render_mesh_asset_prepared_read_budget(wire, wire_length, &budget, &decoded_mesh, error, sizeof error)
    );
    REQUIRE(decoded_mesh.vertices == vertices && budget == 1);
    prior_meshes = render_mesh_table_new();
    render_mesh_table_exchange(prior_meshes);
    staged_meshes = render_mesh_table_new();
    REQUIRE(!render_mesh_table_add_owned(staged_meshes, "../invalid", &decoded_mesh));
    REQUIRE(
        decoded_mesh.vertices == vertices &&
        render_mesh_table_source_count(staged_meshes) == 0
    );
    char path[] = "content/meshes/triangle.mesh";
    REQUIRE(render_mesh_table_add_owned(staged_meshes, path, &decoded_mesh));
    REQUIRE(!decoded_mesh.vertices && render_mesh_table_source_count(staged_meshes) == 1);
    memset(path, 'x', sizeof path - 1);
    uint64_t generation = render_mesh_assets_generation();
    render_mesh_table_exchange(staged_meshes);
    REQUIRE(render_mesh_assets_generation() != generation);
    const render_mesh_asset
        *resident = render_mesh_asset_load("content/meshes/triangle.mesh", error, sizeof error);
    REQUIRE(resident && resident->identity && !strcmp(resident->groups[0].name, "Face"));
    uint64_t old_identity = resident->identity;
    budget = 8192;
    REQUIRE(
        render_mesh_asset_prepared_read_budget(wire, wire_length, &budget, &decoded_mesh, error, sizeof error)
    );
    free(wire);
    wire = NULL;
    free(decoded_mesh.groups[0].material);
    decoded_mesh.groups[0].material = xstrdup("NewMaterial");
    replacement_meshes = render_mesh_table_new();
    REQUIRE(
        render_mesh_table_add_owned(replacement_meshes, "content/meshes/triangle.mesh", &decoded_mesh)
    );
    render_mesh_table_exchange(replacement_meshes);
    resident = render_mesh_asset_load("content/meshes/triangle.mesh", error, sizeof error);
    REQUIRE(
        resident && resident->identity != old_identity &&
        !strcmp(resident->groups[0].material, "NewMaterial")
    );
    REQUIRE(render_mesh_table_source_count(replacement_meshes) == 1);
    REQUIRE(!strcmp(render_mesh_table_source_at(replacement_meshes, 0), "content/meshes/triangle.mesh"));
    const render_mesh_asset *old = render_mesh_table_asset_at(replacement_meshes, 0);
    REQUIRE(old->identity == old_identity && !strcmp(old->groups[0].material, "OldMaterial"));
    REQUIRE(
        near(old->vertices[2].uv.y, 1) &&
        near(old->vertices[2].part_from_object[2][2], 1)
    );
    return true;
}
void test_model_runtime()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(model_runtime_retains_numeric_parts_and_shader_snapshots);
    RUN_TEST_CASE(model_runtime_owns_structural_profile_and_path_points);
    RUN_TEST_CASE(model_resource_changes_require_geometry_preparation);
    RUN_TEST_CASE(shader_persistence_owns_uniforms_ramps_and_resident_resource_keys);
    RUN_TEST_CASE(shader_prepared_preserves_time_math_and_rejects_retired_opcodes);
    RUN_TEST_CASE(mesh_persistence_transfers_detached_generations);
    pu_clear_setup();
}
