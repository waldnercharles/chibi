#include "runtime/component_catalog.h"
#include "runtime/material_flow.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/runtime/material_fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST_CASE(sleeping)
{
    material_fluid *fluid = material_fluid_new(10, 2);
    fluid_particle p = material_test_particle(-30, 20, FLUID_GAS);
    p.velocity.x = 12;
    p.lifetime = .05f;
    REQUIRE(material_fluid_add(fluid, p));
    REQUIRE(material_fluid_add(fluid, material_test_particle(500, 0, FLUID_GAS)));
    REQUIRE(!material_fluid_add(fluid, p));
    for (int i = 0; i < 100; i++) {
        material_fluid_tick(fluid, 1.f / 60, 64, NULL, 0, NULL, 0, NULL, 0);
    }
    int count;
    const fluid_particle *values = material_fluid_particles(fluid, &count);
    REQUIRE(count == 2 && !memcmp(&values[0], &p, sizeof p));
    REQUIRE(!material_fluid_active_count(fluid));
    /* A region elsewhere in the same negative-coordinate chunk wakes it. */
    fluid_region region = { -60, 5, 2, 2 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
    REQUIRE(material_fluid_active_count(fluid) == 1);
    REQUIRE(fabsf(values[0].position.x - (p.position.x + .2f)) < .001f);
    REQUIRE(values[1].position.x == 500);
    for (int i = 0; i < 4; i++) {
        material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
    }
    values = material_fluid_particles(fluid, &count);
    REQUIRE(count == 1 && values[0].position.x == 500);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(cohesion)
{
    material_fluid *fluid = material_fluid_new(10, 1000);
    for (int y = -8; y <= 8; y++) {
        for (int x = -8; x <= 8; x++) {
            if (x * x + y * y <= 64) {
                REQUIRE(
                    material_fluid_add(fluid, material_test_particle(x * 4, y * 4, FLUID_LIQUID))
                );
            }
        }
    }
    for (int step = 0; step < 600; step++) {
        material_test_tick(fluid);
    }
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    fluid_vec center = { 0 }, momentum = { 0 };
    float radius = 0;
    for (int i = 0; i < count; i++) {
        REQUIRE(isfinite(p[i].position.x) && isfinite(p[i].position.y));
        center.x += p[i].position.x;
        center.y += p[i].position.y;
        momentum.x += p[i].mass * p[i].velocity.x;
        momentum.y += p[i].mass * p[i].velocity.y;
        radius = fmaxf(radius, hypotf(p[i].position.x, p[i].position.y));
    }
    REQUIRE(count == 197);
    REQUIRE(hypotf(center.x, center.y) / count < .05f);
    REQUIRE(hypotf(momentum.x, momentum.y) < .02f);
    REQUIRE(radius > 15 && radius < 40);
    material_fluid_free(fluid);
    return true;
}

static int liquid_clusters(const material_fluid *fluid)
{
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    bool *seen = xcalloc((size_t)count, sizeof *seen);
    int *queue = xmalloc((size_t)count * sizeof *queue);
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

TEST_CASE(liquid_split_merge)
{
    material_fluid *fluid = material_fluid_new(10, 1000);
    for (int side = -1; side <= 1; side += 2) {
        for (int y = -4; y <= 4; y++) {
            for (int x = -4; x <= 4; x++) {
                if (x * x + y * y > 16) {
                    continue;
                }
                fluid_particle p = material_test_particle(x * 4 + side * 26, y * 4, FLUID_LIQUID);
                p.velocity.x = -side * 15;
                REQUIRE(material_fluid_add(fluid, p));
            }
        }
    }
    REQUIRE(liquid_clusters(fluid) == 2);
    for (int i = 0; i < 180; i++) {
        material_test_tick(fluid);
    }
    REQUIRE(liquid_clusters(fluid) == 1);
    material_fluid_clear(fluid);
    for (int y = -6; y <= 6; y++) {
        for (int x = -6; x <= 6; x++) {
            if (x * x + y * y > 36) {
                continue;
            }
            fluid_particle p = material_test_particle(x * 4, y * 4, FLUID_LIQUID);
            p.velocity.x = x < 0 ? -80 : 80;
            REQUIRE(material_fluid_add(fluid, p));
        }
    }
    REQUIRE(liquid_clusters(fluid) == 1);
    for (int i = 0; i < 90; i++) {
        material_test_tick(fluid);
    }
    REQUIRE(liquid_clusters(fluid) >= 2);
    int count;
    material_fluid_particles(fluid, &count);
    REQUIRE(count == 113);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(body_response)
{
    material_fluid *fluid = material_fluid_new(10, 2);
    fluid_particle p = material_test_particle(9, 0, FLUID_GAS);
    p.velocity.x = -10;
    material_fluid_add(fluid, p);
    fluid_body body = { .inverse_mass = 2 };
    fluid_obstacle obstacle = { .radius = 10 };
    fluid_region region = { -100, -100, 200, 200 };
    material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &obstacle, 1);
    int count;
    const fluid_particle *out = material_fluid_particles(fluid, &count);
    REQUIRE(out[0].position.x >= 10 && body.impulse.x < 0);
    REQUIRE(
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
    REQUIRE(out[0].velocity.x > 0 && body.impulse.x < 0);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(gas_flow)
{
    /* Independent noncombat consumer: a probe stirs initially still gas. */
    material_fluid *fluid = material_fluid_new(10, 400);
    material_fluid_set_flow(fluid, (fluid_flow_settings){ 10, 2, .1f });
    for (int y = -8; y <= 8; y++) {
        for (int x = -8; x <= 8; x++) {
            material_fluid_add(fluid, material_test_particle(x * 10 + 5, y * 10 + 5, FLUID_GAS));
        }
    }
    for (int step = 0; step < 5; step++) {
        material_test_tick(fluid);
    }
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    for (int i = 0; i < count; i++) {
        REQUIRE(p[i].velocity.x == 0 && p[i].velocity.y == 0);
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
    REQUIRE(lateral > .1f);
    for (int step = 0; step < 120; step++) {
        material_test_tick(fluid);
    }
    float motion = 0;
    for (int i = 0; i < count; i++) {
        REQUIRE(isfinite(p[i].position.x) && isfinite(p[i].position.y));
        float speed = hypotf(p[i].velocity.x, p[i].velocity.y);
        REQUIRE(speed < 120);
        motion += speed;
    }
    printf("gas wake after two seconds: summed speed %.3f\n", motion);
    REQUIRE(motion > 1);
    fluid_particle saved = p[100];
    for (int step = 0; step < 30; step++) {
        material_fluid_tick(fluid, 1.f / 60, 64, NULL, 0, NULL, 0, NULL, 0);
    }
    REQUIRE(!memcmp(&saved, p + 100, sizeof saved));
    material_test_tick(fluid);
    REQUIRE(isfinite(p[100].velocity.x));
    material_fluid_clear(fluid);
    fluid_particle jet = material_test_particle(0, 0, FLUID_GAS);
    jet.velocity = (fluid_vec){ 50, 20 };
    jet.lifetime = jet.style.duration = 2;
    material_fluid_add(fluid, jet);
    material_test_tick(fluid);
    p = material_fluid_particles(fluid, &count);
    REQUIRE(fabsf(p[0].velocity.x - 50) < .001f && fabsf(p[0].velocity.y - 20) < .001f);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(gas_buoyancy)
{
    enum
    {
        SIDE = 15,
        COUNT = SIDE * SIDE
    };
    fluid_particle parcels[COUNT];
    int active[COUNT];
    task_system tasks = { .workers = 1 };
    for (int mode = 0; mode < 5; mode++) {
        material_flow *flow = material_flow_new();
        fluid_flow_settings settings = { .cell_size = 8,
                                         .gravity = { 0, -60 },
                                         .buoyancy = 1,
                                         .ambient_heat = 1 };
        if (mode == 2) {
            settings.gravity = (fluid_vec){ 0 };
        }
        if (mode == 3) {
            settings.buoyancy = 0;
        }
        for (int i = 0; i < COUNT; i++) {
            parcels[i] = material_test_particle(
                (i % SIDE - SIDE / 2) * 8 + 4,
                (i / SIDE - SIDE / 2) * 8 + 4,
                FLUID_GAS
            );
            parcels[i].heat = mode == 1 ? 0 : mode == 4 ? 1 : 2;
            parcels[i].mass = 1 + i % 3;
            parcels[i].color = i & 1 ? 0xff0000ff : 0xffff0000;
            parcels[i].fuel = .25f;
            active[i] = i;
        }
        fluid_particle before[COUNT];
        memcpy(before, parcels, sizeof before);
        material_flow_step(flow, settings, 1.f / 60, parcels, active, COUNT, NULL, NULL, 0, &tasks);
        float vertical = 0;
        for (int i = 0; i < COUNT; i++) {
            vertical += parcels[i].velocity.y;
            fluid_vec velocity = parcels[i].velocity;
            parcels[i].velocity = before[i].velocity;
            /* Grid transport cannot alter composition or heat energy. */
            REQUIRE(!memcmp(parcels + i, before + i, sizeof *parcels));
            parcels[i].velocity = velocity;
            if (mode >= 2) {
                REQUIRE(velocity.x == 0 && velocity.y == 0);
            }
        }
        if (mode == 0) {
            REQUIRE(vertical > 1);
        }
        if (mode == 1) {
            REQUIRE(vertical < -1);
        }
        material_flow_free(flow);
    }
    return true;
}

TEST_CASE(gas_mixing_barrier)
{
    material_fluid *fluid = material_fluid_new(4, 64);
    material_fluid_set_flow(fluid, (fluid_flow_settings){ .cell_size = 4, .vorticity = 1 });
    fluid_body body = { 0 };
    fluid_obstacle wall = { .half_size = { 2, 100 } };
    fluid_region region = { -128, -128, 256, 256 };
    for (int i = 0; i < 64; i++) {
        fluid_particle p = material_test_particle(-8 - (i % 8) * 4, (i / 8 - 4) * 4, FLUID_GAS);
        p.velocity = (fluid_vec){ 30, (i & 1 ? 1 : -1) * 10 };
        p.color = i & 1 ? 0xff0000ff : 0xffff0000;
        p.heat = i & 1;
        REQUIRE(material_fluid_add(fluid, p));
    }
    float vertical = 0;
    for (int step = 0; step < 120; step++) {
        material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, &wall, 1);
        int count;
        const fluid_particle *p = material_fluid_particles(fluid, &count);
        REQUIRE(count == 64);
        for (int i = 0; i < count; i++) {
            REQUIRE(p[i].position.x <= -2);
            REQUIRE(p[i].color == (i & 1 ? 0xff0000ff : 0xffff0000));
            REQUIRE(p[i].heat == (i & 1));
            REQUIRE(isfinite(p[i].velocity.x) && isfinite(p[i].velocity.y));
            vertical = fmaxf(vertical, fabsf(p[i].position.y - (i / 8 - 4) * 4));
        }
    }
    REQUIRE(vertical > 1);
    material_fluid_free(fluid);
    return true;
}

static bool parallel_parity(bool burning)
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
            fluid_particle p = material_test_particle(x * 4, y * 4, FLUID_LIQUID);
            if (burning) {
                p.kind = FLUID_GAS;
                p.fuel = 1;
                p.heat = x == 0 ? 2 : 0;
            }
            material_fluid_add(serial, p);
            material_fluid_add(parallel, p);
        }
    }
    task_pool *pool = task_pool_acquire_workers(4);
    material_fluid_set_tasks(parallel, task_pool_tasks(pool));
    for (int i = 0; i < 10; i++) {
        material_test_tick(serial);
        task_pool_begin(pool, true);
        material_test_tick(parallel);
        task_pool_end(pool);
        int na, nb;
        const fluid_blast *ba = material_fluid_blasts(serial, &na);
        const fluid_blast *bb = material_fluid_blasts(parallel, &nb);
        REQUIRE(na == nb && (!na || !memcmp(ba, bb, (size_t)na * sizeof *ba)));
    }
    int count, other_count;
    const fluid_particle *a = material_fluid_particles(serial, &count);
    const fluid_particle *b = material_fluid_particles(parallel, &other_count);
    REQUIRE(count == other_count && !memcmp(a, b, (size_t)count * sizeof *a));
    REQUIRE(material_fluid_flow_hash(serial) == material_fluid_flow_hash(parallel));
    REQUIRE(!burning || (a[0].burning && a[1].burning && !a[49].burning));
    material_fluid_free(serial);
    material_fluid_free(parallel);
    task_pool_release(pool);
    return true;
}

typedef struct
{
    task_system tasks;
    int submissions;
} flow_executor;

static void *flow_submit(task_range *run, int count, int minimum, void *context, void *user)
{
    flow_executor *executor = user;
    executor->submissions++;
    return executor->tasks.enqueue(run, count, minimum, context, executor->tasks.user);
}

static void flow_finish(void *task, void *user)
{
    flow_executor *executor = user;
    executor->tasks.finish(task, executor->tasks.user);
}

TEST_CASE(gas_deposit_equivalence)
{
    enum
    {
        POINTS = 64
    };
    fluid_particle split[POINTS * 3], merged[POINTS];
    int split_active[POINTS * 3], merged_active[POINTS];
    /* Cell size eight makes these offsets exactly representable just below,
     * on and above integer/half-cell boundaries. */
    const float offsets[] = { -.5f, 0, .5f, 3.5f, 4, 4.5f, 2, 6 };
    task_system tasks = { .workers = 1 };
    fluid_flow_settings settings = { .cell_size = 8 };
    material_flow *retained = material_flow_new(), *reference = material_flow_new();
    for (int round = 0; round < 2; round++) {
        if (round) {
            material_flow_clear(retained);
            material_flow_free(reference);
            reference = material_flow_new();
        }
        /* Prime both owned grids before clearing. Reset the dyadic deposition
         * inputs each step; previous flow fields still evolve identically. */
        for (int step = 0; step < (round ? 1 : 2); step++) {
            for (int i = 0; i < POINTS; i++) {
                int index = (i * 17) % POINTS;
                float x = (i % 8 - 4) * 16 + offsets[i % 8] + round * 128;
                float y = (i / 8 - 4) * 16 + offsets[i / 8] - round * 192;
                split[index * 3 + 2] = material_test_particle(x, y, FLUID_GAS);
                split[index * 3 + 2].mass = 1;
                split[index * 3 + 2].velocity = (fluid_vec){ 2, -4 };
                split[index * 3] = material_test_particle(x, y, FLUID_GAS);
                split[index * 3].mass = 1;
                split[index * 3].velocity = (fluid_vec){ 6, 8 };
                split[index * 3 + 1] = material_test_particle(10000 + x, -10000 + y, FLUID_LIQUID);
                split[index * 3 + 1].velocity = (fluid_vec){ 101, -103 };
                merged[index] = material_test_particle(x, y, FLUID_GAS);
                merged[index].mass = 2;
                merged[index].velocity = (fluid_vec){ 4, 2 };
                split_active[i * 3] = index * 3 + 1;
                split_active[i * 3 + 1] = index * 3 + 2;
                split_active[i * 3 + 2] = index * 3;
                merged_active[i] = index;
            }
            /* Coincident parcels have equal combined mass/momentum and activate
             * the same overlapping halos in the same first-insertion order. */
            material_flow_step(
                retained,
                settings,
                1.f / 60,
                split,
                split_active,
                POINTS * 3,
                NULL,
                NULL,
                0,
                &tasks
            );
            material_flow_step(reference, settings, 1.f / 60, merged, merged_active, POINTS, NULL, NULL, 0, &tasks);
            REQUIRE(material_flow_hash(retained) == material_flow_hash(reference));
            for (int i = 0; i < POINTS; i++) {
                REQUIRE(!memcmp(&split[i * 3].velocity, &merged[i].velocity, sizeof(fluid_vec)));
                REQUIRE(
                    !memcmp(&split[i * 3 + 2].velocity, &merged[i].velocity, sizeof(fluid_vec))
                );
                REQUIRE(
                    split[i * 3 + 1].velocity.x == 101 &&
                    split[i * 3 + 1].velocity.y == -103
                );
            }
        }
    }
    material_flow_free(retained);
    material_flow_free(reference);
    return true;
}

TEST_CASE(parallel_flow)
{
    enum
    {
        SIDE = 192,
        COUNT = SIDE * SIDE
    };
    fluid_particle *serial = xcalloc(COUNT, sizeof *serial);
    fluid_particle *parallel = xcalloc(COUNT, sizeof *parallel);
    int *active = xmalloc(COUNT * sizeof *active);
    for (int i = 0; i < COUNT; i++) {
        int x = i % SIDE - SIDE / 2, y = i / SIDE - SIDE / 2;
        serial[i] = material_test_particle(x * 10 + 5, y * 10 + 5, FLUID_GAS);
        serial[i].velocity = (fluid_vec){ (x % 7) * .3f, (y % 5) * .4f };
        serial[i].heat = (i % 11) * .2f;
        active[i] = i;
    }
    memcpy(parallel, serial, COUNT * sizeof *serial);
    material_flow *a = material_flow_new(), *b = material_flow_new();
    task_pool *pool = task_pool_acquire_workers(4);
    flow_executor executor = { .tasks = *task_pool_tasks(pool) };
    task_system tasks = { .workers = executor.tasks.workers,
                          .user = &executor,
                          .enqueue = flow_submit,
                          .finish = flow_finish };
    task_system inline_tasks = { .workers = 1 };
    fluid_flow_settings settings = { .cell_size = 10,
                                     .vorticity = 2,
                                     .drag = .1f,
                                     .gravity = { 0, -30 },
                                     .buoyancy = .5f,
                                     .ambient_heat = 1 };
    fluid_body bodies[] = { { .velocity = { 30, -15 }, .angle = .3f },
                            { .position = { -20, 30 }, .velocity = { -5, 12 } } };
    fluid_obstacle obstacles[] = { { .half_size = { 30, 15 } },
                                   { .body = 1, .radius = 25 } };
    for (int step = 0; step < 4; step++) {
        bodies[0].position.x += 5;
        int count = COUNT - step * SIDE;
        material_flow_step(a, settings, 1.f / 60, serial, active, count, bodies, obstacles, 2, &inline_tasks);
        task_pool_begin(pool, true);
        material_flow_step(b, settings, 1.f / 60, parallel, active, count, bodies, obstacles, 2, &tasks);
        task_pool_end(pool);
        REQUIRE(!memcmp(serial, parallel, COUNT * sizeof *serial));
        REQUIRE(material_flow_hash(a) == material_flow_hash(b));
    }
    if (tasks.workers > 1) {
        REQUIRE(executor.submissions >= 64);
    }
    material_flow_clear(a);
    material_flow_clear(b);
    int submissions = executor.submissions;
    material_flow_step(a, settings, 1.f / 60, serial, active, 0, NULL, NULL, 0, &inline_tasks);
    task_pool_begin(pool, true);
    material_flow_step(b, settings, 1.f / 60, parallel, active, 0, NULL, NULL, 0, &tasks);
    task_pool_end(pool);
    REQUIRE(executor.submissions == submissions);
    REQUIRE(material_flow_hash(a) == material_flow_hash(b));
    material_flow_free(a);
    material_flow_free(b);
    free(serial);
    free(parallel);
    free(active);
    task_pool_release(pool);
    return true;
}

TEST_CASE(fluid_profile_index_builds)
{
    material_fluid *fluid = material_fluid_new(10, 1);
    REQUIRE(material_fluid_add(fluid, material_test_particle(-20, -20, FLUID_LIQUID)));
    fluid_region region = { -64, -64, 128, 128 };
    material_fluid_profile profile;
    material_fluid_tick_profiled(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0, &profile);
    REQUIRE(profile.index_builds == 5);
    REQUIRE(isfinite(profile.collision_index_ms) && profile.collision_index_ms >= 0);
    REQUIRE(profile.collision_index_ms <= profile.collisions_ms);
    material_fluid_tick_profiled(fluid, 1.f / 60, 64, NULL, 0, NULL, 0, NULL, 0, &profile);
    REQUIRE(material_fluid_active_count(fluid) == 0 && profile.index_builds == 3);
    REQUIRE(profile.collision_index_ms == 0 && profile.collisions_ms >= 0);
    material_fluid_clear(fluid);
    material_fluid_tick_profiled(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0, &profile);
    REQUIRE(profile.index_builds == 3 && profile.collision_index_ms == 0);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(parallel_parity_liquid)
{
    return parallel_parity(false);
}

TEST_CASE(parallel_parity_burning)
{
    return parallel_parity(true);
}

void test_material_fluids()
{
    RUN_TEST_CASE(sleeping);
    RUN_TEST_CASE(cohesion);
    RUN_TEST_CASE(liquid_split_merge);
    RUN_TEST_CASE(body_response);
    RUN_TEST_CASE(gas_flow);
    RUN_TEST_CASE(gas_buoyancy);
    RUN_TEST_CASE(gas_mixing_barrier);
    RUN_TEST_CASE(gas_deposit_equivalence);
    RUN_TEST_CASE(parallel_flow);
    RUN_TEST_CASE(fluid_profile_index_builds);
    RUN_TEST_CASE(parallel_parity_liquid);
    RUN_TEST_CASE(parallel_parity_burning);
}
