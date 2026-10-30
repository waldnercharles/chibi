#include "render/mesh_surface.h"

#include "render/geometry_2d.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static bool close_value(float actual, float expected)
{
    return fabsf(actual - expected) < .001f;
}

/* Each supported primitive has whole triangles of unit normals. */
TEST_CASE(primitive_vertices_have_unit_normals)
{
    char error[256];
    render_mesh_surface_vertex *vertices = NULL;
    for (int primitive = PRIMITIVE_BOX; primitive < PRIMITIVE_COUNT; primitive++) {
        bool supported = primitive != PRIMITIVE_SPHERE &&
                         primitive != PRIMITIVE_CAPSULE &&
                         primitive != PRIMITIVE_CONE;
        REQUIRE(
            render_mesh_surface_vertices(primitive, &vertices, error, sizeof error) == supported
        );
        if (!supported) {
            continue;
        }
        REQUIRE(acount(vertices) > 0 && acount(vertices) % 3 == 0);
        for (int i = 0; i < acount(vertices); i++) {
            REQUIRE(close_value(cf_len_v3(vertices[i].normal), 1));
        }
    }
    afree(vertices);
    return true;
}

static void teardown()
{
    render_scene3d_parts_clear();
}

void test_mesh_surface()
{
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(primitive_vertices_have_unit_normals);
    pu_clear_setup();
}
