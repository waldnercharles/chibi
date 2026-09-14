#ifndef TEST_WHOLE_REACTION_FIXTURE_H
#define TEST_WHOLE_REACTION_FIXTURE_H
#include "graph/model/behavior_asset.h"
behavior_asset *whole_reaction_fixture(bool waits, bool target);
void whole_reaction_smoke();
void test_compiled_reaction(const char *path);
void emit_reaction_c(const char *path);
int bench_reaction(const char *path, int count, int rotation);
#endif
