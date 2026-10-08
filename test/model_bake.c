#include "runtime/models/model_bake.h"

#include "authoring/mesh_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static void value(graph *g, node *n, int input, float number)
{
    pin_value v = { .type = PIN_F32, .f = number };
    assert(!graph_set_pin_default(g, n, false, input, &v));
}
static node *sphere(graph *g, float radius)
{
    node *n = graph_add_node(g, &NODE_ModelSphere);
    value(g, n, 0, radius);
    return n;
}
static void compile(behavior_asset *asset, model_program *program)
{
    shader_error error = { 0 };
    model_program_init(program);
    assert(model_compile(asset, program, &error));
}
static void check_mesh(const model_bake_result *bake, bool hollow)
{
    assert(acount(bake->mesh.vertices) > 0 && acount(bake->mesh.vertices) % 3 == 0);
    int inner = 0, outer = 0;
    for (int i = 0; i < acount(bake->mesh.vertices); i++) {
        const render_mesh_surface_vertex *v = &bake->mesh.vertices[i];
        float r = cf_len_v3(v->position);
        assert(fabsf(cf_len_v3(v->normal) - 1) < 1e-4f);
        if (hollow && r < .8f) {
            assert(fabsf(r - .55f) < 1e-4f);
            assert(cf_dot_v3(v->position, v->normal) < -.5f);
            inner++;
        } else {
            assert(fabsf(r - 1) < 1e-4f);
            assert(cf_dot_v3(v->position, v->normal) > .9f);
            outer++;
        }
    }
    assert(outer && (!hollow || inner));
    for (int i = 0; i < acount(bake->mesh.vertices); i += 3) {
        const render_mesh_surface_vertex *v = &bake->mesh.vertices[i];
        CF_V3 cross = cf_cross_v3(
            cf_sub_v3(v[1].position, v[0].position),
            cf_sub_v3(v[2].position, v[0].position)
        );
        assert(cf_len_v3(cross) > RENDER_MESH_MIN_TRIANGLE_AREA);
        assert(
            cf_dot_v3(cross, cf_add_v3(cf_add_v3(v[0].normal, v[1].normal), v[2].normal)) > 0
        );
    }
}
static void check_importable(const render_mesh_asset *mesh)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    yyjson_mut_val *vertices = yyjson_mut_obj_add_arr(doc, root, "vertices");
    for (int i = 0; i < acount(mesh->vertices); i++) {
        const render_mesh_surface_vertex *v = &mesh->vertices[i];
        double values[] = { v->position.x, v->position.y, v->position.z,
                            v->normal.x,   v->normal.y,   v->normal.z,
                            v->uv.x,       v->uv.y };
        yyjson_mut_arr_add_val(vertices, yyjson_mut_arr_with_real(doc, values, 8));
    }
    char *text = yyjson_mut_write(doc, 0, NULL);
    render_mesh_asset parsed = { 0 };
    char error[256];
    assert(render_mesh_asset_parse(text, &parsed, error, sizeof error));
    assert(acount(parsed.vertices) == acount(mesh->vertices));
    render_mesh_asset_free(&parsed);
    free(text);
    yyjson_mut_doc_free(doc);
}

void test_model_bake()
{
    shader_error error = { 0 };
    behavior_asset *asset = behavior_asset_new("BakeSphere");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    assert(!graph_connect_data(g, sphere(g, 1), 0, model_graph_add_output(g), 0));
    model_program program;
    compile(asset, &program);
    model_bake_options options = model_bake_defaults();
    options.cell_size = .2f;
    model_bake_result a = { 0 }, b = { 0 };
    assert(model_bake(&program, NULL, &options, &a, &error));
    check_mesh(&a, false);
    assert(model_bake(&program, NULL, &options, &b, &error));
    assert(acount(a.mesh.vertices) == acount(b.mesh.vertices));
    assert(!memcmp(
        a.mesh.vertices,
        b.mesh.vertices,
        (size_t)acount(a.mesh.vertices) * sizeof *a.mesh.vertices
    ));
    assert(a.samples == b.samples && a.work == b.work && a.max_residual == b.max_residual);
    render_mesh_surface_vertex *retained = a.mesh.vertices;
    options.max_triangles = 1;
    assert(!model_bake(&program, NULL, &options, &a, &error));
    assert(a.mesh.vertices == retained && strstr(error.message, "triangle"));
    options = model_bake_defaults();
    options.cell_size = .001f;
    assert(!model_bake(&program, NULL, &options, &a, &error));
    assert(a.mesh.vertices == retained && strstr(error.message, "axis"));
    options = model_bake_defaults();
    options.max_samples = 1;
    assert(!model_bake(&program, NULL, &options, &a, &error));
    assert(a.mesh.vertices == retained && strstr(error.message, "budget"));
    model_bake_result_free(&a);
    model_bake_result_free(&b);
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = behavior_asset_new("BakeCavity");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    g = asset->graph;
    node *difference = graph_add_node(g, &NODE_ModelDifference);
    assert(!graph_connect_data(g, sphere(g, 1), 0, difference, 0));
    assert(!graph_connect_data(g, sphere(g, .55f), 0, difference, 1));
    assert(!graph_connect_data(g, difference, 0, model_graph_add_output(g), 0));
    compile(asset, &program);
    options = model_bake_defaults();
    options.cell_size = .2f;
    assert(model_bake(&program, NULL, &options, &a, &error));
    check_mesh(&a, true);
    model_bake_result_free(&a);
    model_program_free(&program);
    behavior_asset_free(asset);

    /* A box close to grid-aligned corners must not export microscopic cuts
       that the actual float-position mesh importer rejects. */
    asset = behavior_asset_new("BakeAlignedBox");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    g = asset->graph;
    node *box = graph_add_node(g, &NODE_ModelBox);
    float half_size[3] = { .99999994f, 1.000000119f, .80000001f };
    pin_value dimensions = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = half_size };
    assert(!graph_set_pin_default(g, box, false, 0, &dimensions));
    node *offset_box = graph_add_node(g, &NODE_ModelTranslate);
    float center[3] = { 3, 2.75f, -5.02f };
    pin_value translation = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = center };
    assert(!graph_set_pin_default(g, offset_box, false, 1, &translation));
    assert(!graph_connect_data(g, box, 0, offset_box, 0));
    assert(!graph_connect_data(g, offset_box, 0, model_graph_add_output(g), 0));
    compile(asset, &program);
    options = model_bake_defaults();
    options.cell_size = .4f;
    assert(model_bake(&program, NULL, &options, &a, &error));
    assert(acount(a.mesh.vertices));
    for (int i = 0; i < acount(a.mesh.vertices); i += 3) {
        const render_mesh_surface_vertex *v = &a.mesh.vertices[i];
        float area = cf_len_v3(cf_cross_v3(
            cf_sub_v3(v[1].position, v[0].position),
            cf_sub_v3(v[2].position, v[0].position)
        ));
        assert(area > RENDER_MESH_MIN_TRIANGLE_AREA);
    }
    check_importable(&a.mesh);
    model_bake_result_free(&a);
    model_program_free(&program);
    behavior_asset_free(asset);

    asset = behavior_asset_new("BakeParts");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    g = asset->graph;
    node *joined = graph_add_node(g, &NODE_ModelUnion);
    for (int i = 0; i < 2; i++) {
        node *part = graph_add_node(g, &NODE_ModelPart);
        assert(!graph_set_node_reference(g, part, 0, i ? "Right" : "Left"));
        assert(!graph_connect_data(g, sphere(g, .5f), 0, part, 0));
        node *translated = graph_add_node(g, &NODE_ModelTranslate);
        float coordinates[3] = { i ? 1 : -1, 0, 0 };
        pin_value offset = { .type = PIN_STRUCT,
                             .type_name = "ShaderVec3",
                             .ptr = coordinates };
        assert(!graph_set_pin_default(g, translated, false, 1, &offset));
        assert(!graph_connect_data(g, part, 0, translated, 0));
        assert(!graph_connect_data(g, translated, 0, joined, i));
    }
    assert(!graph_connect_data(g, joined, 0, model_graph_add_output(g), 0));
    compile(asset, &program);
    options.cell_size = .2f;
    assert(model_bake(&program, NULL, &options, &a, &error));
    assert(acount(a.mesh.groups) == 2 && a.mesh.has_part_coordinates);
    for (int i = 0; i < acount(a.mesh.groups); i++) {
        const render_mesh_group *group = &a.mesh.groups[i];
        const model_part *part = &program.parts[group->part];
        assert(group->node == part->node && !strcmp(group->path, part->path));
        float center = !strcmp(group->name, "Left") ? -1 : 1;
        for (int j = group->first; j < group->first + group->count; j++) {
            const render_mesh_surface_vertex *v = &a.mesh.vertices[j];
            assert(fabsf(v->part_position.x - (v->position.x - center)) < 1e-5f);
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    assert(fabsf(v->part_from_object[r][c] - (r == c)) < 1e-5f);
                }
            }
        }
    }
    model_bake_result_free(&a);
    model_program_free(&program);
    behavior_asset_free(asset);
}
