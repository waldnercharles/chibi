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

/* Sends one request to chibi_ctl from `root`, inside the project "game" when
 * `scoped`; returns its exit status and keeps its reply. */
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

/* The message of the first diagnostic in `diagnostics`. */
static const char *first_message(yyjson_val *diagnostics)
{
    return yyjson_get_str(yyjson_obj_get(yyjson_arr_get_first(diagnostics), "message"));
}

static void set_start_scene(const char *scene)
{
    char manifest[256];
    path(manifest, sizeof manifest, "game/project.kdl");
    char *text = tool_read(manifest);
    char *line = text ? strstr(text, "start_scene ") : NULL;
    assert(line);
    char *rest = strchr(line, '\n');
    *line = '\0';
    char updated[1024];
    snprintf(updated, sizeof updated, "%sstart_scene \"%s\"%s", text, scene, rest ? rest : "\n");
    free(text);
    tool_write(manifest, updated);
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
    REQUIRE(validate() != 0 && errors() == 2);
    REQUIRE(!strcmp(first_message(result("project")), "Unknown UI document 'Hud'"));
    REQUIRE(rmdir(hud) == 0);
    write_file("game/content/ui/Hud.rml", "<div>Score</div>\n");
    write_file("game/content/ui/Pause.rml", "<div>Paused</div>\n");
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
     * preparation, refuses one whose Vorbis audio they cannot decode. */
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

    /* Its first page, then one that ends the stream: whole pages, but no
     * comment or setup header and no audio. */
    unsigned char bare[OGG_FIRST_PAGE + OGG_PAGE_HEADER] = { 0 };
    memcpy(bare, tone, OGG_FIRST_PAGE);
    memcpy(bare + OGG_FIRST_PAGE, "OggS", 4);
    bare[OGG_FIRST_PAGE + 5] = OGG_END_OF_STREAM;
    write_bytes("game/content/sounds/Tone.ogg", bare, sizeof bare);
    const char *message = tone_refusal();
    REQUIRE(message && !strcmp(message, refusal));

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
    RUN_TEST_CASE(scenarios_load_graph_assets);
    RUN_TEST_CASE(sounds_must_decode);
    pu_clear_setup();
}
