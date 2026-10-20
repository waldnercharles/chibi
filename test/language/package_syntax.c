#include "authoring/source_package_syntax.h"
#include "package_syntax_chibi.h"
#include "third_party/pico_headers/pico_unit.h"

#include <string.h>

/* One owning test_language selector. Explicit resolved-directory fixture maps
 * are data, not a resolver: exact quoted token plus declaring file selects an
 * already canonical directory. No disk, normalization, body resolution or code
 * execution. At most four member snapshots, one edited snapshot, twelve imports
 * and one successful graph coexist; all source strings are fixed below. */
enum
{
    MAX_FILES = 4,
    MAX_IMPORTS = 12
};
static script_syntax *syntax[MAX_FILES], *edited;
static source_package_syntax *source[MAX_FILES], *revision;
static source_package_file files[MAX_FILES];
static source_package_import imports[MAX_IMPORTS];
static int file_count, import_count;
static source_packages *graph;
static source_package_error graph_error;
static script_diagnostic syntax_error;
static bool differential;

typedef struct
{
    int file;
    const char *literal, *directory;
} resolved_path;

static void cleanup()
{
    source_packages_free(graph);
    graph = NULL;
    source_package_syntax_free(revision);
    revision = NULL;
    script_syntax_free(edited);
    edited = NULL;
    for (int i = 0; i < MAX_FILES; i++) {
        source_package_syntax_free(source[i]);
        script_syntax_free(syntax[i]);
        source[i] = NULL;
        syntax[i] = NULL;
    }
    file_count = import_count = 0;
}
static bool text_is(int file, script_range range, const char *text)
{
    return range.start >= 0 && range.end >= range.start &&
           (size_t)range.end <= strlen(syntax[file]->text) &&
           range.end - range.start == (int)strlen(text) &&
           !memcmp(syntax[file]->text + range.start, text, strlen(text));
}
static bool range_is(script_range range, int start, int end, int line, int column)
{
    return range.start == start && range.end == end && range.line == line &&
           range.column == column;
}
static bool add_file(const char *path, const char *directory, const char *text)
{
    REQUIRE(file_count < MAX_FILES);
    int at = file_count++;
    syntax[at] = script_parse(text);
    REQUIRE(syntax[at]->diagnostic_count == 0);
    source[at] = source_package_syntax_new(syntax[at], path, directory, at, &syntax_error);
    if (differential) {
        REQUIRE(
            package_syntax_chibi_compare(syntax[at], source[at], &syntax_error, path, directory, at)
        );
    }
    REQUIRE(source[at]);
    REQUIRE(!syntax_error.message[0]);
    files[at] = source[at]->file;
    return true;
}
static bool resolve(const resolved_path *map, int count)
{
    import_count = 0;
    for (int file = 0; file < file_count; file++) {
        for (int i = 0; i < source[file]->import_count; i++) {
            source_package_import item = source[file]->imports[i];
            REQUIRE(!item.directory);
            int found = 0;
            for (int j = 0; j < count; j++) {
                if (map[j].file == file &&
                    text_is(file, source[file]->locations[i].path, map[j].literal)) {
                    item.directory = map[j].directory;
                    found++;
                }
            }
            REQUIRE(found == 1);
            REQUIRE(import_count < MAX_IMPORTS);
            imports[import_count++] = item;
        }
    }
    return true;
}
static bool create(source_package_code code)
{
    graph = source_packages_new(files, file_count, imports, import_count, &graph_error);
    REQUIRE(graph_error.code == code);
    REQUIRE((graph != NULL) == (code == SOURCE_PACKAGE_OK));
    return true;
}

TEST_CASE(package_syntax_legacy_roots_keep_shape_and_names)
{
    const char *texts[] = {
        "package :: behavior { import := 1; }",
        "import :: library { package :: () -> int { return 1; } }",
        "package :: system() { import := 1; }",
        "import :: component { package: int; }",
    };
    const char *types[] = { "behavior", "library", "system", "component" };
    for (int i = 0; i < 4; i++) {
        syntax[0] = script_parse(texts[i]);
        REQUIRE(syntax[0]->diagnostic_count == 0);
        TSNode root = ts_tree_root_node(syntax[0]->tree);
        REQUIRE(ts_node_named_child_count(root) == 1);
        REQUIRE(!strcmp(ts_node_type(ts_node_named_child(root, 0)), types[i]));
        source[0] = source_package_syntax_new(syntax[0], "/p/a.chibi", "/p", 0, &syntax_error);
        if (differential) {
            REQUIRE(
                package_syntax_chibi_compare(syntax[0], source[0], &syntax_error, "/p/a.chibi", "/p", 0)
            );
        }
        REQUIRE(!source[0]);
        REQUIRE(!strcmp(syntax_error.message, "Expected package header"));
        cleanup();
    }
    return true;
}
TEST_CASE(package_syntax_rejects_malformed_or_mixed_headers)
{
    const char *texts[] = {
        "import lib \"x\"\npackage app\n",
        "Demo :: behavior {}\npackage app\n",
        "package app\npackage app\n",
        "package app\nHelpers :: library {}\n",
        "package app import \"x\"\n",
        "package \"app\"\n",
        "package app\nimport \"x\" as lib\n",
        "package app\nimport lib\n",
        "package app\nDemo :: behavior { import lib \"x\" }\n",
    };
    for (int i = 0; i < 9; i++) {
        syntax[0] = script_parse(texts[i]);
        REQUIRE(syntax[0]->diagnostic_count > 0);
        source[0] = source_package_syntax_new(syntax[0], "/p/a.chibi", "/p", 0, &syntax_error);
        if (differential) {
            REQUIRE(
                package_syntax_chibi_compare(syntax[0], source[0], &syntax_error, "/p/a.chibi", "/p", 0)
            );
        }
        REQUIRE(!source[0]);
        REQUIRE(syntax_error.range.start == syntax[0]->diagnostics[0].range.start);
        REQUIRE(syntax_error.range.end == syntax[0]->diagnostics[0].range.end);
        REQUIRE(!strcmp(syntax_error.message, syntax[0]->diagnostics[0].message));
        cleanup();
    }
    return true;
}
TEST_CASE(package_syntax_rejects_blank_package_name)
{
    syntax[0] = script_parse("/* π */\r\npackage _; // header\r\nimport _ \".\"\r\n");
    REQUIRE(syntax[0]->diagnostic_count == 0);
    source[0] = source_package_syntax_new(syntax[0], "/p/a.chibi", "/p", 0, &syntax_error);
    if (differential) {
        REQUIRE(
            package_syntax_chibi_compare(syntax[0], source[0], &syntax_error, "/p/a.chibi", "/p", 0)
        );
    }
    REQUIRE(!source[0]);
    REQUIRE(range_is(syntax_error.range, 18, 19, 2, 9));
    REQUIRE(text_is(0, syntax_error.range, "_"));
    REQUIRE(!strcmp(syntax_error.message, "A package needs a non-blank name"));
    return true;
}
TEST_CASE(package_syntax_siblings_alias_scope_and_blank_edges)
{
    REQUIRE(add_file("/p/app/z.chibi", "/p/app", "package app\nimport lib \"./nested\"\n"));
    REQUIRE(add_file(
        "/p/app/a.chibi",
        "/p/app",
        "package app\nimport lib \"project:math\"\nimport \"../math\"\n"
        "Answer :: () -> int { return lib.One(); }\n"
        "import _ \"./nested\"\nimport _ \"./nested\"\n"
    ));
    REQUIRE(add_file("/p/app/nested/n.chibi", "/p/app/nested", "package nested\n"));
    REQUIRE(add_file("/p/math/m.chibi", "/p/math", "package numbers\nOne :: () -> int { return 1; }\n"));
    const resolved_path map[] = {
        { 0, "\"./nested\"", "/p/app/nested" },
        { 1, "\"project:math\"", "/p/math" },
        { 1, "\"../math\"", "/p/math" },
        { 1, "\"./nested\"", "/p/app/nested" },
    };
    REQUIRE(resolve(map, 4));
    REQUIRE(create(SOURCE_PACKAGE_OK));
    int app = source_packages_file(graph, 0), nested = source_packages_file(graph, 2);
    int math = source_packages_file(graph, 3);
    REQUIRE(source_packages_count(graph) == 3);
    REQUIRE(source_packages_file(graph, 1) == app);
    REQUIRE(source_packages_at(graph, app)->files[0] == 1);
    REQUIRE(source_packages_at(graph, app)->files[1] == 0);
    REQUIRE(source_packages_lookup(graph, 0, "lib") == nested);
    REQUIRE(source_packages_lookup(graph, 1, "lib") == math);
    REQUIRE(source_packages_lookup(graph, 0, "math") == -1);
    REQUIRE(source_packages_lookup(graph, 1, "math") == math);
    REQUIRE(source_packages_lookup(graph, 1, "numbers") == -1);
    REQUIRE(source_packages_lookup(graph, 1, "_") == -1);
    REQUIRE(source_packages_dependency(graph, app, nested) == 0);
    REQUIRE(source_packages_dependency(graph, app, math) == 1);
    REQUIRE(import_count == 5);
    return true;
}
TEST_CASE(package_syntax_header_mismatch_retains_both_ranges)
{
    REQUIRE(add_file("/p/a.chibi", "/p", "// first\npackage app\n"));
    REQUIRE(add_file("/p/b.chibi", "/p", "package other\n"));
    REQUIRE(create(SOURCE_PACKAGE_HEADER_MISMATCH));
    REQUIRE(graph_error.file == 1 && graph_error.related_file == 0);
    REQUIRE(text_is(1, files[graph_error.file].header, "other"));
    REQUIRE(text_is(0, files[graph_error.related_file].header, "app"));
    REQUIRE(files[0].header.line == 2 && files[1].header.line == 1);
    return true;
}
TEST_CASE(package_syntax_duplicate_alias_retains_both_ranges)
{
    REQUIRE(
        add_file("/p/app/a.chibi", "/p/app", "package app\nimport lib \"../math\"\nimport lib \"project:math\"\n")
    );
    REQUIRE(add_file("/p/math/a.chibi", "/p/math", "package math\n"));
    const resolved_path map[] = {
        { 0, "\"../math\"", "/p/math" },
        { 0, "\"project:math\"", "/p/math" },
    };
    REQUIRE(resolve(map, 2));
    REQUIRE(create(SOURCE_PACKAGE_DUPLICATE_IMPORT));
    REQUIRE(graph_error.import == 1 && graph_error.related_import == 0);
    REQUIRE(text_is(0, imports[graph_error.import].range, "import lib \"project:math\""));
    REQUIRE(text_is(0, imports[graph_error.related_import].range, "import lib \"../math\""));
    return true;
}
TEST_CASE(package_syntax_self_imports_preserve_dependency_order)
{
    REQUIRE(
        add_file("/p/a/a.chibi", "/p/a", "package a\nimport this_package \".\"\nimport _ \"../b\"\n")
    );
    REQUIRE(add_file("/p/b/b.chibi", "/p/b", "package b\nimport _ \".\"\n"));
    const resolved_path map[] = {
        { 0, "\".\"", "/p/a" },
        { 0, "\"../b\"", "/p/b" },
        { 1, "\".\"", "/p/b" },
    };
    REQUIRE(resolve(map, 3));
    REQUIRE(create(SOURCE_PACKAGE_OK));
    int a = source_packages_file(graph, 0), b = source_packages_file(graph, 1);
    REQUIRE(source_packages_count(graph) == 2);
    REQUIRE(source_packages_lookup(graph, 0, "this_package") == a);
    REQUIRE(source_packages_lookup(graph, 1, "_") == -1);
    REQUIRE(source_packages_dependency(graph, a, a) == 0);
    REQUIRE(source_packages_dependency(graph, b, b) == 2);
    REQUIRE(source_packages_order(graph, 0) == b);
    REQUIRE(source_packages_order(graph, 1) == a);
    return true;
}
TEST_CASE(package_syntax_unused_cycle_omits_self_edges)
{
    REQUIRE(add_file("/p/a/a.chibi", "/p/a", "package a\nimport _ \".\"\nimport _ \"../b\"\n"));
    REQUIRE(add_file("/p/b/b.chibi", "/p/b", "package b\nimport _ \".\"\nimport _ \"../a\"\n"));
    const resolved_path map[] = {
        { 0, "\".\"", "/p/a" },
        { 0, "\"../b\"", "/p/b" },
        { 1, "\".\"", "/p/b" },
        { 1, "\"../a\"", "/p/a" },
    };
    REQUIRE(resolve(map, 4));
    REQUIRE(create(SOURCE_PACKAGE_CYCLE));
    REQUIRE(graph_error.cycle_count == 2);
    REQUIRE(graph_error.cycle[0] == 1 && graph_error.cycle[1] == 3);
    REQUIRE(text_is(0, imports[1].range, "import _ \"../b\""));
    REQUIRE(text_is(1, imports[3].range, "import _ \"../a\""));
    REQUIRE(imports[1].range.line == 3 && imports[3].range.line == 3);
    return true;
}
TEST_CASE(package_syntax_byte_ranges_exclude_terminators)
{
    REQUIRE(add_file(
        "/p/app/a.chibi",
        "/p/app",
        "/* π */\r\npackage app; // header\r\n"
        "  import chosen \"project:math\"; // one\r\n"
        "import \"../math\"\r\nimport _ \".\"\r\n"
    ));
    REQUIRE(add_file("/p/math/m.chibi", "/p/math", "package numbers\n"));
    const resolved_path map[] = {
        { 0, "\"project:math\"", "/p/math" },
        { 0, "\"../math\"", "/p/math" },
        { 0, "\".\"", "/p/app" },
    };
    REQUIRE(resolve(map, 3));
    REQUIRE(create(SOURCE_PACKAGE_OK));
    REQUIRE(range_is(files[0].header, 18, 21, 2, 9));
    REQUIRE(range_is(imports[0].range, 36, 64, 3, 3));
    REQUIRE(range_is(source[0]->locations[0].alias, 43, 49, 3, 10));
    REQUIRE(range_is(source[0]->locations[0].path, 50, 64, 3, 17));
    REQUIRE(range_is(imports[1].range, 74, 90, 4, 1));
    REQUIRE(range_is(source[0]->locations[1].path, 81, 90, 4, 8));
    REQUIRE(range_is(source[0]->locations[1].alias, 0, 0, 0, 0));
    REQUIRE(range_is(imports[2].range, 92, 104, 5, 1));
    REQUIRE(range_is(source[0]->locations[2].alias, 99, 100, 5, 8));
    REQUIRE(range_is(source[0]->locations[2].path, 101, 104, 5, 10));
    REQUIRE(text_is(0, imports[0].range, "import chosen \"project:math\""));
    REQUIRE(!imports[1].alias && !strcmp(imports[2].alias, "_"));
    return true;
}
TEST_CASE(package_syntax_missing_target_retains_import_range)
{
    REQUIRE(add_file("/p/app/a.chibi", "/p/app", "package app\nimport absent \"../missing\"\n"));
    const resolved_path map[] = { { 0, "\"../missing\"", "/p/missing" } };
    REQUIRE(resolve(map, 1));
    REQUIRE(create(SOURCE_PACKAGE_MISSING));
    REQUIRE(graph_error.file == 0 && graph_error.import == 0);
    REQUIRE(text_is(0, imports[0].range, "import absent \"../missing\""));
    return true;
}
TEST_CASE(package_syntax_incremental_header_keeps_retained_graph)
{
    REQUIRE(add_file("/p/a.chibi", "/p", "package app\n"));
    REQUIRE(add_file("/p/b.chibi", "/p", "package app\n"));
    REQUIRE(create(SOURCE_PACKAGE_OK));
    edited = script_edit(syntax[1], 8, 11, "other");
    REQUIRE(edited->diagnostic_count == 0);
    revision = source_package_syntax_new(edited, "/p/b.chibi", "/p", 1, &syntax_error);
    if (differential) {
        REQUIRE(
            package_syntax_chibi_compare(edited, revision, &syntax_error, "/p/b.chibi", "/p", 1)
        );
    }
    REQUIRE(revision);
    source_package_file changed[] = { files[0], revision->file };
    source_package_error rejected;
    source_packages *candidate = source_packages_new(changed, 2, NULL, 0, &rejected);
    /* Free even an unexpectedly accepted candidate before asserting refusal. */
    bool accepted = candidate != NULL;
    source_packages_free(candidate);
    REQUIRE(!accepted && rejected.code == SOURCE_PACKAGE_HEADER_MISMATCH);
    REQUIRE(range_is(revision->file.header, 8, 13, 1, 9));
    REQUIRE(!strcmp(source_packages_at(graph, 0)->name, "app"));
    REQUIRE(!strcmp(source[1]->file.name, "app"));
    REQUIRE(!strcmp(revision->file.name, "other"));
    if (differential) {
        REQUIRE(package_syntax_chibi_complete());
    }
    return true;
}
TEST_SUITE(package_syntax_suite)
{
    RUN_TEST_CASE(package_syntax_legacy_roots_keep_shape_and_names);
    RUN_TEST_CASE(package_syntax_rejects_malformed_or_mixed_headers);
    RUN_TEST_CASE(package_syntax_rejects_blank_package_name);
    RUN_TEST_CASE(package_syntax_siblings_alias_scope_and_blank_edges);
    RUN_TEST_CASE(package_syntax_header_mismatch_retains_both_ranges);
    RUN_TEST_CASE(package_syntax_duplicate_alias_retains_both_ranges);
    RUN_TEST_CASE(package_syntax_self_imports_preserve_dependency_order);
    RUN_TEST_CASE(package_syntax_unused_cycle_omits_self_edges);
    RUN_TEST_CASE(package_syntax_byte_ranges_exclude_terminators);
    RUN_TEST_CASE(package_syntax_missing_target_retains_import_range);
    RUN_TEST_CASE(package_syntax_incremental_header_keeps_retained_graph);
}
void test_language_package_syntax()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_SUITE(package_syntax_suite);
    pu_clear_setup();
}
void test_language_package_syntax_chibi()
{
    differential = true;
    pu_setup(NULL, cleanup);
    RUN_TEST_SUITE(package_syntax_suite);
    pu_clear_setup();
    package_syntax_chibi_finish();
    differential = false;
}
