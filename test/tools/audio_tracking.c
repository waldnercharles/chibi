#include "third_party/btt/BTT.h"
#include "third_party/btt/src/DFT.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <string.h>

#define TRACKING_RATE 44100
#define TRACKING_EVENTS 64
#define TRACKING_TAU 6.283185307179586

typedef struct
{
    unsigned long long onsets[TRACKING_EVENTS], beats[TRACKING_EVENTS];
    int onset_count, beat_count;
    bool overflow;
} tracking_events;

static BTT *tracking_a, *tracking_b;
static tracking_events events_a, events_b;
static float tracking_samples[TRACKING_RATE * 4 + 37];

static void record_onset(void *context, unsigned long long time)
{
    tracking_events *events = context;
    if (events->onset_count == TRACKING_EVENTS) {
        events->overflow = true;
        return;
    }
    events->onsets[events->onset_count++] = time;
}

static void record_beat(void *context, unsigned long long time)
{
    tracking_events *events = context;
    if (events->beat_count == TRACKING_EVENTS) {
        events->overflow = true;
        return;
    }
    events->beats[events->beat_count++] = time;
}

static BTT *new_tracking(int rate, tracking_events *events)
{
    BTT *tracker = btt_new(
        BTT_SUGGESTED_SPECTRAL_FLUX_STFT_LEN,
        BTT_SUGGESTED_SPECTRAL_FLUX_STFT_OVERLAP,
        BTT_SUGGESTED_OSS_FILTER_ORDER,
        BTT_SUGGESTED_OSS_LENGTH,
        BTT_SUGGESTED_ONSET_THRESHOLD_N,
        BTT_SUGGESTED_CBSS_LENGTH,
        rate,
        BTT_DEFAULT_ANALYSIS_LATENCY_ONSET_ADJUSTMENT,
        BTT_DEFAULT_ANALYSIS_LATENCY_BEAT_ADJUSTMENT
    );
    assert(tracker);
    btt_set_onset_tracking_callback(tracker, record_onset, events);
    btt_set_beat_tracking_callback(tracker, record_beat, events);
    return tracker;
}

static void tracking_setup()
{
    events_a = (tracking_events){ 0 };
    events_b = (tracking_events){ 0 };
    tracking_a = new_tracking(TRACKING_RATE, &events_a);
    tracking_b = new_tracking(TRACKING_RATE, &events_b);
}

static void tracking_teardown()
{
    btt_destroy(tracking_a);
    btt_destroy(tracking_b);
}

static void tracking_pcm(int rate, int count)
{
    assert(count <= (int)(sizeof(tracking_samples) / sizeof(*tracking_samples)));
    int period = rate / 2;
    for (int i = 0; i < count; i++) {
        double phase = (double)((i + period - rate / 10) % period) / rate;
        tracking_samples[i] = 0.8 * exp(-phase * 45) * sin(TRACKING_TAU * 110 * phase) +
                              0.008 * sin(TRACKING_TAU * 1600 * i / rate);
    }
}

static void tracking_chunks(BTT *tracker, int count)
{
    const int chunks[] = { 1, 37, 511, 128, 3, 2049, 97 };
    int chunk = 0;
    for (int at = 0; at < count;) {
        int length = chunks[chunk++ % 7];
        if (length > count - at) {
            length = count - at;
        }
        btt_process(tracker, tracking_samples + at, length);
        at += length;
    }
}

static bool matching_events()
{
    return !events_a.overflow && !events_b.overflow &&
           events_a.onset_count == events_b.onset_count &&
           events_a.beat_count == events_b.beat_count &&
           !memcmp(
               events_a.onsets,
               events_b.onsets,
               events_a.onset_count * sizeof(*events_a.onsets)
           ) &&
           !memcmp(
               events_a.beats,
               events_b.beats,
               events_a.beat_count * sizeof(*events_a.beats)
           );
}

TEST_CASE(audio_tracking_autocorrelation_matches_direct_correlation)
{
    enum
    {
        COUNT = 64
    };
    float signal[COUNT], actual[COUNT];
    for (int i = 0; i < COUNT; i++) {
        signal[i] = (float)((i * 17 + 3) % 23 - 11) / 11 + (i % 2 ? -0.3f : 0.3f);
    }
    memcpy(actual, signal, sizeof(actual));
    rdft_real_generalized_autocorrelation(actual, COUNT, 2);
    for (int lag = 0; lag < COUNT; lag++) {
        double expected = 0;
        for (int i = 0; i < COUNT; i++) {
            expected += (double)signal[i] * signal[(i + lag) % COUNT];
        }
        REQUIRE(fabs(actual[lag] - expected) < 0.001);
    }
    return true;
}

TEST_CASE(audio_tracking_events_use_sample_positions_across_chunks)
{
    enum
    {
        COUNT = TRACKING_RATE * 5 / 4 + 37
    };
    tracking_pcm(TRACKING_RATE, COUNT);
    btt_init_tempo(tracking_a, 120);
    btt_init_tempo(tracking_b, 120);
    /* The first onset predates this offset and must clamp to zero. */
    btt_set_analysis_latency_onset_adjustment(tracking_a, TRACKING_RATE / 4);
    btt_set_analysis_latency_onset_adjustment(tracking_b, TRACKING_RATE / 4);
    btt_process(tracking_a, tracking_samples, COUNT);
    tracking_chunks(tracking_b, COUNT);
    REQUIRE(events_a.onset_count >= 2 && events_a.beat_count >= 2);
    REQUIRE(events_a.onsets[0] == 0);
    REQUIRE(events_a.onsets[events_a.onset_count - 1] > 0);
    REQUIRE(matching_events());
    for (int i = 0; i < events_a.onset_count; i++) {
        REQUIRE(events_a.onsets[i] <= COUNT);
    }
    for (int i = 0; i < events_a.beat_count; i++) {
        REQUIRE(events_a.beats[i] < COUNT + TRACKING_RATE / 2);
    }
    REQUIRE(btt_get_num_audio_samples_processed(tracking_a) == COUNT);
    REQUIRE(btt_get_num_audio_samples_processed(tracking_b) == COUNT);
    return true;
}

TEST_CASE(audio_tracking_clear_matches_a_new_tracker)
{
    enum
    {
        PRIME = TRACKING_RATE * 4 + 37,
        COUNT = TRACKING_RATE * 4 + 37
    };
    tracking_pcm(TRACKING_RATE, PRIME);
    btt_init_tempo(tracking_a, 120);
    btt_process(tracking_a, tracking_samples, PRIME);
    REQUIRE(events_a.beat_count > 0 && events_a.onset_count > 0);
    btt_clear(tracking_a);
    events_a = (tracking_events){ 0 };
    REQUIRE(btt_get_num_audio_samples_processed(tracking_a) == 0);
    REQUIRE(btt_get_tempo_bpm(tracking_a) == 0);
    REQUIRE(btt_get_tempo_certainty(tracking_a) == 0);
    tracking_pcm(TRACKING_RATE, COUNT);
    tracking_chunks(tracking_a, COUNT);
    btt_process(tracking_b, tracking_samples, COUNT);
    REQUIRE(events_b.onset_count > 0 && events_b.beat_count > 0);
    REQUIRE(matching_events());
    REQUIRE(btt_get_tempo_bpm(tracking_b) > 0);
    REQUIRE(btt_get_tempo_bpm(tracking_a) == btt_get_tempo_bpm(tracking_b));
    REQUIRE(btt_get_tempo_certainty(tracking_a) == btt_get_tempo_certainty(tracking_b));
    REQUIRE(btt_get_num_audio_samples_processed(tracking_a) == COUNT);
    return true;
}

static void configure_tracking(BTT *tracker)
{
    btt_set_min_tempo(tracker, 70);
    btt_set_max_tempo(tracker, 170);
    btt_set_log_gaussian_tempo_weight_mean(tracker, 110);
    btt_set_log_gaussian_tempo_weight_width(tracker, 55);
    btt_set_oss_filter_cutoff(tracker, 8);
    btt_set_onset_threshold_min(tracker, 1);
    btt_set_analysis_latency_onset_adjustment(tracker, 2000);
    btt_set_analysis_latency_beat_adjustment(tracker, 1000);
}

TEST_CASE(audio_tracking_rate_changes_match_native_construction)
{
    const int rates[] = { 48000, TRACKING_RATE };
    configure_tracking(tracking_a);
    for (int rate_index = 0; rate_index < 2; rate_index++) {
        int old_rate = btt_get_sample_rate(tracking_a);
        int rate = rates[rate_index];
        int count = rate + 37;
        tracking_pcm(old_rate, old_rate / 4 + 37);
        btt_process(tracking_a, tracking_samples, old_rate / 4 + 37);
        btt_set_sample_rate(tracking_a, rate);
        events_a = (tracking_events){ 0 };
        events_b = (tracking_events){ 0 };
        tracking_b = btt_destroy(tracking_b);
        tracking_b = new_tracking(rate, &events_b);
        configure_tracking(tracking_b);
        REQUIRE(btt_get_sample_rate(tracking_a) == rate);
        REQUIRE(btt_get_num_audio_samples_processed(tracking_a) == 0);
        REQUIRE(btt_get_tempo_bpm(tracking_a) == 0);
        REQUIRE(btt_get_tempo_certainty(tracking_a) == 0);
        REQUIRE(btt_get_min_tempo(tracking_a) == btt_get_min_tempo(tracking_b));
        REQUIRE(btt_get_max_tempo(tracking_a) == btt_get_max_tempo(tracking_b));
        /* These getters convert through a float lag even though they return double. */
        REQUIRE(
            fabs(btt_get_log_gaussian_tempo_weight_mean(tracking_a) - 110) <
            2 * FLT_EPSILON * 110
        );
        REQUIRE(
            fabs(btt_get_log_gaussian_tempo_weight_width(tracking_a) - 55) <
            2 * FLT_EPSILON * 55
        );
        REQUIRE(btt_get_oss_filter_cutoff(tracking_a) == 8);
        REQUIRE(btt_get_onset_threshold_min(tracking_a) == 1);
        REQUIRE(btt_get_analysis_latency_onset_adjustment(tracking_a) == 2000);
        REQUIRE(btt_get_analysis_latency_beat_adjustment(tracking_a) == 1000);
        void *context = NULL;
        REQUIRE(btt_get_onset_tracking_callback(tracking_a, &context) == record_onset);
        REQUIRE(context == &events_a);
        context = NULL;
        REQUIRE(btt_get_beat_tracking_callback(tracking_a, &context) == record_beat);
        REQUIRE(context == &events_a);
        btt_init_tempo(tracking_a, 120);
        btt_init_tempo(tracking_b, 120);
        tracking_pcm(rate, count);
        tracking_chunks(tracking_a, count);
        btt_process(tracking_b, tracking_samples, count);
        REQUIRE(events_b.onset_count > 0 && events_b.beat_count > 0);
        REQUIRE(matching_events());
        REQUIRE(btt_get_tempo_bpm(tracking_a) == btt_get_tempo_bpm(tracking_b));
        REQUIRE(btt_get_tempo_certainty(tracking_a) == btt_get_tempo_certainty(tracking_b));
        REQUIRE(btt_get_num_audio_samples_processed(tracking_a) == (unsigned long long)count);
    }
    return true;
}

void test_tool_audio_tracking()
{
    RUN_TEST_CASE(audio_tracking_autocorrelation_matches_direct_correlation);
    pu_setup(tracking_setup, tracking_teardown);
    RUN_TEST_CASE(audio_tracking_events_use_sample_positions_across_chunks);
    RUN_TEST_CASE(audio_tracking_clear_matches_a_new_tracker);
    RUN_TEST_CASE(audio_tracking_rate_changes_match_native_construction);
    pu_clear_setup();
}
