#ifndef CBLUEPRINT_GAME_INTERFACE_H
#define CBLUEPRINT_GAME_INTERFACE_H

#include "../core/annotate.h"
#include "components.h"

NODE_EXEC void game_component_probe(void) NODE_READS(GameHealth) NODE_ENTITY_SCOPED;
NODE_FN NODE_JIT(
    "v2-move"
) GameTransform game_move_transform(GameTransform transform, float x, float y);
NODE_FN NODE_CONVERTS(GameTransform, CF_V2)
CF_V2 game_transform_to_cf(GameTransform transform);
NODE_FN NODE_CONVERTS(CF_V2, GameTransform)
GameTransform game_transform_from_cf(CF_V2 value);

#endif
