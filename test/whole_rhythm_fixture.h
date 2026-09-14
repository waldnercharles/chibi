#ifndef TEST_WHOLE_RHYTHM_FIXTURE_H
#define TEST_WHOLE_RHYTHM_FIXTURE_H
#include "graph/model/behavior_asset.h"
behavior_asset *whole_rhythm_fixture(bool waits);
void whole_rhythm_smoke();
void test_compiled_rhythm(const char *path);
void emit_rhythm_c(const char *path);
int bench_rhythm(const char *path, int count, int rotation);
#endif
