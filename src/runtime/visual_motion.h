#ifndef CHIBI_VISUAL_MOTION_H
#define CHIBI_VISUAL_MOTION_H

#include "ecs/ecs.h"

/* Integrates local pitch/yaw/roll and a finite half-sine reaction. Motion owns
 * the resolved visual angles and elevation, preserving scale and physics pose.
 * FLOW_ERR_STATE: missing VisualTransform or Geometry3D.
 * FLOW_ERR_ARGUMENT: nonfinite values or negative delta/speed/reaction timing.
 * Validation is atomic: an invalid entity leaves every motion unchanged. */
int visual_motion_tick(ecs_world *world, float delta);

#endif
