#include "authoring/workspace_runtime_evidence.h"

#include "authoring/automation_contract.h"
#include "authoring/image_prepare.h"
#include "authoring/shader_evidence.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/rooted_files.h"
#include "support/sha256.h"
#include "support/utf8.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    char *path;
    unsigned char *data;
    size_t bytes;
} evidence_file;
struct workspace_runtime_evidence_plan
{
    evidence_file *files;
    size_t bytes, source_budget;
    int visited, width, height, reserved_files;
    av_doc *manifest;
};
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static int entry_compare(const void *a, const void *b)
{
    return strcmp(
        ((const rooted_file_entry *)a)->name,
        ((const rooted_file_entry *)b)->name
    );
}
static bool freeze(
    workspace_runtime_evidence_plan *p,
    rooted_files *root,
    const char *directory,
    int depth,
    char *error,
    size_t size
)
{
    if (depth > 16) {
        return fail(error, size, "Runtime evidence directory depth exceeds16");
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    if (!rooted_files_list(root, directory, 4096 - p->visited, &entries, &count)) {
        return fail(error, size, "Frozen compiler root cannot be inventoried safely");
    }
    p->visited += count;
    if (count > 1) {
        qsort(entries, (size_t)count, sizeof *entries, entry_compare);
    }
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        char relative[WORKSPACE_FILE_PATH_MAX], path[WORKSPACE_FILE_PATH_MAX];
        int n = snprintf(
            relative,
            sizeof relative,
            "%s%s%s",
            directory,
            directory[0] ? "/" : "",
            entries[i].name
        );
        int m = snprintf(path, sizeof path, "project/%s", relative);
        if (n < 0 || (size_t)n >= sizeof relative || m < 0 ||
            (size_t)m >= sizeof path || entries[i].info.link) {
            ok = fail(error, size, "Frozen compiler path is linked or too long");
            break;
        }
        if (entries[i].info.directory) {
            ok = freeze(p, root, relative, depth + 1, error, size);
            continue;
        }
        if (!entries[i].info.regular ||
            acount(p->files) >= WORKSPACE_BUNDLE_FILES_MAX - p->reserved_files ||
            entries[i].info.bytes > WORKSPACE_FILE_BYTES_MAX ||
            p->bytes + entries[i].info.bytes > p->source_budget) {
            ok = fail(error, size, "Runtime evidence exceeds regular-file or bounded source budget");
            break;
        }
        unsigned char *data = NULL;
        size_t bytes = 0;
        if (!rooted_files_read(root, relative, WORKSPACE_FILE_BYTES_MAX, &data, &bytes) ||
            p->bytes + bytes > p->source_budget) {
            free(data);
            ok = fail(error, size, "Frozen compiler file changed or cannot be read safely");
            break;
        }
        apush(p->files, ((evidence_file){ xstrdup(path), data, bytes }));
        p->bytes += bytes;
    }
    rooted_file_entries_free(entries, count);
    return ok;
}
void workspace_runtime_evidence_free(workspace_runtime_evidence_plan *p)
{
    if (!p) {
        return;
    }
    for (int i = 0; i < acount(p->files); i++) {
        free(p->files[i].path);
        free(p->files[i].data);
    }
    afree(p->files);
    av_doc_free(p->manifest);
    free(p);
}
static void vector(av_doc *doc, av_value *object, const char *name, const float *values, int count)
{
    av_value *a = av_obj_add_arr(doc, object, name);
    for (int i = 0; i < count; i++) {
        av_arr_append(a, av_float(doc, values[i]));
    }
}
static bool timeline_valid(const char *text, const char *expected)
{
    if (!text || strlen(text) > 1024u * 1024u || !utf8_text_valid(text, strlen(text))) {
        return false;
    }
    bool quoted = false, escaped = false;
    int depth = 0;
    for (const char *p = text; *p; p++) {
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (*p == '\\') {
                escaped = true;
            } else if (*p == '"') {
                quoted = false;
            }
        } else if (*p == '"') {
            quoted = true;
        } else if (*p == '[' || *p == '{') {
            if (++depth > 6) {
                return false;
            }
        } else if (*p == ']' || *p == '}') {
            if (--depth < 0) {
                return false;
            }
        }
    }
    if (depth || quoted) {
        return false;
    }
    av_doc *doc = av_json_read(text);
    const av_value *root = doc ? av_doc_root(doc) : NULL;
    const av_value *keys = av_obj_get(root, "keys"),
                   *version = av_obj_get(root, "version"),
                   *identity = av_obj_get(root, "identity");
    const av_value *initial_save = av_obj_get(root, "initial_save");
    bool ok = av_is_obj(root) && av_obj_size(root) == 6 &&
              av_is_obj(initial_save) && !av_obj_size(initial_save) &&
              av_is_int(version) && av_get_int(version) == 2 &&
              av_is_str(identity) && av_get_len(identity) == 64 &&
              (!expected || !strcmp(av_get_str(identity), expected)) &&
              av_is_arr(keys) && av_arr_size(keys) > 0 && av_arr_size(keys) <= 4096;
    av_doc_free(doc);
    return ok;
}
workspace_runtime_evidence_plan *workspace_runtime_evidence_prepare(
    const workspace_runtime_evidence_input *in,
    char *error,
    size_t size
)
{
    if (!in || !in->frozen_root || !in->frozen_root[0] || !in->artifact_relative ||
        !in->artifact_relative[0] || !in->executed_artifact_sha256 ||
        strlen(in->executed_artifact_sha256) != 64 || !in->backend ||
        !in->backend[0] || in->width < 1 || in->height < 1 ||
        in->width > 2048 || in->height > 2048 || !isfinite(in->host_time) ||
        !isfinite(in->simulation_time) || !in->replay_trace_json ||
        strlen(in->replay_trace_json) > 1024u * 1024u) {
        fail(error, size, "Runtime evidence requires frozen executable lineage and finite last-draw settings");
        return NULL;
    }
    if (!shader_authoring_evidence_identity_valid(in->engine_version, in->build_id, in->manifest_hash, error, size)) {
        return NULL;
    }
    const engine_viewport_view *view = &in->camera;
    const float fields[] = { view->center_x,
                             view->center_y,
                             view->width,
                             view->height,
                             view->target_elevation,
                             view->yaw,
                             view->pitch,
                             view->roll,
                             view->focal_distance,
                             view->lens.eye.x,
                             view->lens.eye.y,
                             view->lens.eye.z,
                             view->lens.forward.x,
                             view->lens.forward.y,
                             view->lens.forward.z,
                             view->lens.up.x,
                             view->lens.up.y,
                             view->lens.up.z,
                             view->lens.fov,
                             view->lens.near_clip,
                             view->lens.far_clip,
                             in->grid };
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++) {
        if (!isfinite(fields[i])) {
            fail(error, size, "Runtime evidence camera/settings must be finite");
            return NULL;
        }
    }
    for (int i = 0; i < 16; i++) {
        if (!isfinite(in->view_matrix.elements[i]) ||
            !isfinite(in->projection_matrix.elements[i])) {
            fail(error, size, "Runtime evidence camera matrices must be finite");
            return NULL;
        }
    }
    if (view->width <= 0 || view->height <= 0 || in->grid < 0) {
        fail(error, size, "Runtime evidence requires positive camera spans");
        return NULL;
    }
    int nesting = 0;
    bool quoted = false, escaped = false;
    for (const char *at = in->replay_trace_json; *at; at++) {
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (*at == '\\') {
                escaped = true;
            } else if (*at == '"') {
                quoted = false;
            }
        } else if (*at == '"') {
            quoted = true;
        } else if (*at == '[' || *at == '{') {
            if (++nesting > 8) {
                fail(error, size, "Runtime evidence trace nesting exceeds8");
                return NULL;
            }
        } else if (*at == ']' || *at == '}') {
            nesting--;
        }
    }
    av_doc *trace = av_json_read(in->replay_trace_json);
    if (!trace || av_get_type(av_doc_root(trace)) != AV_ARRAY ||
        av_arr_size(av_doc_root(trace)) > 4096) {
        av_doc_free(trace);
        fail(error, size, "Runtime replay trace is invalid JSON");
        return NULL;
    }
    if (in->input_timeline_json && !timeline_valid(in->input_timeline_json, NULL)) {
        av_doc_free(trace);
        fail(error, size, "Runtime input timeline attachment is invalid or exceeds bounds");
        return NULL;
    }
    workspace_runtime_evidence_plan *p = xcalloc(1, sizeof *p);
    p->source_budget = (in->input_timeline_json ? 45u : 46u) * 1024u * 1024u;
    p->reserved_files = in->input_timeline_json ? 3 : 2;
    p->width = in->width;
    p->height = in->height;
    rooted_files *root = rooted_files_open(in->frozen_root);
    bool ok = root ? freeze(p, root, "", 0, error, size)
                   : fail(error, size, "Frozen compiler root is unavailable");
    rooted_files_close(root);
    char artifact[WORKSPACE_FILE_PATH_MAX];
    int n = snprintf(artifact, sizeof artifact, "project/%s", in->artifact_relative);
    bool found = false;
    size_t artifact_bytes = 0;
    char artifact_hash[65];
    if (n > 0 && (size_t)n < sizeof artifact) {
        for (int i = 0; i < acount(p->files); i++) {
            if (!strcmp(p->files[i].path, artifact)) {
                found = true;
                artifact_bytes = p->files[i].bytes;
                sha256_hex(p->files[i].data, p->files[i].bytes, artifact_hash);
            }
        }
    }
    if (ok && found &&
        (artifact_bytes != in->executed_artifact_bytes ||
         strcmp(artifact_hash, in->executed_artifact_sha256))) {
        ok = fail(error, size, "Frozen executable differs from the bytes actually decoded by the player");
    }
    if (!ok || !found) {
        if (ok) {
            fail(error, size, "Frozen executable is absent from evidence source inventory");
        }
        av_doc_free(trace);
        workspace_runtime_evidence_free(p);
        return NULL;
    }
    if (in->input_timeline_json) {
        char identity[65], combined[197];
        bool bounded = strlen(in->build_id) == 64 && strlen(in->manifest_hash) == 64;
        if (bounded) {
            snprintf(combined, sizeof combined, "%s:%s:%s", artifact_hash, in->build_id, in->manifest_hash);
            sha256_hex(combined, strlen(combined), identity);
        }
        if (!bounded || !timeline_valid(in->input_timeline_json, identity)) {
            av_doc_free(trace);
            workspace_runtime_evidence_free(p);
            fail(error, size, "Input timeline executable/SDK identity differs from frozen evidence");
            return NULL;
        }
        size_t bytes = strlen(in->input_timeline_json);
        apush(
            p->files,
            ((evidence_file){ xstrdup("input-timeline.json"),
                              (unsigned char *)xstrdup(in->input_timeline_json),
                              bytes })
        );
        p->bytes += bytes;
    }
    p->manifest = av_doc_new();
    av_value *m = av_obj(p->manifest);
    av_doc_set_root(p->manifest, m);
    av_obj_add_int(p->manifest, m, "automation_contract", CHIBI_AUTOMATION_CONTRACT);
    av_obj_add_str(p->manifest, m, "kind", "runtime-viewport-evidence");
    av_obj_add_str(p->manifest, m, "executable", artifact);
    av_value *executed = av_obj_add_obj(p->manifest, m, "executed_artifact");
    av_obj_add_str(p->manifest, executed, "sha256", artifact_hash);
    av_obj_add_uint(p->manifest, executed, "bytes", artifact_bytes);
    av_obj_add_str(p->manifest, m, "scene", in->scene ? in->scene : "");
    av_value *sdk = av_obj_add_obj(p->manifest, m, "sdk");
    av_obj_add_str(p->manifest, sdk, "version", in->engine_version);
    av_obj_add_str(p->manifest, sdk, "build_id", in->build_id);
    av_obj_add_str(p->manifest, sdk, "manifest_sha256", in->manifest_hash);
    av_obj_add_str(p->manifest, m, "backend", in->backend);
    av_obj_add_str(p->manifest, m, "video_driver", in->video_driver ? in->video_driver : "");
    av_obj_add_int(p->manifest, m, "width", in->width);
    av_obj_add_int(p->manifest, m, "height", in->height);
    av_obj_add_int(p->manifest, m, "session", in->session);
    av_obj_add_int(p->manifest, m, "document", in->document);
    av_obj_add_int(p->manifest, m, "revision", in->revision);
    av_obj_add_uint(p->manifest, m, "fixed_steps", in->steps);
    av_obj_set(p->manifest, m, "simulation_time", av_real(p->manifest, in->simulation_time));
    av_obj_set(p->manifest, m, "host_time", av_real(p->manifest, in->host_time));
    av_obj_add_bool(p->manifest, m, "playing", in->playing);
    av_value *camera = av_obj_add_obj(p->manifest, m, "camera");
    const engine_viewport_view *v = &in->camera;
    vector(p->manifest, camera, "center", (float[]){ v->center_x, v->center_y, v->target_elevation }, 3);
    vector(p->manifest, camera, "span", (float[]){ v->width, v->height }, 2);
    vector(p->manifest, camera, "angles", (float[]){ v->yaw, v->pitch, v->roll }, 3);
    vector(
        p->manifest,
        camera,
        "eye",
        (float[]){ v->lens.eye.x, v->lens.eye.y, v->lens.eye.z },
        3
    );
    vector(
        p->manifest,
        camera,
        "forward",
        (float[]){ v->lens.forward.x, v->lens.forward.y, v->lens.forward.z },
        3
    );
    vector(
        p->manifest,
        camera,
        "up",
        (float[]){ v->lens.up.x, v->lens.up.y, v->lens.up.z },
        3
    );
    vector(
        p->manifest,
        camera,
        "lens",
        (float[]){ v->lens.fov, v->lens.near_clip, v->lens.far_clip, v->focal_distance },
        4
    );
    av_obj_add_bool(p->manifest, camera, "perspective", in->perspective);
    vector(p->manifest, camera, "view_matrix", in->view_matrix.elements, 16);
    vector(p->manifest, camera, "projection_matrix", in->projection_matrix.elements, 16);
    av_obj_add_float(p->manifest, camera, "grid", in->grid);
    av_value *presentation = av_obj_add_obj(p->manifest, m, "presentation");
    av_obj_add_bool(p->manifest, presentation, "unlit", in->unlit);
    av_obj_add_bool(p->manifest, presentation, "fogless", in->fogless);
    av_obj_add_bool(p->manifest, presentation, "colliders", in->colliders);
    av_obj_add_bool(p->manifest, presentation, "camera_frames", in->camera_frames);
    av_obj_add_bool(p->manifest, presentation, "debug_truncated", in->debug_truncated);
    av_obj_add_int(p->manifest, presentation, "debug_unsupported", in->debug_unsupported);
    av_value *coverage = av_obj_add_obj(p->manifest, m, "coverage");
    av_obj_add_bool(p->manifest, coverage, "production_viewport_rendered", in->gpu_rendered);
    av_obj_add_bool(p->manifest, coverage, "frozen_compiler_sources", true);
    av_obj_add_bool(p->manifest, coverage, "input_timeline_frozen", in->input_timeline_json != NULL);
    if (in->input_timeline_json) {
        av_obj_add_str(p->manifest, coverage, "initial_save_baseline", "empty in-memory store");
    }
    av_obj_add_bool(p->manifest, coverage, "gameplay_validated", false);
    av_obj_add_bool(p->manifest, coverage, "replay_executed", false);
    av_obj_add_bool(p->manifest, coverage, "random_stream_isolated", in->random_isolated);
    av_obj_add_bool(p->manifest, coverage, "untracked_mutations", in->untracked);
    av_obj_add_bool(p->manifest, coverage, "trace_truncated", in->trace_truncated);
    av_value *random = av_obj_add_obj(p->manifest, m, "random");
    av_obj_add_str(p->manifest, random, "generator", in->random_isolated ? "PCG-XSH-RR32-fixed-stream54" : "unverified");
    av_obj_add_uint(p->manifest, random, "state_after_draw", in->random_state);
    av_obj_add_bool(p->manifest, random, "isolated", in->random_isolated);
    av_obj_add_str(p->manifest, coverage, "limits", "Render evidence only. Trace records host calls, explicit target UI ingress and fixed-step timing; optional input-timeline.json replays gameplay controls only. UI interactions and external world mutation are not replayed. Identical SDK/resources/backend required for pixel comparisons. No gameplay assertions or checkpoint/state restoration are proven.");
    av_obj_set(p->manifest, m, "replay_trace", av_clone(p->manifest, av_doc_root(trace)));
    av_doc_free(trace);
    return p;
}
bool workspace_runtime_evidence_finalize(
    const workspace_runtime_evidence_plan *p,
    workspace_files *files,
    const char *directory,
    const void *png,
    size_t bytes,
    char *error,
    size_t size
)
{
    if (!p || !png || !bytes || bytes > WORKSPACE_FILE_BYTES_MAX) {
        return fail(error, size, "Runtime evidence PNG absent or exceeds16MiB");
    }
    shader_image_view image = { 0 };
    const char *format = image_prepare_decode(png, bytes, &image, error, size);
    bool valid = format && !strcmp(format, "png") && image.width == p->width &&
                 image.height == p->height;
    free((void *)image.pixels);
    if (!valid) {
        return fail(error, size, "Runtime evidence PNG differs from frozen viewport dimensions");
    }
    av_doc *doc = av_doc_new();
    av_value *m = av_clone(doc, av_doc_root(p->manifest));
    av_doc_set_root(doc, m);
    av_value *inventory = av_obj_add_arr(doc, m, "files");
    workspace_file_entry entries[WORKSPACE_BUNDLE_FILES_MAX];
    int count = 0;
    for (int i = 0; i < acount(p->files); i++) {
        entries[count++] = (workspace_file_entry){ p->files[i].path,
                                                   p->files[i].data,
                                                   p->files[i].bytes };
    }
    entries[count++] = (workspace_file_entry){ "viewport.png", png, bytes };
    for (int i = 0; i < count; i++) {
        char hash[65];
        sha256_hex(entries[i].data, entries[i].bytes, hash);
        av_value *row = av_obj(doc);
        av_arr_append(inventory, row);
        av_obj_add_str(doc, row, "path", entries[i].path);
        av_obj_add_int(doc, row, "bytes", entries[i].bytes);
        av_obj_add_str(doc, row, "sha256", hash);
    }
    char *json = av_json_write(m);
    bool ok = json && strlen(json) <= WORKSPACE_FILE_BYTES_MAX;
    if (ok) {
        entries[count++] = (workspace_file_entry){ "manifest.json", json, strlen(json) };
        ok = workspace_file_bundle_write_new(files, directory, entries, count, error, size);
    } else {
        fail(error, size, "Runtime evidence manifest exceeds bounded size");
    }
    free(json);
    av_doc_free(doc);
    return ok;
}
