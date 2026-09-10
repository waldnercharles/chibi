#include "suite_internal.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool folder_matches(const char *folder, const char *const *expected, int expected_count)
{
    char path[256];
    snprintf(path, sizeof path, "content/%s", folder);
    DIR *directory = opendir(path);
    if (!directory) {
        return expected_count == 0;
    }
    bool seen[32] = { 0 };
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

void run_content_catalog_checks()
{
    static const char *const behaviors[] = {
        "ArenaDropBehavior.json",
        "ArenaEnemyVisualBehavior.json",
        "ArenaImpactFeedback.json",
        "ArenaPickupBehavior.json",
        "ArenaProjectileVisualBehavior.json",
        "ArenaShardBehavior.json",
        "ArenaShipMotionBehavior.json",
        "ArenaWaveBehavior.json",
        "CombatFeedback.json",
        "EnemyBehavior.json",
        "HealthBehavior.json",
        "PlayerContactBehavior.json",
        "ProjectileBehavior.json",
        "ProjectileHitBehavior.json",
        "SpawnTelegraphBehavior.json",
        "SurfacePulseBehavior.json",
        "TwinStickInputBehavior.json",
        "TwinStickWeaponBehavior.json",
        "WavePulseBehavior.json",
    };
    static const char *const events[] = {
        "CollectExperience.json", "Damage.json", "Damaged.json", "Died.json",
        "RefreshEncounter.json",
    };
    static const char *const libraries[] = {
        "DirectionMath.json",
        "MovementActions.json",
        "WeaponActions.json",
    };
    static const char *const materials[] = {
        "ArenaEnemy.json",       "ArenaExperience.json", "ArenaFloor.json",
        "ArenaPlayer.json",      "ArenaProjectile.json", "ArenaRim.json",
        "ArenaSpawnMarker.json",
    };
    static const char *const prototypes[] = {
        "ArenaDeathPulse.json",  "ArenaEncounter.json",
        "ArenaEnemy.json",       "ArenaEnemyShard.json",
        "ArenaEnvironment.json", "ArenaExperience.json",
        "ArenaFloor.json",       "ArenaHitPulse.json",
        "ArenaPlayer.json",      "ArenaProjectile.json",
        "ArenaSpawnMarker.json", "ArenaWallHorizontal.json",
        "ArenaWavePulse.json",
    };
    static const char *const scenes[] = { "Arena3D.json" };
    static const char *const shader_libraries[] = { "ShaderMath.json" };
    bool valid = folder_matches("behaviors", behaviors, (int)(sizeof behaviors / sizeof *behaviors));
    valid &= folder_matches("events", events, (int)(sizeof events / sizeof *events));
    valid &= folder_matches("libraries", libraries, (int)(sizeof libraries / sizeof *libraries));
    valid &= folder_matches("materials", materials, (int)(sizeof materials / sizeof *materials));
    valid &= folder_matches("prototypes", prototypes, (int)(sizeof prototypes / sizeof *prototypes));
    valid &= folder_matches("scenes", scenes, (int)(sizeof scenes / sizeof *scenes));
    valid &= folder_matches(
        "shader-libraries",
        shader_libraries,
        (int)(sizeof shader_libraries / sizeof *shader_libraries)
    );
    valid &= folder_matches("shapes", NULL, 0);
    valid &= folder_matches("textures", NULL, 0);
    test_check(valid, "Arena3D content catalog contains only reachable shipped assets");
}
