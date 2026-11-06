#include "render/audio_web.h"

#include "runtime/audio.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#define CAPTURE_SAMPLES 2048
static float capture_samples[CAPTURE_SAMPLES];

// clang-format off
EM_JS(void, capture_stop, (), {
    const capture = Module['chibiAudioCapture'];
    if (capture) capture.finish(0);
})

EM_JS(void, capture_start, (int generation, float *samples, int capacity), {
    const WAITING = 1, READY = 2, UNAVAILABLE = 3, DENIED = 4, ENDED = 5;
    const MAX_BLOCK_AGE_SECONDS = 0.25;
    const capture = { stream: null, context: null, source: null, node: null,
                      gesture: null, end: null, url: null };
    Module['chibiAudioCapture'] = capture;
    const current = () => Module['chibiAudioCapture'] === capture;
    const removeGesture = () => {
        if (!capture.gesture) return;
        window.removeEventListener('keydown', capture.gesture, true);
        window.removeEventListener('click', capture.gesture, true);
        capture.gesture = null;
    };
    capture.finish = state => {
        if (!current()) return;
        Module['chibiAudioCapture'] = null;
        removeGesture();
        window.removeEventListener('pagehide', capture.end);
        if (capture.node) {
            capture.node.port.onmessage = null;
            capture.node.onprocessorerror = null;
            capture.node.port.close();
            capture.node.disconnect();
        }
        if (capture.source) capture.source.disconnect();
        if (capture.stream) {
            for (const track of capture.stream.getTracks()) {
                track.removeEventListener('ended', capture.end);
                track.stop();
            }
        }
        if (capture.context) {
            capture.context.onstatechange = null;
            capture.context.close().catch(() => {});
        }
        if (capture.url) URL.revokeObjectURL(capture.url);
        _render_audio_web_status(generation, state);
    };
    capture.end = () => capture.finish(ENDED);
    window.addEventListener('pagehide', capture.end);
    _render_audio_web_status(generation, WAITING);
    if (!navigator.mediaDevices?.getDisplayMedia || !globalThis.AudioContext ||
        !globalThis.AudioWorkletNode) {
        capture.finish(UNAVAILABLE);
        return;
    }

    const begin = async () => {
        if (!current()) return;
        removeGesture();
        try {
            // Create/resume while the initiating gesture is still active.
            const context = new AudioContext();
            capture.context = context;
            context.onstatechange = () => {
                if (context.state === 'closed') capture.finish(ENDED);
            };
            const resumed = context.resume();
            resumed.catch(() => capture.finish(UNAVAILABLE));
            // Browsers require video even when only audio is consumed. These
            // hints offer system audio where supported; the user must still
            // select a source and enable its audio-sharing option in the picker.
            const stream = await navigator.mediaDevices.getDisplayMedia({
                video: true,
                audio: { suppressLocalAudioPlayback: false },
                systemAudio: 'include',
                windowAudio: 'system',
                selfBrowserSurface: 'exclude',
                preferCurrentTab: false,
            });
            if (!current()) {
                for (const track of stream.getTracks()) track.stop();
                return;
            }
            capture.stream = stream;
            const audioTracks = stream.getAudioTracks();
            if (!audioTracks.length || audioTracks[0].readyState !== 'live') {
                capture.finish(UNAVAILABLE);
                return;
            }
            for (const track of stream.getTracks()) {
                track.addEventListener('ended', capture.end);
            }
            await resumed;
            if (!current()) return;
            const processor = `
                class ChibiAudioCapture extends AudioWorkletProcessor {
                    constructor(options) {
                        super();
                        this.samples = new Float32Array(options.processorOptions.capacity);
                        this.count = 0;
                        this.channels = 0;
                    }
                    process(inputs) {
                        const channels = inputs[0];
                        if (!channels.length) return true;
                        const count = Math.min(channels.length, 2);
                        if (count !== this.channels) {
                            this.count = 0;
                            this.channels = count;
                        }
                        for (let i = 0; i < channels[0].length; i++) {
                            for (let channel = 0; channel < count; channel++) {
                                this.samples[this.count++] = channels[channel][i];
                            }
                            if (this.count === this.samples.length) {
                                const capacity = this.samples.length;
                                this.port.postMessage({
                                    samples: this.samples,
                                    channels: count,
                                    endTime: (currentFrame + i + 1) / sampleRate,
                                }, [this.samples.buffer]);
                                this.samples = new Float32Array(capacity);
                                this.count = 0;
                            }
                        }
                        // Outputs remain zero. Captured audio is never monitored.
                        return true;
                    }
                }
                registerProcessor('chibi-audio-capture', ChibiAudioCapture);
            `;
            capture.url = URL.createObjectURL(new Blob([processor], { type: 'text/javascript' }));
            await context.audioWorklet.addModule(capture.url);
            if (!current()) return;
            URL.revokeObjectURL(capture.url);
            capture.url = null;
            const node = new AudioWorkletNode(context, 'chibi-audio-capture', {
                numberOfInputs: 1,
                numberOfOutputs: 1,
                outputChannelCount: [1],
                processorOptions: { capacity },
            });
            capture.node = node;
            node.onprocessorerror = () => capture.finish(UNAVAILABLE);
            node.port.onmessage = event => {
                if (!current()) return;
                const block = event.data;
                // Main-thread stalls can queue worklet messages. Their production
                // time, rather than delivery time, determines whether PCM is live.
                const age = Math.max(0, context.currentTime - block.endTime);
                if (age >= MAX_BLOCK_AGE_SECONDS) return;
                HEAPF32.set(block.samples, samples >>> 2);
                _render_audio_web_samples(generation, samples,
                                          block.samples.length / block.channels,
                                          block.channels, context.sampleRate,
                                          Math.ceil(age * 1000));
            };
            capture.source = context.createMediaStreamSource(new MediaStream(audioTracks));
            capture.source.connect(node);
            // The graph needs an output to keep processing; the worklet outputs silence.
            node.connect(context.destination);
            _render_audio_web_status(generation, READY);
        } catch (error) {
            capture.finish(error.name === 'NotAllowedError' || error.name === 'AbortError'
                           ? DENIED : UNAVAILABLE);
        }
    };
    if (navigator.userActivation?.isActive) {
        begin();
        return;
    }
    capture.gesture = event => {
        if (!event.isTrusted || event.repeat || event.key === 'Escape') return;
        if (navigator.userActivation && !navigator.userActivation.isActive) return;
        begin();
    };
    window.addEventListener('keydown', capture.gesture, true);
    window.addEventListener('click', capture.gesture, true);
})
// clang-format on

void render_audio_web_start(int generation)
{
    capture_stop();
    capture_start(generation, capture_samples, CAPTURE_SAMPLES);
}

void render_audio_web_stop()
{
    capture_stop();
}
#else
void render_audio_web_start(int generation)
{
    render_audio_web_status(generation, AUDIO_UNAVAILABLE);
}

void render_audio_web_stop() {}
#endif
