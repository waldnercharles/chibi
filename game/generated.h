#ifndef CBLUEPRINT_GAME_GENERATED_H
#define CBLUEPRINT_GAME_GENERATED_H

#include "../core/node.h"
#include "../ecs/ecs.h"
#include "components.h"

extern const ecs_component_desc GAME_COMPONENT_GameTransform;
extern const ecs_component_desc GAME_COMPONENT_GameOrientation;
extern GameOrientation *GAME_GameOrientation_get(ecs_world *, ecs_entity);
extern const GameOrientation *GAME_GameOrientation_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameOrientation_has(const ecs_world *, ecs_entity);
extern int GAME_GameOrientation_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameOrientation_remove(ecs_world *, ecs_entity, uint64_t);
extern node_desc GAME_NODE_Get_GameOrientation;
extern node_desc GAME_NODE_Set_GameOrientation;
extern node_desc GAME_NODE_Has_GameOrientation;
extern node_desc GAME_NODE_Add_GameOrientation;
extern node_desc GAME_NODE_Remove_GameOrientation;
extern const ecs_component_desc GAME_COMPONENT_GameSdfMotion;
extern GameSdfMotion *GAME_GameSdfMotion_get(ecs_world *, ecs_entity);
extern const GameSdfMotion *GAME_GameSdfMotion_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameSdfMotion_has(const ecs_world *, ecs_entity);
extern int GAME_GameSdfMotion_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameSdfMotion_remove(ecs_world *, ecs_entity, uint64_t);
extern node_desc GAME_NODE_Get_GameSdfMotion;
extern node_desc GAME_NODE_Set_GameSdfMotion;
extern node_desc GAME_NODE_Has_GameSdfMotion;
extern node_desc GAME_NODE_Add_GameSdfMotion;
extern node_desc GAME_NODE_Remove_GameSdfMotion;
extern const ecs_component_desc GAME_COMPONENT_GameCollider;
extern const ecs_component_desc GAME_COMPONENT_GameHealth;
extern const ecs_component_desc GAME_COMPONENT_GameMovement;
extern const ecs_component_desc GAME_COMPONENT_GameInput;
extern const ecs_component_desc GAME_COMPONENT_GameWeapon;
extern const ecs_component_desc GAME_COMPONENT_GameProjectile;
extern const ecs_component_desc GAME_COMPONENT_GameAppearance;
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
extern GameMovement *GAME_GameMovement_get(ecs_world *, ecs_entity);
extern const GameMovement *GAME_GameMovement_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameMovement_has(const ecs_world *, ecs_entity);
extern int GAME_GameMovement_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameMovement_remove(ecs_world *, ecs_entity, uint64_t);
extern GameInput *GAME_GameInput_get(ecs_world *, ecs_entity);
extern const GameInput *GAME_GameInput_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameInput_has(const ecs_world *, ecs_entity);
extern int GAME_GameInput_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameInput_remove(ecs_world *, ecs_entity, uint64_t);
extern GameWeapon *GAME_GameWeapon_get(ecs_world *, ecs_entity);
extern const GameWeapon *GAME_GameWeapon_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameWeapon_has(const ecs_world *, ecs_entity);
extern int GAME_GameWeapon_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameWeapon_remove(ecs_world *, ecs_entity, uint64_t);
extern GameProjectile *GAME_GameProjectile_get(ecs_world *, ecs_entity);
extern const GameProjectile *GAME_GameProjectile_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameProjectile_has(const ecs_world *, ecs_entity);
extern int GAME_GameProjectile_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameProjectile_remove(ecs_world *, ecs_entity, uint64_t);

extern GameAppearance *GAME_GameAppearance_get(ecs_world *, ecs_entity);
extern const GameAppearance *GAME_GameAppearance_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameAppearance_has(const ecs_world *, ecs_entity);
extern int GAME_GameAppearance_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameAppearance_remove(ecs_world *, ecs_entity, uint64_t);
extern node_desc GAME_NODE_Get_GameAppearance;
extern node_desc GAME_NODE_Set_GameAppearance;
extern node_desc GAME_NODE_Has_GameAppearance;
extern node_desc GAME_NODE_Add_GameAppearance;
extern node_desc GAME_NODE_Remove_GameAppearance;

extern node_desc GAME_NODE_Get_GameTransform;
extern node_desc GAME_NODE_Set_GameTransform;
extern node_desc GAME_NODE_Has_GameTransform;
extern node_desc GAME_NODE_Add_GameTransform;
extern node_desc GAME_NODE_Remove_GameTransform;
extern GameCollider *GAME_GameCollider_get(ecs_world *, ecs_entity);
extern const GameCollider *GAME_GameCollider_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameCollider_has(const ecs_world *, ecs_entity);
extern int GAME_GameCollider_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameCollider_remove(ecs_world *, ecs_entity, uint64_t);
extern node_desc GAME_NODE_Get_GameCollider;
extern node_desc GAME_NODE_Set_GameCollider;
extern node_desc GAME_NODE_Has_GameCollider;
extern node_desc GAME_NODE_Add_GameCollider;
extern node_desc GAME_NODE_Remove_GameCollider;
extern node_desc GAME_NODE_Get_GameHealth;
extern node_desc GAME_NODE_Set_GameHealth;
extern node_desc GAME_NODE_Has_GameHealth;
extern node_desc GAME_NODE_Add_GameHealth;
extern node_desc GAME_NODE_Remove_GameHealth;
extern node_desc GAME_NODE_Get_GameMovement;
extern node_desc GAME_NODE_Set_GameMovement;
extern node_desc GAME_NODE_Has_GameMovement;
extern node_desc GAME_NODE_Add_GameMovement;
extern node_desc GAME_NODE_Remove_GameMovement;
extern node_desc GAME_NODE_Get_GameInput;
extern node_desc GAME_NODE_Set_GameInput;
extern node_desc GAME_NODE_Has_GameInput;
extern node_desc GAME_NODE_Add_GameInput;
extern node_desc GAME_NODE_Remove_GameInput;
extern node_desc GAME_NODE_Get_GameWeapon;
extern node_desc GAME_NODE_Set_GameWeapon;
extern node_desc GAME_NODE_Has_GameWeapon;
extern node_desc GAME_NODE_Add_GameWeapon;
extern node_desc GAME_NODE_Remove_GameWeapon;
extern node_desc GAME_NODE_Get_GameProjectile;
extern node_desc GAME_NODE_Set_GameProjectile;
extern node_desc GAME_NODE_Has_GameProjectile;
extern node_desc GAME_NODE_Add_GameProjectile;
extern node_desc GAME_NODE_Remove_GameProjectile;
extern node_desc GAME_NODE_game_component_probe;

extern const ecs_component_desc GAME_COMPONENT_GameMaterial;
extern GameMaterial *GAME_GameMaterial_get(ecs_world *, ecs_entity);
extern const GameMaterial *GAME_GameMaterial_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameMaterial_has(const ecs_world *, ecs_entity);
extern int GAME_GameMaterial_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameMaterial_remove(ecs_world *, ecs_entity, uint64_t);
extern node_desc GAME_NODE_Get_GameMaterial;
extern node_desc GAME_NODE_Set_GameMaterial;
extern node_desc GAME_NODE_Has_GameMaterial;
extern node_desc GAME_NODE_Add_GameMaterial;
extern node_desc GAME_NODE_Remove_GameMaterial;

#endif
