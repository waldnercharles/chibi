#ifndef TEST_WHOLE_STORE_FIXTURE_H
#define TEST_WHOLE_STORE_FIXTURE_H
#include "test/whole_aggregate_fixture.h"
enum
{
    WHOLE_STORE_COPY,
    WHOLE_STORE_MAKE,
    WHOLE_STORE_SNAPSHOT,
    WHOLE_STORE_LOOP,
    WHOLE_STORE_COUNT
};
behavior_asset *whole_store_fixture(int kind);
void whole_store_smoke();
#endif
