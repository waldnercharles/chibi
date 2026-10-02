#pragma once

#include "runtime/scenes/physics_settings.h"
#include "support/asset_value.h"

/* Missing settings use defaults; malformed or unknown fields fail. */
bool game_physics_settings_read(av_value *object, game_physics_settings *out);
av_value *game_physics_settings_write(av_doc *doc, game_physics_settings settings);
