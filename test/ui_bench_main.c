/* The S0 UI bench's headless cases as WASM under Node: from the repository
 * root, `node build-web/ui_bench.js` (test/editor/ui_bench.h). */
#include "test/editor/ui_bench.h"

int main()
{
    return ui_bench_headless();
}
