#include "render/audio.h"

#include "platform/cute.h"
#include "render/audio_input.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <SDL3/SDL_init.h>
#include <assert.h>
#include <stdlib.h>

#define VOICES 64
typedef struct
{
    CF_Audio audio;
    CF_Sound sound;
    bool owned; /* a tone's own source, not a library sound */
} voice;
struct render_audio
{
    audio_input *input;
    voice voices[VOICES];
    int next;
    const audio *source;
    uint64_t generation, music_serial;
    CF_Audio *sounds; /* decoded on first play, by library index */
    float volumes[AUDIO_BUSES];
    bool music, paused; /* music: the mixer may hold a track of ours */
};
render_audio *render_audio_new()
{
    render_audio *renderer = xcalloc(1, sizeof(render_audio));
    renderer->input = audio_input_new();
    for (int i = 0; i < AUDIO_BUSES; i++) {
        renderer->volumes[i] = 1;
    }
    return renderer;
}
static void release(voice *v)
{
    if (!v->audio.id) {
        return;
    }
    if (cf_sound_is_active(v->sound)) {
        cf_sound_stop(v->sound);
    }
    if (v->owned) {
        cf_audio_destroy(v->audio);
    }
    *v = (voice){ 0 };
}
static void set_volume(render_audio *renderer, audio_bus bus, float volume)
{
    if (renderer->volumes[bus] == volume) {
        return;
    }
    renderer->volumes[bus] = volume;
    switch (bus) {
        case AUDIO_MASTER: cf_audio_set_global_volume(volume); break;
        case AUDIO_MUSIC: cf_music_set_volume(volume); break;
        case AUDIO_SOUNDS: cf_audio_set_sound_volume(volume); break;
        case AUDIO_BUSES: assert(false);
    }
}
static void set_paused(render_audio *renderer, bool paused)
{
    if (renderer->paused != paused) {
        renderer->paused = paused;
        cf_audio_set_pause(paused);
    }
}
/* Silences everything this host started and restores the mixer. */
static void forget(render_audio *renderer)
{
    audio_input_reset(renderer->input);
    for (int i = 0; i < VOICES; i++) {
        release(&renderer->voices[i]);
    }
    if (renderer->music) {
        cf_music_stop(0);
        renderer->music = false;
    }
    renderer->music_serial = 0;
    set_paused(renderer, false);
    /* The mixer defers freeing a source until its last voice stops. */
    for (int i = 0; i < acount(renderer->sounds); i++) {
        if (renderer->sounds[i].id) {
            cf_audio_destroy(renderer->sounds[i]);
        }
    }
    afree(renderer->sounds);
    for (int i = 0; i < AUDIO_BUSES; i++) {
        set_volume(renderer, i, 1);
    }
}
void render_audio_free(render_audio *renderer)
{
    forget(renderer);
    audio_input_free(renderer->input);
    free(renderer);
}
static CF_Audio decoded(render_audio *renderer, const audio *queue, int index)
{
    assert(index < acount(renderer->sounds));
    CF_Audio *cached = &renderer->sounds[index];
    if (cached->id) {
        return *cached;
    }
    size_t length;
    void *data = (void *)audio_sound(queue, index, &length);
    switch (sound_format_of(data, length)) {
        case SOUND_WAV:
            *cached = cf_audio_load_wav_from_memory(data, (int)length);
            break;
        case SOUND_OGG:
            *cached = cf_audio_load_ogg_from_memory(data, (int)length);
            break;
        case SOUND_INVALID: assert(false);
    }
    return *cached;
}
static void follow_music(render_audio *renderer, const audio *queue)
{
    float fade;
    uint64_t serial;
    int track = audio_music(queue, &fade, &serial);
    if (serial == renderer->music_serial) {
        return;
    }
    renderer->music_serial = serial;
    /* Cute leaves a track it replaces playing; a fading one stays ours until
     * forget cuts it. */
    if (renderer->music) {
        cf_music_stop(track < 0 ? fade : 0);
    }
    if (track < 0) {
        return;
    }
    CF_Audio source = decoded(renderer, queue, track);
    if (!source.id) {
        return;
    }
    cf_music_set_loop(true);
    cf_music_play(source, fade);
    renderer->music = true;
}
static void play(render_audio *renderer, const audio *queue, audio_voice played)
{
    voice v = { .owned = played.sound < 0 };
    if (v.owned) {
        int size;
        unsigned char *wav = tone_wav(played.tone, &size);
        v.audio = cf_audio_load_wav_from_memory(wav, size);
        free(wav);
    } else {
        v.audio = decoded(renderer, queue, played.sound);
    }
    if (!v.audio.id) {
        return;
    }
    voice *slot = &renderer->voices[renderer->next];
    renderer->next = (renderer->next + 1) % VOICES;
    release(slot);
    CF_SoundParams params = cf_sound_params_defaults();
    params.volume = played.volume;
    params.pan = played.pan;
    v.sound = cf_play_sound(v.audio, params);
    *slot = v;
}
/* A new session, its end, a reset or a new sound starts over. */
static void follow_source(render_audio *renderer, const audio *queue)
{
    if (renderer->source == queue &&
        (!queue || renderer->generation == audio_generation(queue))) {
        return;
    }
    forget(renderer);
    renderer->source = queue;
    if (!queue) {
        return;
    }
    renderer->generation = audio_generation(queue);
    for (int i = 0; i < audio_sound_count(queue); i++) {
        apush(renderer->sounds, (CF_Audio){ 0 });
    }
}
void render_audio_update(render_audio *renderer, audio *queue, bool playing)
{
    /* Cute has no mixer when SDL audio fails to start. */
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (queue) {
            uint64_t serial;
            int source = audio_source(queue, &serial);
            audio_set_analysis(queue, (audio_measurement){ .source = source, .status = AUDIO_UNAVAILABLE });
        }
        return;
    }
    follow_source(renderer, queue);
    if (!queue) {
        return;
    }
    for (int i = 0; i < VOICES; i++) {
        voice *v = &renderer->voices[i];
        if (v->audio.id && !cf_sound_is_active(v->sound)) {
            release(v);
        }
    }
    set_paused(renderer, !playing);
    audio_input_update(renderer->input, queue, playing);
    if (!playing) {
        return;
    }
    for (int i = 0; i < AUDIO_BUSES; i++) {
        set_volume(renderer, i, audio_volume(queue, i));
    }
    follow_music(renderer, queue);
    /* Only the newest voices fit; older ones would be cut at once. */
    audio_voice audible[VOICES], taken[VOICES];
    int total = 0;
    for (int count; (count = audio_take(queue, taken, VOICES));) {
        for (int i = 0; i < count; i++) {
            if (taken[i].volume > 0) {
                audible[total++ % VOICES] = taken[i];
            }
        }
    }
    for (int i = total > VOICES ? total - VOICES : 0; i < total; i++) {
        play(renderer, queue, audible[i % VOICES]);
    }
}
