#pragma once
#include "runtime/player.h"
/* Export resident immutable shader/model generations and typed parameter
 * records. The caller owns the returned array, names and encoded payloads. */
bool authoring_visual_prepare(const scene *const *, int scene_count, player_asset **, char *error, size_t size);
void authoring_visual_prepared_free(player_asset *);
