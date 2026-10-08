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
        "library Math{pure function Compute(x:int)->(r:int){\n"
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
        "while p.x>0{p.x-=1;} match enum(p.mode){case 1{continue;} "
        "default{break;}}}\n"
        "function F(x:array<int>)->(r:int){return(g<int>(x[0]));}}"
    ));
    REQUIRE(strstr(text, "array<int>"));
    REQUIRE(strstr(text, "g<int>(x[0])"));
    REQUIRE(strstr(text, "cast<float>(i)"));
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

static bool minimize(const char *input, const char *expected)
{
    text = authoring_source_minimize(input, &catalog, "Demo");
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
        "    function Scale(a: float = 0.0, b: float = 2.0) -> (r: float = "
        "0.0) {\n"
        "        r = a * b;\n"
        "    }\n"
        "    function Use() -> () {\n"
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
        "    function Scale(a: float = 0.0, b: float = 2.0) -> (r: float = "
        "0.0) {\n"
        "        r = a * b\n"
        "    }\n"
        "    function Use() {\n"
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

TEST_CASE(source_minimize_keeps_invalid_source)
{
    const char
        *input = "library Demo { function F() -> () { x := missing(a: 1); } }";
    REQUIRE(minimize(input, input));
    return true;
}

TEST_CASE(source_format_keeps_else_and_arrow_spacing)
{
    REQUIRE(format("library M{function F(x:int)->(r:int){if x>0{r=1;}\nelse{r=engine [\"Self\"]();}}}"));
    REQUIRE(strstr(text, "-> (r: int)"));
    REQUIRE(strstr(text, "} else {"));
    REQUIRE(strstr(text, "engine[\"Self\"]()"));
    return true;
}

void test_language_format()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_format_block_layout);
    RUN_TEST_CASE(source_format_preserves_comments_and_literals);
    RUN_TEST_CASE(source_format_preserves_expression_structure);
    RUN_TEST_CASE(source_format_system_types_and_control_flow);
    RUN_TEST_CASE(source_format_incomplete_source_is_unchanged);
    RUN_TEST_CASE(source_metadata_preserves_bound_identities);
    RUN_TEST_CASE(source_metadata_rejects_stale_or_changed_tokens);
    RUN_TEST_CASE(source_metadata_distinguishes_repeated_statement_anchors);
    RUN_TEST_CASE(source_format_keeps_else_and_arrow_spacing);
    RUN_TEST_CASE(source_minimize_drops_redundant_text);
    RUN_TEST_CASE(source_minimize_keeps_invalid_source);
    pu_setup(NULL, NULL);
}
