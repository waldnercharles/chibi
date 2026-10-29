#include "test/tools/graph_dispatch.h"

#include <string.h>

#ifdef CHIBI_TEST_RENDER_GEOMETRY
void test_appearance_geometry();
void test_mesh_surface();
void test_scene3d_post();
void test_scene3d_shapes();
#endif

bool test_graph_dispatch(const char *selector)
{
    (void)selector;
#ifdef CHIBI_TEST_RENDER_GEOMETRY
    bool render_geometry = !strcmp(selector, "--render-geometry");
    bool scene3d_post = !strcmp(selector, "--scene3d-post");
    bool scene3d_shapes = !strcmp(selector, "--scene3d-shapes");
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
    return false;
}
