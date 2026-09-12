#ifndef TEST_WHOLE_LOOP_FIXTURE_H
#define TEST_WHOLE_LOOP_FIXTURE_H
#include "graph/model/behavior_asset.h"
enum
{
    WHOLE_LOOP_RANGE,
    WHOLE_LOOP_BREAK,
    WHOLE_LOOP_NESTED,
    WHOLE_LOOP_WHILE,
    WHOLE_LOOP_OUTSIDE,
    WHOLE_LOOP_CALL,
    WHOLE_LOOP_RETURN,
    WHOLE_LOOP_SNAPSHOT,
    WHOLE_LOOP_EMPTY,
    WHOLE_LOOP_NESTED_CALL,
    WHOLE_LOOP_COUNT
};
behavior_asset *whole_loop_fixture(int kind);
void whole_loop_smoke();
#endif
