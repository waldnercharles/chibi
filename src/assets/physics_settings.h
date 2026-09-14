#pragma once

#include "runtime/scenes/physics_settings.h"

#include <yyjson.h>

/* Missing settings use defaults; malformed or unknown fields fail. */
bool game_physics_settings_read(yyjson_val *object, game_physics_settings *out);
yyjson_mut_val *game_physics_settings_write(yyjson_mut_doc *doc, game_physics_settings settings);
