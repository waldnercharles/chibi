#include "authoring/source_format.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "tools/docs/formatter/api.h"

#include <stdlib.h>
#include <string.h>

static const char *const examples[] = {
    "package sample\nF::()->int{return 1;}\n",
    "package sample\nF::(a:int,b:int,c:int)->int{return a+b+c;}\n",
    "package sample\nNames::()->string[]{return "
    "[\"Ada\",\"Grace\",\"Katherine\"];}\n",
    "package sample\n/* café 🧭\r\nkept */\nF::()->string{return \"café; "
    "<&>\";}\n",
    "package sample\nF::()->int{a:=1; /* keep */ return a;}\n"
};

TEST_CASE(formatter_web_matches_native_options)
{
    const int indents[] = { 1, 4, 16 }, widths[] = { 0, 1, 24, 4096 };
    for (size_t i = 0; i < sizeof examples / sizeof *examples; i++) {
        script_syntax *syntax = script_parse(examples[i]);
        REQUIRE(syntax && syntax->diagnostic_count == 0);
        for (size_t indent = 0; indent < sizeof indents / sizeof *indents; indent++) {
            for (size_t width = 0; width < sizeof widths / sizeof *widths; width++) {
                for (int tabs = 0; tabs < 2; tabs++) {
                    for (int semicolons = 0; semicolons < 2; semicolons++) {
                        authoring_format_options options = {
                            .indent_width = indents[indent],
                            .preferred_width = widths[width],
                            .use_tabs = tabs != 0,
                            .remove_optional_semicolons = semicolons != 0
                        };
                        char error[512];
                        char *native = authoring_source_format_with_options(
                            syntax,
                            &options,
                            NULL,
                            error,
                            sizeof error
                        );
                        char *web = chibi_format_web(
                            examples[i],
                            (int)strlen(examples[i]),
                            indents[indent],
                            widths[width],
                            tabs,
                            semicolons
                        );
                        REQUIRE(native && web);
                        REQUIRE(!strcmp(native, web));
                        char *again = chibi_format_web(
                            web,
                            (int)strlen(web),
                            indents[indent],
                            widths[width],
                            tabs,
                            semicolons
                        );
                        REQUIRE(again && !strcmp(web, again));
                        REQUIRE(!strcmp(syntax->text, examples[i]));
                        free(again);
                        free(web);
                        free(native);
                    }
                }
            }
        }
        script_syntax_free(syntax);
    }
    return true;
}

TEST_CASE(formatter_web_preserves_literals_and_source)
{
    char input[] = "package sample\nF::()->string{return \"café; <&>\";}\n";
    char before[sizeof input];
    memcpy(before, input, sizeof input);
    char *text = chibi_format_web(input, (int)strlen(input), 2, 0, 0, 1);
    REQUIRE(text);
    REQUIRE(strstr(text, "\"café; <&>\""));
    REQUIRE(strstr(text, "\n  return "));
    REQUIRE(!strstr(text, "\";"));
    REQUIRE(!memcmp(input, before, sizeof input));
    free(text);
    return true;
}

TEST_CASE(formatter_web_rejects_invalid_boundaries)
{
    const char embedded[] = { 'A', 0, 'B', 0 };
    const char *valid_source = examples[0];
    int bytes = (int)strlen(valid_source);
    REQUIRE(!chibi_format_web(NULL, 0, 4, 0, 0, 0));
    REQUIRE(!chibi_format_web("", -1, 4, 0, 0, 0));
    REQUIRE(!chibi_format_web("", CHIBI_FORMAT_WEB_MAX_BYTES + 1, 4, 0, 0, 0));
    REQUIRE(!chibi_format_web(embedded, 3, 4, 0, 0, 0));
    REQUIRE(!chibi_format_web("abcd", 2, 4, 0, 0, 0));
    REQUIRE(!chibi_format_web(valid_source, bytes, 0, 0, 0, 0));
    REQUIRE(!chibi_format_web(valid_source, bytes, 17, 0, 0, 0));
    REQUIRE(!chibi_format_web(valid_source, bytes, 4, -1, 0, 0));
    REQUIRE(!chibi_format_web(valid_source, bytes, 4, 4097, 0, 0));
    REQUIRE(!chibi_format_web(valid_source, bytes, 4, 0, 2, 0));
    REQUIRE(!chibi_format_web(valid_source, bytes, 4, 0, 0, -1));
    REQUIRE(!chibi_format_web("F :: (", 6, 4, 0, 0, 0));
    REQUIRE(*chibi_format_web_error());
    REQUIRE(!chibi_format_web("", 0, 4, 0, 0, 0));
    char *valid = chibi_format_web(examples[0], (int)strlen(examples[0]), 4, 0, 0, 0);
    REQUIRE(valid && !*chibi_format_web_error());
    free(valid);
    return true;
}

TEST_CASE(formatter_web_accepts_exact_byte_budget)
{
    char *input = malloc(CHIBI_FORMAT_WEB_MAX_BYTES + 1);
    REQUIRE(input);
    memset(input, ' ', CHIBI_FORMAT_WEB_MAX_BYTES);
    memcpy(input, examples[0], strlen(examples[0]));
    input[CHIBI_FORMAT_WEB_MAX_BYTES] = 0;
    char *text = chibi_format_web(input, CHIBI_FORMAT_WEB_MAX_BYTES, 4, 0, 0, 0);
    REQUIRE(text);
    REQUIRE(!memcmp(input, examples[0], strlen(examples[0])));
    REQUIRE(input[CHIBI_FORMAT_WEB_MAX_BYTES - 1] == ' ');
    free(text);
    free(input);
    REQUIRE(chibi_format_web_api() == 1);
    REQUIRE(strlen(chibi_format_web_revision()) == 40);
    REQUIRE(!strcmp(chibi_format_web_revision(), CHIBI_FORMAT_REVISION));
    return true;
}

void test_tool_formatter_web()
{
    RUN_TEST_CASE(formatter_web_matches_native_options);
    RUN_TEST_CASE(formatter_web_preserves_literals_and_source);
    RUN_TEST_CASE(formatter_web_rejects_invalid_boundaries);
    RUN_TEST_CASE(formatter_web_accepts_exact_byte_budget);
}
