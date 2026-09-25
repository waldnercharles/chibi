#include "render/gpu_timing.h"

#include <cute.h>
#include <string.h>
#include <time.h>

static struct
{
    bool enabled;
    double last;
    render_gpu_pass passes[RENDER_GPU_TIMING_PASSES];
    int count;
} g_timing;

static double milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1e3 + now.tv_nsec * 1e-6;
}

void render_gpu_timing_enable(bool enabled)
{
    memset(&g_timing, 0, sizeof g_timing);
    g_timing.enabled = enabled;
}

void render_gpu_timing_start(void)
{
    if (!g_timing.enabled) {
        return;
    }
    cf_gpu_sync();
    g_timing.last = milliseconds();
}

void render_gpu_timing_mark(const char *name)
{
    if (!g_timing.enabled) {
        return;
    }
    cf_gpu_sync();
    double now = milliseconds();
    int i = 0;
    while (i < g_timing.count && strcmp(g_timing.passes[i].name, name)) {
        i++;
    }
    if (i == g_timing.count && g_timing.count < RENDER_GPU_TIMING_PASSES) {
        g_timing.passes[g_timing.count++] = (render_gpu_pass){ .name = name };
    }
    if (i < g_timing.count) {
        g_timing.passes[i].total_ms += now - g_timing.last;
        g_timing.passes[i].count++;
    }
    g_timing.last = now;
}

void render_gpu_timing_clear(void)
{
    g_timing.count = 0;
}

const render_gpu_pass *render_gpu_timing_passes(int *count)
{
    *count = g_timing.count;
    return g_timing.passes;
}
