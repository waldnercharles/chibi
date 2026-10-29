#include "authoring/behavior_loader.h"

#include "authoring/preview_source.h"

#include <assert.h>
#include <stdio.h>

static void load_error(const char *path, const char *stage, const char *message)
{
    fprintf(stderr, "Could not load graph asset %s (%s): %s\n", path, stage, message);
}

static int load_sources(preview_world *world, preview_world *second, const char *root)
{
    preview_world *worlds[] = { world, second };
    preview_source_staging *stages[2] = { 0 };
    char error[256];
    for (int i = 0; i < 2; i++) {
        if (!worlds[i]) {
            continue;
        }
        if (preview_sources_prepare(worlds[i], root, behavior_default_backend(), error, sizeof error)) {
            load_error(root, "source preparation", error);
            goto fail;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (!worlds[i]) {
            continue;
        }
        stages[i] = preview_sources_stage(worlds[i], NULL, error, sizeof error);
        if (!stages[i]) {
            load_error(root, "source publication", error);
            goto fail;
        }
    }
    int loaded = authoring_sources_count(preview_sources_working(world));
    for (int i = 0; i < 2; i++) {
        if (stages[i]) {
            preview_sources_commit(stages[i]);
        }
    }
    return loaded;
fail:
    for (int i = 0; i < 2; i++) {
        if (stages[i]) {
            preview_sources_discard(stages[i]);
        }
    }
    return -1;
}

int app_behavior_fixtures_load(preview_world *world, prototype_preview *preview, const char *root)
{
    /* Each world takes each definition once. */
    assert(!preview || prototype_preview_live(preview) != world);
    return load_sources(world, preview ? prototype_preview_live(preview) : NULL, root);
}
