#include "assets/behavior_loader.h"

#include "graph/document/serialize.h"
#include "graph/model/registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_asset(const char *directory, const char *name, const char *json)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.yaml", directory, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(json, file) >= 0);
    assert(fclose(file) == 0);
}

void run_behavior_loader_checks()
{
    char root[] = "/tmp/chibi-loader-XXXXXX";
    assert(mkdtemp(root));
    char directory[256];
    snprintf(directory, sizeof directory, "%s/behaviors", root);
    assert(mkdir(directory, 0700) == 0);
    behavior_asset *asset = behavior_asset_new("Good");
    char *json = behavior_asset_save_json(asset);
    write_asset(directory, "Good", json);
    write_asset(directory, "Duplicate", json);
    free(json);
    behavior_asset_free(asset);
    write_asset(directory, "Broken", "name: [Broken\n");

    asset = behavior_asset_new("Unknown");
    graph_add_node(asset->graph, registry_node_by_name("Integer"));
    json = behavior_asset_save_json(asset);
    char *type = strstr(json, "\"Integer\"");
    assert(type);
    memcpy(type, "\"Missing\"", strlen("\"Missing\""));
    write_asset(directory, "Unknown", json);
    free(json);
    behavior_asset_free(asset);

    asset = behavior_asset_new("MissingLibrary");
    node *call = graph_add_node(asset->graph, &NODE_CallFunction);
    assert(graph_set_node_reference(asset->graph, call, 1, "Missing") == 0);
    call->reference_asset = graph_own_str(asset->graph, "AbsentLibrary", strlen("AbsentLibrary"));
    json = behavior_asset_save_json(asset);
    write_asset(directory, "MissingLibrary", json);
    free(json);
    behavior_asset_free(asset);

    ecs_world *ecs = ecs_world_new();
    game_live_world *world = game_live_world_new(ecs);
    game_prototype_preview *preview = game_prototype_preview_new(
        &(game_prototype_registry){ 0 }
    );
    asset = behavior_asset_new("PreviewFailed");
    assert(game_prototype_preview_register_behavior(preview, asset->name, asset) == 0);
    json = behavior_asset_save_json(asset);
    write_asset(directory, "PreviewFailed", json);
    free(json);
    behavior_asset_free(asset);

    FILE *capture = tmpfile();
    assert(capture);
    fflush(stderr);
    int previous = dup(STDERR_FILENO);
    assert(previous >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0);
    int loaded = app_behavior_fixtures_load(world, preview, root);
    fflush(stderr);
    assert(dup2(previous, STDERR_FILENO) >= 0);
    close(previous);
    rewind(capture);
    char messages[4096] = { 0 };
    size_t count = fread(messages, 1, sizeof messages - 1, capture);
    messages[count] = '\0';
    fclose(capture);

    assert(loaded == 1 && game_live_find_definition(world, "Good"));
    assert(!game_live_find_definition(world, "PreviewFailed"));
    assert(strstr(messages, "Broken.yaml (deserialize): line "));
    assert(strstr(messages, "Unknown.yaml (deserialize): unknown node type \"Missing\""));
    assert(strstr(messages, "Good.yaml (register): A definition with this name is already registered."));
    assert(strstr(messages, "MissingLibrary.yaml (register): AbsentLibrary:"));
    assert(
        strstr(messages, "Could not register preview behavior PreviewFailed: A definition with this name")
    );
    assert(strstr(messages, "PreviewFailed.yaml (preview):"));
    game_prototype_preview_free(preview);
    game_live_world_free(world);
    ecs_world_free(ecs);

    const char *names[] = { "Good",    "Duplicate",      "Broken",
                            "Unknown", "MissingLibrary", "PreviewFailed" };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.yaml", directory, names[i]);
        assert(unlink(path) == 0);
    }
    assert(rmdir(directory) == 0 && rmdir(root) == 0);
    puts("PASS: rejected graph assets preserve file and failure details while valid assets load");
}
