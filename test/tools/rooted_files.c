#include "support/rooted_files.h"

#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char root[128];
static rooted_files *files;
static void cleanup(void)
{
    if (files) {
        rooted_files_close(files);
        files = NULL;
    }
    if (*root) {
        tool_remove_tree(root);
        *root = 0;
    }
}
static bool setup(void)
{
    strcpy(root, "/tmp/chibi-rooted-XXXXXX");
    REQUIRE(mkdtemp(root));
    files = rooted_files_open(root);
    REQUIRE(files);
    return true;
}
TEST_CASE(rooted_files_unicode_binary_cas_and_exclusive_publication)
{
    REQUIRE(setup());
    rooted_files *directory = rooted_files_directory(files, "tést/nested", true);
    REQUIRE(directory);
    rooted_files_close(directory);
    const unsigned char original[] = { 0, 255, 3, 0 };
    REQUIRE(
        rooted_files_write(files, "tést/nested/file.bin", original, sizeof original, 16, false, NULL, 0)
    );
    REQUIRE(!rooted_files_write(files, "tést/nested/file.bin", "bad", 3, 16, false, NULL, 0));
    REQUIRE(errno == EEXIST);
    REQUIRE(!rooted_files_write(files, "tést/nested/file.bin", "bad", 3, 16, true, "wrong", 5));
    REQUIRE(errno == EBUSY);
    unsigned char *bytes = NULL;
    size_t count = 0;
    REQUIRE(rooted_files_read(files, "tést/nested/file.bin", 16, &bytes, &count));
    REQUIRE(count == sizeof original && !memcmp(bytes, original, count));
    free(bytes);
    REQUIRE(
        rooted_files_write(files, "tést/nested/file.bin", "new", 3, 16, true, original, sizeof original)
    );
    REQUIRE(!rooted_files_read(files, "tést/nested/file.bin", 2, &bytes, &count));
    REQUIRE(errno == EFBIG);
    REQUIRE(rooted_files_move(files, "tést/nested/file.bin", "tést/nested/moved.bin", false));
    REQUIRE(rooted_files_remove(files, "tést/nested/moved.bin"));
    return true;
}
TEST_CASE(rooted_files_repeated_sorted_enumeration_and_limits)
{
    REQUIRE(setup());
    REQUIRE(rooted_files_write(files, "z", "z", 1, 8, false, NULL, 0));
    REQUIRE(rooted_files_write(files, "a", "a", 1, 8, false, NULL, 0));
    for (int repeat = 0; repeat < 2; repeat++) {
        rooted_file_entry *entries = NULL;
        int count = 0;
        REQUIRE(rooted_files_list(files, "", 4, &entries, &count));
        REQUIRE(
            count == 2 && !strcmp(entries[0].name, "a") && !strcmp(entries[1].name, "z")
        );
        REQUIRE(entries[0].info.regular && entries[1].info.bytes == 1);
        rooted_file_entries_free(entries, count);
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    REQUIRE(!rooted_files_list(files, "", 1, &entries, &count));
    REQUIRE(entries == NULL && count == 0);
    return true;
}
TEST_CASE(rooted_files_refuses_traversal_links_and_directory_mutations)
{
    REQUIRE(setup());
    const char *invalid[] = { "../escape", "/absolute", "a//b", "a/./b",
                              "a/../b",    "a\\b",      "a/",   "\xff" };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        REQUIRE(!rooted_files_path_valid(invalid[i]));
        REQUIRE(!rooted_files_write(files, invalid[i], "x", 1, 8, false, NULL, 0));
    }
    rooted_files *directory = rooted_files_directory(files, "directory", true);
    REQUIRE(directory);
    rooted_files_close(directory);
    REQUIRE(!rooted_files_remove(files, "directory"));
    REQUIRE(!rooted_files_move(files, "directory", "moved", false));
    REQUIRE(rooted_files_write(files, "directory/owned", "x", 1, 8, false, NULL, 0));
    REQUIRE(!rooted_files_remove_directory(files, "directory"));
    REQUIRE(!rooted_files_remove_directory(files, "directory/owned"));
    char link[512];
    snprintf(link, sizeof link, "%s/link", root);
    REQUIRE(!symlink("directory", link));
    rooted_file_info info;
    REQUIRE(!rooted_files_stat(files, "link", &info));
    REQUIRE(!rooted_files_write(files, "link/escape", "x", 1, 8, false, NULL, 0));
    REQUIRE(!rooted_files_remove(files, "link"));
    REQUIRE(!rooted_files_remove_directory(files, "link"));
    REQUIRE(rooted_files_remove(files, "directory/owned"));
    REQUIRE(rooted_files_remove_directory(files, "directory"));
    REQUIRE(!rooted_files_stat(files, "directory", &info));
    return true;
}
TEST_CASE(rooted_files_identity_matches_actual_volume_case_rules_without_writes)
{
    REQUIRE(setup());
    REQUIRE(rooted_files_write(files, "MiXeD-É.chibi", "value", 5, 8, false, NULL, 0));
    rooted_file_info original, alias;
    REQUIRE(rooted_files_stat(files, "MiXeD-É.chibi", &original));
    bool insensitive = rooted_files_stat(files, "mIxEd-é.chibi", &alias);
    REQUIRE(rooted_files_path_equal(files, "MiXeD-É.chibi", "MiXeD-É.chibi"));
    REQUIRE(rooted_files_path_equal(files, "MiXeD-É.chibi", "mIxEd-é.chibi") == insensitive);
    REQUIRE(rooted_files_path_equal(files, "absent-É.chibi", "ABSENT-é.chibi") == insensitive);
    REQUIRE(!rooted_files_path_equal(files, "absent.chibi", "different.chibi"));
    char *canonical = rooted_files_canonical_path(files, "MiXeD-É.chibi");
    REQUIRE(canonical && !strcmp(canonical, "MiXeD-É.chibi"));
    free(canonical);
    if (insensitive) {
        canonical = rooted_files_canonical_path(files, "mIxEd-é.chibi");
        REQUIRE(canonical && !strcmp(canonical, "MiXeD-É.chibi"));
        free(canonical);
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    REQUIRE(rooted_files_list(files, "", 8, &entries, &count));
    REQUIRE(count == 1);
    rooted_file_entries_free(entries, count);
    return true;
}
TEST_CASE(rooted_files_host_parent_keeps_unicode_leaf_and_refuses_missing_parent)
{
    REQUIRE(setup());
    char path[512];
    snprintf(path, sizeof path, "%s/保存.json", root);
    char *leaf = NULL;
    rooted_files *directory = rooted_files_open_parent(path, &leaf);
    REQUIRE(directory && leaf && !strcmp(leaf, "保存.json"));
    REQUIRE(rooted_files_write(directory, leaf, "{}", 2, 16, false, NULL, 0));
    rooted_files_close(directory);
    free(leaf);
    snprintf(path, sizeof path, "%s/missing/save.json", root);
    REQUIRE(!rooted_files_open_parent(path, &leaf));
    REQUIRE(!leaf);
    snprintf(path, sizeof path, "%s/", root);
    REQUIRE(!rooted_files_open_parent(path, &leaf));
    REQUIRE(!leaf);
    return true;
}
void test_rooted_files(void)
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(rooted_files_unicode_binary_cas_and_exclusive_publication);
    RUN_TEST_CASE(rooted_files_host_parent_keeps_unicode_leaf_and_refuses_missing_parent);
    RUN_TEST_CASE(rooted_files_identity_matches_actual_volume_case_rules_without_writes);
    RUN_TEST_CASE(rooted_files_repeated_sorted_enumeration_and_limits);
    RUN_TEST_CASE(rooted_files_refuses_traversal_links_and_directory_mutations);
    pu_setup(NULL, NULL);
}
