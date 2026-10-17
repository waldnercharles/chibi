#include "platform/cute.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
    WIDTH = 640,
    HEIGHT = 360,
    PIXELS = WIDTH * HEIGHT,
    PATH_SIZE = 4096
};
static const char *player, *artifact, *resources, *shaders, *output;
static char directory[PATH_SIZE], capture[PATH_SIZE], log_path[PATH_SIZE];
static char save_namespace[PATH_SIZE], save_path[PATH_SIZE];
static bool owns_save_namespace;
static char *log_text, *png;
static CF_Image image;

static void path(char *out, const char *name)
{
    int size = snprintf(out, PATH_SIZE, "%s/%s", directory, name);
    assert(size > 0 && size < PATH_SIZE);
}
static void cleanup()
{
    if (pu_test_failed() && log_text) {
        fputs(log_text, stderr);
    }
    free(log_text);
    free(png);
    cf_image_free(&image);
    log_text = png = NULL;
    image = (CF_Image){ 0 };
    if (owns_save_namespace) {
        tool_remove_tree(save_namespace);
        owns_save_namespace = false;
    }
}

TEST_CASE(installed_terrain_lab_renders_a_bounded_playable_frame)
{
    struct stat info;
    REQUIRE(!stat(player, &info) && S_ISREG(info.st_mode));
    REQUIRE(
        !stat(artifact, &info) && S_ISREG(info.st_mode) && info.st_size > 0 &&
        info.st_size <= 64 * 1024 * 1024
    );
    REQUIRE(!stat(resources, &info) && S_ISDIR(info.st_mode));
    REQUIRE(!stat(shaders, &info) && S_ISDIR(info.st_mode));
    REQUIRE(!mkdir(output, 0700) || errno == EEXIST);
    REQUIRE(realpath(output, directory));
    path(capture, "frame.png");
    path(log_path, "player.log");
    REQUIRE(!unlink(capture) || errno == ENOENT);
    int namespace_size = snprintf(save_namespace, sizeof save_namespace, "%s/save-XXXXXX", directory);
    REQUIRE(namespace_size > 0 && namespace_size < (int)sizeof save_namespace);
    REQUIRE(mkdtemp(save_namespace));
    owns_save_namespace = true;
    int save_size = snprintf(save_path, sizeof save_path, "%s/state.json", save_namespace);
    REQUIRE(save_size > 0 && save_size < (int)sizeof save_path);
    const char *args[] = { player,
                           artifact,
                           "--resource-root",
                           resources,
                           "--shader-dir",
                           shaders,
                           "--backend",
                           "vm",
                           "--save",
                           save_path,
                           "--hidden",
                           "--frames",
                           "4",
                           "--fixed-dt",
                           "0.016666666666666666",
                           "--size",
                           "640x360",
                           "--capture",
                           capture,
                           NULL };
    REQUIRE(tool_run_within(args, directory, log_path, 25) == 0);
    log_text = tool_read(log_path);
    REQUIRE(log_text);
    REQUIRE(strstr(log_text, "frames=4 steps=3"));
    const char *backend = strstr(log_text, "capture backend=");
    REQUIRE(backend && strstr(backend, "size=640x360 hidden=true"));
    REQUIRE(!strstr(log_text, "Final frame capture failed"));
    REQUIRE(
        !stat(capture, &info) && S_ISREG(info.st_mode) && info.st_size > 0 &&
        info.st_size < 2 * 1024 * 1024
    );
    png = tool_read(capture);
    REQUIRE(png);
    REQUIRE(!cf_is_error(cf_image_load_png_from_memory(png, (int)info.st_size, &image)));
    REQUIRE(image.w == WIDTH && image.h == HEIGHT);
    int opaque = 0, brown = 0, cool = 0, mint = 0, different = 0;
    CF_Pixel corner = image.pix[0];
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            CF_Pixel pixel = image.pix[y * WIDTH + x];
            opaque += pixel.a == 255;
            different += abs((int)pixel.r - corner.r) +
                             abs((int)pixel.g - corner.g) +
                             abs((int)pixel.b - corner.b) >
                         30;
            /* Loose authored landmarks: warm quarry below, cool backdrop
             * above, mint explorer/marker near the playable center. */
            brown += y >= HEIGHT / 2 && pixel.r > pixel.g + 15 &&
                     pixel.g > pixel.b + 15;
            cool += y < HEIGHT / 2 && pixel.b > pixel.r + 10;
            mint += x >= 200 && x < 440 && y >= 100 && y < 280 &&
                    pixel.g > pixel.r + 40 && pixel.b > pixel.r + 30;
            const unsigned char rgba[] = { pixel.r, pixel.g, pixel.b, pixel.a };
            for (int c = 0; c < 4; c++) {
                hash = (hash ^ rgba[c]) * UINT64_C(1099511628211);
            }
        }
    }
    char summary[4096];
    size_t backend_size = strcspn(backend, "\n");
    if (backend_size > 255) {
        backend_size = 255;
    }
    int length = snprintf(
        summary,
        sizeof summary,
        "project=terrain-lab\nplayer=%s\nartifact=%s\nframes=4\nsize=640x360\n%"
        ".*s\n"
        "opaque_pixels=%d\nnonbackground_pixels=%d\nwarm_terrain_pixels=%"
        "d\ncool_sky_pixels=%d\n"
        "mint_center_pixels=%d\npixel_hash_algorithm=fnv1a64-rgba8\npixel_hash="
        "%016" PRIx64 "\n",
        player,
        artifact,
        (int)backend_size,
        backend,
        opaque,
        different,
        brown,
        cool,
        mint,
        hash
    );
    REQUIRE(length > 0 && length < (int)sizeof summary);
    char evidence[PATH_SIZE];
    path(evidence, "result.txt");
    tool_write(evidence, summary);
    fputs(summary, stdout);
    REQUIRE(opaque == PIXELS);
    REQUIRE(different > 1000);
    REQUIRE(brown > 1000);
    REQUIRE(cool > 1000);
    REQUIRE(mint > 10);
    return true;
}
void test_terrain_playable_render(
    const char *player_path,
    const char *artifact_path,
    const char *resource_root,
    const char *shader_dir,
    const char *evidence_output
)
{
    player = player_path;
    artifact = artifact_path;
    resources = resource_root;
    shaders = shader_dir;
    output = evidence_output;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(installed_terrain_lab_renders_a_bounded_playable_frame);
    pu_clear_setup();
}
