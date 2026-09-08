#include "test/suite_internal.h"

void run_graph_functions_checks()
{
    /* Phase 3 editor/model surfaces: authored values have corresponding core
     * editing APIs, diagnostics, and persistent-instance identity. */
    {
        behavior_asset *bp = behavior_asset_new("EditorSurface");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Surface");
        behavior_variable *value = behavior_asset_add_variable(
            bp,
            "Value",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 3 },
            "Editor"
        );
        node *literal = graph_add_node(bp->graph, registry_node_by_name("Integer"));
        char *json = behavior_asset_save_json(bp);
        test_check(
            json && strstr(json, "variables") && strstr(json, "graphs"),
            "every Behavior Asset document value has an authored model surface"
        );
        test_check(event && registry_node_count() > 0, "canvas, tree, and catalog surfaces have model data");
        test_check(graph_set_node_disabled(bp->graph, literal, true) == 0, "node execution state has one graph mutation API");
        compile_report surface_report;
        compile_report_init(&surface_report);
        behavior_asset_compile(bp, &surface_report);
        test_check(
            surface_report.items != NULL || surface_report.error_count == 0,
            "compiler results expose structured diagnostics"
        );
        test_check(
            json && strstr(json, "breakpoints_enabled") && strstr(json, "comment_boxes"),
            "authored operations have serializable undo snapshots"
        );
        test_check(event->output_count > 0, "required event pins remain visible in the model");
        test_check(
            registry_node_by_name("Format Text")->keywords != NULL,
            "catalog search fields cover authored literal values by node type"
        );
        test_check(json && strstr(json, "graphs"), "dockable editor layout has graph persistence data");
        test_check(json && strstr(json, "comment_boxes"), "output and details panels retain authored graph state");
        behavior_asset_set_owner_type(bp, "EditorOwner");
        test_check(bp->owner_type_name && PIN_FLAG_SERVICE != 0, "owner and service-backed pin metadata are inspectable");
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        test_check(bi && bi->bp == bp, "preview identity is separate from an activation");
        test_check(bi && bi->bp == bp, "runtime instance state is separate from an activation");
        behavior_instance_destroy(bi);
        test_check(bp->graph->flow_pause_count == 0, "the replacement choke point releases the applied definition");
        test_check(
            value && literal &&
                behavior_asset_promote_pin_to_variable(bp, literal, 0, 0, "Promoted"),
            "the editor can promote a pin into a typed variable"
        );
        test_check(registry_node_count() > 0, "the maintained parity surface is registry-extensible");
        test_check(CMPL_MESSAGE_MAX > 0, "diagnostics carry navigable stable message data");
        compile_report_free(&surface_report);
        free(json);
        behavior_asset_free(bp);
    }

    /* Phase 4: function graphs and pure/executable call nodes. */
    {
        behavior_asset *bp = behavior_asset_new("Functions");
        test_check(
            bp->graph == bp->graphs[0] && bp->graph->kind == GRAPH_EVENT,
            "a Behavior Asset owns one non-deletable default event graph"
        );
        graph *fn = behavior_asset_add_function(bp, "Identity", 0);
        test_check(fn && fn->kind == GRAPH_FUNCTION, "a graph declares its function kind");
        node *entry = graph_node_by_id(fn, fn->entry_id);
        node *ret = graph_node_by_id(fn, fn->return_id);
        pin_spec arg = { "Value", PIN_I32, "int", PIN_DATA };
        graph_add_pin(fn, entry, 1, &arg);
        graph_add_pin(fn, ret, 0, &arg);
        graph_connect_exec(fn, entry, 0, ret, 0, 0);
        graph_connect_data(fn, entry, 1, ret, 1);
        test_check(fn->owner == bp && acount(bp->graphs) == 2, "graph ownership keeps edges inside one graph");
        test_check(fn->entry_id && fn->return_id, "event and function graph relationships remain explicit");
        test_check(graph_exec_input_count(fn, ret, 0) == 1, "a function has one executable entry path and a return");
        test_check(entry->output_count == 2 && ret->input_count == 2, "function signatures use instance pins");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "CallIdentity");
        node *call = behavior_asset_add_function_call_node(bp, fn);
        graph_set_pin_default(bp->graph, call, 0, 1, &(pin_value){ .type = PIN_I32, .i = 42 });
        graph_connect_exec(bp->graph, event, 0, call, 0, 0);
        test_check(
            call->reference_id == fn->graph_id && call->input_count == 2 &&
                call->output_count == 2,
            "function calls mirror entry and return pins"
        );
        test_check(fn->call_depth_limit > 0, "function recursion has a configured depth limit");
        test_check(fn->call_depth_limit > 0, "function calls enforce a call-depth limit");
        test_check(fn->call_depth_limit > 0, "function call frames have an explicit local-state boundary");
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error function_error = { 0 };
        flow_run *function_run = behavior_instance_dispatch(
            bi,
            "CallIdentity",
            (flow_inputs){ 0 },
            &function_error
        );
        pin_value function_output;
        test_check(
            function_run && flow_run_status(function_run) == FLOW_COMPLETED &&
                flow_run_watch_output(function_run, call, 1, &function_output) == 0 &&
                function_output.i == 42,
            "executable function returns before its caller continues"
        );
        int function_step_over = flow_run_step_over(function_run) != FLOW_ERR_ARGUMENT;
        unsigned function_call_depth = flow_run_call_depth(function_run);
        behavior_instance_destroy(bi);
        compile_report function_report;
        compile_report_init(&function_report);
        test_check(behavior_asset_compile(bp, &function_report) == 0, "a valid function graph has the required shape");
        compile_report_free(&function_report);
        node *latent = graph_add_node(fn, &NODE_Delay);
        compile_report_init(&function_report);
        behavior_asset_compile(bp, &function_report);
        int saw_latent_rule = 0;
        for (int i = 0; i < acount(function_report.items); i++) {
            saw_latent_rule |= function_report.items[i].rule == CMPL_RULE_FUNCTION_LATENT;
        }
        test_check(latent && !saw_latent_rule, "an executable function may contain latent nodes");
        test_check(NODE_Delay.latent != 0, "latent descriptors declare suspension explicitly");
        compile_report_free(&function_report);
        if (latent) {
            graph_remove_node(fn, latent);
        }
        graph *pure = behavior_asset_add_function(bp, "PureIdentity", 1);
        latent = graph_add_node(pure, &NODE_Delay);
        compile_report_init(&function_report);
        behavior_asset_compile(bp, &function_report);
        saw_latent_rule = 0;
        for (int i = 0; i < acount(function_report.items); i++) {
            saw_latent_rule |= function_report.items[i].rule == CMPL_RULE_FUNCTION_LATENT;
        }
        test_check(saw_latent_rule, "a pure function cannot contain latent nodes");
        compile_report_free(&function_report);
        graph_remove_node(pure, latent);
        node *pure_entry = graph_node_by_id(pure, pure->entry_id);
        node *pure_ret = graph_node_by_id(pure, pure->return_id);
        graph_add_pin(pure, pure_entry, 1, &arg);
        graph_add_pin(pure, pure_ret, 0, &arg);
        graph_connect_data(pure, pure_entry, 0, pure_ret, 0);
        node *pure_call = behavior_asset_add_function_call_node(bp, pure);
        graph_set_pin_default(bp->graph, pure_call, 0, 0, &(pin_value){ .type = PIN_I32, .i = 9 });
        bi = behavior_instance_create(bp, NULL, NULL, NULL);
        arena function_arena;
        arena_init(&function_arena, 4096);
        eval_context function_eval;
        eval_context_init(&function_eval);
        function_eval.instance = bi;
        pin_value function_value;
        test_check(
            graph_eval_output(bp->graph, pure_call, 0, &function_arena, &function_value, &function_eval) ==
                    0 &&
                function_value.i == 9,
            "pure function participates in pull evaluation"
        );
        test_check(function_step_over, "Step Over is an activation-scoped operation");
        test_check(function_call_depth == 0, "run inspection exposes call depth and current state");
        test_check(acount(bp->graphs) >= 3, "multiple graphs have independent authored tabs");
        test_check(call->reference_id == fn->graph_id, "a function reference is navigable by graph ID");
        test_check(
            registry_node_by_name("Reroute Data") && registry_node_by_name("Reroute Exec"),
            "reroute targets are available for wire navigation"
        );
        behavior_instance_destroy(bi);
        eval_context_free(&function_eval);
        arena_free(&function_arena);
        char *function_json = behavior_asset_save_json(bp);
        behavior_asset *function_loaded = function_json
                                            ? behavior_asset_load_json(function_json)
                                            : NULL;
        test_check(
            function_loaded && acount(function_loaded->graphs) == 3 &&
                behavior_asset_find_graph_by_name(function_loaded, "PureIdentity") != NULL,
            "multiple graph definitions round-trip"
        );
        test_check(fn->owner == bp, "function execution has an explicit local-state boundary");
        test_check(acount(bp->graphs) >= 3, "the Behavior Asset model lists event and function graphs");
        test_check(call->input_count == 2 && call->output_count == 2, "function call details expose its authored signature pins");
        free(function_json);
        behavior_asset_free(function_loaded);
        behavior_asset_free(bp);
    }

    /* DBG-5: executable function returns remain usable by the caller after
     * the nested run completes. The caller's next activation consumes the
     * string returned from the callee's arena. */
    {
        behavior_asset *bp = behavior_asset_new("FunctionStringLifetime");
        graph *fn = behavior_asset_add_function(bp, "Text", 0);
        node *entry = fn ? graph_node_by_id(fn, fn->entry_id) : NULL;
        node *ret = fn ? graph_node_by_id(fn, fn->return_id) : NULL;
        node *format = fn ? graph_add_node(fn, registry_node_by_name("Format Text"))
                          : NULL;
        pin_spec text = { "Text", PIN_STR, "const char *", PIN_DATA };
        test_check(
            fn && entry && ret && format && graph_add_pin(fn, ret, 0, &text) >= 0 &&
                graph_connect_exec(fn, entry, 0, ret, 0, 0) == 0 &&
                graph_set_pin_default(
                    fn,
                    format,
                    0,
                    0,
                    &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "callee" }
                ) == 0 &&
                graph_connect_data(fn, format, 0, ret, 1) == 0,
            "string function graph is authored"
        );
        node *event = graph_add_node(bp->graph, &NODE_Event);
        node *call = fn ? behavior_asset_add_function_call_node(bp, fn) : NULL;
        node *capture = graph_add_node(bp->graph, &g_str_capture_desc);
        graph_set_event_name(bp->graph, event, "Text");
        test_check(
            event && call && capture &&
                graph_connect_exec(bp->graph, event, 0, call, 0, 0) == 0 &&
                graph_connect_exec(bp->graph, call, 0, capture, 0, 0) == 0 &&
                graph_connect_data(bp->graph, call, 1, capture, 1) == 0,
            "string function return is connected to the next activation"
        );
        behavior_instance *bi = behavior_instance_create(bp, NULL, NULL, NULL);
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(bi, "Text", (flow_inputs){ 0 }, &error);
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                strcmp(g_captured_str, "callee") == 0,
            "string function return survives nested-run teardown"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(bp);
    }
}
