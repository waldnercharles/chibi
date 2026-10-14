#include "runtime/material_mpm.h"

#include "runtime/task_pool.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static fluid_mpm_settings settings()
{
    return (fluid_mpm_settings){ .cell_size = 2, .max_substeps = 64 };
}

static int block(fluid_particle *particles, int *active, int side, float shear)
{
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            int i = y * side + x;
            particles[i] = (fluid_particle){
                .position = { x - (side - 1) * .5f, y - (side - 1) * .5f },
                .mass = 1,
                .radius = .5f,
                .kind = FLUID_LIQUID,
                .constitutive = { 1, 20, shear },
                .deformation = { .f = { 1, 0, 0, 1 }, .volume = 1 }
            };
            active[i] = i;
        }
    }
    return side * side;
}

static double angular_momentum(const fluid_particle *p, int count, float cell_size)
{
    double result = 0;
    for (int i = 0; i < count; i++) {
        result += p[i].mass * ((double)p[i].position.x * p[i].velocity.y -
                               (double)p[i].position.y * p[i].velocity.x);
        /* APIC carries angular momentum in its affine state as well. */
        result += p[i].mass * (double)cell_size * cell_size * .25 *
                  (p[i].deformation.affine[2] - p[i].deformation.affine[1]);
    }
    return result;
}

TEST_CASE(rest_and_translation)
{
    enum
    {
        COUNT = 25
    };
    fluid_particle p[COUNT];
    int active[COUNT];
    block(p, active, 5, 0);
    material_mpm *mpm = material_mpm_new();
    REQUIRE(material_mpm_step(mpm, settings(), .001f, p, active, COUNT, NULL));
    REQUIRE(material_mpm_node_count(mpm) > 0);
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(p[i].velocity.x == 0 && p[i].velocity.y == 0);
        REQUIRE(p[i].deformation.volume == 1);
        p[i].velocity = (fluid_vec){ 1.25f, -.75f };
    }
    fluid_vec origin[COUNT];
    for (int i = 0; i < COUNT; i++) {
        origin[i] = p[i].position;
    }
    for (int step = 0; step < 50; step++) {
        REQUIRE(material_mpm_step(mpm, settings(), .001f, p, active, COUNT, NULL));
    }
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(fabsf(p[i].position.x - origin[i].x - .0625f) < .00002f);
        REQUIRE(fabsf(p[i].position.y - origin[i].y + .0375f) < .00002f);
        REQUIRE(fabsf(p[i].velocity.x - 1.25f) < .00001f);
        REQUIRE(fabsf(p[i].velocity.y + .75f) < .00001f);
        REQUIRE(fabsf(p[i].deformation.volume - 1) < .00001f);
    }
    material_mpm_clear(mpm);
    REQUIRE(material_mpm_node_count(mpm) == 0);
    material_mpm_free(mpm);
    return true;
}

TEST_CASE(rotation_and_negative_coordinates)
{
    enum
    {
        COUNT = 81
    };
    fluid_particle p[COUNT], shifted[COUNT];
    int active[COUNT];
    block(p, active, 9, 0);
    for (int i = 0; i < COUNT; i++) {
        p[i].velocity = (fluid_vec){ -.25f * p[i].position.y,
                                     .25f * p[i].position.x };
        p[i].deformation.affine[1] = -.25f;
        p[i].deformation.affine[2] = .25f;
        shifted[i] = p[i];
        shifted[i].position.x += 64;
        shifted[i].position.y += 64;
    }
    double before = angular_momentum(p, COUNT, 2);
    material_mpm *a = material_mpm_new(), *b = material_mpm_new();
    REQUIRE(material_mpm_step(a, settings(), .001f, p, active, COUNT, NULL));
    REQUIRE(material_mpm_step(b, settings(), .001f, shifted, active, COUNT, NULL));
    REQUIRE(fabs(angular_momentum(p, COUNT, 2) - before) < .0001);
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(
            p[i].velocity.x == shifted[i].velocity.x &&
            p[i].velocity.y == shifted[i].velocity.y
        );
        REQUIRE(!memcmp(&p[i].deformation, &shifted[i].deformation, sizeof p[i].deformation));
        REQUIRE(fabsf(p[i].position.x - (shifted[i].position.x - 64)) < .00001f);
        REQUIRE(fabsf(p[i].position.y - (shifted[i].position.y - 64)) < .00001f);
    }
    material_mpm_free(a);
    material_mpm_free(b);
    return true;
}

TEST_CASE(elastic_recovery_and_liquid_shear)
{
    enum
    {
        COUNT = 81
    };
    fluid_particle elastic[COUNT], liquid[COUNT];
    int active[COUNT];
    block(elastic, active, 9, 10);
    for (int i = 0; i < COUNT; i++) {
        elastic[i].position.x *= 1.1f;
        elastic[i].position.y /= 1.1f;
        elastic[i].deformation.f[0] = 1.1f;
        elastic[i].deformation.f[3] = 1 / 1.1f;
        elastic[i].deformation.volume = 1;
        liquid[i] = elastic[i];
        liquid[i].constitutive.shear_modulus = 0;
    }
    material_mpm *a = material_mpm_new(), *b = material_mpm_new();
    REQUIRE(material_mpm_step(a, settings(), .001f, elastic, active, COUNT, NULL));
    REQUIRE(material_mpm_step(b, settings(), .001f, liquid, active, COUNT, NULL));
    double motion = 0, stretch = 0;
    for (int i = 0; i < COUNT; i++) {
        motion += fabsf(elastic[i].velocity.x) + fabsf(elastic[i].velocity.y);
        stretch += elastic[i].deformation.f[0];
        REQUIRE(liquid[i].velocity.x == 0 && liquid[i].velocity.y == 0);
        REQUIRE(liquid[i].deformation.volume == 1);
    }
    REQUIRE(motion > 0 && stretch / COUNT < 1.1);
    for (int step = 0; step < 199; step++) {
        float dt = fminf(.001f, material_mpm_timestep(settings(), elastic, active, COUNT));
        REQUIRE(dt > 0 && material_mpm_step(a, settings(), dt, elastic, active, COUNT, NULL));
    }
    stretch = 0;
    for (int i = 0; i < COUNT; i++) {
        stretch += elastic[i].deformation.f[0];
        REQUIRE(
            isfinite(elastic[i].deformation.volume) && elastic[i].deformation.volume > 0
        );
    }
    REQUIRE(stretch / COUNT < 1.1 && stretch / COUNT > .9);
    material_mpm_free(a);
    material_mpm_free(b);
    return true;
}

TEST_CASE(transactional_failure_and_timestep)
{
    fluid_particle p[1];
    int active[1];
    block(p, active, 1, 0);
    float rest = material_mpm_timestep(settings(), p, active, 1);
    p[0].deformation.volume = .25f;
    float compressed = material_mpm_timestep(settings(), p, active, 1);
    REQUIRE(compressed > 0 && compressed < rest * .13f);
    block(p, active, 1, 10);
    float soft = material_mpm_timestep(settings(), p, active, 1);
    p[0].constitutive.bulk_modulus *= 100;
    float stiff = material_mpm_timestep(settings(), p, active, 1);
    REQUIRE(stiff > 0 && stiff < soft);
    p[0].velocity.x = 100;
    REQUIRE(material_mpm_timestep(settings(), p, active, 1) < stiff);
    p[0].deformation.f[3] = -1;
    fluid_particle before = p[0];
    material_mpm *mpm = material_mpm_new();
    REQUIRE(material_mpm_timestep(settings(), p, active, 1) == 0);
    REQUIRE(!material_mpm_step(mpm, settings(), .001f, p, active, 1, NULL));
    REQUIRE(!memcmp(p, &before, sizeof before));
    p[0].kind = FLUID_GAS;
    before = p[0];
    REQUIRE(material_mpm_step(mpm, settings(), .001f, p, active, 1, NULL));
    REQUIRE(!memcmp(p, &before, sizeof before) && material_mpm_node_count(mpm) == 0);
    /* One valid candidate must not commit when a different candidate inverts
     * during G2P. A smaller retry starts from the exact original state. */
    fluid_particle pair[2];
    int pair_active[] = { 0, 1 };
    block(pair, pair_active, 1, 10);
    pair[0].velocity.x = 5;
    pair[1] = pair[0];
    pair[1].position.x = 100;
    pair[1].deformation.affine[0] = 100;
    pair[1].deformation.affine[3] = -100;
    fluid_particle pair_before[2];
    memcpy(pair_before, pair, sizeof pair);
    REQUIRE(!material_mpm_step(mpm, settings(), .02f, pair, pair_active, 2, NULL));
    REQUIRE(!memcmp(pair, pair_before, sizeof pair));
    REQUIRE(material_mpm_step(mpm, settings(), .00001f, pair, pair_active, 2, NULL));
    REQUIRE(
        pair[0].position.x > pair_before[0].position.x && pair[1].deformation.volume > 0
    );
    material_mpm_free(mpm);
    return true;
}

TEST_CASE(ordered_worker_parity)
{
    enum
    {
        SIDE = 96,
        COUNT = SIDE * SIDE
    };
    fluid_particle *serial = xcalloc(COUNT, sizeof *serial);
    fluid_particle *parallel = xcalloc(COUNT, sizeof *parallel);
    int *active = xmalloc(COUNT * sizeof *active);
    block(serial, active, SIDE, 5);
    for (int i = 0; i < COUNT; i++) {
        serial[i].velocity = (fluid_vec){ .05f * serial[i].position.y,
                                          -.03f * serial[i].position.x };
        serial[i].deformation.affine[1] = .05f;
        serial[i].deformation.affine[2] = -.03f;
    }
    memcpy(parallel, serial, COUNT * sizeof *serial);
    task_pool *pool = task_pool_acquire_workers(4);
    material_mpm *a = material_mpm_new(), *b = material_mpm_new();
    for (int step = 0; step < 8; step++) {
        REQUIRE(material_mpm_step(a, settings(), .001f, serial, active, COUNT, NULL));
        task_pool_begin(pool, true);
        REQUIRE(
            material_mpm_step(b, settings(), .001f, parallel, active, COUNT, task_pool_tasks(pool))
        );
        task_pool_end(pool);
    }
    REQUIRE(material_mpm_node_count(a) >= 2048);
    REQUIRE(task_pool_tasks(pool)->workers <= 1 || task_pool_started(pool));
    REQUIRE(!memcmp(serial, parallel, COUNT * sizeof *serial));
    fluid_body body_a = { 0 }, body_b = { 0 };
    fluid_obstacle floor = { .center = { 0, -50 }, .half_size = { 100, 20 } };
    fluid_mpm_colliders contacts_a = { &body_a, 1, &floor, 1, 1 };
    fluid_mpm_colliders contacts_b = { &body_b, 1, &floor, 1, 1 };
    REQUIRE(
        material_mpm_step_colliders(a, settings(), .001f, serial, active, COUNT, NULL, contacts_a)
    );
    task_pool_begin(pool, true);
    REQUIRE(
        material_mpm_step_colliders(b, settings(), .001f, parallel, active, COUNT, task_pool_tasks(pool), contacts_b)
    );
    task_pool_end(pool);
    REQUIRE(body_a.impulse.y < 0);
    REQUIRE(!memcmp(serial, parallel, COUNT * sizeof *serial));
    REQUIRE(!memcmp(&body_a, &body_b, sizeof body_a));
    material_mpm_free(a);
    material_mpm_free(b);
    task_pool_release(pool);
    free(serial);
    free(parallel);
    free(active);
    return true;
}

typedef struct
{
    const task_system *executor;
    int dense_deposits;
} dispatch_record;

static void *record_enqueue(task_range *fn, int count, int grain, void *context, void *user)
{
    dispatch_record *record = user;
    if (count < 2048) {
        assert(count > 1 && grain < count);
        record->dense_deposits++;
    }
    return record->executor->enqueue(fn, count, grain, context, record->executor->user);
}

static void record_finish(void *task, void *user)
{
    dispatch_record *record = user;
    record->executor->finish(task, record->executor->user);
}

TEST_CASE(dense_worker_parity)
{
    enum
    {
        SIDE = 48,
        COUNT = SIDE * SIDE
    };
    fluid_particle *serial = xcalloc(COUNT, sizeof *serial);
    fluid_particle *parallel = xcalloc(COUNT, sizeof *parallel);
    int *active = xmalloc(COUNT * sizeof *active);
    block(serial, active, SIDE, 0);
    for (int i = 0; i < COUNT; i++) {
        serial[i].position.x *= .25f;
        serial[i].position.y *= .25f;
        serial[i].mass = .0625f;
        serial[i].velocity = (fluid_vec){ .05f * serial[i].position.y,
                                          -.03f * serial[i].position.x };
        serial[i].deformation.affine[1] = .05f;
        serial[i].deformation.affine[2] = -.03f;
    }
    memcpy(parallel, serial, COUNT * sizeof *serial);
    task_pool *pool = task_pool_acquire_workers(4);
    dispatch_record record = { .executor = task_pool_tasks(pool) };
    task_system tasks = { .workers = record.executor->workers,
                          .user = &record,
                          .enqueue = record_enqueue,
                          .finish = record_finish };
    fluid_mpm_settings config = { .cell_size = 1, .max_substeps = 64 };
    material_mpm *a = material_mpm_new(), *b = material_mpm_new();
    fluid_body body_a = { 0 }, body_b = { 0 };
    fluid_obstacle floor = { .center = { 0, -8 }, .half_size = { 10, 2 } };
    fluid_mpm_colliders contacts_a = { &body_a, 1, &floor, 1, 1 };
    fluid_mpm_colliders contacts_b = { &body_b, 1, &floor, 1, 1 };
    for (int i = 0; i < 8; i++) {
        REQUIRE(
            material_mpm_step_colliders(a, config, .001f, serial, active, COUNT, NULL, contacts_a)
        );
        task_pool_begin(pool, true);
        REQUIRE(
            material_mpm_step_colliders(b, config, .001f, parallel, active, COUNT, &tasks, contacts_b)
        );
        task_pool_end(pool);
        REQUIRE(material_mpm_node_count(b) > 0 && material_mpm_node_count(b) < 2048);
    }
    REQUIRE(tasks.workers <= 1 || (record.dense_deposits == 8 && task_pool_started(pool)));
    REQUIRE(body_a.impulse.y < 0);
    REQUIRE(!memcmp(serial, parallel, COUNT * sizeof *serial));
    REQUIRE(!memcmp(&body_a, &body_b, sizeof body_a));
    material_mpm_free(a);
    material_mpm_free(b);
    task_pool_release(pool);
    free(serial);
    free(parallel);
    free(active);
    return true;
}

TEST_CASE(collider_momentum_and_transaction)
{
    fluid_particle p[2];
    int active[2] = { 0, 1 };
    block(p, active, 1, 0);
    p[0].position = (fluid_vec){ .5f, .25f };
    p[0].velocity.y = -2;
    fluid_body body = { .inverse_mass = .1f, .inverse_inertia = .02f };
    fluid_obstacle floor = { .center = { 0, -1 }, .half_size = { 8, 1 } };
    fluid_mpm_colliders colliders = { &body, 1, &floor, 1, 1 };
    material_mpm *mpm = material_mpm_new();
    double before = angular_momentum(p, 1, settings().cell_size);
    REQUIRE(material_mpm_step_colliders(mpm, settings(), .001f, p, active, 1, NULL, colliders));
    REQUIRE(body.impulse.y < 0 && body.angular_impulse < 0);
    REQUIRE(fabs(p[0].velocity.y + 2 + body.impulse.y) < 1e-6);
    REQUIRE(
        fabs(angular_momentum(p, 1, settings().cell_size) + body.angular_impulse - before) < 1e-6
    );

    block(p, active, 1, 0);
    p[0].position = (fluid_vec){ .5f, .25f };
    p[0].velocity.y = -2;
    p[1] = p[0];
    p[1].position = (fluid_vec){ 100, 100 };
    p[1].constitutive.shear_modulus = 10;
    p[1].deformation.affine[0] = 100;
    p[1].deformation.affine[3] = -100;
    fluid_particle original[2];
    memcpy(original, p, sizeof p);
    fluid_body saved = body;
    REQUIRE(!material_mpm_step_colliders(mpm, settings(), .02f, p, active, 2, NULL, colliders));
    REQUIRE(!memcmp(original, p, sizeof p));
    REQUIRE(!memcmp(&saved, &body, sizeof body));
    material_mpm_free(mpm);
    return true;
}

static bool hydrostatic_pool(float impact)
{
    enum
    {
        WIDTH = 16,
        HEIGHT = 12,
        COUNT = WIDTH * HEIGHT
    };
    fluid_particle p[COUNT];
    int active[COUNT];
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            int i = y * WIDTH + x;
            p[i] = (fluid_particle){
                .position = { (x - (WIDTH - 1) * .5f) * .5f, .25f + y * .5f },
                .mass = .25f,
                .radius = .25f,
                .kind = FLUID_LIQUID,
                .constitutive = { 1, 2000, 0 },
                .deformation = { .f = { 1, 0, 0, 1 }, .volume = 1 }
            };
            p[i].velocity.y = -impact;
            active[i] = i;
        }
    }
    fluid_body body = { 0 };
    fluid_obstacle walls[4] = { { .center = { 0, -4 }, .half_size = { 12, 4 } },
                                { .center = { -5, 4 }, .half_size = { 1, 8 } },
                                { .center = { 5, 4 }, .half_size = { 1, 8 } },
                                { .center = { 0, 12 }, .half_size = { 12, 4 } } };
    fluid_mpm_colliders colliders = { &body, 1, walls, impact > 0 ? 4 : 3, 1 };
    fluid_mpm_settings config = { .cell_size = 1,
                                  .max_substeps = 64,
                                  .gravity = { 0, -10 } };
    material_mpm *mpm = material_mpm_new();
    for (int i = 0; i < 3000; i++) {
        float dt = fminf(.001f, material_mpm_timestep(config, p, active, COUNT));
        REQUIRE(dt > 0);
        REQUIRE(material_mpm_step_colliders(mpm, config, dt, p, active, COUNT, NULL, colliders));
    }
    double low = INFINITY, high = -INFINITY, volume = 0, closest = INFINITY;
    double minimum_volume = INFINITY;
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(isfinite(p[i].position.x) && isfinite(p[i].position.y));
        low = fmin(low, p[i].position.y);
        high = fmax(high, p[i].position.y);
        volume += p[i].deformation.volume;
        minimum_volume = fmin(minimum_volume, p[i].deformation.volume);
        for (int j = 0; j < i; j++) {
            closest = fmin(
                closest,
                hypot(
                    p[i].position.x - p[j].position.x,
                    p[i].position.y - p[j].position.y
                )
            );
        }
    }
    /* Forty-eight units of rest area in an eight-unit basin must retain a
     * resolved pool, rather than stack all parcels on the bottom boundary. */
    REQUIRE(low > -.5 && high - low > 3 && high < 8);
    REQUIRE(volume / COUNT > .7 && volume / COUNT < 1.1);
    REQUIRE(minimum_volume > .5);
    REQUIRE(closest > .08);
    material_mpm_free(mpm);
    return true;
}

TEST_CASE(hydrostatic_pool_at_rest)
{
    return hydrostatic_pool(0);
}

TEST_CASE(hydrostatic_pool_after_impact)
{
    return hydrostatic_pool(60);
}

void test_material_mpm()
{
    RUN_TEST_CASE(rest_and_translation);
    RUN_TEST_CASE(rotation_and_negative_coordinates);
    RUN_TEST_CASE(elastic_recovery_and_liquid_shear);
    RUN_TEST_CASE(transactional_failure_and_timestep);
    RUN_TEST_CASE(ordered_worker_parity);
    RUN_TEST_CASE(dense_worker_parity);
    RUN_TEST_CASE(collider_momentum_and_transaction);
    RUN_TEST_CASE(hydrostatic_pool_at_rest);
    RUN_TEST_CASE(hydrostatic_pool_after_impact);
}
