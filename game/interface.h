#ifndef CBLUEPRINT_GAME_INTERFACE_H
#define CBLUEPRINT_GAME_INTERFACE_H

#include "../core/annotate.h"

NODE_EXEC void game_component_probe(void) NODE_READS(GameHealth) NODE_ENTITY_SCOPED;

#endif
