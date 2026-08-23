#ifndef CBLUEPRINT_GAME_GENERATED_H
#define CBLUEPRINT_GAME_GENERATED_H

#include "../core/node.h"
#include "../ecs/ecs.h"
#include "components.h"

extern const ecs_component_desc GAME_COMPONENT_GameTransform;
extern const ecs_component_desc GAME_COMPONENT_GameHealth;
extern const ecs_component_desc *const GAME_REGISTRY_COMPONENTS[];
extern const int GAME_REGISTRY_COMPONENT_COUNT;

extern GameTransform *GAME_GameTransform_get(ecs_world *, ecs_entity);
extern const GameTransform *GAME_GameTransform_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameTransform_has(const ecs_world *, ecs_entity);
extern int GAME_GameTransform_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameTransform_remove(ecs_world *, ecs_entity, uint64_t);
extern GameHealth *GAME_GameHealth_get(ecs_world *, ecs_entity);
extern const GameHealth *GAME_GameHealth_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameHealth_has(const ecs_world *, ecs_entity);
extern int GAME_GameHealth_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameHealth_remove(ecs_world *, ecs_entity, uint64_t);

extern node_desc GAME_NODE_Get_GameTransform;
extern node_desc GAME_NODE_Set_GameTransform;
extern node_desc GAME_NODE_Has_GameTransform;
extern node_desc GAME_NODE_Add_GameTransform;
extern node_desc GAME_NODE_Remove_GameTransform;
extern node_desc GAME_NODE_Get_GameHealth;
extern node_desc GAME_NODE_Set_GameHealth;
extern node_desc GAME_NODE_Has_GameHealth;
extern node_desc GAME_NODE_Add_GameHealth;
extern node_desc GAME_NODE_Remove_GameHealth;
extern node_desc GAME_NODE_game_component_probe;

#endif
