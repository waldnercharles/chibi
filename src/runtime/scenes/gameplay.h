#ifndef CBLUEPRINT_GAME_GAMEPLAY_H
#define CBLUEPRINT_GAME_GAMEPLAY_H

#include "ecs/ecs.h"

struct game_entities;
typedef struct game_contacts game_contacts;

game_contacts *game_contacts_new(ecs_world *world, struct game_entities *entities);
void game_contacts_free(game_contacts *contacts);
void game_contacts_reset(game_contacts *contacts);
/* Snapshot overlaps, then queue Contact Begin for both participants. */
int game_contacts_tick(game_contacts *contacts);

#endif
