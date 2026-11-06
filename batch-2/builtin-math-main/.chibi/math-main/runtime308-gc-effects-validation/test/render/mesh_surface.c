#include "render/mesh_surface.h"

#include "authoring/parts_prepare.h"
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

static void set_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *component,
    const void *value
)
{
    void *current = ecs_component_data(world, entity, component->id);
    if (current) {
        memcpy(current, value, component->element_size);
        return;
    }
    assert(ecs_world_record_add(world, entity, component->id, value, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
}

/* A box's surface takes its world bounds, banks with its visual roll, picks
 * its frontmost face, and joins compound parts. */
TEST_CASE(mesh_surfaces_bound_bank_pick_and_compound)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    Transform position = { 10, 20 };
    Geometry3D shape = { .primitive = PRIMITIVE_BOX,
                         .width = 8,
                         .height = 4,
                         .depth = 6,
                         .elevation = 7 };
    MeshSurface2D marker = { .enabled = true };
    set_component(world, entity, &COMPONENT_Transform, &position);
    set_component(world, entity, &COMPONENT_Geometry3D, &shape);
    set_component(world, entity, &COMPONENT_MeshSurface2D, &marker);
    render_mesh_surface surface = { 0 };
    char error[256];
    REQUIRE(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    REQUIRE(close_value(surface.min.x, 6));
    REQUIRE(close_value(surface.max.z, 23));
    float height;
    REQUIRE(render_mesh_surface_pick(&surface, position, &height));
    REQUIRE(close_value(height, 9)); /* Frontmost face is +Y, not the back face. */
    REQUIRE(!render_mesh_surface_pick(&surface, (CF_V2){ 15, 20 }, &height));
    CF_V2 min, max;
    REQUIRE(render_geometry_2d_mesh_bounds(world, entity, NULL, &min, &max, error, sizeof error));
    REQUIRE(close_value(min.y, 17));
    REQUIRE(close_value(max.x, 14));

    VisualTransform transform = { .scale = 2, .roll = 1.5707963268f };
    set_component(world, entity, &COMPONENT_VisualTransform, &transform);
    REQUIRE(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    REQUIRE(close_value(surface.max.y - surface.min.y, 12));
    REQUIRE(close_value(surface.max.z - surface.min.z, 8));
    REQUIRE(close_value(surface.max.x - surface.min.x, 16));
    REQUIRE(render_mesh_surface_pick(&surface, position, &height));
    REQUIRE(close_value(height, 13));
    CF_M4x4 model = render_scene3d_visual_model(&surface.parts[0].visual);
    CF_V3 normal = render_mesh_surface_normal(model, (CF_V3){ 0, 1, 0 });
    REQUIRE(close_value(normal.x, 0));
    REQUIRE(close_value(normal.y, 0));
    REQUIRE(close_value(normal.z, 1));

    MeshParts parts = { .parts = "(array)- {\n"
                                 "    - {\n"
                                 "        primitive 0\n"
                                 "        material \"Hull\"\n"
                                 "        (array)size 8 4 6\n"
                                 "    }\n"
                                 "    - {\n"
                                 "        primitive 0\n"
                                 "        material \"Engine\"\n"
                                 "        (array)size 2 2 2\n"
                                 "        (array)position 0 6 0\n"
                                 "    }\n"
                                 "}\n" };
    transform = (VisualTransform){ .scale = 1 };
    set_component(world, entity, &COMPONENT_VisualTransform, &transform);
    set_component(world, entity, &COMPONENT_MeshParts, &parts);
    REQUIRE(authoring_parts_prepare(parts.parts, error, sizeof error));
    REQUIRE(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    REQUIRE(acount(surface.parts) == 2);
    REQUIRE(!strcmp(surface.parts[1].visual.part.material, "Engine"));
    REQUIRE(render_mesh_surface_pick(&surface, position, &height));
    REQUIRE(close_value(height, 14));

    render_mesh_surface_free(&surface);
    ecs_world_free(world);
    return true;
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
    RUN_TEST_CASE(mesh_surfaces_bound_bank_pick_and_compound);
    RUN_TEST_CASE(primitive_vertices_have_unit_normals);
    pu_clear_setup();
}
