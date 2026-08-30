#ifndef CBLUEPRINT_GAME_HOT_RELOAD_H
#define CBLUEPRINT_GAME_HOT_RELOAD_H

#include "../core/behavior_asset.h"
#include "../ecs/ecs.h"

#include <stddef.h>

#ifndef GAME_HOT_RELOAD
#define GAME_HOT_RELOAD 1
#endif

typedef const node_desc *(*game_hot_node_resolve_fn)(const char *name, void *user);

typedef struct
{
    const char *module_name;
    game_hot_node_resolve_fn resolve_node;
    void *resolve_user;
    const ecs_component_desc *const *components;
    int component_count;
} game_hot_image;

typedef struct
{
    int rebound_nodes;
    int changed_components;
    int preserved_fields;
    int zeroed_fields;
    int failed;
    char message[192];
} game_hot_report;

typedef struct game_hot_module game_hot_module;

game_hot_module *game_hot_module_new(const game_hot_image *image);
void game_hot_module_free(game_hot_module *module);
const game_hot_image *game_hot_module_active(const game_hot_module *module);
int game_hot_module_reload(
    game_hot_module *module,
    const game_hot_image *candidate,
    ecs_world *world,
    behavior_asset *const *definitions,
    int definition_count,
    game_hot_report *report
);
int game_hot_rebind_behavior_asset(
    behavior_asset *definition,
    const game_hot_image *image,
    game_hot_report *report
);
int game_hot_reload_components(ecs_world *world, const game_hot_image *image, game_hot_report *report);
int game_hot_reload_available(void);

#endif
