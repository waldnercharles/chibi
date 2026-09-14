#ifndef TEST_WHOLE_ARRAY_FIXTURE_H
#define TEST_WHOLE_ARRAY_FIXTURE_H
#include "test/whole_aggregate_fixture.h"
enum
{
    WHOLE_ARRAY_RANGE,
    WHOLE_ARRAY_BREAK,
    WHOLE_ARRAY_RETURN,
    WHOLE_ARRAY_NESTED,
    WHOLE_ARRAY_CALL,
    WHOLE_ARRAY_STRUCT,
    WHOLE_ARRAY_VARIABLE,
    WHOLE_ARRAY_EMPTY_BODY,
    WHOLE_ARRAY_SUMMARY,
    WHOLE_ARRAY_MATH,
    WHOLE_ARRAY_COUNT
};
behavior_asset *whole_array_fixture(int kind);
void whole_array_smoke();
#endif
