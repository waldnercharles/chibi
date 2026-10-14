#pragma once

#include "runtime/input_timeline.h"
#include "support/asset_value.h"

#include <stddef.h>

/* A scenario replays gameplay input on a scene from its start:
 *
 *   scene Sandbox
 *   frames 600
 *   seed 7
 *   saved_state { best "12"; }
 *   (array)keys {
 *       - frame=0 firing=#true {
 *           movement x=1 y=0
 *           aim x=0 y=1
 *           (array)controls "Mouse Left"
 *           (array)controllers {
 *               - connected=#true { (array)buttons "Dpad Left"; }
 *               - connected=#true { (array)buttons "A"; }
 *           }
 *       }
 *       - frame=90 { movement x=0 y=-1; }
 *   }
 *
 * Each key holds its whole input from its frame (a fixed step) until the next
 * key; omitted fields are zero. Keys that share a frame are sampled in order
 * before the step, as controls pressed and released between two steps are. A timeline that does not key frame zero holds
 * no input until its first key. The scene starts as authored, with the
 * game's saved progress replaced by saved_state (none when omitted). */
typedef struct
{
    char *scene;
    int frames;
    unsigned seed;
    av_value *saved_state; /* owned committed save values, or NULL */
    input_keyframe *keys; /* at least one, from frame zero, in increasing frames */
} scenario_document;

scenario_document *scenario_document_load(const char *text, char *error, size_t error_size);
/* Owned code source. */
char *scenario_document_save(const scenario_document *document);
void scenario_document_free(scenario_document *document);
