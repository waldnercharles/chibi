#include "authoring/source_format.h"
#include "authoring/source_metadata.h"
#include "authoring/source_minimize.h"
#include "third_party/pico_headers/pico_unit.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static script_syntax *source, *formatted;
static char *text, *again;
static char *metadata, *remapped;
static authoring_format_map edit_map;
static script_analysis *state_before, *state_after;

static void cleanup()
{
    script_analysis_free(state_before);
    script_analysis_free(state_after);
    state_before = state_after = NULL;
    script_syntax_free(source);
    script_syntax_free(formatted);
    free(text);
    free(again);
    free(metadata);
    free(remapped);
    authoring_format_map_free(&edit_map);
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

static bool configured(const char *input, authoring_format_options options)
{
    source = script_parse(input);
    char error[192];
    text = authoring_source_format_with_options(source, &options, &edit_map, error, sizeof error);
    if (!text) {
        fprintf(stderr, "configured format: %s\n", error);
        return false;
    }
    formatted = script_parse(text);
    again = authoring_source_format_with_options(formatted, &options, NULL, error, sizeof error);
    if (!again || strcmp(text, again)) {
        fprintf(stderr, "configured format is not idempotent: %s\n%s\n%s\n", error, text, again ? again : "<null>");
        return false;
    }
    return !formatted->diagnostic_count && !strcmp(source->text, input);
}

static bool map_reproduces_source()
{
    REQUIRE(edit_map.source_size == (int)strlen(source->text));
    REQUIRE(edit_map.formatted_size == (int)strlen(text));
    int before = 0, after = 0;
    for (int i = 0; i < edit_map.count; i++) {
        authoring_format_edit e = edit_map.edits[i];
        REQUIRE(e.start >= before && e.end >= e.start && e.end <= edit_map.source_size);
        REQUIRE(
            e.new_start >= after && e.new_end >= e.new_start &&
            e.new_end <= edit_map.formatted_size
        );
        REQUIRE(e.start - before == e.new_start - after);
        REQUIRE(!memcmp(source->text + before, text + after, (size_t)(e.start - before)));
        for (int p = before; p < e.start; p++) {
            if (p > before || !i ||
                edit_map.edits[i - 1].start != edit_map.edits[i - 1].end) {
                REQUIRE(
                    authoring_format_map_position(&edit_map, p, AUTHORING_FORMAT_BEFORE) ==
                    after + p - before
                );
            }
            REQUIRE(
                authoring_format_map_position(&edit_map, p, AUTHORING_FORMAT_AFTER) ==
                after + p - before
            );
        }
        for (int p = e.start; p < e.end; p++) {
            REQUIRE(isspace((unsigned char)source->text[p]));
        }
        for (int p = e.new_start; p < e.new_end; p++) {
            REQUIRE(isspace((unsigned char)text[p]));
        }
        REQUIRE(
            authoring_format_map_position(&edit_map, e.start, AUTHORING_FORMAT_BEFORE) ==
            e.new_start
        );
        REQUIRE(
            authoring_format_map_position(&edit_map, e.end, AUTHORING_FORMAT_AFTER) ==
            e.new_end
        );
        if (e.end - e.start > 1) {
            REQUIRE(
                authoring_format_map_position(&edit_map, e.start + 1, AUTHORING_FORMAT_BEFORE) ==
                e.new_start
            );
            REQUIRE(
                authoring_format_map_position(&edit_map, e.start + 1, AUTHORING_FORMAT_AFTER) ==
                e.new_end
            );
        }
        before = e.end;
        after = e.new_end;
    }
    REQUIRE(edit_map.source_size - before == edit_map.formatted_size - after);
    REQUIRE(!strcmp(source->text + before, text + after));
    return true;
}

TEST_CASE(source_format_spaces_typed_constant_colons)
{
    REQUIRE(format("library L {\n    Limit:int:5\n    Mask :: 255\n}\n"));
    REQUIRE(strstr(text, "Limit: int : 5") && strstr(text, "Mask :: 255"));
    return true;
}
TEST_CASE(source_format_keeps_attributes_on_their_line)
{
    REQUIRE(format("package facts\n@test\nAdds :: fn() { }\n@ test Second :: fn() { }\n"));
    REQUIRE(!strcmp(text, "package facts\n@test\nAdds :: fn() {}\n@test Second :: fn() {}\n"));
    return true;
}
TEST_CASE(source_format_spaces_variant_patterns)
{
    const char
        *input = "package facts\n"
                 "Shape :: union {\n"
                 "    Circle(radius: int)\n"
                 "    | Empty\n"
                 "}\n"
                 "Area :: fn(shape: Shape) -> int {\n"
                 "    switch shape {\n"
                 "        case .Circle(radius):\n"
                 "            return radius;\n"
                 "        case .Empty:\n"
                 "            return 0;\n"
                 "    }\n"
                 "}\n"
                 "Round :: fn(shape: Shape) -> bool {\n"
                 "    return shape is .Circle(radius) && shape is not .Empty;\n"
                 "}\n";
    REQUIRE(format(input));
    REQUIRE(!strcmp(text, input));
    return true;
}
TEST_CASE(source_format_aligns_bracket_continuations)
{
    REQUIRE(format(
        "package facts\n"
        "Pair :: union {\n"
        "    Both(first: int,\n"
        "second: int)\n"
        "    | Neither\n"
        "}\n"
        "Sum :: fn(first: int,\n"
        "  second: int) -> int {\n"
        "    if (first > 0 ||\n"
        "second > 0) {\n"
        "        return Add(first,\n"
        "second);\n"
        "    }\n"
        "    switch Both(first, second) {\n"
        "        case .Both(left,\n"
        "right):\n"
        "            return left;\n"
        "    }\n"
        "    point := {x: first,\n"
        "y: second};\n"
        "    return Add(first, Add(\n"
        "second,\n"
        "first\n"
        "    ));\n"
        "}\n"
    ));
    REQUIRE(!strcmp(
        text,
        "package facts\n"
        "Pair :: union {\n"
        "    Both(first: int,\n"
        "         second: int)\n"
        "    | Neither\n"
        "}\n"
        "Sum :: fn(first: int,\n"
        "          second: int) -> int {\n"
        "    if (first > 0 ||\n"
        "        second > 0) {\n"
        "        return Add(first,\n"
        "                   second);\n"
        "    }\n"
        "    switch Both(first, second) {\n"
        "        case .Both(left,\n"
        "                   right):\n"
        "            return left;\n"
        "    }\n"
        "    point := {x: first,\n"
        "              y: second};\n"
        "    return Add(first, Add(\n"
        "        second,\n"
        "        first\n"
        "    ));\n"
        "}\n"
    ));
    cleanup();
    REQUIRE(configured(
        "package facts\nSum :: fn(first: int, second: int) -> int {\n"
        "return Add(first,\nsecond);\n}\n",
        (authoring_format_options){ .indent_width = 4, .use_tabs = true }
    ));
    REQUIRE(!strcmp(
        text,
        "package facts\nSum :: fn(first: int, second: int) -> int {\n"
        "\treturn Add(first,\n\t           second);\n}\n"
    ));
    REQUIRE(map_reproduces_source());
    return true;
}
TEST_CASE(source_format_aligns_operator_continuations)
{
    REQUIRE(format(
        "package facts\n"
        "Valid :: fn(first: int, second: int) -> bool {\n"
        "    ready := first > 0 &&\n"
        "second > 0;\n"
        "    return ready ||\n"
        "  first + second is int &&\n"
        "            Check(first,\n"
        "second);\n"
        "}\n"
    ));
    REQUIRE(!strcmp(
        text,
        "package facts\n"
        "Valid :: fn(first: int, second: int) -> bool {\n"
        "    ready := first > 0 &&\n"
        "             second > 0;\n"
        "    return ready ||\n"
        "           first + second is int &&\n"
        "           Check(first,\n"
        "                 second);\n"
        "}\n"
    ));
    return true;
}
TEST_CASE(source_format_hugs_struct_braces)
{
    REQUIRE(format("package facts\nOrigin :: fn() -> Point {\n    return { x: 0,y: 0 };\n}\n"));
    REQUIRE(strstr(text, "    return {x: 0, y: 0};\n"));
    cleanup();
    REQUIRE(format("behavior Demo{value:={ left:1 };empty:={ };}"));
    REQUIRE(strstr(text, "    value := {left: 1};\n"));
    REQUIRE(strstr(text, "    empty := {};\n"));
    return true;
}
TEST_CASE(source_format_options_preserve_defaults)
{
    const char *input = "behavior Demo{value:=[\n1,\n2];on Tick{value[0]+=1;}}";
    REQUIRE(configured(input, authoring_format_defaults()));
    char error[192];
    char *legacy = authoring_source_format(source, error, sizeof error);
    REQUIRE(legacy && !strcmp(text, legacy));
    free(legacy);
    authoring_format_map defaults_map = { 0 };
    legacy = authoring_source_format_with_options(source, NULL, &defaults_map, error, sizeof error);
    REQUIRE(legacy && !strcmp(text, legacy));
    REQUIRE(defaults_map.count == edit_map.count && defaults_map.count > 0);
    REQUIRE(
        defaults_map.source_size == edit_map.source_size &&
        defaults_map.formatted_size == edit_map.formatted_size
    );
    authoring_format_map_free(&defaults_map);
    free(legacy);
    REQUIRE(strstr(text, "value := [\n        1,\n        2];"));
    REQUIRE(map_reproduces_source());
    return true;
}

TEST_CASE(source_format_configures_indentation)
{
    const char *input = "behavior Demo{on Tick{call(\n1,\n2);}}";
    const int widths[] = { 1, 2, 16 };
    for (int i = 0; i < 3; i++) {
        authoring_format_options options = { .indent_width = widths[i] };
        REQUIRE(configured(input, options));
        const char *line = strchr(text, '\n') + 1;
        REQUIRE((int)strspn(line, " ") == widths[i]);
        REQUIRE(map_reproduces_source());
        cleanup();
    }
    REQUIRE(configured(input, (authoring_format_options){ .indent_width = 4, .use_tabs = true }));
    REQUIRE(!strcmp(text, "behavior Demo {\n\ton Tick {\n\t\tcall(\n\t\t\t1,\n\t\t\t2);\n\t}\n}\n"));
    return true;
}

TEST_CASE(source_format_reflows_lists_and_records)
{
    const char
        *input = "behavior Demo{numbers:=[1,2,3];value:={left:1,right:[2,3]};}";
    REQUIRE(
        configured(input, (authoring_format_options){ .indent_width = 2, .preferred_width = 20 })
    );
    REQUIRE(
        !strcmp(text, "behavior Demo {\n  numbers := [\n    1,\n    2,\n    3\n  ];\n  value := {\n    left: 1,\n    right: [2, 3]\n  };\n}\n")
    );
    REQUIRE(map_reproduces_source());
    cleanup();
    REQUIRE(configured(
        "behavior Demo{numbers:=[\n1,\n2,\n3\n];}",
        (authoring_format_options){ .indent_width = 2, .preferred_width = 80 }
    ));
    REQUIRE(strstr(text, "numbers := [1, 2, 3];"));
    return true;
}

TEST_CASE(source_format_reflows_parameters_calls_and_binary)
{
    REQUIRE(configured(
        "library "
        "Demo{Compute::(first:int,second:int)->int{result:=(first+second*123);"
        "return call(first,second,result);}}",
        (authoring_format_options){ .indent_width = 2, .preferred_width = 24 }
    ));
    REQUIRE(strstr(text, "Compute :: (\n    first: int,\n    second: int\n  ) -> int"));
    REQUIRE(strstr(text, "result := (\n      first +\n      second *\n      123\n    );"));
    REQUIRE(strstr(text, "call(\n      first,\n      second,\n      result\n    );"));
    REQUIRE(map_reproduces_source());
    return true;
}

TEST_CASE(source_format_width_is_byte_based_and_bounded)
{
    const char *input = "behavior Demo{x:=[\"é\",\"é\"];empty:=[];}";
    REQUIRE(
        configured(input, (authoring_format_options){ .indent_width = 1, .preferred_width = 18 })
    );
    REQUIRE(strstr(text, "x := [\"é\", \"é\"];"));
    cleanup();
    REQUIRE(
        configured(input, (authoring_format_options){ .indent_width = 1, .preferred_width = 17 })
    );
    REQUIRE(strstr(text, "x := [\n  \"é\",\n  \"é\"\n ];"));
    REQUIRE(strstr(text, "empty := [];"));
    cleanup();
    REQUIRE(configured(
        input,
        (authoring_format_options){ .indent_width = 16, .preferred_width = 1, .use_tabs = true }
    ));
    REQUIRE(strstr(text, "\tx := [\n\t\t\"é\""));
    cleanup();
    REQUIRE(
        configured(input, (authoring_format_options){ .indent_width = 1, .preferred_width = 4096 })
    );
    REQUIRE(strstr(text, "x := [\"é\", \"é\"];"));
    return true;
}

TEST_CASE(source_format_reflow_preserves_comments)
{
    REQUIRE(configured(
        "/* module\r\n comment */\r\nbehavior Demo{x:=[1, // first\r\n2, /* hé "
        "*/ 3];on Tick{call(\"// exact\",/* x\ny */\"é\");}} // end",
        (authoring_format_options){ .indent_width = 2, .preferred_width = 12 }
    ));
    REQUIRE(strstr(text, "/* module\r\n comment */"));
    REQUIRE(strstr(text, "1, // first\r\n"));
    REQUIRE(strstr(text, "/* hé */"));
    REQUIRE(strstr(text, "/* x\ny */"));
    REQUIRE(strstr(text, "\"// exact\""));
    REQUIRE(strstr(text, "} // end\n"));
    REQUIRE(map_reproduces_source());
    return true;
}

TEST_CASE(source_format_maps_insertions_deletions_and_empty_draft)
{
    REQUIRE(configured("  behavior Demo{ x :=  1 ; }  ", authoring_format_defaults()));
    REQUIRE(map_reproduces_source());
    REQUIRE(edit_map.count > 3);
    bool internal_insertion = false;
    for (int i = 0; i < edit_map.count; i++) {
        authoring_format_edit e = edit_map.edits[i];
        if (e.start == e.end && e.start > 0 && e.start < edit_map.source_size) {
            REQUIRE(e.new_end > e.new_start);
            REQUIRE(
                authoring_format_map_position(&edit_map, e.start, AUTHORING_FORMAT_BEFORE) ==
                e.new_start
            );
            REQUIRE(
                authoring_format_map_position(&edit_map, e.start, AUTHORING_FORMAT_AFTER) ==
                e.new_end
            );
            internal_insertion = true;
        }
    }
    REQUIRE(internal_insertion);
    REQUIRE(authoring_format_map_position(&edit_map, 1, AUTHORING_FORMAT_AFTER) == 0);
    cleanup();
    REQUIRE(configured("behavior Demo {\n    x := 1;\n}\n", authoring_format_defaults()));
    REQUIRE(edit_map.count == 0);
    REQUIRE(
        authoring_format_map_position(&edit_map, edit_map.source_size, AUTHORING_FORMAT_AFTER) ==
        edit_map.formatted_size
    );
    cleanup();
    REQUIRE(configured("behavior Demo {}", authoring_format_defaults()));
    REQUIRE(edit_map.count == 1);
    REQUIRE(
        authoring_format_map_position(&edit_map, edit_map.source_size, AUTHORING_FORMAT_BEFORE) ==
        edit_map.source_size
    );
    REQUIRE(
        authoring_format_map_position(&edit_map, edit_map.source_size, AUTHORING_FORMAT_AFTER) ==
        edit_map.source_size + 1
    );
    REQUIRE(map_reproduces_source());
    cleanup();
    source = script_parse("");
    REQUIRE(source->diagnostic_count);
    text = authoring_source_format_with_options(source, NULL, &edit_map, NULL, 0);
    REQUIRE(!text && !edit_map.count && !edit_map.edits);
    REQUIRE(!strcmp(source->text, ""));
    return true;
}

TEST_CASE(source_format_rejects_options_and_drafts_atomically)
{
    source = script_parse("behavior Demo {x:=1;}");
    const authoring_format_options invalid[] = {
        { .indent_width = 0 },
        { .indent_width = 17 },
        { .indent_width = 4, .preferred_width = -1 },
        { .indent_width = 4, .preferred_width = 4097 },
    };
    for (int i = 0; i < 4; i++) {
        char error[192];
        text = authoring_source_format_with_options(source, &invalid[i], &edit_map, error, sizeof error);
        REQUIRE(!text && strstr(error, "Invalid formatter options"));
        REQUIRE(
            !edit_map.edits && !edit_map.count && !edit_map.source_size &&
            !edit_map.formatted_size
        );
        REQUIRE(!strcmp(source->text, "behavior Demo {x:=1;}"));
    }
    cleanup();
    source = script_parse("behavior Broken {x:=}");
    text = authoring_source_format_with_options(source, NULL, &edit_map, NULL, 0);
    REQUIRE(!text && !edit_map.edits && !edit_map.count);
    return true;
}

TEST_CASE(source_format_preserves_package_separators)
{
    const struct
    {
        const char *input, *expected;
    } cases[] = {
        { "package app\rimport \"x\"\r", "package app\nimport \"x\"\n" },
        { "package app\nimport \"x\"\n", "package app\nimport \"x\"\n" },
        { "package app\r\nimport \"x\"\r\n", "package app\nimport \"x\"\n" },
        { "package app\r\rimport \"x\"\r", "package app\n\nimport \"x\"\n" },
        { "package app\r\n\rimport \"x\"\r", "package app\n\nimport \"x\"\n" },
        { "package app;import \"x\";", "package app; import \"x\";\n" },
        { "package app /* header */\rimport \"x\"\r",
          "package app /* header */\nimport \"x\"\n" },
        { "package app\r/* header */\rimport \"x\"\r",
          "package app\n/* header */\nimport \"x\"\n" },
        { "package app/*\r*/import \"x\"\r", "package app /*\r*/ import \"x\"\n" },
        { "package app // header\r\nimport \"x\"\r\n",
          "package app // header\r\nimport \"x\"\n" },
        { "package app // header\nimport \"x\"\n", "package app // header\nimport \"x\"\n" },
        { "package app // header\rimport \"x\"\r",
          "package app // header\rimport \"x\"\r\n" },
        { "package app; /* header */\rimport chosen \"x\";\rimport _ \"y\"\r",
          "package app; /* header */\nimport chosen \"x\";\nimport _ \"y\"\n" },
        { "package app\r", "package app\n" },
        { "behavior package{\r\rimport:=1;}\r", "behavior package {\n    import := 1;\n}\n" },
        { "package app\rValues::[\r1,\r2\r]\r",
          "package app\nValues :: [\n    1,\n    2\n]\n" },
    };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        REQUIRE(configured(cases[i].input, authoring_format_defaults()));
        REQUIRE(!strcmp(text, cases[i].expected));
        REQUIRE(map_reproduces_source());
        cleanup();
    }
    REQUIRE(configured(
        "package app\rValues::[\r1,\r2\r]\r",
        (authoring_format_options){ .indent_width = 4, .preferred_width = 80 }
    ));
    REQUIRE(!strcmp(text, "package app\nValues :: [1, 2]\n"));
    REQUIRE(map_reproduces_source());
    cleanup();
    const struct
    {
        const char *input, *expected;
    } width_cases[] = {
        { "package app\rValues::/*\r*/[1,2]\r", "package app\nValues :: /*\r*/ [1, 2]\n" },
        { "package app\nValues::/*\n*/[1,2]\n", "package app\nValues :: /*\n*/ [1, 2]\n" },
        { "package app\r\nValues::/*\r\n*/[1,2]\r\n",
          "package app\nValues :: /*\r\n*/ [1, 2]\n" },
        { "behavior A{x:=/*\r*/[1,2];}",
          "behavior A {\n x := /*\r*/ [\n  1,\n  2\n ];\n}\n" },
    };
    for (int i = 0; i < (int)(sizeof width_cases / sizeof *width_cases); i++) {
        REQUIRE(configured(
            width_cases[i].input,
            (authoring_format_options){ .indent_width = 1, .preferred_width = 12 }
        ));
        REQUIRE(!strcmp(text, width_cases[i].expected));
        REQUIRE(map_reproduces_source());
        cleanup();
    }
    return true;
}

TEST_CASE(source_format_block_layout)
{
    REQUIRE(format("behavior Demo{count:=1;on Tick{count+=2;}}"));
    REQUIRE(
        !strcmp(text, "behavior Demo {\n    count := 1;\n    on Tick {\n        count += 2;\n    }\n}\n")
    );
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
        "on Tick(dt:float){for(i in 0..3){p.x+=float(i);}\n"
        "while p.x>0{p.x-=1;} switch (p.mode){case 1: {continue;} "
        "case: {break;}}}\n"
        "F :: fn(x:array<int>)->(r:int){return(g<int>(x[0]));}}"
    ));
    REQUIRE(strstr(text, "array<int>"));
    REQUIRE(strstr(text, "g<int>(x[0])"));
    REQUIRE(strstr(text, "float(i)"));
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
    authoring_format_options options = { .indent_width = 2, .preferred_width = 80 };
    text = authoring_source_format_with_options(source, &options, &edit_map, error, sizeof error);
    REQUIRE(!text && strstr(error, "limit"));
    REQUIRE(
        !edit_map.edits && !edit_map.count && !edit_map.source_size && !edit_map.formatted_size
    );
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

static bool minimize_valid(const script_catalog *in, const char *input, const char *expected)
{
    source = script_parse(input);
    script_analysis *analysis = script_analyze_document(source, in, "Demo");
    bool valid = analysis->valid && !analysis->diagnostic_count;
    script_analysis_free(analysis);
    REQUIRE(!source->diagnostic_count && valid);
    REQUIRE(minimize_in(in, input, expected));
    formatted = script_parse(text);
    analysis = script_analyze_document(formatted, in, "Demo");
    valid = analysis->valid && !analysis->diagnostic_count;
    script_analysis_free(analysis);
    REQUIRE(!formatted->diagnostic_count && valid);
    again = authoring_source_minimize(text, in, "Demo");
    REQUIRE(again && !strcmp(text, again));
    return true;
}

TEST_CASE(source_minimize_preserves_wrapper_comments)
{
    const char *input = "library Demo { F :: fn(a: float) -> (r: float) { "
                        "r = (/* before hé */ a /* after */) } }";
    REQUIRE(minimize_valid(&catalog, input, input));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        "library Demo { F :: fn(a: float) -> (r: float) { "
        "r = (// before\r\n a // after\r\n); } }",
        "library Demo { F :: fn(a: float) -> (r: float) { "
        "r = (// before\r\n a // after\r\n) } }"
    ));
    cleanup();
    /* Copied operand comments do not prevent removing outer parentheses. */
    REQUIRE(minimize_valid(
        &catalog,
        "library Demo { F :: fn(a: float) -> (r: float) { "
        "r = ((a /* same */ + a /* same */ + a)); } }",
        "library Demo { F :: fn(a: float) -> (r: float) { "
        "r = a /* same */ + a /* same */ + a } }"
    ));
    return true;
}

TEST_CASE(source_minimize_preserves_declaration_and_argument_comments)
{
    REQUIRE(minimize_valid(
        &catalog,
        "library Demo { F :: fn(a: float = 1.0, b: float = 2.0) -> (r: float) "
        "{ r = a + b } "
        "Use :: fn() { x: /* type */ float = F(a: /* label */ 1.0, b: /* "
        "default */ 2.0); } }",
        "library Demo { F :: fn(a: float = 1.0, b: float = 2.0) -> (r: float) "
        "{ r = a + b } "
        "Use :: fn() { x: /* type */ float = F(a: /* label */ 1.0, b: /* "
        "default */ 2.0) } }"
    ));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        "library Demo { F :: fn(a: float = 1.0, b: float = 2.0) -> (r: float) "
        "{ r = a + b } "
        "Use :: fn() { x := F(a: 1.0, /* separator */ b: 2.0); } }",
        "library Demo { F :: fn(a: float = 1.0, b: float = 2.0) -> (r: float) "
        "{ r = a + b } "
        "Use :: fn() { x := F(1.0, /* separator */ 2.0) } }"
    ));
    return true;
}

TEST_CASE(source_minimize_preserves_alias_cast_and_storage_comments)
{
    REQUIRE(minimize_valid(
        &catalog,
        "behavior Demo { on Tick /* alias */ as frame { x := frame.dt; } }",
        "behavior Demo { on Tick /* alias */ as frame { x := frame.dt } }"
    ));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        "library Demo { state total: /* declared */ float = 0.0\n"
        "F :: fn() -> (r: float) { r = float(/* cast */ state./* member "
        "*/total); } }",
        "library Demo { total: /* declared */ float = 0.0\n"
        "F :: fn() -> (r: float) { r = float(/* cast */ state./* member "
        "*/total) } }"
    ));
    return true;
}

TEST_CASE(source_minimize_distinguishes_comments_from_strings)
{
    REQUIRE(
        minimize_valid(&catalog, "library Demo { F :: fn() -> (r: string) { r = (\"/* text */ // text\"); } }", "library Demo { F :: fn() -> (r: string) { r = \"/* text */ // text\" } }")
    );
    return true;
}

TEST_CASE(source_format_options_compose_with_comment_preserving_minimizer)
{
    const char *input = "library Demo { F :: fn(a: float) -> (r: float) { "
                        "r = (/* before hé */ a /* after */) } }";
    char *minimized = authoring_source_minimize(input, &catalog, "Demo");
    REQUIRE(minimized && !strcmp(input, minimized));
    bool valid = configured(
        minimized,
        (authoring_format_options){ .indent_width = 2, .preferred_width = 16 }
    );
    free(minimized);
    REQUIRE(valid);
    REQUIRE(strstr(text, "/* before hé */"));
    REQUIRE(strstr(text, "/* after */"));
    script_analysis *analysis = script_analyze_document(formatted, &catalog, "Demo");
    valid = analysis->valid && !analysis->diagnostic_count;
    script_analysis_free(analysis);
    REQUIRE(valid);
    REQUIRE(map_reproduces_source());
    return true;
}

TEST_CASE(source_format_and_minimize_remain_separate)
{
    const char *input = "library Demo { state count: int = 0\n"
                        "F :: fn() -> (r: int) { state local: int = 1\n"
                        "r = (/* keep */ state.count + local); } }";
    REQUIRE(format(input));
    REQUIRE(strstr(text, "state count: int = 0"));
    REQUIRE(strstr(text, "state local: int = 1"));
    REQUIRE(strstr(text, "r = (/* keep */ state.count + local);"));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        input,
        "library Demo { count: int = 0\n"
        "F :: fn() -> (r: int) { state local: int = 1\n"
        "r = (/* keep */ count + local) } }"
    ));
    free(again);
    char error[192];
    again = authoring_source_format(formatted, error, sizeof error);
    REQUIRE(again);
    REQUIRE(strstr(again, "count: int = 0") && !strstr(again, "state count"));
    REQUIRE(strstr(again, "state local: int = 1"));
    REQUIRE(strstr(again, "r = (/* keep */ count + local)"));
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

TEST_CASE(source_minimize_preserves_accessor_and_operator_comments)
{
    REQUIRE(minimize_valid(
        &component_catalog,
        "behavior Demo { on Tick { if has_velocity(/* present */) { "
        "set_velocity_x(/* set */ multiply(get_velocity(/* get */).Value.x, /* "
        "multiply */ 3.0), self); "
        "} } }",
        "behavior Demo { on Tick { if has_velocity(/* present */) { "
        "set_velocity_x(/* set */ multiply(get_velocity(/* get */).Value.x, /* "
        "multiply */ 3.0), self) "
        "} } }"
    ));
    return true;
}

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

static bool root_storage_identity(const script_analysis *analysis, const char *input)
{
    const char *root_name = strstr(input, "/* root */ count"),
               *callable_name = strstr(input, "/* callable */ count"),
               *read_name = strstr(input, "return count");
    REQUIRE(root_name && callable_name && read_name);
    int root_offset = (int)(root_name - input) + (int)strlen("/* root */ "),
        callable_offset = (int)(callable_name - input) + (int)strlen("/* callable */ "),
        read_offset = (int)(read_name - input) + (int)strlen("return ");
    const script_symbol *root = script_symbol_at(analysis, root_offset),
                        *callable = script_symbol_at(analysis, callable_offset);
    REQUIRE(root && callable && root != callable);
    REQUIRE(root->kind == SCRIPT_STATE && callable->kind == SCRIPT_STATE);
    REQUIRE(root->scope != callable->scope);
    REQUIRE(analysis->scopes[callable->scope].parent == root->scope);
    REQUIRE(root->declaration.start == root_offset && root->declaration.end == root_offset + 5);
    REQUIRE(
        callable->declaration.start == callable_offset &&
        callable->declaration.end == callable_offset + 5
    );
    REQUIRE(root->type.storage == PIN_I32 && callable->type.storage == PIN_I32);
    REQUIRE(root->writable && callable->writable);
    const script_use *use = script_use_at(analysis, read_offset);
    REQUIRE(use && use->kind == SCRIPT_READ);
    REQUIRE(use->symbol == (int)(callable - analysis->symbols));
    REQUIRE(use->range.start == read_offset && use->range.end == read_offset + 5);
    REQUIRE(script_symbol_at(analysis, read_offset) == callable);
    const char *shared_name = strstr(input, "shared other");
    if (shared_name) {
        const script_symbol *shared = script_symbol_at(
            analysis,
            (int)(shared_name - input) + (int)strlen("shared ")
        );
        REQUIRE(shared && shared->kind == SCRIPT_SHARED_STATE);
        REQUIRE(shared->scope == root->scope);
    }
    return true;
}

TEST_CASE(source_minimize_elides_only_redundant_root_state_tokens)
{
    const char *inputs[] = {
        "behavior Demo { state /* root */ count: int = 1\nF :: () -> int { "
        "state /* callable */ count: int = 2\nreturn count } }",
        "library Demo { state /* root */ count: int = 1\nF :: () -> int { "
        "state /* callable */ count: int = 2\nreturn count } }",
        "system Demo { state /* root */ count: int = 1\nshared other: int = "
        "2\nF :: () -> int { state /* callable */ count: int = 2\nreturn count "
        "} }",
    };
    for (int i = 0; i < 3; i++) {
        cleanup();
        source = script_parse(inputs[i]);
        state_before = script_analyze_document(source, &catalog, "Demo");
        REQUIRE(!source->diagnostic_count && state_before->valid);
        REQUIRE(root_storage_identity(state_before, inputs[i]));
        text = authoring_source_minimize(inputs[i], &catalog, "Demo");
        REQUIRE(text && !strstr(text, "state /* root */"));
        REQUIRE(strstr(text, "/* root */ count"));
        REQUIRE(strstr(text, "state /* callable */ count"));
        REQUIRE(i != 2 || strstr(text, "shared other"));
        formatted = script_parse(text);
        state_after = script_analyze_document(formatted, &catalog, "Demo");
        REQUIRE(!formatted->diagnostic_count && state_after->valid);
        REQUIRE(root_storage_identity(state_after, text));
        REQUIRE(state_before->symbol_count == state_after->symbol_count);
        for (int s = 0; s < state_before->symbol_count; s++) {
            REQUIRE(state_before->symbols[s].id == state_after->symbols[s].id);
            REQUIRE(state_before->symbols[s].kind == state_after->symbols[s].kind);
            REQUIRE(state_before->symbols[s].scope == state_after->symbols[s].scope);
        }
        again = authoring_source_minimize(text, &catalog, "Demo");
        REQUIRE(again && !strcmp(text, again));
    }
    return true;
}

TEST_CASE(source_format_preserves_state_tokens_and_minimizer_keeps_invalid_source)
{
    const char *input = "behavior Demo{state /* hé */ count:int=1;}";
    REQUIRE(format(input));
    REQUIRE(strstr(text, "state /* hé */ count"));
    cleanup();
    input = "behavior Demo { state /* keep */ count: int = missing }";
    REQUIRE(minimize(input, input));
    return true;
}

void test_language_root_state()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_minimize_elides_only_redundant_root_state_tokens);
    RUN_TEST_CASE(source_format_preserves_state_tokens_and_minimizer_keeps_invalid_source);
    pu_clear_setup();
}

void test_language_format()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_format_spaces_typed_constant_colons);
    RUN_TEST_CASE(source_format_keeps_attributes_on_their_line);
    RUN_TEST_CASE(source_format_spaces_variant_patterns);
    RUN_TEST_CASE(source_format_aligns_bracket_continuations);
    RUN_TEST_CASE(source_format_aligns_operator_continuations);
    RUN_TEST_CASE(source_format_hugs_struct_braces);
    RUN_TEST_CASE(source_format_options_preserve_defaults);
    RUN_TEST_CASE(source_format_configures_indentation);
    RUN_TEST_CASE(source_format_reflows_lists_and_records);
    RUN_TEST_CASE(source_format_reflows_parameters_calls_and_binary);
    RUN_TEST_CASE(source_format_width_is_byte_based_and_bounded);
    RUN_TEST_CASE(source_format_reflow_preserves_comments);
    RUN_TEST_CASE(source_format_maps_insertions_deletions_and_empty_draft);
    RUN_TEST_CASE(source_format_rejects_options_and_drafts_atomically);
    RUN_TEST_CASE(source_format_preserves_package_separators);
    RUN_TEST_CASE(source_format_block_layout);
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
    RUN_TEST_CASE(source_minimize_reaches_a_fixed_point);
    RUN_TEST_CASE(source_minimize_preserves_wrapper_comments);
    RUN_TEST_CASE(source_minimize_preserves_declaration_and_argument_comments);
    RUN_TEST_CASE(source_minimize_preserves_alias_cast_and_storage_comments);
    RUN_TEST_CASE(source_minimize_distinguishes_comments_from_strings);
    RUN_TEST_CASE(source_format_options_compose_with_comment_preserving_minimizer);
    RUN_TEST_CASE(source_format_and_minimize_remain_separate);
    RUN_TEST_CASE(source_minimize_keeps_argument_order);
    RUN_TEST_CASE(source_minimize_keeps_invalid_source);
    RUN_TEST_CASE(source_minimize_keeps_mixed_grouping_and_tight_ranges);
    RUN_TEST_CASE(source_minimize_preserves_accessor_and_operator_comments);
    RUN_TEST_CASE(source_minimize_spells_accessors_as_paths_and_operators);
    RUN_TEST_CASE(source_minimize_keeps_generic_component_calls);
    RUN_TEST_CASE(source_minimize_elides_only_redundant_root_state_tokens);
    RUN_TEST_CASE(source_format_preserves_state_tokens_and_minimizer_keeps_invalid_source);
    pu_setup(NULL, NULL);
}
