#include "runtime/player_prepared.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* Sends requests, one per line, to one chibi_ctl from `root`, inside the
 * project "game" when `scoped`; returns its exit status and keeps the last
 * reply. */
static int ctl(bool scoped, const char *request)
{
    write_file("request.json", request);
    const char *command = scoped
                            ? "exec \"$0\" --project-root game < request.json"
                            : "exec \"$0\" < request.json";
    const char *args[] = { "/bin/sh", "-c", command, program, NULL };
    int status = tool_run(args, root, log_path);
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

static int validate()
{
    return ctl(true, "{\"contract\":1,\"operation\":\"validate_project\"}");
}

static yyjson_val *result(const char *key)
{
    return yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(reply), "result"), key);
}

static int errors()
{
    return (int)yyjson_get_int(result("errors"));
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

TEST_CASE(old_projects_are_not_upgraded)
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
    char manifest[256], name[GAME_PLAYER_APPLICATION_TEXT_MAX + 2],
        updated[2 * GAME_PLAYER_APPLICATION_TEXT_MAX];
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
    REQUIRE(!strcmp(message, "prototype include Hull.kdl is named Hull"));
    return true;
}

/* Whether `text` is there and reads `expected`. */
static bool reads(const char *text, const char *expected)
{
    return text && !strcmp(text, expected);
}

/* Runs chibi_ctl --format on the project, with "--check" to only compare. */
static int format(const char *check)
{
    const char *args[] = { program, "--project-root", "game", "--format", check,
                           NULL };
    return tool_run(args, root, log_path);
}

TEST_CASE(manifest_content_folder_is_used)
{
    char from[256], to[256];
    path(from, sizeof from, "game/content");
    path(to, sizeof to, "game/assets");
    REQUIRE(rename(from, to) == 0);
    set_manifest("content", "assets");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    /* A start scene named by its content/ path is in the folder too. */
    set_start_scene("content/scenes/Start.kdl");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
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
    REQUIRE(format("--check") != 0 && format(NULL) == 0 && format("--check") == 0);

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

void test_tool_validate_project(const char *binary)
{
    /* The requests run from the project's temporary folder. */
    bool found = realpath(binary, program);
    assert(found);
    (void)found;
    pu_setup(setup, teardown);
    RUN_TEST_CASE(new_project_is_valid);
    RUN_TEST_CASE(ui_style_problems_are_listed);
    RUN_TEST_CASE(start_scene_outside_scenes_is_parsed);
    RUN_TEST_CASE(cataloged_start_scene_is_reported_once);
    RUN_TEST_CASE(missing_ui_documents_are_reported);
    RUN_TEST_CASE(catalog_errors_are_reported_once);
    RUN_TEST_CASE(old_projects_are_not_upgraded);
    RUN_TEST_CASE(application_settings_are_checked);
    RUN_TEST_CASE(prototype_includes_resolve);
    RUN_TEST_CASE(manifest_content_folder_is_used);
    pu_clear_setup();
}
