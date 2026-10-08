#include "authoring/behavior_loader.h"

#include "authoring/graph_loader_internal.h"
#include "authoring/preview_source.h"

#include <stdio.h>

static void load_error(const char *path, const char *stage, const char *message)
{
    fprintf(stderr, "Could not load graph asset %s (%s): %s\n", path, stage, message);
}

static int load_sources(game_preview_world *world, game_preview_world *second, const char *root)
{
    game_preview_world *worlds[] = { world, second };
    game_preview_source_staging *stages[2] = { 0 };
    char error[256];
    for (int i = 0; i < 2; i++) {
        if (!worlds[i] || (i && worlds[i] == worlds[0])) {
            continue;
        }
        if (game_preview_sources_prepare(worlds[i], root, behavior_default_backend(), error, sizeof error)) {
            load_error(root, "source preparation", error);
            goto fail;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (!worlds[i] || (i && worlds[i] == worlds[0])) {
            continue;
        }
        stages[i] = game_preview_sources_stage(worlds[i], NULL, error, sizeof error);
        if (!stages[i]) {
            load_error(root, "source publication", error);
            goto fail;
        }
    }
    int loaded = authoring_sources_count(game_preview_sources_working(world));
    for (int i = 0; i < 2; i++) {
        if (stages[i]) {
            game_preview_sources_commit(stages[i]);
        }
    }
    return loaded;
fail:
    for (int i = 0; i < 2; i++) {
        if (stages[i]) {
            game_preview_sources_discard(stages[i]);
        }
    }
    return -1;
}

int app_behavior_fixtures_load(game_preview_world *world, game_prototype_preview *preview, const char *root)
{
    int graphs = authoring_graphs_prepare(world, preview, root);
    if (graphs < 0) {
        return -1;
    }
    int sources = load_sources(world, preview ? game_prototype_preview_live(preview) : NULL, root);
    return sources < 0 ? -1 : graphs + sources;
}
