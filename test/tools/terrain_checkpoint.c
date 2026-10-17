#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/material_checkpoint.h"
#include "runtime/materials.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ecs_world *world;
static entities *entity_service;
static materials *runtime;
static ecs_entity source;
static unsigned char *saved;
static size_t saved_length;
static char error[256], path[64];
static const char *executable;

static field_program *solid_program(float value)
{
    field_ir ir = { .op = FIELD_CONSTANT, .value = value };
    return field_program_new(&ir, 1, 0, NULL, 0, error, sizeof error);
}
static void cleanup()
{
    free(saved);
    saved = NULL;
    if (runtime) {
        materials_free(runtime);
    }
    runtime = NULL;
    if (entity_service) {
        entities_free(entity_service);
    }
    entity_service = NULL;
    if (world) {
        ecs_world_free(world);
    }
    world = NULL;
    if (*path) {
        unlink(path);
    }
    *path = 0;
}
static bool prepare(bool streamed, bool unrelated)
{
    world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    entity_service = entities_new(world, NULL);
    runtime = materials_new(world, entity_service, NULL);
    if (unrelated) {
        REQUIRE(
            material_field_register(materials_fields(runtime), "Unrelated", solid_program(12))
        );
    }
    REQUIRE(material_field_register(materials_fields(runtime), "Rock", solid_program(-1)));
    if (unrelated) {
        REQUIRE(material_field_register(materials_fields(runtime), "Rock", solid_program(-1)));
    }
    source = ecs_world_create_entity(world);
    MaterialTerrain terrain = { .width = streamed ? 0 : 256,
                                .height = streamed ? 0 : 8,
                                .cell_size = 1,
                                .field = "Rock",
                                .seed = 17,
                                .min_fragment_cells = 1,
                                .stream_radius = streamed ? 1 : 0,
                                .stream_budget = 8 };
    Transform transform = { 0 };
    REQUIRE(
        ecs_world_record_add(world, source, COMPONENT_MaterialTerrain.id, &terrain, 0) == ECS_OK
    );
    REQUIRE(ecs_world_record_add(world, source, COMPONENT_Transform.id, &transform, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    if (streamed) {
        material_stream_view view = { -64, -64, 1, 1 };
        materials_set_stream_views(runtime, &view, 1);
    }
    const char *problem = materials_prepare(runtime);
    if (problem) {
        fprintf(stderr, "terrain prepare: %s\n", problem);
    }
    REQUIRE(!problem);
    return true;
}
static material_mask *tile(uint64_t key)
{
    const MaterialTerrain *terrain = MaterialTerrain_get_const(world, source);
    field_grid grid = { .width = (int)ceilf(terrain->width / terrain->cell_size),
                        .height = (int)ceilf(terrain->height / terrain->cell_size),
                        .cell_size = terrain->cell_size };
    field_grid address;
    assert(material_stream_tile_grid(grid, key, &address));
    const Transform *origin = Transform_get_const(world, source);
    float x = origin->x + address.center_x, y = origin->y + address.center_y;
    ecs_component_id ids[] = { COMPONENT_Destructible.id, COMPONENT_Transform.id };
    ecs_query query;
    assert(ecs_query_begin(world, &query, ids, 2) == ECS_OK);
    ecs_entity entity;
    material_mask *result = NULL;
    while (ecs_query_next(&query, &entity)) {
        const Transform *transform = Transform_get_const(world, entity);
        if (transform->x == x && transform->y == y &&
            !materials_is_fragment(runtime, entity)) {
            /* Test edits canonical samples; production edits use queued damage. */
            result = (material_mask *)materials_mask(runtime, entity);
        }
    }
    ecs_query_end(&query);
    return result;
}

static void empty(material_mask *mask)
{
    int count = mask->width * mask->height;
    memset(mask->cells, 0, (size_t)count);
    for (int i = 0; i < count; i++) {
        mask->distance[i] = 1;
    }
    mask->count = 0;
    material_mask_rebuild(mask);
}
static void partial(material_mask *mask, float energy)
{
    unsigned revision = mask->revision;
    int cells = mask->count;
    int removed = material_mask_apply_damage(mask, .5f, .5f, .75f, energy, 1);
    assert(!removed && cells == mask->count && revision == mask->revision);
}
static bool has_damage(const material_mask *mask, float amount)
{
    if (!mask || !mask->damage) {
        return false;
    }
    for (int i = 0; i < mask->width * mask->height; i++) {
        if (mask->damage[i] == amount) {
            return true;
        }
    }
    return false;
}
static bool edited_finite()
{
    REQUIRE(tile(0) && tile(0)->count == 0);
    REQUIRE(tile(1) && tile(1)->count == 1024);
    REQUIRE(has_damage(tile(1), .25f));
    material_mask reduced = material_mask_reduce(tile(0));
    REQUIRE(reduced.count == 0);
    material_mask_free(&reduced);
    return true;
}
TEST_CASE(finite_checkpoint_roundtrip_and_validation)
{
    REQUIRE(prepare(false, false));
    REQUIRE(tile(0) && tile(1));
    empty(tile(0));
    partial(tile(1), .25f);
    REQUIRE(
        materials_terrain_checkpoint_write(runtime, source, "world/rock", &saved, &saved_length, error, sizeof error)
    );
    REQUIRE(saved_length < 64 * 1024);
    REQUIRE(material_field_register(materials_fields(runtime), "Unrelated", solid_program(99)));
    partial(tile(1), .125f);
    uint64_t before = materials_inspect(runtime).hash;
    REQUIRE(
        !materials_terrain_checkpoint_read(runtime, source, "world/wrong", saved, saved_length, error, sizeof error)
    );
    REQUIRE(materials_inspect(runtime).hash == before);
    saved[saved_length - 1] ^= 1;
    REQUIRE(
        !materials_terrain_checkpoint_read(runtime, source, "world/rock", saved, saved_length, error, sizeof error)
    );
    REQUIRE(materials_inspect(runtime).hash == before);
    saved[saved_length - 1] ^= 1;
    REQUIRE(
        !materials_terrain_checkpoint_read(runtime, source, "world/rock", saved, saved_length - 1, error, sizeof error)
    );
    REQUIRE(materials_inspect(runtime).hash == before);
    REQUIRE(
        materials_terrain_checkpoint_read(runtime, source, "world/rock", saved, saved_length, error, sizeof error)
    );
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(edited_finite());
    REQUIRE(
        tile(0)->incarnation && tile(1)->incarnation &&
        tile(0)->incarnation != tile(1)->incarnation
    );
    MaterialTerrain_get(world, source)->seed++;
    REQUIRE(
        !materials_terrain_checkpoint_read(runtime, source, "world/rock", saved, saved_length, error, sizeof error)
    );
    REQUIRE(materials_prepare(runtime));
    REQUIRE(edited_finite());
    REQUIRE(
        tile(0)->incarnation && tile(1)->incarnation &&
        tile(0)->incarnation != tile(1)->incarnation
    );
    MaterialTerrain_get(world, source)->seed--;
    REQUIRE(!materials_prepare(runtime));
    snprintf(path, sizeof path, "/tmp/chibi-checkpoint-XXXXXX");
    int fd = mkstemp(path);
    REQUIRE(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    REQUIRE(file);
    REQUIRE(fwrite(saved, 1, saved_length, file) == saved_length);
    REQUIRE(!fclose(file));
    const char *args[] = { executable, "--terrain-checkpoint-child", path, NULL };
    char log[96];
    snprintf(log, sizeof log, "%s.log", path);
    int status = tool_run(args, tool_root(), log);
    unlink(log);
    REQUIRE(status == 0);
    return true;
}
TEST_CASE(stream_checkpoint_cold_resident_negative_and_empty)
{
    REQUIRE(prepare(true, false));
    uint64_t negative = UINT64_MAX;
    REQUIRE(tile(negative));
    partial(tile(negative), .25f);
    material_stream_view view = { 1024, 1024, 1, 1 };
    materials_set_stream_views(runtime, &view, 1);
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(!tile(negative));
    view = (material_stream_view){ -64, -64, 1, 1 };
    materials_set_stream_views(runtime, &view, 1);
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(has_damage(tile(negative), .25f));
    /* Resident modification supersedes the older cold record without a geometry revision. */
    partial(tile(negative), .125f);
    REQUIRE(
        materials_terrain_checkpoint_write(runtime, source, "world/infinite", &saved, &saved_length, error, sizeof error)
    );
    REQUIRE(
        materials_terrain_checkpoint_read(runtime, source, "world/infinite", saved, saved_length, error, sizeof error)
    );
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(has_damage(tile(negative), .375f));
    empty(tile(negative));
    free(saved);
    saved = NULL;
    REQUIRE(
        materials_terrain_checkpoint_write(runtime, source, "world/infinite", &saved, &saved_length, error, sizeof error)
    );
    REQUIRE(
        materials_terrain_checkpoint_read(runtime, source, "world/infinite", saved, saved_length, error, sizeof error)
    );
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(materials_inspect(runtime).cells == 0);
    view = (material_stream_view){ 1024, 1024, 1, 1 };
    materials_set_stream_views(runtime, &view, 1);
    REQUIRE(!materials_prepare(runtime));
    view = (material_stream_view){ -64, -64, 1, 1 };
    materials_set_stream_views(runtime, &view, 1);
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(materials_inspect(runtime).cells == 0);
    return true;
}
TEST_CASE(checkpoint_codec_rejects_duplicate_keys_and_invalid_samples)
{
    material_checkpoint checkpoint = { 0 }, decoded = { 0 };
    field_grid grid = { 2, 2, 1, 0, 0 };
    material_mask *a = material_checkpoint_copy_mask(NULL, grid);
    material_mask *b = material_checkpoint_copy_mask(NULL, grid);
    REQUIRE(a->incarnation && b->incarnation && a->incarnation != b->incarnation);
    material_mask reduced = material_mask_reduce(a);
    REQUIRE(reduced.count == 0);
    material_mask_free(&reduced);
    apush(checkpoint.tiles, ((material_stream_tile){ 0, grid, a }));
    apush(checkpoint.tiles, ((material_stream_tile){ 0, grid, b }));
    REQUIRE(
        material_checkpoint_write(&checkpoint, "duplicate", 7, &saved, &saved_length, error, sizeof error)
    );
    REQUIRE(
        !material_checkpoint_read(saved, saved_length, "duplicate", 7, &decoded, error, sizeof error)
    );
    REQUIRE(!decoded.tiles);
    REQUIRE(!material_checkpoint_read(
        saved,
        MATERIAL_CHECKPOINT_MAX_BYTES + (size_t)1,
        "duplicate",
        7,
        &decoded,
        error,
        sizeof error
    ));
    free(saved);
    saved = NULL;
    checkpoint.tiles[1].key = 1;
    a->damage[0] = -1;
    REQUIRE(
        !material_checkpoint_write(&checkpoint, "invalid", 7, &saved, &saved_length, error, sizeof error)
    );
    material_checkpoint_free(&checkpoint);
    return true;
}

TEST_CASE(checkpoint_recipe_tracks_resolved_typed_arguments)
{
    material_fields *fields = material_fields_new();
    field_parameter parameter = { .name = "Density",
                                  .value = { .type = PIN_F32, .f = -1 } };
    field_ir ir = { .op = FIELD_PARAMETER, .parameter = 0 };
    field_program *program = field_program_new(&ir, 1, 0, &parameter, 1, error, sizeof error);
    REQUIRE(program);
    REQUIRE(material_field_register(fields, "Parametric", program));
    material_checkpoint_recipe recipe = {
        .fields = fields,
        .field = "Parametric",
        .grid = { .width = 4, .height = 4, .cell_size = 1 }
    };
    uint64_t defaults, explicit_default, changed;
    REQUIRE(material_checkpoint_fingerprint(recipe, &defaults, error, sizeof error));
    recipe.context = (field_context){ .parameters = &parameter, .parameter_count = 1 };
    REQUIRE(material_checkpoint_fingerprint(recipe, &explicit_default, error, sizeof error));
    REQUIRE(defaults == explicit_default);
    parameter.value.f = -2;
    REQUIRE(material_checkpoint_fingerprint(recipe, &changed, error, sizeof error));
    REQUIRE(defaults != changed);
    parameter.value.f = -1;
    recipe.context.seed = 1;
    REQUIRE(material_checkpoint_fingerprint(recipe, &changed, error, sizeof error));
    REQUIRE(defaults != changed);
    recipe.context.seed = 0;
    recipe.island_cells = 2;
    REQUIRE(material_checkpoint_fingerprint(recipe, &changed, error, sizeof error));
    REQUIRE(defaults != changed);
    material_fields_free(fields);
    return true;
}

TEST_CASE(terrain_damage_updates_air_across_partial_chunk_boundary)
{
    REQUIRE(prepare(false, false));
    MaterialTerrain *terrain = MaterialTerrain_get(world, source);
    terrain->width = 130;
    terrain->height = 4;
    Transform_get(world, source)->x = -100;
    /* Seam at world x=-37. The right tile's nearest sample is air, while
     * its nearest solid sample lies outside this damage disk. */
    const field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_ADD, .count = 2, .inputs = { 0, 1 } },
        { .op = FIELD_CONSTANT, .value = -62.875f },
        { .op = FIELD_ADD, .count = 2, .inputs = { 2, 3 } },
    };
    field_program *program = field_program_new(code, 5, 4, NULL, 0, error, sizeof error);
    REQUIRE(program);
    REQUIRE(material_field_register(materials_fields(runtime), "Rock", program));
    REQUIRE(!materials_prepare(runtime));
    const material_mask *left = tile(0), *right = tile(1);
    REQUIRE(left && right && left->width == 128 && right->width == 2);
    float left_before[128 * 4], right_before[2 * 4];
    memcpy(left_before, left->distance, sizeof left_before);
    memcpy(right_before, right->distance, sizeof right_before);
    int left_cells = left->count, right_cells = right->count;
    int neighbor = right->width;
    REQUIRE(right->cells[neighbor] == 0 && right->distance[neighbor] == .125f);
    material_point hit = { -37, 0 };
    materials_damage(
        runtime,
        source,
        (fluid_vec){ hit.x, hit.y },
        1.4f,
        .25f,
        MATERIAL_DAMAGE_CONTACT,
        (material_impact){ 0 }
    );
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(left->count == left_cells && right->count == right_cells);
    REQUIRE(!memcmp(left_before, left->distance, sizeof left_before));
    REQUIRE(!memcmp(right_before, right->distance, sizeof right_before));
    materials_damage(
        runtime,
        source,
        (fluid_vec){ hit.x, hit.y },
        1.4f,
        .75f,
        MATERIAL_DAMAGE_CONTACT,
        (material_impact){ 0 }
    );
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(left->count < left_cells && right->count == right_cells);
    REQUIRE(right->cells[neighbor] == 0);
    REQUIRE(fabsf(right->distance[neighbor] - (1.4f - sqrtf(.5f))) < .00001f);
    REQUIRE(
        materials_terrain_checkpoint_write(runtime, source, "world/diagonal", &saved, &saved_length, error, sizeof error)
    );
    REQUIRE(
        materials_terrain_checkpoint_read(runtime, source, "world/diagonal", saved, saved_length, error, sizeof error)
    );
    REQUIRE(!materials_prepare(runtime));
    right = tile(1);
    REQUIRE(right && right->count == right_cells && right->cells[neighbor] == 0);
    REQUIRE(fabsf(right->distance[neighbor] - (1.4f - sqrtf(.5f))) < .00001f);
    return true;
}

TEST_CASE(terrain_damage_preserves_streamed_air_without_an_entity)
{
    REQUIRE(prepare(true, false));
    const field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_CONSTANT, .value = -128.125f },
        { .op = FIELD_ADD, .count = 2, .inputs = { 0, 1 } },
    };
    field_program *program = field_program_new(code, 3, 2, NULL, 0, error, sizeof error);
    REQUIRE(program);
    REQUIRE(material_field_register(materials_fields(runtime), "Rock", program));
    material_stream_view view = { 128, 64, 1, 1 };
    materials_set_stream_views(runtime, &view, 1);
    REQUIRE(!materials_prepare(runtime));
    const material_mask *left = tile(0);
    REQUIRE(left && !tile(1));
    int cells = left->count;
    uint64_t fingerprint;
    REQUIRE(material_checkpoint_fingerprint(
        (material_checkpoint_recipe){ .fields = materials_fields(runtime),
                                      .field = "Rock",
                                      .context = { .seed = 17 },
                                      .grid = { .cell_size = 1 },
                                      .streamed = true },
        &fingerprint,
        error,
        sizeof error
    ));
    for (int stage = 0; stage < 3; stage++) {
        if (stage < 2) {
            materials_damage(
                runtime,
                source,
                (fluid_vec){ 127.5f, 64.5f },
                1.4f,
                stage ? .75f : .25f,
                MATERIAL_DAMAGE_CONTACT,
                (material_impact){ 0 }
            );
        } else {
            REQUIRE(materials_terrain_checkpoint_read(
                runtime,
                source,
                "world/stream-air",
                saved,
                saved_length,
                error,
                sizeof error
            ));
        }
        REQUIRE(!materials_prepare(runtime));
        left = tile(0);
        REQUIRE(left && !tile(1));
        REQUIRE(stage ? left->count < cells : left->count == cells);
        if (!stage) {
            REQUIRE(left->distance[64 * 128 + 127] == -.625f);
        }
        free(saved);
        saved = NULL;
        REQUIRE(materials_terrain_checkpoint_write(
            runtime,
            source,
            "world/stream-air",
            &saved,
            &saved_length,
            error,
            sizeof error
        ));
        material_checkpoint checkpoint = { 0 };
        REQUIRE(
            material_checkpoint_read(saved, saved_length, "world/stream-air", fingerprint, &checkpoint, error, sizeof error)
        );
        REQUIRE(acount(checkpoint.tiles) == 2);
        const material_mask *air = NULL;
        for (int i = 0; i < acount(checkpoint.tiles); i++) {
            if (checkpoint.tiles[i].key == 1) {
                air = checkpoint.tiles[i].mask;
            }
        }
        REQUIRE(air && !air->count);
        /* First weak hit leaves the right tile's original positive field alone.
         * A successful left cut updates it, and checkpoint restore retains it. */
        REQUIRE(fabsf(air->distance[64 * 128] - (stage ? .4f : .375f)) < .00001f);
        material_checkpoint_free(&checkpoint);
    }
    return true;
}

int terrain_checkpoint_child(const char *filename)
{
    if (!prepare(false, true)) {
        return 1;
    }
    FILE *file = fopen(filename, "rb");
    if (!file || fseek(file, 0, SEEK_END)) {
        return 1;
    }
    long length = ftell(file);
    if (length <= 0 || length > MATERIAL_CHECKPOINT_MAX_BYTES ||
        fseek(file, 0, SEEK_SET)) {
        return 1;
    }
    saved_length = (size_t)length;
    saved = malloc(saved_length);
    if (fread(saved, 1, saved_length, file) != saved_length || fclose(file)) {
        return 1;
    }
    bool ok = materials_terrain_checkpoint_read(runtime, source, "world/rock", saved, saved_length, error, sizeof error) &&
              !materials_prepare(runtime) && edited_finite();
    cleanup();
    return ok ? 0 : 1;
}
void test_terrain_checkpoint(const char *binary)
{
    executable = binary;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(finite_checkpoint_roundtrip_and_validation);
    RUN_TEST_CASE(stream_checkpoint_cold_resident_negative_and_empty);
    RUN_TEST_CASE(checkpoint_codec_rejects_duplicate_keys_and_invalid_samples);
    RUN_TEST_CASE(checkpoint_recipe_tracks_resolved_typed_arguments);
    RUN_TEST_CASE(terrain_damage_updates_air_across_partial_chunk_boundary);
    RUN_TEST_CASE(terrain_damage_preserves_streamed_air_without_an_entity);
    pu_clear_setup();
}
