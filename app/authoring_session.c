#include "authoring_session.h"

#include "../core/registry.h"
#include "../core/serialize.h"
#include "../core/xalloc.h"
#include "../ecs/ecs.h"
#include "../game/live.h"
#include "behavior_fixtures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct app_authoring_session
{
    ecs_world *ecs;
    game_live_world *live;
    game_live_definition *active;
    graph *active_graph;
    char *content_root;
};

app_authoring_session *app_authoring_session_new(const char *content_root)
{
    if (!content_root || !content_root[0]) {
        return NULL;
    }
    app_authoring_session *session = (app_authoring_session *)xcalloc(1, sizeof *session);
    session->content_root = xstrdup(content_root);
    session->ecs = ecs_world_new();
    session->live = session->ecs ? game_live_world_new(session->ecs) : NULL;
    if (!session->live) {
        app_authoring_session_free(session);
        return NULL;
    }
    return session;
}

void app_authoring_session_free(app_authoring_session *session)
{
    if (!session) {
        return;
    }
    game_live_world_free(session->live);
    ecs_world_free(session->ecs);
    free(session->content_root);
    free(session);
}

int app_authoring_session_load_fixtures(app_authoring_session *session)
{
    return session
             ? app_behavior_fixtures_load(session->live, NULL, session->content_root)
             : -1;
}

int app_authoring_session_asset_count(const app_authoring_session *session)
{
    return session ? game_live_definition_count(session->live) : 0;
}

const char *app_authoring_session_asset_name(const app_authoring_session *session, int index)
{
    game_live_definition *definition = session
                                         ? game_live_definition_at(session->live, index)
                                         : NULL;
    return definition ? definition->name : NULL;
}

int app_authoring_session_open_behavior(app_authoring_session *session, const char *name)
{
    game_live_definition *definition = session && name
                                         ? game_live_find_definition(session->live, name)
                                         : NULL;
    if (!definition) {
        return -1;
    }
    session->active = definition;
    session->active_graph = definition->working->graph;
    return 0;
}

int app_authoring_session_create_behavior(app_authoring_session *session, const char *name)
{
    if (!session || !name || !name[0] ||
        game_live_find_definition(session->live, name)) {
        return -1;
    }
    behavior_asset *asset = behavior_asset_new(name);
    game_live_definition *definition = asset ? game_live_definition_new(name, asset)
                                             : NULL;
    if (!definition || game_live_add_definition(session->live, definition) != 0) {
        if (definition) {
            game_live_definition_free(definition);
        } else {
            behavior_asset_free(asset);
        }
        return -1;
    }
    game_live_mark_dirty(definition);
    session->active = definition;
    session->active_graph = asset->graph;
    return 0;
}

const behavior_asset *app_authoring_session_behavior(const app_authoring_session *session)
{
    return session && session->active ? session->active->working : NULL;
}

const graph *app_authoring_session_active_graph(const app_authoring_session *session)
{
    return session ? session->active_graph : NULL;
}

int app_authoring_session_select_graph(app_authoring_session *session, uint64_t graph_id)
{
    if (!session || !session->active) {
        return -1;
    }
    graph *graph = behavior_asset_find_graph(session->active->working, graph_id);
    if (!graph) {
        return -1;
    }
    session->active_graph = graph;
    return 0;
}

graph *app_authoring_session_add_graph(app_authoring_session *session, graph_kind kind, const char *name)
{
    if (!session || !session->active) {
        return NULL;
    }
    graph *graph = behavior_asset_add_graph(session->active->working, kind, name);
    if (graph) {
        game_live_mark_dirty(session->active);
        session->active_graph = graph;
    }
    return graph;
}

uint64_t app_authoring_session_add_node(app_authoring_session *session, const char *name)
{
    if (!session || !session->active_graph || !name) {
        return 0;
    }
    const node_desc *desc = registry_node_by_name(name);
    node *node = desc ? graph_add_node(session->active_graph, desc) : NULL;
    if (!node) {
        return 0;
    }
    game_live_mark_dirty(session->active);
    return node->id;
}

int app_authoring_session_set_pin_default(
    app_authoring_session *session,
    uint64_t node_id,
    int output,
    int index,
    const pin_value *value
)
{
    if (!session || !session->active_graph || !value) {
        return -1;
    }
    node *node = graph_node_by_id(session->active_graph, node_id);
    if (!node ||
        graph_set_pin_default(session->active_graph, node, output, index, value) != 0) {
        return -1;
    }
    game_live_mark_dirty(session->active);
    return 0;
}

int app_authoring_session_connect_data(
    app_authoring_session *session,
    uint64_t from_node,
    int from_pin,
    uint64_t to_node,
    int to_pin
)
{
    if (!session || !session->active_graph ||
        graph_connect_data_ids(session->active_graph, from_node, (uint64_t)from_pin, to_node, (uint64_t)to_pin) !=
            0) {
        return -1;
    }
    game_live_mark_dirty(session->active);
    return 0;
}

int app_authoring_session_compile(app_authoring_session *session)
{
    if (!session || !session->active) {
        return -1;
    }
    return behavior_asset_compile(session->active->working, &session->active->diagnostics);
}

int app_authoring_session_rename_graph(app_authoring_session *session, uint64_t graph_id, const char *name)
{
    if (!session || !session->active ||
        behavior_asset_rename_graph(session->active->working, graph_id, name) != 0) {
        return -1;
    }
    game_live_mark_dirty(session->active);
    return 0;
}

static int write_asset(const behavior_asset *asset, const char *path)
{
    char *json = behavior_asset_save_json(asset);
    if (!json) {
        return -1;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        free(json);
        return -1;
    }
    int result = fputs(json, file) < 0 ? -1 : 0;
    if (fclose(file) != 0) {
        result = -1;
    }
    free(json);
    return result;
}

int app_authoring_session_save(app_authoring_session *session, const char *path)
{
    if (!session || !session->active || !path || !path[0] ||
        write_asset(session->active->working, path) != 0 ||
        game_live_definition_set_path(session->active, path) != 0) {
        return -1;
    }
    session->active->dirty = 0;
    return 0;
}

int app_authoring_session_reload(app_authoring_session *session, const char *path)
{
    if (!session || !path || !path[0]) {
        return -1;
    }
    char *json = NULL;
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) {
            fclose(file);
        }
        return -1;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    json = (char *)xmalloc((size_t)size + 1);
    size_t read = fread(json, 1, (size_t)size, file);
    fclose(file);
    json[read] = '\0';
    behavior_asset *loaded = read == (size_t)size ? behavior_asset_load_json(json)
                                                  : NULL;
    free(json);
    if (!loaded || !session->active ||
        strcmp(loaded->name, session->active->name) != 0) {
        behavior_asset_free(loaded);
        return -1;
    }
    behavior_asset_free(session->active->working);
    session->active->working = loaded;
    behavior_asset_free(session->active->applied);
    char *copy_json = behavior_asset_save_json(loaded);
    session->active->applied = copy_json ? behavior_asset_load_json(copy_json) : NULL;
    free(copy_json);
    session->active->dirty = 0;
    session->active_graph = loaded->graph;
    return session->active->applied ? 0 : -1;
}

int app_authoring_session_dirty(const app_authoring_session *session)
{
    return session && session->active ? session->active->dirty : 0;
}

const compile_report *app_authoring_session_diagnostics(const app_authoring_session *session)
{
    return session && session->active ? &session->active->diagnostics : NULL;
}
