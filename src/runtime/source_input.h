#ifndef CHIBI_SOURCE_INPUT_H
#define CHIBI_SOURCE_INPUT_H

#include "runtime/behaviors/source.h"
#include "runtime/engine_input.h"

/* Borrowed source registry and host state must outlive engine_input. Update
 * now/delta at each fixed boundary before applying queued input. */
typedef struct
{
    game_source_registry *sources;
    double now, delta;
} game_source_input;
engine_input_host game_source_input_host(game_source_input *);

#endif
