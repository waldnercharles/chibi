#ifndef CBLUEPRINT_TEST_EVENT_FIXTURE_H
#define CBLUEPRINT_TEST_EVENT_FIXTURE_H
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/text_file.h"

#include <assert.h>
#include <stdlib.h>
static inline void register_damage_event(game_live_world *world)
{
    char *json = text_file_read("content/events/Damage.json");
    assert(json);
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    assert(asset);
    assert(game_live_add_definition(world, game_live_definition_new(asset->name, asset)) == 0);
}
static inline behavior_asset *link_test_libraries(behavior_asset *asset)
{
    const char *paths[] = { "content/libraries/DirectionMath.json",
                            "content/events/Damage.json" };
    behavior_asset *catalog[2];
    for (int i = 0; i < 2; i++) {
        char *json = text_file_read(paths[i]);
        assert(json);
        catalog[i] = behavior_asset_load_json(json);
        free(json);
        assert(catalog[i]);
    }
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(
        asset,
        (const behavior_asset *const *)catalog,
        2,
        error,
        sizeof error
    );
    assert(linked);
    behavior_asset_free(asset);
    for (int i = 0; i < 2; i++) {
        behavior_asset_free(catalog[i]);
    }
    return linked;
}
static inline void register_test_preview_catalog(game_prototype_preview *preview)
{
    if (!preview) {
        return;
    }
    const char *paths[] = { "content/libraries/DirectionMath.json",
                            "content/events/Damage.json" };
    for (int i = 0; i < 2; i++) {
        char *json = text_file_read(paths[i]);
        assert(json);
        behavior_asset *asset = behavior_asset_load_json(json);
        free(json);
        assert(asset);
        assert(game_prototype_preview_register_behavior(preview, asset->name, asset) == 0);
        behavior_asset_free(asset);
    }
}
#endif
