#ifndef CHIBI_WHOLE_RECURSION_FIXTURE_H
#define CHIBI_WHOLE_RECURSION_FIXTURE_H
#include "graph/compiler/behavior_function.h"

behavior_function *whole_recursion_ir(bool waits, int limit);
enum
{
    WHOLE_RECURSION_MUTUAL = 1,
    WHOLE_RECURSION_WAIT = 2,
    WHOLE_RECURSION_OUTER = 4,
    WHOLE_RECURSION_VALUES = 8,
    WHOLE_RECURSION_LIBRARY = 16,
    WHOLE_RECURSION_LOOP = 32,
    WHOLE_RECURSION_COUNT = 64
};
behavior_asset *whole_recursion_graph(int kind);
graph *whole_recursion_root(behavior_asset *asset, int kind);
void whole_recursion_add_host(behavior_asset *asset);
void whole_recursion_add_event(behavior_asset *asset, const graph *function);
void whole_recursion_values(behavior_function *function, int mode, bool empty);
void whole_recursion_case(behavior_function *function, int mode, int n, int budget, int depth, bool cancel);
void whole_recursion_smoke();
#endif
