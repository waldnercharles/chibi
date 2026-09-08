#include "editor/workspace/prototype_still.h"

#include "render/scene_renderer.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

struct prototype_still
{
    const game_prototype_registry *registry;
    game_prototype *prototype;
    char *json;
    ecs_world *world;
    ecs_entity entity;
    game_particles *particles;
    app_scene_renderer *renderer;
    engine_viewport *viewport;
};

static void render(void *user)
{
    prototype_still *still = user;
    app_scene_renderer_draw(
        still->renderer,
        still->world,
        still->particles,
        still->viewport
    );
}

prototype_still *prototype_still_new(const game_prototype_registry *registry)
{
    prototype_still *still = xcalloc(1, sizeof *still);
    still->registry = registry;
    still->renderer = app_scene_renderer_new();
    still->viewport = engine_viewport_new(320, 160);
    still->particles = game_particles_new();
    engine_viewport_set_render_callback(still->viewport, render, still);
    return still;
}

void prototype_still_free(prototype_still *still)
{
    if (!still) {
        return;
    }
    engine_viewport_free(still->viewport);
    app_scene_renderer_free(still->renderer);
    game_particles_free(still->particles);
    ecs_world_free(still->world);
    game_prototype_free(still->prototype);
    free(still->json);
    free(still);
}

int prototype_still_update(prototype_still *still, const game_prototype *prototype, char *error, size_t size)
{
    char *json = game_prototype_save(prototype);
    if (!json) {
        return -1;
    }
    if (still->json && strcmp(still->json, json) == 0) {
        free(json);
        return 0;
    }
    game_prototype *copy = game_prototype_clone(prototype, error, size);
    if (!copy) {
        free(json);
        return -1;
    }
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < still->registry->component_count; i++) {
        ecs_world_register_component(world, still->registry->components[i], NULL);
    }
    ecs_entity entity;
    if (game_prototype_spawn_components(copy, world, &entity, error, size) != ECS_OK) {
        ecs_world_free(world);
        game_prototype_free(copy);
        free(json);
        return -1;
    }
    app_scene_renderer_reset(still->renderer);
    ecs_world_free(still->world);
    game_prototype_free(still->prototype);
    free(still->json);
    still->world = world;
    still->prototype = copy;
    still->json = json;
    still->entity = entity;
    return 1;
}

ecs_world *prototype_still_world(prototype_still *still)
{
    return still->world;
}
engine_viewport *prototype_still_viewport(prototype_still *still)
{
    return still->viewport;
}
ecs_entity prototype_still_entity(const prototype_still *still)
{
    return still->entity;
}
