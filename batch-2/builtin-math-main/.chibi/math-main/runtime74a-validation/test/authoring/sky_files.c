#include "authoring/image_prepare.h"
#include "runtime/images/catalog.h"
#include "runtime/images/sky.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Radiance .hdr skies decoded from bytes, prepared from mounted content and
 * imported into a content folder, all inside one temporary directory. */
enum
{
    FILE_MAX = 4096
};
static char directory[] = "/tmp/chibi-sky-XXXXXX";
static unsigned char file[FILE_MAX];
static size_t file_length;
/* What the cases may create below `directory`, children before parents. */
static const char *const created[] = { "download.hdr",
                                       "broken.hdr",
                                       "content/skies/field.hdr",
                                       "content/skies/download.hdr",
                                       "content/skies/broken.hdr",
                                       "content/skies/new/field.hdr",
                                       "content/skies/new/broken.hdr",
                                       "content/skies/new",
                                       "content/skies",
                                       "content" };

static void inside(char *path, size_t size, const char *name)
{
    int n = snprintf(path, size, "%s/%s", directory, name);
    assert(n > 0 && (size_t)n < size);
}

static void setup()
{
    strcpy(directory, "/tmp/chibi-sky-XXXXXX");
    assert(mkdtemp(directory));
    char path[256];
    inside(path, sizeof path, "content");
    assert(!mkdir(path, 0700));
    inside(path, sizeof path, "content/skies");
    assert(!mkdir(path, 0700));
}

static void teardown()
{
    render_skies_clear();
    render_image_catalog_clear();
    for (size_t i = 0; i < sizeof created / sizeof *created; i++) {
        char path[256];
        inside(path, sizeof path, created[i]);
        remove(path);
    }
    assert(!rmdir(directory));
}

static void begin(const char *header)
{
    file_length = strlen(header);
    assert(file_length <= FILE_MAX);
    memcpy(file, header, file_length);
}

static void append(const void *bytes, size_t length)
{
    assert(file_length + length <= FILE_MAX);
    memcpy(file + file_length, bytes, length);
    file_length += length;
}

/* An 8 x 4 sky in flat rows; each texel's bytes name its column and row. */
static void flat_sky()
{
    begin("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\nEXPOSURE=1.0\n\n-Y 4 +X 8\n");
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 8; x++) {
            append((unsigned char[4]){ x * 16, y * 32, 7, 120 + y }, 4);
        }
    }
}

static bool flat_texels(const render_sky *sky)
{
    REQUIRE(sky->width == 8 && sky->height == 4);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 8; x++) {
            const unsigned char *t = sky->texels + (y * 8 + x) * 4;
            REQUIRE(t[0] == x * 16 && t[1] == y * 32 && t[2] == 7 && t[3] == 120 + y);
        }
    }
    return true;
}

/* The texel byte that run_length_sky writes for `channel` at (x, y). */
static int run_length_byte(int x, int y, int channel)
{
    return x < 9 ? channel * 10 + y : x * 3 + channel;
}

/* A 16 x 8 sky whose rows hold, per channel, a run of nine then seven
 * literal bytes. */
static void run_length_sky()
{
    begin("#?RGBE\n\n-Y 8 +X 16\n");
    for (int y = 0; y < 8; y++) {
        append((unsigned char[4]){ 2, 2, 0, 16 }, 4);
        for (int channel = 0; channel < 4; channel++) {
            append((unsigned char[2]){ 128 + 9, run_length_byte(0, y, channel) }, 2);
            unsigned char literal[8] = { 7 };
            for (int x = 9; x < 16; x++) {
                literal[x - 8] = run_length_byte(x, y, channel);
            }
            append(literal, sizeof literal);
        }
    }
}

static bool run_length_texels(const render_sky *sky)
{
    REQUIRE(sky->width == 16 && sky->height == 8);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 16; x++) {
            for (int channel = 0; channel < 4; channel++) {
                REQUIRE(
                    sky->texels[(y * 16 + x) * 4 + channel] ==
                    run_length_byte(x, y, channel)
                );
            }
        }
    }
    return true;
}

static void write_file(const char *name)
{
    char path[256];
    inside(path, sizeof path, name);
    FILE *out = fopen(path, "wb");
    assert(out && fwrite(file, 1, file_length, out) == file_length && !fclose(out));
}

/* Whether the file `name` holds exactly the current file bytes. */
static bool holds_file(const char *name)
{
    char path[256];
    inside(path, sizeof path, name);
    FILE *in = fopen(path, "rb");
    REQUIRE(in);
    unsigned char bytes[FILE_MAX + 1];
    size_t length = fread(bytes, 1, sizeof bytes, in);
    fclose(in);
    REQUIRE(length == file_length && !memcmp(bytes, file, length));
    return true;
}

static bool exists(const char *name)
{
    char path[256];
    struct stat info;
    inside(path, sizeof path, name);
    return !stat(path, &info);
}

TEST_CASE(radiance_files_decode_flat_and_run_length_rows)
{
    render_sky sky;
    char error[128] = { 0 };
    flat_sky();
    REQUIRE(image_prepare_sky_decode(file, file_length, &sky, error, sizeof error));
    REQUIRE(flat_texels(&sky));
    render_sky_free(&sky);
    run_length_sky();
    REQUIRE(image_prepare_sky_decode(file, file_length, &sky, error, sizeof error));
    REQUIRE(run_length_texels(&sky));
    render_sky_free(&sky);
    return true;
}

TEST_CASE(radiance_files_reject_malformed_skies)
{
    static const struct
    {
        const char *header;
        unsigned char body[8];
        size_t length;
        const char *message;
    } cases[] = {
        { "P6\n8 4\n", { 0 }, 0, "not a Radiance" },
        { "#?RADIANCE\nFORMAT=32-bit_rle_xyze\n\n-Y 4 +X 8\n", { 0 }, 0, "32-bit_rle_rgbe" },
        { "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n", { 0 }, 0, "end with a blank line" },
        { "#?RADIANCE\n\n+Y 4 +X 8\n", { 0 }, 0, "(-Y +X)" },
        { "#?RADIANCE\n\n-Y 4 +X 8 \n", { 0 }, 0, "(-Y +X)" },
        { "#?RADIANCE\n\n-Y 4 +X 4\n", { 0 }, 0, "twice as wide" },
        { "#?RADIANCE\n\n-Y 4096 +X 8192\n", { 0 }, 0, "twice as wide" },
        /* Doubling either height would overflow; the second wraps to 2. */
        { "#?RADIANCE\n\n-Y 2147483647 +X 2\n", { 0 }, 0, "twice as wide" },
        { "#?RADIANCE\n\n-Y -2147483647 +X 2\n", { 0 }, 0, "twice as wide" },
        { "#?RADIANCE\n\n-Y 4 +X 8\n", { 1, 2, 3, 4 }, 4, "truncated" },
        { "#?RADIANCE\n\n-Y 8 +X 16\n", { 2, 2, 0, 9 }, 4, "truncated" },
        { "#?RADIANCE\n\n-Y 8 +X 16\n", { 2, 2, 0, 16, 0 }, 5, "truncated" },
        { "#?RADIANCE\n\n-Y 8 +X 16\n", { 2, 2, 0, 16, 128 + 17, 5 }, 6, "truncated" },
        { "#?RADIANCE\n\n-Y 8 +X 16\n", { 2, 2, 0, 16, 16, 5 }, 6, "truncated" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        begin(cases[i].header);
        append(cases[i].body, cases[i].length);
        render_sky sky;
        char error[128] = { 0 };
        REQUIRE(!image_prepare_sky_decode(file, file_length, &sky, error, sizeof error));
        REQUIRE(strstr(error, cases[i].message));
    }
    render_sky sky;
    char error[128] = { 0 }, line[600];
    memset(line, 'x', sizeof line);
    begin("#?RADIANCE\n");
    append(line, sizeof line);
    append("\n\n-Y 4 +X 8\n", 12);
    REQUIRE(!image_prepare_sky_decode(file, file_length, &sky, error, sizeof error));
    REQUIRE(strstr(error, "under 512 bytes"));
    /* Mantissas of 1, 1, 1 repeat the texel before in the original encoding. */
    flat_sky();
    memcpy(file + file_length - 4, (unsigned char[4]){ 1, 1, 1, 3 }, 4);
    REQUIRE(!image_prepare_sky_decode(file, file_length, &sky, error, sizeof error));
    REQUIRE(strstr(error, "original run-length encoding"));
    return true;
}

TEST_CASE(skies_prepare_once_from_mounted_content)
{
    char content[256], error[128] = { 0 };
    inside(content, sizeof content, "content");
    REQUIRE(render_image_mount_content(content, error, sizeof error));
    flat_sky();
    write_file("content/skies/field.hdr");
    REQUIRE(image_prepare_sky("content/skies/field.hdr", error, sizeof error));
    const render_sky *sky = render_sky_find("content/skies/field.hdr");
    REQUIRE(sky && sky->identity && flat_texels(sky));
    uint64_t identity = sky->identity;
    /* A resident sky is not read again. */
    char path[256];
    inside(path, sizeof path, "content/skies/field.hdr");
    REQUIRE(!remove(path));
    REQUIRE(image_prepare_sky("content/skies/field.hdr", error, sizeof error));
    REQUIRE(render_sky_find("content/skies/field.hdr")->identity == identity);
    /* Identity follows the texels: the same sky prepared again keeps it, so
     * renderers keep their cube, and another sky changes it. */
    render_skies_clear();
    write_file("content/skies/field.hdr");
    REQUIRE(image_prepare_sky("content/skies/field.hdr", error, sizeof error));
    REQUIRE(render_sky_find("content/skies/field.hdr")->identity == identity);
    render_skies_clear();
    file[file_length - 1]++;
    write_file("content/skies/field.hdr");
    REQUIRE(image_prepare_sky("content/skies/field.hdr", error, sizeof error));
    REQUIRE(render_sky_find("content/skies/field.hdr")->identity != identity);
    REQUIRE(!image_prepare_sky("content/skies/missing.hdr", error, sizeof error));
    REQUIRE(strstr(error, "Cannot read"));
    const char *keys[] = { "content/skies/field.png",
                           "content/../field.hdr",
                           "skies/field.hdr" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(!image_prepare_sky(keys[i], error, sizeof error));
        REQUIRE(strstr(error, "content/... .hdr path"));
    }
    begin("#?RADIANCE\n\n-Y 4 +X 8\n");
    write_file("content/skies/broken.hdr");
    REQUIRE(!image_prepare_sky("content/skies/broken.hdr", error, sizeof error));
    REQUIRE(strstr(error, "truncated") && !render_sky_find("content/skies/broken.hdr"));
    return true;
}

/* Whether importing `source` as `key` succeeds with the key `expected`. */
static bool imports(const char *source, const char *key, const char *expected)
{
    char content[256], path[256], error[128] = { 0 };
    inside(content, sizeof content, "content");
    inside(path, sizeof path, source);
    char *imported = image_prepare_sky_import(content, path, key, error, sizeof error);
    REQUIRE(imported && !strcmp(imported, expected));
    free(imported);
    return true;
}

/* Whether importing `source` as `key` fails, saying `message`. */
static bool refuses(const char *source, const char *key, const char *message)
{
    char content[256], path[256], error[160] = { 0 };
    inside(content, sizeof content, "content");
    inside(path, sizeof path, source);
    REQUIRE(!image_prepare_sky_import(content, path, key, error, sizeof error));
    REQUIRE(strstr(error, message));
    return true;
}

TEST_CASE(sky_imports_copy_valid_files_without_replacing_any)
{
    run_length_sky();
    write_file("download.hdr");
    REQUIRE(imports("download.hdr", "content/skies/new/field.hdr", "content/skies/new/field.hdr"));
    REQUIRE(holds_file("content/skies/new/field.hdr"));
    REQUIRE(imports("download.hdr", NULL, "content/skies/download.hdr"));
    REQUIRE(holds_file("content/skies/download.hdr"));
    /* An existing file is kept, even when another sky would replace it. */
    flat_sky();
    write_file("download.hdr");
    REQUIRE(
        refuses("download.hdr", "content/skies/new/field.hdr", "content/skies/new/field.hdr: a file is already there")
    );
    run_length_sky();
    REQUIRE(holds_file("content/skies/new/field.hdr"));
    begin("#?RADIANCE\n\n-Y 4 +X 4\n");
    write_file("broken.hdr");
    REQUIRE(refuses("broken.hdr", "content/skies/new/broken.hdr", "twice as wide"));
    REQUIRE(!exists("content/skies/new/broken.hdr"));
    REQUIRE(refuses("download.hdr", "content/skies/new/field.exr", "content/... .hdr path"));
    REQUIRE(refuses("download.hdr", "content/../field.hdr", "content/... .hdr path"));
    REQUIRE(refuses("missing.hdr", "content/skies/new/other.hdr", "Cannot read the source file"));
    REQUIRE(!exists("content/skies/new/other.hdr"));
    return true;
}

void test_sky_files()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(radiance_files_decode_flat_and_run_length_rows);
    RUN_TEST_CASE(radiance_files_reject_malformed_skies);
    RUN_TEST_CASE(skies_prepare_once_from_mounted_content);
    RUN_TEST_CASE(sky_imports_copy_valid_files_without_replacing_any);
    pu_clear_setup();
}
