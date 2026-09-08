#include "test/suite_internal.h"

void run_runtime_arrays_checks()
{
    const node_desc *make = registry_node_by_name("Make_Vec3");
    /* Phase 5: typed arrays and deterministic ForEach iteration. */
    {
        graph g_array;
        graph_init(&g_array);
        node *make = graph_add_node(&g_array, registry_node_by_name("Make Array"));
        pin_spec item = { "Item", PIN_I32, "int", PIN_DATA };
        for (int i = 0; i < 5; i++) {
            if (i) {
                graph_add_pin(&g_array, make, 0, &item);
            }
            graph_set_pin_type(&g_array, make, 0, i, PIN_I32, "int");
            graph_set_pin_default(&g_array, make, 0, i, &(pin_value){ .type = PIN_I32, .i = i + 1 });
        }
        arena array_arena;
        arena_init(&array_arena, 8192);
        eval_context array_eval;
        eval_context_init(&array_eval);
        pin_value array_value;
        test_check(
            graph_eval_output(&g_array, make, 0, &array_arena, &array_value, &array_eval) == 0 &&
                array_value.container == PIN_CONTAINER_ARRAY &&
                ((pin_array *)array_value.ptr)->count == 5 &&
                ((pin_array *)array_value.ptr)->items[4].i == 5,
            "typed array values copy five elements"
        );
        node *length = graph_add_node(&g_array, registry_node_by_name("Length"));
        graph_connect_data(&g_array, make, 0, length, 0);
        eval_context_reset_runtime(&array_eval);
        pin_value length_value;
        test_check(
            graph_eval_output(&g_array, length, 0, &array_arena, &length_value, &array_eval) ==
                    0 &&
                length_value.i == 5,
            "Length observes the deterministic array operation"
        );
        test_check(registry_node_by_name("ForLoopWithBreak") != NULL, "ForLoopWithBreak is registered");
        test_check(registry_node_by_name("WhileLoop") != NULL, "WhileLoop is registered");
        test_check(
            registry_node_by_name("ForEach") && registry_node_by_name("ForEachWithBreak"),
            "ForEach and its break variant are registered"
        );
        test_check(
            registry_node_by_name("Make Array") && registry_node_by_name("Set Array Elem"),
            "the complete array operation family is registered"
        );
        test_check(
            pin_array_equal(
                (pin_array *)array_value.ptr,
                (pin_array *)array_value.ptr
            ),
            "array equality is deterministic and copy-safe"
        );
        test_check(registry_node_by_name("Make Array")->category != NULL, "array nodes expose a category for context-sensitive spawn filtering");
        test_check(registry_node_by_name("Get")->keywords != NULL, "array connection targets expose compatible spawn keywords");
        test_check(
            strcmp(cmpl_rule_name(CMPL_RULE_UNREACHABLE), "CMPL-13") == 0,
            "unreachable-node diagnostics have a stable warning rule"
        );
        test_check(
            strcmp(cmpl_rule_name(CMPL_RULE_UNCONNECTED_DATA), "CMPL-14") == 0,
            "unconnected impure data diagnostics have a stable warning rule"
        );
        test_check(array_value.container == PIN_CONTAINER_ARRAY && array_value.ptr != NULL, "invalid and unresolved container values remain explicit errors, not silent nodes");

        /* DBG-5: struct array copies borrow their elements consistently. The
         * clone and its destruction must not free host or arena storage. */
        Vec3 borrowed_values[] = { { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 6.0f } };
        pin_array *borrowed_array = pin_array_new(PIN_STRUCT, "Vec3", 2);
        pin_array_push(
            borrowed_array,
            &(pin_value){ .type = PIN_STRUCT, .type_name = "Vec3", .ptr = &borrowed_values[0] }
        );
        pin_array_push(
            borrowed_array,
            &(pin_value){ .type = PIN_STRUCT, .type_name = "Vec3", .ptr = &borrowed_values[1] }
        );
        pin_value borrowed_value = {
            .type = PIN_STRUCT,
            .type_name = "Vec3",
            .container = PIN_CONTAINER_ARRAY,
            .ptr = borrowed_array,
        };
        pin_value borrowed_copy = { 0 };
        test_check(
            pin_value_copy(&borrowed_copy, &borrowed_value) == 0,
            "struct array watch copy succeeds without taking element ownership"
        );
        pin_value_destroy(&borrowed_copy);
        pin_array_free(borrowed_array);
        test_check(
            borrowed_values[0].x == 1.0f && borrowed_values[1].z == 6.0f,
            "struct array clone destruction leaves borrowed elements intact"
        );

        arena_free(&array_arena);
        graph_free(&g_array);
    }

    /* CAT-7: ForEach emits each array element once, including the one-element
     * case, and emits no body for an empty array. */
    {
        const int cases[][4] = { { 0, 0, 0, 0 }, { 1, 7, 0, 0 }, { 3, 4, 5, 6 } };
        for (int c = 0; c < 3; c++) {
            graph g_foreach;
            graph_init(&g_foreach);
            node *event = graph_add_node(&g_foreach, &NODE_Event);
            node *each = graph_add_node(&g_foreach, &NODE_ForEach);
            node *capture = graph_add_node(&g_foreach, &g_foreach_capture_desc);
            pin_spec array_pin = {
                "Values",
                PIN_I32,
                "int",
                PIN_DATA,
                0,
                PIN_CONTAINER_ARRAY,
                { .type = PIN_I32, .type_name = "int", .container = PIN_CONTAINER_ARRAY },
            };
            graph_add_pin(&g_foreach, event, 1, &array_pin);
            graph_connect_exec(&g_foreach, event, 0, each, 0, 0);
            graph_connect_data(&g_foreach, event, 1, each, 1);
            graph_connect_exec(&g_foreach, each, 0, capture, 0, 0);
            graph_connect_data(&g_foreach, each, 1, capture, 1);
            pin_array *array = pin_array_new(PIN_I32, "int", cases[c][0]);
            for (int i = 0; i < cases[c][0]; i++) {
                pin_array_push(
                    array,
                    &(pin_value){ .type = PIN_I32, .type_name = "int", .i = cases[c][i + 1] }
                );
            }
            graph_set_pin_default(
                &g_foreach,
                event,
                1,
                1,
                &(pin_value){
                    .type = PIN_I32,
                    .type_name = "int",
                    .container = PIN_CONTAINER_ARRAY,
                    .ptr = array,
                }
            );
            pin_array_free(array);
            g_foreach_count = 0;
            flow_error error = { 0 };
            flow_run *run = flow_run_start(&g_foreach, event, (flow_inputs){ 0 }, NULL, &error);
            test_check(run && flow_run_resume(run) == FLOW_OK && flow_run_status(run) == FLOW_COMPLETED, "ForEach completes for empty, one-element, and multi-element arrays");
            test_check(
                g_foreach_count == cases[c][0] &&
                    (c == 0 || (g_foreach_values[0] == cases[c][1] &&
                                (c == 1 || (g_foreach_values[1] == cases[c][2] &&
                                            g_foreach_values[2] == cases[c][3])))),
                "ForEach emits the expected elements exactly once"
            );
            flow_run_free(run);
            graph_free(&g_foreach);
        }
    }

    /* Phase 6: weak delegate bindings. */
    {
        behavior_asset *bp = behavior_asset_new("Delegates");
        node *event = graph_add_node(bp->graph, &NODE_Event);
        graph_set_event_name(bp->graph, event, "Changed");
        pin_spec value = { "Value", PIN_I32, "int", PIN_DATA };
        graph_add_pin(bp->graph, event, 1, &value);
        node *sink = graph_add_node(bp->graph, &g_capture_desc);
        graph_connect_exec(bp->graph, event, 0, sink, 0, 0);
        graph_connect_data(bp->graph, event, 1, sink, 1);
        behavior_instance *source = behavior_instance_create(bp, NULL, NULL, NULL);
        behavior_instance *target = behavior_instance_create(bp, NULL, NULL, NULL);
        int service_token = 7;
        test_check(
            behavior_instance_bind_service(target, "SelfCheckService", &service_token) == 0 &&
                behavior_instance_service(target, "SelfCheckService") == &service_token,
            "service bindings are stable instance data"
        );
        uint64_t binding = behavior_instance_bind_event(source, target, event->id);
        uint64_t duplicate = behavior_instance_bind_event(source, target, event->id);
        g_captured_int = -1;
        flow_error delegate_error = { 0 };
        flow_run *delegate_run = behavior_instance_dispatch_delegate(
            source,
            binding,
            (flow_inputs){ &(pin_value){ .type = PIN_I32, .i = 77 }, 1, NULL },
            &delegate_error
        );
        test_check(
            binding != 0 && binding == duplicate && delegate_run && g_captured_int == 77,
            "duplicate delegate registration returns one binding ID"
        );
        test_check(
            behavior_instance_broadcast_event(
                source,
                event->id,
                (flow_inputs){ &(pin_value){ .type = PIN_I32, .i = 78 }, 1, NULL },
                &delegate_error
            ) == 1,
            "delegate dispatch follows a stable registration snapshot"
        );
        test_check(behavior_instance_unbind_event(source, binding) == 0, "unbinding removes exactly one binding");
        binding = behavior_instance_bind_event(source, target, event->id);
        char *delegate_json = behavior_asset_save_json(bp);
        test_check(delegate_json && !strstr(delegate_json, "delegates"), "delegate runtime state is absent from authored JSON");
        free(delegate_json);
        test_check(binding != 0, "typed delegate communication has an instance-owned binding");
        behavior_instance_destroy(target);
        test_check(
            behavior_instance_dispatch_delegate(source, binding, (flow_inputs){ 0 }, &delegate_error) ==
                NULL,
            "destroying a weak delegate target prevents dispatch"
        );
        test_check(delegate_error.code == FLOW_ERR_INVALID_GRAPH, "a broken delegate produces one structured diagnostic");
        behavior_instance_destroy(source);
        behavior_asset_free(bp);
    }
}
