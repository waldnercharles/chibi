#include "assets/model_export_artifacts.h"

#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <string.h>
#include <yyjson.h>

static model_bake_result bake;
static model_program program;
static model_export_artifacts first, second;
static yyjson_doc *json;
static render_mesh_asset decoded;
static void cleanup()
{
    yyjson_doc_free(json);
    json = NULL;
    model_export_artifacts_free(&first);
    model_export_artifacts_free(&second);
    model_bake_result_free(&bake);
    render_mesh_asset_free(&decoded);
    model_program_free(&program);
}
static void fixture()
{
    model_program_init(&program);
    bake.bounds = (model_bounds){ .max = { 1, 1, 0 } };
    bake.mesh.max = (CF_V3){ 1, 1, 0 };
    for (int i = 0; i < 4; i++) {
        render_mesh_surface_vertex v = { .position = { i == 1 || i == 2, i >= 2, 0 },
                                         .normal = { 0, 0, 1 },
                                         .tangent = { 1, 0, 0, 1 },
                                         .uv = { i == 1 || i == 2, i >= 2 } };
        apush(bake.mesh.vertices, v);
    }
    const int indices[] = { 2, 0, 1, 0, 2, 3 };
    for (int i = 0; i < 6; i++) {
        apush(bake.mesh.indices, indices[i]);
    }
    for (int i = 0; i < 2; i++) {
        render_mesh_group g = { .first = i * 3,
                                .count = 3,
                                .part = i,
                                .node = UINT64_MAX - i,
                                .asset = xstrdup("Assembly"),
                                .name = xstrdup(i ? "Back" : "Front"),
                                .path = xstrdup("instance/part"),
                                .material = xstrdup(i ? "" : "Metal\nBlue"),
                                .material_scope = xstrdup("instance") };
        apush(bake.mesh.groups, g);
    }
    model_parameter p = { .slot = 3,
                          .width = 2,
                          .node = UINT64_MAX,
                          .hidden = true,
                          .asset = xstrdup("Assembly"),
                          .scope = xstrdup("instance"),
                          .name = xstrdup("Size"),
                          .type_name = xstrdup("Vector2") };
    apush(program.parameters, p);
    bake.parameters[3][0] = .42f;
    bake.parameters[3][1] = 2;
    bake.samples = UINT64_C(9007199254740993);
    bake.work = UINT64_MAX;
    bake.cells[0] = 2;
    bake.cells[1] = 3;
    bake.cells[2] = 4;
    bake.cell_size = .25f;
    bake.cell_diagonal = .5f;
    bake.max_residual = .125f;
    bake.boundary_triangles = 1;
    shader_image_source image = { .key = "content/images/no-disk-read.png" };
    shader_graph_add_image(&program.shader, &image);
}
TEST_CASE(model_export_retained_bundle_preserves_indices_materials_and_exact_provenance)
{
    fixture();
    model_export_options options = { .bake = model_bake_defaults() };
    model_export_source source = { "Assembly", "models/Assembly.kdl", "hello" };
    char error[256];
    REQUIRE(
        model_export_artifacts_build("Assembly", &program, &bake, &options, &source, 1, 65536, &first, error, sizeof error)
    );
    REQUIRE(model_export_artifacts_build(
        "Assembly",
        &program,
        &bake,
        &options,
        &source,
        1,
        first.size,
        &second,
        error,
        sizeof error
    ));
    size_t total = 0;
    const char *names[] = { "model.mesh", "model.obj", "model.mtl", "provenance.json" };
    for (int i = 0; i < 4; i++) {
        REQUIRE(!strcmp(first.files[i].name, names[i]));
        REQUIRE(first.files[i].size == second.files[i].size);
        REQUIRE(
            !memcmp(first.files[i].bytes, second.files[i].bytes, first.files[i].size)
        );
        total += first.files[i].size;
    }
    REQUIRE(total == first.size);
    size_t remaining = 65536;
    REQUIRE(render_mesh_asset_prepared_read_budget(
        first.files[0].bytes,
        first.files[0].size,
        &remaining,
        &decoded,
        error,
        sizeof error
    ));
    REQUIRE(acount(decoded.vertices) == 4 && acount(decoded.indices) == 6);
    REQUIRE(!memcmp(decoded.indices, bake.mesh.indices, 6 * sizeof(int)));
    REQUIRE(decoded.groups[0].node == UINT64_MAX);
    REQUIRE(!strcmp(decoded.groups[0].material, "Metal\nBlue"));
    REQUIRE(!strcmp(decoded.groups[1].material_scope, "instance"));
    const char *obj = (const char *)first.files[1].bytes;
    REQUIRE(strstr(obj, "g part_0\nusemtl material_0\nf 3/3/3 1/1/1 2/2/2\n"));
    REQUIRE(strstr(obj, "g part_1\nusemtl material_1\nf 1/1/1 3/3/3 4/4/4\n"));
    const char *mtl = (const char *)first.files[2].bytes;
    REQUIRE(strstr(mtl, "# Live engine material reference: Metal Blue\nnewmtl material_0\n"));
    REQUIRE(strstr(mtl, "(inherits entity material)\nnewmtl material_1\n"));
    REQUIRE(strstr(mtl, "Kd 0.7 0.7 0.7") && !strstr(mtl, "map_Kd"));
    json = yyjson_read((const char *)first.files[3].bytes, first.files[3].size, 0);
    REQUIRE(json);
    yyjson_val *root = yyjson_doc_get_root(json);
    REQUIRE(!strcmp(
        yyjson_get_str(yyjson_arr_get(yyjson_obj_get(root, "image_dependencies"), 0)),
        "content/images/no-disk-read.png"
    ));
    REQUIRE(yyjson_get_real(yyjson_obj_get(root, "cell_size")) == .25);
    REQUIRE(yyjson_get_uint(yyjson_obj_get(root, "samples")) == bake.samples);
    REQUIRE(yyjson_get_uint(yyjson_obj_get(root, "work")) == UINT64_MAX);
    yyjson_val *row = yyjson_arr_get(yyjson_obj_get(root, "saved_catalog"), 0);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(row, "hash")), "a430d84680aabd0b"));
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(row, "path")), source.path));
    row = yyjson_arr_get(yyjson_obj_get(root, "parameters"), 0);
    REQUIRE(yyjson_get_uint(yyjson_obj_get(row, "node")) == UINT64_MAX);
    REQUIRE(yyjson_get_bool(yyjson_obj_get(row, "derived")));
    REQUIRE(yyjson_get_real(yyjson_arr_get(yyjson_obj_get(row, "value"), 0)) == (double).42f);
    row = yyjson_arr_get(yyjson_obj_get(root, "parts"), 0);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(row, "material")), "Metal\nBlue"));
    REQUIRE(yyjson_get_uint(yyjson_obj_get(row, "node")) == UINT64_MAX);
    /* Serialization owns every result byte after inputs are changed. */
    source.text = "changed";
    bake.mesh.indices[0] = 0;
    REQUIRE(strstr(obj, "f 3/3/3 1/1/1 2/2/2"));
    REQUIRE(yyjson_get_uint(yyjson_obj_get(row, "node")) == UINT64_MAX);
    return true;
}
TEST_CASE(model_export_refuses_budget_invalid_mesh_and_nonfinite_metadata_without_output)
{
    fixture();
    model_export_options options = { .bake = model_bake_defaults() };
    model_export_source source = { "Assembly", "models/Assembly.kdl", "hello" };
    char error[256];
    REQUIRE(
        model_export_artifacts_build("Assembly", &program, &bake, &options, &source, 1, 65536, &first, error, sizeof error)
    );
    REQUIRE(!model_export_artifacts_build(
        "Assembly",
        &program,
        &bake,
        &options,
        &source,
        1,
        first.size - 1,
        &second,
        error,
        sizeof error
    ));
    REQUIRE(!second.size && !second.files[0].bytes && error[0]);
    bake.mesh.indices[0] = 99;
    REQUIRE(
        !model_export_artifacts_build("Assembly", &program, &bake, &options, &source, 1, 65536, &second, error, sizeof error)
    );
    REQUIRE(!second.size && !second.files[0].bytes);
    bake.mesh.indices[0] = 2;
    program.parameters[0].slot = SHADER_PARAMETER_MAX;
    REQUIRE(
        !model_export_artifacts_build("Assembly", &program, &bake, &options, &source, 1, 65536, &second, error, sizeof error)
    );
    program.parameters[0].slot = 3;
    bake.parameters[3][0] = NAN;
    REQUIRE(
        !model_export_artifacts_build("Assembly", &program, &bake, &options, &source, 1, 65536, &second, error, sizeof error)
    );
    REQUIRE(!second.size && !second.files[0].bytes);
    return true;
}
void test_model_export_artifacts(void)
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(model_export_retained_bundle_preserves_indices_materials_and_exact_provenance);
    RUN_TEST_CASE(model_export_refuses_budget_invalid_mesh_and_nonfinite_metadata_without_output);
    pu_setup(NULL, NULL);
}
