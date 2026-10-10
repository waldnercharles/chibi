#include "assets/project.h"
#include "assets/prototype.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The longest prototype name the player takes, GAME_PROTOTYPE_NAME_MAX. */
#define PROTOTYPE_NAME_MAX 1024
/* How many resources a player takes in all, GAME_PLAYER_MAX_ASSETS. */
#define PLAYER_MAX_ASSETS 4096

/* An Ogg Vorbis file's first page holds only its 30-byte identification
 * header; a page with no segments is its header alone. */
enum
{
    OGG_PAGE_HEADER = 27,
    OGG_FIRST_PAGE = OGG_PAGE_HEADER + 1 + 30,
    OGG_END_OF_STREAM = 4
};

static char program[4096];
static char root[64], log_path[96];
static yyjson_doc *reply;

static void path(char *out, size_t size, const char *relative)
{
    snprintf(out, size, "%s/%s", root, relative);
}

static void write_file(const char *relative, const char *text)
{
    char file[256];
    path(file, sizeof file, relative);
    tool_write(file, text);
}

/* Runs a shell command from `root` that sees chibi_ctl as $0 and its own
 * output as $1, for at most `allowed` seconds; returns its exit status and
 * keeps the last reply. */
static int run_within(const char *command, double allowed)
{
    const char *args[] = { "/bin/sh", "-c", command, program, log_path, NULL };
    int status = tool_run_within(args, root, log_path, allowed);
    char *output = tool_read(log_path);
    const char *json = output ? strstr(output, "{\"contract\"") : NULL;
    for (const char *next; json && (next = strstr(json + 1, "{\"contract\""));) {
        json = next;
    }
    yyjson_doc_free(reply);
    reply = json ? yyjson_read(json, strlen(json), YYJSON_READ_STOP_WHEN_DONE) : NULL;
    free(output);
    assert(reply);
    return status;
}

static int run(const char *command)
{
    return run_within(command, TOOL_RUN_SECONDS);
}

/* Sends requests, one per line, to one chibi_ctl, inside the project "game"
 * when `scoped`. */
static int ctl(bool scoped, const char *request)
{
    write_file("request.json", request);
    return run(scoped ? "exec \"$0\" --project-root game < request.json" : "exec \"$0\" < request.json");
}

static int validate()
{
    return ctl(true, "{\"contract\":1,\"operation\":\"validate_project\"}");
}

/* Validation loads each prototype three times, so under sanitizers a project
 * that reads past the expansion budget can take longer than a usual run. */
static const double EXPANSION_SECONDS = 30;

static int validate_expansion()
{
    write_file("request.json", "{\"contract\":1,\"operation\":\"validate_project\"}");
    return run_within("exec \"$0\" --project-root game < request.json", EXPANSION_SECONDS);
}

static yyjson_val *result(const char *key)
{
    return yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(reply), "result"), key);
}

static int errors()
{
    return (int)yyjson_get_int(result("errors"));
}

/* The message of the last reply's error. */
static const char *failure()
{
    yyjson_val *error = yyjson_obj_get(yyjson_doc_get_root(reply), "error");
    return yyjson_get_str(yyjson_obj_get(error, "message"));
}

/* The code of the last reply's error. */
static const char *failure_code()
{
    yyjson_val *error = yyjson_obj_get(yyjson_doc_get_root(reply), "error");
    return yyjson_get_str(yyjson_obj_get(error, "code"));
}

/* The message of the diagnostic at `index` in `diagnostics`. */
static const char *message_at(yyjson_val *diagnostics, size_t index)
{
    return yyjson_get_str(yyjson_obj_get(yyjson_arr_get(diagnostics, index), "message"));
}

static const char *first_message(yyjson_val *diagnostics)
{
    return message_at(diagnostics, 0);
}

/* Sets the manifest's `key`, a line the scaffold writes, to `value`. */
static void set_manifest(const char *key, const char *value)
{
    char manifest[256], prefix[64];
    path(manifest, sizeof manifest, "game/project.kdl");
    snprintf(prefix, sizeof prefix, "\n%s ", key);
    char *text = tool_read(manifest);
    char *line = text ? strstr(text, prefix) : NULL;
    assert(line);
    char *rest = strchr(line + 1, '\n');
    line[1] = '\0';
    char updated[1024];
    snprintf(updated, sizeof updated, "%s%s \"%s\"%s", text, key, value, rest ? rest : "\n");
    free(text);
    tool_write(manifest, updated);
}

static void set_start_scene(const char *scene)
{
    set_manifest("start_scene", scene);
}

static void append_manifest(const char *line)
{
    char manifest[256];
    path(manifest, sizeof manifest, "game/project.kdl");
    char *text = tool_read(manifest);
    assert(text);
    char updated[1024];
    snprintf(updated, sizeof updated, "%s%s", text, line);
    free(text);
    tool_write(manifest, updated);
}

static void setup()
{
    strcpy(root, "/tmp/chibi-validate-project-XXXXXX");
    assert(mkdtemp(root));
    path(log_path, sizeof log_path, "output");
    int created = ctl(false, "{\"contract\":1,\"operation\":\"new_project\",\"path\":\"game\",\"name\":\"Game\"}");
    assert(created == 0);
}

static void teardown()
{
    yyjson_doc_free(reply);
    reply = NULL;
    tool_remove_tree(root);
}

TEST_CASE(new_project_is_valid)
{
    REQUIRE(validate() == 0);
    REQUIRE(yyjson_get_bool(result("valid")) && errors() == 0);
    REQUIRE(!yyjson_arr_size(result("project")) && !yyjson_arr_size(result("problems")));
    return true;
}

TEST_CASE(stopped_runs_are_reported)
{
    /* The stopped command's report goes to the test's own stderr. */
    char file[160];
    path(file, sizeof file, "stderr");
    fflush(stderr);
    int saved = dup(STDERR_FILENO),
        captured = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    REQUIRE(saved >= 0 && captured >= 0);
    dup2(captured, STDERR_FILENO);
    close(captured);
    const char *args[] = { "/bin/sh", "-c", "exec sleep 5", NULL };
    int status = tool_run_within(args, root, log_path, 1);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    char *text = tool_read(file);
    bool reported = text &&
                    strstr(text, "stopped after 1 seconds: /bin/sh -c exec sleep 5\n");
    free(text);
    REQUIRE(status == 124 && reported);
    return true;
}

TEST_CASE(ui_style_problems_are_listed)
{
    char folder[256];
    path(folder, sizeof folder, "game/content/ui");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file("game/content/ui/Hud.rml", "<div class=\"hud\"><p>Score</p></div>\n");
    write_file("game/content/ui/Hud.rcss", ".hud { colr: red; }\n");
    REQUIRE(validate() != 0 && !yyjson_get_bool(result("valid")) && errors() == 1);
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    REQUIRE(yyjson_arr_size(result("problems")) == 1);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(problem, "name")), "Hud"));
    yyjson_val *diagnostics = yyjson_obj_get(problem, "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 1);
    REQUIRE(
        !strcmp(yyjson_get_str(yyjson_obj_get(yyjson_arr_get_first(diagnostics), "rule_name")), "RMLUI")
    );
    REQUIRE(strstr(first_message(diagnostics), "colr"));
    return true;
}

TEST_CASE(start_scene_outside_scenes_is_parsed)
{
    write_file("broken.kdl", "version 1\nname \"Broken\"\n(array)entities {\n");
    set_start_scene("../../broken.kdl");
    REQUIRE(validate() != 0 && errors() == 1 && !yyjson_arr_size(result("problems")));
    REQUIRE(yyjson_arr_size(result("project")) == 1);
    const char *message = first_message(result("project"));
    REQUIRE(strstr(message, "start scene ../../broken.kdl: ") == message);
    REQUIRE(strstr(message, "Unexpected end of data"));

    write_file("unknown_prototype.kdl", "version 1\nname \"Out\"\n(array)entities {\n    - name=\"Hero\" prototype=\"Hero\"\n}\n");
    set_start_scene("../../unknown_prototype.kdl");
    REQUIRE(validate() != 0 && errors() == 1);
    REQUIRE(strstr(first_message(result("project")), "Hero"));

    write_file("empty.kdl", "version 1\nname \"Empty\"\n(array)entities\n");
    set_start_scene("../../empty.kdl");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    return true;
}

TEST_CASE(cataloged_start_scene_is_reported_once)
{
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n");
    set_start_scene("../content/scenes/Start.kdl");
    REQUIRE(validate() != 0 && !yyjson_arr_size(result("project")));
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    REQUIRE(yyjson_arr_size(result("problems")) == 1);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(problem, "name")), "Start"));
    REQUIRE(errors() == (int)yyjson_arr_size(yyjson_obj_get(problem, "diagnostics")));
    return true;
}

TEST_CASE(missing_ui_documents_are_reported)
{
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\nui \"Pause\"\n(array)entities\n");
    append_manifest("ui \"Hud\"\n");
    REQUIRE(validate() != 0 && errors() == 2);
    REQUIRE(!strcmp(first_message(result("project")), "Unknown UI document 'Hud'"));
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(problem, "name")), "Start"));
    REQUIRE(
        !strcmp(first_message(yyjson_obj_get(problem, "diagnostics")), "Unknown UI document 'Pause'")
    );

    char folder[256], hud[256];
    path(folder, sizeof folder, "game/content/ui");
    REQUIRE(mkdir(folder, 0700) == 0);
    /* Preparation only reads regular files. */
    path(hud, sizeof hud, "game/content/ui/Hud.rml");
    REQUIRE(mkdir(hud, 0700) == 0);
    REQUIRE(validate() != 0 && errors() == 3);
    REQUIRE(!strcmp(first_message(result("project")), "Unknown UI document 'Hud'"));
    REQUIRE(!strcmp(message_at(result("project"), 1), "UI document 'Hud' is not a regular file"));
    REQUIRE(rmdir(hud) == 0);
    /* Preparation follows a link, but the catalog never does, so linked
     * markup is never checked and is refused, named or not. */
    char broken[256], extra[256];
    write_file("broken.rml", "<div>\n");
    path(broken, sizeof broken, "broken.rml");
    REQUIRE(symlink(broken, hud) == 0);
    REQUIRE(validate() != 0 && errors() == 3);
    REQUIRE(!strcmp(first_message(result("project")), "Unknown UI document 'Hud'"));
    REQUIRE(unlink(hud) == 0);
    write_file("game/content/ui/Hud.rml", "<div>Score</div>\n");
    write_file("game/content/ui/Pause.rml", "<div>Paused</div>\n");
    path(extra, sizeof extra, "game/content/ui/Extra.rml");
    REQUIRE(symlink(broken, extra) == 0);
    REQUIRE(validate() != 0 && errors() == 1);
    REQUIRE(!strcmp(first_message(result("project")), "UI document 'Extra' is not a regular file"));
    REQUIRE(unlink(extra) == 0);
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));

    /* A GameUi document is looked up the same way, by name only, in the
     * prototype and in the scene that places it. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Menu\"}") == 0
    );
    write_file("game/content/prototypes/Menu.kdl", "name \"Menu\"\ncomponents {\n    GAME.GameUi document=\"../ui/Hud\"\n}\n");
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Menu\" prototype=\"Menu\"\n}\n");
    REQUIRE(validate() != 0 && errors() == 2 && yyjson_arr_size(result("problems")) == 2);
    size_t index, count;
    yyjson_arr_foreach(result("problems"), index, count, problem)
    {
        const char *message = first_message(yyjson_obj_get(problem, "diagnostics"));
        REQUIRE(!strcmp(message, "Unknown UI document '../ui/Hud'"));
    }
    return true;
}

TEST_CASE(catalog_errors_are_reported_once)
{
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Hero\" prototype=\"Hero\"\n}\n");
    REQUIRE(validate() != 0 && errors() == 1);
    yyjson_val *diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 1);
    REQUIRE(!strcmp(first_message(diagnostics), "Unknown prototype 'Hero'"));

    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Hero\"}") == 0
    );
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Hero\" prototype=\"Hero\" {\n        (array)behaviors \"Ghost\"\n    }\n}\n");
    REQUIRE(validate() != 0 && errors() == 1);
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 1);
    REQUIRE(!strcmp(first_message(diagnostics), "Unknown behavior or system 'Ghost'"));

    /* A system is not a behavior, and the scene still gets one error. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"system\",\"name\":\"Ghost\"}") == 0
    );
    REQUIRE(validate() != 0 && errors() == 1);
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(!strcmp(first_message(diagnostics), "Unknown behavior or system 'Ghost'"));

    /* A source's syntax errors come once, from the catalog, with their lines. */
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Hero\" prototype=\"Hero\"\n}\n");
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"behavior\",\"name\":\"Broken\"}") == 0
    );
    write_file("game/content/behaviors/Broken.chibi", "behavior Broken\n\nfn update( {\n");
    REQUIRE(validate() != 0 && errors() == 2);
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 2);
    yyjson_val *diagnostic;
    size_t index, count;
    yyjson_arr_foreach(diagnostics, index, count, diagnostic)
    {
        REQUIRE(yyjson_obj_get(diagnostic, "line"));
    }

    /* The catalog lists the declaration's unknown type, not the cast's. */
    write_file("game/content/behaviors/Broken.chibi", "behavior \"Broken\" {\n    state a: Foo = 0\n    on Tick {\n        a = cast<Foo>(a)\n    }\n}\n");
    REQUIRE(validate() != 0 && errors() == 2);
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 2);
    return true;
}

TEST_CASE(other_project_versions_are_not_opened)
{
    char manifest[256];
    path(manifest, sizeof manifest, "game/project.kdl");
    char *text = tool_read(manifest);
    int version = 0;
    REQUIRE(text && sscanf(text, "version %d", &version) == 1);
    char old[1024];
    snprintf(old, sizeof old, "version %d%s", version - 1, strchr(text, '\n'));
    free(text);
    tool_write(manifest, old);
    REQUIRE(validate() != 0 && errors() == 1);
    const char *message = first_message(result("project"));
    REQUIRE(message && strstr(message, "--migrate"));
    /* Validation leaves the old project as it was. */
    text = tool_read(manifest);
    bool kept = text && !strcmp(text, old);
    free(text);
    REQUIRE(kept);
    /* A newer build's project needs that build, whatever its fields mean
     * there. */
    snprintf(old, sizeof old, "version %d\nname 5\n", version + 1);
    tool_write(manifest, old);
    REQUIRE(validate() != 0 && errors() == 1);
    message = first_message(result("project"));
    REQUIRE(message && strstr(message, "needs a newer build"));
    return true;
}

TEST_CASE(application_settings_are_checked)
{
    /* Preparation decodes the icon, so one that is missing or not a PNG
     * fails validation too. */
    append_manifest("application {\n    icon \"icon.png\"\n}\n");
    REQUIRE(validate() != 0 && errors() == 1);
    const char *message = first_message(result("project"));
    REQUIRE(
        message &&
        strstr(message, "Application icon is not a readable PNG: icon.png: ") == message
    );
    write_file("game/icon.png", "not a png");
    REQUIRE(validate() != 0 && errors() == 1);
    static const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x64, 0x60, 0xF8, 0x5F,
        0x0F, 0x00, 0x02, 0x87, 0x01, 0x80, 0xEB, 0x47, 0xBA, 0x92, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };
    char icon[256];
    path(icon, sizeof icon, "game/icon.png");
    FILE *file = fopen(icon, "wb");
    REQUIRE(file && fwrite(png, 1, sizeof png, file) == sizeof png && !fclose(file));
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));

    /* The window shows the project name unless a title replaces it. */
    static const char NAME_LINE[] = "name \"Game\"\n";
    char manifest[256], name[CHIBI_APPLICATION_TEXT_MAX + 2],
        updated[2 * CHIBI_APPLICATION_TEXT_MAX];
    path(manifest, sizeof manifest, "game/project.kdl");
    char *text = tool_read(manifest);
    char *line = text ? strstr(text, NAME_LINE) : NULL;
    REQUIRE(line);
    memset(name, 'G', sizeof name - 1);
    name[sizeof name - 1] = '\0';
    *line = '\0';
    snprintf(updated, sizeof updated, "%sname \"%s\"\n%s", text, name, line + strlen(NAME_LINE));
    free(text);
    tool_write(manifest, updated);
    REQUIRE(validate() != 0 && errors() == 1);
    REQUIRE(strstr(first_message(result("project")), "set a shorter application title"));
    return true;
}

TEST_CASE(prototype_includes_resolve)
{
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file("game/content/prototypes/Hull.kdl", "name \"Hull\"\ncomponents {\n    GameHealth current=3 maximum=3\n}\n(array)behaviors \"Spin\"\n");
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"behavior\",\"name\":\"Spin\"}") == 0
    );
    write_file("game/content/prototypes/Drone.kdl", "name \"Drone\"\n(array)includes \"Hull\"\ncomponents {\n    GameTransform x=1 y=2\n}\n");
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Drone\" prototype=\"Drone\"\n}\n");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"Drone\"}") ==
        0
    );
    REQUIRE(yyjson_get_int(result("components")) == 2);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    /* The formatter keeps the include instead of folding Hull's components in. */
    const char *args[] = { program, "--project-root", "game", "--format", NULL };
    REQUIRE(tool_run(args, root, log_path) == 0);
    char drone[256];
    path(drone, sizeof drone, "game/content/prototypes/Drone.kdl");
    char *text = tool_read(drone);
    bool kept = text && strstr(text, "(array)includes \"Hull\"") &&
                !strstr(text, "GameHealth");
    free(text);
    REQUIRE(kept);
    /* The include is an edge from Drone to Hull, and only Drone's. Hull's
     * behavior stays Hull's reference. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"asset_dependencies\",\"kind\":\"prototype\",\"name\":\"Drone\"}") ==
        0
    );
    yyjson_val *edges = result("edges");
    REQUIRE(yyjson_arr_size(edges) == 1);
    yyjson_val *to = yyjson_obj_get(yyjson_arr_get_first(edges), "to");
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(to, "name")), "Hull"));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"asset_dependencies\",\"kind\":\"prototype\",\"name\":\"Hull\",\"direction\":\"incoming\"}") ==
        0
    );
    REQUIRE(yyjson_arr_size(result("edges")) == 1);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"asset_dependencies\",\"kind\":\"behavior\",\"name\":\"Spin\",\"direction\":\"incoming\"}") ==
        0
    );
    REQUIRE(yyjson_arr_size(result("edges")) == 1);
    /* One controller first finds Drone's include missing, then sees the Hull
     * it creates, so only its last reply is checked. */
    char hull[256];
    path(hull, sizeof hull, "game/content/prototypes/Hull.kdl");
    REQUIRE(unlink(hull) == 0);
    ctl(true,
        "{\"contract\":1,\"operation\":\"validate_project\"}\n"
        "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\","
        "\"name\":\"Hull\"}\n"
        "{\"contract\":1,\"operation\":\"validate_project\"}");
    REQUIRE(yyjson_get_bool(result("valid")));

    write_file("game/content/prototypes/Drone.kdl", "name \"Drone\"\n(array)includes \"Nope\"\n");
    REQUIRE(validate() != 0 && errors() == 2 && yyjson_arr_size(result("problems")) == 2);
    yyjson_val *problem;
    size_t index, count;
    yyjson_arr_foreach(result("problems"), index, count, problem)
    {
        const char *message = first_message(yyjson_obj_get(problem, "diagnostics"));
        REQUIRE(!strcmp(message, "prototype include Nope is unavailable"));
    }
    /* An include names a prototype, not its file. */
    write_file("game/content/prototypes/Drone.kdl", "name \"Drone\"\n(array)includes \"Hull.kdl\"\n");
    REQUIRE(validate() != 0 && errors() == 2);
    problem = yyjson_arr_get_first(result("problems"));
    const char *message = first_message(yyjson_obj_get(problem, "diagnostics"));
    REQUIRE(!strcmp(message, "prototype include Hull.kdl is unavailable"));
    return true;
}

TEST_CASE(scenarios_load_graph_assets)
{
    /* A scenario registers each graph asset once, in its one world. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"material\",\"name\":\"Glow\"}") == 0
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Dot\"}") == 0
    );
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Dot\" prototype=\"Dot\"\n}\n");
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    return true;
}

static void write_bytes(const char *relative, const unsigned char *bytes, size_t length)
{
    char file[256];
    path(file, sizeof file, relative);
    FILE *out = fopen(file, "wb");
    assert(out);
    size_t written = fwrite(bytes, 1, length, out);
    int closed = fclose(out);
    assert(written == length && closed == 0);
    (void)written, (void)closed;
}

/* What the scenario refuses with, or NULL when it runs. */
static const char *tone_refusal()
{
    if (ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0) {
        return NULL;
    }
    yyjson_val *error = yyjson_obj_get(yyjson_doc_get_root(reply), "error");
    return yyjson_get_str(yyjson_obj_get(error, "message"));
}

TEST_CASE(sounds_must_decode)
{
    /* Players decode an Ogg sound before they play it, so a scenario, like
     * preparation and validation, refuses one whose Vorbis audio they cannot
     * decode. */
    static const char refusal[] = "sounds/Tone.ogg: holds no Vorbis audio that "
                                  "players can decode";
    char fixture[4096];
    snprintf(fixture, sizeof fixture, "%s/test/fixtures/sounds/Tone.ogg", tool_root());
    FILE *in = fopen(fixture, "rb");
    assert(in);
    unsigned char tone[4096];
    size_t length = fread(tone, 1, sizeof tone, in);
    fclose(in);
    assert(length > OGG_FIRST_PAGE + 150 && length < sizeof tone);
    char folder[256];
    path(folder, sizeof folder, "game/content/sounds");
    REQUIRE(mkdir(folder, 0700) == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Dot\"}") == 0
    );
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Dot\" prototype=\"Dot\"\n}\n");
    write_bytes("game/content/sounds/Tone.ogg", tone, length);
    REQUIRE(!tone_refusal());
    REQUIRE(validate() == 0);

    /* Its first page, then one that ends the stream: whole pages, but no
     * comment or setup header and no audio. */
    unsigned char bare[OGG_FIRST_PAGE + OGG_PAGE_HEADER] = { 0 };
    memcpy(bare, tone, OGG_FIRST_PAGE);
    memcpy(bare + OGG_FIRST_PAGE, "OggS", 4);
    bare[OGG_FIRST_PAGE + 5] = OGG_END_OF_STREAM;
    write_bytes("game/content/sounds/Tone.ogg", bare, sizeof bare);
    const char *message = tone_refusal();
    REQUIRE(message && !strcmp(message, refusal));
    /* Validation lists it too, as a problem of the project, */
    REQUIRE(validate() != 0 && errors() == 1);
    message = first_message(result("project"));
    REQUIRE(message && !strcmp(message, refusal));
    /* even beside another asset's problems, which keep preparation from
     * reaching the sounds. */
    write_file("game/content/prototypes/Broken.kdl", "name \"Broken\"\n(array)components {\n");
    REQUIRE(validate() != 0 && yyjson_arr_size(result("problems")) == 1);
    message = first_message(result("project"));
    REQUIRE(message && !strcmp(message, refusal));
    char broken[256];
    path(broken, sizeof broken, "game/content/prototypes/Broken.kdl");
    REQUIRE(unlink(broken) == 0);

    /* Every header intact, but its one audio page, the last ~200 bytes,
     * garbled. */
    for (size_t i = length - 150; i < length; i++) {
        tone[i] ^= 0xFF;
    }
    write_bytes("game/content/sounds/Tone.ogg", tone, length);
    message = tone_refusal();
    REQUIRE(message && !strcmp(message, refusal));
    return true;
}

/* The smallest WAV players take: one silent 16-bit mono frame at 44.1 kHz. */
static const unsigned char SILENCE[] = {
    'R',  'I',  'F', 'F', 38,   0,    0,    0, 'W', 'A', 'V', 'E',
    'f',  'm',  't', ' ', 16,   0,    0,    0, 1,   0,   1,   0,
    0x44, 0xAC, 0,   0,   0x88, 0x58, 0x01, 0, 2,   0,   16,  0,
    'd',  'a',  't', 'a', 2,    0,    0,    0, 0,   0
};

static void write_silences(int first, int end)
{
    for (int i = first; i < end; i++) {
        char name[64];
        snprintf(name, sizeof name, "game/content/sounds/%04d.wav", i);
        write_bytes(name, SILENCE, sizeof SILENCE);
    }
}

TEST_CASE(resources_are_limited_together)
{
    /* Even a new project carries resources besides its sounds, so sounds
     * that fit the player's resource count on their own overflow it with
     * them, and compiling refuses the project. */
    char folder[256];
    path(folder, sizeof folder, "game/content/sounds");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_silences(0, 1);
    REQUIRE(validate() == 0);
    write_silences(1, PLAYER_MAX_ASSETS - 1);
    REQUIRE(validate() != 0 && errors() == 1);
    const char *message = first_message(result("project"));
    REQUIRE(message && strstr(message, "cannot be read within the resource limit"));
    return true;
}

/* Whether `text` is there and reads `expected`. */
static bool reads(const char *text, const char *expected)
{
    return text && !strcmp(text, expected);
}

/* Whether the catalog lists `member` among the members of the asset found
 * by `query`. */
static bool catalog_lists(const char *query, const char *member)
{
    char request[256];
    snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"search_assets\",\"query\":\"%s\"}", query);
    if (ctl(true, request) != 0) {
        return false;
    }
    yyjson_val *members = yyjson_obj_get(yyjson_arr_get(result("assets"), 0), "members");
    for (size_t i = 0; i < yyjson_arr_size(members); i++) {
        if (reads(yyjson_get_str(yyjson_obj_get(yyjson_arr_get(members, i), "name")), member)) {
            return true;
        }
    }
    return false;
}

/* Runs chibi_ctl --format on the project, with "--check" to only compare. */
static int format(const char *check)
{
    const char *args[] = { program, "--project-root", "game", "--format", check,
                           NULL };
    return tool_run(args, root, log_path);
}

TEST_CASE(prototypes_are_found_by_declared_name)
{
    /* The player finds a prototype by the name it declares, whatever its
     * file is called, and so does chibi_ctl. A file named after one
     * prototype may even declare another. */
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file("game/content/prototypes/hull_base.kdl", "name \"Hull\"\ncomponents {\n    GameHealth current=3 maximum=3\n}\n");
    write_file("game/content/prototypes/Hull.kdl", "name \"Drone\"\n(array)includes \"Hull\"\ncomponents {\n    GameTransform x=1 y=2\n}\n");
    /* Nor need a name be one a file could take. */
    write_file("game/content/prototypes/grunt.kdl", "name \"enemies/grunt\"\n(array)includes \"Hull\"\n");
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Hull\" prototype=\"Hull\"\n    - name=\"Drone\" prototype=\"Drone\"\n    - name=\"Grunt\" prototype=\"enemies/grunt\"\n}\n");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"Drone\"}") ==
        0
    );
    REQUIRE(yyjson_get_int(result("components")) == 2);
    REQUIRE(reads(yyjson_get_str(result("path")), "content/prototypes/Hull.kdl"));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"enemies/grunt\"}") ==
        0
    );
    REQUIRE(reads(yyjson_get_str(result("path")), "content/prototypes/grunt.kdl"));
    /* Or one longer than a file name can be. */
    char long_name[301], text[400], request[600];
    memset(long_name, 'L', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    snprintf(text, sizeof text, "name \"%s\"\n", long_name);
    write_file("game/content/prototypes/long.kdl", text);
    snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"%s\"}", long_name);
    REQUIRE(ctl(true, request) == 0 && reads(yyjson_get_str(result("name")), long_name));
    /* But not one longer than the player takes, which a shorter copy would
     * misname. */
    char huge[PROTOTYPE_NAME_MAX + 2], huge_text[PROTOTYPE_NAME_MAX + 16],
        huge_file[256];
    memset(huge, 'H', sizeof huge - 1);
    huge[sizeof huge - 1] = '\0';
    snprintf(huge_text, sizeof huge_text, "name \"%s\"\n", huge);
    write_file("game/content/prototypes/huge.kdl", huge_text);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"path\":\"content/prototypes/huge.kdl\"}") !=
        0
    );
    REQUIRE(reads(failure(), "content/prototypes/huge.kdl declares a name longer than 1024 bytes"));
    /* And a new prototype waits until every prototype can be read. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Wing\"}") != 0
    );
    REQUIRE(reads(failure_code(), "validation_failed"));
    path(huge_file, sizeof huge_file, "game/content/prototypes/huge.kdl");
    REQUIRE(unlink(huge_file) == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    REQUIRE(ctl(true, "{\"contract\":1,\"operation\":\"list_assets\"}") == 0);
    yyjson_val *asset;
    size_t index, count;
    int listed = 0;
    yyjson_arr_foreach(result("assets"), index, count, asset)
    {
        const char *name = yyjson_get_str(yyjson_obj_get(asset, "name"));
        listed += reads(yyjson_get_str(yyjson_obj_get(asset, "path")), "content/prototypes/hull_base.kdl") &&
                  reads(name, "Hull");
    }
    REQUIRE(listed == 1);

    /* A new prototype cannot take a name a file already declares. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Hull\"}") != 0
    );
    REQUIRE(reads(failure(), "content/prototypes/hull_base.kdl already declares the prototype 'Hull'"));
    /* Two that declare one name are refused, as the player refuses them. */
    write_file("game/content/prototypes/hull_copy.kdl", "name \"Hull\"\n");
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"Hull\"}") !=
        0
    );
    REQUIRE(failure() && strstr(failure(), "both declare the prototype 'Hull'"));
    REQUIRE(reads(failure_code(), "asset_conflict"));
    REQUIRE(validate() != 0);
    /* Even when one of them is the file named after it, which a lookup
     * reads first, the project is refused. */
    char copy[256];
    path(copy, sizeof copy, "game/content/prototypes/hull_copy.kdl");
    REQUIRE(unlink(copy) == 0);
    write_file("game/content/prototypes/Drone.kdl", "name \"Drone\"\n");
    REQUIRE(validate() != 0 && errors() == 1);
    /* A search that cannot read the folder, or a file in it, fails rather
     * than finding no declaration, since what it missed could declare the
     * name, and new_asset stops there too. Reads refuse a linked folder. */
    char linked[256];
    path(linked, sizeof linked, "game/content/linked");
    REQUIRE(rename(folder, linked) == 0 && symlink("linked", folder) == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"enemies/grunt\"}") !=
        0
    );
    REQUIRE(reads(failure(), "asset directory is not a directory: prototypes"));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Wing\"}") != 0
    );
    REQUIRE(reads(failure(), "asset directory is not a directory: prototypes"));
    /* Nor can it list a file in the folder's place. */
    REQUIRE(unlink(folder) == 0);
    write_file("game/content/prototypes", "");
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"enemies/grunt\"}") !=
        0
    );
    REQUIRE(reads(failure(), "asset directory is not a directory: prototypes"));
    return true;
}

TEST_CASE(manifest_content_folder_is_used)
{
    char from[256], to[256];
    path(from, sizeof from, "game/content");
    path(to, sizeof to, "game/assets");
    REQUIRE(rename(from, to) == 0);
    set_manifest("content", "assets");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    /* Only the content folder is read before a request, so the manifest's
     * other problems are still listed with the project's. */
    char kdl[256];
    path(kdl, sizeof kdl, "game/project.kdl");
    char *saved = tool_read(kdl);
    assert(saved);
    append_manifest("application {\n    fullscreen \"yes\"\n}\n");
    REQUIRE(validate() != 0 && errors() == 1);
    const char *listed = first_message(result("project"));
    REQUIRE(listed && strstr(listed, "fullscreen a boolean"));
    tool_write(kdl, saved);
    free(saved);
    /* A start scene's content/ path stays relative to the project root, as
     * earlier builds read it, so with the content in assets/ it is missing. */
    set_start_scene("content/scenes/Start.kdl");
    REQUIRE(validate() != 0 && errors() == 1);
    set_start_scene("scenes/Start.kdl");
    ctl(true, "{\"contract\":1,\"operation\":\"status\"}");
    REQUIRE(reads(yyjson_get_str(result("content_root")), "assets"));

    /* Requests and replies keep naming assets by their content/ paths. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Hero\"}") == 0
    );
    REQUIRE(reads(yyjson_get_str(result("path")), "content/prototypes/Hero.kdl"));
    char hero[256];
    path(hero, sizeof hero, "game/assets/prototypes/Hero.kdl");
    REQUIRE(access(hero, F_OK) == 0);
    write_file("game/assets/prototypes/Hero.kdl", "name \"Hero\"\ncomponents {\n  GameTransform x=1 y=2\n}\n");
    write_file("game/assets/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Hero\" prototype=\"Hero\"\n}\n");
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"path\":\"content/prototypes/Hero.kdl\"}") ==
        0
    );
    REQUIRE(yyjson_get_int(result("components")) == 1);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    /* The input map is formatted where the content is too. */
    write_file("game/assets/input.kdl", "Dash {\n  (array)sources \"Aim Stick\"\n}\n");
    REQUIRE(format("--check") != 0 && format(NULL) == 0 && format("--check") == 0);
    char map[256];
    path(map, sizeof map, "game/assets/input.kdl");
    char *formatted = tool_read(map);
    bool canonical = reads(formatted, "Dash {\n    (array)sources \"Aim Stick\"\n}\n");
    free(formatted);
    REQUIRE(canonical);

    char folder[256];
    path(folder, sizeof folder, "game/assets/ui");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file("game/assets/ui/Hud.rml", "<div class=\"hud\">Score</div>\n");
    ctl(true, "{\"contract\":1,\"operation\":\"find_references\",\"ui_class\":\"hud\"}");
    yyjson_val *use = yyjson_arr_get_first(result("uses"));
    REQUIRE(reads(yyjson_get_str(yyjson_obj_get(use, "path")), "content/ui/Hud.rml"));

    write_file("game/assets/prototypes/Broken.kdl", "name \"Broken\"\ncomponents {\n");
    REQUIRE(validate() != 0 && yyjson_arr_size(result("problems")) == 1);
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    REQUIRE(reads(yyjson_get_str(yyjson_obj_get(problem, "path")), "content/prototypes/Broken.kdl"));

    /* A project too old to open still names where its content is. */
    char manifest[256], old[1024];
    path(manifest, sizeof manifest, "game/project.kdl");
    char *text = tool_read(manifest);
    assert(text);
    snprintf(old, sizeof old, "version 1%s", strchr(text, '\n'));
    free(text);
    tool_write(manifest, old);
    ctl(true, "{\"contract\":1,\"operation\":\"status\"}");
    REQUIRE(reads(yyjson_get_str(result("content_root")), "assets"));
    return true;
}

TEST_CASE(scenes_run_under_their_own_name)
{
    /* A request may name a scene's file, but its scripts see the scene's
     * name, so reloading it restarts the scene that is running. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Dot\"}") == 0
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"behavior\",\"name\":\"Again\"}") == 0
    );
    write_file("game/content/behaviors/Again.chibi", "behavior \"Again\" {\n    on Tick {\n        reload_scene()\n    }\n}\n");
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Dot\" prototype=\"Dot\" {\n        (array)behaviors \"Again\"\n    }\n}\n");
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start.kdl\",\"frames\":2}") ==
        0
    );
    REQUIRE(reads(yyjson_get_str(result("path")), "content/scenes/Start.kdl"));

    /* Only a name that ends with the extension names the file. */
    char start[256], boss[256];
    path(start, sizeof start, "game/content/scenes/Start.kdl");
    path(boss, sizeof boss, "game/content/scenes/Boss.kdl.v2.kdl");
    REQUIRE(rename(start, boss) == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Boss.kdl.v2\",\"frames\":2}") ==
        0
    );
    REQUIRE(reads(yyjson_get_str(result("path")), "content/scenes/Boss.kdl.v2.kdl"));
    return true;
}

/* The x of the GlobalTransform2D on the snapshot entity named `name`, or on
 * the first unnamed one when `name` is NULL. */
static double global_x(const char *name)
{
    yyjson_val *entity, *component;
    size_t i, count, j, components;
    yyjson_arr_foreach(result("entities"), i, count, entity)
    {
        const char *named = yyjson_get_str(yyjson_obj_get(entity, "name"));
        if (name ? !reads(named, name) : named != NULL) {
            continue;
        }
        yyjson_arr_foreach(yyjson_obj_get(entity, "components"), j, components, component)
        {
            if (reads(yyjson_get_str(yyjson_obj_get(component, "name")), "GameGlobalTransform2D")) {
                yyjson_val *position = yyjson_obj_get(
                    yyjson_arr_get(yyjson_obj_get(component, "fields"), 0),
                    "value"
                );
                return yyjson_get_num(yyjson_obj_get(position, "x"));
            }
        }
    }
    return NAN;
}

TEST_CASE(scene_children_are_placed_by_their_parent)
{
    char folder[256], scene[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file("game/content/prototypes/Hull.kdl", "name \"Hull\"\ncomponents {\n    GameTransform x=0 y=0\n}\n");
    /* A root and a child may share a name; siblings may not. */
    write_file(
        "game/content/scenes/Start.kdl",
        "version 1\nname \"Start\"\n(array)entities {\n"
        "    - name=\"Ship\" prototype=\"Hull\" { components { GameTransform "
        "x=100 y=5; }; "
        "(array)children { - name=\"Turret\" prototype=\"Hull\" { components { "
        "GameTransform x=10 y=0; }; }; }; }\n"
        "    - name=\"Turret\" prototype=\"Hull\"\n}\n"
    );
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(format("--check") != 0 && format(NULL) == 0 && format("--check") == 0);
    path(scene, sizeof scene, "game/content/scenes/Start.kdl");
    char *text = tool_read(scene);
    bool nested = text &&
                  strstr(text, "        (array)children {\n            - {\n                name \"Turret\"");
    free(text);
    REQUIRE(nested);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"runtime_snapshot\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    REQUIRE(global_x("Ship.Turret") == 110 && global_x("Turret") == 0);
    /* The catalog lists a child by its path. */
    REQUIRE(catalog_lists("Start", "Ship.Turret"));
    write_file(
        "game/content/scenes/Start.kdl",
        "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Ship\" "
        "prototype=\"Hull\" { "
        "(array)children { - name=\"Turret\" prototype=\"Hull\"; - "
        "name=\"Turret\" prototype=\"Hull\"; }; }\n}\n"
    );
    REQUIRE(validate() != 0);
    return true;
}

/* A prototype's children come with each instance of it, placed by it. */
TEST_CASE(prototype_children_come_with_their_instance)
{
    char folder[256], ship[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file("game/content/prototypes/Hull.kdl", "name \"Hull\"\ncomponents {\n    GameTransform x=0 y=0\n}\n");
    write_file(
        "game/content/prototypes/Ship.kdl",
        "name \"Ship\"\ncomponents { GameTransform x=0 y=0; }\n"
        "(array)children { - name=\"Gun\" prototype=\"Hull\" { components { "
        "GameTransform x=4 y=0; }; }; }\n"
    );
    write_file(
        "game/content/scenes/Start.kdl",
        "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Flagship\" "
        "prototype=\"Ship\" { components { GameTransform x=100 y=0; }; }\n}\n"
    );
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(format(NULL) == 0 && format("--check") == 0);
    path(ship, sizeof ship, "game/content/prototypes/Ship.kdl");
    char *text = tool_read(ship);
    /* A prototype with children is formatted too: Transform's zero fields go. */
    bool nested = text &&
                  strstr(text, "(array)children {\n    - {\n        name \"Gun\"") &&
                  strstr(text, "(object)GAME.GameTransform");
    free(text);
    REQUIRE(nested);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"runtime_snapshot\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    /* Snapshots name only the scene's own objects. */
    REQUIRE(yyjson_arr_size(result("entities")) == 2);
    REQUIRE(global_x("Flagship") == 100 && global_x(NULL) == 104);
    REQUIRE(catalog_lists("Ship", "Gun"));
    /* A child is an edge to the prototype it uses. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"asset_dependencies\",\"kind\":\"prototype\",\"name\":\"Ship\"}") ==
        0
    );
    yyjson_val *edges = result("edges");
    yyjson_val *to = yyjson_obj_get(yyjson_arr_get_first(edges), "to");
    REQUIRE(
        yyjson_arr_size(edges) == 1 &&
        reads(yyjson_get_str(yyjson_obj_get(to, "name")), "Hull")
    );
    /* A child's own behaviors are checked with the scene that spawns it. */
    write_file(
        "game/content/prototypes/Ship.kdl",
        "name \"Ship\"\n(array)children { - name=\"Gun\" prototype=\"Hull\" { "
        "(array)behaviors \"Missing\"; }; }\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"scene\",\"name\":\"Start\"}") !=
        0
    );
    const char *missing = first_message(result("diagnostics"));
    REQUIRE(missing && strstr(missing, "Missing"));
    /* So are the bindings a child's overrides add. */
    write_file(
        "game/content/prototypes/Ship.kdl",
        "name \"Ship\"\n(array)children { - name=\"Gun\" prototype=\"Hull\" { "
        "components { GAME.GameUi document=\"Nowhere\"; }; }; }\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"Ship\"}") !=
        0
    );
    missing = first_message(result("diagnostics"));
    REQUIRE(missing && !strcmp(missing, "Unknown UI document 'Nowhere'"));
    write_file("game/content/prototypes/Ship.kdl", "name \"Ship\"\n(array)children { - name=\"Again\" prototype=\"Ship\"; }\n");
    REQUIRE(validate() != 0);
    return true;
}

/* Prototypes `prefix`0 to `prefix`<levels - 1>, each with two children that
 * use the next, so loading `prefix`0 reads 2^levels - 2 other documents. */
static void write_fans(const char *prefix, int levels)
{
    for (int i = 0; i < levels; i++) {
        char file[128], text[256];
        snprintf(file, sizeof file, "game/content/prototypes/%s%d.kdl", prefix, i);
        int n = snprintf(text, sizeof text, "name \"%s%d\"\n", prefix, i);
        if (i + 1 < levels) {
            snprintf(
                text + n,
                sizeof text - n,
                "(array)children { - name=\"L\" prototype=\"%s%d\"; - "
                "name=\"R\" "
                "prototype=\"%s%d\"; }\n",
                prefix,
                i + 1,
                prefix,
                i + 1
            );
        }
        write_file(file, text);
    }
}

TEST_CASE(prototype_expansion_is_shared)
{
    /* Each load stays under its own limit, but the project's loads together
     * read more than one load may, so preparing it stops. Fan0 to Fan10 read
     * 4083 documents, and each Pair's two Fan0 children read 4094 more. */
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_fans("Fan", 11);
    for (int i = 0; i < 4; i++) {
        char file[64], text[160];
        snprintf(file, sizeof file, "game/content/prototypes/Pair%d.kdl", i);
        snprintf(
            text,
            sizeof text,
            "name \"Pair%d\"\n(array)children { - name=\"L\" "
            "prototype=\"Fan0\"; "
            "- name=\"R\" prototype=\"Fan0\"; }\n",
            i
        );
        write_file(file, text);
    }
    REQUIRE(validate_expansion() != 0);
    REQUIRE(errors() == 1);
    const char *message = first_message(result("project"));
    REQUIRE(message && strstr(message, "through their includes and children"));
    return true;
}

TEST_CASE(scene_instances_share_the_expansion)
{
    /* Each instance loads its prototype again, so a scene's instances share
     * one budget too. An instance of Bush0 reads 512 documents: its
     * overlay, Bush0 and Bush0's 510. */
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_fans("Bush", 9);
    REQUIRE(validate() == 0);
    static char scene[GAME_PROTOTYPE_EXPANSION_MAX];
    int n = snprintf(scene, sizeof scene, "version 1\nname \"Start\"\n(array)entities {\n");
    for (int i = 0; i < GAME_PROTOTYPE_EXPANSION_MAX / 512; i++) {
        n += snprintf(scene + n, sizeof scene - n, "    - name=\"B%d\" prototype=\"Bush0\"\n", i);
    }
    snprintf(scene + n, sizeof scene - n, "}\n");
    write_file("game/content/scenes/Start.kdl", scene);
    REQUIRE(validate_expansion() != 0);
    const char *message = first_message(result("project"));
    REQUIRE(message && strstr(message, "through their includes and children"));
    return true;
}

/* The x a Tick wrote to the scene's only Transform in the last snapshot. */
static double snapshot_x()
{
    yyjson_val *entities = result("entities");
    for (size_t i = 0; i < yyjson_arr_size(entities); i++) {
        yyjson_val *components = yyjson_obj_get(yyjson_arr_get(entities, i), "components");
        for (size_t j = 0; j < yyjson_arr_size(components); j++) {
            yyjson_val *component = yyjson_arr_get(components, j);
            if (reads(yyjson_get_str(yyjson_obj_get(component, "name")), "GameTransform")) {
                yyjson_val *x = yyjson_arr_get(yyjson_obj_get(component, "fields"), 0);
                assert(reads(yyjson_get_str(yyjson_obj_get(x, "name")), "x"));
                return yyjson_get_num(yyjson_obj_get(x, "value"));
            }
        }
    }
    assert(false);
    return 0;
}

TEST_CASE(components_are_declared_in_chibi_sources)
{
    char folder[256], source[256];
    path(folder, sizeof folder, "game/content/components");
    REQUIRE(mkdir(folder, 0700) == 0);
    path(folder, sizeof folder, "game/content/systems");
    REQUIRE(mkdir(folder, 0700) == 0 || errno == EEXIST);
    write_file("game/content/components/Charge.chibi", "// Stored energy.\ncomponent Charge{level : 0 .. 100 charge\nrate:charge / s}\n");
    write_file("game/content/systems/Recharge.chibi", "system Recharge(charge: Charge) {\n    charge.level += charge.rate * dt\n}\n");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    /* chibi_ctl formats component sources with the rest of the content. */
    REQUIRE(format("--check") != 0 && format(NULL) == 0 && format("--check") == 0);
    path(source, sizeof source, "game/content/components/Charge.chibi");
    char *formatted = tool_read(source);
    bool canonical = reads(
        formatted,
        "// Stored energy.\n"
        "component Charge {\n"
        "    level: 0..100 charge\n"
        "    rate: charge/s\n"
        "}\n"
    );
    free(formatted);
    REQUIRE(canonical);
    /* Formatting never writes through a link out of the project. */
    char outside[256], link_path[256];
    path(outside, sizeof outside, "Outside.chibi");
    path(link_path, sizeof link_path, "game/content/components/Outside.chibi");
    const char *loose = "component Outside{x:float}\n";
    write_file("Outside.chibi", loose);
    REQUIRE(symlink(outside, link_path) == 0);
    format(NULL);
    char *kept = tool_read(outside);
    bool untouched = reads(kept, loose);
    free(kept);
    REQUIRE(untouched && unlink(link_path) == 0);
    /* A field's mistake is reported where it is. */
    write_file("game/content/components/Charge.chibi", "component Charge {\n    level: flaot\n}\n");
    REQUIRE(validate() != 0);
    REQUIRE(strstr(failure(), "Charge.chibi: 2:12: Unknown type or unit 'flaot'"));
    write_file("game/content/components/Charge.chibi", "component Charge {\n    level: float\n    job: task\n}\n");
    REQUIRE(validate() != 0);
    REQUIRE(strstr(failure(), "Charge.chibi: 3:5: A component field cannot hold a task"));
    /* A project may not declare a component the engine already has. */
    write_file("game/content/components/Charge.chibi", "component Charge {\n    level: float\n}\n");
    write_file("game/content/components/Transform.chibi", "component GameTransform {\n    x: float\n}\n");
    REQUIRE(validate() != 0);
    REQUIRE(strstr(failure(), "Transform.chibi: the name is already an engine type"));
    return true;
}

TEST_CASE(unit_mismatches_are_warnings)
{
    char folder[256];
    path(folder, sizeof folder, "game/content/components");
    REQUIRE(mkdir(folder, 0700) == 0 || errno == EEXIST);
    path(folder, sizeof folder, "game/content/systems");
    REQUIRE(mkdir(folder, 0700) == 0 || errno == EEXIST);
    write_file("game/content/components/Charge.chibi", "component Charge {\n    level: 0..100 charge\n    rate: charge/s\n}\n");
    /* Declared units, the engine's reflected units and dt's seconds. */
    write_file(
        "game/content/systems/Measure.chibi",
        "system Measure(charge: Charge, movement: GameMovement, collider: "
        "GameCollider) {\n"
        "    charge.level += charge.rate * dt\n"
        "    charge.level += charge.rate\n"
        "    collider.radius = movement.speed\n"
        "    collider.radius += delta\n"
        "}\n"
    );
    /* Warnings first: a mismatch leaves the project valid. */
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")) && errors() == 0);
    REQUIRE(yyjson_get_int(result("warnings")) == 3);
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    yyjson_val *diagnostics = yyjson_obj_get(problem, "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 3);
    yyjson_val *first = yyjson_arr_get(diagnostics, 0);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(first, "severity")), "warning"));
    REQUIRE(!strcmp(message_at(diagnostics, 0), "Units differ: charge += charge/s"));
    REQUIRE(yyjson_get_int(yyjson_obj_get(first, "line")) == 3);
    REQUIRE(!strcmp(message_at(diagnostics, 1), "Units differ: u = u/s"));
    REQUIRE(!strcmp(message_at(diagnostics, 2), "Units differ: u += s"));
    /* Validating the one source counts its warnings apart from errors. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"system\",\"name\":\"Measure\"}") ==
        0
    );
    REQUIRE(errors() == 0 && yyjson_get_int(result("warnings")) == 3);
    diagnostics = result("diagnostics");
    REQUIRE(yyjson_get_int(yyjson_obj_get(yyjson_arr_get(diagnostics, 0), "severity")) == 2);
    /* One chibi_ctl sees a unit change that leaves the layout as it was. */
    write_file("Charge.chibi", "component Charge {\n    level: 0..100 charge\n    rate: 0..10 charge\n}\n");
    write_file("request.json", "{\"contract\":1,\"operation\":\"validate_project\"}\n");
    REQUIRE(
        run("{ cat request.json; until grep -q contract \"$1\"; do sleep 0.02; "
            "done; "
            "cp Charge.chibi game/content/components/Charge.chibi; cat "
            "request.json; } | "
            "exec \"$0\" --project-root game") == 0
    );
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(!strcmp(message_at(diagnostics, 0), "Units differ: charge += charge·s"));
    return true;
}

TEST_CASE(scenarios_hold_input_actions)
{
    /* A scenario's controls reach the input map of the engine that plays it. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Dot\"}") == 0
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"behavior\",\"name\":\"Jumper\"}") == 0
    );
    write_file("game/content/input.kdl", "Jump {\n    (array)sources \"Mouse Left\"\n}\n");
    write_file("game/content/prototypes/Dot.kdl", "name \"Dot\"\ncomponents {\n    (object)GAME.GameTransform\n}\n");
    write_file("game/content/behaviors/Jumper.chibi", "behavior Jumper {\n    on Tick {\n        if input.Jump.held {\n            self.Transform = vec2(7, 0)\n        }\n    }\n}\n");
    write_file("game/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n    - name=\"Dot\" prototype=\"Dot\" {\n        (array)behaviors \"Jumper\"\n    }\n}\n");
    /* 1 is GAME_CONTROL_MOUSE_LEFT, the first GameControl. */
    const char *request = "{\"contract\":1,\"operation\":\"runtime_snapshot\","
                          "\"kind\":\"scene\",\"name\":\"Start\",\"frames\":2,"
                          "\"input_timeline\":[{\"frame\":0,\"input\":"
                          "{\"controls\":[%s]}}]}";
    char held[256], released[256];
    snprintf(held, sizeof held, request, "1");
    snprintf(released, sizeof released, request, "");
    REQUIRE(ctl(true, held) == 0 && snapshot_x() == 7);
    REQUIRE(ctl(true, released) == 0 && snapshot_x() == 0);
    return true;
}

void test_tool_validate_project(const char *binary)
{
    /* The requests run from the project's temporary folder. */
    bool found = realpath(binary, program);
    assert(found);
    (void)found;
    pu_setup(setup, teardown);
    RUN_TEST_CASE(new_project_is_valid);
    RUN_TEST_CASE(stopped_runs_are_reported);
    RUN_TEST_CASE(ui_style_problems_are_listed);
    RUN_TEST_CASE(start_scene_outside_scenes_is_parsed);
    RUN_TEST_CASE(cataloged_start_scene_is_reported_once);
    RUN_TEST_CASE(missing_ui_documents_are_reported);
    RUN_TEST_CASE(catalog_errors_are_reported_once);
    RUN_TEST_CASE(other_project_versions_are_not_opened);
    RUN_TEST_CASE(application_settings_are_checked);
    RUN_TEST_CASE(prototype_includes_resolve);
    RUN_TEST_CASE(prototypes_are_found_by_declared_name);
    RUN_TEST_CASE(scenarios_load_graph_assets);
    RUN_TEST_CASE(sounds_must_decode);
    RUN_TEST_CASE(resources_are_limited_together);
    RUN_TEST_CASE(manifest_content_folder_is_used);
    RUN_TEST_CASE(scenes_run_under_their_own_name);
    RUN_TEST_CASE(scene_children_are_placed_by_their_parent);
    RUN_TEST_CASE(prototype_children_come_with_their_instance);
    RUN_TEST_CASE(prototype_expansion_is_shared);
    RUN_TEST_CASE(scene_instances_share_the_expansion);
    RUN_TEST_CASE(scenarios_hold_input_actions);
    RUN_TEST_CASE(components_are_declared_in_chibi_sources);
    RUN_TEST_CASE(unit_mismatches_are_warnings);
    pu_clear_setup();
}
