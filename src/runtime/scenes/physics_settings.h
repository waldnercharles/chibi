#pragma once

#include "runtime/tasks.h"

#include <math.h>
#include <stdbool.h>

enum
{
    PHYSICS_WORKER_MAX = TASK_WORKER_MAX,
    PHYSICS_SUBSTEP_MAX = 16
};

typedef struct
{
    int workers; /* Zero selects the runtime default; includes the caller. */
    int substeps;
    bool sleep;
    bool continuous; /* Box2D solids; authored swept sensors are independent. */
    bool force_drive; /* Dynamic velocity targets reached by force over a step. */
    float contact_hertz, contact_damping;
} physics_settings;

static inline physics_settings physics_defaults()
{
    return (physics_settings){ .substeps = 4,
                               .sleep = true,
                               .continuous = true,
                               .contact_hertz = 30,
                               .contact_damping = 10 };
}

static inline bool physics_settings_valid(physics_settings settings)
{
    return settings.workers >= 0 && settings.workers <= PHYSICS_WORKER_MAX &&
           settings.substeps >= 1 && settings.substeps <= PHYSICS_SUBSTEP_MAX &&
           isfinite(settings.contact_hertz) && settings.contact_hertz >= 0 &&
           settings.contact_hertz <= 240 && isfinite(settings.contact_damping) &&
           settings.contact_damping >= 0 && settings.contact_damping <= 100;
}

static inline bool physics_settings_equal(physics_settings a, physics_settings b)
{
    return a.workers == b.workers && a.substeps == b.substeps &&
           a.sleep == b.sleep && a.continuous == b.continuous &&
           a.force_drive == b.force_drive && a.contact_hertz == b.contact_hertz &&
           a.contact_damping == b.contact_damping;
}
