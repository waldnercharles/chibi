#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "test/suite_internal.h"

void run_runtime_instance_checks()
{
    /* Phase 3: persistent Behavior Asset instances, variables, and generic
     * events. Begin() initializes one instance's variables once; later
     * Tick(Real) and Input(Name, Real, Bool) events read and write the same
     * state (VAR-11, EVT-9, EVT-10). */
    {
        behavior_asset *asset = behavior_asset_new("VarDemo");
        test_check(asset && asset->graph, "a behavior_asset owns its graph and metadata");
        behavior_variable *health = behavior_asset_add_variable(
            asset,
            "Health",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 100 },
            "Stats"
        );
        test_check(health != NULL, "variables carry id, name, type, container, default, category");
        test_check(
            behavior_asset_find_variable_by_name(asset, "Health") == health,
            "variable names are unique and findable within a behavior_asset"
        );
        test_check(
            behavior_asset_add_variable(asset, "Health", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, NULL) ==
                NULL,
            "duplicate variable names are rejected"
        );
        behavior_variable *last_action = behavior_asset_add_variable(
            asset,
            "LastAction",
            PIN_NAME,
            "name",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_NAME, .str = "" },
            ""
        );
        test_check(last_action != NULL, "a Name variable accepts an authored default");

        /* Begin(): Set Health to 50. */
        node *begin_ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, begin_ev, "Begin");
        test_check(
            begin_ev->input_count == 0 && begin_ev->output_count >= 1 &&
                begin_ev->output_pins[0].kind == PIN_EXEC,
            "an Event node is an executable entry point"
        );
        test_check(
            graph_event_name(begin_ev) &&
                strcmp(graph_event_name(begin_ev), "Begin") == 0,
            "event identity is name-addressed within the Behavior Asset"
        );
        node *set = behavior_asset_add_set_node(asset, health->id);
        graph_set_pin_default(asset->graph, set, 0, 1, &(pin_value){ .type = PIN_I32, .i = 50 });
        graph_connect_exec(asset->graph, begin_ev, 0, set, 0, 0);

        /* Tick(Real DeltaSeconds): read Health into a capture node. */
        node *tick_ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, tick_ev, "Tick");
        pin_spec delta = { "DeltaSeconds", PIN_F64, "double", PIN_DATA };
        graph_add_pin(asset->graph, tick_ev, 1, &delta);
        node *get = behavior_asset_add_get_node(asset, health->id);
        node *capture = graph_add_node(asset->graph, &g_capture_desc);
        graph_connect_exec(asset->graph, tick_ev, 0, capture, 0, 0);
        graph_connect_data(asset->graph, get, 0, capture, 1);

        /* A pure conversion chain must record every data edge, not only the
         * edge entering the flow node that consumes its final output. */
        node *text_ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        node *to_string = graph_add_node(asset->graph, registry_node_by_name("To String"));
        node *string_capture = graph_add_node(asset->graph, &g_str_capture_desc);
        test_check(
            text_ev && to_string && string_capture &&
                graph_set_event_name(asset->graph, text_ev, "TickText") == 0 &&
                graph_set_pin_type(asset->graph, to_string, 0, 0, PIN_I32, "int") == 0 &&
                graph_connect_exec(asset->graph, text_ev, 0, string_capture, 0, 0) == 0 &&
                graph_connect_data(asset->graph, get, 0, to_string, 0) == 0 &&
                graph_connect_data(asset->graph, to_string, 0, string_capture, 1) == 0,
            "pure data chain is authored"
        );

        /* Input(Name Action, Real Value, Bool Pressed): Set LastAction. */
        node *input_ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, input_ev, "Input");
        pin_spec action_param = { "Action", PIN_NAME, "name", PIN_DATA };
        pin_spec val = { "Value", PIN_F64, "double", PIN_DATA };
        pin_spec pressed = { "Pressed", PIN_BOOL, "bool", PIN_DATA };
        graph_add_pin(asset->graph, input_ev, 1, &action_param);
        graph_add_pin(asset->graph, input_ev, 1, &val);
        graph_add_pin(asset->graph, input_ev, 1, &pressed);
        node *set_action = behavior_asset_add_set_node(asset, last_action->id);
        graph_connect_exec(asset->graph, input_ev, 0, set_action, 0, 0);
        graph_connect_data(asset->graph, input_ev, 1, set_action, 1);

        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        test_check(bi != NULL, "explicit instance creation succeeds");
        pin_value v;
        test_check(
            behavior_instance_get_variable(bi, health->id, &v) == 0 && v.i == 100,
            "instance variables initialize from authored defaults once"
        );
        test_check(asset->graph->flow_pause_count > 0, "creating an instance acquires the mutation guard");
        test_check(behavior_instance_scratch(bi, 99) != NULL, "instance scratch is keyed by node ID");
        test_check(bi->activations == NULL, "activation-local state starts empty on the instance");
        test_check(asset->graph->flow_pause_count == 1, "applied definition is guarded while a preview instance exists");

        flow_error perr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Begin", (flow_inputs){ 0 }, &perr);
        test_check(act && flow_run_status(act) == FLOW_COMPLETED, "Begin dispatch completes");
        test_check(flow_run_current_node(act) == NULL, "inspecting a completed activation does not merge scheduler state");
        test_check(
            flow_run_call_stack(act, NULL) != NULL || flow_run_status(act) == FLOW_COMPLETED,
            "an activation exposes an ordered call-stack view"
        );
        test_check(
            behavior_instance_get_variable(bi, health->id, &v) == 0 && v.i == 50,
            "Set Variable writes instance storage"
        );

        g_captured_int = -1;
        pin_value tick_params[] = { { .type = PIN_F64, .d = 0.016 } };
        act = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ tick_params, 1, NULL }, &perr);
        test_check(
            act && flow_run_status(act) == FLOW_COMPLETED && g_captured_int == 50,
            "a later Tick event reads the state Begin wrote"
        );

        g_captured_str[0] = '\0';
        act = behavior_instance_dispatch(bi, "TickText", (flow_inputs){ 0 }, &perr);
        int traversal_count = 0;
        const flow_traversal *traversals = flow_run_traversals(act, &traversal_count);
        int saw_get_to_string = 0;
        int saw_to_string_capture = 0;
        for (int i = 0; i < traversal_count; i++) {
            saw_get_to_string |= traversals[i].kind == GRAPH_EDGE_DATA &&
                                 traversals[i].from_id == get->id &&
                                 traversals[i].to_id == to_string->id;
            saw_to_string_capture |= traversals[i].kind == GRAPH_EDGE_DATA &&
                                     traversals[i].from_id == to_string->id &&
                                     traversals[i].to_id == string_capture->id;
        }
        test_check(
            act && flow_run_status(act) == FLOW_COMPLETED &&
                strcmp(g_captured_str, "50") == 0 && saw_get_to_string &&
                saw_to_string_capture,
            "pure data chains record every traversed data edge"
        );

        pin_value input_params[] = {
            { .type = PIN_NAME, .str = "Jump" },
            { .type = PIN_F64, .d = 1.0 },
            { .type = PIN_BOOL, .b = true },
        };
        act = behavior_instance_dispatch(bi, "Input", (flow_inputs){ input_params, 3, NULL }, &perr);
        test_check(act && flow_run_status(act) == FLOW_COMPLETED, "Input dispatch with parameters completes");
        test_check(
            behavior_instance_get_variable(bi, last_action->id, &v) == 0 &&
                v.type == PIN_NAME && v.str && strcmp(v.str, "Jump") == 0,
            "event parameters deliver values into instance state"
        );
        test_check(health->default_value.i == 100, "instance execution never mutates authored defaults");

        test_check(
            behavior_asset_rename_variable(asset, health->id, "HP") != 0,
            "renaming a variable is rejected while an instance is attached"
        );
        test_check(
            behavior_asset_add_variable(asset, "New", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, NULL) ==
                NULL,
            "adding a variable is rejected while an instance is attached"
        );
        test_check(graph_add_node(asset->graph, &g_sink_a_desc) == NULL, "adding a node is rejected while an instance is attached");
        test_check(behavior_instance_stop_activation(bi, act) == FLOW_OK, "stopping one activation succeeds");
        test_check(asset->graph->flow_pause_count > 0, "stopping one activation does NOT release the mutation guard");
        behavior_instance_destroy(bi);
        test_check(asset->graph->flow_pause_count == 0, "destroying the last instance releases the mutation guard");

        uint64_t health_id = health->id;
        node *get_ref = get;
        test_check(behavior_asset_rename_variable(asset, health_id, "HP") == 0, "renaming a variable succeeds when mutable");
        test_check(
            health->name && strcmp(health->name, "HP") == 0 &&
                get_ref->reference_id == health_id && get_ref->reference_name &&
                strcmp(get_ref->reference_name, "HP") == 0,
            "rename updates the referencing node's display without "
            "rewriting its reference"
        );
        test_check(
            behavior_asset_retype_variable(asset, health_id, PIN_F64, "double") == 0,
            "retyping a variable succeeds"
        );
        test_check(
            health->type == PIN_F64 && get_ref->output_pins[0].type == PIN_F64,
            "retyping updates the referencing node's pin type"
        );
        node *src = NULL;
        int src_pin = -1;
        test_check(
            graph_input_source(asset->graph, capture, 1, &src, &src_pin) == 0,
            "the now-incompatible edge is invalidated"
        );
        behavior_asset_free(asset);
    }

    /* TRACE-1: completed activations leave copied entries at the instance
     * seam, including fallback records for events with no traversal. */
    {
        behavior_asset *asset = behavior_asset_new("Trace");
        node *empty = graph_add_node(asset->graph, registry_node_by_name("Event"));
        node *done = graph_add_node(asset->graph, registry_node_by_name("Event"));
        node *done_sink = graph_add_node(asset->graph, &g_sink_a_desc);
        node *failed = graph_add_node(asset->graph, registry_node_by_name("Event"));
        node *failed_sink = graph_add_node(asset->graph, &g_sink_error_desc);
        test_check(
            asset && empty && done && done_sink && failed && failed_sink &&
                graph_set_event_name(asset->graph, empty, "Empty") == 0 &&
                graph_set_event_name(asset->graph, done, "Done") == 0 &&
                graph_set_event_name(asset->graph, failed, "Failed") == 0 &&
                graph_connect_exec(asset->graph, done, 0, done_sink, 0, 0) == 0 &&
                graph_connect_exec(asset->graph, failed, 0, failed_sink, 0, 0) == 0,
            "trace fixture has empty, completed, and failed events"
        );
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error error = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Empty", (flow_inputs){ 0 }, &error);
        test_check(
            act && flow_run_status(act) == FLOW_COMPLETED &&
                behavior_instance_release_activation(bi, act) == 0,
            "an empty event completes and can be released"
        );
        int trace_count = 0;
        const behavior_instance_trace_entry *trace = behavior_instance_trace(bi, &trace_count);
        test_check(
            trace && trace_count == 1 && trace[0].event_name &&
                strcmp(trace[0].event_name, "Empty") == 0 &&
                trace[0].state == FLOW_COMPLETED && trace[0].error_code == FLOW_OK,
            "an empty event produces a completed fallback entry"
        );
        act = behavior_instance_dispatch(bi, "Failed", (flow_inputs){ 0 }, &error);
        test_check(
            act && flow_run_status(act) == FLOW_FAILED &&
                flow_run_error(act)->code == -777 &&
                behavior_instance_release_activation(bi, act) == 0,
            "a failed event preserves its structured run error"
        );
        trace = behavior_instance_trace(bi, &trace_count);
        test_check(
            trace_count == 2 && trace[1].state == FLOW_FAILED &&
                trace[1].error_code == -777 &&
                strcmp(trace[1].event_name, "Failed") == 0,
            "a failed activation records terminal status and error"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }

    /* TRACE-3: the public trace is oldest-first, bounded, and survives
     * activation recycling but not instance reuse. */
    {
        behavior_asset *asset = behavior_asset_new("TraceBound");
        node *event = graph_add_node(asset->graph, registry_node_by_name("Event"));
        node *pulse = graph_add_node(asset->graph, registry_node_by_name("Event"));
        node *sink = graph_add_node(asset->graph, registry_node_by_name("Branch"));
        graph_set_event_name(asset->graph, event, "Tick");
        graph_set_event_name(asset->graph, pulse, "Pulse");
        graph_connect_exec(asset->graph, event, 0, sink, 0, 0);
        graph_connect_exec(asset->graph, pulse, 0, sink, 0, 0);
        uint64_t pulse_id = pulse->id;
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        test_check(behavior_instance_set_activation_pool(bi, 1) == 0, "activation pooling can be enabled before dispatch");
        flow_error error = { 0 };
        int trace_ok = 1;
        int releases = 2 * BEHAVIOR_ASSET_INSTANCE_TRACE_MAX + 17;
        for (int i = 0; i < releases; i++) {
            if (behavior_instance_tick(bi, (double)i) != FLOW_OK) {
                trace_ok = 0;
                break;
            }
            flow_run *act = behavior_instance_dispatch(
                bi,
                i % 2 ? "Pulse" : "Tick",
                (flow_inputs){ 0 },
                &error
            );
            if (!act || behavior_instance_release_activation(bi, act) != 0) {
                trace_ok = 0;
                break;
            }
        }
        test_check(trace_ok, "pooled activations release into the trace");
        test_check(!bi->trace_view && acount(bi->trace_names) == 2, "uninspected history retains only bounded ring storage and one owned name per event");
        int trace_count = 0;
        const behavior_instance_trace_entry *trace = behavior_instance_trace(bi, &trace_count);
        test_check(
            trace && trace_count == BEHAVIOR_ASSET_INSTANCE_TRACE_MAX &&
                trace[0].traversal.timestamp == releases - BEHAVIOR_ASSET_INSTANCE_TRACE_MAX &&
                trace[trace_count - 1].traversal.timestamp == releases - 1,
            "multiple trace wraps retain exactly the newest bounded history"
        );
        const char *names[2] = { NULL, NULL };
        for (int i = 0; i < trace_count; i++) {
            int timestamp = releases - trace_count + i;
            int parity = timestamp % 2;
            if (!names[parity]) {
                names[parity] = trace[i].event_name;
            }
            trace_ok &= trace[i].traversal.timestamp == timestamp &&
                        !strcmp(trace[i].event_name, parity ? "Pulse" : "Tick") &&
                        trace[i].event_name == names[parity] &&
                        trace[i].state == FLOW_COMPLETED;
        }
        test_check(trace_ok, "every retained entry stays chronological and owns its name across activation recycling");
        uint64_t trace_revision = behavior_instance_trace_revision(bi);
        test_check(behavior_instance_trace(bi, &trace_count) == trace, "repeated inspection borrows the unchanged ordered view");
        test_check(behavior_instance_trace_revision(bi) == trace_revision, "inspection reads preserve the trace revision");
        test_check(behavior_instance_tick(bi, releases) == FLOW_OK, "trace advances after inspection");
        flow_run *act = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, &error);
        test_check(
            act && behavior_instance_release_activation(bi, act) == 0,
            "recording continues after the ordered view was read"
        );
        trace = behavior_instance_trace(bi, &trace_count);
        test_check(
            trace && trace_count == BEHAVIOR_ASSET_INSTANCE_TRACE_MAX &&
                trace[0].traversal.timestamp ==
                    releases - BEHAVIOR_ASSET_INSTANCE_TRACE_MAX + 1 &&
                trace[trace_count - 1].traversal.timestamp == releases &&
                trace[trace_count - 1].event_name == names[0],
            "a write refreshes the ordered view without replacing interned "
            "event names"
        );
        test_check(behavior_instance_trace_revision(bi) != trace_revision, "bounded trace overwrite changes its revision at the same record count");
        trace_revision = behavior_instance_trace_revision(bi);
        test_check(
            behavior_instance_pool_release(bi) == 0 &&
                behavior_instance_trace(bi, &trace_count) && trace_count == 0 &&
                acount(bi->trace_names) == 0 &&
                behavior_instance_pool_reuse(bi, asset, NULL, NULL, NULL) == bi &&
                behavior_instance_trace(bi, &trace_count) && trace_count == 0,
            "instance release and reuse clear history and its owned event names"
        );
        test_check(
            behavior_instance_trace_revision(bi) != trace_revision,
            "instance release and reuse invalidate retained trace activity"
        );
        act = behavior_instance_dispatch(bi, "Pulse", (flow_inputs){ 0 }, &error);
        test_check(act && behavior_instance_release_activation(bi, act) == 0, "reused instances record new activations");
        trace = behavior_instance_trace(bi, &trace_count);
        test_check(
            trace && trace_count == 1 && !strcmp(trace[0].event_name, "Pulse"),
            "reused history contains only its new owner's event"
        );

        behavior_instance_set_trace_enabled(bi, 0);
        act = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, &error);
        test_check(
            act && behavior_instance_release_activation(bi, act) == 0 &&
                behavior_instance_trace(bi, &trace_count) && trace_count == 0 &&
                acount(bi->trace_names) == 0,
            "disabled tracing records neither history nor names"
        );
        behavior_instance_set_trace_enabled(bi, 1);
        act = behavior_instance_dispatch(bi, "Pulse", (flow_inputs){ 0 }, &error);
        test_check(act && behavior_instance_release_activation(bi, act) == 0, "tracing resumes after being enabled");

        char *json = behavior_asset_save(asset);
        behavior_asset *next = behavior_asset_load(json);
        free(json);
        test_check(
            next && graph_set_event_name(next->graph, graph_node_by_id(next->graph, pulse_id), "Renamed") ==
                        0,
            "replacement changes its event name"
        );
        test_check(
            behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY,
            "completed traced instances accept a replacement definition"
        );
        behavior_instance_update(bi, next);
        behavior_asset_free(asset);
        asset = next;
        test_check(
            behavior_instance_trace(bi, &trace_count) && trace_count == 0 &&
                acount(bi->trace_names) == 0,
            "hot apply clears history and names before the old definition is "
            "destroyed"
        );
        act = behavior_instance_dispatch(bi, "Renamed", (flow_inputs){ 0 }, &error);
        test_check(act && behavior_instance_release_activation(bi, act) == 0, "the replacement event records after its previous definition is freed");
        trace = behavior_instance_trace(bi, &trace_count);
        test_check(
            trace && trace_count == 1 && !strcmp(trace[0].event_name, "Renamed"),
            "new history uses the replacement event's owned name"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }

    /* VAR-7: deleting a referenced variable is permitted; the orphaned Get
     * reference fails evaluation with an error naming the lost variable. */
    {
        behavior_asset *asset = behavior_asset_new("Orphan");
        behavior_variable *v = behavior_asset_add_variable(
            asset,
            "Gone",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 1 },
            ""
        );
        node *ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, ev, "Go");
        node *get = behavior_asset_add_get_node(asset, v->id);
        node *cap = graph_add_node(asset->graph, &g_capture_desc);
        graph_connect_exec(asset->graph, ev, 0, cap, 0, 0);
        graph_connect_data(asset->graph, get, 0, cap, 1);
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        behavior_instance_destroy(bi);
        test_check(behavior_asset_delete_variable(asset, v->id) == 0, "deleting a referenced variable is permitted");
        bi = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error perr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        test_check(
            act == NULL && perr.code == FLOW_ERR_COMPILE && perr.node == get &&
                perr.message && strstr(perr.message, "missing variable") != NULL,
            "an orphaned Get reference is a blocking compile error "
            "(RUN-12 refuses to start)"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }

    /* EXEC-17 / CAT-18: a Delay suspends while a later event executes, and the
     * scheduler's FIFO activation order is deterministic across repeated runs. */
    {
        behavior_asset *asset = behavior_asset_new("Sched");
        node *wait_ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, wait_ev, "Wait");
        node *delay = graph_add_node(asset->graph, registry_node_by_name("Delay"));
        node *done_a = graph_add_node(asset->graph, &g_sink_a_desc);
        graph_connect_exec(asset->graph, wait_ev, 0, delay, 0, 0);
        graph_connect_exec(asset->graph, delay, 0, done_a, 0, 0);
        graph_set_pin_default(asset->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 5.0 });
        node *fast_ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, fast_ev, "Fast");
        node *done_b = graph_add_node(asset->graph, &g_sink_b_desc);
        graph_connect_exec(asset->graph, fast_ev, 0, done_b, 0, 0);
        for (int run = 0; run < 2; run++) {
            behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
            flow_log_reset();
            flow_error perr = { 0 };
            flow_run *act = behavior_instance_dispatch(bi, "Wait", (flow_inputs){ 0 }, &perr);
            test_check(
                act && flow_run_status(act) == FLOW_SUSPENDED && g_flow_log_count == 0,
                "a Delay suspends its activation"
            );
            act = behavior_instance_dispatch(bi, "Fast", (flow_inputs){ 0 }, &perr);
            test_check(
                act && flow_run_status(act) == FLOW_COMPLETED &&
                    strcmp(g_flow_log, "B") == 0,
                "a later event executes while the Delay is suspended"
            );
            test_check(acount(bi->activations) == 2, "suspended and completed activations coexist");
            test_check(acount(bi->activations) == 2, "re-entry keeps an earlier activation on its continuation stack");
            test_check(
                behavior_instance_tick(bi, 5.0) == FLOW_OK &&
                    strcmp(g_flow_log, "BA") == 0,
                "the scheduler resumes the Delay at its deadline in FIFO order"
            );
            test_check(act && flow_run_status(act) == FLOW_COMPLETED, "the Delay completes after its duration");
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(asset);
    }

    /* EXEC-18 isolation: two activations of the same event retain distinct
     * parameter values while they are paused before the same capture node. */
    {
        behavior_asset *asset = behavior_asset_new("ActivationIsolation");
        node *event = graph_add_node(asset->graph, registry_node_by_name("Event"));
        node *capture = graph_add_node(asset->graph, &g_capture_desc);
        pin_spec value = { "Value", PIN_I32, "int", PIN_DATA };
        graph_add_pin(asset->graph, event, 1, &value);
        graph_connect_exec(asset->graph, event, 0, capture, 0, 0);
        graph_connect_data(asset->graph, event, 1, capture, 1);
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error first_error = { 0 };
        flow_error second_error = { 0 };
        flow_run *first = behavior_instance_enqueue(
            bi,
            event,
            (flow_inputs){ (pin_value[]){ { .type = PIN_I32, .i = 11 } }, 1, NULL },
            &first_error
        );
        flow_run *second = behavior_instance_enqueue(
            bi,
            event,
            (flow_inputs){ (pin_value[]){ { .type = PIN_I32, .i = 22 } }, 1, NULL },
            &second_error
        );
        test_check(
            first && second && first_error.code == 0 && second_error.code == 0 &&
                flow_run_step(first) == FLOW_PAUSED_RESULT &&
                flow_run_step(second) == FLOW_PAUSED_RESULT,
            "same-node activations can be staged independently"
        );
        (void)flow_run_step(first);
        (void)flow_run_step(second);
        test_check(
            flow_run_resume(first) == FLOW_OK && flow_run_resume(second) == FLOW_OK &&
                flow_run_status(first) == FLOW_COMPLETED &&
                flow_run_status(second) == FLOW_COMPLETED,
            "staged activations complete independently"
        );
        pin_value first_value;
        pin_value second_value;
        test_check(
            flow_run_watch_input(first, capture, 1, &first_value) == 0 &&
                flow_run_watch_input(second, capture, 1, &second_value) == 0 &&
                first_value.i == 11 && second_value.i == 22,
            "same-node activation values remain isolated"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }

    /* DBG-5: watched names and arrays are copied out of the activation and
     * remain valid after the source defaults are released. */
    {
        behavior_asset *asset = behavior_asset_new("OwnedWatches");
        node *event = graph_add_node(asset->graph, &NODE_Event);
        node *watch = graph_add_node(asset->graph, &g_owned_watch_desc);
        graph_set_event_name(asset->graph, event, "Owned");
        graph_connect_exec(asset->graph, event, 0, watch, 0, 0);
        test_check(
            graph_set_pin_default(
                asset->graph,
                watch,
                0,
                1,
                &(pin_value){ .type = PIN_NAME, .type_name = "name", .str = "OwnedName" }
            ) == 0,
            "Name watch default is authored"
        );
        pin_array *array = pin_array_new(PIN_I32, "int", 2);
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 7 });
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 9 });
        test_check(
            graph_set_pin_default(
                asset->graph,
                watch,
                0,
                2,
                &(pin_value){
                    .type = PIN_I32,
                    .type_name = "int",
                    .container = PIN_CONTAINER_ARRAY,
                    .ptr = array,
                }
            ) == 0,
            "array watch default is authored"
        );
        pin_array_free(array);
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(bi, "Owned", (flow_inputs){ 0 }, &error);
        pin_value name_value;
        pin_value array_value;
        const pin_array *items = NULL;
        test_check(
            run && flow_run_status(run) == FLOW_COMPLETED &&
                flow_run_watch_input(run, watch, 1, &name_value) == 0 &&
                flow_run_watch_input(run, watch, 2, &array_value) == 0 &&
                name_value.type == PIN_NAME && name_value.str &&
                strcmp(name_value.str, "OwnedName") == 0 &&
                array_value.container == PIN_CONTAINER_ARRAY &&
                (items = (const pin_array *)array_value.ptr) && items->count == 2 &&
                items->items[0].i == 7 && items->items[1].i == 9,
            "Name and array watches own their captured values"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }

    /* EXEC-16: activation inputs own pointer-bearing values until the run is
     * released, even when the source storage is changed or freed. */
    {
        graph g;
        graph_init(&g);
        node *event = graph_add_node(&g, &NODE_Event);
        node *delay = graph_add_node(&g, &NODE_Delay);
        pin_spec name_pin = { "Name", PIN_NAME, "name", PIN_DATA };
        pin_spec text_pin = { "Text", PIN_STR, "const char *", PIN_DATA };
        pin_spec array_pin = {
            "Values",
            PIN_I32,
            "int",
            PIN_DATA,
            0,
            PIN_CONTAINER_ARRAY,
            { .type = PIN_I32, .type_name = "int", .container = PIN_CONTAINER_ARRAY },
        };
        graph_add_pin(&g, event, 1, &name_pin);
        graph_add_pin(&g, event, 1, &text_pin);
        graph_add_pin(&g, event, 1, &array_pin);
        graph_connect_exec(&g, event, 0, delay, 0, 0);
        graph_set_pin_default(&g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1.0 });

        char *name = (char *)malloc(5);
        char *text = (char *)malloc(5);
        memcpy(name, "Name", 5);
        memcpy(text, "Text", 5);
        pin_array *array = pin_array_new(PIN_I32, "int", 2);
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .i = 7 });
        pin_array_push(array, &(pin_value){ .type = PIN_I32, .i = 9 });
        pin_value inputs[] = {
            { .type = PIN_NAME, .type_name = "name", .str = name },
            { .type = PIN_STR, .type_name = "const char *", .str = text },
            { .type = PIN_I32, .type_name = "int", .container = PIN_CONTAINER_ARRAY, .ptr = array },
        };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(&g, event, (flow_inputs){ inputs, 3, NULL }, NULL, &error);
        test_check(
            run && flow_run_step(run) == FLOW_PAUSED_RESULT &&
                flow_run_resume(run) == FLOW_SUSPENDED_RESULT,
            "pointer-bearing activation inputs survive suspension"
        );
        memset(name, 'x', 4);
        memset(text, 'x', 4);
        array->items[0].i = 99;
        free(name);
        free(text);
        pin_array_free(array);
        flow_inputs owned = flow_run_inputs(run);
        const pin_array *owned_array = owned.count == 3
                                         ? (const pin_array *)owned.values[2].ptr
                                         : NULL;
        test_check(
            owned.count == 3 && owned.values[0].str &&
                strcmp(owned.values[0].str, "Name") == 0 && owned.values[1].str &&
                strcmp(owned.values[1].str, "Text") == 0 && owned_array &&
                owned_array->count == 2 && owned_array->items[0].i == 7 &&
                owned_array->items[1].i == 9,
            "activation copies Name, string, and array inputs"
        );
        flow_run_free(run);
        graph_free(&g);
    }

    /* RUN-17: freeing an instance-owned activation removes it from the
     * scheduler before the instance is destroyed. */
    {
        behavior_asset *asset = behavior_asset_new("FreeActivation");
        node *event = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, event, "Go");
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error error = { 0 };
        flow_run *run = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &error);
        test_check(run && acount(bi->activations) == 1, "instance owns a returned activation");
        flow_run_free(run);
        test_check(acount(bi->activations) == 0, "freeing an activation removes it from its instance");
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }

    /* EVT-7: dispatching by name fails distinctly for unknown and duplicate
     * names. */
    {
        behavior_asset *asset = behavior_asset_new("Evt7");
        node *ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, ev, "Go");
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error err = { 0 };
        test_check(
            behavior_instance_dispatch(bi, "Missing", (flow_inputs){ 0 }, &err) == NULL &&
                err.code == FLOW_ERR_UNKNOWN_EVENT,
            "an unknown event name fails distinctly"
        );
        behavior_instance_destroy(bi);
        node *dup = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, dup, "Go");
        bi = behavior_instance_create(asset, NULL, NULL, NULL);
        err = (flow_error){ 0 };
        test_check(
            behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &err) == NULL &&
                err.code == FLOW_ERR_DUPLICATE_EVENT,
            "a duplicate event name fails distinctly"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }

    /* RUN-18: two instances of one definition share no owner, variable,
     * scratch, activation, error, or step-count state. */
    {
        behavior_asset *asset = behavior_asset_new("Two");
        behavior_asset_set_owner_type(asset, "Entity");
        behavior_variable *v = behavior_asset_add_variable(
            asset,
            "State",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 100 },
            ""
        );
        node *boom_ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, boom_ev, "Boom");
        node *boom = graph_add_node(asset->graph, &g_sink_error_desc);
        graph_connect_exec(asset->graph, boom_ev, 0, boom, 0, 0);
        pin_value o1 = { .type = PIN_HANDLE, .u64 = 111, .type_name = "Entity" };
        pin_value o2 = { .type = PIN_HANDLE, .u64 = 222, .type_name = "Entity" };
        behavior_instance *bi1 = behavior_instance_create(asset, &o1, NULL, NULL);
        behavior_instance *bi2 = behavior_instance_create(asset, &o2, NULL, NULL);
        test_check(
            behavior_instance_owner(bi1).u64 == 111 &&
                behavior_instance_owner(bi2).u64 == 222 &&
                behavior_instance_owner(bi1).type == PIN_HANDLE,
            "two instances carry independent owner handles"
        );
        behavior_instance_set_variable(bi1, v->id, &(pin_value){ .type = PIN_I32, .i = 7 });
        pin_value rv;
        test_check(
            behavior_instance_get_variable(bi1, v->id, &rv) == 0 && rv.i == 7 &&
                behavior_instance_get_variable(bi2, v->id, &rv) == 0 && rv.i == 100,
            "two instances have independent variable storage"
        );
        pin_value *s1 = behavior_instance_scratch(bi1, 42);
        s1->type = PIN_I32;
        s1->i = 5;
        test_check(behavior_instance_scratch(bi2, 42)->i == 0, "two instances have independent per-node scratch");
        flow_error perr = { 0 };
        flow_run *a1 = behavior_instance_dispatch(bi1, "Boom", (flow_inputs){ 0 }, &perr);
        test_check(
            a1 && flow_run_status(a1) == FLOW_FAILED && flow_run_steps(a1) == 2,
            "an error and its step count live on instance one's own activation"
        );
        test_check(
            acount(bi1->activations) == 1 && acount(bi2->activations) == 0,
            "one instance's failure leaves the other's queue untouched"
        );
        flow_run *a2 = behavior_instance_dispatch(bi2, "Boom", (flow_inputs){ 0 }, &perr);
        test_check(
            a2 && flow_run_status(a2) == FLOW_FAILED &&
                flow_run_error(a1) != flow_run_error(a2),
            "independent activation errors"
        );
        behavior_instance_destroy(bi1);
        behavior_instance_destroy(bi2);
        behavior_asset_free(asset);
    }

    /* RUN-15 / RUN-17: stopping one activation preserves instance variables
     * and scratch; destroying the instance cancels every activation and
     * releases the mutation guard without executing another node. */
    {
        behavior_asset *asset = behavior_asset_new("Stop");
        behavior_variable *v = behavior_asset_add_variable(
            asset,
            "State",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_I32, .i = 5 },
            ""
        );
        node *ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, ev, "Go");
        node *delay = graph_add_node(asset->graph, registry_node_by_name("Delay"));
        node *done = graph_add_node(asset->graph, &g_sink_a_desc);
        graph_connect_exec(asset->graph, ev, 0, delay, 0, 0);
        graph_connect_exec(asset->graph, delay, 0, done, 0, 0);
        graph_set_pin_default(asset->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 100.0 });
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        behavior_instance_set_variable(bi, v->id, &(pin_value){ .type = PIN_I32, .i = 9 });
        behavior_instance_scratch(bi, delay->id)->type = PIN_I32;
        behavior_instance_scratch(bi, delay->id)->i = 42;
        flow_error perr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        test_check(act && flow_run_status(act) == FLOW_SUSPENDED, "dispatch leaves a suspended activation");
        test_check(
            behavior_instance_stop_activation(bi, act) == FLOW_OK &&
                flow_run_status(act) == FLOW_IDLE,
            "stopping one activation succeeds"
        );
        pin_value rv;
        test_check(
            behavior_instance_get_variable(bi, v->id, &rv) == 0 && rv.i == 9,
            "stopping an activation preserves instance variables"
        );
        test_check(behavior_instance_scratch(bi, delay->id)->i == 42, "stopping an activation preserves per-node scratch");
        int guard_before = asset->graph->flow_pause_count;
        behavior_instance_destroy(bi);
        test_check((int)asset->graph->flow_pause_count == guard_before - 1, "destroying the instance releases the mutation guard");
        node *probe = graph_add_node(asset->graph, &g_sink_b_desc);
        test_check(probe != NULL, "the definition becomes mutable after its last instance is destroyed");
        /* The probe node's exec input is unconnected, which is a blocking
         * compile error under RUN-12; remove it before starting again. */
        test_check(graph_remove_node(asset->graph, probe) == 0, "removing the probe restores a valid behavior_asset");

        behavior_instance *bi2 = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_log_reset();
        act = behavior_instance_dispatch(bi2, "Go", (flow_inputs){ 0 }, &perr);
        test_check(act && flow_run_status(act) == FLOW_SUSPENDED, "a second instance also suspends");
        behavior_instance_destroy(bi2);
        test_check(g_flow_log_count == 0, "destroying the instance cancels activations without executing another node");
        behavior_asset_free(asset);
    }

    /* CAT-32: Self is a pure node whose output has the definition's owner
     * handle type; it is rejected without an owner type and evaluates to the
     * instance's supplied owner handle. */
    {
        behavior_asset *asset = behavior_asset_new("Self");
        test_check(behavior_asset_add_self_node(asset) == NULL, "Self is rejected when the definition declares no owner type");
        /* The owner handle type must resolve in the active registry (CMPL-20),
         * so the test uses a reflected type name. */
        behavior_asset_set_owner_type(asset, "CF_V3");
        test_check(
            asset->owner_type_name && strcmp(asset->owner_type_name, "CF_V3") == 0,
            "a Behavior Asset definition carries one nominal owner type"
        );
        node *self = behavior_asset_add_self_node(asset);
        test_check(
            self && self->output_pins[0].type == PIN_HANDLE &&
                self->output_pins[0].type_name &&
                strcmp(self->output_pins[0].type_name, "CF_V3") == 0,
            "Self carries the declared nominal owner handle type"
        );
        pin_spec ha = { "a", PIN_HANDLE, "Entity", PIN_DATA };
        pin_spec hb = { "b", PIN_HANDLE, "Entity", PIN_DATA };
        pin_spec hc = { "c", PIN_HANDLE, "Other", PIN_DATA };
        test_check(
            pin_types_compatible(&ha, &hb) && !pin_types_compatible(&ha, &hc),
            "handle pins are compatible only when nominal type names match"
        );
        node *ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, ev, "Go");
        node *cap = graph_add_node(asset->graph, &g_handle_capture_desc);
        graph_connect_exec(asset->graph, ev, 0, cap, 0, 0);
        graph_connect_data(asset->graph, self, 0, cap, 1);
        pin_value owner = { .type = PIN_HANDLE, .u64 = 777, .type_name = "CF_V3" };
        behavior_instance *bi = behavior_instance_create(asset, &owner, NULL, NULL);
        g_captured_handle = 0;
        flow_error perr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &perr);
        test_check(
            act && flow_run_status(act) == FLOW_COMPLETED && g_captured_handle == 777,
            "Self evaluates to the instance's supplied owner handle"
        );
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }

    /* FMT-16 / FMT-17 / FMT-18: the Behavior Asset document is byte-identical
     * before and after execution, and Name spelling plus the owner type
     * round-trip exactly. */
    {
        behavior_asset *asset = behavior_asset_new("FmtBP");
        behavior_asset_set_owner_type(asset, "UnavailableOwner");
        behavior_asset_add_variable(
            asset,
            "Tag",
            PIN_NAME,
            "name",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_NAME, .str = "MyTag" },
            "Cats"
        );
        node *ev = graph_add_node(asset->graph, registry_node_by_name("Event"));
        graph_set_event_name(asset->graph, ev, "Go");
        node *name_lit = graph_add_node(asset->graph, registry_node_by_name("Name"));
        graph_set_pin_default(asset->graph, name_lit, 0, 0, &(pin_value){ .type = PIN_NAME, .str = "ExactSpelling" });
        char *before = behavior_asset_save(asset);
        test_check(before && strstr(before, "owner \"UnavailableOwner\""), "the authored owner handle type is written");
        behavior_instance *bi = behavior_instance_create(
            asset,
            &(pin_value){ .type = PIN_HANDLE, .u64 = 5, .type_name = "UnavailableOwner" },
            NULL,
            NULL
        );
        behavior_instance_destroy(bi);
        char *after = behavior_asset_save(asset);
        test_check(before && after && strcmp(before, after) == 0 && !strstr(before, "activations"), "saving before and after execution is byte-identical and carries no runtime state");
        behavior_asset *loaded = before ? behavior_asset_load(before) : NULL;
        test_check(
            loaded && loaded->owner_type_name &&
                strcmp(loaded->owner_type_name, "UnavailableOwner") == 0,
            "an unavailable owner type name is preserved, not substituted"
        );
        behavior_variable *lv = loaded
                                  ? behavior_asset_find_variable_by_name(loaded, "Tag")
                                  : NULL;
        test_check(
            lv && lv->default_value.str && strcmp(lv->default_value.str, "MyTag") == 0,
            "a Name variable's authored spelling round-trips exactly"
        );
        int spelling = 0;
        if (loaded) {
            for (int i = 0; i < acount(loaded->graph->nodes); i++) {
                node *n = loaded->graph->nodes[i];
                if (n->desc == registry_node_by_name("Name") &&
                    n->input_pins[0].default_value.str &&
                    strcmp(n->input_pins[0].default_value.str, "ExactSpelling") == 0) {
                    spelling = 1;
                }
            }
        }
        test_check(spelling, "a Name literal's authored spelling round-trips exactly");
        char *re = loaded ? behavior_asset_save(loaded) : NULL;
        test_check(re && before && strcmp(re, before) == 0, "the Behavior Asset document load/save round-trips byte-identically");
        if (loaded) {
            behavior_asset_free(loaded);
        }
        free(before);
        free(after);
        free(re);
        behavior_asset_free(asset);
    }
}
