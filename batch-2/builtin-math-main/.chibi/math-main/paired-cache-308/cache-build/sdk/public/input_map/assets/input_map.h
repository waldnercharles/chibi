#pragma once

#include "runtime/input_action.h"

#include <stddef.h>

/* A project's input map, content/input.kdl. Each action names the Control
 * labels that hold it, and may also hold Aim firing:
 *
 *     Fire {
 *         (array)sources "Mouse Left" "Pad Right Trigger"
 *         firing #true
 *     }
 *     P2Left {
 *         (array)sources "J"
 *         controller 1
 *         (array)buttons "Dpad Left" "Left Stick Left"
 *     }
 *     Jump {
 *         (array)sources "Space"
 *     }
 *
 * Maps are support/array.h vectors of actions in file order, whose names they
 * own. */
#define INPUT_MAP_FILE "input.kdl"

bool input_map_read(const char *text, input_action **out, char *error, size_t size);
/* Reads <content>/input.kdl; a project without one has no actions. */
bool input_map_load(const char *content, input_action **out, char *error, size_t size);
char *input_map_write(const input_action *map);
/* Adds controls to the named action, appending the action when it is new. */
void input_map_add(input_action **map, const char *name, uint64_t controls, bool firing);
void input_map_free(input_action *map);
