#include "../core/array.h"
#include "../core/conv_nodes.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../core/text_file.h"
#include "../engine/engine.h"
#include "../game/behavior.h"
#include "../game/generated.h"
#include "../game/live.h"
#include "../game/prototype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int attach_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user)
{
    (void)world;
    return game_behavior_attach((game_behavior_world *)user, entity, asset_name);
}

static int check(int condition, const char *message)
{
    if (condition) {
        return 0;
    }
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int remove_edge_between(graph *g, const char *from_name, const char *to_name)
{
    node *from = NULL;
    node *to = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *candidate = g->nodes[i];
        if (strcmp(candidate->desc->name, from_name) == 0) {
            from = candidate;
        }
        if (strcmp(candidate->desc->name, to_name) == 0) {
            to = candidate;
        }
    }
    if (!from || !to) {
        return -1;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        edge current = g->edges[i];
        if (current.from_id != from->id || current.to_id != to->id) {
            continue;
        }
        int from_pin = -1;
        int to_pin = -1;
        int from_output = 0;
        int to_output = 0;
        if (!graph_pin_by_id(from, current.from_pin_id, &from_output, &from_pin) ||
            !from_output ||
            !graph_pin_by_id(to, current.to_pin_id, &to_output, &to_pin) || to_output) {
            return -1;
        }
        return graph_remove_edge(g, from, from_pin, to, to_pin);
    }
    return -1;
}

int main(void)
{
    registry_validate();
    int failures = 0;
    const type_desc *cf_v2 = registry_type_by_name("CF_V2");
    pin_spec game_position = { "Position", PIN_STRUCT, "GameTransform", PIN_DATA };
    pin_spec cute_position = { "Position", PIN_STRUCT, "CF_V2", PIN_DATA };
    failures += check(
        cf_v2 && cf_v2->field_count == 2 && strcmp(cf_v2->fields[0].name, "x") == 0,
        "CF_V2 is registered as an external reflected type"
    );
    failures += check(pin_types_compatible(&game_position, &cute_position), "GameTransform and CF_V2 share an implicit type alias");
    failures += check(
        conversion_for_pins(&game_position, &cute_position) ==
            registry_node_by_name("game_transform_to_cf"),
        "GameTransform retains an explicit CF_V2 conversion for legacy graphs"
    );
    char *json = text_file_read("content/behaviors/InputBehavior.json");
    blueprint *working = json ? blueprint_load_json(json) : NULL;
    free(json);
    char *original_json = working ? blueprint_save_json(working) : NULL;
    failures += check(working != NULL, "InputBehavior loads from its content file");
    int conversion_edges = 0;
    if (working) {
        for (int i = 0; i < acount(working->graph->edges); i++) {
            conversion_edges += working->graph->edges[i].conversion != NULL;
        }
    }
    failures += check(conversion_edges == 2, "InputBehavior stores its conversions on data edges");
    char *roundtrip_json = working ? blueprint_save_json(working) : NULL;
    blueprint *roundtrip = roundtrip_json ? blueprint_load_json(roundtrip_json) : NULL;
    int roundtrip_conversions = 0;
    if (roundtrip) {
        for (int i = 0; i < acount(roundtrip->graph->edges); i++) {
            roundtrip_conversions += roundtrip->graph->edges[i].conversion != NULL;
        }
    }
    failures += check(roundtrip && roundtrip_conversions == 2, "edge conversions survive blueprint save and load");
    free(roundtrip_json);
    blueprint_free(roundtrip);

    game_live_definition *definition = working
                                         ? game_live_definition_new("InputBehavior", working)
                                         : NULL;
    if (!definition) {
        free(original_json);
        blueprint_free(working);
        return failures +
               check(0, "InputBehavior graph has a serializable applied definition");
    }

    ecs_world *ecs = ecs_world_new();
    failures += check(
        ecs && ecs_world_register_component(ecs, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK,
        "GameTransform registers in the headless test world"
    );
    game_live_world *live = ecs ? game_live_world_new(ecs) : NULL;
    int definition_registered = live &&
                                game_live_add_definition(live, definition) == 0;
    failures += check(definition_registered, "InputBehavior registers before prototype spawning");
    if (!definition_registered ||
        !game_behavior_asset_registered(live->behaviors, "InputBehavior")) {
        game_live_world_free(live);
        if (!definition_registered) {
            game_live_definition_free(definition);
        }
        ecs_world_free(ecs);
        free(original_json);
        return failures + check(0, "InputBehavior is registered as a live asset");
    }

    static const char
        INPUT_ENTITY_JSON[] = "{\"name\":\"InputEntity\",\"components\":{"
                              "\"GameTransform\":{\"x\":0,\"y\":0}},"
                              "\"behaviors\":[\"InputBehavior\"]}";
    const game_prototype_registry registry = {
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        NULL,
        NULL,
    };
    char error[256] = { 0 };
    game_prototype *prototype = game_prototype_load(INPUT_ENTITY_JSON, &registry, error, sizeof error);
    failures += check(prototype != NULL, "an input entity prototype loads with InputBehavior");

    ecs_entity entity = { 0 };
    game_prototype_spawn_hooks hooks = { attach_behavior, live->behaviors };
    if (prototype) {
        failures += check(
            game_prototype_spawn(prototype, ecs, &hooks, &entity, error, sizeof error) ==
                ECS_OK,
            "an input entity spawns through the behavior attach hook"
        );
    }
    game_behavior_attachment_info attachment = { 0 };
    failures += check(
        prototype &&
            game_behavior_attachment_count_for_entity(live->behaviors, entity) == 1 &&
            game_behavior_attachment_info_at(live->behaviors, entity, 0, &attachment) == 0 &&
            strcmp(attachment.asset_name, "InputBehavior") == 0 &&
            attachment.definition == definition->applied,
        "the input entity has the applied InputBehavior attached"
    );

    ecs_entity other = { 0 };
    if (prototype) {
        failures += check(
            game_prototype_spawn(prototype, ecs, &hooks, &other, error, sizeof error) == ECS_OK,
            "a second input entity spawns from the same data"
        );
    }
    GameTransform *transform = ecs ? GAME_GameTransform_get(ecs, entity) : NULL;
    GameTransform *other_transform = ecs ? GAME_GameTransform_get(ecs, other) : NULL;
    engine *runtime = ecs ? engine_new(ecs, live) : NULL;
    failures += check(
        transform && runtime && engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT) == 0,
        "the scene input route has viewport focus"
    );
    flow_error flow = { 0 };
    int right_count = transform && other_transform && runtime
                        ? engine_dispatch_input(runtime, "MoveRight", 1.0, 1, &flow)
                        : -1;
    failures += check(
        transform && other_transform && runtime && right_count == 2 &&
            transform->x == 1.0f && other_transform->x == 1.0f,
        "generic Input reaches every entity with the input behavior"
    );
    float x = transform ? transform->x : 0.0f;
    failures += check(
        transform && runtime &&
            engine_dispatch_input(runtime, "MoveRight", 0.0, 0, &flow) == 2 &&
            transform->x == x,
        "a MoveRight release does not continue movement"
    );
    failures += check(
        transform && runtime &&
            engine_dispatch_input(runtime, "MoveLeft", -1.0, 1, &flow) == 2 &&
            transform->x == 0.0f,
        "MoveLeft applies its signed value"
    );
    failures += check(
        transform && runtime &&
            engine_dispatch_input(runtime, "MoveUp", 1.0, 1, &flow) == 2 &&
            transform->y == 1.0f,
        "MoveUp updates the input entity transform"
    );
    failures += check(
        transform && runtime &&
            engine_dispatch_input(runtime, "MoveDown", -1.0, 1, &flow) == 2 &&
            transform->y == 0.0f,
        "MoveDown applies its signed value"
    );

    graph *edited = definition->working->graph;
    int removed_exec = edited &&
                       remove_edge_between(edited, "Event", "Get_GameTransform") == 0;
    failures += check(removed_exec, "removing an event exec edge from the working behavior succeeds");
    if (removed_exec) {
        game_live_mark_dirty(definition);
        char message[256] = { 0 };
        int applied = game_live_apply(live, definition, message, sizeof message) == 0;
        failures += check(applied, "a disconnected executable node remains applicable");
        if (applied) {
            failures += check(
                engine_dispatch_input(runtime, "MoveRight", 1.0, 1, &flow) == 2 &&
                    transform->x == 0.0f && other_transform->x == 0.0f,
                "a disconnected event exec edge stops the live behavior"
            );
        }
    }
    blueprint *restored = original_json ? blueprint_load_json(original_json) : NULL;
    int restored_ok = restored && game_live_sync_working(definition, restored) == 0;
    if (restored) {
        blueprint_free(restored);
    }
    if (restored_ok) {
        char message[256] = { 0 };
        restored_ok = game_live_apply(live, definition, message, sizeof message) == 0;
    }
    failures += check(restored_ok, "the live behavior restores after the disconnected-edge check");

    edited = definition->working->graph;
    int removed = edited &&
                  remove_edge_between(edited, "Get_GameTransform", "Add") == 0;
    failures += check(removed, "removing a data edge from the working behavior succeeds");
    if (removed) {
        game_live_mark_dirty(definition);
        char message[256] = { 0 };
        int applied = game_live_apply(live, definition, message, sizeof message) == 0;
        failures += check(applied, "the edited behavior snapshot applies");
        if (applied) {
            failures += check(
                engine_dispatch_input(runtime, "MoveRight", 1.0, 1, &flow) == 2 &&
                    transform->x == 0.0f && other_transform->x == 0.0f,
                "an input event remains safe after deleting a struct data edge"
            );
        }
    }

    engine_free(runtime);
    game_prototype_free(prototype);
    game_live_world_free(live);
    ecs_world_free(ecs);
    free(original_json);
    return failures;
}
