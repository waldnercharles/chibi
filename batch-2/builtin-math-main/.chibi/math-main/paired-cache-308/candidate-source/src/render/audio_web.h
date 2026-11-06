#ifndef CHIBI_RENDER_AUDIO_WEB_H
#define CHIBI_RENDER_AUDIO_WEB_H

/* One capture per browser player. Start requires current user activation or
 * waits for the next trusted key/click. Stop also cancels a pending request. */
void render_audio_web_start(int generation);
void render_audio_web_stop();

/* Implemented by the playback host. Browser callbacks carry the generation
 * supplied to start; the host ignores callbacks for previous owners. Status:
 * 0 idle, 1 waiting, 2 ready, 3 unavailable, 4 denied, 5 ended.
 * Samples are interleaved PCM with one or two channels, valid only until the
 * callback returns. age_ms preserves the PCM's age across delayed worklet
 * messages. Both callbacks run on the browser main thread. */
void render_audio_web_status(int generation, int state);
void render_audio_web_samples(
    int generation,
    const float *samples,
    int frames,
    int channels,
    int sample_rate,
    int age_ms
);

#endif
