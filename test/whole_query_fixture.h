#ifndef TEST_WHOLE_QUERY_FIXTURE_H
#define TEST_WHOLE_QUERY_FIXTURE_H
#include "graph/model/behavior_asset.h"
enum
{
    WHOLE_QUERY_RANGE,
    WHOLE_QUERY_REPEAT,
    WHOLE_QUERY_NESTED,
    WHOLE_QUERY_BREAK,
    WHOLE_QUERY_RETURN,
    WHOLE_QUERY_CALL,
    WHOLE_QUERY_PURE,
    WHOLE_QUERY_COUNT
};
behavior_asset *whole_query_fixture(const node_desc *query, const char *key, int kind);
void whole_query_smoke();
#endif
