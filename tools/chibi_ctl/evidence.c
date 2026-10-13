#include "evidence.h"

#include "assets/project.h"
#include "authoring/resource_catalog.h"
#include "support/asset_json.h"
#include "support/resources.h"
#include "support/xalloc.h"

#include <cute_base64.h>
#include <cute_image.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PREVIEW_PNG_BYTES_MAX (72 * 1024 * 1024)
#define PREVIEW_SIDE_MAX 4096
#define MANIFEST_BYTES_MAX (2 * 1024 * 1024)
#define FNV_OFFSET UINT64_C(14695981039346656037)
struct chibi_ctl_evidence
{
    char *directory, *project;
    FILE *requests, *responses;
    av_doc *doc;
    av_value *root, *operations;
    uint64_t request_hash, response_hash;
    int count;
    bool failed, static_validation, cpu_runtime, cpu_shader_sampling, render,
        gpu_rendered;
};

static uint64_t digest(uint64_t hash, const char *bytes, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ (unsigned char)bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static void hash_field(av_doc *doc, av_value *root, const char *key, uint64_t hash)
{
    char text[17];
    snprintf(text, sizeof text, "%016" PRIx64, hash);
    av_obj_add_str(doc, root, key, text);
}

static FILE *open_file(chibi_ctl_evidence *e, const char *name)
{
    size_t size = strlen(e->directory) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", e->directory, name);
    FILE *file = fopen(path, "wx");
    free(path);
    if (!file) {
        perror("chibi_ctl evidence");
        e->failed = true;
    }
    return file;
}

static void manifest_snapshot(chibi_ctl_evidence *e, av_value *value, const char *key)
{
    av_value *manifest = av_obj_add_obj(e->doc, value, "manifest");
    size_t size = strlen(e->project) + 32;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/project.kdl", e->project);
    struct stat info;
    int inspected = lstat(path, &info);
    av_obj_add_bool(e->doc, manifest, "exists", inspected == 0);
    if (inspected) {
        av_obj_add_bool(e->doc, manifest, "available", errno == ENOENT);
        free(path);
        return;
    }
    int fd = S_ISREG(info.st_mode) && info.st_size >= 0 && info.st_size <= MANIFEST_BYTES_MAX
               ? open(path, O_RDONLY | O_NOFOLLOW)
               : -1;
    free(path);
    if (fd < 0) {
        av_obj_add_bool(e->doc, manifest, "available", false);
        av_obj_add_str(e->doc, manifest, "reason", "Manifest must be a readable regular file <= 2 MiB; symlinks excluded.");
        return;
    }
    FILE *input = fdopen(fd, "rb");
    if (!input) {
        close(fd);
        av_obj_add_bool(e->doc, manifest, "available", false);
        return;
    }
    size_t bytes = (size_t)info.st_size;
    char *content = xmalloc(bytes ? bytes : 1);
    bool valid = fread(content, 1, bytes, input) == bytes &&
                 fgetc(input) == EOF && !ferror(input);
    valid = !fclose(input) && valid;
    av_obj_add_bool(e->doc, manifest, "available", valid);
    if (valid) {
        char filename[64];
        snprintf(filename, sizeof filename, "%s.kdl", key);
        FILE *output = open_file(e, filename);
        if (output) {
            bool saved = fwrite(content, 1, bytes, output) == bytes;
            saved = !fclose(output) && saved;
            e->failed |= !saved;
        }
        av_obj_add_str(e->doc, manifest, "bytes_file", filename);
        av_obj_add_uint(e->doc, manifest, "bytes", bytes);
        hash_field(e->doc, manifest, "fnv1a64", digest(FNV_OFFSET, content, bytes));
    }
    free(content);
}

static void snapshot(chibi_ctl_evidence *e, const char *key)
{
    char error[512];
    char *content = chibi_project_content(e->project, error, sizeof error);
    av_doc *catalog = content
                        ? authoring_resource_catalog(content, error, sizeof error)
                        : NULL;
    free(content);
    av_value *value = av_obj_add_obj(e->doc, e->root, key);
    manifest_snapshot(e, value, key);
    av_obj_add_bool(e->doc, value, "available", catalog != NULL);
    if (!catalog) {
        av_obj_add_str(e->doc, value, "error", error);
        return;
    }
    char *json = av_json_write(av_doc_root(catalog));
    hash_field(e->doc, value, "catalog_fnv1a64", digest(FNV_OFFSET, json, strlen(json)));
    av_obj_set(e->doc, value, "catalog", av_clone(e->doc, av_doc_root(catalog)));
    free(json);
    av_doc_free(catalog);
}

static void binary_identity(chibi_ctl_evidence *e, const char *program)
{
    FILE *file = fopen(program, "rb");
    av_value *value = av_obj_add_obj(e->doc, e->root, "executable");
    av_obj_add_str(e->doc, value, "invoked_path", program);
    av_obj_add_bool(e->doc, value, "hash_available", file != NULL);
    if (!file) {
        return;
    }
    uint64_t hash = FNV_OFFSET;
    char buffer[8192];
    size_t size;
    while ((size = fread(buffer, 1, sizeof buffer, file))) {
        hash = digest(hash, buffer, size);
    }
    if (!ferror(file)) {
        hash_field(e->doc, value, "fnv1a64", hash);
    } else {
        av_obj_set(e->doc, value, "hash_available", av_bool(e->doc, false));
    }
    fclose(file);
}

chibi_ctl_evidence *chibi_ctl_evidence_begin(
    const char *directory,
    const char *project,
    bool render,
    int argc,
    char **argv
)
{
    char error[512];
    char *content = chibi_project_content(project, error, sizeof error);
    char *canonical_content = content ? realpath(content, NULL) : NULL;
    free(content);
    char *parent = xstrdup(directory);
    char *slash = strrchr(parent, '/');
    if (slash) {
        slash[slash == parent ? 1 : 0] = 0;
    } else {
        free(parent);
        parent = xstrdup(".");
    }
    char *canonical_parent = realpath(parent, NULL);
    free(parent);
    bool inside = canonical_content && canonical_parent &&
                  !strncmp(canonical_parent, canonical_content, strlen(canonical_content)) &&
                  (canonical_parent[strlen(canonical_content)] == 0 ||
                   canonical_parent[strlen(canonical_content)] == '/');
    free(canonical_content);
    free(canonical_parent);
    if (inside) {
        fprintf(stderr, "chibi_ctl --evidence: directory must be outside project content\n");
        return NULL;
    }
    if (mkdir(directory, 0700)) {
        perror("chibi_ctl --evidence: create new directory");
        return NULL;
    }
    chibi_ctl_evidence *e = xcalloc(1, sizeof *e);
    e->directory = xstrdup(directory);
    e->project = xstrdup(project);
    e->request_hash = e->response_hash = FNV_OFFSET;
    e->doc = av_doc_new();
    e->root = av_obj(e->doc);
    av_doc_set_root(e->doc, e->root);
    av_obj_add_int(e->doc, e->root, "version", 1);
    av_obj_add_str(e->doc, e->root, "digest_algorithm", "FNV-1a-64, non-cryptographic");
    av_obj_add_str(e->doc, e->root, "project_root", project);
    av_obj_add_str(e->doc, e->root, "project_identity_scope", "Default project manifest bytes and content catalog. External model inputs, external shader source bytes, per-request project_root overrides and files beyond catalog/manifest limits are not fingerprinted. Consult each catalog complete flag and manifest available flag.");
    char *cwd = getcwd(NULL, 0);
    av_obj_add_str(e->doc, e->root, "working_directory", cwd ? cwd : "unavailable");
    free(cwd);
    av_value *arguments = av_obj_add_arr(e->doc, e->root, "invocation_argv");
    e->render = render;
    for (int i = 0; i < argc; i++) {
        av_arr_add_str(e->doc, arguments, argv[i]);
    }
    av_obj_add_str(e->doc, e->root, "request_transport", e->render ? "single_json_preview" : "ndjson_automation");
    av_value *replay = av_obj_add_obj(e->doc, e->root, "replay");
    av_value *replay_args = av_obj_add_arr(e->doc, replay, "argv");
    av_arr_add_str(e->doc, replay_args, argv[0]);
    av_arr_add_str(e->doc, replay_args, "--project-root");
    av_arr_add_str(e->doc, replay_args, project);
    if (e->render) {
        av_arr_add_str(e->doc, replay_args, "--render");
    }
    av_obj_add_str(e->doc, replay, "stdin_file", "requests.ndjson");
    av_value *environment = av_obj_add_obj(e->doc, replay, "environment");
    static const char *const names[] = { "CHIBI_RESOURCE_DIR",
                                         "CHIBI_RENDER_SHADER_DIR",
                                         "CHIBI_EXECUTION_PROFILE",
                                         "CHIBI_ALLOW_FULL_GPU",
                                         "CHIBI_PREVIEW_DISPLAY_CONTEXT",
                                         "SDL_VIDEO_DRIVER",
                                         "SDL_VIDEODRIVER",
                                         "DISPLAY",
                                         "WAYLAND_DISPLAY",
                                         "MTL_HUD_ENABLED" };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        const char *value = getenv(names[i]);
        av_obj_set(e->doc, environment, names[i], value ? av_str(e->doc, value) : av_null(e->doc));
    }
    av_obj_add_str(e->doc, replay, "environment_scope", "Explicit resource/execution/video/display/instrumentation allowlist only. Restore string values and unset null entries. Other environment is not captured; supervisor deadlines must be fresh.");
    av_obj_add_str(e->doc, replay, "requires", "Original working directory, identical engine and project inputs. Redirect the saved stdin_file into argv; preview replay uses saved bytes rather than the original --render-request path. Mutating commands require restoring the before-project inputs; this bundle is not a backup.");
    char *info = resource_info();
    av_doc *identity = info ? av_json_read(info) : NULL;
    av_obj_set(
        e->doc,
        e->root,
        "engine",
        identity ? av_clone(e->doc, av_doc_root(identity)) : av_null(e->doc)
    );
    free(info);
    av_doc_free(identity);
    binary_identity(e, argv[0]);
    e->operations = av_obj_add_arr(e->doc, e->root, "operations");
    snapshot(e, "project_before");
    e->requests = open_file(e, "requests.ndjson");
    e->responses = open_file(e, "responses.ndjson");
    return e;
}

bool chibi_ctl_evidence_request(chibi_ctl_evidence *e, const char *request, size_t size)
{
    if (!e) {
        return true;
    }
    if (!e->requests || fwrite(request, 1, size, e->requests) != size ||
        fflush(e->requests)) {
        e->failed = true;
        return false;
    }
    e->request_hash = digest(e->request_hash, request, size);
    return true;
}

static bool export_preview(chibi_ctl_evidence *e, av_value *entry, const av_value *preview)
{
    av_value *artifact = av_obj_add_obj(e->doc, entry, "preview_artifact");
    const char *data = av_get_str(av_obj_get(preview, "data"));
    size_t length = av_get_len(av_obj_get(preview, "data"));
    int width = av_get_int(av_obj_get(preview, "width"));
    int height = av_get_int(av_obj_get(preview, "height"));
    bool valid = data && length >= 4 && length % 4 == 0 &&
                 length <= CF_BASE64_ENCODED_SIZE(PREVIEW_PNG_BYTES_MAX) &&
                 av_is_int(av_obj_get(preview, "width")) &&
                 av_is_int(av_obj_get(preview, "height")) &&
                 av_get_num(av_obj_get(preview, "width")) > 0 &&
                 av_get_num(av_obj_get(preview, "width")) <= PREVIEW_SIDE_MAX &&
                 av_get_num(av_obj_get(preview, "height")) > 0 &&
                 av_get_num(av_obj_get(preview, "height")) <= PREVIEW_SIDE_MAX;
    unsigned char *bytes = NULL;
    size_t size = 0;
    if (valid) {
        size_t capacity = CF_BASE64_DECODED_SIZE(length);
        size = capacity - (data[length - 1] == '=') - (data[length - 2] == '=');
        bytes = xmalloc(capacity);
        valid = !cf_is_error(cf_base64_decode(bytes, capacity, data, length));
    }
    static const unsigned char signature[] = { 137, 'P', 'N', 'G',
                                               13,  10,  26,  10 };
    if (valid) {
        valid = size >= 33 && size <= PREVIEW_PNG_BYTES_MAX &&
                !memcmp(bytes, signature, sizeof signature);
    }
    int decoded_width = 0, decoded_height = 0;
    if (valid) {
        valid = !cf_is_error(
                    cf_image_load_png_wh(bytes, (int)size, &decoded_width, &decoded_height)
                ) &&
                decoded_width == width && decoded_height == height;
    }
    CF_Image image = { 0 };
    if (valid) {
        valid = !cf_is_error(cf_image_load_png_from_memory(bytes, (int)size, &image)) &&
                image.w == width && image.h == height;
    }
    cf_image_free(&image);
    if (!valid) {
        av_obj_add_bool(e->doc, artifact, "available", false);
        av_obj_add_str(e->doc, artifact, "reason", "Expected decodable base64 PNG <= 72 MiB, dimensions matching native metadata and <= 4096 per side.");
        free(bytes);
        return false;
    }
    char filename[64];
    snprintf(filename, sizeof filename, "preview-%06d.png", e->count + 1);
    FILE *file = open_file(e, filename);
    bool saved = false;
    if (file) {
        saved = fwrite(bytes, 1, size, file) == size;
        saved = !fclose(file) && saved;
        e->failed |= !saved;
    }
    av_obj_add_bool(e->doc, artifact, "available", saved);
    if (saved) {
        av_obj_add_str(e->doc, artifact, "file", filename);
        av_obj_add_uint(e->doc, artifact, "bytes", size);
        hash_field(e->doc, artifact, "fnv1a64", digest(FNV_OFFSET, (const char *)bytes, size));
    }
    free(bytes);
    return saved;
}

bool chibi_ctl_evidence_response(chibi_ctl_evidence *e, const char *request, const char *response, int status)
{
    if (!e) {
        return true;
    }
    size_t size = strlen(response);
    if (!e->responses || fwrite(response, 1, size, e->responses) != size ||
        fputc('\n', e->responses) == EOF || fflush(e->responses)) {
        e->failed = true;
        return false;
    }
    e->response_hash = digest(e->response_hash, response, size);
    e->response_hash = digest(e->response_hash, "\n", 1);
    av_doc *input = av_json_read(request), *output = av_json_read(response);
    const char *operation = av_get_str(av_obj_get(av_doc_root(input), "operation"));
    bool ok = status == 0 && av_get_bool(av_obj_get(av_doc_root(output), "ok"));
    av_value *entry = av_arr_add_obj(e->doc, e->operations);
    av_obj_add_str(e->doc, entry, "operation", operation ? operation : "invalid_request");
    av_obj_add_int(e->doc, entry, "exit_status", status);
    av_obj_add_bool(e->doc, entry, "ok", ok);
    const char *coverage = "operation_only";
    if (ok && operation &&
        (!strcmp(operation, "validate_project") ||
         !strcmp(operation, "validate_asset"))) {
        coverage = "static_validation";
        e->static_validation = true;
    }
    if (ok && operation &&
        (!strcmp(operation, "run_scenario") || !strcmp(operation, "replay_scenario"))) {
        coverage = "cpu_runtime";
        e->cpu_runtime = true;
    }
    av_value *result = av_obj_get(av_doc_root(output), "result");
    av_value *sample = av_obj_get(av_obj_get(result, "shader_report"), "cpu_sample");
    if (ok && operation && !strcmp(operation, "inspect_graph") &&
        av_get_bool(av_obj_get(av_doc_root(input), "cpu_sample")) &&
        av_get_bool(av_obj_get(sample, "valid"))) {
        coverage = "cpu_shader_sampling";
        e->cpu_shader_sampling = true;
    }
    av_value *preview = av_obj_get(result, "preview");
    const char *backend = av_get_str(av_obj_get(result, "backend"));
    const char *mime = av_get_str(av_obj_get(preview, "mime_type"));
    const char *data = av_get_str(av_obj_get(preview, "data"));
    if (e->render && ok && operation &&
        (!strcmp(operation, "preview_shader") || !strcmp(operation, "preview_scene")) &&
        backend && *backend && mime && !strcmp(mime, "image/png") && data &&
        *data && export_preview(e, entry, preview)) {
        coverage = "gpu_rendered";
        e->gpu_rendered = true;
        av_value *metadata = av_obj_add_obj(e->doc, entry, "preview_metadata");
        const char *fields[] = { "backend", "video_driver", "performance" };
        for (int i = 0; i < 3; i++) {
            av_value *value = av_obj_get(result, fields[i]);
            if (value) {
                av_obj_set(e->doc, metadata, fields[i], av_clone(e->doc, value));
            }
        }
        av_obj_set(e->doc, metadata, "width", av_clone(e->doc, av_obj_get(preview, "width")));
        av_obj_set(e->doc, metadata, "height", av_clone(e->doc, av_obj_get(preview, "height")));
        av_obj_add_str(e->doc, metadata, "window", "Native preview renderer uses a hidden window; full native response retained in responses.ndjson.");
    }
    av_obj_add_str(e->doc, entry, "coverage", coverage);
    e->count++;
    av_doc_free(input);
    av_doc_free(output);
    return !e->failed;
}

bool chibi_ctl_evidence_finish(chibi_ctl_evidence *e, int status)
{
    if (!e) {
        return true;
    }
    if (e->requests && fclose(e->requests)) {
        e->failed = true;
    }
    if (e->responses && fclose(e->responses)) {
        e->failed = true;
    }
    snapshot(e, "project_after");
    char *info = resource_info();
    av_doc *identity = info ? av_json_read(info) : NULL;
    av_obj_set(
        e->doc,
        e->root,
        "engine_after",
        identity ? av_clone(e->doc, av_doc_root(identity)) : av_null(e->doc)
    );
    free(info);
    av_doc_free(identity);
    av_obj_add_int(e->doc, e->root, "exit_status", status);
    av_obj_add_int(e->doc, e->root, "response_count", e->count);
    hash_field(e->doc, e->root, "requests_fnv1a64", e->request_hash);
    hash_field(e->doc, e->root, "responses_fnv1a64", e->response_hash);
    av_value *coverage = av_obj_add_obj(e->doc, e->root, "coverage");
    av_obj_add_bool(e->doc, coverage, "static_validation", e->static_validation);
    av_obj_add_bool(e->doc, coverage, "cpu_runtime", e->cpu_runtime);
    av_obj_add_bool(e->doc, coverage, "cpu_shader_sampling", e->cpu_shader_sampling);
    av_obj_add_bool(e->doc, coverage, "gpu_rendered", e->gpu_rendered);
    av_obj_add_bool(e->doc, coverage, "full_verification", false);
    FILE *file = open_file(e, "manifest.json");
    av_obj_add_bool(e->doc, e->root, "evidence_complete", !e->failed);
    char *json = av_json_write(e->root);
    if (file) {
        if (fputs(json, file) == EOF || fputc('\n', file) == EOF) {
            e->failed = true;
        }
        if (fclose(file)) {
            e->failed = true;
        }
    }
    bool ok = !e->failed;
    free(json);
    av_doc_free(e->doc);
    free(e->directory);
    free(e->project);
    free(e);
    return ok;
}
