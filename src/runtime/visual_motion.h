#ifndef CHIBI_GAME_VISUAL_MOTION_H
#define CHIBI_GAME_VISUAL_MOTION_H

#include "ecs/ecs.h"
#include "graph/model/node.h"

/* Integrates local pitch/yaw/roll and a finite half-sine reaction. Motion owns
 * the resolved visual angles and elevation, preserving scale and physics pose.
 * FLOW_ERR_STATE: missing GameVisualTransform or GameGeometry3D.
 * FLOW_ERR_ARGUMENT: nonfinite values or negative delta/speed/reaction timing.
 * Validation is atomic: an invalid entity leaves every motion unchanged. */
int game_visual_motion_tick(ecs_world *world, float delta);

extern node_desc NODE_Set_Visual_Reaction;

#endif
