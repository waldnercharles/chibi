#include "assets/project.h"
#include "suite_internal.h"

#include <assert.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool folder_matches(
    const char *content_root,
    const char *folder,
    const char *const *expected,
    int expected_count
)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", content_root, folder);
    DIR *directory = opendir(path);
    if (!directory) {
        return expected_count == 0;
    }
    bool seen[64] = { 0 };
    bool valid = true;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char child[512];
        snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
        struct stat info;
        if (stat(child, &info) != 0 || !S_ISREG(info.st_mode)) {
            valid = false;
            continue;
        }
        int found = -1;
        for (int i = 0; i < expected_count; i++) {
            if (!strcmp(entry->d_name, expected[i])) {
                found = i;
                break;
            }
        }
        if (found < 0 || seen[found]) {
            valid = false;
            continue;
        }
        seen[found] = true;
    }
    closedir(directory);
    for (int i = 0; i < expected_count; i++) {
        valid &= seen[i];
    }
    return valid;
}

/* The error from opening a temporary project whose manifest starts with the
   given version field. The repository projects cover the current version. */
static const char *version_error(const char *version, char *error, size_t error_size)
{
    char root[] = "/tmp/chibi-project-version-XXXXXX";
    assert(mkdtemp(root));
    char manifest[64];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    FILE *file = fopen(manifest, "w");
    assert(file);
    fprintf(file, "%scontent: content\n", version);
    fclose(file);
    chibi_project project = { 0 };
    error[0] = '\0';
    chibi_project_open(&project, root, error, error_size);
    chibi_project_free(&project);
    unlink(manifest);
    rmdir(root);
    return error;
}

static void check_project_versions()
{
    char older[32], newer[32], error[256];
    snprintf(older, sizeof older, "version: %d\n", CHIBI_PROJECT_VERSION - 1);
    snprintf(newer, sizeof newer, "version: %d\n", CHIBI_PROJECT_VERSION + 1);
    bool valid = strstr(version_error(older, error, sizeof error), "needs an older build") &&
                 strstr(version_error(newer, error, sizeof error), "needs a newer build") &&
                 strstr(version_error("", error, sizeof error), "needs an integer version");
    test_check(valid, "project manifests open only at this build's project version");
}

void run_content_catalog_checks()
{
    check_project_versions();
    static const char *const behaviors[] = {
        "ArenaDropBehavior.yaml",
        "ArenaEnemyVisualBehavior.yaml",
        "ArenaImpactFeedback.yaml",
        "ArenaPickupBehavior.yaml",
        "ArenaProjectileVisualBehavior.yaml",
        "ArenaShardBehavior.yaml",
        "ArenaShipMotionBehavior.yaml",
        "ArenaWaveBehavior.yaml",
        "BloomCircuitGoal.yaml",
        "CombatFeedback.yaml",
        "EnemyBehavior.yaml",
        "HealthBehavior.yaml",
        "OrbitLaunch.yaml",
        "OrbitFlipperMotion.yaml",
        "PhaseWardenBehavior.yaml",
        "PlayerContactBehavior.yaml",
        "ProjectileBehavior.yaml",
        "ProjectileHitBehavior.yaml",
        "SpawnTelegraphBehavior.yaml",
        "SurfacePulseBehavior.yaml",
        "TwinStickInputBehavior.yaml",
        "TwinStickWeaponBehavior.yaml",
        "WavePulseBehavior.yaml",
    };
    static const char *const events[] = {
        "CollectExperience.yaml", "Damage.yaml", "Damaged.yaml", "Died.yaml",
        "RefreshEncounter.yaml",
    };
    static const char *const libraries[] = {
        "DirectionMath.yaml",
        "MovementActions.yaml",
        "WeaponActions.yaml",
    };
    static const char *const materials[] = {
        "ArenaEnemy.yaml",       "ArenaExperience.yaml",     "ArenaFloor.yaml",
        "ArenaPlayer.yaml",      "ArenaProjectile.yaml",     "ArenaRim.yaml",
        "ArenaSpawnMarker.yaml", "PhaseWardenMaterial.yaml",
    };
    static const char *const prototypes[] = {
        "ActionBinding.yaml",    "ArenaDeathPulse.yaml",
        "ArenaEncounter.yaml",   "ArenaEnemy.yaml",
        "ArenaEnemyShard.yaml",  "ArenaEnvironment.yaml",
        "ArenaExperience.yaml",  "ArenaFloor.yaml",
        "ArenaHitPulse.yaml",    "ArenaPlayer.yaml",
        "ArenaProjectile.yaml",  "PhaseWarden.yaml",
        "ArenaSpawnMarker.yaml", "ArenaWallHorizontal.yaml",
        "ArenaWavePulse.yaml",   "BloomCircuitGuide.yaml",
        "OrbitBall.yaml",        "OrbitBumper.yaml",
        "OrbitCabinet.yaml",     "OrbitFlipper.yaml",
        "OrbitRail.yaml",        "OrbitRing.yaml",
        "RotatingGate.yaml",
    };
    static const char *const scenes[] = { "Arena3D.yaml",
                                          "BloomCircuit.yaml",
                                          "OrbitPinball.yaml" };
    static const char *const shader_libraries[] = { "ShaderMath.yaml" };
    bool valid = folder_matches("content", "behaviors", behaviors, (int)(sizeof behaviors / sizeof *behaviors));
    valid &= folder_matches("content", "events", events, (int)(sizeof events / sizeof *events));
    valid &= folder_matches("content", "libraries", libraries, (int)(sizeof libraries / sizeof *libraries));
    valid &= folder_matches("content", "materials", materials, (int)(sizeof materials / sizeof *materials));
    valid &= folder_matches("content", "prototypes", prototypes, (int)(sizeof prototypes / sizeof *prototypes));
    valid &= folder_matches("content", "scenes", scenes, (int)(sizeof scenes / sizeof *scenes));
    valid &= folder_matches(
        "content",
        "shader-libraries",
        shader_libraries,
        (int)(sizeof shader_libraries / sizeof *shader_libraries)
    );
    static const char *const shapes[] = { "PhaseWardenShape.yaml" };
    valid &= folder_matches("content", "shapes", shapes, (int)(sizeof shapes / sizeof *shapes));
    valid &= folder_matches("content", "textures", NULL, 0);
    static const char *const ui[] = { "BloomCircuit.rml", "BloomCircuit.rcss",
                                      "OrbitPinball.rml", "OrbitPinball.rcss",
                                      "PauseMenu.rml",    "PauseMenu.rcss" };
    valid &= folder_matches("content", "ui", ui, (int)(sizeof ui / sizeof *ui));
    test_check(valid, "Arena3D project catalog contains only its shipped assets");

    static const char *const arena2d_behaviors[] = {
        "BoostEmitter.yaml",
        "CameraFollow.yaml",
        "CutMaterial.yaml",
        "HudDisplay.yaml",
        "HullDamage.yaml",
        "MaterialImpactHealth.yaml",
        "Movement.yaml",
        "MovementEmitter.yaml",
        "PlayerEffects.yaml",
        "ProjectileEffects.yaml",
        "ProjectileHit.yaml",
        "Pursuit.yaml",
        "SwarmerEffects.yaml",
        "Weapon.yaml",
        "ProjectileLifecycle.yaml",
    };
    static const char *const arena2d_materials[] = {
        "Basalt.yaml",
    };
    static const char *const arena2d_events[] = {
        "CollectExperience.yaml", "Damage.yaml",
        "Damaged.yaml",           "Died.yaml",
        "Material Impact.yaml",   "Material Result.yaml",
        "RefreshEncounter.yaml",
    };
    static const char *const arena2d_prototypes[] = {
        "Acid.yaml",
        "BasaltDeposits.yaml",
        "IceDeposits.yaml",
        "Jelly.yaml",
        "SlagDeposits.yaml",
        "LabChiller.yaml",
        "LabHeater.yaml",
        "LabMetal.yaml",
        "LabWall.yaml",
        "Ash.yaml",
        "Dust.yaml",
        "Exhaust.yaml",
        "Fire.yaml",
        "Fog.yaml",
        "FuelGas.yaml",
        "Lava.yaml",
        "MoltenMetal.yaml",
        "Oil.yaml",
        "ReactionDouse.yaml",
        "ReactionMetalQuench.yaml",
        "ReactionQuench.yaml",
        "Sand.yaml",
        "Slime.yaml",
        "Smoke.yaml",
        "Spores.yaml",
        "Steam.yaml",
        "Water.yaml",
        "BoostEmitter.yaml",
        "Camera.yaml",
        "Cave.yaml",
        "Cloud.yaml",
        "Crystal.yaml",
        "DeathEmitter.yaml",
        "DriftingMarker.yaml",
        "FireMouse.yaml",
        "FireStick.yaml",
        "FreeMotionSystem.yaml",
        "FuelCloud.yaml",
        "HardRock.yaml",
        "HRC.yaml",
        "HitEmitter.yaml",
        "HUD.yaml",
        "Liquid.yaml",
        "MovementEmitter.yaml",
        "MaterialSpecimen.yaml",
        "MaterialStudyCamera.yaml",
        "MaterialStudyWorld.yaml",
        "Player.yaml",
        "Projectile.yaml",
        "SpawnEmitter.yaml",
        "Swarmer.yaml",
        "TrailEmitter.yaml",
    };
    static const char *const arena2d_scenes[] = {
        "HRCBounce.yaml",      "HRCPinhole.yaml", "HRCShadow.yaml",
        "HRCSinglePixel.yaml", "HRCVolume.yaml",  "MaterialStudy.yaml",
        "MatterLab.yaml",      "Sandbox.yaml",
    };
    static const char *const arena2d_libraries[] = { "DirectionMath.yaml",
                                                     "FieldShapes.yaml",
                                                     "WeaponActions.yaml" };
    static const char *const arena2d_fields[] = { "BasaltCave.yaml",
                                                  "CaveField.yaml",
                                                  "Empty.yaml",
                                                  "MaterialIslands.yaml" };
    static const char *const arena2d_systems[] = { "FreeMotion.yaml" };
    const char *arena2d_root = "projects/arena2d/content";
    valid = folder_matches(
        arena2d_root,
        "behaviors",
        arena2d_behaviors,
        (int)(sizeof arena2d_behaviors / sizeof *arena2d_behaviors)
    );
    valid &= folder_matches(
        arena2d_root,
        "fields",
        arena2d_fields,
        (int)(sizeof arena2d_fields / sizeof *arena2d_fields)
    );
    valid &= folder_matches(arena2d_root, "systems", arena2d_systems, 1);
    valid &= folder_matches(
        arena2d_root,
        "events",
        arena2d_events,
        (int)(sizeof arena2d_events / sizeof *arena2d_events)
    );
    valid &= folder_matches(
        arena2d_root,
        "libraries",
        arena2d_libraries,
        (int)(sizeof arena2d_libraries / sizeof *arena2d_libraries)
    );
    valid &= folder_matches(
        arena2d_root,
        "materials",
        arena2d_materials,
        (int)(sizeof arena2d_materials / sizeof *arena2d_materials)
    );
    valid &= folder_matches(
        arena2d_root,
        "prototypes",
        arena2d_prototypes,
        (int)(sizeof arena2d_prototypes / sizeof *arena2d_prototypes)
    );
    valid &= folder_matches(
        arena2d_root,
        "scenes",
        arena2d_scenes,
        (int)(sizeof arena2d_scenes / sizeof *arena2d_scenes)
    );
    valid &= folder_matches(
        arena2d_root,
        "shader-libraries",
        shader_libraries,
        (int)(sizeof shader_libraries / sizeof *shader_libraries)
    );
    valid &= folder_matches(arena2d_root, "shapes", NULL, 0);
    valid &= folder_matches(arena2d_root, "textures", NULL, 0);
    static const char *const arena2d_ui[] = {
        "HUD.rml",   "HUD.rcss",   "MenuButton.rml", "MenuButton.rcss",
        "Meter.rml", "Meter.rcss", "PauseMenu.rml",  "PauseMenu.rcss"
    };
    valid &= folder_matches(arena2d_root, "ui", arena2d_ui, (int)(sizeof arena2d_ui / sizeof *arena2d_ui));
    test_check(valid, "Arena2D project catalog is isolated from Arena3D assets");

    char error[256] = { 0 };
    chibi_project arena3d = { 0 };
    chibi_project arena2d = { 0 };
    chibi_project arena2d_content = { 0 };
    chibi_project default_project = { 0 };
    char *arena3d_scene = NULL;
    char *arena2d_scene = NULL;
    char *arena2d_content_scene = NULL;
    bool projects_valid = chibi_project_open(&arena3d, ".", error, sizeof error) == 0 &&
                          chibi_project_open(&arena2d, "projects/arena2d", error, sizeof error) ==
                              0 &&
                          chibi_project_open(&arena2d_content, "projects/arena2d/content", error, sizeof error) ==
                              0 &&
                          chibi_project_open(&default_project, NULL, error, sizeof error) ==
                              0;
    if (projects_valid) {
        arena3d_scene = chibi_project_scene_path(&arena3d, NULL);
        arena2d_scene = chibi_project_scene_path(&arena2d, NULL);
        arena2d_content_scene = chibi_project_scene_path(&arena2d_content, NULL);
        projects_valid = arena3d.name && !strcmp(arena3d.name, "Arena3D") &&
                         arena2d.name && !strcmp(arena2d.name, "Neon Swarm") &&
                         !strcmp(default_project.root, arena2d.root) &&
                         !strcmp(default_project.start_scene, arena2d.start_scene) &&
                         arena2d_content.name &&
                         !strcmp(arena2d_content.name, "arena2d") && arena3d_scene &&
                         strstr(arena3d_scene, "/content/scenes/Arena3D.yaml") &&
                         arena2d_scene &&
                         strstr(arena2d_scene, "/projects/arena2d/content/scenes/Sandbox.yaml") &&
                         arena2d_content_scene &&
                         strstr(arena2d_content_scene, "/projects/arena2d/content/scenes/Sandbox.yaml");
    }
    test_check(projects_valid, "project roots and legacy content folders resolve independent scenes");
    free(arena3d_scene);
    free(arena2d_scene);
    free(arena2d_content_scene);
    chibi_project_free(&arena3d);
    chibi_project_free(&arena2d);
    chibi_project_free(&arena2d_content);
    chibi_project_free(&default_project);
}
