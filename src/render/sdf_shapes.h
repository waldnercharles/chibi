#ifndef CHIBI_APP_SDF_SHAPES_H
#define CHIBI_APP_SDF_SHAPES_H

#include "platform/cute.h"
#include "runtime/components.h"

#include <stdbool.h>

typedef CF_CustomShape app_sdf_shapes;

/* Register once per Cute app lifetime, before queuing scene drawing. A zero id
 * reports that Cute's instanced renderer is unavailable or the shader failed
 * to compile. */
app_sdf_shapes app_sdf_shapes_init();
/* Transform positions use game Y-down; radians and shader coordinates use Y-up.
 * The caller owns color, interior border and glow draw state. */
bool app_sdf_shapes_draw(
    app_sdf_shapes shapes,
    const GameAppearance *appearance,
    const GameTransform *transform,
    float radians,
    const GameSdfMotion *motion
);

#endif
