#include "../app/scene_history.h"

#include "../game/generated.h"
#include "../game/prototype.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ecs_entity add_entity(ecs_world *world, GameTransform position)
{
    ecs_entity entity = ecs_world_create_entity(world);
    assert(entity.slot);
    assert(GAME_GameTransform_add(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameTransform_get(world, entity) = position;
    return entity;
}

static void expect_position(ecs_world *world, ecs_entity entity, GameTransform position)
{
    const GameTransform *value = GAME_GameTransform_get_const(world, entity);
    assert(value && value->x == position.x && value->y == position.y);
}

typedef struct
{
    bool enabled;
    int mode;
    uint64_t target;
    unsigned color;
} history_values;

static const type_field value_fields[] = {
    { "enabled", PIN_BOOL, offsetof(history_values, enabled), sizeof(bool), "bool" },
    { "mode", PIN_ENUM, offsetof(history_values, mode), sizeof(int), "HistoryMode" },
    { "target", PIN_HANDLE, offsetof(history_values, target), sizeof(uint64_t), "ecs.entity" },
    { "color", PIN_U32, offsetof(history_values, color), sizeof(unsigned), "unsigned" },
};
static const type_desc values_type = {
    .name = "HistoryValues",
    .size = sizeof(history_values),
    .fields = value_fields,
    .field_count = 4,
};
static const ecs_component_desc values_component = {
    .id = 0x77617665,
    .name = "HistoryValues",
    .element_size = sizeof(history_values),
    .type = &values_type,
    .index = ECS_INDEX_DENSE,
};

static void scalar_history()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    assert(ecs_world_register_component(world, &values_component, NULL) == ECS_OK);
    ecs_entity entity = add_entity(world, (GameTransform){ 1, 2 });
    assert(ecs_world_record_add(world, entity, values_component.id, NULL, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    history_values *values = ecs_component_data(world, entity, values_component.id);
    *values = (history_values){ true, 2, ecs_entity_to_u64(entity), 0x123456FF };
    scene_history *history = scene_history_new();
    GameTransform moved = { 3, 4 };
    *GAME_GameTransform_get(world, entity) = moved;
    assert(scene_history_push(history, entity, (GameTransform){ 1, 2 }, moved));
    char handle_name[] = "ecs.entity";
    scene_field_edit fields[] = {
        { values_component.id, 0, { .type = PIN_BOOL }, { .type = PIN_BOOL, .b = true } },
        { values_component.id,
          1,
          { .type = PIN_ENUM, .type_name = "HistoryMode", .e = 1 },
          { .type = PIN_ENUM, .type_name = "HistoryMode", .e = 2 } },
        { values_component.id,
          2,
          { .type = PIN_HANDLE, .type_name = handle_name },
          { .type = PIN_HANDLE, .type_name = handle_name, .u64 = ecs_entity_to_u64(entity) } },
        { values_component.id,
          3,
          { .type = PIN_U32, .u = 0xFFFFFFFF },
          { .type = PIN_U32, .u = 0x123456FF } },
    };
    assert(scene_history_push_fields(history, entity, fields, 4));
    memset(handle_name, 'x', sizeof handle_name - 1);
    memset(fields, 0, sizeof fields);
    assert(scene_history_undo(history, world, NULL) == 1);
    assert(
        !values->enabled && values->mode == 1 && values->target == 0 &&
        values->color == 0xFFFFFFFF
    );
    expect_position(world, entity, moved);
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, entity, (GameTransform){ 1, 2 });
    pin_value unchanged = { .type = PIN_U32, .u = 0xFFFFFFFF, .type_name = "unsigned" };
    pin_value same = { .type = PIN_U32, .u = 0xFFFFFFFF };
    assert(!scene_history_push_field(history, entity, values_component.id, 3, unchanged, same));
    assert(!scene_history_push_fields(history, entity, NULL, 0));
    assert(scene_history_can_redo(history));
    pin_value bad = { .type = PIN_STRUCT, .ptr = values };
    assert(!scene_history_push_field(history, entity, values_component.id, 0, bad, bad));
    bad = (pin_value){ .type = PIN_STR, .str = "borrowed" };
    assert(!scene_history_push_field(history, entity, values_component.id, 0, bad, bad));
    bad = (pin_value){ .type = PIN_U32, .container = PIN_CONTAINER_ARRAY, .ptr = values };
    assert(!scene_history_push_field(history, entity, values_component.id, 0, bad, bad));
    assert(scene_history_can_redo(history));
    assert(scene_history_redo(history, world, NULL) == 1);
    assert(scene_history_redo(history, world, NULL) == 1);
    assert(values->enabled && values->mode == 2 && values->target == ecs_entity_to_u64(entity));
    assert(values->color == 0x123456FF);
    assert(scene_history_undo(history, world, NULL) == 1);
    values->color = 7;
    assert(scene_history_push_field(
        history,
        entity,
        values_component.id,
        3,
        (pin_value){ .type = PIN_U32, .u = 0xFFFFFFFF },
        (pin_value){ .type = PIN_U32, .u = 7 }
    ));
    assert(!scene_history_can_redo(history));
    scene_history_clear(history);
    scene_field_edit atomic[] = {
        { GAME_COMPONENT_GameTransform.id,
          0,
          { .type = PIN_F32, .f = 1 },
          { .type = PIN_F32, .f = 3 } },
        { values_component.id, 3, { .type = PIN_U32, .u = 1 }, { .type = PIN_U32, .u = 7 } },
    };
    scene_field_edit duplicate[] = { atomic[0], atomic[0] };
    assert(!scene_history_push_fields(history, entity, duplicate, 2));
    assert(!scene_history_can_undo(history));
    assert(scene_history_push_fields(history, entity, atomic, 2));
    assert(ecs_world_record_remove(world, entity, values_component.id, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    ecs_entity selected = { 77, 88 };
    assert(scene_history_undo(history, world, &selected) == -1);
    expect_position(world, entity, moved);
    assert(selected.slot == 77 && selected.generation == 88);
    assert(scene_history_can_undo(history) && !scene_history_can_redo(history));
    scene_history_clear(history);
    assert(scene_history_push_field(
        history,
        entity,
        GAME_COMPONENT_GameTransform.id,
        0,
        (pin_value){ .type = PIN_I32, .i = 1 },
        (pin_value){ .type = PIN_I32, .i = 3 }
    ));
    assert(scene_history_undo(history, world, NULL) == -1);
    expect_position(world, entity, moved);
    scene_history_clear(history);
    assert(scene_history_push_fields(history, entity, atomic, 1));
    assert(ecs_world_destroy_entity(world, entity) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    ecs_entity replacement = add_entity(world, (GameTransform){ 90, 91 });
    assert(replacement.slot == entity.slot && replacement.generation != entity.generation);
    assert(scene_history_undo(history, world, NULL) == -1);
    expect_position(world, replacement, (GameTransform){ 90, 91 });
    scene_history_free(history);
    ecs_world_free(world);
}

static void actor_history()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    scene_history *history = scene_history_new();
    ecs_entity entity = add_entity(world, (GameTransform){ 1, 2 });
    ecs_entity unrelated = entity;
    unrelated.generation += 50;
    assert(
        scene_history_push(history, unrelated, (GameTransform){ 50, 60 }, (GameTransform){ 70, 80 })
    );
    char name[] = "Actor", prototype[] = "Player", base[] = "{base}",
         state[] = "{state}";
    scene_actor_edit actor = { 2, name, prototype, base, state, true };
    assert(scene_history_push_actor(history, entity, &actor));
    memset(name, 'x', sizeof name - 1);
    memset(prototype, 'x', sizeof prototype - 1);
    memset(base, 'x', sizeof base - 1);
    memset(state, 'x', sizeof state - 1);
    ecs_entity observed = { 0 };
    bool present = true;
    const scene_actor_edit *peek = scene_history_peek_actor(history, false, &observed, &present);
    assert(peek && !present && peek->index == 2 && peek->created);
    assert(strcmp(peek->name, "Actor") == 0 && strcmp(peek->prototype_name, "Player") == 0);
    assert(strcmp(peek->base_json, "{base}") == 0 && strcmp(peek->state_json, "{state}") == 0);
    assert(ecs_entity_to_u64(observed) == ecs_entity_to_u64(entity));
    assert(scene_history_undo(history, world, NULL) == -1);
    expect_position(world, entity, (GameTransform){ 1, 2 });
    assert(
        scene_history_push(history, entity, (GameTransform){ 1, 2 }, (GameTransform){ 3, 4 })
    );
    *GAME_GameTransform_get(world, entity) = (GameTransform){ 9, 4 };
    assert(scene_history_push_field(
        history,
        entity,
        GAME_COMPONENT_GameTransform.id,
        0,
        (pin_value){ .type = PIN_F32, .f = 3 },
        (pin_value){ .type = PIN_F32, .f = 9 }
    ));
    observed = (ecs_entity){ 77, 88 };
    present = true;
    assert(!scene_history_peek_actor(history, false, &observed, &present));
    assert(observed.slot == 77 && observed.generation == 88 && present);
    actor = (scene_actor_edit){ 2,        "Actor",           "Player",
                                "{base}", "{deleted state}", false };
    assert(ecs_world_destroy_entity(world, entity) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(scene_history_push_actor(history, entity, &actor));
    assert(scene_history_peek_actor(history, false, &observed, &present) && present);
    assert(scene_history_undo(history, world, NULL) == -1);
    /* The app reconstructs the actor; history rebinds only its exact old handle. */
    ecs_entity restored = add_entity(world, (GameTransform){ 9, 4 });
    assert(restored.slot == entity.slot && restored.generation != entity.generation);
    scene_history_complete_actor(history, false, restored);
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, restored, (GameTransform){ 3, 4 });
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, restored, (GameTransform){ 1, 2 });
    assert(scene_history_peek_actor(history, false, &observed, &present) && !present);
    assert(ecs_entity_to_u64(observed) == ecs_entity_to_u64(restored));
    assert(ecs_world_destroy_entity(world, restored) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    scene_history_complete_actor(history, false, (ecs_entity){ 0 });
    assert(scene_history_undo(history, world, NULL) == -1);
    /* Redo creation rebinds future scalar, transform and deletion entries too. */
    assert(scene_history_peek_actor(history, true, &observed, &present) && present);
    assert(ecs_entity_to_u64(observed) == ecs_entity_to_u64(restored));
    restored = add_entity(world, (GameTransform){ 1, 2 });
    scene_history_complete_actor(history, true, restored);
    assert(scene_history_redo(history, world, NULL) == 1);
    expect_position(world, restored, (GameTransform){ 3, 4 });
    assert(scene_history_redo(history, world, NULL) == 1);
    expect_position(world, restored, (GameTransform){ 9, 4 });
    peek = scene_history_peek_actor(history, true, &observed, &present);
    assert(
        peek && !present && !peek->created &&
        strcmp(peek->state_json, "{deleted state}") == 0
    );
    assert(ecs_entity_to_u64(observed) == ecs_entity_to_u64(restored));
    assert(scene_history_redo(history, world, NULL) == -1);
    assert(ecs_world_destroy_entity(world, restored) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    scene_history_complete_actor(history, true, (ecs_entity){ 0 });
    assert(!scene_history_can_redo(history));
    scene_history_clear(history);
    assert(!scene_history_can_undo(history));
    restored = add_entity(world, (GameTransform){ 9, 4 });
    unrelated = restored;
    unrelated.generation += 50;
    assert(
        scene_history_push(history, unrelated, (GameTransform){ 50, 60 }, (GameTransform){ 70, 80 })
    );
    assert(
        scene_history_push(history, restored, (GameTransform){ 1, 2 }, (GameTransform){ 9, 4 })
    );
    assert(scene_history_push_actor(history, restored, &actor));
    assert(ecs_world_destroy_entity(world, restored) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    restored = add_entity(world, (GameTransform){ 9, 4 });
    scene_history_complete_actor(history, false, restored);
    assert(scene_history_undo(history, world, NULL) == 1);
    assert(scene_history_undo(history, world, NULL) == -1);
    expect_position(world, restored, (GameTransform){ 1, 2 });
    scene_history_free(history);
    ecs_world_free(world);
}

static void behavior_history()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    ecs_entity entity = add_entity(world, (GameTransform){ 9, 4 });
    scene_history *history = scene_history_new();
    assert(
        scene_history_push(history, entity, (GameTransform){ 1, 2 }, (GameTransform){ 3, 4 })
    );
    ecs_entity observed = { 77, 88 };
    assert(!scene_history_peek_behaviors(history, false, &observed));
    assert(observed.slot == 77 && observed.generation == 88);
    char first[] = "Health", second[] = "Motion";
    const char *before[] = { first };
    const char *after[] = { second, first };
    scene_behavior_edit edit = { before, 1, after, 2 };
    assert(scene_history_push_behaviors(history, entity, &edit));
    memset(first, 'x', sizeof first - 1);
    memset(second, 'y', sizeof second - 1);
    before[0] = NULL;
    after[0] = after[1] = NULL;
    const scene_behavior_edit *peek = scene_history_peek_behaviors(history, false, &observed);
    assert(peek && peek->before_count == 1 && peek->after_count == 2);
    assert(strcmp(peek->before[0], "Health") == 0);
    assert(strcmp(peek->after[0], "Motion") == 0 && strcmp(peek->after[1], "Health") == 0);
    assert(ecs_entity_to_u64(observed) == ecs_entity_to_u64(entity));
    assert(!scene_history_peek_actor(history, false, NULL, NULL));
    assert(scene_history_undo(history, world, NULL) == -1);
    expect_position(world, entity, (GameTransform){ 9, 4 });
    assert(scene_history_push_field(
        history,
        entity,
        GAME_COMPONENT_GameTransform.id,
        0,
        (pin_value){ .type = PIN_F32, .f = 3 },
        (pin_value){ .type = PIN_F32, .f = 9 }
    ));
    scene_actor_edit deletion = { 0, "Actor", "Player", "{}", "{}", false };
    assert(scene_history_push_actor(history, entity, &deletion));
    assert(ecs_world_destroy_entity(world, entity) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    ecs_entity restored = add_entity(world, (GameTransform){ 9, 4 });
    scene_history_complete_actor(history, false, restored);
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, restored, (GameTransform){ 3, 4 });
    peek = scene_history_peek_behaviors(history, false, &observed);
    assert(peek && ecs_entity_to_u64(observed) == ecs_entity_to_u64(restored));
    scene_history_complete_behaviors(history, false);
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, restored, (GameTransform){ 1, 2 });
    assert(scene_history_redo(history, world, NULL) == 1);
    peek = scene_history_peek_behaviors(history, true, &observed);
    assert(peek && strcmp(peek->after[0], "Motion") == 0);
    assert(scene_history_redo(history, world, NULL) == -1);
    const char *same_before[] = { "Health" }, *same_after[] = { "Health" };
    edit = (scene_behavior_edit){ same_before, 1, same_after, 1 };
    assert(!scene_history_push_behaviors(history, restored, &edit));
    edit = (scene_behavior_edit){ 0 };
    assert(!scene_history_push_behaviors(history, restored, &edit));
    const char *duplicate[] = { "Health", "Health" };
    const char *empty[] = { "" };
    const char *missing[] = { NULL };
    scene_behavior_edit invalid[] = {
        { NULL, -1, same_after, 1 },      { NULL, 1, same_after, 1 },
        { same_before, 1, duplicate, 2 }, { empty, 1, same_after, 1 },
        { same_before, 1, missing, 1 },
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof invalid[0]); i++) {
        assert(!scene_history_push_behaviors(history, restored, &invalid[i]));
    }
    assert(scene_history_can_redo(history));
    scene_history_complete_behaviors(history, true);
    assert(scene_history_redo(history, world, NULL) == 1);
    expect_position(world, restored, (GameTransform){ 9, 4 });
    assert(scene_history_undo(history, world, NULL) == 1);
    const char *ordered[] = { "Health", "Motion" };
    const char *reordered[] = { "Motion", "Health" };
    edit = (scene_behavior_edit){ ordered, 2, reordered, 2 };
    assert(scene_history_push_behaviors(history, restored, &edit));
    assert(!scene_history_can_redo(history));
    scene_history_clear(history);
    assert(!scene_history_can_undo(history));
    scene_history_free(history);
    ecs_world_free(world);
}

static void component_history()
{
    const ecs_component_desc *descriptors[] = { &GAME_COMPONENT_GameTransform,
                                                &GAME_COMPONENT_GameHealth };
    game_prototype_registry registry = { .components = descriptors,
                                         .component_count = 2 };
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < 2; i++) {
        assert(ecs_world_register_component(world, descriptors[i], NULL) == ECS_OK);
    }
    char error[256];
    game_prototype *base = game_prototype_load(
        "{\"name\":\"Base\",\"components\":{\"GameTransform\":{\"x\":1,\"y\":2}"
        "}}",
        &registry,
        error,
        sizeof error
    );
    assert(base);
    ecs_entity entity;
    assert(game_prototype_spawn_components(base, world, &entity, error, sizeof error) == 0);
    scene_history *history = scene_history_new();
    GameTransform before = { 1, 2 }, after = { 3, 4 };
    *GAME_GameTransform_get(world, entity) = after;
    assert(scene_history_push(history, entity, before, after));
    char *captured = game_prototype_entity_component_json(world, entity, descriptors[0], &registry);
    assert(captured);
    scene_component_edit edit = { .component = descriptors[0]->id,
                                  .state_json = captured,
                                  .added = false };
    assert(GAME_GameTransform_remove(world, entity, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(scene_history_push_component(history, entity, &edit));
    memset(captured, 'x', strlen(captured));
    free(captured);
    bool present = false;
    ecs_entity selected = { 0 };
    const scene_component_edit
        *pending = scene_history_peek_component(history, false, &selected, &present);
    assert(pending && present && ecs_entity_to_u64(selected) == ecs_entity_to_u64(entity));
    assert(scene_history_undo(history, world, NULL) == -1);
    assert(
        game_prototype_apply_components_json(world, entity, pending->state_json, &registry, error, sizeof error) ==
        0
    );
    scene_history_complete_component(history, false);
    expect_position(world, entity, after);
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, entity, before);
    assert(scene_history_redo(history, world, NULL) == 1);
    pending = scene_history_peek_component(history, true, NULL, &present);
    assert(pending && !present);
    assert(
        game_prototype_apply_components_json(
            world,
            entity,
            "{\"GameTransform\":null,\"GameHealth\":{\"current\":73,"
            "\"maximum\":100}}",
            &registry,
            error,
            sizeof error
        ) == 0
    );
    scene_history_complete_component(history, true);
    char *overrides = game_prototype_capture_overrides_json(base, world, entity);
    assert(overrides && strstr(overrides, "null") && strstr(overrides, "GameHealth"));
    ecs_entity restored;
    assert(game_prototype_spawn_components(base, world, &restored, error, sizeof error) == 0);
    assert(
        game_prototype_apply_components_json(world, restored, overrides, &registry, error, sizeof error) == 0
    );
    assert(!GAME_GameTransform_has(world, restored));
    assert(GAME_GameHealth_get_const(world, restored)->current == 73);
    /* Repeated null removal must stay absent, not accidentally add a default. */
    assert(
        game_prototype_apply_components_json(world, restored, overrides, &registry, error, sizeof error) == 0
    );
    assert(!GAME_GameTransform_has(world, restored));
    game_prototype *current = game_prototype_capture_entity("Current", world, entity, &registry);
    assert(current);
    char *from_prototype = game_prototype_overrides_json(base, current);
    assert(from_prototype && strcmp(from_prototype, overrides) == 0);
    free(from_prototype);
    free(overrides);
    game_prototype_free(current);
    game_prototype_free(base);
    scene_history_free(history);
    ecs_world_free(world);
    puts("component history PASS: owned state, field chronology and added/removed override roundtrip");
}

static void grouped_history()
{
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    GameTransform before = { 1, 2 }, after = { 3, 4 };
    ecs_entity first = add_entity(world, after), second = add_entity(world, after);
    scene_history *history = scene_history_new();
    scene_history_begin_group(history);
    assert(scene_history_push(history, first, before, after));
    uint64_t group = scene_history_group_id(history, false);
    assert(group);
    assert(scene_history_push(history, second, before, after));
    scene_history_end_group(history);
    assert(scene_history_group_id(history, false) == group);
    assert(scene_history_undo(history, world, NULL) == 1);
    assert(scene_history_group_id(history, false) == group);
    assert(scene_history_group_id(history, true) == group);
    /* A failed second replay can roll back exactly the completed first entry. */
    assert(scene_history_redo(history, world, NULL) == 1);
    expect_position(world, second, after);
    assert(!scene_history_can_redo(history));
    assert(scene_history_undo(history, world, NULL) == 1);
    assert(scene_history_undo(history, world, NULL) == 1);
    assert(!scene_history_can_undo(history));
    scene_history_begin_group(history);
    assert(!scene_history_push(history, first, before, before));
    scene_history_end_group(history);
    assert(scene_history_group_id(history, true) == group);
    assert(scene_history_redo(history, world, NULL) == 1);
    assert(scene_history_redo(history, world, NULL) == 1);
    assert(scene_history_push(history, first, after, before));
    assert(scene_history_group_id(history, false) == 0);
    scene_history_begin_group(history);
    assert(scene_history_push(history, first, before, after));
    scene_history_end_group(history);
    assert(scene_history_group_id(history, false) != group);
    scene_history_clear(history);
    assert(!scene_history_group_id(history, false) && !scene_history_group_id(history, true));
    scene_history_free(history);
    ecs_world_free(world);
    puts("scene groups PASS: explicit scope, stable replay IDs, rollback and empty/no-op preservation");
}

void test_scene_history()
{
    grouped_history();
    component_history();
    scalar_history();
    actor_history();
    behavior_history();
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    scene_history *history = scene_history_new();
    assert(!scene_history_can_undo(history) && !scene_history_can_redo(history));
    assert(scene_history_undo(history, world, NULL) == 0);
    assert(scene_history_redo(history, world, NULL) == 0);
    GameTransform before = { -12.5f, 81.25f };
    GameTransform after = { 44.5f, -7.75f };
    ecs_entity entity = add_entity(world, before);
    *GAME_GameTransform_get(world, entity) = after;
    assert(scene_history_push(history, entity, before, after));
    assert(scene_history_can_undo(history) && !scene_history_can_redo(history));
    ecs_entity selected = { 0 };
    assert(scene_history_undo(history, world, &selected) == 1);
    assert(ecs_entity_to_u64(selected) == ecs_entity_to_u64(entity));
    expect_position(world, entity, before);
    assert(!scene_history_can_undo(history) && scene_history_can_redo(history));
    assert(!scene_history_push(history, entity, before, before));
    assert(!scene_history_can_undo(history) && scene_history_can_redo(history));
    assert(scene_history_redo(history, world, NULL) == 1);
    expect_position(world, entity, after);
    assert(scene_history_can_undo(history) && !scene_history_can_redo(history));
    assert(scene_history_undo(history, world, NULL) == 1);
    GameTransform replacement_edit = { 100.25f, 0.5f };
    *GAME_GameTransform_get(world, entity) = replacement_edit;
    assert(scene_history_push(history, entity, before, replacement_edit));
    assert(!scene_history_can_redo(history));
    assert(scene_history_redo(history, world, NULL) == 0);
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, entity, before);
    assert(scene_history_redo(history, world, NULL) == 1);
    expect_position(world, entity, replacement_edit);
    GameTransform second_edit = { -55.25f, 600.5f };
    *GAME_GameTransform_get(world, entity) = second_edit;
    assert(scene_history_push(history, entity, replacement_edit, second_edit));
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, entity, replacement_edit);
    assert(scene_history_can_undo(history) && scene_history_can_redo(history));
    assert(scene_history_undo(history, world, NULL) == 1);
    expect_position(world, entity, before);
    assert(!scene_history_can_undo(history) && scene_history_can_redo(history));
    assert(scene_history_redo(history, world, NULL) == 1);
    expect_position(world, entity, replacement_edit);
    assert(scene_history_can_undo(history) && scene_history_can_redo(history));
    assert(scene_history_redo(history, world, NULL) == 1);
    expect_position(world, entity, second_edit);
    assert(scene_history_can_undo(history) && !scene_history_can_redo(history));

    assert(ecs_world_destroy_entity(world, entity) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    GameTransform untouched = { 900, -400 };
    ecs_entity replacement = add_entity(world, untouched);
    assert(replacement.slot == entity.slot && replacement.generation != entity.generation);
    selected = replacement;
    assert(scene_history_undo(history, world, &selected) == -1);
    assert(ecs_entity_to_u64(selected) == ecs_entity_to_u64(replacement));
    expect_position(world, replacement, untouched);
    assert(scene_history_can_undo(history) && !scene_history_can_redo(history));
    scene_history_clear(history);
    assert(!scene_history_can_undo(history) && !scene_history_can_redo(history));

    *GAME_GameTransform_get(world, replacement) = after;
    assert(scene_history_push(history, replacement, untouched, after));
    assert(scene_history_undo(history, world, NULL) == 1);
    assert(GAME_GameTransform_remove(world, replacement, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    assert(scene_history_redo(history, world, NULL) == -1);
    assert(!scene_history_can_undo(history) && scene_history_can_redo(history));
    assert(ecs_entity_is_alive(world, replacement));
    assert(!GAME_GameTransform_has(world, replacement));
    scene_history_free(history);
    ecs_world_free(world);
    puts("scene history PASS: exact transforms, redo preservation and stale entity safety");
}
