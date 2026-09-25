#include "graph/document/library.h"
#include "graph/runtime/runtime.h"
#include "runtime/scenes/prototype_preview.h"
#include "test/suite_internal.h"

#include <assert.h>

static void library_contract_checks()
{
    behavior_asset *library = behavior_asset_new("Shared Math");
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *function = behavior_asset_add_function(library, "Identity", 1);
    node *entry = graph_node_by_id(function, function->entry_id);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec parameter = { .name = "Value",
                           .type = PIN_I32,
                           .type_name = "int",
                           .kind = PIN_DATA };
    graph_add_pin(function, entry, 1, &parameter);
    graph_add_pin(function, ret, 0, &parameter);
    graph_connect_data(function, entry, 0, ret, 0);
    behavior_asset *wrapper = behavior_asset_new("Shared Actions");
    wrapper->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *forward = behavior_asset_add_function(wrapper, "Forward", 1);
    node *inner = behavior_asset_add_function_call_node_to_graph(wrapper, forward, function);
    node *output = graph_node_by_id(forward, forward->return_id);
    graph_add_pin(forward, output, 0, &parameter);
    graph_set_pin_default(forward, inner, 0, 0, &(pin_value){ .type = PIN_I32, .i = 42 });
    graph_connect_data(forward, inner, 0, output, 0);
    uint64_t retained_pin = inner->input_pins[0].id;
    entry->output_pins[0].name = graph_own_str(function, "Renamed", 7);
    pin_spec unused = parameter;
    unused.name = "Unused";
    graph_add_pin(function, entry, 1, &unused);
    test_check(
        graph_library_refresh_call(forward, inner, function) == NULL &&
            inner->input_count == 2 && inner->input_pins[0].id == retained_pin &&
            inner->input_pins[0].default_value.i == 42 &&
            strcmp(inner->input_pins[0].name, "Renamed") == 0,
        "signature refresh preserves parameter IDs and values across rename "
        "and addition"
    );
    char *before_refresh = graph_save_json(forward);
    graph_set_pin_type(function, ret, 0, 0, PIN_F32, "float");
    test_check(
        graph_library_refresh_call(forward, inner, function) != NULL,
        "signature refresh diagnoses incompatible connected results"
    );
    char *after_refresh = graph_save_json(forward);
    test_check(strcmp(before_refresh, after_refresh) == 0, "rejected signature refresh preserves every wire and pin");
    free(before_refresh);
    free(after_refresh);
    graph_set_pin_type(function, ret, 0, 0, PIN_I32, "int");
    graph_connect_data(function, entry, 0, ret, 0);
    behavior_asset *asset = behavior_asset_new("LibraryConsumer");
    node *event = graph_add_node(asset->graph, &NODE_Event);
    node *call = behavior_asset_add_function_call_node_to_graph(asset, asset->graph, forward);
    uint64_t result_id = behavior_asset_add_variable(asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                             ->id;
    node *body = behavior_asset_add_set_node(asset, result_id);
    graph_set_event_name(asset->graph, event, "Test");
    graph_connect_exec(asset->graph, event, 0, body, 0, 0);
    graph_connect_data(asset->graph, call, 0, body, 1);
    const behavior_asset *catalog[] = { library, wrapper };
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(asset, catalog, 2, error, sizeof error);
    if (!linked) {
        fprintf(stderr, "Library link failure: %s / %s\n", error, graph_last_error());
    }
    test_check(linked && acount(linked->graphs) == 3, "nested shared libraries link into one behavior snapshot");
    bool authored = linked != NULL;
    for (int g = 0; linked && g < acount(linked->graphs); g++) {
        const graph *copy = linked->graphs[g];
        const behavior_asset *origin = !copy->source_asset ? asset
                                     : !strcmp(copy->source_asset, library->name)
                                         ? library
                                         : wrapper;
        for (int i = 0; i < acount(copy->nodes); i++) {
            graph_node_source source = graph_node_authored(copy, copy->nodes[i]->id);
            const graph *home = behavior_asset_find_graph(origin, source.graph_id);
            const node *n = home ? graph_node_by_id(home, source.node_id) : NULL;
            authored &= source.asset == copy->source_asset && n &&
                        n->desc == copy->nodes[i]->desc;
        }
    }
    test_check(authored, "a linked snapshot's nodes map back to the library nodes they copy");
    behavior_instance *bi = linked ? behavior_instance_create(linked, NULL, NULL, NULL)
                                   : NULL;
    flow_error flow_error = { 0 };
    flow_log_reset();
    flow_run *run = bi ? behavior_instance_dispatch(bi, "Test", (flow_inputs){ 0 }, &flow_error)
                       : NULL;
    pin_value result_value = { 0 };
    if (bi) {
        behavior_instance_get_variable(bi, result_id, &result_value);
    }
    test_check(
        run && flow_run_status(run) == FLOW_COMPLETED && result_value.i == 42,
        "qualified library calls execute despite overlapping source IDs"
    );
    test_check(
        call->reference_asset && strcmp(call->reference_asset, wrapper->name) == 0 &&
            acount(asset->graphs) == 1,
        "linking preserves the authored qualified call and graph list"
    );
    test_check(
        linked && linked->graphs[1]->source_asset &&
            linked->graphs[1]->source_graph_id == forward->graph_id,
        "linked function retains its authored source location"
    );
    behavior_instance_destroy(bi);
    behavior_asset_free(linked);
    linked = behavior_asset_link_libraries(asset, catalog, 1, error, sizeof error);
    test_check(!linked && strstr(error, "missing function library"), "missing library blocks linking with an actionable error");
    node *cycle = behavior_asset_add_function_call_node_to_graph(library, function, forward);
    linked = behavior_asset_link_libraries(asset, catalog, 2, error, sizeof error);
    test_check(cycle && !linked && strstr(error, "cyclic"), "cyclic shared libraries fail before execution");
    graph_remove_node(function, cycle);
    node *wait = graph_add_node(function, registry_node_by_name("Delay"));
    test_check(behavior_asset_validate_library(library) == NULL, "function libraries permit activation-local suspension");
    compile_report report;
    compile_report_init(&report);
    test_check(behavior_asset_compile(library, &report) > 0, "pure functions still reject latent operations");
    compile_report_free(&report);
    graph_remove_node(function, wait);
    node *owner = graph_add_node(function, &NODE_Self);
    test_check(behavior_asset_validate_library(library) != NULL, "library functions require an explicit owner parameter");
    graph_remove_node(function, owner);
    ecs_world *ecs = ecs_world_new();
    game_live_world *world = game_live_world_new(ecs);
    game_live_definition *shared = game_live_definition_new(library->name, library);
    game_live_definition *forwarding = game_live_definition_new(wrapper->name, wrapper);
    game_live_definition *consumer = game_live_definition_new(asset->name, asset);
    test_check(
        game_live_add_definition(world, shared) == 0 &&
            game_live_add_definition(world, forwarding) == 0 &&
            game_live_add_definition(world, consumer) == 0,
        "live catalog registers libraries before their linked consumers"
    );
    ecs_entity entity = ecs_world_create_entity(ecs);
    test_check(
        game_behavior_attach(world->behaviors, entity, consumer->name) == 0,
        "a live entity can execute a shared library consumer"
    );
    game_prototype_registry registry = { 0 };
    game_prototype_preview *preview = game_prototype_preview_new(&registry);
    test_check(
        game_prototype_preview_register_behavior(preview, shared->name, shared->working) == 0 &&
            game_prototype_preview_register_behavior(
                preview,
                forwarding->name,
                forwarding->working
            ) == 0 &&
            game_prototype_preview_register_behavior(
                preview,
                consumer->name,
                consumer->working
            ) == 0,
        "isolated preview owns the shared function catalog"
    );
    game_prototype *prototype = game_prototype_load(
        "{\"name\":\"LibraryActor\",\"components\":{},\"graphs\":["
        "\"LibraryConsumer\"]}",
        &registry,
        error,
        sizeof error
    );
    test_check(
        prototype &&
            game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0,
        "isolated prototype launches a linked library behavior"
    );
    behavior_asset *previous = consumer->applied;
    graph_set_pin_default(forward, inner, 0, 0, &(pin_value){ .type = PIN_I32, .i = 43 });
    game_live_mark_dirty(forwarding);
    test_check(
        game_live_preview(world, forwarding, error, sizeof error) == 0 &&
            consumer->applied != previous,
        "editing a shared library updates its dependent behavior snapshot"
    );
    game_behavior_dispatch(world->behaviors, entity, "Test", (flow_inputs){ 0 }, &flow_error);
    behavior_instance
        *instance = game_behavior_instance(world->behaviors, entity, consumer->name);
    behavior_instance_get_variable(instance, result_id, &result_value);
    test_check(result_value.i == 43, "existing instances execute the updated library calculation");
    test_check(
        game_prototype_preview_prepare_library(
            preview,
            forwarding->name,
            forwarding->working,
            error,
            sizeof error
        ) == 0 &&
            game_prototype_preview_update_behavior(
                preview,
                forwarding->name,
                forwarding->working,
                false,
                error,
                sizeof error
            ) == 0,
        "isolated preview preflights and applies dependent library edits"
    );
    behavior_instance
        *preview_instance = game_prototype_preview_instance(preview, consumer->name);
    behavior_instance_dispatch(preview_instance, "Test", (flow_inputs){ 0 }, &flow_error);
    behavior_instance_get_variable(preview_instance, result_id, &result_value);
    test_check(result_value.i == 43, "scene and prototype instances execute the same updated library");
    game_prototype_preview_free(preview);
    game_prototype_free(prototype);
    previous = consumer->applied;
    graph_add_node(forward, registry_node_by_name("Delay"));
    game_live_mark_dirty(forwarding);
    test_check(
        game_live_preview(world, forwarding, error, sizeof error) < 0 &&
            consumer->applied == previous,
        "an invalid library edit preserves every applied dependent snapshot"
    );
    game_live_world_free(world);
    ecs_world_free(ecs);
}

static void macro_contract_checks()
{
    behavior_asset *asset = behavior_asset_new("MacroPaths");
    uint64_t result_id = behavior_asset_add_variable(asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                             ->id;
    graph *macro = behavior_asset_add_macro(asset, "Choose Path");
    node *entry = graph_node_by_id(macro, macro->entry_id);
    node *ret = graph_node_by_id(macro, macro->return_id);
    pin_spec path = { .name = "Alternate", .type = PIN_FLOW, .kind = PIN_EXEC };
    graph_add_pin(macro, entry, 1, &path);
    graph_add_pin(macro, ret, 0, &path);
    node *delay = graph_add_node(macro, registry_node_by_name("Delay"));
    graph_set_pin_default(macro, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1 });
    graph_connect_exec(macro, entry, 0, ret, 0, 0);
    graph_connect_exec(macro, entry, 1, delay, 0, 0);
    graph_connect_exec(macro, delay, 0, ret, 1, 0);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, event, "Test");
    node *call = behavior_asset_add_macro_call_node(asset, macro);
    node *other = behavior_asset_add_macro_call_node(asset, macro);
    node *set = behavior_asset_add_set_node(asset, result_id);
    graph_set_pin_default(asset->graph, set, 0, 1, &(pin_value){ .type = PIN_I32, .i = 7 });
    graph_connect_exec(asset->graph, event, 0, call, 1, 0);
    graph_connect_exec(asset->graph, call, 1, set, 0, 0);
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(asset, NULL, 0, error, sizeof error);
    test_check(linked && acount(linked->graphs) == 4, "each macro call expands into an independent runtime graph");
    node *expanded = linked ? graph_node_by_id(linked->graph, call->id) : NULL;
    node *expanded_other = linked ? graph_node_by_id(linked->graph, other->id) : NULL;
    test_check(
        expanded && expanded_other && expanded->reference_id != expanded_other->reference_id,
        "macro calls own distinct expanded node identities"
    );
    behavior_instance *bi = linked ? behavior_instance_create(linked, NULL, NULL, NULL)
                                   : NULL;
    flow_error flow_error = { 0 };
    flow_run *run = bi ? behavior_instance_dispatch(bi, "Test", (flow_inputs){ 0 }, &flow_error)
                       : NULL;
    test_check(run && flow_run_status(run) == FLOW_SUSPENDED, "alternate macro entry selects its latent path");
    if (bi) {
        behavior_instance_tick(bi, 1);
    }
    pin_value value = { 0 };
    if (bi) {
        behavior_instance_get_variable(bi, result_id, &value);
    }
    test_check(value.i == 7 && run && flow_run_status(run) == FLOW_COMPLETED, "macro resumes through the selected execution exit");
    behavior_instance_destroy(bi);
    behavior_asset_free(linked);
    behavior_asset_add_macro_call_node_to_graph(asset, macro, macro);
    linked = behavior_asset_link_libraries(asset, NULL, 0, error, sizeof error);
    test_check(!linked && strstr(error, "recursive"), "recursive macro expansion fails before execution");
    behavior_asset_free(asset);
}

static void foreach_watch_checks()
{
    for (int breaking = 0; breaking < 2; breaking++) {
        graph g;
        graph_init(&g);
        node *event = graph_add_node(&g, &NODE_Event);
        assert(graph_set_event_name(&g, event, "Watch") == 0);
        assert(
            graph_add_pin(
                &g,
                event,
                true,
                &(pin_spec){ .name = "Values", .kind = PIN_DATA, .type = PIN_STR, .container = PIN_CONTAINER_ARRAY }
            ) == 1
        );
        node *loop = graph_add_node(&g, registry_node_by_name(breaking ? "ForEachWithBreak" : "ForEach"));
        node *delay = graph_add_node(&g, registry_node_by_name("Delay"));
        node *completed_delay = graph_add_node(&g, registry_node_by_name("Delay"));
        node *print = graph_add_node(&g, registry_node_by_name("Print String"));
        assert(
            graph_set_pin_default(&g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .1 }) == 0
        );
        assert(
            graph_set_pin_default(&g, completed_delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .1 }) == 0
        );
        assert(graph_connect_exec(&g, event, 0, loop, 0, 0) == 0);
        assert(graph_connect_data(&g, event, 1, loop, breaking ? 2 : 1) == 0);
        assert(graph_connect_exec(&g, loop, 0, delay, 0, 0) == 0);
        assert(graph_connect_exec(&g, loop, 3, completed_delay, 0, 0) == 0);
        assert(graph_connect_exec(&g, completed_delay, 0, print, 0, 0) == 0);
        assert(graph_connect_data(&g, loop, 1, print, 1) == 0);
        if (breaking) {
            assert(graph_connect_exec(&g, delay, 0, loop, 1, 0) == 0);
        }
        pin_array *values = pin_array_new(PIN_STR, NULL, 3);
        const char *words[] = { "one", "two", "three" };
        for (int i = 0; i < 3; i++) {
            assert(
                pin_array_push(values, &(pin_value){ .type = PIN_STR, .str = words[i] }) == i
            );
        }
        pin_value input = { .type = PIN_STR,
                            .container = PIN_CONTAINER_ARRAY,
                            .ptr = values };
        flow_error error = { 0 };
        flow_options printing = { .sink = print_sink };
        g_print_count = 0;
        flow_run *run = flow_run_start(&g, event, (flow_inputs){ &input, 1, NULL }, &printing, &error);
        assert(run && flow_run_resume(run) == FLOW_SUSPENDED_RESULT);
        pin_value array_watch, item, index;
        assert(flow_run_watch_input(run, loop, breaking ? 2 : 1, &array_watch) == 0);
        assert(flow_run_watch_output(run, loop, 1, &item) == 0 && !strcmp(item.str, "one"));
        /* Change the producer's live input after ForEach took its iteration
         * snapshot. Continuations must retain the input that started the loop. */
        pin_array *live = flow_run_inputs(run).values[0].ptr;
        live->items[0].str = "changed";
        live->items[2].str = "changed";
        assert(flow_run_tick_at(run, .1) == FLOW_SUSPENDED_RESULT);
        assert(flow_run_watch_input(run, loop, breaking ? 2 : 1, &array_watch) == 0);
        const pin_array *snapshot = array_watch.ptr;
        test_check(
            snapshot->count == 3 && !strcmp(snapshot->items[0].str, "one") &&
                !strcmp(snapshot->items[2].str, "three"),
            "ForEach continuation and Break preserve the original input watch "
            "after the producer mutates"
        );
        assert(flow_run_watch_output(run, loop, 1, &item) == 0);
        assert(flow_run_watch_output(run, loop, 2, &index) == 0);
        test_check(
            !strcmp(item.str, breaking ? "one" : "two") &&
                index.i == (breaking ? 0 : 1),
            "suspended ForEach watches show the latest element and index"
        );
        if (!breaking) {
            assert(flow_run_tick_at(run, .2) == FLOW_SUSPENDED_RESULT);
            assert(flow_run_tick_at(run, .31) == FLOW_SUSPENDED_RESULT);
        }
        assert(flow_run_watch_output(run, loop, 1, &item) == 0);
        test_check(flow_run_status(run) == FLOW_SUSPENDED && !strcmp(item.str, breaking ? "one" : "three"), "the final string element watch survives release of the iteration array");
        assert(g_print_count == 0 && flow_run_tick_at(run, .5) == FLOW_OK);
        test_check(
            flow_run_status(run) == FLOW_COMPLETED && g_print_count == 1 &&
                !strcmp(g_print_messages[0], breaking ? "one" : "three"),
            "delayed Completed consumers read the owned final ForEach element "
            "after normal completion and Break"
        );
        assert(flow_run_recycle(run) == FLOW_OK);
        test_check(
            flow_run_watch_input(run, loop, breaking ? 2 : 1, &array_watch) != 0 &&
                flow_run_watch_output(run, loop, 1, &item) != 0,
            "recycling clears watch values and their lookup index"
        );
        assert(
            flow_run_start_reuse(run, &g, event, (flow_inputs){ &input, 1, NULL }, &printing, &error) == run
        );
        assert(flow_run_resume(run) == FLOW_SUSPENDED_RESULT);
        assert(flow_run_watch_output(run, loop, 1, &item) == 0 && !strcmp(item.str, "one"));
        flow_run_free(run);
        pin_array_free(values);
        graph_free(&g);
    }
}

static void flow_contract_checks()
{
    foreach_watch_checks();
    for (int variant = 0; variant < 3; variant++) {
        graph g;
        graph_init(&g);
        node *event = graph_add_node(&g, &NODE_Event);
        node *loop = graph_add_node(
            &g,
            registry_node_by_name(variant == 2 ? "ForEachWithBreak" : "ForLoopWithBreak")
        );
        node *body = graph_add_node(&g, &g_sink_a_desc);
        node *done = graph_add_node(&g, &g_sink_done_desc);
        graph_set_event_name(&g, event, "Test");
        graph_connect_exec(&g, event, 0, loop, 0, 0);
        graph_connect_exec(&g, loop, 0, body, 0, 0);
        graph_connect_exec(&g, loop, variant == 2 ? 3 : 2, done, 0, 0);
        if (variant == 0) {
            node *stop = graph_add_node(&g, registry_node_by_name("Break"));
            graph_connect_exec(&g, body, 0, stop, 0, 0);
        } else {
            graph_connect_exec(&g, body, 0, loop, 1, 0);
        }
        if (variant == 2) {
            node *array = graph_add_node(&g, registry_node_by_name("Make Array"));
            graph_set_pin_type(&g, array, 0, 0, PIN_I32, "int");
            graph_connect_data(&g, array, 0, loop, 2);
        } else {
            graph_set_pin_default(&g, loop, 0, 3, &(pin_value){ .type = PIN_I32, .i = 8 });
        }
        flow_log_reset();
        flow_error error = { 0 };
        flow_run *run = graph_run_event(&g, "Test", (flow_inputs){ 0 }, &error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "AD") == 0,
            "loop Break exits after one body and reaches Completed"
        );
        flow_run_free(run);
        graph_free(&g);
    }
    behavior_asset *asset = behavior_asset_new("WhileContract");
    behavior_variable *condition = behavior_asset_add_variable(
        asset,
        "Condition",
        PIN_BOOL,
        "bool",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_BOOL, .b = true },
        ""
    );
    node *event = graph_add_node(asset->graph, &NODE_Event);
    node *loop = graph_add_node(asset->graph, registry_node_by_name("WhileLoop"));
    node *get = behavior_asset_add_get_node(asset, condition->id);
    node *set = behavior_asset_add_set_node(asset, condition->id);
    node *done = graph_add_node(asset->graph, &g_sink_done_desc);
    graph_set_event_name(asset->graph, event, "Test");
    graph_connect_exec(asset->graph, event, 0, loop, 0, 0);
    graph_connect_data(asset->graph, get, 0, loop, 1);
    graph_connect_exec(asset->graph, loop, 0, set, 0, 0);
    graph_connect_exec(asset->graph, loop, 1, done, 0, 0);
    graph_set_pin_default(asset->graph, set, 0, 1, &(pin_value){ .type = PIN_BOOL, .b = false });
    behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
    flow_error error = { 0 };
    flow_log_reset();
    flow_run *run = behavior_instance_dispatch(bi, "Test", (flow_inputs){ 0 }, &error);
    test_check(
        run && flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "D") == 0,
        "WhileLoop reevaluates changed state and completes without a backedge"
    );
    behavior_instance_destroy(bi);
    behavior_asset_free(asset);

    asset = behavior_asset_new("RetriggerContract");
    event = graph_add_node(asset->graph, &NODE_Event);
    node *delay = graph_add_node(asset->graph, registry_node_by_name("Retriggerable Delay"));
    done = graph_add_node(asset->graph, &g_sink_done_desc);
    graph_set_event_name(asset->graph, event, "Test");
    graph_connect_exec(asset->graph, event, 0, delay, 0, 0);
    graph_connect_exec(asset->graph, delay, 0, done, 0, 0);
    graph_set_pin_default(asset->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1 });
    bi = behavior_instance_create(asset, NULL, NULL, NULL);
    flow_log_reset();
    behavior_instance_dispatch(bi, "Test", (flow_inputs){ 0 }, &error);
    behavior_instance_tick(bi, 0.5);
    behavior_instance_dispatch(bi, "Test", (flow_inputs){ 0 }, &error);
    behavior_instance_tick(bi, 1.0);
    test_check(g_flow_log_count == 0, "retrigger postpones the original completion");
    behavior_instance_tick(bi, 1.5);
    test_check(strcmp(g_flow_log, "D") == 0, "retrigger emits one completion at the updated deadline");
    behavior_instance_destroy(bi);
    behavior_asset_free(asset);
    asset = behavior_asset_new("MultiGateContract");
    event = graph_add_node(asset->graph, &NODE_Event);
    node *reset = graph_add_node(asset->graph, &NODE_Event);
    node *gate = graph_add_node(asset->graph, registry_node_by_name("MultiGate"));
    node *a = graph_add_node(asset->graph, &g_sink_a_desc);
    node *b = graph_add_node(asset->graph, &g_sink_b_desc);
    graph_set_event_name(asset->graph, event, "Test");
    graph_set_event_name(asset->graph, reset, "Reset");
    graph_connect_exec(asset->graph, event, 0, gate, 0, 0);
    graph_connect_exec(asset->graph, reset, 0, gate, 1, 0);
    graph_connect_exec(asset->graph, gate, 0, a, 0, 0);
    graph_connect_exec(asset->graph, gate, 1, b, 0, 0);
    graph_set_pin_default(asset->graph, gate, 0, 3, &(pin_value){ .type = PIN_BOOL, .b = true });
    graph_set_pin_default(asset->graph, gate, 0, 4, &(pin_value){ .type = PIN_I32, .i = -1 });
    bi = behavior_instance_create(asset, NULL, NULL, NULL);
    bool varied = false;
    for (int round = 0; round < 32; round++) {
        flow_log_reset();
        behavior_instance_dispatch(bi, "Reset", (flow_inputs){ 0 }, &error);
        for (int i = 0; i < 3; i++) {
            behavior_instance_dispatch(bi, "Test", (flow_inputs){ 0 }, &error);
        }
        test_check(
            strcmp(g_flow_log, "AB") == 0 || strcmp(g_flow_log, "BA") == 0,
            "random MultiGate visits every output once then closes"
        );
        varied |= g_flow_log[0] == 'B';
    }
    test_check(varied, "MultiGate Random changes traversal order");
    behavior_instance_destroy(bi);
    behavior_asset_free(asset);
}

static void runtime_lookup_edits()
{
    graph g;
    graph_init(&g);
    node *a = graph_add_node(&g, registry_node_by_name("Integer"));
    node *b = graph_add_node(&g, registry_node_by_name("Integer"));
    runtime_store *first = runtime_store_create(&g);
    runtime_store *second = runtime_store_create(&g);
    runtime_node *kept = runtime_store_node(first, b);
    runtime_node_outputs(kept)[0] = (pin_value){ .type = PIN_I32, .i = 73 };
    g.id_counter += 10000;
    node *unvisited = graph_add_node(&g, registry_node_by_name("Integer"));
    node *sparse = graph_add_node(&g, registry_node_by_name("Integer"));
    runtime_store_node(first, sparse);
    test_check(
        runtime_store_node_const(first, b) == kept &&
            runtime_node_outputs(kept)[0].i == 73 &&
            runtime_store_node_const(first, unvisited) == NULL,
        "sparse runtime lookup growth preserves existing values and "
        "initializes new slots"
    );
    test_check(runtime_store_node_const(second, b) == NULL, "runtime lookup state remains private to each activation");
    runtime_store_node(first, a);
    graph_remove_node(&g, a);
    runtime_store_reset(first);
    test_check(runtime_store_node_const(first, b) == kept, "runtime lookup preserves surviving state after node removal");
    node *replacement = graph_add_node(&g, registry_node_by_name("Integer"));
    test_check(runtime_store_node_const(first, replacement) == NULL, "new graph nodes never inherit removed runtime state");
    runtime_store_node(first, replacement);
    b->id = graph_alloc_id(&g);
    graph_reindex(&g);
    runtime_store_reset(first);
    test_check(
        runtime_store_node_const(first, b) == NULL &&
            runtime_node_outputs(runtime_store_node(first, b))[0].i == 0,
        "graph ID reassignment invalidates the old runtime lookup"
    );
    runtime_store_free(second);
    runtime_store_free(first);
    graph_free(&g);
}

void run_runtime_execution_checks()
{
    runtime_lookup_edits();
    flow_contract_checks();
    library_contract_checks();
    macro_contract_checks();
    flow_error dispatch_error = { 0 };
    /* 15. DATA/EXEC edge distinction, explicit order, and flow registry. */
    {
        graph ge;
        graph_init(&ge);
        const node_desc *event_desc = registry_node_by_name("Event");
        const node_desc *branch_desc = registry_node_by_name("Branch");
        const node_desc *bool_desc = registry_node_by_name("Bool");
        test_check(event_desc && branch_desc && bool_desc, "Branch flow built-in is registered");
        node *event = graph_add_node(&ge, event_desc);
        node *branch = graph_add_node(&ge, branch_desc);
        node *truth = graph_add_node(&ge, bool_desc);
        test_check(graph_set_event_name(&ge, event, "EdgeTest") == 0, "event name is graph metadata");
        test_check(graph_connect_exec(&ge, event, 0, branch, 0, 7) == 0, "exec edge connects");
        node *fanout = graph_add_node(&ge, &g_sink_a_desc);
        test_check(graph_connect_exec(&ge, event, 0, fanout, 0, 0) != 0, "ordinary EXEC output rejects ambiguous fan-out");
        test_check(graph_connect_data(&ge, truth, 0, branch, 1) == 0, "data edge connects");
        pin_value true_value = { .type = PIN_BOOL, .b = true };
        graph_set_pin_default(&ge, truth, 0, 0, &true_value);
        test_check(acount(ge.edges) == 2 && ge.edges[0].kind != ge.edges[1].kind, "data and exec edges remain distinct");
        node *src = NULL;
        int src_pin = -1;
        int order = -1;
        test_check(
            graph_exec_input_source(&ge, branch, 0, &src, &src_pin, &order) &&
                src == event && src_pin == 0 && order == 7,
            "exec input source preserves order"
        );
        node *target = NULL;
        int target_pin = -1;
        test_check(
            graph_exec_target_at(&ge, event, 0, 7, &target, &target_pin) &&
                target == branch && target_pin == 0,
            "exec target lookup preserves order"
        );
        node *event_merge = graph_add_node(&ge, event_desc);
        test_check(
            graph_connect_exec(&ge, event_merge, 0, branch, 0, 8) == 0 &&
                acount(ge.edges) == 3,
            "exec input accepts multiple incoming drivers"
        );
        test_check(graph_connect_data(&ge, event, 0, branch, 1) != 0, "exec output cannot become a data edge");
        graph_free(&ge);
    }

    /* 16. Event -> Branch true/false and named dispatch errors. */
    {
        graph gf;
        graph_init(&gf);
        node *event = graph_add_node(&gf, registry_node_by_name("Event"));
        node *branch = graph_add_node(&gf, registry_node_by_name("Branch"));
        node *condition = graph_add_node(&gf, registry_node_by_name("Bool"));
        node *yes = graph_add_node(&gf, &g_sink_a_desc);
        node *no = graph_add_node(&gf, &g_sink_b_desc);
        test_check(graph_set_event_name(&gf, event, "Choose") == 0, "named event configured");
        test_check(graph_connect_exec(&gf, event, 0, branch, 0, 0) == 0, "Event -> Branch exec");
        test_check(graph_connect_data(&gf, condition, 0, branch, 1) == 0, "Branch condition data");
        test_check(graph_connect_exec(&gf, branch, 0, yes, 0, 0) == 0, "Branch true edge");
        test_check(graph_connect_exec(&gf, branch, 1, no, 0, 0) == 0, "Branch false edge");
        graph_set_pin_default(&gf, condition, 0, 0, &(pin_value){ .type = PIN_BOOL, .b = true });
        flow_log_reset();
        flow_run *run = graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "A") == 0,
            "named event takes Branch true"
        );
        test_check(run != NULL, "named event creates a flow run");
        test_check(flow_run_status(run) == FLOW_COMPLETED, "completed run reports one state");
        int traversal_count = 0;
        const flow_traversal *traversals = flow_run_traversals(run, &traversal_count);
        test_check(
            traversal_count >= 3 && traversals[0].kind == GRAPH_EDGE_EXEC &&
                traversals[1].kind == GRAPH_EDGE_DATA &&
                traversals[2].kind == GRAPH_EDGE_EXEC &&
                traversals[0].activation <= traversals[1].activation &&
                traversals[1].activation <= traversals[2].activation,
            "traversed exec path is identifiable in order"
        );
        test_check(
            traversals[0].timestamp <= traversals[1].timestamp &&
                traversals[1].timestamp <= traversals[2].timestamp,
            "traversal records carry ordered runtime timestamps"
        );
        flow_run_stop(run);
        int stopped_count = 0;
        (void)flow_run_traversals(run, &stopped_count);
        test_check(stopped_count == traversal_count, "stopped flow records can settle visually");
        flow_run_free(run);
        graph_set_pin_default(&gf, condition, 0, 0, &(pin_value){ .type = PIN_BOOL, .b = false });
        flow_log_reset();
        run = graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "B") == 0,
            "named event takes Branch false"
        );
        flow_run_free(run);
        test_check(
            graph_run_event(&gf, "Missing", (flow_inputs){ 0 }, &dispatch_error) == NULL &&
                dispatch_error.code == FLOW_ERR_UNKNOWN_EVENT,
            "unknown event reports through the dispatch out-parameter"
        );
        node *duplicate = graph_add_node(&gf, registry_node_by_name("Event"));
        test_check(graph_set_event_name(&gf, duplicate, "Choose") == 0, "duplicate event configured");
        test_check(
            graph_run_event(&gf, "Choose", (flow_inputs){ 0 }, &dispatch_error) == NULL &&
                dispatch_error.code == FLOW_ERR_DUPLICATE_EVENT,
            "duplicate event uses dispatch error"
        );
        graph_free(&gf);
    }

    /* 17. Sequence executes outputs in order and stops on the first error. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *sequence = graph_add_node(&gs, registry_node_by_name("Sequence"));
        pin_spec seq_extra = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
        if (sequence) {
            graph_add_pin(&gs, sequence, 1, &seq_extra);
            graph_add_pin(&gs, sequence, 1, &seq_extra);
        }
        node *a = graph_add_node(&gs, &g_sink_a_desc);
        node *b = graph_add_node(&gs, &g_sink_b_desc);
        node *c = graph_add_node(&gs, &g_sink_c_desc);
        test_check(graph_set_event_name(&gs, event, "Sequence") == 0, "sequence event configured");
        test_check(graph_connect_exec(&gs, event, 0, sequence, 0, 0) == 0, "event -> sequence");
        test_check(
            graph_connect_exec(&gs, sequence, 0, a, 0, 9) == 0 &&
                graph_connect_exec(&gs, sequence, 1, b, 0, 4) == 0 &&
                graph_connect_exec(&gs, sequence, 2, c, 0, 1) == 0,
            "sequence ordered outputs connect"
        );
        flow_log_reset();
        flow_run *run = graph_run_event(&gs, "Sequence", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "ABC") == 0,
            "Sequence order is 0,1,2,3"
        );
        int traversal_count = 0;
        (void)flow_run_traversals(run, &traversal_count);
        test_check(traversal_count == 4, "Sequence records each traversed exec edge");
        flow_run_free(run);
        graph_free(&gs);

        graph capped;
        graph_init(&capped);
        node *cap_event = graph_add_node(&capped, registry_node_by_name("Event"));
        node *cap_sequence = graph_add_node(&capped, registry_node_by_name("Sequence"));
        node *cap_sink = graph_add_node(&capped, &g_sink_a_desc);
        graph_set_event_name(&capped, cap_event, "CappedFlow");
        graph_connect_exec(&capped, cap_event, 0, cap_sequence, 0, 0);
        for (int i = 2; i < 300; i++) {
            pin_spec extra = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
            graph_add_pin(&capped, cap_sequence, 1, &extra);
        }
        for (int i = 0; i < cap_sequence->output_count; i++) {
            graph_connect_exec(&capped, cap_sequence, i, cap_sink, 0, i);
        }
        run = graph_run_event(&capped, "CappedFlow", (flow_inputs){ 0 }, &dispatch_error);
        traversal_count = 0;
        const flow_traversal *bounded = flow_run_traversals(run, &traversal_count);
        test_check(traversal_count == 256, "traversal records stay within their bound");
        bool chronological = true;
        for (int i = 0; i < traversal_count; i++) {
            /* Sequence schedules its stack in reverse output order. */
            int output = traversal_count - i - 1;
            chronological &= bounded[i].from_pin_id ==
                             cap_sequence->output_pins[output].id;
        }
        test_check(chronological, "bounded traversal history retains the latest edges in recording order");
        flow_run_free(run);
        graph_free(&capped);

        graph seven;
        graph_init(&seven);
        node *seven_event = graph_add_node(&seven, registry_node_by_name("Event"));
        node *seven_sequence = graph_add_node(&seven, registry_node_by_name("Sequence"));
        pin_spec seven_pin = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
        while (seven_sequence && seven_sequence->output_count < 7) {
            graph_add_pin(&seven, seven_sequence, 1, &seven_pin);
        }
        node *seven_sinks[7];
        for (int i = 0; i < 7; i++) {
            seven_sinks[i] = graph_add_node(&seven, &g_sink_a_desc);
        }
        graph_set_event_name(&seven, seven_event, "Seven");
        graph_connect_exec(&seven, seven_event, 0, seven_sequence, 0, 0);
        for (int i = 0; i < 7; i++) {
            graph_connect_exec(&seven, seven_sequence, i, seven_sinks[i], 0, i);
        }
        flow_log_reset();
        run = graph_run_event(&seven, "Seven", (flow_inputs){ 0 }, &dispatch_error);
        test_check(seven_sequence && seven_sequence->output_count == 7, "Sequence extends from its default of two outputs");
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_log_count == 7,
            "seven Sequence outputs all fire in ascending order"
        );
        flow_run_free(run);
        graph_free(&seven);

        graph ge;
        graph_init(&ge);
        event = graph_add_node(&ge, registry_node_by_name("Event"));
        sequence = graph_add_node(&ge, registry_node_by_name("Sequence"));
        if (sequence) {
            graph_add_pin(&ge, sequence, 1, &seq_extra);
            graph_add_pin(&ge, sequence, 1, &seq_extra);
        }
        a = graph_add_node(&ge, &g_sink_a_desc);
        node *error_node = graph_add_node(&ge, &g_sink_error_desc);
        c = graph_add_node(&ge, &g_sink_c_desc);
        graph_set_event_name(&ge, event, "Stop");
        graph_connect_exec(&ge, event, 0, sequence, 0, 0);
        graph_connect_exec(&ge, sequence, 0, a, 0, 0);
        graph_connect_exec(&ge, sequence, 1, error_node, 0, 0);
        graph_connect_exec(&ge, sequence, 2, c, 0, 0);
        flow_log_reset();
        run = graph_run_event(&ge, "Stop", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_FAILED &&
                strcmp(g_flow_log, "A") == 0 && flow_run_error(run)->node == error_node,
            "Sequence stops immediately on error"
        );
        test_check(
            flow_run_error(run)->node == error_node && flow_run_error(run)->message,
            "structured run errors identify the responsible node and message"
        );
        flow_run_free(run);
        graph_free(&ge);
    }

    /* 18. Event parameter pins carry ordered values and preserve identity when moved. */
    {
        graph gp;
        graph_init(&gp);
        node *event = graph_add_node(&gp, registry_node_by_name("Event"));
        node *sink = graph_add_node(&gp, &g_parameter_sink_desc);
        pin_spec integer = { "Count", PIN_I32, "int", PIN_DATA };
        pin_spec flag = { "Enabled", PIN_BOOL, "bool", PIN_DATA };
        pin_spec text = { "Label", PIN_STR, "const char *", PIN_DATA };
        graph_add_pin(&gp, event, 1, &integer);
        graph_add_pin(&gp, event, 1, &flag);
        graph_add_pin(&gp, event, 1, &text);
        pin_value authored_text = { .type = PIN_STR, .str = "authored" };
        graph_set_pin_default(&gp, event, 1, 3, &authored_text);
        graph_connect_exec(&gp, event, 0, sink, 0, 0);
        graph_connect_data(&gp, event, 1, sink, 1);
        graph_connect_data(&gp, event, 2, sink, 2);
        graph_connect_data(&gp, event, 3, sink, 3);
        uint64_t moved_id = event->output_pins[3].id;
        pin_value first_inputs[] = {
            { .type = PIN_I32, .i = 7 },
            { .type = PIN_BOOL, .b = true },
            { .type = PIN_STR, .str = "first" },
        };
        flow_error parameter_error;
        flow_run *parameter_run = flow_run_start(
            &gp,
            event,
            (flow_inputs){ first_inputs, 3, NULL },
            NULL,
            &parameter_error
        );
        (void)flow_run_resume(parameter_run);
        test_check(
            parameter_run && flow_run_status(parameter_run) == FLOW_COMPLETED &&
                g_parameter_integer == 7 && g_parameter_flag &&
                strcmp(g_parameter_text, "first") == 0,
            "event parameters deliver typed values in order"
        );
        flow_run_free(parameter_run);

        test_check(
            graph_move_pin(&gp, event, 1, 3, 1) == 1 &&
                event->output_pins[1].id == moved_id &&
                event->output_pins[1].default_value.str &&
                strcmp(event->output_pins[1].default_value.str, "authored") == 0,
            "moving a pin preserves its ID, authored value, and position"
        );
        node *source = NULL;
        int source_pin = -1;
        int moved_output = -1;
        const pin_spec *resolved = graph_pin_by_id(event, moved_id, &moved_output, &source_pin);
        test_check(
            resolved && moved_output && source_pin == 1 &&
                graph_input_source(&gp, sink, 3, &source, &source_pin) == 1 &&
                source == event && source_pin == 1,
            "moved pin ID keeps its attached edge resolved"
        );

        pin_value reordered_inputs[] = {
            { .type = PIN_STR, .str = "second" },
            { .type = PIN_I32, .i = 11 },
            { .type = PIN_BOOL, .b = false },
        };
        parameter_run = flow_run_start(
            &gp,
            event,
            (flow_inputs){ reordered_inputs, 3, NULL },
            NULL,
            &parameter_error
        );
        (void)flow_run_resume(parameter_run);
        test_check(
            parameter_run && flow_run_status(parameter_run) == FLOW_COMPLETED &&
                g_parameter_integer == 11 && !g_parameter_flag &&
                strcmp(g_parameter_text, "second") == 0,
            "reordering parameters reorders delivered values"
        );
        flow_run_free(parameter_run);
        graph_free(&gp);
    }

    /* 19. ForLoop uses inclusive forward and reverse ranges. */
    {
        graph gl;
        graph_init(&gl);
        node *event = graph_add_node(&gl, registry_node_by_name("Event"));
        node *loop = graph_add_node(&gl, registry_node_by_name("ForLoop"));
        node *body = graph_add_node(&gl, &g_sink_body_desc);
        node *done = graph_add_node(&gl, &g_sink_done_desc);
        graph_set_event_name(&gl, event, "Loop");
        graph_connect_exec(&gl, event, 0, loop, 0, 0);
        graph_connect_exec(&gl, loop, 0, body, 0, 0);
        graph_connect_data(&gl, loop, 1, body, 1);
        graph_connect_exec(&gl, loop, 2, done, 0, 0);
        graph_set_pin_default(&gl, loop, 0, 1, &(pin_value){ .type = PIN_I32, .i = 2 });
        graph_set_pin_default(&gl, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 4 });
        flow_log_reset();
        flow_run *run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_index_count == 3 &&
                g_flow_indices[0] == 2 && g_flow_indices[1] == 3 &&
                g_flow_indices[2] == 4 && strcmp(g_flow_log, "D") == 0,
            "ForLoop runs inclusive 2..4 then Completed"
        );
        test_check(
            g_flow_index_count == 3 && g_flow_indices[0] == 2 && g_flow_indices[1] == 3,
            "each loop activation evaluates fresh data"
        );
        flow_run_free(run);
        graph_set_pin_default(&gl, loop, 0, 1, &(pin_value){ .type = PIN_I32, .i = 2 });
        graph_set_pin_default(&gl, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 2 });
        flow_log_reset();
        run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_index_count == 1 &&
                g_flow_indices[0] == 2 && strcmp(g_flow_log, "D") == 0,
            "ForLoop executes equal bounds once"
        );
        flow_run_free(run);
        graph_set_pin_default(&gl, loop, 0, 1, &(pin_value){ .type = PIN_I32, .i = 4 });
        graph_set_pin_default(&gl, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = 2 });
        flow_log_reset();
        run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED && g_flow_index_count == 3 &&
                g_flow_indices[0] == 4 && g_flow_indices[1] == 3 &&
                g_flow_indices[2] == 2 && strcmp(g_flow_log, "D") == 0,
            "ForLoop runs inclusive 4..2 in reverse then completes"
        );
        flow_run_free(run);
        test_check(graph_remove_edge(&gl, event, 0, loop, 0) == 0, "removing Event -> ForLoop disconnects the executable path");
        flow_log_reset();
        run = graph_run_event(&gl, "Loop", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                g_flow_index_count == 0 && g_flow_log_count == 0,
            "a disconnected Event does not activate ForLoop"
        );
        flow_run_free(run);
        graph_free(&gl);
    }

    /* 19. Delay yields, resumes from host time, and preserves nested Sequence. */
    {
        graph gd;
        graph_init(&gd);
        node *event = graph_add_node(&gd, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gd, registry_node_by_name("Delay"));
        node *done = graph_add_node(&gd, &g_sink_done_desc);
        graph_set_event_name(&gd, event, "Wait");
        graph_connect_exec(&gd, event, 0, delay, 0, 0);
        graph_connect_exec(&gd, delay, 0, done, 0, 0);
        pin_spec guard_pin = { "Parameter", PIN_I32, "int", PIN_DATA };
        graph_add_pin(&gd, event, 1, &guard_pin);
        graph_add_pin(&gd, event, 1, &guard_pin);
        graph_set_pin_default(&gd, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 5.0 });
        flow_log_reset();
        flow_run *run = graph_run_event(&gd, "Wait", (flow_inputs){ 0 }, &dispatch_error);
        test_check(run && flow_run_status(run) == FLOW_SUSPENDED && g_flow_log_count == 0, "Delay suspends flow run");
        test_check(graph_connect_exec(&gd, event, 0, done, 0, 0) != 0, "paused flow rejects topology mutation");
        test_check(graph_move_pin(&gd, event, 1, 2, 1) != 0, "live run rejects instance-pin reordering");
        test_check(
            flow_run_tick_at(run, 4.0) == FLOW_SUSPENDED_RESULT &&
                flow_run_status(run) == FLOW_SUSPENDED,
            "Delay remains suspended before host deadline"
        );
        test_check(
            flow_run_tick_at(run, 5.0) == FLOW_OK &&
                flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "D") == 0,
            "Delay resumes at host monotonic time"
        );
        test_check(flow_run_status(run) == FLOW_COMPLETED, "host-supplied monotonic time advances the run");
        flow_run_free(run);
        graph_free(&gd);

        graph gt;
        graph_init(&gt);
        node *topology_event = graph_add_node(&gt, registry_node_by_name("Event"));
        node *topology_delay = graph_add_node(&gt, registry_node_by_name("Delay"));
        graph_connect_exec(&gt, topology_event, 0, topology_delay, 0, 0);
        graph_set_pin_default(&gt, topology_delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 10.0 });
        flow_run *topology_run = flow_run_start(&gt, topology_event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(topology_run);
        graph_flow_resume(&gt);
        node *topology_mutation = graph_add_node(&gt, &g_sink_a_desc);
        graph_flow_pause(&gt);
        test_check(
            topology_mutation && topology_run &&
                flow_run_tick_at(topology_run, 1.0) == FLOW_ERR_TOPOLOGY &&
                flow_run_status(topology_run) == FLOW_FAILED &&
                flow_run_error(topology_run)->code == FLOW_ERR_TOPOLOGY,
            "changed topology fails the run"
        );
        flow_run_free(topology_run);
        graph_free(&gt);

        graph gn;
        graph_init(&gn);
        event = graph_add_node(&gn, registry_node_by_name("Event"));
        node *sequence = graph_add_node(&gn, registry_node_by_name("Sequence"));
        delay = graph_add_node(&gn, registry_node_by_name("Delay"));
        node *first = graph_add_node(&gn, &g_sink_a_desc);
        node *second = graph_add_node(&gn, &g_sink_b_desc);
        graph_set_event_name(&gn, event, "Nested");
        graph_connect_exec(&gn, event, 0, sequence, 0, 0);
        graph_connect_exec(&gn, sequence, 0, delay, 0, 0);
        graph_connect_exec(&gn, delay, 0, first, 0, 0);
        graph_connect_exec(&gn, sequence, 1, second, 0, 0);
        graph_set_pin_default(&gn, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 3.0 });
        flow_log_reset();
        run = graph_run_event(&gn, "Nested", (flow_inputs){ 0 }, &dispatch_error);
        test_check(
            run && flow_run_status(run) == FLOW_SUSPENDED && g_flow_log_count == 0,
            "nested Sequence suspends at first Delay"
        );
        flow_run_tick_at(run, 3.0);
        test_check(
            flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "AB") == 0,
            "nested Sequence resumes remaining outputs in order"
        );
        flow_run_free(run);
        graph_free(&gn);
    }

    /* 20. Step budget and v3 persistence. */
    {
        graph gbudget;
        graph_init(&gbudget);
        node *cycle = graph_add_node(&gbudget, &g_sink_a_desc);
        graph_connect_exec(&gbudget, cycle, 0, cycle, 0, 0);
        flow_options options = { .step_budget = 3 };
        flow_run *run = flow_run_start(&gbudget, cycle, (flow_inputs){ 0 }, &options, &dispatch_error);
        test_check(
            run && flow_run_resume(run) == FLOW_ERR_STEP_BUDGET &&
                flow_run_status(run) == FLOW_FAILED && flow_run_steps(run) == 3,
            "flow step budget produces structured failure"
        );
        flow_run_free(run);
        graph_free(&gbudget);

        graph gj;
        graph_init(&gj);
        node *event = graph_add_node(&gj, registry_node_by_name("Event"));
        node *branch = graph_add_node(&gj, registry_node_by_name("Branch"));
        node *condition = graph_add_node(&gj, registry_node_by_name("Bool"));
        graph_set_event_name(&gj, event, "Saved");
        graph_connect_exec(&gj, event, 0, branch, 0, 12);
        graph_connect_data(&gj, condition, 0, branch, 1);
        graph_set_node_presentation(&gj, condition, NODE_PRESENTATION_EXPANDED);
        char *json = graph_save_json(&gj);
        test_check(
            json && strstr(json, "\"version\": 3") &&
                strstr(json, "\"from\": \"1.Then\"") && strstr(json, "\"order\": 12") &&
                strstr(json, "\"reference_name\": \"Saved\""),
            "v3 JSON names flow endpoints by pin"
        );
        graph *loaded = json ? graph_load_json(json) : NULL;
        test_check(
            loaded && acount(loaded->edges) == 2 &&
                loaded->edges[0].kind == GRAPH_EDGE_EXEC &&
                graph_event_name(loaded->nodes[0]) &&
                strcmp(graph_event_name(loaded->nodes[0]), "Saved") == 0,
            "v3 JSON loads flow metadata"
        );
        if (loaded) {
            graph_free(loaded);
            free(loaded);
        }
        free(json);

        node *flow_subset[] = { event, branch };
        char *subset_json = graph_save_nodes_json(&gj, flow_subset, 2);
        graph appended;
        graph_init(&appended);
        test_check(
            subset_json &&
                graph_append_json(&appended, subset_json, 10.0f, 20.0f) == 0 &&
                acount(appended.edges) == 1 && appended.edges[0].kind == GRAPH_EDGE_EXEC &&
                appended.edges[0].exec_order == 12,
            "append remaps ID-based flow edge"
        );
        uint64_t first_copy = appended.nodes[0]->id;
        uint64_t second_copy = appended.nodes[1]->id;
        test_check(
            subset_json &&
                graph_append_json(&appended, subset_json, 20.0f, 30.0f) == 0 &&
                acount(appended.nodes) == 4 && acount(appended.edges) == 2 &&
                appended.nodes[2]->id != first_copy &&
                appended.nodes[3]->id != second_copy &&
                appended.edges[1].from_id == appended.nodes[2]->id &&
                appended.edges[1].to_id == appended.nodes[3]->id,
            "repeated paste allocates fresh IDs and rewires internal "
            "edges"
        );
        graph_free(&appended);
        free(subset_json);
        graph_free(&gj);

        test_check(
            graph_load_json("{\"version\":2,\"nodes\":[],\"edges\":[]}") == NULL &&
                strstr(graph_last_error(), "version"),
            "v2 documents are rejected"
        );
        test_check(
            graph_load_json("{\"nodes\":[],\"edges\":[]}") == NULL &&
                strstr(graph_last_error(), "version"),
            "versionless documents are rejected"
        );
    }

    /* 21. External pause, one-activation stepping, and resume order. */
    {
        graph gp;
        graph_init(&gp);
        node *event = graph_add_node(&gp, registry_node_by_name("Event"));
        node *first = graph_add_node(&gp, &g_sink_a_desc);
        node *second = graph_add_node(&gp, &g_sink_b_desc);
        node *third = graph_add_node(&gp, &g_sink_c_desc);
        graph_connect_exec(&gp, event, 0, first, 0, 0);
        graph_connect_exec(&gp, first, 0, second, 0, 0);
        graph_connect_exec(&gp, second, 0, third, 0, 0);

        flow_run *run = flow_run_start(&gp, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        flow_log_reset();
        test_check(
            run && flow_run_step(run) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED &&
                flow_run_steps(run) == 1 && g_flow_log_count == 0,
            "one step activates exactly one entry node"
        );
        test_check(
            flow_run_pause(run) == FLOW_OK && flow_run_status(run) == FLOW_PAUSED,
            "external pause preserves paused state"
        );
        test_check(
            flow_run_resume(run) == FLOW_OK && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_flow_log, "ABC") == 0,
            "resume continues from the next unactivated node"
        );
        flow_run_free(run);

        flow_log_reset();
        run = flow_run_start(&gp, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        while (run && flow_run_status(run) != FLOW_COMPLETED &&
               flow_run_status(run) != FLOW_FAILED) {
            (void)flow_run_step(run);
        }
        char stepped_log[sizeof g_flow_log];
        snprintf(stepped_log, sizeof stepped_log, "%s", g_flow_log);
        test_check(strcmp(stepped_log, "ABC") == 0, "stepping preserves activation order");
        flow_run_free(run);

        flow_log_reset();
        run = flow_run_start(&gp, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        test_check(strcmp(g_flow_log, stepped_log) == 0, "stepped output matches uninterrupted resume");
        flow_run_free(run);

        graph gpause;
        graph_init(&gpause);
        node *pause_event = graph_add_node(&gpause, registry_node_by_name("Event"));
        node *pause_print = graph_add_node(&gpause, registry_node_by_name("Print String"));
        node *pause_sink = graph_add_node(&gpause, &g_sink_a_desc);
        node *pause_text = make_string(&gpause, "pause");
        graph_connect_exec(&gpause, pause_event, 0, pause_print, 0, 0);
        graph_connect_exec(&gpause, pause_print, 0, pause_sink, 0, 0);
        graph_connect_data(&gpause, pause_text, 0, pause_print, 1);
        flow_options pause_options = { .sink = print_sink };
        flow_log_reset();
        g_print_count = 0;
        run = flow_run_start(&gpause, pause_event, (flow_inputs){ 0 }, &pause_options, &dispatch_error);
        g_pause_target = run;
        (void)flow_run_resume(run);
        test_check(
            run && flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 2 &&
                g_print_count == 1 && g_flow_log_count == 0,
            "external pause request stops between activations"
        );
        (void)flow_run_resume(run);
        test_check(
            flow_run_status(run) == FLOW_COMPLETED && strcmp(g_flow_log, "A") == 0,
            "paused run resumes at the next node"
        );
        flow_run_free(run);
        graph_free(&gpause);
        graph_free(&gp);
    }

    /* 22. Persistent single-step mode crosses a latent suspension. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gs, registry_node_by_name("Delay"));
        node *done = graph_add_node(&gs, &g_sink_done_desc);
        graph_connect_exec(&gs, event, 0, delay, 0, 0);
        graph_connect_exec(&gs, delay, 0, done, 0, 0);
        graph_set_pin_default(&gs, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1.0 });
        flow_options clocked = { .initial_time = 0.0 };
        flow_log_reset();
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        test_check(
            run && flow_run_step(run) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 1,
            "step mode pauses after one activation before Delay"
        );
        test_check(
            flow_run_step(run) == FLOW_SUSPENDED_RESULT &&
                flow_run_status(run) == FLOW_SUSPENDED && flow_run_steps(run) == 2,
            "stepping activates exactly one Delay across suspension"
        );
        test_check(
            flow_run_tick_at(run, 1.0) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 3 &&
                g_flow_log_count == 1 && strcmp(g_flow_log, "D") == 0,
            "deadline tick keeps step mode and pauses after one "
            "activation"
        );
        test_check(
            flow_run_resume(run) == FLOW_OK &&
                flow_run_status(run) == FLOW_COMPLETED && flow_run_steps(run) == 3,
            "explicit Resume clears step mode and continues freely"
        );
        flow_run_free(run);
        graph_free(&gs);
    }

    /* 23. Pausing a suspended run enters the same persistent step mode. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gs, registry_node_by_name("Delay"));
        node *done = graph_add_node(&gs, &g_sink_done_desc);
        graph_connect_exec(&gs, event, 0, delay, 0, 0);
        graph_connect_exec(&gs, delay, 0, done, 0, 0);
        graph_set_pin_default(&gs, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1.0 });
        flow_options clocked = { .initial_time = 0.0 };
        flow_log_reset();
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        (void)flow_run_resume(run);
        test_check(
            run && flow_run_status(run) == FLOW_SUSPENDED &&
                flow_run_pause(run) == FLOW_OK &&
                flow_run_tick_at(run, 1.0) == FLOW_PAUSED_RESULT &&
                flow_run_status(run) == FLOW_PAUSED && flow_run_steps(run) == 3,
            "pausing while SUSPENDED steps once after the deadline"
        );
        test_check(
            flow_run_resume(run) == FLOW_OK && flow_run_status(run) == FLOW_COMPLETED,
            "resumed suspended step completes without another step limit"
        );
        flow_run_free(run);
        graph_free(&gs);
    }

    /* 24. Per-run errors and step counts do not share process-global state. */
    {
        graph gtwo;
        graph_init(&gtwo);
        node *event = graph_add_node(&gtwo, registry_node_by_name("Event"));
        node *error_node = graph_add_node(&gtwo, &g_sink_error_desc);
        graph_connect_exec(&gtwo, event, 0, error_node, 0, 0);
        flow_run *run_a = flow_run_start(&gtwo, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        flow_run *run_b = flow_run_start(&gtwo, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        int error_a = run_a ? flow_run_resume(run_a) : 0;
        int error_b = run_b ? flow_run_resume(run_b) : 0;
        test_check(
            run_a && run_b && error_a == -777 && error_b == -777 &&
                flow_run_steps(run_a) == 2 && flow_run_steps(run_b) == 2 &&
                flow_run_error(run_a) != flow_run_error(run_b),
            "two runs keep independent errors and step counts"
        );
        flow_run_free(run_a);
        flow_run_free(run_b);
        graph_free(&gtwo);
    }

    /* 23. Stop releases graph guards and clears runtime state. */
    {
        graph gr;
        graph_init(&gr);
        node *event = graph_add_node(&gr, registry_node_by_name("Event"));
        node *sink = graph_add_node(&gr, &g_sink_a_desc);
        graph_connect_exec(&gr, event, 0, sink, 0, 0);
        flow_log_reset();
        flow_run *run = flow_run_start(&gr, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        char first_output[sizeof g_flow_log];
        snprintf(first_output, sizeof first_output, "%s", g_flow_log);
        test_check(flow_run_stop(run) == FLOW_OK && flow_run_status(run) == FLOW_IDLE, "stop returns run to idle");
        test_check(
            graph_add_node(&gr, &g_sink_b_desc) != NULL && flow_run_status(run) == FLOW_IDLE,
            "stop clears activation state and releases graph mutation guard"
        );
        flow_run_free(run);
        flow_log_reset();
        run = flow_run_start(&gr, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        test_check(strcmp(first_output, g_flow_log) == 0, "fresh run output matches the first run");
        flow_run_free(run);
        graph_free(&gr);
    }

    /* 24. Stopping a latent run releases the graph without another activation. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gs, registry_node_by_name("Delay"));
        node *done = graph_add_node(&gs, &g_sink_done_desc);
        graph_connect_exec(&gs, event, 0, delay, 0, 0);
        graph_connect_exec(&gs, delay, 0, done, 0, 0);
        graph_set_pin_default(&gs, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 10.0 });
        flow_log_reset();
        flow_options clocked = { .initial_time = 0.0 };
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, &clocked, &dispatch_error);
        (void)flow_run_resume(run);
        test_check(flow_run_status(run) == FLOW_SUSPENDED, "Delay leaves a suspended run");
        test_check(
            flow_run_stop(run) == FLOW_OK &&
                flow_run_status(run) == FLOW_IDLE && g_flow_log_count == 0,
            "stop aborts without activating the continuation"
        );
        test_check(graph_add_node(&gs, &g_sink_a_desc) != NULL, "stopped run leaves graph mutable");
        test_check(graph_add_node(&gs, &g_sink_b_desc) != NULL, "authoring mutation succeeds after suspended run teardown");
        flow_run_free(run);
        graph_free(&gs);
    }

    /* 25. Print String delivers ordered host messages with their origins. */
    {
        graph gp;
        graph_init(&gp);
        node *event = graph_add_node(&gp, registry_node_by_name("Event"));
        node *print_a = graph_add_node(&gp, registry_node_by_name("Print String"));
        node *print_b = graph_add_node(&gp, registry_node_by_name("Print String"));
        node *str_a = make_string(&gp, "first");
        node *str_b = make_string(&gp, "second");
        graph_connect_exec(&gp, event, 0, print_a, 0, 0);
        graph_connect_exec(&gp, print_a, 0, print_b, 0, 0);
        graph_connect_data(&gp, str_a, 0, print_a, 1);
        graph_connect_data(&gp, str_b, 0, print_b, 1);
        memset(g_print_messages, 0, sizeof g_print_messages);
        memset(g_print_origins, 0, sizeof g_print_origins);
        g_print_count = 0;
        flow_options printing = { .sink = print_sink };
        flow_run *run = flow_run_start(&gp, event, (flow_inputs){ 0 }, &printing, &dispatch_error);
        (void)flow_run_resume(run);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                g_print_count == 2 && strcmp(g_print_messages[0], "first") == 0 &&
                strcmp(g_print_messages[1], "second") == 0 &&
                g_print_origins[0] == print_a && g_print_origins[1] == print_b,
            "Print String sends ordered messages and originating nodes"
        );
        test_check(g_print_count == 2 && g_print_origins[0] == print_a, "host sink receives output with its origin");
        flow_run_free(run);
        graph_free(&gp);
    }

    /* 26. Struct watches copy out of the run arena before the run is freed. */
    {
        graph gw;
        graph_init(&gw);
        node *event = graph_add_node(&gw, registry_node_by_name("Event"));
        node *watch_node = graph_add_node(&gw, &g_watch_desc);
        graph_connect_exec(&gw, event, 0, watch_node, 0, 0);
        flow_run *run = flow_run_start(&gw, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        pin_value recorded;
        CF_V3 copied = { 0 };
        int watch_result = flow_run_watch_output(run, watch_node, 1, &recorded);
        int copy_result = flow_run_watch_copy_output(run, watch_node, 1, &recorded, &copied, sizeof copied);
        test_check(
            watch_result == 0 && copy_result == 0 &&
                recorded.type == PIN_STRUCT && recorded.ptr != &g_watch_struct &&
                copied.x == 4.0f && copied.y == 5.0f && copied.z == 6.0f,
            "struct watch value is copied out of the run arena"
        );
        test_check(
            flow_run_watch_output(run, watch_node, 1, &recorded) == 0 &&
                recorded.ptr != &g_watch_struct && ((CF_V3 *)recorded.ptr)->x == 4.0f,
            "live run watch reads the recorded execution value"
        );
        flow_run_free(run);
        test_check(copied.x == 4.0f && copied.y == 5.0f && copied.z == 6.0f, "copied struct remains valid after run free");
        run = flow_run_start(&gw, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        test_check(
            flow_run_stop(run) == FLOW_OK &&
                flow_run_watch_output(run, watch_node, 1, &recorded) != 0,
            "Stop clears recorded watch values"
        );
        flow_run_free(run);
        graph_free(&gw);
    }

    /* 27. Serialization contains authored graph data, never run state. */
    {
        graph gs;
        graph_init(&gs);
        node *event = graph_add_node(&gs, registry_node_by_name("Event"));
        node *delay = graph_add_node(&gs, registry_node_by_name("Delay"));
        graph_connect_exec(&gs, event, 0, delay, 0, 0);
        graph_set_pin_default(&gs, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 2.0 });
        flow_run *run = flow_run_start(&gs, event, (flow_inputs){ 0 }, NULL, &dispatch_error);
        (void)flow_run_resume(run);
        char *json = graph_save_json(&gs);
        graph *loaded = json ? graph_load_json(json) : NULL;
        test_check(
            json && !strstr(json, "\\\"run\\\"") && !strstr(json, "\\\"state\\\"") &&
                loaded && acount(loaded->nodes) == 2,
            "suspended save contains authored data and no run state"
        );
        if (loaded) {
            graph_free(loaded);
            free(loaded);
        }
        free(json);
        flow_run_free(run);
        graph_free(&gs);
    }
}
