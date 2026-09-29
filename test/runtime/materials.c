#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/material_terrain.h"
#include "support/text_file.h"
float *reference_cave_generate(unsigned, int, int, float, float, float);
#include "runtime/material_debris.h"
#include "runtime/material_destruction.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "runtime/material_solid.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "test/runtime/material_substances.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check_cover(const material_mask *mask)
{
    int total = 0;
    for (int y = 0; y < mask->height; y++) {
        for (int x = 0; x < mask->width; x++) {
            float px = (x + .5f - mask->width * .5f) * mask->cell_size;
            float py = (y + .5f - mask->height * .5f) * mask->cell_size;
            int covers = 0;
            for (int i = 0; i < acount(mask->rects); i++) {
                material_rect r = mask->rects[i];
                covers += fabsf(px - r.x) < r.width * .5f &&
                          fabsf(py - r.y) < r.height * .5f;
            }
            assert(covers == mask->cells[y * mask->width + x]);
            assert(material_mask_contains(mask, px, py) == (covers == 1));
            total += covers;
        }
    }
    assert(total == mask->count);
}

static void destruction()
{
    material_mask mask = material_mask_new(24, 12, 2, false);
    assert(acount(mask.rects) == 1);
    int before = mask.count;
    int removed = material_mask_carve(&mask, 0, 0, 13);
    assert(removed > 0);
    assert(!material_mask_contains(&mask, 0, 0));
    check_cover(&mask);
    int dust;
    material_mask *pieces = material_mask_extract_fragments(&mask, 3, false, &dust);
    assert(acount(pieces) == 1);
    int retained = mask.count + dust;
    for (int i = 0; i < acount(pieces); i++) {
        check_cover(&pieces[i]);
        retained += pieces[i].count;
        for (int j = 0; j < mask.width * mask.height; j++) {
            assert(!(mask.cells[j] && pieces[i].cells[j]));
        }
        material_mask_free(&pieces[i]);
    }
    assert(retained + removed == before);
    afree(pieces);
    check_cover(&mask);
    material_mask_carve(&mask, 0, 0, 100);
    assert(!mask.count && !acount(mask.rects));
    material_mask_free(&mask);

    mask = material_mask_new(24, 12, 2, true);
    assert(mask.count < 24 * 12);
    check_cover(&mask);
    material_mask_carve(&mask, 0, 0, 13);
    before = mask.count;
    pieces = material_mask_extract_fragments(&mask, 1000, false, &dust);
    assert(!acount(pieces) && dust > 0 && mask.count + dust == before);
    check_cover(&mask);
    material_mask_free(&mask);
}

static bool surface_contains(const material_mask *mask, float x, float y)
{
    for (int i = 0; i < acount(mask->surface); i++) {
        const material_polygon *p = &mask->surface[i];
        bool inside = false;
        for (int a = 0, b = p->count - 1; a < p->count; b = a++) {
            float ax = p->points[2 * a], ay = p->points[2 * a + 1];
            float bx = p->points[2 * b], by = p->points[2 * b + 1];
            if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax) {
                inside = !inside;
            }
        }
        if (inside) {
            return true;
        }
    }
    return false;
}

static void contours()
{
    material_mask mask = material_mask_new(128, 128, 2, false);
    /* Solid chunks remain one rectangle with no gaps along their shared edges. */
    assert(acount(mask.surface) == 1);
    assert(surface_contains(&mask, -127.99f, 0));
    assert(surface_contains(&mask, 127.99f, 0));
    material_mask_free(&mask);
    unsigned char cells[] = { 1, 0, 0, 1 };
    mask = material_mask_from_cells(2, 2, 1, cells);
    assert(material_mask_contains(&mask, -.1f, -.1f));
    assert(!surface_contains(&mask, -.1f, -.1f));
    assert(surface_contains(&mask, -.4f, -.4f));
    assert(surface_contains(&mask, .4f, .4f));
    assert(!surface_contains(&mask, 0, 0)); /* No false bridge across diagonal islands. */
    material_mask_carve(&mask, -.5f, -.5f, .1f);
    assert(!surface_contains(&mask, -.4f, -.4f));
    assert(surface_contains(&mask, .4f, .4f));
    material_mask_free(&mask);
    float field[] = { -.1f, .9f, -.1f, .9f };
    mask = material_mask_from_field(2, 2, 1, field);
    assert(surface_contains(&mask, -.45f, .1f));
    assert(!surface_contains(&mask, -.35f, .1f));
    material_mask_free(&mask);
    mask = material_mask_new(16, 16, 1, false);
    material_mask_carve(&mask, 0, 0, 2.2f);
    assert(!surface_contains(&mask, 2.1f, .01f));
    assert(surface_contains(&mask, 2.3f, .01f));
    material_mask_free(&mask);
}

static behavior_asset *cave_asset()
{
    char *json = text_file_read("projects/arena2d/content/fields/CaveField.yaml");
    assert(json);
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    assert(asset);
    return asset;
}
static float *cave_field_generate(unsigned seed, int width, int height, float cell, float radius, float roughness)
{
    behavior_asset *asset = cave_asset();
    char error[256];
    field_program *program = field_program_compile(asset, error, sizeof error);
    assert(program);
    float *out = malloc((size_t)width * height * sizeof(float));
    assert(out);
    field_parameter parameters[] = { { "Radius", { .type = PIN_F32, .f = radius } },
                                     { "Roughness",
                                       { .type = PIN_F32, .f = roughness } } };
    field_grid grid = { width, height, cell };
    assert(
        field_program_bake(program, (field_context){ seed, parameters, 2 }, grid, out, NULL, error, sizeof error)
    );
    material_terrain_connect(out, grid, MATERIAL_CONNECTIVITY_FIRST_AIR, 8);
    field_program_free(program);
    behavior_asset_free(asset);
    return out;
}

static void cave_generation()
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
    assert(max_error < .0002f && !occupancy_errors);
    free(reference);
    assert(!memcmp(a, b, (size_t)length * sizeof *a));
    assert(memcmp(a, c, (size_t)length * sizeof *a));
    int air = 0, first = -1;
    for (int i = 0; i < length; i++) {
        if (a[i] > 0) {
            air++;
            first = i;
        }
    }
    assert(air > length / 5 && air < length * 4 / 5);
    int *queue = malloc((size_t)length * sizeof *queue);
    assert(queue);
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
    assert(end == air);
    for (int y = 0; y < height; y += 128) {
        for (int x = 0; x < width; x += 128) {
            float chunk[128 * 128];
            for (int row = 0; row < 128; row++) {
                memcpy(chunk + row * 128, a + (y + row) * width + x, 128 * sizeof *a);
            }
            material_mask mask = material_mask_from_field(128, 128, 2, chunk);
            check_cover(&mask);
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
    assert(mask.count == 3 && acount(pieces) == 1 && pieces[0].count == 2 && !dust);
    material_mask_free(&pieces[0]);
    afree(pieces);
    material_mask_free(&mask);
}

static void shipping_cave_parity()
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
    assert(error < .001f && !differences);
    free(a);
    free(b);
}

static fluid_particle particle(float x, float y, int substance)
{
    return test_particle(x, y, substance);
}

static void tick(material_fluid *fluid)
{
    fluid_region region = { -1000, -1000, 2000, 2000 };
    material_fluid_tick(
        fluid,
        &(fluid_step){ .delta = 1.f / 60,
                       .chunk_size = 64,
                       .regions = &region,
                       .region_count = 1,
                       .colliders = { 0 } }
    );
}

static void sleeping()
{
    material_fluid *fluid = test_fluid(10, 2);
    fluid_particle p = particle(-30, 20, FLUID_GAS);
    p.velocity.x = 12;
    p.lifetime = .05f;
    assert(material_fluid_add(fluid, p));
    assert(material_fluid_add(fluid, particle(500, 0, FLUID_GAS)));
    assert(!material_fluid_add(fluid, p));
    for (int i = 0; i < 100; i++) {
        material_fluid_tick(
            fluid,
            &(fluid_step){ .delta = 1.f / 60,
                           .chunk_size = 64,
                           .regions = NULL,
                           .region_count = 0,
                           .colliders = { 0 } }
        );
    }
    int count;
    const fluid_particle *values = material_fluid_particles(fluid, &count);
    assert(count == 2 && !memcmp(&values[0], &p, sizeof p));
    assert(!material_fluid_active_count(fluid));
    /* A region elsewhere in the same negative-coordinate chunk wakes it. */
    fluid_region region = { -60, 5, 2, 2 };
    material_fluid_tick(
        fluid,
        &(fluid_step){ .delta = 1.f / 60,
                       .chunk_size = 64,
                       .regions = &region,
                       .region_count = 1,
                       .colliders = { 0 } }
    );
    assert(material_fluid_active_count(fluid) == 1);
    assert(fabsf(values[0].position.x - (p.position.x + .2f)) < .001f);
    assert(values[1].position.x == 500);
    for (int i = 0; i < 4; i++) {
        material_fluid_tick(
            fluid,
            &(fluid_step){ .delta = 1.f / 60,
                           .chunk_size = 64,
                           .regions = &region,
                           .region_count = 1,
                           .colliders = { 0 } }
        );
    }
    values = material_fluid_particles(fluid, &count);
    assert(count == 1 && values[0].position.x == 500);
    material_fluid_free(fluid);
}

static void cohesion()
{
    material_fluid *fluid = test_fluid(10, 1000);
    for (int y = -8; y <= 8; y++) {
        for (int x = -8; x <= 8; x++) {
            if (x * x + y * y <= 64) {
                assert(material_fluid_add(fluid, particle(x * 4, y * 4, FLUID_LIQUID)));
            }
        }
    }
    for (int step = 0; step < 600; step++) {
        tick(fluid);
    }
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    fluid_vec center = { 0 }, momentum = { 0 };
    float radius = 0;
    for (int i = 0; i < count; i++) {
        assert(isfinite(p[i].position.x) && isfinite(p[i].position.y));
        center.x += p[i].position.x;
        center.y += p[i].position.y;
        momentum.x += p[i].mass * p[i].velocity.x;
        momentum.y += p[i].mass * p[i].velocity.y;
        radius = fmaxf(radius, hypotf(p[i].position.x, p[i].position.y));
    }
    assert(count == 197);
    assert(hypotf(center.x, center.y) / count < .05f);
    assert(hypotf(momentum.x, momentum.y) < .02f);
    assert(radius > 15 && radius < 40);
    material_fluid_free(fluid);
}

static int liquid_clusters(const material_fluid *fluid)
{
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    bool *seen = calloc((size_t)count, sizeof *seen);
    int *queue = malloc((size_t)count * sizeof *queue);
    assert(seen && queue);
    int clusters = 0;
    for (int i = 0; i < count; i++) {
        if (seen[i]) {
            continue;
        }
        clusters++;
        seen[i] = true;
        queue[0] = i;
        int end = 1;
        for (int head = 0; head < end; head++) {
            for (int j = 0; j < count; j++) {
                if (!seen[j] && hypotf(
                                    p[queue[head]].position.x - p[j].position.x,
                                    p[queue[head]].position.y - p[j].position.y
                                ) < 10) {
                    seen[j] = true;
                    queue[end++] = j;
                }
            }
        }
    }
    free(seen);
    free(queue);
    return clusters;
}

static void liquid_split_merge()
{
    material_fluid *fluid = test_fluid(10, 1000);
    for (int side = -1; side <= 1; side += 2) {
        for (int y = -4; y <= 4; y++) {
            for (int x = -4; x <= 4; x++) {
                if (x * x + y * y > 16) {
                    continue;
                }
                fluid_particle p = particle(x * 4 + side * 26, y * 4, FLUID_LIQUID);
                p.velocity.x = -side * 15;
                assert(material_fluid_add(fluid, p));
            }
        }
    }
    assert(liquid_clusters(fluid) == 2);
    /* Blobs drifting together join into one body when they meet. */
    bool joined = false;
    for (int i = 0; i < 90; i++) {
        tick(fluid);
        joined |= liquid_clusters(fluid) == 1;
    }
    assert(joined);
    material_fluid_clear(fluid);
    for (int y = -6; y <= 6; y++) {
        for (int x = -6; x <= 6; x++) {
            if (x * x + y * y > 36) {
                continue;
            }
            fluid_particle p = particle(x * 4, y * 4, FLUID_LIQUID);
            p.velocity.x = x < 0 ? -80 : 80;
            assert(material_fluid_add(fluid, p));
        }
    }
    assert(liquid_clusters(fluid) == 1);
    for (int i = 0; i < 90; i++) {
        tick(fluid);
    }
    assert(liquid_clusters(fluid) >= 2);
    int count;
    material_fluid_particles(fluid, &count);
    assert(count == 113);
    material_fluid_free(fluid);
}

static void body_response()
{
    material_fluid *fluid = test_fluid(10, 2);
    fluid_particle p = particle(9, 0, FLUID_GAS);
    p.velocity.x = -10;
    material_fluid_add(fluid, p);
    fluid_body body = { .inverse_mass = 2 };
    fluid_obstacle obstacle = { .radius = 10 };
    fluid_region region = { -100, -100, 200, 200 };
    material_fluid_tick(
        fluid,
        &(fluid_step){ .delta = 1.f / 60,
                       .chunk_size = 64,
                       .regions = &region,
                       .region_count = 1,
                       .colliders = { &body, 1, &obstacle, 1 } }
    );
    int count;
    const fluid_particle *out = material_fluid_particles(fluid, &count);
    assert(out[0].position.x >= 10 && body.impulse.x < 0);
    assert(
        fabsf(
            out[0].mass * out[0].velocity.x + body.impulse.x -
            p.mass * p.velocity.x
        ) < .001f
    );
    material_fluid_clear(fluid);
    p.position.x = 9;
    p.velocity.x = 0;
    material_fluid_add(fluid, p);
    body.velocity.x = 40;
    material_fluid_tick(
        fluid,
        &(fluid_step){ .delta = 1.f / 60,
                       .chunk_size = 64,
                       .regions = &region,
                       .region_count = 1,
                       .colliders = { &body, 1, &obstacle, 1 } }
    );
    out = material_fluid_particles(fluid, &count);
    assert(out[0].velocity.x > 0 && body.impulse.x < 0);
    material_fluid_free(fluid);
}

static void gas_flow()
{
    /* Independent noncombat consumer: a probe stirs initially still gas. */
    material_fluid *fluid = test_fluid(10, 400);
    material_fluid_set_flow(fluid, (fluid_flow_settings){ 10, 2, .1f });
    for (int y = -8; y <= 8; y++) {
        for (int x = -8; x <= 8; x++) {
            material_fluid_add(fluid, particle(x * 10 + 5, y * 10 + 5, FLUID_GAS));
        }
    }
    for (int step = 0; step < 5; step++) {
        tick(fluid);
    }
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    for (int i = 0; i < count; i++) {
        assert(p[i].velocity.x == 0 && p[i].velocity.y == 0);
    }
    fluid_body probe = { .position = { -20, 0 }, .velocity = { 60, 0 } };
    fluid_obstacle obstacle = { .radius = 10 };
    fluid_region region = { -128, -128, 256, 256 };
    material_fluid_tick(
        fluid,
        &(fluid_step){ .delta = 1.f / 60,
                       .chunk_size = 64,
                       .regions = &region,
                       .region_count = 1,
                       .colliders = { &probe, 1, &obstacle, 1 } }
    );
    float lateral = 0;
    for (int i = 0; i < count; i++) {
        /* These parcels never touch the probe. Pressure must turn the flow. */
        if (p[i].position.x > 0 && fabsf(p[i].position.y) > 15) {
            lateral = fmaxf(lateral, fabsf(p[i].velocity.y));
        }
    }
    printf("gas probe wake: transverse speed %.3f\n", lateral);
    assert(lateral > .1f);
    for (int step = 0; step < 120; step++) {
        tick(fluid);
    }
    float motion = 0;
    for (int i = 0; i < count; i++) {
        assert(isfinite(p[i].position.x) && isfinite(p[i].position.y));
        float speed = hypotf(p[i].velocity.x, p[i].velocity.y);
        assert(speed < 120);
        motion += speed;
    }
    printf("gas wake after two seconds: summed speed %.3f\n", motion);
    assert(motion > 1);
    fluid_particle saved = p[100];
    for (int step = 0; step < 30; step++) {
        material_fluid_tick(
            fluid,
            &(fluid_step){ .delta = 1.f / 60,
                           .chunk_size = 64,
                           .regions = NULL,
                           .region_count = 0,
                           .colliders = { 0 } }
        );
    }
    assert(!memcmp(&saved, p + 100, sizeof saved));
    tick(fluid);
    assert(isfinite(p[100].velocity.x));
    material_fluid_clear(fluid);
    fluid_particle jet = particle(0, 0, FLUID_GAS);
    jet.velocity = (fluid_vec){ 50, 20 };
    jet.lifetime = jet.style.duration = 2;
    material_fluid_add(fluid, jet);
    tick(fluid);
    p = material_fluid_particles(fluid, &count);
    assert(fabsf(p[0].velocity.x - 50) < .001f && fabsf(p[0].velocity.y - 20) < .001f);
    material_fluid_free(fluid);
}

static void parallel_parity(bool burning)
{
    material_fluid *serial = test_fluid(10, 2500);
    material_fluid *parallel = test_fluid(10, 2500);
    if (burning) {
        material_fluid_set_flow(serial, (fluid_flow_settings){ 20, 2, .1f });
        material_fluid_set_flow(parallel, (fluid_flow_settings){ 20, 2, .1f });
        /* Liquid touching fire turns to smoke, exercising pair reactions,
         * conduction and burning together. */
        fluid_reaction douse = { TEST_LIQUID, FLUID_BURNING, 30,
                                 TEST_SMOKE,  FLUID_KEEP,    .5f };
        fluid_heat heat = { .conduction = 2, .glow_temperature = 2 };
        material_fluid_set_reactions(serial, &douse, 1);
        material_fluid_set_reactions(parallel, &douse, 1);
        material_fluid_set_heat(serial, heat);
        material_fluid_set_heat(parallel, heat);
    }
    for (int y = 0; y < 50; y++) {
        for (int x = 0; x < 50; x++) {
            fluid_particle p = particle(x * 4, y * 4, FLUID_LIQUID);
            if (burning && y < 45) {
                p = particle(x * 4, y * 4, TEST_FUEL);
                p.temperature = x == 0 ? 2 : 0;
            }
            material_fluid_add(serial, p);
            material_fluid_add(parallel, p);
        }
    }
    game_task_pool *pool = game_task_pool_acquire_workers(4);
    material_fluid_set_tasks(parallel, game_task_pool_tasks(pool));
    for (int i = 0; i < 10; i++) {
        tick(serial);
        game_task_pool_begin(pool, true);
        tick(parallel);
        game_task_pool_end(pool);
        int na, nb;
        const fluid_blast *ba = material_fluid_blasts(serial, &na);
        const fluid_blast *bb = material_fluid_blasts(parallel, &nb);
        assert(na == nb && (!na || !memcmp(ba, bb, (size_t)na * sizeof *ba)));
    }
    int count, other_count;
    const fluid_particle *a = material_fluid_particles(serial, &count);
    const fluid_particle *b = material_fluid_particles(parallel, &other_count);
    assert(count == other_count && !memcmp(a, b, (size_t)count * sizeof *a));
    assert(material_fluid_flow_hash(serial) == material_fluid_flow_hash(parallel));
    assert(!burning || (a[0].burning && a[1].burning && !a[49].burning));
    int smoke = 0;
    for (int i = 0; i < count; i++) {
        smoke += a[i].substance == TEST_SMOKE;
    }
    assert(burning == (smoke > 0));
    material_fluid_free(serial);
    material_fluid_free(parallel);
    game_task_pool_release(pool);
}

static void component(ecs_world *world, ecs_entity entity, const ecs_component_desc *desc, const void *value)
{
    assert(ecs_world_record_add(world, entity, desc->id, value, 0) == ECS_OK);
    if (desc == &GAME_COMPONENT_GameDestructible) {
        MaterialBreakProfile profile = { .chip_speed = 35,
                                         .fragment_speed = 30,
                                         .blast_speed = 70,
                                         .release_speed = 12,
                                         .dust_limit = 8,
                                         .dust_speed_min = 4,
                                         .dust_speed_max = 12,
                                         .dust_lifetime_min = .15f,
                                         .dust_lifetime_max = .3f,
                                         .dust_size_min = .25f,
                                         .dust_size_max = .5f };
        assert(
            ecs_world_record_add(
                world,
                entity,
                GAME_COMPONENT_MaterialBreakProfile.id,
                &profile,
                0
            ) == ECS_OK
        );
    }
}

static void physical_destruction()
{
    /* A second, noncombat consumer: a movable probe and a cuttable slab. */
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    ecs_entity settings = ecs_world_create_entity(world);
    component(world, settings, &GAME_COMPONENT_MaterialDestructionWorld, &(MaterialDestructionWorld){ 8 });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    ecs_entity slab = ecs_world_create_entity(world);
    component(world, slab, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(
        world,
        slab,
        &GAME_COMPONENT_GameCollider,
        &(
            GameCollider
        ){ .shape = GAME_COLLIDER_BOX, .width = 64, .height = 16, .category = 8, .mask = 1 }
    );
    component(
        world,
        slab,
        &GAME_COMPONENT_GameDestructible,
        &(GameDestructible){ .cell_size = 2, .min_fragment_cells = 4 }
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->physics, 0) == 0);
    game_ray_hit hit;
    assert(
        game_physics_raycast(
            world,
            live->entities,
            (CF_V2){ 0, -30 },
            (CF_V2){ 0, 30 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    assert(hit.found);
    game_materials_damage(live->materials, slab, (fluid_vec){ 0, 0 }, 11, 1, 0, (material_impact){ 0 });
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(ecs_world_live_count(world) == 3); /* settings, core, detached body */
    assert(
        game_physics_raycast(
            world,
            live->entities,
            (CF_V2){ 0, -30 },
            (CF_V2){ 0, 30 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    assert(!hit.found);
    assert(
        game_physics_raycast(
            world,
            live->entities,
            (CF_V2){ 24, -30 },
            (CF_V2){ 24, 30 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    assert(hit.found && GAME_GameBody_get_const(world, hit.entity)->type == GAME_BODY_DYNAMIC);

    ecs_entity probe = ecs_world_create_entity(world);
    component(world, probe, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0, -24 });
    component(world, probe, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 0, 2880 });
    component(
        world,
        probe,
        &GAME_COMPONENT_GameBody,
        &(GameBody){ .type = GAME_BODY_KINEMATIC, .continuous = true }
    );
    component(
        world,
        probe,
        &GAME_COMPONENT_GameCollider,
        &(GameCollider){ .radius = 1, .category = 1, .mask = 8, .sensor = true }
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(GAME_GameTransform_get_const(world, probe)->y > 20);
    *GAME_GameTransform_get(world, probe) = (GameTransform){ 24, -24 };
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(GAME_GameTransform_get_const(world, probe)->y < -7);
    /* Ordinary overlap callbacks must find individual compound pieces, keep
     * the carved opening empty, and deduplicate multiple pieces per body. */
    GAME_GameBody_get(world, probe)->continuous = false;
    *GAME_GameVelocity_get(world, probe) = (GameVelocity){ 0 };
    *GAME_GameTransform_get(world, probe) = (GameTransform){ 24, 0 };
    game_physics_set_profiling(live->physics, true);
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(game_physics_last_profile(live->physics)->pairs == 1);
    *GAME_GameTransform_get(world, probe) = (GameTransform){ 0 };
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(game_physics_last_profile(live->physics)->pairs == 0);
    GameCollider *bar = GAME_GameCollider_get(world, probe);
    bar->shape = GAME_COLLIDER_BOX;
    bar->width = 60;
    bar->height = 2;
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(game_physics_last_profile(live->physics)->pairs == 2);
    /* Destruction and fragment masks survive fluid sleep, without catch-up. */
    const material_mask *mask = game_materials_mask(live->materials, slab);
    int cells = mask->count;
    for (int i = 0; i < 30; i++) {
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    assert(mask->count == cells);
    game_physics_reset(live->physics);
    game_materials_reset(live->materials);
    assert(!game_materials_mask(live->materials, slab));
    assert(ecs_world_live_count(world) == 3); /* detached fragment was removed */
    game_live_world_free(live);
    ecs_world_free(world);
}

static void terrain_boundary_cut()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    behavior_asset *field = cave_asset();
    char field_error[256];
    assert(
        material_field_compile(game_materials_fields(live->materials), field, field_error, sizeof field_error)
    );
    behavior_asset_free(field);
    ecs_entity settings = ecs_world_create_entity(world);
    component(world, settings, &GAME_COMPONENT_MaterialDestructionWorld, &(MaterialDestructionWorld){ 8 });
    component(
        world,
        settings,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ .chunk_size = 128, .buffer = 32, .kernel = 10, .particle_limit = 10 }
    );
    ecs_entity source = ecs_world_create_entity(world);
    component(world, source, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(
        world,
        source,
        &GAME_COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .seed = 1729,
                            .width = 512,
                            .height = 512,
                            .cell_size = 2,
                            .field = "CaveField",
                            .parameters = "{\"Radius\":72,\"Roughness\":30}",
                            .connectivity = MATERIAL_CONNECTIVITY_FIRST_AIR,
                            .border_cells = 8,
                            .category = 8,
                            .mask = 1,
                            .min_fragment_cells = 4 }
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(!game_materials_prepare(live->materials));
    ecs_entity target = { 0 };
    ecs_query query;
    ecs_component_id id = GAME_COMPONENT_GameDestructible.id;
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    ecs_entity entity;
    int before = 0;
    while (ecs_query_next(&query, &entity)) {
        const GameTransform *p = GAME_GameTransform_get_const(world, entity);
        const material_mask *mask = game_materials_mask(live->materials, entity);
        before += mask->count;
        if (p->x < 0 && p->y > 0) {
            target = entity;
        }
    }
    ecs_query_end(&query);
    assert(ecs_entity_to_u64(target));
    game_material_state initial = game_materials_inspect(live->materials);
    game_materials_damage(live->materials, target, (fluid_vec){ 0, 220 }, 12, 1, 0, (material_impact){ 0 });
    assert(!game_materials_prepare(live->materials));
    int after = 0, opened = 0;
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const GameTransform *p = GAME_GameTransform_get_const(world, entity);
        const material_mask *mask = game_materials_mask(live->materials, entity);
        after += mask->count;
        if (p->y > 0) {
            float x = p->x < 0 ? -1 : 1;
            assert(!material_mask_contains(mask, x - p->x, 220 - p->y));
            opened++;
        }
    }
    ecs_query_end(&query);
    assert(opened == 2 && before - after > 100);
    game_material_state damaged = game_materials_inspect(live->materials);
    assert(initial.hash != damaged.hash);
    assert(!game_materials_prepare(live->materials));
    assert(game_materials_inspect(live->materials).hash == damaged.hash);
    field = cave_asset();
    assert(
        material_field_compile(game_materials_fields(live->materials), field, field_error, sizeof field_error)
    );
    behavior_asset_free(field);
    assert(!game_materials_prepare(live->materials));
    assert(game_materials_inspect(live->materials).cells == initial.cells);
    assert(!ecs_entity_is_alive(world, target));
    MaterialTerrain *terrain = GAME_MaterialTerrain_get(world, source);
    const char *original_parameters = terrain->parameters;
    terrain->parameters = "{\"Radius\":96,\"Roughness\":30}";
    assert(!game_materials_prepare(live->materials));
    assert(game_materials_inspect(live->materials).cells < initial.cells);
    terrain = GAME_MaterialTerrain_get(world, source);
    terrain->parameters = original_parameters;
    assert(!game_materials_prepare(live->materials));
    assert(game_materials_inspect(live->materials).cells == initial.cells);
    game_materials_reset(live->materials);
    assert(ecs_world_live_count(world) == 2);
    game_materials_damage(live->materials, source, (fluid_vec){ 0, 220 }, 12, 1, 0, (material_impact){ 0 });
    assert(!game_materials_prepare(live->materials));
    assert(game_materials_inspect(live->materials).cells == damaged.cells);
    assert(ecs_world_live_count(world) > 2);
    assert(ecs_world_destroy_entity(world, source) == ECS_OK);
    assert(!game_materials_prepare(live->materials));
    assert(ecs_world_live_count(world) == 1);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void terrain_stream_restore()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    behavior_asset *field = cave_asset();
    char error[256];
    assert(
        material_field_compile(game_materials_fields(live->materials), field, error, sizeof error)
    );
    behavior_asset_free(field);
    ecs_entity camera = ecs_world_create_entity(world);
    component(world, camera, &GAME_COMPONENT_GameCamera, &(GameCamera){ .width = 128, .height = 128 });
    component(world, camera, &GAME_COMPONENT_GameTransform, &(GameTransform){ -1500, 0 });
    ecs_entity source = ecs_world_create_entity(world);
    component(world, source, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(
        world,
        source,
        &GAME_COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .seed = 1729,
                            .width = 4096,
                            .height = 512,
                            .cell_size = 2,
                            .field = "CaveField",
                            .parameters = "{\"Radius\":72,\"Roughness\":30}",
                            .category = 8,
                            .mask = 1,
                            .min_fragment_cells = 4,
                            .stream_radius = 128,
                            .stream_budget = 64 }
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(!game_materials_prepare(live->materials));
    game_material_state pristine = game_materials_inspect(live->materials);
    assert(pristine.streaming.resident > 0 && pristine.cells > 0);
    ecs_component_id id = GAME_COMPONENT_GameDestructible.id;
    ecs_query query;
    ecs_entity entity, target = { 0 };
    GameTransform position = { 0 };
    fluid_vec point = { 0 };
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const material_mask *mask = game_materials_mask(live->materials, entity);
        for (int y = 16; y < mask->height - 16 && !ecs_entity_to_u64(target); y++) {
            for (int x = 16; x < mask->width - 16; x++) {
                if (!mask->cells[y * mask->width + x]) {
                    continue;
                }
                target = entity;
                position = *GAME_GameTransform_get_const(world, entity);
                point = (fluid_vec){
                    position.x + (x + .5f - mask->width * .5f) * mask->cell_size,
                    position.y + (y + .5f - mask->height * .5f) * mask->cell_size
                };
                break;
            }
        }
    }
    ecs_query_end(&query);
    assert(ecs_entity_to_u64(target));
    game_materials_damage(live->materials, target, point, 10, 1, 0, (material_impact){ 0 });
    assert(!game_materials_prepare(live->materials));
    assert(game_materials_inspect(live->materials).cells < pristine.cells);
    assert(!material_mask_contains(
        game_materials_mask(live->materials, target),
        point.x - position.x,
        point.y - position.y
    ));
    GAME_GameTransform_get(world, camera)->x = 1500;
    assert(!game_materials_prepare(live->materials));
    assert(!ecs_entity_is_alive(world, target));
    assert(game_materials_inspect(live->materials).streaming.unloaded > 0);
    GAME_GameTransform_get(world, camera)->x = -1500;
    assert(!game_materials_prepare(live->materials));
    bool restored = false;
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const GameTransform *p = GAME_GameTransform_get_const(world, entity);
        if (p->x == position.x && p->y == position.y) {
            assert(!material_mask_contains(
                game_materials_mask(live->materials, entity),
                point.x - p->x,
                point.y - p->y
            ));
            restored = true;
        }
    }
    ecs_query_end(&query);
    assert(restored);
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    game_physics_reset(live->physics);
    game_materials_reset(live->materials);
    assert(!game_materials_prepare(live->materials));
    assert(game_materials_inspect(live->materials).cells == pristine.cells);
    assert(!game_materials_inspect(live->materials).streaming.stored);
    MaterialTerrain authored = *GAME_MaterialTerrain_get_const(world, source);
    assert(ecs_world_record_remove(world, source, GAME_COMPONENT_MaterialTerrain.id, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(!game_materials_prepare(live->materials));
    assert(!game_materials_inspect(live->materials).streaming.resident);
    assert(ecs_world_live_count(world) == 2);
    component(world, source, &GAME_COMPONENT_MaterialTerrain, &authored);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(!game_materials_prepare(live->materials));
    assert(game_materials_inspect(live->materials).cells == pristine.cells);
    assert(ecs_world_destroy_entity(world, source) == ECS_OK);
    assert(!game_materials_prepare(live->materials));
    assert(!game_materials_inspect(live->materials).streaming.resident);
    assert(ecs_world_live_count(world) == 1);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void finite_drive()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    ecs_entity entity = ecs_world_create_entity(world);
    component(world, entity, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(world, entity, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 0 });
    component(world, entity, &GAME_COMPONENT_GameBody, &(GameBody){ .type = GAME_BODY_DYNAMIC });
    component(world, entity, &GAME_COMPONENT_GameCollider, &(GameCollider){ .radius = 10 });
    component(
        world,
        entity,
        &GAME_COMPONENT_GameDrive,
        &(GameDrive){ .target = { 120, 0 }, .acceleration = 600 }
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    float speed = GAME_GameVelocity_get_const(world, entity)->x;
    assert(speed > 9.9f && speed < 10.1f);
    for (int i = 0; i < 14; i++) {
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    assert(fabsf(GAME_GameVelocity_get_const(world, entity)->x - 120) < .1f);
    GAME_GameDrive_get(world, entity)->target.x = 0;
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(fabsf(GAME_GameVelocity_get_const(world, entity)->x - 110) < .1f);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void reactions()
{
    material_fluid *f = test_fluid(10, 8);
    fluid_particle hot = particle(-8, 0, TEST_GAS);
    hot.temperature = 2;
    fluid_particle fuel = particle(0, 0, TEST_FUEL);
    material_fluid_add(f, hot);
    material_fluid_add(f, fuel);
    fuel.position.x = 8;
    material_fluid_add(f, fuel);
    tick(f);
    int count;
    const fluid_particle *p = material_fluid_particles(f, &count);
    assert(p[1].burning && !p[2].burning); /* synchronous propagation */
    tick(f);
    assert(p[2].burning);
    int blasts;
    material_fluid_blasts(f, &blasts);
    assert(blasts == 1);
    for (int i = 0; i < 45; i++) {
        tick(f);
    }
    /* Burned fuel became smoke that still glows as it cools. */
    assert(p[1].substance == TEST_SMOKE && p[2].substance == TEST_SMOKE && !p[1].burning);
    assert(p[1].billow && p[1].glow > 0 && p[1].glow < 1);
    assert(p[1].color == 0x444444ff && p[1].lifetime > 0 && p[1].lifetime < 3);
    material_fluid_blasts(f, &blasts);
    assert(!blasts);
    float heat = p[1].temperature, life = p[1].lifetime;
    material_fluid_tick(
        f,
        &(fluid_step){ .delta = 1.f / 60,
                       .chunk_size = 64,
                       .regions = NULL,
                       .region_count = 0,
                       .colliders = { 0 } }
    );
    assert(p[1].temperature == heat && p[1].lifetime == life);
    float glow = p[1].glow;
    for (int i = 0; i < 60; i++) {
        tick(f);
    }
    assert(p[1].glow < glow * .15f && p[1].lifetime < life);
    material_fluid_clear(f);
    material_fluid_add(f, hot);
    fuel.position.x = 0;
    material_fluid_add(f, fuel);
    fluid_body wall = { .position = { -4, 0 } };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 1, 20 } };
    fluid_region region = { -50, -50, 100, 100 };
    material_fluid_tick(
        f,
        &(fluid_step){ .delta = 1.f / 60,
                       .chunk_size = 64,
                       .regions = &region,
                       .region_count = 1,
                       .colliders = { &wall, 1, &obstacle, 1 } }
    );
    p = material_fluid_particles(f, &count);
    assert(!p[1].burning); /* even a thin wall shields the fuel */
    material_fluid_clear(f);
    material_fluid_add(f, fuel);
    fluid_body shot = { .position = { 20, 0 }, .velocity = { 2400, 0 }, .heat = 2 };
    material_fluid_tick(
        f,
        &(fluid_step){ .delta = 1.f / 60,
                       .chunk_size = 64,
                       .regions = &region,
                       .region_count = 1,
                       .colliders = { &shot, 1, NULL, 0 } }
    );
    p = material_fluid_particles(f, &count);
    assert(p[0].burning);
    material_fluid_free(f);
}

enum
{
    WATER,
    STEAM,
    LAVA,
    FIRE,
    SMOKE,
    SUBSTANCES,
    BASALT = FLUID_DEPOSIT,
    ICE = FLUID_DEPOSIT - 1
};

/* Water boils to steam and freezes to ice; lava quenches to basalt; water
 * douses fire. Deposits leave the fluid and report where they formed. */
static material_fluid *chemistry(int capacity)
{
    material_fluid *f = material_fluid_new(10, capacity);
    fluid_substance substances[SUBSTANCES] = {
        [WATER] = { .phase = FLUID_LIQUID,
                    .density = .25f,
                    .stiffness = .7f,
                    .cooling = .3f,
                    .boil_at = 1,
                    .burns_to = FLUID_GONE,
                    .boils_to = STEAM,
                    .freeze_at = -.5f,
                    .freezes_to = ICE },
        [STEAM] = { .phase = FLUID_GAS,
                    .density = .01f,
                    .temperature = 1.2f,
                    .buoyancy = .5f,
                    .lifetime = 5,
                    .burns_to = FLUID_GONE,
                    .boils_to = FLUID_KEEP,
                    .freezes_to = FLUID_KEEP },
        [LAVA] = { .phase = FLUID_LIQUID,
                   .density = .7f,
                   .stiffness = .7f,
                   .temperature = 3,
                   .cooling = .04f,
                   .burns_to = FLUID_GONE,
                   .boils_to = FLUID_KEEP,
                   .freeze_at = .8f,
                   .freezes_to = BASALT },
        [FIRE] = { .phase = FLUID_GAS,
                   .density = .01f,
                   .temperature = 2,
                   .ignite_at = .5f,
                   .burn_rate = 2,
                   .burn_heat = 2,
                   .burns_to = SMOKE,
                   .boils_to = FLUID_KEEP,
                   .freezes_to = FLUID_KEEP },
        [SMOKE] = { .phase = FLUID_GAS,
                    .density = .016f,
                    .lifetime = 3,
                    .burns_to = FLUID_GONE,
                    .boils_to = FLUID_KEEP,
                    .freezes_to = FLUID_KEEP },
    };
    fluid_reaction reactions[] = {
        { WATER, LAVA, 60, STEAM, BASALT, 0 },
        { WATER, FLUID_BURNING, 60, STEAM, SMOKE, 0 },
    };
    material_fluid_set_substances(f, substances, SUBSTANCES);
    material_fluid_set_reactions(f, reactions, 2);
    material_fluid_set_heat(f, (fluid_heat){ .conduction = 2, .glow_temperature = 2 });
    return f;
}

static int count_substance(const material_fluid *f, int substance)
{
    int count, found = 0;
    const fluid_particle *p = material_fluid_particles(f, &count);
    for (int i = 0; i < count; i++) {
        found += p[i].substance == substance;
    }
    return found;
}

static void substance_chemistry()
{
    material_fluid *f = chemistry(64);
    /* Heat boils water; cold freezes it into an ice deposit. */
    fluid_particle water = material_fluid_particle(f, WATER, (fluid_vec){ 0, 0 });
    assert(material_fluid_add(f, water));
    tick(f);
    material_fluid_heat_area(f, (fluid_vec){ 0, 0 }, 1, 5);
    tick(f);
    assert(count_substance(f, WATER) == 0 && count_substance(f, STEAM) == 1);
    material_fluid_clear(f);
    assert(material_fluid_add(f, water));
    tick(f);
    material_fluid_heat_area(f, (fluid_vec){ 0, 0 }, 1, -2);
    tick(f);
    int count;
    const fluid_deposit *deposits = material_fluid_deposits(f, &count);
    assert(count == 1 && deposits[0].target == 1 && deposits[0].area > 0);
    material_fluid_particles(f, &count);
    assert(count == 0);

    /* Water touching lava flashes to steam; the lava quenches to basalt. */
    material_fluid_clear(f);
    assert(material_fluid_add(f, water));
    assert(material_fluid_add(f, material_fluid_particle(f, LAVA, (fluid_vec){ 4, 0 })));
    int basalt = 0;
    for (int i = 0; i < 10 && !basalt; i++) {
        tick(f);
        const fluid_deposit *d = material_fluid_deposits(f, &count);
        basalt += count && d[0].target == 0;
    }
    assert(basalt && count_substance(f, STEAM) == 1 && count_substance(f, LAVA) == 0);

    /* Water douses fire: both leave as steam and smoke. */
    material_fluid_clear(f);
    assert(material_fluid_add(f, water));
    assert(material_fluid_add(f, material_fluid_particle(f, FIRE, (fluid_vec){ 4, 0 })));
    for (int i = 0; i < 10 && count_substance(f, FIRE); i++) {
        tick(f);
    }
    assert(count_substance(f, SMOKE) == 1 && count_substance(f, STEAM) == 1);

    /* Lava far from water conducts heat into it and cools, until it freezes. */
    material_fluid_clear(f);
    fluid_particle lava = material_fluid_particle(f, LAVA, (fluid_vec){ 0, 0 });
    lava.temperature = .85f;
    assert(material_fluid_add(f, lava));
    for (int i = 0; i < 180 && count_substance(f, LAVA); i++) {
        tick(f);
    }
    assert(!count_substance(f, LAVA));

    /* Hot steam rises against gravity; water falls. */
    material_fluid_clear(f);
    assert(material_fluid_add(f, material_fluid_particle(f, STEAM, (fluid_vec){ -40, 0 })));
    assert(material_fluid_add(f, material_fluid_particle(f, WATER, (fluid_vec){ 40, 0 })));
    fluid_region region = { -1000, -1000, 2000, 2000 };
    for (int i = 0; i < 30; i++) {
        material_fluid_tick(
            f,
            &(fluid_step){ .delta = 1.f / 60,
                           .chunk_size = 64,
                           .gravity = { 0, 320 },
                           .regions = &region,
                           .region_count = 1 }
        );
    }
    const fluid_particle *p = material_fluid_particles(f, &count);
    assert(count == 2);
    for (int i = 0; i < count; i++) {
        assert(p[i].substance == STEAM ? p[i].position.y < -20 : p[i].position.y > 40);
    }
    material_fluid_free(f);
    puts("PASS: boiling, freezing into deposits, quenching, dousing, cooling and buoyancy");
}

static void resistance_and_chips()
{
    material_mask mask = material_mask_new(10, 10, 1, false);
    assert(!material_mask_apply_damage(&mask, 0, 0, 3, 1, 3));
    assert(!material_mask_apply_damage(&mask, 0, 0, 3, 1, 3));
    assert(material_mask_apply_damage(&mask, 0, 0, 3, 1, 3) > 0);
    material_mask_rebuild(&mask);
    check_cover(&mask);
    material_mask_free(&mask);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    ecs_entity config = ecs_world_create_entity(world),
               slab = ecs_world_create_entity(world);
    component(world, config, &GAME_COMPONENT_MaterialDestructionWorld, &(MaterialDestructionWorld){ 8 });
    component(
        world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    component(world, slab, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(
        world,
        slab,
        &GAME_COMPONENT_GameCollider,
        &(
            GameCollider
        ){ .shape = GAME_COLLIDER_BOX, .width = 20, .height = 20, .category = 8, .mask = 15 }
    );
    component(world, slab, &GAME_COMPONENT_GameDestructible, &(GameDestructible){ 1, 3 });
    component(world, slab, &GAME_COMPONENT_GameMaterial, &(GameMaterial){ .emission_multiplier = .5f });
    component(world, slab, &GAME_COMPONENT_GameFragmentGlow, &(GameFragmentGlow){ 1, .3f });
    component(
        world,
        slab,
        &GAME_COMPONENT_GameSubstance,
        &(GameSubstance){ .hardness = 1, .blast_multiplier = 3, .retain_chips = true }
    );
    component(
        world,
        slab,
        &GAME_COMPONENT_GameReflection,
        &(GameReflection){ .strength = .8f, .roughness = .2f, .angle = 1.2f }
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->physics, 0) == 0);
    game_materials_damage(live->materials, slab, (fluid_vec){ 8, 0 }, 3, 1, 0, (material_impact){ 0 });
    assert(game_physics_tick(live->physics, 0) == 0);
    ecs_query query;
    ecs_entity entity;
    int pieces = 0;
    ecs_component_id body = GAME_COMPONENT_GameBody.id;
    assert(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const GameVelocity *v = GAME_GameVelocity_get_const(world, entity);
        assert(v && hypotf(v->x, v->y) > 20);
        assert(GAME_GameSubstance_get_const(world, entity)->blast_multiplier == 3);
        assert(GAME_GameReflection_get_const(world, entity)->strength == .8f);
        assert(game_materials_mask(live->materials, entity)->count >= 3);
        assert(GAME_GameMaterial_get_const(world, entity)->emission_multiplier == .5f);
        pieces++;
    }
    ecs_query_end(&query);
    assert(pieces > 0);
    for (int step = 0; step < 30; step++) {
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    assert(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        /* The chipped right edge must drift out of the slab, not into it. */
        assert(GAME_GameTransform_get_const(world, entity)->x > 8);
        float glow = GAME_GameMaterial_get_const(world, entity)->emission_multiplier;
        assert(glow > 0 && glow < .15f);
    }
    ecs_query_end(&query);
    for (int step = 0; step < 35; step++) {
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    assert(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        assert(GAME_GameMaterial_get_const(world, entity)->emission_multiplier == 0);
        assert(game_materials_mask(live->materials, entity)->count >= 3);
    }
    ecs_query_end(&query);
    /* Losing power must not remove material or require combat components. */
    assert(GAME_GameMaterial_get_const(world, slab)->emission_multiplier == .5f);
    int cells = game_materials_mask(live->materials, slab)->count;
    game_materials_release(live->materials, slab);
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(!game_materials_mask(live->materials, slab));
    int largest = 0;
    assert(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const material_mask *hull = game_materials_mask(live->materials, entity);
        if (hull && hull->count > largest) {
            largest = hull->count;
        }
    }
    ecs_query_end(&query);
    assert(largest == cells);
    game_live_world_free(live);
    ecs_world_free(world);
}

enum
{
    CAPACITY_TEST_SOURCE = 901
};

typedef struct
{
    material_mask mask;
    int created, created_cells, destroyed, damage_dust, damage_removed;
    bool retain_chips, available;
} material_capacity_probe;

static bool capacity_read_source(void *user, uint64_t target, material_source *source)
{
    material_capacity_probe *probe = user;
    if (target != CAPACITY_TEST_SOURCE || !probe->available) {
        return false;
    }
    *source = (material_source){ .mask = &probe->mask,
                                 .hardness = 1,
                                 .minimum = 1,
                                 .initial_cells = probe->mask.count,
                                 .retain_chips = probe->retain_chips };
    return true;
}

static void capacity_create_fragment(void *user, uint64_t source, material_fragment fragment)
{
    material_capacity_probe *probe = user;
    assert(source == CAPACITY_TEST_SOURCE);
    probe->created++;
    probe->created_cells += fragment.mask.count;
    material_mask_free(&fragment.mask);
}

static void capacity_destroy_source(void *user, uint64_t target)
{
    material_capacity_probe *probe = user;
    assert(target == CAPACITY_TEST_SOURCE);
    probe->destroyed++;
}

static void capacity_invalidate(void *user, uint64_t target)
{
    (void)user;
    assert(target == CAPACITY_TEST_SOURCE);
}

static void capacity_result(void *user, const material_event *event)
{
    material_capacity_probe *probe = user;
    if (event->kind != MATERIAL_EVENT_DAMAGE) {
        return;
    }
    probe->damage_removed = event->removed;
    probe->damage_dust = event->dust;
}

static material_mask capacity_split_shape(void)
{
    unsigned char cells[25] = { 0 };
    for (int y = 0; y < 5; y++) {
        cells[y * 5] = cells[y * 5 + 1] = 1;
        cells[y * 5 + 3] = cells[y * 5 + 4] = 1;
    }
    cells[2 * 5 + 2] = 1;
    return material_mask_from_cells(5, 5, 1, cells);
}

static material_host capacity_host(material_capacity_probe *probe)
{
    return (material_host){ .user = probe,
                            .read_source = capacity_read_source,
                            .create_fragment = capacity_create_fragment,
                            .destroy_source = capacity_destroy_source,
                            .invalidate_geometry = capacity_invalidate,
                            .result = capacity_result };
}

static void material_capacity_fallback()
{
    material_destruction *destruction = material_destruction_new();
    material_capacity_probe probe = { .mask = capacity_split_shape(), .available = true };
    material_host host = capacity_host(&probe);
    material_destruction_queue_damage(
        destruction,
        (material_damage){ .target = CAPACITY_TEST_SOURCE,
                           .point = { 0, 0 },
                           .radius = .1f,
                           .energy = 1 }
    );
    material_destruction_step(destruction, &host, 0);
    assert(probe.mask.count == 20 && probe.created == 0);
    assert(probe.damage_removed == 1 && probe.damage_dust == 1);
    material_destruction_step(destruction, &host, 0);
    assert(probe.mask.count == 20 && probe.created == 0);
    material_destruction_step(destruction, &host, 1);
    assert(probe.mask.count == 10 && probe.created == 1 && probe.created_cells == 10);
    assert(probe.mask.count + probe.created_cells == 20);
    material_mask_free(&probe.mask);
    material_destruction_free(destruction);

    destruction = material_destruction_new();
    probe = (material_capacity_probe){ .mask = capacity_split_shape(),
                                       .available = true };
    host = capacity_host(&probe);
    material_destruction_queue_damage(
        destruction,
        (material_damage){ .target = CAPACITY_TEST_SOURCE,
                           .point = { 0, 0 },
                           .radius = .1f,
                           .energy = 1 }
    );
    material_destruction_step(destruction, &host, 0);
    probe.available = false;
    material_destruction_step(destruction, &host, 0);
    probe.available = true;
    material_destruction_step(destruction, &host, 1);
    assert(probe.mask.count == 20 && probe.created == 0);
    material_mask_free(&probe.mask);
    material_destruction_free(destruction);

    destruction = material_destruction_new();
    probe = (material_capacity_probe){ .mask = capacity_split_shape(),
                                       .retain_chips = true,
                                       .available = true };
    host = capacity_host(&probe);
    material_destruction_queue_damage(
        destruction,
        (material_damage){ .target = CAPACITY_TEST_SOURCE,
                           .point = { 0, 0 },
                           .radius = .1f,
                           .energy = 1 }
    );
    material_destruction_step(destruction, &host, 1);
    assert(probe.created == 1 && probe.created_cells == 10);
    assert(probe.mask.count == 10 && !material_mask_contains(&probe.mask, 0, 0));
    assert(probe.damage_dust == 1);
    assert(probe.mask.count + probe.created_cells + probe.damage_dust == 21);
    material_destruction_step(destruction, &host, 1);
    assert(probe.mask.count == 10 && probe.created == 1);
    material_mask_free(&probe.mask);
    material_destruction_free(destruction);

    destruction = material_destruction_new();
    probe = (material_capacity_probe){ .mask = material_mask_new(5, 5, 1, false),
                                       .available = true };
    host = capacity_host(&probe);
    material_destruction_queue_release(destruction, CAPACITY_TEST_SOURCE);
    material_destruction_step(destruction, &host, 0);
    assert(!probe.destroyed && !probe.created && probe.mask.count == 25);
    material_destruction_step(destruction, &host, 1);
    assert(probe.destroyed == 1 && probe.created == 1 && probe.created_cells == 25);
    assert(!probe.mask.count);
    material_destruction_step(destruction, &host, 1);
    assert(probe.destroyed == 1 && probe.created == 1);
    material_mask_free(&probe.mask);
    material_destruction_free(destruction);
}

static void small_hull_release()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    ecs_entity config = ecs_world_create_entity(world),
               slab = ecs_world_create_entity(world);
    component(world, config, &GAME_COMPONENT_MaterialDestructionWorld, &(MaterialDestructionWorld){ 8 });
    component(
        world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    component(world, slab, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(
        world,
        slab,
        &GAME_COMPONENT_GameCollider,
        &(
            GameCollider
        ){ .shape = GAME_COLLIDER_BOX, .width = 20, .height = 20, .category = 8, .mask = 15 }
    );
    component(
        world,
        slab,
        &GAME_COMPONENT_GameDestructible,
        &(GameDestructible){ .cell_size = 1, .min_fragment_cells = 3, .release_fraction = .3f }
    );
    component(world, slab, &GAME_COMPONENT_GameHealth, &(GameHealth){ 3, 3 });
    component(world, slab, &GAME_COMPONENT_GameProjectile, &(GameProjectile){ 0 });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(game_materials_mask(live->materials, slab)->count == 400);
    game_materials_damage(live->materials, slab, (fluid_vec){ 10, 0 }, 9, 1, 0, (material_impact){ 0 });
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(game_materials_mask(live->materials, slab)->count > 240);
    assert(GAME_GameHealth_get_const(world, slab)->current == 3);
    game_materials_damage(live->materials, slab, (fluid_vec){ -10, 0 }, 12, 1, 0, (material_impact){ 0 });
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(!game_materials_mask(live->materials, slab));
    ecs_query query;
    ecs_entity entity;
    ecs_component_id body = GAME_COMPONENT_GameBody.id;
    int fragments = 0;
    assert(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        assert(game_materials_is_fragment(live->materials, entity));
        assert(!GAME_GameHealth_get_const(world, entity));
        assert(!GAME_GameProjectile_get_const(world, entity));
        assert(GAME_GameDestructible_get_const(world, entity)->release_fraction == 0);
        fragments++;
    }
    ecs_query_end(&query);
    assert(fragments > 0);
    game_live_world_free(live);
    ecs_world_free(world);
}

/* Fragments keep the frame their source's material was laid out in, including
 * a frame the source itself inherited, so world-space patterns travel with them. */
static void fragment_material_frame()
{
    for (int inherited = 0; inherited < 2; inherited++) {
        ecs_world *world = ecs_world_new();
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
            assert(
                ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
            );
        }
        game_live_world *live = game_live_world_new(world);
        ecs_entity config = ecs_world_create_entity(world),
                   slab = ecs_world_create_entity(world);
        component(world, config, &GAME_COMPONENT_MaterialDestructionWorld, &(MaterialDestructionWorld){ 8 });
        component(
            world,
            config,
            &GAME_COMPONENT_MaterialFluidWorld,
            &(MaterialFluidWorld){ 64, 32, 10, 100 }
        );
        component(world, slab, &GAME_COMPONENT_GameTransform, &(GameTransform){ 30, -12 });
        component(world, slab, &GAME_COMPONENT_GameOrientation, &(GameOrientation){ .4f });
        component(
            world,
            slab,
            &GAME_COMPONENT_GameCollider,
            &(GameCollider){ .shape = GAME_COLLIDER_BOX,
                             .width = 20,
                             .height = 20,
                             .category = 8,
                             .mask = 15 }
        );
        component(
            world,
            slab,
            &GAME_COMPONENT_GameDestructible,
            &(GameDestructible){ .cell_size = 1, .min_fragment_cells = 3, .release_fraction = .3f }
        );
        component(world, slab, &GAME_COMPONENT_GameShader, &(GameShader){ .material = "Basalt" });
        GameMaterialFrame expected = { 30, -12, .4f };
        if (inherited) {
            expected = (GameMaterialFrame){ -5, 7, 1.1f };
            component(world, slab, &GAME_COMPONENT_GameMaterialFrame, &expected);
        }
        assert(ecs_world_apply_commands(world) == ECS_OK);
        assert(game_physics_tick(live->physics, 0) == 0);
        game_materials_damage(live->materials, slab, (fluid_vec){ 40, -12 }, 9, 1, 0, (material_impact){ 0 });
        assert(game_physics_tick(live->physics, 0) == 0);
        game_materials_damage(live->materials, slab, (fluid_vec){ 20, -12 }, 12, 1, 0, (material_impact){ 0 });
        assert(game_physics_tick(live->physics, 0) == 0);
        ecs_query query;
        ecs_entity entity;
        ecs_component_id body = GAME_COMPONENT_GameBody.id;
        int fragments = 0;
        assert(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
        while (ecs_query_next(&query, &entity)) {
            const GameMaterialFrame *frame = GAME_GameMaterialFrame_get_const(world, entity);
            assert(
                frame && frame->x == expected.x && frame->y == expected.y &&
                frame->radians == expected.radians
            );
            fragments++;
        }
        ecs_query_end(&query);
        assert(fragments > 0);
        game_live_world_free(live);
        ecs_world_free(world);
    }
}

static void rotated_occlusion()
{
    fluid_body body = { .position = { 10, 20 }, .angle = 1.57079632679f };
    fluid_obstacle obstacle = { .center = { 30, 0 }, .half_size = { 8, 2 } };
    /* An offset box rotates far from its body's origin. Conservative culling
     * must preserve both a short crossing and a segment starting far away. */
    assert(material_fluid_blocked(
        (fluid_vec){ 0, 50 },
        (fluid_vec){ 20, 50 },
        &(fluid_colliders){ &body, 1, &obstacle, 1 },
        NULL,
        -1
    ));
    assert(material_fluid_blocked(
        (fluid_vec){ -1000, 50 },
        (fluid_vec){ 1000, 50 },
        &(fluid_colliders){ &body, 1, &obstacle, 1 },
        NULL,
        -1
    ));
    assert(!material_fluid_blocked(
        (fluid_vec){ 100, 50 },
        (fluid_vec){ 120, 50 },
        &(fluid_colliders){ &body, 1, &obstacle, 1 },
        NULL,
        -1
    ));
    obstacle.radius = 2;
    assert(material_fluid_blocked(
        (fluid_vec){ 0, 50 },
        (fluid_vec){ 20, 50 },
        &(fluid_colliders){ &body, 1, &obstacle, 1 },
        NULL,
        -1
    ));
    assert(!material_fluid_blocked(
        (fluid_vec){ 0, 55 },
        (fluid_vec){ 20, 55 },
        &(fluid_colliders){ &body, 1, &obstacle, 1 },
        NULL,
        -1
    ));
}

static void fragment_penetration()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    ecs_entity config = ecs_world_create_entity(world);
    component(world, config, &GAME_COMPONENT_MaterialDestructionWorld, &(MaterialDestructionWorld){ 8 });
    component(
        world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    for (int i = 0; i < 2; i++) {
        ecs_entity slab = ecs_world_create_entity(world);
        component(world, slab, &GAME_COMPONENT_GameTransform, &(GameTransform){ 20 + 60 * i, 0 });
        component(
            world,
            slab,
            &GAME_COMPONENT_GameCollider,
            &(GameCollider){ .shape = GAME_COLLIDER_BOX,
                             .width = 8,
                             .height = 16,
                             .category = 8,
                             .mask = 4 }
        );
        component(
            world,
            slab,
            &GAME_COMPONENT_GameDestructible,
            &(GameDestructible){ .cell_size = 1,
                                 .min_fragment_cells = 2,
                                 .pierceable_fragments = i == 0 }
        );
        assert(ecs_world_apply_commands(world) == ECS_OK);
        /* Releasing before mask initialization must preserve the request. */
        game_materials_release(live->materials, slab);
    }
    ecs_entity wall = ecs_world_create_entity(world),
               shot = ecs_world_create_entity(world);
    component(world, wall, &GAME_COMPONENT_GameTransform, &(GameTransform){ 50, 0 });
    component(
        world,
        wall,
        &GAME_COMPONENT_GameCollider,
        &(
            GameCollider
        ){ .shape = GAME_COLLIDER_BOX, .width = 8, .height = 16, .category = 8, .mask = 4 }
    );
    component(world, shot, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(world, shot, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 6000, 0 });
    component(
        world,
        shot,
        &GAME_COMPONENT_GameBody,
        &(GameBody){ .type = GAME_BODY_KINEMATIC, .continuous = true }
    );
    component(
        world,
        shot,
        &GAME_COMPONENT_GameCollider,
        &(GameCollider){ .radius = 2, .category = 4, .mask = 8, .sensor = true }
    );
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_physics_set_profiling(live->physics, true);
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(game_materials_inspect(live->materials).fragments == 2);
    assert(GAME_GameTransform_get_const(world, shot)->x < 20);
    assert(game_physics_last_profile(live->physics)->pairs == 1);

    game_physics_reset(live->physics);
    GAME_GameTransform_get(world, shot)->x = 0;
    GAME_GameCollider_get(world, shot)->pierce_fragments = true;
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    float x = GAME_GameTransform_get_const(world, shot)->x;
    assert(x > 43 && x < 45);
    /* The wall clips both travel and events: the far fragment is untouched. */
    assert(game_physics_last_profile(live->physics)->pairs == 2);

    assert(GAME_GameCollider_remove(world, wall, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    game_physics_reset(live->physics);
    GAME_GameTransform_get(world, shot)->x = 0;
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(
        GAME_GameTransform_get_const(world, shot)->x > 73 &&
        GAME_GameTransform_get_const(world, shot)->x < 77
    );
    assert(game_physics_last_profile(live->physics)->pairs == 2);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void swept_ignition()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(world);
    ecs_entity config = ecs_world_create_entity(world),
               wall = ecs_world_create_entity(world),
               shot = ecs_world_create_entity(world);
    component(world, config, &GAME_COMPONENT_MaterialDestructionWorld, &(MaterialDestructionWorld){ 8 });
    component(
        world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    component(world, wall, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(
        world,
        wall,
        &GAME_COMPONENT_GameCollider,
        &(
            GameCollider
        ){ .shape = GAME_COLLIDER_BOX, .width = 2, .height = 40, .category = 8, .mask = 4 }
    );
    component(world, wall, &GAME_COMPONENT_GameDestructible, &(GameDestructible){ 1, 3 });
    component(world, shot, &GAME_COMPONENT_GameTransform, &(GameTransform){ -20, 0 });
    component(world, shot, &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 2400, 0 });
    component(
        world,
        shot,
        &GAME_COMPONENT_GameBody,
        &(GameBody){ .type = GAME_BODY_KINEMATIC, .continuous = true }
    );
    component(
        world,
        shot,
        &GAME_COMPONENT_GameCollider,
        &(GameCollider){ .radius = 2, .category = 4, .mask = 8, .sensor = true }
    );
    component(world, shot, &GAME_COMPONENT_GameProjectile, &(GameProjectile){ 0 });
    component(world, shot, &GAME_COMPONENT_GameIgniter, &(GameIgniter){ 2 });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    const char *substances[] = { "FuelGas", "Smoke" };
    test_register_prototypes(live, substances, 2);
    assert(game_physics_tick(live->physics, 0) == 0);
    fluid_particle fuel = material_fluid_particle(
        game_materials_fluid(live->materials),
        game_materials_substance(live->materials, "FuelGas"),
        (fluid_vec){ 8, 0 }
    );
    assert(game_materials_emit(live->materials, fuel));
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(GAME_GameTransform_get_const(world, shot)->x < 0);
    assert(!game_materials_inspect(live->materials).burning_particles);
    game_materials_damage(live->materials, wall, (fluid_vec){ 0 }, 10, 1, 0, (material_impact){ 0 });
    *GAME_GameTransform_get(world, shot) = (GameTransform){ -20, 0 };
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(game_materials_inspect(live->materials).burning_particles == 1);
    game_live_world_free(live);
    ecs_world_free(world);
}

static void blast_policy()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(!ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL));
    }
    game_live_world *live = game_live_world_new(world);
    const char *paths[] = {
        "projects/arena2d/content/events/Material Impact.yaml",
        "projects/arena2d/content/behaviors/MaterialImpactHealth.yaml"
    };
    for (int i = 0; i < 2; i++) {
        char *json = text_file_read(paths[i]);
        assert(json);
        behavior_asset *asset = behavior_asset_load_json(json);
        free(json);
        assert(asset);
        assert(!game_live_add_definition(live, game_live_definition_new(asset->name, asset)));
    }
    ecs_entity config = ecs_world_create_entity(world), bodies[3];
    component(
        world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ .chunk_size = 64,
                               .buffer = 32,
                               .kernel = 10,
                               .particle_limit = 100,
                               .blast_radius = 200,
                               .blast_speed = 10,
                               .blast_damage = 20,
                               .glow_temperature = 2 }
    );
    component(world, config, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(world, config, &GAME_COMPONENT_GameCamera, &(GameCamera){ .width = 256, .height = 256 });
    GameTransform positions[] = { { 30, 0 }, { 0, 30 }, { -30, 0 } };
    for (int i = 0; i < 3; i++) {
        bodies[i] = ecs_world_create_entity(world);
        component(world, bodies[i], &GAME_COMPONENT_GameTransform, &positions[i]);
        component(world, bodies[i], &GAME_COMPONENT_GameVelocity, &(GameVelocity){ 0 });
        component(world, bodies[i], &GAME_COMPONENT_GameCollider, &(GameCollider){ .radius = 2 });
        component(world, bodies[i], &GAME_COMPONENT_GameBody, &(GameBody){ .type = GAME_BODY_DYNAMIC });
        component(world, bodies[i], &GAME_COMPONENT_GameHealth, &(GameHealth){ i == 2 ? 1 : 50, 50 });
    }
    assert(!ecs_world_apply_commands(world));
    assert(!game_behavior_attach(live->behaviors, bodies[0], "MaterialImpactHealth"));
    assert(!game_behavior_attach(live->behaviors, bodies[2], "MaterialImpactHealth"));
    engine *runtime = engine_new(world, live);
    const char *substances[] = { "FuelGas", "Smoke" };
    test_register_prototypes(live, substances, 2);
    assert(!engine_frame(runtime, 0));
    assert(!game_physics_tick(live->physics, 0));
    for (int i = 0; i < 8; i++) {
        fluid_particle fuel = material_fluid_particle(
            game_materials_fluid(live->materials),
            game_materials_substance(live->materials, "FuelGas"),
            (fluid_vec){ (i % 4) * 2 - 3, (i / 4) * 2 - 1 }
        );
        fuel.temperature = 2;
        assert(game_materials_emit(live->materials, fuel));
    }
    assert(!game_physics_tick(live->physics, 1.f / 60));
    int blast_count;
    material_fluid_blasts(game_materials_fluid(live->materials), &blast_count);
    assert(blast_count > 0);
    assert(GAME_GameHealth_get_const(world, bodies[0])->current == 50);
    assert(GAME_GameHealth_get_const(world, bodies[1])->current == 50);
    assert(ecs_entity_is_alive(world, bodies[2]));
    int result = engine_frame(runtime, 1 / 60.);
    if (result) {
        fprintf(stderr, "blast graph: %s\n", engine_error(runtime));
    }
    assert(!result);
    assert(GAME_GameHealth_get_const(world, bodies[0])->current < 50);
    assert(GAME_GameHealth_get_const(world, bodies[1])->current == 50);
    assert(!ecs_entity_is_alive(world, bodies[2]));
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(world);
}

/* A static solid below y = floor (gameplay y runs down), 128 units wide. */
static material_solids *floor_solids(material_mask *mask, float floor)
{
    enum
    {
        WIDTH = 64,
        HEIGHT = 16
    };
    float distance[WIDTH * HEIGHT];
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            distance[y * WIDTH + x] = 10 - (y + .5f) * 2;
        }
    }
    *mask = material_mask_from_field(WIDTH, HEIGHT, 2, distance);
    material_solids *solids = material_solids_new();
    material_solid solid = { mask, { 0, floor + 6 }, 0, 0 };
    material_solids_set(solids, &solid, 1);
    return solids;
}

static material_chunk debris_chunk(fluid_vec position, fluid_vec velocity, float age)
{
    material_chunk chunk = { .position = position,
                             .velocity = velocity,
                             .radius = .8f,
                             .age = age,
                             .lifetime = 5 };
    material_chunk_shape(&chunk, 7);
    return chunk;
}

static void debris()
{
    material_mask floor;
    material_solids *solids = floor_solids(&floor, 10);
    material_debris *debris = material_debris_new(4);
    material_debris_add(debris, debris_chunk((fluid_vec){ 0, 0 }, (fluid_vec){ 0, 60 }, 0));
    int count;
    const material_chunk *chunk = material_debris_chunks(debris, &count);
    assert(chunk->count >= 5 && chunk->count <= MATERIAL_CHUNK_POINTS);
    for (int k = 0; k < chunk->count; k++) {
        assert(hypotf(chunk->points[2 * k], chunk->points[2 * k + 1]) <= chunk->radius + 1e-4f);
    }
    for (int i = 0; i < 60; i++) {
        material_debris_tick(
            debris,
            &(fluid_step){ .delta = 1 / 60.f, .colliders = { 0 }, .solids = solids },
            NULL,
            0
        );
        chunk = material_debris_chunks(debris, &count);
        assert(count == 1 && chunk->position.y <= 9.25f);
    }
    /* Too small to split, it bounced back up, losing most of its speed. */
    assert(chunk->velocity.y < 0 && chunk->velocity.y > -30);

    /* A hard knock splits a large chunk into smaller pieces that keep its
     * material and heat; pieces of pieces split once more at most. */
    material_debris_clear(debris);
    material_chunk large = debris_chunk((fluid_vec){ 0, 0 }, (fluid_vec){ 0, 90 }, 1);
    large.radius = 2;
    large.source = 42;
    large.heat = 1;
    material_chunk_shape(&large, 3);
    material_debris_add(debris, large);
    for (int i = 0; i < 30; i++) {
        material_debris_tick(
            debris,
            &(fluid_step){ .delta = 1 / 60.f, .colliders = { 0 }, .solids = solids },
            NULL,
            0
        );
    }
    chunk = material_debris_chunks(debris, &count);
    assert(count >= 2 && count <= 4);
    for (int i = 0; i < count; i++) {
        assert(chunk[i].source == 42 && chunk[i].generation >= 1 && chunk[i].radius < 1.5f);
        assert(chunk[i].heat > 0 && chunk[i].heat < 1 && chunk[i].position.y <= 10);
    }

    /* A blast throws a chunk away from its center. */
    material_debris_clear(debris);
    material_debris_add(debris, debris_chunk((fluid_vec){ 0, 8 }, (fluid_vec){ 0 }, 0));
    material_debris_push(debris, (material_debris_blast){ { 0, 12 }, 20, 200 });
    chunk = material_debris_chunks(debris, &count);
    assert(chunk->velocity.y < -20 && chunk->shock > 20);

    /* A moving box body shoves chunks it overlaps. */
    material_debris_clear(debris);
    material_debris_add(debris, debris_chunk((fluid_vec){ 0, 0 }, (fluid_vec){ 0 }, 0));
    fluid_body body = { .position = { -1.5f, 0 },
                        .velocity = { 40, 0 },
                        .inverse_mass = 1 };
    fluid_obstacle box = { .body = 0, .half_size = { 1, 1 } };
    material_debris_tick(
        debris,
        &(fluid_step){ .delta = 1 / 60.f, .colliders = { &body, 1, &box, 1 }, .solids = NULL },
        NULL,
        0
    );
    chunk = material_debris_chunks(debris, &count);
    assert(chunk->position.x >= .29f && chunk->velocity.x > 30);

    /* A full pool replaces its oldest chunk; chunks shrink, then expire. */
    material_debris_free(debris);
    debris = material_debris_new(2);
    material_debris_add(debris, debris_chunk((fluid_vec){ 1, 0 }, (fluid_vec){ 0 }, 3));
    material_debris_add(debris, debris_chunk((fluid_vec){ 2, 0 }, (fluid_vec){ 0 }, 1));
    material_debris_add(debris, debris_chunk((fluid_vec){ 3, 0 }, (fluid_vec){ 0 }, 0));
    chunk = material_debris_chunks(debris, &count);
    assert(count == 2 && chunk[0].position.x == 3 && chunk[1].position.x == 2);
    assert(material_chunk_scale(&chunk[0]) == 1);
    int expired = 0;
    for (int i = 0; i < 280; i++) {
        material_debris_tick(
            debris,
            &(fluid_step){ .delta = 1 / 60.f, .colliders = { 0 }, .solids = NULL },
            NULL,
            0
        );
        int gone;
        const material_chunk *spent = material_debris_expired(debris, &gone);
        assert(gone == 0 || (gone == 1 && spent->position.x == 2));
        expired += gone;
    }
    chunk = material_debris_chunks(debris, &count);
    assert(expired == 1 && count == 1 && chunk->position.x == 3);
    assert(material_chunk_scale(chunk) < 1 && material_chunk_scale(chunk) > 0);
    material_debris_free(debris);
    material_solids_free(solids);
    material_mask_free(&floor);
}

/* A static solid mask whose open space is the rectangle half_open around
 * open_center, in the mask's local frame; everything else is rock. */
static material_mask open_box_cells(int width, int height, float cell, fluid_vec open_center, fluid_vec half_open)
{
    float *distance = malloc((size_t)width * height * sizeof *distance);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float px = ((x + .5f) - width * .5f) * cell - open_center.x;
            float py = ((y + .5f) - height * .5f) * cell - open_center.y;
            float qx = fabsf(px) - half_open.x, qy = fabsf(py) - half_open.y;
            float outside = hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) +
                            fminf(fmaxf(qx, qy), 0);
            distance[y * width + x] = -outside;
        }
    }
    material_mask mask = material_mask_from_field(width, height, cell, distance);
    free(distance);
    return mask;
}

static material_mask open_box(int width, int height, fluid_vec open_center, fluid_vec half_open)
{
    return open_box_cells(width, height, 2, open_center, half_open);
}

static void fall(material_fluid *fluid, const material_solids *solids, int steps)
{
    fluid_region region = { -1000, -1000, 2000, 2000 };
    for (int i = 0; i < steps; i++) {
        material_fluid_tick(
            fluid,
            &(fluid_step){ .delta = 1.f / 60,
                           .chunk_size = 64,
                           .gravity = { 0, 320 },
                           .regions = &region,
                           .region_count = 1,
                           .solids = solids }
        );
    }
}

static void gravity_pooling()
{
    /* A bowl 96 units wide with its floor at y = 32. Liquid dropped as a tall
     * column spreads over the whole floor and comes to rest above it. */
    material_mask bowl = open_box(64, 48, (fluid_vec){ 0, -14 }, (fluid_vec){ 48, 46 });
    material_solids *solids = material_solids_new();
    material_solids_set(solids, &(material_solid){ &bowl, { 0, 0 }, 0, 0 }, 1);
    material_fluid *fluid = test_fluid(10, 400);
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 8; x++) {
            assert(material_fluid_add(fluid, particle(-14 + x * 4, -50 + y * 4, FLUID_LIQUID)));
        }
    }
    fall(fluid, solids, 360);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    float left = INFINITY, right = -INFINITY, top = INFINITY, speed = 0;
    for (int i = 0; i < count; i++) {
        material_solid_sample s = material_solids_sample(solids, p[i].position);
        assert(s.distance > -1 && p[i].position.y < 32);
        left = fminf(left, p[i].position.x);
        right = fmaxf(right, p[i].position.x);
        top = fminf(top, p[i].position.y);
        speed += hypotf(p[i].velocity.x, p[i].velocity.y) / count;
    }
    printf("liquid pool: width %.1f, depth %.1f, mean speed %.2f\n", right - left, 32 - top, speed);
    assert(count == 128 && right - left > 80 && 32 - top < 40 && speed < 15);
    material_fluid_free(fluid);

    /* A blob on a slope runs downhill and falls down a shaft. The slope
     * descends to the right into a shaft at its low end. */
    material_mask ramp = { 0 };
    {
        enum
        {
            WIDTH = 96,
            HEIGHT = 96
        };
        float *distance = malloc(WIDTH * HEIGHT * sizeof *distance);
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                float px = ((x + .5f) - WIDTH * .5f) * 2,
                      py = ((y + .5f) - HEIGHT * .5f) * 2;
                /* Ground under the line y = x / 2 and right of x = 80,
                 * except the shaft 60 < x < 80. */
                float ground = fmaxf((py - px * .5f) * .894f, px - 80);
                float shaft = fminf(px - 60, 80 - px);
                distance[y * WIDTH + x] = fmaxf(-ground, shaft);
            }
        }
        ramp = material_mask_from_field(WIDTH, HEIGHT, 2, distance);
        free(distance);
    }
    material_solids_set(solids, &(material_solid){ &ramp, { 0, 0 }, 0, 0 }, 1);
    fluid = test_fluid(10, 100);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            assert(material_fluid_add(fluid, particle(-60 + x * 4, -46 + y * 4, FLUID_LIQUID)));
        }
    }
    fall(fluid, solids, 240);
    p = material_fluid_particles(fluid, &count);
    for (int i = 0; i < count; i++) {
        assert(material_solids_sample(solids, p[i].position).distance > -1);
        assert(p[i].position.y > 60);
    }
    material_fluid_free(fluid);
    material_solids_free(solids);
    material_mask_free(&bowl);
    material_mask_free(&ramp);
    puts("PASS: liquid pools on a floor, runs down a slope and falls down a shaft");
}

static game_live_world *matter_world(ecs_world **world, const char *const *substances, int count)
{
    *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(*world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    game_live_world *live = game_live_world_new(*world);
    test_register_prototypes(live, substances, count);
    game_physics_settings settings = game_physics_defaults();
    settings.gravity_y = 320;
    assert(game_physics_configure(live->physics, settings) == 0);
    ecs_entity config = ecs_world_create_entity(*world),
               camera = ecs_world_create_entity(*world);
    component(*world, config, &GAME_COMPONENT_MaterialDestructionWorld, &(MaterialDestructionWorld){ 8 });
    component(
        *world,
        config,
        &GAME_COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ .chunk_size = 64,
                               .buffer = 32,
                               .kernel = 10,
                               .particle_limit = 400,
                               .heat_conduction = 2 }
    );
    component(*world, camera, &GAME_COMPONENT_GameCamera, &(GameCamera){ .width = 256, .height = 256 });
    component(*world, camera, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    return live;
}

static ecs_entity slab(ecs_world *world, float x, float y, GameSubstance substance)
{
    ecs_entity entity = ecs_world_create_entity(world);
    component(world, entity, &GAME_COMPONENT_GameTransform, &(GameTransform){ x, y });
    component(
        world,
        entity,
        &GAME_COMPONENT_GameCollider,
        &(
            GameCollider
        ){ .shape = GAME_COLLIDER_BOX, .width = 40, .height = 20, .category = 8, .mask = 15 }
    );
    component(world, entity, &GAME_COMPONENT_GameDestructible, &(GameDestructible){ 2, 4 });
    component(world, entity, &GAME_COMPONENT_GameSubstance, &substance);
    return entity;
}

static void pour(game_live_world *live, const char *substance, fluid_vec at, int columns, int rows)
{
    const material_fluid *fluid = game_materials_fluid(live->materials);
    int index = game_materials_substance(live->materials, substance);
    assert(fluid && index >= 0);
    float spacing = material_fluid_spacing(
        fluid,
        material_fluid_substances(fluid, &(int){ 0 })[index].phase
    );
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < columns; x++) {
            fluid_vec p = { at.x + (x - columns * .5f) * spacing, at.y - y * spacing };
            assert(game_materials_emit(live->materials, material_fluid_particle(fluid, index, p)));
        }
    }
}

/* Acid eats a soft slab but not a resistant one, and is spent by eating. */
static void corrosion()
{
    ecs_world *world;
    const char *substances[] = { "Acid" };
    game_live_world *live = matter_world(&world, substances, 1);
    ecs_entity soft = slab(world, -40, 30, (GameSubstance){ .hardness = 1 });
    ecs_entity metal = slab(world, 40, 30, (GameSubstance){ .hardness = 1, .corrosion_resistance = 1 });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->physics, 0) == 0);
    int soft_cells = game_materials_mask(live->materials, soft)->count;
    int metal_cells = game_materials_mask(live->materials, metal)->count;
    pour(live, "Acid", (fluid_vec){ -40, 12 }, 8, 2);
    pour(live, "Acid", (fluid_vec){ 40, 12 }, 8, 2);
    int poured = game_materials_inspect(live->materials).particles;
    for (int i = 0; i < 480; i++) {
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    int eaten = soft_cells - game_materials_mask(live->materials, soft)->count;
    int left = game_materials_inspect(live->materials).particles;
    printf("corrosion: %d of %d soft cells dissolved, %d of %d acid parcels left\n", eaten, soft_cells, left, poured);
    assert(eaten > 0 && game_materials_mask(live->materials, metal)->count == metal_cells);
    assert(left < poured && left >= poured / 2);
    game_live_world_free(live);
    ecs_world_free(world);
}

static ecs_entity deposit_terrain(ecs_world *world, game_live_world *live, const char *deposit, GameSubstance substance)
{
    char *text = text_file_read("projects/arena2d/content/fields/Empty.yaml");
    behavior_asset *field = behavior_asset_load_json(text);
    free(text);
    char error[256];
    assert(
        field &&
        material_field_compile(game_materials_fields(live->materials), field, error, sizeof error)
    );
    behavior_asset_free(field);
    ecs_entity entity = ecs_world_create_entity(world);
    component(world, entity, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(world, entity, &GAME_COMPONENT_GameSubstance, &substance);
    component(
        world,
        entity,
        &GAME_COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .cell_size = 2,
                            .field = "Empty",
                            .category = 8,
                            .mask = 15,
                            .min_fragment_cells = 4,
                            .stream_radius = 128,
                            .stream_budget = 64,
                            .deposit = deposit }
    );
    return entity;
}

static int deposited_cells(game_live_world *live)
{
    return game_materials_inspect(live->materials).cells;
}

/* Lava cooling on its own becomes basalt cells in the terrain that receives
 * them; they persist when the camera leaves and returns. Heat melts ice
 * deposits back into water. */
static void deposition()
{
    ecs_world *world;
    const char *substances[] = { "Lava", "Water", "Steam", "ReactionQuench" };
    game_live_world *live = matter_world(&world, substances, 4);
    deposit_terrain(world, live, "Basalt", (GameSubstance){ .hardness = 2 });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->physics, 0) == 0);
    assert(deposited_cells(live) == 0);
    const material_fluid *fluid = game_materials_fluid(live->materials);
    fluid_particle lava = material_fluid_particle(
        fluid,
        game_materials_substance(live->materials, "Lava"),
        (fluid_vec){ 20, 30 }
    );
    lava.temperature = .8f;
    assert(game_materials_emit(live->materials, lava));
    for (int i = 0; i < 10; i++) {
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    int cells = deposited_cells(live);
    printf(
        "deposit: %d cells, %d particles, resident %d\n",
        cells,
        game_materials_inspect(live->materials).particles,
        game_materials_inspect(live->materials).streaming.resident
    );
    assert(cells > 0 && game_materials_inspect(live->materials).particles == 0);
    ecs_entity camera = { 0 };
    ecs_component_id id = GAME_COMPONENT_GameCamera.id;
    ecs_query query;
    assert(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    assert(ecs_query_next(&query, &camera));
    ecs_query_end(&query);
    GAME_GameTransform_get(world, camera)->x = 5000;
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(
        deposited_cells(live) == 0 &&
        game_materials_inspect(live->materials).streaming.unloaded > 0
    );
    GAME_GameTransform_get(world, camera)->x = 0;
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    assert(deposited_cells(live) == cells);
    game_live_world_free(live);
    ecs_world_free(world);

    /* Heat melts a deposit of ice back into water. */
    live = matter_world(&world, substances, 4);
    deposit_terrain(world, live, "Ice", (GameSubstance){ .hardness = 1, .melts_at = .5f, .melts_to = "Water" });
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(game_physics_tick(live->physics, 0) == 0);
    fluid = game_materials_fluid(live->materials);
    fluid_particle water = material_fluid_particle(
        fluid,
        game_materials_substance(live->materials, "Water"),
        (fluid_vec){ -20, 30 }
    );
    assert(game_materials_emit(live->materials, water));
    assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    game_materials_heat_area(live->materials, (fluid_vec){ -20, 30 }, 4, -3);
    for (int i = 0; i < 3; i++) {
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    int ice = deposited_cells(live);
    assert(ice > 0 && game_materials_inspect(live->materials).particles == 0);
    game_materials_heat_area(live->materials, (fluid_vec){ -20, 30 }, 6, 1);
    for (int i = 0; i < 2; i++) {
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    printf(
        "melting: %d ice cells melted into %d water parcels\n",
        ice - deposited_cells(live),
        game_materials_inspect(live->materials).particles
    );
    assert(
        deposited_cells(live) < ice && game_materials_inspect(live->materials).particles > 0
    );
    game_live_world_free(live);
    ecs_world_free(world);
    puts("PASS: corrosion resistance, deposits that stream and persist, and melting");
}

static node *matter_node(graph *g, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    return graph_add_node(g, desc);
}

static void matter_default(graph *g, node *n, int index, pin_value value)
{
    assert(graph_set_pin_default(g, n, 0, index, &value) == 0);
}

static pin_value probe_variable(game_live_world *live, ecs_entity entity, behavior_variable *variable)
{
    behavior_instance *instance = game_behavior_instance(live->behaviors, entity, "MatterProbe");
    pin_value value;
    assert(behavior_instance_get_variable(instance, variable->id, &value) == 0);
    return value;
}

/* Emit Fluid and Heat Area from one event; Fluid At reads the result from
 * another; Material At finds the solid under a point. */
static void matter_nodes()
{
    ecs_world *world;
    const char *substances[] = { "Water", "Steam" };
    game_live_world *live = matter_world(&world, substances, 2);
    ecs_entity rock = slab(world, 0, 200, (GameSubstance){ .hardness = 3 });
    ecs_entity owner = ecs_world_create_entity(world);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    behavior_asset *asset = behavior_asset_new("MatterProbe");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    pin_value text = { .type = PIN_STR, .type_name = "string", .str = "" };
    pin_value number = { .type = PIN_F32, .type_name = "float" };
    behavior_variable *found = behavior_asset_add_variable(
        asset,
        "Found",
        PIN_STR,
        "string",
        PIN_CONTAINER_SINGLE,
        &text,
        "Test"
    );
    behavior_variable *heat = behavior_asset_add_variable(
        asset,
        "Heat",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &number,
        "Test"
    );
    graph *g = asset->graph;
    node *pour = matter_node(g, "Event");
    assert(graph_set_event_name(g, pour, "Pour") == 0);
    node *emit = matter_node(g, "Emit Fluid");
    matter_default(g, emit, 1, (pin_value){ .type = PIN_NAME, .str = "Water" });
    matter_default(g, emit, 4, (pin_value){ .type = PIN_I32, .i = 24 });
    matter_default(g, emit, 5, (pin_value){ .type = PIN_F32, .f = 6 });
    node *warm = matter_node(g, "Heat Area");
    matter_default(g, warm, 2, (pin_value){ .type = PIN_F32, .f = 30 });
    matter_default(g, warm, 3, (pin_value){ .type = PIN_F32, .f = .5f });
    assert(graph_connect_exec(g, pour, 0, emit, 0, 0) == 0);
    assert(graph_connect_exec(g, emit, 0, warm, 0, 0) == 0);
    node *probe = matter_node(g, "Event");
    assert(graph_set_event_name(g, probe, "Probe") == 0);
    node *sample = matter_node(g, "Fluid At");
    matter_default(g, sample, 1, (pin_value){ .type = PIN_F32, .f = 12 });
    node *set_found = behavior_asset_add_set_node(asset, found->id);
    node *set_heat = behavior_asset_add_set_node(asset, heat->id);
    assert(graph_connect_exec(g, probe, 0, set_found, 0, 0) == 0);
    assert(graph_connect_exec(g, set_found, 0, set_heat, 0, 0) == 0);
    assert(graph_connect_data(g, sample, 0, set_found, 1) == 0);
    assert(graph_connect_data(g, sample, 2, set_heat, 1) == 0);
    assert(game_behavior_register_asset(live->behaviors, "MatterProbe", asset) == 0);
    assert(game_behavior_attach(live->behaviors, owner, "MatterProbe") == 0);
    assert(game_physics_tick(live->physics, 0) == 0);
    const char *events[] = { "Pour", "Heat", "Probe" };
    for (int e = 0; e < 3; e++) {
        if (e != 1) {
            assert(
                game_entities_request_event(live->entities, owner, events[e], (flow_inputs){ 0 }) == FLOW_OK
            );
            do {
                assert(game_entities_dispatch_events(live->entities) >= 0);
            } while (game_entities_pending_events(live->entities));
        }
        assert(game_physics_tick(live->physics, 1.f / 60) == 0);
    }
    pin_value substance = probe_variable(live, owner, found);
    float temperature = probe_variable(live, owner, heat).f;
    printf("matter nodes: found %s at temperature %.3f\n", substance.str, temperature);
    assert(!strcmp(substance.str, "Water") && temperature > .2f && temperature < .5f);
    material_hit hit = game_materials_at(live->materials, (fluid_vec){ 0, 200 });
    assert(
        hit.solid && ecs_entity_to_u64(hit.entity) == ecs_entity_to_u64(rock) &&
        hit.hardness == 3
    );
    assert(!game_materials_at(live->materials, (fluid_vec){ 0, 0 }).solid);
    game_live_world_free(live);
    ecs_world_free(world);
    puts("PASS: Emit Fluid, Heat Area, Fluid At and Material At");
}

enum
{
    MPM_WATER,
    MPM_SAND,
    MPM_JELLY,
    MPM_SUBSTANCES
};

static material_fluid *mpm_fluid(int capacity)
{
    material_fluid *f = material_fluid_new(10, capacity);
    fluid_substance substances[MPM_SUBSTANCES] = {
        [MPM_WATER] = { .phase = FLUID_LIQUID,
                        .density = .25f,
                        .stiffness = .9f,
                        .viscosity = .02f,
                        .cohesion = .5f,
                        .friction = .05f },
        [MPM_SAND] = { .phase = FLUID_POWDER, .density = .6f, .stiffness = .9f, .friction = .75f },
        [MPM_JELLY] = { .phase = FLUID_ELASTIC, .density = .4f, .stiffness = .6f, .friction = .6f },
    };
    for (int i = 0; i < MPM_SUBSTANCES; i++) {
        substances[i].burns_to = FLUID_GONE;
        substances[i].boils_to = substances[i].freezes_to = FLUID_KEEP;
    }
    material_fluid_set_substances(f, substances, MPM_SUBSTANCES);
    return f;
}

static void fill(material_fluid *f, int substance, fluid_vec center, int columns, int rows, uint64_t owner)
{
    float spacing = material_fluid_spacing(f, FLUID_LIQUID);
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < columns; x++) {
            fluid_vec at = { center.x + (x - (columns - 1) * .5f) * spacing,
                             center.y + (y - (rows - 1) * .5f) * spacing };
            fluid_particle p = material_fluid_particle(f, substance, at);
            p.owner = owner;
            assert(material_fluid_add(f, p));
        }
    }
}

typedef struct
{
    fluid_vec low, high;
    int count;
} extent;

static extent extent_of(const material_fluid *f, int substance)
{
    extent e = { { INFINITY, INFINITY }, { -INFINITY, -INFINITY }, 0 };
    int count;
    const fluid_particle *p = material_fluid_particles(f, &count);
    for (int i = 0; i < count; i++) {
        if (p[i].substance != substance) {
            continue;
        }
        e.low.x = fminf(e.low.x, p[i].position.x);
        e.low.y = fminf(e.low.y, p[i].position.y);
        e.high.x = fmaxf(e.high.x, p[i].position.x);
        e.high.y = fmaxf(e.high.y, p[i].position.y);
        e.count++;
    }
    return e;
}

/* A 400-unit bowl, floor at y = 100, with water, a sand heap and a jelly
 * block dropped in. */
static void mpm_scene(material_fluid *f, material_mask *bowl, material_solids *solids)
{
    *bowl = open_box(256, 160, (fluid_vec){ 0, -60 }, (fluid_vec){ 200, 160 });
    material_solids_set(solids, &(material_solid){ bowl, { 0, 0 }, 0, 0 }, 1);
    fill(f, MPM_WATER, (fluid_vec){ -120, 40 }, 30, 30, 0);
    fill(f, MPM_SAND, (fluid_vec){ 80, 20 }, 16, 30, 0);
    fill(f, MPM_JELLY, (fluid_vec){ 150, -60 }, 10, 10, 7);
}

static void mpm_run(
    material_fluid *f,
    const material_solids *solids,
    fluid_body *body,
    fluid_obstacle *obstacle,
    int steps
)
{
    fluid_region region = { -2000, -2000, 4000, 4000 };
    for (int i = 0; i < steps; i++) {
        material_fluid_tick(
            f,
            &(fluid_step){ .delta = 1.f / 60,
                           .chunk_size = 64,
                           .gravity = { 0, 320 },
                           .regions = &region,
                           .region_count = 1,
                           .colliders = { body, body ? 1 : 0, obstacle, obstacle ? 1 : 0 },
                           .solids = solids }
        );
    }
}

/* Ten seconds of water, sand and jelly in a bowl stay finite, slow, out of
 * the rock and inside the bowl; sand keeps a heap and jelly its size. */
static void mpm_stability()
{
    material_fluid *f = mpm_fluid(2000);
    material_mask bowl;
    material_solids *solids = material_solids_new();
    mpm_scene(f, &bowl, solids);
    extent jelly = extent_of(f, MPM_JELLY);
    float jelly_area = (jelly.high.x - jelly.low.x) * (jelly.high.y - jelly.low.y);
    for (int second = 0; second < 10; second++) {
        mpm_run(f, solids, NULL, NULL, 60);
        int count;
        const fluid_particle *p = material_fluid_particles(f, &count);
        assert(count == 1480);
        for (int i = 0; i < count; i++) {
            assert(isfinite(
                p[i].position.x + p[i].position.y + p[i].velocity.x +
                p[i].velocity.y
            ));
            assert(
                fabsf(p[i].position.x) < 204 && p[i].position.y < 104 &&
                p[i].position.y > -300
            );
            assert(material_solids_sample(solids, p[i].position).distance > -4);
        }
    }
    extent water = extent_of(f, MPM_WATER), sand = extent_of(f, MPM_SAND);
    jelly = extent_of(f, MPM_JELLY);
    float speed = 0;
    int count;
    const fluid_particle *p = material_fluid_particles(f, &count);
    for (int i = 0; i < count; i++) {
        speed = fmaxf(speed, hypotf(p[i].velocity.x, p[i].velocity.y));
    }
    float jelly_now = (jelly.high.x - jelly.low.x) * (jelly.high.y - jelly.low.y);
    printf(
        "mpm after 10 s: water %.0f..%.0f x %.0f..%.0f, sand %.0f..%.0f x "
        "%.0f..%.0f, jelly area %.0f of %.0f, top speed %.1f\n",
        water.low.x,
        water.high.x,
        water.low.y,
        water.high.y,
        sand.low.x,
        sand.high.x,
        sand.low.y,
        sand.high.y,
        jelly_now,
        jelly_area,
        speed
    );
    /* Water spreads over the floor; sand stands as a heap; jelly holds. */
    assert(water.high.x - water.low.x > 250 && 100 - water.low.y < 80);
    assert(100 - sand.low.y > 20);
    assert(jelly_now > jelly_area * .5f && jelly_now < jelly_area * 2);
    assert(speed < 150);
    material_fluid_free(f);
    material_solids_free(solids);
    material_mask_free(&bowl);
    puts("PASS: MPM water, sand and jelly stay stable, bounded and out of solids for ten seconds");
}

/* A body falling into water is slowed by it and pushes the water aside; the
 * owner handles find, move and carve a jelly. Serial and parallel runs of
 * the whole scene match exactly. */
static void mpm_coupling()
{
    material_fluid *f = mpm_fluid(2000);
    material_mask bowl;
    material_solids *solids = material_solids_new();
    mpm_scene(f, &bowl, solids);
    mpm_run(f, solids, NULL, NULL, 120);
    fluid_body body = { .position = { -120, 30 },
                        .previous_position = { -120, 28 },
                        .velocity = { 0, 120 },
                        .inverse_mass = 20,
                        .swept = true };
    fluid_obstacle box = { .half_size = { 12, 12 } };
    mpm_run(f, solids, &body, &box, 1);
    printf("mpm body impulse %.4f\n", body.impulse.y);
    assert(body.impulse.y < 0);

    int count;
    fluid_vec before = material_fluid_centroid(f, 7, &count);
    assert(count == 100);
    material_fluid_push_owner(f, 7, (fluid_vec){ -4000, 0 });
    mpm_run(f, solids, NULL, NULL, 10);
    fluid_vec after = material_fluid_centroid(f, 7, &count);
    printf("jelly moved from %.1f to %.1f\n", before.x, after.x);
    assert(after.x < before.x - 1);
    assert(material_fluid_remove_owned(f, 7, after, 12) > 0);
    material_fluid_centroid(f, 7, &count);
    assert(count < 100);

    material_fluid *serial = mpm_fluid(2000), *parallel = mpm_fluid(2000);
    material_mask second;
    material_solids *parallel_solids = material_solids_new();
    material_mask_free(&bowl);
    mpm_scene(serial, &bowl, solids);
    mpm_scene(parallel, &second, parallel_solids);
    game_task_pool *pool = game_task_pool_acquire_workers(4);
    material_fluid_set_tasks(parallel, game_task_pool_tasks(pool));
    for (int i = 0; i < 90; i++) {
        fluid_body a = body, b = body;
        mpm_run(serial, solids, &a, &box, 1);
        game_task_pool_begin(pool, true);
        mpm_run(parallel, parallel_solids, &b, &box, 1);
        game_task_pool_end(pool);
        assert(a.impulse.x == b.impulse.x && a.impulse.y == b.impulse.y);
    }
    int other;
    const fluid_particle *pa = material_fluid_particles(serial, &count);
    const fluid_particle *pb = material_fluid_particles(parallel, &other);
    assert(count == other && !memcmp(pa, pb, (size_t)count * sizeof *pa));
    game_task_pool_release(pool);
    material_fluid_free(serial);
    material_fluid_free(parallel);
    material_fluid_free(f);
    material_solids_free(solids);
    material_solids_free(parallel_solids);
    material_mask_free(&bowl);
    material_mask_free(&second);
    puts("PASS: MPM body coupling, owned matter handles and serial/parallel parity");
}

void test_materials()
{
    gravity_pooling();
    substance_chemistry();
    corrosion();
    deposition();
    matter_nodes();
    mpm_stability();
    mpm_coupling();
    blast_policy();
    rotated_occlusion();
    fragment_penetration();
    fragment_material_frame();
    debris();
    swept_ignition();
    reactions();
    resistance_and_chips();
    material_capacity_fallback();
    small_hull_release();
    destruction();
    contours();
    cave_generation();
    shipping_cave_parity();
    sleeping();
    cohesion();
    liquid_split_merge();
    body_response();
    gas_flow();
    parallel_parity(false);
    parallel_parity(true);
    physical_destruction();
    finite_drive();
    terrain_boundary_cut();
    terrain_stream_restore();
    puts("PASS: material destruction, cohesion, sleeping, momentum and parallel parity");
}

static double bench_now()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000.0 + now.tv_nsec * 1e-6;
}

/* Times the fluid step with a pool of `particles` MPM water particles in a
 * wide bowl, stirred by a moving body, on the shared worker pool. */
int bench_matter(int particles, int frames)
{
    if (particles < 100 || frames < 1) {
        return 2;
    }
    material_fluid *f = mpm_fluid(particles + 16);
    /* A shallow pool, as wide as a 2048-unit bowl allows. */
    float spacing = material_fluid_spacing(f, FLUID_LIQUID);
    int columns = (int)fminf(480, sqrtf(particles * 6.f)), rows = particles / columns;
    material_mask bowl = open_box_cells(
        256,
        128,
        8,
        (fluid_vec){ 0, -312 },
        (fluid_vec){ columns * spacing * .5f + 32, 512 }
    );
    material_solids *solids = material_solids_new();
    material_solids_set(solids, &(material_solid){ &bowl, { 0, 0 }, 0, 0 }, 1);
    fill(f, MPM_WATER, (fluid_vec){ 0, 196 - rows * spacing * .5f }, columns, rows, 0);
    game_task_pool *pool = game_task_pool_acquire();
    material_fluid_set_tasks(f, game_task_pool_tasks(pool));
    fluid_obstacle box = { .half_size = { 16, 16 } };
    double total = 0, worst = 0;
    for (int i = 0; i < 60 + frames; i++) {
        float x = sinf(i * .02f) * columns * spacing * .3f;
        fluid_body body = { .position = { x, 196 - rows * spacing * .5f },
                            .velocity = { 60, 0 },
                            .inverse_mass = .01f };
        double start = bench_now();
        game_task_pool_begin(pool, true);
        mpm_run(f, solids, &body, &box, 1);
        game_task_pool_end(pool);
        double elapsed = bench_now() - start;
        if (i >= 60) {
            total += elapsed;
            worst = fmax(worst, elapsed);
        }
    }
    printf(
        "matter particles=%d active=%d workers=%d mean_ms=%.3f worst_ms=%.3f\n",
        particles,
        material_fluid_active_count(f),
        game_task_pool_tasks(pool)->workers,
        total / frames,
        worst
    );
    game_task_pool_release(pool);
    material_fluid_free(f);
    material_solids_free(solids);
    material_mask_free(&bowl);
    return 0;
}
