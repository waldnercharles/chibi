#include "render/arena3d.h"

#include "platform/cute.h"
#include "render/arena_geometry.h"
#include "render/arena_parts.h"
#include "render/arena_post.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/visual_parts.h"
#include "runtime/visual_transform.h"
#include "test/support/code_fixture.h"
#include "test/support/event_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE
};

static ecs_world *new_world()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    return world;
}

static ecs_entity add_shape(ecs_world *world, int primitive, CF_V3 position, unsigned color)
{
    ecs_entity entity = ecs_world_create_entity(world);
    GameTransform transform = { position.x, position.z };
    GameGeometry3D shape = { primitive, 2, 2, 2, position.y };
    GameMaterial material = { .scene = color };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameTransform.id, &transform, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameGeometry3D.id, &shape, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameMaterial.id, &material, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return entity;
}

static render_arena_camera front_camera()
{
    return (render_arena_camera){
        .view = cf_look_at((CF_V3){ 0, 0, 8 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 }),
        .projection = cf_perspective(0.7853981634f, 1, 0.1f, 100)
    };
}

static bool camera_checks()
{
    float spans[][2] = { { 640, 360 }, { 360, 640 }, { 640, 640 } };
    for (int aspect = 0; aspect < 3; aspect++) {
        render_arena_camera
            camera = render_arena_camera_make(320, 180, spans[aspect][0], spans[aspect][1]);
        CF_V3 target = render_arena_project(&camera, (CF_V3){ 320, 0, -180 });
        if (fabsf(target.x) > 0.0001f || fabsf(target.y) > 0.0001f) {
            fprintf(
                stderr,
                "arena3d camera center failed: aspect %d, target (%g, %g)\n",
                aspect,
                target.x,
                target.y
            );
            return false;
        }
        for (int i = -1; i <= 1; i++) {
            CF_V3 world = { 320 + i * 60, 0, -180 + i * 40 };
            CF_V3 projected = render_arena_project(&camera, world);
            CF_V2 game;
            if (!render_arena_ground(&camera, (CF_V2){ projected.x, projected.y }, &game) ||
                fabsf(game.x - world.x) > 0.2f || fabsf(game.y - world.z) > 0.2f) {
                fprintf(stderr, "arena3d ground mapping failed: aspect %d, point %d\n", aspect, i);
                return false;
            }
        }
        CF_V3 down = render_arena_project(&camera, (CF_V3){ 320, 0, -120 });
        if (down.y >= target.y) {
            fprintf(stderr, "arena3d game Y direction failed: aspect %d\n", aspect);
            return false;
        }
        CF_Ray3 center = render_arena_ray(&camera, (CF_V2){ 0 });
        CF_V3 across = render_arena_project(&camera, (CF_V3){ 380, 0, -180 });
        float horizontal_pixels = fabsf(across.x) * spans[aspect][0];
        float vertical_pixels = fabsf(down.y) * spans[aspect][1];
        float proportion = vertical_pixels / horizontal_pixels;
        if (center.d.y > -0.97f || proportion < 0.95f || proportion > 1.05f) {
            fprintf(stderr, "arena3d overhead composition failed: aspect %d, proportion %g\n", aspect, proportion);
            return false;
        }
    }
    render_arena_camera camera = front_camera();
    CF_V3 far = render_arena_project(&camera, (CF_V3){ 1, 0, 0 });
    CF_V3 near = render_arena_project(&camera, (CF_V3){ 1, 0, 3 });
    return near.x > far.x * 1.5f;
}

static bool pick_checks()
{
    render_arena_camera camera = front_camera();
    for (int primitive = GAME_PRIMITIVE_BOX; primitive <= GAME_PRIMITIVE_TETRAHEDRON;
         primitive++) {
        ecs_world *world = new_world();
        add_shape(world, primitive, (CF_V3){ 0, 0, 0 }, 0x0000ffffu);
        ecs_entity near = add_shape(world, primitive, (CF_V3){ 0, 0, 3 }, 0xff0000ffu);
        CF_V3 probe = render_arena_project(
            &camera,
            primitive == GAME_PRIMITIVE_TETRAHEDRON ? (CF_V3){ -.4f, -.4f, 3 }
                                                    : (CF_V3){ 0, 0, 3 }
        );
        ecs_entity hit = render_arena_pick(
            world,
            &camera,
            (CF_V2){ probe.x, probe.y },
            (render_skip){ 0 }
        );
        bool ok = ecs_entity_to_u64(hit) == ecs_entity_to_u64(near) &&
                  !render_arena_pick(world, &camera, (CF_V2){ 0.98f, 0.98f }, (render_skip){ 0 })
                       .slot;
        ecs_world_free(world);
        if (!ok) {
            fprintf(stderr, "arena3d picking primitive %d failed\n", primitive);
            return false;
        }
    }
    return true;
}

static bool bounds_checks()
{
    ecs_world *world = new_world();
    ecs_entity entity = add_shape(world, GAME_PRIMITIVE_BOX, (CF_V3){ 30, 20, -10 }, 0xffffffffu);
    GameGeometry3D *shape = GAME_GameGeometry3D_get(world, entity);
    shape->width = 8;
    shape->depth = 2;
    shape->height = 40;
    GameOrientation yaw = { .radians = 0.7853981634f };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameOrientation.id, &yaw, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(!GAME_GameAppearance_has(world, entity));
    CF_V3 corners[8] = { 0 };
    bool ok = render_arena_entity_bounds(world, entity, corners);
    ok &= fabsf(corners[7].x - 32.12132f) < 0.0001f;
    ok &= fabsf(corners[7].z + 6.464466f) < 0.0001f;
    ok &= corners[0].y == 0 && corners[7].y == 40;
    if (!ok) {
        fprintf(
            stderr,
            "arena3d rotated bounds failed: corner 7 (%g, %g, %g), base %g\n",
            corners[7].x,
            corners[7].y,
            corners[7].z,
            corners[0].y
        );
    }
    const float aspects[] = { 0.5f, 1, 3.5f };
    for (int aspect = 0; aspect < 3; aspect++) {
        bool framed = true;
        CF_Aabb fit = render_arena_fit(corners, 8, aspects[aspect]);
        render_arena_camera camera = render_arena_camera_make(
            (fit.min.x + fit.max.x) * 0.5f,
            (fit.min.y + fit.max.y) * 0.5f,
            fit.max.x - fit.min.x,
            fit.max.y - fit.min.y
        );
        float extent = 0;
        for (int i = 0; i < 8; i++) {
            CF_V3 ndc = render_arena_project(&camera, corners[i]);
            extent = fmaxf(extent, fmaxf(fabsf(ndc.x), fabsf(ndc.y)));
            framed &= ndc.z > 0 && ndc.z < 1;
        }
        framed &= extent > 0.83f && extent < 0.834f;
        CF_Aabb projected = { 0 };
        bool projected_bounds = render_arena_screen_bounds(&camera, corners, &projected);
        for (int i = 0; i < 8; i++) {
            CF_V3 ndc = render_arena_project(&camera, corners[i]);
            projected_bounds &= ndc.x >= projected.min.x - 0.0001f &&
                                ndc.x <= projected.max.x + 0.0001f &&
                                ndc.y >= projected.min.y - 0.0001f &&
                                ndc.y <= projected.max.y + 0.0001f;
        }
        if (!framed || !projected_bounds) {
            fprintf(
                stderr,
                "arena3d fit failed: aspect %g, extent %g, depth/framing %d, "
                "projected bounds %d\n",
                aspects[aspect],
                extent,
                framed,
                projected_bounds
            );
        }
        ok &= framed && projected_bounds;
    }
    render_arena_camera camera = front_camera();
    *GAME_GameTransform_get(world, entity) = (GameTransform){ 0, 7.95f };
    shape = GAME_GameGeometry3D_get(world, entity);
    *shape = (GameGeometry3D){ GAME_PRIMITIVE_BOX, 2, 2, 2, 0 };
    assert(render_arena_entity_bounds(world, entity, corners));
    CF_Aabb clipped = { 0 };
    bool near_clip = render_arena_screen_bounds(&camera, corners, &clipped);
    near_clip &= isfinite(
        clipped.min.x + clipped.min.y + clipped.max.x + clipped.max.y
    );
    GAME_GameTransform_get(world, entity)->y = 20;
    assert(render_arena_entity_bounds(world, entity, corners));
    bool behind = !render_arena_screen_bounds(&camera, corners, &clipped);
    if (!near_clip || !behind) {
        fprintf(stderr, "arena3d bounds clipping failed: near %d, behind %d\n", near_clip, behind);
    }
    ok &= near_clip && behind;
    ecs_world_free(world);
    return ok;
}

static bool read_pixels(CF_Canvas canvas, CF_Pixel *pixels)
{
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        return false;
    }
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        if (cf_get_ticks() - start > cf_get_tick_frequency() * 10) {
            cf_destroy_readback(readback);
            return false;
        }
        cf_sleep(1);
    }
    int bytes = PIXELS * (int)sizeof *pixels;
    bool ok = cf_readback_data(readback, pixels, bytes) == bytes;
    cf_destroy_readback(readback);
    return ok;
}

static bool serial_geometry;
static render_arena_work geometry_work;
static bool render(ecs_world *world, const render_arena_camera *camera, CF_Pixel *pixels)
{
    render_arena_post *post = render_arena_post_new();
    GameEnvironment environment = render_arena_post_defaults();
    environment.bloom_strength = 0;
    assert(render_arena_post_prepare(post, SIDE, SIDE, &environment));
    CF_Canvas canvas = render_arena_post_target(post);
    render_arena *arena = render_arena_new();
    render_arena_set_parallel(arena, !serial_geometry);
    game_particles *particles = game_particles_new();
    assert(canvas.id && arena);
    cf_canvas_set_clear_color(canvas, (CF_Color){ 0, 0, 0, 1 });
    cf_app_update(NULL);
    bool ok = render_arena_draw(arena, world, particles, camera, 0, NULL, (render_skip){ 0 });
    geometry_work = render_arena_last_work(arena);
    cf_render_to(canvas, true);
    ok &= render_arena_post_process(post);
    cf_app_draw_onto_screen(false);
    ok &= read_pixels(render_arena_post_output(post), pixels);
    render_arena_free(arena);
    game_particles_free(particles);
    render_arena_post_free(post);
    return ok;
}

static int brightness(const CF_Pixel *pixels, const render_arena_camera *camera, CF_V3 point)
{
    CF_V3 ndc = render_arena_project(camera, point);
    int x = (int)((ndc.x * 0.5f + 0.5f) * SIDE);
    int y = (int)((0.5f - ndc.y * 0.5f) * SIDE);
    assert(x > 0 && x < SIDE - 1 && y > 0 && y < SIDE - 1);
    int total = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            total += pixels[(y + dy) * SIDE + x + dx].colors.r;
        }
    }
    return total / 9;
}

static bool fitted_render_check(CF_Pixel *pixels)
{
    ecs_world *world = new_world();
    ecs_entity entity = add_shape(world, GAME_PRIMITIVE_BOX, (CF_V3){ 0, 15, 0 }, 0xffffffffu);
    *GAME_GameGeometry3D_get(world, entity) = (GameGeometry3D){ GAME_PRIMITIVE_BOX,
                                                                8,
                                                                2,
                                                                30,
                                                                15 };
    GameOrientation yaw = { .radians = 0.7f };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameOrientation.id, &yaw, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    CF_V3 points[8];
    assert(render_arena_entity_bounds(world, entity, points));
    CF_Aabb fit = render_arena_fit(points, 8, 1);
    render_arena_camera camera = render_arena_camera_make(
        (fit.min.x + fit.max.x) * 0.5f,
        (fit.min.y + fit.max.y) * 0.5f,
        fit.max.x - fit.min.x,
        fit.max.y - fit.min.y
    );
    bool ok = render(world, &camera, pixels);
    float extent = 0;
    int covered = 0;
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            if (pixels[y * SIDE + x].colors.r > 20) {
                covered++;
                extent = fmaxf(
                    extent,
                    fmaxf(fabsf(2 * (x + 0.5f) / SIDE - 1), fabsf(2 * (y + 0.5f) / SIDE - 1))
                );
            }
        }
    }
    ok &= covered > 500 && extent > 0.78f && extent < 0.85f;
    if (!ok) {
        fprintf(stderr, "arena3d rendered fit failed: covered %d pixels, extent %g\n", covered, extent);
    }
    ecs_world_free(world);
    return ok;
}

static bool prism_checks(CF_Pixel *pixels)
{
    ecs_world *world = new_world();
    ecs_entity entity = add_shape(world, GAME_PRIMITIVE_TRIANGLE_PRISM, (CF_V3){ 0 }, 0xffffffffu);
    *GAME_GameGeometry3D_get(world, entity) = (GameGeometry3D){ GAME_PRIMITIVE_TRIANGLE_PRISM,
                                                                4,
                                                                3,
                                                                1,
                                                                0.4f };
    GameOrientation orientation = { 0 };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameOrientation.id, &orientation, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_live_world *live = game_live_world_new(world);
    char *json = text_file_read("content/behaviors/TwinStickInputBehavior.code");
    assert(json);
    behavior_asset *asset = test_project_asset("content/behaviors/TwinStickInputBehavior.code", json);
    free(json);
    assert(asset);
    asset = link_test_libraries(asset);
    assert(game_live_add_definition(live, game_live_definition_new(asset->name, asset)) == 0);
    assert(game_behavior_attach(live->behaviors, entity, "TwinStickInputBehavior") == 0);
    GameAim aim = { 0 };
    assert(ecs_world_record_add(world, entity, GAME_COMPONENT_GameAim.id, &aim, 0) == ECS_OK);
    assert(GAME_GameVelocity_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    engine *runtime = engine_new(world, live);
    render_arena_camera camera = front_camera();
    camera.view = cf_look_at((CF_V3){ 0, 8, 0 }, (CF_V3){ 0 }, (CF_V3){ 0, 0, -1 });
    /* Wide at the back and narrow at the +X tip; both empty samples are in its box bounds. */
    const CF_V3 samples[] = {
        { -1.2f, 0.9f, 0.75f },
        { 1.2f, 0.9f, 0 },
        { 1.2f, 0.9f, 0.9f },
        { 1.2f, 0.9f, -0.9f },
    };
    bool ok = true;
    for (int turn = 0; turn < 8; turn++) {
        float angle = turn * 0.7853981634f;
        GAME_GameAim_get(world, entity)->direction = (CF_V2){ cosf(angle), sinf(angle) };
        assert(engine_frame(runtime, turn / 60.0) == FLOW_OK);
        bool rendered = render(world, &camera, pixels);
        CF_V3 corners[8];
        CF_Aabb bounds;
        bool bounded = render_arena_entity_bounds(world, entity, corners) &&
                       render_arena_screen_bounds(&camera, corners, &bounds);
        ok &= rendered && bounded;
        for (int i = 0; i < 4; i++) {
            CF_V3 point = samples[i];
            point.x = cosf(angle) * samples[i].x - sinf(angle) * samples[i].z;
            point.z = sinf(angle) * samples[i].x + cosf(angle) * samples[i].z;
            CF_V3 ndc = render_arena_project(&camera, point);
            bool in_bounds = bounded && ndc.x > bounds.min.x &&
                             ndc.x < bounds.max.x && ndc.y > bounds.min.y &&
                             ndc.y < bounds.max.y;
            ecs_entity hit = render_arena_pick(
                world,
                &camera,
                (CF_V2){ ndc.x, ndc.y },
                (render_skip){ 0 }
            );
            bool picked = ecs_entity_to_u64(hit) == ecs_entity_to_u64(entity);
            int light = brightness(pixels, &camera, point);
            bool inside = i < 2;
            bool sample_ok = in_bounds && picked == inside &&
                             (inside ? light > 60 : light == 0);
            if (!sample_ok) {
                fprintf(
                    stderr,
                    "arena3d prism failed: turn %d, sample %d, bounds %d, "
                    "picked %d, brightness %d\n",
                    turn,
                    i,
                    in_bounds,
                    picked,
                    light
                );
            }
            ok &= sample_ok;
        }
    }
    /* The underside closes the extrusion and retains an outward normal. */
    camera.view = cf_look_at((CF_V3){ 0, -8, 0 }, (CF_V3){ 0 }, (CF_V3){ 0, 0, 1 });
    ok &= render(world, &camera, pixels);
    int underside = brightness(pixels, &camera, (CF_V3){ 0, -0.1f, 0 });
    if (underside <= 40) {
        fprintf(stderr, "arena3d prism underside failed: brightness %d\n", underside);
    }
    ok &= underside > 40;
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
    return ok;
}

static bool octahedron_checks(CF_Pixel *pixels)
{
    ecs_world *world = new_world();
    ecs_entity entity = add_shape(world, GAME_PRIMITIVE_OCTAHEDRON, (CF_V3){ 0 }, 0xffffffffu);
    *GAME_GameGeometry3D_get(world, entity) = (GameGeometry3D){ GAME_PRIMITIVE_OCTAHEDRON,
                                                                4,
                                                                3,
                                                                2,
                                                                0 };
    GameOrientation orientation = { 0 };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameOrientation.id, &orientation, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    /* The last two points lie inside the box but outside the diamond outline.
     * Front and underside views verify a closed solid, not a flat floor shape. */
    static const CF_V3 overhead_samples[] = {
        { -1.2f, 0, 0 },
        { 0, 0, 0.9f },
        { 1.3f, 0, 0.95f },
        { -1.3f, 0, -0.95f },
    };
    static const CF_V3 front_samples[] = {
        { -1.2f, 0, 0 },
        { 0, 0.6f, 0 },
        { 1.3f, 0.7f, 0 },
        { -1.3f, -0.7f, 0 },
    };
    bool ok = true;
    for (int view = 0; view < 10; view++) {
        float angle = view < 8 ? view * 0.7853981634f : 0;
        GAME_GameOrientation_get(world, entity)->radians = angle;
        render_arena_camera camera = front_camera();
        if (view < 8) {
            camera.view = cf_look_at((CF_V3){ 0, 8, 0 }, (CF_V3){ 0 }, (CF_V3){ 0, 0, -1 });
        }
        if (view == 8) {
            camera.view = cf_look_at((CF_V3){ 0, -8, 0 }, (CF_V3){ 0 }, (CF_V3){ 0, 0, 1 });
        }
        ok &= render(world, &camera, pixels);
        const CF_V3 *samples = view == 9 ? front_samples : overhead_samples;
        for (int sample = 0; sample < 4; sample++) {
            CF_V3 point = samples[sample];
            point.x = cosf(angle) * samples[sample].x -
                      sinf(angle) * samples[sample].z;
            point.z = sinf(angle) * samples[sample].x +
                      cosf(angle) * samples[sample].z;
            CF_V3 ndc = render_arena_project(&camera, point);
            ecs_entity hit = render_arena_pick(
                world,
                &camera,
                (CF_V2){ ndc.x, ndc.y },
                (render_skip){ 0 }
            );
            bool picked = ecs_entity_to_u64(hit) == ecs_entity_to_u64(entity);
            int light = brightness(pixels, &camera, point);
            bool inside = sample < 2;
            bool sample_ok = picked == inside && (inside ? light > 40 : light == 0);
            if (!sample_ok) {
                fprintf(stderr, "arena3d octahedron failed: view %d, sample %d, picked %d, brightness %d\n", view, sample, picked, light);
            }
            ok &= sample_ok;
        }
    }
    ecs_world_free(world);
    return ok;
}

static bool composed_visuals(CF_Pixel *first, CF_Pixel *second)
{
    ecs_world *world = new_world();
    ecs_entity entity = add_shape(world, GAME_PRIMITIVE_OCTAHEDRON, (CF_V3){ 0 }, 0xffffffffu);
    *GAME_GameGeometry3D_get(world, entity) = (GameGeometry3D){ GAME_PRIMITIVE_OCTAHEDRON,
                                                                4,
                                                                3,
                                                                2,
                                                                0 };
    GameVisualTransform transform = { .pitch = .31f, .yaw = .22f, .roll = .5f, .scale = .8f };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameVisualTransform.id, &transform, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    render_arena_camera camera = front_camera();
    bool ok = render(world, &camera, first);
    bool before[225];
    for (int i = 0; i < 225; i++) {
        CF_V2 point = { (i % 15 - 7) * .065f, (i / 15 - 7) * .065f };
        before[i] = render_arena_pick(world, &camera, point, (render_skip){ 0 }).slot != 0;
    }
    /* Eight canonical tetrahedra reconstruct every visible face of the source solid. */
    arena_parts parts = { .count = 8 };
    for (int i = 0; i < 8; i++) {
        int x = i & 1 ? 1 : -1, y = i & 2 ? 1 : -1, z = i & 4 ? 1 : -1;
        bool even = x * y * z > 0;
        parts.parts[i] = (arena_part){
            .primitive = GAME_PRIMITIVE_TETRAHEDRON,
            .dimensions = even ? (CF_V3){ 2, 1, 1.5f } : (CF_V3){ 1.5f, 1, 2 },
            .position = { x, y * .5f, z * .75f },
            .rotation = { 0,
                          even ? (x < 0 ? 3.1415926536f : 0) : z * 1.5707963268f,
                          y < 0 ? 3.1415926536f : 0 }
        };
    }
    char *json = render_arena_parts_write(&parts);
    GameMeshParts mesh = { .parts = json };
    assert(
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameMeshParts.id, &mesh, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    ok &= render(world, &camera, second);
    int changed = 0;
    for (int i = 0; i < PIXELS; i++) {
        changed += abs(first[i].colors.r - second[i].colors.r) > 5;
    }
    ok &= changed < 20;
    for (int i = 0; i < 225; i++) {
        CF_V2 point = { (i % 15 - 7) * .065f, (i / 15 - 7) * .065f };
        ok &= (render_arena_pick(world, &camera, point, (render_skip){ 0 }).slot !=
               0) == before[i];
    }
    GAME_GameVisualTransform_get(world, entity)->scale = 0;
    CF_V3 bounds[8];
    ok &= !render_arena_entity_bounds(world, entity, bounds);
    ok &= !render_arena_pick(world, &camera, (CF_V2){ 0 }, (render_skip){ 0 }).slot;
    if (!ok) {
        fprintf(stderr, "composed visual mismatch: %d pixels changed across exact tetrahedron decomposition\n", changed);
    }
    ecs_world_free(world);
    free(json);
    return ok;
}

static bool polygon_visuals(CF_Pixel *pixels)
{
    render_arena_camera camera = front_camera();
    camera.view = cf_look_at((CF_V3){ 0, 8, 0 }, (CF_V3){ 0 }, (CF_V3){ 0, 0, -1 });
    const int primitives[] = { GAME_PRIMITIVE_CHEVRON_PYRAMID,
                               GAME_PRIMITIVE_BOX_FRAME,
                               GAME_PRIMITIVE_TRIANGLE_OUTLINE,
                               GAME_PRIMITIVE_TRIANGLE };
    const CF_V3 solid[] = { { 0 }, { .88f, .88f, 0 }, { 1.0f / 3 - .04f, 0, .44f }, { 0 } };
    /* The ship's center is filled; its concave rear notch stays outside the mesh. */
    const CF_V3 hole[] = { { -.95f, 0, 0 }, { 0 }, { 0 } };
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        ecs_world *world = new_world();
        ecs_entity entity = add_shape(world, primitives[i], (CF_V3){ 0 }, 0xffffffffu);
        if (i >= 2) {
            GAME_GameGeometry3D_get(world, entity)->height = .1f;
        }
        ok &= render(world, &camera, pixels);
        CF_V3 ndc = render_arena_project(&camera, solid[i]);
        bool visible = brightness(pixels, &camera, solid[i]) > 40 &&
                       render_arena_pick(world, &camera, (CF_V2){ ndc.x, ndc.y }, (render_skip){ 0 })
                               .slot == entity.slot;
        if (i < 3) {
            ndc = render_arena_project(&camera, hole[i]);
            visible &= !render_arena_pick(world, &camera, (CF_V2){ ndc.x, ndc.y }, (render_skip){ 0 })
                            .slot &&
                       brightness(pixels, &camera, hole[i]) == 0;
        }
        if (!visible) {
            fprintf(stderr, "arena3d polygon primitive %d disagrees between mesh and picking\n", primitives[i]);
        }
        ok &= visible;
        ecs_world_free(world);
    }
    return ok;
}

static bool centered_triangles()
{
    bool ok = true;
    for (int primitive = GAME_PRIMITIVE_TRIANGLE_OUTLINE;
         primitive <= GAME_PRIMITIVE_TRIANGLE;
         primitive++) {
        CF_V3 triangles[ARENA_MESH_TRIANGLE_LIMIT][3];
        int count = render_arena_mesh_triangles(primitive, triangles);
        CF_V3 centroid = { 0 };
        float area = 0;
        for (int i = 0; i < count; i++) {
            CF_V3 *p = triangles[i];
            float weight = cf_len_v3(
                cf_cross_v3(cf_sub_v3(p[1], p[0]), cf_sub_v3(p[2], p[0]))
            );
            centroid = cf_add_v3(
                centroid,
                cf_mul_v3_f(cf_add_v3(cf_add_v3(p[0], p[1]), p[2]), weight / 3)
            );
            area += weight;
        }
        ok &= area > 0 && cf_len_v3(cf_mul_v3_f(centroid, 1 / area)) < .00001f;
        ecs_world *world = new_world();
        ecs_entity entity = add_shape(world, primitive, (CF_V3){ 0 }, 0xffffffffu);
        GAME_GameGeometry3D_get(world, entity)->height = .1f;
        GameOrientation heading = { 0 };
        assert(
            ecs_world_record_add(world, entity, GAME_COMPONENT_GameOrientation.id, &heading, 0) == ECS_OK
        );
        assert(ecs_world_apply_commands(world) == ECS_OK);
        render_arena_camera camera = front_camera();
        camera.view = cf_look_at((CF_V3){ 0, 8, 0 }, (CF_V3){ 0 }, (CF_V3){ 0, 0, -1 });
        for (int step = 0; step < 8; step++) {
            float angle = step * .7853981634f;
            GAME_GameOrientation_get(world, entity)->radians = angle;
            CF_V3 bounds[8];
            ok &= render_arena_entity_bounds(world, entity, bounds);
            CF_M4x4 rotation = cf_m4_from_trs(
                (CF_V3){ 0 },
                game_visual_rotation(angle, (CF_V3){ 0 }),
                (CF_V3){ 1, 1, 1 }
            );
            CF_M4x4 inverse = cf_m4_invert(rotation);
            CF_V3 minimum = cf_m4_transform_point(inverse, bounds[0]);
            CF_V3 maximum = cf_m4_transform_point(inverse, bounds[7]);
            ok &= fabsf(minimum.x + 2.0f / 3) < .0001f &&
                  fabsf(maximum.x - 4.0f / 3) < .0001f;
            ok &= fabsf(maximum.z - minimum.z - 2) < .0001f;
            bool picked_center = render_arena_pick(world, &camera, (CF_V2){ 0 }, (render_skip){ 0 })
                                     .slot == entity.slot;
            ok &= picked_center == (primitive == GAME_PRIMITIVE_TRIANGLE);
            CF_V3 edge = cf_m4_transform_point(rotation, (CF_V3){ 1.0f / 3 - .04f, 0, .44f });
            CF_V3 ndc = render_arena_project(&camera, edge);
            ok &= render_arena_pick(world, &camera, (CF_V2){ ndc.x, ndc.y }, (render_skip){ 0 })
                      .slot == entity.slot;
        }
        ecs_world_free(world);
    }
    if (!ok) {
        fprintf(stderr, "flat triangle centroid, rotating bounds, or exact picking failed\n");
    }
    return ok;
}

static bool frame_parts(CF_Pixel *before, CF_Pixel *after)
{
    ecs_world *world = new_world();
    const unsigned color = 0xf07888ffu;
    ecs_entity source = add_shape(world, GAME_PRIMITIVE_BOX_FRAME, (CF_V3){ 0 }, color);
    GameGeometry3D shape = { .primitive = GAME_PRIMITIVE_BOX_FRAME,
                             .width = 4,
                             .height = 2,
                             .depth = 3 };
    *GAME_GameGeometry3D_get(world, source) = shape;
    GameVisualTransform visual = { .pitch = .31f, .yaw = .22f, .roll = .5f, .scale = .8f };
    GameOrientation heading = { .radians = .35f };
    assert(
        ecs_world_record_add(world, source, GAME_COMPONENT_GameVisualTransform.id, &visual, 0) == ECS_OK
    );
    assert(
        ecs_world_record_add(world, source, GAME_COMPONENT_GameOrientation.id, &heading, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    render_arena_camera camera = front_camera();
    bool ok = render(world, &camera, before), hits[625];
    for (int i = 0; i < 625; i++) {
        CF_V2 point = { (i % 25 - 12) * .04f, (i / 25 - 12) * .04f };
        hits[i] = render_arena_pick(world, &camera, point, (render_skip){ 0 }).slot != 0;
    }
    CF_M4x4 frame = cf_m4_from_trs(
        (CF_V3){ 0 },
        game_visual_rotation(
            heading.radians,
            (CF_V3){ visual.pitch, visual.yaw, visual.roll }
        ),
        (CF_V3){ visual.scale, visual.scale, visual.scale }
    );
    assert(ecs_world_destroy_entity(world, source) == ECS_OK);
    for (int i = 0; i < game_visual_part_count(shape.primitive); i++) {
        game_visual_part part = game_visual_part_at(shape, i);
        CF_V3 center = cf_m4_transform_point(frame, part.center);
        ecs_entity piece = add_shape(world, part.primitive, center, color);
        *GAME_GameGeometry3D_get(world, piece) = (GameGeometry3D){
            .primitive = part.primitive,
            .width = part.dimensions.x,
            .height = part.dimensions.y,
            .depth = part.dimensions.z,
            .elevation = center.y
        };
        assert(
            ecs_world_record_add(
                world,
                piece,
                GAME_COMPONENT_GameVisualTransform.id,
                &visual,
                0
            ) == ECS_OK
        );
        assert(
            ecs_world_record_add(world, piece, GAME_COMPONENT_GameOrientation.id, &heading, 0) == ECS_OK
        );
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    ok &= render(world, &camera, after);
    int changed = 0;
    for (int i = 0; i < PIXELS; i++) {
        changed += abs(before[i].colors.r - after[i].colors.r) > 5 ||
                   abs(before[i].colors.g - after[i].colors.g) > 5 ||
                   abs(before[i].colors.b - after[i].colors.b) > 5;
    }
    ok &= changed < 20;
    for (int i = 0; i < 625; i++) {
        CF_V2 point = { (i % 25 - 12) * .04f, (i / 25 - 12) * .04f };
        ok &= (render_arena_pick(world, &camera, point, (render_skip){ 0 }).slot !=
               0) == hits[i];
    }
    if (!ok) {
        fprintf(stderr, "frame breakup changed source coverage or lighting at birth (%d pixels)\n", changed);
    }
    ecs_world_free(world);
    return ok;
}

static bool parallel_geometry(CF_Pixel *first, CF_Pixel *second)
{
    ecs_world *world = new_world();
    for (int i = 0; i < 1537; i++) {
        ecs_entity entity = add_shape(
            world,
            i % 2 ? GAME_PRIMITIVE_BOX : GAME_PRIMITIVE_OCTAHEDRON,
            (CF_V3){ (i % 32 - 15.5f) * .15f, (i / 32 - 23.5f) * .15f, (i % 3) * .1f },
            i % 3 ? 0x4080ffffu : 0xff804080u
        );
        GameGeometry3D *shape = GAME_GameGeometry3D_get(world, entity);
        shape->width = shape->height = shape->depth = .1f;
        if (i % 200 == 0) {
            GameMeshParts parts = { .parts = "(array)- {\n"
                                             "    - {\n"
                                             "        primitive 0\n"
                                             "        (array)size 0.1 0.1 0.1\n"
                                             "        (array)rotation 0 0.3 0\n"
                                             "    }\n"
                                             "}\n" };
            assert(!ecs_world_record_add(
                world,
                entity,
                GAME_COMPONENT_GameMeshParts.id,
                &parts,
                0
            ));
            assert(!ecs_world_apply_commands(world));
        }
    }
    render_arena_camera camera = front_camera();
    serial_geometry = true;
    bool ok = render(world, &camera, first);
    assert(!geometry_work.tasks);
    serial_geometry = false;
    ok &= render(world, &camera, second);
    ok &= !memcmp(first, second, PIXELS * sizeof *first);
    printf(
        "parallel rendering %s: entities=%d tasks=%d workers=%d, exact "
        "opaque/transparent pixels\n",
        ok ? "PASS" : "FAIL",
        geometry_work.entities,
        geometry_work.tasks,
        geometry_work.workers
    );
    ecs_world_free(world);
    return ok;
}

int test_arena3d()
{
    bool camera_ok = camera_checks();
    bool picking = pick_checks();
    bool bounds = bounds_checks();
    CF_Pixel first[PIXELS] = { 0 }, second[PIXELS] = { 0 };
    bool parallel = parallel_geometry(first, second);
    bool fitted = fitted_render_check(first);
    bool prism = prism_checks(first);
    bool octahedron = octahedron_checks(first);
    bool composed = composed_visuals(first, second);
    composed &= polygon_visuals(first);
    composed &= centered_triangles();
    composed &= frame_parts(first, second);
    bool depth = true;
    render_arena_camera camera = front_camera();
    for (int order = 0; order < 2; order++) {
        ecs_world *world = new_world();
        for (int i = 0; i < 2; i++) {
            bool near = i == order;
            add_shape(world, GAME_PRIMITIVE_BOX, (CF_V3){ 0, 0, near ? 3 : 0 }, near ? 0xff0000ffu : 0x0000ffffu);
        }
        depth &= render(world, &camera, order ? second : first);
        ecs_world_free(world);
    }
    CF_Pixel center = first[SIDE / 2 * SIDE + SIDE / 2];
    /* Dielectric highlights add neutral light; the nearer red box must still dominate. */
    bool red = center.colors.r > 100 && center.colors.r > center.colors.g * 3 &&
               center.colors.r > center.colors.b * 3;
    bool same_order = memcmp(first, second, sizeof first) == 0;
    depth &= red && same_order;
    if (!depth) {
        fprintf(
            stderr,
            "arena3d depth failed: center RGB (%d, %d, %d), order independence "
            "%d\n",
            center.colors.r,
            center.colors.g,
            center.colors.b,
            same_order
        );
    }

    ecs_world *world = new_world();
    add_shape(world, GAME_PRIMITIVE_BOX, (CF_V3){ 0 }, 0xffffffffu);
    camera.view = cf_look_at((CF_V3){ 5, 4, 7 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 });
    bool lighting = render(world, &camera, first);
    int top = brightness(first, &camera, (CF_V3){ 0, 1, 0 });
    int side = brightness(first, &camera, (CF_V3){ 1, 0, 0 });
    lighting &= top > side + 30;
    ecs_world_free(world);

    camera = front_camera();
    bool primitives = true;
    for (int primitive = GAME_PRIMITIVE_BOX; primitive <= GAME_PRIMITIVE_TETRAHEDRON;
         primitive++) {
        world = new_world();
        add_shape(world, primitive, (CF_V3){ 0 }, 0xffffffffu);
        bool visible = render(world, &camera, first);
        int red = primitive == GAME_PRIMITIVE_TETRAHEDRON
                    ? brightness(first, &camera, (CF_V3){ -.4f, -.4f, 0 })
                    : first[SIDE / 2 * SIDE + SIDE / 2].colors.r;
        visible &= red > 60;
        if (!visible) {
            fprintf(stderr, "arena3d primitive %d failed: center red %d\n", primitive, red);
        }
        primitives &= visible;
        ecs_world_free(world);
    }
    const int repeated[] = { GAME_PRIMITIVE_BOX,
                             GAME_PRIMITIVE_TRIANGLE_PRISM,
                             GAME_PRIMITIVE_OCTAHEDRON };
    bool batching = true;
    int instances = 0, commands = 0;
    for (int primitive = 0; primitive < (int)(sizeof repeated / sizeof *repeated);
         primitive++) {
        world = new_world();
        for (int i = 0; i < 256; i++) {
            ecs_entity entity = add_shape(
                world,
                repeated[primitive],
                (CF_V3){ (i % 16 - 7.5f) * 0.3f, (i / 16 - 7.5f) * 0.3f, 0 },
                0x4080ffffu
            );
            GameGeometry3D *shape = GAME_GameGeometry3D_get(world, entity);
            shape->width = shape->depth = shape->height = 0.2f;
        }
        cf_draw3d_stats(); /* Read-and-reset excludes the earlier render cases. */
        batching &= render(world, &camera, first);
        CF_DrawStats3d stats = cf_draw3d_stats();
        batching &= stats.instances == 256 && stats.commands <= 4;
        instances += stats.instances;
        commands += stats.commands;
        ecs_world_free(world);
    }
    bool ok = parallel && camera_ok && picking && bounds && fitted && depth &&
              lighting && primitives && prism && octahedron && composed && batching;
    printf(
        "arena3d %s: camera %d, picking %d, bounds %d, rendered fit %d, depth "
        "%d, "
        "lighting %d (%d/%d), primitives %d, prism %d, octahedron %d, batching "
        "%d (%d "
        "instances/%d "
        "draws)\n",
        ok ? "PASS" : "FAIL",
        camera_ok,
        picking,
        bounds,
        fitted,
        depth,
        lighting,
        top,
        side,
        primitives,
        prism,
        octahedron,
        batching,
        instances,
        commands
    );
    fflush(stdout);
    return !ok;
}
