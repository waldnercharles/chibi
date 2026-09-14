#ifndef TEST_WHOLE_BATCH_FIXTURE_H
#define TEST_WHOLE_BATCH_FIXTURE_H
#include "graph/model/behavior_asset.h"

/* Begin initializes a counter. Tick calls a function that increments it by
 * Delta Time and optionally copies self.weight + Delta Time to Target.weight. */
behavior_asset *whole_batch_fixture(bool components);
void whole_batch_smoke();
void whole_bound_components_smoke();
#endif
