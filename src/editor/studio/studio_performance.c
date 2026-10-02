/* Performance: live charts of the last 30 seconds (Liveline). Frame time
 * against the 60 Hz budget, the running scene's behavior, physics, systems
 * and rendering time with a legend (Recharts), and the scene's GPU time
 * against target_fps with the lighting grids adaptive resolution chose.
 * Readouts refresh four times a second so they stay readable. */
#include "editor/studio/studio_internal.h"
#include "render/scene_renderer.h"
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

static void publish_telemetry(studio *s);

/* Samples only frames the editor runs for input or a playing scene: an idle
 * editor's frames are not worth charting, and charting them would keep it
 * drawing. */
void studio_performance_sample(studio *s, double delta_seconds)
{
    if (!studio_active(s)) {
        publish_telemetry(s);
        return;
    }
    const engine_frame_timing *timing = app_scene_authoring_runtime_timing(s->scene);
    bool running = timing && app_scene_authoring_runtime_state(s->scene).playing;
    render_radiance_status lighting;
    bool lit = app_scene_renderer_lighting_status(s->scene->renderer, &lighting);
    studio_perf_sample sample = {
        .time = (float)s->time,
        .frame_ms = (float)(delta_seconds * 1000),
        .scene_gpu_ms = lit ? (float)lighting.gpu_ms : -1,
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
    publish_telemetry(s);
}

/* Input continues to emit the chart for scrubbing and range easing. Its data
 * advances independently, retaining every recorded frame between publications. */
static void publish_telemetry(studio *s)
{
    bool playing = app_scene_authoring_runtime_state(s->scene).playing;
    if (s->perf_publications && playing == s->perf_publish_playing &&
        s->time < s->perf_publish_next) {
        return;
    }
    int count = acount(s->perf);
    asetlen(s->perf_published, count);
    if (count) {
        memcpy(s->perf_published, s->perf, (size_t)count * sizeof *s->perf);
    }
    s->perf_publications++;
    s->perf_publish_playing = playing;
    s->perf_publish_next = s->time + (playing ? 1.0 / 20.0 : 0.25);
    render_radiance_status lighting;
    s->perf_lighting.available = app_scene_renderer_lighting_status(s->scene->renderer, &lighting);
    if (!s->perf_lighting.available) {
        return;
    }
    s->perf_lighting.budget_ms = (float)lighting.budget_ms;
    static const char *const detail[] = { "Full", "Half", "Quarter", "Eighth" };
    int used = snprintf(
        s->perf_lighting.text,
        sizeof s->perf_lighting.text,
        "Resolution %g \xc2\xb7 color %s \xc2\xb7 gather %s",
        lighting.resolution,
        detail[lighting.color],
        lighting.gather_half ? "Half" : "Full"
    );
    if (lighting.adaptive && lighting.rungs > 1) {
        snprintf(
            s->perf_lighting.text + used,
            sizeof s->perf_lighting.text - used,
            " \xc2\xb7 step %d of %d",
            lighting.rung + 1,
            lighting.rungs
        );
    }
}

/* Averages refresh at 4 Hz; a readout that changes every frame cannot be read.
 * An idle editor draws only when something changes, which it says. */
static void refresh_readout(studio *s)
{
    if (!studio_active(s)) {
        snprintf(s->perf_readout, sizeof s->perf_readout, "Idle");
        return;
    }
    if (s->time - s->perf_readout_time < 0.25 && s->perf_readout_time > 0) {
        return;
    }
    s->perf_readout_time = s->time;
    double sum = 0, peak = 0, last = acount(s->perf) ? alast(s->perf).time : s->time;
    int count = 0;
    for (int i = 0; i < acount(s->perf); i++) {
        if (s->perf[i].time >= last - 2) {
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
    kit_live_label(k, "readout", "perf-readout", s->perf_readout);
}

/* The scene's GPU time against a frame at target_fps, and the lighting grids
 * adaptive resolution chose. */
static void lighting_section(studio *s)
{
    kit *k = s->kit;
    int count = acount(s->perf_published);
    if (!s->perf_lighting.available) {
        return;
    }
    kit_label(k, "lighting-title", "perf-heading", "Scene GPU and lighting");
    kit_label(k, "lighting-state", "perf-status", s->perf_lighting.text);
    float *times = xmalloc((size_t)(count ? count : 1) * sizeof *times);
    float *ms = xmalloc((size_t)(count ? count : 1) * sizeof *ms);
    int measured = 0;
    for (int i = 0; i < count; i++) {
        if (s->perf_published[i].scene_gpu_ms >= 0) {
            times[measured] = s->perf_published[i].time;
            ms[measured++] = s->perf_published[i].scene_gpu_ms;
        }
    }
    if (measured) {
        kit_chart_series series = { times, ms, measured, SERIES_COLORS[SERIES_RENDERING] };
        kit_chart_desc chart = {
            .series = &series,
            .series_count = 1,
            .now = s->perf_published[count - 1].time,
            .window = (float)WINDOW,
            .reference = s->perf_lighting.budget_ms,
            .unit = "ms",
            .precision = 2,
            .live = studio_active(s),
        };
        kit_chart(k, "lighting", "perf-chart", &chart);
    } else {
        kit_label(k, "lighting-idle", "perf-status", "This GPU reports no timestamps");
    }
    free(times);
    free(ms);
}

void studio_performance_body(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    int count = acount(s->perf_published);
    float *times = xmalloc((size_t)(count ? count : 1) * sizeof *times);
    float *frame = xmalloc((size_t)(count ? count : 1) * sizeof *frame);
    float *ms[SERIES_COUNT];
    int running = 0;
    for (int i = 0; i < count; i++) {
        running += s->perf_published[i].running;
    }
    for (int c = 0; c < SERIES_COUNT; c++) {
        ms[c] = xmalloc((size_t)(running ? running : 1) * sizeof *ms[c]);
    }
    float *run_times = xmalloc((size_t)(running ? running : 1) * sizeof *run_times);
    for (int i = 0, r = 0; i < count; i++) {
        times[i] = s->perf_published[i].time;
        frame[i] = s->perf_published[i].frame_ms;
        if (s->perf_published[i].running) {
            run_times[r] = s->perf_published[i].time;
            for (int c = 0; c < SERIES_COUNT; c++) {
                ms[c][r] = s->perf_published[i].ms[c];
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
        .now = count ? s->perf_published[count - 1].time : 0,
        .window = (float)WINDOW,
        .reference = BUDGET_MS,
        .unit = "ms",
        .precision = 1,
        .fill = true,
        .live = studio_active(s),
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
            .now = count ? s->perf_published[count - 1].time : 0,
            .window = (float)WINDOW,
            .unit = "ms",
            .precision = 2,
            .live = studio_active(s),
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
    lighting_section(s);
    kit_pop(k);
    for (int c = 0; c < SERIES_COUNT; c++) {
        free(ms[c]);
    }
    free(run_times);
    free(times);
    free(frame);
}
