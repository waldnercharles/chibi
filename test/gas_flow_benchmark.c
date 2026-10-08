#include "graph/model/registry.h"
#include "runtime/material_fluid.h"
#include "runtime/task_pool.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double seconds()
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    registry_set_packs(REGISTRY_PACKS, REGISTRY_PACK_COUNT);
    assert(argc == 4);
    int side = atoi(argv[1]), frames = atoi(argv[2]), workers = atoi(argv[3]);
    assert(side >= 16 && side <= 256 && frames >= 10 && workers > 0);
    material_fluid *fluid = material_fluid_new(10, side * side);
    material_fluid_set_flow(fluid, (fluid_flow_settings){ 10, 2, .1f });
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            fluid_particle p = { .position = { (x - side / 2) * 10 + 5,
                                               (y - side / 2) * 10 + 5 },
                                 .velocity = { (x % 7) * .3f, (y % 5) * .4f },
                                 .kind = FLUID_GAS,
                                 .mass = 1,
                                 .radius = 1 };
            assert(material_fluid_add(fluid, p));
        }
    }
    game_task_pool *pool = game_task_pool_acquire_workers(workers);
    material_fluid_set_tasks(fluid, game_task_pool_tasks(pool));
    fluid_region region = { -100000, -100000, 200000, 200000 };
    fluid_body bodies[] = { { .velocity = { 30, -15 }, .angle = .3f },
                            { .position = { -20, 30 }, .velocity = { -5, 12 } } };
    fluid_obstacle obstacles[] = { { .half_size = { 30, 15 } },
                                   { .body = 1, .radius = 25 } };
    double start = 0, gas = 0;
    for (int frame = -5; frame < frames; frame++) {
        if (!frame) {
            start = seconds();
        }
        material_fluid_profile profile = { 0 };
        game_task_pool_begin(pool, true);
        material_fluid_tick_profiled(fluid, 1.f / 60, 64, &region, 1, bodies, 2, obstacles, 2, &profile);
        game_task_pool_end(pool);
        if (frame >= 0) {
            gas += profile.gas_flow_ms;
        }
    }
    double total = (seconds() - start) * 1000 / frames;
    printf(
        "gas side=%d frames=%d workers=%d flow_ms=%.6f tick_ms=%.6f "
        "hash=%llu\n",
        side,
        frames,
        game_task_pool_tasks(pool)->workers,
        gas / frames,
        total,
        (unsigned long long)material_fluid_flow_hash(fluid)
    );
    material_fluid_free(fluid);
    game_task_pool_release(pool);
}
