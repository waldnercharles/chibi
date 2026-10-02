#include "runtime/material_fluid.h"
#include "runtime/task_pool.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double seconds()
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return now.tv_sec + now.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    const char *scenario = argv[1];
    int side = atoi(argv[2]), frames = atoi(argv[3]), workers = atoi(argv[4]);
    bool liquid = !strcmp(scenario, "cold-liquid");
    bool cold = !strcmp(scenario, "cold") || liquid,
         exchange = !strcmp(scenario, "exchange"),
         quench = !strcmp(scenario, "quench");
    assert(
        (cold || exchange || quench) && side >= 4 && side <= 256 &&
        frames >= 10 && workers > 0
    );
    material_fluid *fluid = material_fluid_new(10, side * side);
    fluid_reaction reaction = { .ignition = 1,
                                .burn_rate = .1f,
                                .heat_release = 2,
                                .smoke_lifetime = 3,
                                .flame_color = 0xff8000ff,
                                .flame_emission = 4 };
#ifndef MATERIAL_THERMAL_BASELINE
    reaction.extinction = .8f;
#endif
    if (cold || quench) {
        material_fluid_set_reaction(fluid, reaction);
    }
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            bool hot = exchange ? x < side / 2 : (x + y) % 2 == 0;
            fluid_particle p = { .position = { x * 4.f, y * 4.f },
                                 .kind = liquid ? FLUID_LIQUID : FLUID_GAS,
                                 .mass = liquid ? .004f : 1,
                                 .radius = 1,
                                 .color = 0x4080ffff,
                                 .heat = cold ? 0
                                       : hot  ? 2
                                              : 0,
                                 .burning = quench && hot,
                                 .fuel = cold || (quench && hot) ? 1 : 0 };
#ifndef MATERIAL_THERMAL_BASELINE
            if (!cold) {
                p.thermal = (fluid_thermal){ hot ? 1 : 20, 30 };
            }
#endif
            assert(material_fluid_add(fluid, p));
        }
    }
    game_task_pool *pool = game_task_pool_acquire_workers(workers);
    material_fluid_set_tasks(fluid, game_task_pool_tasks(pool));
    fluid_region region = { -100000, -100000, 200000, 200000 };
    int warmup = quench ? 0 : 5;
    double start = 0, reactions = 0;
    for (int frame = -warmup; frame < frames; frame++) {
        if (!frame) {
            start = seconds();
        }
        material_fluid_profile profile = { 0 };
        game_task_pool_begin(pool, true);
        material_fluid_tick_profiled(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0, &profile);
        game_task_pool_end(pool);
        if (frame >= 0) {
            reactions += profile.reactions_ms;
        }
    }
    double total = (seconds() - start) * 1000 / frames, heat = 0, fuel = 0,
           energy = 0;
    int count, burning = 0;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    float min_heat = count ? p[0].heat : 0, max_heat = min_heat;
    for (int i = 0; i < count; i++) {
        heat += p[i].heat;
        min_heat = p[i].heat < min_heat ? p[i].heat : min_heat;
        max_heat = p[i].heat > max_heat ? p[i].heat : max_heat;
        fuel += p[i].fuel;
        burning += p[i].burning;
#ifndef MATERIAL_THERMAL_BASELINE
        energy += (double)p[i].mass * p[i].thermal.capacity * p[i].heat;
#endif
    }
    printf(
        "thermal scenario=%s phase=%s warmup=%d side=%d frames=%d workers=%d "
        "particles=%d "
        "reaction_ms=%.6f tick_ms=%.6f heat=%.6f min_heat=%.6f max_heat=%.6f "
        "energy=%.6f fuel=%.6f "
        "burning=%d\n",
        scenario,
        quench ? "transient-to-settled" : "warmed",
        warmup,
        side,
        frames,
        game_task_pool_tasks(pool)->workers,
        count,
        reactions / frames,
        total,
        heat,
        min_heat,
        max_heat,
        energy,
        fuel,
        burning
    );
    material_fluid_free(fluid);
    game_task_pool_release(pool);
}
