#include "runtime/tilemap.h"
#include "support/asset_text.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define EVIDENCE "/tmp/chibi-publish-validation/evidence"
#define COLUMNS 64
#define ROWS 23
#define TILE_SIZE 8
#define OCCUPIED 230
#define BASELINE_COMMIT "56ca9d486b93c15a504bf293dc722c6202cd2bf5"

static av_doc *before, *room, *scene;
static tilemap_grid grid;
static char project[4096], program[4096];

static av_doc *read_asset(const char *path, size_t maximum)
{
    struct stat info;
    assert(!stat(path, &info) && S_ISREG(info.st_mode));
    assert(info.st_size > 0 && (uint64_t)info.st_size <= maximum);
    char *text = tool_read(path), error[256];
    assert(text);
    av_doc *document = asset_text_read(text, error, sizeof error);
    if (!document) {
        fprintf(stderr, "%s: %s\n", path, error);
    }
    free(text);
    return document;
}

static void setup()
{
    assert(!mkdir("/tmp/chibi-publish-validation", 0700) || errno == EEXIST);
    assert(!mkdir(EVIDENCE, 0700) || errno == EEXIST);
    int size = snprintf(project, sizeof project, "%s/../chibi-celeste", tool_root());
    assert(size > 0 && size < (int)sizeof project);
    size = snprintf(program, sizeof program, "%s/build/bin/chibi_ctl", tool_root());
    assert(size > 0 && size < (int)sizeof program);
}

static void teardown()
{
    av_doc_free(before);
    av_doc_free(room);
    av_doc_free(scene);
    before = room = scene = NULL;
    tilemap_grid_free(&grid);
}

TEST_CASE(authored_grid_matches_previous_scene)
{
    const char *arguments[] = { "git",
                                "-C",
                                project,
                                "show",
                                BASELINE_COMMIT ":content/scenes/Level.kdl",
                                NULL };
    REQUIRE(tool_run(arguments, tool_root(), EVIDENCE "/celeste-before-Level.kdl") == 0);
    tool_write(EVIDENCE "/tilemap-project-baseline.sha", BASELINE_COMMIT "\n");
    before = read_asset(EVIDENCE "/celeste-before-Level.kdl", 64 * 1024);
    REQUIRE(!unlink(EVIDENCE "/celeste-before-Level.kdl"));
    char path[4096];
    int size = snprintf(path, sizeof path, "%s/content/prototypes/Room.kdl", project);
    REQUIRE(size > 0 && size < (int)sizeof path);
    room = read_asset(path, 16 * 1024);
    size = snprintf(path, sizeof path, "%s/content/scenes/Level.kdl", project);
    REQUIRE(size > 0 && size < (int)sizeof path);
    scene = read_asset(path, 4 * 1024);
    REQUIRE(before && room && scene);
    const av_value *components = av_obj_get(av_doc_root(room), "components");
    const av_value *map = av_obj_get(components, "Tilemap");
    REQUIRE(av_is_obj(map));
    const av_value *origin = av_obj_get(components, "Transform");
    REQUIRE(av_is_obj(origin));
    REQUIRE(av_get_num(av_obj_get(origin, "x")) == 0);
    REQUIRE(av_get_num(av_obj_get(origin, "y")) == 0);
    const char *image = av_get_str(av_obj_get(map, "image"));
    REQUIRE(image && !strcmp(image, "content/images/tiles.png"));
    REQUIRE(av_get_int(av_obj_get(map, "layer")) == 1);
    REQUIRE(av_get_int(av_obj_get(map, "columns")) == COLUMNS);
    REQUIRE(av_get_int(av_obj_get(map, "rows")) == ROWS);
    REQUIRE(av_get_int(av_obj_get(map, "frame_width")) == TILE_SIZE);
    REQUIRE(av_get_int(av_obj_get(map, "frame_height")) == TILE_SIZE);
    REQUIRE(
        tilemap_grid_update(&grid, av_get_str(av_obj_get(map, "cells")), COLUMNS, ROWS, TILE_SIZE, TILE_SIZE)
    );
    int expected[COLUMNS * ROWS];
    for (int i = 0; i < COLUMNS * ROWS; i++) {
        expected[i] = -1;
    }
    const av_value *entities = av_obj_get(av_doc_root(before), "entities");
    REQUIRE(av_arr_size(entities) == OCCUPIED + 4);
    int count = 0;
    for (size_t i = 0; i < av_arr_size(entities); i++) {
        const av_value *entity = av_arr_get(entities, i);
        const char *prototype = av_get_str(av_obj_get(entity, "prototype"));
        REQUIRE(prototype);
        if (strcmp(prototype, "Tile")) {
            continue;
        }
        components = av_obj_get(entity, "components");
        const av_value *at = av_obj_get(components, "Transform");
        REQUIRE(av_is_num(av_obj_get(at, "x")) && av_is_num(av_obj_get(at, "y")));
        double x = av_get_num(av_obj_get(at, "x")),
               y = av_get_num(av_obj_get(at, "y"));
        REQUIRE(x >= TILE_SIZE / 2 && x < COLUMNS * TILE_SIZE);
        REQUIRE(y >= TILE_SIZE / 2 && y < ROWS * TILE_SIZE);
        int column = (int)((x - TILE_SIZE / 2) / TILE_SIZE);
        int row = (int)((y - TILE_SIZE / 2) / TILE_SIZE);
        REQUIRE(column >= 0 && column < COLUMNS && row >= 0 && row < ROWS);
        REQUIRE(x == column * TILE_SIZE + TILE_SIZE / 2);
        REQUIRE(y == row * TILE_SIZE + TILE_SIZE / 2);
        REQUIRE(expected[row * COLUMNS + column] == -1);
        const av_value *sprite = av_obj_get(components, "Sprite");
        REQUIRE(av_is_num(av_obj_get(sprite, "frame")));
        int frame = av_get_int(av_obj_get(sprite, "frame"));
        REQUIRE(frame >= 0);
        expected[row * COLUMNS + column] = frame;
        count++;
    }
    REQUIRE(count == OCCUPIED);
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLUMNS; x++) {
            REQUIRE(tilemap_grid_frame(&grid, x, y) == expected[y * COLUMNS + x]);
        }
    }
    entities = av_obj_get(av_doc_root(scene), "entities");
    REQUIRE(av_arr_size(entities) == 5);
    count = 0;
    unsigned matched = 0;
    for (size_t i = 0; i < av_arr_size(entities); i++) {
        const av_value *entity = av_arr_get(entities, i);
        const char *prototype = av_get_str(av_obj_get(entity, "prototype"));
        REQUIRE(prototype && strcmp(prototype, "Tile"));
        if (!strcmp(prototype, "Room")) {
            REQUIRE(!av_obj_get(entity, "components"));
            count++;
            continue;
        }
        bool same = false;
        const av_value *old_entities = av_obj_get(av_doc_root(before), "entities");
        for (size_t j = 0; j < 4; j++) {
            if (av_equals(entity, av_arr_get(old_entities, j))) {
                REQUIRE(!(matched & (1u << j)));
                matched |= 1u << j;
                same = true;
                break;
            }
        }
        REQUIRE(same);
    }
    REQUIRE(count == 1);
    REQUIRE(matched == 0xf);
    return true;
}

static yyjson_doc *request(const char *name, const char *text)
{
    char input[4096], log[4096];
    snprintf(input, sizeof input, EVIDENCE "/tilemap-project-%s.request.json", name);
    snprintf(log, sizeof log, EVIDENCE "/tilemap-project-%s.log", name);
    tool_write(input, text);
    const char *arguments[] = {
        "/bin/sh", "-c",    "exec \"$0\" --project-root \"$1\" < \"$2\"",
        program,   project, input,
        NULL
    };
    int status = tool_run(arguments, tool_root(), log);
    assert(!unlink(input));
    if (status) {
        return NULL;
    }
    char *output = tool_read(log);
    const char *start = output ? strstr(output, "{\"contract\"") : NULL;
    yyjson_doc *reply = start ? yyjson_read(start, strlen(start), YYJSON_READ_STOP_WHEN_DONE)
                              : NULL;
    free(output);
    return reply;
}

TEST_CASE(current_project_compiles_and_runs)
{
    yyjson_doc *reply = request("validate", "{\"contract\":1,\"operation\":\"validate_project\"}\n");
    REQUIRE(reply);
    yyjson_val *root = yyjson_doc_get_root(reply);
    bool valid = yyjson_get_bool(yyjson_obj_get(root, "ok"));
    yyjson_val *result = yyjson_obj_get(root, "result");
    valid &= yyjson_get_bool(yyjson_obj_get(result, "valid"));
    valid &= yyjson_get_int(yyjson_obj_get(result, "errors")) == 0;
    yyjson_doc_free(reply);
    REQUIRE(valid);
    const char *artifact = EVIDENCE "/tilemap-project.chibip";
    const char *arguments[] = { program,  "--project-root",
                                project,  "--prepare-project",
                                artifact, NULL };
    int status = tool_run(arguments, tool_root(), EVIDENCE "/tilemap-project-prepare.log");
    REQUIRE(status == 0);
    REQUIRE(!unlink(artifact));
    REQUIRE(!unlink(EVIDENCE "/tilemap-project.chibip.resources.json"));
    reply = request("scenario", "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Level\",\"frames\":2}\n");
    REQUIRE(reply);
    root = yyjson_doc_get_root(reply);
    valid = yyjson_get_bool(yyjson_obj_get(root, "ok"));
    result = yyjson_obj_get(root, "result");
    valid &= yyjson_get_int(yyjson_obj_get(result, "frames")) == 2;
    valid &= yyjson_get_int(yyjson_obj_get(result, "authored_entities")) == 5;
    valid &= yyjson_get_int(yyjson_obj_get(result, "live_entities")) == 15;
    yyjson_val *failures = yyjson_obj_get(result, "behavior_failures");
    valid &= yyjson_is_arr(failures) && yyjson_arr_size(failures) == 0;
    yyjson_doc_free(reply);
    REQUIRE(valid);
    return true;
}

void test_tool_tilemap_project()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(authored_grid_matches_previous_scene);
    RUN_TEST_CASE(current_project_compiles_and_runs);
    pu_setup(NULL, NULL);
}
