#include "hidden_app.h"
#include "render/material_lod.h"
#include "render/scene_renderer.h"
#include "runtime/component_catalog.h"
#include "runtime/fields/program.h"
#include "runtime/materials.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum
{
    SIDE = 64,
    PIXELS = SIDE * SIDE
};
static ecs_world *world;
static particles *particle_state;
static entities *entity_state;
static materials *terrain_materials;
static engine *runtime;
static app_scene_renderer *renderer;
static engine_viewport *viewport;
static ecs_entity source;
static CF_Pixel pixels[PIXELS], fine_before[PIXELS], coarse_before[PIXELS],
    edited[PIXELS];
static unsigned char *checkpoint;
static size_t checkpoint_size;
static bool drawn;
static int frames;
static char error[256];

static void component(ecs_entity entity, const ecs_component_desc *desc, const void *data)
{
    assert(ecs_world_record_add(world, entity, desc->id, data, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
}
static void draw_scene(void *)
{
    drawn = app_scene_renderer_draw(renderer, world, particle_state, viewport, runtime, (render_skip){ 0 });
}
static void teardown()
{
    free(checkpoint);
    checkpoint = NULL;
    engine_viewport_free(viewport);
    app_scene_renderer_free(renderer);
    engine_free(runtime);
    materials_free(terrain_materials);
    entities_free(entity_state);
    particles_free(particle_state);
    ecs_world_free(world);
}
static void setup()
{
    world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    particle_state = particles_new();
    entity_state = entities_new(world, NULL);
    terrain_materials = materials_new(world, entity_state, particle_state);
    entities_bind_materials(entity_state, terrain_materials);
    runtime = engine_new(
        world,
        &(engine_modules){ .entities = entity_state,
                           .materials = terrain_materials,
                           .particles = particle_state }
    );
    renderer = app_scene_renderer_new();
    app_scene_renderer_set_view(renderer, true, true);
    viewport = engine_viewport_new(SIDE, SIDE);
    engine_viewport_set_render_callback(viewport, draw_scene, NULL);
    const field_ir code[] = { { .op = FIELD_X },
                              { .op = FIELD_Y },
                              { .op = FIELD_ADD, .count = 2, .inputs = { 0, 1 } },
                              { .op = FIELD_CONSTANT, .value = .125f },
                              { .op = FIELD_ADD, .count = 2, .inputs = { 2, 3 } } };
    field_program *program = field_program_new(code, 5, 4, NULL, 0, error, sizeof error);
    assert(
        program &&
        material_field_register(materials_fields(terrain_materials), "Diagonal", program)
    );
    source = ecs_world_create_entity(world);
    component(source, &COMPONENT_Transform, &(Transform){ 0 });
    component(
        source,
        &COMPONENT_Appearance,
        &(Appearance){ .color = 0xffffffffu, .border_color = 0x00ff00ffu, .border_width = 1 }
    );
    component(
        source,
        &COMPONENT_Material,
        &(Material){ .scene = 0xffffffffu,
                     .albedo = 0xffffffffu,
                     .border_scene = 0x00ff00ffu,
                     .border_albedo = 0x00ff00ffu }
    );
    component(
        source,
        &COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .width = 256,
                            .height = 128,
                            .cell_size = 1,
                            .field = "Diagonal",
                            .min_fragment_cells = 1,
                            .category = 1,
                            .mask = 1 }
    );
    assert(!materials_prepare(terrain_materials));
    frames = 0;
}
static bool save_pixels(const char *name, uint64_t hash)
{
    const char *directory = getenv("CHIBI_TERRAIN_SEAMS_OUTPUT");
    if (!directory || !*directory) {
        return true;
    }
    if (mkdir(directory, 0700) && errno != EEXIST) {
        return false;
    }
    char path[4096];
    int length = snprintf(path, sizeof path, "%s/%s.png", directory, name);
    if (length < 0 || length >= (int)sizeof path) {
        return false;
    }
    CF_Image image = { SIDE, SIDE, pixels };
    void *png = NULL;
    int size = 0;
    if (cf_is_error(cf_image_save_png_to_memory(&image, &png, &size))) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    bool ok = file && fwrite(png, 1, (size_t)size, file) == (size_t)size;
    if (file) {
        ok = fclose(file) == 0 && ok;
    }
    cf_free(png);
    if (!ok) {
        return false;
    }
    length = snprintf(path, sizeof path, "%s/evidence.txt", directory);
    if (length < 0 || length >= (int)sizeof path) {
        return false;
    }
    file = fopen(path, frames == 1 ? "wb" : "ab");
    if (!file) {
        return false;
    }
    ok = fprintf(
             file,
             "terrain-seams frame=%d state=%s size=64x64 backend=%s "
             "pixels=%016llx\n",
             frames,
             name,
             cf_backend_type_to_string(cf_query_backend()),
             (unsigned long long)hash
         ) > 0;
    return fclose(file) == 0 && ok;
}
static bool draw(float span, const char *name)
{
    engine_viewport_set_view(viewport, &(engine_viewport_view){ .width = span, .height = span });
    cf_app_update(NULL);
    drawn = false;
    if (engine_viewport_render(viewport, SIDE, SIDE)) {
        return false;
    }
    engine_viewport_present(viewport);
    cf_app_draw_onto_screen(true);
    frames++;
    if (!drawn || !test_read_canvas(cf_app_get_canvas(), pixels, sizeof pixels)) {
        return false;
    }
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *bytes = (const unsigned char *)pixels;
    for (size_t i = 0; i < sizeof pixels; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    fprintf(
        stderr,
        "terrain-seams frame=%d state=%s size=64x64 backend=%s "
        "pixels=%016llx\n",
        frames,
        name,
        cf_backend_type_to_string(cf_query_backend()),
        (unsigned long long)hash
    );
    return save_pixels(name, hash);
}
static bool white(CF_Pixel pixel)
{
    return pixel.colors.r > 200 && pixel.colors.g > 200 && pixel.colors.b > 200;
}
static bool black(CF_Pixel pixel)
{
    return pixel.colors.r < 40 && pixel.colors.g < 40 && pixel.colors.b < 40;
}
static void borders(float width)
{
    ecs_component_id id = COMPONENT_Appearance.id;
    ecs_query query;
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        Appearance_get(world, entity)->border_width = width;
    }
    ecs_query_end(&query);
}
static bool save_edits()
{
    free(checkpoint);
    checkpoint = NULL;
    checkpoint_size = 0;
    return materials_terrain_checkpoint_write(
        terrain_materials,
        source,
        "terrain-seams/v1",
        &checkpoint,
        &checkpoint_size,
        error,
        sizeof error
    );
}
static bool reset()
{
    materials_reset(terrain_materials);
    return !materials_prepare(terrain_materials);
}
static bool restore()
{
    return reset() &&
           materials_terrain_checkpoint_read(
               terrain_materials,
               source,
               "terrain-seams/v1",
               checkpoint,
               checkpoint_size,
               error,
               sizeof error
           ) &&
           !materials_prepare(terrain_materials);
}
static bool cut()
{
    materials_damage(terrain_materials, source, (fluid_vec){ 0, 0 }, 1.4f, 100, 0, (material_impact){ 0 });
    return !materials_prepare(terrain_materials);
}
TEST_CASE(terrain_surfaces_borders_and_lod_remain_continuous_after_restore)
{
    render_material_lod *lod = render_material_lod_new();
    render_material_lod_begin(lod);
    ecs_component_id required[] = { COMPONENT_Destructible.id,
                                    COMPONENT_Transform.id };
    ecs_query query;
    REQUIRE(ecs_query_begin(world, &query, required, 2) == ECS_OK);
    ecs_entity entity;
    int chunks = 0;
    while (ecs_query_next(&query, &entity)) {
        const material_mask *mask = materials_mask(terrain_materials, entity);
        REQUIRE(mask && mask->halo_radius >= 32 && mask->lod_limit >= 1);
        const material_mask *coarse = render_material_lod_get(lod, mask, 1);
        REQUIRE(coarse->cell_size == 2 && coarse->halo_radius >= 16);
        const Transform *position = Transform_get_const(world, entity);
        int x = position->x < 0 ? coarse->width : -1, y = coarse->height / 2 - 2;
        bool occupied;
        float distance;
        REQUIRE(material_mask_sample(coarse, x, y, &occupied, &distance));
        float wx = position->x + (x + .5f - coarse->width * .5f) * 2;
        float wy = position->y + (y + .5f - coarse->height * .5f) * 2;
        REQUIRE(fabsf(distance - (wx + wy + .125f)) < .0001f);
        chunks++;
    }
    ecs_query_end(&query);
    render_material_lod_free(lod);
    REQUIRE(chunks == 2);
    REQUIRE(draw(8, "fine-before"));
    memcpy(fine_before, pixels, sizeof pixels);
    /* Both sides of the internal seam remain fill, never a false green border. */
    REQUIRE(white(pixels[8 * SIDE + 31]) && white(pixels[8 * SIDE + 32]));
    REQUIRE(black(pixels[48 * SIDE + 31]) && black(pixels[48 * SIDE + 32]));
    /* The true diagonal crosses the seam at y=-.125. The old independent
     * edge clamp shifted the two crossings by a full cell. */
    REQUIRE(
        pixels[28 * SIDE + 31].colors.g > 200 && pixels[28 * SIDE + 32].colors.g > 200
    );
    REQUIRE(black(pixels[34 * SIDE + 31]) && black(pixels[34 * SIDE + 32]));
    REQUIRE(cut() && draw(8, "fine-cut"));
    REQUIRE(black(pixels[25 * SIDE + 31]) && black(pixels[25 * SIDE + 32]));
    REQUIRE(white(pixels[8 * SIDE + 31]) && white(pixels[8 * SIDE + 32]));
    REQUIRE(memcmp(fine_before, pixels, sizeof pixels));
    memcpy(edited, pixels, sizeof pixels);
    REQUIRE(save_edits());
    REQUIRE(restore() && draw(8, "fine-restored"));
    REQUIRE(!memcmp(edited, pixels, sizeof pixels));
    REQUIRE(reset());
    borders(0);
    REQUIRE(draw(128, "coarse-before"));
    memcpy(coarse_before, pixels, sizeof pixels);
    /* At two world units per pixel, x=-1,y and x=1,y-2 lie on the same
     * analytic half-plane. Their coverage must agree across the chunk edge. */
    for (int y = 28; y <= 35; y++) {
        REQUIRE(!memcmp(&pixels[y * SIDE + 31], &pixels[(y - 1) * SIDE + 32], sizeof(CF_Pixel)));
    }
    REQUIRE(white(pixels[20 * SIDE + 31]) && white(pixels[20 * SIDE + 32]));
    REQUIRE(black(pixels[44 * SIDE + 31]) && black(pixels[44 * SIDE + 32]));
    REQUIRE(cut() && draw(128, "coarse-cut"));
    REQUIRE(white(pixels[20 * SIDE + 31]) && white(pixels[20 * SIDE + 32]));
    REQUIRE(memcmp(coarse_before, pixels, sizeof pixels));
    memcpy(edited, pixels, sizeof pixels);
    REQUIRE(save_edits());
    REQUIRE(restore());
    borders(0);
    REQUIRE(draw(128, "coarse-restored"));
    REQUIRE(!memcmp(edited, pixels, sizeof pixels));
    REQUIRE(frames == 6);
    return true;
}
void test_render_material_terrain_seams()
{
    test_open_hidden_app("material-terrain-seams", SIDE, SIDE);
    cf_app_set_canvas_size(SIDE, SIDE);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(terrain_surfaces_borders_and_lod_remain_continuous_after_restore);
    pu_clear_setup();
    cf_destroy_app();
}
