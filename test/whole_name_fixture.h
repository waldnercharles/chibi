#ifndef TEST_WHOLE_NAME_FIXTURE_H
#define TEST_WHOLE_NAME_FIXTURE_H
#include "graph/model/behavior_asset.h"
behavior_asset *whole_name_fixture(bool edge, bool pure, bool waits);
void whole_name_smoke();
void test_compiled_names(const char *path);
void emit_name_c(const char *path);
int bench_names(const char *path, int count, int rotation);
#endif
