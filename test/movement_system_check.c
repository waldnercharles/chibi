#include "../core/jit_abi.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../core/text_file.h"
#include "../game/generated.h"
#include "../game/system.h"

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

static int add_player_components(ecs_world *world, ecs_entity entity)
{
    return GAME_GameTransform_add(world, entity, 0) == ECS_OK &&
           GAME_GameMovement_add(world, entity, 0) == ECS_OK &&
           GAME_GameInput_add(world, entity, 0) == ECS_OK &&
           ecs_world_apply_commands(world) == ECS_OK;
}

int main(void)
{
    registry_validate();
    int failures = 0;
    char *json = text_file_read("content/systems/MovementSystem.json");
    blueprint *blueprint = json ? blueprint_load_json(json) : NULL;
    free(json);
    failures += check(blueprint != NULL, graph_last_error());
    if (!blueprint) {
        return failures;
    }

    ecs_world *world = ecs_world_new();
    failures += check(
        world &&
            ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK &&
            ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL) == ECS_OK &&
            ecs_world_register_component(world, &GAME_COMPONENT_GameInput, NULL) == ECS_OK &&
            ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL) == ECS_OK,
        "movement components register in the ECS world"
    );
    game_system *system = world ? game_system_new(world, blueprint) : NULL;
    blueprint = NULL;
    failures += check(system != NULL, "MovementSystem creates a query-driven runtime");
    failures += check(system && game_system_query_count(system) == 3, "the system query is derived from its component graph");
    ecs_query_expr *health_filter = ecs_query_expr_has(GAME_COMPONENT_GameHealth.id);
    failures += check(system && game_system_set_query(system, health_filter) == 0, "the system accepts an authored query filter");
    ecs_query_expr_free(health_filter);
    const struct blueprint *compiled_definition = game_system_blueprint(system);
    const graph *compiled_graph = compiled_definition ? compiled_definition->graph
                                                      : NULL;
    int compact = compiled_graph && compiled_graph->dataflow &&
                  acount(compiled_graph->nodes) == 8;
    if (compact) {
        for (int i = 0; i < acount(compiled_graph->nodes); i++) {
            const node *n = compiled_graph->nodes[i];
            if (!n->desc || !n->desc->name) {
                continue;
            }
            int get = strncmp(n->desc->name, "Get_", 4) == 0;
            int set = strncmp(n->desc->name, "Set_", 4) == 0;
            if (!get && !set) {
                continue;
            }
            int input_exec = 0;
            int output_exec = 0;
            for (int pin = 0; pin < n->input_count; pin++) {
                input_exec |= n->input_pins[pin].kind == PIN_EXEC;
            }
            for (int pin = 0; pin < n->output_count; pin++) {
                output_exec |= n->output_pins[pin].kind == PIN_EXEC;
            }
            compact &= get ? !input_exec && !output_exec : input_exec && output_exec;
        }
    }
    failures += check(compact, "the movement graph uses data reads and ordered component writes");
    failures += check(
        system && game_system_uses_bytecode(system),
        "the movement graph compiles to the data-driven bytecode backend"
    );
    failures += check(
        system && game_system_uses_typed_bytecode(system),
        "the movement graph uses the reflected typed bytecode fast path"
    );
    failures += check(system && game_system_uses_sljit(system), "the movement graph compiles to the SLJIT backend");
    failures += check(system && game_system_uses_sljit_registers(system), "the movement graph uses the SLJIT register path");
    const node_desc *scale = registry_node_by_name("Multiply");
    const node_desc *add = registry_node_by_name("Add");
    failures += check(
        scale && add && scale->jit_kind == NODE_JIT_MUL && add->jit_kind == NODE_JIT_ADD &&
            (scale->jit_flags & NODE_JIT_FLAG_FAST_MATH) &&
            (add->jit_flags & NODE_JIT_FLAG_FAST_MATH),
        "reflection carries the SLJIT lowering metadata"
    );
    const graph_ir *ir = system ? game_system_graph_ir(system) : NULL;
    const graph_program *program = system ? game_system_program(system) : NULL;
    const char *ir_reason = NULL;
    failures += check(
        ir && graph_ir_validate(ir, &ir_reason) == 0,
        ir_reason ? ir_reason : "the movement graph lowers to valid graph IR"
    );
    failures += check(
        program && graph_program_ir(program) == ir && graph_program_sljit(program) != NULL,
        "System and backend share one graph program storage"
    );
    uint64_t ir_hash = ir ? graph_ir_hash(ir) : 0;
    failures += check(ir_hash != 0 && ir_hash == graph_ir_hash(ir), "the graph IR has a stable cache key");
    failures += check(GRAPH_JIT_ABI_VERSION == 1u, "the JIT ABI version is available to the runtime");

    ecs_entity player = world ? ecs_world_create_entity(world) : (ecs_entity){ 0 };
    failures += check(world && add_player_components(world, player), "the player receives the movement components");
    GameTransform *transform = world ? GAME_GameTransform_get(world, player) : NULL;
    GameMovement *movement = world ? GAME_GameMovement_get(world, player) : NULL;
    GameInput *input = world ? GAME_GameInput_get(world, player) : NULL;
    failures += check(
        world && GAME_GameHealth_add(world, player, 0) == ECS_OK &&
            ecs_world_apply_commands(world) == ECS_OK,
        "the filtered entity receives the authored query term"
    );
    if (transform && movement && input) {
        movement->speed = 100.0f;
        input->move = (CF_V2){ 1.0f, -0.5f };
    }
    flow_error error = { 0 };
    int result = system ? game_system_tick_mode(system, 1.0, 0.5, &error, GAME_SYSTEM_EXEC_SJLIT)
                        : FLOW_ERR_STATE;
    failures += check(result == FLOW_OK, error.message ? error.message : "the movement system ticks");
    failures += check(
        transform && transform->x == 50.0f && transform->y == -25.0f,
        "the graph moves a matching entity from input, speed, and delta"
    );
    failures += check(system && game_system_match_count(system) == 1, "the system reports its matching entities");

    ecs_entity other = world ? ecs_world_create_entity(world) : (ecs_entity){ 0 };
    if (world) {
        GAME_GameTransform_add(world, other, 0);
        ecs_world_apply_commands(world);
    }
    GameTransform *other_transform = world ? GAME_GameTransform_get(world, other)
                                           : NULL;
    result = system ? game_system_tick(system, 2.0, 0.5, &error) : FLOW_ERR_STATE;
    failures += check(result == FLOW_OK, "the system keeps running with a non-matching entity");
    failures += check(
        other_transform && other_transform->x == 0.0f && other_transform->y == 0.0f,
        "the query excludes entities without movement input and configuration"
    );

    failures += check(system && game_system_match_count(system) == 1, "the authored filter excludes otherwise matching entities");

    game_system_free(system);
    ecs_world_free(world);
    blueprint_free(blueprint);
    return failures;
}
