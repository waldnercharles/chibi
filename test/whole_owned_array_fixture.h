#ifndef TEST_WHOLE_OWNED_ARRAY_FIXTURE_H
#define TEST_WHOLE_OWNED_ARRAY_FIXTURE_H
#include "graph/model/behavior_asset.h"
enum
{
    OWNED_INTS,
    OWNED_STRINGS,
    OWNED_STRUCTS,
    OWNED_KINDS
};
behavior_asset *whole_owned_array_fixture(int kind, bool waits);
void whole_owned_array_smoke();
void test_owned_arrays(const char *path);
void emit_owned_array_c(const char *path);
int bench_owned_arrays(const char *path, int count, int rotation);
#endif
