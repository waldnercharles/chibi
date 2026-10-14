#include "authoring/workspace_file.h"

#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char root[128], outside[128], error[256];
static workspace_files *files;
static void cleanup()
{
    workspace_files_free(files);
    files = NULL;
    if (*root) {
        tool_remove_tree(root);
        *root = 0;
    }
    if (*outside) {
        tool_remove_tree(outside);
        *outside = 0;
    }
}
static bool setup()
{
    strcpy(root, "/tmp/chibi-bundle-XXXXXX");
    REQUIRE(mkdtemp(root));
    files = workspace_files_new(root, error, sizeof error);
    REQUIRE(files);
    return true;
}
static bool absent(const char *relative)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    return access(path, F_OK) != 0;
}
TEST_CASE(workspace_bundle_binary_files_and_manifest_publish_exclusively)
{
    REQUIRE(setup());
    const unsigned char pixels[] = { 0, 255, 3, 0, 128 };
    const char manifest[] = "{\"complete\":true}\n";
    const workspace_file_entry entries[] = {
        { "images/frame.bin", pixels, sizeof pixels },
        { "empty.txt", NULL, 0 },
        { "manifest.json", manifest, sizeof manifest - 1 }
    };
    REQUIRE(workspace_file_bundle_write_new(files, "evidence", entries, 3, error, sizeof error));
    char path[512];
    snprintf(path, sizeof path, "%s/evidence/images/frame.bin", root);
    FILE *file = fopen(path, "rb");
    REQUIRE(file);
    unsigned char bytes[32];
    size_t count = fread(bytes, 1, sizeof bytes, file);
    fclose(file);
    REQUIRE(count == sizeof pixels && !memcmp(bytes, pixels, sizeof pixels));
    snprintf(path, sizeof path, "%s/evidence/manifest.json", root);
    char *text = tool_read(path);
    REQUIRE(text && !strcmp(text, manifest));
    free(text);
    const workspace_file_entry replacement = { "manifest.json", "bad", 3 };
    REQUIRE(
        !workspace_file_bundle_write_new(files, "evidence", &replacement, 1, error, sizeof error)
    );
    text = tool_read(path);
    REQUIRE(text && !strcmp(text, manifest));
    free(text);
    return true;
}
TEST_CASE(workspace_bundle_invalid_input_creates_no_directory)
{
    REQUIRE(setup());
    const char *invalid[] = { "../escape", "/absolute", "a//b", "a/./b",
                              "a/../b",    "a\\b",      "a/" };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        workspace_file_entry entry = { invalid[i], "x", 1 };
        REQUIRE(
            !workspace_file_bundle_write_new(files, "rejected", &entry, 1, error, sizeof error)
        );
        REQUIRE(absent("rejected"));
    }
    workspace_file_entry entries[] = { { "same", "x", 1 }, { "same", "y", 1 } };
    REQUIRE(!workspace_file_bundle_write_new(files, "rejected", entries, 2, error, sizeof error));
    REQUIRE(absent("rejected"));
    entries[1].path = "same/child";
    REQUIRE(!workspace_file_bundle_write_new(files, "rejected", entries, 2, error, sizeof error));
    REQUIRE(absent("rejected"));
    entries[0] = (workspace_file_entry){ "missing", NULL, 1 };
    REQUIRE(!workspace_file_bundle_write_new(files, "rejected", entries, 1, error, sizeof error));
    REQUIRE(absent("rejected"));
    entries[0] = (workspace_file_entry){ "oversized", "x", WORKSPACE_FILE_BYTES_MAX + 1 };
    REQUIRE(!workspace_file_bundle_write_new(files, "rejected", entries, 1, error, sizeof error));
    REQUIRE(absent("rejected"));
    REQUIRE(
        !workspace_file_bundle_write_new(files, "rejected", entries, WORKSPACE_BUNDLE_FILES_MAX + 1, error, sizeof error)
    );
    REQUIRE(absent("rejected"));
    char long_name[257];
    memset(long_name, 'x', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = 0;
    entries[0] = (workspace_file_entry){ long_name, "x", 1 };
    REQUIRE(!workspace_file_bundle_write_new(files, "rejected", entries, 1, error, sizeof error));
    REQUIRE(absent("rejected"));
    return true;
}
TEST_CASE(workspace_bundle_refuses_linked_parent_without_touching_its_target)
{
    REQUIRE(setup());
    strcpy(outside, "/tmp/chibi-bundle-outside-XXXXXX");
    REQUIRE(mkdtemp(outside));
    char path[512];
    snprintf(path, sizeof path, "%s/linked", root);
    REQUIRE(!symlink(outside, path));
    workspace_file_entry entry = { "manifest.json", "{}", 2 };
    REQUIRE(
        !workspace_file_bundle_write_new(files, "linked/evidence", &entry, 1, error, sizeof error)
    );
    snprintf(path, sizeof path, "%s/evidence", outside);
    REQUIRE(access(path, F_OK));
    REQUIRE(absent("evidence"));
    return true;
}
void test_workspace_file()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(workspace_bundle_binary_files_and_manifest_publish_exclusively);
    RUN_TEST_CASE(workspace_bundle_invalid_input_creates_no_directory);
    RUN_TEST_CASE(workspace_bundle_refuses_linked_parent_without_touching_its_target);
    pu_setup(NULL, NULL);
}
