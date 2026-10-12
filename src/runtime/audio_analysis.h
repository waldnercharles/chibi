#ifndef CHIBI_AUDIO_ANALYSIS_H
#define CHIBI_AUDIO_ANALYSIS_H

#include <stdint.h>

typedef struct audio_analysis audio_analysis;
typedef struct
{
    float level, bass, mid, treble, onset;
} audio_analysis_frame;

typedef struct
{
    float tempo, phase, confidence, beat;
} audio_rhythm_frame;

audio_analysis *audio_analysis_new();
void audio_analysis_free(audio_analysis *analysis);
/* Clear all signal history when playback stops, seeks or changes source. */
void audio_analysis_reset(audio_analysis *analysis);
/* Interleaved float PCM, one to eight channels. No allocations. Format changes
 * reset signal history. Partial 10 ms windows survive across push calls. */
void audio_analysis_push(audio_analysis *analysis, const float *samples, int frames, int channels, int sample_rate);
/* Queued hosts include the original frame position, counting dropped frames.
 * A gap discards tracking history before accepting the next block. */
void audio_analysis_push_at(
    audio_analysis *analysis,
    const float *samples,
    int frames,
    int channels,
    int sample_rate,
    uint64_t position
);
/* Absolute RMS amplitudes in [0, 1], with 20 ms attack and 120 ms release.
 * The approximate bands split at 250 Hz and 2500 Hz. Onset reports positive
 * level jumps with an 80 ms release; silence never generates onsets. */
audio_analysis_frame audio_analysis_read(const audio_analysis *analysis);
/* Estimated tempo in BPM and beat phase [0, 1). Confidence is accumulated
 * rhythmic evidence, not a probability. Beat has an 80 ms release. Rhythm
 * needs several seconds to acquire; silence and stale onsets return zeros. */
audio_rhythm_frame audio_analysis_rhythm(const audio_analysis *analysis);

#endif
