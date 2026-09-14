#ifndef TEST_WHOLE_RETRIGGER_FIXTURE_H
#define TEST_WHOLE_RETRIGGER_FIXTURE_H
#include "graph/model/behavior_asset.h"
enum
{
    WHOLE_RETRIGGER_DIRECT,
    WHOLE_RETRIGGER_CALL,
    WHOLE_RETRIGGER_NESTED,
    WHOLE_RETRIGGER_LOOP,
    WHOLE_RETRIGGER_RECURSIVE,
    WHOLE_RETRIGGER_COUNT
};
behavior_asset *whole_retrigger_fixture(int kind);
void whole_retrigger_smoke();
void test_compiled_retrigger(const char *library);
void emit_retrigger_c(const char *path);
#endif
