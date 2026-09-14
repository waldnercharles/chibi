#ifndef TEST_WHOLE_DATA_FIXTURE_H
#define TEST_WHOLE_DATA_FIXTURE_H
#include "graph/model/behavior_asset.h"
#include "test/whole_aggregate_fixture.h"
typedef struct
{
    int calls, fail_at;
    double bias;
} whole_data_service;
behavior_asset *whole_data_fixture(const node_desc *desc, bool event_arguments);
behavior_asset *whole_data_nested(bool event_arguments);
void whole_data_smoke();
#endif
