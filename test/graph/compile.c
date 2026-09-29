#include "graph/compiler/behavior_program.h"
#include "test/suite_internal.h"

void run_graph_compile_checks()
{
    /* NODE-13: descriptors classify as pure, executable entry, or executable;
     * the existing pure helper keeps classifying the same way. */
    {
        test_check(node_desc_behavior(&g_const_desc) == NODE_BEHAVIOR_PURE, "a pure descriptor has no exec pins");
        test_check(
            node_desc_behavior(&NODE_Event) == NODE_BEHAVIOR_ENTRY,
            "an event is an executable entry (no exec input, one exec output)"
        );
        test_check(node_desc_behavior(&NODE_Branch) == NODE_BEHAVIOR_EXEC, "a node with an exec input is executable");
        test_check(
            node_desc_is_pure(&g_const_desc) && !node_desc_is_pure(&NODE_Branch),
            "the existing pure helper agrees with the classification"
        );
    }

    /* CMPL-1: compiling is explicit, ordered, read-only, and clean on an
     * empty behavior_asset. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL1");
        compile_report rep;
        compile_report_init(&rep);
        unsigned long long rev = graph_topology_revision(asset->graph);
        char *before = behavior_asset_save_json(asset);
        int errors = behavior_asset_compile(asset, &rep);
        char *after = behavior_asset_save_json(asset);
        test_check(
            errors == 0 && rep.error_count == 0 && rep.warning_count == 0 &&
                acount(rep.items) == 0,
            "compiling an empty behavior_asset produces no diagnostics"
        );
        test_check(
            rev == graph_topology_revision(asset->graph) && before && after &&
                strcmp(before, after) == 0,
            "the compile pass never modifies the authored graph"
        );
        test_check(
            compile_report_blocked(&rep) == 0 && compile_report_first_error(&rep) == NULL,
            "an empty report has no blocking error"
        );
        free(before);
        free(after);
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* PERF-1: publication batches an event whose logic reads no payload, and
     * not one with a duplicate name. */
    {
        behavior_asset *asset = behavior_asset_new("PERF1");
        node *event = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, event, "Tick");
        behavior_program *program = behavior_program_publish(asset);
        node *entry = NULL;
        test_check(
            behavior_program_batch_event(program, "Tick", &entry) && entry == event &&
                behavior_program_backend(program) != BEHAVIOR_UNAVAILABLE,
            "a parameterless event has a batch path"
        );
        pin_spec payload = { "Unused", PIN_F32, "float", PIN_DATA };
        int payload_pin = graph_add_pin(asset->graph, event, 1, &payload);
        test_check(payload_pin >= 0, "an event can expose a payload pin");
        program = behavior_program_publish(asset);
        entry = NULL;
        test_check(
            behavior_program_batch_event(program, "Tick", &entry) && entry == event,
            "an unread event payload keeps the batch path"
        );
        graph_remove_pin(asset->graph, event, 1, payload_pin);
        node *duplicate = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, duplicate, "Tick");
        program = behavior_program_publish(asset);
        test_check(behavior_program_batch_event(program, "Tick", NULL) == NULL, "duplicate event names are not batched");
        behavior_asset_free(asset);
    }

    /* PERF-2: latent flow stays on the activation path rather than being
     * mistaken for a fast batch. */
    {
        behavior_asset *asset = behavior_asset_new("PERF2");
        node *event = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, event, "Go");
        node *delay = graph_add_node(asset->graph, &NODE_Delay);
        graph_connect_exec(asset->graph, event, 0, delay, 0, 0);
        test_check(
            behavior_program_batch_event(behavior_program_publish(asset), "Go", NULL) == NULL,
            "a latent event is not batched"
        );
        behavior_asset_free(asset);
    }

    /* CMPL-4: an executable node whose exec input has no incoming edge is
     * unreachable but remains valid to apply; the event entry is exempt. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL4");
        node *ev = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, ev, "Go");
        node *branch = graph_add_node(asset->graph, &NODE_Branch);
        node *print = graph_add_node(asset->graph, &NODE_PrintString);
        print->y = 100.0f; /* below the branch: order is tested under CMPL-16 */
        test_check(
            node_behavior_for(ev) == NODE_BEHAVIOR_ENTRY &&
                node_behavior_for(branch) == NODE_BEHAVIOR_EXEC,
            "node instances classify like their descriptors"
        );
        compile_report rep;
        compile_report_init(&rep);
        test_check(
            behavior_asset_compile(asset, &rep) == 0 && rep.error_count == 0 &&
                rep.warning_count == 2,
            "two executable nodes with unconnected exec inputs warn"
        );
        int count = 0, on_branch = 0, on_print = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            const compile_diagnostic *d = &rep.items[i];
            if (d->rule != CMPL_RULE_UNCONNECTED_EXEC || d->severity != CMPL_WARNING) {
                continue;
            }
            count++;
            if (d->node_id == branch->id) {
                on_branch = 1;
            }
            if (d->node_id == print->id) {
                on_print = 1;
            }
        }
        test_check(count == 2 && on_branch && on_print, "both diagnostics name the unconnected executable nodes");
        test_check(
            rep.items[0].node_id == branch->id &&
                rep.items[0].pin_id == branch->input_pins[0].id &&
                rep.items[0].graph_id == CMPL_GRAPH_DEFAULT,
            "the diagnostic carries graph, node, and pin ids"
        );
        test_check(
            strcmp(cmpl_rule_name(CMPL_RULE_UNCONNECTED_EXEC), "CMPL-4") == 0,
            "every diagnostic carries a stable rule code"
        );
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* CMPL-5: a data edge between incompatible pins. The graph API rejects
     * incompatible connections, so the test inserts the edge directly. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL5");
        node *ev = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, ev, "Go");
        node *src = graph_add_node(asset->graph, &g_all_types_desc);
        node *sink = graph_add_node(asset->graph, &g_parameter_sink_desc);
        graph_connect_exec(asset->graph, ev, 0, sink, 0, 0);
        graph_connect_data(asset->graph, src, 5, sink, 1); /* i32 -> Integer: ok */
        edge bad = {
            .from_id = src->id,
            .from_pin_id = src->output_pins[5].id,
            .to_id = sink->id,
            .to_pin_id = sink->input_pins[2].id,
            .kind = GRAPH_EDGE_DATA,
        };
        apush(asset->graph->edges, bad); /* i32 -> Flag bool: incompatible */
        compile_report rep;
        compile_report_init(&rep);
        behavior_asset_compile(asset, &rep);
        /* The graph also carries the all-types node's unregistered enum output
         * (CMPL-20); count only the incompatible-edge diagnostics here. */
        int bad_edges = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_INCOMPATIBLE_DATA_EDGE) {
                bad_edges++;
            }
        }
        test_check(bad_edges == 1, "one incompatible data edge errors");
        const compile_diagnostic *c5 = NULL;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_INCOMPATIBLE_DATA_EDGE) {
                c5 = &rep.items[i];
            }
        }
        test_check(
            c5 && c5->node_id == sink->id && c5->pin_id == sink->input_pins[2].id,
            "the diagnostic names the incompatible input pin"
        );
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* CMPL-6: a cycle among data edges reports once, at the closing back
     * edge; an acyclic chain stays clean. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL6");
        static pin_spec cycle_in[] = { { "In", PIN_I32, "int", PIN_DATA } };
        static pin_spec cycle_out[] = { { "Out", PIN_I32, "int", PIN_DATA } };
        static node_desc cycle_desc = {
            .name = "CycleIO",
            .input_count = 1,
            .output_count = 1,
            .inputs = cycle_in,
            .outputs = cycle_out,
            .pure_kind = NODE_PURE_CONST,
        };
        node *a = graph_add_node(asset->graph, &cycle_desc);
        node *b = graph_add_node(asset->graph, &cycle_desc);
        node *c = graph_add_node(asset->graph, &cycle_desc);
        graph_connect_data(asset->graph, a, 0, b, 0);
        graph_connect_data(asset->graph, b, 0, c, 0);
        compile_report rep;
        compile_report_init(&rep);
        test_check(behavior_asset_compile(asset, &rep) == 0, "an acyclic data chain compiles clean");
        graph_connect_data(asset->graph, c, 0, a, 0);
        compile_report_free(&rep);
        compile_report_init(&rep);
        test_check(
            behavior_asset_compile(asset, &rep) == 1 &&
                rep.items[0].rule == CMPL_RULE_DATA_CYCLE &&
                rep.items[0].node_id == c->id,
            "closing the chain reports one data-cycle error at the "
            "back edge"
        );
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* CMPL-7: a reference to a retired or missing variable, event, function,
     * or macro. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL7");
        behavior_variable *v = behavior_asset_add_variable(
            asset,
            "Score",
            PIN_I32,
            "int",
            PIN_CONTAINER_SINGLE,
            NULL,
            ""
        );
        behavior_asset_add_get_node(asset, v->id);
        behavior_asset_add_set_node(asset, v->id);
        node *ev = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, ev, "Go");
        compile_report rep;
        compile_report_init(&rep);
        behavior_asset_compile(asset, &rep);
        int refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        test_check(refs == 0, "valid variable and event references compile clean");
        test_check(behavior_asset_delete_variable(asset, v->id) == 0, "deleting a referenced variable is permitted (VAR-7)");
        compile_report_free(&rep);
        compile_report_init(&rep);
        behavior_asset_compile(asset, &rep);
        refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        test_check(refs == 2, "Get and Set both report the retired variable");
        test_check(graph_remove_node(asset->graph, ev) == 0, "removing the referenced event is permitted");
        compile_report_free(&rep);
        compile_report_init(&rep);
        behavior_asset_compile(asset, &rep);
        refs = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            if (rep.items[i].rule == CMPL_RULE_MISSING_REFERENCE) {
                refs++;
            }
        }
        test_check(refs == 2, "variable references remain after event removal");
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* CMPL-8: duplicate event names, duplicate parameter names within one
     * list, and duplicate variable names each report once. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL8");
        node *e1 = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, e1, "Begin");
        node *e2 = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, e2, "Begin");
        compile_report rep;
        compile_report_init(&rep);
        behavior_asset_compile(asset, &rep);
        test_check(
            rep.error_count == 1 && rep.items[0].rule == CMPL_RULE_DUPLICATE_NAME &&
                rep.items[0].node_id == e2->id,
            "two events sharing a name produce one diagnostic on the "
            "later event"
        );
        node *e3 = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, e3, "Tick");
        pin_spec amount_a = { "Amount", PIN_I32, "int", PIN_DATA };
        pin_spec amount_b = { "Amount", PIN_I32, "int", PIN_DATA };
        graph_add_pin(asset->graph, e3, 1, &amount_a);
        graph_add_pin(asset->graph, e3, 1, &amount_b);
        behavior_variable dup_var_a = {
            .id = graph_alloc_id(asset->graph),
            .name = graph_own_str(asset->graph, "Score", 5),
            .type = PIN_I32,
            .type_name = "int",
            .container = PIN_CONTAINER_SINGLE,
        };
        behavior_variable dup_var_b = dup_var_a;
        dup_var_b.id = graph_alloc_id(asset->graph);
        apush(asset->variables, dup_var_a);
        apush(asset->variables, dup_var_b);
        compile_report_free(&rep);
        compile_report_init(&rep);
        behavior_asset_compile(asset, &rep);
        test_check(rep.error_count == 3, "duplicate event, parameter, and variable names each error");
        int ev_dup = 0, param_dup = 0, var_dup = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            const compile_diagnostic *d = &rep.items[i];
            if (d->rule != CMPL_RULE_DUPLICATE_NAME) {
                continue;
            }
            if (d->node_id == e2->id) {
                ev_dup++;
            } else if (d->node_id == e3->id) {
                param_dup++;
            } else if (d->node_id == 0 && strstr(d->message, "Score")) {
                var_dup++;
            }
        }
        test_check(ev_dup == 1 && param_dup == 1 && var_dup == 1, "each duplicate kind reports exactly once");
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* CMPL-12: an unresolved wildcard pin that carries an edge. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL12");
        static pin_spec wild_in = {
            .name = "Value",
            .type = PIN_FLOW,
            .kind = PIN_DATA,
            .flags = PIN_FLAG_WILDCARD,
        };
        static pin_spec wild_out = {
            .name = "Out",
            .type = PIN_I32,
            .type_name = "int",
            .kind = PIN_DATA,
        };
        static node_desc wild_desc = {
            .name = "WildcardIn",
            .input_count = 1,
            .output_count = 1,
            .inputs = &wild_in,
            .outputs = &wild_out,
            .pure_kind = NODE_PURE_CONST,
        };
        node *w = graph_add_node(asset->graph, &wild_desc);
        node *src = graph_add_node(asset->graph, &g_const_desc);
        compile_report rep;
        compile_report_init(&rep);
        test_check(behavior_asset_compile(asset, &rep) == 0, "an unconnected wildcard pin is not an error");
        edge we = {
            .from_id = src->id,
            .from_pin_id = src->output_pins[0].id,
            .to_id = w->id,
            .to_pin_id = w->input_pins[0].id,
            .kind = GRAPH_EDGE_DATA,
        };
        apush(asset->graph->edges, we);
        compile_report_free(&rep);
        compile_report_init(&rep);
        test_check(
            behavior_asset_compile(asset, &rep) == 1 &&
                rep.items[0].rule == CMPL_RULE_UNRESOLVED_WILDCARD &&
                rep.items[0].node_id == w->id &&
                rep.items[0].pin_id == w->input_pins[0].id,
            "an unresolved wildcard pin that carries an edge errors"
        );
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* CMPL-16: deterministic ordering by graph, then node position (canvas y,
     * then x, then node id), then rule code. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL16");
        node *top = graph_add_node(asset->graph, &NODE_PrintString);
        node *mid = graph_add_node(asset->graph, &NODE_Branch);
        node *bot = graph_add_node(asset->graph, &NODE_Branch);
        top->y = 10.0f;
        top->x = 500.0f;
        mid->y = 20.0f;
        mid->x = 0.0f;
        bot->y = 30.0f;
        bot->x = 1.0f;
        compile_report r1, r2;
        compile_report_init(&r1);
        compile_report_init(&r2);
        behavior_asset_compile(asset, &r1);
        behavior_asset_compile(asset, &r2);
        int same = acount(r1.items) == acount(r2.items);
        for (int i = 0; same && i < acount(r1.items); i++) {
            same = memcmp(&r1.items[i], &r2.items[i], sizeof r1.items[i]) == 0;
        }
        test_check(same && acount(r1.items) == 3, "compiling twice yields an identical ordered list");
        test_check(
            r1.items[0].node_id == top->id && r1.items[1].node_id == mid->id &&
                r1.items[2].node_id == bot->id,
            "diagnostics order by canvas y, then x, then node id"
        );
        test_check(
            r1.items[0].rule == CMPL_RULE_UNCONNECTED_EXEC &&
                r1.items[1].rule == CMPL_RULE_UNCONNECTED_EXEC,
            "equal-rule diagnostics keep position order"
        );
        compile_report_free(&r1);
        compile_report_free(&r2);
        behavior_asset_free(asset);
    }

    /* CMPL-16: diagnostics on one node order by rule code; the set node
     * carries both an unconnected exec input (CMPL-4) and a missing variable
     * reference (CMPL-7). */
    {
        behavior_asset *asset = behavior_asset_new("CMPL16b");
        node *set = graph_add_node(asset->graph, &NODE_SetVariable);
        graph_set_node_reference(asset->graph, set, 4242, "Lost");
        compile_report rep;
        compile_report_init(&rep);
        test_check(
            behavior_asset_compile(asset, &rep) == 1 && rep.error_count == 1 &&
                rep.warning_count == 1,
            "one node can carry an error and a warning"
        );
        test_check(
            acount(rep.items) == 2 && rep.items[0].rule == CMPL_RULE_UNCONNECTED_EXEC &&
                rep.items[1].rule == CMPL_RULE_MISSING_REFERENCE,
            "diagnostics on one node order by rule code"
        );
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* CMPL-18: a disabled node is excluded from every rule except one
     * warning reporting that it is disabled. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL18");
        node *dbranch = graph_add_node(asset->graph, &NODE_Branch);
        node *dget = graph_add_node(asset->graph, &NODE_GetVariable);
        graph_set_node_disabled(asset->graph, dbranch, 1);
        graph_set_node_disabled(asset->graph, dget, 1);
        compile_report rep;
        compile_report_init(&rep);
        test_check(
            behavior_asset_compile(asset, &rep) == 0 && rep.error_count == 0 &&
                rep.warning_count == 2,
            "disabled nodes are excluded from every rule except one warning "
            "each"
        );
        int warn_branch = 0, warn_get = 0, other = 0;
        for (int i = 0; i < acount(rep.items); i++) {
            const compile_diagnostic *d = &rep.items[i];
            if (d->rule != CMPL_RULE_DISABLED || d->severity != CMPL_WARNING) {
                other++;
                continue;
            }
            if (d->node_id == dbranch->id) {
                warn_branch++;
            }
            if (d->node_id == dget->id) {
                warn_get++;
            }
        }
        test_check(warn_branch == 1 && warn_get == 1 && other == 0, "exactly one disabled warning per node and no other rules");
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* CMPL-20: an authored nominal type name not present in the active
     * registry is a blocking diagnostic that preserves the name. */
    {
        behavior_asset *asset = behavior_asset_new("CMPL20");
        static pin_spec nominal_in[] = {
            { "CF_V3In", PIN_STRUCT, "CF_V3", PIN_DATA },
            { "Control", PIN_ENUM, "GameControl", PIN_DATA },
        };
        static pin_spec nominal_out[] = {
            { "Handle", PIN_HANDLE, "MissingType", PIN_DATA },
        };
        static node_desc nominal_desc = {
            .name = "Nominal",
            .input_count = 2,
            .output_count = 1,
            .inputs = nominal_in,
            .outputs = nominal_out,
            .pure_kind = NODE_PURE_CONST,
        };
        graph_add_node(asset->graph, &nominal_desc);
        compile_report rep;
        compile_report_init(&rep);
        test_check(behavior_asset_compile(asset, &rep) == 1, "registered struct and enum pins compile; an unknown handle name errors");
        test_check(
            rep.items[0].rule == CMPL_RULE_UNKNOWN_NOMINAL_TYPE &&
                strstr(rep.items[0].message, "MissingType") != NULL,
            "the diagnostic preserves the unresolved type name"
        );
        compile_report_free(&rep);
        behavior_asset_free(asset);
    }

    /* RUN-12: warnings do not block a run; a disconnected executable node is
     * simply unreachable until an exec edge is connected. */
    {
        behavior_asset *asset = behavior_asset_new("Run12");
        node *ev = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, ev, "Go");
        node *branch = graph_add_node(asset->graph, &NODE_Branch);
        node *print = graph_add_node(asset->graph, &NODE_PrintString);
        print->y = -50.0f; /* warning order is tested under CMPL-16 */
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error rerr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &rerr);
        test_check(
            act && flow_run_status(act) == FLOW_COMPLETED && rerr.code == FLOW_OK,
            "unconnected executable nodes do not block dispatch"
        );
        test_check(
            behavior_instance_enqueue(bi, ev, (flow_inputs){ 0 }, &rerr) != NULL &&
                rerr.code == FLOW_OK,
            "direct enqueue also accepts unreachable executable nodes"
        );
        behavior_instance_destroy(bi);
        /* Connect the event; the next instance recompiles at the new revision. */
        graph_remove_node(asset->graph, print);
        graph_connect_exec(asset->graph, ev, 0, branch, 0, 0);
        bi = behavior_instance_create(asset, NULL, NULL, NULL);
        act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &rerr);
        test_check(act != NULL && flow_run_status(act) == FLOW_COMPLETED, "a connected event also starts and completes");
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }
    {
        behavior_asset *asset = behavior_asset_new("Run12w");
        node *ev = graph_add_node(asset->graph, &NODE_Event);
        graph_set_event_name(asset->graph, ev, "Go");
        node *br = graph_add_node(asset->graph, &NODE_Branch);
        graph_set_node_disabled(asset->graph, br, 1);
        behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error werr = { 0 };
        flow_run *act = behavior_instance_dispatch(bi, "Go", (flow_inputs){ 0 }, &werr);
        test_check(act != NULL && flow_run_status(act) == FLOW_COMPLETED, "warnings do not block a start (CMPL-3)");
        behavior_instance_destroy(bi);
        behavior_asset_free(asset);
    }
}
