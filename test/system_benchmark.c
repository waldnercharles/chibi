#include "../core/registry.h"
#include "../core/serialize.h"
#include "../core/text_file.h"
#include "../game/generated.h"
#include "../game/system.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_ENTITIES 1000
#define DEFAULT_FRAMES 1000
#define DELTA_SECONDS (1.0 / 60.0)
#define BENCHMARK_SPEED 180.0f
#define POSITION_EPSILON 0.01f

static double elapsed_ns(struct timespec start, struct timespec end)
{
    int64_t seconds = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nanoseconds = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
    return (double)(seconds * INT64_C(1000000000) + nanoseconds);
}

static const char *system_backend_name(const game_system *system)
{
    if (game_system_uses_sljit_batch(system) && game_system_uses_sljit_simd(system)) {
        return "sljit_simd_batch";
    }
    if (game_system_uses_sljit_batch(system)) {
        return "sljit_batch";
    }
    if (game_system_uses_sljit_simd(system)) {
        return "sljit_simd";
    }
    if (game_system_uses_sljit_registers(system)) {
        return "sljit_registers";
    }
    if (game_system_uses_sljit(system)) {
        return "sljit_memory";
    }
    if (game_system_uses_typed_bytecode(system)) {
        return "typed_bytecode";
    }
    if (game_system_uses_bytecode(system)) {
        return "generic_bytecode";
    }
    return "flow_interpreter";
}

static void reset_transforms(ecs_entity *entities, int count, ecs_world *world)
{
    for (int i = 0; i < count; i++) {
        GameTransform *transform = GAME_GameTransform_get(world, entities[i]);
        transform->x = 0.0f;
        transform->y = 0.0f;
    }
}

static int check_positions(ecs_entity *entities, int count, int frame_count, ecs_world *world)
{
    float expected = BENCHMARK_SPEED * (float)DELTA_SECONDS * (float)frame_count;
    for (int i = 0; i < count; i++) {
        const GameTransform *transform = GAME_GameTransform_get_const(world, entities[i]);
        if (fabsf(transform->x - expected) > POSITION_EPSILON ||
            fabsf(transform->y) > POSITION_EPSILON) {
            return -1;
        }
    }
    return 0;
}

static int run_system(game_system *system, int frame_count, game_system_exec_mode mode, flow_error *error)
{
    for (int frame = 0; frame < frame_count; frame++) {
        if (game_system_tick_mode(system, (double)frame * DELTA_SECONDS, DELTA_SECONDS, error, mode) !=
            FLOW_OK) {
            return -1;
        }
    }
    return 0;
}

static void *native_column(const ecs_component_desc *component, ecs_entity entity)
{
    return (unsigned char *)component->column_base +
           (size_t)entity.slot * component->element_size;
}

static void run_native_query(ecs_world *world, int frame_count)
{
    const ecs_component_id components[] = {
        GAME_COMPONENT_GameTransform.id,
        GAME_COMPONENT_GameMovement.id,
        GAME_COMPONENT_GameInput.id,
    };
    const ecs_component_desc *transform = ecs_world_component(world, components[0]);
    const ecs_component_desc *movement = ecs_world_component(world, components[1]);
    const ecs_component_desc *input = ecs_world_component(world, components[2]);
    if (!transform || !movement || !input) {
        abort();
    }
    for (int frame = 0; frame < frame_count; frame++) {
        ecs_query query;
        if (ecs_query_begin(world, &query, components, 3) != ECS_OK) {
            abort();
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity) > 0) {
            GameTransform *position = (GameTransform *)native_column(transform, entity);
            const GameMovement *speed = (const GameMovement *)native_column(movement, entity);
            const GameInput *direction = (const GameInput *)native_column(input, entity);
            position->x += direction->move.x * speed->speed * (float)DELTA_SECONDS;
            position->y += direction->move.y * speed->speed * (float)DELTA_SECONDS;
        }
        ecs_query_end(&query);
    }
}

static void run_native(ecs_entity *entities, int entity_count, int frame_count, ecs_world *world)
{
    for (int frame = 0; frame < frame_count; frame++) {
        for (int i = 0; i < entity_count; i++) {
            GameTransform *transform = GAME_GameTransform_get(world, entities[i]);
            const GameMovement *movement = GAME_GameMovement_get_const(world, entities[i]);
            const GameInput *input = GAME_GameInput_get_const(world, entities[i]);
            transform->x += input->move.x * movement->speed * (float)DELTA_SECONDS;
            transform->y += input->move.y * movement->speed * (float)DELTA_SECONDS;
        }
    }
}

int main(int argc, char **argv)
{
    int entity_count = argc > 1 ? atoi(argv[1]) : DEFAULT_ENTITIES;
    int frame_count = argc > 2 ? atoi(argv[2]) : DEFAULT_FRAMES;
    if (entity_count <= 0 || frame_count <= 0) {
        fprintf(stderr, "usage: system_benchmark [entities] [frames]\n");
        return 1;
    }
    registry_validate();
    char *json = text_file_read("content/systems/MovementSystem.json");
    behavior_asset *definition = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    ecs_world *world = ecs_world_new();
    if (!definition || !world ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) != ECS_OK ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) != ECS_OK ||
        ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL) != ECS_OK) {
        behavior_asset_free(definition);
        ecs_world_free(world);
        fprintf(stderr, "could not initialize benchmark world\n");
        return 1;
    }
    game_system *system = game_system_new(world, definition);
    definition = NULL;
    ecs_entity *entities = (ecs_entity *)malloc((size_t)entity_count * sizeof *entities);
    if (!system || !entities) {
        game_system_free(system);
        if (!system) {
            behavior_asset_free(definition);
        }
        ecs_world_free(world);
        free(entities);
        return 1;
    }
    for (int i = 0; i < entity_count; i++) {
        entities[i] = ecs_world_create_entity(world);
        if (GAME_GameTransform_add(world, entities[i], 0) != ECS_OK ||
            GAME_GameMovement_add(world, entities[i], 0) != ECS_OK ||
            GAME_GameInput_add(world, entities[i], 0) != ECS_OK ||
            ecs_world_apply_commands(world) != ECS_OK) {
            fprintf(stderr, "could not create benchmark entity\n");
            free(entities);
            game_system_free(system);
            behavior_asset_free(definition);
            ecs_world_free(world);
            return 1;
        }
        GAME_GameMovement_get(world, entities[i])->speed = BENCHMARK_SPEED;
        GAME_GameInput_get(world, entities[i])->move = (CF_V2){ 1.0f, 0.0f };
    }
    if (!game_system_uses_typed_bytecode(system) || !game_system_uses_sljit(system)) {
        fprintf(stderr, "movement graph did not compile to typed bytecode and SLJIT\n");
        free(entities);
        game_system_free(system);
        behavior_asset_free(definition);
        ecs_world_free(world);
        return 1;
    }
    struct timespec start;
    struct timespec end;
    flow_error error = { 0 };
    double generic_ns;
    double typed_ns;
    double sljit_ns;
    double native_query_ns;
    double native_ns;
    int result = 1;

    reset_transforms(entities, entity_count, world);
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (run_system(system, frame_count, GAME_SYSTEM_EXEC_GENERIC_BYTECODE, &error) != 0) {
        fprintf(stderr, "generic system failed: %s\n", error.message ? error.message : "unknown");
        goto cleanup;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    generic_ns = elapsed_ns(start, end);
    if (check_positions(entities, entity_count, frame_count, world) != 0) {
        fprintf(stderr, "generic system produced the wrong position\n");
        goto cleanup;
    }

    reset_transforms(entities, entity_count, world);
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (run_system(system, frame_count, GAME_SYSTEM_EXEC_TYPED_BYTECODE, &error) != 0) {
        fprintf(stderr, "typed system failed: %s\n", error.message ? error.message : "unknown");
        goto cleanup;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    typed_ns = elapsed_ns(start, end);
    if (check_positions(entities, entity_count, frame_count, world) != 0) {
        fprintf(stderr, "typed system produced the wrong position\n");
        goto cleanup;
    }

    reset_transforms(entities, entity_count, world);
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (run_system(system, frame_count, GAME_SYSTEM_EXEC_SJLIT, &error) != 0) {
        fprintf(stderr, "SLJIT system failed: %s\n", error.message ? error.message : "unknown");
        goto cleanup;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    sljit_ns = elapsed_ns(start, end);
    if (check_positions(entities, entity_count, frame_count, world) != 0) {
        fprintf(stderr, "SLJIT system produced the wrong position\n");
        goto cleanup;
    }

    reset_transforms(entities, entity_count, world);
    clock_gettime(CLOCK_MONOTONIC, &start);
    run_native_query(world, frame_count);
    clock_gettime(CLOCK_MONOTONIC, &end);
    native_query_ns = elapsed_ns(start, end);
    if (check_positions(entities, entity_count, frame_count, world) != 0) {
        fprintf(stderr, "native query produced the wrong position\n");
        goto cleanup;
    }

    reset_transforms(entities, entity_count, world);
    clock_gettime(CLOCK_MONOTONIC, &start);
    run_native(entities, entity_count, frame_count, world);
    clock_gettime(CLOCK_MONOTONIC, &end);
    native_ns = elapsed_ns(start, end);
    if (check_positions(entities, entity_count, frame_count, world) != 0) {
        fprintf(stderr, "native loop produced the wrong position\n");
        goto cleanup;
    }

    printf("entities=%d frames=%d\n", entity_count, frame_count);
    printf("system_backend=%s\n", system_backend_name(system));
    printf("generic_bytecode_ns_per_entity_frame=%.2f\n", generic_ns / entity_count / frame_count);
    printf("typed_bytecode_ns_per_entity_frame=%.2f\n", typed_ns / entity_count / frame_count);
    printf("sljit_ns_per_entity_frame=%.2f\n", sljit_ns / entity_count / frame_count);
    printf("native_query_ns_per_entity_frame=%.2f\n", native_query_ns / entity_count / frame_count);
    printf("native_ns_per_entity_frame=%.2f\n", native_ns / entity_count / frame_count);
    result = 0;

cleanup:
    free(entities);
    game_system_free(system);
    behavior_asset_free(definition);
    ecs_world_free(world);
    return result;
}
