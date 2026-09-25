/* Performance: live charts of the last 30 seconds (Liveline). Frame time
 * against the 60 Hz budget, and the running scene's behavior, physics, systems
 * and rendering time with a legend (Recharts). Readouts refresh four times a
 * second so they stay readable. */
#include "editor/studio/studio_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW 30.0
#define BUDGET_MS 16.7f

enum
{
    SERIES_BEHAVIOR,
    SERIES_PHYSICS,
    SERIES_SYSTEMS,
    SERIES_RENDERING,
    SERIES_COUNT,
};

static const char *const SERIES_LABELS[SERIES_COUNT] = { "Behavior",
                                                         "Physics",
                                                         "Systems",
                                                         "Rendering" };
static const uint32_t SERIES_COLORS[SERIES_COUNT] = { 0x6f9fe8ff,
                                                      0x57ab5aff,
                                                      0xd9a53dff,
                                                      0xc586c0ff };

void studio_performance_sample(studio *s, double delta_seconds)
{
    const engine_frame_timing *timing = app_scene_authoring_runtime_timing(s->scene);
    bool running = timing && app_scene_authoring_runtime_state(s->scene).playing;
    studio_perf_sample sample = {
        .time = (float)s->time,
        .frame_ms = (float)(delta_seconds * 1000),
        .running = running,
    };
    if (running) {
        sample.ms[SERIES_BEHAVIOR] = (float)timing->behavior_ms;
        sample.ms[SERIES_PHYSICS] = (float)timing->physics_ms;
        sample.ms[SERIES_SYSTEMS] = (float)timing->systems_ms;
        sample.ms[SERIES_RENDERING] = (float)timing->rendering_ms;
    }
    apush(s->perf, sample);
    int old = 0;
    while (old < acount(s->perf) && s->perf[old].time < s->time - WINDOW - 1) {
        old++;
    }
    if (old) {
        memmove(
            s->perf,
            s->perf + old,
            (size_t)(acount(s->perf) - old) * sizeof *s->perf
        );
        asetlen(s->perf, acount(s->perf) - old);
    }
}

/* Averages refresh at 4 Hz; a readout that changes every frame cannot be read. */
static void refresh_readout(studio *s)
{
    if (s->time - s->perf_readout_time < 0.25 && s->perf_readout_time > 0) {
        return;
    }
    s->perf_readout_time = s->time;
    double sum = 0, peak = 0;
    int count = 0;
    for (int i = 0; i < acount(s->perf); i++) {
        if (s->perf[i].time >= s->time - 2) {
            sum += s->perf[i].frame_ms;
            peak = fmax(peak, s->perf[i].frame_ms);
            count++;
        }
    }
    double average = count ? sum / count : 0;
    snprintf(
        s->perf_readout,
        sizeof s->perf_readout,
        "%.0f fps \xc2\xb7 %.1f ms \xc2\xb7 max %.1f ms",
        average > 0 ? 1000 / average : 0,
        average,
        peak
    );
}

void studio_performance_header(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    refresh_readout(s);
    kit_label(k, "title", "perf-title", "Performance");
    kit_leaf(k, "spacer", NULL, "spacer");
    kit_label(k, "readout", "perf-readout", s->perf_readout);
}

void studio_performance_body(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    int count = acount(s->perf);
    float *times = xmalloc((size_t)(count ? count : 1) * sizeof *times);
    float *frame = xmalloc((size_t)(count ? count : 1) * sizeof *frame);
    float *ms[SERIES_COUNT];
    int running = 0;
    for (int i = 0; i < count; i++) {
        running += s->perf[i].running;
    }
    for (int c = 0; c < SERIES_COUNT; c++) {
        ms[c] = xmalloc((size_t)(running ? running : 1) * sizeof *ms[c]);
    }
    float *run_times = xmalloc((size_t)(running ? running : 1) * sizeof *run_times);
    for (int i = 0, r = 0; i < count; i++) {
        times[i] = s->perf[i].time;
        frame[i] = s->perf[i].frame_ms;
        if (s->perf[i].running) {
            run_times[r] = s->perf[i].time;
            for (int c = 0; c < SERIES_COUNT; c++) {
                ms[c][r] = s->perf[i].ms[c];
            }
            r++;
        }
    }
    kit_push(k, "perf", NULL, "perf");
    kit_label(k, "frame-title", "perf-heading", "Frame");
    kit_chart_series frame_series = { times, frame, count, 0 };
    kit_chart_desc frame_chart = {
        .series = &frame_series,
        .series_count = 1,
        .now = s->time,
        .window = (float)WINDOW,
        .reference = BUDGET_MS,
        .unit = "ms",
        .precision = 1,
        .fill = true,
        .live = true,
    };
    kit_chart(k, "frame", "perf-chart", &frame_chart);
    kit_label(k, "sim-title", "perf-heading", "Simulation");
    if (running) {
        kit_chart_series series[SERIES_COUNT];
        for (int c = 0; c < SERIES_COUNT; c++) {
            series[c] = (kit_chart_series){ run_times, ms[c], running, SERIES_COLORS[c] };
        }
        kit_chart_desc simulation = {
            .series = series,
            .series_count = SERIES_COUNT,
            .now = s->time,
            .window = (float)WINDOW,
            .unit = "ms",
            .precision = 2,
            .live = true,
        };
        kit_chart(k, "simulation", "perf-chart", &simulation);
        kit_push(k, "legend", NULL, "perf-legend");
        for (int c = 0; c < SERIES_COUNT; c++) {
            char key[16], color[16];
            snprintf(key, sizeof key, "s%d", c);
            snprintf(color, sizeof color, "#%08x", SERIES_COLORS[c]);
            kit_push(k, key, NULL, "perf-key");
            kit_leaf(k, "dot", NULL, "perf-dot");
            kit_style(k, "background-color", color);
            kit_label(k, "label", NULL, SERIES_LABELS[c]);
            kit_pop(k);
        }
        kit_pop(k);
    } else {
        kit_label(k, "idle", "perf-idle", "Not running");
    }
    kit_pop(k);
    for (int c = 0; c < SERIES_COUNT; c++) {
        free(ms[c]);
    }
    free(run_times);
    free(times);
    free(frame);
}
