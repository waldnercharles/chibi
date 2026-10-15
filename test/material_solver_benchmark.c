#include "graph/model/registry.h"
#include "runtime/material_fluid.h"
#include "runtime/task_pool.h"
#include "support/xalloc.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
    PHASE_COUNT = 9
};
static const char *phase_names[PHASE_COUNT] = {
    "tick",       "reactions",  "gas_flow",        "index", "density",
    "relaxation", "collisions", "collision_index", "mpm"
};

static double clock_ms()
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
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

static void diagnostics(const material_fluid *fluid, int side, bool collision)
{
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    assert(count == side * side);
    double mass = 0, cx = 0, cy = 0, px = 0, py = 0;
    float min_x = p[0].position.x, max_x = min_x, min_y = p[0].position.y,
          max_y = min_y, min_volume = p[0].deformation.volume,
          max_volume = min_volume;
    for (int i = 0; i < count; i++) {
        assert(isfinite(p[i].position.x) && isfinite(p[i].position.y));
        assert(isfinite(p[i].velocity.x) && isfinite(p[i].velocity.y));
        assert(isfinite(p[i].deformation.volume));
        assert(p[i].mass == .001f);
        mass += p[i].mass;
        cx += (double)p[i].mass * p[i].position.x;
        cy += (double)p[i].mass * p[i].position.y;
        px += (double)p[i].mass * p[i].velocity.x;
        py += (double)p[i].mass * p[i].velocity.y;
        min_x = fminf(min_x, p[i].position.x);
        max_x = fmaxf(max_x, p[i].position.x);
        min_y = fminf(min_y, p[i].position.y);
        max_y = fmaxf(max_y, p[i].position.y);
        min_volume = fminf(min_volume, p[i].deformation.volume);
        max_volume = fmaxf(max_volume, p[i].deformation.volume);
    }
    double initial_mass = (double)(side * side) * .001f;
    printf(
        "state particles=%d active=%d mass=%.12g initial_mass=%.12g "
        "mass_delta=%.12g "
        "com_x=%.9g com_y=%.9g momentum_x=%.9g momentum_y=%.9g "
        "initial_momentum_x=%.9g initial_momentum_y=%.9g "
        "min_x=%.9g max_x=%.9g min_y=%.9g max_y=%.9g "
        "bounds_area=%.9g initial_bounds_area=%.9g volume_min=%.9g "
        "volume_max=%.9g\n",
        count,
        material_fluid_active_count(fluid),
        mass,
        initial_mass,
        mass - initial_mass,
        cx / mass,
        cy / mass,
        px,
        py,
        collision ? initial_mass * 3 : 0,
        collision ? initial_mass * -12 : 0,
        min_x,
        max_x,
        min_y,
        max_y,
        (double)(max_x - min_x) * (max_y - min_y),
        (double)(side - 1) * (side - 1),
        min_volume,
        max_volume
    );
}

static uint64_t run(int solver, int side, int frames, int workers, bool collision, bool measured)
{
    material_fluid *fluid = make_patch(solver, side, collision);
    task_pool *pool = task_pool_acquire_workers(workers);
    material_fluid_set_tasks(fluid, task_pool_tasks(pool));
    /* Keep the entire moving patch active, independent of its spreading. */
    fluid_region region = { -1000000, -1000000, 2000000, 2000000 };
    fluid_body body = { .position = { 0, -side * .5f - 4 } };
    fluid_obstacle floor = { .body = 0, .half_size = { side * 4.f, 1 } };
    double *samples = measured ? xmalloc(sizeof(double) * PHASE_COUNT * frames) : NULL;
    int max_substeps = 0, max_nodes = 0, limited_frames = 0;
    double substeps = 0, advanced_time = 0;
    /* No warmup: both runs include the same finite physical interval. */
    for (int frame = 0; frame < frames; frame++) {
        material_fluid_profile profile = { 0 };
        double start = measured ? clock_ms() : 0;
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
            measured ? &profile : NULL
        );
        task_pool_end(pool);
        if (measured) {
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
        fluid_mpm_state state = material_fluid_mpm_state(fluid);
        max_substeps = state.substeps > max_substeps ? state.substeps : max_substeps;
        max_nodes = state.nodes > max_nodes ? state.nodes : max_nodes;
        limited_frames += state.limited;
        substeps += state.substeps;
        advanced_time += state.advanced_time;
    }
    if (measured) {
        printf(
            "solver=%s preset=%s side=%d frames=%d requested_workers=%d "
            "workers=%d "
            "dt=%.9g spacing=1 kernel=3 mass_per_particle=0.001 density=0.001 "
            "bulk_modulus=2 shear_modulus=0 cell_size=2 max_substeps=32 "
            "gravity=0 warmup=0\n",
            solver == FLUID_SOLVER_MPM ? "mpm" : "ddr",
            collision ? "collision" : "patch",
            side,
            frames,
            workers,
            task_pool_tasks(pool)->workers,
            1.f / 60
        );
        for (int phase = 0; phase < PHASE_COUNT; phase++) {
            double *values = samples + phase * frames, sum = 0;
            for (int i = 0; i < frames; i++) {
                sum += values[i];
            }
            qsort(values, frames, sizeof(*values), compare_double);
            printf(
                "phase=%s mean_ms=%.6f p50_ms=%.6f p95_ms=%.6f p99_ms=%.6f\n",
                phase_names[phase],
                sum / frames,
                values[(int)ceil(.50 * frames) - 1],
                values[(int)ceil(.95 * frames) - 1],
                values[(int)ceil(.99 * frames) - 1]
            );
        }
        diagnostics(fluid, side, collision);
        printf(
            "mpm mean_substeps=%.6f max_substeps=%d limited_frames=%d "
            "max_nodes=%d advanced_time=%.9g requested_time=%.9g\n",
            substeps / frames,
            max_substeps,
            limited_frames,
            max_nodes,
            advanced_time,
            frames * (double)(1.f / 60)
        );
    }
    uint64_t hash = state_hash(fluid);
    free(samples);
    material_fluid_free(fluid);
    task_pool_release(pool);
    return hash;
}

int main(int argc, char **argv)
{
    registry_set_packs(REGISTRY_PACKS, REGISTRY_PACK_COUNT);
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "usage: %s ddr|mpm side frames workers [patch|collision]\n", argv[0]);
        return 2;
    }
    int solver = !strcmp(argv[1], "mpm") ? FLUID_SOLVER_MPM : FLUID_SOLVER_DDR;
    int side = atoi(argv[2]), frames = atoi(argv[3]), workers = atoi(argv[4]);
    bool collision = argc == 6 && !strcmp(argv[5], "collision");
    assert(!strcmp(argv[1], "ddr") || !strcmp(argv[1], "mpm"));
    assert(argc == 5 || collision || !strcmp(argv[5], "patch"));
    assert(side >= 4 && side <= 256 && frames >= 1 && frames <= 100000 && workers > 0);
    uint64_t hash = run(solver, side, frames, workers, collision, true);
    uint64_t serial = workers == 1 ? hash
                                   : run(solver, side, frames, 1, collision, false);
    printf("state_hash=%016" PRIx64 " serial_hash=%016" PRIx64 " worker_equal=%d\n", hash, serial, hash == serial);
    return hash != serial;
}
