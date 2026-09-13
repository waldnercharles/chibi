#ifndef TEST_WHOLE_REFLECTED_FIXTURE_H
#define TEST_WHOLE_REFLECTED_FIXTURE_H
#include "graph/model/behavior_asset.h"
behavior_asset *whole_reflected_fixture(int kind, bool waits);
void whole_reflected_smoke();
void test_compiled_reflected(const char *path);
void emit_reflected_c(const char *path);
int bench_reflected(const char *path, int count, int rotation);
#endif
