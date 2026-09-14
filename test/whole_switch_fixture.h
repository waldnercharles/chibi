#ifndef TEST_WHOLE_SWITCH_FIXTURE_H
#define TEST_WHOLE_SWITCH_FIXTURE_H
#include "graph/model/behavior_asset.h"
enum
{
    WHOLE_SWITCH_WAIT = 4,
    WHOLE_SWITCH_LOOP = 8,
    WHOLE_SWITCH_CALL = 16,
    WHOLE_SWITCH_COUNT = 32
};
behavior_asset *whole_switch_fixture(int kind);
graph *whole_switch_root(behavior_asset *asset);
pin_value whole_switch_argument(int kind, int sample);
int whole_switch_expected(int kind, int sample);
void whole_switch_smoke();
void test_compiled_switches(const char *library);
void emit_switch_c(const char *path);
int bench_switch(const char *library, int count, int rotation);
#endif
