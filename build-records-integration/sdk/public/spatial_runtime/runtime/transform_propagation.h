#ifndef CHIBI_TRANSFORM_PROPAGATION_H
#define CHIBI_TRANSFORM_PROPAGATION_H

#include "ecs/ecs.h"
#include "runtime/components.h"

/* Writes every GlobalTransform2D and GlobalTransform3D from the entity's local
 * transform and its nearest ChildOf ancestor's global transform, parents
 * first. Worlds without the transform components have nothing to write.
 * Returns ECS_OK, or the ECS error that stopped it. */
int transform_propagate(ecs_world *world);
/* The nearest ChildOf ancestor with a Transform, which places the entity, or a
 * zero handle when the entity is placed in the world. */
ecs_entity transform_parent(const ecs_world *world, ecs_entity entity);
/* The entity's world placement from its own and its ancestors' Transform and
 * Orientation. It is exact at any time, while GlobalTransform2D holds the same
 * value as of the last propagation. The entity must have a Transform. */
GlobalTransform2D transform_world_2d(const ecs_world *world, ecs_entity entity);
/* The nearest ChildOf ancestor with a Transform3D, or a zero handle. */
ecs_entity transform_parent_3d(const ecs_world *world, ecs_entity entity);
/* The entity's world placement from its own and its ancestors' Transform3D,
 * exact at any time like transform_world_2d. The entity must have a
 * Transform3D. */
GlobalTransform3D transform_world_3d(const ecs_world *world, ecs_entity entity);
/* The affine matrix whose columns a GlobalTransform3D holds. */
CF_M4x4 transform_matrix_3d(const GlobalTransform3D *global);

#endif
