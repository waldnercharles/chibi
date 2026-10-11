#ifndef CHIBI_AUDIO_H
#define CHIBI_AUDIO_H
#include "execution/host.h"

#include <stddef.h>
#include <stdint.h>

/* Deterministic audio requests. Scripts queue voices and set the music and
 * bus volumes here; a playback host drains the voices and mirrors the rest. */
typedef struct audio audio;
typedef enum
{
    AUDIO_IDLE,
    AUDIO_WAITING,
    AUDIO_READY,
    AUDIO_UNAVAILABLE,
    AUDIO_DENIED,
    AUDIO_ENDED
} audio_status;
typedef struct
{
    float level, bass, mid, treble, onset;
    bool active;
    int source;
    audio_status status;
} audio_measurement;
typedef struct
{
    float frequency, end_frequency, duration, volume, pan, noise;
} tone;
typedef struct
{
    int sound; /* library index, or -1 for the tone */
    tone tone;
    float volume, pan;
} audio_voice;
typedef enum
{
    AUDIO_MASTER,
    AUDIO_MUSIC,
    AUDIO_SOUNDS,
    AUDIO_BUSES
} audio_bus;
typedef enum
{
    SOUND_INVALID,
    SOUND_WAV,
    SOUND_OGG
} sound_format;
/* Identifies an encoded sound file the playback host can decode, from its
 * headers: a mono or stereo integer or float WAV with all its samples, or mono
 * or stereo Ogg Vorbis in whole pages, sampled at 44.1 kHz since the mixer does
 * not resample. Damaged Vorbis data inside whole pages
 * is only found when it plays. */
sound_format sound_format_of(const void *data, size_t length);
audio *audio_new();
void audio_free(audio *audio);
/* Stops everything: drops queued voices, stops the music, restores the bus
 * volumes and advances the generation. The sound library stays. */
void audio_reset(audio *audio);
/* Makes room for count more sounds, so the library's storage is known before
 * any file is copied. */
void audio_reserve_sounds(audio *audio, int count);
/* Copies a WAV or OGG file that scripts play by name. */
void audio_add_sound(audio *audio, const char *name, const void *data, size_t length);
int audio_sound_count(const audio *audio);
/* Bytes the sound library holds: its records, names and copied files. */
size_t audio_sound_storage_bytes(const audio *audio);
/* Identifies the library's names and files, in order. */
uint64_t audio_library_hash(const audio *audio);
const void *audio_sound(const audio *audio, int index, size_t *length);
bool audio_tone(audio *audio, tone tone);
int audio_take(audio *audio, audio_voice *out, int capacity);
/* The looping track (-1 for none), its fade seconds, and a serial that
 * changes with each start or stop. */
int audio_music(const audio *audio, float *fade, uint64_t *serial);
float audio_volume(const audio *audio, audio_bus bus);
/* Unique to each reset or new sound across all queues; hosts stop and
 * reload when it changes. */
uint64_t audio_generation(const audio *audio);
uint64_t audio_count(const audio *audio);
uint64_t audio_hash(const audio *audio);
/* Source 0 observes the game's output; source 1 requests external audio.
 * The playback host publishes a snapshot before the next game tick. */
int audio_source(const audio *audio, uint64_t *serial);
audio_measurement audio_get_analysis(const audio *audio);
void audio_set_analysis(audio *audio, audio_measurement analysis);
/* Owned mono PCM WAV. Headless tests and the playback host share synthesis. */
unsigned char *tone_wav(tone tone, int *size);
extern const node_host_call AUDIO_TONE_CALL;
extern const node_host_call AUDIO_SOUND_CALL;
extern const node_host_call AUDIO_MUSIC_CALL;
extern const node_host_call AUDIO_STOP_MUSIC_CALL;
extern const node_host_call AUDIO_MASTER_VOLUME_CALL;
extern const node_host_call AUDIO_MUSIC_VOLUME_CALL;
extern const node_host_call AUDIO_SOUND_VOLUME_CALL;
extern const node_host_call AUDIO_SOURCE_CALL;
extern const node_host_call AUDIO_ANALYSIS_CALL;
extern const node_access AUDIO_ACCESS;
extern const node_access AUDIO_READ_ACCESS;
#endif
