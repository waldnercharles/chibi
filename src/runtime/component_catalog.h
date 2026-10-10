#ifndef CHIBI_COMPONENT_CATALOG_H
#define CHIBI_COMPONENT_CATALOG_H

#include "ecs/ecs.h"
#include "runtime/components.h"
#include "types/catalog.h"

/* External reflected types are emitted alongside component descriptors. */
extern const type_desc GAME_T_CF_V2;
extern const ecs_component_desc GAME_COMPONENT_EngineSystem;
extern const EngineSystem *GAME_EngineSystem_get_const(const ecs_world *, ecs_entity);

extern const ecs_component_desc GAME_COMPONENT_GameCombustion;
extern const GameCombustion *GAME_GameCombustion_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_MaterialThermal;
extern MaterialThermal *GAME_MaterialThermal_get(ecs_world *, ecs_entity);
extern const MaterialThermal *GAME_MaterialThermal_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_MaterialConstitutive;
extern const MaterialConstitutive *GAME_MaterialConstitutive_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameFuel;
extern const GameFuel *GAME_GameFuel_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameIgniter;
extern const GameIgniter *GAME_GameIgniter_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameSubstance;
extern const GameSubstance *GAME_GameSubstance_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameMaterialCavity;
extern const GameMaterialCavity *GAME_GameMaterialCavity_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameReflection;
extern const GameReflection *GAME_GameReflection_get_const(const ecs_world *, ecs_entity);

extern const ecs_component_desc GAME_COMPONENT_GameDrive;
extern GameDrive *GAME_GameDrive_get(ecs_world *, ecs_entity);
extern const GameDrive *GAME_GameDrive_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameDrive_add(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_MaterialFluidWorld;
extern MaterialFluidWorld *GAME_MaterialFluidWorld_get(ecs_world *, ecs_entity);
extern const MaterialFluidWorld *GAME_MaterialFluidWorld_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_MaterialDestructionWorld;
extern const MaterialDestructionWorld *GAME_MaterialDestructionWorld_get_const(
    const ecs_world *,
    ecs_entity
);
extern const ecs_component_desc GAME_COMPONENT_MaterialBreakProfile;
extern const MaterialBreakProfile *GAME_MaterialBreakProfile_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameDestructible;
extern const ecs_component_desc GAME_COMPONENT_MaterialTerrain;
extern MaterialTerrain *GAME_MaterialTerrain_get(ecs_world *, ecs_entity);
extern const MaterialTerrain *GAME_MaterialTerrain_get_const(const ecs_world *, ecs_entity);
extern const GameDestructible *GAME_GameDestructible_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameFragmentGlow;
extern const GameFragmentGlow *GAME_GameFragmentGlow_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameMaterialFrame;
extern const GameMaterialFrame *GAME_GameMaterialFrame_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameDestructible_has(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameFluidVolume;
extern const GameFluidVolume *GAME_GameFluidVolume_get_const(const ecs_world *, ecs_entity);

extern const ecs_component_desc GAME_COMPONENT_GameBody;
extern GameBody *GAME_GameBody_get(ecs_world *, ecs_entity);
extern const GameBody *GAME_GameBody_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameBody_has(const ecs_world *, ecs_entity);
extern int GAME_GameBody_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameBody_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameGravity;
extern GameGravity *GAME_GameGravity_get(ecs_world *, ecs_entity);
extern const GameGravity *GAME_GameGravity_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameGravity_add(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameVelocity;
extern GameVelocity *GAME_GameVelocity_get(ecs_world *, ecs_entity);
extern const GameVelocity *GAME_GameVelocity_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameVelocity_has(const ecs_world *, ecs_entity);
extern int GAME_GameVelocity_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameVelocity_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameGlobalBinding;
extern GameGlobalBinding *GAME_GameGlobalBinding_get(ecs_world *, ecs_entity);
extern const GameGlobalBinding *GAME_GameGlobalBinding_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameGlobalBinding_has(const ecs_world *, ecs_entity);
extern int GAME_GameGlobalBinding_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameGlobalBinding_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameTransform;
extern const ecs_component_desc GAME_COMPONENT_GameEnvironment;
extern const ecs_component_desc GAME_COMPONENT_GamePointLight;
extern const ecs_component_desc GAME_COMPONENT_GameDirectionalLight;
extern const ecs_component_desc GAME_COMPONENT_GameSpotLight;
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
extern GameDirectionalLight *GAME_GameDirectionalLight_get(ecs_world *, ecs_entity);
extern const GameDirectionalLight *GAME_GameDirectionalLight_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameDirectionalLight_has(const ecs_world *, ecs_entity);
extern int GAME_GameDirectionalLight_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameDirectionalLight_remove(ecs_world *, ecs_entity, uint64_t);
extern GameSpotLight *GAME_GameSpotLight_get(ecs_world *, ecs_entity);
extern const GameSpotLight *GAME_GameSpotLight_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameSpotLight_has(const ecs_world *, ecs_entity);
extern int GAME_GameSpotLight_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameSpotLight_remove(ecs_world *, ecs_entity, uint64_t);
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
extern const ecs_component_desc GAME_COMPONENT_GameCollider;
extern const ecs_component_desc GAME_COMPONENT_GameHealth;
extern const ecs_component_desc GAME_COMPONENT_GameMovement;
extern const ecs_component_desc GAME_COMPONENT_GameInput;
extern const ecs_component_desc GAME_COMPONENT_GameWeapon;
extern const ecs_component_desc GAME_COMPONENT_GameProjectile;
extern const ecs_component_desc GAME_COMPONENT_GameAppearance;
extern const ecs_component_desc *const GAME_REGISTRY_COMPONENTS[];
extern const int GAME_REGISTRY_COMPONENT_COUNT;
extern const type_catalog_pack GAME_TYPE_PACK;

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
extern const ecs_component_desc GAME_COMPONENT_GameSprite;
extern const GameSprite *GAME_GameSprite_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameTilemap;
extern GameTilemap *GAME_GameTilemap_get(ecs_world *, ecs_entity);
extern int GAME_GameTilemap_add(ecs_world *, ecs_entity, uint64_t);
extern const GameTilemap *GAME_GameTilemap_get_const(const ecs_world *, ecs_entity);

extern GameCollider *GAME_GameCollider_get(ecs_world *, ecs_entity);
extern const GameCollider *GAME_GameCollider_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameCollider_has(const ecs_world *, ecs_entity);
extern int GAME_GameCollider_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameCollider_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameMaterial;
extern GameMaterial *GAME_GameMaterial_get(ecs_world *, ecs_entity);
extern const GameMaterial *GAME_GameMaterial_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameMaterial_has(const ecs_world *, ecs_entity);
extern int GAME_GameMaterial_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameMaterial_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameShader;
extern GameShader *GAME_GameShader_get(ecs_world *, ecs_entity);
extern const GameShader *GAME_GameShader_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameShader_has(const ecs_world *, ecs_entity);
extern int GAME_GameShader_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameShader_remove(ecs_world *, ecs_entity, uint64_t);

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

extern const ecs_component_desc GAME_COMPONENT_GameMeshAsset;
extern GameMeshAsset *GAME_GameMeshAsset_get(ecs_world *, ecs_entity);
extern const GameMeshAsset *GAME_GameMeshAsset_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameMeshParts;
extern const ecs_component_desc GAME_COMPONENT_GameSdfSurface3D;
extern GameSdfSurface3D *GAME_GameSdfSurface3D_get(ecs_world *, ecs_entity);
extern const GameSdfSurface3D *GAME_GameSdfSurface3D_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameSurfaceQuality;
extern const GameSurfaceQuality *GAME_GameSurfaceQuality_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameMeshSurface2D;
extern const GameMeshSurface2D *GAME_GameMeshSurface2D_get_const(const ecs_world *, ecs_entity);
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

extern const ecs_component_desc GAME_COMPONENT_GameCamera;
extern GameCamera *GAME_GameCamera_get(ecs_world *, ecs_entity);
extern const GameCamera *GAME_GameCamera_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameCamera_has(const ecs_world *, ecs_entity);
extern int GAME_GameCamera_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameCamera_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameUi;
extern GameUi *GAME_GameUi_get(ecs_world *, ecs_entity);
extern const GameUi *GAME_GameUi_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameUi_has(const ecs_world *, ecs_entity);
extern int GAME_GameUi_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameUi_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameFog;
extern GameFog *GAME_GameFog_get(ecs_world *, ecs_entity);
extern const GameFog *GAME_GameFog_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameFog_has(const ecs_world *, ecs_entity);
extern int GAME_GameFog_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameFog_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameVision;
extern GameVision *GAME_GameVision_get(ecs_world *, ecs_entity);
extern const GameVision *GAME_GameVision_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameVision_has(const ecs_world *, ecs_entity);
extern int GAME_GameVision_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameVision_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameSensorContact;
extern GameSensorContact *GAME_GameSensorContact_get(ecs_world *, ecs_entity);
extern const GameSensorContact *GAME_GameSensorContact_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameSensorContact_has(const ecs_world *, ecs_entity);
extern int GAME_GameSensorContact_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameSensorContact_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameRenderState;
extern GameRenderState *GAME_GameRenderState_get(ecs_world *, ecs_entity);
extern const GameRenderState *GAME_GameRenderState_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameRenderState_has(const ecs_world *, ecs_entity);
extern int GAME_GameRenderState_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameRenderState_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameStarfield2D;
extern const GameStarfield2D *GAME_GameStarfield2D_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameHRC2D;
extern GameHRC2D *GAME_GameHRC2D_get(ecs_world *, ecs_entity);
extern const GameHRC2D *GAME_GameHRC2D_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameHRC2D_has(const ecs_world *, ecs_entity);
extern int GAME_GameHRC2D_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameHRC2D_remove(ecs_world *, ecs_entity, uint64_t);

extern const ecs_component_desc GAME_COMPONENT_GameTransform3D;
extern GameTransform3D *GAME_GameTransform3D_get(ecs_world *, ecs_entity);
extern const GameTransform3D *GAME_GameTransform3D_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameTransform3D_has(const ecs_world *, ecs_entity);
extern int GAME_GameTransform3D_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameTransform3D_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameGlobalTransform3D;
extern GameGlobalTransform3D *GAME_GameGlobalTransform3D_get(ecs_world *, ecs_entity);
extern const GameGlobalTransform3D *GAME_GameGlobalTransform3D_get_const(const ecs_world *, ecs_entity);
extern const ecs_component_desc GAME_COMPONENT_GameGlobalTransform2D;
extern GameGlobalTransform2D *GAME_GameGlobalTransform2D_get(ecs_world *, ecs_entity);
extern const GameGlobalTransform2D *GAME_GameGlobalTransform2D_get_const(const ecs_world *, ecs_entity);

extern const ecs_component_desc GAME_COMPONENT_GameCollider3D;
extern GameCollider3D *GAME_GameCollider3D_get(ecs_world *, ecs_entity);
extern const GameCollider3D *GAME_GameCollider3D_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameCollider3D_has(const ecs_world *, ecs_entity);
extern int GAME_GameCollider3D_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameCollider3D_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameBody3D;
extern GameBody3D *GAME_GameBody3D_get(ecs_world *, ecs_entity);
extern const GameBody3D *GAME_GameBody3D_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameBody3D_has(const ecs_world *, ecs_entity);
extern int GAME_GameBody3D_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameBody3D_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameGravity3D;
extern GameGravity3D *GAME_GameGravity3D_get(ecs_world *, ecs_entity);
extern const GameGravity3D *GAME_GameGravity3D_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameGravity3D_has(const ecs_world *, ecs_entity);
extern int GAME_GameGravity3D_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameGravity3D_remove(ecs_world *, ecs_entity, uint64_t);
extern const ecs_component_desc GAME_COMPONENT_GameCharacter3D;
extern GameCharacter3D *GAME_GameCharacter3D_get(ecs_world *, ecs_entity);
extern const GameCharacter3D *GAME_GameCharacter3D_get_const(const ecs_world *, ecs_entity);
extern int GAME_GameCharacter3D_has(const ecs_world *, ecs_entity);
extern int GAME_GameCharacter3D_add(ecs_world *, ecs_entity, uint64_t);
extern int GAME_GameCharacter3D_remove(ecs_world *, ecs_entity, uint64_t);

#endif
