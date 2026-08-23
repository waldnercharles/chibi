#include "live.h"

#include "../core/array.h"
#include "../core/serialize.h"
#include "../core/xalloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_live_definition
{
    char *name;
    blueprint *working;
    blueprint *applied;
    game_live_world *world;
    int registered;
    int dirty;
};

struct game_live_world
{
    ecs_world *ecs;
    game_behavior_world *behaviors;
    game_live_definition **definitions;
};

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

game_live_definition *game_live_definition_new(const char *asset_name, blueprint *working)
{
    if (!asset_name || !asset_name[0] || !working) {
        return NULL;
    }
    char *json = blueprint_save_json(working);
    blueprint *applied = json ? blueprint_load_json(json) : NULL;
    free(json);
    if (!applied) {
        return NULL;
    }
    game_live_definition *definition = (game_live_definition *)xcalloc(1, sizeof *definition);
    definition->name = xstrdup(asset_name);
    definition->working = working;
    definition->applied = applied;
    return definition;
}

void game_live_definition_free(game_live_definition *definition)
{
    if (!definition) {
        return;
    }
    blueprint_free(definition->working);
    blueprint_free(definition->applied);
    free(definition->name);
    free(definition);
}

blueprint *game_live_working(game_live_definition *definition)
{
    return definition ? definition->working : NULL;
}

const blueprint *game_live_applied(const game_live_definition *definition)
{
    return definition ? definition->applied : NULL;
}

const char *game_live_definition_name(const game_live_definition *definition)
{
    return definition ? definition->name : NULL;
}

int game_live_definition_dirty(const game_live_definition *definition)
{
    return definition ? definition->dirty : 0;
}

int game_live_mark_dirty(game_live_definition *definition)
{
    if (!definition || !blueprint_mutable(definition->working)) {
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
    char *json = blueprint_save_json(definition->working);
    blueprint *snapshot = json ? blueprint_load_json(json) : NULL;
    free(json);
    if (!snapshot) {
        set_message(message, message_size, "working definition could not be loaded: %s", graph_last_error());
        return -1;
    }
    compile_report report;
    compile_report_init(&report);
    if (blueprint_compile(snapshot, &report) != 0 && compile_report_blocked(&report)) {
        const compile_diagnostic *diagnostic = compile_report_first_error(&report);
        set_message(message, message_size, "apply blocked: %s", diagnostic->message);
        compile_report_free(&report);
        blueprint_free(snapshot);
        return -1;
    }
    compile_report_free(&report);
    blueprint *old = NULL;
    if (game_behavior_replace_asset(world->behaviors, definition->name, snapshot, &old) !=
        0) {
        set_message(message, message_size, "definition replacement failed");
        blueprint_free(snapshot);
        return -1;
    }
    definition->applied = snapshot;
    definition->dirty = 0;
    blueprint_free(old);
    set_message(
        message,
        message_size,
        "applied %s: destroyed and recreated its instances",
        definition->name
    );
    return 0;
}

game_behavior_world *game_live_behaviors(game_live_world *world)
{
    return world ? world->behaviors : NULL;
}
