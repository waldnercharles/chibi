#include "authoring/shader_prepare.h"
#include "hidden_app.h"
#include "hrc_cases.h"
#include "material_graphs.h"
#include "memory_images.h"
#include "render/fluid_raster.h"
#include "render/material_raster.h"
#include "render/radiance.h"
#include "render/reflections.h"
#include "render/shader_catalog.h"
#include "render/shader_fragment.h"
#include "render/shader_parameters.h"
#include "render/shader_program.h"
#include "runtime/component_catalog.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* What the material raster draws for HRC from graph materials and fluids, the
 * light HRC gives fluids, and crystal reflections, in one hidden app. */
enum
{
    SIDE = 128,
    PIXELS = SIDE * SIDE,
    /* The side of the graph material cases. */
    SMALL = 16
};

static const char *const TINT = "Tinted material", *const CIRCLE = "Circle shape",
                         *const PARAMETERS = "Parameter material", *const RADIUS = "Radius shape";

/* ---------- Material parameters, without drawing ---------- */

/* Set Material Parameter overrides one entity's parameter until its world is
 * cleared. Other entities keep their binding's text, and text edited in place
 * applies at once without touching other bindings. */
TEST_CASE(material_parameters_override_one_entity_until_cleared)
{
    REQUIRE(test_register_tint(TINT, (float[3]){ 1, 0, 0 }));
    int id = render_shader_find(TINT);
    const char *green = "(array)Tint 0 1 0\n";
    char edited[] = "(array)Tint 1 0 0\n";
    shader_error error = { 0 };
    REQUIRE(render_shader_parameters_validate(id, green, &error));
    REQUIRE(render_shader_parameters_validate(id, edited, &error));
    ecs_world *world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &COMPONENT_Shader, NULL) == ECS_OK);
    ecs_entity first = ecs_world_create_entity(world),
               second = ecs_world_create_entity(world);
    Shader binding = { .material = TINT, .parameters = green };
    REQUIRE(ecs_world_record_add(world, first, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(world, second, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    float override[3] = { 0, 0, 1 };
    pin_value inputs[] = {
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = ecs_entity_to_u64(first) },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "Tint", .length = sizeof("Tint") - 1 } },
        { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = override }
    };
    REQUIRE(!RENDER_MATERIAL_PARAMETER_CALL.invoke(world, inputs, NULL));
    float a[SHADER_PARAMETER_MAX][4], b[SHADER_PARAMETER_MAX][4];
    render_shader_parameters(id, world, first, binding.parameters, a);
    render_shader_parameters(id, world, second, binding.parameters, b);
    REQUIRE(a[0][2] == 1 && a[0][1] == 0 && b[0][1] == 1 && b[0][2] == 0);
    render_shader_clear_world(world);
    render_shader_parameters(id, world, first, binding.parameters, a);
    REQUIRE(a[0][1] == 1 && a[0][2] == 0);
    render_shader_parameters(id, world, second, edited, b);
    REQUIRE(b[0][0] == 1 && b[0][1] == 0);
    memcpy(edited, "(array)Tint 0 0 1\n", sizeof edited);
    REQUIRE(render_shader_parameters_validate(id, edited, &error));
    render_shader_parameters(id, world, second, edited, b);
    REQUIRE(b[0][0] == 0 && b[0][2] == 1);
    render_shader_parameters(id, world, first, binding.parameters, a);
    REQUIRE(a[0][1] == 1 && a[0][2] == 0);
    ecs_world_free(world);
    return true;
}

/* ---------- Material and fluid pixels, in a hidden app ---------- */

static render_material_raster *raster;
static render_radiance *radiance;
static CF_Canvas canvas;
static uint16_t halves[PIXELS * 4];

static void setup()
{
    raster = render_material_raster_new();
    assert(raster);
    assert(render_material_raster_resize(raster, SIDE, SIDE));
}

static void teardown()
{
    render_material_raster_free(raster);
}

/* Draws `count` entities into the raster alone. */
static bool raster_only(const render_material_entity *entities, int count, render_view_2d view)
{
    cf_app_update(NULL);
    bool drawn = render_material_raster_draw(raster, entities, count, view);
    cf_app_draw_onto_screen(false);
    return drawn;
}

/* Reads `side` x `side` pixels of one of the raster's textures into `halves`. */
static bool read_raster(CF_Texture texture, int side)
{
    return test_read_texture(texture, halves, side * side);
}

/* A channel of pixel (x, y) of the texture read last, `width` pixels wide. */
static float texel(int width, int x, int y, int channel)
{
    return test_half(halves[4 * (y * width + x) + channel]);
}

/* A world-space material samples each pixel's world x: unframed, where the
 * pixel is now, and framed, where the pixel sat in the frame the material was
 * laid out in, here turned half a turn and then a quarter turn. */
TEST_CASE(world_space_materials_follow_their_layout_frame)
{
    REQUIRE(test_register_world_step("World step"));
    render_material_entity entity = test_emitter();
    entity.pos[0] = entity.pos[1] = 0;
    entity.extent[0] = entity.extent[1] = 8;
    entity.radius = 6;
    entity.border_thickness = 0;
    entity.reserved[0] = (float)render_shader_find("World step");
    const float frames[3][4] = { { 0 }, { 0, 0, -1, 0 }, { 0, 0, 0, 1 } };
    /* Red at the left, right, top and bottom samples. */
    const bool red[3][4] = { { 0, 1, 1, 1 }, { 1, 0, 0, 0 }, { 1, 1, 0, 1 } };
    const int samples[4][2] = { { 4, 8 }, { 12, 8 }, { 8, 4 }, { 8, 12 } };
    REQUIRE(render_material_raster_resize(raster, SMALL, SMALL));
    for (int f = 0; f < 3; f++) {
        memcpy(entity.material_frame, frames[f], sizeof entity.material_frame);
        REQUIRE(raster_only(&entity, 1, test_centered(SMALL)));
        REQUIRE(read_raster(render_material_raster_textures(raster).scene, SMALL));
        /* Unframed, the top and bottom samples sit on the step's edge. */
        for (int i = 0; i < (f ? 4 : 2); i++) {
            REQUIRE((texel(SMALL, samples[i][0], samples[i][1], 0) > .5f) == red[f][i]);
        }
    }
    return true;
}

/* Draws `program` for `entity` into a small canvas and reads it back. */
static bool draw_fragment(
    render_programs *programs,
    int program,
    const render_material_entity *entity,
    CF_Pixel *out
)
{
    CF_Canvas target = cf_make_canvas(cf_canvas_defaults(SMALL, SMALL));
    assert(target.id);
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SMALL, SMALL));
    render_shader_fragment_draw(programs, program, entity, (CF_V2){ 1, 1 });
    cf_render_to(target, true);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    bool read = test_read_canvas(target, out, SMALL * SMALL * (int)sizeof *out);
    cf_destroy_canvas(target);
    return read;
}

/* A box cut to a shape graph's circle shows its graph material's parameter
 * default through the raster and through its own fragment program, and
 * registering the material again replaces the color in place. */
TEST_CASE(graph_materials_draw_their_parameters_in_their_shapes)
{
    REQUIRE(test_register_tint(TINT, (float[3]){ 1, 0, 0 }));
    REQUIRE(test_register_circle_shape(CIRCLE, 4));
    int id = render_shader_find(TINT);
    Shader shape = { .shape = CIRCLE };
    REQUIRE(
        render_shader_contains(&shape, NULL, (ecs_entity){ 0 }, (float[2]){ 0 }, (float[2]){ 0 }, 0)
    );
    REQUIRE(!render_shader_contains(
        &shape,
        NULL,
        (ecs_entity){ 0 },
        (float[2]){ 6, 0 },
        (float[2]){ 6, 0 },
        0
    ));
    render_material_entity entity = test_emitter();
    entity.pos[0] = entity.pos[1] = 0;
    entity.extent[0] = entity.extent[1] = 8;
    entity.shape_type = RENDER_SHAPE_BOX;
    entity.points[0] = entity.points[1] = 8;
    entity.reserved[0] = (float)id;
    entity.reserved[1] = (float)render_shader_find(CIRCLE);
    REQUIRE(render_material_raster_resize(raster, SMALL, SMALL));
    render_view_2d view = { .min_x = -8, .max_y = 8, .width = SMALL, .height = SMALL };
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            REQUIRE(test_register_tint(TINT, (float[3]){ 0, 0, 1 }));
            REQUIRE(render_shader_find(TINT) == id);
        }
        render_shader_parameters(id, NULL, (ecs_entity){ 0 }, NULL, entity.material_parameters);
        REQUIRE(raster_only(&entity, 1, view));
        REQUIRE(read_raster(render_material_raster_textures(raster).scene, SMALL));
        REQUIRE(texel(SMALL, 8, 8, pass ? 2 : 0) > .9f);
        /* The box's corner lies outside the circle. */
        REQUIRE(texel(SMALL, 0, 0, 3) < .01f);
    }
    render_programs *fragment = render_programs_new(RENDER_PROGRAM_FRAGMENT);
    shader_error error = { 0 };
    int program = render_shader_fragment_prepare(fragment, &entity, &error);
    CF_Pixel drawn[SMALL * SMALL];
    bool read = program && draw_fragment(fragment, program, &entity, drawn);
    render_programs_free(fragment);
    REQUIRE(read && drawn[8 * SMALL + 8].colors.b > 200 && drawn[0].colors.a < 5);
    return true;
}

/* The components of a mesh actor. */
enum
{
    MESH_PARTS = 5
};
static const ecs_component_desc *const MESH_COMPONENTS[MESH_PARTS] = {
    &COMPONENT_Geometry3D,
    &COMPONENT_VisualTransform,
    &COMPONENT_Orientation,
    &COMPONENT_MeshSurface2D,
    &COMPONENT_Material
};

/* A mesh actor: a blue box 12 units square and 2 tall, drawn as a 2D surface. */
static ecs_entity mesh_actor(ecs_world *world)
{
    ecs_entity e = ecs_world_create_entity(world);
    Geometry3D geometry = { .primitive = PRIMITIVE_BOX, .width = 12, .depth = 12, .height = 2 };
    VisualTransform visual = { .scale = 1 };
    Orientation orientation = { 0 };
    MeshSurface2D surface = { true };
    Material material = { .scene = 0x0000ffff,
                          .albedo = 0x0000ffff,
                          .absorption = 1,
                          .edge_light = 1 };
    const void *values[MESH_PARTS] = { &geometry, &visual, &orientation, &surface, &material };
    for (int i = 0; i < MESH_PARTS; i++) {
        assert(ecs_world_record_add(world, e, MESH_COMPONENTS[i]->id, values[i], 0) == ECS_OK);
    }
    assert(ecs_world_apply_commands(world) == ECS_OK);
    return e;
}

/* An opaque box of `color` at x, 20 units square. */
static render_material_entity plain_box(float x, const float color[3])
{
    return (render_material_entity){ .inv_rot = { 1, 0, 0, 1 },
                                     .pos = { x, 0 },
                                     .extent = { 13, 13 },
                                     .shape_type = RENDER_SHAPE_BOX,
                                     .aa = 1,
                                     .points = { 10, 10 },
                                     .scene = { color[0], color[1], color[2], 1 },
                                     .albedo = { color[0], color[1], color[2], 1 },
                                     .absorption = { 1, 1, 1, 1 } };
}

/* A box at x drawn by the parameter material with `material` and cut to the
 * radius shape with `shape`, both prepared. */
static render_material_entity graph_box(float x, const char *material, const char *shape)
{
    render_material_entity e = plain_box(x, (float[3]){ 1, 1, 1 });
    int ids[2] = { render_shader_find(PARAMETERS), render_shader_find(RADIUS) };
    e.reserved[0] = (float)ids[0];
    e.reserved[1] = (float)ids[1];
    render_shader_parameters(ids[0], NULL, (ecs_entity){ 0 }, material, e.material_parameters);
    render_shader_parameters(ids[1], NULL, (ecs_entity){ 0 }, shape, e.shape_parameters);
    return e;
}

/* A marker that draws mesh actor `actor` at its place in the layers. */
static render_material_entity mesh_marker(int actor)
{
    return (render_material_entity){ .shape_type = RENDER_SHAPE_MESH,
                                     .points = { (float)actor } };
}

enum
{
    UPLOAD_WIDTH = 96,
    UPLOAD_HEIGHT = 64
};

/* Draws `count` layers and both mesh actors of `world` into the raster, one
 * world unit per pixel with world x 0 at pixel 48 and y 0 at row 32. */
static bool draw_mixed(
    const render_material_entity *layers,
    int count,
    ecs_world *world,
    const render_mesh_material_actor *actors
)
{
    render_view_2d view = { .min_x = -48,
                            .max_y = 32,
                            .width = UPLOAD_WIDTH,
                            .height = UPLOAD_HEIGHT };
    cf_app_update(NULL);
    bool drawn = render_material_raster_draw_mixed(raster, layers, count, view, world, actors, 2, 0);
    cf_app_draw_onto_screen(false);
    return drawn;
}

/* Whether the last draw uploaded `entities` records and `parameters` blocks
 * of graph parameters, and nothing more. */
static bool uploaded(int entities, int parameters)
{
    render_material_upload_work work = render_material_raster_upload_work(raster);
    size_t record = offsetof(render_material_entity, material_parameters),
           block = offsetof(render_material_entity, layer) - record;
    return work.entity_records == entities && work.parameter_blocks == parameters &&
           work.entity_bytes == (size_t)entities * record &&
           work.parameter_bytes == (size_t)parameters * block &&
           work.parameter_buffer_bytes >= work.parameter_bytes;
}

/* Whether pixel (x, y) of `texture` holds `expected`, to within .03. */
static bool holds(CF_Texture texture, int x, int y, const float expected[4])
{
    REQUIRE(test_read_texture(texture, halves, UPLOAD_WIDTH * UPLOAD_HEIGHT));
    for (int c = 0; c < 4; c++) {
        float value = texel(UPLOAD_WIDTH, x, y, c);
        REQUIRE(isfinite(value) && fabsf(value - expected[c]) <= .03f);
    }
    return true;
}

/* Mesh markers split the raster's batches in draw order but upload nothing.
 * Graph materials between them keep their own parameters in all five
 * targets, and draws of markers, plain boxes or nothing upload only what they
 * hold. */
TEST_CASE(mixed_batches_upload_only_what_they_draw)
{
    REQUIRE(test_register_parameters(PARAMETERS) && test_register_radius_shape(RADIUS, 12));
    const char *materials[] = { "(array)Tint 0 1 0\n"
                                "(array)Glow 0.1 0.2 0.3\n"
                                "Roughness 0.2\n"
                                "Height 2\n",
                                "(array)Tint 0.7 0 0.6\n"
                                "(array)Glow 0.4 0.5 0.6\n"
                                "Roughness 0.4\n"
                                "Height 4\n",
                                "(array)Tint 0 0.5 1\n"
                                "(array)Glow 0.7 0.8 0.9\n"
                                "Roughness 0.6\n"
                                "Height 6\n" };
    const char *shapes[] = { "Radius 10\n", "Radius 10\n", "Radius 6\n" };
    shader_error error = { 0 };
    for (int i = 0; i < 3; i++) {
        REQUIRE(
            render_shader_parameters_validate(render_shader_find(PARAMETERS), materials[i], &error)
        );
        REQUIRE(
            render_shader_parameters_validate(render_shader_find(RADIUS), shapes[i], &error)
        );
    }
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < MESH_PARTS; i++) {
        REQUIRE(ecs_world_register_component(world, MESH_COMPONENTS[i], NULL) == ECS_OK);
    }
    render_mesh_material_actor actors[] = {
        { .entity = mesh_actor(world), .position = { -12, 0 } },
        { .entity = mesh_actor(world), .position = { 16, 0 } }
    };
    render_material_entity layers[] = { plain_box(-32, (float[3]){ 1, 0, 0 }),
                                        graph_box(-12, materials[0], shapes[0]),
                                        mesh_marker(0),
                                        graph_box(0, materials[1], shapes[1]),
                                        plain_box(16, (float[3]){ 1, 1, 0 }),
                                        mesh_marker(1),
                                        graph_box(24, materials[2], shapes[2]) };
    const float red[4] = { 1, 0, 0, 1 }, blue[4] = { 0, 0, 1, 1 },
                yellow[4] = { 1, 1, 0, 1 };
    REQUIRE(render_material_raster_resize(raster, UPLOAD_WIDTH, UPLOAD_HEIGHT));
    REQUIRE(draw_mixed(layers, 7, world, actors) && uploaded(5, 3));
    render_material_textures t = render_material_raster_textures(raster);
    /* The plain box before the first marker, the first mesh over the graph
     * box before it, the plain box after the marker and the second mesh. */
    REQUIRE(holds(t.scene, 16, 32, red) && holds(t.scene, 32, 32, blue));
    REQUIRE(holds(t.scene, 64, 24, yellow) && holds(t.scene, 64, 32, blue));
    /* Each graph box where it is on top. */
    const int xs[] = { 28, 40, 68 };
    const float colors[3][4] = { { 0, 1, 0, 1 }, { .7f, 0, .6f, 1 }, { 0, .5f, 1, 1 } },
                glows[3][4] = { { .1f, .2f, .3f, 1 },
                                { .4f, .5f, .6f, 1 },
                                { .7f, .8f, .9f, 1 } };
    for (int i = 0; i < 3; i++) {
        float albedo[4] = { colors[i][0], colors[i][1], colors[i][2], 2 + 2 * i },
              normal[4] = { 0, 0, 0, .8f - .2f * i };
        REQUIRE(holds(t.scene, xs[i], 32, colors[i]) && holds(t.source, xs[i], 32, glows[i]));
        REQUIRE(holds(t.albedo, xs[i], 32, albedo) && holds(t.normal, xs[i], 32, normal));
        REQUIRE(holds(t.cover, xs[i], 32, (float[4]){ 1, 1, 0, 0 }));
    }
    /* The first graph box after its mesh now covers it. */
    render_material_entity swapped = layers[1];
    layers[1] = layers[2];
    layers[2] = swapped;
    REQUIRE(draw_mixed(layers, 7, world, actors) && uploaded(5, 3));
    REQUIRE(holds(render_material_raster_textures(raster).scene, 36, 32, colors[0]));
    /* A leading marker uploads nothing, and the graph box after it, shrunk to
     * radius 3, covers the mesh only inside its circle. */
    render_material_entity leading[] = { layers[1], layers[2] };
    leading[1].shape_parameters[0][0] = 3;
    REQUIRE(draw_mixed(leading, 2, world, actors) && uploaded(1, 1));
    REQUIRE(holds(render_material_raster_textures(raster).scene, 31, 32, blue));
    REQUIRE(holds(render_material_raster_textures(raster).scene, 36, 32, colors[0]));
    render_material_entity markers[] = { layers[1], layers[5] };
    REQUIRE(draw_mixed(markers, 2, world, actors) && uploaded(0, 0));
    REQUIRE(holds(render_material_raster_textures(raster).scene, 36, 32, blue));
    REQUIRE(draw_mixed(layers, 1, world, actors) && uploaded(1, 0));
    REQUIRE(holds(render_material_raster_textures(raster).scene, 16, 32, red));
    REQUIRE(draw_mixed(NULL, 0, world, actors) && uploaded(0, 0));
    ecs_world_free(world);
    return true;
}

/* A 2x2 white image with maps whose texels differ: normals tilted right, up,
 * flat and left, and heights from 0 to 1 in thirds. */
static const unsigned char SPRITE_WHITE[16] = { 255, 255, 255, 255, 255, 255,
                                                255, 255, 255, 255, 255, 255,
                                                255, 255, 255, 255 },
                           SPRITE_NORMALS[16] = { 204, 128, 230, 255, 128, 204,
                                                  230, 255, 128, 128, 255, 255,
                                                  51,  128, 230, 255 },
                           SPRITE_HEIGHTS[16] = { 0,   0,   0,   255, 85, 0,
                                                  0,   255, 170, 0,   0,  255,
                                                  255, 0,   0,   255 };

/* An opaque sprite of the whole 2x2 image filling the small view. */
static render_material_entity map_sprite()
{
    return (render_material_entity){ .inv_rot = { 1, 0, 0, 1 },
                                     .extent = { 9, 9 },
                                     .shape_type = RENDER_SHAPE_SPRITE,
                                     .aa = 1,
                                     .points = { 8, 8, [10] = 1, [11] = 1 },
                                     .scene = { 1, 1, 1, 1 },
                                     .albedo = { 1, 1, 1, 1 },
                                     .absorption = { 1, 1, 1, 1 } };
}

/* Whether the texel the sprite draws at small pixel (x, y) tilts by (tx, ty)
 * and stands `height` above the plane, to within .02. */
static bool shaded(int x, int y, float tx, float ty, float height)
{
    render_material_textures t = render_material_raster_textures(raster);
    REQUIRE(read_raster(t.normal, SMALL));
    REQUIRE(
        fabsf(texel(SMALL, x, y, 0) - tx) < .02f && fabsf(texel(SMALL, x, y, 1) - ty) < .02f
    );
    REQUIRE(read_raster(t.albedo, SMALL));
    REQUIRE(fabsf(texel(SMALL, x, y, 3) - height) < .02f);
    return true;
}

/* A sprite's normal map tilts each texel in world axes, mirrored with the
 * frame and turned with the sprite, and its height map raises each texel by
 * the sprite's relief, which raises a sprite without one evenly. */
TEST_CASE(sprite_maps_tilt_and_raise_their_texels)
{
    static const test_image images[] = { { "white.png", 2, 2, SPRITE_WHITE },
                                         { "normals.png", 2, 2, SPRITE_NORMALS },
                                         { "heights.png", 2, 2, SPRITE_HEIGHTS } };
    test_mount_images(images, 3);
    render_material_entity sprite = map_sprite();
    sprite.points[12] = (float)test_sprite_variant("white.png", false);
    sprite.points[13] = (float)test_sprite_variant("normals.png", true);
    sprite.points[14] = (float)test_sprite_variant("heights.png", true);
    sprite.relief = 3;
    REQUIRE(render_material_raster_resize(raster, SMALL, SMALL));
    REQUIRE(raster_only(&sprite, 1, test_centered(SMALL)));
    REQUIRE(shaded(4, 4, .6f, 0, 0) && shaded(12, 4, 0, .6f, 1));
    REQUIRE(shaded(4, 12, 0, 0, 2) && shaded(12, 12, -.6f, 0, 3));
    /* Mirrored, the right column shows the left one tilted the other way. */
    render_material_entity mirrored = sprite;
    mirrored.points[8] = 1;
    mirrored.points[10] = 0;
    REQUIRE(raster_only(&mirrored, 1, test_centered(SMALL)));
    REQUIRE(shaded(4, 4, 0, .6f, 1) && shaded(12, 4, -.6f, 0, 0));
    /* A quarter turn counterclockwise takes the top left texel, tilted right,
     * to the bottom left, tilted up, and the top right one, tilted up, to the
     * top left, tilted left. */
    render_material_entity turned = sprite;
    turned.inv_rot[1] = 1;
    turned.inv_rot[2] = -1;
    turned.inv_rot[0] = turned.inv_rot[3] = 0;
    REQUIRE(raster_only(&turned, 1, test_centered(SMALL)));
    REQUIRE(shaded(4, 12, 0, .6f, 0) && shaded(4, 4, -.6f, 0, 1));
    render_material_entity even = sprite;
    even.points[13] = even.points[14] = 0;
    REQUIRE(raster_only(&even, 1, test_centered(SMALL)));
    REQUIRE(shaded(4, 4, 0, 0, 3) && shaded(12, 12, 0, 0, 3));
    /* Graph Height adds after the height map scales the sprite's relief. */
    REQUIRE(test_register_relief("SpriteRaised", (test_relief){ .height = 2 }));
    sprite.reserved[0] = (float)render_shader_find("SpriteRaised");
    REQUIRE(raster_only(&sprite, 1, test_centered(SMALL)));
    REQUIRE(read_raster(render_material_raster_textures(raster).albedo, SMALL));
    REQUIRE(fabsf(texel(SMALL, 4, 4, 3) - 2) < .02f);
    REQUIRE(fabsf(texel(SMALL, 12, 4, 3) - 3) < .02f);
    REQUIRE(fabsf(texel(SMALL, 4, 12, 3) - 4) < .02f);
    REQUIRE(fabsf(texel(SMALL, 12, 12, 3) - 5) < .02f);
    return true;
}

/* A shape stands its relief above the plane where it covers, and a
 * material's Height raises it further. */
TEST_CASE(shapes_stand_at_their_relief)
{
    REQUIRE(test_register_relief("Raised", (test_relief){ .height = 2 }));
    REQUIRE(render_material_raster_resize(raster, SMALL, SMALL));
    render_material_entity disc = { .inv_rot = { 1, 0, 0, 1 },
                                    .extent = { 5, 5 },
                                    .shape_type = RENDER_SHAPE_CIRCLE,
                                    .radius = 4,
                                    .aa = 1,
                                    .relief = 3,
                                    .scene = { 1, 1, 1, 1 },
                                    .albedo = { 1, 1, 1, 1 },
                                    .absorption = { 1, 1, 1, 1 } };
    REQUIRE(raster_only(&disc, 1, test_centered(SMALL)));
    REQUIRE(read_raster(render_material_raster_textures(raster).albedo, SMALL));
    REQUIRE(fabsf(texel(SMALL, 8, 8, 3) - 3) < .02f && fabsf(texel(SMALL, 1, 1, 3)) < .02f);
    render_material_entity box = plain_box(0, (float[3]){ 1, 1, 1 });
    box.relief = 5;
    REQUIRE(raster_only(&box, 1, test_centered(SMALL)));
    REQUIRE(read_raster(render_material_raster_textures(raster).albedo, SMALL));
    REQUIRE(fabsf(texel(SMALL, 8, 8, 3) - 5) < .02f);
    box.reserved[0] = (float)render_shader_find("Raised");
    REQUIRE(raster_only(&box, 1, test_centered(SMALL)));
    REQUIRE(read_raster(render_material_raster_textures(raster).albedo, SMALL));
    REQUIRE(fabsf(texel(SMALL, 8, 8, 3) - 7) < .02f);
    return true;
}

/* Every fluid case makes its fluid first, for teardown to free. */
static material_fluid *fluid;
static render_fluid_raster *fluid_raster;

static void fluid_setup()
{
    setup();
    fluid_raster = render_fluid_raster_new();
    assert(fluid_raster);
}

static void fluid_teardown()
{
    assert(fluid);
    material_fluid_free(fluid);
    fluid = NULL;
    render_fluid_raster_free(fluid_raster);
    teardown();
}

/* Draws `count` solids and the fluid over them into the raster in `view`. */
static bool draw_fluid(const render_material_entity *solids, int count, render_view_2d view)
{
    return render_material_raster_draw(raster, solids, count, view) &&
           render_fluid_raster_draw(
               fluid_raster,
               fluid,
               render_material_raster_canvas(raster),
               view,
               SIDE,
               SIDE
           );
}

/* draw_fluid() alone in a frame. */
static bool fluid_frame(render_view_2d view)
{
    cf_app_update(NULL);
    bool drawn = draw_fluid(NULL, 0, view);
    cf_app_draw_onto_screen(false);
    return drawn;
}

/* The game's physics is Y-down and the raster Y-up: a particle emits where
 * its flipped position falls, not where its unflipped one would, also after
 * the camera moves down. */
TEST_CASE(fluids_land_where_game_y_points_down)
{
    fluid = material_fluid_new(10, 1);
    fluid_particle particle = { .position = { 17, 23 },
                                .mass = 1,
                                .radius = 5,
                                .color = 0xff0000ffu,
                                .emission = 1 };
    REQUIRE(material_fluid_add(fluid, particle));
    for (int offset = 0; offset <= 16; offset += 16) {
        render_view_2d view = test_centered(SIDE);
        view.max_y -= offset;
        REQUIRE(fluid_frame(view));
        REQUIRE(read_raster(render_material_raster_textures(raster).source, SIDE));
        REQUIRE(texel(SIDE, 81, 87 - offset, 0) > .5f);
        REQUIRE(texel(SIDE, 81, 41 - offset, 0) < .01f);
    }
    return true;
}

/* A white gas particle of radius 6 at (x, .5) that emits. */
static fluid_particle gas(float x)
{
    return (fluid_particle){ .position = { x, .5f },
                             .mass = 1,
                             .radius = 6,
                             .color = 0xffffffffu,
                             .emission = 1,
                             .style = { .blend = 4 } };
}

/* A white particle of radius 12 at (x, .5) that emits nothing. */
static fluid_particle volume(float x)
{
    return (
        fluid_particle
    ){ .position = { x, .5f }, .mass = 1, .radius = 12, .color = 0xffffffffu };
}

/* Replaces the fluid's particles and draws them alone. */
static bool draw_particles(const fluid_particle *particles, int count)
{
    material_fluid_clear(fluid);
    for (int i = 0; i < count; i++) {
        REQUIRE(material_fluid_add(fluid, particles[i]));
    }
    return fluid_frame(test_centered(SIDE));
}

/* Two gas particles 13 units apart join into one emitting volume, and
 * authored shrinking parts them again. */
TEST_CASE(gas_particles_join_until_they_shrink)
{
    fluid = material_fluid_new(20, 2);
    fluid_particle pair[] = { gas(-6.5f), gas(6.5f) };
    REQUIRE(draw_particles(pair, 2));
    REQUIRE(read_raster(render_material_raster_textures(raster).source, SIDE));
    REQUIRE(texel(SIDE, 64, 64, 0) > .9f && texel(SIDE, 61, 64, 0) > .9f);
    for (int i = 0; i < 2; i++) {
        pair[i].lifetime = 1;
        pair[i].style.duration = 2;
        pair[i].style.size_exponent = 1;
        pair[i].style.end_color = pair[i].color;
    }
    REQUIRE(draw_particles(pair, 2));
    REQUIRE(read_raster(render_material_raster_textures(raster).source, SIDE));
    REQUIRE(texel(SIDE, 64, 64, 0) < .01f);
    return true;
}

/* A liquid that emits nothing has a crisp edge, adds no emission and shows
 * to the camera through its visibility floor, which goes with it. */
TEST_CASE(liquids_show_crisp_unlit_edges)
{
    fluid = material_fluid_new(20, 2);
    fluid_particle liquid = volume(.5f);
    liquid.kind = FLUID_LIQUID;
    liquid.visibility = .06f;
    REQUIRE(draw_particles(&liquid, 1));
    REQUIRE(read_raster(render_material_raster_textures(raster).scene, SIDE));
    REQUIRE(texel(SIDE, 61, 64, 0) > .9f && texel(SIDE, 71, 64, 0) < .01f);
    REQUIRE(
        read_raster(render_material_raster_textures(raster).source, SIDE) &&
        texel(SIDE, 64, 64, 0) == 0
    );
    REQUIRE(
        read_raster(render_material_raster_textures(raster).visibility, SIDE) &&
        texel(SIDE, 64, 64, 0) > .01f
    );
    material_fluid_clear(fluid);
    REQUIRE(fluid_frame(test_centered(SIDE)));
    REQUIRE(
        read_raster(render_material_raster_textures(raster).visibility, SIDE) &&
        texel(SIDE, 64, 64, 0) == 0
    );
    return true;
}

/* Burning fuel glows as small separate cores, while the exhaust between them
 * still emits. */
TEST_CASE(burning_fuel_glows_in_separate_cores)
{
    fluid = material_fluid_new(20, 2);
    fluid_particle pair[] = { gas(-6.5f), gas(6.5f) };
    for (int i = 0; i < 2; i++) {
        pair[i].burning = true;
        pair[i].glow = 1;
        pair[i].style = (fluid_style){ 0 };
    }
    REQUIRE(draw_particles(pair, 2));
    REQUIRE(read_raster(render_material_raster_textures(raster).source, SIDE));
    REQUIRE(texel(SIDE, 64, 64, 0) < .01f && texel(SIDE, 70, 64, 0) > .9f);
    return true;
}

/* Two overlapping volumes join into one plume without particle-sized holes or
 * bright beads, and end where the particles do. */
TEST_CASE(joined_plumes_have_no_holes_or_beads)
{
    fluid = material_fluid_new(20, 2);
    fluid_particle pair[] = { volume(-3.5f), volume(4.5f) };
    REQUIRE(draw_particles(pair, 2));
    REQUIRE(read_raster(render_material_raster_textures(raster).scene, SIDE));
    float low = 1, high = 0;
    for (int x = 61; x <= 67; x++) {
        low = fminf(low, texel(SIDE, x, 64, 0));
        high = fmaxf(high, texel(SIDE, x, 64, 0));
    }
    REQUIRE(low > .08f && high < low * 1.6f);
    REQUIRE(texel(SIDE, 84, 64, 0) == 0);
    return true;
}

/* A lone cold parcel fades instead of surviving as a bright bead. */
TEST_CASE(lone_cold_parcels_fade)
{
    fluid = material_fluid_new(20, 2);
    fluid_particle parcel = volume(.5f);
    parcel.absorption = 1;
    REQUIRE(draw_particles(&parcel, 1));
    REQUIRE(read_raster(render_material_raster_textures(raster).scene, SIDE));
    REQUIRE(texel(SIDE, 64, 64, 0) < .04f);
    return true;
}

/* A moving parcel stretches into a wisp along its velocity, across tile
 * boundaries: its optical depth, source's alpha, reaches 17 pixels along the
 * motion and none across it. */
TEST_CASE(wisps_stretch_along_their_velocity)
{
    fluid = material_fluid_new(20, 2);
    for (int axis = 0; axis < 2; axis++) {
        fluid_particle parcel = volume(.5f);
        parcel.absorption = 1;
        parcel.velocity = axis ? (fluid_vec){ 0, 40 } : (fluid_vec){ 40, 0 };
        REQUIRE(draw_particles(&parcel, 1));
        REQUIRE(read_raster(render_material_raster_textures(raster).source, SIDE));
        float along = texel(SIDE, axis ? 64 : 81, axis ? 81 : 64, 3);
        float across = texel(SIDE, axis ? 81 : 64, axis ? 64 : 81, 3);
        REQUIRE(along > .02f && across == 0);
    }
    return true;
}

/* The lighting cases also light the fluid with HRC into the canvas. A new
 * HRC compiles all of HRC's shaders, so only they make one. */
static void light_setup()
{
    fluid_setup();
    radiance = render_radiance_new();
    canvas = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    assert(radiance && canvas.id);
    assert(render_radiance_resize(radiance, SIDE, SIDE));
}

static void light_teardown()
{
    cf_destroy_canvas(canvas);
    render_radiance_free(radiance);
    fluid_teardown();
}

/* Lights the raster's solids and fluid into the canvas for `frames` frames
 * from a reset, with `background` behind them. */
static bool light_fluid(const render_material_entity *solids, int count, CF_Texture background, int frames)
{
    render_radiance_reset(radiance);
    for (int frame = 0; frame < frames; frame++) {
        cf_app_update(NULL);
        bool drawn = draw_fluid(solids, count, test_centered(SIDE));
        render_radiance_inputs inputs = { .material = render_material_raster_textures(raster),
                                          .background = background };
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
        drawn = drawn &&
                render_radiance_draw(radiance, inputs, test_centered(SIDE), canvas);
        cf_draw_pop();
        cf_app_draw_onto_screen(false);
        REQUIRE(drawn);
    }
    return true;
}

/* The mean color over 5 x 5 pixels inside the fluid volume. */
static bool volume_color(float color[3])
{
    static CF_Pixel pixels[PIXELS];
    REQUIRE(test_read_canvas(canvas, pixels, (int)sizeof pixels));
    color[0] = color[1] = color[2] = 0;
    for (int y = 62; y <= 66; y++) {
        for (int x = 82; x <= 86; x++) {
            CF_Pixel p = pixels[y * SIDE + x];
            color[0] += p.colors.r / 25.f;
            color[1] += p.colors.g / 25.f;
            color[2] += p.colors.b / 25.f;
        }
    }
    return true;
}

/* The reference lighting without fog, feedback or adaptive steps, tracing
 * `trace_levels` cascades. */
static bool set_fluid_lighting(int trace_levels)
{
    HRC2D lighting = test_reference_lighting();
    render_radiance_lab lab = render_radiance_lab_defaults();
    lighting.adaptive = lighting.feedback = false;
    lab.trace_levels = trace_levels;
    lighting.fog = 0;
    return render_radiance_set_lighting(radiance, lighting, lab);
}

/* A volume of `kind` right of the wall's place takes the green or blue light
 * of the emitter left of it and emits none itself, and the wall shadows it. */
static bool volume_takes_light(int kind, int trace_levels)
{
    REQUIRE(set_fluid_lighting(trace_levels));
    material_fluid_clear(fluid);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            fluid_particle particle = { .position = { 11 + 6 * x, -9 + 6 * y },
                                        .mass = 1,
                                        .radius = 10,
                                        .kind = kind,
                                        .color = 0xffffffffu,
                                        .absorption = .02f,
                                        .visibility = kind == FLUID_GAS ? .02f : 0 };
            REQUIRE(material_fluid_add(fluid, particle));
        }
    }
    /* Dark, lit green, lit blue, then lit green behind the wall. */
    float color[4][3];
    for (int stage = 0; stage < 4; stage++) {
        render_material_entity solids[] = { test_emitter(), test_wall() };
        for (int c = 0; c < 3; c++) {
            bool lit = stage && c == (stage == 2 ? 2 : 1);
            solids[0].emission[c] = solids[0].border_emission[c] = lit ? .08f : 0;
        }
        REQUIRE(light_fluid(solids, stage == 3 ? 2 : 1, (CF_Texture){ 0 }, 8));
        REQUIRE(volume_color(color[stage]));
    }
    REQUIRE(color[1][1] > color[0][1] + 3 && color[2][2] > color[0][2] + 3);
    REQUIRE(color[1][1] > color[1][2] + 3 && color[2][2] > color[2][1] + 3);
    REQUIRE(color[3][1] < color[0][1] + (color[1][1] - color[0][1]) * .35f);
    REQUIRE(read_raster(render_material_raster_textures(raster).source, SIDE));
    for (int c = 0; c < 3; c++) {
        REQUIRE(halves[4 * (64 * SIDE + 84) + c] == 0);
    }
    return true;
}

/* Gas and liquid take HRC's light and shadow with one traced cascade and
 * with three. */
TEST_CASE(fluid_volumes_take_light_and_shadow)
{
    fluid = material_fluid_new(20, 16);
    for (int levels = 1; levels <= 3; levels += 2) {
        for (int kind = FLUID_GAS; kind <= FLUID_LIQUID; kind++) {
            REQUIRE(volume_takes_light(kind, levels));
        }
    }
    return true;
}

/* A refracting liquid bends the striped background behind it and leaves the
 * background around it as it was. */
TEST_CASE(liquids_refract_only_what_lies_behind_them)
{
    fluid = material_fluid_new(32, 1);
    static CF_Pixel stripes[PIXELS], images[2][PIXELS];
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            bool stripe = (x / 3) % 2;
            stripes[y * SIDE + x] = (CF_Pixel){
                .colors = { stripe ? 180 : 15, 0, stripe ? 15 : 180, 255 }
            };
        }
    }
    CF_Texture background = cf_make_texture(cf_texture_defaults(SIDE, SIDE));
    assert(background.id);
    cf_texture_update(background, stripes, sizeof stripes);
    REQUIRE(set_fluid_lighting(3));
    bool drawn = true;
    for (int pass = 0; pass < 2; pass++) {
        material_fluid_clear(fluid);
        fluid_particle liquid = { .position = { .5f, .5f },
                                  .mass = 1,
                                  .radius = 30,
                                  .kind = FLUID_LIQUID,
                                  .color = 0x8080ff99,
                                  .refraction = pass ? 12 : 0 };
        drawn = drawn && material_fluid_add(fluid, liquid) &&
                light_fluid(NULL, 0, background, 1) &&
                test_read_canvas(canvas, images[pass], (int)sizeof images[pass]);
    }
    cf_destroy_texture(background);
    REQUIRE(drawn);
    int changed = 0, outside = 0;
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            int d = abs(
                (int)images[0][y * SIDE + x].colors.r -
                images[1][y * SIDE + x].colors.r
            );
            if (x > 50 && x < 78 && y > 50 && y < 78) {
                changed += d > 8;
            }
            if (x < 45 || x > 83 || y < 45 || y > 83) {
                outside += d > 2;
            }
        }
    }
    REQUIRE(changed > 20 && outside == 0);
    return true;
}

/* ---------- Reflections, in a hidden app ---------- */

/* A crystal right of a red box above a green one shows them mirrored across
 * its vertical plane, each at its own height. */
TEST_CASE(crystals_mirror_the_scene_beside_them)
{
    CF_Canvas scene = cf_make_canvas(cf_canvas_defaults(SIDE, SIDE));
    render_reflections *reflections = render_reflections_new();
    assert(scene.id && reflections);
    cf_app_update(NULL);
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, SIDE, SIDE));
    cf_draw_push_color(cf_make_color_rgba_f(1, 0, 0, 1));
    cf_draw_box_fill((CF_Aabb){ { -30, 8 }, { -18, 16 } }, 0);
    cf_draw_pop_color();
    cf_draw_push_color(cf_make_color_rgba_f(0, 1, 0, 1));
    cf_draw_box_fill((CF_Aabb){ { -30, -16 }, { -18, -8 } }, 0);
    cf_draw_pop_color();
    cf_canvas_set_clear_color(scene, cf_make_color_rgba_f(0, 0, .1f, 1));
    cf_render_to(scene, true);
    render_reflection crystal = {
        .corners = { { 4, -24 }, { 40, -24 }, { 40, 24 }, { 4, 24 } },
        .plane = { 0, 0, 0, 1 },
        .tint = { 1, 1, 1, 1 },
        .strength = 1
    };
    render_view_2d view = { .min_x = -64, .max_y = 64, .width = SIDE, .height = SIDE };
    CF_Canvas output = render_reflections_draw(reflections, scene, SIDE, SIDE, view, &crystal, 1);
    cf_draw_pop();
    cf_app_draw_onto_screen(false);
    static CF_Pixel pixels[PIXELS];
    bool read = output.id && test_read_canvas(output, pixels, (int)sizeof pixels);
    render_reflections_free(reflections);
    cf_destroy_canvas(scene);
    REQUIRE(read);
    CF_Pixel red = pixels[52 * SIDE + 88], green = pixels[76 * SIDE + 88];
    REQUIRE(red.colors.r > 180 && red.colors.g < 30);
    REQUIRE(green.colors.g > 180 && green.colors.r < 30);
    return true;
}

void test_render_material_pixels()
{
    test_open_hidden_app("material pixels", SIDE, SIDE);
    RUN_TEST_CASE(material_parameters_override_one_entity_until_cleared);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(world_space_materials_follow_their_layout_frame);
    RUN_TEST_CASE(graph_materials_draw_their_parameters_in_their_shapes);
    RUN_TEST_CASE(mixed_batches_upload_only_what_they_draw);
    RUN_TEST_CASE(sprite_maps_tilt_and_raise_their_texels);
    RUN_TEST_CASE(shapes_stand_at_their_relief);
    pu_setup(fluid_setup, fluid_teardown);
    RUN_TEST_CASE(fluids_land_where_game_y_points_down);
    RUN_TEST_CASE(gas_particles_join_until_they_shrink);
    RUN_TEST_CASE(liquids_show_crisp_unlit_edges);
    RUN_TEST_CASE(burning_fuel_glows_in_separate_cores);
    RUN_TEST_CASE(joined_plumes_have_no_holes_or_beads);
    RUN_TEST_CASE(lone_cold_parcels_fade);
    RUN_TEST_CASE(wisps_stretch_along_their_velocity);
    pu_setup(light_setup, light_teardown);
    RUN_TEST_CASE(fluid_volumes_take_light_and_shadow);
    RUN_TEST_CASE(liquids_refract_only_what_lies_behind_them);
    pu_clear_setup();
    RUN_TEST_CASE(crystals_mirror_the_scene_beside_them);
    cf_destroy_app();
}
