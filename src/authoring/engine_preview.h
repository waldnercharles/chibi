#pragma once
#include "authoring/preview_runtime.h"
#include "runtime/engine.h"
/* Native preview composition owns all borrowed services until engine_free. */
engine *authoring_engine_new(ecs_world *, game_preview_runtime *);
