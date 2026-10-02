#include "assets/project.h"
#include "suite_internal.h"
#include "support/text_file.h"

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

/* Project isolation permits additional authored assets, while requiring its
 * baseline catalog and rejecting assets owned exclusively by the other game. */
static bool folder_isolated(
    const char *root,
    const char *folder,
    const char *const *required,
    int required_count,
    const char *const *other,
    int other_count
)
{
    char path[512];
    struct stat info;
    for (int i = 0; i < required_count; i++) {
        snprintf(path, sizeof path, "%s/%s/%s", root, folder, required[i]);
        if (stat(path, &info) || !S_ISREG(info.st_mode)) {
            return false;
        }
    }
    for (int i = 0; i < other_count; i++) {
        bool shared = false;
        for (int j = 0; j < required_count; j++) {
            shared |= !strcmp(other[i], required[j]);
        }
        snprintf(path, sizeof path, "%s/%s/%s", root, folder, other[i]);
        if (!shared && stat(path, &info) == 0) {
            return false;
        }
    }
    return true;
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
    fprintf(file, "%scontent \"content\"\n", version);
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
    snprintf(older, sizeof older, "version %d\n", CHIBI_PROJECT_VERSION - 1);
    snprintf(newer, sizeof newer, "version %d\n", CHIBI_PROJECT_VERSION + 1);
    bool valid = strstr(version_error(older, error, sizeof error), "needs an older build") &&
                 strstr(version_error(newer, error, sizeof error), "needs a newer build") &&
                 strstr(version_error("", error, sizeof error), "needs an integer version");
    test_check(valid, "project manifests open only at this build's project version");
}

static void check_project_creation()
{
    char parent[] = "/tmp/chibi-project-create-XXXXXX", root[256], error[256];
    assert(mkdtemp(parent));
    snprintf(root, sizeof root, "%s/sample-project", parent);
    bool created = chibi_project_create(root, "Sample: Project", error, sizeof error) == 0;
    chibi_project project = { 0 };
    bool opened = created &&
                  chibi_project_open(&project, root, error, sizeof error) == 0;
    char *scene = opened ? chibi_project_scene_path(&project, NULL) : NULL;
    char *text = scene ? text_file_read(scene) : NULL;
    bool valid = opened && !strcmp(project.name, "Sample: Project") &&
                 !strcmp(project.start_scene, "scenes/Start.kdl") && text &&
                 strstr(text, "(array)entities");
    test_check(valid, "new projects serialize their name and open with an empty start scene");
    bool duplicate = chibi_project_create(root, "Other", error, sizeof error) != 0;
    test_check(duplicate, "project creation never overwrites an existing folder");
    free(text);
    free(scene);
    chibi_project_free(&project);
    char path[512];
    snprintf(path, sizeof path, "%s/content/scenes/Start.kdl", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/project.kdl", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/content/scenes", root);
    rmdir(path);
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    rmdir(parent);
}

void run_content_catalog_checks()
{
    check_project_versions();
    check_project_creation();
    static const char *const behaviors[] = {
        "ArenaDropBehavior.kdl",
        "ArenaEnemyVisualBehavior.kdl",
        "ArenaImpactFeedback.kdl",
        "ArenaPickupBehavior.kdl",
        "ArenaProjectileVisualBehavior.kdl",
        "ArenaShardBehavior.kdl",
        "ArenaShipMotionBehavior.kdl",
        "ArenaWaveBehavior.kdl",
        "BloomCircuitGoal.kdl",
        "CombatFeedback.kdl",
        "EnemyBehavior.kdl",
        "HealthBehavior.kdl",
        "OrbitLaunch.kdl",
        "OrbitFlipperMotion.kdl",
        "PhaseWardenBehavior.kdl",
        "PlayerContactBehavior.kdl",
        "ProjectileBehavior.kdl",
        "ProjectileHitBehavior.kdl",
        "SpawnTelegraphBehavior.kdl",
        "SurfacePulseBehavior.kdl",
        "TwinStickInputBehavior.kdl",
        "TwinStickWeaponBehavior.kdl",
        "WavePulseBehavior.kdl",
    };
    static const char *const events[] = {
        "CollectExperience.kdl", "Damage.kdl", "Damaged.kdl", "Died.kdl",
        "RefreshEncounter.kdl",
    };
    static const char *const libraries[] = {
        "DirectionMath.kdl",
        "MovementActions.kdl",
        "WeaponActions.kdl",
    };
    static const char *const materials[] = {
        "ArenaEnemy.kdl",       "ArenaExperience.kdl",     "ArenaFloor.kdl",
        "ArenaPlayer.kdl",      "ArenaProjectile.kdl",     "ArenaRim.kdl",
        "ArenaSpawnMarker.kdl", "PhaseWardenMaterial.kdl",
    };
    static const char *const prototypes[] = {
        "ActionBinding.kdl",    "ArenaDeathPulse.kdl",
        "ArenaEncounter.kdl",   "ArenaEnemy.kdl",
        "ArenaEnemyShard.kdl",  "ArenaEnvironment.kdl",
        "ArenaExperience.kdl",  "ArenaFloor.kdl",
        "ArenaHitPulse.kdl",    "ArenaPlayer.kdl",
        "ArenaProjectile.kdl",  "PhaseWarden.kdl",
        "ArenaSpawnMarker.kdl", "ArenaWallHorizontal.kdl",
        "ArenaWavePulse.kdl",   "BloomCircuitGuide.kdl",
        "OrbitBall.kdl",        "OrbitBumper.kdl",
        "OrbitCabinet.kdl",     "OrbitFlipper.kdl",
        "OrbitRail.kdl",        "OrbitRing.kdl",
        "RotatingGate.kdl",
    };
    static const char *const scenes[] = { "Arena3D.kdl",
                                          "BloomCircuit.kdl",
                                          "OrbitPinball.kdl" };
    static const char *const shader_libraries[] = { "ShaderMath.kdl" };
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
    static const char *const shapes[] = { "PhaseWardenShape.kdl" };
    valid &= folder_matches("content", "shapes", shapes, (int)(sizeof shapes / sizeof *shapes));
    valid &= folder_matches("content", "textures", NULL, 0);
    static const char *const ui[] = { "BloomCircuit.rml", "BloomCircuit.rcss",
                                      "OrbitPinball.rml", "OrbitPinball.rcss",
                                      "PauseMenu.rml",    "PauseMenu.rcss" };
    valid &= folder_matches("content", "ui", ui, (int)(sizeof ui / sizeof *ui));
    test_check(valid, "Arena3D project catalog contains only its shipped assets");

    static const char *const arena2d_behaviors[] = {
        "BoostEmitter.kdl",   "CameraFollow.kdl",
        "CutMaterial.kdl",    "HudDisplay.kdl",
        "HullDamage.kdl",     "MaterialImpactHealth.kdl",
        "Movement.kdl",       "MovementEmitter.kdl",
        "PlayerEffects.kdl",  "ProjectileEffects.kdl",
        "ProjectileHit.kdl",  "Pursuit.kdl",
        "SwarmerEffects.kdl", "ThermalStudyBreach.kdl",
        "Weapon.kdl",         "ProjectileLifecycle.kdl",
    };
    static const char *const arena2d_materials[] = {
        "Basalt.kdl",
    };
    static const char *const arena2d_events[] = {
        "CollectExperience.kdl", "Damage.kdl",
        "Damaged.kdl",           "Died.kdl",
        "Material Impact.kdl",   "Material Result.kdl",
        "RefreshEncounter.kdl",
    };
    static const char *const arena2d_prototypes[] = {
        "BoostEmitter.kdl",
        "Camera.kdl",
        "Cave.kdl",
        "Cloud.kdl",
        "Crystal.kdl",
        "DeathEmitter.kdl",
        "DriftingMarker.kdl",
        "FireMouse.kdl",
        "FireStick.kdl",
        "FreeMotionSystem.kdl",
        "FuelCloud.kdl",
        "HardRock.kdl",
        "HRC.kdl",
        "HitEmitter.kdl",
        "HUD.kdl",
        "Liquid.kdl",
        "MovementEmitter.kdl",
        "MaterialDetailWorld.kdl",
        "MaterialSpecimen.kdl",
        "MaterialStudyCamera.kdl",
        "MaterialStudyWorld.kdl",
        "Player.kdl",
        "Projectile.kdl",
        "SpawnEmitter.kdl",
        "Swarmer.kdl",
        "ThermalStudyEntity.kdl",
        "ThermalStudyWorld.kdl",
        "TrailEmitter.kdl",
    };
    static const char *const arena2d_scenes[] = {
        "HRCBounce.kdl",
        "HRCPinhole.kdl",
        "HRCShadow.kdl",
        "HRCSinglePixel.kdl",
        "HRCVolume.kdl",
        "MaterialStudy.kdl",
        "MaterialDetail.kdl",
        "MaterialDetailClosed.kdl",
        "Sandbox.kdl",
        "ThermalStudy.kdl",
        "ThermalStudyClosed.kdl",
    };
    static const char *const arena2d_libraries[] = { "DirectionMath.kdl",
                                                     "FieldShapes.kdl",
                                                     "WeaponActions.kdl" };
    static const char *const arena2d_fields[] = { "BasaltCave.kdl",
                                                  "CaveField.kdl",
                                                  "MaterialIslands.kdl" };
    static const char *const arena2d_systems[] = { "FreeMotion.kdl" };
    const char *arena2d_root = "projects/arena2d/content";
    valid = folder_isolated(
        arena2d_root,
        "behaviors",
        arena2d_behaviors,
        (int)(sizeof arena2d_behaviors / sizeof *arena2d_behaviors),
        behaviors,
        (int)(sizeof behaviors / sizeof *behaviors)
    );
    valid &= folder_isolated(
        arena2d_root,
        "fields",
        arena2d_fields,
        (int)(sizeof arena2d_fields / sizeof *arena2d_fields),
        NULL,
        0
    );
    valid &= folder_isolated(arena2d_root, "systems", arena2d_systems, 1, NULL, 0);
    valid &= folder_isolated(
        arena2d_root,
        "events",
        arena2d_events,
        (int)(sizeof arena2d_events / sizeof *arena2d_events),
        events,
        (int)(sizeof events / sizeof *events)
    );
    valid &= folder_isolated(
        arena2d_root,
        "libraries",
        arena2d_libraries,
        (int)(sizeof arena2d_libraries / sizeof *arena2d_libraries),
        libraries,
        (int)(sizeof libraries / sizeof *libraries)
    );
    valid &= folder_isolated(
        arena2d_root,
        "materials",
        arena2d_materials,
        (int)(sizeof arena2d_materials / sizeof *arena2d_materials),
        materials,
        (int)(sizeof materials / sizeof *materials)
    );
    valid &= folder_isolated(
        arena2d_root,
        "prototypes",
        arena2d_prototypes,
        (int)(sizeof arena2d_prototypes / sizeof *arena2d_prototypes),
        prototypes,
        (int)(sizeof prototypes / sizeof *prototypes)
    );
    valid &= folder_isolated(
        arena2d_root,
        "scenes",
        arena2d_scenes,
        (int)(sizeof arena2d_scenes / sizeof *arena2d_scenes),
        scenes,
        (int)(sizeof scenes / sizeof *scenes)
    );
    valid &= folder_matches(
        arena2d_root,
        "shader-libraries",
        shader_libraries,
        (int)(sizeof shader_libraries / sizeof *shader_libraries)
    );
    valid &= folder_isolated(arena2d_root, "shapes", NULL, 0, shapes, (int)(sizeof shapes / sizeof *shapes));
    valid &= folder_matches(arena2d_root, "textures", NULL, 0);
    static const char *const arena2d_ui[] = {
        "HUD.rml",   "HUD.rcss",   "MenuButton.rml", "MenuButton.rcss",
        "Meter.rml", "Meter.rcss", "PauseMenu.rml",  "PauseMenu.rcss"
    };
    valid &= folder_isolated(
        arena2d_root,
        "ui",
        arena2d_ui,
        (int)(sizeof arena2d_ui / sizeof *arena2d_ui),
        ui,
        (int)(sizeof ui / sizeof *ui)
    );
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
                         strstr(arena3d_scene, "/content/scenes/Arena3D.kdl") &&
                         arena2d_scene &&
                         strstr(arena2d_scene, "/projects/arena2d/content/scenes/Sandbox.kdl") &&
                         arena2d_content_scene &&
                         strstr(arena2d_content_scene, "/projects/arena2d/content/scenes/Sandbox.kdl");
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
