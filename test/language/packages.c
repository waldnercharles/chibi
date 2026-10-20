#include "authoring/source_packages.h"
#include "package_graph_chibi.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

/* Resolved descriptor corpus. The ordinary route tests the C graph alone;
 * the differential route also executes the authored implementation through its
 * public value facade. Neither route reads runtime filesystem input.
 * The existing test_language dispatcher owns this fixture.
 * At most two C graph owners coexist; the largest has 256 files/packages and
 * 255 imports. All loops are bounded by those counts. */
static source_packages *packages, *retained;
static source_package_error error;
static bool differential;
static void cleanup()
{
    source_packages_free(packages);
    source_packages_free(retained);
    packages = retained = NULL;
}
static bool create(
    const source_package_file *files,
    int file_count,
    const source_package_import *imports,
    int import_count
)
{
    packages = source_packages_new(files, file_count, imports, import_count, &error);
    if (differential) {
        REQUIRE(
            package_graph_chibi_compare(files, file_count, imports, import_count, packages, &error)
        );
    }
    REQUIRE(packages);
    REQUIRE(error.code == SOURCE_PACKAGE_OK);
    REQUIRE(error.file == -1 && error.import == -1 && !error.cycle_count);
    return true;
}
static bool rejected(
    const source_package_file *files,
    int file_count,
    const source_package_import *imports,
    int import_count,
    source_package_code code
)
{
    packages = source_packages_new(files, file_count, imports, import_count, &error);
    if (differential) {
        REQUIRE(
            package_graph_chibi_compare(files, file_count, imports, import_count, packages, &error)
        );
    }
    REQUIRE(!packages);
    REQUIRE(error.code == code);
    return true;
}
static const source_package_file files[] = {
    { "/project/app/z.chibi", "/project/app", "app", { 8, 11, 1, 9, 0 } },
    { "/project/app/a.chibi", "/project/app", "app", { 8, 11, 1, 9, 0 } },
    { "/project/app/nested/n.chibi", "/project/app/nested", "nested", { 8, 14, 1, 9, 0 } },
    { "/project/math/m.chibi", "/project/math", "numbers", { 8, 15, 1, 9, 0 } },
};
TEST_CASE(package_empty_snapshot)
{
    REQUIRE(create(NULL, 0, NULL, 0));
    REQUIRE(source_packages_count(packages) == 0);
    cleanup();
    return true;
}
TEST_CASE(package_membership_and_filename_order)
{
    REQUIRE(create(files, 4, NULL, 0));
    REQUIRE(source_packages_count(packages) == 3);
    REQUIRE(source_packages_file(packages, 0) == source_packages_file(packages, 1));
    REQUIRE(source_packages_file(packages, 1) != source_packages_file(packages, 2));
    const source_package *app = source_packages_at(packages, source_packages_file(packages, 0));
    REQUIRE(!strcmp(app->directory, "/project/app"));
    REQUIRE(!strcmp(app->name, "app"));
    REQUIRE(app->file_count == 2 && app->files[0] == 1 && app->files[1] == 0);
    cleanup();
    return true;
}
TEST_CASE(package_rejects_conflicting_member_headers)
{
    source_package_file changed[] = { files[0], files[1] };
    changed[1].name = "other";
    REQUIRE(rejected(changed, 2, NULL, 0, SOURCE_PACKAGE_HEADER_MISMATCH));
    REQUIRE(error.file == 1 && error.related_file == 0);
    REQUIRE(changed[error.file].header.line == 1);
    REQUIRE(changed[error.related_file].header.start == 8);
    cleanup();
    return true;
}
TEST_CASE(package_rejects_distinct_directories_with_same_name)
{
    source_package_file changed[] = { files[0], files[3] };
    changed[1].name = "app";
    REQUIRE(strcmp(changed[0].directory, changed[1].directory));
    REQUIRE(rejected(changed, 2, NULL, 0, SOURCE_PACKAGE_DUPLICATE_NAME));
    REQUIRE(error.file == 1 && error.related_file == 0);
    cleanup();
    return true;
}
TEST_CASE(package_rejects_duplicate_file)
{
    source_package_file changed[] = { files[0], files[0] };
    REQUIRE(rejected(changed, 2, NULL, 0, SOURCE_PACKAGE_DUPLICATE_FILE));
    REQUIRE(error.file == 1 && error.related_file == 0);
    cleanup();
    return true;
}
TEST_CASE(package_resolved_identity_ignores_import_spelling)
{
    /* Both '../math' and 'project:math' resolve upstream to this directory.
     * This fixture proves identity after resolution, not path normalization. */
    source_package_import imports[] = {
        { 0, "/project/math", "first", { 0 } },
        { 0, "/project/math", "second", { 0 } },
        { 1, "/project/math", "other", { 0 } },
    };
    REQUIRE(create(files, 4, imports, 3));
    int app = source_packages_file(packages, 0),
        math = source_packages_file(packages, 3);
    REQUIRE(source_packages_count(packages) == 3);
    REQUIRE(source_packages_lookup(packages, 0, "first") == math);
    REQUIRE(source_packages_lookup(packages, 0, "second") == math);
    REQUIRE(source_packages_lookup(packages, 1, "other") == math);
    REQUIRE(source_packages_dependency(packages, app, math) == 0);
    cleanup();
    return true;
}
TEST_CASE(package_aliases_are_file_local)
{
    source_package_import imports[] = {
        { 0, "/project/math", "lib", { 0 } },
        { 1, "/project/app/nested", "lib", { 0 } },
    };
    REQUIRE(create(files, 4, imports, 2));
    REQUIRE(source_packages_lookup(packages, 0, "lib") == source_packages_file(packages, 3));
    REQUIRE(source_packages_lookup(packages, 1, "lib") == source_packages_file(packages, 2));
    REQUIRE(source_packages_lookup(packages, 2, "lib") == -1);
    REQUIRE(source_packages_lookup(packages, 3, "lib") == -1);
    cleanup();
    return true;
}
TEST_CASE(package_default_alias_uses_resolved_basename)
{
    source_package_import import = { 0, "/project/math", NULL, { 0 } };
    REQUIRE(create(files, 4, &import, 1));
    REQUIRE(source_packages_lookup(packages, 0, "math") == source_packages_file(packages, 3));
    REQUIRE(source_packages_lookup(packages, 0, "numbers") == -1);
    REQUIRE(source_packages_lookup(packages, 1, "math") == -1);
    cleanup();
    return true;
}
TEST_CASE(package_invalid_basename_requires_explicit_alias)
{
    source_package_file changed[] = {
        files[0],
        { "/project/math-v2/m.chibi", "/project/math-v2", "numbers", { 0 } }
    };
    source_package_import import = { 0, "/project/math-v2", NULL, { 0 } };
    REQUIRE(rejected(changed, 2, &import, 1, SOURCE_PACKAGE_IMPORT_NAME));
    REQUIRE(error.file == 0 && error.import == 0);
    import.alias = "math";
    REQUIRE(create(changed, 2, &import, 1));
    REQUIRE(source_packages_lookup(packages, 0, "math") == source_packages_file(packages, 1));
    cleanup();
    return true;
}
TEST_CASE(package_underscore_basename_is_not_implicit_blank)
{
    source_package_file changed[] = {
        files[0],
        { "/project/_/m.chibi", "/project/_", "numbers", { 0 } }
    };
    source_package_import import = { 0, "/project/_", NULL, { 0 } };
    REQUIRE(rejected(changed, 2, &import, 1, SOURCE_PACKAGE_IMPORT_NAME));
    import.alias = "_";
    REQUIRE(create(changed, 2, &import, 1));
    REQUIRE(source_packages_lookup(packages, 0, "_") == -1);
    REQUIRE(source_packages_dependency(packages, 0, 1) == 0);
    cleanup();
    return true;
}
TEST_CASE(package_blank_imports_keep_edges_without_bindings)
{
    source_package_import imports[] = {
        { 0, "/project/math", "_", { 0 } },
        { 0, "/project/math", "_", { 0 } },
        { 0, "/project/app/nested", "_", { 0 } },
    };
    REQUIRE(create(files, 4, imports, 3));
    REQUIRE(source_packages_lookup(packages, 0, "_") == -1);
    REQUIRE(source_packages_lookup(packages, 0, "math") == -1);
    REQUIRE(source_packages_dependency(packages, 0, 2) == 0);
    REQUIRE(source_packages_dependency(packages, 0, 1) == 2);
    REQUIRE(source_packages_order(packages, 2) == 0);
    cleanup();
    return true;
}
TEST_CASE(package_duplicate_alias_reports_both_imports)
{
    source_package_import imports[] = {
        { 0, "/project/math", "lib", { 12, 24, 2, 1, 0 } },
        { 0, "/project/app/nested", "lib", { 25, 38, 3, 1, 0 } },
    };
    REQUIRE(rejected(files, 4, imports, 2, SOURCE_PACKAGE_DUPLICATE_IMPORT));
    REQUIRE(error.file == 0 && error.related_file == 0);
    REQUIRE(error.import == 1 && error.related_import == 0);
    REQUIRE(imports[error.import].range.line == 3);
    REQUIRE(imports[error.related_import].range.line == 2);
    cleanup();
    return true;
}
TEST_CASE(package_rejects_missing_snapshot_member)
{
    source_package_import import = { 0, "/project/math", "math", { 0 } };
    REQUIRE(rejected(files, 2, &import, 1, SOURCE_PACKAGE_MISSING));
    REQUIRE(error.file == 0 && error.import == 0);
    cleanup();
    return true;
}
TEST_CASE(package_direct_self_import_preserves_single_identity)
{
    source_package_import imports[] = {
        { 0, "/project/app", "this_package", { 0 } },
        { 0, "/project/app", NULL, { 0 } },
        { 1, "/project/app", "_", { 0 } },
    };
    REQUIRE(create(files, 2, imports, 3));
    REQUIRE(source_packages_count(packages) == 1);
    REQUIRE(source_packages_lookup(packages, 0, "this_package") == 0);
    REQUIRE(source_packages_lookup(packages, 0, "app") == 0);
    REQUIRE(source_packages_lookup(packages, 1, "app") == -1);
    REQUIRE(source_packages_lookup(packages, 1, "_") == -1);
    REQUIRE(source_packages_dependency(packages, 0, 0) == 0);
    REQUIRE(source_packages_order(packages, 0) == 0);
    cleanup();
    return true;
}
static bool cycle_is_closed(const source_package_file *members, const source_package_import *imports)
{
    REQUIRE(error.cycle_count > 1 && error.cycle_count <= SOURCE_PACKAGE_MAX_FILES);
    for (int i = 0; i < error.cycle_count; i++) {
        const source_package_import *edge = &imports[error.cycle[i]];
        const source_package_import *next = &imports[error.cycle[(i + 1) % error.cycle_count]];
        REQUIRE(!strcmp(edge->directory, members[next->file].directory));
        REQUIRE(strcmp(members[edge->file].directory, edge->directory));
    }
    return true;
}
TEST_CASE(package_rejects_unused_two_package_cycle)
{
    source_package_import imports[] = { { 0, "/project/math", "_", { 0 } },
                                        { 3, "/project/app", "_", { 0 } } };
    REQUIRE(rejected(files, 4, imports, 2, SOURCE_PACKAGE_CYCLE));
    REQUIRE(error.cycle_count == 2);
    REQUIRE(cycle_is_closed(files, imports));
    cleanup();
    return true;
}
TEST_CASE(package_rejects_unused_indirect_cycle)
{
    source_package_import imports[] = {
        { 0, "/project/app/nested", "_", { 0 } },
        { 2, "/project/math", "_", { 0 } },
        { 3, "/project/app", "_", { 0 } },
    };
    REQUIRE(rejected(files, 4, imports, 3, SOURCE_PACKAGE_CYCLE));
    REQUIRE(error.cycle_count == 3);
    REQUIRE(cycle_is_closed(files, imports));
    cleanup();
    return true;
}
TEST_CASE(package_self_edges_do_not_hide_multi_package_cycles)
{
    /* Coverage of the approved larger-cycle rule, not an executed Odin bug
     * witness or an attempt to reproduce incidental queue behavior. */
    source_package_import imports[] = {
        { 0, "/project/app", "_", { 0 } },
        { 0, "/project/math", "_", { 0 } },
        { 3, "/project/math", "_", { 0 } },
        { 3, "/project/app", "_", { 0 } },
    };
    REQUIRE(rejected(files, 4, imports, 4, SOURCE_PACKAGE_CYCLE));
    REQUIRE(error.cycle_count == 2);
    REQUIRE(cycle_is_closed(files, imports));
    cleanup();
    return true;
}
TEST_CASE(package_cycle_witness_omits_acyclic_tail)
{
    source_package_import imports[] = {
        { 0, "/project/app/nested", "_", { 0 } },
        { 2, "/project/math", "_", { 0 } },
        { 3, "/project/app/nested", "_", { 0 } },
    };
    REQUIRE(rejected(files, 4, imports, 3, SOURCE_PACKAGE_CYCLE));
    REQUIRE(error.cycle_count == 2);
    REQUIRE(error.cycle[0] != 0 && error.cycle[1] != 0);
    REQUIRE(cycle_is_closed(files, imports));
    cleanup();
    return true;
}
TEST_CASE(package_order_is_dependency_first_with_self_edges)
{
    /* Approved dependency-correct order, rather than literal parity with the
     * pinned priority queue's incidental mixed-self ordering. */
    source_package_import imports[] = {
        { 0, "/project/app/nested", "_", { 0 } },
        { 0, "/project/math", "_", { 0 } },
        { 2, "/project/math", "_", { 0 } },
        { 3, "/project/math", "_", { 0 } },
    };
    REQUIRE(create(files, 4, imports, 4));
    REQUIRE(source_packages_order(packages, 0) == source_packages_file(packages, 3));
    REQUIRE(source_packages_order(packages, 1) == source_packages_file(packages, 2));
    REQUIRE(source_packages_order(packages, 2) == source_packages_file(packages, 0));
    REQUIRE(source_packages_dependency(packages, 0, 1) == 0);
    REQUIRE(source_packages_dependency(packages, 0, 2) == 1);
    REQUIRE(source_packages_dependency(packages, 1, 2) == 2);
    REQUIRE(source_packages_dependency(packages, 2, 2) == 3);
    cleanup();
    return true;
}
TEST_CASE(package_order_is_independent_of_discovery_order)
{
    source_package_file reversed[] = { files[3], files[2], files[1], files[0] };
    REQUIRE(create(files, 4, NULL, 0));
    retained = packages;
    packages = NULL;
    REQUIRE(create(reversed, 4, NULL, 0));
    for (int i = 0; i < 3; i++) {
        const source_package *a = source_packages_at(retained, source_packages_order(retained, i));
        const source_package *b = source_packages_at(packages, source_packages_order(packages, i));
        REQUIRE(!strcmp(a->directory, b->directory));
        REQUIRE(a->file_count == b->file_count);
        for (int j = 0; j < a->file_count; j++) {
            REQUIRE(!strcmp(files[a->files[j]].path, reversed[b->files[j]].path));
        }
    }
    cleanup();
    return true;
}
TEST_CASE(package_invalid_candidate_preserves_retained_graph)
{
    source_package_import imports[] = { { 0, "/project/math", "math", { 0 } },
                                        { 3, "/project/app", "_", { 0 } } };
    REQUIRE(create(files, 4, imports, 1));
    retained = packages;
    packages = NULL;
    REQUIRE(rejected(files, 4, imports, 2, SOURCE_PACKAGE_CYCLE));
    REQUIRE(source_packages_lookup(retained, 0, "math") == source_packages_file(retained, 3));
    REQUIRE(source_packages_dependency(retained, 2, 0) == -1);
    cleanup();
    return true;
}
TEST_CASE(package_snapshot_owners_do_not_share_bindings)
{
    source_package_import first = { 0, "/project/math", "one", { 0 } };
    source_package_import second = { 0, "/project/math", "two", { 0 } };
    REQUIRE(create(files, 4, &first, 1));
    retained = packages;
    packages = NULL;
    REQUIRE(create(files, 4, &second, 1));
    REQUIRE(source_packages_lookup(retained, 0, "one") >= 0);
    REQUIRE(source_packages_lookup(retained, 0, "two") == -1);
    REQUIRE(source_packages_lookup(packages, 0, "two") >= 0);
    REQUIRE(source_packages_lookup(packages, 0, "one") == -1);
    cleanup();
    return true;
}
TEST_CASE(package_existing_maximum_file_count)
{
    enum
    {
        COUNT = SOURCE_PACKAGE_MAX_FILES
    };
    char directories[COUNT][24], paths[COUNT][32], names[COUNT][8];
    source_package_file members[COUNT];
    source_package_import imports[COUNT - 1];
    for (int i = 0; i < COUNT; i++) {
        snprintf(directories[i], sizeof directories[i], "/project/p%03d", i);
        snprintf(paths[i], sizeof paths[i], "%s/a.chibi", directories[i]);
        snprintf(names[i], sizeof names[i], "p%03d", i);
        members[i] = (source_package_file){ paths[i], directories[i], names[i], { 0 } };
    }
    for (int i = 0; i < COUNT - 1; i++) {
        imports[i] = (source_package_import){ i, directories[i + 1], "_", { 0 } };
    }
    REQUIRE(create(members, COUNT, imports, COUNT - 1));
    REQUIRE(source_packages_count(packages) == COUNT);
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(source_packages_order(packages, i) == COUNT - i - 1);
    }
    /* Inputs are borrowed: release the graph before these stack arrays expire. */
    source_packages_free(packages);
    packages = NULL;
    cleanup();
    return true;
}
TEST_SUITE(package_graph_suite)
{
    RUN_TEST_CASE(package_empty_snapshot);
    RUN_TEST_CASE(package_membership_and_filename_order);
    RUN_TEST_CASE(package_rejects_conflicting_member_headers);
    RUN_TEST_CASE(package_rejects_distinct_directories_with_same_name);
    RUN_TEST_CASE(package_rejects_duplicate_file);
    RUN_TEST_CASE(package_resolved_identity_ignores_import_spelling);
    RUN_TEST_CASE(package_aliases_are_file_local);
    RUN_TEST_CASE(package_default_alias_uses_resolved_basename);
    RUN_TEST_CASE(package_invalid_basename_requires_explicit_alias);
    RUN_TEST_CASE(package_underscore_basename_is_not_implicit_blank);
    RUN_TEST_CASE(package_blank_imports_keep_edges_without_bindings);
    RUN_TEST_CASE(package_duplicate_alias_reports_both_imports);
    RUN_TEST_CASE(package_rejects_missing_snapshot_member);
    RUN_TEST_CASE(package_direct_self_import_preserves_single_identity);
    RUN_TEST_CASE(package_rejects_unused_two_package_cycle);
    RUN_TEST_CASE(package_rejects_unused_indirect_cycle);
    RUN_TEST_CASE(package_self_edges_do_not_hide_multi_package_cycles);
    RUN_TEST_CASE(package_cycle_witness_omits_acyclic_tail);
    RUN_TEST_CASE(package_order_is_dependency_first_with_self_edges);
    RUN_TEST_CASE(package_order_is_independent_of_discovery_order);
    RUN_TEST_CASE(package_invalid_candidate_preserves_retained_graph);
    RUN_TEST_CASE(package_snapshot_owners_do_not_share_bindings);
    RUN_TEST_CASE(package_existing_maximum_file_count);
}
void test_language_package_graph()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_SUITE(package_graph_suite);
    pu_clear_setup();
}
void test_language_package_graph_chibi()
{
    differential = true;
    pu_setup(NULL, cleanup);
    RUN_TEST_SUITE(package_graph_suite);
    pu_clear_setup();
    package_graph_chibi_finish();
    differential = false;
}
