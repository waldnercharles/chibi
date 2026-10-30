#include "runtime/transform_propagation.h"

#include "runtime/component_catalog.h"
#include "support/cos_sin.h"

#include <assert.h>
#include <cute_math3d.h>

enum
{
    TERM_LOCAL,
    TERM_GLOBAL,
    TERM_PARENT,
    TERM_COUNT
};

/* Visits entities with local and global, parents first; the parent term is
 * the nearest ancestor's global. */
static int begin(ecs_world *world, ecs_query *query, ecs_component_id local, ecs_component_id global)
{
    ecs_term terms[TERM_COUNT] = {
        [TERM_LOCAL] = { .id = local },
        [TERM_GLOBAL] = { .id = global },
        [TERM_PARENT] = { .oper = ECS_OPTIONAL, .id = global, .trav = ECS_UP, .cascade = true },
    };
    return ecs_query_begin_terms(world, query, terms, TERM_COUNT);
}

static bool installed(const ecs_world *world, ecs_component_id local, ecs_component_id global)
{
    return ecs_world_component(world, local) && ecs_world_component(world, global);
}

static GlobalTransform2D compose_2d(GlobalTransform2D parent, CF_V2 position, float radians)
{
    cos_sin turn = cos_sin_compute(parent.radians);
    return (GlobalTransform2D){
        .position = { parent.position.x + turn.c * position.x -
                          turn.s * position.y,
                      parent.position.y + turn.s * position.x +
                          turn.c * position.y },
        .radians = parent.radians + radians,
    };
}

static float local_radians(const ecs_world *world, ecs_entity entity)
{
    const Orientation *orientation = Orientation_get_const(world, entity);
    return orientation ? orientation->radians : 0;
}

ecs_entity transform_parent(const ecs_world *world, ecs_entity entity)
{
    ecs_entity parent = ecs_parent(world, entity);
    while (parent.slot && !Transform_has(world, parent)) {
        parent = ecs_parent(world, parent);
    }
    return parent;
}

GlobalTransform2D transform_world_2d(const ecs_world *world, ecs_entity entity)
{
    const Transform *local = Transform_get_const(world, entity);
    assert(local);
    ecs_entity parent = transform_parent(world, entity);
    if (!parent.slot) {
        return (GlobalTransform2D){ *local, local_radians(world, entity) };
    }
    return compose_2d(transform_world_2d(world, parent), *local, local_radians(world, entity));
}

static int propagate_2d(ecs_world *world)
{
    if (!installed(world, COMPONENT_Transform.id, COMPONENT_GlobalTransform2D.id)) {
        return ECS_OK;
    }
    ecs_query query;
    int result = begin(
        world,
        &query,
        COMPONENT_Transform.id,
        COMPONENT_GlobalTransform2D.id
    );
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        ecs_entity parent = ecs_query_src(&query, TERM_PARENT);
        CF_V2 local = *Transform_get_const(world, entity);
        float radians = local_radians(world, entity);
        *GlobalTransform2D_get(
            world,
            entity
        ) = parent.slot
              ? compose_2d(*GlobalTransform2D_get_const(world, parent), local, radians)
              : (GlobalTransform2D){ local, radians };
    }
    ecs_query_end(&query);
    return ECS_OK;
}

CF_M4x4 transform_matrix_3d(const GlobalTransform3D *global)
{
    CF_M4x4 m = cf_m4_identity();
    const CF_V3 columns[] = { global->x_axis,
                              global->y_axis,
                              global->z_axis,
                              global->position };
    for (int i = 0; i < 4; i++) {
        m.elements[i * 4] = columns[i].x;
        m.elements[i * 4 + 1] = columns[i].y;
        m.elements[i * 4 + 2] = columns[i].z;
    }
    return m;
}

static CF_V3 column(CF_M4x4 m, int index)
{
    return (CF_V3){ m.elements[index * 4],
                    m.elements[index * 4 + 1],
                    m.elements[index * 4 + 2] };
}

/* An unset (zero) rotation reads as identity. */
static CF_Quat unit(CF_Quat q)
{
    return q.x || q.y || q.z || q.w ? cf_quat_norm(q) : cf_quat_identity();
}

static CF_M4x4 local_matrix(const Transform3D *local)
{
    return cf_m4_from_trs(local->position, unit(local->rotation), local->scale);
}

static GlobalTransform3D global_3d(CF_M4x4 m)
{
    return (GlobalTransform3D){
        .x_axis = column(m, 0),
        .y_axis = column(m, 1),
        .z_axis = column(m, 2),
        .position = column(m, 3),
    };
}

ecs_entity transform_parent_3d(const ecs_world *world, ecs_entity entity)
{
    ecs_entity parent = ecs_parent(world, entity);
    while (parent.slot && !Transform3D_has(world, parent)) {
        parent = ecs_parent(world, parent);
    }
    return parent;
}

static CF_M4x4 world_matrix_3d(const ecs_world *world, ecs_entity entity)
{
    const Transform3D *local = Transform3D_get_const(world, entity);
    assert(local);
    ecs_entity parent = transform_parent_3d(world, entity);
    CF_M4x4 m = local_matrix(local);
    return parent.slot ? cf_mul_m4(world_matrix_3d(world, parent), m) : m;
}

GlobalTransform3D transform_world_3d(const ecs_world *world, ecs_entity entity)
{
    return global_3d(world_matrix_3d(world, entity));
}

static int propagate_3d(ecs_world *world)
{
    if (!installed(world, COMPONENT_Transform3D.id, COMPONENT_GlobalTransform3D.id)) {
        return ECS_OK;
    }
    ecs_query query;
    int result = begin(
        world,
        &query,
        COMPONENT_Transform3D.id,
        COMPONENT_GlobalTransform3D.id
    );
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        ecs_entity parent = ecs_query_src(&query, TERM_PARENT);
        CF_M4x4 m = local_matrix(Transform3D_get_const(world, entity));
        if (parent.slot) {
            m = cf_mul_m4(transform_matrix_3d(GlobalTransform3D_get_const(world, parent)), m);
        }
        *GlobalTransform3D_get(world, entity) = global_3d(m);
    }
    ecs_query_end(&query);
    return ECS_OK;
}

int transform_propagate(ecs_world *world)
{
    int result = propagate_2d(world);
    return result == ECS_OK ? propagate_3d(world) : result;
}
