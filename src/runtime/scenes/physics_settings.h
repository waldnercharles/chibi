#pragma once

#include "runtime/tasks.h"

#include <math.h>
#include <stdbool.h>

enum
{
    GAME_PHYSICS_WORKER_MAX = GAME_TASK_WORKER_MAX,
    GAME_PHYSICS_SUBSTEP_MAX = 16
};

typedef struct
{
    int workers; /* Zero selects the runtime default; includes the caller. */
    int substeps;
    bool sleep;
    bool continuous; /* Box2D solids; authored swept sensors are independent. */
    bool force_drive; /* Dynamic velocity targets reached by force over a step. */
    float contact_hertz, contact_damping;
} game_physics_settings;

static inline game_physics_settings game_physics_defaults()
{
    return (game_physics_settings){ .substeps = 4,
                                    .sleep = true,
                                    .continuous = true,
                                    .contact_hertz = 30,
                                    .contact_damping = 10 };
}

static inline bool game_physics_settings_valid(game_physics_settings settings)
{
    return settings.workers >= 0 && settings.workers <= GAME_PHYSICS_WORKER_MAX &&
           settings.substeps >= 1 && settings.substeps <= GAME_PHYSICS_SUBSTEP_MAX &&
           isfinite(settings.contact_hertz) && settings.contact_hertz >= 0 &&
           settings.contact_hertz <= 240 && isfinite(settings.contact_damping) &&
           settings.contact_damping >= 0 && settings.contact_damping <= 100;
}

static inline bool game_physics_settings_equal(game_physics_settings a, game_physics_settings b)
{
    return a.workers == b.workers && a.substeps == b.substeps &&
           a.sleep == b.sleep && a.continuous == b.continuous &&
           a.force_drive == b.force_drive && a.contact_hertz == b.contact_hertz &&
           a.contact_damping == b.contact_damping;
}
