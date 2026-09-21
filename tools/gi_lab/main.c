/* Hidden GPU experiments. Fixture content stays in this tooling target. */
#include "platform/cute.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    int width, height, display_width, display_height, directions;
    int rays, events, iterations, warmup, frames, off_frame, move_frame, scene;
    float fog, shift, move_shift;
    bool iterative, coarse;
    int boundary_filter;
    const char *output;
} Options;

static double now_ms()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec * 1e-6;
}

static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static CF_Canvas canvas(int width, int height, CF_PixelFormat format)
{
    CF_CanvasParams p = cf_canvas_defaults(width, height);
    p.target.pixel_format = format;
    p.target.filter = CF_FILTER_LINEAR;
    p.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas c = cf_make_canvas(p);
    assert(c.id);
    return c;
}

static CF_ComputeShader shader(const char *name)
{
    CF_ComputeShader s = cf_make_compute_shader(name);
    if (!s.id) {
        fprintf(stderr, "Failed shader: %s\n", name);
        exit(1);
    }
    return s;
}

static void dispatch_image(CF_ComputeShader s, CF_Material m, CF_Texture t, int x, int y, int z)
{
    CF_ComputeDispatch d = cf_compute_dispatch_defaults(x, y, z);
    d.rw_textures = &t;
    d.rw_texture_count = 1;
    cf_dispatch_compute(s, m, d);
}

static void save_field(CF_Canvas c, Options o, int frame)
{
    CF_Readback readback = cf_canvas_readback(c);
    assert(readback.id);
    while (!cf_readback_ready(readback)) {
    }
    int bytes = o.width * o.height * 4 * (int)sizeof(float);
    float *data = xmalloc(bytes);
    assert(cf_readback_size(readback) == bytes);
    int got = cf_readback_data(readback, data, bytes);
    assert(got == bytes);
    cf_destroy_readback(readback);
    char path[2048];
    snprintf(path, sizeof path, "%s-%04d.pfm", o.output, frame);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "PF\n%d %d\n-1.0\n", o.width, o.height);
    for (int i = 0; i < o.width * o.height; i++) {
        for (int c = 0; c < 3; c++) {
            assert(isfinite(data[i * 4 + c]) && data[i * 4 + c] >= 0);
        }
        fwrite(data + i * 4, sizeof(float), 3, f);
    }
    fclose(f);
    free(data);
}

static Options options(int argc, char **argv)
{
    Options o = { .width = 240,
                  .height = 135,
                  .display_width = 2560,
                  .display_height = 1440,
                  .directions = 32,
                  .rays = 1024,
                  .events = 4,
                  .iterations = 4,
                  .warmup = 128,
                  .frames = 64,
                  .off_frame = -1,
                  .move_frame = -1,
                  .move_shift = .1f,
                  .scene = 1,
                  .fog = .2f,
                  .output = "gi-lab" };
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) {
            fprintf(stderr, "Missing value for %s\n", argv[i]);
            exit(1);
        }
        const char *key = argv[i], *value = argv[i + 1];
        if (!strcmp(key, "--method")) {
            assert(
                !strcmp(value, "trace") || !strcmp(value, "iterate") ||
                !strcmp(value, "coarse")
            );
            o.iterative = strcmp(value, "trace") != 0;
            o.coarse = !strcmp(value, "coarse");
        } else if (!strcmp(key, "--width")) {
            o.width = atoi(value);
        } else if (!strcmp(key, "--height")) {
            o.height = atoi(value);
        } else if (!strcmp(key, "--display-width")) {
            o.display_width = atoi(value);
        } else if (!strcmp(key, "--display-height")) {
            o.display_height = atoi(value);
        } else if (!strcmp(key, "--directions")) {
            o.directions = atoi(value);
        } else if (!strcmp(key, "--rays")) {
            o.rays = atoi(value);
        } else if (!strcmp(key, "--events")) {
            o.events = atoi(value);
        } else if (!strcmp(key, "--iterations")) {
            o.iterations = atoi(value);
        } else if (!strcmp(key, "--warmup")) {
            o.warmup = atoi(value);
        } else if (!strcmp(key, "--frames")) {
            o.frames = atoi(value);
        } else if (!strcmp(key, "--off-frame")) {
            o.off_frame = atoi(value);
        } else if (!strcmp(key, "--move-frame")) {
            o.move_frame = atoi(value);
        } else if (!strcmp(key, "--move-shift")) {
            o.move_shift = strtof(value, NULL);
        } else if (!strcmp(key, "--boundary-filter")) {
            o.boundary_filter = atoi(value);
        } else if (!strcmp(key, "--scene")) {
            o.scene = atoi(value);
        } else if (!strcmp(key, "--fog")) {
            o.fog = strtof(value, NULL);
        } else if (!strcmp(key, "--shift")) {
            o.shift = strtof(value, NULL);
        } else if (!strcmp(key, "--output")) {
            o.output = value;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", key);
            exit(1);
        }
    }
    assert(o.width > 0 && o.width <= 1024 && o.height > 0 && o.height <= 1024);
    assert(
        o.display_width > 0 && o.display_width <= 4096 &&
        o.display_height > 0 && o.display_height <= 4096
    );
    assert(o.directions >= 8 && o.directions <= 64 && o.directions % 8 == 0);
    assert(o.boundary_filter == 0 || o.boundary_filter == 1);
    assert(o.events >= 0 && o.events <= 4 && o.rays > 0 && o.rays <= 16384);
    assert(
        o.iterations > 0 && o.iterations <= 64 && o.warmup >= 0 &&
        o.frames > 0 && o.frames <= 4096
    );
    assert(
        o.scene >= 0 && o.scene <= 4 && o.fog >= 0 && isfinite(o.fog) &&
        isfinite(o.shift) && isfinite(o.move_shift)
    );
    return o;
}

int main(int argc, char **argv)
{
    Options o = options(argc, argv);
    CF_Result made = cf_make_app(
        "GI experiments",
        0,
        0,
        0,
        64,
        64,
        CF_APP_OPTIONS_HIDDEN_BIT | CF_APP_OPTIONS_NO_AUDIO_BIT |
            CF_APP_OPTIONS_NO_HIGH_DPI_BIT,
        argv[0]
    );
    if (cf_is_error(made)) {
        fprintf(stderr, "%s\n", made.details);
        return 1;
    }
    assert(cf_app_set_present_mode(CF_PRESENT_MODE_IMMEDIATE));
    cf_fs_mount(GI_LAB_SHADER_DIR, "/gi-lab", true);
    cf_shader_directory("/gi-lab");
    CF_Material m = cf_make_material();
    CF_Canvas result = canvas(o.width, o.height, CF_PIXEL_FORMAT_R32G32B32A32_FLOAT);
    CF_Canvas display = canvas(o.display_width, o.display_height, CF_PIXEL_FORMAT_R8G8B8A8_UNORM);
    CF_Canvas field[4][2] = { 0 };
    int widths[4] = { o.width }, heights[4] = { o.height }, levels = 1;
    while (o.coarse && levels < 4 && widths[levels - 1] > 40) {
        widths[levels] = (widths[levels - 1] + 1) / 2;
        heights[levels] = (heights[levels - 1] + 1) / 2;
        levels++;
    }
    CF_StorageBuffer accumulator = { 0 };
    CF_ComputeShader clear = { 0 }, trace = { 0 }, solve = { 0 };
    if (o.iterative) {
        for (int level = 0; level < levels; level++) {
            for (int i = 0; i < 2; i++) {
                field[level][i] = canvas(
                    widths[level] * 8,
                    heights[level] * (o.directions / 8),
                    CF_PIXEL_FORMAT_R16G16B16A16_FLOAT
                );
            }
        }
        trace = shader("iterate.shd");
        solve = shader("gather.shd");
    } else {
        CF_StorageBufferParams p = cf_storage_buffer_defaults(
            o.width * o.height * 3 * (int)sizeof(unsigned)
        );
        p.compute_readable = true;
        p.compute_writable = true;
        accumulator = cf_make_storage_buffer(p);
        assert(accumulator.id);
        clear = shader("clear.shd");
        trace = shader("trace.shd");
        solve = shader("resolve.shd");
    }
    CF_ComputeShader present = shader("present.shd");
    int grid[] = { o.width, o.height, o.directions, o.scene };
    int budget[] = { o.rays, o.events, 0, o.boundary_filter };
    float scene[] = { o.fog, 1, o.shift, 1 };
    cf_material_set_uniform_cs(m, "u_grid", grid, CF_UNIFORM_TYPE_INT, 4);
    cf_material_set_uniform_cs(m, "u_budget", budget, CF_UNIFORM_TYPE_INT, 4);
    cf_material_set_uniform_cs(m, "u_scene", scene, CF_UNIFORM_TYPE_FLOAT, 4);
    size_t field_bytes = (size_t)o.width * o.height * (16 + (o.iterative ? 0 : 12)) +
                         (size_t)o.display_width * o.display_height * 4;
    if (o.iterative) {
        for (int level = 0; level < levels; level++) {
            field_bytes += (size_t)widths[level] * heights[level] * o.directions * 16;
        }
    }
    int ping = 0;
    double *times = xmalloc(o.frames * sizeof *times);
    double update_ms = 0, optics_ms = 0, submit_ms = 0;
    for (int frame = -o.warmup; frame < o.frames; frame++) {
        double start = now_ms();
        cf_app_update(NULL);
        double after_update = now_ms();
        scene[1] = o.off_frame >= 0 && frame >= o.off_frame ? 0 : 1;
        scene[2] = o.shift +
                   (o.move_frame >= 0 && frame >= o.move_frame ? o.move_shift : 0);
        cf_material_set_uniform_cs(m, "u_scene", scene, CF_UNIFORM_TYPE_FLOAT, 4);
        if (o.iterative) {
            for (int level = levels - 1; level >= 0; level--) {
                int w = widths[level], h = heights[level];
                grid[0] = w;
                grid[1] = h;
                cf_material_set_uniform_cs(m, "u_grid", grid, CF_UNIFORM_TYPE_INT, 4);
                if (o.coarse || frame == -o.warmup) {
                    budget[2] = level == levels - 1 ? 1 : 2;
                    cf_material_set_uniform_cs(m, "u_budget", budget, CF_UNIFORM_TYPE_INT, 4);
                    CF_Canvas previous = level == levels - 1
                                           ? field[level][1]
                                           : field[level + 1][ping];
                    cf_material_set_texture_cs(m, "u_previous", cf_canvas_get_target(previous));
                    dispatch_image(
                        trace,
                        m,
                        cf_canvas_get_target(field[level][0]),
                        (w + 7) / 8,
                        (h + 7) / 8,
                        o.directions
                    );
                    ping = 0;
                    budget[2] = 0;
                    cf_material_set_uniform_cs(m, "u_budget", budget, CF_UNIFORM_TYPE_INT, 4);
                }
                int steps = o.coarse && level == levels - 1 ? w * 2 : o.iterations;
                for (int i = 0; i < steps; i++) {
                    cf_material_set_texture_cs(
                        m,
                        "u_previous",
                        cf_canvas_get_target(field[level][ping])
                    );
                    dispatch_image(
                        trace,
                        m,
                        cf_canvas_get_target(field[level][1 - ping]),
                        (w + 7) / 8,
                        (h + 7) / 8,
                        o.directions
                    );
                    ping = 1 - ping;
                }
            }
            cf_material_set_texture_cs(m, "u_previous", cf_canvas_get_target(field[0][ping]));
            dispatch_image(
                solve,
                m,
                cf_canvas_get_target(result),
                (o.width + 15) / 16,
                (o.height + 15) / 16,
                1
            );
        } else {
            CF_ComputeDispatch d = cf_compute_dispatch_defaults(
                (o.width * o.height * 3 + 255) / 256,
                1,
                1
            );
            d.rw_buffers = &accumulator;
            d.rw_buffer_count = 1;
            cf_dispatch_compute(clear, m, d);
            d.group_count_x = (o.rays + 63) / 64;
            cf_dispatch_compute(trace, m, d);
            d = cf_compute_dispatch_defaults((o.width + 15) / 16, (o.height + 15) / 16, 1);
            d.ro_buffers = &accumulator;
            d.ro_buffer_count = 1;
            CF_Texture target = cf_canvas_get_target(result);
            d.rw_textures = &target;
            d.rw_texture_count = 1;
            cf_dispatch_compute(solve, m, d);
        }
        cf_material_set_texture_cs(m, "u_field", cf_canvas_get_target(result));
        dispatch_image(
            present,
            m,
            cf_canvas_get_target(display),
            (o.display_width + 15) / 16,
            (o.display_height + 15) / 16,
            1
        );
        cf_gpu_sync();
        double after_optics = now_ms();
        cf_app_draw_onto_screen(true);
        if (frame >= 0) {
            update_ms += after_update - start;
            optics_ms += after_optics - after_update;
            submit_ms += now_ms() - after_optics;
        }
        if (frame >= 0) {
            times[frame] = now_ms() - start;
        }
        // Captures are after the timed GPU completion and before the next frame.
        int change = o.off_frame >= 0 ? o.off_frame : o.move_frame;
        if (frame == o.frames - 1 ||
            (change >= 0 && (frame == change - 1 || frame == change ||
                             frame == change + 3 || frame == change + 15))) {
            save_field(result, o, frame);
        }
    }
    fprintf(
        stderr,
        "Mean update %.4f, optics+GPU wait %.4f, empty present %.4f ms\n",
        update_ms / o.frames,
        optics_ms / o.frames,
        submit_ms / o.frames
    );
    qsort(times, o.frames, sizeof *times, compare);
    printf(
        "{\"method\":\"%s\",\"scene\":%d,\"hidden_window\":true,\"grid\":[%d,%"
        "d],"
        "\"display\":[%d,%d],\"directions\":%d,\"rays\":%d,\"events\":%d,"
        "\"iterations\":%d,"
        "\"warmup\":%d,\"frames\":%d,\"p50_ms\":%.4f,\"p95_ms\":%.4f,\"p99_"
        "ms\":%.4f,"
        "\"boundary_filter\":%d,\"allocated_field_mib\":%.3f,\"levels\":%d,"
        "\"timing\":\"CPU dispatch "
        "plus synchronized GPU completion; optical field and display "
        "reconstruction only\"}\n",
        o.coarse      ? "coarse"
        : o.iterative ? "iterate"
                      : "trace",
        o.scene,
        o.width,
        o.height,
        o.display_width,
        o.display_height,
        o.directions,
        o.rays,
        o.events,
        o.iterations,
        o.warmup,
        o.frames,
        times[o.frames / 2],
        times[(int)(o.frames * .95)],
        times[(int)(o.frames * .99)],
        o.boundary_filter,
        (double)field_bytes / (1024 * 1024),
        levels
    );
    free(times);
    if (accumulator.id) {
        cf_destroy_storage_buffer(accumulator);
    }
    for (int level = 0; level < levels; level++) {
        for (int i = 0; i < 2; i++) {
            if (field[level][i].id) {
                cf_destroy_canvas(field[level][i]);
            }
        }
    }
    if (clear.id) {
        cf_destroy_compute_shader(clear);
    }
    cf_destroy_compute_shader(trace);
    cf_destroy_compute_shader(solve);
    cf_destroy_compute_shader(present);
    cf_destroy_canvas(result);
    cf_destroy_canvas(display);
    cf_destroy_material(m);
    cf_destroy_app();
    return 0;
}
