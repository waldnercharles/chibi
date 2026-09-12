#ifndef TEST_WHOLE_ALIAS_FIXTURE_H
#define TEST_WHOLE_ALIAS_FIXTURE_H
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

behavior_asset *whole_alias_fixture(const node_desc *conversion, bool edge, bool pure);
extern const registry_pack WHOLE_ALIAS_PACK;
void whole_alias_smoke();
#endif
