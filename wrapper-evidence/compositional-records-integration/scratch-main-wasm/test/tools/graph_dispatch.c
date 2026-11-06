#include "test/tools/graph_dispatch.h"

#include <string.h>

#ifdef CHIBI_TEST_RENDER_GEOMETRY
void test_appearance_geometry();
void test_mesh_surface();
void test_scene3d_post();
void test_scene3d_shapes();
#endif
#ifdef CHIBI_TEST_MODEL_GRAPHS
void test_modeling();
void test_model_constructive();
void test_model_profiles();
void test_model_reuse();
void test_model_inspection();
void test_model_sampled();
#ifdef CHIBI_TEST_RENDER_RUNTIME
void test_model_parameters();
void test_model_material_scope();
void test_model_validation();
void test_model_materials();
void test_model_preview();
void test_model_surface();
void test_shader_detail();
void test_shader_effects_cpu();
void test_shader_image_cpu();
void test_shader_parity();
void test_shader_programs();
void test_material_cameras();
void test_mesh_materials();
void test_sdf_quality();
#endif
#endif

bool test_graph_dispatch(const char *selector)
{
    (void)selector;
#ifdef CHIBI_TEST_RENDER_GEOMETRY
    bool render_geometry = !strcmp(selector, "--render-geometry");
    bool scene3d_post = !strcmp(selector, "--scene3d-post");
    bool scene3d_shapes = !strcmp(selector, "--scene3d-shapes");
#endif
#ifdef CHIBI_TEST_MODEL_GRAPHS
    bool model_graphs = !strcmp(selector, "--model-graphs");
#ifdef CHIBI_TEST_RENDER_RUNTIME
    bool model_catalog = !strcmp(selector, "--model-catalog");
    bool shader_graphs = !strcmp(selector, "--shader-graphs");
    bool shader_parity = !strcmp(selector, "--shader-parity");
    bool shader_programs = !strcmp(selector, "--shader-programs");
    bool material_cameras = !strcmp(selector, "--material-cameras");
    bool mesh_materials = !strcmp(selector, "--mesh-materials");
    bool sdf_quality = !strcmp(selector, "--sdf-quality");
#endif
#endif
#ifdef CHIBI_TEST_RENDER_GEOMETRY
    if (render_geometry || scene3d_post || scene3d_shapes) {
        if (scene3d_post) {
            test_scene3d_post();
        } else if (scene3d_shapes) {
            test_scene3d_shapes();
        } else {
            test_appearance_geometry();
            test_mesh_surface();
        }
        return true;
    }
#endif
#ifdef CHIBI_TEST_MODEL_GRAPHS
    if (model_graphs) {
        test_modeling();
        test_model_constructive();
        test_model_profiles();
        test_model_reuse();
        test_model_inspection();
        test_model_sampled();
        return true;
    }
#ifdef CHIBI_TEST_RENDER_RUNTIME
    if (model_catalog) {
        test_model_parameters();
        test_model_material_scope();
        test_model_validation();
        test_model_materials();
        test_model_preview();
        test_model_surface();
        return true;
    }
    if (shader_graphs || shader_parity || shader_programs || material_cameras ||
        mesh_materials || sdf_quality) {
        if (material_cameras) {
            test_material_cameras();
        } else if (mesh_materials) {
            test_mesh_materials();
        } else if (sdf_quality) {
            test_sdf_quality();
        } else if (shader_parity) {
            test_shader_parity();
        } else if (shader_programs) {
            test_shader_programs();
        } else {
            test_shader_detail();
            test_shader_effects_cpu();
            test_shader_image_cpu();
        }
        return true;
    }
#endif
#endif
    return false;
}
