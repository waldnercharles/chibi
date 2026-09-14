#ifndef TEST_WHOLE_SEARCH_FIXTURE_H
#define TEST_WHOLE_SEARCH_FIXTURE_H
#include "graph/model/behavior_asset.h"
extern const struct registry_pack WHOLE_SEARCH_PACK;
behavior_asset *whole_search_fixture(pin_type type, bool contains, bool waits);
behavior_asset *whole_search_loop_fixture();
void whole_search_smoke();
void test_compiled_search(const char *path);
void emit_search_c(const char *path);
int bench_search(const char *path, int count, int rotation);
#endif
