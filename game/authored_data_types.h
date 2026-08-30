#ifndef CBLUEPRINT_GAME_AUTHORED_DATA_TYPES_H
#define CBLUEPRINT_GAME_AUTHORED_DATA_TYPES_H

#include "../core/annotate.h"

/* Asset schemas are reflected types, not ECS columns. Runtime composition still
 * names ordinary reflected components in the asset JSON. */
#define GAME_DATA_TYPE(NAME) NODE_TYPE(NAME)

typedef GAME_DATA_TYPE(GamePlayerData)
{
    float max_health;
    float move_speed;
    float fire_rate;
}
GamePlayerData;
typedef GAME_DATA_TYPE(GameEnemyData)
{
    float max_health;
    float move_speed;
    float contact_damage;
}
GameEnemyData;
typedef GAME_DATA_TYPE(GameWaveData)
{
    float spawn_interval;
    int enemy_count;
    int first_wave;
}
GameWaveData;
typedef GAME_DATA_TYPE(GameArenaData)
{
    float width;
    float height;
    float spawn_radius;
}
GameArenaData;
typedef GAME_DATA_TYPE(GameWeaponData)
{
    float damage;
    float cooldown;
    int projectile_count;
}
GameWeaponData;
typedef GAME_DATA_TYPE(GameProjectileData)
{
    float speed;
    float lifetime;
    float damage;
}
GameProjectileData;
typedef GAME_DATA_TYPE(GameProgressionData)
{
    int xp_per_enemy;
    int xp_per_level;
    float difficulty_scale;
}
GameProgressionData;
typedef GAME_DATA_TYPE(GameSupportData)
{
    float heal_amount;
    float cooldown;
    int charges;
}
GameSupportData;
typedef GAME_DATA_TYPE(GameMaterialData)
{
    float friction;
    float damage_multiplier;
    float emission;
}
GameMaterialData;

#endif
