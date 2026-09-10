#ifndef CHIBI_GAME_GENERATED_H
#define CHIBI_GAME_GENERATED_H

#include "ecs/ecs.h"
#include "graph/model/node.h"
#include "runtime/components.h"

/* External reflected types are emitted alongside component descriptors. */
extern const type_desc GAME_T_CF_V2;

extern const ecs_component_desc GAME_COMPONENT_GameBody;
extern GameBody *GAME_GameBody_get(ecs_world *, ecs_entity);
extern const GameBody *GAME_GameBody_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameBody_has(const ecs_world *, ecs_entity);
extern int GAME_GameBody_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameBody_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameVelocity;
extern GameVelocity *GAME_GameVelocity_get(ecs_world *, ecs_entity);
extern const GameVelocity *GAME_GameVelocity_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameVelocity_has(const ecs_world *, ecs_entity);
extern int GAME_GameVelocity_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameVelocity_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameTransform;
extern const ecs_component_desc GAME_COMPONENT_GameEnvironment;
extern const ecs_component_desc GAME_COMPONENT_GamePointLight;
extern const ecs_component_desc GAME_COMPONENT_GameSurfaceInfluence;
extern GameSurfaceInfluence *GAME_GameSurfaceInfluence_get(ecs_world *, ecs_entity);
extern const GameSurfaceInfluence *GAME_GameSurfaceInfluence_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameSurfaceInfluence_has(const ecs_world *, ecs_entity);
extern int GAME_GameSurfaceInfluence_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameSurfaceInfluence_remove(ecs_world *, ecs_entity, uint64_t);
extern GamePointLight *GAME_GamePointLight_get(ecs_world *, ecs_entity);
extern const GamePointLight *GAME_GamePointLight_get_const(const ecs_world *, ecs_entity);
extern int GAME_GamePointLight_has(const ecs_world *, ecs_entity);
extern int GAME_GamePointLight_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GamePointLight_remove(ecs_world *, ecs_entity, uint64_t);
extern GameEnvironment *GAME_GameEnvironment_get(ecs_world *, ecs_entity);
extern const GameEnvironment *GAME_GameEnvironment_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameEnvironment_has(const ecs_world *, ecs_entity);
extern int GAME_GameEnvironment_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameEnvironment_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameGeometry3D;
extern GameGeometry3D *GAME_GameGeometry3D_get(ecs_world *, ecs_entity);
extern const GameGeometry3D *GAME_GameGeometry3D_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameGeometry3D_has(const ecs_world *, ecs_entity);
extern int GAME_GameGeometry3D_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameGeometry3D_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameAim;
extern GameAim *GAME_GameAim_get(ecs_world *, ecs_entity);
extern const GameAim *GAME_GameAim_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameAim_has(const ecs_world *, ecs_entity);
extern int GAME_GameAim_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameAim_remove(ecs_world *, ecs_entity, uint64_t);
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

extern const ecs_component_desc GAME_COMPONENT_GameShader;
extern GameShader *GAME_GameShader_get(ecs_world *, ecs_entity);
extern const GameShader *GAME_GameShader_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameShader_has(const ecs_world *, ecs_entity);
extern int GAME_GameShader_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameShader_remove(ecs_world *, ecs_entity, uint64_t);
extern node_desc GAME_NODE_Get_GameShader;
extern node_desc GAME_NODE_Set_GameShader;
extern node_desc GAME_NODE_Has_GameShader;
extern node_desc GAME_NODE_Add_GameShader;
extern node_desc GAME_NODE_Remove_GameShader;

extern const ecs_component_desc GAME_COMPONENT_GameDash;
extern GameDash *GAME_GameDash_get(ecs_world *, ecs_entity);
extern const GameDash *GAME_GameDash_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameDash_has(const ecs_world *, ecs_entity);
extern int GAME_GameDash_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameDash_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameEncounter;
extern GameEncounter *GAME_GameEncounter_get(ecs_world *, ecs_entity);
extern const GameEncounter *GAME_GameEncounter_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameEncounter_has(const ecs_world *, ecs_entity);
extern int GAME_GameEncounter_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameEncounter_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameEncounterMember;
extern GameEncounterMember *GAME_GameEncounterMember_get(ecs_world *, ecs_entity);
extern const GameEncounterMember *GAME_GameEncounterMember_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameEncounterMember_has(const ecs_world *, ecs_entity);
extern int GAME_GameEncounterMember_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameEncounterMember_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameRhythm;
extern GameRhythm *GAME_GameRhythm_get(ecs_world *, ecs_entity);
extern const GameRhythm *GAME_GameRhythm_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameRhythm_has(const ecs_world *, ecs_entity);
extern int GAME_GameRhythm_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameRhythm_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameVisualTransform;
extern GameVisualTransform *GAME_GameVisualTransform_get(ecs_world *, ecs_entity);
extern const GameVisualTransform *GAME_GameVisualTransform_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameVisualTransform_has(const ecs_world *, ecs_entity);
extern int GAME_GameVisualTransform_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameVisualTransform_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameVisualMotion;
extern GameVisualMotion *GAME_GameVisualMotion_get(ecs_world *, ecs_entity);
extern const GameVisualMotion *GAME_GameVisualMotion_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameVisualMotion_has(const ecs_world *, ecs_entity);
extern int GAME_GameVisualMotion_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameVisualMotion_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameMeshParts;
extern GameMeshParts *GAME_GameMeshParts_get(ecs_world *, ecs_entity);
extern const GameMeshParts *GAME_GameMeshParts_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameMeshParts_has(const ecs_world *, ecs_entity);
extern int GAME_GameMeshParts_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameMeshParts_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameParticleEmitter;
extern GameParticleEmitter *GAME_GameParticleEmitter_get(ecs_world *, ecs_entity);
extern const GameParticleEmitter *GAME_GameParticleEmitter_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameParticleEmitter_has(const ecs_world *, ecs_entity);
extern int GAME_GameParticleEmitter_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameParticleEmitter_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameSpawnRequest;
extern GameSpawnRequest *GAME_GameSpawnRequest_get(ecs_world *, ecs_entity);
extern const GameSpawnRequest *GAME_GameSpawnRequest_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameSpawnRequest_has(const ecs_world *, ecs_entity);
extern int GAME_GameSpawnRequest_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameSpawnRequest_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameTrail;
extern GameTrail *GAME_GameTrail_get(ecs_world *, ecs_entity);
extern const GameTrail *GAME_GameTrail_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameTrail_has(const ecs_world *, ecs_entity);
extern int GAME_GameTrail_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameTrail_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameExperience;
extern GameExperience *GAME_GameExperience_get(ecs_world *, ecs_entity);
extern const GameExperience *GAME_GameExperience_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameExperience_has(const ecs_world *, ecs_entity);
extern int GAME_GameExperience_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameExperience_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GamePickup;
extern GamePickup *GAME_GamePickup_get(ecs_world *, ecs_entity);
extern const GamePickup *GAME_GamePickup_get_const(const ecs_world *, ecs_entity);
extern int GAME_GamePickup_has(const ecs_world *, ecs_entity);
extern int GAME_GamePickup_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GamePickup_remove(ecs_world *, ecs_entity, uint64_t);

#endif
