#pragma once
#include "runtime/audio.h"
#include "runtime/player.h"

/* Appends each WAV or OGG file in content/sounds as a sound record named by
 * its file stem, in name order. A project without the directory has none.
 * On failure the caller frees the records, which may include some sounds. */
bool authoring_sounds_prepare(const char *content, player_asset **assets, char *error, size_t size);
/* Adds every content/sounds file to a world's sound library. */
bool authoring_sounds_load(const char *content, audio *audio, char *error, size_t size);
