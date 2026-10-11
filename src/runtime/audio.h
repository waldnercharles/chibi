#ifndef CHIBI_GAME_AUDIO_H
#define CHIBI_GAME_AUDIO_H
#include "execution/host.h"

#include <stddef.h>
#include <stdint.h>

/* Deterministic audio requests. Scripts queue voices and set the music and
 * bus volumes here; a playback host drains the voices and mirrors the rest. */
typedef struct game_audio game_audio;
typedef enum
{
    GAME_AUDIO_IDLE,
    GAME_AUDIO_WAITING,
    GAME_AUDIO_READY,
    GAME_AUDIO_UNAVAILABLE,
    GAME_AUDIO_DENIED,
    GAME_AUDIO_ENDED
} game_audio_status;
typedef struct
{
    float level, bass, mid, treble, onset;
    bool active;
    int source;
    game_audio_status status;
} game_audio_analysis;
typedef struct
{
    float frequency, end_frequency, duration, volume, pan, noise;
} game_tone;
typedef struct
{
    int sound; /* library index, or -1 for the tone */
    game_tone tone;
    float volume, pan;
} game_voice;
typedef enum
{
    GAME_AUDIO_MASTER,
    GAME_AUDIO_MUSIC,
    GAME_AUDIO_SOUNDS,
    GAME_AUDIO_BUSES
} game_audio_bus;
typedef enum
{
    GAME_SOUND_INVALID,
    GAME_SOUND_WAV,
    GAME_SOUND_OGG
} game_sound_format;
/* Identifies an encoded sound file the playback host can decode, from its
 * headers: a mono or stereo integer or float WAV with all its samples, or mono
 * or stereo Ogg Vorbis in whole pages, sampled at 44.1 kHz since the mixer does
 * not resample. Damaged Vorbis data inside whole pages
 * is only found when it plays. */
game_sound_format game_sound_format_of(const void *data, size_t length);
game_audio *game_audio_new();
void game_audio_free(game_audio *audio);
/* Stops everything: drops queued voices, stops the music, restores the bus
 * volumes and advances the generation. The sound library stays. */
void game_audio_reset(game_audio *audio);
/* Makes room for count more sounds, so the library's storage is known before
 * any file is copied. */
void game_audio_reserve_sounds(game_audio *audio, int count);
/* Copies a WAV or OGG file that scripts play by name. */
void game_audio_add_sound(game_audio *audio, const char *name, const void *data, size_t length);
int game_audio_sound_count(const game_audio *audio);
/* Bytes the sound library holds: its records, names and copied files. */
size_t game_audio_sound_storage_bytes(const game_audio *audio);
/* Identifies the library's names and files, in order. */
uint64_t game_audio_library_hash(const game_audio *audio);
const void *game_audio_sound(const game_audio *audio, int index, size_t *length);
bool game_audio_tone(game_audio *audio, game_tone tone);
int game_audio_take(game_audio *audio, game_voice *out, int capacity);
/* The looping track (-1 for none), its fade seconds, and a serial that
 * changes with each start or stop. */
int game_audio_music(const game_audio *audio, float *fade, uint64_t *serial);
float game_audio_volume(const game_audio *audio, game_audio_bus bus);
/* Unique to each reset or new sound across all queues; hosts stop and
 * reload when it changes. */
uint64_t game_audio_generation(const game_audio *audio);
uint64_t game_audio_count(const game_audio *audio);
uint64_t game_audio_hash(const game_audio *audio);
/* Source 0 observes the game's output; source 1 requests external audio.
 * The playback host publishes a snapshot before the next game tick. */
int game_audio_source(const game_audio *audio, uint64_t *serial);
game_audio_analysis game_audio_get_analysis(const game_audio *audio);
void game_audio_set_analysis(game_audio *audio, game_audio_analysis analysis);
/* Owned mono PCM WAV. Headless tests and the playback host share synthesis. */
unsigned char *game_tone_wav(game_tone tone, int *size);
extern const node_host_call GAME_AUDIO_TONE_CALL;
extern const node_host_call GAME_AUDIO_SOUND_CALL;
extern const node_host_call GAME_AUDIO_MUSIC_CALL;
extern const node_host_call GAME_AUDIO_STOP_MUSIC_CALL;
extern const node_host_call GAME_AUDIO_MASTER_VOLUME_CALL;
extern const node_host_call GAME_AUDIO_MUSIC_VOLUME_CALL;
extern const node_host_call GAME_AUDIO_SOUND_VOLUME_CALL;
extern const node_host_call GAME_AUDIO_SOURCE_CALL;
extern const node_host_call GAME_AUDIO_ANALYSIS_CALL;
extern const node_access GAME_AUDIO_ACCESS;
extern const node_access GAME_AUDIO_READ_ACCESS;
#endif
