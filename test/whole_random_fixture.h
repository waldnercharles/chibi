#ifndef TEST_WHOLE_RANDOM_FIXTURE_H
#define TEST_WHOLE_RANDOM_FIXTURE_H
#include "graph/model/behavior_asset.h"

enum
{
    WHOLE_RANDOM_UNIT,
    WHOLE_RANDOM_RANGE,
    WHOLE_RANDOM_VECTOR,
    WHOLE_RANDOM_PROJECT,
    WHOLE_RANDOM_COUNT
};
behavior_asset *whole_random_fixture(int kind);
void whole_random_smoke();
void test_compiled_random(const char *path);
void emit_random_c(const char *path);
int bench_random(const char *path, int count, int rotation);
#endif
