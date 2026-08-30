#ifndef CBLUEPRINT_APP_BEHAVIOR_FIXTURES_H
#define CBLUEPRINT_APP_BEHAVIOR_FIXTURES_H

#include "../game/live.h"

/* Loads the canonical behavior fixtures used by the shipped app and headless
 * authoring sessions. Paths are visited in lexical order. */
int app_behavior_fixtures_load(game_live_world *world, const char *content_root);

#endif
