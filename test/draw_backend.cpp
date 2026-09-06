#include <stdint.h>

/* These diagnostic functions have C++ linkage in Cute's internal draw API. */
void cf_draw_set_tiled_enabled(bool enabled);
bool cf_draw_tiled_available();
void cf_draw_set_tiled_auto();
void cf_draw_tiled_stats(int *tiled, int *instanced, uint64_t *bytes);

extern "C" void test_draw_set_tiled(bool enabled)
{
    cf_draw_set_tiled_enabled(enabled);
}

extern "C" bool test_draw_tiled_available()
{
    return cf_draw_tiled_available();
}

extern "C" void test_draw_tiled_auto()
{
    cf_draw_set_tiled_auto();
}

extern "C" void test_draw_tiled_stats(int *tiled, int *instanced)
{
    cf_draw_tiled_stats(tiled, instanced, 0);
}
