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

static GameGlobalTransform2D compose_2d(GameGlobalTransform2D parent, CF_V2 position, float radians)
{
    cos_sin turn = cos_sin_compute(parent.radians);
    return (GameGlobalTransform2D){
        .position = { parent.position.x + turn.c * position.x -
                          turn.s * position.y,
                      parent.position.y + turn.s * position.x +
                          turn.c * position.y },
        .radians = parent.radians + radians,
    };
}

static float local_radians(const ecs_world *world, ecs_entity entity)
{
    const GameOrientation *orientation = GAME_GameOrientation_get_const(world, entity);
    return orientation ? orientation->radians : 0;
}

ecs_entity game_transform_parent(const ecs_world *world, ecs_entity entity)
{
    ecs_entity parent = ecs_parent(world, entity);
    while (parent.slot && !GAME_GameTransform_has(world, parent)) {
        parent = ecs_parent(world, parent);
    }
    return parent;
}

GameGlobalTransform2D game_transform_world_2d(const ecs_world *world, ecs_entity entity)
{
    const GameTransform *local = GAME_GameTransform_get_const(world, entity);
    assert(local);
    ecs_entity parent = game_transform_parent(world, entity);
    if (!parent.slot) {
        return (GameGlobalTransform2D){ *local, local_radians(world, entity) };
    }
    return compose_2d(game_transform_world_2d(world, parent), *local, local_radians(world, entity));
}

static int propagate_2d(ecs_world *world)
{
    if (!installed(
            world,
            GAME_COMPONENT_GameTransform.id,
            GAME_COMPONENT_GameGlobalTransform2D.id
        )) {
        return ECS_OK;
    }
    ecs_query query;
    int result = begin(
        world,
        &query,
        GAME_COMPONENT_GameTransform.id,
        GAME_COMPONENT_GameGlobalTransform2D.id
    );
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        ecs_entity parent = ecs_query_src(&query, TERM_PARENT);
        CF_V2 local = *GAME_GameTransform_get_const(world, entity);
        float radians = local_radians(world, entity);
        *GAME_GameGlobalTransform2D_get(
            world,
            entity
        ) = parent.slot
              ? compose_2d(*GAME_GameGlobalTransform2D_get_const(world, parent), local, radians)
              : (GameGlobalTransform2D){ local, radians };
    }
    ecs_query_end(&query);
    return ECS_OK;
}

static CF_M4x4 matrix_3d(const GameGlobalTransform3D *global)
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

static int propagate_3d(ecs_world *world)
{
    if (!installed(
            world,
            GAME_COMPONENT_GameTransform3D.id,
            GAME_COMPONENT_GameGlobalTransform3D.id
        )) {
        return ECS_OK;
    }
    ecs_query query;
    int result = begin(
        world,
        &query,
        GAME_COMPONENT_GameTransform3D.id,
        GAME_COMPONENT_GameGlobalTransform3D.id
    );
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        ecs_entity parent = ecs_query_src(&query, TERM_PARENT);
        const GameTransform3D *local = GAME_GameTransform3D_get_const(world, entity);
        CF_M4x4 m = cf_m4_from_trs(local->position, unit(local->rotation), local->scale);
        if (parent.slot) {
            m = cf_mul_m4(matrix_3d(GAME_GameGlobalTransform3D_get_const(world, parent)), m);
        }
        *GAME_GameGlobalTransform3D_get(world, entity) = (GameGlobalTransform3D){
            .x_axis = column(m, 0),
            .y_axis = column(m, 1),
            .z_axis = column(m, 2),
            .position = column(m, 3),
        };
    }
    ecs_query_end(&query);
    return ECS_OK;
}

int game_transform_propagate(ecs_world *world)
{
    int result = propagate_2d(world);
    return result == ECS_OK ? propagate_3d(world) : result;
}
