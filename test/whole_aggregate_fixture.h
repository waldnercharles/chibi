#ifndef TEST_WHOLE_AGGREGATE_FIXTURE_H
#define TEST_WHOLE_AGGREGATE_FIXTURE_H
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"

typedef struct
{
    float x, y;
} whole_pair;
typedef struct
{
    uint8_t tag;
    whole_pair position;
    double weight;
    bool active;
    int64_t stamp;
} whole_record;
extern const registry_pack WHOLE_VALUE_PACK;
behavior_asset *whole_aggregate_fixture();
behavior_asset *whole_aggregate_scopes();
behavior_asset *whole_aggregate_identity(bool project);
void whole_aggregate_smoke();
#endif
