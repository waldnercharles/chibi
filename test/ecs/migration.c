#include "test/suite_internal.h"

typedef struct
{
    int current;
    int maximum;
    int armor;
} migrated_health;

static const type_field fields[] = {
    { "current", PIN_I32, offsetof(migrated_health, current), sizeof(int), "int" },
    { "maximum", PIN_I32, offsetof(migrated_health, maximum), sizeof(int), "int" },
    { "armor", PIN_I32, offsetof(migrated_health, armor), sizeof(int), "int" },
};
static const type_desc health_type = { "GameHealthV2", sizeof(migrated_health), fields, 3 };

void run_ecs_migration_checks()
{
    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity entity = ecs_world_create_entity(world);
    GAME_GameHealth_add(world, entity, 1);
    ecs_world_apply_commands(world);
    GameHealth *health = GAME_GameHealth_get(world, entity);
    health->current = 75;
    health->maximum = 100;

    ecs_entity high = entity;
    for (int i = 1; i < 20001; i++) {
        high = ecs_world_record_create(world, (uint64_t)i);
        assert(high.slot);
    }
    assert(!ecs_world_apply_commands(world));
    assert(!GAME_GameHealth_add(world, high, 0));
    assert(!ecs_world_apply_commands(world));
    *GAME_GameHealth_get(world, high) = (GameHealth){ 25, 50 };

    ecs_component_desc replacement = GAME_COMPONENT_GameHealth;
    replacement.name = "GameHealthV2";
    replacement.type = &health_type;
    replacement.element_size = sizeof(migrated_health);
    replacement.layout_hash = ecs_layout_hash(&health_type);
    ecs_migration_report report;
    test_check(
        ecs_world_migrate_component(world, &replacement, &report) == ECS_OK,
        "component migration accepts an evolved layout with the same identity"
    );
    migrated_health *migrated = ecs_component_data(world, entity, replacement.id);
    test_check(
        report.changed && report.preserved_fields == 4 &&
            report.zeroed_fields == 2 && migrated && migrated->current == 75 &&
            migrated->maximum == 100 && migrated->armor == 0,
        "component migration preserves matching fields and initializes new "
        "fields"
    );
    test_check(
        !strcmp(ecs_world_component(world, replacement.id)->name, "GameHealthV2"),
        "stable component identity permits a reflected rename"
    );
    test_check(
        ecs_world_migrate_component(world, &replacement, &report) == ECS_OK &&
            !report.changed &&
            ecs_component_data(world, entity, replacement.id) == migrated,
        "an unchanged layout preserves its storage"
    );
    migrated_health *high_value = ecs_component_data(world, high, replacement.id);
    test_check(
        high_value->current == 25 && high_value->maximum == 50 && !high_value->armor,
        "schema migration preserves component values above the former entity "
        "limit"
    );
    replacement.key = "different.identity";
    test_check(
        ecs_world_migrate_component(world, &replacement, &report) == ECS_ERR_ARGUMENT &&
            ecs_component_data(world, entity, replacement.id) == migrated &&
            migrated->current == 75 && ecs_entity_is_alive(world, entity),
        "rejected component identity preserves the live entity and its values"
    );
    ecs_world_free(world);
}
