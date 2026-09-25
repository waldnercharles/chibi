#include "graph/nodes/component_nodes.h"

#include "graph/authoring/optimize.h"
#include "graph/compiler/compile.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"

#include <assert.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The asset file text: YAML without the positions of unplaced nodes. */
static char *disk_text(const behavior_asset *asset)
{
    char *json = behavior_asset_save_json(asset);
    char error[256];
    char *yaml = asset_text_to_disk(json, error, sizeof error);
    assert(yaml);
    free(json);
    return yaml;
}

static behavior_asset *reload(const behavior_asset *asset)
{
    char *yaml = disk_text(asset);
    behavior_asset *copy = behavior_asset_load_json(yaml);
    if (!copy) {
        fprintf(stderr, "%s\n%s\n", graph_last_error(), yaml);
    }
    assert(copy);
    free(yaml);
    return copy;
}

/* The saved document without the ID counter, which raising advances. */
static char *document(const behavior_asset *asset)
{
    char *json = behavior_asset_save_json(asset);
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_mut_doc *copy = yyjson_doc_mut_copy(doc, NULL);
    yyjson_mut_obj_remove_key(yyjson_mut_doc_get_root(copy), "id_counter");
    char *text = yyjson_mut_write(copy, 0, NULL);
    yyjson_mut_doc_free(copy);
    yyjson_doc_free(doc);
    free(json);
    return text;
}

static int raise_all(behavior_asset *asset)
{
    int raised = 0;
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            raised += component_node_raise(g, g->nodes[j]);
        }
    }
    return raised;
}

static int compile_errors(const behavior_asset *asset)
{
    compile_report report;
    compile_report_init(&report);
    int errors = behavior_asset_compile(asset, &report);
    compile_report_free(&report);
    return errors;
}

/* Every generated access node in project content has a component node that
 * saves, loads and lowers back to the same graph. */
static void project_equivalence()
{
    static const char *const patterns[] = {
        "projects/*/content/behaviors/*.yaml",
        "projects/*/content/libraries/*.yaml",
        "content/behaviors/*.yaml",
        "content/libraries/*.yaml",
        "test/fixtures/content/behaviors/*.yaml",
    };
    int files = 0, raised = 0;
    for (size_t p = 0; p < sizeof patterns / sizeof *patterns; p++) {
        glob_t paths = { 0 };
        if (glob(patterns[p], 0, NULL, &paths)) {
            continue;
        }
        for (size_t i = 0; i < paths.gl_pathc; i++) {
            char *text = text_file_read(paths.gl_pathv[i]);
            behavior_asset *original = behavior_asset_load_json(text);
            behavior_asset *authored = behavior_asset_load_json(text);
            free(text);
            if (!original) {
                fprintf(stderr, "%s: %s\n", paths.gl_pathv[i], graph_last_error());
            }
            assert(original && authored);
            files++;
            int count = raise_all(authored);
            raised += count;
            behavior_asset *loaded = reload(authored);
            char *saved = disk_text(authored);
            char *resaved = disk_text(loaded);
            if (strcmp(saved, resaved)) {
                fprintf(stderr, "%s does not round-trip:\n%s\n%s\n", paths.gl_pathv[i], saved, resaved);
                assert(false);
            }
            bool authored_nodes = behavior_asset_has_component_nodes(original);
            assert(behavior_asset_has_component_nodes(loaded) == (count > 0 || authored_nodes));
            assert(compile_errors(loaded) == compile_errors(original));
            char error[256] = "";
            if (behavior_asset_lower_component_nodes(original, error, sizeof error) ||
                behavior_asset_lower_component_nodes(loaded, error, sizeof error)) {
                fprintf(stderr, "%s: %s\n", paths.gl_pathv[i], error);
                assert(false);
            }
            char *expected = document(original);
            char *actual = document(loaded);
            if (strcmp(expected, actual)) {
                fprintf(stderr, "%s lowers differently:\n%s\n%s\n", paths.gl_pathv[i], expected, actual);
                assert(false);
            }
            free(expected);
            free(actual);
            free(saved);
            free(resaved);
            behavior_asset_free(original);
            behavior_asset_free(authored);
            behavior_asset_free(loaded);
        }
        globfree(&paths);
    }
    assert(files > 20 && raised > 100);
    printf("component node equivalence: %d behaviors, %d generated nodes raised and lowered back\n", files, raised);
}

typedef struct
{
    behavior_asset *asset;
    graph *g;
    node *event;
} fixture;

static fixture fixture_new()
{
    fixture f = { .asset = behavior_asset_new("Component access") };
    assert(behavior_asset_set_owner_type(f.asset, "ecs.entity") == 0);
    f.g = f.asset->graph;
    f.event = graph_add_node(f.g, &NODE_Event);
    assert(graph_set_event_name(f.g, f.event, "Run") == 0);
    pin_spec other = { .name = "Other", .type = PIN_HANDLE, .type_name = "ecs.entity" };
    assert(graph_add_pin(f.g, f.event, 1, &other) == 1);
    return f;
}

static node *add(fixture *f, const char *type)
{
    node *n = graph_add_node(f->g, registry_node_by_name(type));
    assert(n);
    return n;
}

static node *component(fixture *f, const node_desc *desc, const char *name)
{
    node *n = graph_add_node(f->g, desc);
    pin_value key = { .type = PIN_NAME, .str = name };
    assert(graph_set_pin_default(f->g, n, 0, component_node_setting(n), &key) == 0);
    return n;
}

static void set_default(fixture *f, node *n, int pin, pin_value value)
{
    assert(graph_set_pin_default(f->g, n, 0, pin, &value) == 0);
}

static void exec(fixture *f, node *from, int from_pin, node *to)
{
    assert(graph_connect_exec(f->g, from, from_pin, to, 0, 0) == 0);
}

static void data(fixture *f, node *from, int from_pin, node *to, int to_pin)
{
    assert(graph_connect_data(f->g, from, from_pin, to, to_pin) == 0);
}

static GameHealth health_value = { 5, 9 };

static pin_value health_default()
{
    return (pin_value){ .type = PIN_STRUCT, .type_name = "GameHealth", .ptr = &health_value };
}

static node *rebuild_event(fixture *f)
{
    node *event = graph_add_node(f->g, &NODE_Event);
    assert(graph_set_event_name(f->g, event, "Rebuild") == 0);
    return event;
}

/* Copies Velocity into Transform, writes Other's Health, then records whether
 * Other has Input, using the generated descriptors. Rebuild adds Input to the
 * owner and removes its Velocity. */
static behavior_asset *generated_form()
{
    fixture f = fixture_new();
    node *velocity = add(&f, "Get_GameVelocity");
    assert(graph_expose_output_fields(f.g, velocity, 0) == 0);
    node *split = add(&f, "Break_GameVelocity");
    node *x = add(&f, "Set_GameTransform_x");
    node *y = add(&f, "Set_GameTransform_y");
    node *made = add(&f, "Make_GameHealth");
    node *health = add(&f, "SetEntity_GameHealth");
    node *has = add(&f, "HasEntity_GameInput");
    node *branch = graph_add_node(f.g, &NODE_Branch);
    node *yes = add(&f, "Set_GameHealth_current");
    node *no = add(&f, "Set_GameHealth_current");
    data(&f, velocity, 1, x, 1);
    data(&f, velocity, 0, split, 0);
    data(&f, split, 1, y, 1);
    data(&f, f.event, 1, health, 1);
    set_default(&f, made, 0, (pin_value){ .type = PIN_I32, .i = health_value.current });
    set_default(&f, made, 1, (pin_value){ .type = PIN_I32, .i = health_value.maximum });
    data(&f, made, 0, health, 2);
    data(&f, f.event, 1, has, 0);
    data(&f, has, 0, branch, 1);
    set_default(&f, yes, 1, (pin_value){ .type = PIN_I32, .i = 1 });
    set_default(&f, no, 1, (pin_value){ .type = PIN_I32, .i = 2 });
    exec(&f, f.event, 0, x);
    exec(&f, x, 0, y);
    exec(&f, y, 0, health);
    exec(&f, health, 0, branch);
    exec(&f, branch, 0, yes);
    exec(&f, branch, 1, no);
    node *rebuild = rebuild_event(&f);
    node *input = add(&f, "Add_GameInput");
    node *removal = add(&f, "Remove_GameVelocity");
    exec(&f, rebuild, 0, input);
    exec(&f, input, 0, removal);
    return f.asset;
}

/* The same behavior authored with component nodes; one Set writes two fields. */
static behavior_asset *component_form()
{
    fixture f = fixture_new();
    node *velocity = component(&f, &NODE_GetComponent, "GameVelocity");
    assert(!strcmp(velocity->output_pins[0].type_name, "GameVelocity"));
    assert(graph_expose_output_fields(f.g, velocity, 0) == 0);
    node *split = component(&f, &NODE_BreakStruct, "GameVelocity");
    assert(split->input_count == 2 && split->output_count == 2);
    node *transform = component(&f, &NODE_SetComponent, "GameTransform");
    assert(component_node_show_field(f.g, transform, "y", true) == 0);
    assert(component_node_show_field(f.g, transform, "x", true) == 0);
    assert(transform->input_count == 5 && !strcmp(transform->input_pins[3].name, "x"));
    node *made = component(&f, &NODE_MakeStruct, "GameHealth");
    assert(made->input_count == 3 && !strcmp(made->output_pins[0].type_name, "GameHealth"));
    node *health = component(&f, &NODE_SetComponent, "game.GameHealth");
    node *has = component(&f, &NODE_HasComponent, "GameInput");
    node *branch = graph_add_node(f.g, &NODE_Branch);
    node *yes = component(&f, &NODE_SetComponent, "GameHealth");
    node *no = component(&f, &NODE_SetComponent, "GameHealth");
    assert(component_node_show_field(f.g, yes, "current", true) == 0);
    assert(component_node_show_field(f.g, no, "current", true) == 0);
    data(&f, velocity, 1, transform, 3);
    data(&f, velocity, 0, split, 0);
    data(&f, split, 1, transform, 4);
    data(&f, f.event, 1, health, 2);
    set_default(&f, made, 0, (pin_value){ .type = PIN_I32, .i = health_value.current });
    set_default(&f, made, 1, (pin_value){ .type = PIN_I32, .i = health_value.maximum });
    data(&f, made, 0, health, 3);
    data(&f, f.event, 1, has, 1);
    data(&f, has, 0, branch, 1);
    set_default(&f, yes, 3, (pin_value){ .type = PIN_I32, .i = 1 });
    set_default(&f, no, 3, (pin_value){ .type = PIN_I32, .i = 2 });
    exec(&f, f.event, 0, transform);
    exec(&f, transform, 0, health);
    exec(&f, health, 0, branch);
    exec(&f, branch, 0, yes);
    exec(&f, branch, 1, no);
    node *rebuild = rebuild_event(&f);
    node *input = component(&f, &NODE_AddComponent, "GameInput");
    node *removal = component(&f, &NODE_RemoveComponent, "GameVelocity");
    exec(&f, rebuild, 0, input);
    exec(&f, input, 0, removal);
    return f.asset;
}

typedef struct
{
    GameTransform transform;
    GameHealth owner, other, third;
    bool input, velocity;
} outcome;

static void run(ecs_world *world, behavior_instance *instance, ecs_entity other)
{
    pin_value target = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(other) };
    flow_error error;
    flow_run *activation = behavior_instance_dispatch(
        instance,
        "Run",
        (flow_inputs){ .values = &target, .count = 1 },
        &error
    );
    assert(activation && flow_run_status(activation) == FLOW_COMPLETED);
    (void)world;
}

static outcome execute(const behavior_asset *authored)
{
    char error[256] = "";
    behavior_asset *linked = behavior_asset_link_libraries(authored, NULL, 0, error, sizeof error);
    assert(linked && !behavior_asset_has_component_nodes(linked));
    ecs_world *world = ecs_world_new();
    const ecs_component_desc *components[] = { &GAME_COMPONENT_GameTransform,
                                               &GAME_COMPONENT_GameVelocity,
                                               &GAME_COMPONENT_GameHealth,
                                               &GAME_COMPONENT_GameInput };
    for (size_t i = 0; i < sizeof components / sizeof *components; i++) {
        assert(ecs_world_register_component(world, components[i], NULL) == ECS_OK);
    }
    ecs_entity owner = ecs_world_create_entity(world);
    ecs_entity other = ecs_world_create_entity(world);
    ecs_entity third = ecs_world_create_entity(world);
    assert(GAME_GameTransform_add(world, owner, 0) == ECS_OK);
    assert(GAME_GameVelocity_add(world, owner, 0) == ECS_OK);
    assert(GAME_GameHealth_add(world, owner, 0) == ECS_OK);
    assert(GAME_GameHealth_add(world, other, 0) == ECS_OK);
    assert(GAME_GameInput_add(world, other, 0) == ECS_OK);
    assert(GAME_GameHealth_add(world, third, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    *GAME_GameVelocity_get(world, owner) = (GameVelocity){ 3, 4 };
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    behavior_instance *instance = behavior_instance_create(linked, &handle, NULL, NULL);
    assert(instance && behavior_instance_bind_service(instance, "ecs.world", world) == 0);
    run(world, instance, other);
    outcome result = { .transform = *GAME_GameTransform_get_const(world, owner),
                       .other = *GAME_GameHealth_get_const(world, other) };
    assert(GAME_GameHealth_get_const(world, owner)->current == 1);
    run(world, instance, third);
    result.owner = *GAME_GameHealth_get_const(world, owner);
    result.third = *GAME_GameHealth_get_const(world, third);
    flow_error error_state;
    flow_run *rebuilt = behavior_instance_dispatch(instance, "Rebuild", (flow_inputs){ 0 }, &error_state);
    assert(rebuilt && flow_run_status(rebuilt) == FLOW_COMPLETED);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    result.input = GAME_GameInput_has(world, owner);
    result.velocity = GAME_GameVelocity_has(world, owner);
    behavior_instance_destroy(instance);
    behavior_asset_free(linked);
    ecs_world_free(world);
    return result;
}

static bool same_health(GameHealth a, GameHealth b)
{
    return a.current == b.current && a.maximum == b.maximum;
}

static bool same_outcome(outcome a, outcome b)
{
    return a.transform.x == b.transform.x && a.transform.y == b.transform.y &&
           same_health(a.owner, b.owner) && same_health(a.other, b.other) &&
           same_health(a.third, b.third) && a.input == b.input &&
           a.velocity == b.velocity;
}

static void runtime_equivalence()
{
    behavior_asset *generated = generated_form();
    behavior_asset *raised = generated_form();
    assert(raise_all(raised) == 11);
    behavior_asset *authored = component_form();
    behavior_asset *loaded = reload(authored);
    outcome expected = execute(generated);
    assert(expected.transform.x == 3 && expected.transform.y == 4);
    assert(expected.other.current == 5 && expected.other.maximum == 9);
    assert(expected.owner.current == 2 && expected.third.current == 5);
    assert(expected.input && !expected.velocity);
    const behavior_asset *forms[] = { raised, authored, loaded };
    for (size_t i = 0; i < sizeof forms / sizeof *forms; i++) {
        outcome actual = execute(forms[i]);
        assert(same_outcome(actual, expected));
    }
    char error[256] = "";
    behavior_asset *lowered = behavior_asset_link_libraries(authored, NULL, 0, error, sizeof error);
    int setters = 0;
    for (int i = 0; i < acount(lowered->graph->nodes); i++) {
        setters += !strncmp(lowered->graph->nodes[i]->desc->name, "Set_GameTransform_", 18);
    }
    assert(setters == 2);
    behavior_asset_free(lowered);
    behavior_asset_free(generated);
    behavior_asset_free(raised);
    behavior_asset_free(authored);
    behavior_asset_free(loaded);
    puts("component node runtime: generated, raised, authored and reloaded forms agree");
}

/* A Get Component reading Self folds into component field bindings like the
 * generated getter; one reading another entity stays a node. */
static void optimizer_folds()
{
    fixture f = fixture_new();
    node *velocity = component(&f, &NODE_GetComponent, "GameVelocity");
    node *other = component(&f, &NODE_GetComponent, "GameVelocity");
    assert(graph_expose_output_fields(f.g, velocity, 0) == 0);
    assert(graph_expose_output_fields(f.g, other, 0) == 0);
    node *transform = component(&f, &NODE_SetComponent, "GameTransform");
    assert(component_node_split_value(f.g, transform, true) == 0);
    data(&f, velocity, 1, transform, 3);
    data(&f, other, 2, transform, 4);
    data(&f, f.event, 1, other, 1);
    exec(&f, f.event, 0, transform);
    node *last = component(&f, &NODE_SetComponent, "GameExperience");
    assert(component_node_show_field(f.g, last, "current", true) == 0);
    exec(&f, transform, 0, last);
    /* Break Struct over a Self read becomes a nested field binding. */
    node *health = component(&f, &NODE_GetComponent, "GameHealth");
    node *split = component(&f, &NODE_BreakStruct, "GameHealth");
    data(&f, health, 0, split, 0);
    data(&f, split, 0, last, 3);
    node *unused = component(&f, &NODE_HasComponent, "GameAim");
    uint64_t last_id = last->id, unused_id = unused->id, split_id = split->id;
    uint64_t folded = velocity->id;
    graph_optimize_options options = graph_optimize_defaults();
    graph_optimize_report report = { 0 };
    assert(graph_optimize(f.g, &options, &report) > 0);
    assert(
        report.removed_nodes == 3 && !graph_node_by_id(f.g, folded) &&
        !graph_node_by_id(f.g, split_id) && graph_node_by_id(f.g, other->id) == other
    );
    const pin_binding *nested = last->input_pins[3].binding;
    assert(nested && nested->kind == PIN_BINDING_COMPONENT_FIELD);
    assert(
        !strcmp(nested->component_key, "game.GameHealth") &&
        !strcmp(nested->field_name, "current")
    );
    const pin_binding *binding = transform->input_pins[3].binding;
    assert(binding && binding->kind == PIN_BINDING_COMPONENT_FIELD);
    assert(
        !strcmp(binding->component_key, "GAME.GameVelocity") &&
        !strcmp(binding->field_name, "x")
    );
    assert(!transform->input_pins[4].binding);
    assert(graph_node_by_id(f.g, last_id) && graph_node_by_id(f.g, unused_id));
    graph_optimize_report_free(&report);
    behavior_asset_free(f.asset);
    puts("component node optimizer: Self reads and Break Struct fold into bindings; explicit entities and unused reads stay");
}

/* Choosing, splitting and saving component nodes, including unfinished ones. */
static void authoring()
{
    fixture f = fixture_new();
    node *unset[] = { graph_add_node(f.g, &NODE_GetComponent),
                      graph_add_node(f.g, &NODE_SetComponent),
                      graph_add_node(f.g, &NODE_HasComponent) };
    behavior_asset *loaded = reload(f.asset);
    char *saved = disk_text(f.asset), *resaved = disk_text(loaded);
    assert(!strcmp(saved, resaved));
    free(saved);
    free(resaved);
    behavior_asset_free(loaded);
    char error[256] = "";
    behavior_asset *linked = behavior_asset_link_libraries(f.asset, NULL, 0, error, sizeof error);
    assert(!linked && !strcmp(error, "Choose a component for Get Component"));
    assert(compile_errors(f.asset) == 1);
    pin_value unknown = { .type = PIN_NAME, .str = "GameNothing" };
    assert(graph_set_pin_default(f.g, unset[0], 0, 0, &unknown) == -1);

    node *set = unset[1];
    pin_value health = { .type = PIN_NAME, .str = "GameHealth" };
    assert(graph_set_pin_default(f.g, set, 0, 1, &health) == 0);
    assert(graph_split_pin(f.g, set, 0, 3) == 3);
    assert(component_node_split(set) && set->input_count == 5);
    assert(component_node_show_field(f.g, set, "maximum", false) == 0);
    data(&f, f.event, 1, set, 2);
    assert(graph_connect_data(f.g, unset[2], 0, set, 3) == -1);
    pin_value velocity = { .type = PIN_NAME, .str = "GameVelocity" };
    assert(graph_set_pin_default(f.g, set, 0, 1, &velocity) == 0);
    assert(!component_node_split(set) && set->input_count == 4);
    assert(!strcmp(set->input_pins[3].type_name, "GameVelocity"));
    node *source = NULL;
    int source_pin = -1;
    assert(graph_input_source(f.g, set, 2, &source, &source_pin) && source == f.event);
    assert(component_node_show_field(f.g, set, "y", true) == 0);
    assert(graph_recombine_pin(f.g, set, 0, 2) == -1 && component_node_split(set));
    assert(graph_recombine_pin(f.g, set, 0, 3) == 3 && !component_node_split(set));

    /* An Entity binding reads another entity, so it lowers to the entity form. */
    node *get = unset[0];
    pin_value transform = { .type = PIN_NAME, .str = "GameTransform" };
    assert(graph_set_pin_default(f.g, get, 0, 0, &transform) == 0);
    pin_binding target = { .kind = PIN_BINDING_VARIABLE, .variable_id = 1 };
    assert(graph_set_pin_binding(f.g, get, 0, 1, &target) == 0);
    assert(!component_node_self(f.g, get) && !component_node_getter(f.g, get));
    pin_value aim = { .type = PIN_NAME, .str = "GameAim" };
    assert(graph_set_pin_default(f.g, unset[2], 0, 0, &aim) == 0);
    uint64_t get_id = get->id;
    behavior_asset *copy = reload(f.asset);
    assert(graph_lower_component_nodes(copy->graph, error, sizeof error) == 0);
    node *lowered = graph_node_by_id(copy->graph, get_id);
    assert(lowered->desc == registry_node_by_name("GetEntity_GameTransform"));
    assert(
        lowered->input_pins[0].binding && lowered->input_pins[0].binding->kind == PIN_BINDING_VARIABLE
    );
    behavior_asset_free(copy);

    /* A literal entity is explicit too. */
    pin_value handle = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 7 };
    assert(graph_set_pin_binding(f.g, get, 0, 1, NULL) == 0);
    assert(graph_set_pin_default(f.g, get, 0, 1, &handle) == 0);
    assert(!component_node_self(f.g, get));

    /* A Set writing two fields keeps its exposed outputs on the last setter. */
    node *pair = component(&f, &NODE_SetComponent, "GameTransform");
    assert(component_node_split_value(f.g, pair, true) == 0 && pair->input_count == 5);
    assert(graph_expose_output_fields(f.g, pair, 1) == 0 && pair->output_count == 4);
    node *sink = component(&f, &NODE_SetComponent, "GameVelocity");
    assert(component_node_show_field(f.g, sink, "x", true) == 0);
    data(&f, pair, 2, sink, 3);
    exec(&f, f.event, 0, pair);
    exec(&f, pair, 0, sink);
    uint64_t exposed = pair->output_pins[2].id, sink_id = sink->id;
    assert(graph_lower_component_nodes(f.g, error, sizeof error) == 0);
    sink = graph_node_by_id(f.g, sink_id);
    source = NULL;
    assert(graph_input_source(f.g, sink, 1, &source, &source_pin));
    assert(source->desc == registry_node_by_name("Set_GameTransform_y"));
    assert(source->output_pins[source_pin].id == exposed);
    assert(graph_node_by_id(f.g, get_id)->desc == registry_node_by_name("GetEntity_GameTransform"));
    behavior_asset_free(f.asset);

    assert(!behavior_asset_load_json(
        "version: 3\nname: Unknown\nid_counter: 10\ngraphs:\n- kind: event\n  "
        "name: \"\"\n  nodes:\n"
        "  - {id: 1, type: Get Component, inputs: {Component: GameNothing}}\n"
    ));
    assert(strstr(graph_last_error(), "unknown Component"));
    assert(!behavior_asset_load_json(
        "version: 3\nname: Unknown field\nid_counter: 10\ngraphs:\n- kind: "
        "event\n  name: \"\"\n  nodes:\n"
        "  - {id: 1, type: Set Component, inputs: {Component: GameHealth, "
        "bogus: 1}}\n"
    ));
    assert(strstr(graph_last_error(), "has no pin bogus"));
    /* A document may name the component and write only field inputs. */
    behavior_asset *named = behavior_asset_load_json(
        "version: 3\nname: Named\nid_counter: 10\ngraphs:\n- kind: event\n  "
        "name: \"\"\n  nodes:\n"
        "  - {id: 1, type: Set Component, inputs: {Component: GameHealth, "
        "current: 4}}\n"
    );
    assert(named);
    node *written = named->graph->nodes[0];
    assert(!strcmp(component_node_key(written), "game.GameHealth"));
    assert(
        component_node_split(written) && written->input_count == 4 &&
        written->input_pins[3].default_value.i == 4
    );
    behavior_asset_free(named);

    /* Libraries take explicit entities; a Self read belongs to a behavior. */
    behavior_asset *library = behavior_asset_new("Component library");
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *function = behavior_asset_add_function(library, "Health", 1);
    node *read = graph_add_node(function, &NODE_GetComponent);
    assert(graph_set_pin_default(function, read, 0, 0, &health) == 0);
    assert(behavior_asset_validate_library(library));
    node *entry = graph_node_by_id(function, function->entry_id);
    pin_spec entity = { .name = "Target", .type = PIN_HANDLE, .type_name = "ecs.entity" };
    int parameter = graph_add_pin(function, entry, 1, &entity);
    assert(graph_connect_data(function, entry, parameter, read, 1) == 0);
    assert(!behavior_asset_validate_library(library));
    behavior_asset_free(library);
    puts("component node authoring: retargeting, splitting, unfinished nodes, bindings and libraries");
}

/* Choosing a type reshapes Make and Break Struct; retargeting Add or Remove
 * keeps its wires, since every component gives them the same pins. */
static void struct_authoring()
{
    fixture f = fixture_new();
    node *made = graph_add_node(f.g, &NODE_MakeStruct);
    char error[256] = "";
    assert(!behavior_asset_link_libraries(f.asset, NULL, 0, error, sizeof error));
    assert(!strcmp(error, "Choose a type for Make Struct"));
    node *split = component(&f, &NODE_BreakStruct, "CF_V2");
    assert(split->output_count == 2 && component_node_setting(split) == 1);
    made = graph_node_by_id(f.g, made->id);
    pin_value health = { .type = PIN_NAME, .str = "GameHealth" };
    assert(graph_set_pin_default(f.g, made, 0, 0, &health) == 0);
    assert(made->input_count == 3 && !strcmp(made->input_pins[1].name, "maximum"));
    assert(!strcmp(component_node_key(made), "GameHealth"));
    pin_value transform = { .type = PIN_NAME, .str = "GameTransform" };
    assert(graph_set_pin_default(f.g, made, 0, component_node_setting(made), &transform) == 0);
    assert(made->input_count == 3 && !strcmp(made->input_pins[0].name, "x"));
    assert(!strcmp(made->output_pins[0].type_name, "GameTransform"));
    pin_value unknown = { .type = PIN_NAME, .str = "GameNothing" };
    assert(graph_set_pin_default(f.g, made, 0, 2, &unknown) == -1);
    node *add = component(&f, &NODE_AddComponent, "GameInput");
    exec(&f, f.event, 0, add);
    pin_value aim = { .type = PIN_NAME, .str = "GameAim" };
    assert(graph_set_pin_default(f.g, add, 0, 1, &aim) == 0);
    assert(graph_exec_input_count(f.g, add, 0) == 1);
    assert(!strcmp(component_node_key(add), "GAME.GameAim"));
    node *removal = component(&f, &NODE_RemoveComponent, "GameAim");
    exec(&f, add, 0, removal);
    behavior_asset *loaded = reload(f.asset);
    char *saved = disk_text(f.asset), *resaved = disk_text(loaded);
    assert(!strcmp(saved, resaved));
    free(saved);
    free(resaved);
    uint64_t ids[] = { made->id, split->id, add->id, removal->id };
    const char *lowered[] = { "Make_GameTransform", "Break_CF_V2", "Add_GameAim", "Remove_GameAim" };
    assert(behavior_asset_lower_component_nodes(loaded, error, sizeof error) == 0);
    for (int i = 0; i < 4; i++) {
        assert(
            graph_node_by_id(loaded->graph, ids[i])->desc ==
            registry_node_by_name(lowered[i])
        );
    }
    behavior_asset_free(loaded);
    behavior_asset_free(f.asset);

    behavior_asset *named = behavior_asset_load_json(
        "version: 3\nname: Named\nid_counter: 10\ngraphs:\n- kind: event\n  "
        "name: \"\"\n  nodes:\n"
        "  - {id: 1, type: Break Struct, inputs: {Type: CF_V2}}\n"
    );
    assert(named && named->graph->nodes[0]->output_count == 2);
    behavior_asset_free(named);
    assert(!behavior_asset_load_json(
        "version: 3\nname: Unknown\nid_counter: 10\ngraphs:\n- kind: event\n  "
        "name: \"\"\n  nodes:\n"
        "  - {id: 1, type: Make Struct, inputs: {Type: GameNothing}}\n"
    ));
    assert(strstr(graph_last_error(), "unknown Type"));
    puts("component node structs: Make and Break reshape by type; Add and Remove keep their wires");
}

/* Lowering gives a multi-field Set's outputs to its last setter, a node of its
 * own; a watch on the authored Value output still reads the written value. */
static void lowered_watches()
{
    fixture f = fixture_new();
    node *transform = component(&f, &NODE_SetComponent, "GameTransform");
    assert(component_node_show_field(f.g, transform, "x", true) == 0);
    assert(component_node_show_field(f.g, transform, "y", true) == 0);
    set_default(&f, transform, 3, (pin_value){ .type = PIN_F32, .f = 7 });
    set_default(&f, transform, 4, (pin_value){ .type = PIN_F32, .f = 8 });
    exec(&f, f.event, 0, transform);
    assert(graph_set_pin_watch(f.g, transform, 1, 1, 1) == 0);
    char error[256] = "";
    behavior_asset *linked = behavior_asset_link_libraries(f.asset, NULL, 0, error, sizeof error);
    assert(linked);
    ecs_world *world = ecs_world_new();
    assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    ecs_entity owner = ecs_world_create_entity(world);
    assert(GAME_GameTransform_add(world, owner, 0) == ECS_OK);
    assert(ecs_world_apply_commands(world) == ECS_OK);
    pin_value handle = { .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) };
    behavior_instance *instance = behavior_instance_create(linked, &handle, NULL, NULL);
    assert(instance && behavior_instance_bind_service(instance, "ecs.world", world) == 0);
    flow_error run_error;
    flow_run *activation = behavior_instance_dispatch(
        instance,
        "Run",
        (flow_inputs){ .values = &handle, .count = 1 },
        &run_error
    );
    assert(activation && flow_run_status(activation) == FLOW_COMPLETED);
    pin_value value;
    assert(flow_run_watch_output(activation, transform, 1, &value) == 0);
    const GameTransform *written = value.ptr;
    assert(value.type == PIN_STRUCT && written && written->x == 7 && written->y == 8);
    behavior_instance_destroy(instance);
    behavior_asset_free(linked);
    behavior_asset_free(f.asset);
    ecs_world_free(world);
    puts("component node watches: a multi-field Set's outputs read through the authored node");
}

void test_component_nodes()
{
    project_equivalence();
    runtime_equivalence();
    lowered_watches();
    optimizer_folds();
    authoring();
    struct_authoring();
}
