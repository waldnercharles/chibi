#pragma once

#include "runtime/input_timeline.h"

#include <stddef.h>

/* A scenario replays gameplay input on a scene from its start:
 *
 *   scene: Sandbox
 *   frames: 600
 *   seed: 7
 *   keys:
 *   - {frame: 0, movement: {x: 1, y: 0}, aim: {x: 0, y: 1}, firing: true, controls: [Mouse Left]}
 *   - {frame: 90, movement: {x: 0, y: -1}}
 *
 * Each key holds its whole input from its frame (a fixed step) until the next
 * key; omitted fields are zero. A timeline that does not key frame zero holds
 * no input until its first key. */
typedef struct
{
    char *scene;
    int frames;
    unsigned seed;
    input_keyframe *keys; /* at least one, from frame zero, in increasing frames */
} scenario_document;

scenario_document *scenario_document_load(const char *json, char *error, size_t error_size);
/* Owned JSON text. */
char *scenario_document_save(const scenario_document *document);
void scenario_document_free(scenario_document *document);
