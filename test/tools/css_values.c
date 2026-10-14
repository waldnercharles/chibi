#include "authoring/css_values.h"

#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
TEST_CASE(css_scalar_units_roundtrip_without_resolving_relative_lengths)
{
    static const char *const units[] = { "px", "dp", "em", "rem", "%",  "vw",
                                         "vh", "in", "cm", "mm",  "pt", "pc" };
    char text[128], error[128], literal[32];
    for (size_t i = 0; i < sizeof units / sizeof *units; i++) {
        snprintf(literal, sizeof literal, "12.5%s", units[i]);
        css_value value = css_value_query("width", literal);
        REQUIRE(value.ok && value.known && value.simple && value.length && value.percent);
        REQUIRE(value.value == 12.5 && !strcmp(value.unit, units[i]));
        REQUIRE(
            css_value_format("width", literal, "number", 13.5, "", "", NULL, text, sizeof text, error, sizeof error)
        );
        value = css_value_query("width", text);
        REQUIRE(value.simple && value.value == 13.5 && !strcmp(value.unit, units[i]));
    }
    REQUIRE(!css_value_query("width", "12").simple);
    REQUIRE(css_value_query("width", "0").simple);
    REQUIRE(
        !css_value_format("width", "0", "number", 12, "", "", NULL, text, sizeof text, error, sizeof error)
    );
    REQUIRE(
        !css_value_format("width", "12%", "number", 13, "px", "", NULL, text, sizeof text, error, sizeof error)
    );
    REQUIRE(!css_value_query("width", "0x10px").simple);
    REQUIRE(!css_value_query("width", "nanpx").simple);
    REQUIRE(!css_value_query("width", "1e300px").simple);
    REQUIRE(css_value_query("line-height", "1.25").simple);
    REQUIRE(!css_value_query("opacity", "12px").simple);
    return true;
}
TEST_CASE(css_colors_share_linear_codec_and_preserve_srgb_hex_roundtrips)
{
    static const char *const inputs[] = { "#abc",      "#abcd",       "#8090a0",
                                          "#8090a080", "transparent", "white",
                                          "black" };
    char text[32], error[128];
    for (size_t i = 0; i < sizeof inputs / sizeof *inputs; i++) {
        css_value value = css_value_query("background-color", inputs[i]);
        REQUIRE(value.ok && value.simple && !strcmp(value.kind, "color"));
        REQUIRE(css_value_format(
            "background-color",
            inputs[i],
            "color",
            0,
            "",
            "",
            value.rgba,
            text,
            sizeof text,
            error,
            sizeof error
        ));
        css_value decoded = css_value_query("background-color", text);
        REQUIRE(decoded.simple);
        for (int channel = 0; channel < 4; channel++) {
            REQUIRE(fabsf(value.rgba[channel] - decoded.rgba[channel]) < 0.00001f);
        }
    }
    css_value mid = css_value_query("color", "#80808080");
    REQUIRE(
        mid.rgba[0] > .21f && mid.rgba[0] < .22f && mid.rgba[3] > .50f &&
        mid.rgba[3] < .51f
    );
    REQUIRE(!css_value_query("color", "#ggg").simple);
    REQUIRE(!css_value_query("width", "#ffffff").simple);
    return true;
}
TEST_CASE(css_keywords_defaults_and_complex_fallback_remain_property_specific)
{
    css_value value = css_value_query("align-content", "stretch");
    REQUIRE(value.ok && value.simple && !strcmp(value.initial, "stretch"));
    REQUIRE(strstr(value.keywords, "space-evenly") && strstr(value.keywords, "stretch"));
    REQUIRE(!strcmp(css_value_query("display", "block").initial, "inline"));
    REQUIRE(!strcmp(css_value_query("background-color", "#fff").initial, "transparent"));
    REQUIRE(css_value_query("color", "white").inherited);
    REQUIRE(css_value_query("margin", "10px 20px").shorthand);
    REQUIRE(!css_value_query("margin", "10px").simple);
    REQUIRE(!css_value_query("--custom", "10px").known);
    char text[128] = "unchanged", error[128];
    REQUIRE(
        css_value_format("display", "block", "keyword", 0, "", "flex", NULL, text, sizeof text, error, sizeof error)
    );
    REQUIRE(!strcmp(text, "flex"));
    REQUIRE(
        !css_value_format("position", "static", "keyword", 0, "", "flex", NULL, text, sizeof text, error, sizeof error)
    );
    REQUIRE(!strcmp(text, "flex"));
    static const char *const expressions[] = { "calc(100% - 20px)",
                                               "var(--width)",
                                               "{{ self.width }}",
                                               "10px 20px" };
    for (size_t i = 0; i < sizeof expressions / sizeof *expressions; i++) {
        REQUIRE(
            css_value_query("width", expressions[i]).ok &&
            !css_value_query("width", expressions[i]).simple
        );
        REQUIRE(
            !css_value_format("width", expressions[i], "number", 1, "", "", NULL, text, sizeof text, error, sizeof error)
        );
        REQUIRE(
            !css_value_format("width", expressions[i], "keyword", 0, "", "auto", NULL, text, sizeof text, error, sizeof error)
        );
    }
    REQUIRE(
        !css_value_format("width", "20px", "color", 0, "", "", value.rgba, text, sizeof text, error, sizeof error)
    );
    REQUIRE(
        !css_value_format("z-index", "2", "number", 2.5, "", "", NULL, text, sizeof text, error, sizeof error)
    );
    return true;
}
void test_tool_css_values()
{
    RUN_TEST_CASE(css_scalar_units_roundtrip_without_resolving_relative_lengths);
    RUN_TEST_CASE(css_colors_share_linear_codec_and_preserve_srgb_hex_roundtrips);
    RUN_TEST_CASE(css_keywords_defaults_and_complex_fallback_remain_property_specific);
}
