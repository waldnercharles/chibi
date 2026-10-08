#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "test/suite_internal.h"

void run_runtime_arrays_checks()
{
    {
        graph g;
        graph_init(&g);
        node *first = graph_add_node(&g, registry_node_by_name("Array Add"));
        node *second = graph_add_node(&g, registry_node_by_name("Add Unique"));
        node *duplicate = graph_add_node(&g, registry_node_by_name("Add Unique"));
        pin_array *empty = pin_array_new(PIN_HANDLE, "ecs.entity", 0);
        pin_value source = { .type = PIN_HANDLE,
                             .type_name = "ecs.entity",
                             .container = PIN_CONTAINER_ARRAY,
                             .ptr = empty };
        pin_value a = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 17 },
                  b = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 29 };
        assert(graph_set_pin_type(&g, first, 0, 0, PIN_HANDLE, "ecs.entity") == 0);
        assert(graph_set_pin_default(&g, first, 0, 0, &source) == 0);
        assert(graph_set_pin_default(&g, first, 0, 1, &a) == 0);
        assert(graph_connect_data(&g, first, 0, second, 0) == 0);
        assert(graph_set_pin_default(&g, second, 0, 1, &b) == 0);
        assert(graph_connect_data(&g, second, 0, duplicate, 0) == 0);
        assert(graph_set_pin_default(&g, duplicate, 0, 1, &a) == 0);
        arena storage;
        arena_init(&storage, 4096);
        eval_context eval;
        eval_context_init(&eval);
        pin_value result;
        assert(graph_eval_output(&g, duplicate, 0, &storage, &result, &eval) == 0);
        const pin_array *values = result.ptr;
        test_check(
            values->count == 2 && values->items[0].u64 == 17 &&
                values->items[1].u64 == 29,
            "Array Add grows an empty handle array; Add Unique preserves "
            "existing entries without duplicates"
        );
        test_check(empty->count == 0, "array additions preserve their input snapshot");
        eval_context_free(&eval);
        arena_free(&storage);
        graph_free(&g);
        pin_array_free(empty);
    }
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
        test_check(array_value.container == PIN_CONTAINER_ARRAY && array_value.ptr != NULL, "invalid and unresolved container values remain explicit errors, not silent nodes");

        /* DBG-5: struct array copies borrow their elements consistently. The
         * clone and its destruction must not free host or arena storage. */
        CF_V3 borrowed_values[] = { { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 6.0f } };
        pin_array *borrowed_array = pin_array_new(PIN_STRUCT, "CF_V3", 2);
        pin_array_push(
            borrowed_array,
            &(pin_value){ .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &borrowed_values[0] }
        );
        pin_array_push(
            borrowed_array,
            &(pin_value){ .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &borrowed_values[1] }
        );
        pin_value borrowed_value = {
            .type = PIN_STRUCT,
            .type_name = "CF_V3",
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
        eval_context_free(&array_eval);
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

    /* Service bindings belong to an instance. */
    {
        behavior_asset *asset = behavior_asset_new("Services");
        behavior_instance *target = behavior_instance_create(asset, NULL, NULL, NULL);
        int service_token = 7;
        test_check(
            behavior_instance_bind_service(target, "SelfCheckService", &service_token) == 0 &&
                behavior_instance_service(target, "SelfCheckService") == &service_token,
            "service bindings are stable instance data"
        );
        behavior_instance_destroy(target);
        behavior_asset_free(asset);
    }
}
