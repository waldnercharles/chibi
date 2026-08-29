#include "web_loop.h"

#ifdef __EMSCRIPTEN__
extern void emscripten_set_main_loop(void (*frame)(void), int fps, int simulate_infinite_loop);
extern void emscripten_cancel_main_loop(void);

void app_web_set_main_loop(void (*frame)(void))
{
    emscripten_set_main_loop(frame, 0, 1);
}

void app_web_cancel_main_loop(void)
{
    emscripten_cancel_main_loop();
}
#else

void app_web_set_main_loop(void (*)(void)) {}
void app_web_cancel_main_loop(void) {}

#endif
