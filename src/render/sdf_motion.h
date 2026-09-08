#ifndef CBLUEPRINT_GAME_SDF_MOTION_H
#define CBLUEPRINT_GAME_SDF_MOTION_H

#include "runtime/components.h"

#include <stdbool.h>

typedef struct ecs_world ecs_world;

/* Source Neon-Swarm radial SDF distortion in local coordinates. Fields must be
 * finite and angular frequency nonnegative. Inactive amplitudes are ignored.
 * Invalid inputs/overflow leave output untouched. At the normalization
 * singularity (-0.0001,-0.0001), displacement is defined as zero. */
bool game_sdf_motion_warp(const GameSdfMotion *motion, CF_V2 point, CF_V2 *out);
bool game_sdf_motion_padding(const GameSdfMotion *motion, float *out);

/* Advances only simulation clocks. Returns ECS_OK or a negative error; invalid
 * fields/delta/overflow reject the entire world update without mutation. */
int game_sdf_motion_tick(ecs_world *world, double delta);

#endif
