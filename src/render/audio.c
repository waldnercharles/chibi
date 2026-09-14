#include "render/audio.h"

#include "platform/cute.h"
#include "support/xalloc.h"

#include <stdlib.h>

#define VOICES 64
typedef struct
{
    CF_Audio audio;
    CF_Sound sound;
} voice;
struct render_audio
{
    voice voices[VOICES];
    int next;
};
render_audio *render_audio_new()
{
    return xcalloc(1, sizeof(render_audio));
}
static void release(voice *v)
{
    if (!v->audio.id) {
        return;
    }
    if (cf_sound_is_active(v->sound)) {
        cf_sound_stop(v->sound);
    }
    cf_audio_destroy(v->audio);
    *v = (voice){ 0 };
}
void render_audio_free(render_audio *audio)
{
    for (int i = 0; i < VOICES; i++) {
        release(&audio->voices[i]);
    }
    free(audio);
}
void render_audio_update(render_audio *audio, game_audio *queue, bool playing)
{
    for (int i = 0; i < VOICES; i++) {
        voice *v = &audio->voices[i];
        if (v->audio.id && !cf_sound_is_active(v->sound)) {
            release(v);
        }
        if (v->audio.id) {
            cf_sound_set_is_paused(v->sound, !playing);
        }
    }
    if (!playing) {
        return;
    }
    game_sound sounds[128];
    int count = game_audio_take(queue, sounds, 128);
    for (int i = 0; i < count; i++) {
        if (sounds[i].volume == 0) {
            continue;
        }
        int size;
        unsigned char *wav = game_sound_wav(sounds[i], &size);
        voice *v = &audio->voices[audio->next];
        audio->next = (audio->next + 1) % VOICES;
        release(v);
        v->audio = cf_audio_load_wav_from_memory(wav, size);
        free(wav);
        if (!v->audio.id) {
            continue;
        }
        CF_SoundParams params = cf_sound_params_defaults();
        params.volume = sounds[i].volume;
        params.pan = sounds[i].pan;
        v->sound = cf_play_sound(v->audio, params);
    }
}
