#pragma once

#include "runtime/behaviors/live.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <string.h>

/* Independent C reference for the fixed-position, inactive-feedback scale
 * workload. This is a lower bound for an optimized update system, not an AOT
 * export of the authored scene. Component pointers are borrowed only after
 * spawning finishes, and the caller permits no structural changes. */
typedef struct
{
    const GameTransform *position;
    const GameMovement *movement;
    GameVelocity *velocity;
    const pin_value *hit_remaining;
    ecs_entity entity;
} arena_reference_row;

typedef struct
{
    GameTransform position;
    uint64_t handle;
} arena_reference_target;

static arena_reference_row *reference_rows(game_live_world *live, ecs_entity *enemies)
{
    arena_reference_row *rows = NULL;
    for (int i = 0; i < acount(enemies); i++) {
        ecs_entity entity = enemies[i];
        arena_reference_row row = {
            .position = GAME_GameTransform_get_const(live->ecs, entity),
            .movement = GAME_GameMovement_get_const(live->ecs, entity),
            .velocity = GAME_GameVelocity_get(live->ecs, entity),
            .entity = entity,
        };
        assert(!GAME_GameDash_get_const(live->ecs, entity));
        int count = game_behavior_attachment_count_for_entity(live->behaviors, entity);
        for (int j = 0; j < count; j++) {
            game_behavior_attachment_info info;
            assert(!game_behavior_attachment_info_at(live->behaviors, entity, j, &info));
            behavior_instance *instance = info.instance;
            if (strcmp(instance->asset->name, "CombatFeedback")) {
                continue;
            }
            for (int k = 0; k < acount(instance->asset->variables); k++) {
                if (!strcmp(instance->asset->variables[k].name, "Hit Remaining")) {
                    row.hit_remaining = &instance->variables[k];
                }
            }
        }
        assert(row.position && row.movement && row.velocity && row.hit_remaining);
        apush(rows, row);
    }
    return rows;
}

static void reference_tick(ecs_world *world, const arena_reference_row *rows, arena_reference_target **targets)
{
    aclear(*targets);
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameCollider.id };
    ecs_query query;
    assert(!ecs_query_begin(world, &query, required, 2));
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameCollider *collider = GAME_GameCollider_get_const(world, entity);
        const GameTransform *position = GAME_GameTransform_get_const(world, entity);
        if ((collider->category & 1) && isfinite(position->x) &&
            isfinite(position->y)) {
            arena_reference_target target = { *position, ecs_entity_to_u64(entity) };
            apush(*targets, target);
        }
    }
    ecs_query_end(&query);
    for (int i = 0; i < acount(rows); i++) {
        const arena_reference_row *row = &rows[i];
        assert(row->hit_remaining->f == 0);
        double best = 2000.0 * 2000.0;
        uint64_t selected = 0, self = ecs_entity_to_u64(row->entity);
        GameTransform position = { 0 };
        for (int j = 0; j < acount(*targets); j++) {
            arena_reference_target target = (*targets)[j];
            double dx = (double)target.position.x - row->position->x;
            double dy = (double)target.position.y - row->position->y;
            double distance = dx * dx + dy * dy;
            if (target.handle == self || distance > best ||
                (distance == best && selected && target.handle > selected)) {
                continue;
            }
            selected = target.handle;
            position = target.position;
            best = distance;
        }
        GameVelocity velocity = { 0 };
        if (selected) {
            float dx = position.x - row->position->x;
            float dy = position.y - row->position->y;
            double length = hypot(dx, dy);
            float speed = fminf(
                fmaxf(row->movement->speed, 0),
                (float)sqrt(best) / (float)(1.0 / 60.0)
            );
            if (length > 0 && isfinite(length)) {
                velocity.x = (float)(dx / length) * speed;
                velocity.y = (float)(dy / length) * speed;
            }
        }
        *row->velocity = velocity;
    }
}

static void reference_snapshot(const arena_reference_row *rows, GameVelocity *snapshot, bool check)
{
    for (int i = 0; i < acount(rows); i++) {
        GameVelocity velocity = *rows[i].velocity;
        if (check) {
            assert(fabsf(velocity.x - snapshot[i].x) < .001f);
            assert(fabsf(velocity.y - snapshot[i].y) < .001f);
            continue;
        }
        snapshot[i] = velocity;
    }
}
