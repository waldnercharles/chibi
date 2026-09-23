#ifndef CHIBI_RENDER_GPU_TIMING_H
#define CHIBI_RENDER_GPU_TIMING_H

#include <stdbool.h>

/* GPU pass timing without vendor tools. While enabled, each mark submits the
 * work recorded since the previous mark and waits for the GPU to finish it.
 * That serializes the CPU and GPU, so whole frames slow down; per-pass times
 * remain comparable between runs. Marks cost nothing while disabled. */

enum
{
    RENDER_GPU_TIMING_PASSES = 32
};

typedef struct
{
    const char *name;
    double total_ms;
    int count;
} render_gpu_pass;

/* Enabling or disabling clears the accumulated passes. */
void render_gpu_timing_enable(bool enabled);
bool render_gpu_timing_enabled(void);
/* Waits for the work already submitted; the next mark times from here. */
void render_gpu_timing_start(void);
/* name must be a string literal: passes are keyed by it. */
void render_gpu_timing_mark(const char *name);
/* Clears the totals while keeping timing enabled. */
void render_gpu_timing_clear(void);
const render_gpu_pass *render_gpu_timing_passes(int *count);

#endif
