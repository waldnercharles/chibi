#include "platform/cute.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_SIZE 4096
#define SOURCE_BYTES (32 * 1024 * 1024)
#define ARTIFACT_BYTES (64 * 1024 * 1024)
#define PIXELS (320 * 180)

static const struct
{
    const char *name, *tree;
} lab_inputs[] = {
    { "local_mesh_sampled", "85ac5202d497b042d8a1000b48e3e21fda81ab15" },
    { "hrc_lab", "b38b72d407e3dbcc047ef09f24d9d84b2e4b21a2" },
    { "modeling_lab", "fbca28ba002657e3606b600105fa84262c103225" },
    { "sdf_lab", "cff7485e80cd1d7108990e7d2eadd73a06489a45" },
    { "suzanne_sampled", "2316d9a6ddbf867da532938cb68d38926149c7ec" },
};
static const char *lab, *expected_tree;
static char scratch[PATH_SIZE], evidence[PATH_SIZE];
static yyjson_doc *pin, *sdk_manifest, *player_manifest, *resources;

static void path(char *out, const char *base, const char *name)
{
    int n = snprintf(out, PATH_SIZE, "%s/%s", base, name);
    assert(n > 0 && n < PATH_SIZE);
}

static const char *setting(const char *name)
{
    const char *value = getenv(name);
    assert(value && value[0] == '/');
    return value;
}

static bool outside(const char *directory, const char *other)
{
    char resolved[PATH_SIZE], base[PATH_SIZE];
    if (!realpath(directory, resolved) || !realpath(other, base)) {
        return false;
    }
    size_t size = strlen(base);
    return strncmp(resolved, base, size) || (resolved[size] && resolved[size] != '/');
}

static yyjson_doc *read_json(const char *filename, size_t maximum)
{
    struct stat info;
    assert(!lstat(filename, &info) && S_ISREG(info.st_mode));
    assert(info.st_size > 0 && (uint64_t)info.st_size <= maximum);
    yyjson_doc *doc = yyjson_read_file(filename, 0, NULL, NULL);
    assert(doc && yyjson_is_obj(yyjson_doc_get_root(doc)));
    return doc;
}

static const char *text(yyjson_val *object, const char *key)
{
    const char *value = yyjson_get_str(yyjson_obj_get(object, key));
    assert(value && value[0]);
    return value;
}

/* Bound inventories before copying, hashing or handing input to the compiler. */
static bool inventory(const char *root, int *files, size_t *bytes, int limit, size_t maximum)
{
    struct stat info;
    if (lstat(root, &info)) {
        return false;
    }
    if (S_ISREG(info.st_mode)) {
        if (info.st_size < 0 || (uint64_t)info.st_size > maximum - *bytes) {
            return false;
        }
        *bytes += (size_t)info.st_size;
        return ++*files <= limit;
    }
    if (!S_ISDIR(info.st_mode)) {
        return false;
    }
    DIR *directory = opendir(root);
    if (!directory) {
        return false;
    }
    bool valid = true;
    struct dirent *entry;
    while (valid && (entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char child[PATH_SIZE];
        path(child, root, entry->d_name);
        valid = inventory(child, files, bytes, limit, maximum);
    }
    closedir(directory);
    return valid;
}

static int run(const char *const argv[], const char *log, double seconds)
{
    char filename[PATH_SIZE];
    path(filename, evidence, log);
    return tool_run_within(argv, scratch, filename, seconds);
}

static int copy(const char *source, const char *destination)
{
#ifdef __APPLE__
    const char *argv[] = { "cp", "-cR", source, destination, NULL };
#else
    const char *argv[] = { "cp",   "--reflink=auto", "-R",
                           source, destination,      NULL };
#endif
    return run(argv, "copy.log", 10);
}

static bool fingerprint(const char *filename, size_t expected, const char *hash)
{
    struct stat info;
    if (lstat(filename, &info) || !S_ISREG(info.st_mode) ||
        (uint64_t)info.st_size != expected || expected > ARTIFACT_BYTES) {
        return false;
    }
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return false;
    }
    uint64_t value = UINT64_C(14695981039346656037);
    unsigned char buffer[8192];
    size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, file))) {
        for (size_t i = 0; i < count; i++) {
            value = (value ^ buffer[i]) * UINT64_C(1099511628211);
        }
    }
    bool valid = !ferror(file);
    fclose(file);
    char actual[17];
    snprintf(actual, sizeof actual, "%016" PRIx64, value);
    return valid && !strcmp(actual, hash);
}

static bool relative_path(const char *name)
{
    if (!name || !name[0] || name[0] == '/' || strchr(name, '\\')) {
        return false;
    }
    for (const char *part = name; part;) {
        const char *end = strchr(part, '/');
        size_t size = end ? (size_t)(end - part) : strlen(part);
        if (!size || (size == 1 && part[0] == '.') ||
            (size == 2 && part[0] == '.' && part[1] == '.')) {
            return false;
        }
        part = end ? end + 1 : NULL;
    }
    return true;
}

static void parents(char *filename)
{
    for (char *p = filename + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            struct stat info;
            if (stat(filename, &info)) {
                assert(!mkdir(filename, 0700));
            } else {
                assert(S_ISDIR(info.st_mode));
            }
            *p = '/';
        }
    }
}

static int foreground(const CF_Image *image, int left, int top, int right, int bottom)
{
    CF_Pixel corner = image->pix[0];
    int count = 0;
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            CF_Pixel pixel = image->pix[y * image->w + x];
            count += abs((int)pixel.r - corner.r) + abs((int)pixel.g - corner.g) +
                         abs((int)pixel.b - corner.b) >
                     30;
        }
    }
    return count;
}

/* Broad scene landmarks reject empty or unrelated output without golden pixels. */
static int brightness(const CF_Image *image, int left, int top, int right, int bottom)
{
    int total = 0;
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            CF_Pixel pixel = image->pix[y * image->w + x];
            total += pixel.r + pixel.g + pixel.b;
        }
    }
    return total / (3 * (right - left) * (bottom - top));
}

static bool landmarks(const CF_Image *image)
{
    if (!strcmp(lab, "local_mesh_sampled")) {
        return foreground(image, 140, 50, 180, 130) > 2000 &&
               foreground(image, 0, 0, 120, 180) == 0 &&
               foreground(image, 200, 0, 320, 180) == 0;
    }
    if (!strcmp(lab, "hrc_lab")) {
        int shadow = brightness(image, 155, 10, 205, 55);
        return brightness(image, 43, 86, 51, 94) > shadow + 50 &&
               brightness(image, 25, 65, 70, 115) > shadow + 10 &&
               brightness(image, 160, 84, 200, 96) > shadow + 3;
    }
    if (!strcmp(lab, "modeling_lab")) {
        int shadow = brightness(image, 70, 128, 100, 154);
        return brightness(image, 95, 77, 121, 102) > shadow + 20 &&
               brightness(image, 200, 77, 224, 102) > shadow + 20 &&
               brightness(image, 157, 27, 163, 33) > 200;
    }
    if (!strcmp(lab, "suzanne_sampled")) {
        int shadow = brightness(image, 95, 110, 125, 135);
        return brightness(image, 130, 55, 185, 90) > shadow + 20 &&
               brightness(image, 83, 72, 99, 86) > shadow + 20 &&
               brightness(image, 223, 74, 235, 84) > shadow + 20 &&
               brightness(image, 148, 122, 171, 142) > shadow + 20;
    }
    if (!strcmp(lab, "sdf_lab")) {
        int shadow = brightness(image, 115, 45, 125, 60);
        return brightness(image, 84, 47, 101, 62) > shadow + 10 &&
               brightness(image, 145, 42, 174, 65) > shadow + 15 &&
               brightness(image, 211, 43, 220, 64) >
                   brightness(image, 225, 50, 232, 58) + 10 &&
               brightness(image, 75, 115, 107, 137) > shadow + 10 &&
               brightness(image, 149, 117, 170, 133) > shadow + 10 &&
               brightness(image, 212, 119, 244, 133) > shadow + 10;
    }
    return false;
}

static void teardown()
{
    yyjson_doc_free(pin);
    yyjson_doc_free(sdk_manifest);
    yyjson_doc_free(player_manifest);
    yyjson_doc_free(resources);
    pin = sdk_manifest = player_manifest = resources = NULL;
    if (scratch[0]) {
        tool_remove_tree(scratch);
        scratch[0] = 0;
    }
}

TEST_CASE(installed_lab_renders_after_relocation)
{
    const char *sdk = setting("CHIBI_EXAMPLES_SDK");
    const char *player = setting("CHIBI_EXAMPLES_PLAYER");
    const char *examples = setting("CHIBI_EXAMPLES_ROOT");
    const char *output = setting("CHIBI_EXAMPLES_EVIDENCE");
    REQUIRE(
        outside(output, tool_root()) && outside(output, examples) &&
        outside(output, sdk) && outside(output, player)
    );
    path(evidence, output, lab);
    REQUIRE(!mkdir(evidence, 0700));
    path(scratch, evidence, "scratch-XXXXXX");
    REQUIRE(mkdtemp(scratch));
    pin = read_json(setting("CHIBI_EXAMPLES_PIN"), 16384);
    yyjson_val *pins = yyjson_doc_get_root(pin);
    int files = 0;
    size_t bytes = 0;
    REQUIRE(inventory(sdk, &files, &bytes, 8192, (size_t)2 * 1024 * 1024 * 1024));
    files = 0;
    bytes = 0;
    REQUIRE(inventory(player, &files, &bytes, 256, 256 * 1024 * 1024));
    /* These are the production package validators, not a second package test
     * implementation. All acceptance decisions and workloads stay in C. */
    char helpers[PATH_SIZE];
    path(helpers, tool_root(), "tools");
    const char *verify[] = {
        "python3",
        "-B",
        "-c",
        "import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); "
        "from package_sdk import previous_package; from package_player import "
        "validate_package; "
        "previous_package(Path(sys.argv[2]),'sdk'); "
        "validate_package(Path(sys.argv[3]))",
        helpers,
        sdk,
        player,
        NULL
    };
    REQUIRE(!run(verify, "packages.log", 15));
    char filename[PATH_SIZE];
    path(filename, sdk, "share/chibi/package.json");
    sdk_manifest = read_json(filename, 4 * 1024 * 1024);
    yyjson_val *compiler = yyjson_doc_get_root(sdk_manifest);
    path(filename, player, "share/chibi/package.json");
    player_manifest = read_json(filename, 1024 * 1024);
    yyjson_val *runtime = yyjson_doc_get_root(player_manifest);
    REQUIRE(!strcmp(text(compiler, "build_id"), text(pins, "compiler_build_id")));
    REQUIRE(!strcmp(text(runtime, "build_id"), text(pins, "player_build_id")));
    static const char *const contracts[] = {
        "runtime_headers_sha256", "artifact_formats", "project_format",
        "prepared_format",        "system",           "processor",
        "pointer_bits",           "byte_order"
    };
    for (int key = 0; key < (int)(sizeof contracts / sizeof *contracts); key++) {
        yyjson_val *before = yyjson_obj_get(yyjson_obj_get(compiler, "requirements"), contracts[key]);
        yyjson_val *after = yyjson_obj_get(yyjson_obj_get(runtime, "requirements"), contracts[key]);
        REQUIRE(
            before && after && !yyjson_is_null(before) &&
            !yyjson_is_null(after) && yyjson_equals(before, after)
        );
    }
    const char *tree = text(yyjson_obj_get(pins, "projects"), lab);
    REQUIRE(!strcmp(tree, expected_tree));
    char archive[PATH_SIZE], source[PATH_SIZE], stage[PATH_SIZE], deployed[PATH_SIZE];
    path(archive, scratch, "project.tar");
    const char *export[] = { "git",     "-C",           examples,
                             "archive", "--format=tar", "--output",
                             archive,   tree,           NULL };
    REQUIRE(!run(export, "archive.log", 10));
    struct stat info;
    REQUIRE(!lstat(archive, &info) && info.st_size <= SOURCE_BYTES);
    path(source, scratch, "source");
    REQUIRE(!mkdir(source, 0700));
    const char *extract[] = { "tar", "-xf", archive, "-C", source, NULL };
    REQUIRE(!run(extract, "extract.log", 10));
    REQUIRE(!unlink(archive));
    files = 0;
    bytes = 0;
    REQUIRE(inventory(source, &files, &bytes, 512, SOURCE_BYTES));
    path(stage, scratch, "stage");
    REQUIRE(!mkdir(stage, 0700));
    char binary[PATH_SIZE], artifact[PATH_SIZE];
    path(binary, sdk, "bin/chibi-compile");
    path(artifact, stage, "game.chibip");
    if (!strcmp(lab, "local_mesh_sampled")) {
        char image[PATH_SIZE], missing[PATH_SIZE];
        path(image, source, "content/sdf/a745f5a57f501fe4.sdf.png");
        path(missing, scratch, "missing.png");
        REQUIRE(!rename(image, missing));
        const char *reject[] = { binary,     "--project-root", source,
                                 "--output", artifact,         NULL };
        REQUIRE(run(reject, "missing-image.log", 10) == 1);
        path(filename, evidence, "missing-image.log");
        char *failure = tool_read(filename);
        REQUIRE(failure && strstr(failure, "a745f5a57f501fe4.sdf.png"));
        free(failure);
        REQUIRE(access(artifact, F_OK));
        REQUIRE(!rename(missing, image));
    }
    const char *compile[] = { binary,     "--project-root", source,
                              "--output", artifact,         NULL };
    REQUIRE(!run(compile, "compile.log", 20));
    path(filename, stage, "game.chibip.resources.json");
    resources = read_json(filename, 1024 * 1024);
    yyjson_val *manifest = yyjson_doc_get_root(resources);
    REQUIRE(yyjson_get_int(yyjson_obj_get(manifest, "version")) == 1);
    REQUIRE(!stat(artifact, &info) && info.st_size > 0 && info.st_size <= ARTIFACT_BYTES);
    REQUIRE(fingerprint(artifact, (size_t)info.st_size, text(manifest, "artifact_hash")));
    yyjson_val *external = yyjson_obj_get(manifest, "external");
    REQUIRE(yyjson_is_arr(external) && yyjson_arr_size(external) <= 64);
    yyjson_val *embedded = yyjson_obj_get(manifest, "embedded");
    REQUIRE(
        yyjson_is_arr(embedded) && yyjson_arr_size(embedded) >= 2 &&
        yyjson_arr_size(embedded) <= 1024
    );
    if (!strcmp(lab, "local_mesh_sampled")) {
        bool sampled_image = false;
        size_t index, length;
        yyjson_val *item;
        yyjson_arr_foreach(embedded, index, length, item)
        {
            sampled_image |= !strcmp(text(item, "kind"), "image");
        }
        REQUIRE(sampled_image);
        REQUIRE(yyjson_arr_size(external) == 0);
    }
    size_t i, count;
    yyjson_val *record;
    yyjson_arr_foreach(external, i, count, record)
    {
        const char *relative = text(record, "path");
        REQUIRE(relative_path(relative));
        char input[PATH_SIZE], output[PATH_SIZE];
        path(input, source, relative);
        path(output, stage, relative);
        REQUIRE(
            fingerprint(input, yyjson_get_uint(yyjson_obj_get(record, "bytes")), text(record, "content_hash"))
        );
        parents(output);
        REQUIRE(!copy(input, output));
    }
    path(filename, stage, "player");
    REQUIRE(!copy(player, filename));
    char saved_manifest[PATH_SIZE];
    path(saved_manifest, evidence, "resources.json");
    path(filename, stage, "game.chibip.resources.json");
    REQUIRE(!copy(filename, saved_manifest));
    /* The compiler's source root ceases to exist; only the declared external
     * resource closure, prepared artifact and installed player are relocated. */
    tool_remove_tree(source);
    path(deployed, scratch, "relocated");
    REQUIRE(!rename(stage, deployed));
    path(binary, deployed, "player/bin/chibi-player");
    path(artifact, deployed, "game.chibip");
    REQUIRE(!unsetenv("CHIBI_RESOURCE_DIR"));
    char capture[PATH_SIZE];
    if (!strcmp(lab, "local_mesh_sampled")) {
        path(capture, evidence, "invalid.png");
        const char *no_frames[] = { binary,    "--hidden",  "--size",
                                    "320x180", "--capture", capture,
                                    NULL };
        REQUIRE(run(no_frames, "capture-needs-frames.log", 5) == 2);
        const char *oversize[] = { binary,    "--frames",  "1",     "--size",
                                   "640x640", "--capture", capture, NULL };
        REQUIRE(run(oversize, "capture-size-bound.log", 5) == 2);
        REQUIRE(access(capture, F_OK));
    }
    path(capture, evidence, "frame.png");
    const char *render[] = { binary,
                             artifact,
                             "--resource-root",
                             deployed,
                             "--backend",
                             "vm",
                             "--hidden",
                             "--frames",
                             "4",
                             "--fixed-dt",
                             "0.016666666666666666",
                             "--size",
                             "320x180",
                             "--capture",
                             capture,
                             NULL };
    REQUIRE(!run(render, "player.log", 25));
    path(filename, evidence, "player.log");
    char *log = tool_read(filename);
    REQUIRE(log && strstr(log, "frames=4 steps=3"));
    fputs(log, stdout);
    free(log);
    CF_Image image = { 0 };
    REQUIRE(!stat(capture, &info) && info.st_size > 0 && info.st_size < 1024 * 1024);
    char *png = tool_read(capture);
    REQUIRE(png);
    CF_Result decoded = cf_image_load_png_from_memory(png, (int)info.st_size, &image);
    free(png);
    REQUIRE(!cf_is_error(decoded));
    REQUIRE(image.w == 320 && image.h == 180);
    int changed = foreground(&image, 0, 0, 320, 180), opaque = 0;
    for (int pixel = 0; pixel < PIXELS; pixel++) {
        CF_Pixel value = image.pix[pixel];
        opaque += value.a == 255;
    }
    bool visible = landmarks(&image);
    cf_image_free(&image);
    REQUIRE(visible);
    REQUIRE(opaque == PIXELS);
    REQUIRE(changed > 100);
    char summary[1024];
    snprintf(
        summary,
        sizeof summary,
        "lab=%s\nasset_tree=%s\ncompiler=%s\nplayer=%s\nframes=4\nsize="
        "320x180\nforeground_pixels=%d\nexternal_resources=%zu\n",
        lab,
        tree,
        text(compiler, "build_id"),
        text(runtime, "build_id"),
        changed,
        yyjson_arr_size(external)
    );
    path(filename, evidence, "result.txt");
    tool_write(filename, summary);
    fputs(summary, stdout);
    return true;
}

void test_installed_examples(const char *name)
{
    lab = name;
    for (int i = 0; i < (int)(sizeof lab_inputs / sizeof *lab_inputs); i++) {
        if (!strcmp(lab_inputs[i].name, name)) {
            expected_tree = lab_inputs[i].tree;
        }
    }
    assert(expected_tree);
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(installed_lab_renders_after_relocation);
}
