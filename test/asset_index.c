#include "editor/workspace/asset_index.h"

#include "editor/ui/search.h"
#include "graph/model/behavior_asset.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

static int count_uses(const asset_index *index, asset_index_target target, uint64_t node_id)
{
    const asset_index_reference **uses = NULL;
    asset_index_find_uses(index, target, &uses);
    int count = acount(uses);
    for (int i = 0; i < count; i++) {
        assert(uses[i]->node_id == node_id);
    }
    afree(uses);
    return count;
}

/* Find References reaches a function's calls and a variable's nodes and
   bound inputs within their own asset, and an event's handlers elsewhere. */
static void test_find_uses()
{
    behavior_asset *asset = behavior_asset_new("Healer");
    graph *heal = behavior_asset_add_function(asset, "Heal", false);
    behavior_variable *health = behavior_asset_add_variable(
        asset,
        "health",
        PIN_I32,
        NULL,
        PIN_CONTAINER_SINGLE,
        NULL,
        NULL
    );
    node *call = behavior_asset_add_function_call_node(asset, heal);
    node *get = behavior_asset_add_get_node(asset, health->id);
    assert(call && get);
    asset_index *index = asset_index_new();
    int healer = asset_index_add_asset(index, ASSET_INDEX_BEHAVIOR, "Healer", "content/behaviors/Healer.yaml", 0);
    int event = asset_index_ensure_name(index, ASSET_INDEX_EVENT, "Healed");
    asset_index_add_reference(
        index,
        (asset_index_reference){ .kind = ASSET_INDEX_EVENT_CONSUMER,
                                 .source = healer,
                                 .target = event,
                                 .graph_id = asset->graph->graph_id,
                                 .node_id = 99 }
    );
    asset_index_add_graph_references(index, healer, asset);
    assert(
        count_uses(
            index,
            (asset_index_target){ healer, ASSET_INDEX_SYMBOL_FUNCTION, heal->graph_id },
            call->id
        ) == 1
    );
    assert(
        count_uses(
            index,
            (asset_index_target){ healer, ASSET_INDEX_SYMBOL_VARIABLE, health->id },
            get->id
        ) == 1
    );
    assert(
        count_uses(index, (asset_index_target){ event, ASSET_INDEX_SYMBOL_ASSET, 0 }, 99) == 1
    );
    /* An asset's own calls and variables are not uses of the asset. */
    assert(
        count_uses(index, (asset_index_target){ healer, ASSET_INDEX_SYMBOL_ASSET, 0 }, 0) == 0
    );
    asset_index_free(index);
    behavior_asset_free(asset);
}

void test_asset_index()
{
    asset_index *index = asset_index_new();
    int prototype = asset_index_add_asset(
        index,
        ASSET_INDEX_PROTOTYPE,
        "ArenaPlayer",
        "content/prototypes/ArenaPlayer.yaml",
        0
    );
    int material = asset_index_add_asset(
        index,
        ASSET_INDEX_MATERIAL,
        "ArenaPlayer",
        "content/materials/ArenaPlayer.yaml",
        0
    );
    assert(prototype >= 0 && material >= 0 && prototype != material);
    assert(asset_index_find_name(index, ASSET_INDEX_PROTOTYPE, "ArenaPlayer") == prototype);
    assert(asset_index_find_name(index, ASSET_INDEX_MATERIAL, "ArenaPlayer") == material);
    assert(
        asset_index_find(index, ASSET_INDEX_PROTOTYPE, "./content/prototypes/ArenaPlayer.yaml", 0) == prototype
    );

    int event = asset_index_ensure_name(index, ASSET_INDEX_EVENT, "Damaged");
    assert(event >= 0);
    assert(asset_index_asset_at(index, event)->unresolved);
    int behavior = asset_index_add_asset(
        index,
        ASSET_INDEX_BEHAVIOR,
        "HealthBehavior",
        "content/behaviors/HealthBehavior.yaml",
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

    int resolved = asset_index_add_asset(index, ASSET_INDEX_EVENT, "Damaged", "content/events/Damaged.yaml", 0);
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
        editor_search_score("player arena", "PlayerContactBehavior", "content/behaviors/PlayerContactBehavior.yaml") <
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
    test_find_uses();
}
