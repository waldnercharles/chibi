#include "assets/code_source.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/reactive.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/instance.h"
#include "runtime/behaviors/reactive.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int, const char *);

static behavior_asset *parse_asset(const char *text)
{
    code_source *source = code_source_parse(text);
    behavior_asset *asset = code_source_compile_behavior(source);
    if (!asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            fprintf(
                stderr,
                "Reactive %d:%d: %s\n",
                source->diagnostics[i].line,
                source->diagnostics[i].column,
                source->diagnostics[i].message
            );
        }
    }
    code_source_free(source);
    return asset;
}

static behavior_asset *compile(const char *text)
{
    behavior_asset *source = parse_asset(text);
    if (!source) {
        return NULL;
    }
    char error[256] = { 0 };
    behavior_asset *asset = behavior_asset_link_libraries(source, NULL, 0, error, sizeof error);
    if (!asset) {
        fprintf(stderr, "Reactive link: %s\n", error);
    }
    behavior_asset_free(source);
    return asset;
}

static void rejected(const char *text, const char *reason, const char *message)
{
    code_source *source = code_source_parse(text);
    bool diagnosed = false;
    for (int i = 0; i < acount(source->diagnostics); i++) {
        diagnosed |= strstr(source->diagnostics[i].message, reason) != NULL;
    }
    test_check(!source->asset && diagnosed, message);
    code_source_free(source);
}

static ecs_world *world_new()
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        test_check(
            ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK,
            "reactive fixture registers reflected components"
        );
    }
    return world;
}

static ecs_entity binding(ecs_world *world, bool pressed)
{
    ecs_entity entity = ecs_world_create_entity(world);
    GameActionBinding value = { .pressed = pressed };
    ecs_world_record_add(world, entity, GAME_COMPONENT_GameActionBinding.id, &value, 0);
    test_check(ecs_world_apply_commands(world) == ECS_OK, "binding fixture applies its component");
    return entity;
}

static ecs_entity emitter(ecs_world *world, ecs_entity parent)
{
    ecs_entity entity = ecs_world_create_entity(world);
    GameParticleEmitter value = { .parent = ecs_entity_to_u64(parent) };
    ecs_world_record_add(world, entity, GAME_COMPONENT_GameParticleEmitter.id, &value, 0);
    test_check(ecs_world_apply_commands(world) == ECS_OK, "emitter fixture applies its component");
    return entity;
}

static behavior_instance *instance_new(behavior_asset *asset, ecs_world *world, ecs_entity entity)
{
    pin_value owner = { .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(entity) };
    behavior_instance *instance = behavior_instance_create(asset, &owner, NULL, NULL);
    const compile_diagnostic *diagnostic = behavior_program_diagnostic(asset->program);
    if (diagnostic) {
        fprintf(stderr, "Reactive program %s: %s\n", asset->name, diagnostic->message);
    }
    for (int i = 0; i < acount(asset->reactives); i++) {
        const graph *g = behavior_asset_find_graph(asset, asset->reactives[i].graph_id);
        const node *entry = graph_node_by_id(g, asset->reactives[i].event_id);
        const behavior_graph_program *compiled = behavior_program_graph(asset->program, g);
        const behavior_function_diagnostic
            *excluded = compiled
                          ? behavior_program_function_diagnostic(compiled, entry)
                          : NULL;
        if (excluded) {
            fprintf(
                stderr,
                "Reactive compiled body %s: %s\n",
                asset->name,
                excluded->reason
            );
        }
    }
    test_check(
        instance && behavior_instance_bind_service(instance, "ecs.world", world) == 0,
        "reactive instance binds its entity world"
    );
    return instance;
}

static game_reactive_stats maintain(game_reactive *reactive, behavior_instance *instance)
{
    flow_error error = { 0 };
    game_reactive_begin(reactive);
    int result = game_reactive_apply(reactive, instance, &error);
    if (error.message) {
        fprintf(stderr, "Reactive maintenance: %s\n", error.message);
    }
    test_check(result >= 0 && !error.message, "reactive maintenance executes without a flow failure");
    game_reactive_end(reactive);
    return game_reactive_last_update(reactive);
}

static void source_contracts()
{
    behavior_asset *asset = parse_asset(
        "behavior Maintained { bind self.ParticleEmitter.Active = "
        "self.ParticleEmitter.Parent.ActionBinding.Pressed == true; }"
    );
    test_check(asset && acount(asset->reactives) == 1, "pure comparison bind lowers to a native reactive root");
    if (asset) {
        av_doc *native = av_doc_new();
        behavior_asset *copy = behavior_asset_from_value(
            behavior_asset_to_value(native, asset)
        );
        av_doc_free(native);
        test_check(
            copy && acount(copy->reactives) == 1 &&
                copy->reactives[0].value_node_id == asset->reactives[0].value_node_id,
            "native serialization retains reactive value and target identities"
        );
        char *text = copy ? code_source_format(copy) : NULL;
        test_check(text && strstr(text, "bind ") && !strstr(text, "__Bind"), "reactive native graphs print readable bind source without hidden callbacks");
        code_source *roundtrip = text ? code_source_parse(text) : NULL;
        test_check(roundtrip && roundtrip->asset && acount(roundtrip->asset->reactives) == 1, "formatted maintained source reparses its executable reactive contract");
        code_source_free(roundtrip);
        free(text);
        behavior_asset_free(copy);
    }
    behavior_asset_free(asset);
    asset = parse_asset(
        "behavior Fallback { bind self.ParticleEmitter.Active = "
        "fallback(self.ActionBinding.Pressed, binding: "
        "&component(\"GAME.GameActionBinding\", \"disabled\"), default: true); "
        "}"
    );
    bool fallback = false;
    if (asset) {
        node *set = graph_node_by_id(asset->graph, asset->reactives[0].statement_node_id);
        for (int i = 0; i < set->input_count; i++) {
            fallback |= set->input_pins[i].type == PIN_BOOL &&
                        set->input_pins[i].binding &&
                        set->input_pins[i].default_value.b;
        }
    }
    test_check(fallback, "binding capture preserves both reflected fallback binding and literal default");
    behavior_asset_free(asset);
    rejected(
        "behavior Duplicate { bind self.ParticleEmitter.Active = true; "
        "bind self.ParticleEmitter.Active = false; }",
        "same component field",
        "competing declarations cannot own the same component field"
    );
    rejected(
        "behavior Cycle { bind self.ActionBinding.Pressed = "
        "self.ActionBinding.Disabled; "
        "bind self.ActionBinding.Disabled = self.ActionBinding.Pressed; }",
        "dependency cycle",
        "component-field binding cycles are rejected before publication"
    );
    rejected(
        "behavior RandomAlias { value sample = engine[\"Random Real\"](); "
        "bind self.ParticleEmitter.Rate = sample; }",
        "deterministic",
        "a value alias cannot hide nondeterminism from binding validation"
    );
    rejected(
        "behavior RandomFunction { pure function sample() -> (Result: float) { "
        "value random = engine[\"Random Real\"](); result Result: float = "
        "random; } "
        "bind self.ParticleEmitter.Rate = sample().Result; }",
        "deterministic",
        "a pure function declaration cannot hide randomness from a binding"
    );
}

static void maintained_relationship()
{
    behavior_asset *asset = compile(
        "behavior Maintained { bind self.ParticleEmitter.Active = "
        "self.ParticleEmitter.Parent.ActionBinding.Pressed; }"
    );
    test_check(asset != NULL, "cross-entity maintained relationship compiles");
    if (!asset) {
        return;
    }
    ecs_world *world = world_new();
    ecs_entity parent = binding(world, true), other = binding(world, false);
    ecs_entity child = emitter(world, parent);
    behavior_instance *instance = instance_new(asset, world, child);
    game_reactive *reactive = game_reactive_new(world);
    if (instance) {
        game_reactive_stats stats = maintain(reactive, instance);
        test_check(
            GAME_GameParticleEmitter_get(world, child)->active && stats.writes == 1,
            "a maintained relationship synchronizes before its first event"
        );
        stats = maintain(reactive, instance);
        test_check(stats.writes == 0, "unchanged maintained values skip component assignments");
        GAME_GameActionBinding_get(world, parent)->pressed = false;
        maintain(reactive, instance);
        test_check(
            !GAME_GameParticleEmitter_get(world, child)->active,
            "safe points observe direct native writes to a binding dependency"
        );
        GAME_GameParticleEmitter_get(world, child)->parent = ecs_entity_to_u64(other);
        GAME_GameActionBinding_get(world, other)->pressed = true;
        maintain(reactive, instance);
        test_check(
            GAME_GameParticleEmitter_get(world, child)->active,
            "changing the parent handle retargets the maintained source"
        );
        ecs_world_record_remove(world, other, GAME_COMPONENT_GameActionBinding.id, 0);
        ecs_world_apply_commands(world);
        maintain(reactive, instance);
        test_check(!GAME_GameParticleEmitter_get(world, child)->active, "a missing source component clears the maintained target to its zero value");
        GameActionBinding restored = { .pressed = true };
        ecs_world_record_add(world, other, GAME_COMPONENT_GameActionBinding.id, &restored, 0);
        ecs_world_apply_commands(world);
        maintain(reactive, instance);
        test_check(GAME_GameParticleEmitter_get(world, child)->active, "restoring a source component reactivates the relationship");
        ecs_world_destroy_entity(world, other);
        maintain(reactive, instance);
        test_check(!GAME_GameParticleEmitter_get(world, child)->active, "a destroyed parent cannot leave a stale pressed value");
    }
    behavior_instance_destroy(instance);
    game_reactive_begin(reactive);
    game_reactive_end(reactive);
    test_check(game_reactive_last_update(reactive).events == 0, "an empty safe point prunes removed instances without dispatching callbacks");
    game_reactive_free(reactive);
    ecs_world_free(world);
    behavior_asset_free(asset);
}

static void dependency_order()
{
    behavior_asset *asset = compile(
        "behavior Ordered { "
        "bind self.ActionBinding.Disabled = self.ParticleEmitter.Active; "
        "bind self.ParticleEmitter.Active = self.ActionBinding.Pressed; }"
    );
    test_check(asset != NULL, "an acyclic maintained dependency chain compiles");
    if (!asset) {
        return;
    }
    ecs_world *world = world_new();
    ecs_entity entity = binding(world, true);
    GameParticleEmitter emitter_value = { 0 };
    ecs_world_record_add(world, entity, GAME_COMPONENT_GameParticleEmitter.id, &emitter_value, 0);
    ecs_world_apply_commands(world);
    behavior_instance *instance = instance_new(asset, world, entity);
    game_reactive *reactive = game_reactive_new(world);
    if (instance) {
        maintain(reactive, instance);
        test_check(
            GAME_GameActionBinding_get(world, entity)->disabled &&
                GAME_GameParticleEmitter_get(world, entity)->active,
            "dependency order synchronizes reverse-declared bindings in one "
            "safe point"
        );
        GAME_GameActionBinding_get(world, entity)->pressed = false;
        maintain(reactive, instance);
        test_check(
            !GAME_GameActionBinding_get(world, entity)->disabled &&
                !GAME_GameParticleEmitter_get(world, entity)->active,
            "dependency order propagates a native change through the entire "
            "chain"
        );
    }
    behavior_instance_destroy(instance);
    game_reactive_free(reactive);
    ecs_world_free(world);
    behavior_asset_free(asset);
}

static void maintained_vector()
{
    behavior_asset *asset = compile("behavior Vector { bind self.Transform = self.Velocity; }");
    test_check(asset != NULL, "a whole vector component can be maintained without scalar casts");
    if (!asset) {
        return;
    }
    ecs_world *world = world_new();
    ecs_entity entity = ecs_world_create_entity(world);
    GameTransform position = { .x = -1, .y = -2 };
    GameVelocity velocity = { .x = 3, .y = 4 };
    ecs_world_record_add(world, entity, GAME_COMPONENT_GameTransform.id, &position, 0);
    ecs_world_record_add(world, entity, GAME_COMPONENT_GameVelocity.id, &velocity, 0);
    ecs_world_apply_commands(world);
    behavior_instance *instance = instance_new(asset, world, entity);
    game_reactive *reactive = game_reactive_new(world);
    if (instance) {
        game_reactive_stats stats = maintain(reactive, instance);
        const GameTransform *result = GAME_GameTransform_get_const(world, entity);
        test_check(result->x == 3 && result->y == 4 && stats.writes == 1, "initial vector synchronization copies every lane and counts one assignment");
        stats = maintain(reactive, instance);
        test_check(stats.writes == 0, "an unchanged vector skips its component assignment");
        GAME_GameVelocity_get(world, entity)->y = 7;
        stats = maintain(reactive, instance);
        result = GAME_GameTransform_get_const(world, entity);
        test_check(result->x == 3 && result->y == 7 && stats.writes == 1, "a change in one vector lane propagates while retaining the other lane");
    }
    behavior_instance_destroy(instance);
    game_reactive_free(reactive);
    ecs_world_free(world);
    behavior_asset_free(asset);
}

static void changed_payload()
{
    behavior_asset *asset = compile(
        "behavior Watch { state changes := 0; "
        "on self.ParticleEmitter.Parent.ActionBinding.PressedChanged(pressed) "
        "{ "
        "changes += 1; self.ParticleEmitter.Active = pressed; } }"
    );
    test_check(asset != NULL, "qualified pressed-change events compile with an inferred bool payload");
    if (!asset) {
        return;
    }
    ecs_world *world = world_new();
    ecs_entity parent = binding(world, false), child = emitter(world, parent);
    behavior_instance *instance = instance_new(asset, world, child);
    game_reactive *reactive = game_reactive_new(world);
    if (instance) {
        maintain(reactive, instance);
        test_check(instance->variables[0].d == 0, "initial subscription setup emits no synthetic edge");
        GAME_GameActionBinding_get(world, parent)->pressed = true;
        maintain(reactive, instance);
        test_check(
            instance->variables[0].d == 1 &&
                GAME_GameParticleEmitter_get(world, child)->active,
            "native pressed transitions deliver true as the event payload"
        );
        maintain(reactive, instance);
        test_check(instance->variables[0].d == 1, "unchanged pressed values do not repeat their event");
        flow_error error = { 0 };
        pin_value released = { .type = PIN_BOOL, .type_name = "bool", .b = false };
        pin_value pressed = { .type = PIN_BOOL, .type_name = "bool", .b = true };
        int release_result = game_reactive_changed(
            reactive,
            instance,
            parent,
            GAME_COMPONENT_GameActionBinding.id,
            offsetof(GameActionBinding, pressed),
            released,
            &error
        );
        int press_result = game_reactive_changed(
            reactive,
            instance,
            parent,
            GAME_COMPONENT_GameActionBinding.id,
            offsetof(GameActionBinding, pressed),
            pressed,
            &error
        );
        test_check(
            release_result >= 0 && press_result >= 0 && !error.message &&
                instance->variables[0].d == 3 &&
                GAME_GameParticleEmitter_get(world, child)->active,
            "queued release and repress edges each deliver their captured bool "
            "payload"
        );
        maintain(reactive, instance);
        test_check(instance->variables[0].d == 3, "sampling after queued edges does not duplicate the final transition");
        GAME_GameActionBinding_get(world, parent)->pressed = false;
        maintain(reactive, instance);
        test_check(
            instance->variables[0].d == 4 &&
                !GAME_GameParticleEmitter_get(world, child)->active,
            "native release transitions deliver false as the event payload"
        );
    }
    behavior_instance_destroy(instance);
    game_reactive_free(reactive);
    ecs_world_free(world);
    behavior_asset_free(asset);
}

static void competing_managers()
{
    const char *first_sources[] = {
        "behavior First { bind self.ParticleEmitter.Active = true; }",
        "behavior First { bind self.Transform = self.Velocity; }",
    };
    const char *second_sources[] = {
        "behavior Second { bind self.ParticleEmitter.Active = false; }",
        "behavior Second { bind self.Transform.y = 9; }",
    };
    for (int i = 0; i < 2; i++) {
        behavior_asset *first_asset = compile(first_sources[i]);
        behavior_asset *second_asset = compile(second_sources[i]);
        test_check(first_asset && second_asset, "separate competing declarations each compile in isolation");
        if (!first_asset || !second_asset) {
            behavior_asset_free(first_asset);
            behavior_asset_free(second_asset);
            continue;
        }
        ecs_world *world = world_new();
        ecs_entity entity = emitter(world, (ecs_entity){ 0 });
        GameTransform position = { 0 };
        GameVelocity velocity = { .x = 2, .y = 3 };
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameTransform.id, &position, 0);
        ecs_world_record_add(world, entity, GAME_COMPONENT_GameVelocity.id, &velocity, 0);
        ecs_world_apply_commands(world);
        behavior_instance *first = instance_new(first_asset, world, entity);
        behavior_instance *second = instance_new(second_asset, world, entity);
        game_reactive *first_manager = game_reactive_new(world);
        game_reactive *second_manager = game_reactive_new(world);
        if (first && second) {
            flow_error error = { 0 };
            game_reactive_world_begin(world);
            game_reactive_begin(first_manager);
            int accepted = game_reactive_apply(first_manager, first, &error);
            game_reactive_end(first_manager);
            test_check(accepted >= 0 && !error.message, "the first manager acquires its maintained component field");
            /* Grow component columns between managers: ownership is recorded
             * against the entity and byte range rather than a storage pointer. */
            for (int j = 0; j < 128; j++) {
                ecs_entity extra = ecs_world_create_entity(world);
                if (i == 0) {
                    GameParticleEmitter extra_emitter = { 0 };
                    ecs_world_record_add(
                        world,
                        extra,
                        GAME_COMPONENT_GameParticleEmitter.id,
                        &extra_emitter,
                        0
                    );
                } else {
                    ecs_world_record_add(
                        world,
                        extra,
                        GAME_COMPONENT_GameTransform.id,
                        &position,
                        0
                    );
                }
            }
            test_check(ecs_world_apply_commands(world) == ECS_OK, "ownership fixture grows the target component column between managers");
            game_reactive_begin(second_manager);
            int refused = game_reactive_apply(second_manager, second, &error);
            game_reactive_end(second_manager);
            game_reactive_world_end(world);
            test_check(
                refused == FLOW_ERR_COMPILE && error.code == FLOW_ERR_COMPILE &&
                    error.message &&
                    strstr(error.message, "overlapping component fields"),
                i == 0 ? "separate managers cannot maintain the same component "
                         "field"
                       : "a whole-component writer conflicts with a writer of "
                         "its second lane"
            );
        }
        behavior_instance_destroy(first);
        behavior_instance_destroy(second);
        game_reactive_free(first_manager);
        game_reactive_free(second_manager);
        ecs_world_free(world);
        behavior_asset_free(first_asset);
        behavior_asset_free(second_asset);
    }
}

static void graph_binding_edits()
{
    code_source *source = code_source_parse(
        "behavior Edited { bind self.ParticleEmitter.Active = false; }"
    );
    behavior_asset *edited = source->asset ? behavior_asset_clone(source->asset) : NULL;
    test_check(edited != NULL, "binding graph editing fixture parses");
    if (!edited) {
        code_source_free(source);
        return;
    }
    graph *g = edited->graph;
    node *set = graph_node_by_id(g, edited->reactives[0].statement_node_id);
    int value = -1, owner = -1;
    for (int i = 0; i < set->input_count; i++) {
        if (set->input_pins[i].type == PIN_BOOL) {
            value = i;
        }
        if (!strcmp(set->input_pins[i].name, "Entity") ||
            !strcmp(set->input_pins[i].name, "Target")) {
            owner = i;
        }
    }
    test_check(value >= 0 && owner >= 0, "binding exposes typed value and entity Graph inputs");
    if (value < 0 || owner < 0) {
        behavior_asset_free(edited);
        code_source_free(source);
        return;
    }
    pin_value yes = { .type = PIN_BOOL, .b = true };
    node *old_value = NULL;
    int old_value_pin;
    if (graph_input_source(g, set, value, &old_value, &old_value_pin)) {
        graph_remove_edge(g, old_value, old_value_pin, set, value);
    }
    graph_set_pin_default(g, set, 0, value, &yes);
    char error[256] = { 0 };
    test_check(!behavior_reactive_validate(edited, error, sizeof error), "publication rejects a stale generated binding guard");
    error[0] = 0;
    char *metadata = NULL;
    char *text = code_source_reconcile_with_metadata(source, edited, &metadata, error, sizeof error);
    if (!text) {
        fprintf(stderr, "Reactive value reconcile: %s\n", error);
    }
    test_check(text && strstr(text, "true"), "Graph binding value edit regenerates readable source and its guard");
    behavior_asset *asset = text ? compile(text) : NULL;
    ecs_world *world = world_new();
    ecs_entity parent = emitter(world, (ecs_entity){ 0 });
    ecs_entity child = emitter(world, parent);
    behavior_instance *instance = asset ? instance_new(asset, world, child) : NULL;
    game_reactive *reactive = game_reactive_new(world);
    if (instance) {
        maintain(reactive, instance);
    }
    test_check(
        instance && GAME_GameParticleEmitter_get(world, child)->active,
        "Graph binding value edit executes the regenerated compiled assignment"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    free(text);
    free(metadata);
    node *read = graph_node_by_id(g, edited->reactives[0].source_node_id);
    node *parent_read = graph_add_node(g, read->desc);
    if (component_node_kind_of(read->desc) == COMPONENT_NODE_GET) {
        component_node_configure(g, parent_read, component_node_key(read), NULL);
    }
    for (int i = 0; i < read->input_count; i++) {
        if (!strcmp(read->input_pins[i].name, "Entity")) {
            node *self = NULL;
            int self_pin;
            if (graph_input_source(g, read, i, &self, &self_pin)) {
                graph_connect_data(g, self, self_pin, parent_read, i);
            }
        }
    }
    int whole = 0;
    while (parent_read->output_pins[whole].kind != PIN_DATA) {
        whole++;
    }
    int parent_pin = graph_expose_output_field(g, parent_read, whole, "parent");
    test_check(
        parent_pin >= 0 &&
            graph_connect_data(g, parent_read, parent_pin, set, owner) == 0,
        "binding target can be edited to a reflected entity path"
    );
    error[0] = 0;
    metadata = NULL;
    text = code_source_reconcile_with_metadata(source, edited, &metadata, error, sizeof error);
    if (!text) {
        fprintf(stderr, "Reactive target reconcile: %s\n", error);
    }
    test_check(text && strstr(text, "Parent"), "Graph target edit regenerates the semantic target path and read-back guard");
    asset = text ? compile(text) : NULL;
    GAME_GameParticleEmitter_get(world, child)->active = false;
    instance = asset ? instance_new(asset, world, child) : NULL;
    if (instance) {
        maintain(reactive, instance);
    }
    test_check(
        instance && GAME_GameParticleEmitter_get(world, parent)->active &&
            !GAME_GameParticleEmitter_get(world, child)->active,
        "regenerated binding writes its edited parent target without writing "
        "the old target"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    free(text);
    free(metadata);
    GameActionBinding parent_binding = { .pressed = false };
    ecs_world_record_add(world, parent, GAME_COMPONENT_GameActionBinding.id, &parent_binding, 0);
    test_check(ecs_world_apply_commands(world) == ECS_OK, "Graph source fixture adds its reflected binding component");
    node *pressed_read = graph_add_node(g, &NODE_GetComponent);
    component_node_configure(g, pressed_read, GAME_COMPONENT_GameActionBinding.key, NULL);
    int pressed_pin = graph_expose_output_field(g, pressed_read, 0, "pressed");
    int pressed_owner = -1;
    for (int i = 0; i < pressed_read->input_count; i++) {
        if (!strcmp(pressed_read->input_pins[i].name, "Entity")) {
            pressed_owner = i;
        }
    }
    test_check(
        pressed_pin >= 0 && pressed_owner >= 0 &&
            graph_connect_data(g, parent_read, parent_pin, pressed_read, pressed_owner) == 0 &&
            graph_connect_data(g, pressed_read, pressed_pin, set, value) == 0,
        "Graph binding value accepts a direct reflected field wire"
    );
    error[0] = 0;
    metadata = NULL;
    text = code_source_reconcile_with_metadata(source, edited, &metadata, error, sizeof error);
    if (!text) {
        fprintf(stderr, "Reactive source wire reconcile: %s\n", error);
    }
    test_check(text && strstr(text, "Pressed"), "direct Graph source wire regenerates an atomic field path without an identity expression");
    asset = text ? compile(text) : NULL;
    instance = asset ? instance_new(asset, world, child) : NULL;
    if (instance) {
        maintain(reactive, instance);
    }
    test_check(
        instance && !GAME_GameParticleEmitter_get(world, parent)->active,
        "compiled Graph source wire updates its reflected parent target"
    );
    GAME_GameActionBinding_get(world, parent)->pressed = true;
    if (instance) {
        maintain(reactive, instance);
    }
    test_check(
        instance && GAME_GameParticleEmitter_get(world, parent)->active,
        "compiled Graph source wire observes later native dependency writes"
    );
    behavior_instance_destroy(instance);
    game_reactive_free(reactive);
    ecs_world_free(world);
    behavior_asset_free(asset);
    free(text);
    free(metadata);
    behavior_asset_free(edited);
    code_source_free(source);
}

static void graph_shared_read_retarget()
{
    code_source *source = code_source_parse("behavior Retarget { bind self.ActionBinding.Pressed = self.ActionBinding.Disabled; }");
    behavior_asset *edited = source->asset ? behavior_asset_clone(source->asset) : NULL;
    test_check(edited != NULL, "shared binding read retarget fixture parses");
    if (!edited) {
        code_source_free(source);
        return;
    }
    graph *g = edited->graph;
    behavior_reactive *r = &edited->reactives[0];
    node *set = graph_node_by_id(g, r->statement_node_id);
    node *get = graph_node_by_id(g, r->source_node_id);
    int owner = -1, value = -1, whole = -1;
    for (int i = 0; i < set->input_count; i++) {
        if (!strcmp(set->input_pins[i].name, "Entity") ||
            !strcmp(set->input_pins[i].name, "Target")) {
            owner = i;
        } else if (set->input_pins[i].type == PIN_BOOL) {
            value = i;
        }
    }
    for (int i = 0; i < get->output_count; i++) {
        if (get->output_pins[i].kind == PIN_DATA && !get->output_pins[i].field_source) {
            whole = i;
            break;
        }
    }
    int disabled = whole >= 0 ? graph_expose_output_field(g, get, whole, "disabled")
                              : -1;
    node *parent_read = graph_add_node(g, &NODE_GetComponent);
    component_node_configure(g, parent_read, GAME_COMPONENT_GameParticleEmitter.key, NULL);
    int parent_pin = graph_expose_output_field(g, parent_read, 0, "parent");
    test_check(
        owner >= 0 && value >= 0 && disabled >= 0 && parent_pin >= 0,
        "shared read fixture exposes reflected target and value fields"
    );
    if (owner < 0 || value < 0 || disabled < 0 || parent_pin < 0) {
        behavior_asset_free(edited);
        code_source_free(source);
        return;
    }
    graph_connect_data(g, get, disabled, set, value);
    graph_connect_data(g, parent_read, parent_pin, set, owner);
    char error[256] = { 0 };
    char *metadata = NULL;
    char *text = code_source_reconcile_with_metadata(source, edited, &metadata, error, sizeof error);
    if (!text) {
        fprintf(stderr, "Reactive shared read retarget: %s\n", error);
    }
    test_check(text != NULL, "target edit isolates guard read from the shared authored RHS");
    behavior_asset *asset = text ? compile(text) : NULL;
    ecs_world *world = world_new();
    ecs_entity parent = binding(world, false);
    ecs_entity child = emitter(world, parent);
    GameActionBinding child_binding = { .disabled = true };
    ecs_world_record_add(world, child, GAME_COMPONENT_GameActionBinding.id, &child_binding, 0);
    ecs_world_apply_commands(world);
    game_reactive *reactive = game_reactive_new(world);
    behavior_instance *instance = asset ? instance_new(asset, world, child) : NULL;
    if (instance) {
        maintain(reactive, instance);
    }
    test_check(
        instance && GAME_GameActionBinding_get(world, parent)->pressed &&
            !GAME_GameActionBinding_get(world, child)->pressed,
        "retargeted binding reads the original entity RHS and writes only the "
        "parent"
    );
    GAME_GameActionBinding_get(world, child)->disabled = false;
    GAME_GameActionBinding_get(world, parent)->disabled = true;
    if (instance) {
        maintain(reactive, instance);
    }
    test_check(instance && !GAME_GameActionBinding_get(world, parent)->pressed, "retargeted binding keeps its authored RHS dependency on later native writes");
    behavior_instance_destroy(instance);
    game_reactive_free(reactive);
    ecs_world_free(world);
    behavior_asset_free(asset);
    free(metadata);
    free(text);
    behavior_asset_free(edited);
    code_source_free(source);
}

void test_code_reactive()
{
    source_contracts();
    maintained_relationship();
    changed_payload();
    dependency_order();
    maintained_vector();
    competing_managers();
    graph_binding_edits();
    graph_shared_read_retarget();
}
