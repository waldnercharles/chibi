#include "runtime/audio.h"

#include "execution/host.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SOUND_QUEUE 128
#define SOUND_RATE 44100
struct game_audio
{
    game_sound pending[SOUND_QUEUE];
    int count;
    uint64_t emitted, hash;
};
game_audio *game_audio_new()
{
    return xcalloc(1, sizeof(game_audio));
}
void game_audio_free(game_audio *audio)
{
    free(audio);
}
void game_audio_reset(game_audio *audio)
{
    *audio = (game_audio){ 0 };
}
uint64_t game_audio_count(const game_audio *audio)
{
    return audio->emitted;
}
uint64_t game_audio_hash(const game_audio *audio)
{
    return audio->hash;
}

static bool valid(game_sound s)
{
    return isfinite(s.frequency) && s.frequency >= 20 && s.frequency <= 10000 &&
           isfinite(s.end_frequency) && s.end_frequency >= 20 &&
           s.end_frequency <= 10000 && isfinite(s.duration) &&
           s.duration >= 0.01f && s.duration <= 4 && isfinite(s.volume) &&
           s.volume >= 0 && s.volume <= 1 && isfinite(s.pan) && s.pan >= 0 &&
           s.pan <= 1 && isfinite(s.noise) && s.noise >= 0 && s.noise <= 1;
}

bool game_audio_emit(game_audio *audio, game_sound sound)
{
    if (!valid(sound)) {
        return false;
    }
    /* Presentation overload drops the oldest queued voice. Gameplay and the
     * deterministic cue history still observe every requested sound. */
    if (audio->count == SOUND_QUEUE) {
        memmove(audio->pending, audio->pending + 1, sizeof(game_sound) * (SOUND_QUEUE - 1));
        audio->count--;
    }
    audio->pending[audio->count++] = sound;
    audio->emitted++;
    const float values[] = { sound.frequency, sound.end_frequency,
                             sound.duration,  sound.volume,
                             sound.pan,       sound.noise };
    const unsigned char *bytes = (const unsigned char *)values;
    uint64_t hash = audio->hash ? audio->hash : UINT64_C(14695981039346656037);
    for (size_t i = 0; i < sizeof values; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    audio->hash = hash;
    return true;
}

int game_audio_take(game_audio *audio, game_sound *out, int capacity)
{
    int count = audio->count < capacity ? audio->count : capacity;
    memcpy(out, audio->pending, (size_t)count * sizeof *out);
    audio->count -= count;
    memmove(audio->pending, audio->pending + count, (size_t)audio->count * sizeof *out);
    return count;
}

static void u32(unsigned char *p, uint32_t x)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (unsigned char)(x >> (i * 8));
    }
}
static void u16(unsigned char *p, uint16_t x)
{
    p[0] = (unsigned char)x;
    p[1] = (unsigned char)(x >> 8);
}
unsigned char *game_sound_wav(game_sound sound, int *size)
{
    if (!valid(sound)) {
        return NULL;
    }
    int samples = (int)(sound.duration * SOUND_RATE);
    *size = 44 + samples * 2;
    unsigned char *wav = xcalloc((size_t)*size, 1);
    memcpy(wav, "RIFF", 4);
    u32(wav + 4, (uint32_t)*size - 8);
    memcpy(wav + 8, "WAVEfmt ", 8);
    u32(wav + 16, 16);
    u16(wav + 20, 1);
    u16(wav + 22, 1);
    u32(wav + 24, SOUND_RATE);
    u32(wav + 28, SOUND_RATE * 2);
    u16(wav + 32, 2);
    u16(wav + 34, 16);
    memcpy(wav + 36, "data", 4);
    u32(wav + 40, (uint32_t)samples * 2);
    double phase = 0;
    uint32_t random = 0x75432768;
    for (int i = 0; i < samples; i++) {
        float t = (float)i / (samples - 1);
        float envelope = fminf(1, t * sound.duration / 0.005f) * (1 - t) * (1 - t);
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        float noise = (float)(random >> 8) / 8388607.5f - 1;
        float signal = (1 - sound.noise) * (float)sin(phase) + sound.noise * noise;
        int16_t sample = (int16_t)(signal * envelope * 24000);
        u16(wav + 44 + i * 2, (uint16_t)sample);
        phase += 6.283185307179586 *
                 (sound.frequency + (sound.end_frequency - sound.frequency) * t) /
                 SOUND_RATE;
    }
    return wav;
}

static const char *play_sound(void *service, const pin_value *values, pin_value *)
{
    game_sound sound = { values[0].f, values[1].f, values[2].f,
                         values[3].f, values[4].f, values[5].f };
    if (!game_audio_emit(service, sound)) {
        return "Sound requires 20-10000 Hz, 0.01-4 seconds, and 0-1 volume, "
               "pan and noise";
    }
    return NULL;
}
const node_host_call GAME_AUDIO_PLAY_CALL = {
    .service = "game.audio",
    .invoke = play_sound,
    .missing_service = "Play Sound requires a game world",
};
const node_access GAME_AUDIO_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
