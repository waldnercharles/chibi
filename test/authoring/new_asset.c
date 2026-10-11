#include "assets/new_asset.h"

#include "third_party/pico_headers/pico_unit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

    /* A linked folder would put the asset outside the project, where the
       catalog never looks, so neither level may be a link. */
    char outside[128], link[128];
    snprintf(outside, sizeof outside, "%s/outside", content);
    REQUIRE(mkdir(outside, 0755) == 0);
    snprintf(link, sizeof link, "%s/systems", content);
    REQUIRE(symlink(outside, link) == 0);
    errno = 0;
    REQUIRE(!new_asset_create(content, NEW_ASSET_SYSTEM, "Wind", error, sizeof error));
    REQUIRE(errno == ENOTDIR && strstr(error, "not a folder"));
    snprintf(link, sizeof link, "%s/linked", content);
    REQUIRE(symlink(outside, link) == 0);
    errno = 0;
    REQUIRE(!new_asset_create(link, NEW_ASSET_EVENT, "Wind", error, sizeof error));
    REQUIRE(errno == ENOTDIR);
    snprintf(expected_path, sizeof expected_path, "%s/Wind.chibi", outside);
    REQUIRE(access(expected_path, F_OK) != 0);
    snprintf(expected_path, sizeof expected_path, "%s/events", outside);
    REQUIRE(access(expected_path, F_OK) != 0);
    return true;
}

TEST_CASE(shader_source_creation_preserves_existing_glsl)
{
    snprintf(content, sizeof content, "/tmp/chibi-new-asset-XXXXXX");
    REQUIRE(mkdtemp(content));
    char error[256] = { 0 };
    char *path = new_asset_create(content, NEW_ASSET_SHADER_SOURCE, "Rain", error, sizeof error);
    REQUIRE(path && strstr(path, "/shaders/Rain.kdl"));
    REQUIRE(strstr(read_text(path), "kind \"particle\""));
    char glsl[128];
    snprintf(glsl, sizeof glsl, "%s/shaders/Rain.glsl", content);
    REQUIRE(strstr(read_text(glsl), "ChibiParticle particle("));
    REQUIRE(unlink(path) == 0);
    free(path);
    FILE *file = fopen(glsl, "wb");
    REQUIRE(file && fputs("existing GLSL\n", file) >= 0 && fclose(file) == 0);
    errno = 0;
    REQUIRE(!new_asset_create(content, NEW_ASSET_SHADER_SOURCE, "Rain", error, sizeof error));
    REQUIRE(errno == EEXIST && strstr(error, "Rain.glsl already exists"));
    REQUIRE(!strcmp(read_text(glsl), "existing GLSL\n"));
    char descriptor[128];
    snprintf(descriptor, sizeof descriptor, "%s/shaders/Rain.kdl", content);
    REQUIRE(access(descriptor, F_OK) != 0 && errno == ENOENT);
    return true;
}

void test_new_asset()
{
    pu_setup(NULL, remove_tree);
    RUN_TEST_CASE(new_asset_create_writes_the_template_once_into_its_folder);
    RUN_TEST_CASE(shader_source_creation_preserves_existing_glsl);
    pu_clear_setup();
}
