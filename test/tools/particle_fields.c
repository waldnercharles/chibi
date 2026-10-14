#include "render/scene3d_particles.h"
#include "render/shader_catalog_records.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <string.h>

static ecs_world *world;
static scene3d_particles *fields;

static void cleanup()
{
    if (fields) {
        render_scene3d_particles_free(fields);
    }
    fields = NULL;
    ecs_world_free(world);
    world = NULL;
    render_shader_catalog_clear();
}

static void particle_program()
{
    shader_asset asset = { .name = xstrdup("Budget.Point") };
    const char *names[] = { "Index", "Count", "Seed" };
    for (int i = 0; i < 3; i++) {
        int node = shader_graph_add(&asset.graph, SHADER_PARAMETER, 1);
        asset.graph.nodes[node].parameter = i;
        asset.names[i] = xstrdup(names[i]);
        asset.widths[i] = 1;
    }
    const int widths[] = { 3, 1, 4 };
    for (int i = 0; i < 3; i++) {
        int node = shader_graph_add(&asset.graph, SHADER_CONSTANT, widths[i]);
        asset.graph.nodes[node].value[0] = 1;
        asset.graph.output[i] = node;
    }
    asset.graph.output_count = 3;
    apush(shader_catalog_assets, asset);
}

static bool add_field(ecs_entity *entity)
{
    *entity = ecs_world_create_entity(world);
    ParticleField3D field = { .seed = 1, .time = 1, .additive = true };
    Geometry3D geometry = { .primitive = PRIMITIVE_OCTAHEDRON,
                            .width = 1,
                            .height = 1,
                            .depth = 1 };
    Shader shader = { .material = "Budget.Point" };
    Transform3D transform = { .scale = { 1, 1, 1 } };
    REQUIRE(
        ecs_world_record_add(world, *entity, COMPONENT_ParticleField3D.id, &field, 0) == ECS_OK
    );
    REQUIRE(
        ecs_world_record_add(world, *entity, COMPONENT_Geometry3D.id, &geometry, 0) == ECS_OK
    );
    REQUIRE(ecs_world_record_add(world, *entity, COMPONENT_Shader.id, &shader, 0) == ECS_OK);
    REQUIRE(
        ecs_world_record_add(world, *entity, COMPONENT_Transform3D.id, &transform, 0) == ECS_OK
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    return true;
}

TEST_CASE(particle_source_budget_reclaims_destroyed_fields)
{
    world = ecs_world_new();
    const ecs_component_desc *components[] = { &COMPONENT_ParticleField3D,
                                               &COMPONENT_Geometry3D,
                                               &COMPONENT_Shader,
                                               &COMPONENT_Transform3D };
    for (int i = 0; i < 4; i++) {
        REQUIRE(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    particle_program();
    REQUIRE(render_shader_is_particle(render_shader_find("Budget.Point")));
    fields = render_scene3d_particles_new();
    ecs_entity entities[SCENE3D_PARTICLE_FIELD_SOURCES + 1];
    for (int i = 0; i < SCENE3D_PARTICLE_FIELD_SOURCES; i++) {
        REQUIRE(add_field(&entities[i]));
    }
    char error[256] = { 0 };
    /* Zero-count fields exercise ownership and budgets without a GPU or engine. */
    REQUIRE(
        render_scene3d_particles_prepare(fields, world, entities, SCENE3D_PARTICLE_FIELD_SOURCES, NULL, error, sizeof error)
    );
    REQUIRE(render_scene3d_particles_count(fields) == 0);
    REQUIRE(render_scene3d_particles_draws(fields) == 0);
    REQUIRE(add_field(&entities[SCENE3D_PARTICLE_FIELD_SOURCES]));
    REQUIRE(!render_scene3d_particles_prepare(
        fields,
        world,
        entities,
        SCENE3D_PARTICLE_FIELD_SOURCES + 1,
        NULL,
        error,
        sizeof error
    ));
    REQUIRE(strstr(error, "128-source"));
    REQUIRE(ecs_world_destroy_entity(world, entities[0]) == ECS_OK);
    entities[0] = entities[SCENE3D_PARTICLE_FIELD_SOURCES];
    REQUIRE(
        render_scene3d_particles_prepare(fields, world, entities, SCENE3D_PARTICLE_FIELD_SOURCES, NULL, error, sizeof error)
    );
    REQUIRE(render_scene3d_particles_count(fields) == 0);
    REQUIRE(render_scene3d_particles_draws(fields) == 0);
    return true;
}

void test_particle_fields()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(particle_source_budget_reclaims_destroyed_fields);
    pu_clear_setup();
}
