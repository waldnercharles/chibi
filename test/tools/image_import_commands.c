#include "assets/project.h"
#include "authoring/image_import_commit.h"
#include "authoring/image_prepare.h"
#include "runtime/player.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_REPLIES 12
static const char *program;
static char root[128], source[4096], log_path[160], input_path[160];
static yyjson_doc *replies[MAX_REPLIES];
static int reply_count, request_count;
static FILE *requests;
static yyjson_mut_doc *request;
static yyjson_mut_val *input;

static void path(char *out, size_t size, const char *relative)
{
    int n = snprintf(out, size, "%s/%s", root, relative);
    assert(n > 0 && n < (int)size);
}
static void write_at(const char *relative, const char *text)
{
    char file[512];
    path(file, sizeof file, relative);
    tool_write(file, text);
}
static void png(const char *filename, int width, int height, unsigned char value)
{
    size_t length = (size_t)width * height * 4;
    unsigned char *pixels = malloc(length);
    assert(pixels);
    for (size_t i = 0; i < length; i += 4) {
        pixels[i] = value;
        pixels[i + 1] = value / 2;
        pixels[i + 2] = 255 - value;
        pixels[i + 3] = 255;
    }
    shader_image_view image = { pixels, width, height };
    unsigned char *bytes = NULL;
    size_t count = 0;
    char error[256];
    assert(image_prepare_png_encode(&image, &bytes, &count, error, sizeof error));
    free(pixels);
    FILE *file = fopen(filename, "wb");
    assert(file && fwrite(bytes, 1, count, file) == count && !fclose(file));
    free(bytes);
}
static yyjson_val *get(yyjson_val *value, const char *key)
{
    return yyjson_obj_get(value, key);
}
static yyjson_val *reply(int i)
{
    assert(i >= 0 && i < reply_count);
    return yyjson_doc_get_root(replies[i]);
}
static yyjson_val *result(int i)
{
    return get(reply(i), "result");
}
static bool ok(int i)
{
    return yyjson_get_bool(get(reply(i), "ok"));
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
static void begin(const char *operation, const char *destination, const char *hash)
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
    yyjson_mut_obj_add_str(request, input, "source", source);
    yyjson_mut_obj_add_str(request, input, "destination", destination);
    if (hash) {
        yyjson_mut_obj_add_str(request, input, "plan_hash", hash);
    }
}
static void send()
{
    char *text = yyjson_mut_write(request, 0, NULL);
    assert(text && request_count < MAX_REPLIES);
    assert(fprintf(requests, "%s\n", text) > 0);
    free(text);
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
        "/bin/sh", "-c", "exec \"$0\" --project-root \"$1\" < \"$2\"",
        program,   root, input_path,
        NULL
    };
    int status = tool_run(args, root, log_path);
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
    strcpy(root, "/tmp/chibi-image-import-XXXXXX");
    assert(mkdtemp(root));
    char *resolved = realpath(root, NULL);
    assert(resolved && strlen(resolved) < sizeof root);
    strcpy(root, resolved);
    free(resolved);
    path(source, sizeof source, "original.png");
    path(log_path, sizeof log_path, "output.log");
    path(input_path, sizeof input_path, "requests.jsonl");
    const char *dirs[] = { "content", "content/scenes", "content/materials", "content/images" };
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) {
        char directory[256];
        path(directory, sizeof directory, dirs[i]);
        assert(!mkdir(directory, 0700));
    }
    char manifest[256];
    snprintf(manifest, sizeof manifest, "version %d\nname \"Image import proof\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n", CHIBI_PROJECT_VERSION);
    write_at("project.kdl", manifest);
    write_at("content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {}\n");
    png(source, 8, 4, 120);
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
    if (*root) {
        tool_remove_tree(root);
    }
    *root = 0;
}
static void copy_hash(char hash[17], yyjson_val *value)
{
    const char *text = yyjson_get_str(value);
    assert(text && strlen(text) == 16);
    strcpy(hash, text);
}
static unsigned char *bytes_read(const char *file, size_t *length)
{
    FILE *stream = fopen(file, "rb");
    assert(stream && !fseek(stream, 0, SEEK_END));
    long count = ftell(stream);
    assert(count > 0 && !fseek(stream, 0, SEEK_SET));
    *length = (size_t)count;
    unsigned char *bytes = malloc(*length);
    assert(bytes && fread(bytes, 1, *length, stream) == *length && !fclose(stream));
    return bytes;
}
static bool unchanged(const char *file, const unsigned char *expected, size_t length)
{
    size_t count;
    unsigned char *bytes = bytes_read(file, &count);
    bool same = count == length && !memcmp(bytes, expected, length);
    free(bytes);
    return same;
}
static void image_graph(int count)
{
    for (int i = 0; i < count; i++) {
        char file[256], relative[64];
        snprintf(relative, sizeof relative, "content/materials/Images%d.kdl", i);
        path(file, sizeof file, relative);
        FILE *out = fopen(file, "wb");
        assert(out);
        fprintf(out, "version 3\nname \"Images%d\"\ndomain \"material\"\nid_counter 1000\n(array)graphs {\n - {\n kind \"event\"\n name \"\"\n (array)nodes {\n - id=1 type=\"Shader Surface Output\"\n", i);
        fprintf(out, " - {\n id 100\n type \"Shader Image Texture\"\n inputs {\n Image \"content/images/Resident%d.png\"\n \"Color Space\" 1\n }\n }\n", i);
        fputs(" }\n (array)edges {\n - from=\"100.Color\" to=\"1.Base Color\"\n }\n }\n}\n", out);
        assert(!fclose(out));
    }
}

TEST_CASE(image_import_plan_is_read_only_and_apply_is_exclusive)
{
    const char *destination = "content/textures/new/Derivative.png";
    begin("plan_image_import", destination, NULL);
    send();
    REQUIRE(run() == 0 && ok(0));
    REQUIRE(!yyjson_get_bool(get(result(0), "applied")));
    char hash[17], output_hash[17];
    copy_hash(hash, get(result(0), "plan_hash"));
    copy_hash(output_hash, get(get(result(0), "output_image"), "hash"));
    yyjson_val *image = get(result(0), "output_image");
    REQUIRE(yyjson_get_int(get(image, "width")) == 8);
    REQUIRE(yyjson_get_int(get(image, "height")) == 4);
    REQUIRE(yyjson_get_uint(get(image, "pixel_bytes")) == 8 * 4 * 4);
    REQUIRE(yyjson_get_uint(get(image, "prepared_bytes")) > 8 * 4 * 4);
    yyjson_val *budget = get(result(0), "visual_budget");
    REQUIRE(yyjson_get_bool(get(budget, "fits")));
    REQUIRE(yyjson_get_uint(get(budget, "current_bytes")) > 0);
    REQUIRE(
        yyjson_get_uint(get(budget, "proposed_bytes")) >
        yyjson_get_uint(get(image, "prepared_bytes"))
    );
    char file[256], directory[256];
    path(file, sizeof file, destination);
    path(directory, sizeof directory, "content/textures");
    REQUIRE(access(file, F_OK) != 0 && access(directory, F_OK) != 0);
    size_t length;
    unsigned char *original = bytes_read(source, &length);
    begin("import_image", destination, hash);
    send();
    REQUIRE(run() == 0 && ok(0));
    REQUIRE(yyjson_get_bool(get(result(0), "applied")));
    REQUIRE(string_is(get(get(result(0), "output_image"), "hash"), output_hash));
    REQUIRE(!access(file, F_OK) && unchanged(source, original, length));
    size_t saved_length;
    unsigned char *saved = bytes_read(file, &saved_length);
    begin("import_image", destination, hash);
    send();
    REQUIRE(run() != 0 && !ok(0));
    REQUIRE(unchanged(file, saved, saved_length) && unchanged(source, original, length));
    free(saved);
    free(original);
    return true;
}

TEST_CASE(image_import_rejects_stale_sources_catalogs_and_unsafe_paths)
{
    const char *destination = "content/images/Derivative.png";
    char resident[256];
    path(resident, sizeof resident, "content/images/Resident0.png");
    png(resident, 8, 4, 20);
    image_graph(1);
    begin("plan_image_import", destination, NULL);
    send();
    REQUIRE(run() == 0 && ok(0));
    char hash[17];
    copy_hash(hash, get(result(0), "plan_hash"));
    size_t current_bytes = yyjson_get_uint(
        get(get(result(0), "visual_budget"), "current_bytes")
    );
    png(resident, 8, 4, 200);
    begin("import_image", destination, hash);
    send();
    REQUIRE(run() != 0 && !ok(0));
    REQUIRE(string_is(get(get(reply(0), "error"), "code"), "asset_conflict"));
    REQUIRE(yyjson_get_uint(get(get(result(0), "visual_budget"), "current_bytes")) == current_bytes);
    copy_hash(hash, get(result(0), "plan_hash"));
    png(source, 8, 4, 60);
    begin("import_image", destination, hash);
    send();
    REQUIRE(run() != 0 && !ok(0));
    REQUIRE(string_is(get(get(reply(0), "error"), "code"), "asset_conflict"));
    char escape[256], outside[256];
    path(escape, sizeof escape, "content/escape");
    path(outside, sizeof outside, "outside");
    REQUIRE(!mkdir(outside, 0700) && !symlink(outside, escape));
    begin("plan_image_import", "content/../Escape.png", NULL);
    send();
    begin("plan_image_import", "content/escape/Escape.png", NULL);
    send();
    begin("plan_image_import", "/tmp/Escape.png", NULL);
    send();
    begin("import_image", destination, NULL);
    send();
#ifndef _WIN32
    char fifo[256];
    path(fifo, sizeof fifo, "source.fifo");
    REQUIRE(!mkfifo(fifo, 0600));
    char *png_source = strdup(source);
    strcpy(source, fifo);
    begin("plan_image_import", destination, NULL);
    send();
    strcpy(source, png_source);
    free(png_source);
#endif
    REQUIRE(run() != 0);
    for (int i = 0; i < reply_count; i++) {
        REQUIRE(!ok(i));
    }
    char file[256];
    path(file, sizeof file, destination);
    REQUIRE(access(file, F_OK) != 0);
    path(file, sizeof file, "outside/Escape.png");
    REQUIRE(access(file, F_OK) != 0);
    return true;
}

TEST_CASE(image_import_admission_includes_existing_prepared_visual_collection)
{
    for (int i = 0; i < 3; i++) {
        char file[256], relative[64];
        snprintf(relative, sizeof relative, "content/images/Resident%d.png", i);
        path(file, sizeof file, relative);
        png(file, 2048, 2048, (unsigned char)(40 + i * 40));
    }
    image_graph(3);
    png(source, 2048, 2048, 180);
    const char *destination = "content/images/TooMany.png";
    begin("plan_image_import", destination, NULL);
    yyjson_mut_obj_add_str(request, input, "size", "2k");
    yyjson_mut_obj_add_str(request, input, "semantic", "scalar");
    send();
    REQUIRE(run() == 0 && ok(0));
    yyjson_val *budget = get(result(0), "visual_budget");
    REQUIRE(yyjson_get_uint(get(budget, "current_pixel_bytes")) == 3u * 2048u * 2048u * 4u);
    REQUIRE(yyjson_get_uint(get(budget, "proposed_pixel_bytes")) == 64u * 1024u * 1024u);
    REQUIRE(yyjson_get_uint(get(budget, "proposed_bytes")) > 64u * 1024u * 1024u);
    REQUIRE(!yyjson_get_bool(get(budget, "fits")));
    REQUIRE(yyjson_get_uint(get(budget, "limit_bytes")) == 64u * 1024u * 1024u);
    char hash[17];
    copy_hash(hash, get(result(0), "plan_hash"));
    begin("import_image", destination, hash);
    yyjson_mut_obj_add_str(request, input, "size", "2k");
    yyjson_mut_obj_add_str(request, input, "semantic", "scalar");
    send();
    REQUIRE(run() != 0 && !ok(0));
    REQUIRE(!yyjson_get_bool(get(result(0), "applied")));
    char file[256];
    path(file, sizeof file, destination);
    REQUIRE(access(file, F_OK) != 0);
    return true;
}

static char texture_content[160];
static image_import_file_plan texture_plans[2];
static image_import_texture texture_records[2];
static unsigned char *retained_texture;
static size_t retained_texture_length;
static const char *texture_key = "content/images/Managed.png";
static char texture_error[256];

static bool live_texture_measure(
    void *context,
    const image_import_proposal *proposals,
    int count,
    authoring_visual_budget *budget,
    char *error,
    size_t size
)
{
    (void)context;
    return authoring_visual_measure(NULL, 0, proposals, count, budget, error, size);
}
static image_import_budget_host live_texture_host = { .measure = live_texture_measure };

static void texture_setup()
{
    setup();
    path(texture_content, sizeof texture_content, "content");
    render_image_catalog_clear();
    assert(render_image_mount_content(texture_content, texture_error, sizeof texture_error));
}
static void texture_cleanup()
{
    for (int i = 0; i < 2; i++) {
        image_import_file_plan_free(&texture_plans[i]);
        image_import_texture_free(&texture_records[i]);
    }
    free(retained_texture);
    retained_texture = NULL;
    retained_texture_length = 0;
    render_image_catalog_clear();
    cleanup();
}
static bool import_texture(image_import_file_options options)
{
    REQUIRE(image_import_file_prepare(
        texture_content,
        source,
        texture_key,
        options,
        live_texture_host,
        &texture_plans[0],
        texture_error,
        sizeof texture_error
    ));
    REQUIRE(
        image_import_file_commit(texture_content, &texture_plans[0], live_texture_host, texture_error, sizeof texture_error)
    );
    return true;
}
static void retain_texture()
{
    char file[256];
    path(file, sizeof file, texture_key);
    free(retained_texture);
    retained_texture = bytes_read(file, &retained_texture_length);
}
static bool texture_unchanged()
{
    char file[256];
    path(file, sizeof file, texture_key);
    return unchanged(file, retained_texture, retained_texture_length);
}

TEST_CASE(image_import_texture_settings_roundtrip_and_stale_save_guard)
{
    REQUIRE(import_texture((image_import_file_options){ 1024, IMAGE_IMPORT_COLOR }));
    retain_texture();
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[0], texture_error, sizeof texture_error)
    );
    REQUIRE(!strcmp(texture_records[0].source, "original.png"));
    REQUIRE(texture_records[0].options.max_side == 1024);
    REQUIRE(texture_records[0].options.semantic == IMAGE_IMPORT_COLOR);
    uint64_t first_hash = texture_records[0].hash;
    REQUIRE(first_hash != 0);
    texture_records[0].options = (image_import_file_options){ 2048, IMAGE_IMPORT_NORMAL };
    REQUIRE(
        image_import_texture_save(texture_content, texture_key, &texture_records[0], first_hash, texture_error, sizeof texture_error)
    );
    REQUIRE(texture_unchanged());
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[1], texture_error, sizeof texture_error)
    );
    REQUIRE(!strcmp(texture_records[1].source, "original.png"));
    REQUIRE(texture_records[1].options.max_side == 2048);
    REQUIRE(texture_records[1].options.semantic == IMAGE_IMPORT_NORMAL);
    REQUIRE(texture_records[1].hash != first_hash);
    uint64_t saved_hash = texture_records[1].hash;
    REQUIRE(!image_import_texture_save(
        texture_content,
        texture_key,
        &texture_records[0],
        first_hash,
        texture_error,
        sizeof texture_error
    ));
    image_import_texture_free(&texture_records[0]);
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[0], texture_error, sizeof texture_error)
    );
    REQUIRE(texture_records[0].hash == saved_hash);
    REQUIRE(texture_records[0].options.semantic == IMAGE_IMPORT_NORMAL);
    REQUIRE(texture_unchanged());
    char *text = image_import_texture_serialize(&texture_records[0], texture_error, sizeof texture_error);
    REQUIRE(text);
    image_import_texture_free(&texture_records[1]);
    bool parsed = image_import_texture_parse(text, &texture_records[1], texture_error, sizeof texture_error);
    free(text);
    REQUIRE(parsed);
    REQUIRE(!strcmp(texture_records[1].source, "original.png"));
    REQUIRE(texture_records[1].options.max_side == 2048);
    REQUIRE(texture_records[1].options.semantic == IMAGE_IMPORT_NORMAL);
    return true;
}

TEST_CASE(image_import_reimport_rejects_source_changes_after_review)
{
    REQUIRE(import_texture((image_import_file_options){ 1024, IMAGE_IMPORT_SCALAR }));
    retain_texture();
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[0], texture_error, sizeof texture_error)
    );
    png(source, 8, 4, 40);
    REQUIRE(image_import_file_reimport_prepare(
        texture_content,
        texture_key,
        live_texture_host,
        &texture_plans[1],
        texture_error,
        sizeof texture_error
    ));
    REQUIRE(texture_plans[1].reimport);
    png(source, 8, 4, 50);
    REQUIRE(
        !image_import_file_commit(texture_content, &texture_plans[1], live_texture_host, texture_error, sizeof texture_error)
    );
    REQUIRE(texture_unchanged());
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[1], texture_error, sizeof texture_error)
    );
    REQUIRE(texture_records[1].hash == texture_records[0].hash);
    return true;
}

TEST_CASE(image_import_reimport_keeps_key_and_replaces_budget_bytes)
{
    png(source, 32, 16, 40);
    REQUIRE(import_texture((image_import_file_options){ 1024, IMAGE_IMPORT_SCALAR }));
    REQUIRE(render_image_reload(texture_key, texture_error, sizeof texture_error));
    REQUIRE(render_image_source_count() == 1);
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[0], texture_error, sizeof texture_error)
    );
    uint64_t old_output = texture_plans[0].image.output_hash;
    size_t old_prepared = texture_plans[0].image.prepared_bytes;
    png(source, 48, 24, 180);
    REQUIRE(image_import_file_reimport_prepare(
        texture_content,
        texture_key,
        live_texture_host,
        &texture_plans[1],
        texture_error,
        sizeof texture_error
    ));
    const image_import_file_plan *review = &texture_plans[1];
    REQUIRE(review->reimport && !strcmp(review->destination, texture_key));
    REQUIRE(review->image.output_hash != old_output);
    REQUIRE(review->budget.current[PLAYER_ASSET_IMAGE].count == 1);
    REQUIRE(review->budget.proposed[PLAYER_ASSET_IMAGE].count == 1);
    REQUIRE(review->budget.current_pixel_bytes == 32u * 16u * 4u);
    REQUIRE(review->budget.proposed_pixel_bytes == 48u * 24u * 4u);
    REQUIRE(review->budget.current[PLAYER_ASSET_IMAGE].bytes == old_prepared);
    REQUIRE(review->budget.proposed[PLAYER_ASSET_IMAGE].bytes == review->image.prepared_bytes);
    REQUIRE(
        review->budget.proposed_bytes ==
        review->budget.current_bytes - old_prepared + review->image.prepared_bytes
    );
    REQUIRE(review->budget.fits);
    REQUIRE(
        image_import_file_commit(texture_content, review, live_texture_host, texture_error, sizeof texture_error)
    );
    char file[256];
    path(file, sizeof file, texture_key);
    REQUIRE(unchanged(file, review->image.png, review->image.png_bytes));
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[1], texture_error, sizeof texture_error)
    );
    REQUIRE(texture_records[1].hash == texture_records[0].hash);
    REQUIRE(render_image_reload(texture_key, texture_error, sizeof texture_error));
    int width, height;
    REQUIRE(render_image_info(texture_key, &width, &height));
    REQUIRE(width == 48 && height == 24);
    REQUIRE(render_image_source_count() == 1);
    return true;
}

TEST_CASE(image_import_reimport_preserves_tampered_destination)
{
    REQUIRE(import_texture((image_import_file_options){ 1024, IMAGE_IMPORT_SCALAR }));
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[0], texture_error, sizeof texture_error)
    );
    png(source, 8, 4, 180);
    REQUIRE(image_import_file_reimport_prepare(
        texture_content,
        texture_key,
        live_texture_host,
        &texture_plans[1],
        texture_error,
        sizeof texture_error
    ));
    char file[256];
    path(file, sizeof file, texture_key);
    png(file, 8, 4, 220);
    retain_texture();
    REQUIRE(
        !image_import_file_commit(texture_content, &texture_plans[1], live_texture_host, texture_error, sizeof texture_error)
    );
    REQUIRE(texture_unchanged());
    REQUIRE(
        image_import_texture_read(texture_content, texture_key, &texture_records[1], texture_error, sizeof texture_error)
    );
    REQUIRE(texture_records[1].hash == texture_records[0].hash);
#ifndef _WIN32
    REQUIRE(!unlink(file) && !symlink(source, file));
    REQUIRE(
        !image_import_file_commit(texture_content, &texture_plans[1], live_texture_host, texture_error, sizeof texture_error)
    );
    struct stat info;
    REQUIRE(!lstat(file, &info) && S_ISLNK(info.st_mode));
#endif
    return true;
}

void test_image_import_commands(const char *controller)
{
    program = controller;
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(image_import_plan_is_read_only_and_apply_is_exclusive);
    RUN_TEST_CASE(image_import_rejects_stale_sources_catalogs_and_unsafe_paths);
    RUN_TEST_CASE(image_import_admission_includes_existing_prepared_visual_collection);
    pu_setup(texture_setup, texture_cleanup);
    RUN_TEST_CASE(image_import_texture_settings_roundtrip_and_stale_save_guard);
    RUN_TEST_CASE(image_import_reimport_rejects_source_changes_after_review);
    RUN_TEST_CASE(image_import_reimport_keeps_key_and_replaces_budget_bytes);
    RUN_TEST_CASE(image_import_reimport_preserves_tampered_destination);
    pu_clear_setup();
}

static const char *grey_rock_original;
static unsigned char *grey_source_bytes, *grey_output_bytes, *grey_prepared_bytes;
static shader_image_view grey_pixels;

static void grey_rock_cleanup()
{
    free(grey_source_bytes);
    free(grey_output_bytes);
    free(grey_prepared_bytes);
    free((void *)grey_pixels.pixels);
    grey_source_bytes = grey_output_bytes = grey_prepared_bytes = NULL;
    grey_pixels = (shader_image_view){ 0 };
    cleanup();
}

TEST_CASE(image_import_grey_rock_original_keeps_aspect_and_source)
{
    REQUIRE(grey_rock_original && grey_rock_original[0] == '/');
    REQUIRE(strlen(grey_rock_original) < sizeof source);
    strcpy(source, grey_rock_original);
    size_t source_length;
    grey_source_bytes = bytes_read(source, &source_length);
    char error[256];
    const char *format = image_prepare_import_decode(
        grey_source_bytes,
        source_length,
        &grey_pixels,
        error,
        sizeof error
    );
    if (!format) {
        fprintf(stderr, "Grey Rock original decode: %s\n", error);
    }
    REQUIRE(format && !strcmp(format, "png"));
    REQUIRE(grey_pixels.width == 3911 && grey_pixels.height == 4096);
    free((void *)grey_pixels.pixels);
    grey_pixels = (shader_image_view){ 0 };
    char source_hash[17];
    snprintf(
        source_hash,
        sizeof source_hash,
        "%016llx",
        (unsigned long long)player_asset_fingerprint(grey_source_bytes, source_length)
    );
    const char *destination = "content/textures/GreyRock01_BaseColor_1k.png";
    begin("plan_image_import", destination, NULL);
    yyjson_mut_obj_add_str(request, input, "size", "1k");
    yyjson_mut_obj_add_str(request, input, "semantic", "color");
    send();
    REQUIRE(run() == 0 && ok(0));
    REQUIRE(!yyjson_get_bool(get(result(0), "applied")));
    yyjson_val *original = get(result(0), "source_image");
    REQUIRE(yyjson_get_int(get(original, "width")) == 3911);
    REQUIRE(yyjson_get_int(get(original, "height")) == 4096);
    REQUIRE(yyjson_get_uint(get(original, "encoded_bytes")) == source_length);
    REQUIRE(string_is(get(original, "hash"), source_hash));
    yyjson_val *derived = get(result(0), "output_image");
    REQUIRE(yyjson_get_int(get(derived, "width")) == 978);
    REQUIRE(yyjson_get_int(get(derived, "height")) == 1024);
    REQUIRE(yyjson_get_uint(get(derived, "pixel_bytes")) == 978u * 1024u * 4u);
    size_t expected_prepared = yyjson_get_uint(get(derived, "prepared_bytes"));
    REQUIRE(expected_prepared > 978u * 1024u * 4u);
    REQUIRE(yyjson_get_bool(get(get(result(0), "visual_budget"), "fits")));
    char plan_hash[17], output_hash[17];
    copy_hash(plan_hash, get(result(0), "plan_hash"));
    copy_hash(output_hash, get(derived, "hash"));
    char output[256];
    path(output, sizeof output, destination);
    REQUIRE(access(output, F_OK) != 0);
    REQUIRE(unchanged(source, grey_source_bytes, source_length));
    begin("import_image", destination, plan_hash);
    yyjson_mut_obj_add_str(request, input, "size", "1k");
    yyjson_mut_obj_add_str(request, input, "semantic", "color");
    send();
    REQUIRE(run() == 0 && ok(0));
    REQUIRE(yyjson_get_bool(get(result(0), "applied")));
    REQUIRE(string_is(get(get(result(0), "source_image"), "hash"), source_hash));
    REQUIRE(string_is(get(get(result(0), "output_image"), "hash"), output_hash));
    size_t output_length;
    grey_output_bytes = bytes_read(output, &output_length);
    char actual_hash[17];
    snprintf(
        actual_hash,
        sizeof actual_hash,
        "%016llx",
        (unsigned long long)player_asset_fingerprint(grey_output_bytes, output_length)
    );
    REQUIRE(!strcmp(actual_hash, output_hash));
    REQUIRE(
        image_prepare_decode(grey_output_bytes, output_length, &grey_pixels, error, sizeof error)
    );
    REQUIRE(grey_pixels.width == 978 && grey_pixels.height == 1024);
    size_t prepared_length = 0;
    shader_error diagnostic = { 0 };
    REQUIRE(
        render_image_pixels_prepared_write(&grey_pixels, &grey_prepared_bytes, &prepared_length, &diagnostic)
    );
    REQUIRE(prepared_length == expected_prepared);
    REQUIRE(unchanged(source, grey_source_bytes, source_length));
    return true;
}

void test_image_import_grey_rock(const char *controller, const char *original)
{
    program = controller;
    grey_rock_original = original;
    pu_setup(setup, grey_rock_cleanup);
    RUN_TEST_CASE(image_import_grey_rock_original_keeps_aspect_and_source);
    pu_clear_setup();
}
