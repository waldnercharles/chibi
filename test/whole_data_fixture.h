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
enum
{
    WHOLE_READ_BRANCH,
    WHOLE_READ_JOIN,
    WHOLE_READ_WRITE,
    WHOLE_READ_WAIT,
    WHOLE_READ_LOOP,
    WHOLE_READ_INPUT_EFFECT,
    WHOLE_READ_COMPONENT,
    WHOLE_READ_KINDS
};
behavior_asset *whole_read_branch_fixture(int kind);
void whole_read_branch_check(behavior_asset *asset, int kind, bool observed);
void whole_read_branch_smoke();
#endif
