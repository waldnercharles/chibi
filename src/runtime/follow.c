#include "runtime/follow.h"

#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

/* Longer target chains than this are cycles. */
#define FOLLOW_DEPTH_MAX 256

typedef enum
{
    FOLLOW_PENDING,
    FOLLOW_ACTIVE,
    FOLLOW_DONE
} follow_state;

typedef struct
{
    ecs_world *world;
    game_physics *physics;
    float seconds;
    unsigned char *states; /* follow_state by entity slot */
} follow_pass;

static float heading_of(ecs_world *world, ecs_entity entity)
{
    const GameOrientation *orientation = GAME_GameOrientation_get_const(world, entity);
    return orientation ? orientation->radians : 0;
}

static CF_V2 turn(CF_V2 v, float radians)
{
    float c = cosf(radians), s = sinf(radians);
    return (CF_V2){ v.x * c - v.y * s, v.x * s + v.y * c };
}

/* Turns from heading toward goal by the shorter way, a rate fraction per step. */
static float approach(float heading, float goal, float rate, float seconds)
{
    float blend = rate > 0 ? fminf(1, rate * seconds) : 1;
    return heading + remainderf(goal - heading, 6.28318530718f) * blend;
}

static const char *follow(follow_pass *pass, ecs_entity entity, int depth);

/* Moves one follower; a target that follows something is settled first. */
static const char *place(follow_pass *pass, ecs_entity entity, int depth)
{
    ecs_world *world = pass->world;
    GameFollow *rule = GAME_GameFollow_get(world, entity);
    ecs_entity target = ecs_entity_from_u64(rule->target);
    if (rule->target && !ecs_entity_is_alive(world, target)) {
        rule->target = 0;
    }
    if (!rule->target) {
        return NULL;
    }
    const char *error = GAME_GameFollow_get(world, target)
                          ? follow(pass, target, depth + 1)
                          : NULL;
    if (error) {
        return error;
    }
    GameTransform *position = GAME_GameTransform_get(world, entity);
    const GameTransform *goal = GAME_GameTransform_get_const(world, target);
    if (!position || !goal) {
        return "Follow requires a Transform on the entity and its target";
    }
    if (!isfinite(rule->offset.x + rule->offset.y + rule->distance + rule->stiffness) ||
        rule->distance < 0 || rule->stiffness < 0 ||
        (rule->mode != GAME_FOLLOW_ATTACH && rule->mode != GAME_FOLLOW_CHAIN)) {
        return "Follow requires a finite offset and nonnegative distance and "
               "stiffness";
    }
    float target_heading = heading_of(world, target);
    CF_V2 anchor = cf_add(*goal, turn(rule->offset, target_heading));
    CF_V2 placed = anchor, away = cf_sub(*position, anchor);
    float length = cf_len(away);
    if (rule->mode == GAME_FOLLOW_CHAIN) {
        placed = length > 0
                   ? cf_add(anchor, cf_mul(away, rule->distance / length))
                   : cf_add(anchor, turn((CF_V2){ -rule->distance, 0 }, target_heading));
    }
    *position = placed;
    GameOrientation *orientation = GAME_GameOrientation_get(world, entity);
    if (orientation && rule->inherit_heading) {
        orientation->radians = rule->mode == GAME_FOLLOW_ATTACH
                                 ? target_heading
                                 : approach(
                                       orientation->radians,
                                       length > 0 ? atan2f(-away.y, -away.x) : target_heading,
                                       rule->stiffness,
                                       pass->seconds
                                   );
    }
    /* Moving with the target keeps the next solve close to the next placement. */
    GameVelocity *velocity = GAME_GameVelocity_get(world, entity);
    const GameVelocity *carried = GAME_GameVelocity_get_const(world, target);
    if (velocity) {
        *velocity = carried ? *carried : (GameVelocity){ 0 };
    }
    game_physics_place(pass->physics, entity, placed, heading_of(world, entity));
    return NULL;
}

static const char *follow(follow_pass *pass, ecs_entity entity, int depth)
{
    unsigned char *state = &pass->states[entity.slot];
    if (*state == FOLLOW_ACTIVE || depth > FOLLOW_DEPTH_MAX) {
        return "Follow targets form a cycle";
    }
    if (*state == FOLLOW_DONE) {
        return NULL;
    }
    *state = FOLLOW_ACTIVE;
    const char *error = place(pass, entity, depth);
    pass->states[entity.slot] = FOLLOW_DONE;
    return error;
}

const char *game_follow_update(ecs_world *world, game_physics *physics, float seconds)
{
    ecs_component_id id = GAME_COMPONENT_GameFollow.id;
    if (!ecs_world_component(world, id) ||
        !ecs_component_has_entities(world, id, NULL, 0)) {
        return NULL;
    }
    ecs_entity *followers = NULL, entity;
    ecs_query query;
    int result = ecs_query_begin(world, &query, &id, 1);
    assert(result == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        apush(followers, entity);
    }
    ecs_query_end(&query);
    follow_pass pass = { world,
                         physics,
                         seconds,
                         xcalloc(ecs_world_high_water_mark(world) + 1, 1) };
    const char *error = NULL;
    for (int i = 0; i < acount(followers) && !error; i++) {
        error = follow(&pass, followers[i], 0);
    }
    free(pass.states);
    afree(followers);
    return error;
}
