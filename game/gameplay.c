#include "gameplay.h"

#include "generated.h"

#include <math.h>
#include <stddef.h>

#define GAMEPLAY_PROJECTILE_SORT_KEY 0x100u
#define GAMEPLAY_HIT_RADIUS 16.0f

static int close_enough(const GameTransform *left, const GameTransform *right)
{
    float x = left->x - right->x;
    float y = left->y - right->y;
    return x * x + y * y <= GAMEPLAY_HIT_RADIUS * GAMEPLAY_HIT_RADIUS;
}

int game_gameplay_fire(ecs_world *world, ecs_entity owner)
{
    if (!world || !ecs_entity_is_alive(world, owner)) {
        return -1;
    }
    const GameTransform *origin = GAME_GameTransform_get_const(world, owner);
    const GameWeapon *weapon = GAME_GameWeapon_get_const(world, owner);
    if (!origin || !weapon || weapon->projectile_count <= 0) {
        return 0;
    }
    for (int i = 0; i < weapon->projectile_count; i++) {
        ecs_entity projectile = ecs_world_record_create(
            world,
            GAMEPLAY_PROJECTILE_SORT_KEY + (uint64_t)i
        );
        if (!projectile.slot || ecs_world_record_add(
                                    world,
                                    projectile,
                                    GAME_COMPONENT_GameTransform.id,
                                    origin,
                                    GAMEPLAY_PROJECTILE_SORT_KEY + (uint64_t)i
                                ) != ECS_OK) {
            return -1;
        }
        GameProjectile value = {
            .speed = 420.0f,
            .lifetime = 2.0f,
            .damage = weapon->damage,
            .direction_x = 1.0f,
            .direction_y = 0.0f,
            .age = 0.0f,
        };
        if (ecs_world_record_add(
                world,
                projectile,
                GAME_COMPONENT_GameProjectile.id,
                &value,
                GAMEPLAY_PROJECTILE_SORT_KEY + (uint64_t)i
            ) != ECS_OK) {
            return -1;
        }
    }
    return weapon->projectile_count;
}

int game_gameplay_tick(ecs_world *world, double delta_seconds)
{
    if (!world || delta_seconds < 0.0) {
        return -1;
    }
    if (!ecs_world_component(world, GAME_COMPONENT_GameTransform.id) ||
        !ecs_world_component(world, GAME_COMPONENT_GameProjectile.id)) {
        return 0;
    }
    ecs_component_id required[] = {
        GAME_COMPONENT_GameTransform.id,
        GAME_COMPONENT_GameProjectile.id,
    };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return -1;
    }
    ecs_entity projectile;
    int expired = 0;
    while (ecs_query_next(&query, &projectile)) {
        GameTransform *transform = GAME_GameTransform_get(world, projectile);
        GameProjectile *state = GAME_GameProjectile_get(world, projectile);
        if (!transform || !state) {
            continue;
        }
        transform->x += state->direction_x * state->speed * (float)delta_seconds;
        transform->y += state->direction_y * state->speed * (float)delta_seconds;
        state->age += (float)delta_seconds;
        int hit = 0;
        ecs_component_id health_id = GAME_COMPONENT_GameHealth.id;
        ecs_query targets;
        if (ecs_query_begin(world, &targets, &health_id, 1) == ECS_OK) {
            ecs_entity target;
            while (ecs_query_next(&targets, &target)) {
                if (target.slot == projectile.slot &&
                    target.generation == projectile.generation) {
                    continue;
                }
                GameTransform *target_transform = GAME_GameTransform_get(world, target);
                GameHealth *health = GAME_GameHealth_get(world, target);
                if (target_transform && health &&
                    close_enough(transform, target_transform)) {
                    health->current -= state->damage;
                    hit = 1;
                    break;
                }
            }
            ecs_query_end(&targets);
        }
        if (hit || state->age >= state->lifetime) {
            ecs_world_record_destroy(world, projectile, GAMEPLAY_PROJECTILE_SORT_KEY);
            expired++;
        }
    }
    ecs_query_end(&query);
    if (expired) {
        return ecs_world_apply_commands(world) == ECS_OK ? expired : -1;
    }
    return 0;
}
