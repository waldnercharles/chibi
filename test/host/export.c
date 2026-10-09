#include "host/export.h"

#include "third_party/pico_headers/pico_unit.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/chibi-export-XXXXXX";

static void write_at(const char *relative, const char *text, mode_t mode)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    for (char *slash = strchr(path + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        mkdir(path, 0755);
        *slash = '/';
    }
    FILE *file = fopen(path, "wb");
    fputs(text, file);
    fclose(file);
    chmod(path, mode);
}

static void remove_at(const char *relative)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    unlink(path);
}

static bool has(const char *relative, const char *text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    char contents[256] = { 0 };
    fread(contents, 1, sizeof contents - 1, file);
    fclose(file);
    return !text || !strcmp(contents, text);
}

static bool executable(const char *relative)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    return access(path, X_OK) == 0;
}

/* Only the named entries remain in the root: no staging folder is left. */
static int entries()
{
    DIR *dir = opendir(root);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        count += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    }
    closedir(dir);
    return count;
}

static bool export_to(const char *relative)
{
    char project[512], bin[512], output[512];
    snprintf(project, sizeof project, "%s/project", root);
    snprintf(bin, sizeof bin, "%s/sdk/bin", root);
    snprintf(output, sizeof output, "%s/%s", root, relative);
    chibi_export_spec spec = { .project = project,
                               .name = "My Game/2",
                               .bin = bin,
                               .output = output };
    return chibi_export(&spec);
}

TEST_CASE(export_assembles_a_runnable_folder)
{
    REQUIRE(mkdtemp(root));
    /* The fake compiler writes the artifact and lists the external resources,
     * the icon among them. */
    write_at(
        "sdk/bin/chibi-compile",
        "#!/bin/sh\n"
        "test -f \"$2/fail\" && exit 3\n"
        "printf artifact > \"$4\"\n"
        "printf '{\"external\":[{\"path\":\"art/sheet.bin\"},"
        "{\"kind\":\"icon\",\"path\":\"art/icon.png\"}]}' > "
        "\"$4.resources.json\"\n",
        0755
    );
    write_at("sdk/bin/chibi-player", "player", 0755);
    write_at("sdk/share/chibi/engine.json", "{}", 0644);
    write_at("sdk/share/chibi/shaders/nested/a.glsl", "shader", 0644);
    write_at("sdk/share/chibi/licenses/LICENSE.txt", "license", 0644);
    write_at("project/art/icon.png", "icon", 0644);
    write_at("project/art/sheet.bin", "sheet", 0644);

    REQUIRE(export_to("out/Game/"));
    REQUIRE(has("out/Game/My Game_2", "player") && executable("out/Game/My Game_2"));
    REQUIRE(has("out/Game/data/game.chibip", "artifact"));
    REQUIRE(!has("out/Game/data/game.chibip.resources.json", NULL));
    REQUIRE(has("out/Game/data/.chibi-export", ""));
    REQUIRE(has("out/Game/data/art/sheet.bin", "sheet"));
    REQUIRE(has("out/Game/data/art/icon.png", "icon"));
    REQUIRE(has("out/Game/data/engine/engine.json", "{}"));
    REQUIRE(has("out/Game/data/engine/shaders/nested/a.glsl", "shader"));
    REQUIRE(has("out/Game/data/engine/licenses/LICENSE.txt", "license"));

    /* An earlier export is replaced; any other folder is left alone. */
    write_at("out/Game/stale", "", 0644);
    REQUIRE(export_to("out/Game"));
    REQUIRE(!has("out/Game/stale", NULL) && has("out/Game/data/game.chibip", "artifact"));
    write_at("mine/notes.txt", "keep", 0644);
    REQUIRE(!export_to("mine"));
    REQUIRE(has("mine/notes.txt", "keep"));
    /* Looking like an export is not enough: only the exporter's marker
     * allows replacing a folder. */
    write_at("mine/data/game.chibip", "theirs", 0644);
    REQUIRE(!export_to("mine"));
    REQUIRE(has("mine/notes.txt", "keep") && has("mine/data/game.chibip", "theirs"));
    REQUIRE(!export_to("out/Game/.") && has("out/Game/data/game.chibip", "artifact"));

    /* A failed compile or a missing resource leaves no partial folder. */
    write_at("project/fail", "", 0644);
    REQUIRE(!export_to("broken"));
    remove_at("project/fail");
    remove_at("project/art/sheet.bin");
    REQUIRE(!export_to("broken"));
    REQUIRE(entries() == 4); /* sdk, project, out and mine */

    char command[600];
    snprintf(command, sizeof command, "rm -rf '%s'", root);
    REQUIRE(system(command) == 0);
    return true;
}

void test_host_export()
{
    RUN_TEST_CASE(export_assembles_a_runnable_folder);
}
