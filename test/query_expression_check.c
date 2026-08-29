#include "../ecs/ecs.h"
#include "../game/generated.h"
#include "../game/query.h"

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

    char *json = game_query_save(root, resolve_component, component_name, NULL);
    char error[128] = { 0 };
    ecs_query_expr *roundtrip = game_query_load(json, resolve_component, NULL, error, sizeof error);
    failures += check(json && roundtrip, error[0] ? error : "query round-trips through JSON");
    free(json);
    ecs_query_expr_free(roundtrip);

    ecs_query_expr_free(root);
    ecs_query_expr *unknown = ecs_query_expr_has(UINT64_C(0xffff));
    failures += check(
        ecs_query_begin_expression(world, &query, unknown) == ECS_ERR_ARGUMENT,
        "unknown query terms are rejected"
    );
    ecs_query_expr_free(unknown);
    (void)movement_input;
    (void)unrelated;
    ecs_world_free(world);
    return failures;
}
