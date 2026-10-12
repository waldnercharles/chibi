#include "runtime/audio_analysis.h"

#include "BTT.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define CHANNELS 8
#define WINDOWS_PER_SECOND 100
#define BASS_HZ 250.0f
#define TREBLE_HZ 2500.0f
#define TAU 6.283185307179586f
#define SILENCE 0.00001f
#define RHYTHM_SILENCE 0.0025f
#define RHYTHM_HOP 128
#define SPECTRUM_BINS (BTT_SUGGESTED_SPECTRAL_FLUX_STFT_LEN / 2 + 1)
enum
{
    LEVEL,
    BASS,
    MID,
    TREBLE,
    BANDS
};
struct audio_analysis
{
    int channels, sample_rate, window_frames, count;
    float low[CHANNELS], high[CHANNELS];
    float low_gain, high_gain, attack, release, onset_release;
    double energy[BANDS];
    float amplitude[BANDS], previous_level, onset;
    BTT *tracker;
    double channel_energy[CHANNELS];
    float rhythm_samples[RHYTHM_HOP];
    int rhythm_count, rhythm_channel, quiet_windows, onsets;
    unsigned long long rhythm_clock, last_onset, last_beat, pending_beat;
    bool heard_beat, beat_pending;
    uint64_t next_position;
    bool positioned;
    audio_character_frame character;
    float spectrum[SPECTRUM_BINS], character_gain, flux_release, image_gain;
    double spectrum_sum, peak_energy, stereo_side, stereo_total;
    bool spectrum_ready;
};

static void measured_spectrum(void *context, const float *magnitudes, int bins, unsigned long long sample_time)
{
    audio_analysis *analysis = context;
    assert(bins == SPECTRUM_BINS);
    int length = (bins - 1) * 2;
    if (sample_time < (unsigned)length || analysis->previous_level <= RHYTHM_SILENCE) {
        return;
    }
    double magnitude_sum = 0, power_sum = 0, weighted = 0, low = 0, rise = 0;
    int count = 0;
    for (int i = 1; i < bins; i++) {
        double hz = (double)i * analysis->sample_rate / length;
        if (hz < 40 || hz > 16000) {
            continue;
        }
        double magnitude = magnitudes[i], power = magnitude * magnitude;
        magnitude_sum += magnitude;
        power_sum += power;
        weighted += magnitude * hz;
        low += hz < BASS_HZ ? power : 0;
        rise += fmax(0, magnitude - analysis->spectrum[i]);
        count++;
    }
    if (power_sum <= 1e-12) {
        analysis->character = (audio_character_frame){ 0 };
        analysis->spectrum_ready = false;
        return;
    }
    double log_power = 0;
    for (int i = 1; i < bins; i++) {
        double hz = (double)i * analysis->sample_rate / length;
        if (hz < 40 || hz > 16000) {
            continue;
        }
        double magnitude = magnitudes[i];
        log_power += log(fmax(magnitude * magnitude, power_sum * 1e-12));
        analysis->spectrum[i] = magnitude;
    }
    float gain = analysis->spectrum_ready ? analysis->character_gain : 1;
    audio_character_frame *character = &analysis->character;
    character->centroid += (weighted / magnitude_sum - character->centroid) * gain;
    character->flatness += (fmin(1, exp(log_power / count) * count / power_sum) -
                            character->flatness) *
                           gain;
    character->low_weight += (low / power_sum - character->low_weight) * gain;
    float flux = analysis->spectrum_ready
                   ? rise / (magnitude_sum + analysis->spectrum_sum)
                   : 0;
    character->flux = fmaxf(flux, character->flux * analysis->flux_release);
    analysis->spectrum_sum = magnitude_sum;
    analysis->spectrum_ready = true;
}

static void heard_onset(void *context, unsigned long long sample)
{
    audio_analysis *analysis = context;
    analysis->last_onset = sample;
    if (analysis->onsets < 2) {
        analysis->onsets++;
    }
}

static void heard_beat(void *context, unsigned long long sample)
{
    audio_analysis *analysis = context;
    analysis->pending_beat = sample;
    analysis->beat_pending = true;
}

audio_analysis *audio_analysis_new()
{
    audio_analysis *analysis = xcalloc(1, sizeof(audio_analysis));
    analysis->tracker = btt_new_default();
    assert(analysis->tracker);
    btt_set_onset_threshold_min(analysis->tracker, 0.5);
    btt_set_onset_tracking_callback(analysis->tracker, heard_onset, analysis);
    btt_set_beat_tracking_callback(analysis->tracker, heard_beat, analysis);
    btt_set_spectrum_callback(analysis->tracker, measured_spectrum, analysis);
    return analysis;
}

void audio_analysis_free(audio_analysis *analysis)
{
    btt_destroy(analysis->tracker);
    free(analysis);
}

void audio_analysis_reset(audio_analysis *analysis)
{
    BTT *tracker = analysis->tracker;
    btt_clear(tracker);
    memset(analysis, 0, sizeof(*analysis));
    analysis->tracker = tracker;
}

static float normalized(float value)
{
    if (value < SILENCE) {
        return 0;
    }
    return fminf(value, 1);
}

static void configure(audio_analysis *analysis, int channels, int sample_rate)
{
    audio_analysis_reset(analysis);
    analysis->channels = channels;
    analysis->sample_rate = sample_rate;
    if (sample_rate >= BTT_MIN_SAMPLE_RATE && sample_rate <= BTT_MAX_SAMPLE_RATE) {
        btt_set_sample_rate(analysis->tracker, sample_rate);
        btt_set_analysis_latency_onset_adjustment(
            analysis->tracker,
            (int)round(857.0 * sample_rate / 44100)
        );
        btt_set_analysis_latency_beat_adjustment(
            analysis->tracker,
            (int)round(1270.0 * sample_rate / 44100)
        );
    }
    analysis->window_frames = sample_rate / WINDOWS_PER_SECOND;
    if (!analysis->window_frames) {
        analysis->window_frames = 1;
    }
    analysis->low_gain = 1 - expf(-TAU * BASS_HZ / sample_rate);
    analysis->high_gain = 1 - expf(-TAU * TREBLE_HZ / sample_rate);
    float seconds = (float)analysis->window_frames / sample_rate;
    analysis->attack = 1 - expf(-seconds / 0.02f);
    analysis->release = 1 - expf(-seconds / 0.12f);
    analysis->onset_release = expf(-seconds / 0.08f);
    analysis->image_gain = 1 - expf(-seconds / 0.15f);
    analysis->character_gain = 1 - expf(-(float)RHYTHM_HOP / sample_rate / 0.15f);
    analysis->flux_release = expf(-(float)RHYTHM_HOP / sample_rate / 0.08f);
}

static void finish_window(audio_analysis *analysis)
{
    float rms[BANDS];
    for (int band = 0; band < BANDS; band++) {
        rms[band] = sqrt(
            analysis->energy[band] / (analysis->window_frames * analysis->channels)
        );
        float before = analysis->amplitude[band];
        float gain = rms[band] > before ? analysis->attack : analysis->release;
        analysis->amplitude[band] = before + gain * (rms[band] - before);
        analysis->energy[band] = 0;
    }
    /* Compare adjacent PCM windows. The relative threshold rejects ordinary
     * RMS ripple from a sustained tone; the absolute floor rejects noise. */
    float rise = rms[LEVEL] - analysis->previous_level;
    float threshold = fmaxf(0.02f, analysis->previous_level * 0.25f);
    float onset = normalized((rise - threshold) * 4);
    analysis->onset = fmaxf(onset, analysis->onset * analysis->onset_release);
    analysis->previous_level = rms[LEVEL];
    if (rms[LEVEL] <= RHYTHM_SILENCE) {
        analysis->character = (audio_character_frame){ 0 };
        analysis->spectrum_ready = false;
        analysis->spectrum_sum = 0;
        memset(analysis->spectrum, 0, sizeof(analysis->spectrum));
    } else {
        float width = analysis->stereo_total > 0
                        ? analysis->stereo_side / analysis->stereo_total
                        : 0;
        float crest = fmaxf(0, 1 - rms[LEVEL] / sqrt(analysis->peak_energy));
        analysis->character.width += (width - analysis->character.width) *
                                     analysis->image_gain;
        analysis->character.crest += (crest - analysis->character.crest) *
                                     analysis->image_gain;
    }
    analysis->peak_energy = analysis->stereo_side = analysis->stereo_total = 0;
    /* Select one stable loud channel, rather than cancelling antiphase stereo.
     * The 40% margin prevents switching on small left/right balance changes. */
    int loudest = analysis->rhythm_channel;
    for (int channel = 0; channel < analysis->channels; channel++) {
        if (analysis->channel_energy[channel] > analysis->channel_energy[loudest] * 1.4) {
            loudest = channel;
        }
    }
    analysis->rhythm_channel = loudest;
    memset(analysis->channel_energy, 0, sizeof(analysis->channel_energy));
    analysis->quiet_windows = rms[LEVEL] <= RHYTHM_SILENCE
                                ? analysis->quiet_windows + 1
                                : 0;
    if (analysis->quiet_windows == 150) {
        btt_clear(analysis->tracker);
        analysis->rhythm_clock = analysis->rhythm_count = analysis->onsets = 0;
        analysis->last_onset = analysis->last_beat = 0;
        analysis->heard_beat = analysis->beat_pending = false;
    }
    if (analysis->quiet_windows > 150) {
        analysis->quiet_windows = 151;
    }
    analysis->count = 0;
}

void audio_analysis_push(audio_analysis *analysis, const float *samples, int frames, int channels, int sample_rate)
{
    assert(frames >= 0 && channels > 0 && channels <= CHANNELS && sample_rate > 0);
    assert(samples || !frames);
    if (analysis->sample_rate != sample_rate || analysis->channels != channels) {
        configure(analysis, channels, sample_rate);
    }
    for (int frame = 0; frame < frames; frame++) {
        float mono = samples[analysis->rhythm_channel];
        if (channels >= 2) {
            double left = samples[0], right = samples[1];
            analysis->stereo_side += (left - right) * (left - right);
            analysis->stereo_total += 2 * (left * left + right * right);
        }
        double frame_energy = 0;
        for (int channel = 0; channel < channels; channel++) {
            float sample = *samples++;
            assert(isfinite(sample));
            analysis->low[channel] += analysis->low_gain *
                                      (sample - analysis->low[channel]);
            analysis->high[channel] += analysis->high_gain *
                                       (sample - analysis->high[channel]);
            float bass = analysis->low[channel];
            float mid = analysis->high[channel] - bass;
            float treble = sample - analysis->high[channel];
            analysis->energy[LEVEL] += (double)sample * sample;
            frame_energy += (double)sample * sample;
            analysis->channel_energy[channel] += (double)sample * sample;
            analysis->energy[BASS] += (double)bass * bass;
            analysis->energy[MID] += (double)mid * mid;
            analysis->energy[TREBLE] += (double)treble * treble;
        }
        analysis->peak_energy = fmax(analysis->peak_energy, frame_energy / channels);
        if (sample_rate >= BTT_MIN_SAMPLE_RATE && sample_rate <= BTT_MAX_SAMPLE_RATE) {
            analysis->rhythm_samples[analysis->rhythm_count++] = mono;
            analysis->rhythm_clock++;
            if (analysis->rhythm_count == RHYTHM_HOP) {
                btt_process(analysis->tracker, analysis->rhythm_samples, RHYTHM_HOP);
                analysis->rhythm_count = 0;
            }
            if (analysis->beat_pending &&
                analysis->pending_beat <= analysis->rhythm_clock) {
                analysis->last_beat = analysis->pending_beat;
                analysis->heard_beat = true;
                analysis->beat_pending = false;
            }
        }
        if (++analysis->count == analysis->window_frames) {
            finish_window(analysis);
        }
    }
}

void audio_analysis_push_at(
    audio_analysis *analysis,
    const float *samples,
    int frames,
    int channels,
    int sample_rate,
    uint64_t position
)
{
    if (analysis->positioned && position != analysis->next_position) {
        audio_analysis_reset(analysis);
    }
    audio_analysis_push(analysis, samples, frames, channels, sample_rate);
    analysis->next_position = position + frames;
    analysis->positioned = true;
}

audio_analysis_frame audio_analysis_read(const audio_analysis *analysis)
{
    return (audio_analysis_frame){
        .level = normalized(analysis->amplitude[LEVEL]),
        .bass = normalized(analysis->amplitude[BASS]),
        .mid = normalized(analysis->amplitude[MID]),
        .treble = normalized(analysis->amplitude[TREBLE]),
        .onset = normalized(analysis->onset),
    };
}

audio_rhythm_frame audio_analysis_rhythm(const audio_analysis *analysis)
{
    if (analysis->previous_level <= RHYTHM_SILENCE || analysis->onsets < 2 ||
        !analysis->heard_beat ||
        analysis->rhythm_clock - analysis->last_onset > analysis->sample_rate * 1.5) {
        return (audio_rhythm_frame){ 0 };
    }
    double tempo = btt_get_tempo_bpm(analysis->tracker);
    if (tempo <= 0) {
        return (audio_rhythm_frame){ 0 };
    }
    double age = (double)(analysis->rhythm_clock - analysis->last_beat) /
                 analysis->sample_rate;
    return (audio_rhythm_frame){
        .tempo = tempo,
        .phase = fmod(age * tempo / 60, 1),
        .confidence = fmin(1, btt_get_tempo_certainty(analysis->tracker)),
        .beat = exp(-age / 0.08),
    };
}

audio_character_frame audio_analysis_character(const audio_analysis *analysis)
{
    if (!analysis->spectrum_ready || analysis->previous_level <= RHYTHM_SILENCE) {
        return (audio_character_frame){ 0 };
    }
    return analysis->character;
}
