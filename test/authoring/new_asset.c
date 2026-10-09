#include "assets/new_asset.h"

#include "third_party/pico_headers/pico_unit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char content[64];

/* The whole small file, or "" when it cannot be read. */
static const char *read_text(const char *path)
{
    static char text[4096];
    FILE *file = fopen(path, "rb");
    size_t length = file ? fread(text, 1, sizeof text - 1, file) : 0;
    if (file) {
        fclose(file);
    }
    text[length] = '\0';
    return text;
}

static void remove_tree()
{
    char command[128];
    snprintf(command, sizeof command, "rm -rf '%s'", content);
    if (content[0] && system(command) != 0) {
        abort();
    }
    content[0] = '\0';
}

TEST_CASE(new_asset_create_writes_the_template_once_into_its_folder)
{
    snprintf(content, sizeof content, "/tmp/chibi-new-asset-XXXXXX");
    REQUIRE(mkdtemp(content));
    char error[256] = { 0 };
    /* The folder is missing; the first asset of a kind creates it. */
    char *path = new_asset_create(content, NEW_ASSET_BEHAVIOR, "Board", error, sizeof error);
    REQUIRE(path);
    char expected_path[128];
    snprintf(expected_path, sizeof expected_path, "%s/behaviors/Board.chibi", content);
    REQUIRE(!strcmp(path, expected_path));
    char *expected = new_asset_text(NEW_ASSET_BEHAVIOR, "Board");
    REQUIRE(expected && !strcmp(read_text(path), expected));
    free(expected);
    free(path);

    errno = 0;
    REQUIRE(!new_asset_create(content, NEW_ASSET_BEHAVIOR, "Board", error, sizeof error));
    REQUIRE(errno == EEXIST && strstr(error, "already exists"));

    path = new_asset_create(content, NEW_ASSET_SCENE, "Level", error, sizeof error);
    REQUIRE(path && strstr(path, "/scenes/Level.kdl"));
    expected = new_asset_text(NEW_ASSET_SCENE, "Level");
    REQUIRE(expected && !strcmp(read_text(path), expected));
    free(expected);
    free(path);

    path = new_asset_create(content, NEW_ASSET_SHAPE, "Block", error, sizeof error);
    REQUIRE(path);
    free(path);
    errno = 0;
    REQUIRE(!new_asset_create(content, NEW_ASSET_MODEL, "Block", error, sizeof error));
    REQUIRE(errno == EEXIST);

    errno = 0;
    REQUIRE(!new_asset_create(content, NEW_ASSET_PROTOTYPE, "1Block", error, sizeof error));
    REQUIRE(errno == EINVAL);
    snprintf(expected_path, sizeof expected_path, "%s/prototypes/1Block.kdl", content);
    REQUIRE(access(expected_path, F_OK) != 0);

    /* A file where the folder belongs is a storage failure, not a bad name. */
    snprintf(expected_path, sizeof expected_path, "%s/libraries", content);
    FILE *blocker = fopen(expected_path, "wb");
    REQUIRE(blocker && !fclose(blocker));
    errno = 0;
    REQUIRE(!new_asset_create(content, NEW_ASSET_LIBRARY, "Wind", error, sizeof error));
    REQUIRE(errno == ENOTDIR);
    return true;
}

void test_new_asset()
{
    pu_setup(NULL, remove_tree);
    RUN_TEST_CASE(new_asset_create_writes_the_template_once_into_its_folder);
    pu_clear_setup();
}
