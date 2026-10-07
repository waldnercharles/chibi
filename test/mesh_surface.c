#include "render/mesh_surface.h"

#include "render/geometry_2d.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void close_value(float actual, float expected)
{
    assert(fabsf(actual - expected) < .001f);
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

void test_mesh_surface()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity entity = ecs_world_create_entity(world);
    GameTransform position = { 10, 20 };
    GameGeometry3D shape = { .primitive = GAME_PRIMITIVE_BOX,
                             .width = 8,
                             .height = 4,
                             .depth = 6,
                             .elevation = 7 };
    GameMeshSurface2D marker = { .enabled = true };
    set_component(world, entity, &GAME_COMPONENT_GameTransform, &position);
    set_component(world, entity, &GAME_COMPONENT_GameGeometry3D, &shape);
    set_component(world, entity, &GAME_COMPONENT_GameMeshSurface2D, &marker);
    render_mesh_surface surface = { 0 };
    char error[256];
    assert(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    close_value(surface.min.x, 6);
    close_value(surface.max.z, 23);
    float height;
    assert(render_mesh_surface_pick(&surface, position, &height));
    close_value(height, 9); /* Frontmost face is +Y, not the back face. */
    assert(!render_mesh_surface_pick(&surface, (CF_V2){ 15, 20 }, &height));
    CF_V2 min, max;
    assert(render_geometry_2d_mesh_bounds(world, entity, NULL, &min, &max, error, sizeof error));
    close_value(min.y, 17);
    close_value(max.x, 14);

    GameVisualTransform transform = { .scale = 2, .roll = 1.5707963268f };
    set_component(world, entity, &GAME_COMPONENT_GameVisualTransform, &transform);
    assert(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    close_value(surface.max.y - surface.min.y, 12);
    close_value(surface.max.z - surface.min.z, 8);
    close_value(surface.max.x - surface.min.x, 16);
    assert(render_mesh_surface_pick(&surface, position, &height));
    close_value(height, 13);
    CF_M4x4 model = render_arena_visual_model(&surface.parts[0].visual);
    CF_V3 normal = render_mesh_surface_normal(model, (CF_V3){ 0, 1, 0 });
    close_value(normal.x, 0);
    close_value(normal.y, 0);
    close_value(normal.z, 1);

    GameMeshParts parts = { .parts = "(array)- {\n"
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
    transform = (GameVisualTransform){ .scale = 1 };
    set_component(world, entity, &GAME_COMPONENT_GameVisualTransform, &transform);
    set_component(world, entity, &GAME_COMPONENT_GameMeshParts, &parts);
    assert(render_mesh_surface_build(world, entity, NULL, &surface, error, sizeof error));
    assert(acount(surface.parts) == 2);
    assert(!strcmp(surface.parts[1].visual.part.material, "Engine"));
    assert(render_mesh_surface_pick(&surface, position, &height));
    close_value(height, 14);

    render_mesh_surface_vertex *vertices = NULL;
    for (int primitive = GAME_PRIMITIVE_BOX; primitive < GAME_PRIMITIVE_COUNT;
         primitive++) {
        bool supported = primitive != GAME_PRIMITIVE_SPHERE &&
                         primitive != GAME_PRIMITIVE_CAPSULE &&
                         primitive != GAME_PRIMITIVE_CONE;
        assert(render_mesh_surface_vertices(primitive, &vertices, error, sizeof error) == supported);
        if (!supported) {
            continue;
        }
        assert(acount(vertices) > 0 && acount(vertices) % 3 == 0);
        for (int i = 0; i < acount(vertices); i++) {
            close_value(cf_len_v3(vertices[i].normal), 1);
        }
    }
    afree(vertices);
    render_mesh_surface_free(&surface);
    ecs_world_free(world);
    puts("mesh surface PASS: world bounds, banked normals, frontmost picking, compound parts");
}
