#ifndef TEST_WHOLE_SUSPEND_FIXTURE_H
#define TEST_WHOLE_SUSPEND_FIXTURE_H
#include "graph/compiler/behavior_function.h"

/* Compiler-boundary stress fixture: insert yields into a real lowered graph. */
void whole_suspend_instrument(behavior_function *function, bool expressions);
behavior_asset *whole_suspend_float_fixture(pin_type type);
behavior_function *whole_suspend_float_ir(pin_type type);
void whole_suspend_smoke();
#endif
