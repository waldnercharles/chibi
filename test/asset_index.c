#include "editor/workspace/asset_index.h"

#include "editor/ui/search.h"

#include <assert.h>
#include <string.h>

void test_asset_index()
{
    asset_index *index = asset_index_new();
    int prototype = asset_index_add_asset(
        index,
        ASSET_INDEX_PROTOTYPE,
        "ArenaPlayer",
        "content/prototypes/ArenaPlayer.json",
        0
    );
    int material = asset_index_add_asset(
        index,
        ASSET_INDEX_MATERIAL,
        "ArenaPlayer",
        "content/materials/ArenaPlayer.json",
        0
    );
    assert(prototype >= 0 && material >= 0 && prototype != material);
    assert(asset_index_find_name(index, ASSET_INDEX_PROTOTYPE, "ArenaPlayer") == prototype);
    assert(asset_index_find_name(index, ASSET_INDEX_MATERIAL, "ArenaPlayer") == material);
    assert(
        asset_index_find(index, ASSET_INDEX_PROTOTYPE, "./content/prototypes/ArenaPlayer.json", 0) == prototype
    );

    int event = asset_index_ensure_name(index, ASSET_INDEX_EVENT, "Damaged");
    assert(event >= 0);
    assert(asset_index_asset_at(index, event)->unresolved);
    int behavior = asset_index_add_asset(
        index,
        ASSET_INDEX_BEHAVIOR,
        "HealthBehavior",
        "content/behaviors/HealthBehavior.json",
        0
    );
    assert(
        asset_index_add_reference(
            index,
            (asset_index_reference){
                .kind = ASSET_INDEX_EVENT_CONSUMER,
                .source = behavior,
                .target = event,
                .detail = "Default",
                .graph_id = 10,
                .node_id = 20,
            }
        ) == 0
    );
    assert(asset_index_reference_count(index) == 1);

    int resolved = asset_index_add_asset(index, ASSET_INDEX_EVENT, "Damaged", "content/events/Damaged.json", 0);
    assert(resolved == event);
    assert(!asset_index_asset_at(index, event)->unresolved);
    assert(asset_index_score(asset_index_asset_at(index, event), "dam") >= 0);

    const asset_index_asset *player = asset_index_asset_at(index, prototype);
    assert(asset_index_score(player, "player arena") >= 0);
    assert(asset_index_score(player, "prototypes player") >= 0);
    assert(asset_index_score(player, "AP") >= 0);
    assert(asset_index_score(player, "playre") >= 0);
    assert(asset_index_score(player, "player missing") < 0);
    assert(editor_search_score("  ", "Player", NULL) == 0);
    assert(
        editor_search_score(" \tplayer  ", "Player", NULL) ==
        editor_search_score("player", "Player", NULL)
    );
    assert(
        editor_search_score("player", "Player", NULL) >
        editor_search_score("player", "PlayerMovement", NULL)
    );
    assert(
        editor_search_score("player", "PlayerMovement", NULL) >
        editor_search_score("player", "ArenaPlayer", NULL)
    );
    assert(
        editor_search_score("player", "ArenaPlayer", NULL) >
        editor_search_score("player", "PlaybackRenderer", NULL)
    );
    assert(
        editor_search_score("player", "Player", NULL) >
        editor_search_score("player", "Spawn", "player")
    );
    assert(editor_search_score("lerp", "Linear Interpolate", "blend lerp") >= 0);
    assert(editor_search_score("math blend", "Linear Interpolate", "Math blend lerp") >= 0);
    assert(
        editor_search_score("aevb", "ArenaEnemyVisualBehavior", NULL) >
        editor_search_score("aevb", "ArenaEveryVerb", NULL)
    );
    assert(editor_search_score("playr", "Player", NULL) >= 0);
    assert(editor_search_score("playyer", "Player", NULL) >= 0);
    assert(editor_search_score("playar", "Player", NULL) >= 0);
    assert(editor_search_score("plsyr", "Player", NULL) < 0);
    assert(
        editor_search_score("player arena", "PlayerContactBehavior", "content/behaviors/PlayerContactBehavior.json") <
        0
    );
    assert(editor_search_score("cat", "Cast", NULL) >= 0);
    assert(editor_search_score("cot", "Cat", NULL) < 0);

    uint64_t session = asset_index_session_id(index, ASSET_INDEX_BEHAVIOR, "behavior:Scratch");
    assert(session != 0);
    assert(asset_index_session_id(index, ASSET_INDEX_BEHAVIOR, "behavior:Scratch") == session);
    int scratch = asset_index_add_asset(index, ASSET_INDEX_BEHAVIOR, "Scratch", NULL, session);
    assert(asset_index_find(index, ASSET_INDEX_BEHAVIOR, NULL, session) == scratch);
    int pending = asset_index_ensure_name(index, ASSET_INDEX_BEHAVIOR, "PendingBehavior");
    uint64_t pending_session = asset_index_session_id(index, ASSET_INDEX_BEHAVIOR, "behavior:pending");
    assert(
        asset_index_add_asset(index, ASSET_INDEX_BEHAVIOR, "PendingBehavior", NULL, pending_session) == pending
    );
    assert(!asset_index_asset_at(index, pending)->unresolved);
    asset_index_clear(index);
    assert(asset_index_session_id(index, ASSET_INDEX_BEHAVIOR, "behavior:Scratch") == session);
    asset_index_free(index);
}
