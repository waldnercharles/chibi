#include "live.h"

#include "../core/array.h"
#include "../core/serialize.h"
#include "../core/xalloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_message(char *message, size_t size, const char *format, ...)
{
    if (!message || !size) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(message, size, format, args);
    va_end(args);
}

static game_live_definition *find_definition(game_live_world *world, const char *name)
{
    for (int i = 0; i < acount(world->definitions); i++) {
        if (strcmp(world->definitions[i]->name, name) == 0) {
            return world->definitions[i];
        }
    }
    return NULL;
}

game_live_definition *game_live_definition_new(const char *asset_name, behavior_asset *working)
{
    if (!asset_name || !asset_name[0] || !working) {
        return NULL;
    }
    char *json = behavior_asset_save_json(working);
    behavior_asset *applied = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!applied) {
        return NULL;
    }
    game_live_definition *definition = (game_live_definition *)xcalloc(1, sizeof *definition);
    definition->name = xstrdup(asset_name);
    definition->working = working;
    definition->applied = applied;
    compile_report_init(&definition->diagnostics);
    return definition;
}

void game_live_definition_free(game_live_definition *definition)
{
    if (!definition) {
        return;
    }
    behavior_asset_free(definition->working);
    behavior_asset_free(definition->applied);
    compile_report_free(&definition->diagnostics);
    free(definition->name);
    free(definition->path);
    free(definition);
}

game_live_definition *game_live_find_definition(game_live_world *world, const char *asset_name)
{
    return asset_name ? find_definition(world, asset_name) : NULL;
}

const char *game_live_definition_path(const game_live_definition *definition)
{
    return definition ? definition->path : NULL;
}

int game_live_definition_set_path(game_live_definition *definition, const char *path)
{
    if (!definition || !path || !path[0]) {
        return -1;
    }
    char *copy = xstrdup(path);
    free(definition->path);
    definition->path = copy;
    return 0;
}

int game_live_definition_count(const game_live_world *world)
{
    return world ? acount(world->definitions) : 0;
}

game_live_definition *game_live_definition_at(game_live_world *world, int index)
{
    if (!world || index < 0 || index >= acount(world->definitions)) {
        return NULL;
    }
    return world->definitions[index];
}

int game_live_sync_working(game_live_definition *definition, const behavior_asset *source)
{
    if (!definition || !source) {
        return -1;
    }
    char *json = behavior_asset_save_json(source);
    behavior_asset *copy = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!copy) {
        return -1;
    }
    behavior_asset_free(definition->working);
    definition->working = copy;
    definition->dirty = 1;
    return 0;
}

int game_live_mark_dirty(game_live_definition *definition)
{
    if (!definition || !behavior_asset_mutable(definition->working)) {
        return -1;
    }
    definition->dirty = 1;
    return 0;
}

game_live_world *game_live_world_new(ecs_world *ecs)
{
    if (!ecs) {
        return NULL;
    }
    game_live_world *world = (game_live_world *)xcalloc(1, sizeof *world);
    world->ecs = ecs;
    world->behaviors = game_behavior_world_new(ecs);
    if (!world->behaviors) {
        free(world);
        return NULL;
    }
    return world;
}

void game_live_world_free(game_live_world *world)
{
    if (!world) {
        return;
    }
    game_behavior_world_free(world->behaviors);
    for (int i = 0; i < acount(world->definitions); i++) {
        game_live_definition *definition = world->definitions[i];
        definition->world = NULL;
        definition->registered = 0;
        game_live_definition_free(definition);
    }
    afree(world->definitions);
    free(world);
}

int game_live_add_definition(game_live_world *world, game_live_definition *definition)
{
    if (!world || !definition || definition->world ||
        find_definition(world, definition->name) ||
        game_behavior_register_asset(
            world->behaviors,
            definition->name,
            definition->applied
        ) != 0) {
        return -1;
    }
    definition->world = world;
    definition->registered = 1;
    apush(world->definitions, definition);
    return 0;
}

int game_live_remove_definition(game_live_world *world, const char *asset_name)
{
    if (!world || !asset_name) {
        return -1;
    }
    for (int i = 0; i < acount(world->definitions); i++) {
        game_live_definition *definition = world->definitions[i];
        if (strcmp(definition->name, asset_name) != 0 ||
            game_behavior_unregister_asset(world->behaviors, asset_name, &definition->applied) !=
                0) {
            continue;
        }
        definition->world = NULL;
        definition->registered = 0;
        adel(world->definitions, i);
        game_live_definition_free(definition);
        return 0;
    }
    return -1;
}

int game_live_duplicate_definition(game_live_world *world, const char *asset_name, const behavior_asset *source)
{
    if (!world || !asset_name || !asset_name[0] || !source ||
        find_definition(world, asset_name)) {
        return -1;
    }
    char *json = behavior_asset_save_json(source);
    behavior_asset *working = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    game_live_definition *definition = working
                                         ? game_live_definition_new(asset_name, working)
                                         : NULL;
    if (!definition) {
        behavior_asset_free(working);
        return -1;
    }
    if (game_live_add_definition(world, definition) != 0) {
        game_live_definition_free(definition);
        return -1;
    }
    return 0;
}

int game_live_apply(game_live_world *world, game_live_definition *definition, char *message, size_t message_size)
{
    if (!world || !definition || definition->world != world || !definition->registered) {
        set_message(message, message_size, "definition is not registered with this world");
        return -1;
    }
    if (!definition->dirty) {
        set_message(message, message_size, "definition has no unapplied edits");
        return 0;
    }
    char *json = behavior_asset_save_json(definition->working);
    behavior_asset *snapshot = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!snapshot) {
        set_message(message, message_size, "working definition could not be loaded: %s", graph_last_error());
        return -1;
    }
    compile_report_free(&definition->diagnostics);
    compile_report_init(&definition->diagnostics);
    if (behavior_asset_compile(snapshot, &definition->diagnostics) != 0 &&
        compile_report_blocked(&definition->diagnostics)) {
        const compile_diagnostic *diagnostic = compile_report_first_error(
            &definition->diagnostics
        );
        set_message(message, message_size, "apply blocked: %s", diagnostic->message);
        behavior_asset_free(snapshot);
        return -1;
    }
    behavior_asset *old = NULL;
    if (game_behavior_replace_asset(world->behaviors, definition->name, snapshot, &old) !=
        0) {
        set_message(message, message_size, "definition replacement failed");
        behavior_asset_free(snapshot);
        return -1;
    }
    definition->applied = snapshot;
    definition->dirty = 0;
    behavior_asset_free(old);
    set_message(
        message,
        message_size,
        "applied %s: destroyed and recreated its instances",
        definition->name
    );
    return 0;
}
