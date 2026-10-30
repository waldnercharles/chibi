#include "hidden_app.h"
#include "render/scene3d.h"
#include "render/scene3d_geometry.h"
#include "render/scene3d_parts.h"
#include "render/scene3d_post.h"
#include "runtime/component_catalog.h"
#include "runtime/visual_parts.h"
#include "runtime/visual_transform.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The 3D path's primitive solids: framing, ground mapping, picking and bounds
 * of every primitive, and what the renderer draws of them, lit only by the
 * Environment's ambient light, through a hidden 128x128 app. */

enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE
};

static const float EIGHTH_TURN = .7853981634f;

static ecs_world *world;
static render_scene3d *scene3d;
static render_scene3d_post *post;
static particles *test_particles;
static CF_Pixel first[PIXELS], second[PIXELS];

static void fresh_world()
{
    ecs_world_free(world);
    world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
}

static void setup()
{
    fresh_world();
    scene3d = render_scene3d_new();
    post = render_scene3d_post_new();
    test_particles = particles_new();
}

static void teardown()
{
    render_scene3d_free(scene3d);
    render_scene3d_post_free(post);
    particles_free(test_particles);
    ecs_world_free(world);
    world = NULL;
    render_scene3d_parts_clear();
}

static void add(ecs_entity entity, const ecs_component_desc *component, const void *value)
{
    assert(ecs_world_record_add(world, entity, component->id, value, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
}

/* A 2-unit `primitive` centered at `position`, colored `color`. */
static ecs_entity add_shape(int primitive, CF_V3 position, unsigned color)
{
    ecs_entity entity = ecs_world_create_entity(world);
    add(entity, &COMPONENT_Transform, &(Transform){ position.x, position.z });
    add(entity,
        &COMPONENT_Geometry3D,
        &(Geometry3D){ primitive, 2, 2, 2, position.y });
    add(entity, &COMPONENT_Material, &(Material){ .scene = color });
    return entity;
}

static void turn(ecs_entity entity, float radians)
{
    add(entity, &COMPONENT_Orientation, &(Orientation){ .radians = radians });
}

/* Looks at the origin from 8 units in front. */
static render_scene3d_camera front_camera()
{
    return (render_scene3d_camera){
        .view = cf_look_at((CF_V3){ 0, 0, 8 }, (CF_V3){ 0 }, (CF_V3){ 0, 1, 0 }),
        .projection = cf_perspective(EIGHTH_TURN, 1, .1f, 100),
        .eye = { 0, 0, 8 },
    };
}

/* Looks down at the origin from 8 units above, or up from below. */
static render_scene3d_camera vertical_camera(float height)
{
    render_scene3d_camera camera = front_camera();
    camera.eye = (CF_V3){ 0, height, 0 };
    camera.view = cf_look_at(camera.eye, (CF_V3){ 0 }, (CF_V3){ 0, 0, height > 0 ? -1 : 1 });
    return camera;
}

static bool picks(const render_scene3d_camera *camera, CF_V2 ndc, ecs_entity entity)
{
    ecs_entity hit = render_scene3d_pick(world, camera, ndc, (render_skip){ 0 });
    return ecs_entity_to_u64(hit) == ecs_entity_to_u64(entity);
}

static bool picks_any(const render_scene3d_camera *camera, CF_V2 ndc)
{
    return render_scene3d_pick(world, camera, ndc, (render_skip){ 0 }).slot != 0;
}

static CF_V2 project(const render_scene3d_camera *camera, CF_V3 point)
{
    CF_V3 ndc = render_scene3d_project(camera, point);
    return (CF_V2){ ndc.x, ndc.y };
}

/* Draws the world through `camera` and reads back the tone-mapped output. */
static bool render(const render_scene3d_camera *camera, CF_Pixel *pixels)
{
    Environment environment = environment_defaults();
    environment.bloom_strength = 0;
    environment.occlusion = OCCLUSION_OFF;
    if (!render_scene3d_post_prepare(post, SIDE, SIDE, &environment)) {
        return false;
    }
    CF_Canvas canvas = render_scene3d_post_target(post);
    cf_canvas_set_clear_color(canvas, (CF_Color){ 0, 0, 0, 1 });
    cf_app_update(NULL);
    bool drawn = render_scene3d_draw(
        scene3d,
        world,
        test_particles,
        camera,
        0,
        NULL,
        &environment,
        (render_skip){ 0 }
    );
    cf_render_to(canvas, true);
    bool processed = render_scene3d_post_process(post, 0);
    cf_app_draw_onto_screen(false);
    return drawn && processed &&
           test_read_canvas(render_scene3d_post_output(post), pixels, PIXELS * (int)sizeof *pixels);
}

/* The mean red of the 3x3 pixels showing `point`. */
static int brightness(const CF_Pixel *pixels, const render_scene3d_camera *camera, CF_V3 point)
{
    CF_V2 ndc = project(camera, point);
    int x = (int)((ndc.x * .5f + .5f) * SIDE), y = (int)((.5f - ndc.y * .5f) * SIDE);
    assert(x > 0 && x < SIDE - 1 && y > 0 && y < SIDE - 1);
    int total = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            total += pixels[(y + dy) * SIDE + x + dx].colors.r;
        }
    }
    return total / 9;
}

/* `point` turned by `angle` about the up axis, as a heading turns it. */
static CF_V3 turned(CF_V3 point, float angle)
{
    return (CF_V3){ cosf(angle) * point.x - sinf(angle) * point.z,
                    point.y,
                    sinf(angle) * point.x + cosf(angle) * point.z };
}

static int changed_pixels(const CF_Pixel *a, const CF_Pixel *b)
{
    int changed = 0;
    for (int i = 0; i < PIXELS; i++) {
        changed += abs(a[i].colors.r - b[i].colors.r) > 5 ||
                   abs(a[i].colors.g - b[i].colors.g) > 5 ||
                   abs(a[i].colors.b - b[i].colors.b) > 5;
    }
    return changed;
}

TEST_CASE(overhead_cameras_center_and_map_the_ground)
{
    const float spans[][2] = { { 640, 360 }, { 360, 640 }, { 640, 640 } };
    for (int aspect = 0; aspect < 3; aspect++) {
        render_scene3d_camera camera = render_scene3d_camera_make(
            320,
            180,
            spans[aspect][0],
            spans[aspect][1]
        );
        CF_V3 target = render_scene3d_project(&camera, (CF_V3){ 320, 0, -180 });
        REQUIRE(fabsf(target.x) <= .0001f && fabsf(target.y) <= .0001f);
        for (int i = -1; i <= 1; i++) {
            CF_V3 point = { 320 + i * 60, 0, -180 + i * 40 };
            CF_V2 ground;
            REQUIRE(render_scene3d_ground(&camera, project(&camera, point), &ground));
            REQUIRE(fabsf(ground.x - point.x) <= .2f && fabsf(ground.y - point.z) <= .2f);
        }
        /* Game Y points up the screen, nearly straight down, and keeps the
         * proportions of the ground. */
        CF_V3 down = render_scene3d_project(&camera, (CF_V3){ 320, 0, -120 });
        REQUIRE(down.y < target.y);
        CF_V3 across = render_scene3d_project(&camera, (CF_V3){ 380, 0, -180 });
        float proportion = fabsf(down.y) * spans[aspect][1] /
                           (fabsf(across.x) * spans[aspect][0]);
        REQUIRE(render_scene3d_ray(&camera, (CF_V2){ 0 }).d.y <= -.97f);
        REQUIRE(proportion >= .95f && proportion <= 1.05f);
    }
    /* Perspective enlarges what is nearer. */
    render_scene3d_camera camera = front_camera();
    CF_V3 far = render_scene3d_project(&camera, (CF_V3){ 1, 0, 0 });
    CF_V3 near = render_scene3d_project(&camera, (CF_V3){ 1, 0, 3 });
    REQUIRE(near.x > far.x * 1.5f);
    return true;
}

TEST_CASE(picks_find_the_nearest_solid_of_each_primitive)
{
    render_scene3d_camera camera = front_camera();
    for (int primitive = PRIMITIVE_BOX; primitive <= PRIMITIVE_TETRAHEDRON;
         primitive++) {
        fresh_world();
        add_shape(primitive, (CF_V3){ 0, 0, 0 }, 0x0000ffffu);
        ecs_entity near = add_shape(primitive, (CF_V3){ 0, 0, 3 }, 0xff0000ffu);
        /* The tetrahedron's apex is not at its center, so probe its body. */
        CF_V3 probe = primitive == PRIMITIVE_TETRAHEDRON ? (CF_V3){ -.4f, -.4f, 3 }
                                                         : (CF_V3){ 0, 0, 3 };
        REQUIRE(picks(&camera, project(&camera, probe), near));
        REQUIRE(!picks_any(&camera, (CF_V2){ .98f, .98f }));
    }
    return true;
}

TEST_CASE(turned_bounds_fit_frame_and_clip)
{
    ecs_entity entity = add_shape(PRIMITIVE_BOX, (CF_V3){ 30, 20, -10 }, 0xffffffffu);
    Geometry3D *shape = Geometry3D_get(world, entity);
    shape->width = 8;
    shape->depth = 2;
    shape->height = 40;
    turn(entity, EIGHTH_TURN);
    REQUIRE(!Appearance_has(world, entity));
    CF_V3 corners[8];
    REQUIRE(render_scene3d_entity_bounds(world, entity, corners));
    /* Headings turn through the deterministic cosine and sine, which are
     * accurate to about .001. */
    REQUIRE(fabsf(corners[7].x - 32.12132f) < .02f);
    REQUIRE(fabsf(corners[7].z + 6.464466f) < .02f);
    REQUIRE(corners[0].y == 0 && corners[7].y == 40);
    const float aspects[] = { .5f, 1, 3.5f };
    for (int aspect = 0; aspect < 3; aspect++) {
        CF_Aabb fit = render_scene3d_fit(corners, 8, aspects[aspect]);
        render_scene3d_camera camera = render_scene3d_camera_make(
            (fit.min.x + fit.max.x) * .5f,
            (fit.min.y + fit.max.y) * .5f,
            fit.max.x - fit.min.x,
            fit.max.y - fit.min.y
        );
        CF_Aabb projected;
        REQUIRE(render_scene3d_screen_bounds(&camera, corners, &projected));
        float extent = 0;
        for (int i = 0; i < 8; i++) {
            CF_V3 ndc = render_scene3d_project(&camera, corners[i]);
            extent = fmaxf(extent, fmaxf(fabsf(ndc.x), fabsf(ndc.y)));
            REQUIRE(ndc.z > 0 && ndc.z < 1);
            REQUIRE(ndc.x >= projected.min.x - .0001f && ndc.x <= projected.max.x + .0001f);
            REQUIRE(ndc.y >= projected.min.y - .0001f && ndc.y <= projected.max.y + .0001f);
        }
        /* The fit's 20 percent margin leaves the bounds at 1 / 1.2 of the view. */
        REQUIRE(extent > .83f && extent < .834f);
    }
    /* A box crossing the near plane keeps finite bounds; one behind has none. */
    render_scene3d_camera camera = front_camera();
    *Transform_get(world, entity) = (Transform){ 0, 7.95f };
    *Geometry3D_get(world, entity) = (Geometry3D){ PRIMITIVE_BOX, 2, 2, 2, 0 };
    REQUIRE(render_scene3d_entity_bounds(world, entity, corners));
    CF_Aabb clipped;
    REQUIRE(render_scene3d_screen_bounds(&camera, corners, &clipped));
    REQUIRE(isfinite(clipped.min.x + clipped.min.y + clipped.max.x + clipped.max.y));
    Transform_get(world, entity)->y = 20;
    REQUIRE(render_scene3d_entity_bounds(world, entity, corners));
    REQUIRE(!render_scene3d_screen_bounds(&camera, corners, &clipped));
    return true;
}

/* Flat triangles center their area on the origin, so they turn in place, and
 * picks follow their outline and hole exactly. */
TEST_CASE(flat_triangles_turn_about_their_area_center)
{
    render_scene3d_camera camera = vertical_camera(8);
    for (int primitive = PRIMITIVE_TRIANGLE_OUTLINE; primitive <= PRIMITIVE_TRIANGLE;
         primitive++) {
        CF_V3 triangles[SCENE3D_MESH_TRIANGLE_LIMIT][3];
        int count = render_scene3d_mesh_triangles(primitive, triangles);
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
        REQUIRE(area > 0 && cf_len_v3(cf_mul_v3_f(centroid, 1 / area)) < .00001f);
        fresh_world();
        ecs_entity entity = add_shape(primitive, (CF_V3){ 0 }, 0xffffffffu);
        Geometry3D_get(world, entity)->height = .1f;
        turn(entity, 0);
        for (int step = 0; step < 8; step++) {
            float angle = step * EIGHTH_TURN;
            Orientation_get(world, entity)->radians = angle;
            CF_V3 bounds[8];
            REQUIRE(render_scene3d_entity_bounds(world, entity, bounds));
            CF_M4x4 rotation = cf_m4_from_trs(
                (CF_V3){ 0 },
                visual_rotation(angle, (CF_V3){ 0 }),
                (CF_V3){ 1, 1, 1 }
            );
            CF_M4x4 inverse = cf_m4_invert(rotation);
            CF_V3 minimum = cf_m4_transform_point(inverse, bounds[0]);
            CF_V3 maximum = cf_m4_transform_point(inverse, bounds[7]);
            REQUIRE(fabsf(minimum.x + 2.0f / 3) < .0001f && fabsf(maximum.x - 4.0f / 3) < .0001f);
            REQUIRE(fabsf(maximum.z - minimum.z - 2) < .0001f);
            REQUIRE(picks(&camera, (CF_V2){ 0 }, entity) == (primitive == PRIMITIVE_TRIANGLE));
            CF_V3 edge = cf_m4_transform_point(rotation, (CF_V3){ 1.0f / 3 - .04f, 0, .44f });
            REQUIRE(picks(&camera, project(&camera, edge), entity));
        }
    }
    return true;
}

TEST_CASE(every_solid_primitive_draws)
{
    render_scene3d_camera camera = front_camera();
    for (int primitive = PRIMITIVE_BOX; primitive <= PRIMITIVE_TETRAHEDRON;
         primitive++) {
        fresh_world();
        add_shape(primitive, (CF_V3){ 0 }, 0xffffffffu);
        REQUIRE(render(&camera, first));
        int red = primitive == PRIMITIVE_TETRAHEDRON
                    ? brightness(first, &camera, (CF_V3){ -.4f, -.4f, 0 })
                    : first[SIDE / 2 * SIDE + SIDE / 2].colors.r;
        REQUIRE(red > 60);
    }
    return true;
}

TEST_CASE(nearer_solids_win_in_any_order)
{
    render_scene3d_camera camera = front_camera();
    for (int order = 0; order < 2; order++) {
        fresh_world();
        for (int i = 0; i < 2; i++) {
            bool near = i == order;
            add_shape(PRIMITIVE_BOX, (CF_V3){ 0, 0, near ? 3 : 0 }, near ? 0xff0000ffu : 0x0000ffffu);
        }
        REQUIRE(render(&camera, order ? second : first));
    }
    CF_Pixel center = first[SIDE / 2 * SIDE + SIDE / 2];
    REQUIRE(center.colors.r > 100);
    REQUIRE(
        center.colors.r > center.colors.g * 3 && center.colors.r > center.colors.b * 3
    );
    REQUIRE(!memcmp(first, second, sizeof first));
    return true;
}

/* A tall turned box framed by its own fit fills 1 / 1.2 of the image. */
TEST_CASE(fitted_bounds_frame_what_draws)
{
    ecs_entity entity = add_shape(PRIMITIVE_BOX, (CF_V3){ 0, 15, 0 }, 0xffffffffu);
    *Geometry3D_get(world, entity) = (Geometry3D){ PRIMITIVE_BOX, 8, 2, 30, 15 };
    turn(entity, .7f);
    CF_V3 points[8];
    REQUIRE(render_scene3d_entity_bounds(world, entity, points));
    CF_Aabb fit = render_scene3d_fit(points, 8, 1);
    render_scene3d_camera camera = render_scene3d_camera_make(
        (fit.min.x + fit.max.x) * .5f,
        (fit.min.y + fit.max.y) * .5f,
        fit.max.x - fit.min.x,
        fit.max.y - fit.min.y
    );
    REQUIRE(render(&camera, first));
    float extent = 0;
    int covered = 0;
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            if (first[y * SIDE + x].colors.r <= 20) {
                continue;
            }
            covered++;
            extent = fmaxf(
                extent,
                fmaxf(fabsf(2 * (x + .5f) / SIDE - 1), fabsf(2 * (y + .5f) / SIDE - 1))
            );
        }
    }
    REQUIRE(covered > 500);
    REQUIRE(extent > .78f && extent < .85f);
    return true;
}

/* `inside` samples draw and pick the entity; the rest, inside its box bounds
 * but outside its outline, do neither. */
static bool outline_matches(
    const render_scene3d_camera *camera,
    ecs_entity entity,
    CF_V3 sample,
    bool inside,
    int lit
)
{
    int light = brightness(first, camera, sample);
    bool picked = picks(camera, project(camera, sample), entity);
    if (picked == inside && (inside ? light > lit : light == 0)) {
        return true;
    }
    fprintf(stderr, "outline mismatch: picked %d, brightness %d\n", picked, light);
    return false;
}

TEST_CASE(prisms_draw_and_pick_their_heading)
{
    ecs_entity entity = add_shape(PRIMITIVE_TRIANGLE_PRISM, (CF_V3){ 0 }, 0xffffffffu);
    *Geometry3D_get(world, entity) = (Geometry3D){ PRIMITIVE_TRIANGLE_PRISM, 4, 3, 1, .4f };
    turn(entity, 0);
    render_scene3d_camera camera = vertical_camera(8);
    /* Wide at the back and narrow at the +X tip. */
    const CF_V3 samples[] = {
        { -1.2f, .9f, .75f },
        { 1.2f, .9f, 0 },
        { 1.2f, .9f, .9f },
        { 1.2f, .9f, -.9f },
    };
    for (int step = 0; step < 8; step++) {
        float angle = step * EIGHTH_TURN;
        Orientation_get(world, entity)->radians = angle;
        REQUIRE(render(&camera, first));
        CF_V3 corners[8];
        CF_Aabb bounds;
        REQUIRE(render_scene3d_entity_bounds(world, entity, corners));
        REQUIRE(render_scene3d_screen_bounds(&camera, corners, &bounds));
        for (int i = 0; i < 4; i++) {
            CF_V3 point = turned(samples[i], angle);
            CF_V2 ndc = project(&camera, point);
            REQUIRE(ndc.x > bounds.min.x && ndc.x < bounds.max.x);
            REQUIRE(ndc.y > bounds.min.y && ndc.y < bounds.max.y);
            REQUIRE(outline_matches(&camera, entity, point, i < 2, 60));
        }
    }
    /* The underside closes the extrusion with an outward normal. */
    camera = vertical_camera(-8);
    REQUIRE(render(&camera, first));
    REQUIRE(brightness(first, &camera, (CF_V3){ 0, -.1f, 0 }) > 40);
    return true;
}

/* Overhead, below and front views see a closed eight-faced solid, not a flat
 * diamond. */
TEST_CASE(octahedra_draw_and_pick_a_closed_solid)
{
    ecs_entity entity = add_shape(PRIMITIVE_OCTAHEDRON, (CF_V3){ 0 }, 0xffffffffu);
    *Geometry3D_get(world, entity) = (Geometry3D){ PRIMITIVE_OCTAHEDRON, 4, 3, 2, 0 };
    turn(entity, 0);
    /* The last two of each lie inside the box but outside the diamond. */
    static const CF_V3 overhead_samples[] = {
        { -1.2f, 0, 0 },
        { 0, 0, .9f },
        { 1.3f, 0, .95f },
        { -1.3f, 0, -.95f },
    };
    static const CF_V3 front_samples[] = {
        { -1.2f, 0, 0 },
        { 0, .6f, 0 },
        { 1.3f, .7f, 0 },
        { -1.3f, -.7f, 0 },
    };
    for (int view = 0; view < 10; view++) {
        float angle = view < 8 ? view * EIGHTH_TURN : 0;
        Orientation_get(world, entity)->radians = angle;
        render_scene3d_camera camera = view < 8  ? vertical_camera(8)
                                     : view == 8 ? vertical_camera(-8)
                                                 : front_camera();
        REQUIRE(render(&camera, first));
        const CF_V3 *samples = view == 9 ? front_samples : overhead_samples;
        for (int i = 0; i < 4; i++) {
            REQUIRE(outline_matches(&camera, entity, turned(samples[i], angle), i < 2, 40));
        }
    }
    return true;
}

/* Flat and hollow polygons agree between their mesh and exact picking: the
 * chevron's rear notch, the frame's and outline's holes stay empty. */
TEST_CASE(polygons_draw_and_pick_the_same_outline)
{
    render_scene3d_camera camera = vertical_camera(8);
    const int primitives[] = { PRIMITIVE_CHEVRON_PYRAMID,
                               PRIMITIVE_BOX_FRAME,
                               PRIMITIVE_TRIANGLE_OUTLINE,
                               PRIMITIVE_TRIANGLE };
    const CF_V3 solid[] = { { 0 }, { .88f, .88f, 0 }, { 1.0f / 3 - .04f, 0, .44f }, { 0 } };
    const CF_V3 hole[] = { { -.95f, 0, 0 }, { 0 }, { 0 } };
    for (int i = 0; i < 4; i++) {
        fresh_world();
        ecs_entity entity = add_shape(primitives[i], (CF_V3){ 0 }, 0xffffffffu);
        if (primitives[i] >= PRIMITIVE_TRIANGLE_OUTLINE) {
            Geometry3D_get(world, entity)->height = .1f;
        }
        REQUIRE(render(&camera, first));
        REQUIRE(brightness(first, &camera, solid[i]) > 40);
        REQUIRE(picks(&camera, project(&camera, solid[i]), entity));
        if (i < 3) {
            REQUIRE(!picks_any(&camera, project(&camera, hole[i])));
            REQUIRE(brightness(first, &camera, hole[i]) == 0);
        }
    }
    return true;
}

/* A box frame broken into its twelve part entities looks and picks as the
 * frame did. */
TEST_CASE(box_frame_parts_match_their_frame)
{
    const unsigned color = 0xf07888ffu;
    ecs_entity source = add_shape(PRIMITIVE_BOX_FRAME, (CF_V3){ 0 }, color);
    Geometry3D shape = { .primitive = PRIMITIVE_BOX_FRAME,
                         .width = 4,
                         .height = 2,
                         .depth = 3 };
    *Geometry3D_get(world, source) = shape;
    VisualTransform visual = { .pitch = .31f, .yaw = .22f, .roll = .5f, .scale = .8f };
    const float heading = .35f;
    add(source, &COMPONENT_VisualTransform, &visual);
    turn(source, heading);
    render_scene3d_camera camera = front_camera();
    REQUIRE(render(&camera, first));
    bool hits[625];
    for (int i = 0; i < 625; i++) {
        hits[i] = picks_any(&camera, (CF_V2){ (i % 25 - 12) * .04f, (i / 25 - 12) * .04f });
    }
    CF_M4x4 frame = cf_m4_from_trs(
        (CF_V3){ 0 },
        visual_rotation(heading, (CF_V3){ visual.pitch, visual.yaw, visual.roll }),
        (CF_V3){ visual.scale, visual.scale, visual.scale }
    );
    assert(ecs_world_destroy_entity(world, source) == ECS_OK);
    for (int i = 0; i < visual_part_count(shape.primitive); i++) {
        visual_part part = visual_part_at(shape, i);
        CF_V3 center = cf_m4_transform_point(frame, part.center);
        ecs_entity piece = add_shape(part.primitive, center, color);
        *Geometry3D_get(world, piece) = (Geometry3D){ .primitive = part.primitive,
                                                      .width = part.dimensions.x,
                                                      .height = part.dimensions.y,
                                                      .depth = part.dimensions.z,
                                                      .elevation = center.y };
        add(piece, &COMPONENT_VisualTransform, &visual);
        turn(piece, heading);
    }
    REQUIRE(render(&camera, second));
    REQUIRE(changed_pixels(first, second) < 20);
    for (int i = 0; i < 625; i++) {
        REQUIRE(
            picks_any(&camera, (CF_V2){ (i % 25 - 12) * .04f, (i / 25 - 12) * .04f }) ==
            hits[i]
        );
    }
    return true;
}

TEST_CASE(repeated_primitives_share_instanced_draws)
{
    render_scene3d_camera camera = front_camera();
    const int repeated[] = { PRIMITIVE_BOX, PRIMITIVE_TRIANGLE_PRISM, PRIMITIVE_OCTAHEDRON };
    for (int p = 0; p < (int)(sizeof repeated / sizeof *repeated); p++) {
        fresh_world();
        for (int i = 0; i < 256; i++) {
            ecs_entity entity = ecs_world_create_entity(world);
            add(entity, &COMPONENT_Transform, &(Transform){ (i % 16 - 7.5f) * .3f, 0 });
            add(entity,
                &COMPONENT_Geometry3D,
                &(Geometry3D){ repeated[p], .2f, .2f, .2f, (i / 16 - 7.5f) * .3f });
            add(entity, &COMPONENT_Material, &(Material){ .scene = 0x4080ffffu });
        }
        cf_draw3d_stats(); /* Reading resets the counts, so earlier draws drop out. */
        REQUIRE(render(&camera, first));
        CF_DrawStats3d stats = cf_draw3d_stats();
        REQUIRE(stats.instances == 256);
        REQUIRE(stats.commands <= 4);
    }
    return true;
}

void test_scene3d_shapes()
{
    test_open_hidden_app("scene3d shapes", SIDE, SIDE);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(overhead_cameras_center_and_map_the_ground);
    RUN_TEST_CASE(picks_find_the_nearest_solid_of_each_primitive);
    RUN_TEST_CASE(turned_bounds_fit_frame_and_clip);
    RUN_TEST_CASE(flat_triangles_turn_about_their_area_center);
    RUN_TEST_CASE(every_solid_primitive_draws);
    RUN_TEST_CASE(repeated_primitives_share_instanced_draws);
    RUN_TEST_CASE(nearer_solids_win_in_any_order);
    RUN_TEST_CASE(fitted_bounds_frame_what_draws);
    RUN_TEST_CASE(prisms_draw_and_pick_their_heading);
    RUN_TEST_CASE(octahedra_draw_and_pick_a_closed_solid);
    RUN_TEST_CASE(polygons_draw_and_pick_the_same_outline);
    RUN_TEST_CASE(box_frame_parts_match_their_frame);
    pu_clear_setup();
    cf_destroy_app();
}
