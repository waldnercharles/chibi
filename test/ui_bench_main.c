#include "graph/model/registry.h"
/* The S0 UI bench's headless cases as WASM under Node: from the repository
 * root, `node build-web/ui_bench.js` (test/editor/ui_bench.h). */
#include "test/editor/ui_bench.h"

int main()
{
    registry_set_packs(REGISTRY_PACKS, REGISTRY_PACK_COUNT);
    return ui_bench_headless();
}
