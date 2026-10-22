#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_REPLIES 16

static const char *program;
static char scratch[128], project[160], input_path[160], log_path[160],
    save_path[160];
static yyjson_doc *replies[MAX_REPLIES];
static int reply_count, request_count;
static FILE *requests;
static yyjson_mut_doc *request;
static yyjson_mut_val *input;
static char *scene, *prototype;

static void at(char *path, size_t size, const char *relative)
{
    int n = snprintf(path, size, "%s/%s", project, relative);
    assert(n > 0 && n < (int)size);
}

static void write_asset(const char *relative, const char *text)
{
    char path[512];
    at(path, sizeof path, relative);
    tool_write(path, text);
}

static yyjson_val *get(yyjson_val *value, const char *key)
{
    return yyjson_obj_get(value, key);
}

static yyjson_val *reply(int index)
{
    assert(index >= 0 && index < reply_count);
    return yyjson_doc_get_root(replies[index]);
}

static yyjson_val *result(int index)
{
    return get(reply(index), "result");
}

static bool ok(int index)
{
    return yyjson_get_bool(get(reply(index), "ok"));
}

static bool string_is(yyjson_val *value, const char *expected)
{
    const char *text = yyjson_get_str(value);
    return text && !strcmp(text, expected);
}

static void clear_replies()
{
    for (int i = 0; i < reply_count; i++) {
        yyjson_doc_free(replies[i]);
    }
    reply_count = 0;
}

static void begin(const char *operation, const char *entity, const char *identity, const char *namespace)
{
    assert(!request);
    if (!requests) {
        requests = fopen(input_path, "wb");
        assert(requests);
        request_count = 0;
    }
    request = yyjson_mut_doc_new(NULL);
    input = yyjson_mut_obj(request);
    yyjson_mut_doc_set_root(request, input);
    yyjson_mut_obj_add_int(request, input, "contract", 1);
    yyjson_mut_obj_add_str(request, input, "operation", operation);
    yyjson_mut_obj_add_str(request, input, "name", "SeamStudy");
    if (entity) {
        yyjson_mut_obj_add_str(request, input, "entity", entity);
    }
    if (identity) {
        yyjson_mut_obj_add_str(request, input, "identity", identity);
    }
    if (namespace) {
        yyjson_mut_obj_add_str(request, input, "save_path", namespace);
    }
}

static void send()
{
    char *json = yyjson_mut_write(request, 0, NULL);
    assert(json && request_count < MAX_REPLIES);
    assert(fprintf(requests, "%s\n", json) > 0);
    free(json);
    yyjson_mut_doc_free(request);
    request = NULL;
    request_count++;
}

static int run()
{
    assert(requests && !request);
    assert(!fclose(requests));
    requests = NULL;
    clear_replies();
    const char *args[] = {
        "/bin/sh", "-c",    "exec \"$0\" --project-root \"$1\" < \"$2\"",
        program,   project, input_path,
        NULL
    };
    int status = tool_run(args, scratch, log_path);
    char *output = tool_read(log_path);
    assert(output);
    for (const char *line = output; *line;) {
        size_t length = strcspn(line, "\n");
        if (!strncmp(line, "{\"contract\"", 11)) {
            assert(reply_count < MAX_REPLIES);
            replies[reply_count] = yyjson_read(line, length, 0);
            assert(replies[reply_count++]);
        }
        line += length + (line[length] == '\n');
    }
    if (reply_count != request_count) {
        fputs(output, stderr);
    }
    free(output);
    assert(reply_count == request_count);
    return status;
}

static void setup()
{
    strcpy(scratch, "/tmp/chibi-terrain-storage-XXXXXX");
    assert(mkdtemp(scratch));
    snprintf(project, sizeof project, "%s/project", scratch);
    snprintf(input_path, sizeof input_path, "%s/requests.jsonl", scratch);
    snprintf(log_path, sizeof log_path, "%s/output.log", scratch);
    snprintf(save_path, sizeof save_path, "%s/save.json", scratch);
    char fixture[1024];
    snprintf(fixture, sizeof fixture, "%s/test/fixtures/environment-authoring", tool_root());
#ifdef __APPLE__
    const char *args[] = { "/bin/cp", "-cR", fixture, project, NULL };
#else
    const char *args[] = { "/bin/cp", "--reflink=auto", "-R",
                           fixture,   project,          NULL };
#endif
    assert(tool_run(args, scratch, log_path) == 0);
    char path[512];
    at(path, sizeof path, "content/scenes/SeamStudy.chibi");
    scene = tool_read(path);
    at(path, sizeof path, "content/prototypes/SeamTerrain.chibi");
    prototype = tool_read(path);
    assert(scene && prototype);
    tool_write(save_path, "{\"existing\":\"kept\"}\n");
}

static void cleanup()
{
    if (requests) {
        fclose(requests);
        requests = NULL;
    }
    yyjson_mut_doc_free(request);
    request = NULL;
    clear_replies();
    free(scene);
    free(prototype);
    scene = prototype = NULL;
    if (*scratch) {
        tool_remove_tree(scratch);
    }
    *scratch = 0;
}

static void replace_asset(const char *relative, const char *source, const char *before, const char *after)
{
    const char *where = strstr(source, before);
    assert(where);
    size_t capacity = strlen(source) + strlen(after) + 1;
    char *text = malloc(capacity);
    assert(text);
    size_t prefix = (size_t)(where - source);
    memcpy(text, source, prefix);
    strcpy(text + prefix, after);
    strcat(text, where + strlen(before));
    write_asset(relative, text);
    free(text);
}

static void cut_scene(bool cut)
{
    if (!cut) {
        write_asset("content/scenes/SeamStudy.chibi", scene);
        return;
    }
    /* Reuse the checked-in CPU cut behavior and slab scene. No game policy. */
    replace_asset(
        "content/scenes/SeamStudy.chibi",
        scene,
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Cut "
        "slab\"))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Cut "
        "slab\")),\n"
        "            data.Field.Named(\"behaviors\", data.List([\n"
        "                data.Value.Text(\"CutMaterial\")\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
}

static void attachment(char *path, size_t size, const char *identity)
{
    static const char hex[] = "0123456789abcdef";
    char encoded[129];
    size_t length = strlen(identity);
    assert(length <= 64);
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)identity[i];
        encoded[2 * i] = hex[byte >> 4];
        encoded[2 * i + 1] = hex[byte & 15];
    }
    encoded[2 * length] = 0;
    int n = snprintf(path, size, "%s.blob-%s.bin", save_path, encoded);
    assert(n > 0 && n < (int)size);
}

static unsigned char *read_bytes(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long bytes = ftell(file);
    assert(bytes > 0 && !fseek(file, 0, SEEK_SET));
    *length = (size_t)bytes;
    unsigned char *data = malloc(*length);
    assert(data && fread(data, 1, *length, file) == *length);
    assert(!fclose(file));
    return data;
}

static void write_bytes(const char *path, const unsigned char *data, size_t length)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, length, file) == length);
    assert(!fclose(file));
}

static bool unchanged(const char *path, const unsigned char *expected, size_t length)
{
    size_t actual_length;
    unsigned char *actual = read_bytes(path, &actual_length);
    bool same = actual_length == length && !memcmp(actual, expected, length);
    free(actual);
    return same;
}

static const char *snapshot(int index)
{
    return yyjson_get_str(get(get(result(index), "terrain"), "snapshot_hash"));
}

static bool completed(int index)
{
    yyjson_val *completion = get(result(index), "completion");
    REQUIRE(ok(index));
    REQUIRE(yyjson_get_int(get(completion, "token")) > 0);
    REQUIRE(!yyjson_get_bool(get(completion, "pending")));
    REQUIRE(yyjson_get_bool(get(completion, "succeeded")));
    REQUIRE(string_is(get(completion, "status"), "succeeded"));
    REQUIRE(yyjson_get_bool(get(result(index), "physics_ready")));
    REQUIRE(yyjson_get_int(get(get(result(index), "materials"), "cells")) > 0);
    REQUIRE(snapshot(index) && strlen(snapshot(index)) == 16);
    REQUIRE(yyjson_get_uint(get(get(result(index), "terrain"), "snapshot_bytes")) > 0);
    return true;
}

TEST_CASE(terrain_storage_controller_contract_requires_explicit_namespace)
{
    begin("describe_contract", NULL, NULL, NULL);
    send();
    begin("save_terrain", "Cut slab", "slot", NULL);
    send();
    begin("save_terrain", "Cut slab", "slot", "relative.json");
    send();
    begin("save_terrain", "Cut slab", "slot", save_path);
    yyjson_mut_obj_add_int(request, input, "frames", 121);
    send();
    begin("save_terrain", "Cut slab", "slot", save_path);
    yyjson_mut_obj_add_obj(request, input, "saved_state");
    send();
    char too_long[66];
    memset(too_long, 'x', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = 0;
    begin("save_terrain", "Cut slab", too_long, save_path);
    send();
    begin("record_scenario", NULL, NULL, save_path);
    send();
    begin("replay_scenario", NULL, NULL, save_path);
    send();
    begin("replay_scenario", NULL, NULL, NULL);
    yyjson_mut_val *recording = yyjson_mut_obj_add_obj(request, input, "recording");
    yyjson_mut_obj_add_str(request, recording, "save_path", save_path);
    send();
    begin("run_scenario", NULL, NULL, save_path);
    yyjson_mut_obj_add_str(request, input, "kind", "scene");
    yyjson_mut_obj_add_obj(request, input, "saved_state");
    send();
    REQUIRE(run() != 0);
    REQUIRE(ok(0));
    yyjson_val *operations = get(result(0), "operations");
    int found = 0;
    for (size_t i = 0; i < yyjson_arr_size(operations); i++) {
        yyjson_val *operation = yyjson_arr_get(operations, i);
        if (!string_is(get(operation, "name"), "save_terrain") &&
            !string_is(get(operation, "name"), "load_terrain")) {
            continue;
        }
        REQUIRE(!yyjson_get_bool(get(operation, "read_only")));
        yyjson_val *fields = get(operation, "request_fields");
        int required = 0;
        for (size_t n = 0; n < yyjson_arr_size(fields); n++) {
            yyjson_val *field = yyjson_arr_get(fields, n);
            required += yyjson_get_bool(get(field, "required"));
        }
        REQUIRE(required == 4);
        found++;
    }
    REQUIRE(found == 2);
    for (int i = 1; i < reply_count; i++) {
        REQUIRE(!ok(i));
        REQUIRE(string_is(get(get(reply(i), "error"), "code"), "invalid_request"));
    }
    char blob[512];
    attachment(blob, sizeof blob, "slot");
    REQUIRE(access(blob, F_OK) != 0);
    char *text = tool_read(save_path);
    REQUIRE(text && !strcmp(text, "{\"existing\":\"kept\"}\n"));
    free(text);
    return true;
}

TEST_CASE(terrain_storage_controller_restores_cut_after_process_restart)
{
    begin("save_terrain", "Cut slab", "slot", save_path);
    send();
    REQUIRE(run() == 0 && completed(0));
    char original[17];
    strcpy(original, snapshot(0));
    cut_scene(true);
    begin("save_terrain", "Cut slab", "slot", save_path);
    yyjson_mut_obj_add_int(request, input, "frames", 1);
    send();
    REQUIRE(run() == 0 && completed(0));
    char cut[17];
    strcpy(cut, snapshot(0));
    REQUIRE(strcmp(original, cut));
    char blob[512];
    attachment(blob, sizeof blob, "slot");
    struct stat info;
    REQUIRE(!stat(blob, &info) && S_ISREG(info.st_mode) && info.st_size > 0);
    size_t length;
    unsigned char *saved = read_bytes(blob, &length);
    cut_scene(false);
    /* Every run starts a new controller process and a fresh world. */
    begin("load_terrain", "Cut slab", "slot", save_path);
    send();
    REQUIRE(run() == 0 && completed(0));
    REQUIRE(!strcmp(cut, snapshot(0)));
    REQUIRE(strcmp(original, snapshot(0)));
    REQUIRE(unchanged(blob, saved, length));
    begin("save_terrain", "Cut slab", "slot", save_path);
    send();
    REQUIRE(run() == 0 && completed(0));
    REQUIRE(!strcmp(original, snapshot(0)));
    char *text = tool_read(save_path);
    REQUIRE(text && !strcmp(text, "{\"existing\":\"kept\"}\n"));
    free(text);
    free(saved);
    return true;
}

TEST_CASE(terrain_storage_controller_rejects_invalid_loads_without_replacing_terrain)
{
    begin("save_terrain", "Cut slab", "slot", save_path);
    send();
    REQUIRE(run() == 0 && completed(0));
    char original[17];
    strcpy(original, snapshot(0));
    char material_hash[17];
    strcpy(material_hash, yyjson_get_str(get(get(result(0), "materials"), "hash")));
    int cells = yyjson_get_int(get(get(result(0), "materials"), "cells"));
    char blob[512], wrong[512];
    attachment(blob, sizeof blob, "slot");
    attachment(wrong, sizeof wrong, "other");
    size_t length;
    unsigned char *saved = read_bytes(blob, &length);
    write_bytes(wrong, saved, length);
    begin("load_terrain", "Cut slab", "missing", save_path);
    send();
    begin("load_terrain", "Cut slab", "other", save_path);
    send();
    begin("load_terrain", "Absent terrain", "slot", save_path);
    send();
    REQUIRE(run() != 0);
    for (int i = 0; i < 2; i++) {
        REQUIRE(!ok(i));
        REQUIRE(!yyjson_get_bool(get(get(result(i), "completion"), "succeeded")));
        REQUIRE(yyjson_get_bool(get(result(i), "physics_ready")));
        REQUIRE(string_is(get(get(result(i), "materials"), "hash"), material_hash));
        REQUIRE(yyjson_get_int(get(get(result(i), "materials"), "cells")) == cells);
    }
    REQUIRE(!ok(2));
    REQUIRE(unchanged(blob, saved, length) && unchanged(wrong, saved, length));
    char unavailable[256];
    snprintf(unavailable, sizeof unavailable, "%s/absent/save.json", scratch);
    begin("save_terrain", "Cut slab", "slot", unavailable);
    send();
    REQUIRE(run() != 0 && !ok(0));
    REQUIRE(!yyjson_get_bool(get(get(result(0), "completion"), "succeeded")));
    REQUIRE(unchanged(blob, saved, length));
    REQUIRE(access(unavailable, F_OK) != 0);
    saved[length - 1] ^= 1;
    write_bytes(blob, saved, length);
    begin("load_terrain", "Cut slab", "slot", save_path);
    send();
    REQUIRE(run() != 0 && !ok(0));
    REQUIRE(!strcmp(original, snapshot(0)));
    REQUIRE(unchanged(blob, saved, length));
    saved[length - 1] ^= 1;
    write_bytes(blob, saved, length);
    replace_asset("content/prototypes/SeamTerrain.chibi", prototype, "seed=7", "seed=8");
    begin("load_terrain", "Cut slab", "slot", save_path);
    send();
    REQUIRE(run() != 0 && !ok(0));
    REQUIRE(!yyjson_get_bool(get(get(result(0), "completion"), "succeeded")));
    REQUIRE(unchanged(blob, saved, length));
    free(saved);
    return true;
}

void test_terrain_storage_commands(const char *controller)
{
    program = controller;
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(terrain_storage_controller_contract_requires_explicit_namespace);
    RUN_TEST_CASE(terrain_storage_controller_restores_cut_after_process_restart);
    RUN_TEST_CASE(terrain_storage_controller_rejects_invalid_loads_without_replacing_terrain);
    pu_clear_setup();
}
