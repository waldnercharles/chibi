#include "../core/text_file.h"
#include "../ecs/ecs.h"
#include "../game/generated.h"
#include "../game/query.h"
#include "../game/query_editor.h"
#include "../game/system_document.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition ? 0 : 1;
}

static int add(ecs_world *world, ecs_entity entity, ecs_component_id component)
{
    return ecs_world_record_add(world, entity, component, NULL, 0) == ECS_OK;
}

static int check_slots(ecs_world *world, const ecs_query_expr *expression, const uint32_t *expected, int expected_count)
{
    ecs_query query;
    if (ecs_query_begin_expression(world, &query, expression) != ECS_OK) {
        return 1;
    }
    int failures = 0;
    int count = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity) > 0) {
        failures += check(
            count < expected_count && entity.slot == expected[count],
            "planned expression query preserves ascending slot order"
        );
        count++;
    }
    failures += check(count == expected_count, "planned expression query has the expected count");
    ecs_query_end(&query);
    return failures;
}

static const ecs_component_desc *resolve_component(const char *name, void *)
{
    if (!strcmp(name, GAME_COMPONENT_GameTransform.name)) {
        return &GAME_COMPONENT_GameTransform;
    }
    if (!strcmp(name, GAME_COMPONENT_GameMovement.name)) {
        return &GAME_COMPONENT_GameMovement;
    }
    if (!strcmp(name, GAME_COMPONENT_GameInput.name)) {
        return &GAME_COMPONENT_GameInput;
    }
    return NULL;
}

static const char *component_name(ecs_component_id id, void *)
{
    if (id == GAME_COMPONENT_GameTransform.id) {
        return GAME_COMPONENT_GameTransform.name;
    }
    if (id == GAME_COMPONENT_GameMovement.id) {
        return GAME_COMPONENT_GameMovement.name;
    }
    if (id == GAME_COMPONENT_GameInput.id) {
        return GAME_COMPONENT_GameInput.name;
    }
    return NULL;
}

static char *append_query(const char *blueprint_json)
{
    size_t size = strlen(blueprint_json);
    while (size &&
           (blueprint_json[size - 1] == ' ' || blueprint_json[size - 1] == '\n')) {
        size--;
    }
    const char query[] = ",\"query\":{\"op\":\"has\",\"component\":"
                         "\"GameTransform\"}}";
    char *json = (char *)malloc(size + sizeof query);
    memcpy(json, blueprint_json, size - 1);
    memcpy(json + size - 1, query, sizeof query);
    return json;
}

int main(void)
{
    int failures = 0;
    ecs_world *world = ecs_world_new();
    failures += check(world != NULL, "query world creates");
    if (!world) {
        return failures;
    }
    failures += check(
        ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK &&
            ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) == ECS_OK &&
            ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL) == ECS_OK,
        "query components register"
    );

    ecs_entity transform_only = ecs_world_create_entity(world);
    ecs_entity movement_input = ecs_world_create_entity(world);
    ecs_entity unrelated = ecs_world_create_entity(world);
    add(world, transform_only, GAME_COMPONENT_GameTransform.id);
    add(world, movement_input, GAME_COMPONENT_GameMovement.id);
    add(world, movement_input, GAME_COMPONENT_GameInput.id);
    ecs_world_apply_commands(world);

    ecs_query_expr *any = ecs_query_expr_new(ECS_QUERY_ANY);
    ecs_query_expr_add(any, ecs_query_expr_has(GAME_COMPONENT_GameTransform.id));
    ecs_query_expr_add(any, ecs_query_expr_has(GAME_COMPONENT_GameMovement.id));
    ecs_query_expr *root = ecs_query_expr_new(ECS_QUERY_ALL);
    ecs_query_expr_add(root, any);
    ecs_query_expr *not_input = ecs_query_expr_new(ECS_QUERY_NOT);
    ecs_query_expr_add(not_input, ecs_query_expr_has(GAME_COMPONENT_GameInput.id));
    ecs_query_expr_add(root, not_input);

    ecs_query query;
    failures += check(ecs_query_begin_expression(world, &query, root) == ECS_OK, "boolean query begins");
    ecs_entity found;
    int count = 0;
    int result;
    while ((result = ecs_query_next(&query, &found)) > 0) {
        failures += check(found.slot == transform_only.slot, "boolean query matches the expected entity");
        count++;
    }
    failures += check(result == 0 && count == 1, "boolean query returns one entity");
    ecs_query_end(&query);

    ecs_query_expr *overlap = ecs_query_expr_new(ECS_QUERY_ANY);
    ecs_query_expr_add(overlap, ecs_query_expr_has(GAME_COMPONENT_GameTransform.id));
    ecs_query_expr_add(overlap, ecs_query_expr_has(GAME_COMPONENT_GameMovement.id));
    uint32_t expected_slots[] = { transform_only.slot, movement_input.slot };
    failures += check_slots(world, overlap, expected_slots, 2);
    ecs_query_expr *nested = ecs_query_expr_new(ECS_QUERY_ANY);
    ecs_query_expr_add(nested, overlap);
    ecs_query_expr *nested_any = ecs_query_expr_new(ECS_QUERY_ANY);
    ecs_query_expr_add(
        nested_any,
        ecs_query_expr_has(GAME_COMPONENT_GameTransform.id)
    );
    ecs_query_expr_add(nested_any, ecs_query_expr_has(GAME_COMPONENT_GameInput.id));
    ecs_query_expr_add(nested, nested_any);
    failures += check_slots(world, nested, expected_slots, 2);

    char *json = game_query_save(root, resolve_component, component_name, NULL);
    char error[128] = { 0 };
    ecs_query_expr *roundtrip = game_query_load(json, resolve_component, NULL, error, sizeof error);
    failures += check(json && roundtrip, error[0] ? error : "query round-trips through JSON");
    free(json);
    ecs_query_expr_free(roundtrip);

    char *blueprint_json = text_file_read("content/systems/MovementSystem.json");
    char *document_json = blueprint_json ? append_query(blueprint_json) : NULL;
    game_system_document *document = document_json ? game_system_document_load(
                                                         document_json,
                                                         resolve_component,
                                                         NULL,
                                                         error,
                                                         sizeof error
                                                     )
                                                   : NULL;
    failures += check(document != NULL, error[0] ? error : "system document loads");
    char *saved_document = document
                             ? game_system_document_save(document, resolve_component, component_name, NULL)
                             : NULL;
    failures += check(saved_document != NULL, "system document saves");
    free(saved_document);
    game_system_document_free(document);
    free(document_json);
    free(blueprint_json);

    ecs_query_expr_free(root);
    ecs_query_expr_free(nested);
    ecs_query_expr *unknown = ecs_query_expr_has(UINT64_C(0xffff));
    failures += check(
        ecs_query_begin_expression(world, &query, unknown) == ECS_ERR_ARGUMENT,
        "unknown query terms are rejected"
    );
    ecs_query_expr_free(unknown);
    game_query_editor *query_editor = game_query_editor_new(NULL);
    int root_path[1] = { 0 };
    failures += check(
        game_query_editor_add(
            query_editor,
            NULL,
            0,
            ECS_QUERY_HAS,
            GAME_COMPONENT_GameTransform.id
        ) == 0,
        "query editor adds a term"
    );
    failures += check(
        game_query_editor_add(
            query_editor,
            NULL,
            0,
            ECS_QUERY_ANY,
            GAME_COMPONENT_GameMovement.id
        ) == 0,
        "query editor adds a nested group"
    );
    failures += check(
        game_query_editor_move(query_editor, root_path, 1, 1) == 0 &&
            game_query_editor_undo(query_editor) == 0 &&
            game_query_editor_redo(query_editor) == 0,
        "query editor reorders and tracks history"
    );
    char query_error[64] = { 0 };
    failures += check(
        game_query_editor_validate(game_query_editor_expression(query_editor), query_error, sizeof query_error) ==
            0,
        "query editor validates edited trees"
    );
    game_query_editor_free(query_editor);
    (void)movement_input;
    (void)unrelated;
    ecs_world_free(world);
    return failures;
}
