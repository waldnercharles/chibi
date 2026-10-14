#include "authoring/model_evidence.h"

#include "assets/project.h"
#include "authoring/automation_contract.h"
#include "authoring/image_prepare.h"
#include "authoring/shader_evidence.h"
#include "authoring/shader_report.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "runtime/models/model_inspection.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/sha256.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    FILES_MAX = 128,
    BYTES_MAX = 64 * 1024 * 1024
};
typedef struct
{
    char *path;
    void *data;
    size_t bytes;
} frozen_file;
struct model_evidence_plan
{
    frozen_file *files;
    size_t bytes;
    av_doc *manifest;
    int width, height;
};
static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}
static bool add(model_evidence_plan *p, const char *path, const void *data, size_t bytes, char *error, size_t size)
{
    if (acount(p->files) >= FILES_MAX - 2 || bytes > WORKSPACE_FILE_BYTES_MAX ||
        p->bytes + bytes > BYTES_MAX - 2 * WORKSPACE_FILE_BYTES_MAX) {
        return fail(error, size, "Frozen model evidence exceeds its file or byte budget");
    }
    for (int i = 0; i < acount(p->files); i++) {
        if (!strcmp(p->files[i].path, path)) {
            return true;
        }
    }
    void *copy = bytes ? xmalloc(bytes) : NULL;
    if (bytes) {
        memcpy(copy, data, bytes);
    }
    apush(p->files, ((frozen_file){ xstrdup(path), copy, bytes }));
    p->bytes += bytes;
    return true;
}
static bool text(model_evidence_plan *p, const char *path, const char *data, char *error, size_t size)
{
    return add(p, path, data, strlen(data), error, size);
}
static void vector(av_doc *doc, av_value *object, const char *key, const float *values, int count)
{
    av_value *a = av_obj_add_arr(doc, object, key);
    for (int i = 0; i < count; i++) {
        av_arr_append(a, av_float(doc, values[i]));
    }
}
static bool asset(
    model_evidence_plan *p,
    const behavior_asset *source,
    const char *kind,
    int index,
    char *error,
    size_t size
)
{
    char path[128];
    snprintf(path, sizeof path, "content/%s/%03d.kdl", kind, index);
    char *serialized = behavior_asset_save(source);
    if (!serialized) {
        return fail(error, size, "Frozen model dependency cannot serialize");
    }
    bool ok = text(p, path, serialized, error, size);
    free(serialized);
    return ok;
}
static const behavior_asset *find(const model_evidence_input *in, const char *name)
{
    if (!strcmp(in->source->name, name)) {
        return in->source;
    }
    for (int i = 0; i < in->catalog_count; i++) {
        if (!strcmp(in->catalog[i]->name, name)) {
            return in->catalog[i];
        }
    }
    return NULL;
}
static bool image(
    model_evidence_plan *p,
    const model_evidence_input *in,
    const shader_image_source *source,
    char *error,
    size_t size
)
{
    for (int i = 0; i < acount(p->files); i++) {
        if (!strcmp(p->files[i].path, source->key)) {
            return true;
        }
    }
    if (!in->resolve_image) {
        return fail(error, size, "Model evidence requires resident image verification");
    }
    shader_image_view resident = { 0 };
    if (!in->resolve_image(in->image_user, source, &resident)) {
        return fail(error, size, "Model resident image dependency is unavailable");
    }
    int length = 0;
    void *encoded = image_prepare_loader_bytes(in->images, source->key, &length, error, size);
    if (!encoded) {
        return false;
    }
    shader_image_view decoded = { 0 };
    bool ok = image_prepare_decode(encoded, (size_t)length, &decoded, error, size) != NULL;
    ok = ok && decoded.width == resident.width &&
         decoded.height == resident.height && decoded.pixels && resident.pixels &&
         !memcmp(
             decoded.pixels,
             resident.pixels,
             (size_t)decoded.width * decoded.height * 4
         );
    if (!ok && !error[0]) {
        fail(error, size, "Image source bytes differ from the rendered resident generation");
    }
    if (ok) {
        ok = add(p, source->key, encoded, (size_t)length, error, size);
    }
    free((void *)decoded.pixels);
    free(encoded);
    return ok;
}
static bool images(
    model_evidence_plan *p,
    const model_evidence_input *in,
    const shader_graph *graph,
    char *error,
    size_t size
)
{
    for (int i = 0; i < acount(graph->images); i++) {
        if (!image(p, in, &graph->images[i], error, size)) {
            return false;
        }
    }
    return true;
}
static av_doc *request(
    const model_evidence_input *in,
    const model_program *program,
    const float values[32][4]
)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "contract", CHIBI_AUTOMATION_CONTRACT);
    av_obj_add_str(doc, root, "operation", "preview_model");
    av_obj_add_str(doc, root, "path", "content/models/000.kdl");
    av_obj_add_int(doc, root, "width", in->width);
    av_obj_add_int(doc, root, "height", in->height);
    av_obj_add_float(doc, root, "time", in->time);
    av_value *view = av_obj_add_obj(doc, root, "view");
    av_obj_add_float(doc, view, "yaw", in->yaw);
    av_obj_add_float(doc, view, "pitch", in->pitch);
    av_obj_add_float(doc, view, "zoom", in->zoom);
    av_obj_add_float(doc, view, "pan_x", in->pan_x);
    av_obj_add_float(doc, view, "pan_y", in->pan_y);
    av_value *inspection = av_obj_add_obj(doc, root, "inspection");
    av_obj_add_int(doc, inspection, "part", in->part);
    av_obj_add_int(doc, inspection, "section_axis", in->section_axis);
    av_obj_add_float(doc, inspection, "section_offset", in->section_offset);
    av_obj_add_bool(doc, inspection, "reference_visible", in->reference_visible);
    av_obj_add_float(doc, inspection, "reference_opacity", in->reference_opacity);
    av_value *parameters = av_obj_add_obj(doc, root, "parameters");
    for (int i = 0; i < acount(program->parameters); i++) {
        const model_parameter *p = &program->parameters[i];
        if (p->hidden) {
            continue;
        }
        char key[512];
        snprintf(key, sizeof key, "%s%s%s", p->scope, p->scope[0] ? "." : "", p->name);
        if (p->width == 1) {
            av_obj_set(doc, parameters, key, av_float(doc, values[p->slot][0]));
        } else {
            vector(doc, parameters, key, values[p->slot], p->width);
        }
    }
    return doc;
}
static bool valid(const model_evidence_input *in, char *error, size_t size)
{
    if (!in || !in->source || in->source->domain != BEHAVIOR_DOMAIN_MODEL ||
        in->catalog_count < 0 || in->catalog_count > 256 ||
        (in->catalog_count && !in->catalog) || !in->backend || !*in->backend) {
        return fail(error, size, "Model evidence requires a model snapshot, bounded catalog and backend identity");
    }
    if (!shader_authoring_evidence_identity_valid(
            in->engine_version,
            in->build_id,
            in->manifest_sha256,
            error,
            size
        )) {
        return false;
    }
    if (in->width < 16 || in->height < 16 || in->width > 512 || in->height > 512 ||
        in->width * in->height > 640 * 360 || !isfinite(in->time) ||
        fabsf(in->time) > 3600 || !isfinite(in->yaw) || fabsf(in->yaw) > 3.14159266f ||
        !isfinite(in->pitch) || fabsf(in->pitch) > 1.57079633f ||
        !isfinite(in->zoom) || in->zoom < .25f || in->zoom > 4 ||
        in->part < -1 || in->section_axis < 0 || in->section_axis > 3 ||
        !isfinite(in->section_offset) || fabsf(in->section_offset) > 1e6f ||
        !isfinite(in->pan_x) || fabsf(in->pan_x) > 1e6f || !isfinite(in->pan_y) ||
        fabsf(in->pan_y) > 1e6f || !isfinite(in->reference_opacity) ||
        in->reference_opacity < 0 || in->reference_opacity > 1) {
        return fail(error, size, "Model evidence settings exceed bounded replayable preview limits");
    }
    for (int i = 0; i < in->catalog_count; i++) {
        if (!in->catalog[i]) {
            return fail(error, size, "Model evidence catalog contains an absent source");
        }
    }
    return true;
}
model_evidence_plan *model_evidence_prepare(const model_evidence_input *in, char *error, size_t size)
{
    error[0] = 0;
    if (!valid(in, error, size)) {
        return NULL;
    }
    model_program program;
    model_program_init(&program);
    shader_error problem = { 0 };
    bool ok = model_compile_with_catalog(in->source, in->catalog, in->catalog_count, &program, &problem);
    program.resolve_image = in->resolve_image;
    program.image_userdata = in->image_user;
    float values[32][4];
    model_bounds bounds;
    ok = ok && model_program_prepare(&program, in->parameters, values, &bounds, &problem);
    if (!ok) {
        model_program_free(&program);
        fail(error, size, problem.message);
        return NULL;
    }
    if (bounds.empty || in->part >= acount(program.parts)) {
        model_program_free(&program);
        fail(error, size, "Model evidence requires visible volume and an existing selected part");
        return NULL;
    }
    for (int i = 0; i < acount(program.parameters); i++) {
        const model_parameter *parameter = &program.parameters[i];
        if (!parameter->hidden &&
            strlen(parameter->scope) + strlen(parameter->name) + 2 > 512) {
            model_program_free(&program);
            fail(error, size, "Model parameter replay identity exceeds bounded key length");
            return NULL;
        }
    }
    model_evidence_plan *plan = xcalloc(1, sizeof *plan);
    plan->width = in->width;
    plan->height = in->height;
    char project[128];
    snprintf(project, sizeof project, "version %d\nname \"Model evidence\"\ncontent \"content\"\n", CHIBI_PROJECT_VERSION);
    ok = text(plan, "project.kdl", project, error, size) &&
         asset(plan, in->source, "models", 0, error, size) &&
         images(plan, in, &program.shader, error, size);
    int index = 1;
    for (int i = 0; ok && i < acount(program.dependencies); i++) {
        if (!strcmp(program.dependencies[i], in->source->name)) {
            continue;
        }
        const behavior_asset *dependency = find(in, program.dependencies[i]);
        if (!dependency) {
            ok = fail(error, size, "Frozen model dependency source is unavailable");
            break;
        }
        ok = asset(plan, dependency, "models", index++, error, size);
    }
    for (int i = 0; ok && i < acount(program.material_dependencies); i++) {
        const behavior_asset *source = find(in, program.material_dependencies[i]);
        if (!source || source->domain != BEHAVIOR_DOMAIN_MATERIAL) {
            ok = fail(error, size, "Frozen material dependency source is unavailable");
            break;
        }
        behavior_asset *linked = behavior_asset_link_libraries(
            source,
            in->catalog,
            in->catalog_count,
            error,
            size
        );
        shader_authoring_report report;
        shader_authoring_report_init(&report);
        ok = linked && shader_authoring_inspect(linked, &report, &problem) &&
             images(plan, in, &report.program, error, size) &&
             asset(plan, linked, "materials", i, error, size);
        if (!ok && !error[0]) {
            fail(error, size, problem.message);
        }
        shader_authoring_report_free(&report);
        behavior_asset_free(linked);
    }
    if (ok && in->reference_visible && in->source->reference.image &&
        in->reference_opacity > 0) {
        char key[1040];
        snprintf(key, sizeof key, "content/%s", in->source->reference.image);
        shader_image_source reference = { .key = key,
                                          .color_space = SHADER_IMAGE_LINEAR,
                                          .filter = SHADER_IMAGE_BILINEAR,
                                          .wrap = SHADER_IMAGE_CLAMP };
        ok = image(plan, in, &reference, error, size);
    }
    av_doc *replay = request(in, &program, values);
    char *json = av_json_write(av_doc_root(replay));
    ok = ok && text(plan, "requests.ndjson", json, error, size);
    free(json);
    plan->manifest = av_doc_new();
    av_value *root = av_obj(plan->manifest);
    av_doc_set_root(plan->manifest, root);
    av_obj_add_int(plan->manifest, root, "version", 1);
    av_obj_add_str(plan->manifest, root, "kind", "model-preview-evidence");
    av_obj_add_str(plan->manifest, root, "model", in->source->name);
    av_obj_add_int(plan->manifest, root, "document_revision", in->revision);
    av_obj_set(plan->manifest, root, "request", av_clone(plan->manifest, av_doc_root(replay)));
    av_doc_free(replay);
    av_value *sdk = av_obj_add_obj(plan->manifest, root, "sdk");
    av_obj_add_str(plan->manifest, sdk, "version", in->engine_version);
    av_obj_add_str(plan->manifest, sdk, "build_id", in->build_id);
    av_obj_add_str(plan->manifest, sdk, "manifest_sha256", in->manifest_sha256);
    av_obj_add_str(plan->manifest, root, "backend", in->backend);
    av_obj_add_str(plan->manifest, root, "video_driver", in->video_driver ? in->video_driver : "");
    av_value *coverage = av_obj_add_obj(plan->manifest, root, "coverage");
    av_obj_add_bool(plan->manifest, coverage, "model_compiled", true);
    av_obj_add_bool(plan->manifest, coverage, "cpu_bounds", true);
    av_obj_add_bool(plan->manifest, coverage, "resident_images_matched", true);
    av_obj_add_bool(plan->manifest, coverage, "gpu_preview_rendered", in->gpu_rendered);
    av_obj_add_bool(
        plan->manifest,
        coverage,
        "reference_overlay",
        in->reference_visible && in->source->reference.image && in->reference_opacity > 0
    );
    av_obj_add_bool(plan->manifest, coverage, "cpu_mesh_baked", false);
    av_obj_add_bool(plan->manifest, coverage, "gameplay_validated", false);
    vector(plan->manifest, root, "bounds_min", bounds.min, 3);
    vector(plan->manifest, root, "bounds_max", bounds.max, 3);
    model_inspection inspection = { 0 };
    model_program_inspect(&program, &inspection);
    av_value *cost = av_obj_add_obj(plan->manifest, root, "costs");
    av_obj_add_int(plan->manifest, cost, "distance_nodes", inspection.distance_nodes);
    av_obj_add_int(plan->manifest, cost, "hit_nodes", inspection.hit_nodes);
    av_obj_add_int(plan->manifest, cost, "parts", acount(inspection.parts));
    model_inspection_free(&inspection);
    const char
        *readme = "This bundle owns frozen model/material sources, raw image "
                  "dependencies and a preview PNG. Replay requests.ndjson from "
                  "this directory with the matching SDK: chibi_ctl "
                  "--project-root . --render < requests.ndjson. The backend, "
                  "fixed time, view, selected part and section must match for "
                  "pixel comparisons. GPU provenance is supplied by the "
                  "producer; model compilation and CPU bounds are validated "
                  "separately. No mesh bake or gameplay acceptance is implied. "
                  "manifest.json is written last.\n";
    ok = ok && text(plan, "README.txt", readme, error, size);
    model_program_free(&program);
    if (!ok) {
        model_evidence_free(plan);
        return NULL;
    }
    return plan;
}
void model_evidence_free(model_evidence_plan *plan)
{
    if (!plan) {
        return;
    }
    for (int i = 0; i < acount(plan->files); i++) {
        free(plan->files[i].path);
        free(plan->files[i].data);
    }
    afree(plan->files);
    av_doc_free(plan->manifest);
    free(plan);
}
bool model_evidence_finalize(
    const model_evidence_plan *plan,
    workspace_files *files,
    const char *directory,
    const void *png,
    size_t png_bytes,
    char *error,
    size_t size
)
{
    error[0] = 0;
    if (!plan || !png || png_bytes > WORKSPACE_FILE_BYTES_MAX) {
        return fail(error, size, "Model evidence PNG is absent or exceeds16MiB");
    }
    shader_image_view decoded = { 0 };
    const char *format = image_prepare_decode(png, png_bytes, &decoded, error, size);
    bool valid = format && !strcmp(format, "png") &&
                 decoded.width == plan->width && decoded.height == plan->height;
    free((void *)decoded.pixels);
    if (!valid) {
        return fail(error, size, "Model evidence PNG differs from the frozen dimensions or format");
    }
    av_doc *manifest = av_doc_new();
    av_value *root = av_clone(manifest, av_doc_root(plan->manifest));
    av_doc_set_root(manifest, root);
    av_value *inventory = av_obj_add_arr(manifest, root, "files");
    workspace_file_entry entries[FILES_MAX];
    int count = 0;
    for (int i = 0; i < acount(plan->files); i++) {
        entries[count++] = (workspace_file_entry){ plan->files[i].path,
                                                   plan->files[i].data,
                                                   plan->files[i].bytes };
    }
    entries[count++] = (workspace_file_entry){ "preview.png", png, png_bytes };
    for (int i = 0; i < count; i++) {
        char digest[65];
        sha256_hex(entries[i].data, entries[i].bytes, digest);
        av_value *row = av_obj(manifest);
        av_arr_append(inventory, row);
        av_obj_add_str(manifest, row, "path", entries[i].path);
        av_obj_add_int(manifest, row, "bytes", (int64_t)entries[i].bytes);
        av_obj_add_str(manifest, row, "sha256", digest);
    }
    char *json = av_json_write(root);
    entries[count++] = (workspace_file_entry){ "manifest.json", json, strlen(json) };
    bool ok = workspace_file_bundle_write_new(files, directory, entries, count, error, size);
    free(json);
    av_doc_free(manifest);
    return ok;
}
