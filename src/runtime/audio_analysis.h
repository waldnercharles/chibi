#ifndef CHIBI_AUDIO_ANALYSIS_H
#define CHIBI_AUDIO_ANALYSIS_H

typedef struct audio_analysis audio_analysis;
typedef struct
{
    float level, bass, mid, treble, onset;
} audio_analysis_frame;

audio_analysis *audio_analysis_new();
void audio_analysis_free(audio_analysis *analysis);
/* Clear all signal history when playback stops, seeks or changes source. */
void audio_analysis_reset(audio_analysis *analysis);
/* Interleaved float PCM, one to eight channels. No allocations. Format changes
 * reset signal history. Partial 10 ms windows survive across push calls. */
void audio_analysis_push(audio_analysis *analysis, const float *samples, int frames, int channels, int sample_rate);
/* Absolute RMS amplitudes in [0, 1], with 20 ms attack and 120 ms release.
 * The approximate bands split at 250 Hz and 2500 Hz. Onset reports positive
 * level jumps with an 80 ms release; silence never generates onsets. */
audio_analysis_frame audio_analysis_read(const audio_analysis *analysis);

#endif
