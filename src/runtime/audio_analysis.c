#include "runtime/audio_analysis.h"

#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CHANNELS 8
#define WINDOWS_PER_SECOND 100
#define BASS_HZ 250.0f
#define TREBLE_HZ 2500.0f
#define TAU 6.283185307179586f
#define SILENCE 0.00001f
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
};

audio_analysis *audio_analysis_new()
{
    return xcalloc(1, sizeof(audio_analysis));
}

void audio_analysis_free(audio_analysis *analysis)
{
    free(analysis);
}

void audio_analysis_reset(audio_analysis *analysis)
{
    memset(analysis, 0, sizeof(*analysis));
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
            analysis->energy[BASS] += (double)bass * bass;
            analysis->energy[MID] += (double)mid * mid;
            analysis->energy[TREBLE] += (double)treble * treble;
        }
        if (++analysis->count == analysis->window_frames) {
            finish_window(analysis);
        }
    }
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
