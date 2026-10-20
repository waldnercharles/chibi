#include "authoring/source_package_snapshot.h"
#include "package_snapshot_chibi.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

/* Eight finite resolver cases. No filesystem, lowering, authored execution,
 * environment, process, clock or network work. All source inputs are fixed,
 * below1KiB; each snapshot has at most five files and four imports. */
enum
{
    MAX_SNAPSHOTS = 3,
    MAX_FILES = 5
};
static source_package_snapshot *snapshots[MAX_SNAPSHOTS];
static bool observe;
static const source_package_collection collections[] = { { "core", "/sdk/core" } };
static const source_package_input corpus[] = {
    { "/workspace/project/app/a.chibi", "package app\nimport first \"../math\"\nimport second \"project:math/.\"\n", 0 },
    { "/workspace/project/app/z.chibi", "package app\nimport _ \"core:scalar\"\n", 0 },
    { "/workspace/project/math/a.chibi", "package numbers\n", 0 },
    { "/workspace/project/other/a.chibi", "package other\n", 0 },
    { "/sdk/core/scalar/a.chibi", "package scalar\n", 0 },
};
static void cleanup()
{
    for (int i = 0; i < MAX_SNAPSHOTS; i++) {
        source_package_snapshot_free(snapshots[i]);
        snapshots[i] = NULL;
    }
}
static bool create(
    int slot,
    const source_package_input *files,
    int count,
    const char *project_root,
    const source_package_collection *roots,
    int root_count
)
{
    source_package_snapshot_free(snapshots[slot]);
    source_package_input inputs[MAX_FILES];
    for (int i = 0; i < count; i++) {
        inputs[i] = files[i];
        if (!inputs[i].size) {
            inputs[i].size = strlen(inputs[i].text);
        }
    }
    source_package_snapshot_options options = {
        .project_root = project_root,
        .files = inputs,
        .file_count = count,
        .collections = roots,
        .collection_count = root_count,
    };
    snapshots[slot] = source_package_snapshot_new(&options);
    return !observe || package_snapshot_chibi_compare(&options, snapshots[slot]);
}
static bool valid(int slot)
{
    REQUIRE(snapshots[slot]);
    REQUIRE(!source_package_snapshot_error_at(snapshots[slot]));
    REQUIRE(source_package_snapshot_graph(snapshots[slot]));
    return true;
}
static const source_package_snapshot_error *error(int slot)
{
    if (!snapshots[slot] || source_package_snapshot_graph(snapshots[slot])) {
        return NULL;
    }
    return source_package_snapshot_error_at(snapshots[slot]);
}
static int package(int slot, const char *identity)
{
    const source_packages *graph = source_package_snapshot_graph(snapshots[slot]);
    for (int i = 0; graph && i < source_packages_count(graph); i++) {
        const char *key = source_package_snapshot_identity(snapshots[slot], i);
        if (key && !strcmp(key, identity)) {
            return i;
        }
    }
    return -1;
}
static bool range_is(script_range range, int document, const char *text, const char *token)
{
    const char *start = strstr(text, token);
    REQUIRE(start);
    int line = 1, column = 1;
    for (const char *p = text; p < start; p++) {
        if (*p == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }
    return range.document == document && range.start == start - text &&
           range.end == start - text + (int)strlen(token) &&
           range.line == line && range.column == column;
}
static bool located(const source_package_snapshot_error *failure, int document, const char *text)
{
    REQUIRE(failure && failure->diagnostic.message[0]);
    script_range range = failure->diagnostic.range;
    REQUIRE(range.document == document);
    REQUIRE(range.start >= 0 && range.end >= range.start && (size_t)range.end <= strlen(text));
    return true;
}

TEST_CASE(package_resolution_relative_and_collection_paths_share_identity)
{
    REQUIRE(create(0, corpus, 5, "/workspace/./project", collections, 1));
    REQUIRE(valid(0));
    const source_packages *graph = source_package_snapshot_graph(snapshots[0]);
    REQUIRE(source_packages_count(graph) == 4);
    int app = package(0, "project:app"), math = package(0, "project:math");
    int scalar = package(0, "core:scalar");
    REQUIRE(app >= 0 && math >= 0 && scalar >= 0);
    REQUIRE(source_packages_file(graph, 0) == app && source_packages_file(graph, 1) == app);
    REQUIRE(source_packages_lookup(graph, 0, "first") == math);
    REQUIRE(source_packages_lookup(graph, 0, "second") == math);
    REQUIRE(source_packages_dependency(graph, app, math) >= 0);
    REQUIRE(source_packages_dependency(graph, app, scalar) >= 0);
    int count = 0;
    const script_package_document
        *documents = source_package_snapshot_documents(snapshots[0], &count);
    REQUIRE(documents && count == 5);
    REQUIRE(!strcmp(documents[0].key, "project:app/a.chibi"));
    REQUIRE(!strcmp(documents[4].key, "core:scalar/a.chibi"));
    REQUIRE(!strcmp(documents[0].path, corpus[0].path));
    const source_package_import *imports = source_package_snapshot_imports(snapshots[0], &count);
    REQUIRE(imports && count == 3);
    REQUIRE(!strcmp(imports[0].directory, imports[1].directory));
    REQUIRE(range_is(imports[1].range, 0, corpus[0].text, "import second \"project:math/.\""));
    return true;
}
TEST_CASE(package_resolution_aliases_are_file_local_and_blank_imports_keep_edges)
{
    const source_package_input inputs[] = {
        { "app/a.chibi", "package app\nimport lib \"../math\"\nimport _ \"../other\"\n", 0 },
        { "app/b.chibi", "package app\nimport lib \"../other\"\nimport _ \"../math\"\n", 0 },
        corpus[2],
        corpus[3],
    };
    REQUIRE(create(0, inputs, 4, "/workspace/project", NULL, 0));
    REQUIRE(valid(0));
    const source_packages *graph = source_package_snapshot_graph(snapshots[0]);
    int app = package(0, "project:app"), math = package(0, "project:math"),
        other = package(0, "project:other");
    REQUIRE(source_packages_lookup(graph, 0, "lib") == math);
    REQUIRE(source_packages_lookup(graph, 1, "lib") == other);
    REQUIRE(
        source_packages_lookup(graph, 0, "_") == -1 &&
        source_packages_lookup(graph, 1, "_") == -1
    );
    REQUIRE(source_packages_lookup(graph, 2, "lib") == -1);
    REQUIRE(
        source_packages_dependency(graph, app, math) >= 0 &&
        source_packages_dependency(graph, app, other) >= 0
    );
    return true;
}
TEST_CASE(package_resolution_path_failures_keep_the_quoted_token_range)
{
    static const char *texts[] = {
        "package app\nimport lib \"../../outside\"\n",
        "package app\nimport lib \"unknown:math\"\n",
        "package app\nimport lib \"../missing\"\n",
        "package app\nimport lib \"/sdk/core/scalar\"\n",
    };
    static const char *tokens[] = { "\"../../outside\"",
                                    "\"unknown:math\"",
                                    "\"../missing\"",
                                    "\"/sdk/core/scalar\"" };
    for (int i = 0; i < 4; i++) {
        const source_package_input inputs[] = { { "app/a.chibi", texts[i], 0 },
                                                corpus[2] };
        REQUIRE(create(0, inputs, 2, "/workspace/project", collections, 1));
        const source_package_snapshot_error *failure = error(0);
        REQUIRE(located(failure, 0, texts[i]));
        REQUIRE(range_is(failure->diagnostic.range, 0, texts[i], tokens[i]));
    }
    /* A nested collection owns this source even though the project root also
     * contains it. Relative paths cannot escape that longest matching root. */
    const source_package_collection nested[] = { { "nested", "/workspace/project/app" } };
    const char *text = "package app\nimport lib \"../math\"\n";
    const source_package_input inputs[] = { { "app/a.chibi", text, 0 }, corpus[2] };
    REQUIRE(create(0, inputs, 2, "/workspace/project", nested, 1));
    const source_package_snapshot_error *failure = error(0);
    REQUIRE(located(failure, 0, text));
    REQUIRE(range_is(failure->diagnostic.range, 0, text, "\"../math\""));
    return true;
}
TEST_CASE(package_resolution_rejects_malformed_strings_and_headers_with_local_ranges)
{
    static const char *texts[] = {
        "package app\nimport lib \"bad\\u0000path\"\n",
        "package app\nimport lib \"\"\n",
        "package app\nimport lib \"bad\\q\"\n",
        "package _\n",
    };
    for (int i = 0; i < 4; i++) {
        const source_package_input input = { "app/a.chibi", texts[i], 0 };
        REQUIRE(create(0, &input, 1, "/workspace/project", NULL, 0));
        const source_package_snapshot_error *failure = error(0);
        REQUIRE(located(failure, 0, texts[i]));
        if (i == 0) {
            REQUIRE(range_is(failure->diagnostic.range, 0, texts[i], "\"bad\\u0000path\""));
        }
        if (i == 1) {
            REQUIRE(range_is(failure->diagnostic.range, 0, texts[i], "\"\""));
        }
        if (i == 3) {
            REQUIRE(range_is(failure->diagnostic.range, 0, texts[i], "_"));
        }
    }
    const source_package_input mismatch[] = {
        { "app/a.chibi", "package app\n", 0 },
        { "app/b.chibi", "package other\n", 0 },
    };
    REQUIRE(create(0, mismatch, 2, "/workspace/project", NULL, 0));
    const source_package_snapshot_error *failure = error(0);
    REQUIRE(located(failure, 1, mismatch[1].text));
    REQUIRE(range_is(failure->diagnostic.range, 1, mismatch[1].text, "other"));
    REQUIRE(range_is(failure->related, 0, mismatch[0].text, "app"));
    static const char nul_text[] = "package app\n\0trailing bytes";
    const source_package_input nul = { "app/a.chibi", nul_text, sizeof nul_text - 1 };
    REQUIRE(create(0, &nul, 1, "/workspace/project", NULL, 0));
    REQUIRE(error(0) && error(0)->diagnostic.range.document == 0);
    return true;
}
TEST_CASE(package_resolution_rejects_duplicate_files_and_collection_roots)
{
    const source_package_input duplicate[] = {
        corpus[0],
        { "/workspace/project/app/./a.chibi", "package app\n", 0 },
        corpus[2]
    };
    REQUIRE(create(0, duplicate, 3, "/workspace/project", NULL, 0));
    REQUIRE(error(0));
    REQUIRE(error(0)->diagnostic.range.document == 1);
    const source_package_collection roots[][2] = {
        { { "core", "/sdk/core" }, { "core", "/sdk/other" } },
        { { "core", "/sdk/core" }, { "vendor", "/sdk/./core" } },
        { { "project", "/sdk/core" }, { "vendor", "/sdk/vendor" } },
    };
    for (int i = 0; i < 3; i++) {
        const source_package_input file = { "app/a.chibi", "package app\n", 0 };
        REQUIRE(create(0, &file, 1, "/workspace/project", roots[i], 2));
        REQUIRE(error(0));
        REQUIRE(error(0)->diagnostic.range.document == -1);
    }
    return true;
}
TEST_CASE(package_resolution_unused_cycles_reject_and_self_edges_remain_valid)
{
    const source_package_input pair[] = {
        { "a/a.chibi", "package alpha\nimport _ \"../b\"\n", 0 },
        { "b/b.chibi", "package beta\nimport _ \"../a\"\n", 0 },
    };
    REQUIRE(create(0, pair, 2, "/workspace/project", NULL, 0));
    const source_package_snapshot_error *failure = error(0);
    REQUIRE(
        failure && failure->graph.code == SOURCE_PACKAGE_CYCLE &&
        failure->graph.cycle_count == 2
    );
    REQUIRE(failure->diagnostic.range.document >= 0 && failure->diagnostic.range.document < 2);
    const source_package_input triple[] = {
        pair[0],
        { "b/b.chibi", "package beta\nimport _ \"../c\"\n", 0 },
        { "c/c.chibi", "package gamma\nimport _ \"../a\"\n", 0 },
    };
    REQUIRE(create(1, triple, 3, "/workspace/project", NULL, 0));
    failure = error(1);
    REQUIRE(
        failure && failure->graph.code == SOURCE_PACKAGE_CYCLE &&
        failure->graph.cycle_count == 3
    );
    const source_package_input self[] = {
        { "a/a.chibi", "package alpha\nimport _ \".\"\nimport _ \"../b\"\n", 0 },
        { "b/b.chibi", "package beta\n", 0 },
    };
    REQUIRE(create(2, self, 2, "/workspace/project", NULL, 0));
    REQUIRE(valid(2));
    const source_packages *graph = source_package_snapshot_graph(snapshots[2]);
    int a = package(2, "project:a"), b = package(2, "project:b");
    REQUIRE(source_packages_dependency(graph, a, a) >= 0);
    REQUIRE(source_packages_order(graph, 0) == b && source_packages_order(graph, 1) == a);
    return true;
}
TEST_CASE(package_resolution_input_permutation_and_relocation_preserve_logical_keys)
{
    REQUIRE(create(0, corpus, 5, "/workspace/project", collections, 1));
    source_package_input reverse[5];
    for (int i = 0; i < 5; i++) {
        reverse[i] = corpus[4 - i];
    }
    REQUIRE(create(1, reverse, 5, "/workspace/project", collections, 1));
    const source_package_input relocated[] = {
        { "/moved/project/app/a.chibi", corpus[0].text, 0 },
        { "/moved/project/app/z.chibi", corpus[1].text, 0 },
        { "/moved/project/math/a.chibi", corpus[2].text, 0 },
        { "/moved/project/other/a.chibi", corpus[3].text, 0 },
        { "/moved/core/scalar/a.chibi", corpus[4].text, 0 },
    };
    const source_package_collection moved[] = { { "core", "/moved/core" } };
    REQUIRE(create(2, relocated, 5, "/moved/project", moved, 1));
    for (int i = 0; i < 3; i++) {
        REQUIRE(valid(i));
    }
    const source_packages *first = source_package_snapshot_graph(snapshots[0]);
    const source_packages *second = source_package_snapshot_graph(snapshots[1]);
    REQUIRE(source_packages_count(first) == source_packages_count(second));
    for (int ordinal = 0; ordinal < source_packages_count(first); ordinal++) {
        const char *a = source_package_snapshot_identity(
            snapshots[0],
            source_packages_order(first, ordinal)
        );
        const char *b = source_package_snapshot_identity(
            snapshots[1],
            source_packages_order(second, ordinal)
        );
        REQUIRE(a && b && !strcmp(a, b));
    }
    int count;
    const script_package_document
        *original = source_package_snapshot_documents(snapshots[0], &count);
    const script_package_document
        *replacement = source_package_snapshot_documents(snapshots[2], &count);
    REQUIRE(count == 5);
    for (int i = 0; i < 5; i++) {
        REQUIRE(!strcmp(original[i].key, replacement[i].key));
    }
    REQUIRE(package(2, "project:app") >= 0 && package(2, "core:scalar") >= 0);
    return true;
}
TEST_CASE(package_resolution_owns_caller_roots_files_text_and_descriptors)
{
    char *project = strdup("/workspace/project"), *name = strdup("core"),
         *root = strdup("/sdk/core");
    char *paths[] = { strdup(corpus[1].path), strdup(corpus[4].path) };
    char *texts[] = { strdup(corpus[1].text), strdup(corpus[4].text) };
    source_package_input inputs[] = { { paths[0], texts[0], strlen(texts[0]) },
                                      { paths[1], texts[1], strlen(texts[1]) } };
    source_package_collection roots[] = { { name, root } };
    REQUIRE(create(0, inputs, 2, project, roots, 1));
    free(project);
    free(name);
    free(root);
    for (int i = 0; i < 2; i++) {
        free(paths[i]);
        free(texts[i]);
    }
    memset(inputs, 0, sizeof inputs);
    memset(roots, 0, sizeof roots);
    REQUIRE(valid(0));
    int count;
    const script_package_document
        *documents = source_package_snapshot_documents(snapshots[0], &count);
    REQUIRE(documents && count == 2);
    REQUIRE(!strcmp(documents[0].path, corpus[1].path));
    REQUIRE(!strcmp(documents[0].key, "project:app/z.chibi"));
    REQUIRE(!strcmp(documents[0].syntax->text, corpus[1].text));
    const source_package_import *imports = source_package_snapshot_imports(snapshots[0], &count);
    REQUIRE(imports && count == 1 && !strcmp(imports[0].directory, "/sdk/core/scalar"));
    const source_packages *graph = source_package_snapshot_graph(snapshots[0]);
    REQUIRE(
        source_packages_dependency(graph, package(0, "project:app"), package(0, "core:scalar")) >= 0
    );
    return true;
}
void test_language_package_resolution()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_resolution_relative_and_collection_paths_share_identity);
    RUN_TEST_CASE(package_resolution_aliases_are_file_local_and_blank_imports_keep_edges);
    RUN_TEST_CASE(package_resolution_path_failures_keep_the_quoted_token_range);
    RUN_TEST_CASE(package_resolution_rejects_malformed_strings_and_headers_with_local_ranges);
    RUN_TEST_CASE(package_resolution_rejects_duplicate_files_and_collection_roots);
    RUN_TEST_CASE(package_resolution_unused_cycles_reject_and_self_edges_remain_valid);
    RUN_TEST_CASE(package_resolution_input_permutation_and_relocation_preserve_logical_keys);
    RUN_TEST_CASE(package_resolution_owns_caller_roots_files_text_and_descriptors);
    pu_clear_setup();
}

void test_language_package_resolution_observed()
{
    observe = true;
    test_language_package_resolution();
    observe = false;
}
