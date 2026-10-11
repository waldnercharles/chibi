#include "runtime/audio_analysis.h"

#include "runtime/audio.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stddef.h>

#define RATE 48000
#define TAU 6.283185307179586
#define BLOCK 256
static audio_analysis *analysis, *other;
static audio *queue;

static void setup()
{
    analysis = audio_analysis_new();
    other = audio_analysis_new();
    queue = audio_new();
}

static void teardown()
{
    audio_analysis_free(other);
    audio_analysis_free(analysis);
    audio_free(queue);
}

static void push_tone(audio_analysis *target, int rate, int frames, float hz, float volume, int channels)
{
    float samples[BLOCK * 8];
    for (int at = 0; at < frames;) {
        int count = frames - at;
        if (count > BLOCK) {
            count = BLOCK;
        }
        for (int frame = 0; frame < count; frame++) {
            float value = volume * sin(TAU * hz * (at + frame) / rate);
            for (int channel = 0; channel < channels; channel++) {
                samples[frame * channels + channel] = channel % 2 ? -value : value;
            }
        }
        audio_analysis_push(target, samples, count, channels, rate);
        at += count;
    }
}

static bool silent(audio_analysis_frame frame)
{
    return frame.level == 0 && frame.bass == 0 && frame.mid == 0 &&
           frame.treble == 0 && frame.onset == 0;
}

static bool near(audio_analysis_frame a, audio_analysis_frame b, float tolerance)
{
    return fabsf(a.level - b.level) <= tolerance &&
           fabsf(a.bass - b.bass) <= tolerance && fabsf(a.mid - b.mid) <= tolerance &&
           fabsf(a.treble - b.treble) <= tolerance &&
           fabsf(a.onset - b.onset) <= tolerance;
}

TEST_CASE(audio_bands_follow_tone_frequencies)
{
    push_tone(analysis, RATE, RATE, 60, 0.8f, 1);
    audio_analysis_frame bass = audio_analysis_read(analysis);
    REQUIRE(bass.bass > bass.mid * 3);
    REQUIRE(bass.bass > bass.treble * 3);
    audio_analysis_reset(analysis);
    push_tone(analysis, RATE, RATE, 1000, 0.8f, 1);
    audio_analysis_frame mid = audio_analysis_read(analysis);
    REQUIRE(mid.mid > mid.bass * 2);
    REQUIRE(mid.mid > mid.treble * 2);
    audio_analysis_reset(analysis);
    push_tone(analysis, RATE, RATE, 8000, 0.8f, 1);
    audio_analysis_frame treble = audio_analysis_read(analysis);
    REQUIRE(treble.treble > treble.bass * 3);
    REQUIRE(treble.treble > treble.mid * 2);
    REQUIRE(fabsf(mid.level - 0.8f / sqrtf(2)) < 0.001f);
    return true;
}

TEST_CASE(audio_levels_follow_amplitude_and_stay_normalized)
{
    push_tone(analysis, RATE, RATE, 1000, 0.25f, 1);
    push_tone(other, RATE, RATE, 1000, 0.5f, 1);
    audio_analysis_frame low = audio_analysis_read(analysis);
    audio_analysis_frame high = audio_analysis_read(other);
    REQUIRE(fabsf(high.level - low.level * 2) < 0.0001f);
    REQUIRE(fabsf(high.mid - low.mid * 2) < 0.0001f);
    audio_analysis_reset(analysis);
    push_tone(analysis, RATE, RATE, 1000, 8, 1);
    high = audio_analysis_read(analysis);
    REQUIRE(high.level == 1);
    REQUIRE(high.bass >= 0 && high.bass <= 1);
    REQUIRE(high.mid >= 0 && high.mid <= 1);
    REQUIRE(high.treble >= 0 && high.treble <= 1);
    REQUIRE(high.onset >= 0 && high.onset <= 1);
    return true;
}

TEST_CASE(audio_silence_and_reset_clear_signal_history)
{
    REQUIRE(silent(audio_analysis_read(analysis)));
    push_tone(analysis, RATE, RATE, 1000, 0, 1);
    REQUIRE(silent(audio_analysis_read(analysis)));
    push_tone(analysis, RATE, RATE / 100, 1000, 0.8f, 1);
    REQUIRE(audio_analysis_read(analysis).level > 0);
    audio_analysis_reset(analysis);
    REQUIRE(silent(audio_analysis_read(analysis)));
    push_tone(analysis, RATE, RATE, 1000, 0, 1);
    REQUIRE(silent(audio_analysis_read(analysis)));
    push_tone(analysis, RATE, RATE / 100, 60, 0.8f, 1);
    float previous = audio_analysis_read(analysis).level;
    for (int i = 0; i < 200; i++) {
        push_tone(analysis, RATE, RATE / 100, 60, 0, 1);
        audio_analysis_frame frame = audio_analysis_read(analysis);
        REQUIRE(frame.level <= previous);
        previous = frame.level;
    }
    REQUIRE(silent(audio_analysis_read(analysis)));
    return true;
}

TEST_CASE(audio_onset_requires_rising_pcm_energy)
{
    push_tone(analysis, RATE, RATE, 1000, 0.2f, 1);
    REQUIRE(audio_analysis_read(analysis).onset == 0);
    push_tone(analysis, RATE, RATE / 100, 1000, 0.8f, 1);
    audio_analysis_frame peak = audio_analysis_read(analysis);
    REQUIRE(peak.onset > 0.8f);
    push_tone(analysis, RATE, RATE / 100, 1000, 0.8f, 1);
    REQUIRE(audio_analysis_read(analysis).onset < peak.onset);
    push_tone(analysis, RATE, RATE, 1000, 0.8f, 1);
    REQUIRE(audio_analysis_read(analysis).onset == 0);
    push_tone(analysis, RATE, RATE / 100, 1000, 0, 1);
    REQUIRE(audio_analysis_read(analysis).onset == 0);
    audio_analysis_reset(analysis);
    push_tone(analysis, RATE, RATE, 60, 0.8f, 1);
    REQUIRE(audio_analysis_read(analysis).onset == 0);
    return true;
}

TEST_CASE(audio_chunk_boundaries_do_not_change_results_or_allocate)
{
    enum
    {
        SAMPLE_COUNT = 12037
    };
    float samples[SAMPLE_COUNT];
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        float gain = i < 3207 ? 0.1f : 0.8f;
        samples[i] = gain * sin(TAU * 173 * i / RATE);
    }
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    audio_analysis_push(analysis, samples, SAMPLE_COUNT, 1, RATE);
    for (int at = 0; at < SAMPLE_COUNT;) {
        int count = 1 + at % 347;
        if (count > SAMPLE_COUNT - at) {
            count = SAMPLE_COUNT - at;
        }
        audio_analysis_push(other, samples + at, count, 1, RATE);
        at += count;
    }
    REQUIRE(near(audio_analysis_read(analysis), audio_analysis_read(other), 0));
    /* The next push also exercises the retained partial window. */
    audio_analysis_push(analysis, samples, 503, 1, RATE);
    for (int i = 0; i < 503; i++) {
        audio_analysis_push(other, samples + i, 1, 1, RATE);
    }
    xalloc_stats_get(&after);
    REQUIRE(near(audio_analysis_read(analysis), audio_analysis_read(other), 0));
    REQUIRE(before.calls == after.calls);
    return true;
}

TEST_CASE(audio_format_changes_discard_old_filter_and_window_state)
{
    push_tone(analysis, RATE, 473, 60, 0.8f, 1);
    push_tone(analysis, 44100, 44100, 1000, 0.5f, 1);
    push_tone(other, 44100, 44100, 1000, 0.5f, 1);
    REQUIRE(near(audio_analysis_read(analysis), audio_analysis_read(other), 0));
    audio_analysis_reset(other);
    push_tone(other, RATE, RATE, 1000, 0.5f, 1);
    REQUIRE(near(audio_analysis_read(analysis), audio_analysis_read(other), 0.01f));
    push_tone(analysis, 44100, 437, 60, 0.8f, 1);
    audio_analysis_reset(other);
    push_tone(analysis, 44100, 44100, 1000, 0.5f, 2);
    push_tone(other, 44100, 44100, 1000, 0.5f, 2);
    REQUIRE(near(audio_analysis_read(analysis), audio_analysis_read(other), 0));
    return true;
}

TEST_CASE(audio_antiphase_channels_keep_their_energy)
{
    push_tone(analysis, RATE, RATE, 1000, 0.8f, 1);
    push_tone(other, RATE, RATE, 1000, 0.8f, 2);
    REQUIRE(near(audio_analysis_read(analysis), audio_analysis_read(other), 0.000001f));
    audio_analysis_reset(other);
    push_tone(other, RATE, RATE, 1000, 0.8f, 8);
    REQUIRE(near(audio_analysis_read(analysis), audio_analysis_read(other), 0.000001f));
    return true;
}

static bool empty_snapshot(audio_measurement frame)
{
    return silent((
               audio_analysis_frame
           ){ frame.level, frame.bass, frame.mid, frame.treble, frame.onset }) &&
           !frame.active;
}

TEST_CASE(audio_source_changes_clear_published_levels)
{
    uint64_t before, after;
    REQUIRE(audio_source(queue, &before) == 0);
    REQUIRE(before > 0);
    REQUIRE(audio_get_analysis(queue).status == AUDIO_IDLE);
    audio_set_analysis(queue, (audio_measurement){ .level = 0.6f, .active = true, .status = AUDIO_READY });
    pin_value source = { .type = PIN_I32, .i = 1 };
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    REQUIRE(audio_source(queue, &after) == 1);
    REQUIRE(after > before);
    audio_measurement frame = audio_get_analysis(queue);
    REQUIRE(empty_snapshot(frame));
    REQUIRE(frame.source == 1 && frame.status == AUDIO_WAITING);
    source.i = 0;
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    REQUIRE(audio_source(queue, &before) == 0);
    REQUIRE(before > after);
    frame = audio_get_analysis(queue);
    REQUIRE(empty_snapshot(frame));
    REQUIRE(frame.source == 0 && frame.status == AUDIO_WAITING);
    return true;
}

TEST_CASE(audio_snapshot_binding_returns_host_measurements)
{
    pin_value source = { .type = PIN_I32, .i = 1 };
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    audio_measurement published = { .level = 0.2f,
                                    .bass = 0.3f,
                                    .mid = 0.4f,
                                    .treble = 0.5f,
                                    .onset = 0.6f,
                                    .active = true,
                                    .source = 1,
                                    .status = AUDIO_READY };
    audio_set_analysis(queue, published);
    audio_measurement frame = audio_get_analysis(queue);
    REQUIRE(frame.level == published.level);
    REQUIRE(frame.active && frame.source == 1 && frame.status == AUDIO_READY);
    pin_value output[8] = { 0 };
    REQUIRE(!AUDIO_ANALYSIS_CALL.invoke(queue, NULL, output));
    const float expected[] = { 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };
    for (int i = 0; i < 5; i++) {
        REQUIRE(output[i].type == PIN_F32 && output[i].f == expected[i]);
    }
    REQUIRE(output[5].type == PIN_BOOL && output[5].b);
    REQUIRE(output[6].type == PIN_I32 && output[6].i == 1);
    REQUIRE(output[7].type == PIN_I32 && output[7].i == AUDIO_READY);
    return true;
}

TEST_CASE(audio_invalid_and_duplicate_sources_preserve_state)
{
    pin_value source = { .type = PIN_I32, .i = 1 };
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    audio_set_analysis(
        queue,
        (audio_measurement){ .level = 0.7f, .active = true, .source = 1, .status = AUDIO_READY }
    );
    uint64_t before, after;
    REQUIRE(audio_source(queue, &before) == 1);
    uint64_t count = audio_count(queue), hash = audio_hash(queue);
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    REQUIRE(audio_source(queue, &after) == 1 && before == after);
    REQUIRE(audio_get_analysis(queue).level == 0.7f);
    const int invalid[] = { -1, 2 };
    for (int i = 0; i < 2; i++) {
        source.i = invalid[i];
        REQUIRE(AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
        REQUIRE(audio_source(queue, &after) == 1 && before == after);
        audio_measurement frame = audio_get_analysis(queue);
        REQUIRE(frame.level == 0.7f && frame.active && frame.source == 1);
        REQUIRE(frame.status == AUDIO_READY);
    }
    REQUIRE(audio_count(queue) == count && audio_hash(queue) == hash);
    return true;
}

TEST_CASE(audio_queue_reset_clears_capture_request_and_snapshot)
{
    pin_value source = { .type = PIN_I32, .i = 1 };
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    audio_set_analysis(
        queue,
        (audio_measurement){ .level = 0.7f,
                             .bass = 0.6f,
                             .mid = 0.5f,
                             .treble = 0.4f,
                             .onset = 0.3f,
                             .active = true,
                             .source = 1,
                             .status = AUDIO_READY }
    );
    uint64_t before, after, generation = audio_generation(queue);
    REQUIRE(audio_source(queue, &before) == 1);
    audio_reset(queue);
    REQUIRE(audio_source(queue, &after) == 0 && after > before);
    REQUIRE(audio_generation(queue) != generation);
    audio_measurement frame = audio_get_analysis(queue);
    REQUIRE(empty_snapshot(frame));
    REQUIRE(frame.source == 0 && frame.status == AUDIO_IDLE);
    return true;
}

void test_tool_audio_analysis()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(audio_bands_follow_tone_frequencies);
    RUN_TEST_CASE(audio_levels_follow_amplitude_and_stay_normalized);
    RUN_TEST_CASE(audio_silence_and_reset_clear_signal_history);
    RUN_TEST_CASE(audio_onset_requires_rising_pcm_energy);
    RUN_TEST_CASE(audio_chunk_boundaries_do_not_change_results_or_allocate);
    RUN_TEST_CASE(audio_format_changes_discard_old_filter_and_window_state);
    RUN_TEST_CASE(audio_antiphase_channels_keep_their_energy);
    RUN_TEST_CASE(audio_source_changes_clear_published_levels);
    RUN_TEST_CASE(audio_snapshot_binding_returns_host_measurements);
    RUN_TEST_CASE(audio_invalid_and_duplicate_sources_preserve_state);
    RUN_TEST_CASE(audio_queue_reset_clears_capture_request_and_snapshot);
    pu_setup(NULL, NULL);
}
