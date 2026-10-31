#pragma once

#include "runtime/scenes/physics_settings.h"
#include "support/asset_value.h"

/* Missing settings use defaults; malformed or unknown fields fail. */
bool physics_settings_read(av_value *object, physics_settings *out);
av_value *physics_settings_write(av_doc *doc, physics_settings settings);
