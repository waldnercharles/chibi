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
    const game_audio *source;
    uint64_t generation, music_serial;
    CF_Audio *sounds; /* decoded on first play, by library index */
    float volumes[GAME_AUDIO_BUSES];
    bool music, paused; /* music: the mixer may hold a track of ours */
};
render_audio *render_audio_new()
{
    render_audio *audio = xcalloc(1, sizeof(render_audio));
    audio->input = audio_input_new();
    for (int i = 0; i < GAME_AUDIO_BUSES; i++) {
        audio->volumes[i] = 1;
    }
    return audio;
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
static void set_volume(render_audio *audio, game_audio_bus bus, float volume)
{
    if (audio->volumes[bus] == volume) {
        return;
    }
    audio->volumes[bus] = volume;
    switch (bus) {
        case GAME_AUDIO_MASTER: cf_audio_set_global_volume(volume); break;
        case GAME_AUDIO_MUSIC: cf_music_set_volume(volume); break;
        case GAME_AUDIO_SOUNDS: cf_audio_set_sound_volume(volume); break;
        case GAME_AUDIO_BUSES: assert(false);
    }
}
static void set_paused(render_audio *audio, bool paused)
{
    if (audio->paused != paused) {
        audio->paused = paused;
        cf_audio_set_pause(paused);
    }
}
/* Silences everything this host started and restores the mixer. */
static void forget(render_audio *audio)
{
    audio_input_reset(audio->input);
    for (int i = 0; i < VOICES; i++) {
        release(&audio->voices[i]);
    }
    if (audio->music) {
        cf_music_stop(0);
        audio->music = false;
    }
    audio->music_serial = 0;
    set_paused(audio, false);
    /* The mixer defers freeing a source until its last voice stops. */
    for (int i = 0; i < acount(audio->sounds); i++) {
        if (audio->sounds[i].id) {
            cf_audio_destroy(audio->sounds[i]);
        }
    }
    afree(audio->sounds);
    for (int i = 0; i < GAME_AUDIO_BUSES; i++) {
        set_volume(audio, i, 1);
    }
}
void render_audio_free(render_audio *audio)
{
    forget(audio);
    audio_input_free(audio->input);
    free(audio);
}
static CF_Audio decoded(render_audio *audio, const game_audio *queue, int index)
{
    assert(index < acount(audio->sounds));
    CF_Audio *cached = &audio->sounds[index];
    if (cached->id) {
        return *cached;
    }
    size_t length;
    void *data = (void *)game_audio_sound(queue, index, &length);
    switch (game_sound_format_of(data, length)) {
        case GAME_SOUND_WAV:
            *cached = cf_audio_load_wav_from_memory(data, (int)length);
            break;
        case GAME_SOUND_OGG:
            *cached = cf_audio_load_ogg_from_memory(data, (int)length);
            break;
        case GAME_SOUND_INVALID: assert(false);
    }
    return *cached;
}
static void follow_music(render_audio *audio, const game_audio *queue)
{
    float fade;
    uint64_t serial;
    int track = game_audio_music(queue, &fade, &serial);
    if (serial == audio->music_serial) {
        return;
    }
    audio->music_serial = serial;
    /* Cute leaves a track it replaces playing; a fading one stays ours until
     * forget cuts it. */
    if (audio->music) {
        cf_music_stop(track < 0 ? fade : 0);
    }
    if (track < 0) {
        return;
    }
    CF_Audio source = decoded(audio, queue, track);
    if (!source.id) {
        return;
    }
    cf_music_set_loop(true);
    cf_music_play(source, fade);
    audio->music = true;
}
static void play(render_audio *audio, const game_audio *queue, game_voice played)
{
    voice v = { .owned = played.sound < 0 };
    if (v.owned) {
        int size;
        unsigned char *wav = game_tone_wav(played.tone, &size);
        v.audio = cf_audio_load_wav_from_memory(wav, size);
        free(wav);
    } else {
        v.audio = decoded(audio, queue, played.sound);
    }
    if (!v.audio.id) {
        return;
    }
    voice *slot = &audio->voices[audio->next];
    audio->next = (audio->next + 1) % VOICES;
    release(slot);
    CF_SoundParams params = cf_sound_params_defaults();
    params.volume = played.volume;
    params.pan = played.pan;
    v.sound = cf_play_sound(v.audio, params);
    *slot = v;
}
/* A new session, its end, a reset or a new sound starts over. */
static void follow_source(render_audio *audio, const game_audio *queue)
{
    if (audio->source == queue &&
        (!queue || audio->generation == game_audio_generation(queue))) {
        return;
    }
    forget(audio);
    audio->source = queue;
    if (!queue) {
        return;
    }
    audio->generation = game_audio_generation(queue);
    for (int i = 0; i < game_audio_sound_count(queue); i++) {
        apush(audio->sounds, (CF_Audio){ 0 });
    }
}
void render_audio_update(render_audio *audio, game_audio *queue, bool playing)
{
    /* Cute has no mixer when SDL audio fails to start. */
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (queue) {
            uint64_t serial;
            int source = game_audio_source(queue, &serial);
            game_audio_set_analysis(
                queue,
                (game_audio_analysis){ .source = source, .status = GAME_AUDIO_UNAVAILABLE }
            );
        }
        return;
    }
    follow_source(audio, queue);
    if (!queue) {
        return;
    }
    for (int i = 0; i < VOICES; i++) {
        voice *v = &audio->voices[i];
        if (v->audio.id && !cf_sound_is_active(v->sound)) {
            release(v);
        }
    }
    set_paused(audio, !playing);
    audio_input_update(audio->input, queue, playing);
    if (!playing) {
        return;
    }
    for (int i = 0; i < GAME_AUDIO_BUSES; i++) {
        set_volume(audio, i, game_audio_volume(queue, i));
    }
    follow_music(audio, queue);
    /* Only the newest voices fit; older ones would be cut at once. */
    game_voice audible[VOICES], taken[VOICES];
    int total = 0;
    for (int count; (count = game_audio_take(queue, taken, VOICES));) {
        for (int i = 0; i < count; i++) {
            if (taken[i].volume > 0) {
                audible[total++ % VOICES] = taken[i];
            }
        }
    }
    for (int i = total > VOICES ? total - VOICES : 0; i < total; i++) {
        play(audio, queue, audible[i % VOICES]);
    }
}
