#include "render/scene3d_volume.h"

#include "runtime/component_catalog.h"
#include "test/render/hidden_app.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

TEST_CASE(default_density_allocates_no_graphics_work)
{
    Environment e = environment_defaults();
    REQUIRE(e.fog_density == 0);
    char error[256] = { 0 };
    render_scene3d_volume *volume = render_scene3d_volume_new();
    REQUIRE(render_scene3d_volume_prepare(volume, &e, 0, 0, error, sizeof error));
    REQUIRE(!render_scene3d_volume_active(volume));
    /* Default worlds may call the draw hook without graphics or lights. */
    REQUIRE(render_scene3d_volume_draw(volume, NULL, NULL, error, sizeof error));
    render_scene3d_volume_free(volume);
    return true;
}

TEST_CASE(enabled_medium_validates_physical_interval)
{
    Environment e = environment_defaults();
    e.fog_density = .0015f;
    e.fog_start = 600;
    e.fog_distance = 1200;
    char error[256] = { 0 };
    REQUIRE(render_scene3d_volume_valid(&e, error, sizeof error));
    e.fog_distance = 600;
    REQUIRE(!render_scene3d_volume_valid(&e, error, sizeof error));
    REQUIRE(strstr(error, "distance"));
    e.fog_distance = 1200;
    e.fog_start = -1;
    REQUIRE(!render_scene3d_volume_valid(&e, error, sizeof error));
    e.fog_start = 0;
    e.fog_density = NAN;
    REQUIRE(!render_scene3d_volume_valid(&e, error, sizeof error));
    e.fog_density = -1;
    REQUIRE(!render_scene3d_volume_valid(&e, error, sizeof error));
    return true;
}

TEST_CASE(volume_quality_and_phase_are_bounded)
{
    Environment e = environment_defaults();
    e.fog_density = .002f;
    char error[256] = { 0 };
    e.fog_steps = 4;
    e.fog_resolution = .25f;
    e.fog_anisotropy = -.9f;
    REQUIRE(render_scene3d_volume_valid(&e, error, sizeof error));
    e.fog_steps = 64;
    e.fog_resolution = 1;
    e.fog_anisotropy = .9f;
    REQUIRE(render_scene3d_volume_valid(&e, error, sizeof error));
    e.fog_steps = 65;
    REQUIRE(!render_scene3d_volume_valid(&e, error, sizeof error));
    e.fog_steps = 32;
    e.fog_resolution = .1f;
    REQUIRE(!render_scene3d_volume_valid(&e, error, sizeof error));
    e.fog_resolution = .5f;
    e.fog_anisotropy = 1;
    REQUIRE(!render_scene3d_volume_valid(&e, error, sizeof error));
    return true;
}

void test_scene3d_volume()
{
    RUN_TEST_CASE(default_density_allocates_no_graphics_work);
    RUN_TEST_CASE(enabled_medium_validates_physical_interval);
    RUN_TEST_CASE(volume_quality_and_phase_are_bounded);
}

enum
{
    SIDE = 64
};
static ecs_world *gpu_world;
static particles *gpu_particles;
static render_scene3d *gpu_scene;
static CF_Canvas gpu_canvas;
static render_scene3d_camera gpu_camera;

static void gpu_setup()
{
    gpu_world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(gpu_world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    gpu_particles = particles_new();
    gpu_scene = render_scene3d_new();
    render_scene3d_set_size(gpu_scene, SIDE, SIDE);
    CF_CanvasParams p = cf_canvas_defaults(SIDE, SIDE);
    p.target.pixel_format = CF_PIXEL_FORMAT_R16G16B16A16_FLOAT;
    p.depth_stencil_enable = true;
    gpu_canvas = cf_make_canvas(p);
    cf_canvas_set_clear_color(gpu_canvas, (CF_Color){ 0, 0, 0, 1 });
    gpu_camera = (render_scene3d_camera){
        .eye = { 0, 20, 0 },
        .view = cf_look_at((CF_V3){ 0, 20, 0 }, (CF_V3){ 0 }, (CF_V3){ 0, 0, -1 }),
        .projection = cf_ortho(-4, 4, -4, 4, .1f, 60),
        .orthographic = true,
    };
}

static void gpu_teardown()
{
    render_scene3d_free(gpu_scene);
    particles_free(gpu_particles);
    ecs_world_free(gpu_world);
    cf_destroy_canvas(gpu_canvas);
}

static ecs_entity gpu_add(ecs_component_id component, const void *data)
{
    ecs_entity entity = ecs_world_create_entity(gpu_world);
    assert(ecs_world_record_add(gpu_world, entity, component, data, 0) == ECS_OK);
    assert(ecs_world_apply_commands(gpu_world) == ECS_OK);
    return entity;
}

static bool gpu_render(const Environment *e, float *center)
{
    cf_app_update(NULL);
    if (!render_scene3d_draw(gpu_scene, gpu_world, gpu_particles, &gpu_camera, 0, NULL, e, (render_skip){ 0 })) {
        fprintf(stderr, "scene3d-volume: %s\n", render_scene3d_error(gpu_scene));
        return false;
    }
    cf_render_to(gpu_canvas, true);
    cf_app_draw_onto_screen(false);
    uint16_t pixels[SIDE * SIDE * 4];
    if (!test_read_canvas(gpu_canvas, pixels, sizeof pixels)) {
        return false;
    }
    float sum = 0;
    for (int y = SIDE / 2 - 2; y < SIDE / 2 + 2; y++) {
        for (int x = SIDE / 2 - 2; x < SIDE / 2 + 2; x++) {
            float r = test_half(pixels[4 * (y * SIDE + x)]);
            if (!isfinite(r)) {
                return false;
            }
            sum += r;
        }
    }
    *center = sum / 16;
    return true;
}

/* Five 64x64 frames. A real box blocks a spot's 3D shadow map while leaving
 * the camera's center ray clear. Disabling that shadow restores the shaft.
 * A second opaque slab then verifies depth-limited Beer-Lambert extinction
 * without any light or scattering source. */
TEST_CASE(integrated_volume_uses_mesh_shadows_and_opaque_depth)
{
    Environment e = environment_defaults();
    e.ambient_intensity = 0;
    e.ambient_sky = e.ambient_ground = 0x000000ffu;
    e.occlusion = OCCLUSION_OFF;
    e.global_illumination = GLOBAL_ILLUMINATION_OFF;
    e.fog_density = .1f;
    e.fog_color = 0xffffffffu;
    e.fog_start = 15;
    e.fog_distance = 30;
    e.fog_steps = 16;
    e.fog_resolution = .5f;
    SpotLight spot = spot_light_defaults();
    spot.elevation = 0;
    spot.direction = (CF_V3){ 1, 0, 0 };
    spot.intensity = 40;
    spot.radius = 30;
    spot.inner_angle = 45;
    spot.outer_angle = 60;
    spot.shadows = true;
    ecs_entity lamp = gpu_add(COMPONENT_Transform.id, &(Transform){ -6, 0 });
    REQUIRE(ecs_world_record_add(gpu_world, lamp, COMPONENT_SpotLight.id, &spot, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(gpu_world) == ECS_OK);
    float clear, blocked, unshadowed, opaque, attenuated;
    /* No visible solids: this must still integrate the spotlight's volume. */
    REQUIRE(gpu_render(&e, &clear));
    REQUIRE(clear > .01f);
    ecs_entity wall = gpu_add(COMPONENT_Transform.id, &(Transform){ -3, 0 });
    REQUIRE(
        ecs_world_record_add(
            gpu_world,
            wall,
            COMPONENT_Geometry3D.id,
            &(Geometry3D){ PRIMITIVE_BOX, .5f, 16, 16, 0 },
            0
        ) == ECS_OK
    );
    REQUIRE(
        ecs_world_record_add(gpu_world, wall, COMPONENT_Material.id, &(Material){ .scene = 0x000000ffu }, 0) ==
        ECS_OK
    );
    REQUIRE(ecs_world_apply_commands(gpu_world) == ECS_OK);
    REQUIRE(gpu_render(&e, &blocked));
    SpotLight_get(gpu_world, lamp)->shadows = false;
    REQUIRE(ecs_component_modified(gpu_world, lamp, COMPONENT_SpotLight.id) == ECS_OK);
    REQUIRE(gpu_render(&e, &unshadowed));
    fprintf(stderr, "scene3d-volume clear=%g blocked=%g unshadowed=%g\n", clear, blocked, unshadowed);
    REQUIRE(blocked < clear * .1f);
    REQUIRE(unshadowed > clear * .8f);

    SpotLight_get(gpu_world, lamp)->intensity = 0;
    REQUIRE(ecs_component_modified(gpu_world, lamp, COMPONENT_SpotLight.id) == ECS_OK);
    ecs_entity slab = gpu_add(COMPONENT_Transform.id, &(Transform){ 0 });
    REQUIRE(
        ecs_world_record_add(
            gpu_world,
            slab,
            COMPONENT_Geometry3D.id,
            &(Geometry3D){ PRIMITIVE_BOX, 8, 8, .2f, 0 },
            0
        ) == ECS_OK
    );
    REQUIRE(
        ecs_world_record_add(
            gpu_world,
            slab,
            COMPONENT_Material.id,
            &(Material){ .scene = 0x000000ffu,
                         .emission_color = 0xffffffffu,
                         .emission_intensity = 1,
                         .emission_multiplier = 1 },
            0
        ) == ECS_OK
    );
    REQUIRE(ecs_world_apply_commands(gpu_world) == ECS_OK);
    e.fog_density = 0;
    REQUIRE(gpu_render(&e, &opaque));
    e.fog_density = .1f;
    e.fog_color = 0x000000ffu;
    e.fog_start = 0;
    REQUIRE(gpu_render(&e, &attenuated));
    /* Eye Y20 to slab front Y0.1, minus near-plane distance 0.1. */
    float expected = expf(-.1f * 19.8f);
    fprintf(stderr, "scene3d-volume opaque=%g attenuated=%g expected-ratio=%g\n", opaque, attenuated, expected);
    REQUIRE(opaque > .5f);
    REQUIRE(fabsf(attenuated / opaque - expected) < .02f);
    return true;
}

void test_scene3d_volume_gpu()
{
    test_open_hidden_app("scene3d-volume", SIDE, SIDE);
    pu_setup(gpu_setup, gpu_teardown);
    RUN_TEST_CASE(integrated_volume_uses_mesh_shadows_and_opaque_depth);
    pu_clear_setup();
    cf_destroy_app();
}
