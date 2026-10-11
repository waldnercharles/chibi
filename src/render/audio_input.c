#include "render/audio_input.h"

#include "platform/cute.h"
#include "render/audio_web.h"
#include "runtime/audio_analysis.h"
#include "support/xalloc.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#define INPUT_BLOCKS 16
#define INPUT_FRAMES 1024
#define INPUT_CHANNELS 8
#define INPUT_STALE_MS 250
typedef struct
{
    float samples[INPUT_FRAMES * INPUT_CHANNELS];
    int frames, channels, rate;
    uint64_t timestamp;
} audio_block;
struct audio_input
{
    audio_analysis *analysis;
    audio_block blocks[INPUT_BLOCKS];
    _Atomic unsigned read, write;
    SDL_AudioDeviceID output;
    SDL_AudioStream *capture;
    SDL_AudioDeviceID capture_device, capture_physical;
    _Atomic bool removed;
    audio *owner;
    uint64_t generation, serial, last_data;
    int source, web_generation;
    audio_status status;
};
/* The browser owns one audio capture stream, just as Cute owns one mixer. */
static audio_input *web_input;
static audio_input *output_input;
#ifdef __EMSCRIPTEN__
static unsigned web_generation;
#endif

/* Single producer: either the output callback, recording callback, or web
 * message handler. Drop new blocks when full; the callback never waits. */
static void samples(audio_input *input, const float *pcm, int frames, int channels, int rate, uint64_t timestamp)
{
    if (channels < 1 || channels > INPUT_CHANNELS) {
        return;
    }
    while (frames > 0) {
        unsigned write = atomic_load_explicit(&input->write, memory_order_relaxed);
        unsigned next = (write + 1) % INPUT_BLOCKS;
        if (next == atomic_load_explicit(&input->read, memory_order_acquire)) {
            return;
        }
        int count = frames < INPUT_FRAMES ? frames : INPUT_FRAMES;
        audio_block *block = &input->blocks[write];
        memcpy(block->samples, pcm, (size_t)count * channels * sizeof *pcm);
        block->frames = count;
        block->channels = channels;
        block->rate = rate;
        block->timestamp = timestamp;
        atomic_store_explicit(&input->write, next, memory_order_release);
        pcm += count * channels;
        frames -= count;
    }
}
static void postmix(void *user, const SDL_AudioSpec *spec, float *pcm, int bytes)
{
    samples(user, pcm, bytes / (int)sizeof(float) / spec->channels, spec->channels, spec->freq, SDL_GetTicks());
}
static bool device_event(void *user, SDL_Event *event)
{
    audio_input *input = user;
    if (event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED &&
        (event->adevice.which == input->capture_device ||
         event->adevice.which == input->capture_physical)) {
        atomic_store(&input->removed, true);
    }
    return true;
}
static void close_capture(audio_input *input)
{
    SDL_RemoveEventWatch(device_event, input);
    SDL_DestroyAudioStream(input->capture);
    input->capture = NULL;
}
#ifndef __EMSCRIPTEN__
static void recorded(void *user, SDL_AudioStream *stream, int, int)
{
    float pcm[INPUT_FRAMES * 2];
    int bytes;
    while ((bytes = SDL_GetAudioStreamData(stream, pcm, sizeof pcm)) > 0) {
        samples(user, pcm, bytes / (int)sizeof(float) / 2, 2, 44100, SDL_GetTicks());
    }
}
static bool loopback(const char *name)
{
    if (!strcmp(name, "Background Music")) {
        return true;
    }
    const char *known[] = { "BlackHole", "Soundflower", "Stereo Mix", "CABLE Output", "Monitor of" };
    for (int i = 0; i < (int)(sizeof known / sizeof *known); i++) {
        if (strstr(name, known[i])) {
            return true;
        }
    }
    return false;
}
static void capture(audio_input *input)
{
    const char *selected = getenv("CHIBI_AUDIO_INPUT");
    int count;
    SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
    SDL_AudioDeviceID device = 0;
    for (int i = 0; i < count; i++) {
        const char *name = SDL_GetAudioDeviceName(devices[i]);
        if (name && (selected ? !strcmp(name, selected) : loopback(name))) {
            device = devices[i];
            break;
        }
    }
    SDL_free(devices);
    if (!device) {
        input->status = AUDIO_UNAVAILABLE;
        return;
    }
    SDL_AudioSpec spec = { .format = SDL_AUDIO_F32, .channels = 2, .freq = 44100 };
    input->capture = SDL_OpenAudioDeviceStream(device, &spec, recorded, input);
    if (!input->capture) {
        input->status = AUDIO_UNAVAILABLE;
        return;
    }
    input->capture_device = SDL_GetAudioStreamDevice(input->capture);
    input->capture_physical = device;
    atomic_store(&input->removed, false);
    if (!SDL_AddEventWatch(device_event, input) ||
        !SDL_ResumeAudioStreamDevice(input->capture)) {
        close_capture(input);
        input->status = AUDIO_UNAVAILABLE;
        return;
    }
    input->status = AUDIO_READY;
}
#endif
EMSCRIPTEN_KEEPALIVE void render_audio_web_status(int generation, int state)
{
    audio_input *input = web_input;
    if (!input || input->web_generation != generation) {
        return;
    }
    assert(state >= AUDIO_IDLE && state <= AUDIO_ENDED);
    input->status = state;
    if (state != AUDIO_READY) {
        audio_analysis_reset(input->analysis);
        input->last_data = 0;
    }
}
EMSCRIPTEN_KEEPALIVE void render_audio_web_samples(
    int generation,
    const float *pcm,
    int frames,
    int channels,
    int rate,
    int age_ms
)
{
    audio_input *input = web_input;
    if (input && input->web_generation == generation && input->status == AUDIO_READY) {
        assert(age_ms >= 0);
        uint64_t now = SDL_GetTicks();
        uint64_t timestamp = (uint64_t)age_ms < now ? now - (uint64_t)age_ms : 0;
        samples(input, pcm, frames, channels, rate, timestamp);
    }
}
audio_input *audio_input_new()
{
    audio_input *input = xcalloc(1, sizeof *input);
    input->analysis = audio_analysis_new();
    return input;
}
void audio_input_reset(audio_input *input)
{
    if (output_input == input) {
        SDL_SetAudioPostmixCallback(input->output, NULL, NULL);
        output_input = NULL;
    }
    input->output = 0;
    if (input->capture) {
        close_capture(input);
    }
    if (web_input == input) {
        web_input = NULL;
        render_audio_web_stop();
    }
    atomic_store(&input->read, 0);
    atomic_store(&input->write, 0);
    input->owner = NULL;
    input->last_data = 0;
    input->status = AUDIO_IDLE;
    audio_analysis_reset(input->analysis);
}
void audio_input_free(audio_input *input)
{
    audio_input_reset(input);
    audio_analysis_free(input->analysis);
    free(input);
}
static void start(audio_input *input, audio *audio, int source, uint64_t serial)
{
    audio_input_reset(input);
    input->owner = audio;
    input->generation = audio_generation(audio);
    input->serial = serial;
    input->source = source;
    if (!source) {
        SDL_AudioDeviceID device = cf_audio_get_sdl_device();
        input->status = AUDIO_UNAVAILABLE;
        if (device && SDL_SetAudioPostmixCallback(device, postmix, input)) {
            if (output_input) {
                output_input->status = AUDIO_ENDED;
                output_input->last_data = 0;
                audio_analysis_reset(output_input->analysis);
            }
            output_input = input;
            input->output = device;
            input->status = AUDIO_READY;
        }
        return;
    }
    input->status = AUDIO_WAITING;
#ifdef __EMSCRIPTEN__
    if (web_input) {
        web_input->status = AUDIO_ENDED;
        web_input->last_data = 0;
        audio_analysis_reset(web_input->analysis);
    }
    web_input = input;
    input->web_generation = (int)++web_generation;
    render_audio_web_start(input->web_generation);
#else
    capture(input);
#endif
}
void audio_input_update(audio_input *input, audio *audio, bool playing)
{
    uint64_t serial;
    int source = audio_source(audio, &serial);
    if (audio != input->owner || input->generation != audio_generation(audio) ||
        serial != input->serial) {
        start(input, audio, source, serial);
    }
    if (input->capture && atomic_load(&input->removed)) {
        close_capture(input);
        input->status = AUDIO_ENDED;
        input->last_data = 0;
    }
    unsigned read = atomic_load_explicit(&input->read, memory_order_relaxed);
    unsigned end = atomic_load_explicit(&input->write, memory_order_acquire);
    uint64_t now = SDL_GetTicks();
    while (read != end) {
        audio_block *block = &input->blocks[read];
        if (playing && input->status == AUDIO_READY &&
            now - block->timestamp < INPUT_STALE_MS) {
            audio_analysis_push(
                input->analysis,
                block->samples,
                block->frames,
                block->channels,
                block->rate
            );
            input->last_data = block->timestamp;
        }
        read = (read + 1) % INPUT_BLOCKS;
        atomic_store_explicit(&input->read, read, memory_order_release);
    }
    bool active = playing && input->status == AUDIO_READY && input->last_data &&
                  now - input->last_data < INPUT_STALE_MS;
    if (!active) {
        audio_analysis_reset(input->analysis);
    }
    audio_analysis_frame frame = audio_analysis_read(input->analysis);
    audio_set_analysis(
        audio,
        (audio_measurement){ .level = frame.level,
                             .bass = frame.bass,
                             .mid = frame.mid,
                             .treble = frame.treble,
                             .onset = frame.onset,
                             .active = active,
                             .source = source,
                             .status = input->status }
    );
}
