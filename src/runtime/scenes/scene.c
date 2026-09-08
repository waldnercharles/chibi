#include "runtime/scenes/scene.h"

#include "graph/model/registry.h"
#include "runtime/generated.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <time.h>

struct game_scene
{
    ecs_world *world;
    game_behavior_world *behaviors;
    behavior_asset *definition;
    ecs_entity *entities;
    int entity_count;
    int behavior_count;
    double behavior_ms;
};

static behavior_asset *scene_definition(void)
{
    behavior_asset *definition = behavior_asset_new("SeededSceneBehavior");
    if (!definition || behavior_asset_set_owner_type(definition, "ecs.entity") != 0) {
        behavior_asset_free(definition);
        return NULL;
    }
    node *event = graph_add_node(definition->graph, registry_node_by_name("Event"));
    node *branch = graph_add_node(definition->graph, registry_node_by_name("Branch"));
    pin_value condition = { .type = PIN_BOOL, .type_name = "bool", .b = true };
    if (!event || !branch ||
        graph_set_event_name(definition->graph, event, "Tick") != 0 ||
        graph_set_pin_default(definition->graph, branch, 0, 1, &condition) != 0 ||
        graph_connect_exec(definition->graph, event, 0, branch, 0, 0) != 0) {
        behavior_asset_free(definition);
        return NULL;
    }
    return definition;
}

static uint32_t next_seed(uint32_t *seed)
{
    uint32_t value = *seed;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *seed = value ? value : 1;
    return *seed;
}

static double elapsed_ms(struct timespec start, struct timespec end)
{
    int64_t seconds = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nanoseconds = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
    return (double)(seconds * INT64_C(1000000000) + nanoseconds) / 1000000.0;
}

game_scene *game_scene_new(int entity_count, int behavior_count, uint64_t seed)
{
    if (entity_count < 1 || behavior_count < 0 || behavior_count > entity_count) {
        return NULL;
    }
    game_scene *scene = (game_scene *)xcalloc(1, sizeof *scene);
    scene->world = ecs_world_new();
    scene->entities = (ecs_entity *)xcalloc((size_t)entity_count, sizeof *scene->entities);
    scene->entity_count = entity_count;
    scene->behavior_count = behavior_count;
    if (ecs_world_register_component(scene->world, &GAME_COMPONENT_GameTransform, NULL) !=
            ECS_OK ||
        ecs_world_register_component(scene->world, &GAME_COMPONENT_GameHealth, NULL) !=
            ECS_OK) {
        game_scene_free(scene);
        return NULL;
    }
    uint32_t random = (uint32_t)seed | 1u;
    for (int i = 0; i < entity_count; i++) {
        scene->entities[i] = ecs_world_record_create(scene->world, (uint64_t)i);
        if (!scene->entities[i].slot) {
            game_scene_free(scene);
            return NULL;
        }
    }
    if (ecs_world_apply_commands(scene->world) != ECS_OK) {
        game_scene_free(scene);
        return NULL;
    }
    for (int i = 0; i < entity_count; i++) {
        GameTransform transform = {
            .x = (float)(next_seed(&random) % 1000u) / 10.0f,
            .y = (float)(next_seed(&random) % 1000u) / 10.0f,
        };
        GameHealth health = {
            .current = (int)(next_seed(&random) % 100u) + 1,
            .maximum = 100,
        };
        if (GAME_GameTransform_add(scene->world, scene->entities[i], (uint64_t)i * 2) !=
                ECS_OK ||
            GAME_GameHealth_add(scene->world, scene->entities[i], (uint64_t)i * 2 + 1) !=
                ECS_OK) {
            game_scene_free(scene);
            return NULL;
        }
        ecs_world_apply_commands(scene->world);
        GameTransform *
            stored_transform = GAME_GameTransform_get(scene->world, scene->entities[i]);
        GameHealth *stored_health = GAME_GameHealth_get(scene->world, scene->entities[i]);
        *stored_transform = transform;
        *stored_health = health;
    }
    scene->behaviors = game_behavior_world_new(scene->world);
    scene->definition = scene_definition();
    if (!scene->behaviors || !scene->definition ||
        game_behavior_register_asset(scene->behaviors, "SceneBehavior", scene->definition) !=
            0) {
        game_scene_free(scene);
        return NULL;
    }
    for (int i = 0; i < behavior_count; i++) {
        if (game_behavior_attach(scene->behaviors, scene->entities[i], "SceneBehavior") !=
            0) {
            game_scene_free(scene);
            return NULL;
        }
    }
    return scene;
}

void game_scene_free(game_scene *scene)
{
    if (!scene) {
        return;
    }
    game_behavior_world_free(scene->behaviors);
    behavior_asset_free(scene->definition);
    ecs_world_free(scene->world);
    free(scene->entities);
    free(scene);
}

int game_scene_frame(game_scene *scene, double monotonic_time)
{
    if (!scene) {
        return FLOW_ERR_ARGUMENT;
    }
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < scene->behavior_count; i++) {
        if (game_behavior_dispatch(scene->behaviors, scene->entities[i], "Tick", (flow_inputs){ 0 }, NULL) <
            0) {
            return FLOW_ERR_NODE;
        }
    }
    if (game_behavior_pump(scene->behaviors, monotonic_time) < 0) {
        return FLOW_ERR_NODE;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    scene->behavior_ms = elapsed_ms(start, end);
    if (ecs_world_apply_commands(scene->world) != ECS_OK) {
        return ECS_ERR_COMMAND;
    }
    for (int i = 0; i < scene->entity_count; i++) {
        GameHealth *health = GAME_GameHealth_get(scene->world, scene->entities[i]);
        health->current = health->current >= health->maximum ? 1 : health->current + 1;
    }
    return ECS_OK;
}

void game_scene_set_parallel_for(game_scene *scene, flow_parallel_for_fn parallel_for, void *parallel_user)
{
    if (scene) {
        game_behavior_set_parallel_for(scene->behaviors, parallel_for, parallel_user);
    }
}

uint64_t game_scene_hash(const game_scene *scene)
{
    return scene ? ecs_world_hash(scene->world) : 0;
}

double game_scene_behavior_ms(const game_scene *scene)
{
    return scene ? scene->behavior_ms : 0.0;
}

int game_scene_entity_count(const game_scene *scene)
{
    return scene ? scene->entity_count : 0;
}

int game_scene_behavior_count(const game_scene *scene)
{
    return scene ? scene->behavior_count : 0;
}
