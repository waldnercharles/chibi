#include "graph/compiler/graph_jit_sljit.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"
#include "runtime/behaviors/entity_query.h"
#include "runtime/rhythm.h"
#include "runtime/visual_motion.h"
#include "test/pending_fixture.h"
#include "test/whole_reaction_fixture.h"
#include "test/whole_reflected_fixture.h"
#include "test/whole_rhythm_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

extern const registry_pack GAME_REGISTRY_PACK;
extern void test_physics();
static const type_desc entity_type = { .name = "ecs.entity", .size = sizeof(uint64_t) };
static const type_desc *const types[] = { &entity_type };
static node_desc *const nodes[] = { &NODE_Set_Visual_Reaction,
                                    &NODE_Scene_Rhythm,
                                    &NODE_Count_Matching_Entities };
static const registry_pack capabilities = { .name = "Runtime smoke",
                                            .types = types,
                                            .type_count = 1,
                                            .nodes = nodes,
                                            .node_count = 3 };
const registry_pack *const REGISTRY_PACKS[] = { &CORE_EXTRA_PACK,
                                                &CORE_FLOW_PACK,
                                                &GAME_REGISTRY_PACK,
                                                &capabilities };
const int REGISTRY_PACK_COUNT = 4;

int type_catalog_pack_count()
{
    return REGISTRY_PACK_COUNT;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    const registry_pack *pack = REGISTRY_PACKS[index];
    return (type_catalog_pack){ pack->types, pack->type_count, pack->enums, pack->enum_count };
}
int main()
{
    registry_set_packs(REGISTRY_PACKS, REGISTRY_PACK_COUNT);
#ifdef __EMSCRIPTEN__
    assert(!graph_sljit_available() && behavior_default_backend() == BEHAVIOR_BYTECODE);
#else
    if (!graph_sljit_available()) {
        behavior_asset *asset = whole_reaction_fixture(false, false);
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        float rotation[] = { 1, 2, 3 };
        pin_value args[] = { { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = rotation },
                             { .type = PIN_F32, .f = 4 },
                             { .type = PIN_HANDLE, .type_name = "ecs.entity" } };
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ args, 3 }, &error);
        assert(!run && error.code == FLOW_ERR_COMPILE && strstr(error.message, "requires SLJIT"));
        assert(behavior_instance_backend(instance) == BEHAVIOR_UNAVAILABLE);
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
        puts("UNAVAILABLE: runtime capability rejected without interpreter fallback");
        return 0;
    }
    assert(behavior_default_backend() == BEHAVIOR_SLJIT);
#endif
    whole_reaction_smoke();
    whole_rhythm_smoke();
    whole_reflected_smoke();
    test_pending_backend(behavior_default_backend());
    extern void test_entity_query(behavior_backend backend);
    test_entity_query(behavior_default_backend());
    test_physics();
    return 0;
}
