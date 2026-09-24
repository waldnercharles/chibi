#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/material_terrain.h"
#include "support/text_file.h"
float *reference_cave_generate(unsigned, int, int, float, float, float);
#include "runtime/material_debris.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "runtime/task_pool.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    material_mask *pieces = material_mask_split(&mask, 3, &dust);
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
    pieces = material_mask_split(&mask, 1000, &dust);
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
    material_mask *pieces = material_mask_split_anchored(&mask, 2, &dust);
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

static fluid_particle particle(float x, float y, int kind)
{
    return (fluid_particle){ .position = { x, y }, .kind = kind, .mass = .01f, .radius = 3 };
}

static void tick(material_fluid *fluid)
{
    fluid_region region = { -1000, -1000, 2000, 2000 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
}

static void sleeping()
{
    material_fluid *fluid = material_fluid_new(10, 2);
    fluid_particle p = particle(-30, 20, FLUID_GAS);
    p.velocity.x = 12;
    p.lifetime = .05f;
    assert(material_fluid_add(fluid, p));
    assert(material_fluid_add(fluid, particle(500, 0, FLUID_GAS)));
    assert(!material_fluid_add(fluid, p));
    for (int i = 0; i < 100; i++) {
        material_fluid_tick(fluid, 1.f / 60, 64, NULL, 0, NULL, 0, NULL, 0);
    }
    int count;
    const fluid_particle *values = material_fluid_particles(fluid, &count);
    assert(count == 2 && !memcmp(&values[0], &p, sizeof p));
    assert(!material_fluid_active_count(fluid));
    /* A region elsewhere in the same negative-coordinate chunk wakes it. */
    fluid_region region = { -60, 5, 2, 2 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
    assert(material_fluid_active_count(fluid) == 1);
    assert(fabsf(values[0].position.x - (p.position.x + .2f)) < .001f);
    assert(values[1].position.x == 500);
    for (int i = 0; i < 4; i++) {
        material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
    }
    values = material_fluid_particles(fluid, &count);
    assert(count == 1 && values[0].position.x == 500);
    material_fluid_free(fluid);
}

static void cohesion()
{
    material_fluid *fluid = material_fluid_new(10, 1000);
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
    material_fluid *fluid = material_fluid_new(10, 1000);
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
    for (int i = 0; i < 180; i++) {
        tick(fluid);
    }
    assert(liquid_clusters(fluid) == 1);
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
    material_fluid *fluid = material_fluid_new(10, 2);
    fluid_particle p = particle(9, 0, FLUID_GAS);
    p.velocity.x = -10;
    material_fluid_add(fluid, p);
    fluid_body body = { .inverse_mass = 2 };
    fluid_obstacle obstacle = { .radius = 10 };
    fluid_region region = { -100, -100, 200, 200 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
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
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    out = material_fluid_particles(fluid, &count);
    assert(out[0].velocity.x > 0 && body.impulse.x < 0);
    material_fluid_free(fluid);
}

static void gas_flow()
{
    /* Independent noncombat consumer: a probe stirs initially still gas. */
    material_fluid *fluid = material_fluid_new(10, 400);
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
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &probe, 1, &obstacle, 1);
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
        material_fluid_tick(fluid, 1.f / 60, 64, NULL, 0, NULL, 0, NULL, 0);
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
    material_fluid *serial = material_fluid_new(10, 2500);
    material_fluid *parallel = material_fluid_new(10, 2500);
    if (burning) {
        material_fluid_set_flow(serial, (fluid_flow_settings){ 20, 2, .1f });
        material_fluid_set_flow(parallel, (fluid_flow_settings){ 20, 2, .1f });
        fluid_reaction reaction = { .ignition = 1,
                                    .cooling = 2,
                                    .burn_rate = 2,
                                    .heat_release = 2,
                                    .smoke_lifetime = 3,
                                    .flame_color = 0xff8800ff,
                                    .flame_emission = 1 };
        material_fluid_set_reaction(serial, reaction);
        material_fluid_set_reaction(parallel, reaction);
    }
    for (int y = 0; y < 50; y++) {
        for (int x = 0; x < 50; x++) {
            fluid_particle p = particle(x * 4, y * 4, FLUID_LIQUID);
            if (burning) {
                p.kind = FLUID_GAS;
                p.fuel = 1;
                p.heat = x == 0 ? 2 : 0;
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
    material_fluid *f = material_fluid_new(10, 8);
    fluid_reaction r = { .ignition = 1,
                         .cooling = 2,
                         .burn_rate = 2,
                         .heat_release = 2,
                         .smoke_lifetime = 3,
                         .smoke_color = 0x444444ff,
                         .smoke_absorption = .1f,
                         .flame_color = 0xff8800ff,
                         .flame_emission = 1 };
    material_fluid_set_reaction(f, r);
    fluid_particle hot = particle(-8, 0, FLUID_GAS);
    hot.heat = 2;
    fluid_particle fuel = particle(0, 0, FLUID_GAS);
    fuel.fuel = 1;
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
    assert(p[1].fuel == 0 && p[2].fuel == 0 && !p[1].burning);
    assert(p[1].smoke && p[1].glow > 0 && p[1].glow < 1);
    assert(p[1].color != r.smoke_color && p[1].lifetime > 0 && p[1].lifetime < 3);
    material_fluid_blasts(f, &blasts);
    assert(!blasts);
    float heat = p[1].heat, life = p[1].lifetime;
    material_fluid_tick(f, 1.f / 60, 64, NULL, 0, NULL, 0, NULL, 0);
    assert(p[1].heat == heat && p[1].lifetime == life);
    float glow = p[1].glow, emission = p[1].emission;
    unsigned red = p[1].color >> 24;
    for (int i = 0; i < 60; i++) {
        tick(f);
    }
    assert(p[1].glow < glow * .15f && p[1].emission < emission * .15f);
    assert((p[1].color >> 24) < red && p[1].lifetime < life);
    material_fluid_clear(f);
    material_fluid_add(f, hot);
    fuel.position.x = 0;
    material_fluid_add(f, fuel);
    fluid_body wall = { .position = { -4, 0 } };
    fluid_obstacle obstacle = { .body = 0, .half_size = { 1, 20 } };
    fluid_region region = { -50, -50, 100, 100 };
    material_fluid_tick(f, 1.f / 60, 64, &region, 1, &wall, 1, &obstacle, 1);
    p = material_fluid_particles(f, &count);
    assert(!p[1].burning); /* even a thin wall shields the fuel */
    material_fluid_clear(f);
    material_fluid_add(f, fuel);
    fluid_body shot = { .position = { 20, 0 }, .velocity = { 2400, 0 }, .heat = 2 };
    material_fluid_tick(f, 1.f / 60, 64, &region, 1, &shot, 1, NULL, 0);
    p = material_fluid_particles(f, &count);
    assert(p[0].burning);
    material_fluid_free(f);
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
    assert(
        material_fluid_blocked((fluid_vec){ 0, 50 }, (fluid_vec){ 20, 50 }, &body, &obstacle, 1, -1)
    );
    assert(
        material_fluid_blocked((fluid_vec){ -1000, 50 }, (fluid_vec){ 1000, 50 }, &body, &obstacle, 1, -1)
    );
    assert(
        !material_fluid_blocked((fluid_vec){ 100, 50 }, (fluid_vec){ 120, 50 }, &body, &obstacle, 1, -1)
    );
    obstacle.radius = 2;
    assert(
        material_fluid_blocked((fluid_vec){ 0, 50 }, (fluid_vec){ 20, 50 }, &body, &obstacle, 1, -1)
    );
    assert(
        !material_fluid_blocked((fluid_vec){ 0, 55 }, (fluid_vec){ 20, 55 }, &body, &obstacle, 1, -1)
    );
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
    component(
        world,
        config,
        &GAME_COMPONENT_GameCombustion,
        &(GameCombustion){ .ignition = 1,
                           .cooling = 2,
                           .burn_rate = 2,
                           .heat_release = 2,
                           .smoke_lifetime = 3 }
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
    assert(game_physics_tick(live->physics, 0) == 0);
    fluid_particle fuel = particle(8, 0, FLUID_GAS);
    fuel.fuel = 1;
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
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    component(world, config, &GAME_COMPONENT_GameTransform, &(GameTransform){ 0 });
    component(world, config, &GAME_COMPONENT_GameCamera, &(GameCamera){ .width = 256, .height = 256 });
    component(
        world,
        config,
        &GAME_COMPONENT_GameCombustion,
        &(GameCombustion){ .ignition = 1,
                           .cooling = 2,
                           .burn_rate = 2,
                           .heat_release = 2,
                           .smoke_lifetime = 3,
                           .blast_radius = 200,
                           .blast_speed = 10,
                           .blast_damage = 20 }
    );
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
    assert(!engine_frame(runtime, 0));
    assert(!game_physics_tick(live->physics, 0));
    for (int i = 0; i < 8; i++) {
        fluid_particle fuel = particle((i % 4) * 2 - 3, (i / 4) * 2 - 1, FLUID_GAS);
        fuel.fuel = 1;
        fuel.heat = 2;
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

/* Solid below y = 10 (gameplay y runs down). */
static bool debris_floor(void *user, fluid_vec point, float *distance)
{
    (void)user;
    *distance = 10 - point.y;
    return true;
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
    material_debris *debris = material_debris_new(4);
    material_debris_add(debris, debris_chunk((fluid_vec){ 0, 0 }, (fluid_vec){ 0, 60 }, 0));
    int count;
    const material_chunk *chunk = material_debris_chunks(debris, &count);
    assert(chunk->count >= 5 && chunk->count <= MATERIAL_CHUNK_POINTS);
    for (int k = 0; k < chunk->count; k++) {
        assert(hypotf(chunk->points[2 * k], chunk->points[2 * k + 1]) <= chunk->radius + 1e-4f);
    }
    for (int i = 0; i < 60; i++) {
        material_debris_tick(debris, 1 / 60.f, debris_floor, NULL, NULL, 0, NULL, 0, NULL, 0);
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
        material_debris_tick(debris, 1 / 60.f, debris_floor, NULL, NULL, 0, NULL, 0, NULL, 0);
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
    material_debris_tick(debris, 1 / 60.f, NULL, NULL, &body, 1, &box, 1, NULL, 0);
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
        material_debris_tick(debris, 1 / 60.f, NULL, NULL, NULL, 0, NULL, 0, NULL, 0);
        int gone;
        const material_chunk *spent = material_debris_expired(debris, &gone);
        assert(gone == 0 || (gone == 1 && spent->position.x == 2));
        expired += gone;
    }
    chunk = material_debris_chunks(debris, &count);
    assert(expired == 1 && count == 1 && chunk->position.x == 3);
    assert(material_chunk_scale(chunk) < 1 && material_chunk_scale(chunk) > 0);
    material_debris_free(debris);
}

void test_materials()
{
    blast_policy();
    rotated_occlusion();
    fragment_penetration();
    fragment_material_frame();
    debris();
    swept_ignition();
    reactions();
    resistance_and_chips();
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
