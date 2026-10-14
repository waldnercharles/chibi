#include "support/rcss.h"

#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool expands(const char *text, const char *expected)
{
    char error[256] = "";
    char *out = rcss_expand(text, NULL, NULL, error, sizeof error);
    bool same = out && !strcmp(out, expected);
    if (!same) {
        fprintf(stderr, "rcss: %s\nexpected:\n%sgot:\n%s\n", error, expected, out ? out : "(null)");
    }
    free(out);
    return same;
}

static bool fails(const char *text, const char *message)
{
    char error[256] = "";
    char *out = rcss_expand(text, NULL, NULL, error, sizeof error);
    free(out);
    return !out && !strcmp(error, message);
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file);
    fputs(text, file);
    fclose(file);
}

/* Whether line `line` of the map came from `file` (NULL for the text
 * expanded itself) at line `source`. */
static bool traced(const rcss_map *map, int line, const char *file, int source)
{
    if (line < 1 || line > acount(map->lines)) {
        return false;
    }
    rcss_origin o = map->lines[line - 1];
    bool same_file = file ? o.file >= 0 && !strcmp(map->files[o.file], file)
                          : o.file == -1;
    return same_file && o.line == source;
}

TEST_CASE(rcss_imports_resolve_in_order_once)
{
    char root[] = "/tmp/chibi-rcss-XXXXXX";
    assert(mkdtemp(root));
    char parts[64], tokens[96], menu[96];
    snprintf(parts, sizeof parts, "%s/parts", root);
    snprintf(tokens, sizeof tokens, "%s/parts/tokens.rcss", root);
    snprintf(menu, sizeof menu, "%s/parts/menu.rcss", root);
    assert(mkdir(parts, 0755) == 0);
    write_text(tokens, "body { --fg: #fff; }\n");
    write_text(menu, "@import \"tokens.rcss\";\n.menu {\n  color: var(--fg);\n\n  .item { padding: 2dp; }\n}\n");
    rcss_map map = { 0 };
    char error[256] = "";
    char *out = rcss_expand(
        "@import \"parts/tokens.rcss\";\n@import 'parts/menu.rcss';\n.last { "
        "margin: 0; }\n",
        root,
        &map,
        error,
        sizeof error
    );
    REQUIRE(
        out && !strcmp(out, "body { --fg: #fff; }\n.menu { color: var(--fg); }\n.menu .item { padding: 2dp; }\n.last { margin: 0; }\n")
    );
    REQUIRE(
        acount(map.files) == 2 && !strcmp(map.files[0], tokens) &&
        !strcmp(map.files[1], menu)
    );
    /* A nested rule behind an import maps to its selector in that file. */
    REQUIRE(acount(map.lines) == 4 && traced(&map, 1, tokens, 1) && traced(&map, 2, menu, 2));
    REQUIRE(traced(&map, 3, menu, 5) && traced(&map, 4, NULL, 3));
    free(out);
    rcss_map_free(&map);
    out = rcss_expand("@import \"parts/missing.rcss\";", root, &map, error, sizeof error);
    REQUIRE(!out && strstr(error, "cannot read") && strstr(error, "missing.rcss"));
    REQUIRE(acount(map.files) == 1 && strstr(map.files[0], "missing.rcss") && !map.lines);
    rcss_map_free(&map);
    REQUIRE(expands("@import \"x.rcss\";\n.a { x: 1; }", "@import \"x.rcss\";\n.a { x: 1; }\n"));
    unlink(tokens);
    unlink(menu);
    rmdir(parts);
    rmdir(root);
    return true;
}

TEST_CASE(rcss_lists_custom_properties_as_written)
{
    const char
        *text = "/* palette */\n:root {\n  --radius: 6dp;\n  --primary: "
                "#3b6fc2; // blue\n}\n"
                ".dark,\n.x { --primary: color-mix(in srgb, var(--a) 50%, "
                "#000) }\n"
                ".a { color: red; content: \"--no: 1;\"; .b { --c : 1 } }\n";
    rcss_variable *v = NULL;
    rcss_variables(text, &v);
    static const char *const expected[][3] = {
        { ":root", "--radius", "6dp" },
        { ":root", "--primary", "#3b6fc2" },
        { ".dark, .x", "--primary", "color-mix(in srgb, var(--a) 50%, #000)" },
        { ".b", "--c", "1" },
    };
    REQUIRE(acount(v) == 4);
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            !strcmp(v[i].selector, expected[i][0]) &&
            !strcmp(v[i].name, expected[i][1])
        );
        REQUIRE(!strncmp(text + v[i].start, expected[i][2], strlen(expected[i][2])));
        REQUIRE(v[i].end - v[i].start == (int)strlen(expected[i][2]));
    }
    afree(v);
    return true;
}

/* Each rule is one line; a block that passes through keeps its lines. */
TEST_CASE(rcss_maps_lines_through_expansion_and_joins)
{
    rcss_map map = { 0 };
    char error[256] = "";
    char *out = rcss_expand(
        ".a,\n.b {\n  color:\n    red;\n  @media (x) {\n    .c { y: 1; }\n  "
        "}\n}\n"
        "@keyframes k {\n  from { x: 0; }\n}\n",
        NULL,
        &map,
        error,
        sizeof error
    );
    REQUIRE(
        out &&
        !strcmp(
            out,
            ".a, .b { color: red; }\n@media (x) {\n.a .c, .b .c { y: 1; }\n}\n"
            "@keyframes k {\n  from { x: 0; }\n}\n"
        )
    );
    REQUIRE(acount(map.lines) == 7 && traced(&map, 1, NULL, 1) && traced(&map, 2, NULL, 5));
    REQUIRE(traced(&map, 3, NULL, 6) && traced(&map, 4, NULL, 7));
    REQUIRE(traced(&map, 5, NULL, 9) && traced(&map, 6, NULL, 10) && traced(&map, 7, NULL, 11));
    free(out);
    rcss_map_own(&map, "own.rcss");
    REQUIRE(traced(&map, 1, "own.rcss", 1) && acount(map.files) == 1);

    /* Joined sheets keep their lines' origins; text without a map has none. */
    rcss_map part = { 0 };
    out = rcss_expand("\n.x { a: 1; }\n.y { b: 2; }", NULL, &part, error, sizeof error);
    rcss_map_own(&part, "part.rcss");
    char *sheet = NULL;
    rcss_map joined = { 0 };
    rcss_join(&sheet, &joined, "body { c: 3; }", NULL);
    rcss_join(&sheet, &joined, out, &part);
    rcss_join(&sheet, &joined, ".z { d: 4; }\n", &map);
    apush(sheet, '\0');
    REQUIRE(!strcmp(sheet, "body { c: 3; }\n.x { a: 1; }\n.y { b: 2; }\n.z { d: 4; }\n"));
    REQUIRE(
        acount(joined.lines) == 4 && joined.lines[0].line == 0 &&
        joined.lines[0].file == -1
    );
    REQUIRE(traced(&joined, 2, "part.rcss", 2) && traced(&joined, 3, "part.rcss", 3));
    REQUIRE(traced(&joined, 4, "own.rcss", 1) && acount(joined.files) == 2);
    free(out);
    afree(sheet);
    rcss_map_free(&part);
    rcss_map_free(&map);
    rcss_map_free(&joined);
    return true;
}

TEST_CASE(rcss_flattens_nesting_media_and_comments)
{
    REQUIRE(expands(".a { color: red; }", ".a { color: red; }\n"));
    REQUIRE(expands(
        ".menu { color: red; .item { margin: 0; &:hover { color: blue; } } "
        "&.open, .dark & { opacity: 1; } }",
        ".menu { color: red; }\n.menu .item { margin: 0; }\n.menu .item:hover "
        "{ color: blue; }\n"
        ".menu.open, .dark .menu { opacity: 1; }\n"
    ));
    REQUIRE(expands(".a, .b { .c, .d { x: 1; } }", ".a .c, .a .d, .b .c, .b .d { x: 1; }\n"));
    REQUIRE(
        expands(":root { --a: 1; } :root .x, :rooted { y: 2; }", "body { --a: 1; }\nbody .x, :rooted { y: 2; }\n")
    );
    REQUIRE(
        expands("[data-x=\"a,b\"] { :not(.y, .z) { x: 1 } }", "[data-x=\"a,b\"] :not(.y, .z) { x: 1; }\n")
    );
    REQUIRE(expands(".empty {}", ""));
    REQUIRE(
        expands("// line\n.a { /* block */ background: url(//x.png); // after\n content: \"//not\"; }", ".a { background: url(//x.png); content: \"//not\"; }\n")
    );
    REQUIRE(
        expands(".a { color: red; @media (theme: dark) { color: blue; .b { x: 1; } } }", ".a { color: red; }\n@media (theme: dark) {\n.a { color: blue; }\n.a .b { x: 1; }\n}\n")
    );
    REQUIRE(
        expands("@keyframes pulse { 0% { opacity: 0; } 100% { opacity: 1; } }\n.a { animation: 1s pulse; }", "@keyframes pulse { 0% { opacity: 0; } 100% { opacity: 1; } }\n.a { animation: 1s pulse; }\n")
    );
    return true;
}

TEST_CASE(rcss_errors_name_their_line)
{
    REQUIRE(fails(".a { color: red;", "line 1: missing }"));
    REQUIRE(fails(".a { }\n}", "line 2: unexpected }"));
    REQUIRE(fails("& { x: 1; }", "line 1: & needs an enclosing rule"));
    REQUIRE(fails("color: red;", "line 1: color: red needs a rule"));
    REQUIRE(fails(".a { @keyframes k { } }", "line 1: @keyframes k belongs at the top level"));
    REQUIRE(fails(".a { x: 1 } /* open", "line 1: a comment is missing its */"));
    return true;
}

void test_rcss()
{
    RUN_TEST_CASE(rcss_flattens_nesting_media_and_comments);
    RUN_TEST_CASE(rcss_errors_name_their_line);
    RUN_TEST_CASE(rcss_lists_custom_properties_as_written);
    RUN_TEST_CASE(rcss_maps_lines_through_expansion_and_joins);
    RUN_TEST_CASE(rcss_imports_resolve_in_order_once);
}
