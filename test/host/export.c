#include "host/export.h"

#include "third_party/pico_headers/pico_unit.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/chibi-export-XXXXXX";
static const char *game = "My Game/2";

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

/* Entries in a folder under the root, to show no staging or partial file is
 * left. */
static int entries(const char *relative)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    DIR *dir = opendir(path);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        count += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    }
    closedir(dir);
    return count;
}

static const char *title = "My <Game> \\ & Co";
static const char *icon = "art/icon.png";
static const char *const *natives;

static bool export_as(const char *relative, chibi_export_layout layout, bool archive)
{
    char project[512], bin[512], web[512], output[512];
    snprintf(project, sizeof project, "%s/project", root);
    snprintf(bin, sizeof bin, "%s/sdk/bin", root);
    snprintf(web, sizeof web, "%s/web", root);
    snprintf(output, sizeof output, "%s/%s", root, relative);
    chibi_export_spec spec = { .project = project,
                               .name = game,
                               .title = title,
                               .icon = icon,
                               .bin = bin,
                               .web = layout == CHIBI_EXPORT_WEB ? web : NULL,
                               .output = output,
                               .layout = layout,
                               .archive = archive,
                               .natives = natives };
    return chibi_export(&spec);
}

static bool export_to(const char *relative)
{
    return export_as(relative, CHIBI_EXPORT_FOLDER, false);
}

/* A PNG header for a `width` by `height` image is all the .icns writer reads. */
static void write_png(int width, int height)
{
    char path[512];
    snprintf(path, sizeof path, "%s/project/art/icon.png", root);
    unsigned char header[26] = "\x89PNG\r\n\x1a\n\0\0\0\rIHDR";
    header[18] = (unsigned char)(width >> 8);
    header[19] = (unsigned char)width;
    header[22] = (unsigned char)(height >> 8);
    header[23] = (unsigned char)height;
    header[24] = 8;
    header[25] = 6;
    FILE *icon = fopen(path, "wb");
    fwrite(header, 1, sizeof header, icon);
    fclose(icon);
}

static bool starts_with(const char *relative, const void *bytes, size_t size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    char contents[64] = { 0 };
    size_t read = fread(contents, 1, sizeof contents, file);
    fclose(file);
    return read >= size && !memcmp(contents, bytes, size);
}

static bool contains(const char *relative, const char *text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    char contents[4096] = { 0 };
    fread(contents, 1, sizeof contents - 1, file);
    fclose(file);
    return strstr(contents, text) != NULL;
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
        "{\"path\":\"files.json\"},"
        "{\"kind\":\"icon\",\"path\":\"art/icon.png\"}]}' > "
        "\"$4.resources.json\"\n",
        0755
    );
    write_at("sdk/bin/chibi-player", "player", 0755);
    write_at("sdk/share/chibi/engine.json", "{}", 0644);
    write_at("sdk/share/chibi/shaders/nested/a.glsl", "shader", 0644);
    write_at("sdk/share/chibi/licenses/LICENSE.txt", "license", 0644);
    write_at("project/art/icon.png", "", 0644);
    write_png(64, 64);
    write_at("project/art/sheet.bin", "sheet", 0644);
    write_at("project/files.json", "authored", 0644);

    REQUIRE(export_to("out/Game/"));
    REQUIRE(has("out/Game/My Game_2", "player") && executable("out/Game/My Game_2"));
    REQUIRE(has("out/Game/data/game.chibip", "artifact"));
    REQUIRE(!has("out/Game/data/game.chibip.resources.json", NULL));
    REQUIRE(has("out/Game/data/.chibi-export", ""));
    REQUIRE(has("out/Game/data/art/sheet.bin", "sheet"));
    REQUIRE(contains("out/Game/data/art/icon.png", "PNG"));
    REQUIRE(contains("out/Game/my-game-2.desktop", "Name=My <Game> \\\\ & Co\nExec=\"My Game_2\"\n"));
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
    /* Nor is a link to an export's data folder. */
    char linked[512], target[512];
    snprintf(linked, sizeof linked, "%s/linked/data", root);
    snprintf(target, sizeof target, "%s/out/Game/data", root);
    write_at("linked/notes.txt", "keep", 0644);
    REQUIRE(symlink(target, linked) == 0);
    REQUIRE(!export_to("linked") && has("linked/notes.txt", "keep"));
    REQUIRE(!export_to("out/Game/.") && has("out/Game/data/game.chibip", "artifact"));

    /* A long project name is cut to a file name every file system takes. */
    char long_name[301];
    memset(long_name, 'a', 300);
    long_name[300] = 0;
    game = long_name;
    REQUIRE(export_to("out/Game"));
    long_name[128] = 0;
    char cut[160];
    snprintf(cut, sizeof cut, "out/Game/%s", long_name);
    REQUIRE(has(cut, "player"));
    game = "My Game/2";

    /* A macOS app keeps the same data in Contents/Resources, and an archive
     * holds the export under its own name. */
    REQUIRE(export_as("apps/Game", CHIBI_EXPORT_APP, true));
    REQUIRE(has("apps/Game.app/Contents/MacOS/My Game_2", "player"));
    REQUIRE(has("apps/Game.app/Contents/Resources/game.chibip", "artifact"));
    REQUIRE(has("apps/Game.app/Contents/Resources/engine/engine.json", "{}"));
    REQUIRE(
        contains("apps/Game.app/Contents/Info.plist", "<string>My &lt;Game&gt; \\ &amp; Co</string>")
    );
    REQUIRE(contains("apps/Game.app/Contents/Info.plist", "games.chibi.my-game-2"));
    /* The .icns holds the 26-byte PNG as its 64-pixel icon. */
    REQUIRE(starts_with(
        "apps/Game.app/Contents/Resources/My Game_2.icns",
        "icns\0\0\0\x2a"
        "icp6\0\0\0\x22\x89PNG",
        20
    ));
    REQUIRE(has("apps/Game.app.tar.gz", NULL));
    int archived_entries = entries("apps");
    /* A tar that cannot run leaves the earlier archive in place. */
    char path[4096];
    snprintf(path, sizeof path, "%s", getenv("PATH"));
    setenv("PATH", "", 1);
    bool archived = export_as("apps/Game", CHIBI_EXPORT_APP, true);
    setenv("PATH", path, 1);
    REQUIRE(!archived && has("apps/Game.app.tar.gz", NULL));
    REQUIRE(entries("apps") == archived_entries);

    /* Without a title, people see the project name. */
    title = NULL;
    REQUIRE(export_as("apps/Game.app", CHIBI_EXPORT_APP, false));
    REQUIRE(
        contains("apps/Game.app/Contents/Info.plist", "<key>CFBundleName</key><string>My Game/2</string>")
    );
    REQUIRE(export_to("out/Game"));
    REQUIRE(contains("out/Game/my-game-2.desktop", "Name=My Game/2\n"));

    /* An app's icon must be a size macOS has, and its output must name a
     * folder; both fail before anything is built. */
    write_png(48, 48);
    REQUIRE(!export_as("apps/Odd", CHIBI_EXPORT_APP, false));
    write_png(64, 32);
    REQUIRE(!export_as("apps/Odd", CHIBI_EXPORT_APP, false));
    write_png(64, 64);
    char icon_path[512];
    snprintf(icon_path, sizeof icon_path, "%s/project/art/icon.png", root);
    REQUIRE(truncate(icon_path, 65 * 1024 * 1024) == 0); /* sparse, over the bound */
    REQUIRE(!export_as("apps/Odd", CHIBI_EXPORT_APP, false));
    write_png(64, 64);
    REQUIRE(!export_as("apps/.", CHIBI_EXPORT_APP, false));
    REQUIRE(!export_as("apps/.app", CHIBI_EXPORT_APP, false));
    REQUIRE(!has("apps/Odd.app/Contents/Info.plist", NULL));

    /* A Windows export names the player <name>.exe and writes no desktop
     * entry. Only a Windows host can put the icon in its resources. */
    REQUIRE(!export_as("win/Game", CHIBI_EXPORT_WINDOWS, false));
    REQUIRE(!has("win/Game/My Game_2.exe", NULL));
    icon = NULL;
    REQUIRE(export_as("win/Game", CHIBI_EXPORT_WINDOWS, false));
    REQUIRE(has("win/Game/My Game_2.exe", "player"));
    REQUIRE(has("win/Game/data/game.chibip", "artifact"));
    REQUIRE(!has("win/Game/my-game-2.desktop", NULL));
    REQUIRE(export_to("win/Game")); /* replaces a Windows export */
    game = "Con.1";                 /* a device name Windows reserves */
    REQUIRE(export_as("win/Con", CHIBI_EXPORT_WINDOWS, false));
    REQUIRE(has("win/Con/game.exe", "player"));
    REQUIRE(export_to("out/Con") && has("out/Con/Con.1", "player"));
    game = "My Game/2";
    icon = "art/icon.png";

    /* A web export is a static site: the browser player's page titled for the
     * game beside its script and module, and files.json listing every data
     * file for the page to fetch, even an authored data/files.json. */
    write_at("web/index.html", "<title>{{TITLE}}</title>{{TITLE", 0644);
    /* The browser player's compiler runs under Node; this fake node runs the
     * fake compiler script instead. */
    write_at("node/node", "#!/bin/sh\nscript=$1\nshift\nexec sh \"$script\" \"$@\"\n", 0755);
    char compiler[512], fake_compiler[512];
    snprintf(compiler, sizeof compiler, "%s/sdk/bin/chibi-compile", root);
    snprintf(fake_compiler, sizeof fake_compiler, "%s/web/compiler/chibi-compile.js", root);
    write_at("web/compiler/chibi-compile.js", "", 0644);
    REQUIRE(rename(compiler, fake_compiler) == 0);
    char *saved_path = strdup(getenv("PATH")), node_path[1024];
    snprintf(node_path, sizeof node_path, "%s/node:%s", root, saved_path);
    setenv("PATH", node_path, 1);
    write_at("web/chibi-player.js", "script", 0644);
    write_at("web/chibi-player.wasm", "module", 0644);
    title = "<Web> & Co";
    REQUIRE(export_as("site", CHIBI_EXPORT_WEB, false));
    REQUIRE(has("site/index.html", "<title>&lt;Web&gt; &amp; Co</title>{{TITLE"));
    REQUIRE(has("site/chibi-player.js", "script") && has("site/chibi-player.wasm", "module"));
    REQUIRE(!has("site/My Game_2", NULL) && !has("site/my-game-2.desktop", NULL));
    REQUIRE(has("site/data/game.chibip", "artifact"));
    REQUIRE(has("site/data/files.json", "authored"));
    REQUIRE(contains("site/files.json", "\"game.chibip\""));
    REQUIRE(contains("site/files.json", "\"art/sheet.bin\""));
    REQUIRE(contains("site/files.json", "\"files.json\""));
    REQUIRE(contains("site/files.json", "\"engine/shaders/nested/a.glsl\""));
    REQUIRE(has("site/data/.chibi-export", "") && !contains("site/files.json", ".chibi-export"));
    REQUIRE(export_as("site", CHIBI_EXPORT_WEB, false)); /* replaces a site */
    remove_at("web/chibi-player.wasm");
    REQUIRE(!export_as("broken", CHIBI_EXPORT_WEB, false));
    setenv("PATH", saved_path, 1);
    free(saved_path);
    REQUIRE(rename(fake_compiler, compiler) == 0);

    /* The natives command writes the libraries into the data folder's
     * natives/, which it receives last; when it fails, nothing is exported. */
    write_at(
        "sdk/natives.sh",
        "#!/bin/sh\n"
        "test \"$1\" = cache || exit 3\n"
        "for last; do :; done\n"
        "mkdir \"$last\" && printf so > \"$last/Mix.so\"\n",
        0755
    );
    char script[512];
    snprintf(script, sizeof script, "%s/sdk/natives.sh", root);
    const char *const built[] = { script, "cache", NULL }, *const failing[] = { script,
                                                                                "none",
                                                                                NULL };
    natives = built;
    REQUIRE(export_to("native/Game") && has("native/Game/data/natives/Mix.so", "so"));
    natives = failing;
    REQUIRE(!export_to("native/Broken") && !has("native/Broken/data/game.chibip", NULL));
    REQUIRE(entries("native") == 1);
    natives = NULL;

    /* A failed compile or a missing resource leaves no partial folder. */
    write_at("project/fail", "", 0644);
    REQUIRE(!export_to("broken"));
    remove_at("project/fail");
    remove_at("project/art/sheet.bin");
    REQUIRE(!export_to("broken"));
    REQUIRE(entries(".") == 11); /* sdk, project, out, apps, mine, linked, win, web, node, site and native */

    char command[600];
    snprintf(command, sizeof command, "rm -rf '%s'", root);
    REQUIRE(system(command) == 0);
    return true;
}

void test_host_export()
{
    RUN_TEST_CASE(export_assembles_a_runnable_folder);
}
