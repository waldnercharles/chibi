#include "render/scene3d_terrain.h"

#include "render/scene3d_light_shadow.h"
#include "render/scene3d_mesh_cache.h"
#include "render/scene3d_terrain_volume.h"
#include "runtime/geometry/terrain_volume.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/render/hidden_app.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static MaterialSurface3D terrain_options(void)
{
    return (MaterialSurface3D){ .enabled = true,
                                .elevation = 8,
                                .depth = 4,
                                .bevel_width = 1,
                                .relief = .5f,
                                .relief_scale = 8,
                                .spacing = 1,
                                .seed = 7 };
}
static bool covers(const render_scene3d_terrain_surface *surface, float x, float z)
{
    for (int i = 0; i < surface->proxy_count; i++) {
        CF_M4x4 box = surface->proxies[i];
        if (fabsf(x - box.elements[12]) < box.elements[0] * .5f &&
            fabsf(z - box.elements[14]) < box.elements[10] * .5f) {
            return true;
        }
    }
    return false;
}
static bool hollow_mesh_bounds_do_not_suppress_cave_shadows(void)
{
    render_scene3d_light_shadow *shadow = render_scene3d_light_shadow_new();
    CF_V4 light = { 0, 0, 0, 10 }, cone = { 1, 0, 0, .8f };
    render_scene3d_light_shadow_set(shadow, &light, &cone, 1);
    CF_M4x4 cave = cf_m4_scale((CF_V3){ 100, 20, 100 });
    REQUIRE(!render_scene3d_light_shadow_near(shadow, 0, cave));
    REQUIRE(render_scene3d_light_shadow_mesh_near(shadow, 0, cave));
    REQUIRE(render_scene3d_light_shadow_reaches(shadow, 0, 0, cave));
    CF_M4x4 far = cf_mul_m4(cf_m4_translate((CF_V3){ 1000, 0, 0 }), cave);
    REQUIRE(!render_scene3d_light_shadow_near(shadow, 0, far));
    REQUIRE(!render_scene3d_light_shadow_mesh_near(shadow, 0, far));
    CF_M4x4 wall = cf_mul_m4(cf_m4_translate((CF_V3){ 5, 0, 0 }), cf_m4_scale((CF_V3){ 1, 2, 2 }));
    REQUIRE(render_scene3d_light_shadow_near(shadow, 0, wall));
    REQUIRE(render_scene3d_light_shadow_mesh_near(shadow, 0, wall));
    render_scene3d_light_shadow_free(shadow);
    return true;
}

static bool opening_revision_and_retirement(void)
{
    render_scene3d_terrain *terrain = render_scene3d_terrain_new();
    material_mask mask = material_mask_new(12, 12, 1, false);
    char error[256] = { 0 };
    render_scene3d_terrain_begin(terrain);
    const render_scene3d_terrain_surface *surface = render_scene3d_terrain_get(
        terrain,
        &mask,
        terrain_options(),
        (MaterialFrame){ 0 },
        (GlobalTransform2D){ 0 },
        NULL,
        (ecs_entity){ 0 },
        true,
        error,
        sizeof error
    );
    REQUIRE(surface);
    REQUIRE(covers(surface, 0, 0));
    uint64_t first = surface->revision;
    const render_mesh_asset *asset = surface->mesh;
    render_scene3d_terrain_end(terrain);
    REQUIRE(render_scene3d_terrain_rebuilds(terrain) == 1);
    render_scene3d_terrain_begin(terrain);
    surface = render_scene3d_terrain_get(
        terrain,
        &mask,
        terrain_options(),
        (MaterialFrame){ 0 },
        (GlobalTransform2D){ .position = { 30, 40 } },
        NULL,
        (ecs_entity){ 0 },
        true,
        error,
        sizeof error
    );
    REQUIRE(surface && surface->mesh == asset && surface->revision == first);
    REQUIRE(surface->frame.elements[12] == 30 && surface->frame.elements[14] == 40);
    render_scene3d_terrain_end(terrain);
    REQUIRE(render_scene3d_terrain_rebuilds(terrain) == 0);
    REQUIRE(material_mask_carve(&mask, 0, 0, 2.5f) > 0);
    render_scene3d_terrain_begin(terrain);
    surface = render_scene3d_terrain_get(
        terrain,
        &mask,
        terrain_options(),
        (MaterialFrame){ 0 },
        (GlobalTransform2D){ 0 },
        NULL,
        (ecs_entity){ 0 },
        true,
        error,
        sizeof error
    );
    REQUIRE(surface && surface->revision != first);
    REQUIRE(!covers(surface, 0, 0));
    REQUIRE(covers(surface, 4.5f, 4.5f));
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 12; x++) {
            float px = x - 5.5f, pz = y - 5.5f;
            REQUIRE(covers(surface, px, pz) == material_mask_contains(&mask, px, pz));
        }
    }
    render_scene3d_terrain_end(terrain);
    REQUIRE(render_scene3d_terrain_rebuilds(terrain) == 1);
    render_scene3d_terrain_begin(terrain);
    render_scene3d_terrain_end(terrain);
    REQUIRE(render_scene3d_terrain_rebuilds(terrain) == 1);
    render_scene3d_terrain_reset(terrain);
    render_scene3d_terrain_begin(terrain);
    surface = render_scene3d_terrain_get(
        terrain,
        &mask,
        terrain_options(),
        (MaterialFrame){ 0 },
        (GlobalTransform2D){ 0 },
        NULL,
        (ecs_entity){ 0 },
        true,
        error,
        sizeof error
    );
    REQUIRE(surface && !covers(surface, 0, 0));
    material_mask_carve(&mask, 0, 0, 100);
    surface = render_scene3d_terrain_get(
        terrain,
        &mask,
        terrain_options(),
        (MaterialFrame){ 0 },
        (GlobalTransform2D){ 0 },
        NULL,
        (ecs_entity){ 0 },
        true,
        error,
        sizeof error
    );
    REQUIRE(surface && !surface->proxy_count && !acount(surface->mesh->indices));
    render_scene3d_terrain_end(terrain);
    render_scene3d_terrain_free(terrain);
    material_mask_free(&mask);
    return true;
}
static bool recess_options_rebuild_and_keep_proxy_openings(void)
{
    render_scene3d_terrain *terrain = render_scene3d_terrain_new();
    material_mask mask = material_mask_new(12, 12, 1, false);
    REQUIRE(material_mask_carve(&mask, 0, 0, 2.5f) > 0);
    MaterialSurface3D style = terrain_options();
    style.depth = 10;
    style.recess_width = 3;
    style.recess_depth = 4;
    char error[256] = { 0 };
    uint64_t revisions[2];
    for (int i = 0; i < 2; i++) {
        style.recess_depth = 4 + i;
        render_scene3d_terrain_begin(terrain);
        const render_scene3d_terrain_surface *surface = render_scene3d_terrain_get(
            terrain,
            &mask,
            style,
            (MaterialFrame){ 0 },
            (GlobalTransform2D){ 0 },
            NULL,
            (ecs_entity){ 0 },
            true,
            error,
            sizeof error
        );
        REQUIRE(surface);
        revisions[i] = surface->revision;
        /* The receding surface fills the cut; indirect proxies keep it open. */
        REQUIRE(surface->mesh->min.y < style.elevation - 4);
        REQUIRE(!covers(surface, 0, 0));
        render_scene3d_terrain_end(terrain);
    }
    REQUIRE(revisions[0] != revisions[1]);
    render_scene3d_terrain_free(terrain);
    material_mask_free(&mask);
    return true;
}
static bool complex_occupancy_is_rejected_without_closing_openings(void)
{
    unsigned char cells[92 * 92];
    for (int y = 0; y < 92; y++) {
        for (int x = 0; x < 92; x++) {
            cells[y * 92 + x] = (x + y) % 2;
        }
    }
    material_mask mask = material_mask_from_cells(92, 92, 1, cells);
    REQUIRE(material_mask_collision(&mask).count > RENDER_TERRAIN_GI_PROXY_LIMIT);
    render_scene3d_terrain *terrain = render_scene3d_terrain_new();
    char error[256] = { 0 };
    render_scene3d_terrain_begin(terrain);
    REQUIRE(!render_scene3d_terrain_get(
        terrain,
        &mask,
        terrain_options(),
        (MaterialFrame){ 0 },
        (GlobalTransform2D){ 0 },
        NULL,
        (ecs_entity){ 0 },
        true,
        error,
        sizeof error
    ));
    REQUIRE(strstr(error, "no opening-closing simplification"));
    render_scene3d_terrain_free(terrain);
    material_mask_free(&mask);
    return true;
}
static bool dynamic_gpu_revision_lifecycle(void)
{
    material_mask mask = material_mask_new(4, 4, 1, false);
    render_scene3d_terrain *terrain = render_scene3d_terrain_new();
    render_scene3d_mesh_cache *cache = render_scene3d_mesh_cache_new();
    char error[256] = { 0 };
    render_scene3d_terrain_begin(terrain);
    const render_scene3d_terrain_surface *surface = render_scene3d_terrain_get(
        terrain,
        &mask,
        terrain_options(),
        (MaterialFrame){ 0 },
        (GlobalTransform2D){ 0 },
        NULL,
        (ecs_entity){ 0 },
        true,
        error,
        sizeof error
    );
    REQUIRE(surface);
    render_scene3d_mesh_cache_dynamic_begin(cache);
    REQUIRE(render_scene3d_mesh_cache_dynamic_upload(
        cache,
        surface->identity,
        surface->revision,
        surface->mesh
    ));
    render_scene3d_mesh_cache_dynamic_end(cache);
    REQUIRE(render_scene3d_mesh_cache_dynamic_count(cache) == 1);
    REQUIRE(render_scene3d_mesh_cache_dynamic_uploads(cache) == 1);
    render_scene3d_mesh_cache_dynamic_begin(cache);
    REQUIRE(render_scene3d_mesh_cache_dynamic_upload(
        cache,
        surface->identity,
        surface->revision,
        surface->mesh
    ));
    render_scene3d_mesh_cache_dynamic_end(cache);
    REQUIRE(render_scene3d_mesh_cache_dynamic_uploads(cache) == 0);
    material_mask_carve(&mask, 0, 0, 1);
    surface = render_scene3d_terrain_get(
        terrain,
        &mask,
        terrain_options(),
        (MaterialFrame){ 0 },
        (GlobalTransform2D){ 0 },
        NULL,
        (ecs_entity){ 0 },
        true,
        error,
        sizeof error
    );
    REQUIRE(surface);
    render_scene3d_mesh_cache_dynamic_begin(cache);
    REQUIRE(render_scene3d_mesh_cache_dynamic_upload(
        cache,
        surface->identity,
        surface->revision,
        surface->mesh
    ));
    render_scene3d_mesh_cache_dynamic_end(cache);
    REQUIRE(render_scene3d_mesh_cache_dynamic_uploads(cache) == 1);
    REQUIRE(render_scene3d_mesh_cache_dynamic_count(cache) == 1);
    render_scene3d_mesh_cache_dynamic_begin(cache);
    render_scene3d_mesh_cache_dynamic_end(cache);
    REQUIRE(render_scene3d_mesh_cache_dynamic_count(cache) == 0);
    REQUIRE(render_scene3d_mesh_cache_dynamic_upload(
        cache,
        surface->identity,
        surface->revision,
        surface->mesh
    ));
    render_scene3d_mesh_cache_dynamic_clear(cache);
    REQUIRE(render_scene3d_mesh_cache_dynamic_count(cache) == 0);
    render_scene3d_mesh_cache_free(cache);
    render_scene3d_terrain_free(terrain);
    material_mask_free(&mask);
    return true;
}
/* Rock with a spherical cavity of radius 20 at depth 12 behind the plane
 * origin; negative is solid. */
static field_program *cavity_field(void)
{
    char error[192];
    field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_Z },
        { .op = FIELD_CONSTANT, .value = 12 },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 2, 3 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 0, 0 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 1, 1 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 4, 4 } },
        { .op = FIELD_ADD, .count = 2, .inputs = { 5, 6 } },
        { .op = FIELD_ADD, .count = 2, .inputs = { 8, 7 } },
        { .op = FIELD_CONSTANT, .value = 400 },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 10, 9 } },
        { .op = FIELD_CONSTANT, .value = 40 },
        { .op = FIELD_DIVIDE, .count = 2, .inputs = { 11, 12 } },
    };
    return field_program_new(code, 14, 13, NULL, 0, error, sizeof error);
}

static float *carve_nothing(const terrain_volume *volume, unsigned char **open)
{
    size_t samples = (size_t)volume->plane.width * volume->plane.height;
    *open = xcalloc(samples, 1);
    float *removed = xmalloc(samples * sizeof *removed);
    terrain_volume_removed(
        volume,
        *open,
        0,
        0,
        volume->plane.width,
        volume->plane.height,
        removed
    );
    return removed;
}

static bool same_chunks(
    const render_scene3d_volume_chunk *a,
    const render_scene3d_volume_chunk *b,
    int count
)
{
    for (int i = 0; i < count; i++) {
        const render_mesh_asset *x = a[i].mesh, *y = b[i].mesh;
        if (acount(x->vertices) != acount(y->vertices) ||
            acount(x->indices) != acount(y->indices) ||
            memcmp(x->vertices, y->vertices, acount(x->vertices) * sizeof *x->vertices) ||
            memcmp(x->indices, y->indices, acount(x->indices) * sizeof *x->indices) ||
            memcmp(&x->min, &y->min, sizeof x->min) ||
            memcmp(&x->max, &y->max, sizeof x->max)) {
            return false;
        }
    }
    return true;
}

static bool empty_volume_keeps_no_meshes_or_boxes(void)
{
    char error[192];
    field_ir code[] = { { .op = FIELD_CONSTANT, .value = 100 } };
    field_program *program = field_program_new(code, 1, 0, NULL, 0, error, sizeof error);
    terrain_volume volume = { 0 };
    REQUIRE(
        program && terrain_volume_bake(
                       program,
                       (field_context){ 0 },
                       (terrain_volume_shape){ { 3, 3, 1, 0, 0 }, 1, 3, 1, true, 1 },
                       NULL,
                       &volume,
                       error,
                       sizeof error
                   )
    );
    field_program_free(program);
    unsigned char *open;
    float *removed = carve_nothing(&volume, &open);
    render_scene3d_terrain_volume *volumes = render_scene3d_terrain_volume_new();
    materials_depth_volume view = { &volume, removed, 1, 1 };
    render_scene3d_volume_surface
        first = render_scene3d_terrain_volume_get(volumes, view, 0, (CF_V2){ 0 }, NULL);
    REQUIRE(!first.chunk_count && !first.caster_count && !first.box_count);
    REQUIRE(render_scene3d_terrain_volume_rebuilds(volumes) > 0);
    render_scene3d_terrain_volume_begin(volumes);
    render_scene3d_volume_surface
        cached = render_scene3d_terrain_volume_get(volumes, view, 0, (CF_V2){ 0 }, NULL);
    REQUIRE(!cached.chunk_count && !cached.caster_count && !cached.box_count);
    REQUIRE(render_scene3d_terrain_volume_rebuilds(volumes) == 0);
    render_scene3d_volume_surface
        moved = render_scene3d_terrain_volume_get(volumes, view, 0, (CF_V2){ 1, 0 }, NULL);
    REQUIRE(!moved.chunk_count && !moved.caster_count && !moved.box_count);
    REQUIRE(render_scene3d_terrain_volume_rebuilds(volumes) > 0);
    render_scene3d_terrain_volume_end(volumes);
    render_scene3d_terrain_volume_free(volumes);
    free(removed);
    free(open);
    terrain_volume_free(&volume);
    return true;
}

/* A cut re-meshes and refits only what it can reshape, and the result equals
 * a fresh build from the same carve: drawn and caster meshes and boxes. */
static bool volume_rebuild_after_a_cut_matches_a_fresh_build(void)
{
    char error[192];
    field_program *program = cavity_field();
    terrain_volume volume = { 0 };
    /* 97 x 97 fine samples, 8 ahead of the plane and 39 behind it: 4 x 4
     * columns of one caster chunk and two drawn chunks. */
    REQUIRE(
        program && terrain_volume_bake(
                       program,
                       (field_context){ 0 },
                       (terrain_volume_shape){ { 49, 49, 2, 0, 0 }, 4, 24, 2, false, 6 },
                       NULL,
                       &volume,
                       error,
                       sizeof error
                   )
    );
    field_program_free(program);
    unsigned char *open;
    float *removed = carve_nothing(&volume, &open);
    render_scene3d_terrain_volume *kept = render_scene3d_terrain_volume_new(),
                                  *fresh = render_scene3d_terrain_volume_new();
    materials_depth_volume view = { &volume, removed, 1, 1 };
    render_scene3d_volume_surface
        first = render_scene3d_terrain_volume_get(kept, view, 8, (CF_V2){ 0 }, NULL);
    int chunks = render_scene3d_terrain_volume_rebuilds(kept);
    REQUIRE(
        chunks == 48 && first.chunk_count > 0 && first.caster_count > 0 &&
        first.box_count > 0
    );
    render_scene3d_volume_chunk *before = NULL;
    for (int i = 0; i < first.chunk_count; i++) {
        apush(before, first.chunks[i]);
    }
    /* A disk opened in solid rock in the third column across and down. */
    int width = volume.plane.width;
    for (int y = 76; y < 85; y++) {
        for (int x = 76; x < 85; x++) {
            open[y * width + x] = (x - 80) * (x - 80) + (y - 80) * (y - 80) <= 16;
        }
    }
    terrain_volume_removed(&volume, open, 76, 76, 85, 85, removed);
    view.revision = 2;
    render_scene3d_terrain_volume_begin(kept);
    render_scene3d_volume_surface
        after = render_scene3d_terrain_volume_get(kept, view, 8, (CF_V2){ 0 }, NULL);
    render_scene3d_terrain_volume_end(kept);
    int rebuilt = render_scene3d_terrain_volume_rebuilds(kept);
    REQUIRE(rebuilt > 0 && rebuilt < chunks);
    int reused = 0;
    for (int i = 0; i < after.chunk_count; i++) {
        for (int k = 0; k < acount(before); k++) {
            reused += after.chunks[i].identity == before[k].identity &&
                      after.chunks[i].revision == before[k].revision;
        }
    }
    REQUIRE(reused > 0 && reused < after.chunk_count);
    render_scene3d_volume_surface
        whole = render_scene3d_terrain_volume_get(fresh, view, 8, (CF_V2){ 0 }, NULL);
    REQUIRE(
        after.chunk_count == whole.chunk_count && after.caster_count == whole.caster_count
    );
    REQUIRE(same_chunks(after.chunks, whole.chunks, after.chunk_count));
    REQUIRE(same_chunks(after.casters, whole.casters, after.caster_count));
    REQUIRE(after.box_count == whole.box_count);
    REQUIRE(!memcmp(
        after.boxes,
        whole.boxes,
        (size_t)after.box_count * sizeof *after.boxes
    ));
    afree(before);
    render_scene3d_terrain_volume_free(kept);
    render_scene3d_terrain_volume_free(fresh);
    free(removed);
    free(open);
    terrain_volume_free(&volume);
    return true;
}

/* Past one slab box per chunk column, columns share box tiles: the volume
 * stays within half the scene's indirect proxies and still boxes all of its
 * solid rock. */
static bool volume_boxes_fit_the_budget_with_many_columns(void)
{
    char error[192];
    field_ir code[] = { { .op = FIELD_CONSTANT, .value = -100 } };
    field_program *program = field_program_new(code, 1, 0, NULL, 0, error, sizeof error);
    terrain_volume volume = { 0 };
    /* 1473 x 1473 fine samples: 46 x 46 chunk columns over 184 x 184 x 2
     * solid bricks of 8 units. */
    REQUIRE(
        program &&
        terrain_volume_bake(
            program,
            (field_context){ 0 },
            (terrain_volume_shape){ { 185, 185, 8, 0, 0 }, 0, 3, 8, false, 2 },
            NULL,
            &volume,
            error,
            sizeof error
        )
    );
    field_program_free(program);
    unsigned char *open;
    float *removed = carve_nothing(&volume, &open);
    render_scene3d_terrain_volume *volumes = render_scene3d_terrain_volume_new();
    materials_depth_volume view = { &volume, removed, 1, 1 };
    render_scene3d_volume_surface
        surface = render_scene3d_terrain_volume_get(volumes, view, 0, (CF_V2){ 0 }, NULL);
    REQUIRE(surface.box_count > 0 && surface.box_count <= RENDER_TERRAIN_GI_PROXY_LIMIT / 2);
    double boxed = 0;
    for (int i = 0; i < surface.box_count; i++) {
        CF_V3 e = cf_sub_v3(surface.boxes[i].max, surface.boxes[i].min);
        boxed += (double)e.x * e.y * e.z;
    }
    REQUIRE(fabs(boxed - 184.0 * 184 * 2 * 512) < 1);
    render_scene3d_terrain_volume_free(volumes);
    free(removed);
    free(open);
    terrain_volume_free(&volume);
    return true;
}

void test_tool_scene3d_terrain(void)
{
    RUN_TEST_CASE(hollow_mesh_bounds_do_not_suppress_cave_shadows);
    RUN_TEST_CASE(opening_revision_and_retirement);
    RUN_TEST_CASE(recess_options_rebuild_and_keep_proxy_openings);
    RUN_TEST_CASE(complex_occupancy_is_rejected_without_closing_openings);
    RUN_TEST_CASE(empty_volume_keeps_no_meshes_or_boxes);
    RUN_TEST_CASE(volume_rebuild_after_a_cut_matches_a_fresh_build);
    RUN_TEST_CASE(volume_boxes_fit_the_budget_with_many_columns);
}
void test_tool_scene3d_terrain_gpu(void)
{
    test_open_hidden_app("scene3d-terrain", 32, 32);
    RUN_TEST_CASE(dynamic_gpu_revision_lifecycle);
    cf_destroy_app();
}
