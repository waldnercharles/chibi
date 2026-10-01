#include "graph/runtime/runtime.h"
#include "test/suite_internal.h"

static void evaluation_lifetime_checks()
{
    graph g;
    eval_context context;
    eval_context_init(&context);
    bool fresh = true;
    for (int i = 1; i <= 64; i++) {
        graph_init(&g);
        arena storage;
        arena_init(&storage, 1024);
        node *value = graph_add_node(&g, registry_node_by_name("Integer"));
        graph_set_pin_default(&g, value, 0, 0, &(pin_value){ .type = PIN_I32, .i = i });
        pin_value result = { 0 };
        fresh &= graph_eval_output(&g, value, 0, &storage, &result, &context) == 0 &&
                 result.type == PIN_I32 && result.i == i;
        graph_free(&g);
        arena_free(&storage);
    }
    eval_context_free(&context);
    test_check(fresh, "one evaluator never reuses memo values after a graph is recreated at the same address");

    graph_init(&g);
    node *value = graph_add_node(&g, registry_node_by_name("Integer"));
    uint64_t lifetime = g.lifetime_id;
    runtime_store *borrowed = runtime_store_create(&g);
    runtime_store_node(borrowed, value);
    graph_set_pin_default(&g, value, 0, 0, &(pin_value){ .type = PIN_I32, .i = 7 });
    char *json = graph_save(&g);
    graph *copy = graph_load(json);
    test_check(
        g.lifetime_id == lifetime && runtime_store_matches_graph(borrowed, &g) &&
            copy && copy->lifetime_id != lifetime &&
            !runtime_store_matches_graph(borrowed, copy) &&
            !strstr(json, "lifetime_id"),
        "graph edits retain runtime identity while loaded copies receive a "
        "distinct nonserialized lifetime"
    );
    graph_free(copy);
    free(copy);
    free(json);
    graph_free(&g);
    graph_init(&g);
    value = graph_add_node(&g, registry_node_by_name("Integer"));
    test_check(!runtime_store_matches_graph(borrowed, &g) && !runtime_store_node_const(borrowed, value), "a borrowed runtime store refuses a different graph lifetime at the same address");
    runtime_store_free(borrowed);
    graph_free(&g);
}

static void vector_binding_checks()
{
    const char *types[] = { "CF_V2", "CF_V3", "ShaderVec2", "ShaderVec3", "ShaderVec4" };
    arena storage;
    arena_init(&storage, 4096);
    for (int i = 0; i < 5; i++) {
        pin_spec target = { .type = PIN_STRUCT, .type_name = types[i] };
        pin_spec scalar = { .type = PIN_F64, .type_name = "double" };
        const node_desc *conversion = conversion_for_pins(&scalar, &target);
        assert(conversion && conversion_is_vector_splat(conversion));
        pin_value value = { .type = PIN_F64, .d = -2.75 };
        pin_value result = conversion->fn(&value, 1, &storage);
        for (int axis = 0; axis < pin_vector_width(&target); axis++) {
            test_check(((float *)result.ptr)[axis] == -2.75f, "scalar conversion fills every vector component");
        }
    }
    for (int count = 2; count <= 3; count++) {
        graph g;
        graph_init(&g);
        node *target = graph_add_node(&g, registry_node_by_name(count == 2 ? "Break_CF_V2" : "Break_CF_V3"));
        node *number = graph_add_node(&g, registry_node_by_name("Integer"));
        graph_set_pin_default(&g, number, false, 0, &(pin_value){ .type = PIN_I32, .i = 7 });
        const node_desc *conversion = conversion_for_pins(
            &number->output_pins[0],
            &target->input_pins[0]
        );
        assert(graph_connect_data_conversion(&g, number, 0, target, 0, conversion) == 0);
        assert(graph_set_node_presentation(&g, number, NODE_PRESENTATION_INLINE) == 0);
        char *json = graph_save(&g);
        graph *copy = graph_load(json);
        assert(copy);
        eval_context context;
        eval_context_init(&context);
        for (int axis = 0; axis < count; axis++) {
            pin_value value = { 0 };
            assert(
                graph_eval_output(copy, graph_node_by_id(copy, target->id), axis, &storage, &value, &context) ==
                0
            );
            test_check(value.type == PIN_F32 && value.f == 7, "saved inline integer broadcasts through runtime vector inputs");
        }
        eval_context_free(&context);
        graph_free(copy);
        free(copy);
        free(json);
        graph_remove_node(&g, number);
        node *vector = graph_add_node(&g, graph_vector_constructor(&target->input_pins[0]));
        assert(graph_connect_data(&g, vector, 0, target, 0) == 0);
        assert(graph_set_node_presentation(&g, vector, NODE_PRESENTATION_INLINE) == 0);
        graph_set_pin_default(&g, vector, false, 1, &(pin_value){ .type = PIN_F32, .f = 3 });
        number = graph_add_node(&g, registry_node_by_name("Real"));
        graph_set_pin_default(&g, number, false, 0, &(pin_value){ .type = PIN_F64, .d = 2 });
        assert(graph_connect_data(&g, number, 0, vector, 0) == 0);
        assert(graph_set_node_presentation(&g, number, NODE_PRESENTATION_INLINE) == 0);
        test_check(
            graph_node_is_inline(&g, vector) && graph_node_is_inline(&g, number) &&
                graph_inline_owner(&g, number) == target,
            "vector owns independently bound component sources"
        );
        json = graph_save(&g);
        copy = graph_load(json);
        assert(copy && graph_node_is_inline(copy, graph_node_by_id(copy, number->id)));
        eval_context_init(&context);
        for (int axis = 0; axis < count; axis++) {
            pin_value value = { 0 };
            assert(
                graph_eval_output(copy, graph_node_by_id(copy, target->id), axis, &storage, &value, &context) ==
                0
            );
            test_check(
                value.f == (axis == 0   ? 2
                            : axis == 1 ? 3
                                        : 0),
                "saved inline vector combines bound and literal components"
            );
        }
        eval_context_free(&context);
        graph_free(copy);
        free(copy);
        free(json);
        graph_remove_node_tree(&g, target);
        test_check(acount(g.nodes) == 0, "removing vector owner removes its nested inline sources");
        graph_free(&g);
    }
    arena_free(&storage);
}

void run_graph_conversion_checks()
{
    evaluation_lifetime_checks();
    vector_binding_checks();
    eval_context eval_ctx;
    eval_context_init(&eval_ctx);
    /* TYPE-7 / CAT-26 / UI-22: explicit conversion nodes. conversion_for()
     * answers the permitted pairs and refuses the lossy ones. */
    {
        test_check(
            registry_node_by_name("To String") && registry_node_by_name("To Real") &&
                registry_node_by_name("To Integer64") &&
                registry_node_by_name("Format Text"),
            "conversion and Format Text nodes are registered"
        );
        test_check(
            registry_node_by_name("To Real")->category &&
                registry_node_by_name("Format Text")->keywords,
            "conversion entries expose category paths and search "
            "keywords"
        );
        test_check(
            conversion_for(PIN_I32, PIN_F64) == &NODE_To_Real &&
                conversion_for(PIN_I32, PIN_F32) == &NODE_To_Real &&
                conversion_for(PIN_U8, PIN_F64) == &NODE_To_Real,
            "Integer and Byte to Real have an explicit conversion"
        );
        test_check(conversion_for(PIN_I32, PIN_I64) == &NODE_To_Integer64, "Integer widens to Integer64 within one signedness");
        test_check(conversion_for(PIN_I32, PIN_STR) == &NODE_To_String, "Integer to String has an explicit conversion");
        test_check(conversion_for(PIN_F64, PIN_STR) == &NODE_To_String, "Real to String has an explicit conversion");
        test_check(conversion_for(PIN_BOOL, PIN_STR) == &NODE_To_String, "Bool to String has an explicit conversion");
        test_check(conversion_for(PIN_NAME, PIN_STR) == &NODE_To_String, "Name to String has an explicit conversion");
        test_check(conversion_for(PIN_STR, PIN_NAME) == &NODE_To_Name, "String to Name supports authored prototype references");
        test_check(conversion_for(PIN_STR, PIN_F64) == NULL, "String to Real is not a permitted conversion");
        test_check(conversion_for(PIN_F64, PIN_I32) == &NODE_To_Integer, "Real to Integer uses explicit saturating truncation");
        test_check(conversion_for(PIN_I64, PIN_I32) == NULL, "Integer64 to Integer narrowing is not offered");
    }

    {
        char text[768];
        memset(text, 'a', sizeof text - 1);
        text[sizeof text - 1] = 0;
        arena storage;
        arena_init(&storage, 4096);
        pin_value input = { .type = PIN_STR, .type_name = "const char *", .str = text };
        pin_value name = NODE_To_Name.fn(&input, 1, &storage);
        text[0] = 'b';
        test_check(
            name.type == PIN_NAME && !strcmp(name.type_name, "name") &&
                strlen(name.str) == sizeof text - 1 && name.str[0] == 'a',
            "To Name preserves complete text and owns its conversion result"
        );
        arena_free(&storage);
    }

    /* CAT-26: each conversion converts correctly headless, with the input
     * retyped to the source type (PIN-3). */
    {
        graph gc;
        graph_init(&gc);
        const node_desc *real_lit = registry_node_by_name("Real");
        const node_desc *int_lit = registry_node_by_name("Integer");
        const node_desc *byte_lit = registry_node_by_name("Byte");
        const node_desc *bool_lit = registry_node_by_name("Bool");
        const node_desc *name_lit = registry_node_by_name("Name");
        arena ac;
        arena_init(&ac, 4096);
        pin_value out;

        node *to_real = graph_add_node(&gc, &NODE_To_Real);
        node *ival = graph_add_node(&gc, int_lit);
        graph_set_pin_default(
            &gc,
            ival,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 42 }
        );
        graph_connect(&gc, ival, 0, to_real, 0);
        test_check(
            graph_eval_output(&gc, to_real, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_F64 && out.d == 42.0,
            "To Real converts Integer 42 to Real 42.0"
        );

        /* The reported case: an Integer onto a float input. To
         * Real offers, and its F64 output coerces to float at the boundary
         * (TYPE-10). */
        node *addf = graph_add_node(&gc, &g_test_add_desc);
        graph_connect(&gc, to_real, 0, addf, 0);
        test_check(
            graph_eval_output(&gc, addf, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_F32 && out.f == 42.0f,
            "Integer through To Real feeds a float input at the "
            "boundary"
        );

        node *to_real_b = graph_add_node(&gc, &NODE_To_Real);
        graph_set_pin_type(&gc, to_real_b, 0, 0, PIN_U8, "byte");
        node *bval = graph_add_node(&gc, byte_lit);
        graph_set_pin_default(
            &gc,
            bval,
            0,
            0,
            &(pin_value){ .type = PIN_U8, .type_name = "byte", .u8 = 7 }
        );
        graph_connect(&gc, bval, 0, to_real_b, 0);
        test_check(
            graph_eval_output(&gc, to_real_b, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_F64 && out.d == 7.0,
            "a Byte feeds a retyped To Real input and converts to 7.0"
        );

        node *to_i64 = graph_add_node(&gc, &NODE_To_Integer64);
        node *i64in = graph_add_node(&gc, int_lit);
        graph_set_pin_default(
            &gc,
            i64in,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = -3 }
        );
        graph_connect_data_conversion(&gc, i64in, 0, to_i64, 0, &NODE_To_Real);
        test_check(
            graph_eval_output(&gc, to_i64, 0, &ac, &out, &eval_ctx) == 0 &&
                out.type == PIN_I64 && out.i64 == -3,
            "To Integer64 widens Integer -3 to Integer64"
        );

        node *to_str = graph_add_node(&gc, &NODE_To_String);
        graph_set_pin_type(&gc, to_str, 0, 0, PIN_F64, "double");
        node *rval = graph_add_node(&gc, real_lit);
        graph_set_pin_default(
            &gc,
            rval,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.5 }
        );
        graph_connect(&gc, rval, 0, to_str, 0);
        char expected_str[64];
        snprintf(
            expected_str,
            sizeof expected_str,
            "%s",
            pin_value_to_string(
                &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.5 }
            )
        );
        test_check(
            graph_eval_output(&gc, to_str, 0, &ac, &out, &eval_ctx) == 0 &&
                strcmp(out.str, expected_str) == 0,
            "To String renders a Real identically to "
            "pin_value_to_string"
        );

        node *to_str_bool = graph_add_node(&gc, &NODE_To_String);
        graph_set_pin_type(&gc, to_str_bool, 0, 0, PIN_BOOL, "bool");
        node *boolval = graph_add_node(&gc, bool_lit);
        graph_set_pin_default(
            &gc,
            boolval,
            0,
            0,
            &(pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = 1 }
        );
        graph_connect(&gc, boolval, 0, to_str_bool, 0);
        node *to_str_name = graph_add_node(&gc, &NODE_To_String);
        graph_set_pin_type(&gc, to_str_name, 0, 0, PIN_NAME, "name");
        node *nameval = graph_add_node(&gc, name_lit);
        graph_set_pin_default(
            &gc,
            nameval,
            0,
            0,
            &(pin_value){ .type = PIN_NAME, .type_name = "name", .str = "Foo" }
        );
        graph_connect(&gc, nameval, 0, to_str_name, 0);
        node *to_str_int = graph_add_node(&gc, &NODE_To_String);
        graph_set_pin_type(&gc, to_str_int, 0, 0, PIN_I32, "int");
        node *intval = graph_add_node(&gc, int_lit);
        graph_set_pin_default(
            &gc,
            intval,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = -7 }
        );
        graph_connect(&gc, intval, 0, to_str_int, 0);
        test_check(
            graph_eval_output(&gc, to_str_bool, 0, &ac, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "true") == 0 &&
                graph_eval_output(&gc, to_str_name, 0, &ac, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "Foo") == 0 &&
                graph_eval_output(&gc, to_str_int, 0, &ac, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "-7") == 0,
            "To String renders Bool, Name, and Integer scalars"
        );

        /* The conversion output must not alias the pin_value_to_string ring. */
        pin_value saved = out;
        for (int i = 0; i < 8; i++) {
            (void)pin_value_to_string(&(pin_value){ .type = PIN_F64,
                                                    .type_name = "double",
                                                    .d = (double)i });
        }
        test_check(strcmp(saved.str, "-7") == 0, "To String copies into the arena, not the format ring");

        arena_free(&ac);
        graph_free(&gc);
    }

    /* CAT-38: Format Text substitutes {n} and {} placeholders, renders a
     * missing argument as empty, and its argument pins are Generic
     * wildcards (PIN-11) that adopt the connected type. */
    {
        graph gf;
        graph_init(&gf);
        arena af;
        arena_init(&af, 8192);
        pin_value out;
        node *fmt = graph_add_node(&gf, &NODE_Format_Text);
        test_check(
            fmt->input_count == 2 && (fmt->input_pins[1].flags & PIN_FLAG_WILDCARD) &&
                fmt->input_pins[1].type == PIN_FLOW,
            "Format Text starts with Format and one Generic spare"
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "Hello {0} World" }
        );
        node *s1 = graph_add_node(&gf, registry_node_by_name("String"));
        graph_set_pin_default(
            &gf,
            s1,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "Bob" }
        );
        graph_connect(&gf, s1, 0, fmt, 1);
        test_check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "Hello Bob World") == 0,
            "\"Hello {0} World\" with a String argument renders "
            "\"Hello Bob World\""
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "{1}" }
        );
        eval_context_reset_runtime(&eval_ctx);
        test_check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                out.str[0] == '\0',
            "{1} with one argument renders as the empty string"
        );
        /* The reported crash: an Integer driven straight onto the spare
         * replaces the String driver, re-adopts the pin (PIN-11), and
         * renders like its pin display (UI-39). */
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "n={0}" }
        );
        node *ival2 = graph_add_node(&gf, registry_node_by_name("Integer"));
        graph_set_pin_default(
            &gf,
            ival2,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 42 }
        );
        graph_connect(&gf, ival2, 0, fmt, 1);
        eval_context_reset_runtime(&eval_ctx);
        test_check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=42") == 0,
            "a non-string argument renders like its pin display"
        );
        /* {} consumes arguments in order; a Real through a To String
         * conversion renders like its pin display. */
        pin_spec arg1 = { .name = "Arg1",
                          .type = PIN_FLOW,
                          .kind = PIN_DATA,
                          .flags = PIN_FLAG_WILDCARD };
        graph_add_pin(&gf, fmt, 0, &arg1);
        char dynamic_arg_name[32] = "Arg2";
        pin_spec dynamic_arg = { .name = dynamic_arg_name,
                                 .type = PIN_FLOW,
                                 .kind = PIN_DATA,
                                 .flags = PIN_FLAG_WILDCARD };
        graph_add_pin(&gf, fmt, 0, &dynamic_arg);
        snprintf(dynamic_arg_name, sizeof dynamic_arg_name, "renamed");
        test_check(strcmp(fmt->input_pins[3].name, "Arg2") == 0, "dynamically added pin owns its name");
        node *to_str = graph_add_node(&gf, &NODE_To_String);
        graph_set_pin_type(&gf, to_str, 0, 0, PIN_F64, "double");
        node *rlit = graph_add_node(&gf, registry_node_by_name("Real"));
        graph_set_pin_default(
            &gf,
            rlit,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.5 }
        );
        graph_connect(&gf, rlit, 0, to_str, 0);
        graph_connect(&gf, to_str, 0, fmt, 2);
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "{} {}" }
        );
        eval_context_reset_runtime(&eval_ctx);
        test_check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "42 3.5") == 0,
            "{} consumes arguments in order and a Real through To "
            "String renders like its pin display"
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "|{:>8}|" }
        );
        eval_context_reset_runtime(&eval_ctx);
        test_check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "|      42|") == 0,
            "{:>8} applies printf alignment to an argument"
        );
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "a{{b}}c" }
        );
        eval_context_reset_runtime(&eval_ctx);
        test_check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "a{b}c") == 0,
            "{{ and }} print literal braces"
        );
        /* Precision and base specifiers render through the value's own C
         * type: {:.2f} on a Real, {:x} and {:08d} on an Integer. */
        pin_spec spare2 = { .name = "Arg2",
                            .type = PIN_FLOW,
                            .kind = PIN_DATA,
                            .flags = PIN_FLAG_WILDCARD };
        graph_add_pin(&gf, fmt, 0, &spare2);
        node *rlit2 = graph_add_node(&gf, registry_node_by_name("Real"));
        graph_set_pin_default(
            &gf,
            rlit2,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.14159 }
        );
        graph_connect(&gf, rlit2, 0, fmt, 3);
        graph_set_pin_default(
            &gf,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "|{2:.2f}|{0:x}|{0:08d}|{0:.2f}|" }
        );
        eval_context_reset_runtime(&eval_ctx);
        test_check(
            graph_eval_output(&gf, fmt, 0, &af, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "|3.14|2a|00000042|42.00|") == 0,
            "precision and base specifiers render numeric values"
        );
        arena_free(&af);
        graph_free(&gf);
    }

    /* PIN-11: a Generic argument pin adopts the peer's type on connection
     * and reverts to Generic when its edge goes away. */
    {
        graph gp;
        graph_init(&gp);
        arena ap;
        arena_init(&ap, 8192);
        pin_value out;
        node *fmt = graph_add_node(&gp, &NODE_Format_Text);
        graph_set_pin_default(
            &gp,
            fmt,
            0,
            0,
            &(pin_value){ .type = PIN_STR, .type_name = "const char *", .str = "n={0}" }
        );
        node *ival = graph_add_node(&gp, registry_node_by_name("Integer"));
        graph_set_pin_default(
            &gp,
            ival,
            0,
            0,
            &(pin_value){ .type = PIN_I32, .type_name = "int", .i = 42 }
        );
        graph_connect(&gp, ival, 0, fmt, 1);
        test_check(
            fmt->input_pins[1].type == PIN_I32 &&
                (fmt->input_pins[1].flags & PIN_FLAG_WILDCARD),
            "connecting to the spare adopts the peer's type"
        );
        test_check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=42") == 0,
            "an adopted Generic argument evaluates its peer"
        );
        /* A second driver re-adopts (PIN-6 replacement on a wildcard). */
        node *rval = graph_add_node(&gp, registry_node_by_name("Real"));
        graph_set_pin_default(
            &gp,
            rval,
            0,
            0,
            &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 3.5 }
        );
        graph_connect(&gp, rval, 0, fmt, 1);
        test_check(fmt->input_pins[1].type == PIN_F64, "replacing the driver re-adopts the new type");
        eval_context_reset_runtime(&eval_ctx);
        test_check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=3.5") == 0,
            "the re-adopted argument evaluates the new peer"
        );
        graph_remove_edge(&gp, rval, 0, fmt, 1);
        int reverted = fmt->input_pins[1].type == PIN_FLOW &&
                       (fmt->input_pins[1].flags & PIN_FLAG_WILDCARD) &&
                       acount(gp.edges) == 0;
        /* Removing the driver node reverts the target the same way. */
        graph_connect(&gp, ival, 0, fmt, 1);
        graph_remove_node(&gp, ival);
        test_check(
            reverted && fmt->input_pins[1].type == PIN_FLOW &&
                (fmt->input_pins[1].flags & PIN_FLAG_WILDCARD),
            "removing the edge or its driver reverts the Generic pin"
        );
        /* An unconnected spare renders as an empty argument. */
        eval_context_reset_runtime(&eval_ctx);
        test_check(
            graph_eval_output(&gp, fmt, 0, &ap, &out, &eval_ctx) == 0 &&
                strcmp(out.str, "n=") == 0,
            "an unconnected Generic argument renders empty"
        );
        /* Adopted types survive a save/load round trip. The editor appends
         * the next spare when the last pin is taken, so mirror that and
         * verify the appended spare round-trips as Generic too. */
        graph_connect(&gp, rval, 0, fmt, 1);
        pin_spec spare = { .name = "Arg1",
                           .type = PIN_FLOW,
                           .kind = PIN_DATA,
                           .flags = PIN_FLAG_WILDCARD };
        graph_add_pin(&gp, fmt, 0, &spare);
        char *gp_json = graph_save(&gp);
        graph *gp2 = gp_json ? graph_load(gp_json) : NULL;
        test_check(
            gp2 && gp2->nodes[0]->input_pins[1].type == PIN_F64 &&
                (gp2->nodes[0]->input_pins[1].flags & PIN_FLAG_WILDCARD) &&
                gp2->nodes[0]->input_pins[2].type == PIN_FLOW &&
                (gp2->nodes[0]->input_pins[2].flags & PIN_FLAG_WILDCARD),
            "adopted types and the spare survive save/load"
        );
        if (gp2) {
            arena ap2;
            arena_init(&ap2, 8192);
            eval_context_reset_runtime(&eval_ctx);
            test_check(
                graph_eval_output(gp2, gp2->nodes[0], 0, &ap2, &out, &eval_ctx) == 0 &&
                    strcmp(out.str, "n=3.5") == 0,
                "a loaded Generic argument still evaluates"
            );
            arena_free(&ap2);
            graph_free(gp2);
            free(gp2);
        }
        free(gp_json);
        arena_free(&ap);
        graph_free(&gp);
    }

    eval_context_free(&eval_ctx);
}
