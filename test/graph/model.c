#include "test/suite_internal.h"

void run_graph_model_checks()
{
    g_watch_desc.type = registry_type_by_name("Vec3");
    test_check(editor_preview_allowed(FLOW_IDLE), "idle preview is allowed");
    test_check(
        !editor_preview_allowed(FLOW_RUNNING) &&
            !editor_preview_allowed(FLOW_SUSPENDED) &&
            !editor_preview_allowed(FLOW_PAUSED) &&
            !editor_preview_allowed(FLOW_COMPLETED) &&
            !editor_preview_allowed(FLOW_FAILED),
        "preview is suspended for every non-idle run state"
    );

    arena a;
    arena_init(&a, 4096);

    graph g;
    graph_init(&g);
    flow_error dispatch_error = { 0 };
    eval_context eval_ctx;
    eval_context_init(&eval_ctx);

    const node_desc *make = registry_node_by_name("Make_Vec3");
    const node_desc *brk = registry_node_by_name("Break_Vec3");
    const node_desc *add = registry_node_by_name("add_floats");
    test_check(make && brk && add, "registry exposes Make_Vec3, Break_Vec3, add_floats");
    test_check(make && make->type && make->type->field_count == 3, "reflected struct has a type descriptor");
    test_check(
        make && make->type && make->type->fields &&
            make->type->fields[0].size == sizeof(float),
        "reflected fields carry compiler layout"
    );
    test_check(
        PIN_BOOL != PIN_I8 && PIN_I8 != PIN_U8 && PIN_U8 != PIN_I16 &&
            PIN_I16 != PIN_U16 && PIN_U16 != PIN_I32 && PIN_I32 != PIN_U32 &&
            PIN_U32 != PIN_I64 && PIN_I64 != PIN_U64 && PIN_U64 != PIN_F32 &&
            PIN_F32 != PIN_F64 && PIN_F64 != PIN_STR && PIN_STR != PIN_STRUCT &&
            PIN_STRUCT != PIN_ENUM && PIN_ENUM != PIN_FLOW,
        "primitive and flow pin types are distinct"
    );
    pin_spec i32_pin = { "i", PIN_I32, "int", PIN_DATA };
    pin_spec i64_pin = { "i", PIN_I64, "long", PIN_DATA };
    test_check(!pin_types_compatible(&i32_pin, &i64_pin), "integer widths stay distinct");
    test_check(g_const_desc.pure_kind == NODE_PURE_CONST, "hand-authored node descriptor is usable");
    test_check(
        make && make->type && make->type->name && strcmp(make->type->name, "Vec3") == 0,
        "build-time registry exposes reflected types"
    );
    test_check(make && make->inputs && make->outputs, "annotations emit descriptor pin metadata");
    test_check(add && add->fn, "annotated function emits a call adapter");
    test_check(
        make && make->type->fields[0].offset == 0 && make->type->size >= sizeof(float),
        "generated layout uses sizeof and offsetof"
    );
    test_check(
        make && brk && make->pure_kind == NODE_PURE_MAKE && brk->pure_kind == NODE_PURE_BREAK,
        "annotation expansion keeps Make and Break policy"
    );
    test_check(registry_node_count() > 0, "the parity surface is a classified registry, not a fixed count claim");
    test_check(PIN_BOOL != PIN_I32 && PIN_F32 != PIN_F64, "the complete primitive pin set is distinct");
    test_check(operator_resolve("missing", PIN_I32, PIN_I32) == NULL, "operator resolution has no implicit C fallback");

    /* 1. two constants -> add call node */
    node *c1 = make_const(&g, 2.0f);
    node *c2 = make_const(&g, 3.0f);
    node *a1 = graph_add_node(&g, add);
    test_check(graph_connect(&g, c1, 0, a1, 0) == 0, "connect c1 -> add.a");
    test_check(graph_connect(&g, c2, 0, a1, 1) == 0, "connect c2 -> add.b");

    pin_value sum;
    test_check(graph_eval_output(&g, a1, 0, &a, &sum, &eval_ctx) == 0, "eval reflected call node");
    test_check(sum.type == PIN_F32 && sum.f == 5.0f, "2.0 + 3.0 == 5.0");

    /* 2. Make(Vec3) -> Break(Vec3) round trip */
    eval_context_reset_runtime(&eval_ctx);
    arena_reset(&a);

    node *mk = graph_add_node(&g, make);
    node *br = graph_add_node(&g, brk);
    node *cx = make_const(&g, 1.0f);
    node *cy = make_const(&g, 2.0f);
    node *cz = make_const(&g, 3.0f);
    graph_connect(&g, cx, 0, mk, 0);
    graph_connect(&g, cy, 0, mk, 1);
    graph_connect(&g, cz, 0, mk, 2);
    graph_connect(&g, mk, 0, br, 0);

    pin_value oy;
    test_check(graph_eval_output(&g, br, 1, &a, &oy, &eval_ctx) == 0, "eval Break(Vec3).y");
    pin_value make_output;
    test_check(
        eval_context_output(&eval_ctx, mk, 0, &make_output) == 0 &&
            make_output.ptr != NULL,
        "Make(Vec3) value is arena-owned"
    );
    test_check(oy.type == PIN_F32 && oy.f == 2.0f, "Make(Vec3) -> Break(Vec3): y == 2.0");

    /* 3. memo: one eval across two reads */
    eval_context_reset_runtime(&eval_ctx);
    arena_reset(&a);
    g_count = 0;
    node *ctr = graph_add_node(&g, &g_counter_desc);
    pin_value v1, v2;
    test_check(graph_eval_output(&g, ctr, 0, &a, &v1, &eval_ctx) == 0, "eval counter (1st read)");
    test_check(graph_eval_output(&g, ctr, 0, &a, &v2, &eval_ctx) == 0, "eval counter (2nd read)");
    test_check(g_count == 1, "memo: eval ran once for two reads");
    test_check(v1.f == 1.0f && v2.f == 1.0f, "memo: both reads see the same value");

    /* 4. cycle guard */
    eval_context_reset_runtime(&eval_ctx);
    arena_reset(&a);
    node *A = graph_add_node(&g, add);
    node *B = graph_add_node(&g, add);
    graph_connect(&g, A, 0, B, 0); /* A.out -> B.a */
    graph_connect(&g, B, 0, A, 0); /* B.out -> A.a */
    pin_value junk;
    test_check(graph_eval_output(&g, A, 0, &a, &junk, &eval_ctx) != 0, "cycle detected (eval returns error)");
    test_check(
        eval_context_status(&eval_ctx)->code == -2 &&
            eval_context_status(&eval_ctx)->node == A &&
            eval_context_status(&eval_ctx)->pin >= -1,
        "evaluation status lives on the explicit context"
    );
    eval_context independent_a, independent_b;
    eval_context_init(&independent_a);
    eval_context_init(&independent_b);
    pin_value isolated;
    eval_context_reset_runtime(&eval_ctx);
    arena_reset(&a);
    test_check(
        graph_eval_output(&g, c1, 0, &a, &isolated, &independent_a) == 0 &&
            eval_context_status(&independent_a)->code == 0 &&
            eval_context_status(&independent_b)->code == 0,
        "evaluation status is isolated per context"
    );

    /* 5. remove a node and its edges */
    int nb = acount(g.nodes);
    int eb = acount(g.edges);
    test_check(graph_remove_node(&g, A) == 0, "remove node A");
    test_check(acount(g.nodes) == nb - 1, "node count drops by one");
    test_check(acount(g.edges) == eb - 2, "A's two edges dropped");
    test_check(graph_remove_node(&g, A) == -1, "removing a missing node fails");

    /* Phase 2 identity and instance-pin invariants. */
    {
        graph ids;
        graph_init(&ids);
        node *a_id = graph_add_node(&ids, add);
        uint64_t retired = a_id->id;
        uint64_t retired_pin = a_id->output_pins[0].id;
        test_check(graph_remove_node(&ids, a_id) == 0, "deleting a node retires its ID");
        node *b_id = graph_add_node(&ids, add);
        test_check(
            b_id->id != retired && b_id->output_pins[0].id != retired_pin &&
                graph_node_by_id(&ids, retired) == NULL,
            "new allocations never reuse retired IDs"
        );
        node *p1 = graph_add_node(&ids, add);
        node *p2 = graph_add_node(&ids, add);
        test_check(
            graph_set_pin_name(&ids, p1, 0, 0, "left") == 0 &&
                strcmp(p1->input_pins[0].name, "left") == 0 &&
                strcmp(p2->input_pins[0].name, "a") == 0,
            "instance pin edits do not mutate sibling instances"
        );
        test_check(
            strcmp(add->inputs[0].name, "a") == 0 &&
                strcmp(p2->input_pins[0].name, "a") == 0,
            "instance pin storage is copied from an untouched descriptor "
            "template"
        );
        pin_value authored = { .type = PIN_F32, .type_name = "float", .f = 7.0f };
        test_check(
            graph_set_pin_default(&ids, p1, 0, 0, &authored) == 0 &&
                p1->input_pins[0].default_value.f == 7.0f,
            "authored defaults have separate instance storage"
        );
        pin_spec extra = { "ThenExtra", PIN_FLOW, NULL, PIN_EXEC };
        node *seq = graph_add_node(&ids, registry_node_by_name("Sequence"));
        int old_outputs = seq->output_count;
        test_check(
            graph_add_pin(&ids, seq, 1, &extra) == old_outputs &&
                seq->output_count == old_outputs + 1,
            "user-extensible groups add instance pins"
        );
        test_check(
            graph_remove_pin(&ids, seq, 1, old_outputs) == 0 &&
                seq->output_count == old_outputs,
            "removing an instance pin succeeds"
        );
        test_check(
            b_id->id != 0 && b_id->input_pins[0].id != 0 && b_id->output_pins[0].id != 0,
            "nodes and pins carry stable non-zero IDs"
        );
        test_check(
            b_id->input_pins[0].name && b_id->input_pins[0].type_name &&
                b_id->input_pins[0].kind == PIN_DATA,
            "instance pins carry identity, shape, kind, and defaults"
        );
        test_check(b_id->desc == add, "each node references one descriptor");
        b_id->x = 3.5f;
        b_id->y = -2.25f;
        test_check(b_id->x == 3.5f && b_id->y == -2.25f, "node canvas position is authored data");
        test_check(
            node_desc_is_pure(&g_const_desc) && !node_desc_is_pure(&g_sink_a_desc),
            "descriptors explicitly distinguish pure and impure"
        );
        test_check(g_const_desc.display_class == NODE_DISPLAY_LITERAL, "descriptors carry a display class");
        test_check(
            graph_node_by_id(&ids, 0) == NULL &&
                graph_pin_by_id(b_id, 0, NULL, NULL) == NULL,
            "zero is never an allocated or resolvable ID"
        );
        char *id_json = graph_save_json(&ids);
        graph *id_loaded = id_json ? graph_load_json(id_json) : NULL;
        test_check(
            id_loaded && graph_node_by_id(id_loaded, b_id->id) &&
                graph_pin_by_id(
                    graph_node_by_id(id_loaded, b_id->id),
                    b_id->input_pins[0].id,
                    NULL,
                    NULL
                ),
            "node and pin IDs survive save/load"
        );
        if (id_loaded) {
            graph_free(id_loaded);
            free(id_loaded);
        }
        free(id_json);
        test_check(
            graph_connect_data_ids(
                &ids,
                p1->id,
                p1->output_pins[0].id,
                p2->id,
                p2->input_pins[0].id
            ) != 0 ||
                acount(ids.edges) == 1,
            "cross-entity APIs address endpoints by IDs"
        );
        graph_free(&ids);
    }

    {
        graph authored;
        graph_init(&authored);
        node *literal = graph_add_node(&authored, registry_node_by_name("Real"));
        pin_value changed = { .type = PIN_F64, .type_name = "double", .d = 9.0 };
        arena authored_arena;
        arena_init(&authored_arena, 4096);
        pin_value evaluated;
        char *authored_json = NULL;
        graph *authored_loaded = NULL;
        test_check(
            graph_set_pin_default(&authored, literal, 0, 0, &changed) == 0 &&
                graph_eval_output(&authored, literal, 0, &authored_arena, &evaluated, &eval_ctx) ==
                    0 &&
                evaluated.d == 9.0 && literal->input_pins[0].default_value.d == 9.0,
            "setting an authored default changes evaluation after a "
            "later eval"
        );
        authored_json = graph_save_json(&authored);
        authored_loaded = authored_json ? graph_load_json(authored_json) : NULL;
        test_check(
            authored_loaded &&
                authored_loaded->nodes[0]->input_pins[0].default_value.d == 9.0,
            "changed authored default is recorded by save"
        );
        arena_free(&authored_arena);
        if (authored_loaded) {
            graph_free(authored_loaded);
            free(authored_loaded);
        }
        free(authored_json);
        graph_free(&authored);
    }

    {
        graph real;
        graph_init(&real);
        node *real_event = graph_add_node(&real, registry_node_by_name("Event"));
        node *real_delay = graph_add_node(&real, registry_node_by_name("Delay"));
        node *real_const = make_const(&real, 2.5f);
        test_check(
            graph_connect_data(&real, real_const, 0, real_delay, 1) == 0 &&
                real_delay->input_pins[1].type == PIN_F64,
            "F32 output connects to F64 Real input"
        );
        static pin_spec int_output[] = { { "value", PIN_I32, "int" } };
        static pin_spec int64_input[] = { { "value", PIN_I64, "int64" } };
        static node_desc int_source = {
            .name = "test_int_source",
            .output_count = 1,
            .outputs = int_output,
            .pure_kind = NODE_PURE_CONST,
        };
        static node_desc int_target = {
            .name = "test_int_target",
            .input_count = 1,
            .inputs = int64_input,
            .pure_kind = NODE_PURE_CONST,
        };
        node *integer = graph_add_node(&real, &int_source);
        node *integer64 = graph_add_node(&real, &int_target);
        test_check(graph_connect(&real, integer, 0, integer64, 0) != 0, "Integer output remains incompatible with Integer64 input");
        graph_set_event_name(&real, real_event, "Real");
        graph_connect_exec(&real, real_event, 0, real_delay, 0, 0);
        flow_run *real_run = graph_run_event(&real, "Real", (flow_inputs){ 0 }, &dispatch_error);
        pin_value real_input;
        test_check(
            real_run &&
                flow_run_watch_input(real_run, real_delay, 1, &real_input) == 0 &&
                real_input.type == PIN_F64 && real_input.d == 2.5,
            "Real boundary converts F32 value to F64 for Delay"
        );
        if (real_run) {
            flow_run_free(real_run);
        }

        pin_spec f32 = { .type = PIN_F32, .type_name = "float" };
        pin_spec f64 = { .type = PIN_F64, .type_name = "double" };
        pin_value converted = { .type = PIN_F64, .type_name = "double", .d = 7.25 };
        test_check(
            pin_compatible(&f32, &f64) && pin_coerce(&converted, &f32) &&
                converted.type == PIN_F32 && converted.f == 7.25f,
            "Real boundary also converts F64 to F32"
        );
        graph_free(&real);
    }

    {
        graph literals;
        graph_init(&literals);
        const char *names[] = { "Bool", "Byte", "Integer", "Integer64",
                                "Real", "Name", "String" };
        for (int i = 0; i < 7; i++) {
            test_check(registry_node_by_name(names[i]) != NULL, "core literal is registered");
        }
        node *lb = graph_add_node(&literals, registry_node_by_name("Bool"));
        node *lbyte = graph_add_node(&literals, registry_node_by_name("Byte"));
        node *li = graph_add_node(&literals, registry_node_by_name("Integer"));
        node *li64 = graph_add_node(&literals, registry_node_by_name("Integer64"));
        node *lr = graph_add_node(&literals, registry_node_by_name("Real"));
        node *ln = graph_add_node(&literals, registry_node_by_name("Name"));
        node *ls = graph_add_node(&literals, registry_node_by_name("String"));
        test_check(
            lb && lb->input_count == 1 && lb->output_count == 1 &&
                lb->input_pins[0].kind == PIN_DATA && lb->output_pins[0].kind == PIN_DATA,
            "a literal has one Value input and one return output"
        );
        pin_value vb = { .type = PIN_BOOL, .b = true },
                  vbyte = { .type = PIN_U8, .u8 = 7 };
        pin_value vi = { .type = PIN_I32, .i = 8 },
                  vi64 = { .type = PIN_I64, .i64 = 9 };
        pin_value vr = { .type = PIN_F64, .d = 2.5 },
                  vn = { .type = PIN_NAME, .str = "ExactName" };
        pin_value vs = { .type = PIN_STR, .str = "literal" };
        graph_set_pin_default(&literals, lb, 0, 0, &vb);
        graph_set_pin_default(&literals, lbyte, 0, 0, &vbyte);
        graph_set_pin_default(&literals, li, 0, 0, &vi);
        graph_set_pin_default(&literals, li64, 0, 0, &vi64);
        graph_set_pin_default(&literals, lr, 0, 0, &vr);
        graph_set_pin_default(&literals, ln, 0, 0, &vn);
        graph_set_pin_default(&literals, ls, 0, 0, &vs);
        char *literal_json = graph_save_json(&literals);
        graph *literal_loaded = literal_json ? graph_load_json(literal_json) : NULL;
        int literal_values = 0;
        if (literal_loaded && acount(literal_loaded->nodes) == 7) {
            for (int i = 0; i < acount(literal_loaded->nodes); i++) {
                node *n = literal_loaded->nodes[i];
                if (strcmp(n->desc->name, "Real") == 0 &&
                    n->input_pins[0].default_value.d == 2.5) {
                    literal_values++;
                }
                if (strcmp(n->desc->name, "String") == 0 &&
                    n->input_pins[0].default_value.str &&
                    strcmp(n->input_pins[0].default_value.str, "literal") == 0) {
                    literal_values++;
                }
                if (strcmp(n->desc->name, "Name") == 0 &&
                    n->input_pins[0].default_value.str &&
                    strcmp(n->input_pins[0].default_value.str, "ExactName") == 0) {
                    literal_values++;
                }
            }
        }
        test_check(literal_values == 3, "literals hold authored values through save/load");
        if (literal_loaded) {
            graph_free(literal_loaded);
            free(literal_loaded);
        }
        free(literal_json);
        graph_free(&literals);
    }

    {
        graph ordering;
        graph_init(&ordering);
        node *early = make_const(&ordering, 1.0f);
        node *late = make_const(&ordering, 2.0f);
        node *target = graph_add_node(&ordering, add);
        test_check(graph_connect(&ordering, late, 0, target, 0) == 0, "ID edge is authored before deletion");
        test_check(graph_remove_node(&ordering, early) == 0, "deleting an earlier array entry succeeds");
        node *source = NULL;
        int source_pin = -1;
        test_check(
            graph_input_source(&ordering, target, 0, &source, &source_pin) &&
                source == late && source_pin == 0,
            "edge still resolves to the original node after reorder"
        );
        graph_free(&ordering);
    }

    /* 6. graph_connect replaces an existing data-input driver. */
    {
        graph g_replace;
        graph_init(&g_replace);
        node *r1 = make_const(&g_replace, 4.0f);
        node *r2 = make_const(&g_replace, 5.0f);
        node *ra = graph_add_node(&g_replace, add);
        test_check(graph_connect(&g_replace, r1, 0, ra, 0) == 0, "replace: first driver connects");
        test_check(graph_connect(&g_replace, r2, 0, ra, 0) == 0, "replace: second driver connects");
        test_check(acount(g_replace.edges) == 1 && g_replace.edges[0].from_id == r2->id, "replace: input has one driver");
        graph_free(&g_replace);
    }

    /* 7. graph_remove_edge removes an existing edge. */
    {
        graph g6;
        graph_init(&g6);
        node *rc1 = make_const(&g6, 4.0f);
        node *rc2 = make_const(&g6, 5.0f);
        node *radd = graph_add_node(&g6, add);
        test_check(graph_connect(&g6, rc1, 0, radd, 0) == 0, "remove_edge: c1 -> add.a");
        test_check(graph_connect(&g6, rc2, 0, radd, 0) == 0, "remove_edge: c2 -> add.a replaces c1");
        test_check(graph_connect(&g6, rc2, 0, radd, 1) == 0, "remove_edge: c2 -> add.b");
        test_check(acount(g6.edges) == 2, "remove_edge: two edges");
        test_check(graph_remove_edge(&g6, rc1, 0, radd, 0) == -1, "remove_edge: replaced edge is absent");
        test_check(graph_remove_edge(&g6, rc2, 0, radd, 0) == 0, "remove_edge: c2 -> add.a removed");
        test_check(acount(g6.edges) == 1, "remove_edge: one edge remains");

        arena a6;
        arena_init(&a6, 4096);
        pin_value out;
        test_check(graph_eval_output(&g6, radd, 0, &a6, &out, &eval_ctx) == 0, "remove_edge: eval after removal");
        test_check(out.type == PIN_F32 && out.f == 5.0f, "remove_edge: unconnected a defaults to zero");
        arena_free(&a6);
        graph_free(&g6);
    }

    {
        graph fan;
        graph_init(&fan);
        node *driver = make_const(&fan, 8.0f);
        node *fan_a = graph_add_node(&fan, add);
        node *fan_b = graph_add_node(&fan, add);
        node *fan_c = graph_add_node(&fan, add);
        test_check(
            graph_connect(&fan, driver, 0, fan_a, 0) == 0 &&
                graph_connect(&fan, driver, 0, fan_b, 0) == 0 &&
                graph_connect(&fan, driver, 0, fan_c, 0) == 0,
            "one data output drives three inputs"
        );
        arena fan_arena;
        arena_init(&fan_arena, 4096);
        pin_value fan_value;
        test_check(
            graph_eval_output(&fan, fan_a, 0, &fan_arena, &fan_value, &eval_ctx) == 0 &&
                fan_value.f == 8.0f &&
                graph_eval_output(&fan, fan_b, 0, &fan_arena, &fan_value, &eval_ctx) == 0 &&
                fan_value.f == 8.0f &&
                graph_eval_output(&fan, fan_c, 0, &fan_arena, &fan_value, &eval_ctx) == 0 &&
                fan_value.f == 8.0f,
            "every driven input receives the output value"
        );
        test_check(
            graph_remove_edge(&fan, driver, 0, fan_b, 0) == 0 && acount(fan.edges) == 2,
            "removing one fan-out edge leaves the other two"
        );
        arena_free(&fan_arena);
        graph_free(&fan);
    }

    eval_context_free(&independent_a);
    eval_context_free(&independent_b);
    graph_free(&g);
    arena_free(&a);
    eval_context_free(&eval_ctx);
}
