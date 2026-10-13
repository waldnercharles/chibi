#include "authoring/shader_evidence.h"

#include "assets/project.h"
#include "authoring/automation_contract.h"
#include "authoring/image_prepare.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/xalloc.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILE_BYTES_MAX (16 * 1024 * 1024)
#define TOTAL_BYTES_MAX (64 * 1024 * 1024)
#define FILES_MAX 128
#define FNV_OFFSET UINT64_C(14695981039346656037)

typedef struct
{
    char *path;
    unsigned char *bytes;
    size_t size;
} evidence_file;

struct shader_authoring_evidence_plan
{
    shader_authoring_evidence input;
    behavior_asset *snapshot;
    char *source;
    shader_authoring_report report;
    evidence_file *images;
    float parameters[SHADER_PARAMETER_MAX][4];
    bool resident_images_verified;
};

static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}
static char *join(const char *a, const char *b)
{
    size_t size = strlen(a) + strlen(b) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", a, b);
    return path;
}
static void hash(av_doc *doc, av_value *object, const void *data, size_t size)
{
    uint64_t value = FNV_OFFSET;
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    }
    char text[17];
    snprintf(text, sizeof text, "%016" PRIx64, value);
    av_obj_add_str(doc, object, "fnv1a64", text);
}
static void optional(av_doc *doc, av_value *object, const char *key, const char *value)
{
    av_obj_set(doc, object, key, value ? av_str(doc, value) : av_null(doc));
}
static void vector(av_doc *doc, av_value *object, const char *key, const float *values, int count)
{
    av_value *array = av_obj_add_arr(doc, object, key);
    for (int i = 0; i < count; i++) {
        av_arr_append(array, isfinite(values[i]) ? av_float(doc, values[i]) : av_null(doc));
    }
}
static bool valid_key(const char *key)
{
    if (!key || strncmp(key, "content/", 8) || !key[8] || strlen(key) > 512 ||
        strchr(key, '\\')) {
        return false;
    }
    for (const char *part = key + 8; *part;) {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || (length == 1 && *part == '.') ||
            (length == 2 && !strncmp(part, "..", 2))) {
            return false;
        }
        if (!end) {
            return true;
        }
        part = end + 1;
    }
    return false;
}
static bool add_file(
    evidence_file **files,
    const char *path,
    const void *bytes,
    size_t size,
    char *error,
    size_t capacity
)
{
    size_t total = 0;
    for (int i = 0; i < acount(*files); i++) {
        if (!strcmp((*files)[i].path, path)) {
            return fail(error, capacity, "Evidence file paths collide");
        }
        total += (*files)[i].size;
    }
    if (acount(*files) >= FILES_MAX || size > FILE_BYTES_MAX ||
        size > TOTAL_BYTES_MAX - total) {
        return fail(error, capacity, "Evidence exceeds 128 files, 16 MiB/file or 64 MiB total");
    }
    evidence_file file = { .path = xstrdup(path),
                           .bytes = xmalloc(size ? size : 1),
                           .size = size };
    memcpy(file.bytes, bytes, size);
    apush(*files, file);
    return true;
}
static bool add_text(evidence_file **files, const char *path, const char *text, char *error, size_t size)
{
    return add_file(files, path, text, strlen(text), error, size);
}
static bool add_json(evidence_file **files, const char *path, av_doc *doc, char *error, size_t size)
{
    char *text = av_json_write(av_doc_root(doc));
    bool valid = add_text(files, path, text, error, size);
    free(text);
    return valid;
}
static bool image_file(evidence_file **files, const char *content, const char *key, char *error, size_t size)
{
    const char *extension = key ? strrchr(key, '.') : NULL;
    if (!extension || (strcasecmp(extension, ".png") && strcasecmp(extension, ".jpg") &&
                       strcasecmp(extension, ".jpeg"))) {
        return fail(error, size, "Evidence image dependencies must be PNG/JPEG files");
    }
    for (int i = 0; i < acount(*files); i++) {
        if (!strcmp((*files)[i].path, key)) {
            return image_prepare_check((*files)[i].bytes, (*files)[i].size, error, size) !=
                   NULL;
        }
    }
    if (!valid_key(key)) {
        return fail(error, size, "Image dependency has an unsafe content-relative key");
    }
    char *root = realpath(content, NULL);
    char *path = root ? join(root, key + 8) : NULL;
    /* Reject symlinks on every component, including the leaf. */
    bool valid = path != NULL;
    for (char *at = path ? path + strlen(root) + 1 : NULL; valid && at && *at; at++) {
        if (*at != '/') {
            continue;
        }
        *at = 0;
        struct stat info;
        valid = !lstat(path, &info) && S_ISDIR(info.st_mode);
        *at = '/';
    }
    int fd = valid ? open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK) : -1;
    struct stat info;
    valid = fd >= 0 && !fstat(fd, &info) && S_ISREG(info.st_mode) &&
            info.st_size >= 0 && info.st_size <= FILE_BYTES_MAX;
    FILE *file = valid ? fdopen(fd, "rb") : NULL;
    if (fd >= 0 && !file) {
        close(fd);
    }
    unsigned char *bytes = NULL;
    if (file) {
        size_t count = (size_t)info.st_size;
        bytes = xmalloc(count ? count : 1);
        valid = fread(bytes, 1, count, file) == count && fgetc(file) == EOF &&
                !ferror(file);
        valid = !fclose(file) && valid;
        if (valid) {
            valid = image_prepare_check(bytes, count, error, size) != NULL;
        }
        if (valid) {
            valid = add_file(files, key, bytes, count, error, size);
        }
    } else {
        valid = false;
    }
    free(bytes);
    free(path);
    free(root);
    if (!valid && !error[0]) {
        fail(error, size, "Cannot read bounded regular image dependency without symlinks");
    }
    return valid;
}
static bool settings_valid(const shader_authoring_evidence *in, bool check_png, char *error, size_t size)
{
    if (!in->snapshot || !in->content_root || !in->destination ||
        !*in->destination || !in->backend || !*in->backend ||
        (check_png && (!in->png || !in->png_size || in->png_size > FILE_BYTES_MAX)) ||
        in->width < 16 || in->width > 512 || in->height < 16 || in->height > 512 ||
        in->width * in->height > 640 * 360 || !isfinite(in->time) ||
        fabsf(in->time) > 3600 || !isfinite(in->yaw) || fabsf(in->yaw) > 3.14159266f ||
        !isfinite(in->pitch) || fabsf(in->pitch) > 1.57079633f ||
        !isfinite(in->zoom) || in->zoom < .25f || in->zoom > 4 ||
        !isfinite(in->beat_phase) || in->beat_phase < 0 || in->beat_phase > 1 ||
        !isfinite(in->beat_pulse) || in->beat_pulse < 0 || in->beat_pulse > 1 ||
        (in->snapshot->domain != BEHAVIOR_DOMAIN_MATERIAL &&
         in->snapshot->domain != BEHAVIOR_DOMAIN_SHAPE) ||
        (in->snapshot->domain == BEHAVIOR_DOMAIN_SHAPE && (in->yaw || in->pitch))) {
        return fail(error, size, "Evidence requires a frozen material/shape, PNG, backend and bounded replayable preview settings");
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(in->influence[i]) || in->influence[i] < 0 ||
            in->influence[i] > 64) {
            return fail(error, size, "Evidence influence must be finite and in [0,64]");
        }
    }
    for (int i = 0; in->has_camera && i < 2; i++) {
        if (!isfinite(in->camera_center[i]) ||
            fabsf(in->camera_center[i]) > 1000000 || !isfinite(in->camera_span[i]) ||
            in->camera_span[i] <= 0 || in->camera_span[i] > 1000000) {
            return fail(error, size, "Evidence camera must have bounded center and positive finite span");
        }
    }
    if (!check_png) {
        return true;
    }
    char message[256];
    const char *format = image_prepare_check(in->png, in->png_size, message, sizeof message);
    const unsigned char *bytes = in->png;
    if (!format || strcmp(format, "png") || in->png_size < 24) {
        return fail(error, size, "Evidence preview must be a decodable PNG");
    }
    uint32_t width = (uint32_t)bytes[16] << 24 | (uint32_t)bytes[17] << 16 |
                     (uint32_t)bytes[18] << 8 | bytes[19];
    uint32_t height = (uint32_t)bytes[20] << 24 | (uint32_t)bytes[21] << 16 |
                      (uint32_t)bytes[22] << 8 | bytes[23];
    return (width == (uint32_t)in->width && height == (uint32_t)in->height) ||
           fail(error, size, "PNG dimensions differ from the frozen preview");
}
static av_doc *request_doc(
    const shader_authoring_evidence *in,
    const shader_authoring_report *report,
    const char *path
)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "contract", CHIBI_AUTOMATION_CONTRACT);
    av_obj_add_str(doc, root, "operation", "preview_shader");
    av_obj_add_str(doc, root, "path", path);
    av_obj_add_int(doc, root, "width", in->width);
    av_obj_add_int(doc, root, "height", in->height);
    av_obj_add_float(doc, root, "time", in->time);
    av_obj_add_float(doc, root, "beat_phase", in->beat_phase);
    av_obj_add_float(doc, root, "beat_pulse", in->beat_pulse);
    vector(doc, root, "influence", in->influence, 3);
    av_value *view = av_obj_add_obj(doc, root, "view");
    av_obj_add_float(doc, view, "yaw", in->yaw);
    av_obj_add_float(doc, view, "pitch", in->pitch);
    av_obj_add_float(doc, view, "zoom", in->zoom);
    if (in->has_camera) {
        av_value *camera = av_obj_add_obj(doc, root, "camera");
        vector(doc, camera, "center", in->camera_center, 2);
        vector(doc, camera, "span", in->camera_span, 2);
    }
    if (!in->parameters) {
        return doc; /* Frozen source retains each slot's authored default. */
    }
    av_value *parameters = av_obj_add_obj(doc, root, "parameters");
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        if (!report->widths[i]) {
            continue;
        }
        const float *values = in->parameters[i];
        if (report->widths[i] == 1) {
            av_obj_set(doc, parameters, report->names[i], av_float(doc, values[0]));
        } else {
            vector(doc, parameters, report->names[i], values, report->widths[i]);
        }
    }
    return doc;
}
static void cpu_report(
    av_doc *doc,
    av_value *root,
    const shader_authoring_evidence *in,
    const shader_authoring_report *report
)
{
    av_value *cpu = av_obj_add_obj(doc, root, "cpu_sample");
    av_obj_add_bool(doc, cpu, "attempted", in->cpu_attempted);
    av_obj_add_bool(doc, cpu, "valid", in->cpu_attempted && in->cpu_valid);
    av_obj_add_str(doc, cpu, "coverage", "Frozen caller CPU values; exporter does not evaluate, render or verify correspondence to PNG pixels");
    av_value *context = av_obj_add_obj(doc, cpu, "context");
    vector(doc, context, "position", in->cpu_context.position, 2);
    vector(doc, context, "uv", in->cpu_context.uv, 2);
    vector(doc, context, "object_position", in->cpu_context.object_position, 3);
    vector(doc, context, "part_position", in->cpu_context.part_position, 3);
    vector(doc, context, "world_position", in->cpu_context.world_position, 3);
    vector(doc, context, "normal", in->cpu_context.normal, 3);
    vector(doc, context, "view_direction", in->cpu_context.view_direction, 3);
    av_obj_add_float(doc, context, "time", in->cpu_context.time);
    av_obj_add_float(doc, context, "beat_phase", in->cpu_context.beat_phase);
    av_obj_add_float(doc, context, "beat_pulse", in->cpu_context.beat_pulse);
    vector(doc, context, "object_from_world", &in->cpu_context.object_from_world[0][0], 9);
    vector(doc, context, "part_from_world", &in->cpu_context.part_from_world[0][0], 9);
    vector(doc, context, "uv_from_world", &in->cpu_context.uv_from_world[0][0], 6);
    av_obj_add_str(doc, context, "resolver_scope", "Callback addresses/userdata excluded; copied image dependencies support replay, scene influence callback state is not captured");
    if (!in->cpu_attempted) {
        return;
    }
    if (!in->cpu_valid) {
        av_obj_add_str(doc, cpu, "error", in->cpu_error.message);
        av_obj_add_uint(doc, cpu, "node", in->cpu_error.node);
        return;
    }
    static const char *const names[] = { "base_color", "emission", "roughness",
                                         "metallic",   "normal",   "opacity",
                                         "unlit",      "height",   "occlusion" };
    av_value *outputs = av_obj_add_arr(doc, cpu, "outputs");
    for (int i = 0; i < report->program.output_count; i++) {
        av_value *output = av_arr_add_obj(doc, outputs);
        bool shape = in->snapshot->domain == BEHAVIOR_DOMAIN_SHAPE;
        int width = shape ? 1
                          : (i == SHADER_SURFACE_BASE_COLOR ||
                                     i == SHADER_SURFACE_EMISSION || i == SHADER_SURFACE_NORMAL
                                 ? 3
                                 : 1);
        av_obj_add_str(doc, output, "name", shape ? "distance" : names[i]);
        av_obj_add_str(doc, output, "type", width == 1 ? "float" : "vec3");
        vector(doc, output, "value", in->cpu_outputs[i], width);
    }
}
static bool write_file(const char *destination, const evidence_file *file)
{
    char *path = join(destination, file->path);
    bool valid = true;
    for (char *at = path + strlen(destination) + 1; valid && *at; at++) {
        if (*at != '/') {
            continue;
        }
        *at = 0;
        struct stat info;
        valid = !mkdir(path, 0700) ||
                (errno == EEXIST && !lstat(path, &info) && S_ISDIR(info.st_mode));
        *at = '/';
    }
    FILE *out = valid ? fopen(path, "wx") : NULL;
    free(path);
    if (!out) {
        return false;
    }
    valid = fwrite(file->bytes, 1, file->size, out) == file->size;
    return !fclose(out) && valid;
}
static bool export_frozen(
    const shader_authoring_evidence_plan *plan,
    const void *png,
    size_t png_size,
    char *error,
    size_t size
)
{
    shader_authoring_evidence frozen = plan->input;
    frozen.png = png;
    frozen.png_size = png_size;
    const shader_authoring_evidence *in = &frozen;
    error[0] = 0;
    if (!settings_valid(in, true, error, size)) {
        return false;
    }
    const shader_authoring_report *report = &plan->report;
    evidence_file *files = NULL;
    bool valid = true;
    const char *asset_path = in->snapshot->domain == BEHAVIOR_DOMAIN_SHAPE
                               ? "content/shapes/Snapshot.kdl"
                               : "content/materials/Snapshot.kdl";
    char manifest[128];
    snprintf(manifest, sizeof manifest, "version %d\nname \"Shader evidence\"\ncontent \"content\"\n", CHIBI_PROJECT_VERSION);
    valid = valid && add_text(&files, "project.kdl", manifest, error, size) &&
            add_text(&files, asset_path, plan->source, error, size) &&
            add_file(&files, "preview.png", in->png, in->png_size, error, size);
    av_doc *request = request_doc(in, report, asset_path);
    valid = valid && add_json(&files, "requests.ndjson", request, error, size);
    av_doc_free(request);
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "version", 1);
    av_obj_add_str(doc, root, "producer", "studio_in_process");
    av_obj_add_str(doc, root, "asset_name", in->snapshot->name);
    av_obj_add_str(doc, root, "asset_kind", in->snapshot->domain == BEHAVIOR_DOMAIN_SHAPE ? "shape" : "material");
    av_obj_add_uint(doc, root, "edit_revision", in->edit_revision);
    av_obj_add_str(doc, root, "digest_algorithm", "FNV-1a-64, non-cryptographic");
    av_value *identity = av_obj_add_obj(doc, root, "engine");
    optional(doc, identity, "version", in->engine_version);
    optional(doc, identity, "build_id", in->engine_build_id);
    optional(doc, identity, "manifest_hash", in->engine_manifest_hash);
    av_obj_add_bool(
        doc,
        identity,
        "complete",
        in->engine_version && *in->engine_version && in->engine_build_id &&
            *in->engine_build_id && in->engine_manifest_hash && *in->engine_manifest_hash
    );
    av_value *preview = av_obj_add_obj(doc, root, "preview");
    av_obj_add_str(doc, preview, "file", "preview.png");
    av_obj_add_str(doc, preview, "backend", in->backend);
    optional(doc, preview, "video_driver", in->video_driver);
    av_obj_add_int(doc, preview, "width", in->width);
    av_obj_add_int(doc, preview, "height", in->height);
    av_obj_add_str(doc, preview, "provenance", "Caller attests frozen PNG/settings/snapshot describe the same rendered preview; exporter validates PNG and source but does not rerender");
    request = request_doc(in, report, asset_path);
    av_obj_set(doc, preview, "request", av_clone(doc, av_doc_root(request)));
    av_doc_free(request);
    av_value *coverage = av_obj_add_obj(doc, root, "coverage");
    av_obj_add_bool(doc, coverage, "graph_validated", true);
    av_obj_add_bool(doc, coverage, "cpu_shader_sampling", in->cpu_attempted && in->cpu_valid);
    av_obj_add_bool(doc, coverage, "cpu_runtime", false);
    av_obj_add_bool(doc, coverage, "gpu_rendered", true);
    av_obj_add_bool(doc, coverage, "resident_images_verified", plan->resident_images_verified);
    av_obj_add_bool(doc, coverage, "full_verification", false);
    av_value *costs = av_obj_add_obj(doc, root, "static_costs");
    av_obj_add_int(doc, costs, "nodes", report->costs.nodes);
    av_obj_add_int(doc, costs, "parameters", report->costs.uniforms);
    av_obj_add_int(doc, costs, "images", report->costs.images);
    av_obj_add_int(doc, costs, "texture_calls", report->costs.texture_samples);
    av_obj_add_int(doc, costs, "ramp_vec4s", report->costs.ramp_vec4s);
    av_obj_add_int(doc, costs, "helper_evaluations", report->costs.detail_expressions);
    av_obj_add_str(doc, costs, "coverage", "Static compiler estimates, not GPU timings or full raymarch work");
    av_value *images = av_obj_add_arr(doc, root, "images");
    for (int i = 0; valid && i < acount(report->program.images); i++) {
        const shader_image_source *image = &report->program.images[i];
        bool copied = false;
        for (int j = 0; j < acount(files); j++) {
            copied |= !strcmp(files[j].path, image->key);
        }
        for (int j = 0; !copied && valid && j < acount(plan->images); j++) {
            const evidence_file *file = &plan->images[j];
            if (!strcmp(file->path, image->key)) {
                valid = add_file(&files, file->path, file->bytes, file->size, error, size);
                copied = valid;
            }
        }
        valid = copied && valid;
        av_value *item = av_arr_add_obj(doc, images);
        av_obj_add_str(doc, item, "key", image->key);
        av_obj_add_str(doc, item, "color_space", image->color_space == SHADER_IMAGE_SRGB ? "srgb" : "linear");
        av_obj_add_str(doc, item, "filter", image->filter == SHADER_IMAGE_NEAREST ? "nearest" : "bilinear");
        av_obj_add_str(doc, item, "wrap", image->wrap == SHADER_IMAGE_REPEAT ? "repeat" : "clamp");
    }
    cpu_report(doc, root, in, report);
    av_value *replay = av_obj_add_obj(doc, root, "replay");
    av_value *argv = av_obj_add_arr(doc, replay, "argv");
    const char *arguments[] = { "chibi_ctl", "--project-root", ".", "--render" };
    for (int i = 0; i < 4; i++) {
        av_arr_add_str(doc, argv, arguments[i]);
    }
    av_obj_add_str(doc, replay, "stdin_file", "requests.ndjson");
    av_obj_add_str(doc, replay, "working_directory", "bundle root");
    av_obj_add_str(doc, replay, "requires", "Identical SDK/resources; original backend for pixel comparisons. Browser WebGL capture replayed natively may differ. No source project files needed; this is a preview project, not a gameplay project.");
    av_obj_add_str(doc, replay, "execution_profile", "iteration");
    valid = valid && add_json(&files, "report.json", doc, error, size);
    av_obj_add_bool(doc, root, "evidence_complete", true);
    av_value *records = av_obj_add_arr(doc, root, "files");
    for (int i = 0; i < acount(files); i++) {
        av_value *item = av_arr_add_obj(doc, records);
        av_obj_add_str(doc, item, "path", files[i].path);
        av_obj_add_uint(doc, item, "bytes", files[i].size);
        hash(doc, item, files[i].bytes, files[i].size);
    }
    valid = valid && add_json(&files, "manifest.json", doc, error, size);
    if (valid && mkdir(in->destination, 0700)) {
        valid = fail(error, size, "Evidence destination must be a new directory whose parent exists");
    } else if (valid) {
        for (int i = 0; valid && i < acount(files); i++) {
            valid = write_file(in->destination, &files[i]);
        }
        if (!valid) {
            snprintf(error, size, "Evidence publication failed; partial export retained at %s", in->destination);
        }
    }
    for (int i = 0; i < acount(files); i++) {
        free(files[i].path);
        free(files[i].bytes);
    }
    afree(files);
    av_doc_free(doc);
    return valid;
}

static const evidence_file *prepared_image(const evidence_file *files, const char *key)
{
    for (int i = 0; i < acount(files); i++) {
        if (!strcmp(files[i].path, key)) {
            return &files[i];
        }
    }
    return NULL;
}

static bool resident_matches(
    const shader_authoring_evidence *in,
    const shader_image_source *source,
    const evidence_file *file,
    char *error,
    size_t size
)
{
    shader_image_view resident = { 0 };
    if (!in->cpu_context.resolve_image(in->cpu_context.image_userdata, source, &resident) ||
        !resident.pixels) {
        snprintf(error, size, "Image is not currently resident: %s", source->key);
        return false;
    }
    shader_image_view decoded;
    bool valid = image_prepare_decode(file->bytes, file->size, &decoded, error, size) !=
                 NULL;
    valid = valid && decoded.width == resident.width &&
            decoded.height == resident.height &&
            !memcmp(
                decoded.pixels,
                resident.pixels,
                (size_t)decoded.width * (size_t)decoded.height * 4
            );
    free((void *)decoded.pixels);
    if (!valid) {
        snprintf(
            error,
            size,
            "Disk image pixels differ from the current resident preview: %s",
            source->key
        );
    }
    return valid;
}

void shader_authoring_evidence_free(shader_authoring_evidence_plan *plan)
{
    if (!plan) {
        return;
    }
    behavior_asset_free(plan->snapshot);
    shader_authoring_report_free(&plan->report);
    free(plan->source);
    const char *strings[] = {
        plan->input.content_root,         plan->input.destination,
        plan->input.engine_version,       plan->input.engine_build_id,
        plan->input.engine_manifest_hash, plan->input.backend,
        plan->input.video_driver
    };
    for (int i = 0; i < 7; i++) {
        free((char *)strings[i]);
    }
    for (int i = 0; i < acount(plan->images); i++) {
        free(plan->images[i].path);
        free(plan->images[i].bytes);
    }
    afree(plan->images);
    free(plan);
}

static const char *copy_string(const char *text)
{
    return text ? xstrdup(text) : NULL;
}

static bool parameter_names_distinct(const char *a, const char *b, behavior_domain domain)
{
    const char *prefix = domain == BEHAVIOR_DOMAIN_SHAPE ? "shape." : "material.";
    size_t length = strlen(prefix);
    return strcmp(a, b) && (strncmp(a, prefix, length) || strcmp(a + length, b)) &&
           (strncmp(b, prefix, length) || strcmp(b + length, a));
}

shader_authoring_evidence_plan *shader_authoring_evidence_prepare(
    const shader_authoring_evidence *in,
    char *error,
    size_t size
)
{
    error[0] = 0;
    if (!settings_valid(in, false, error, size) ||
        (in->cpu_valid && !in->cpu_attempted)) {
        if (!error[0]) {
            fail(error, size, "A valid CPU sample requires an attempted frozen sample");
        }
        return NULL;
    }
    char *source = behavior_asset_save(in->snapshot);
    if (!source || strlen(source) > FILE_BYTES_MAX) {
        free(source);
        fail(error, size, "Frozen shader source exceeds 16 MiB or cannot serialize");
        return NULL;
    }
    behavior_asset *loaded = source ? behavior_asset_load(source) : NULL;
    behavior_asset *snapshot = loaded
                                 ? behavior_asset_link_libraries(loaded, NULL, 0, error, size)
                                 : NULL;
    behavior_asset_free(loaded);
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    shader_error problem = { 0 };
    bool valid = snapshot && shader_authoring_inspect(snapshot, &report, &problem);
    if (!valid) {
        free(source);
        behavior_asset_free(snapshot);
        shader_authoring_report_free(&report);
        if (!error[0]) {
            fail(error, size, problem.message[0] ? problem.message : "Snapshot cannot relink without external shader libraries");
        }
        return NULL;
    }
    for (int i = 0; valid && in->parameters && i < SHADER_PARAMETER_MAX; i++) {
        if (!report.widths[i]) {
            continue;
        }
        valid = report.names[i] && *report.names[i];
        for (int j = 0; valid && j < i; j++) {
            valid = !report.widths[j] || parameter_names_distinct(
                                             report.names[j],
                                             report.names[i],
                                             snapshot->domain
                                         );
        }
    }
    if (!valid) {
        free(source);
        behavior_asset_free(snapshot);
        shader_authoring_report_free(&report);
        fail(error, size, "Explicit preview overrides require unique nonempty exposed names without same-domain material./shape. scope aliases");
        return NULL;
    }
    shader_authoring_evidence_plan *plan = xcalloc(1, sizeof *plan);
    plan->snapshot = snapshot;
    plan->source = source;
    plan->report = report;
    plan->input = *in;
    plan->input.snapshot = snapshot;
    plan->input.png = NULL;
    plan->input.png_size = 0;
    plan->input.content_root = copy_string(in->content_root);
    plan->input.destination = copy_string(in->destination);
    plan->input.engine_version = copy_string(in->engine_version);
    plan->input.engine_build_id = copy_string(in->engine_build_id);
    plan->input.engine_manifest_hash = copy_string(in->engine_manifest_hash);
    plan->input.backend = copy_string(in->backend);
    plan->input.video_driver = copy_string(in->video_driver);
    if (in->parameters) {
        memcpy(plan->parameters, in->parameters, sizeof plan->parameters);
        plan->input.parameters = plan->parameters;
    }
    for (int i = 0; valid && i < SHADER_PARAMETER_MAX; i++) {
        for (int j = 0; valid && j < report.widths[i]; j++) {
            valid = isfinite(
                in->parameters ? in->parameters[i][j] : report.defaults[i][j]
            );
        }
    }
    for (int i = 0; valid && in->cpu_valid && i < report.program.output_count; i++) {
        for (int j = 0; valid && j < 4; j++) {
            valid = isfinite(in->cpu_outputs[i][j]);
        }
    }
    if (!valid) {
        fail(error, size, "Preview parameters and successful CPU outputs must be finite");
    }
    for (int i = 0; valid && i < acount(report.program.images); i++) {
        const shader_image_source *image = &report.program.images[i];
        valid = image_file(&plan->images, in->content_root, image->key, error, size);
        if (valid && in->cpu_context.resolve_image) {
            valid = resident_matches(
                in,
                image,
                prepared_image(plan->images, image->key),
                error,
                size
            );
        }
    }
    plan->resident_images_verified = acount(report.program.images) == 0 ||
                                     in->cpu_context.resolve_image != NULL;
    plan->input.cpu_context.resolve_image = NULL;
    plan->input.cpu_context.image_userdata = NULL;
    plan->input.cpu_context.resolve_influence = NULL;
    plan->input.cpu_context.influence_userdata = NULL;
    if (!valid) {
        shader_authoring_evidence_free(plan);
        return NULL;
    }
    return plan;
}

bool shader_authoring_evidence_finalize(
    const shader_authoring_evidence_plan *plan,
    const void *png,
    size_t png_size,
    char *error,
    size_t size
)
{
    return export_frozen(plan, png, png_size, error, size);
}

bool shader_authoring_export_evidence(const shader_authoring_evidence *in, char *error, size_t size)
{
    shader_authoring_evidence_plan *plan = shader_authoring_evidence_prepare(in, error, size);
    if (!plan) {
        return false;
    }
    bool valid = shader_authoring_evidence_finalize(plan, in->png, in->png_size, error, size);
    shader_authoring_evidence_free(plan);
    return valid;
}
