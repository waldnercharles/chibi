#ifndef CBLUEPRINT_GAME_APPEARANCE_GEOMETRY_H
#define CBLUEPRINT_GAME_APPEARANCE_GEOMETRY_H

#include "runtime/components.h"

#include <stdbool.h>

/* Local-space body geometry, excluding border and glow. Bounds include the
 * stroke of stroked shapes. Invalid geometry leaves output arguments untouched. */
bool game_appearance_bounds(const GameAppearance *appearance, CF_V2 *min, CF_V2 *max);
bool game_appearance_contains(const GameAppearance *appearance, CF_V2 point);
/* Returns four vertices for polygon shapes, zero for other or invalid shapes.
 * The stroked chevron is closed and retains its authored local origin. */
int game_appearance_points(const GameAppearance *appearance, CF_V2 points[4]);

/* Positive radians rotate counterclockwise in Y-up space. Bounds exclude border
 * and glow. Invalid angles/points leave output arguments untouched. */
bool game_appearance_rotate_point(CF_V2 point, float radians, CF_V2 *out);
bool game_appearance_rotated_bounds(
    const GameAppearance *appearance,
    float radians,
    CF_V2 *min,
    CF_V2 *max
);
bool game_appearance_contains_rotated(const GameAppearance *appearance, float radians, CF_V2 point);

#endif
