#include "render/gpu_timing.h"

#include <assert.h>
#include <cute.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

/* Power-of-two ring of reported command buffers, indexed by id. */
#define COMMIT_SLOTS (1 << 16)
#define PENDING_MARKS 4096
#define RESOLVE_WAIT_MS 2000

typedef struct
{
    _Atomic unsigned id;
    double start, end;
} commit_slot;

/* A pass covering the command buffers with ids in (first, last]. */
typedef struct
{
    const char *name;
    unsigned first, last;
} pending_mark;

static struct
{
    render_gpu_timing_mode mode;
    bool detail;
    double last;
    render_gpu_pass passes[RENDER_GPU_TIMING_PASSES];
    int count;
    _Atomic unsigned committed;
    /* Last id covered by a mark or start, folded into the horizon, and
     * covered when the totals were last cleared. */
    unsigned mark_commit, processed, cleared;
    /* End of all folded GPU work. */
    double horizon;
    pending_mark pending[PENDING_MARKS];
    int pending_head, pending_count;
} g_timing;

static commit_slot g_commits[COMMIT_SLOTS];
/* A timestamp host tracks every commit, so spans can time any frame. */
static bool g_track_commits;

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten/html5.h>

/* WebGL has no command buffer timestamps, so spans time their work with the
 * browser's timer queries where it offers them. */
#define GL_TIME_ELAPSED 0x88BF
#define GL_GPU_DISJOINT 0x8FBB

static bool timer_queries(void)
{
    static int available = -1;
    if (available < 0) {
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_get_current_context();
        available = context &&
                    emscripten_webgl_enable_extension(context, "EXT_disjoint_timer_query_webgl2");
    }
    return available;
}
#endif

static double milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1e3 + now.tv_nsec * 1e-6;
}

static void s_add(const char *name, double ms)
{
    int i = 0;
    while (i < g_timing.count && strcmp(g_timing.passes[i].name, name)) {
        i++;
    }
    if (i == g_timing.count && g_timing.count < RENDER_GPU_TIMING_PASSES) {
        g_timing.passes[g_timing.count++] = (render_gpu_pass){ .name = name };
    }
    if (i < g_timing.count) {
        g_timing.passes[i].total_ms += ms;
        g_timing.passes[i].count++;
    }
}

static bool s_completed(unsigned id)
{
    commit_slot *slot = &g_commits[id & (COMMIT_SLOTS - 1)];
    return atomic_load_explicit(&slot->id, memory_order_acquire) == id;
}

/* Charges the part of a completed command buffer's interval that follows the
 * end of all earlier work, then extends the horizon. */
static double s_fold(unsigned id)
{
    commit_slot *slot = &g_commits[id & (COMMIT_SLOTS - 1)];
    double charged = fmax(0, slot->end - fmax(slot->start, g_timing.horizon));
    g_timing.horizon = fmax(g_timing.horizon, slot->end);
    return charged;
}

static void s_resolve(void)
{
    while (g_timing.pending_count) {
        pending_mark mark = g_timing.pending[g_timing.pending_head];
        for (unsigned id = g_timing.processed + 1; id <= mark.last; id++) {
            if (!s_completed(id)) {
                return;
            }
        }
        double unmarked = 0, charged = 0;
        for (unsigned id = g_timing.processed + 1; id <= mark.first; id++) {
            unmarked += s_fold(id);
        }
        for (unsigned id = mark.first + 1; id <= mark.last; id++) {
            charged += s_fold(id);
        }
        g_timing.processed = mark.last;
        g_timing.pending_head = (g_timing.pending_head + 1) % PENDING_MARKS;
        g_timing.pending_count--;
        if (mark.last <= g_timing.cleared) {
            continue;
        }
        if (unmarked > 0) {
            s_add("Unmarked", unmarked);
        }
        s_add(mark.name, charged);
    }
}

void render_gpu_timing_enable(render_gpu_timing_mode mode, bool detail)
{
    unsigned committed = atomic_load(&g_timing.committed);
    memset(&g_timing, 0, sizeof g_timing);
    atomic_store(&g_timing.committed, committed);
    g_timing.mode = mode;
    g_timing.detail = mode != RENDER_GPU_TIMING_OFF && detail;
    g_timing.mark_commit = g_timing.processed = g_timing.cleared = committed;
}

void render_gpu_timing_start(void)
{
    if (g_timing.mode == RENDER_GPU_TIMING_SYNC) {
        cf_gpu_sync();
        g_timing.last = milliseconds();
    }
    if (g_timing.mode == RENDER_GPU_TIMING_TIMESTAMPS) {
        s_resolve();
        cf_gpu_submit();
        g_timing.mark_commit = atomic_load(&g_timing.committed);
    }
}

void render_gpu_timing_mark(const char *name)
{
    if (g_timing.mode == RENDER_GPU_TIMING_SYNC) {
        cf_gpu_sync();
        double now = milliseconds();
        s_add(name, now - g_timing.last);
        g_timing.last = now;
    }
    if (g_timing.mode == RENDER_GPU_TIMING_TIMESTAMPS) {
        cf_gpu_submit();
        assert(g_timing.pending_count < PENDING_MARKS);
        unsigned last = atomic_load(&g_timing.committed);
        int tail = (g_timing.pending_head + g_timing.pending_count++) % PENDING_MARKS;
        g_timing.pending[tail] = (pending_mark){ name, g_timing.mark_commit, last };
        g_timing.mark_commit = last;
    }
}

void render_gpu_timing_detail(const char *name)
{
    if (g_timing.detail) {
        render_gpu_timing_mark(name);
    }
}

void render_gpu_timing_clear(void)
{
    g_timing.count = 0;
    g_timing.cleared = g_timing.mark_commit;
}

const render_gpu_pass *render_gpu_timing_passes(int *count)
{
    double deadline = milliseconds() + RESOLVE_WAIT_MS;
    s_resolve();
    while (g_timing.pending_count && milliseconds() < deadline) {
        cf_sleep(1);
        s_resolve();
    }
    *count = g_timing.count;
    return g_timing.passes;
}

void render_gpu_timing_track_commits(bool track)
{
    g_track_commits = track;
}

bool render_gpu_span_available(void)
{
#ifdef __EMSCRIPTEN__
    if (timer_queries()) {
        return true;
    }
#endif
    return g_track_commits;
}

static void s_span_drop(render_gpu_span *span)
{
#ifdef __EMSCRIPTEN__
    if (timer_queries()) {
        glDeleteQueries(1, &span->pending[span->head].first);
    }
#endif
    span->head = (span->head + 1) % (int)(sizeof span->pending / sizeof *span->pending);
    span->count--;
}

static void s_span_push(render_gpu_span *span, unsigned last)
{
    int size = (int)(sizeof span->pending / sizeof *span->pending);
    if (span->count == size) {
        s_span_drop(span);
    }
    int tail = (span->head + span->count++) % size;
    span->pending[tail].first = span->first;
    span->pending[tail].last = last;
}

void render_gpu_span_begin(render_gpu_span *span)
{
#ifdef __EMSCRIPTEN__
    if (timer_queries()) {
        glGenQueries(1, &span->first);
        glBeginQuery(GL_TIME_ELAPSED, span->first);
        return;
    }
#endif
    if (!g_track_commits) {
        return;
    }
    cf_gpu_submit();
    span->first = atomic_load(&g_timing.committed);
}

void render_gpu_span_end(render_gpu_span *span)
{
#ifdef __EMSCRIPTEN__
    if (timer_queries()) {
        glEndQuery(GL_TIME_ELAPSED);
        s_span_push(span, 0);
        return;
    }
#endif
    if (!g_track_commits) {
        return;
    }
    cf_gpu_submit();
    s_span_push(span, atomic_load(&g_timing.committed));
}

#ifdef __EMSCRIPTEN__
static bool s_query_take(render_gpu_span *span, double *ms)
{
    GLuint query = span->pending[span->head].first, ready = 0, ns = 0;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) {
        return false;
    }
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT, &disjoint);
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &ns);
    s_span_drop(span);
    /* A disjoint interval, such as across a GPU clock change, is no time. */
    *ms = disjoint ? NAN : ns * 1e-6;
    return true;
}
#endif

bool render_gpu_span_take(render_gpu_span *span, double *ms)
{
    if (!span->count) {
        return false;
    }
#ifdef __EMSCRIPTEN__
    if (timer_queries()) {
        return s_query_take(span, ms);
    }
#endif
    unsigned first = span->pending[span->head].first;
    unsigned last = span->pending[span->head].last;
    for (unsigned id = first ? first : 1; id <= last; id++) {
        if (!s_completed(id)) {
            return false;
        }
    }
    /* The GPU overlaps consecutive command buffers. Time starts when the work
     * submitted before the span ends, and overlap is charged once. */
    double total = 0;
    double horizon = first ? g_commits[first & (COMMIT_SLOTS - 1)].end : -INFINITY;
    for (unsigned id = first + 1; id <= last; id++) {
        const commit_slot *slot = &g_commits[id & (COMMIT_SLOTS - 1)];
        total += fmax(0, slot->end - fmax(slot->start, horizon));
        horizon = fmax(horizon, slot->end);
    }
    s_span_drop(span);
    *ms = total;
    return true;
}

void render_gpu_span_discard(render_gpu_span *span)
{
    while (span->count) {
        s_span_drop(span);
    }
    *span = (render_gpu_span){ 0 };
}

unsigned render_gpu_timing_committed(void)
{
    if (g_timing.mode != RENDER_GPU_TIMING_TIMESTAMPS && !g_track_commits) {
        return 0;
    }
    return atomic_fetch_add(&g_timing.committed, 1) + 1;
}

void render_gpu_timing_completed(unsigned id, double start_ms, double end_ms)
{
    if (!id) {
        return;
    }
    commit_slot *slot = &g_commits[id & (COMMIT_SLOTS - 1)];
    slot->start = start_ms;
    slot->end = end_ms;
    atomic_store_explicit(&slot->id, id, memory_order_release);
}
