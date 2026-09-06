#ifndef CBLUEPRINT_APP_SDF_SHAPES_H
#define CBLUEPRINT_APP_SDF_SHAPES_H

#include "../game/components.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t id;
} app_sdf_shapes;

/* Register once per Cute app lifetime, before queuing scene drawing. A zero id
 * reports unsupported custom coverage shaders or shader compilation failure. */
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
