#ifndef CHIBI_RENDER_GPU_TIMING_H
#define CHIBI_RENDER_GPU_TIMING_H

#include <stdbool.h>

/* GPU pass timing without vendor tools. Each mark ends a pass at the work
 * recorded since the previous mark. Marks cost nothing while disabled.
 *
 * Sync mode waits for the GPU at every mark. That serializes the CPU and GPU,
 * so whole frames slow down and passes include submission latency.
 *
 * Timestamp mode submits at every mark without waiting. The host reports the
 * GPU execution interval of every command buffer it commits; a pass is charged
 * the part of its command buffers' interval that follows the end of all earlier
 * work, so idle gaps and overlap are not counted twice. Command buffers that
 * no mark covers (presentation, UI) accumulate in the "Unmarked" pass. */

enum
{
    RENDER_GPU_TIMING_PASSES = 96
};

typedef enum
{
    RENDER_GPU_TIMING_OFF,
    RENDER_GPU_TIMING_SYNC,
    RENDER_GPU_TIMING_TIMESTAMPS,
} render_gpu_timing_mode;

typedef struct
{
    const char *name;
    double total_ms;
    int count;
} render_gpu_pass;

/* Enabling or disabling clears the accumulated passes. Detail enables the
 * marks passed to render_gpu_timing_detail, such as individual HRC levels. */
void render_gpu_timing_enable(render_gpu_timing_mode mode, bool detail);
/* Begins a frame's first pass; the next mark times from here. */
void render_gpu_timing_start(void);
/* name must be a string literal: passes are keyed by it. */
void render_gpu_timing_mark(const char *name);
/* A mark that only applies with detail enabled. */
void render_gpu_timing_detail(const char *name);
/* Clears the totals while keeping timing enabled. */
void render_gpu_timing_clear(void);
/* Resolves outstanding timestamps first, waiting briefly for late reports. */
const render_gpu_pass *render_gpu_timing_passes(int *count);

/* The GPU time of one piece of each frame's work, such as lighting, measured
 * with the host's command buffer timestamps. Beginning and ending a span each
 * submit the recorded work, so its command buffers hold only the span's work.
 * Without a timestamp host, spans do nothing and report no times. */
typedef struct
{
    struct
    {
        unsigned first, last;
    } pending[8];
    int head, count;
    unsigned first;
} render_gpu_span;

/* Timestamp hosts call this when their commit hook is installed or removed. */
void render_gpu_timing_track_commits(bool track);
bool render_gpu_span_available(void);
void render_gpu_span_begin(render_gpu_span *span);
void render_gpu_span_end(render_gpu_span *span);
/* Takes the oldest span whose GPU work finished: its milliseconds, in the
 * order the spans ended. Never waits; false when none has finished. */
bool render_gpu_span_take(render_gpu_span *span, double *ms);
/* Drops the spans not yet taken and releases what they hold, leaving an empty
 * span. Call it when their times no longer matter and before freeing a span. */
void render_gpu_span_discard(render_gpu_span *span);

/* Timestamp hosts call this on the committing thread immediately before each
 * command buffer commit. Returns the buffer's id, or 0 when it is not tracked. */
unsigned render_gpu_timing_committed(void);
/* Timestamp hosts report a tracked command buffer's GPU execution interval in
 * milliseconds of any monotonic clock. Safe to call from any thread. */
void render_gpu_timing_completed(unsigned id, double start_ms, double end_ms);

#endif
