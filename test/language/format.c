#include "authoring/source_format.h"
#include "authoring/source_metadata.h"
#include "authoring/source_minimize.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static script_syntax *source, *formatted;
static char *text, *again;
static char *metadata, *remapped;

static void cleanup()
{
    script_syntax_free(source);
    script_syntax_free(formatted);
    free(text);
    free(again);
    free(metadata);
    free(remapped);
    source = formatted = NULL;
    text = again = NULL;
    metadata = remapped = NULL;
}

static bool format(const char *input)
{
    source = script_parse(input);
    char error[192];
    text = authoring_source_format(source, error, sizeof error);
    if (!text) {
        fprintf(stderr, "format: %s\n", error);
        return false;
    }
    formatted = script_parse(text);
    again = authoring_source_format(formatted, error, sizeof error);
    return again && !strcmp(text, again) && !formatted->diagnostic_count &&
           !strcmp(source->text, input);
}

TEST_CASE(source_format_block_layout)
{
    REQUIRE(format("behavior Demo{count:=1;on Tick{count+=2;}}"));
    REQUIRE(
        !strcmp(text, "behavior Demo {\n    count := 1;\n    on Tick {\n        count += 2;\n    }\n}\n")
    );
    return true;
}
TEST_CASE(source_format_qualified_callable_fields)
{
    REQUIRE(format(
        "behavior Fields { Next::fn(){self.calls:int=0;self.calls+=1;"
        "self.\"saved value\":int=0;} }"
    ));
    REQUIRE(strstr(text, "self.calls: int = 0;"));
    REQUIRE(strstr(text, "self.\"saved value\": int = 0;"));
    return true;
}
TEST_CASE(source_format_reductions)
{
    REQUIRE(format(
        "system Damage(health:Health){reduce health.damage+=2;"
        "reduce min (health.damage,3);reduce max(health.damage,4);"
        "reduce unordered health.damage+=5; }"
    ));
    REQUIRE(!strcmp(
        text,
        "system Damage(health: Health) {\n"
        "    reduce health.damage += 2;\n"
        "    reduce min(health.damage, 3);\n"
        "    reduce max(health.damage, 4);\n"
        "    reduce unordered health.damage += 5;\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_format_component_measures)
{
    REQUIRE(format(
        "// Fired.\ncomponent Projectile{speed : 0 .. 600 u / s\n"
        "// Pull.\naccel:u/s ^ 2;drift:- 1..1 rad\nlevel:float}"
    ));
    REQUIRE(!strcmp(
        text,
        "// Fired.\n"
        "component Projectile {\n"
        "    speed: 0..600 u/s\n"
        "    // Pull.\n"
        "    accel: u/s^2;\n"
        "    drift: -1..1 rad\n"
        "    level: float\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_format_measured_signatures)
{
    REQUIRE(format(
        "library Measures{Travel::(speed:number u / s,elapsed:s)->u{return "
        "speed*elapsed;}"
        "Borrow::(value:ref float charge)->ref float charge{return value;}}"
    ));
    REQUIRE(strstr(text, "speed: number u/s"));
    REQUIRE(strstr(text, "elapsed: s"));
    REQUIRE(strstr(text, "-> u"));
    REQUIRE(strstr(text, "value: ref float charge"));
    REQUIRE(strstr(text, "-> ref float charge"));
    return true;
}

TEST_CASE(source_format_preserves_comments_and_literals)
{
    const char *input = "// module comment\nbehavior \"quoted name\" { value "
                        "label=\"héllo { // }\"; // trailing\n"
                        "/* first\n second */ on Begin { print(label); /* "
                        "inline */ } }";
    REQUIRE(format(input));
    REQUIRE(strstr(text, "// module comment\n"));
    REQUIRE(strstr(text, "\"héllo { // }\""));
    REQUIRE(strstr(text, "; // trailing\n"));
    REQUIRE(strstr(text, "/* first\n second */"));
    REQUIRE(strstr(text, "; /* inline */"));
    return true;
}

TEST_CASE(source_format_preserves_expression_structure)
{
    REQUIRE(format(
        "library Math{Compute :: pure fn(x:int)->(r:int){\n"
        "a:={left:1,right:[2,3]};\n"
        "r=x+\n2*3; if r>0 {return(r);}else{return(-x);}\n"
        "} macro Step()->(){entry Again{} exit Done;} }"
    ));
    REQUIRE(strstr(text, "r = x +\n"));
    REQUIRE(strstr(text, "2 * 3;"));
    REQUIRE(strstr(text, "return (-x);"));
    REQUIRE(strstr(text, "return (r);"));
    REQUIRE(strstr(text, "entry Again {}"));
    REQUIRE(strstr(text, "left: 1, right: [2, 3]"));
    return true;
}

TEST_CASE(source_format_system_types_and_control_flow)
{
    REQUIRE(format(
        "system Move(Position p) without Disabled{query p;\n"
        "on Tick(dt:float){for(i in 0..3){p.x+=cast<float>(i);}\n"
        "while p.x>0{p.x-=1;} switch (p.mode){case 1: {continue;} "
        "case: {break;}}}\n"
        "F :: fn(x:array<int>)->(r:int){return(g<int>(x[0]));}}"
    ));
    REQUIRE(strstr(text, "array<int>"));
    REQUIRE(strstr(text, "g<int>(x[0])"));
    REQUIRE(strstr(text, "cast<float>(i)"));
    return true;
}

TEST_CASE(source_format_switch_arm_indentation)
{
    REQUIRE(format(
        "library Damage{Base::2;DamageFor::(level:int)->number{"
        "switch level{case 1:consume(Base);consume(3);case 2:{consume(4);}"
        "case:consume(5);}}}"
    ));
    REQUIRE(strstr(text, "DamageFor :: (level: int) -> number {"));
    REQUIRE(strstr(text, "case 1:\n                consume(Base);\n                consume(3);"));
    REQUIRE(strstr(text, "            case 2: {\n                consume(4);"));
    REQUIRE(strstr(text, "            case:\n                consume(5);"));
    return true;
}

TEST_CASE(source_format_query_header_words)
{
    REQUIRE(format(
        "system Move query position:Transform,velocity:Velocity? "
        "join aim:Targets.Transform? where not Disabled and "
        "(Targets.Active or ancestor(ChildOf).Active) and has(Targets)"
        "{on Tick{position.x+=1;}}"
    ));
    REQUIRE(
        strstr(text, "where not Disabled and (Targets.Active or ancestor(ChildOf).Active) and has(Targets)")
    );
    REQUIRE(strstr(text, "velocity: Velocity?"));
    REQUIRE(strstr(text, "join aim: Targets.Transform?"));
    return true;
}

TEST_CASE(source_format_query_generators_and_value_filters)
{
    REQUIRE(format(
        "system Follow query p:Position join target in self.Targets "
        "join next in target.Targets join aim:Position? from next "
        "where aim and aim.x>p.x and changed(aim){p.x=aim.x;}"
    ));
    REQUIRE(strstr(text, "join target in self.Targets"));
    REQUIRE(strstr(text, "join next in target.Targets"));
    REQUIRE(strstr(text, "join aim: Position? from next"));
    REQUIRE(strstr(text, "where aim and aim.x > p.x and changed(aim)"));
    return true;
}

TEST_CASE(source_format_incomplete_source_is_unchanged)
{
    const char *input = "behavior Broken { bad := ; later := 7 }";
    source = script_parse(input);
    REQUIRE(source->diagnostic_count > 0);
    char error[192];
    text = authoring_source_format(source, error, sizeof error);
    REQUIRE(!text);
    REQUIRE(strstr(error, "syntax errors"));
    REQUIRE(!strcmp(source->text, input));
    script_syntax_free(source);
    source = NULL;

    char nested[1024];
    int n = snprintf(nested, sizeof nested, "behavior Deep { x := ");
    for (int i = 0; i < 300; i++) {
        nested[n++] = '(';
    }
    nested[n++] = '1';
    for (int i = 0; i < 300; i++) {
        nested[n++] = ')';
    }
    memcpy(nested + n, "; }", 4);
    source = script_parse(nested);
    REQUIRE(source->diagnostic_count == 0);
    text = authoring_source_format(source, error, sizeof error);
    REQUIRE(!text);
    REQUIRE(strstr(error, "limit"));
    REQUIRE(!strcmp(source->text, nested));
    return true;
}

TEST_CASE(source_metadata_preserves_bound_identities)
{
    const char
        *input = "// exact comment\nbehavior Demo{count:=1;on Tick{count+=2;}}";
    REQUIRE(format(input));
    char json[256];
    snprintf(
        json,
        sizeof json,
        "{\"version\":2,\"anchors\":[{\"start\":%zu,\"id\":41,\"kind\":"
        "\"node\"}],"
        "\"nodes\":[{\"id\":41,\"position\":[2,3]}]}",
        (size_t)(strstr(input, "count+=") - input)
    );
    metadata = authoring_source_metadata_bind(json, input);
    REQUIRE(metadata);
    REQUIRE(authoring_source_metadata_matches(metadata, input));
    remapped = authoring_source_metadata_remap(metadata, source, formatted);
    REQUIRE(remapped);
    REQUIRE(authoring_source_metadata_matches(remapped, text));
    REQUIRE(!authoring_source_metadata_matches(remapped, input));
    char anchor[64];
    snprintf(anchor, sizeof anchor, "\"start\":%zu", (size_t)(strstr(text, "count +=") - text));
    REQUIRE(strstr(remapped, anchor));
    REQUIRE(strstr(remapped, "\"id\":41"));
    REQUIRE(strstr(remapped, "\"position\":[2,3]"));
    return true;
}

TEST_CASE(source_metadata_rejects_stale_or_changed_tokens)
{
    const char *input = "behavior Demo{count:=1;}";
    source = script_parse(input);
    formatted = script_parse("behavior Demo { count := 2; }");
    const char *json = "{\"version\":2,\"anchors\":[{\"start\":14,\"id\":41,"
                       "\"kind\":\"value\"}]}";
    metadata = authoring_source_metadata_bind(json, input);
    REQUIRE(metadata);
    REQUIRE(!authoring_source_metadata_remap(metadata, source, formatted));
    REQUIRE(!authoring_source_metadata_remap(metadata, formatted, formatted));
    REQUIRE(!authoring_source_metadata_bind("[]", input));
    script_syntax_free(formatted);
    formatted = script_parse("behavior Demo { count := 1; }");
    free(metadata);
    metadata = authoring_source_metadata_bind("{\"version\":2,\"anchors\":[{\"start\":1,\"id\":41,\"kind\":\"value\"}]}", input);
    REQUIRE(metadata);
    REQUIRE(!authoring_source_metadata_remap(metadata, source, formatted));
    return true;
}

TEST_CASE(source_metadata_distinguishes_repeated_statement_anchors)
{
    const char
        *input = "behavior Repeated{count:int=0;on Tick{count+=1;count+=1;}}";
    REQUIRE(format(input));
    REQUIRE(!strcmp(text, again));
    const char *first = strstr(input, "count+=1");
    REQUIRE(first);
    const char *second = strstr(first + 1, "count+=1");
    REQUIRE(second && first != second);
    char json[384];
    snprintf(
        json,
        sizeof json,
        "{\"version\":2,\"anchors\":[{\"start\":%zu,\"id\":41,\"kind\":"
        "\"node\"},"
        "{\"start\":%zu,\"id\":42,\"kind\":\"node\"}],"
        "\"nodes\":[{\"id\":41,\"position\":[2,3]},{\"id\":42,\"position\":[5,"
        "7]}]}",
        (size_t)(first - input),
        (size_t)(second - input)
    );
    metadata = authoring_source_metadata_bind(json, input);
    REQUIRE(metadata);
    remapped = authoring_source_metadata_remap(metadata, source, formatted);
    REQUIRE(remapped && authoring_source_metadata_matches(remapped, text));
    first = strstr(text, "count += 1");
    REQUIRE(first);
    second = strstr(first + 1, "count += 1");
    REQUIRE(second && first != second);
    char first_anchor[80], second_anchor[80];
    snprintf(first_anchor, sizeof first_anchor, "\"start\":%zu,\"id\":41", (size_t)(first - text));
    snprintf(second_anchor, sizeof second_anchor, "\"start\":%zu,\"id\":42", (size_t)(second - text));
    REQUIRE(strstr(remapped, first_anchor));
    REQUIRE(strstr(remapped, second_anchor));
    REQUIRE(strstr(remapped, "\"id\":41,\"position\":[2,3]"));
    REQUIRE(strstr(remapped, "\"id\":42,\"position\":[5,7]"));
    return true;
}

static const script_catalog_parameter tick[] = { { .name = "dt",
                                                   .type = { .storage = PIN_F32 } } };
static const script_catalog_capability capabilities[] = {
    { .id = 30,
      .name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = tick,
      .parameter_count = 1 },
};
static const script_catalog catalog = {
    .capabilities = capabilities,
    .capability_count = 1,
    .entity_type = 40,
    .tick_event = 30,
};

static bool minimize_in(const script_catalog *in, const char *input, const char *expected)
{
    text = authoring_source_minimize(input, in, "Demo");
    if (strcmp(text, expected)) {
        fprintf(stderr, "minimize:\n%s\nexpected:\n%s\n", text, expected);
        return false;
    }
    return true;
}
static bool minimize(const char *input, const char *expected)
{
    return minimize_in(&catalog, input, expected);
}

/* The chibi_ctl --format pipeline: minimize, then re-indent the result. */
static bool minimize_formatted(const char *input, const char *expected)
{
    char *minimal = authoring_source_minimize(input, &catalog, "Demo");
    script_syntax *shortened = script_parse(minimal);
    char error[192];
    text = authoring_source_format(shortened, error, sizeof error);
    script_syntax_free(shortened);
    free(minimal);
    if (!text) {
        fprintf(stderr, "format: %s\n", error);
        return false;
    }
    if (strcmp(text, expected)) {
        fprintf(stderr, "minimize:\n%s\nexpected:\n%s\n", text, expected);
        return false;
    }
    return true;
}

TEST_CASE(source_minimize_drops_redundant_text)
{
    REQUIRE(minimize(
        "library Demo {\n"
        "    Scale :: fn(a: float = 0.0, b: float = 2.0) -> (r: float = "
        "0.0) {\n"
        "        r = a * b;\n"
        "    }\n"
        "    Twice :: fn(a: float = 0.0) -> (r: float = 0.0) {\n"
        "        return(a * 2.0);\n"
        "    }\n"
        "    Use :: fn() -> () {\n"
        "        x: float = Scale(a: 1.0, b: 2.0);\n"
        "        y: float = Scale(b: 3.0);\n"
        "        if (x > 0) {\n"
        "            x = 1.0;\n"
        "        } else {\n"
        "        }\n"
        "        return();\n"
        "    }\n"
        "}\n",
        "library Demo {\n"
        "    Scale :: fn(a: float = 0.0, b: float = 2.0) -> (r: float = "
        "0.0) {\n"
        "        r = a * b\n"
        "    }\n"
        "    Twice :: fn(a: float = 0.0) -> (r: float = 0.0) {\n"
        "        return a * 2.0\n"
        "    }\n"
        "    Use :: fn() {\n"
        "        x := Scale(1.0)\n"
        "        y := Scale(b: 3.0)\n"
        "        if x > 0 {\n"
        "            x = 1.0\n"
        "        }\n"
        "        return\n"
        "    }\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_minimize_flattens_else_if_blocks)
{
    REQUIRE(minimize_formatted(
        "behavior Demo {\n"
        "    count := 0\n"
        "    on Tick {\n"
        "        if count > 1 {\n"
        "            count = 0\n"
        "        } else {\n"
        "            if count > 0 {\n"
        "                count = 1\n"
        "            } else {\n"
        "                count = 2\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n",
        "behavior Demo {\n"
        "    count := 0\n"
        "    on Tick {\n"
        "        if count > 1 {\n"
        "            count = 0\n"
        "        } else if count > 0 {\n"
        "            count = 1\n"
        "        } else {\n"
        "            count = 2\n"
        "        }\n"
        "    }\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_minimize_drops_root_state_keyword)
{
    REQUIRE(minimize_formatted(
        "behavior Demo {\n"
        "    state count := 0\n"
        "    on Tick {\n"
        "        count = count + 1\n"
        "    }\n"
        "}\n",
        "behavior Demo {\n"
        "    count := 0\n"
        "    on Tick {\n"
        "        count = count + 1\n"
        "    }\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_minimize_reaches_a_fixed_point)
{
    /* Each round drops only the outermost of nested redundant parentheses. */
    REQUIRE(minimize(
        "library Demo {\n"
        "    Use :: fn(a: float = 0.0) -> (r: float = 0.0) {\n"
        "        return ((((((((((((a))))))))))))\n"
        "    }\n"
        "}\n",
        "library Demo {\n"
        "    Use :: fn(a: float = 0.0) -> (r: float = 0.0) {\n"
        "        return a\n"
        "    }\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_minimize_splits_mixed_overlapping_rewrites)
{
    /* Inferring x would change float storage to int and must be refused.
     * Nested parentheses overlap, while declarations, semicolons and the
     * return spelling on either side remain independently reducible. */
    const char *input = "library Demo {\n"
                        "    Use :: fn(n: int) -> (r: int) {\n"
                        "        before: int = (((n)));\n"
                        "        x: float = n;\n"
                        "        later: int = (((before)));\n"
                        "        return(later);\n"
                        "    }\n"
                        "}\n";
    const char *expected = "library Demo {\n"
                           "    Use :: fn(n: int) -> (r: int) {\n"
                           "        before := n\n"
                           "        x: float = n\n"
                           "        later := before\n"
                           "        return later\n"
                           "    }\n"
                           "}\n";
    REQUIRE(minimize(input, expected));
    again = authoring_source_minimize(text, &catalog, "Demo");
    REQUIRE(!strcmp(text, again));
    source = script_parse(text);
    script_analysis *analysis = script_analyze_document(source, &catalog, "Demo");
    bool valid = analysis->valid && !analysis->diagnostic_count;
    script_analysis_free(analysis);
    REQUIRE(valid);
    char error[192];
    free(again);
    again = authoring_source_format(source, error, sizeof error);
    REQUIRE(again);
    formatted = script_parse(again);
    char *second = authoring_source_format(formatted, error, sizeof error);
    bool stable = second && !strcmp(again, second);
    free(second);
    REQUIRE(stable);
    return true;
}

TEST_CASE(source_minimize_literal_types_keep_contextual_metadata)
{
    const char
        *input = "library Demo {\n"
                 "    Echo :: fn(x: string) -> (r: string) { return x }\n"
                 "    Use :: fn(n: int) {\n"
                 "        label: string = \"hello\";\n"
                 "        empty: string = \"\";\n"
                 "        precise: double = 0;\n"
                 "        narrow: int = 1;\n"
                 "        enabled: bool = true;\n"
                 "        called: string = Echo(label);\n"
                 "        copied: string = label;\n"
                 "        converted: double = cast<double>(n);\n"
                 "    }\n"
                 "}\n";
    const char
        *expected = "library Demo {\n"
                    "    Echo :: fn(x: string) -> (r: string) { return x }\n"
                    "    Use :: fn(n: int) {\n"
                    "        label: string = \"hello\"\n"
                    "        empty: string = \"\"\n"
                    "        precise: double = 0\n"
                    "        narrow: int = 1\n"
                    "        enabled := true\n"
                    "        called := Echo(label)\n"
                    "        copied := label\n"
                    "        converted := cast<double>(n)\n"
                    "    }\n"
                    "}\n";
    REQUIRE(minimize(input, expected));
    again = authoring_source_minimize(text, &catalog, "Demo");
    REQUIRE(!strcmp(text, again));
    source = script_parse(text);
    script_analysis *analysis = script_analyze_document(source, &catalog, "Demo");
    bool valid = analysis->valid && !analysis->diagnostic_count;
    script_analysis_free(analysis);
    REQUIRE(valid);
    return true;
}

TEST_CASE(source_minimize_state_literal_types_keep_contextual_metadata)
{
    REQUIRE(minimize(
        "behavior Demo {\n"
        "    Label: string = \"hello\";\n"
        "    Now: double = 0;\n"
        "    Enabled: bool = false;\n"
        "    on Tick { }\n"
        "}\n",
        "behavior Demo {\n"
        "    Label: string = \"hello\"\n"
        "    Now: double = 0\n"
        "    Enabled := false\n"
        "    on Tick { }\n"
        "}\n"
    ));
    again = authoring_source_minimize(text, &catalog, "Demo");
    REQUIRE(!strcmp(text, again));
    return true;
}

TEST_CASE(source_minimize_resolved_defaults_only)
{
    static const script_catalog_parameter native_parameters[] = {
        { .name = "Required", .type = { .storage = PIN_I32 } },
        { .name = "Step",
          .type = { .storage = PIN_I32 },
          .has_default = true,
          .default_value = { .type = PIN_I32, .i = 2 } },
    };
    static const script_catalog_parameter payload[] = {
        { .name = "Id", .type = { .storage = PIN_STR } },
        { .name = "Mode",
          .type = { .storage = PIN_I32 },
          .has_default = true,
          .default_value = { .type = PIN_I32, .i = 2 } },
    };
    static const script_catalog_parameter target[] = {
        { .name = "Target", .type = { .storage = PIN_HANDLE, .nominal = 40 } },
    };
    static const script_catalog_capability defaults[] = {
        { .id = 30,
          .name = "Tick",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = tick,
          .parameter_count = 1 },
        { .id = 31,
          .name = "Sample",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = native_parameters,
          .parameter_count = 2 },
        { .id = 32,
          .name = "Notify",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = payload,
          .parameter_count = 2 },
        { .id = 33,
          .name = "send_event",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = target,
          .parameter_count = 1,
          .event_sender = true },
    };
    script_catalog in = { .capabilities = defaults,
                          .capability_count = 4,
                          .entity_type = 40,
                          .tick_event = 30 };
    const char *input = "behavior Demo {\n"
                        "    Required :: fn(v: int) -> (r: int) { return v }\n"
                        "    Opt :: fn(a: int, b: int = 2, c: int = 3) -> (r: "
                        "int) { return a + b + c }\n"
                        "    on Tick {\n"
                        "        required := Required(8)\n"
                        "        exact := Opt(5, 2, 3)\n"
                        "        named := Opt(a: 5, c: 9, b: 2)\n"
                        "        nonmatching := Opt(5, 8, 3)\n"
                        "        ordered := Opt(5, 2, 9)\n"
                        "        Sample(4, 2)\n"
                        "        Sample(4, 8)\n"
                        "        send_event<\"Notify\">(self, \"go\", 2)\n"
                        "        send_event<\"Notify\">(self, \"go\", 8)\n"
                        "    }\n"
                        "}\n";
    /* Native omissions introduce constant_snapshot, which the current oracle
     * compares exactly. Matching native/event defaults therefore stay too. */
    const char
        *expected = "behavior Demo {\n"
                    "    Required :: fn(v: int) -> (r: int) { return v }\n"
                    "    Opt :: fn(a: int, b: int = 2, c: int = 3) -> (r: int) "
                    "{ return a + b + c }\n"
                    "    on Tick {\n"
                    "        required := Required(8)\n"
                    "        exact := Opt(5)\n"
                    "        named := Opt(5, c: 9)\n"
                    "        nonmatching := Opt(5, 8)\n"
                    "        ordered := Opt(5, 2, 9)\n"
                    "        Sample(4, 2)\n"
                    "        Sample(4, 8)\n"
                    "        send_event<\"Notify\">(self, \"go\", 2)\n"
                    "        send_event<\"Notify\">(self, \"go\", 8)\n"
                    "    }\n"
                    "}\n";
    REQUIRE(minimize_in(&in, input, expected));
    again = authoring_source_minimize(text, &in, "Demo");
    REQUIRE(!strcmp(text, again));
    source = script_parse(text);
    script_analysis *analysis = script_analyze_document(source, &in, "Demo");
    bool valid = analysis->valid && !analysis->diagnostic_count;
    script_analysis_free(analysis);
    REQUIRE(valid);
    return true;
}

TEST_CASE(source_minimize_labels_preserve_resolved_positional_bindings)
{
    static const script_catalog_parameter data[] = {
        { .name = "Entity",
          .type = { .storage = PIN_HANDLE, .nominal = 40 },
          .has_default = true,
          .default_owner = true },
        { .name = "Name", .type = { .storage = PIN_STR } },
        { .name = "Value", .type = { .storage = PIN_I32 } }
    };
    static const script_catalog_parameter method[] = {
        { .name = "Target", .type = { .storage = PIN_HANDLE, .nominal = 40 } },
        { .name = "Count", .type = { .storage = PIN_I32 } },
        { .name = "Label", .type = { .storage = PIN_STR } }
    };
    static const script_catalog_parameter target[] = {
        { .name = "Target", .type = { .storage = PIN_HANDLE, .nominal = 40 } }
    };
    static const script_catalog_parameter payload[] = {
        { .name = "Id", .type = { .storage = PIN_STR } },
        { .name = "Value", .type = { .storage = PIN_I32 } }
    };
    static const script_catalog_field fields[] = {
        { .id = 51, .name = "x", .type = { .storage = PIN_I32 } },
        { .id = 52, .name = "y", .type = { .storage = PIN_I32 } }
    };
    static const script_catalog_type pair = { .id = 50,
                                              .name = "Pair",
                                              .type = { .storage = PIN_STRUCT,
                                                        .nominal = 50 },
                                              .fields = fields,
                                              .field_count = 2,
                                              .size = 2 * sizeof(int) };
    static const script_catalog_parameter record[] = {
        { .name = "Value", .type = { .storage = PIN_STRUCT, .nominal = 50 } }
    };
    static const script_catalog_capability calls[] = {
        { .id = 30,
          .name = "Tick",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = tick,
          .parameter_count = 1 },
        { .id = 31,
          .name = "set_data",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = data,
          .parameter_count = 3 },
        { .id = 32,
          .name = "adjust",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = method,
          .parameter_count = 3,
          .receiver = 40,
          .method_name = "Adjust" },
        { .id = 33,
          .name = "Notify",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = payload,
          .parameter_count = 2 },
        { .id = 34,
          .name = "send_event",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = target,
          .parameter_count = 1,
          .event_sender = true },
        { .id = 35,
          .name = "Record",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = record,
          .parameter_count = 1 }
    };
    script_catalog in = { .capabilities = calls,
                          .capability_count = 6,
                          .entity_type = 40,
                          .tick_event = 30,
                          .types = &pair,
                          .type_count = 1 };
    const char
        *input = "behavior Demo {\n"
                 "    count: int = 0\n"
                 "    PairFn :: fn(a: int, b: int = 2, c: int = 3) -> (r: int) "
                 "{ return a + b + c }\n"
                 "    Ref :: fn(ref int value) { }\n"
                 "    Identity :: pure fn<T: plain int>(item: T) -> (Result: "
                 "T) { return item }\n"
                 "    on Tick {\n"
                 "        set_data(Name: \"key\", Value: 8)\n"
                 "        set_data(Entity: self, Name: \"key\", Value: 8)\n"
                 "        normal := PairFn(a: 1, b: 8, c: 9)\n"
                 "        reordered := PairFn(c: 9, a: 1, b: 8)\n"
                 "        skipped := PairFn(a: 1, c: 9)\n"
                 "        self.Adjust(Count: 4, Label: \"ok\")\n"
                 "        send_event<\"Notify\">(Target: self, Id: \"go\", "
                 "Value: 8)\n"
                 "        Record(Value.x: 1, Value.y: 2)\n"
                 "        Ref(value: ref int from self.count)\n"
                 "        generic := Identity<int>(item: 4)\n"
                 "    }\n"
                 "}\n";
    const char *expected = "behavior Demo {\n"
                           "    count: int = 0\n"
                           "    PairFn :: fn(a: int, b: int = 2, c: int = 3) "
                           "-> (r: int) { return a + b + c }\n"
                           "    Ref :: fn(ref int value) { }\n"
                           "    Identity :: pure fn<T: plain int>(item: T) -> "
                           "(Result: T) { return item }\n"
                           "    on Tick {\n"
                           "        set_data(Name: \"key\", Value: 8)\n"
                           "        set_data(self, \"key\", 8)\n"
                           "        normal := PairFn(1, 8, 9)\n"
                           "        reordered := PairFn(c: 9, a: 1, b: 8)\n"
                           "        skipped := PairFn(1, c: 9)\n"
                           "        self.Adjust(4, \"ok\")\n"
                           "        send_event<\"Notify\">(self, \"go\", 8)\n"
                           "        Record(Value.x: 1, Value.y: 2)\n"
                           "        Ref(value: ref int from count)\n"
                           "        generic := Identity<int>(4)\n"
                           "    }\n"
                           "}\n";
    REQUIRE(minimize_in(&in, input, expected));
    again = authoring_source_minimize(text, &in, "Demo");
    REQUIRE(!strcmp(text, again));
    source = script_parse(text);
    script_analysis *analysis = script_analyze_document(source, &in, "Demo");
    bool valid = analysis->valid && !analysis->diagnostic_count;
    script_analysis_free(analysis);
    REQUIRE(valid);
    return true;
}
TEST_CASE(source_minimize_keeps_argument_order)
{
    /* Arguments run in source order, so out-of-order labels stay. */
    REQUIRE(minimize(
        "library Demo {\n"
        "    Pair :: fn(a: float = 0.0, b: float = 0.0) -> (r: float = 0.0) "
        "{\n"
        "        return a - b\n"
        "    }\n"
        "    Next :: fn() -> (r: float = 0.0) {\n"
        "        return 1.0\n"
        "    }\n"
        "    Use :: fn() -> (r: float = 0.0) {\n"
        "        return Pair(b: Next(), a: Next())\n"
        "    }\n"
        "}\n",
        "library Demo {\n"
        "    Pair :: fn(a: float = 0.0, b: float = 0.0) -> (r: float = 0.0) "
        "{\n"
        "        return a - b\n"
        "    }\n"
        "    Next :: fn() -> (r: float = 0.0) {\n"
        "        return 1.0\n"
        "    }\n"
        "    Use :: fn() -> (r: float = 0.0) {\n"
        "        return Pair(b: Next(), a: Next())\n"
        "    }\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_minimize_keeps_invalid_source)
{
    const char
        *input = "library Demo { F :: fn() -> () { x := missing(a: 1); } }";
    REQUIRE(minimize(input, input));
    return true;
}

TEST_CASE(source_format_keeps_else_and_arrow_spacing)
{
    REQUIRE(format("library M{F :: fn(x:int)->(r:int){if x>0{r=1;}\nelse{r=engine [\"Self\"]();}}}"));
    REQUIRE(strstr(text, "-> (r: int)"));
    REQUIRE(strstr(text, "} else {"));
    REQUIRE(strstr(text, "engine[\"Self\"]()"));
    return true;
}

TEST_CASE(source_minimize_keeps_mixed_grouping_and_tight_ranges)
{
    REQUIRE(minimize(
        "library Demo {\n"
        "    F :: fn(a: bool, b: bool, c: bool, m: int) {\n"
        "        x := a || (b && c)\n"
        "        y := (a && b) || (c)\n"
        "        z := m | (m & 3)\n"
        "        w := m + (m * 3)\n"
        "        for i in 0 .. m - 1 {\n"
        "            w += i\n"
        "        }\n"
        "    }\n"
        "}\n",
        "library Demo {\n"
        "    F :: fn(a: bool, b: bool, c: bool, m: int) {\n"
        "        x := a || (b && c)\n"
        "        y := (a && b) || c\n"
        "        z := m | (m & 3)\n"
        "        w := m + m * 3\n"
        "        for i in 0 .. m - 1 {\n"
        "            w += i\n"
        "        }\n"
        "    }\n"
        "}\n"
    ));
    free(text);
    text = NULL;
    REQUIRE(format("library M{F :: fn(m:int){for i in 0 .. m-1{m+=i;}}}"));
    REQUIRE(strstr(text, "for i in 0..m - 1 {"));
    return true;
}

static const script_catalog_field velocity_field = { .id = 81,
                                                     .name = "x",
                                                     .type = { .storage = PIN_F64 },
                                                     .writable = true };
static const script_catalog_type velocity_type = {
    .id = 80,
    .name = "Velocity",
    .type = { .storage = PIN_STRUCT, .nominal = 80 },
    .fields = &velocity_field,
    .field_count = 1,
    .size = sizeof(double),
    .component = true,
    .component_id = 90,
};
static const script_catalog_parameter velocity_value[] = {
    { .name = "Value", .type = { .storage = PIN_STRUCT, .nominal = 80 } }
};
static const script_catalog_parameter present[] = {
    { .name = "Value", .type = { .storage = PIN_BOOL } }
};
static const script_catalog_parameter field_write[] = {
    { .name = "Value", .type = { .storage = PIN_F64 } },
    { .name = "Target",
      .type = { .storage = PIN_HANDLE, .nominal = 40 },
      .has_default = true,
      .default_owner = true },
};
static const script_catalog_parameter selector[] = {
    { .name = "Component",
      .type = { .storage = PIN_NAME },
      .reference_kind = SCRIPT_REFERENCE_COMPONENT },
    { .name = "Entity",
      .type = { .storage = PIN_HANDLE, .nominal = 40 },
      .has_default = true,
      .default_owner = true },
};
static const script_catalog_parameter reals[] = {
    { .name = "A", .type = { .storage = PIN_F64 } },
    { .name = "B", .type = { .storage = PIN_F64 } }
};
static const script_catalog_parameter floats[] = {
    { .name = "A", .type = { .storage = PIN_F32 } },
    { .name = "B", .type = { .storage = PIN_F32 } }
};
static const script_catalog_parameter real_result[] = {
    { .name = "Result", .type = { .storage = PIN_F64 } }
};
static const script_catalog_capability component_capabilities[] = {
    { .id = 30,
      .name = "Tick",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = tick,
      .parameter_count = 1 },
    { .id = 31,
      .name = "get_velocity",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = velocity_value,
      .result_count = 1,
      .component_access = SCRIPT_COMPONENT_ACCESS_READ,
      .component = 80,
      .component_entity = -1,
      .component_value = -1,
      .effects = SCRIPT_EFFECT_READ },
    { .id = 32,
      .name = "has_velocity",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = present,
      .result_count = 1,
      .component_access = SCRIPT_COMPONENT_ACCESS_HAS,
      .component = 80,
      .component_entity = -1,
      .component_value = -1,
      .effects = SCRIPT_EFFECT_READ },
    { .id = 33,
      .name = "set_velocity_x",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = field_write,
      .parameter_count = 2,
      .results = velocity_value,
      .result_count = 1,
      .component_access = SCRIPT_COMPONENT_ACCESS_WRITE,
      .component = 80,
      .component_field = 81,
      .component_entity = 1,
      .component_value = 0,
      .effects = SCRIPT_EFFECT_WRITE },
    { .id = 37,
      .name = "has_component",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = selector,
      .parameter_count = 2,
      .results = present,
      .result_count = 1,
      .component_access = SCRIPT_COMPONENT_ACCESS_HAS,
      .component_entity = 1,
      .component_value = -1,
      .effects = SCRIPT_EFFECT_READ },
    { .id = 34,
      .name = "multiply",
      .operator_name = "*",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = reals,
      .parameter_count = 2,
      .results = real_result,
      .result_count = 1,
      .pure = true },
    { .id = 35,
      .name = "less",
      .operator_name = "<",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = reals,
      .parameter_count = 2,
      .results = present,
      .result_count = 1,
      .pure = true },
    { .id = 36,
      .name = "less_float",
      .operator_name = "<",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = floats,
      .parameter_count = 2,
      .results = present,
      .result_count = 1,
      .pure = true },
};
static const script_catalog component_catalog = {
    .types = &velocity_type,
    .type_count = 1,
    .capabilities = component_capabilities,
    .capability_count = sizeof component_capabilities / sizeof *component_capabilities,
    .entity_type = 40,
    .tick_event = 30,
};

TEST_CASE(source_minimize_spells_accessors_as_paths_and_operators)
{
    REQUIRE(minimize_in(
        &component_catalog,
        "behavior Demo {\n"
        "    on Tick {\n"
        "        if has_velocity() {\n"
        "            set_velocity_x(multiply(get_velocity().Value.x, 2.0))\n"
        "        }\n"
        "        if less(get_velocity().Value.x, 1.0) {\n"
        "            set_velocity_x(multiply(2.0, 3.0), self)\n"
        "        }\n"
        "        e := self\n"
        "        set_velocity_x(2.0, e)\n"
        "        lifted := has_velocity()\n"
        "        narrowed := less_float(get_velocity().Value.x, "
        "get_velocity().Value.x)\n"
        "    }\n"
        "}\n",
        "behavior Demo {\n"
        "    on Tick {\n"
        "        if self.Velocity {\n"
        "            self.Velocity.x = self.Velocity.x * 2.0\n"
        "        }\n"
        "        if self.Velocity.x < 1.0 {\n"
        "            self.Velocity.x = 2.0 * 3.0\n"
        "        }\n"
        "        e := self\n"
        "        set_velocity_x(2.0, e)\n"
        "        lifted := has_velocity()\n"
        "        narrowed := less_float(self.Velocity.x, self.Velocity.x)\n"
        "    }\n"
        "}\n"
    ));
    return true;
}

/* A generic call names its component through an argument, not a fixed
 * component the minimizer could spell as a path, so it keeps its call. */
TEST_CASE(source_minimize_keeps_generic_component_calls)
{
    const char *input = "behavior Demo {\n"
                        "    on Tick {\n"
                        "        if has_component(\"Velocity\", self) {\n"
                        "            e := self\n"
                        "        }\n"
                        "    }\n"
                        "}\n";
    REQUIRE(minimize_in(&component_catalog, input, input));
    return true;
}

void test_language_format()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_format_block_layout);
    RUN_TEST_CASE(source_format_qualified_callable_fields);
    RUN_TEST_CASE(source_format_reductions);
    RUN_TEST_CASE(source_format_component_measures);
    RUN_TEST_CASE(source_format_measured_signatures);
    RUN_TEST_CASE(source_format_preserves_comments_and_literals);
    RUN_TEST_CASE(source_format_preserves_expression_structure);
    RUN_TEST_CASE(source_format_system_types_and_control_flow);
    RUN_TEST_CASE(source_format_switch_arm_indentation);
    RUN_TEST_CASE(source_format_query_header_words);
    RUN_TEST_CASE(source_format_query_generators_and_value_filters);
    RUN_TEST_CASE(source_format_incomplete_source_is_unchanged);
    RUN_TEST_CASE(source_metadata_preserves_bound_identities);
    RUN_TEST_CASE(source_metadata_rejects_stale_or_changed_tokens);
    RUN_TEST_CASE(source_metadata_distinguishes_repeated_statement_anchors);
    RUN_TEST_CASE(source_format_keeps_else_and_arrow_spacing);
    RUN_TEST_CASE(source_minimize_drops_redundant_text);
    RUN_TEST_CASE(source_minimize_flattens_else_if_blocks);
    RUN_TEST_CASE(source_minimize_drops_root_state_keyword);
    RUN_TEST_CASE(source_minimize_reaches_a_fixed_point);
    RUN_TEST_CASE(source_minimize_splits_mixed_overlapping_rewrites);
    RUN_TEST_CASE(source_minimize_literal_types_keep_contextual_metadata);
    RUN_TEST_CASE(source_minimize_state_literal_types_keep_contextual_metadata);
    RUN_TEST_CASE(source_minimize_resolved_defaults_only);
    RUN_TEST_CASE(source_minimize_labels_preserve_resolved_positional_bindings);
    RUN_TEST_CASE(source_minimize_keeps_argument_order);
    RUN_TEST_CASE(source_minimize_keeps_invalid_source);
    RUN_TEST_CASE(source_minimize_keeps_mixed_grouping_and_tight_ranges);
    RUN_TEST_CASE(source_minimize_spells_accessors_as_paths_and_operators);
    RUN_TEST_CASE(source_minimize_keeps_generic_component_calls);
    pu_setup(NULL, NULL);
}
