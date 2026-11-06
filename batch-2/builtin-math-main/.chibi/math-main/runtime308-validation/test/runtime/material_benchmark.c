#include "runtime/material_fluid.h"
#include "runtime/task_pool.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Fixed material fluid workloads, timed per frame on four workers. Run them
 * through benchmark_trials to compare builds. Each also checks the result
 * that makes its timing comparable. */
enum
{
    BENCHMARK_WORKERS = 4,
    PHASE_COUNT = 9
};
static const char *phase_names[PHASE_COUNT] = {
    "tick",       "reactions",  "gas_flow",        "index", "density",
    "relaxation", "collisions", "collision_index", "mpm"
};

static double clock_ms()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000.0 + now.tv_nsec * 1e-6;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static uint64_t hash_float(uint64_t hash, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (hash ^ bits) * UINT64_C(1099511628211);
}

/* Explicit fields avoid padding and include the persistent APIC state. */
static uint64_t state_hash(const material_fluid *fluid)
{
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = (hash ^ (uint64_t)count) * UINT64_C(1099511628211);
    for (int i = 0; i < count; i++) {
        hash = hash_float(hash, p[i].position.x);
        hash = hash_float(hash, p[i].position.y);
        hash = hash_float(hash, p[i].velocity.x);
        hash = hash_float(hash, p[i].velocity.y);
        hash = hash_float(hash, p[i].mass);
        hash = hash_float(hash, p[i].heat);
        hash = hash_float(hash, p[i].deformation.volume);
        for (int j = 0; j < 4; j++) {
            hash = hash_float(hash, p[i].deformation.f[j]);
            hash = hash_float(hash, p[i].deformation.affine[j]);
        }
    }
    return hash;
}

/* A liquid patch on a unit lattice, at rest or falling onto a floor. */
static material_fluid *make_patch(int solver, int side, bool collision)
{
    material_fluid *fluid = material_fluid_new(3, side * side);
    material_fluid_set_solver(fluid, solver, (fluid_mpm_settings){ .cell_size = 2, .max_substeps = 32 });
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            fluid_particle p = {
                .position = { x - (side - 1) * .5f, y - (side - 1) * .5f },
                .velocity = collision ? (fluid_vec){ 3, -12 } : (fluid_vec){ 0 },
                .kind = FLUID_LIQUID,
                .mass = .001f,
                .radius = .45f,
                .heat = .25f,
                .color = 0x4080ffff,
                .constitutive = { .density = .001f, .bulk_modulus = 2 }
            };
            assert(material_fluid_add(fluid, p));
        }
    }
    return fluid;
}

static void report_phases(double *samples, int frames)
{
    for (int phase = 0; phase < PHASE_COUNT; phase++) {
        double *values = samples + phase * frames, sum = 0;
        for (int i = 0; i < frames; i++) {
            sum += values[i];
        }
        qsort(values, frames, sizeof(*values), compare_double);
        printf(
            "  phase=%-15s mean_ms=%.6f p50_ms=%.6f p95_ms=%.6f\n",
            phase_names[phase],
            sum / frames,
            values[(int)ceil(.50 * frames) - 1],
            values[(int)ceil(.95 * frames) - 1]
        );
    }
}

/* Steps a patch for a fixed interval and returns its state hash, printing
 * per-phase timings when measured. */
static bool run_patch(int solver, int side, int frames, int workers, bool collision, bool measured, uint64_t *hash)
{
    material_fluid *fluid = make_patch(solver, side, collision);
    task_pool *pool = task_pool_acquire_workers(workers);
    material_fluid_set_tasks(fluid, task_pool_tasks(pool));
    /* Keep the entire moving patch active, independent of its spreading. */
    fluid_region region = { -1000000, -1000000, 2000000, 2000000 };
    fluid_body body = { .position = { 0, -side * .5f - 4 } };
    fluid_obstacle floor = { .body = 0, .half_size = { side * 4.f, 1 } };
    double *samples = xmalloc(sizeof(double) * PHASE_COUNT * frames);
    for (int frame = 0; frame < frames; frame++) {
        material_fluid_profile profile = { 0 };
        double start = clock_ms();
        task_pool_begin(pool, true);
        material_fluid_tick_profiled(
            fluid,
            1.f / 60,
            64,
            &region,
            1,
            collision ? &body : NULL,
            collision ? 1 : 0,
            collision ? &floor : NULL,
            collision ? 1 : 0,
            &profile
        );
        task_pool_end(pool);
        double values[PHASE_COUNT] = {
            clock_ms() - start,    profile.reactions_ms,
            profile.gas_flow_ms,   profile.index_ms,
            profile.density_ms,    profile.relaxation_ms,
            profile.collisions_ms, profile.collision_index_ms,
            profile.mpm_ms
        };
        for (int phase = 0; phase < PHASE_COUNT; phase++) {
            samples[phase * frames + frame] = values[phase];
        }
    }
    if (measured) {
        printf(
            "material benchmark solver=%s preset=%s side=%d frames=%d "
            "workers=%d\n",
            solver == FLUID_SOLVER_MPM ? "mpm" : "ddr",
            collision ? "collision" : "patch",
            side,
            frames,
            task_pool_tasks(pool)->workers
        );
        report_phases(samples, frames);
    }
    int count;
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    REQUIRE(count == side * side);
    for (int i = 0; i < count; i++) {
        REQUIRE(
            isfinite(particles[i].position.x) && isfinite(particles[i].position.y)
        );
        REQUIRE(
            isfinite(particles[i].velocity.x) && isfinite(particles[i].velocity.y)
        );
        REQUIRE(isfinite(particles[i].deformation.volume));
        REQUIRE(particles[i].mass == .001f);
    }
    *hash = state_hash(fluid);
    free(samples);
    material_fluid_free(fluid);
    task_pool_release(pool);
    return true;
}

/* Worker results must equal the serial result, or the timings compare
 * different work. */
static bool patch_parity(int solver, bool collision)
{
    enum
    {
        SIDE = 32,
        FRAMES = 30
    };
    uint64_t parallel, serial;
    REQUIRE(run_patch(solver, SIDE, FRAMES, BENCHMARK_WORKERS, collision, true, &parallel));
    REQUIRE(run_patch(solver, SIDE, FRAMES, 1, collision, false, &serial));
    printf("  state_hash=%016" PRIx64 " serial_hash=%016" PRIx64 "\n", parallel, serial);
    REQUIRE(parallel == serial);
    return true;
}

TEST_CASE(material_benchmark_ddr_patch)
{
    return patch_parity(FLUID_SOLVER_DDR, false);
}

TEST_CASE(material_benchmark_mpm_collision)
{
    return patch_parity(FLUID_SOLVER_MPM, true);
}

/* Hot and cold gas halves exchange heat with no reaction, so their thermal
 * energy stays the same while the reaction phase is timed. */
TEST_CASE(material_benchmark_thermal_exchange)
{
    enum
    {
        SIDE = 48,
        FRAMES = 30,
        WARMUP = 5
    };
    material_fluid *fluid = material_fluid_new(10, SIDE * SIDE);
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            bool hot = x < SIDE / 2;
            fluid_particle p = { .position = { x * 4.f, y * 4.f },
                                 .kind = FLUID_GAS,
                                 .mass = 1,
                                 .radius = 1,
                                 .color = 0x4080ffff,
                                 .heat = hot ? 2 : 0,
                                 .thermal = { hot ? 1 : 20, 30 } };
            REQUIRE(material_fluid_add(fluid, p));
        }
    }
    task_pool *pool = task_pool_acquire_workers(BENCHMARK_WORKERS);
    material_fluid_set_tasks(fluid, task_pool_tasks(pool));
    fluid_region region = { -100000, -100000, 200000, 200000 };
    double start = 0, reactions = 0;
    for (int frame = -WARMUP; frame < FRAMES; frame++) {
        if (!frame) {
            start = clock_ms();
        }
        material_fluid_profile profile = { 0 };
        task_pool_begin(pool, true);
        material_fluid_tick_profiled(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0, &profile);
        task_pool_end(pool);
        if (frame >= 0) {
            reactions += profile.reactions_ms;
        }
    }
    double tick = (clock_ms() - start) / FRAMES, energy = 0;
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    for (int i = 0; i < count; i++) {
        energy += (double)p[i].mass * p[i].thermal.capacity * p[i].heat;
    }
    printf(
        "material benchmark thermal side=%d frames=%d workers=%d "
        "reaction_ms=%.6f tick_ms=%.6f energy=%.6f\n",
        SIDE,
        FRAMES,
        task_pool_tasks(pool)->workers,
        reactions / FRAMES,
        tick,
        energy
    );
    /* Each hot parcel holds 1 * 1 * 2 and each cold one none. */
    REQUIRE(count == SIDE * SIDE && fabs(energy - SIDE * SIDE) < 1e-2 * SIDE * SIDE);
    material_fluid_free(fluid);
    task_pool_release(pool);
    return true;
}

/* Gas stirred by two moving bodies, timing the flow grid. */
TEST_CASE(material_benchmark_gas_flow)
{
    enum
    {
        SIDE = 64,
        FRAMES = 30,
        WARMUP = 5
    };
    material_fluid *fluid = material_fluid_new(10, SIDE * SIDE);
    material_fluid_set_flow(fluid, (fluid_flow_settings){ 10, 2, .1f });
    for (int y = 0; y < SIDE; y++) {
        for (int x = 0; x < SIDE; x++) {
            fluid_particle p = { .position = { (x - SIDE / 2) * 10 + 5,
                                               (y - SIDE / 2) * 10 + 5 },
                                 .velocity = { (x % 7) * .3f, (y % 5) * .4f },
                                 .kind = FLUID_GAS,
                                 .mass = 1,
                                 .radius = 1 };
            REQUIRE(material_fluid_add(fluid, p));
        }
    }
    task_pool *pool = task_pool_acquire_workers(BENCHMARK_WORKERS);
    material_fluid_set_tasks(fluid, task_pool_tasks(pool));
    fluid_region region = { -100000, -100000, 200000, 200000 };
    fluid_body bodies[] = { { .velocity = { 30, -15 }, .angle = .3f },
                            { .position = { -20, 30 }, .velocity = { -5, 12 } } };
    fluid_obstacle obstacles[] = { { .half_size = { 30, 15 } },
                                   { .body = 1, .radius = 25 } };
    double start = 0, gas = 0;
    for (int frame = -WARMUP; frame < FRAMES; frame++) {
        if (!frame) {
            start = clock_ms();
        }
        material_fluid_profile profile = { 0 };
        task_pool_begin(pool, true);
        material_fluid_tick_profiled(fluid, 1.f / 60, 64, &region, 1, bodies, 2, obstacles, 2, &profile);
        task_pool_end(pool);
        if (frame >= 0) {
            gas += profile.gas_flow_ms;
        }
    }
    printf(
        "material benchmark gas side=%d frames=%d workers=%d flow_ms=%.6f "
        "tick_ms=%.6f hash=%016" PRIx64 "\n",
        SIDE,
        FRAMES,
        task_pool_tasks(pool)->workers,
        gas / FRAMES,
        (clock_ms() - start) / FRAMES,
        material_fluid_flow_hash(fluid)
    );
    int count;
    material_fluid_particles(fluid, &count);
    REQUIRE(count == SIDE * SIDE && gas > 0);
    material_fluid_free(fluid);
    task_pool_release(pool);
    return true;
}

void test_material_benchmark()
{
    RUN_TEST_CASE(material_benchmark_ddr_patch);
    RUN_TEST_CASE(material_benchmark_mpm_collision);
    RUN_TEST_CASE(material_benchmark_thermal_exchange);
    RUN_TEST_CASE(material_benchmark_gas_flow);
}
