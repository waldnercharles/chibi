#ifndef CHIBI_RENDER_GPU_CLOCK_H
#define CHIBI_RENDER_GPU_CLOCK_H

#include <stdbool.h>

/* Reports the GPU execution interval of every command buffer the renderer
 * commits to render_gpu_timing, for pass timing and GPU spans. Returns false
 * when the backend has no command buffer timestamps; pass timing then falls
 * back to waiting, and spans report nothing. */
bool render_gpu_clock_install(void);
/* Balances an install that succeeded. The last release restores the backend
 * and waits for outstanding reports before renderer teardown. */
void render_gpu_clock_release(void);

#endif
