#include "authoring/preview.h"
#include "graph/model/registry.h"
#include "runtime/component_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PASSES 20

static void require(bool success)
{
    if (!success) {
        fputs("Contact benchmark setup or detection failed.\n", stderr);
        abort();
    }
}

static double seconds()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void measure(int count, float spacing)
{
    ecs_world *ecs = ecs_world_new();
    require(ecs_world_register_component(ecs, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    require(ecs_world_register_component(ecs, &GAME_COMPONENT_GameCollider, NULL) == ECS_OK);
    game_preview_world *live = game_preview_world_new(ecs);
    for (int i = 0; i < count; i++) {
        ecs_entity entity = ecs_world_create_entity(ecs);
        require(GAME_GameTransform_add(ecs, entity, 0) == ECS_OK);
        require(GAME_GameCollider_add(ecs, entity, 0) == ECS_OK);
        require(ecs_world_apply_commands(ecs) == ECS_OK);
        *GAME_GameTransform_get(ecs, entity) = (GameTransform){ (float)(i % 64) * spacing,
                                                                (float)(i / 64) * spacing };
        *GAME_GameCollider_get(ecs, entity) = (GameCollider){ 8.0f, 1, 1 };
    }
    double start = seconds();
    require(game_physics_tick(live->runtime->physics, 0) == 0);
    double first = seconds() - start;
    game_entities_reset(live->runtime->entities);
    start = seconds();
    for (int i = 0; i < PASSES; i++) {
        require(game_physics_tick(live->runtime->physics, 0) == 0);
    }
    double average = (seconds() - start) / PASSES;
    printf(
        "contacts=%d spacing=%.0f first_ms=%.3f steady_ms=%.3f passes=%d\n",
        count,
        spacing,
        first * 1000.0,
        average * 1000.0,
        PASSES
    );
    fflush(stdout);
    game_preview_world_free(live);
    ecs_world_free(ecs);
}

int main()
{
    registry_set_packs(REGISTRY_PACKS, REGISTRY_PACK_COUNT);
    measure(1000, 40);
    measure(4000, 40);
    measure(1000, 12);
    measure(4000, 12);
    return 0;
}
