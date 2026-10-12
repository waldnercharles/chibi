#include "runtime/audio_analysis.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdint.h>

#define CHARACTER_RATE 48000
#define CHARACTER_TAU 6.283185307179586
static audio_analysis *character_a, *character_b;

static void character_setup()
{
    character_a = audio_analysis_new();
    character_b = audio_analysis_new();
}

static void character_teardown()
{
    audio_analysis_free(character_a);
    audio_analysis_free(character_b);
}

static void character_signal(audio_analysis *analysis, int rate, float hz, float gain, int frames, int stereo)
{
    float samples[256 * 2];
    uint32_t random = 7654321;
    int channels = stereo ? 2 : 1;
    for (int at = 0; at < frames;) {
        int count = frames - at < 256 ? frames - at : 256;
        for (int i = 0; i < count; i++) {
            random = random * 1664525u + 1013904223u;
            float sample = gain * sin(CHARACTER_TAU * hz * (at + i) / rate);
            if (hz < 0) {
                sample = gain * ((float)(random >> 8) / 8388608 - 1);
            }
            if (hz == 0) {
                sample = (at + i) % (rate / 100) ? 0 : gain;
            }
            samples[i * channels] = sample;
            if (channels == 2) {
                samples[i * 2 + 1] = stereo == 2 ? -sample : sample;
                if (stereo == 3) {
                    samples[i * 2 + 1] = gain * sin(CHARACTER_TAU * 1700 *
                                                    (at + i) / rate);
                }
            }
        }
        audio_analysis_push(analysis, samples, count, channels, rate);
        at += count;
    }
}

static bool character_same(audio_character_frame a, audio_character_frame b)
{
    return a.centroid == b.centroid && a.flatness == b.flatness &&
           a.flux == b.flux && a.low_weight == b.low_weight &&
           a.width == b.width && a.crest == b.crest;
}

TEST_CASE(audio_character_separates_spectrum_at_equal_level)
{
    character_signal(character_a, CHARACTER_RATE, 120, .2f, CHARACTER_RATE, 0);
    character_signal(character_b, CHARACTER_RATE, 6000, .2f, CHARACTER_RATE, 0);
    audio_character_frame low = audio_analysis_character(character_a);
    audio_character_frame high = audio_analysis_character(character_b);
    REQUIRE(
        fabsf(
            audio_analysis_read(character_a).level -
            audio_analysis_read(character_b).level
        ) < .005f
    );
    REQUIRE(low.centroid > 90 && low.centroid < 160);
    REQUIRE(high.centroid > 5700 && high.centroid < 6300);
    REQUIRE(low.low_weight > .9f && high.low_weight < .01f);
    REQUIRE(low.flatness < .02f && high.flatness < .02f);
    /* Nyquist belongs to the advertised range at lower supported rates. */
    float nyquist[8000];
    for (int i = 0; i < 8000; i++) {
        nyquist[i] = i % 2 ? -.2f : .2f;
    }
    audio_analysis_push(character_a, nyquist, 8000, 1, 8000);
    low = audio_analysis_character(character_a);
    REQUIRE(low.centroid > 3995 && low.centroid <= 4000);
    return true;
}

TEST_CASE(audio_character_separates_noise_from_tones)
{
    character_signal(character_a, CHARACTER_RATE, 1000, .2f, CHARACTER_RATE, 0);
    character_signal(character_b, CHARACTER_RATE, -1, .244949f, CHARACTER_RATE, 0);
    audio_character_frame tone = audio_analysis_character(character_a);
    audio_character_frame noise = audio_analysis_character(character_b);
    REQUIRE(
        fabsf(
            audio_analysis_read(character_a).level -
            audio_analysis_read(character_b).level
        ) < .005f
    );
    REQUIRE(tone.flatness < .02f && noise.flatness > .35f && noise.flatness <= 1);
    REQUIRE(noise.centroid > 6500 && noise.centroid < 9500);
    REQUIRE(tone.flux < .01f && noise.flux > tone.flux + .01f);
    return true;
}

TEST_CASE(audio_character_measures_stereo_image_without_cancellation)
{
    const float expected[] = { 0, 0, 1, .5f };
    for (int stereo = 0; stereo < 4; stereo++) {
        audio_analysis_reset(character_a);
        character_signal(character_a, CHARACTER_RATE, 500, .2f, CHARACTER_RATE, stereo);
        audio_character_frame frame = audio_analysis_character(character_a);
        REQUIRE(fabsf(frame.width - expected[stereo]) < .02f);
        REQUIRE(frame.centroid > 450 && frame.centroid < 550);
    }
    return true;
}

TEST_CASE(audio_character_distinguishes_transients_and_spectral_changes)
{
    character_signal(character_a, CHARACTER_RATE, 1000, .05164f, CHARACTER_RATE, 0);
    character_signal(character_b, CHARACTER_RATE, 0, .8f, CHARACTER_RATE, 0);
    audio_character_frame tone = audio_analysis_character(character_a);
    audio_character_frame impulse = audio_analysis_character(character_b);
    REQUIRE(
        fabsf(
            audio_analysis_read(character_a).level -
            audio_analysis_read(character_b).level
        ) < .005f
    );
    REQUIRE(tone.crest > .25f && tone.crest < .35f);
    REQUIRE(impulse.crest > .85f && impulse.crest < 1);
    character_signal(character_a, CHARACTER_RATE, 6000, .05164f, 2048, 0);
    REQUIRE(audio_analysis_character(character_a).flux > tone.flux + .05f);
    return true;
}

TEST_CASE(audio_character_is_gain_invariant_above_silence)
{
    character_signal(character_a, CHARACTER_RATE, 1000, .02f, CHARACTER_RATE, 0);
    character_signal(character_b, CHARACTER_RATE, 1000, .2f, CHARACTER_RATE, 0);
    audio_character_frame quiet = audio_analysis_character(character_a);
    audio_character_frame loud = audio_analysis_character(character_b);
    REQUIRE(quiet.centroid > 900 && fabsf(quiet.centroid - loud.centroid) < 1);
    REQUIRE(fabsf(quiet.flatness - loud.flatness) < .001f);
    REQUIRE(fabsf(quiet.low_weight - loud.low_weight) < .001f);
    REQUIRE(fabsf(quiet.crest - loud.crest) < .001f);
    return true;
}

TEST_CASE(audio_character_is_chunk_invariant_and_resets_on_gaps_and_formats)
{
    float samples[12037];
    for (int i = 0; i < 12037; i++) {
        samples[i] = .2 * sin(CHARACTER_TAU * 733 * i / CHARACTER_RATE);
    }
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    audio_analysis_push_at(character_a, samples, 12037, 1, CHARACTER_RATE, 0);
    for (int at = 0; at < 12037;) {
        int count = 1 + at % 347;
        if (count > 12037 - at) {
            count = 12037 - at;
        }
        audio_analysis_push_at(character_b, samples + at, count, 1, CHARACTER_RATE, at);
        at += count;
    }
    REQUIRE(audio_analysis_character(character_a).centroid > 500);
    REQUIRE(
        character_same(audio_analysis_character(character_a), audio_analysis_character(character_b))
    );
    audio_analysis_push_at(character_a, samples, 731, 1, CHARACTER_RATE, 15000);
    REQUIRE(character_same(audio_analysis_character(character_a), (audio_character_frame){ 0 }));
    audio_analysis_reset(character_b);
    audio_analysis_push(character_a, samples, 12037, 1, 44100);
    audio_analysis_push(character_b, samples, 12037, 1, 44100);
    REQUIRE(
        character_same(audio_analysis_character(character_a), audio_analysis_character(character_b))
    );
    audio_analysis_reset(character_b);
    audio_analysis_push(character_a, samples, 12037 / 2, 2, 44100);
    audio_analysis_push(character_b, samples, 12037 / 2, 2, 44100);
    REQUIRE(
        character_same(audio_analysis_character(character_a), audio_analysis_character(character_b))
    );
    xalloc_stats_get(&after);
    REQUIRE(before.calls == after.calls);
    return true;
}

TEST_CASE(audio_character_requires_a_full_window_and_audible_pcm)
{
    character_signal(character_a, CHARACTER_RATE, 1000, .2f, 731, 0);
    REQUIRE(character_same(audio_analysis_character(character_a), (audio_character_frame){ 0 }));
    character_signal(character_a, CHARACTER_RATE, 1000, .2f, CHARACTER_RATE, 0);
    REQUIRE(audio_analysis_character(character_a).centroid > 900);
    character_signal(character_a, CHARACTER_RATE, 1000, 0, 2048, 0);
    REQUIRE(character_same(audio_analysis_character(character_a), (audio_character_frame){ 0 }));
    character_signal(character_a, CHARACTER_RATE, 1000, .001f, CHARACTER_RATE, 0);
    REQUIRE(character_same(audio_analysis_character(character_a), (audio_character_frame){ 0 }));
    audio_analysis_reset(character_a);
    REQUIRE(character_same(audio_analysis_character(character_a), (audio_character_frame){ 0 }));
    return true;
}

void test_tool_audio_character()
{
    pu_setup(character_setup, character_teardown);
    RUN_TEST_CASE(audio_character_separates_spectrum_at_equal_level);
    RUN_TEST_CASE(audio_character_separates_noise_from_tones);
    RUN_TEST_CASE(audio_character_measures_stereo_image_without_cancellation);
    RUN_TEST_CASE(audio_character_distinguishes_transients_and_spectral_changes);
    RUN_TEST_CASE(audio_character_is_gain_invariant_above_silence);
    RUN_TEST_CASE(audio_character_is_chunk_invariant_and_resets_on_gaps_and_formats);
    RUN_TEST_CASE(audio_character_requires_a_full_window_and_audible_pcm);
    pu_clear_setup();
}
