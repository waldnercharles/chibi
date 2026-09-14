#ifndef CHIBI_GAME_AUDIO_H
#define CHIBI_GAME_AUDIO_H
#include "graph/model/registry.h"

#include <stddef.h>
#include <stdint.h>

typedef struct game_audio game_audio;
typedef struct
{
    float frequency, end_frequency, duration, volume, pan, noise;
} game_sound;
game_audio *game_audio_new();
void game_audio_free(game_audio *audio);
void game_audio_reset(game_audio *audio);
bool game_audio_emit(game_audio *audio, game_sound sound);
int game_audio_take(game_audio *audio, game_sound *out, int capacity);
uint64_t game_audio_count(const game_audio *audio);
uint64_t game_audio_hash(const game_audio *audio);
/* Owned mono PCM WAV. Headless tests and the playback host share synthesis. */
unsigned char *game_sound_wav(game_sound sound, int *size);
extern const registry_pack GAME_AUDIO_PACK;
#endif
