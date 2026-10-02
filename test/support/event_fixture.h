#ifndef CHIBI_TEST_EVENT_FIXTURE_H
#define CHIBI_TEST_EVENT_FIXTURE_H
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "runtime/behaviors/live.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/text_file.h"

#include <assert.h>
#include <stdlib.h>
static inline void register_damage_event(game_live_world *world)
{
    const char *paths[] = { "content/events/Damage.kdl",
                            "content/events/Damaged.kdl",
                            "content/events/Died.kdl",
                            "content/events/RefreshEncounter.kdl",
                            "content/events/CollectExperience.kdl" };
    for (int i = 0; i < (int)(sizeof paths / sizeof *paths); i++) {
        char *json = text_file_read(paths[i]);
        assert(json);
        behavior_asset *asset = behavior_asset_load(json);
        free(json);
        assert(asset);
        assert(game_live_add_definition(world, game_live_definition_new(asset->name, asset)) == 0);
    }
}
static inline behavior_asset *link_test_libraries(behavior_asset *asset)
{
    const char *paths[] = { "content/libraries/DirectionMath.kdl",
                            "content/libraries/MovementActions.kdl",
                            "content/libraries/WeaponActions.kdl",
                            "content/shader-libraries/ShaderMath.kdl",
                            "content/events/Damage.kdl",
                            "content/events/Damaged.kdl",
                            "content/events/Died.kdl",
                            "content/events/RefreshEncounter.kdl",
                            "content/events/CollectExperience.kdl" };
    enum
    {
        count = sizeof paths / sizeof *paths
    };
    behavior_asset *catalog[count];
    for (int i = 0; i < count; i++) {
        char *json = text_file_read(paths[i]);
        assert(json);
        catalog[i] = behavior_asset_load(json);
        free(json);
        assert(catalog[i]);
    }
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(
        asset,
        (const behavior_asset *const *)catalog,
        count,
        error,
        sizeof error
    );
    assert(linked);
    behavior_asset_free(asset);
    for (int i = 0; i < count; i++) {
        behavior_asset_free(catalog[i]);
    }
    return linked;
}
static inline void register_test_preview_catalog(game_prototype_preview *preview)
{
    if (!preview) {
        return;
    }
    const char *paths[] = { "content/libraries/DirectionMath.kdl",
                            "content/libraries/MovementActions.kdl",
                            "content/libraries/WeaponActions.kdl",
                            "content/events/Damage.kdl",
                            "content/events/Damaged.kdl",
                            "content/events/Died.kdl",
                            "content/events/RefreshEncounter.kdl",
                            "content/events/CollectExperience.kdl" };
    for (int i = 0; i < (int)(sizeof paths / sizeof *paths); i++) {
        char *json = text_file_read(paths[i]);
        assert(json);
        behavior_asset *asset = behavior_asset_load(json);
        free(json);
        assert(asset);
        assert(game_prototype_preview_register_behavior(preview, asset->name, asset) == 0);
        behavior_asset_free(asset);
    }
}
#endif
