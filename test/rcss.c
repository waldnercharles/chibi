#include "support/rcss.h"

#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void expands(const char *text, const char *expected)
{
    char error[256] = "";
    char *out = rcss_expand(text, NULL, NULL, error, sizeof error);
    if (!out || strcmp(out, expected)) {
        fprintf(stderr, "rcss: %s\nexpected:\n%sgot:\n%s\n", error, expected, out ? out : "(null)");
    }
    assert(out && !strcmp(out, expected));
    free(out);
}

static void fails(const char *text, const char *message)
{
    char error[256] = "";
    char *out = rcss_expand(text, NULL, NULL, error, sizeof error);
    assert(!out && !strcmp(error, message));
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static void imports_resolve_in_order_once()
{
    char root[] = "/tmp/chibi-rcss-XXXXXX";
    assert(mkdtemp(root));
    char parts[64], tokens[96], menu[96];
    snprintf(parts, sizeof parts, "%s/parts", root);
    snprintf(tokens, sizeof tokens, "%s/parts/tokens.rcss", root);
    snprintf(menu, sizeof menu, "%s/parts/menu.rcss", root);
    assert(mkdir(parts, 0755) == 0);
    write_text(tokens, "body { --fg: #fff; }\n");
    write_text(menu, "@import \"tokens.rcss\";\n.menu { color: var(--fg); .item { padding: 2dp; } }\n");
    char **imports = NULL;
    char error[256] = "";
    char *out = rcss_expand(
        "@import \"parts/tokens.rcss\";\n@import 'parts/menu.rcss';\n.last { "
        "margin: 0; }\n",
        root,
        &imports,
        error,
        sizeof error
    );
    assert(
        out && !strcmp(out, "body { --fg: #fff; }\n.menu { color: var(--fg); }\n.menu .item { padding: 2dp; }\n.last { margin: 0; }\n")
    );
    assert(acount(imports) == 2 && !strcmp(imports[0], tokens) && !strcmp(imports[1], menu));
    free(out);
    out = rcss_expand("@import \"parts/missing.rcss\";", root, &imports, error, sizeof error);
    assert(!out && strstr(error, "cannot read") && strstr(error, "missing.rcss"));
    for (int i = 0; i < acount(imports); i++) {
        free(imports[i]);
    }
    afree(imports);
    expands("@import \"x.rcss\";\n.a { x: 1; }", "@import \"x.rcss\";\n.a { x: 1; }\n");
    unlink(tokens);
    unlink(menu);
    rmdir(parts);
    rmdir(root);
}

static void lists_variables()
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
    assert(acount(v) == 4);
    for (int i = 0; i < 4; i++) {
        assert(!strcmp(v[i].selector, expected[i][0]) && !strcmp(v[i].name, expected[i][1]));
        assert(!strncmp(text + v[i].start, expected[i][2], strlen(expected[i][2])));
        assert(v[i].end - v[i].start == (int)strlen(expected[i][2]));
    }
    afree(v);
}

void test_rcss()
{
    lists_variables();
    expands(".a { color: red; }", ".a { color: red; }\n");
    expands(
        ".menu { color: red; .item { margin: 0; &:hover { color: blue; } } "
        "&.open, .dark & { opacity: 1; } }",
        ".menu { color: red; }\n.menu .item { margin: 0; }\n.menu .item:hover "
        "{ color: blue; }\n"
        ".menu.open, .dark .menu { opacity: 1; }\n"
    );
    expands(".a, .b { .c, .d { x: 1; } }", ".a .c, .a .d, .b .c, .b .d { x: 1; }\n");
    expands(":root { --a: 1; } :root .x, :rooted { y: 2; }", "body { --a: 1; }\nbody .x, :rooted { y: 2; }\n");
    expands("[data-x=\"a,b\"] { :not(.y, .z) { x: 1 } }", "[data-x=\"a,b\"] :not(.y, .z) { x: 1; }\n");
    expands("// line\n.a { /* block */ background: url(//x.png); // after\n content: \"//not\"; }", ".a { background: url(//x.png); content: \"//not\"; }\n");
    expands(".a { color: red; @media (theme: dark) { color: blue; .b { x: 1; } } }", ".a { color: red; }\n@media (theme: dark) {\n.a { color: blue; }\n.a .b { x: 1; }\n}\n");
    expands("@keyframes pulse { 0% { opacity: 0; } 100% { opacity: 1; } }\n.a { animation: 1s pulse; }", "@keyframes pulse { 0% { opacity: 0; } 100% { opacity: 1; } }\n.a { animation: 1s pulse; }\n");
    fails(".a { color: red;", "line 1: missing }");
    fails(".a { }\n}", "line 2: unexpected }");
    fails("& { x: 1; }", "line 1: & needs an enclosing rule");
    fails("color: red;", "line 1: color: red needs a rule");
    fails(".a { @keyframes k { } }", "line 1: @keyframes k belongs at the top level");
    fails(".a { x: 1 } /* open", "line 1: a comment is missing its */");
    imports_resolve_in_order_once();
    puts("rcss PASS: imports in order and once, nesting with & and selector lists, nested @media, comments, errors with lines and custom properties as written");
}
