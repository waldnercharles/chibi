#ifndef TEST_WHOLE_AGGREGATE_FIXTURE_H
#define TEST_WHOLE_AGGREGATE_FIXTURE_H
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "test/support/reflected_types.h"

extern const registry_pack WHOLE_VALUE_PACK;
extern node_desc WHOLE_NODE_GetRecord, WHOLE_NODE_SetWeight;
void whole_component_smoke();
behavior_asset *whole_aggregate_fixture();
behavior_asset *whole_aggregate_scopes();
behavior_asset *whole_aggregate_identity(bool project);
void whole_aggregate_smoke();
#endif
