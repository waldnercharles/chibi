#include "runtime/audio.h"

#include "execution/host.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOICE_QUEUE 128
#define SOUND_RATE 44100
#define FADE_MAX 60
enum
{
    TAG_MUSIC,
    TAG_VOLUME,
    TAG_VOICE
};
typedef struct
{
    char *name;
    void *data;
    size_t length;
} sound;
struct game_audio
{
    sound *sounds;
    game_voice pending[VOICE_QUEUE];
    int count;
    int music;
    float fade, volumes[GAME_AUDIO_BUSES];
    uint64_t music_serial, generation, emitted, hash;
    char message[192];
};
/* Unique across queues, so a host never mistakes a new queue for an old one. */
static _Atomic uint64_t generations;
static void restore(game_audio *audio)
{
    audio->count = 0;
    audio->music = -1;
    audio->fade = 0;
    audio->music_serial++;
    for (int i = 0; i < GAME_AUDIO_BUSES; i++) {
        audio->volumes[i] = 1;
    }
    audio->emitted = 0;
    audio->hash = 0;
    audio->generation = ++generations;
}
game_audio *game_audio_new()
{
    game_audio *audio = xcalloc(1, sizeof(game_audio));
    restore(audio);
    return audio;
}
void game_audio_free(game_audio *audio)
{
    for (int i = 0; i < acount(audio->sounds); i++) {
        free(audio->sounds[i].name);
        free(audio->sounds[i].data);
    }
    afree(audio->sounds);
    free(audio);
}
void game_audio_reset(game_audio *audio)
{
    restore(audio);
}
static int find(const game_audio *audio, const char *name)
{
    for (int i = 0; i < acount(audio->sounds); i++) {
        if (!strcmp(audio->sounds[i].name, name)) {
            return i;
        }
    }
    return -1;
}
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}
static int le16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}
/* The offset of the body of the first chunk named `id` at or after `at`, or 0
 * when there is none or a chunk runs past the file. Chunks pad to even sizes. */
static size_t wav_chunk(const unsigned char *bytes, size_t length, size_t at, const char *id, uint32_t *size)
{
    while (at + 8 <= length) {
        uint32_t n = le32(bytes + at + 4);
        if (n > length - at - 8) {
            return 0;
        }
        if (!memcmp(bytes + at, id, 4)) {
            *size = n;
            return at + 8;
        }
        at += 8 + (size_t)n + (n & 1);
    }
    return 0;
}
/* What the playback host decodes: a plain PCM (8 to 32-bit) or float (32 or
 * 64-bit) format, not WAVE_FORMAT_EXTENSIBLE, with one or two channels, then a
 * data chunk holding at least one whole frame. */
static bool wav_playable(const unsigned char *bytes, size_t length)
{
    uint32_t size;
    size_t fmt = wav_chunk(bytes, length, 12, "fmt ", &size);
    if (!fmt || size < 16) {
        return false;
    }
    int format = le16(bytes + fmt), channels = le16(bytes + fmt + 2),
        bits = le16(bytes + fmt + 14);
    bool integer = format == 1 &&
                   (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    bool real = format == 3 && (bits == 32 || bits == 64);
    if ((channels != 1 && channels != 2) || !(integer || real)) {
        return false;
    }
    /* The host looks for samples only after the format. */
    size_t data = wav_chunk(bytes, length, fmt + size + (size & 1), "data", &size);
    return data && size >= (uint32_t)(channels * bits / 8);
}
/* An Ogg stream whose first packet identifies mono or stereo Vorbis; Opus and
 * surround streams are not decoded. */
static bool ogg_vorbis(const unsigned char *bytes, size_t length)
{
    if (length < 27) {
        return false;
    }
    size_t packet = 27 + (size_t)bytes[26];
    if (length < packet + 12 || bytes[packet] != 1 ||
        memcmp(bytes + packet + 1, "vorbis", 6)) {
        return false;
    }
    int channels = bytes[packet + 11];
    return channels == 1 || channels == 2;
}
game_sound_format game_sound_format_of(const void *data, size_t length)
{
    const unsigned char *bytes = data;
    if (length > 4 && !memcmp(bytes, "OggS", 4)) {
        return ogg_vorbis(bytes, length) ? GAME_SOUND_OGG : GAME_SOUND_INVALID;
    }
    if (length > 12 && !memcmp(bytes, "RIFF", 4) && !memcmp(bytes + 8, "WAVE", 4)) {
        return wav_playable(bytes, length) ? GAME_SOUND_WAV : GAME_SOUND_INVALID;
    }
    return GAME_SOUND_INVALID;
}
void game_audio_add_sound(game_audio *audio, const char *name, const void *data, size_t length)
{
    assert(find(audio, name) < 0 && game_sound_format_of(data, length));
    void *copy = xmalloc(length);
    memcpy(copy, data, length);
    apush(audio->sounds, ((sound){ xstrdup(name), copy, length }));
    audio->generation = ++generations;
}
int game_audio_sound_count(const game_audio *audio)
{
    return acount(audio->sounds);
}
const void *game_audio_sound(const game_audio *audio, int index, size_t *length)
{
    assert(index >= 0 && index < acount(audio->sounds));
    *length = audio->sounds[index].length;
    return audio->sounds[index].data;
}
int game_audio_music(const game_audio *audio, float *fade, uint64_t *serial)
{
    *fade = audio->fade;
    *serial = audio->music_serial;
    return audio->music;
}
float game_audio_volume(const game_audio *audio, game_audio_bus bus)
{
    return audio->volumes[bus];
}
uint64_t game_audio_generation(const game_audio *audio)
{
    return audio->generation;
}
uint64_t game_audio_count(const game_audio *audio)
{
    return audio->emitted;
}
uint64_t game_audio_hash(const game_audio *audio)
{
    return audio->hash;
}

/* Every request joins the deterministic history in order. */
static void mix(game_audio *audio, unsigned char tag, const float *values, int count)
{
    uint64_t hash = audio->hash ? audio->hash : UINT64_C(14695981039346656037);
    hash = (hash ^ tag) * UINT64_C(1099511628211);
    const unsigned char *bytes = (const unsigned char *)values;
    for (size_t i = 0; i < (size_t)count * sizeof *values; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    audio->hash = hash;
}
static bool unit(float x)
{
    return isfinite(x) && x >= 0 && x <= 1;
}
static bool valid(game_tone t)
{
    return isfinite(t.frequency) && t.frequency >= 20 && t.frequency <= 10000 &&
           isfinite(t.end_frequency) && t.end_frequency >= 20 &&
           t.end_frequency <= 10000 && isfinite(t.duration) && t.duration >= 0.01f &&
           t.duration <= 4 && unit(t.volume) && unit(t.pan) && unit(t.noise);
}

/* Presentation overload drops the oldest queued voice. Gameplay and the
 * deterministic cue history still observe every requested sound. */
static void push(game_audio *audio, game_voice voice)
{
    if (audio->count == VOICE_QUEUE) {
        memmove(audio->pending, audio->pending + 1, sizeof(game_voice) * (VOICE_QUEUE - 1));
        audio->count--;
    }
    audio->pending[audio->count++] = voice;
    audio->emitted++;
    const float values[] = { (float)voice.sound,
                             voice.tone.frequency,
                             voice.tone.end_frequency,
                             voice.tone.duration,
                             voice.tone.noise,
                             voice.volume,
                             voice.pan };
    mix(audio, TAG_VOICE, values, sizeof values / sizeof *values);
}
bool game_audio_tone(game_audio *audio, game_tone tone)
{
    if (!valid(tone)) {
        return false;
    }
    push(audio, (game_voice){ -1, tone, tone.volume, tone.pan });
    return true;
}

int game_audio_take(game_audio *audio, game_voice *out, int capacity)
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
unsigned char *game_tone_wav(game_tone tone, int *size)
{
    if (!valid(tone)) {
        return NULL;
    }
    int samples = (int)(tone.duration * SOUND_RATE);
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
        float envelope = fminf(1, t * tone.duration / 0.005f) * (1 - t) * (1 - t);
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        float noise = (float)(random >> 8) / 8388607.5f - 1;
        float signal = (1 - tone.noise) * (float)sin(phase) + tone.noise * noise;
        int16_t sample = (int16_t)(signal * envelope * 24000);
        u16(wav + 44 + i * 2, (uint16_t)sample);
        phase += 6.283185307179586 *
                 (tone.frequency + (tone.end_frequency - tone.frequency) * t) /
                 SOUND_RATE;
    }
    return wav;
}

static int named(game_audio *audio, const pin_value *value, const char **error)
{
    const char *name = value->str ? value->str : "";
    int index = find(audio, name);
    if (index < 0) {
        snprintf(audio->message, sizeof audio->message, "No sound named '%s'", name);
        *error = audio->message;
    }
    return index;
}
static bool fade(float seconds)
{
    return isfinite(seconds) && seconds >= 0 && seconds <= FADE_MAX;
}

static const char *play_tone(void *service, const pin_value *in, pin_value *)
{
    game_tone tone = { in[0].f, in[1].f, in[2].f, in[3].f, in[4].f, in[5].f };
    if (!game_audio_tone(service, tone)) {
        return "Tone requires 20-10000 Hz, 0.01-4 seconds, and 0-1 volume, "
               "pan and noise";
    }
    return NULL;
}
static const char *play_sound(void *service, const pin_value *in, pin_value *)
{
    game_audio *audio = service;
    const char *error = NULL;
    int index = named(audio, &in[0], &error);
    if (index < 0) {
        return error;
    }
    if (!unit(in[1].f) || !unit(in[2].f)) {
        return "Play Sound requires 0-1 volume and pan";
    }
    push(audio, (game_voice){ .sound = index, .volume = in[1].f, .pan = in[2].f });
    return NULL;
}
/* Starting the track that already plays changes nothing, so music runs on
 * across scenes that each ask for it. */
static const char *change_music(game_audio *audio, int index, float seconds)
{
    if (!fade(seconds)) {
        return "Music fades take 0-60 seconds";
    }
    if (index == audio->music) {
        return NULL;
    }
    audio->music = index;
    audio->fade = seconds;
    audio->music_serial++;
    const float values[] = { (float)index, seconds };
    mix(audio, TAG_MUSIC, values, 2);
    return NULL;
}
static const char *play_music(void *service, const pin_value *in, pin_value *)
{
    game_audio *audio = service;
    const char *error = NULL;
    int index = named(audio, &in[0], &error);
    return index < 0 ? error : change_music(audio, index, in[1].f);
}
static const char *stop_music(void *service, const pin_value *in, pin_value *)
{
    return change_music(service, -1, in[0].f);
}
static const char *set_volume(game_audio *audio, game_audio_bus bus, float volume)
{
    if (!unit(volume)) {
        return "Volume must be 0-1";
    }
    audio->volumes[bus] = volume;
    const float values[] = { (float)bus, volume };
    mix(audio, TAG_VOLUME, values, 2);
    return NULL;
}
static const char *master_volume(void *service, const pin_value *in, pin_value *)
{
    return set_volume(service, GAME_AUDIO_MASTER, in[0].f);
}
static const char *music_volume(void *service, const pin_value *in, pin_value *)
{
    return set_volume(service, GAME_AUDIO_MUSIC, in[0].f);
}
static const char *sound_volume(void *service, const pin_value *in, pin_value *)
{
    return set_volume(service, GAME_AUDIO_SOUNDS, in[0].f);
}
#define CALL(NAME, INVOKE)                                                     \
    const node_host_call NAME = {                                              \
        .service = "game.audio",                                               \
        .invoke = INVOKE,                                                      \
        .missing_service = "Audio requires a game world"                       \
    }
CALL(GAME_AUDIO_TONE_CALL, play_tone);
CALL(GAME_AUDIO_SOUND_CALL, play_sound);
CALL(GAME_AUDIO_MUSIC_CALL, play_music);
CALL(GAME_AUDIO_STOP_MUSIC_CALL, stop_music);
CALL(GAME_AUDIO_MASTER_VOLUME_CALL, master_volume);
CALL(GAME_AUDIO_MUSIC_VOLUME_CALL, music_volume);
CALL(GAME_AUDIO_SOUND_VOLUME_CALL, sound_volume);
const node_access GAME_AUDIO_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
