#include "prototype_preview.h"

#include "../core/array.h"
#include "../core/serialize.h"
#include "../core/xalloc.h"

#include <stdlib.h>

struct game_prototype_preview
{
    ecs_world *world;
    game_behavior_world *behaviors;
    ecs_entity entity;
};

static int attach_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user)
{
    game_prototype_preview *preview = user;
    if (world != preview->world ||
        !game_behavior_asset_registered(preview->behaviors, asset_name)) {
        return -1;
    }
    return game_behavior_attach(preview->behaviors, entity, asset_name);
}

static void clear_entity(game_prototype_preview *preview)
{
    if (!preview->entity.slot) {
        return;
    }
    game_behavior_destroy_entity(preview->behaviors, preview->entity);
    preview->entity = (ecs_entity){ 0 };
}

game_prototype_preview *game_prototype_preview_new(const game_prototype_registry *registry)
{
    if (!registry || registry->component_count < 0 ||
        (registry->component_count && !registry->components)) {
        return NULL;
    }
    game_prototype_preview *preview = xcalloc(1, sizeof *preview);
    preview->world = ecs_world_new();
    preview->behaviors = preview->world ? game_behavior_world_new(preview->world)
                                        : NULL;
    if (!preview->behaviors) {
        ecs_world_free(preview->world);
        free(preview);
        return NULL;
    }
    for (int i = 0; i < registry->component_count; i++) {
        if (registry->components[i] &&
            ecs_world_register_component(preview->world, registry->components[i], NULL) !=
                ECS_OK) {
            game_prototype_preview_free(preview);
            return NULL;
        }
    }
    return preview;
}

void game_prototype_preview_free(game_prototype_preview *preview)
{
    if (!preview) {
        return;
    }
    clear_entity(preview);
    game_behavior_world_free(preview->behaviors);
    ecs_world_free(preview->world);
    free(preview);
}

int game_prototype_preview_register_behavior(
    game_prototype_preview *preview,
    const char *asset_name,
    const behavior_asset *source
)
{
    if (!preview || !asset_name || !asset_name[0] || !source ||
        game_behavior_asset_registered(preview->behaviors, asset_name)) {
        return -1;
    }
    char *json = behavior_asset_save_json(source);
    behavior_asset *copy = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!copy ||
        game_behavior_register_asset(preview->behaviors, asset_name, copy) != 0) {
        behavior_asset_free(copy);
        return -1;
    }
    return 0;
}

int game_prototype_preview_unregister_behavior(
    game_prototype_preview *preview,
    const char *asset_name
)
{
    behavior_asset *definition = NULL;
    if (game_behavior_unregister_asset(preview ? preview->behaviors : NULL, asset_name, &definition) !=
        0) {
        return -1;
    }
    behavior_asset_free(definition);
    return 0;
}

int game_prototype_preview_launch(
    game_prototype_preview *preview,
    const game_prototype *prototype,
    char *error,
    size_t error_size
)
{
    if (!preview || !prototype) {
        return ECS_ERR_ARGUMENT;
    }
    clear_entity(preview);
    return game_prototype_spawn(
        prototype,
        preview->world,
        &(game_prototype_spawn_hooks){ attach_behavior, preview },
        &preview->entity,
        error,
        error_size
    );
}

void game_prototype_preview_stop(game_prototype_preview *preview)
{
    if (preview) {
        clear_entity(preview);
    }
}

int game_prototype_preview_tick(game_prototype_preview *preview, double monotonic_time)
{
    if (!preview) {
        return FLOW_ERR_ARGUMENT;
    }
    return game_behavior_pump(preview->behaviors, monotonic_time);
}

ecs_world *game_prototype_preview_world(game_prototype_preview *preview)
{
    return preview ? preview->world : NULL;
}

ecs_entity game_prototype_preview_entity(const game_prototype_preview *preview)
{
    return preview ? preview->entity : (ecs_entity){ 0 };
}

behavior_instance *game_prototype_preview_instance(game_prototype_preview *preview, const char *asset_name)
{
    if (!preview || !preview->entity.slot || !asset_name) {
        return NULL;
    }
    return game_behavior_instance(preview->behaviors, preview->entity, asset_name);
}
