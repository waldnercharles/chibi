#include "assets/field_parameters.h"
#include "authoring/preview.h"
#include "graph/compiler/material_field.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/field_nodes.h"
#include "runtime/component_catalog.h"
#include "runtime/material_ecs.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "runtime/material_terrain.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"
#include "test/runtime/material_fixture.h"
#include "test/runtime/reference_cave.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Loading and compiling a field uses the field and flow packs temporarily. */
static const registry_pack **use_field_nodes()
{
    const registry_pack **previous = NULL;
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &FIELD_NODE_PACK, &CORE_FLOW_PACK };
    registry_set_packs(packs, 2);
    return previous;
}

static void restore_nodes(const registry_pack **previous)
{
    registry_set_packs(previous, acount(previous));
    afree(previous);
}

static behavior_asset *cave_asset()
{
    char *text = text_file_read(
        CHIBI_SOURCE_ROOT
        "/test/fixtures/material-contracts/content/fields/CaveField.kdl"
    );
    assert(text);
    behavior_asset *asset = behavior_asset_load(text);
    free(text);
    assert(asset);
    return asset;
}

/* Compiles CaveField.kdl into a world's material fields. */
static bool compile_cave(material_fields *fields, char *error, size_t size)
{
    const registry_pack **previous = use_field_nodes();
    behavior_asset *asset = cave_asset();
    bool compiled = material_field_compile(fields, asset, error, size);
    behavior_asset_free(asset);
    restore_nodes(previous);
    return compiled;
}

/* Bakes CaveField.kdl, connecting its air. */
static float *cave_field_generate(unsigned seed, int width, int height, float cell, float radius, float roughness)
{
    const registry_pack **previous = use_field_nodes();
    behavior_asset *asset = cave_asset();
    char error[256];
    field_program *program = field_program_compile(asset, error, sizeof error);
    behavior_asset_free(asset);
    restore_nodes(previous);
    assert(program);
    float *out = xmalloc((size_t)width * height * sizeof(float));
    field_parameter parameters[] = { { "Radius", { .type = PIN_F32, .f = radius } },
                                     { "Roughness",
                                       { .type = PIN_F32, .f = roughness } } };
    field_grid grid = { width, height, cell };
    assert(
        field_program_bake(program, (field_context){ seed, parameters, 2 }, grid, out, NULL, error, sizeof error)
    );
    material_terrain_connect(out, grid, MATERIAL_CONNECTIVITY_FIRST_AIR, 8);
    field_program_free(program);
    return out;
}

TEST_CASE(cave_generation)
{
    const int width = 512, height = 256, length = width * height;
    float *a = cave_field_generate(1729, width, height, 2, 72, 30);
    float *b = cave_field_generate(1729, width, height, 2, 72, 30);
    float *c = cave_field_generate(1730, width, height, 2, 72, 30);
    float *reference = reference_cave_generate(1729, width, height, 2, 72, 30);
    float max_error = 0;
    int occupancy_errors = 0;
    for (int i = 0; i < length; i++) {
        max_error = fmaxf(max_error, fabsf(a[i] - reference[i]));
        occupancy_errors += (a[i] <= 0) != (reference[i] <= 0);
    }
    fprintf(stderr, "Cave parity: max distance error %.9g, occupancy errors %d\n", max_error, occupancy_errors);
    REQUIRE(max_error < .0002f && !occupancy_errors);
    free(reference);
    REQUIRE(!memcmp(a, b, (size_t)length * sizeof *a));
    REQUIRE(memcmp(a, c, (size_t)length * sizeof *a));
    int air = 0, first = -1;
    for (int i = 0; i < length; i++) {
        if (a[i] > 0) {
            air++;
            first = i;
        }
    }
    REQUIRE(air > length / 5 && air < length * 4 / 5);
    int *queue = xmalloc((size_t)length * sizeof *queue);
    int end = 1;
    queue[0] = first;
    b[first] = -1;
    for (int head = 0; head < end; head++) {
        int at = queue[head];
        int neighbors[] = { at - 1, at + 1, at - width, at + width };
        for (int j = 0; j < 4; j++) {
            int next = neighbors[j];
            if (next >= 0 && next < length && b[next] > 0) {
                b[next] = -1;
                queue[end++] = next;
            }
        }
    }
    REQUIRE(end == air);
    for (int y = 0; y < height; y += 128) {
        for (int x = 0; x < width; x += 128) {
            float chunk[128 * 128];
            for (int row = 0; row < 128; row++) {
                memcpy(chunk + row * 128, a + (y + row) * width + x, 128 * sizeof *a);
            }
            material_mask mask = material_mask_from_field(128, 128, 2, chunk);
            material_test_check_cover(&mask);
            material_mask_free(&mask);
        }
    }
    free(queue);
    free(a);
    free(b);
    free(c);

    /* Two border-connected pieces remain terrain; an internal island detaches. */
    unsigned char cells[64] = { 0 };
    cells[0] = cells[7] = cells[63] = cells[27] = cells[28] = 1;
    material_mask mask = material_mask_from_cells(8, 8, 2, cells);
    int dust;
    material_mask *pieces = material_mask_extract_fragments(&mask, 2, true, &dust);
    REQUIRE(mask.count == 3 && acount(pieces) == 1 && pieces[0].count == 2 && !dust);
    material_mask_free(&pieces[0]);
    afree(pieces);
    material_mask_free(&mask);
    return true;
}

TEST_CASE(shipping_cave_parity)
{
    const int width = 2048, height = 768, length = width * height;
    float *a = cave_field_generate(1729, width, height, 2, 225, 100);
    float *b = reference_cave_generate(1729, width, height, 2, 225, 100);
    float error = 0;
    int differences = 0;
    for (int i = 0; i < length; i++) {
        error = fmaxf(error, fabsf(a[i] - b[i]));
        differences += (a[i] <= 0) != (b[i] <= 0);
    }
    printf("Shipping cave parity: max distance error %.9g, occupancy errors %d\n", error, differences);
    REQUIRE(error < .001f && !differences);
    free(a);
    free(b);
    return true;
}

TEST_CASE(terrain_boundary_cut)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    char field_error[256];
    REQUIRE(compile_cave(materials_fields(live->runtime->materials), field_error, sizeof field_error));
    REQUIRE(material_field_prepare_arguments(
        materials_fields(live->runtime->materials),
        "CaveField",
        "Radius 72\nRoughness 30\n",
        field_error,
        sizeof field_error
    ));
    ecs_entity settings = ecs_world_create_entity(world);
    material_test_component(
        world,
        settings,
        &COMPONENT_MaterialDestructionWorld,
        &(MaterialDestructionWorld){ 8 }
    );
    material_test_component(
        world,
        settings,
        &COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ .chunk_size = 128, .buffer = 32, .kernel = 10, .particle_limit = 10 }
    );
    ecs_entity source = ecs_world_create_entity(world);
    material_test_component(world, source, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(
        world,
        source,
        &COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .seed = 1729,
                            .width = 512,
                            .height = 512,
                            .cell_size = 2,
                            .field = "CaveField",
                            .parameters = "Radius 72\n"
                                          "Roughness 30\n",
                            .connectivity = MATERIAL_CONNECTIVITY_FIRST_AIR,
                            .border_cells = 8,
                            .category = 8,
                            .mask = 1,
                            .min_fragment_cells = 4 }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!materials_prepare(live->runtime->materials));
    ecs_entity target = { 0 };
    ecs_query query;
    ecs_component_id id = COMPONENT_Destructible.id;
    REQUIRE(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    ecs_entity entity;
    int before = 0;
    while (ecs_query_next(&query, &entity)) {
        const Transform *p = Transform_get_const(world, entity);
        const material_mask *mask = materials_mask(live->runtime->materials, entity);
        before += mask->count;
        if (p->x < 0 && p->y > 0) {
            target = entity;
        }
    }
    ecs_query_end(&query);
    REQUIRE(ecs_entity_to_u64(target));
    material_state initial = materials_inspect(live->runtime->materials);
    materials_damage(live->runtime->materials, target, (fluid_vec){ 0, 220 }, 12, 1, 0, (material_impact){ 0 });
    REQUIRE(!materials_prepare(live->runtime->materials));
    int after = 0, opened = 0;
    REQUIRE(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const Transform *p = Transform_get_const(world, entity);
        const material_mask *mask = materials_mask(live->runtime->materials, entity);
        after += mask->count;
        if (p->y > 0) {
            float x = p->x < 0 ? -1 : 1;
            REQUIRE(!material_mask_contains(mask, x - p->x, 220 - p->y));
            opened++;
        }
    }
    ecs_query_end(&query);
    REQUIRE(opened == 2 && before - after > 100);
    material_state damaged = materials_inspect(live->runtime->materials);
    REQUIRE(initial.hash != damaged.hash);
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).hash == damaged.hash);
    REQUIRE(compile_cave(materials_fields(live->runtime->materials), field_error, sizeof field_error));
    REQUIRE(material_field_prepare_arguments(
        materials_fields(live->runtime->materials),
        "CaveField",
        "Radius 72\nRoughness 30\n",
        field_error,
        sizeof field_error
    ));
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).cells == initial.cells);
    REQUIRE(!ecs_entity_is_alive(world, target));
    MaterialTerrain *terrain = MaterialTerrain_get(world, source);
    const char *original_parameters = terrain->parameters;
    terrain->parameters = "Radius 96\n"
                          "Roughness 30\n";
    REQUIRE(material_field_prepare_arguments(
        materials_fields(live->runtime->materials),
        terrain->field,
        terrain->parameters,
        field_error,
        sizeof field_error
    ));
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).cells < initial.cells);
    terrain = MaterialTerrain_get(world, source);
    terrain->parameters = original_parameters;
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).cells == initial.cells);
    materials_reset(live->runtime->materials);
    REQUIRE(ecs_world_live_count(world) == 2);
    materials_damage(live->runtime->materials, source, (fluid_vec){ 0, 220 }, 12, 1, 0, (material_impact){ 0 });
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).cells == damaged.cells);
    REQUIRE(ecs_world_live_count(world) > 2);
    REQUIRE(ecs_world_destroy_entity(world, source) == ECS_OK);
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(ecs_world_live_count(world) == 1);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

TEST_CASE(terrain_stream_restore)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    char error[256];
    REQUIRE(compile_cave(materials_fields(live->runtime->materials), error, sizeof error));
    REQUIRE(material_field_prepare_arguments(
        materials_fields(live->runtime->materials),
        "CaveField",
        "Radius 72\nRoughness 30\n",
        error,
        sizeof error
    ));
    ecs_entity camera = ecs_world_create_entity(world);
    material_test_component(world, camera, &COMPONENT_Camera, &(Camera){ .width = 128, .height = 128 });
    material_test_component(world, camera, &COMPONENT_Transform, &(Transform){ -1500, 0 });
    ecs_entity source = ecs_world_create_entity(world);
    material_test_component(world, source, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(
        world,
        source,
        &COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .seed = 1729,
                            .width = 4096,
                            .height = 512,
                            .cell_size = 2,
                            .field = "CaveField",
                            .parameters = "Radius 72\n"
                                          "Roughness 30\n",
                            .category = 8,
                            .mask = 1,
                            .min_fragment_cells = 4,
                            .stream_radius = 128,
                            .stream_budget = 64 }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!materials_prepare(live->runtime->materials));
    material_state pristine = materials_inspect(live->runtime->materials);
    REQUIRE(pristine.streaming.resident > 0 && pristine.cells > 0);
    ecs_component_id id = COMPONENT_Destructible.id;
    ecs_query query;
    ecs_entity entity, target = { 0 };
    Transform position = { 0 };
    fluid_vec point = { 0 };
    REQUIRE(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const material_mask *mask = materials_mask(live->runtime->materials, entity);
        for (int y = 16; y < mask->height - 16 && !ecs_entity_to_u64(target); y++) {
            for (int x = 16; x < mask->width - 16; x++) {
                if (!mask->cells[y * mask->width + x]) {
                    continue;
                }
                target = entity;
                position = *Transform_get_const(world, entity);
                point = (fluid_vec){
                    position.x + (x + .5f - mask->width * .5f) * mask->cell_size,
                    position.y + (y + .5f - mask->height * .5f) * mask->cell_size
                };
                break;
            }
        }
    }
    ecs_query_end(&query);
    REQUIRE(ecs_entity_to_u64(target));
    materials_damage(live->runtime->materials, target, point, 10, 1, 0, (material_impact){ 0 });
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).cells < pristine.cells);
    REQUIRE(!material_mask_contains(
        materials_mask(live->runtime->materials, target),
        point.x - position.x,
        point.y - position.y
    ));
    Transform_get(world, camera)->x = 1500;
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(!ecs_entity_is_alive(world, target));
    REQUIRE(materials_inspect(live->runtime->materials).streaming.unloaded > 0);
    Transform_get(world, camera)->x = -1500;
    REQUIRE(!materials_prepare(live->runtime->materials));
    bool restored = false;
    REQUIRE(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const Transform *p = Transform_get_const(world, entity);
        if (p->x == position.x && p->y == position.y) {
            REQUIRE(!material_mask_contains(
                materials_mask(live->runtime->materials, entity),
                point.x - p->x,
                point.y - p->y
            ));
            restored = true;
        }
    }
    ecs_query_end(&query);
    REQUIRE(restored);
    REQUIRE(physics_tick(live->runtime->physics, 1.f / 60) == 0);
    physics_reset(live->runtime->physics);
    materials_reset(live->runtime->materials);
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).cells == pristine.cells);
    REQUIRE(!materials_inspect(live->runtime->materials).streaming.stored);
    MaterialTerrain authored = *MaterialTerrain_get_const(world, source);
    REQUIRE(ecs_world_record_remove(world, source, COMPONENT_MaterialTerrain.id, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(!materials_inspect(live->runtime->materials).streaming.resident);
    REQUIRE(ecs_world_live_count(world) == 2);
    material_test_component(world, source, &COMPONENT_MaterialTerrain, &authored);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(materials_inspect(live->runtime->materials).cells == pristine.cells);
    REQUIRE(ecs_world_destroy_entity(world, source) == ECS_OK);
    REQUIRE(!materials_prepare(live->runtime->materials));
    REQUIRE(!materials_inspect(live->runtime->materials).streaming.resident);
    REQUIRE(ecs_world_live_count(world) == 1);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

/* A host's stream views prepare terrain beside the cameras', streaming waits
 * until they are filled, and a reset that keeps terrain keeps what streamed. */
TEST_CASE(terrain_host_views)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    char error[256];
    REQUIRE(compile_cave(materials_fields(live->runtime->materials), error, sizeof error));
    REQUIRE(material_field_prepare_arguments(
        materials_fields(live->runtime->materials),
        "CaveField",
        "Radius 72\nRoughness 30\n",
        error,
        sizeof error
    ));
    ecs_entity camera = ecs_world_create_entity(world);
    material_test_component(world, camera, &COMPONENT_Camera, &(Camera){ .width = 128, .height = 128 });
    material_test_component(world, camera, &COMPONENT_Transform, &(Transform){ -1500, 0 });
    ecs_entity source = ecs_world_create_entity(world);
    material_test_component(world, source, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(
        world,
        source,
        &COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .seed = 1729,
                            .width = 4096,
                            .height = 512,
                            .cell_size = 2,
                            .field = "CaveField",
                            .parameters = "Radius 72\n"
                                          "Roughness 30\n",
                            .category = 8,
                            .mask = 1,
                            .min_fragment_cells = 4,
                            .stream_radius = 128,
                            .stream_budget = 64 }
    );
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(!materials_prepare(live->runtime->materials));
    int cameras = materials_inspect(live->runtime->materials).streaming.resident;
    material_stream_view view = { 1500, 0, 900, 0 };
    materials_set_stream_views(live->runtime->materials, &view, 1);
    REQUIRE(!materials_prepare(live->runtime->materials));
    bool waited = materials_streaming(live->runtime->materials);
    for (int i = 0; i < 64 && materials_streaming(live->runtime->materials); i++) {
        REQUIRE(!materials_prepare(live->runtime->materials));
    }
    int resident = materials_inspect(live->runtime->materials).streaming.resident;
    uint32_t entities = ecs_world_live_count(world);
    materials_reset_except_terrain(live->runtime->materials);
    REQUIRE(!materials_prepare(live->runtime->materials));
    bool kept = ecs_world_live_count(world) == entities &&
                materials_inspect(live->runtime->materials).streaming.resident == resident;
    materials_reset(live->runtime->materials);
    bool cleared = ecs_world_live_count(world) == 2;
    REQUIRE(waited && !materials_streaming(live->runtime->materials) && resident > cameras);
    REQUIRE(kept && cleared);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

/* Two terrain origins share preparation capacity but keep separate streams. */
TEST_CASE(terrain_preparation_scratch)
{
    ecs_world *worlds[2];
    preview_world *lives[2];
    ecs_entity sources[2][2];
    ecs_entity cameras[2][2];
    for (int owner = 0; owner < 2; owner++) {
        ecs_world *world = worlds[owner] = ecs_world_new();
        for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
            REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
        }
        preview_world *live = lives[owner] = preview_world_new(world);
        char error[256];
        REQUIRE(compile_cave(materials_fields(live->runtime->materials), error, sizeof error));
        REQUIRE(material_field_prepare_arguments(
            materials_fields(live->runtime->materials),
            "CaveField",
            "Radius 72\nRoughness 30\n",
            error,
            sizeof error
        ));
        for (int origin = 0; origin < 2; origin++) {
            float x = (origin + 1) * 1024.f;
            ecs_entity camera = cameras[owner][origin] = ecs_world_create_entity(world);
            material_test_component(
                world,
                camera,
                &COMPONENT_Camera,
                &(Camera){ .width = 128, .height = 128 }
            );
            material_test_component(world, camera, &COMPONENT_Transform, &(Transform){ x, 0 });
            ecs_entity source = sources[owner][origin] = ecs_world_create_entity(world);
            material_test_component(world, source, &COMPONENT_Transform, &(Transform){ x, 0 });
            material_test_component(
                world,
                source,
                &COMPONENT_MaterialTerrain,
                &(MaterialTerrain){ .seed = 1729,
                                    .width = 512,
                                    .height = 512,
                                    .cell_size = 2,
                                    .field = "CaveField",
                                    .parameters = "Radius 72\n"
                                                  "Roughness 30\n",
                                    .category = 8,
                                    .mask = 1,
                                    .min_fragment_cells = 4,
                                    .stream_radius = 128,
                                    .stream_budget = 64 }
            );
        }
        ecs_entity moving = ecs_world_create_entity(world);
        material_test_component(world, moving, &COMPONENT_Body, &(Body){ .type = BODY_DYNAMIC });
        material_test_component(world, moving, &COMPONENT_Transform, &(Transform){ 2048, 0 });
        material_test_component(world, moving, &COMPONENT_Velocity, &(Velocity){ 2, 0 });
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        REQUIRE(!materials_prepare(live->runtime->materials));
        REQUIRE(!materials_prepare(live->runtime->materials));
        materials *materials = live->runtime->materials;
        REQUIRE(materials->terrain_views && !acount(materials->terrain_views));
        int cells = materials_inspect(materials).cells;
        for (int origin = 0; origin < 2; origin++) {
            material_stream
                *stream = materials->records[sources[owner][origin].slot].stream;
            int count;
            const material_stream_tile *tiles = material_stream_tiles(stream, &count);
            REQUIRE(count > 0);
            for (int i = 0; i < count; i++) {
                REQUIRE(fabsf(tiles[i].grid.center_x) <= 256);
            }
        }
        material_stream_view *storage = materials->terrain_views;
        int capacity = acap(storage);
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        for (int i = 0; i < 3; i++) {
            REQUIRE(!material_ecs_generate_terrains(materials));
        }
        xalloc_stats_get(&after);
        /* Each preparation allocates only its source list; warmed view
         * preparation contributes no further allocations. */
        REQUIRE(after.calls - before.calls == 3);
        REQUIRE(materials->terrain_views == storage && acap(storage) == capacity);
        REQUIRE(materials_inspect(materials).cells == cells);
        /* Empty interest after populated preparation must not retain views. */
        for (int origin = 0; origin < 2; origin++) {
            Camera_get(world, cameras[owner][origin])->disabled = true;
        }
        *Velocity_get(world, moving) = (Velocity){ 0 };
        REQUIRE(!material_ecs_generate_terrains(materials));
        REQUIRE(!materials_inspect(materials).streaming.resident);
        REQUIRE(materials->terrain_views == storage && !acount(materials->terrain_views));
        for (int origin = 0; origin < 2; origin++) {
            Camera_get(world, cameras[owner][origin])->disabled = false;
        }
        Velocity_get(world, moving)->x = 2;
        materials_reset(materials);
        REQUIRE(materials->terrain_views == storage && acap(storage) == capacity);
        REQUIRE(!acount(materials->terrain_views) && !acount(materials->debris_tiles));
        REQUIRE(!materials_prepare(materials));
        REQUIRE(materials_inspect(materials).cells == cells);
    }
    REQUIRE(
        lives[0]->runtime->materials->terrain_views !=
        lives[1]->runtime->materials->terrain_views
    );
    preview_world_free(lives[0]);
    ecs_world_free(worlds[0]);
    REQUIRE(!materials_prepare(lives[1]->runtime->materials));
    preview_world_free(lives[1]);
    ecs_world_free(worlds[1]);
    return true;
}

/* A 512-unit cave terrain with a terrain volume behind it, prepared. */
static ecs_entity volume_terrain(ecs_world *world, materials *runtime)
{
    char error[256];
    bool compiled = compile_cave(materials_fields(runtime), error, sizeof error) &&
                    material_field_prepare_arguments(
                        materials_fields(runtime),
                        "CaveField",
                        "Radius 72\nRoughness 30\n",
                        error,
                        sizeof error
                    );
    assert(compiled);
    (void)compiled;
    ecs_entity settings = ecs_world_create_entity(world);
    material_test_component(
        world,
        settings,
        &COMPONENT_MaterialDestructionWorld,
        &(MaterialDestructionWorld){ 8 }
    );
    material_test_component(
        world,
        settings,
        &COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ .chunk_size = 128, .buffer = 32, .kernel = 10, .particle_limit = 10 }
    );
    ecs_entity source = ecs_world_create_entity(world);
    material_test_component(world, source, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(
        world,
        source,
        &COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .seed = 1729,
                            .width = 512,
                            .height = 512,
                            .cell_size = 2,
                            .field = "CaveField",
                            .parameters = "Radius 72\n"
                                          "Roughness 30\n",
                            .connectivity = MATERIAL_CONNECTIVITY_FIRST_AIR,
                            .border_cells = 8,
                            .category = 8,
                            .mask = 1,
                            .min_fragment_cells = 4 }
    );
    material_test_component(
        world,
        source,
        &COMPONENT_TerrainVolume,
        &(
            TerrainVolume
        ){ .enabled = true, .voxel = 4, .depth = 64, .elevation = 8, .refine = 2, .slab = 32 }
    );
    material_test_component(
        world,
        source,
        &COMPONENT_MaterialSurface3D,
        &(MaterialSurface3D){ .enabled = true, .elevation = 8, .depth = 6, .spacing = 2 }
    );
    int result = ecs_world_apply_commands(world);
    assert(result == ECS_OK);
    (void)result;
    const char *prepared = materials_prepare(runtime);
    assert(!prepared);
    (void)prepared;
    return source;
}

static TerrainVolume *volume_style(ecs_world *world, ecs_entity source)
{
    return ecs_component_data(world, source, COMPONENT_TerrainVolume.id);
}

/* The terrain tile left of the middle and below it. */
static ecs_entity lower_left_tile(ecs_world *world)
{
    ecs_entity target = { 0 }, entity;
    ecs_query query;
    ecs_component_id id = COMPONENT_Destructible.id;
    int result = ecs_query_begin(world, &query, &id, 1);
    assert(result == ECS_OK);
    (void)result;
    while (ecs_query_next(&query, &entity)) {
        const Transform *p = Transform_get_const(world, entity);
        if (p->x < 0 && p->y > 0) {
            target = entity;
        }
    }
    ecs_query_end(&query);
    return target;
}

/* Every fragment's surface straddles the plane at elevation 8; pieces counts
 * them. */
static bool fragments_centered(ecs_world *world, materials *runtime, int *pieces)
{
    bool centered = true;
    *pieces = 0;
    ecs_entity entity;
    ecs_query query;
    ecs_component_id id = COMPONENT_Destructible.id;
    int result = ecs_query_begin(world, &query, &id, 1);
    assert(result == ECS_OK);
    (void)result;
    while (ecs_query_next(&query, &entity)) {
        if (!materials_is_fragment(runtime, entity)) {
            continue;
        }
        const MaterialSurface3D *chunk = MaterialSurface3D_get_const(world, entity);
        centered &= fabsf(chunk->elevation - chunk->depth / 2 - 8) < 1e-3f;
        ++*pieces;
    }
    ecs_query_end(&query);
    return centered;
}

/* A piece cut free of rock with a terrain volume behind it is about as deep
 * as it is wide and straddles the play plane, as do the pieces it splits
 * into, and the volume's carve follows the cut. */
TEST_CASE(terrain_volume_fragments_are_chunks)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    materials *runtime = live->runtime->materials;
    ecs_entity source = volume_terrain(world, runtime);
    materials_depth_volume view;
    REQUIRE(materials_terrain_volume(runtime, source, &view));
    uint64_t revision = view.revision;
    ecs_entity target = lower_left_tile(world);
    REQUIRE(ecs_entity_to_u64(target));
    /* Overlapping disks around solid rock leave its middle loose. */
    for (int k = 0; k < 12; k++) {
        float angle = k * 6.2831853f / 12;
        fluid_vec at = { -60 + 16 * cosf(angle), 200 + 16 * sinf(angle) };
        materials_damage(runtime, target, at, 6, 1, 0, (material_impact){ 0 });
    }
    REQUIRE(!materials_prepare(runtime));
    ecs_entity piece = { 0 }, entity;
    ecs_query query;
    ecs_component_id id = COMPONENT_Destructible.id;
    REQUIRE(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        if (materials_is_fragment(runtime, entity)) {
            piece = entity;
        }
    }
    ecs_query_end(&query);
    REQUIRE(ecs_entity_to_u64(piece));
    const material_mask *mask = materials_mask(runtime, piece);
    float side = sqrtf((float)mask->count) * mask->cell_size;
    const MaterialSurface3D *chunk = MaterialSurface3D_get_const(world, piece);
    REQUIRE(side > 6 && fabsf(chunk->depth - side) < 1e-3f);
    REQUIRE(fabsf(chunk->elevation - (8 + side / 2)) < 1e-3f);
    REQUIRE(materials_terrain_volume(runtime, source, &view) && view.revision > revision);
    /* A strip of disks through the piece's middle splits it in two. */
    float cx = 0, cy = 0, area = 0;
    for (int i = 0; i < acount(mask->rects); i++) {
        material_rect r = mask->rects[i];
        cx += r.x * r.width * r.height;
        cy += r.y * r.width * r.height;
        area += r.width * r.height;
    }
    const Transform *at = Transform_get_const(world, piece);
    fluid_vec middle = { at->x + cx / area, at->y + cy / area };
    for (int k = -4; k <= 4; k++) {
        fluid_vec hit = { middle.x, middle.y + k * 3.f };
        materials_damage(runtime, piece, hit, 2.5f, 1, 0, (material_impact){ 0 });
    }
    REQUIRE(!materials_prepare(runtime));
    int pieces;
    REQUIRE(fragments_centered(world, runtime, &pieces) && pieces >= 2);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

/* A restored save keeps the terrain volume and carves it to the restored
 * mask. Elevation only places the drawn volume, so editing it keeps cuts;
 * slab edits rebake a volume that reports nothing until carved; invalid
 * settings fail before the terrain is replaced. */
TEST_CASE(terrain_volume_restore_and_settings)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    materials *runtime = live->runtime->materials;
    ecs_entity source = volume_terrain(world, runtime);
    ecs_entity target = lower_left_tile(world);
    REQUIRE(ecs_entity_to_u64(target));
    materials_damage(runtime, target, (fluid_vec){ -60, 200 }, 12, 1, 0, (material_impact){ 0 });
    REQUIRE(!materials_prepare(runtime));
    materials_depth_volume cut, view;
    REQUIRE(materials_terrain_volume(runtime, source, &cut));
    size_t samples = (size_t)cut.volume->plane.width * cut.volume->plane.height;
    float *removed = xmalloc(samples * sizeof *removed);
    memcpy(removed, cut.removed, samples * sizeof *removed);
    bool opened = false;
    for (size_t i = 0; i < samples; i++) {
        opened |= removed[i] > 0;
    }
    REQUIRE(opened);
    volume_style(world, source)->elevation = 12;
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(materials_terrain_volume(runtime, source, &view));
    REQUIRE(view.identity == cut.identity && ecs_entity_is_alive(world, target));
    volume_style(world, source)->voxel = -1;
    REQUIRE(materials_prepare(runtime));
    REQUIRE(ecs_entity_is_alive(world, target));
    volume_style(world, source)->voxel = 4;
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(materials_terrain_volume(runtime, source, &view) && view.identity == cut.identity);
    char error[256];
    unsigned char *bytes;
    size_t length;
    REQUIRE(
        materials_terrain_checkpoint_write(runtime, source, "rock", &bytes, &length, error, sizeof error)
    );
    REQUIRE(
        materials_terrain_checkpoint_read(runtime, source, "rock", bytes, length, error, sizeof error)
    );
    free(bytes);
    REQUIRE(!ecs_entity_is_alive(world, target));
    REQUIRE(materials_terrain_volume(runtime, source, &view));
    REQUIRE(view.identity == cut.identity && view.revision > cut.revision);
    REQUIRE(!memcmp(view.removed, removed, samples * sizeof *removed));
    REQUIRE(!materials_prepare(runtime));
    free(removed);
    materials_reset(runtime);
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(materials_terrain_volume(runtime, source, &cut));
    volume_style(world, source)->slab = 16;
    REQUIRE(!material_ecs_generate_terrains(runtime));
    REQUIRE(!materials_terrain_volume(runtime, source, &view));
    REQUIRE(!materials_prepare(runtime));
    REQUIRE(materials_terrain_volume(runtime, source, &view));
    REQUIRE(view.identity != cut.identity && view.volume->slab == 16);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

void test_material_cave_terrain()
{
    RUN_TEST_CASE(cave_generation);
    RUN_TEST_CASE(shipping_cave_parity);
    RUN_TEST_CASE(terrain_boundary_cut);
    RUN_TEST_CASE(terrain_volume_fragments_are_chunks);
    RUN_TEST_CASE(terrain_volume_restore_and_settings);
    RUN_TEST_CASE(terrain_stream_restore);
    RUN_TEST_CASE(terrain_host_views);
    RUN_TEST_CASE(terrain_preparation_scratch);
}
