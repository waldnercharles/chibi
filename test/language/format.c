#include "authoring/source_format.h"
#include "authoring/source_minimize.h"
#include "runtime/core_definition.h"
#include "test/parallel.h"
#include "third_party/pico_headers/pico_unit.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local script_syntax *source, *formatted;
static _Thread_local char *text, *again;
static _Thread_local authoring_format_map edit_map;
static _Thread_local script_analysis *state_before, *state_after;
static _Thread_local native_capabilities *core_native;

static void cleanup()
{
    script_analysis_free(state_before);
    script_analysis_free(state_after);
    state_before = state_after = NULL;
    native_capabilities_free(core_native);
    core_native = NULL;
    script_syntax_free(source);
    script_syntax_free(formatted);
    free(text);
    free(again);
    authoring_format_map_free(&edit_map);
    source = formatted = NULL;
    text = again = NULL;
}

static bool format(const char *input)
{
    source = script_parse(input);
    char error[192];
    text = authoring_source_format(source, error, sizeof error);
    if (!text) {
        fprintf(pu_errors(), "format: %s\n", error);
        return false;
    }
    formatted = script_parse(text);
    again = authoring_source_format(formatted, error, sizeof error);
    return again && !strcmp(text, again) && !formatted->diagnostic_count &&
           !strcmp(source->text, input);
}

TEST_CASE(source_format_spaces_managed_pointers_and_xor)
{
    REQUIRE(format(
        "Demo :: behavior { a: ^ int []; b: ^ int ?; c: ^ (int []); "
        "F :: (dt:number m/s^2) {} on Tick :: () { p := & value; "
        "x := p^+1; y := p ^ +q; z := p ^ q; "
        "a := p^^; b := p^.field; c := p^[i]; F(& p^); } }"
    ));
    REQUIRE(strstr(text, "a: ^int[];"));
    REQUIRE(strstr(text, "b: ^int?;"));
    REQUIRE(strstr(text, "c: ^(int[]);"));
    REQUIRE(strstr(text, "m/s^2"));
    REQUIRE(strstr(text, "p := &value;"));
    REQUIRE(strstr(text, "x := p^ + 1;"));
    REQUIRE(strstr(text, "y := p ^ +q;"));
    REQUIRE(strstr(text, "z := p ^ q;"));
    REQUIRE(strstr(text, "a := p^^;"));
    REQUIRE(strstr(text, "b := p^.field;"));
    REQUIRE(strstr(text, "c := p^[i];"));
    REQUIRE(strstr(text, "F(&p^);"));
    return true;
}

static bool configured(const char *input, authoring_format_options options)
{
    source = script_parse(input);
    char error[192];
    text = authoring_source_format_with_options(source, &options, &edit_map, error, sizeof error);
    if (!text) {
        fprintf(pu_errors(), "configured format: %s\n", error);
        return false;
    }
    formatted = script_parse(text);
    again = authoring_source_format_with_options(formatted, &options, NULL, error, sizeof error);
    if (!again || strcmp(text, again)) {
        fprintf(
            pu_errors(),
            "configured format is not idempotent: %s\n%s\n%s\n",
            error,
            text,
            again ? again : "<null>"
        );
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

/* A handler that names its payload fields keeps the list and its types. */
TEST_CASE(source_format_keeps_named_payload_fields)
{
    REQUIRE(format("Demo :: behavior {\non Tick ::(dt:s){}\non Begin::(){}\n}\n"));
    REQUIRE(strstr(text, "on Tick :: (dt: s) {") && strstr(text, "on Begin :: () {"));
    cleanup();
    REQUIRE(format("Motion :: system(p:^Position) {\np.x += dt\n}\n"));
    REQUIRE(strstr(text, "system(p: ^Position) {"));
    return true;
}
TEST_CASE(source_format_flat_queries_and_iterators)
{
    REQUIRE(configured(
        "DamageTargets::system(damage:Damage,target:entity in self.Targets,"
        "health:^Health from target,if health.value>0,"
        "amount:=damage.amount*2,) {health.value-=amount;}\n",
        (authoring_format_options){ .indent_width = 4, .preferred_width = 65 }
    ));
    REQUIRE(strstr(text, "system(\n    damage: Damage,"));
    REQUIRE(strstr(text, "target: entity in self.Targets,"));
    REQUIRE(strstr(text, "health: ^Health from target,"));
    REQUIRE(strstr(text, "if health.value > 0,"));
    REQUIRE(strstr(text, "amount := damage.amount * 2,\n)"));
    REQUIRE(map_reproduces_source());
    cleanup();
    REQUIRE(format(
        "Demo::behavior{on Tick::(){rows:=query(d:Damage,h:^Health,"
        "if h.value>0);for row in rows{row.h.value-=row.d.amount;}}}"
    ));
    REQUIRE(strstr(text, "query(d: Damage, h: ^Health, if h.value > 0)"));
    return true;
}

TEST_CASE(source_format_spaces_typed_constant_colons)
{
    REQUIRE(format("L :: library {\n    Limit:int:5\n    Mask :: 255\n}\n"));
    REQUIRE(strstr(text, "Limit: int : 5") && strstr(text, "Mask :: 255"));
    return true;
}
TEST_CASE(source_format_spaces_reverse_and_half_open_loops)
{
    REQUIRE(
        format("L :: library {\n    F :: (n: int) {\n        for<i in 0 ..< n {}\n        for <x in [1] {}\n    }\n}\n")
    );
    REQUIRE(strstr(text, "for < i in 0..<n {}") && strstr(text, "for < x in [1] {}"));
    return true;
}
TEST_CASE(source_format_keeps_attributes_on_their_line)
{
    REQUIRE(format("package facts\n@test\nAdds :: () { }\n@ test Second :: () { }\n"));
    REQUIRE(!strcmp(text, "package facts\n@test\nAdds :: () {}\n@test Second :: () {}\n"));
    return true;
}
TEST_CASE(source_format_preserves_fallible_named_results)
{
    REQUIRE(format(
        "package facts\n@ fallible\npub Read :: () -> (value: int, error: "
        "int?) { }\n"
    ));
    REQUIRE(!strcmp(
        text,
        "package facts\n@fallible\npub Read :: () -> (value: int, error: int?) "
        "{}\n"
    ));
    return true;
}
TEST_CASE(source_format_spaces_pub_and_aligns_after_it)
{
    REQUIRE(format(
        "package facts\n@test pub   Adds :: () { }\npub Limit:int:5;\n"
        "pub Sum :: (first: int,\nsecond: int) -> int {\n    return first;\n}\n"
    ));
    REQUIRE(!strcmp(
        text,
        "package facts\n@test pub Adds :: () {}\npub Limit: int : 5;\n"
        "pub Sum :: (first: int,\n            second: int) -> int {\n    "
        "return first;\n}\n"
    ));
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
                 "Area :: (shape: Shape) -> int {\n"
                 "    switch shape {\n"
                 "        case .Circle(radius):\n"
                 "            return radius;\n"
                 "        case .Empty:\n"
                 "            return 0;\n"
                 "    }\n"
                 "}\n"
                 "Round :: (shape: Shape) -> bool {\n"
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
        "Sum :: (first: int,\n"
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
        "Sum :: (first: int,\n"
        "        second: int) -> int {\n"
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
        "package facts\nSum :: (first: int, second: int) -> int {\n"
        "return Add(first,\nsecond);\n}\n",
        (authoring_format_options){ .indent_width = 4, .use_tabs = true }
    ));
    REQUIRE(!strcmp(
        text,
        "package facts\nSum :: (first: int, second: int) -> int {\n"
        "\treturn Add(first,\n\t           second);\n}\n"
    ));
    REQUIRE(map_reproduces_source());
    return true;
}
TEST_CASE(source_format_aligns_operator_continuations)
{
    REQUIRE(format(
        "package facts\n"
        "Valid :: (first: int, second: int) -> bool {\n"
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
        "Valid :: (first: int, second: int) -> bool {\n"
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
    REQUIRE(format("package facts\nOrigin :: () -> Point {\n    return { x: 0,y: 0 };\n}\n"));
    REQUIRE(strstr(text, "    return {x: 0, y: 0};\n"));
    cleanup();
    REQUIRE(format("Demo :: behavior{value:={ left:1 };empty:={ };}"));
    REQUIRE(strstr(text, "    value := {left: 1};\n"));
    REQUIRE(strstr(text, "    empty := {};\n"));
    return true;
}
TEST_CASE(source_format_options_preserve_defaults)
{
    const char *input = "Demo :: behavior{value:=[\n1,\n2];on Tick :: "
                        "(){value[0]+=1;}}";
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
    const char *input = "Demo :: behavior{on Tick :: (){call(\n1,\n2);}}";
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
    REQUIRE(
        !strcmp(text, "Demo :: behavior {\n\ton Tick :: () {\n\t\tcall(\n\t\t\t1,\n\t\t\t2);\n\t}\n}\n")
    );
    return true;
}

TEST_CASE(source_format_reflows_lists_and_records)
{
    const char
        *input = "Demo :: "
                 "behavior{numbers:=[1,2,3];value:={left:1,right:[2,3]};}";
    REQUIRE(
        configured(input, (authoring_format_options){ .indent_width = 2, .preferred_width = 20 })
    );
    REQUIRE(
        !strcmp(text, "Demo :: behavior {\n  numbers := [\n    1,\n    2,\n    3\n  ];\n  value := {\n    left: 1,\n    right: [2, 3]\n  };\n}\n")
    );
    REQUIRE(map_reproduces_source());
    cleanup();
    REQUIRE(configured(
        "Demo :: behavior{numbers:=[\n1,\n2,\n3\n];}",
        (authoring_format_options){ .indent_width = 2, .preferred_width = 80 }
    ));
    REQUIRE(strstr(text, "numbers := [1, 2, 3];"));
    return true;
}

TEST_CASE(source_format_reflows_parameters_calls_and_binary)
{
    REQUIRE(configured(
        "Demo :: "
        "library{Compute::(first:int,second:int)->int{result:=(first+second*"
        "123);"
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
    const char *input = "Demo :: behavior{x:=[\"é\",\"é\"];empty:=[];}";
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
        "/* module\r\n comment */\r\nDemo :: behavior{x:=[1, // first\r\n2, /* "
        "hé "
        "*/ 3];on Tick :: (){call(\"// exact\",/* x\ny */\"é\");}} // end",
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
    REQUIRE(configured("  Demo :: behavior{ x :=  1 ; }  ", authoring_format_defaults()));
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
    REQUIRE(configured("Demo :: behavior {\n    x := 1;\n}\n", authoring_format_defaults()));
    REQUIRE(edit_map.count == 0);
    REQUIRE(
        authoring_format_map_position(&edit_map, edit_map.source_size, AUTHORING_FORMAT_AFTER) ==
        edit_map.formatted_size
    );
    cleanup();
    REQUIRE(configured("Demo :: behavior {}", authoring_format_defaults()));
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
    source = script_parse("Demo :: behavior {x:=1;}");
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
        REQUIRE(!strcmp(source->text, "Demo :: behavior {x:=1;}"));
    }
    cleanup();
    source = script_parse("Broken :: behavior {x:=}");
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
        { "package :: behavior{\r\rimport:=1;}\r",
          "package :: behavior {\n    import := 1;\n}\n" },
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
        { "A :: behavior{x:=/*\r*/[1,2];}",
          "A :: behavior {\n x := /*\r*/ [\n  1,\n  2\n ];\n}\n" },
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
    REQUIRE(format("Demo :: behavior{count:=1;on Tick :: (){count+=2;}}"));
    REQUIRE(
        !strcmp(text, "Demo :: behavior {\n    count := 1;\n    on Tick :: () {\n        count += 2;\n    }\n}\n")
    );
    return true;
}
TEST_CASE(source_format_reductions)
{
    REQUIRE(format(
        "Damage :: system(health:^Health){reduce health.damage+=2;reduce min "
        "(health.damage,3);reduce max(health.damage,4);reduce unordered "
        "health.damage+=5; }"
    ));
    REQUIRE(!strcmp(
        text,
        "Damage :: system(health: ^Health) {\n"
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
        "// Fired.\nProjectile :: component{speed : 0 .. 600 u / s\n"
        "// Pull.\naccel:u/s ^ 2;drift:- 1..1 rad\nlevel:number}"
    ));
    REQUIRE(!strcmp(
        text,
        "// Fired.\n"
        "Projectile :: component {\n"
        "    speed: 0..600 u/s\n"
        "    // Pull.\n"
        "    accel: u/s^2;\n"
        "    drift: -1..1 rad\n"
        "    level: number\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_format_measured_signatures)
{
    REQUIRE(format(
        "Measures :: library{Travel::(speed:number u / s,elapsed:s)->u{return "
        "speed*elapsed;}"
        "Borrow::(value:^ number charge)->^number charge{return value;}}"
    ));
    REQUIRE(strstr(text, "speed: number u/s"));
    REQUIRE(strstr(text, "elapsed: s"));
    REQUIRE(strstr(text, "-> u"));
    REQUIRE(strstr(text, "value: ^number charge"));
    REQUIRE(strstr(text, "-> ^number charge"));
    return true;
}

TEST_CASE(source_format_preserves_comments_and_literals)
{
    const char
        *input = "// module comment\nquoted_name :: behavior { "
                 "label: string=\"héllo { // }\"; // trailing\n"
                 "/* first\n second */ on Begin :: () { print(label); /* "
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
        "Math :: library{Compute :: (x:int)->(r:int){\n"
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
        "Move :: system(p:^Position,if "
        "!Disabled){matches:=query(other:Position);\n"
        "on Tick :: (dt:number){for(i in 0..3){p.x+=number(i);}\n"
        "while p.x>0{p.x-=1;} switch (p.mode){case 1: {continue;} "
        "case: {break;}}}\n"
        "F :: (x:int[])->(r:int){return(g(x[0]));}}"
    ));
    REQUIRE(strstr(text, "x: int[]"));
    REQUIRE(strstr(text, "g(x[0])"));
    REQUIRE(strstr(text, "number(i)"));
    return true;
}

TEST_CASE(source_format_switch_arm_indentation)
{
    REQUIRE(format(
        "Damage :: library{Base::2;DamageFor::(level:int)->number{"
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
        "Move :: system(position:^Transform,velocity:Velocity?, "
        "aim:^Transform? from Targets, if not Disabled and "
        "(Targets.Active or ancestor(ChildOf).Active) and has(Targets))"
        "{on Tick :: (){position.x+=1;}}"
    ));
    REQUIRE(
        strstr(text, "if not Disabled and (Targets.Active or ancestor(ChildOf).Active) and has(Targets)")
    );
    REQUIRE(strstr(text, "velocity: Velocity?"));
    REQUIRE(strstr(text, "aim: ^Transform? from Targets"));
    return true;
}

TEST_CASE(source_format_query_generators_and_value_filters)
{
    REQUIRE(format(
        "Follow :: system(p:^Position, target in self.Targets, "
        "next in target.Targets, aim:Position? from next, "
        "if aim and aim.x>p.x and changed(aim)){p.x=aim.x;}"
    ));
    REQUIRE(strstr(text, "target in self.Targets"));
    REQUIRE(strstr(text, "next in target.Targets"));
    REQUIRE(strstr(text, "aim: Position? from next"));
    REQUIRE(strstr(text, "if aim and aim.x > p.x and changed(aim)"));
    return true;
}

TEST_CASE(source_format_incomplete_source_is_unchanged)
{
    const char *input = "Broken :: behavior { bad := ; later := 7 }";
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
    int n = snprintf(nested, sizeof nested, "Deep :: behavior { x := ");
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
        fprintf(pu_errors(), "minimize:\n%s\nexpected:\n%s\n", text, expected);
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
        "Demo :: library {\n"
        "    Scale :: (a: number = 0.0, b: number = 2.0) -> (r: number = "
        "0.0) {\n"
        "        r = a * b;\n"
        "        return;\n"
        "    }\n"
        "    Twice :: (a: number = 0.0) -> (r: number = 0.0) {\n"
        "        return(a * 2.0);\n"
        "    }\n"
        "    Use :: () -> () {\n"
        "        x: number = Scale(a: 1.0, b: 2.0);\n"
        "        y: number = Scale(b: 3.0);\n"
        "        if (x > 0) {\n"
        "            x = 1.0;\n"
        "        } else {\n"
        "        }\n"
        "        return();\n"
        "    }\n"
        "}\n",
        "Demo :: library {\n"
        "    Scale :: (a: number = 0.0, b: number = 2.0) -> (r: number = "
        "0.0) {\n"
        "        r = a * b\n"
        "        return\n"
        "    }\n"
        "    Twice :: (a: number = 0.0) -> (r: number = 0.0) {\n"
        "        return a * 2.0\n"
        "    }\n"
        "    Use :: () {\n"
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

TEST_CASE(source_minimize_treats_character_literals_as_literals)
{
    REQUIRE(minimize(
        "Demo :: library {\n"
        "    Next :: (c: char = 'a') -> char {\n"
        "        d: char = ('b');\n"
        "        return d;\n"
        "    }\n"
        "    Use :: () -> char {\n"
        "        return Next('a');\n"
        "    }\n"
        "}\n",
        "Demo :: library {\n"
        "    Next :: (c: char = 'a') -> char {\n"
        "        d := 'b'\n"
        "        return d\n"
        "    }\n"
        "    Use :: () -> char {\n"
        "        return Next()\n"
        "    }\n"
        "}\n"
    ));
    return true;
}
TEST_CASE(source_minimize_reaches_a_fixed_point)
{
    /* Each round drops only the outermost of nested redundant parentheses. */
    REQUIRE(minimize(
        "Demo :: library {\n"
        "    Use :: (a: number = 0.0) -> (r: number = 0.0) {\n"
        "        return ((((((((((((a))))))))))))\n"
        "    }\n"
        "}\n",
        "Demo :: library {\n"
        "    Use :: (a: number = 0.0) -> (r: number = 0.0) {\n"
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
    const char *input = "Demo :: library { F :: (a: number) -> (r: number) { "
                        "r = (/* before hé */ a /* after */); return } }";
    REQUIRE(minimize_valid(&catalog, input, input));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        "Demo :: library { F :: (a: number) -> (r: number) { "
        "r = (// before\r\n a // after\r\n); return; } }",
        "Demo :: library { F :: (a: number) -> (r: number) { "
        "r = (// before\r\n a // after\r\n); return } }"
    ));
    cleanup();
    /* Copied operand comments do not prevent removing outer parentheses. */
    REQUIRE(minimize_valid(
        &catalog,
        "Demo :: library { F :: (a: number) -> (r: number) { "
        "r = ((a /* same */ + a /* same */ + a)); return; } }",
        "Demo :: library { F :: (a: number) -> (r: number) { "
        "r = a /* same */ + a /* same */ + a; return } }"
    ));
    return true;
}

TEST_CASE(source_minimize_preserves_declaration_and_argument_comments)
{
    REQUIRE(minimize_valid(
        &catalog,
        "Demo :: library { F :: (a: number = 1.0, b: number = 2.0) -> (r: "
        "number) "
        "{ r = a + b; return } "
        "Use :: () { x: /* type */ number = F(a: /* label */ 1.0, b: /* "
        "default */ 2.0); } }",
        "Demo :: library { F :: (a: number = 1.0, b: number = 2.0) -> (r: "
        "number) "
        "{ r = a + b; return } "
        "Use :: () { x: /* type */ number = F(a: /* label */ 1.0, b: /* "
        "default */ 2.0) } }"
    ));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        "Demo :: library { F :: (a: number = 1.0, b: number = 2.0) -> (r: "
        "number) "
        "{ r = a + b; return } "
        "Use :: () { x := F(a: 1.0, /* separator */ b: 2.0); } }",
        "Demo :: library { F :: (a: number = 1.0, b: number = 2.0) -> (r: "
        "number) "
        "{ r = a + b; return } "
        "Use :: () { x := F(1.0, /* separator */ 2.0) } }"
    ));
    return true;
}

TEST_CASE(source_minimize_preserves_cast_and_storage_comments)
{
    REQUIRE(minimize_valid(
        &catalog,
        "Demo :: library { total: retain /* declared */ number = 0.0\n"
        "F :: () -> (r: number) { r = number(/* cast */ state./* member "
        "*/total); return; } }",
        "Demo :: library { total: /* declared */ number = 0.0\n"
        "F :: () -> (r: number) { r = number(/* cast */ state./* member "
        "*/total); return } }"
    ));
    return true;
}

TEST_CASE(source_minimize_distinguishes_comments_from_strings)
{
    REQUIRE(
        minimize_valid(&catalog, "Demo :: library { F :: () -> (r: string) { r = (\"/* text */ // text\"); return; } }", "Demo :: library { F :: () -> (r: string) { r = \"/* text */ // text\"; return } }")
    );
    return true;
}

TEST_CASE(source_format_options_compose_with_comment_preserving_minimizer)
{
    const char *input = "Demo :: library { F :: (a: number) -> (r: number) { "
                        "r = (/* before hé */ a /* after */); return } }";
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
    const char *input = "Demo :: library { count: retain int = 0\n"
                        "F :: () -> (r: int) { local: retain int = 1\n"
                        "r = (/* keep */ state.count + local); return; } }";
    REQUIRE(format(input));
    REQUIRE(strstr(text, "count: retain int = 0"));
    REQUIRE(strstr(text, "local: retain int = 1"));
    REQUIRE(strstr(text, "r = (/* keep */ state.count + local);"));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        input,
        "Demo :: library { count := 0\n"
        "F :: () -> (r: int) { local: retain = 1\n"
        "r = (/* keep */ count + local); return } }"
    ));
    free(again);
    char error[192];
    again = authoring_source_format(formatted, error, sizeof error);
    REQUIRE(again);
    REQUIRE(strstr(again, "count := 0") && !strstr(again, "count: retain"));
    REQUIRE(strstr(again, "local: retain = 1"));
    REQUIRE(strstr(again, "r = (/* keep */ count + local)"));
    return true;
}

/* A handler that names its payload fields keeps the list and its types. */
TEST_CASE(source_minimize_keeps_named_payload_fields)
{
    REQUIRE(minimize(
        "Demo :: behavior {\n"
        "    angle: number = 0.0\n"
        "    on Tick :: (dt: number) {\n"
        "        angle = angle + dt\n"
        "    }\n"
        "    on Tick :: (dt: number s) {\n"
        "        angle = angle - dt\n"
        "    }\n"
        "}\n",
        "Demo :: behavior {\n"
        "    angle := 0.0\n"
        "    on Tick :: (dt: number) {\n"
        "        angle = angle + dt\n"
        "    }\n"
        "    on Tick :: (dt: number s) {\n"
        "        angle = angle - dt\n"
        "    }\n"
        "}\n"
    ));
    return true;
}
TEST_CASE(source_minimize_keeps_argument_order)
{
    /* Arguments run in source order, so out-of-order labels stay. */
    REQUIRE(minimize(
        "Demo :: library {\n"
        "    Pair :: (a: number = 0.0, b: number = 0.0) -> (r: number = 0.0) "
        "{\n"
        "        return a - b\n"
        "    }\n"
        "    Next :: () -> (r: number = 0.0) {\n"
        "        return 1.0\n"
        "    }\n"
        "    Use :: () -> (r: number = 0.0) {\n"
        "        return Pair(b: Next(), a: Next())\n"
        "    }\n"
        "}\n",
        "Demo :: library {\n"
        "    Pair :: (a: number = 0.0, b: number = 0.0) -> (r: number = 0.0) "
        "{\n"
        "        return a - b\n"
        "    }\n"
        "    Next :: () -> (r: number = 0.0) {\n"
        "        return 1.0\n"
        "    }\n"
        "    Use :: () -> (r: number = 0.0) {\n"
        "        return Pair(b: Next(), a: Next())\n"
        "    }\n"
        "}\n"
    ));
    return true;
}

TEST_CASE(source_minimize_keeps_invalid_source)
{
    const char
        *input = "Demo :: library { F :: () -> () { x := missing(a: 1); } }";
    REQUIRE(minimize(input, input));
    return true;
}

TEST_CASE(source_format_keeps_else_and_arrow_spacing)
{
    REQUIRE(format("M :: library{F :: (x:int)->(r:int){if x>0{r=1;}\nelse{r=self;}}}"));
    REQUIRE(strstr(text, "-> (r: int)"));
    REQUIRE(strstr(text, "} else {"));
    REQUIRE(strstr(text, "self"));
    return true;
}

TEST_CASE(source_minimize_keeps_mixed_grouping_and_tight_ranges)
{
    REQUIRE(minimize(
        "Demo :: library {\n"
        "    F :: (a: bool, b: bool, c: bool, m: int) {\n"
        "        x := a || (b && c)\n"
        "        y := (a && b) || (c)\n"
        "        z := m | (m & 3)\n"
        "        w := m + (m * 3)\n"
        "        for i in 0 .. m - 1 {\n"
        "            w += i\n"
        "        }\n"
        "    }\n"
        "}\n",
        "Demo :: library {\n"
        "    F :: (a: bool, b: bool, c: bool, m: int) {\n"
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
    REQUIRE(format("M :: library{F :: (m:int){for i in 0 .. m-1{m+=i;}}}"));
    REQUIRE(strstr(text, "for i in 0..m - 1 {"));
    return true;
}

static const script_catalog_field velocity_field = { .id = 81,
                                                     .name = "x",
                                                     .type = { .storage = PIN_F32 },
                                                     .writable = true };
static const script_catalog_type velocity_type = {
    .id = 80,
    .name = "Velocity",
    .type = { .storage = PIN_STRUCT, .nominal = 80 },
    .fields = &velocity_field,
    .field_count = 1,
    .size = sizeof(float),
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
    { .name = "Value", .type = { .storage = PIN_F32 } },
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
    { .name = "A", .type = { .storage = PIN_F32 } },
    { .name = "B", .type = { .storage = PIN_F32 } }
};
static const script_catalog_parameter wides[] = {
    { .name = "A", .type = { .storage = PIN_F64 } },
    { .name = "B", .type = { .storage = PIN_F64 } }
};
static const script_catalog_parameter real_result[] = {
    { .name = "Result", .type = { .storage = PIN_F32 } }
};
static const char *const setter_completion[] = { "Completed" };
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
      .outcomes = setter_completion,
      .outcome_count = 1,
      .completion_outcome = 0,
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
      .name = "less_wide",
      .operator_name = "<",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = wides,
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
/* An `after` list that repeats the results' own names drops; one that
 * renames them stays. */
static const script_catalog_parameter measured[] = {
    { .name = "Value", .type = { .storage = PIN_F64 } },
};
static const char *const measure_completion[] = { "Completed" };
static const script_catalog_capability measure_capabilities[] = {
    { .id = 30, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    { .id = 31,
      .name = "Measure",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .results = measured,
      .result_count = 1,
      .outcomes = measure_completion,
      .outcome_count = 1,
      .completion_outcome = 0 },
};
static const script_catalog measure_catalog = { .capabilities = measure_capabilities,
                                                .capability_count = 2 };
TEST_CASE(source_minimize_drops_repeated_result_lists)
{
    const char *prefix = "Demo :: behavior { saved: f64 = 0; action update = "
                         "Measure(); on Tick :: () { update() } ";
    char input[512], expected[512];
    snprintf(input, sizeof input, "%safter update(Value) { saved = Value } }", prefix);
    snprintf(expected, sizeof expected, "%safter update { saved = Value } }", prefix);
    REQUIRE(minimize_in(&measure_catalog, input, expected));
    cleanup();
    snprintf(input, sizeof input, "%safter update(moved) { saved = moved } }", prefix);
    REQUIRE(minimize_in(&measure_catalog, input, input));
    return true;
}
TEST_CASE(source_minimize_preserves_accessor_and_operator_comments)
{
    REQUIRE(minimize_in(
        &component_catalog,
        "Demo :: behavior { on Tick :: () { if has_velocity(/* present */) { "
        "set_velocity_x(/* set */ multiply(get_velocity(/* get */).Value.x, /* "
        "multiply */ 3.0), self); "
        "} } }",
        "Demo :: behavior { on Tick :: () { if has_velocity(/* present */) { "
        "set_velocity_x(/* set */ multiply(get_velocity(/* get */).Value.x, /* "
        "multiply */ 3.0), self); "
        "} } }"
    ));
    return true;
}

TEST_CASE(source_minimize_spells_accessors_as_paths_and_operators)
{
    char error[256];
    const char
        *input = "Demo :: behavior {\n"
                 "    on Tick :: () {\n"
                 "        if has_velocity() {\n"
                 "            set_velocity_x(multiply(get_velocity().Value.x, "
                 "2.0))\n"
                 "        }\n"
                 "        if less(get_velocity().Value.x, 1.0) {\n"
                 "            set_velocity_x(multiply(2.0, 3.0), self)\n"
                 "        }\n"
                 "        e := self\n"
                 "        set_velocity_x(get_velocity().Value.x, e)\n"
                 "        lifted := has_velocity()\n"
                 "        widened := less_wide(get_velocity().Value.x, "
                 "get_velocity().Value.x)\n"
                 "    }\n"
                 "}\n";
    /* Native setters evaluate a non-self entity first, unlike authored calls. */
    text = authoring_source_migrate_generated(input, &component_catalog, "Demo", error, sizeof error);
    REQUIRE(!text && strstr(error, "entity-first"));
    script_catalog_type invalid_type = velocity_type;
    invalid_type.source_name = "Velocity!";
    script_catalog invalid_catalog = component_catalog;
    invalid_catalog.types = &invalid_type;
    text = authoring_source_migrate_generated(
        "Demo :: behavior { on Tick :: () { current := get_velocity(); } }",
        &invalid_catalog,
        "Demo",
        error,
        sizeof error
    );
    REQUIRE(!text && error[0]);
    return true;
}

TEST_CASE(source_migration_preserves_deferred_setter_actions_and_results)
{
    script_catalog_parameter parameters[2];
    memcpy(parameters, field_write, sizeof parameters);
    parameters[0].has_default = true;
    parameters[0].default_value = (pin_value){ .type = PIN_STRUCT };
    script_catalog_capability capabilities[sizeof component_capabilities / sizeof *component_capabilities];
    memcpy(capabilities, component_capabilities, sizeof capabilities);
    capabilities[3].parameters = parameters;
    script_catalog native = component_catalog;
    native.capabilities = capabilities;
    const char *input = "Demo :: behavior { saved: number = 0; "
                        "action update = set_velocity_x(Target: self, Value: "
                        "saved + 1); "
                        "on Tick :: () { update(); } after update { saved = "
                        "update.Value.x; } }";
    char error[256];
    text = authoring_source_migrate_generated(input, &native, "Demo", error, sizeof error);
    if (!text) {
        fprintf(pu_errors(), "deferred migration: %s\n", error);
    }
    REQUIRE(text && !strstr(text, "set_velocity_x"));
    REQUIRE(strstr(text, "action update = import_component_write_"));
    REQUIRE(strstr(text, "after update") && strstr(text, "update.Value.x"));
    REQUIRE(strstr(text, ".Velocity.x = import_arg_0"));
    REQUIRE(strstr(text, "return import_arg_1.Velocity"));
    formatted = script_parse(text);
    state_after = script_analyze_document(formatted, &native, "Demo");
    REQUIRE(state_after->valid);
    return true;
}

TEST_CASE(source_canonical_is_idempotent_for_compact_property_helpers)
{
    const char
        *input = "Demo :: behavior { Write :: (value: Velocity) -> (Value: "
                 "Velocity) "
                 "{ self.Velocity = value; /* retained */ return value; } "
                 "action update = Write({x: 1}); on Tick :: () { update(); } }";
    char error[256];
    text = authoring_source_canonical(input, &component_catalog, "Demo", error, sizeof error);
    REQUIRE(text);
    again = authoring_source_canonical(text, &component_catalog, "Demo", error, sizeof error);
    if (!again || strcmp(text, again)) {
        fprintf(pu_errors(), "canonical first:\n%s\nsecond:\n%s\n", text, again ? again : error);
    }
    REQUIRE(again && !strcmp(text, again));
    REQUIRE(strstr(text, "/* retained */"));
    REQUIRE(strstr(text, "self.Velocity = value"));
    source = script_parse(text);
    state_after = script_analyze_document(source, &component_catalog, "Demo");
    REQUIRE(state_after->valid);
    return true;
}

static bool core_migration_catalog()
{
    native_capability_definition alias = { 0 };
    for (int i = 0; i < CORE_DEFINITION_PACK.definition_count; i++) {
        if (CORE_DEFINITION_PACK.definitions[i].operation == SCRIPT_OPERATION_OWNER) {
            alias = CORE_DEFINITION_PACK.definitions[i];
        }
    }
    alias.canonical_name = "OwnerAlias";
    alias.source_name = "owner_alias";
    const native_definition_pack definitions[] = {
        CORE_DEFINITION_PACK,
        { .definitions = &alias, .definition_count = 1 },
    };
    const script_catalog_capability tick_event = {
        .id = 30,
        .name = "Tick",
        .kind = SCRIPT_CAPABILITY_EVENT,
        .completion_outcome = -1,
    };
    const script_catalog catalog = { .capabilities = &tick_event,
                                     .capability_count = 1,
                                     .tick_event = 30 };
    const native_capability_pack pack = { .catalog = &catalog };
    char error[256];
    core_native = native_capabilities_from_definition_packs(
        &pack,
        1,
        definitions,
        NATIVE_COUNT(definitions),
        error,
        sizeof error
    );
    if (!core_native) {
        fprintf(pu_errors(), "core migration catalog: %s\n", error);
    }
    return core_native != NULL;
}
TEST_CASE(source_migration_matches_core_self_effects_in_each_program_context)
{
    const char *inputs[] = {
        "Demo :: behavior { on Tick :: () { e := Self(); same := Self() == "
        "self; } }",
        "Demo :: library { Read :: () { e := Self(); same := Self() == self; } "
        "}",
        "Demo :: system(if Self() == self) {}",
    };
    for (int i = 0; i < NATIVE_COUNT(inputs); i++) {
        cleanup();
        REQUIRE(core_migration_catalog());
        const script_catalog *catalog = native_capabilities_catalog(core_native);
        source = script_parse(inputs[i]);
        state_before = script_analyze_document(source, catalog, "Demo");
        REQUIRE(!state_before->valid);
        script_analysis_free(state_before);
        state_before = script_analyze_migration_document(source, catalog, "Demo", NULL, 0, false);
        REQUIRE(state_before->valid && state_before->function_count == 1);
        bool invocation = i == 1;
        REQUIRE(
            !!(state_before->functions[0].effects & SCRIPT_EFFECT_INVOCATION_READ) == invocation
        );
        char error[256];
        text = authoring_source_migrate_generated(inputs[i], catalog, "Demo", error, sizeof error);
        if (!text) {
            fprintf(pu_errors(), "core Self migration: %s\n", error);
        }
        REQUIRE(text && !strstr(text, "Self()"));
        REQUIRE(i == 2 || strstr(text, "e := self"));
        REQUIRE(strstr(text, "self == self"));
        formatted = script_parse(text);
        state_after = script_analyze_document(formatted, catalog, "Demo");
        REQUIRE(state_after->valid && state_after->function_count == 1);
        REQUIRE(state_before->functions[0].effects == state_after->functions[0].effects);
    }
    return true;
}
TEST_CASE(source_migration_preserves_other_invocation_sensitive_calls)
{
    REQUIRE(core_migration_catalog());
    const script_catalog *core = native_capabilities_catalog(core_native);
    source = script_parse("Demo :: behavior { on Tick :: () { e := owner_alias(); } }");
    state_before = script_analyze_migration_document(source, core, "Demo", NULL, 0, false);
    REQUIRE(state_before->valid && state_before->function_count == 1);
    REQUIRE(state_before->functions[0].effects & SCRIPT_EFFECT_INVOCATION_READ);
    script_analysis_free(state_before);
    script_syntax_free(source);
    source = script_parse("Demo :: system(if owner_alias() == self) {}");
    state_before = script_analyze_migration_document(source, core, "Demo", NULL, 0, false);
    REQUIRE(!state_before->valid);
    bool rejected = false;
    for (int i = 0; i < state_before->diagnostic_count; i++) {
        rejected |= strstr(state_before->diagnostics[i].message, "Query predicate cannot read") !=
                    NULL;
    }
    REQUIRE(rejected);
    script_catalog_capability caps[NATIVE_COUNT(component_capabilities)];
    memcpy(caps, component_capabilities, sizeof caps);
    int multiply = script_catalog_find_capability(&component_catalog, "multiply", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(multiply >= 0);
    caps[multiply].effects = SCRIPT_EFFECT_INVOCATION_READ;
    script_catalog catalog = component_catalog;
    catalog.capabilities = caps;
    const char *input = "Demo :: behavior { on Tick :: (dt: number) { v := "
                        "multiply(2.0, 3.0); } }";
    script_analysis_free(state_before);
    script_syntax_free(source);
    source = script_parse(input);
    REQUIRE(!source->diagnostic_count);
    state_before = script_analyze_document(source, &catalog, "Demo");
    REQUIRE(state_before->valid && state_before->function_count == 1);
    REQUIRE(state_before->functions[0].effects & SCRIPT_EFFECT_INVOCATION_READ);
    text = authoring_source_minimize(input, &catalog, "Demo");
    REQUIRE(text && strstr(text, "multiply("));
    formatted = script_parse(text);
    REQUIRE(!formatted->diagnostic_count);
    state_after = script_analyze_document(formatted, &catalog, "Demo");
    REQUIRE(state_after->valid && state_after->function_count == 1);
    REQUIRE(state_after->functions[0].effects & SCRIPT_EFFECT_INVOCATION_READ);
    return true;
}
TEST_CASE(source_migration_keeps_system_row_owner_distinct)
{
    REQUIRE(core_migration_catalog());
    const script_catalog *core = native_capabilities_catalog(core_native);
    script_catalog_capability caps[core->capability_count];
    memcpy(caps, core->capabilities, sizeof caps);
    script_catalog catalog = *core;
    catalog.capabilities = caps;
    int self = script_catalog_find_capability(core, "Self", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(self >= 0);
    const char
        *input = "Demo :: system() { e := Self(); same := Self() == self; }";
    char error[256];
    text = authoring_source_migrate_generated(input, core, "Demo", error, sizeof error);
    REQUIRE(!text && strstr(error, "Use self instead of Self()"));
    /* The pre-operation main definition had UNKNOWN identity and zero
     * effects. Its migration also refuses the different system-row binding. */
    caps[self].operation = SCRIPT_OPERATION_UNKNOWN;
    caps[self].effects = 0;
    text = authoring_source_migrate_generated(input, &catalog, "Demo", error, sizeof error);
    REQUIRE(!text && strstr(error, "Use self instead of Self()"));
    return true;
}
TEST_CASE(source_migration_rejects_opaque_or_mismatched_self_equivalence)
{
    REQUIRE(core_migration_catalog());
    const script_catalog *core = native_capabilities_catalog(core_native);
    script_catalog_capability caps[core->capability_count];
    memcpy(caps, core->capabilities, sizeof caps);
    script_catalog catalog = *core;
    catalog.capabilities = caps;
    int self = script_catalog_find_capability(core, "Self", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(self >= 0);
    const char *input = "Demo :: behavior { on Tick :: () { e := Self(); } }";
    caps[self].operation = SCRIPT_OPERATION_UNKNOWN;
    source = script_parse(input);
    state_before = script_analyze_migration_document(source, &catalog, "Demo", NULL, 0, false);
    REQUIRE(state_before->valid && state_before->function_count == 1);
    REQUIRE(state_before->functions[0].effects & SCRIPT_EFFECT_INVOCATION_READ);
    char error[256];
    text = authoring_source_migrate_generated(input, &catalog, "Demo", error, sizeof error);
    REQUIRE(!text && strstr(error, "Use self instead of Self()"));
    caps[self] = core->capabilities[self];
    script_catalog_parameter result = caps[self].results[0];
    result.type = (script_type){ .storage = PIN_I32 };
    caps[self].results = &result;
    text = authoring_source_migrate_generated(input, &catalog, "Demo", error, sizeof error);
    REQUIRE(!text && strstr(error, "Use self instead of Self()"));
    caps[self] = core->capabilities[self];
    caps[self].flow.break_entry = -1;
    text = authoring_source_migrate_generated(input, &catalog, "Demo", error, sizeof error);
    REQUIRE(!text && strstr(error, "Use self instead of Self()"));
    text = authoring_source_migrate_generated(
        "Demo :: behavior { on Tick :: () { self := 5; e := Self(); } }",
        core,
        "Demo",
        error,
        sizeof error
    );
    REQUIRE(!text && error[0]);
    return true;
}

TEST_CASE(source_migration_normalizes_generated_calls_and_library_owner)
{
    script_catalog_capability caps[sizeof component_capabilities / sizeof *component_capabilities + 1];
    memcpy(caps, component_capabilities, sizeof component_capabilities);
    script_catalog_parameter owner = { .name = "Value",
                                       .type = { .storage = PIN_HANDLE, .nominal = 40 } };
    caps[component_catalog.capability_count] = (script_catalog_capability){
        .id = script_catalog_id("capability", "Self"),
        .name = "Self",
        .kind = SCRIPT_CAPABILITY_FUNCTION,
        .results = &owner,
        .result_count = 1,
        .pure = true
    };
    script_catalog native = component_catalog;
    native.capabilities = caps;
    native.capability_count++;
    const char
        *input = "Demo :: library { F :: () { e := Self()\n"
                 "v := get_velocity().Value.x\n"
                 "present := has_velocity()\nset_velocity_x(v, self) } }";
    source = script_parse(input);
    state_before = script_analyze_document(source, &native, "Demo");
    REQUIRE(!state_before->valid);
    again = authoring_source_minimize(input, &native, "Demo");
    REQUIRE(again && !strcmp(again, input));
    char error[256];
    text = authoring_source_migrate_generated(input, &native, "Demo", error, sizeof error);
    if (!text) {
        fprintf(pu_errors(), "migration: %s\n", error);
    }
    REQUIRE(text);
    REQUIRE(strstr(text, "e := self") && strstr(text, "self.Velocity.x"));
    REQUIRE(strstr(text, "self.Velocity != nil"));
    REQUIRE(
        !strstr(text, "Self()") && !strstr(text, "get_velocity") &&
        !strstr(text, "set_velocity")
    );
    formatted = script_parse(text);
    state_after = script_analyze_document(formatted, &native, "Demo");
    REQUIRE(state_after->valid && state_after->owner_symbol >= 0);
    REQUIRE(state_after->functions[0].effects & SCRIPT_EFFECT_INVOCATION_READ);
    return true;
}

TEST_CASE(source_migration_commutes_stable_self_with_deferred_receiver)
{
    const script_catalog_field parent = { .id = 101,
                                          .name = "parent",
                                          .type = { .storage = PIN_HANDLE, .nominal = 40 },
                                          .writable = true };
    const script_catalog_type link = { .id = 100,
                                       .name = "Link",
                                       .type = { .storage = PIN_STRUCT, .nominal = 100 },
                                       .fields = &parent,
                                       .field_count = 1,
                                       .size = 8,
                                       .component = true,
                                       .component_id = 110 };
    const script_catalog_parameter parameters[] = {
        { .name = "Value", .type = { .storage = PIN_HANDLE, .nominal = 40 } },
        { .name = "Entity", .type = { .storage = PIN_HANDLE, .nominal = 40 } }
    };
    const script_catalog_parameter result = { .name = "Value",
                                              .type = { .storage = PIN_STRUCT,
                                                        .nominal = 100 } };
    script_catalog_capability capabilities[] = {
        component_capabilities[0],
        { .id = 102,
          .name = "set_link_parent",
          .kind = SCRIPT_CAPABILITY_ACTION,
          .parameters = parameters,
          .parameter_count = 2,
          .results = &result,
          .result_count = 1,
          .component = 100,
          .component_field = 101,
          .component_value = 0,
          .component_entity = 1,
          .component_access = SCRIPT_COMPONENT_ACCESS_WRITE,
          .outcomes = setter_completion,
          .outcome_count = 1,
          .completion_outcome = 0,
          .effects = SCRIPT_EFFECT_WRITE }
    };
    const script_catalog native = { .types = &link,
                                    .type_count = 1,
                                    .capabilities = capabilities,
                                    .capability_count = 2,
                                    .entity_type = 40,
                                    .tick_event = 30 };
    const char *input = "Demo :: behavior { target: entity = self; saved: "
                        "entity = self; "
                        "action attach = set_link_parent(self, target); "
                        "on Tick :: () { attach(); } after attach { saved = "
                        "attach.Value.parent; } }";
    char error[256];
    text = authoring_source_migrate_generated(input, &native, "Demo", error, sizeof error);
    if (!text) {
        fprintf(pu_errors(), "stable self migration: %s\n", error);
    }
    REQUIRE(text && !strstr(text, "set_link_parent"));
    REQUIRE(strstr(text, "action attach = import_component_write_"));
    REQUIRE(strstr(text, "attach.Value.parent"));
    formatted = script_parse(text);
    state_after = script_analyze_document(formatted, &native, "Demo");
    REQUIRE(state_after->valid);
    return true;
}

/* A generic call names its component through an argument, not a fixed
 * component the minimizer could spell as a path, so it keeps its call. */
TEST_CASE(source_minimize_keeps_generic_component_calls)
{
    const char *input = "Demo :: behavior {\n"
                        "    on Tick :: () {\n"
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
    script_symbol_kind retained = analysis->kind == SCRIPT_PROGRAM_SYSTEM
                                    ? SCRIPT_SHARED_STATE
                                    : SCRIPT_STATE;
    REQUIRE(root->kind == retained && callable->kind == retained);
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
    const char *shared_name = strstr(input, "other: retain");
    if (shared_name) {
        const script_symbol *shared = script_symbol_at(analysis, (int)(shared_name - input));
        REQUIRE(shared && shared->kind == SCRIPT_SHARED_STATE);
        REQUIRE(shared->scope == root->scope);
    }
    return true;
}

TEST_CASE(source_minimize_elides_only_redundant_root_state_tokens)
{
    const char *inputs[] = {
        "Demo :: behavior { /* root */ count: retain int = 1\nF :: () -> int { "
        "/* callable */ count: retain int = 2\nreturn count } }",
        "Demo :: library { /* root */ count: retain int = 1\nF :: () -> int { "
        "/* callable */ count: retain int = 2\nreturn count } }",
        "Demo :: system() { /* root */ count: retain int = 1\nother: "
        "retain "
        "int = "
        "2\nF :: () -> int { /* callable */ count: retain int = 2\nreturn "
        "count "
        "} }",
    };
    for (int i = 0; i < 3; i++) {
        cleanup();
        source = script_parse(inputs[i]);
        state_before = script_analyze_document(source, &catalog, "Demo");
        REQUIRE(!source->diagnostic_count && state_before->valid);
        REQUIRE(root_storage_identity(state_before, inputs[i]));
        text = authoring_source_minimize(inputs[i], &catalog, "Demo");
        REQUIRE(text);
        REQUIRE(i == 2 || !strstr(text, "/* root */ count: retain"));
        REQUIRE(i != 2 || strstr(text, "/* root */ count: retain"));
        REQUIRE(strstr(text, "/* root */ count"));
        REQUIRE(strstr(text, "/* callable */ count: retain"));
        REQUIRE(i != 2 || strstr(text, "other: retain"));
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
    const char *input = "Demo :: behavior{count:retain /* hé */ int=1;}";
    REQUIRE(format(input));
    REQUIRE(strstr(text, "count: retain /* hé */ int"));
    cleanup();
    input = "Demo :: behavior { count: retain /* keep */ int = missing }";
    REQUIRE(minimize(input, input));
    return true;
}

TEST_CASE(source_format_and_minimize_preserve_retained_lifetimes)
{
    REQUIRE(format(
        "Demo :: system(){total:retain number=0;F::()->number{"
        "count:retain number=1;count+=1;return count;}}"
    ));
    REQUIRE(strstr(text, "total: retain number = 0"));
    REQUIRE(strstr(text, "count: retain number = 1"));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        "Demo :: system() { total: retain number = 0\n"
        "F :: () -> number { count: retain number = 1\n"
        "integer: retain int = 1\nfractional: retain number = 1\nreturn count "
        "} "
        "}",
        "Demo :: system() { total: retain number = 0\n"
        "F :: () -> number { count: retain number = 1\n"
        "integer: retain = 1\nfractional: retain number = 1\nreturn count } "
        "}"
    ));
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        "Demo :: behavior { count: retain = 1\nAdvance :: (dt: number) { "
        "elapsed: retain number = 0\nelapsed += dt } }",
        "Demo :: behavior { count:= 1\nAdvance :: (dt: number) { "
        "elapsed: retain number = 0\nelapsed += dt } }"
    ));
    return true;
}

TEST_CASE(source_format_generic_types_maps_and_reference_iteration)
{
    REQUIRE(format(
        "Types::library{Vector::struct($ Element:Type,$ "
        "Size:int){items:Element[];}"
        "Identity::(value:$ T)->T=>{return value;}"
        "Product::(left:$ L,right:$ R)->ProductType(L,R){return left;}"
        "Use::(){scores:[string:int]=[\"Ada\":1,key:2];empty:[int:int]=[ : ];"
        "delete scores [key];gone:=key not  in scores;kept:=maybe??other??0;"
        "for & item in items{item+=1;}}}"
    ));
    REQUIRE(strstr(text, "Vector :: struct($Element: Type, $Size: int) {"));
    REQUIRE(strstr(text, "Identity :: (value: $T) -> T => {"));
    REQUIRE(strstr(text, "ProductType(L, R)"));
    REQUIRE(strstr(text, "scores: [string: int] = [\"Ada\": 1, key: 2]"));
    REQUIRE(strstr(text, "empty: [int: int] = [:]"));
    REQUIRE(strstr(text, "delete scores[key]"));
    REQUIRE(strstr(text, "gone := key not in scores"));
    REQUIRE(strstr(text, "kept := maybe ?? other ?? 0"));
    REQUIRE(strstr(text, "for &item in items {"));
    return true;
}

TEST_CASE(source_format_authored_prototypes_and_relationships)
{
    REQUIRE(format(
        "Boss::prototype(Enemy,Armored){identity:\"stable\","
        "links:{Targets:child.enemy},Health{current:100,maximum:100}}"
    ));
    REQUIRE(strstr(text, "Boss :: prototype(Enemy, Armored) {\n"));
    REQUIRE(strstr(text, "    identity: \"stable\",\n"));
    REQUIRE(strstr(text, "    Health {\n        current: 100,\n        maximum: 100\n    }"));
    cleanup();
    REQUIRE(format("Targets::relationship{exclusive:true,traversable:false,on_delete_target:Remove}"));
    REQUIRE(strstr(text, "Targets :: relationship {\n    exclusive: true,\n"));
    REQUIRE(strstr(text, "    on_delete_target: Remove\n}"));
    return true;
}

TEST_CASE(source_format_preserves_pointer_arguments)
{
    REQUIRE(format(
        "Refs::library{Bump::(v:^int){v^+=1;}Run::(){n:=1;"
        "Bump(& n);Bump(v:&n);Bump(& /*caller*/ n);}}"
    ));
    REQUIRE(strstr(text, "Bump(&n)"));
    REQUIRE(strstr(text, "Bump(v: &n)"));
    REQUIRE(strstr(text, "Bump(&/*caller*/ n)"));
    state_after = script_analyze_document(formatted, &catalog, "Refs");
    REQUIRE(state_after->valid);
    cleanup();
    REQUIRE(minimize_valid(
        &catalog,
        "Refs :: library { Bump :: (v: ^int) { v^ += 1; } "
        "Run :: () { n: int = 1; Bump(v: &n); } }",
        "Refs :: library { Bump :: (v: ^int) { v^ += 1 } "
        "Run :: () { n := 1; Bump(&n) } }"
    ));
    return true;
}

void test_language_root_state()
{
    test_parallel_begin();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_minimize_elides_only_redundant_root_state_tokens);
    RUN_TEST_CASE(source_format_preserves_state_tokens_and_minimizer_keeps_invalid_source);
    RUN_TEST_CASE(source_format_and_minimize_preserve_retained_lifetimes);
    pu_clear_setup();
    test_parallel_end();
}

void test_language_format()
{
    test_parallel_begin();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_format_spaces_managed_pointers_and_xor);
    RUN_TEST_CASE(source_format_keeps_named_payload_fields);
    RUN_TEST_CASE(source_format_flat_queries_and_iterators);
    RUN_TEST_CASE(source_format_spaces_typed_constant_colons);
    RUN_TEST_CASE(source_format_spaces_reverse_and_half_open_loops);
    RUN_TEST_CASE(source_format_keeps_attributes_on_their_line);
    RUN_TEST_CASE(source_format_preserves_fallible_named_results);
    RUN_TEST_CASE(source_format_spaces_pub_and_aligns_after_it);
    RUN_TEST_CASE(source_format_spaces_variant_patterns);
    RUN_TEST_CASE(source_format_aligns_bracket_continuations);
    RUN_TEST_CASE(source_format_aligns_operator_continuations);
    RUN_TEST_CASE(source_format_hugs_struct_braces);
    RUN_TEST_CASE(source_format_options_preserve_defaults);
    RUN_TEST_CASE(source_format_generic_types_maps_and_reference_iteration);
    RUN_TEST_CASE(source_format_preserves_pointer_arguments);
    RUN_TEST_CASE(source_format_authored_prototypes_and_relationships);
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
    RUN_TEST_CASE(source_format_keeps_else_and_arrow_spacing);
    RUN_TEST_CASE(source_minimize_drops_redundant_text);
    RUN_TEST_CASE(source_minimize_treats_character_literals_as_literals);
    RUN_TEST_CASE(source_minimize_reaches_a_fixed_point);
    RUN_TEST_CASE(source_minimize_preserves_wrapper_comments);
    RUN_TEST_CASE(source_minimize_preserves_declaration_and_argument_comments);
    RUN_TEST_CASE(source_minimize_preserves_cast_and_storage_comments);
    RUN_TEST_CASE(source_minimize_distinguishes_comments_from_strings);
    RUN_TEST_CASE(source_format_options_compose_with_comment_preserving_minimizer);
    RUN_TEST_CASE(source_format_and_minimize_remain_separate);
    RUN_TEST_CASE(source_minimize_keeps_named_payload_fields);
    RUN_TEST_CASE(source_minimize_drops_repeated_result_lists);
    RUN_TEST_CASE(source_minimize_keeps_argument_order);
    RUN_TEST_CASE(source_minimize_keeps_invalid_source);
    RUN_TEST_CASE(source_minimize_keeps_mixed_grouping_and_tight_ranges);
    RUN_TEST_CASE(source_minimize_preserves_accessor_and_operator_comments);
    RUN_TEST_CASE(source_minimize_spells_accessors_as_paths_and_operators);
    RUN_TEST_CASE(source_migration_matches_core_self_effects_in_each_program_context);
    RUN_TEST_CASE(source_migration_preserves_other_invocation_sensitive_calls);
    RUN_TEST_CASE(source_migration_keeps_system_row_owner_distinct);
    RUN_TEST_CASE(source_migration_rejects_opaque_or_mismatched_self_equivalence);
    RUN_TEST_CASE(source_migration_normalizes_generated_calls_and_library_owner);
    RUN_TEST_CASE(source_migration_preserves_deferred_setter_actions_and_results);
    RUN_TEST_CASE(source_canonical_is_idempotent_for_compact_property_helpers);
    RUN_TEST_CASE(source_migration_commutes_stable_self_with_deferred_receiver);
    RUN_TEST_CASE(source_minimize_keeps_generic_component_calls);
    RUN_TEST_CASE(source_minimize_elides_only_redundant_root_state_tokens);
    RUN_TEST_CASE(source_format_preserves_state_tokens_and_minimizer_keeps_invalid_source);
    RUN_TEST_CASE(source_format_and_minimize_preserve_retained_lifetimes);
    pu_setup(NULL, NULL);
    test_parallel_end();
}
