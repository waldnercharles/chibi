#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "runtime/behaviors/entities.h"
#include "test/suite_internal.h"
#include "test/whole_command_fixture.h"

static node *named(graph *g, const char *name)
{
    const node_desc *d = registry_node_by_name(name);
    assert(d);
    return graph_add_node(g, d);
}
static void data(graph *g, node *from, int out, node *to, int in)
{
    assert(!graph_connect_data(g, from, out, to, in));
}
static void next(graph *g, node *from, int out, node *to)
{
    assert(!graph_connect_exec(g, from, out, to, 0, 0));
}
static behavior_asset *events()
{
    behavior_asset *a = whole_command_fixture(WHOLE_COMMAND_LOOP, false, false);
    graph *g = a->graphs[1];
    node *entry = g->nodes[0], *loop = NULL, *capture = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->desc == &NODE_ForLoop) {
            loop = g->nodes[i];
        }
        if (g->nodes[i]->desc == whole_command_node()) {
            capture = g->nodes[i];
        }
    }
    assert(loop && capture);
    assert(!graph_remove_node(g, capture));
    assert(!graph_set_pin_default(g, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 64 }));
    node *send = named(g, "Send Event");
    assert(!graph_set_node_reference(g, send, 0, "Queued"));
    data(g, entry, 4, send, 1);
    for (int i = 1; i < 5; i++) {
        assert(graph_add_pin(g, send, 0, &entry->output_pins[i]) == i + 1);
        data(g, i == 1 ? loop : entry, i == 1 ? 1 : i, send, i + 1);
    }
    next(g, loop, 0, send);
    graph *event_graph = a->graph;
    node *event = graph_add_node(event_graph, &NODE_Event),
         *observe = graph_add_node(event_graph, whole_command_node());
    assert(!graph_set_event_name(event_graph, event, "Queued"));
    assert(!graph_set_node_reference(event_graph, observe, 0, "Capture"));
    for (int i = 1; i < 5; i++) {
        assert(graph_add_pin(event_graph, event, 1, &entry->output_pins[i]) == i);
        data(event_graph, event, i, observe, i);
    }
    next(event_graph, event, 0, observe);
    return a;
}
static void delivery(behavior_backend backend)
{
    behavior_asset *a = events();
    ecs_world *w = ecs_world_new();
    game_preview_world *live = game_preview_world_new(w);
    assert(!game_behavior_register_asset(live->runtime->behaviors, "commands", a));
    ecs_entity e = ecs_world_create_entity(w);
    assert(!game_behavior_attach(live->runtime->behaviors, e, "commands"));
    behavior_instance *receiver = game_behavior_instance(live->runtime->behaviors, e, "commands");
    assert(receiver && !behavior_instance_set_backend(receiver, backend));
    behavior_instance_set_trace_enabled(receiver, false);
    whole_command_capture captured = { .record = true };
    assert(!behavior_instance_bind_service(receiver, "test.commands", &captured));
    pin_value owner = { .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(e) };
    behavior_instance *sender = behavior_instance_create(a, &owner, NULL, NULL);
    behavior_instance_set_trace_enabled(sender, false);
    assert(!behavior_instance_set_backend(sender, backend));
    assert(
        !behavior_instance_bind_service(sender, "game.entities", live->runtime->entities)
    );
    char *name = strdup("queued real type");
    pin_value args[] = {
        { .type = PIN_I32, .i = 999 },
        { .type = PIN_F64, .type_name = name, .d = 2.75 },
        { .type = PIN_F32, .f = -1.5 },
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = owner.u64 }
    };
    flow_options options = { .instance = sender };
    graph *g = a->graphs[1];
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 4 }, &options, NULL);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    assert(run && !flow_run_resume(run));
    if (backend != BEHAVIOR_ORACLE) {
        assert(run->whole_function_executed);
    }
    assert(
        game_entities_pending_events(live->runtime->entities) == 65 && !captured.calls
    );
    flow_run_free(run);
    behavior_instance_destroy(sender);
    memset(name, '?', strlen(name));
    free(name);
    assert(!game_entities_dispatch_events(live->runtime->entities));
    if (captured.calls != 64) {
        fprintf(
            stderr,
            "delivery backend=%d calls=%d pending=%d activations=%d\n",
            backend,
            captured.calls,
            game_entities_pending_events(live->runtime->entities),
            acount(receiver->activations)
        );
    }
    assert(captured.calls == 64 && game_entities_pending_events(live->runtime->entities) == 1);
    assert(!game_entities_dispatch_events(live->runtime->entities));
    assert(captured.calls == 65 && !game_entities_pending_events(live->runtime->entities));
    for (int i = 0; i < 65; i++) {
        const pin_value *v = captured.values + i * 4;
        assert(
            v[0].i == i && v[1].d == 2.75 && v[2].f == -1.5 && v[3].u64 == owner.u64
        );
        assert(!strcmp(v[0].type_name, "int"));
        assert(!strcmp(v[1].type_name, "queued real type"));
        assert(!v[2].type_name && !strcmp(v[3].type_name, "ecs.entity"));
    }
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    whole_command_clear(&captured);
    game_preview_world_free(live);
    ecs_world_free(w);
    behavior_asset_free(a);
}
static void destruction(behavior_backend backend)
{
    ecs_world *w = ecs_world_new();
    assert(!ecs_world_register_component(w, &GAME_COMPONENT_GameHealth, NULL));
    ecs_entity entities[4];
    for (int i = 0; i < 4; i++) {
        entities[i] = ecs_world_create_entity(w);
        assert(!GAME_GameHealth_add(w, entities[i], 0));
    }
    assert(!ecs_world_apply_commands(w));
    game_preview_world *live = game_preview_world_new(w);
    game_entities *host = live->runtime->entities;
    behavior_asset *a = behavior_asset_new("Retire snapshot");
    graph *g = behavior_asset_add_function(a, "Retire", false);
    node *entry = g->nodes[0], *ret = g->nodes[1],
         *query = named(g, "Find Entities With Component"),
         *loop = graph_add_node(g, &NODE_ForEach),
         *destroy = named(g, "Destroy Entity");
    assert(!graph_set_pin_default(
        g,
        query,
        0,
        0,
        &(pin_value){ .type = PIN_NAME, .str = GAME_COMPONENT_GameHealth.key }
    ));
    data(g, query, 0, loop, 1);
    data(g, loop, 1, destroy, 1);
    next(g, entry, 0, loop);
    next(g, loop, 0, destroy);
    next(g, loop, 3, ret);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    assert(!behavior_instance_set_backend(bi, backend));
    assert(!behavior_instance_bind_service(bi, "game.entities", host));
    flow_options options = { .instance = bi };
    flow_run *run = flow_run_start(g, entry, (flow_inputs){ 0 }, &options, NULL);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    assert(run && !flow_run_resume(run));
    if (backend != BEHAVIOR_ORACLE) {
        assert(
            run->whole_function_executed && eval == eval_oracle_calls() &&
            flow == flow_oracle_calls()
        );
    }
    for (int i = 0; i < 4; i++) {
        assert(
            ecs_entity_is_alive(w, entities[i]) &&
            game_entities_is_retiring(host, entities[i])
        );
    }
    /* Reusing the entry queries the retirement filter and sees an empty set. */
    flow_run_free(run);
    run = flow_run_start(g, entry, (flow_inputs){ 0 }, &options, NULL);
    assert(run && !flow_run_resume(run));
    assert(!game_entities_flush(host));
    for (int i = 0; i < 4; i++) {
        assert(!ecs_entity_is_alive(w, entities[i]));
    }
    flow_run_free(run);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    game_preview_world_free(live);
    ecs_world_free(w);
}
void test_compiled_entity_commands()
{
    behavior_backend backends[] = { BEHAVIOR_ORACLE,
                                    BEHAVIOR_BYTECODE,
                                    behavior_default_backend() };
    for (int i = 0; i < 3; i++) {
        delivery(backends[i]);
        destruction(backends[i]);
    }
    puts("PASS: whole Send Event FIFO batches, owned payload lifetime, deferred Destroy Entity and query snapshots");
}
