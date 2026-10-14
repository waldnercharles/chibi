#include "authoring/visual_authoring_preview.h"

#include "authoring/image_prepare.h"
#include "authoring/model_evidence.h"
#include "authoring/visual_catalog.h"
#include "authoring/visual_model_preview.h"
#include "graph/document/serialize.h"
#include "platform/cute.h"
#include "render/shader_preview.h"
#include "render/shader_publish.h"
#include "runtime/images/catalog.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
    PREVIEW_MAX = 16,
    JOB_MAX = 8
};
typedef struct
{
    visual_preview_view view;
    render_shader_preview *renderer;
    visual_model_preview *model;
    CF_Canvas canvas;
    behavior_asset *snapshot;
    uint64_t applied_identity;
    visual_model_reference reference;
    int reference_image;
    uint64_t reference_revision;
    char error[256];
    int image_count, image_ids[RENDER_SHADER_IMAGE_MAX];
    uint64_t image_revisions[RENDER_SHADER_IMAGE_MAX];
} surface;
typedef struct
{
    int handle;
    visual_evidence_state state;
    shader_authoring_evidence_plan *plan;
    model_evidence_plan *model_plan;
    CF_Canvas canvas;
    CF_Readback readback;
    uint64_t started;
    int width, height;
    char *destination;
    char error[512];
} evidence_job;
struct visual_previews
{
    workspace_documents *documents;
    workspace_files *files;
    render_resource_context *resources;
    bool owns_resources, owns_world, used;
    preview_world *world;
    char *content, *root;
    int sequence;
    surface surfaces[PREVIEW_MAX];
    evidence_job jobs[JOB_MAX];
};
static bool message(char *error, size_t size, const char *text)
{
    snprintf(error, size, "%s", text);
    return false;
}
static surface *find_surface(visual_previews *service, int handle)
{
    for (int i = 0; i < PREVIEW_MAX; i++) {
        if (handle > 0 && service->surfaces[i].view.preview == handle) {
            return &service->surfaces[i];
        }
    }
    return NULL;
}
static evidence_job *find_job(visual_previews *service, int handle)
{
    for (int i = 0; i < JOB_MAX; i++) {
        if (handle > 0 && service->jobs[i].handle == handle) {
            return &service->jobs[i];
        }
    }
    return NULL;
}
visual_previews *visual_previews_new(
    workspace_documents *documents,
    preview_world *world,
    const char *content,
    const char *workspace_root
)
{
    visual_previews *service = xcalloc(1, sizeof *service);
    service->documents = documents;
    char files_error[256];
    service->files = workspace_files_new(workspace_root, files_error, sizeof files_error);
    service->resources = render_resource_context_new();
    service->owns_resources = true;
    service->world = world;
    if (!world) {
        render_resource_scope scope = { 0 };
        char error[256];
        bool entered = render_resource_scope_enter(service->resources, &scope, error, sizeof error);
        assert(entered);
        if (entered) {
            service->world = preview_world_new(NULL);
            bool left = render_resource_scope_leave(&scope, error, sizeof error);
            assert(left);
            (void)left;
        }
        assert(service->world);
        service->owns_world = true;
    }
    render_resource_scope images = { 0 };
    char image_error[256];
    bool images_entered = render_resource_scope_enter(service->resources, &images, image_error, sizeof image_error);
    assert(images_entered);
    if (images_entered) {
        render_image_loader loader;
        if (image_prepare_loader_new(content, &loader, image_error, sizeof image_error)) {
            render_images_set_loader(loader);
        }
        bool left = render_resource_scope_leave(&images, image_error, sizeof image_error);
        assert(left);
        (void)left;
    }
    service->content = xstrdup(content);
    service->root = xstrdup(workspace_root);
    return service;
}
static void release_surface(surface *slot)
{
    render_shader_preview_free(slot->renderer);
    visual_model_preview_free(slot->model);
    if (slot->canvas.id) {
        cf_destroy_canvas(slot->canvas);
    }
    visual_model_reference_free(&slot->reference);
    behavior_asset_free(slot->snapshot);
    *slot = (surface){ 0 };
}
static void finish_job(evidence_job *job, visual_evidence_state state)
{
    if (job->readback.id) {
        cf_destroy_readback(job->readback);
    }
    if (job->canvas.id) {
        cf_destroy_canvas(job->canvas);
    }
    shader_authoring_evidence_free(job->plan);
    model_evidence_free(job->model_plan);
    job->readback = (CF_Readback){ 0 };
    job->canvas = (CF_Canvas){ 0 };
    job->plan = NULL;
    job->model_plan = NULL;
    job->state = state;
}
static void visual_previews_free_inner(visual_previews *service)
{
    if (!service) {
        return;
    }
    for (int i = 0; i < PREVIEW_MAX; i++) {
        release_surface(&service->surfaces[i]);
    }
    for (int i = 0; i < JOB_MAX; i++) {
        finish_job(&service->jobs[i], VISUAL_EVIDENCE_CANCELLED);
        free(service->jobs[i].destination);
    }
    if (service->owns_world) {
        preview_world_free(service->world);
    }
    workspace_files_free(service->files);
    free(service->content);
    free(service->root);
    free(service);
}
static bool visual_preview_publish_inner(
    visual_previews *service,
    int document,
    int revision,
    char *error,
    size_t size
)
{
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(service->documents, document, &view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return message(error, size, workspace_document_status_text(status));
    }
    if (view.revision != revision) {
        return message(error, size, "Document revision is stale");
    }
    if (!service->world) {
        return message(error, size, "Visual publication requires an authoring world");
    }
    behavior_asset *asset = behavior_asset_load(view.text);
    if (!asset) {
        return message(error, size, graph_last_error());
    }
    if (asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
        asset->domain != BEHAVIOR_DOMAIN_SHAPE && asset->domain != BEHAVIOR_DOMAIN_MODEL &&
        asset->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY &&
        asset->domain != BEHAVIOR_DOMAIN_FIELD) {
        behavior_asset_free(asset);
        return message(error, size, "Publish a material, shape, model, shader library or field document");
    }
    preview_definition
        *definition = preview_find_definition(service->world, asset->name);
    if (definition) {
        if (preview_sync_working(definition, asset) < 0) {
            behavior_asset_free(asset);
            return message(error, size, "Could not replace the working visual generation");
        }
        behavior_asset_free(asset);
    } else {
        definition = preview_definition_new(asset->name, asset);
        if (preview_add_definition(service->world, definition) < 0) {
            preview_definition_free(definition);
            return message(error, size, "Could not register visual document");
        }
    }
    bool applied = preview_update(service->world, definition, error, size) >= 0;
    if (applied && size) {
        error[0] = 0;
    }
    return applied;
}
static bool visual_publish_documents_inner(
    visual_previews *service,
    const int *documents,
    int count,
    visual_publication_result *results,
    char *error,
    size_t size
)
{
    if (count < 0 || count > WORKSPACE_DOCUMENT_COUNT_MAX ||
        (count && (!documents || !results))) {
        return message(error, size, "Visual publication accepts at most 64 document handles");
    }
    behavior_asset *sources[WORKSPACE_DOCUMENT_COUNT_MAX] = { 0 };
    bool valid = true;
    for (int i = 0; i < count; i++) {
        results[i] = (visual_publication_result){ .document = documents[i] };
        workspace_document_view view;
        workspace_document_status
            status = workspace_document_read(service->documents, documents[i], &view);
        if (status != WORKSPACE_DOCUMENT_OK) {
            message(results[i].error, sizeof results[i].error, workspace_document_status_text(status));
            valid = false;
            continue;
        }
        results[i].revision = view.revision;
        sources[i] = behavior_asset_load(view.text);
        if (!sources[i]) {
            message(results[i].error, sizeof results[i].error, graph_last_error());
            valid = false;
            continue;
        }
        for (int j = 0; j < i; j++) {
            if (sources[j] && !strcmp(sources[j]->name, sources[i]->name)) {
                message(results[i].error, sizeof results[i].error, "Visual publication contains duplicate asset names");
                valid = false;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        behavior_asset_free(sources[i]);
    }
    if (!valid) {
        for (int i = 0; i < count; i++) {
            if (!results[i].error[0]) {
                message(results[i].error, sizeof results[i].error, "Publication batch failed preflight; this definition was not changed");
            }
        }
        return message(error, size, "Visual publication preflight failed; no definitions changed");
    }
    int remaining = count;
    for (int pass = 0; pass < count && remaining; pass++) {
        int before = remaining;
        for (int i = 0; i < count; i++) {
            if (results[i].valid) {
                continue;
            }
            results[i].valid = visual_preview_publish(
                service,
                results[i].document,
                results[i].revision,
                results[i].error,
                sizeof results[i].error
            );
            if (results[i].valid) {
                remaining--;
            }
        }
        if (remaining == before) {
            break;
        }
    }
    if (size) {
        error[0] = 0;
    }
    return true;
}
bool visual_preview_open(
    visual_previews *service,
    int document,
    int width,
    int height,
    int *handle,
    char *error,
    size_t size
)
{
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(service->documents, document, &view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return message(error, size, workspace_document_status_text(status));
    }
    if (width < 16 || width > 512 || height < 16 || height > 512 ||
        width * height > 640 * 360) {
        return message(error, size, "Preview dimensions exceed the bounded surface budget");
    }
    if (service->sequence == INT_MAX) {
        return message(error, size, "Preview handle space is exhausted");
    }
    for (int i = 0; i < PREVIEW_MAX; i++) {
        surface *slot = &service->surfaces[i];
        if (slot->view.preview) {
            continue;
        }
        slot->view = (visual_preview_view){ .preview = ++service->sequence,
                                            .document = document,
                                            .width = width,
                                            .height = height,
                                            .zoom = 1,
                                            .model_part = -1 };
        *handle = slot->view.preview;
        return true;
    }
    return message(error, size, "Close a preview before opening another");
}
int visual_preview_handles(const visual_previews *service, int *out, int capacity)
{
    int count = 0;
    for (int i = 0; i < PREVIEW_MAX; i++) {
        if (!service->surfaces[i].view.preview) {
            continue;
        }
        if (count < capacity) {
            out[count] = service->surfaces[i].view.preview;
        }
        count++;
    }
    return count;
}
static bool visual_preview_close_inner(visual_previews *service, int handle)
{
    surface *slot = find_surface(service, handle);
    if (!slot) {
        return false;
    }
    release_surface(slot);
    return true;
}
static bool images_current(const surface *slot)
{
    if (slot->reference_image) {
        shader_image_source source;
        shader_image_view pixels;
        uint64_t revision;
        if (!render_image_sampler(slot->reference_image, &source, &pixels, &revision) ||
            revision != slot->reference_revision) {
            return false;
        }
    }

    for (int i = 0; i < slot->image_count; i++) {
        uint64_t revision;
        shader_image_source source;
        shader_image_view view;
        if (!render_image_sampler(slot->image_ids[i], &source, &view, &revision) ||
            revision != slot->image_revisions[i]) {
            return false;
        }
    }
    return true;
}
static bool remember_images(surface *slot, const behavior_asset *snapshot)
{
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    shader_error error = { 0 };
    bool valid = shader_authoring_inspect(snapshot, &report, &error);
    int count = acount(report.program.images);
    valid = valid && count <= RENDER_SHADER_IMAGE_MAX;
    slot->image_count = 0;
    for (int i = 0; valid && i < count; i++) {
        int id = render_image_variant_resident(&report.program.images[i], &error);
        shader_image_source source;
        shader_image_view view;
        uint64_t revision;
        valid = id && render_image_sampler(id, &source, &view, &revision);
        if (valid) {
            slot->image_ids[slot->image_count] = id;
            slot->image_revisions[slot->image_count++] = revision;
        }
    }
    shader_authoring_report_free(&report);
    if (!valid) {
        message(slot->error, sizeof slot->error, error.message[0] ? error.message : "Preview image dependencies are unavailable");
    }
    return valid;
}
static bool visual_preview_read_inner(visual_previews *service, int handle, visual_preview_view *view)
{
    surface *slot = find_surface(service, handle);
    if (!slot) {
        return false;
    }
    *view = slot->view;
    workspace_document_view document;
    if (view->drawn &&
        (workspace_document_read(service->documents, view->document, &document) !=
             WORKSPACE_DOCUMENT_OK ||
         document.revision != view->revision ||
         render_shader_revision() != (unsigned)view->catalog_revision ||
         !images_current(slot))) {
        view->drawn = false;
        view->texture = 0;
    }
    view->error = slot->error;
    return true;
}
static bool draw_retained(surface *slot, float time, float yaw, float pitch, float zoom)
{
    if (slot->model) {
        if (!visual_model_preview_pan(
                slot->model,
                slot->view.pan_x,
                slot->view.pan_y,
                slot->error,
                sizeof slot->error
            ) ||
            (slot->reference.image && !visual_model_preview_reference(
                                          slot->model,
                                          &slot->reference,
                                          slot->view.reference_visible,
                                          slot->view.reference_opacity,
                                          slot->error,
                                          sizeof slot->error
                                      ))) {
            return false;
        }
        if (!visual_model_preview_draw(
                slot->model,
                time,
                yaw,
                pitch,
                zoom,
                slot->error,
                sizeof slot->error
            )) {
            return false;
        }
        render_shader_set images;
        if (!visual_model_preview_images(
                slot->model,
                &images,
                slot->error,
                sizeof slot->error
            )) {
            return false;
        }
        slot->image_count = 0;
        for (int i = 0; i < images.count; i++) {
            shader_image_source source;
            shader_image_view pixels;
            uint64_t revision;
            if (!render_image_sampler(images.images[i], &source, &pixels, &revision)) {
                return message(slot->error, sizeof slot->error, "Model image dependency is unavailable");
            }
            slot->image_ids[slot->image_count] = images.images[i];
            slot->image_revisions[slot->image_count++] = revision;
        }
        slot->reference_image = visual_model_preview_reference_sampler(slot->model);
        if (slot->reference_image) {
            shader_image_source source;
            shader_image_view pixels;
            if (!render_image_sampler(slot->reference_image, &source, &pixels, &slot->reference_revision)) {
                return message(slot->error, sizeof slot->error, "Rendered reference image is unavailable");
            }
        }
        slot->view.drawn = true;
        slot->view.texture = visual_model_preview_texture(slot->model);
        slot->view.texture_revision++;
        slot->view.time = time;
        slot->view.yaw = yaw;
        slot->view.pitch = pitch;
        slot->view.zoom = zoom;
        return true;
    }
    bool drawn = slot->renderer &&
                 render_shader_preview_set_view(slot->renderer, yaw, pitch, zoom);
    if (!slot->canvas.id) {
        slot->canvas = cf_make_canvas(
            cf_canvas_defaults(slot->view.width, slot->view.height)
        );
    }
    cf_draw_push();
    if (drawn) {
        drawn = render_shader_preview_draw(slot->renderer, time, NULL);
        if (drawn) {
            cf_render_to(slot->canvas, true);
        }
    }
    cf_draw_pop();
    if (!drawn) {
        return false;
    }
    slot->view.drawn = true;
    slot->view.texture = cf_canvas_get_target(slot->canvas).id;
    slot->view.texture_revision++;
    slot->view.time = time;
    slot->view.yaw = yaw;
    slot->view.pitch = pitch;
    slot->view.zoom = zoom;
    return true;
}
static bool visual_preview_draw_inner(
    visual_previews *service,
    int handle,
    int revision,
    float time,
    float yaw,
    float pitch,
    float zoom
)
{
    surface *slot = find_surface(service, handle);
    if (!slot) {
        return false;
    }
    slot->view.drawn = false;
    slot->view.texture = 0;
    slot->error[0] = 0;
    if (slot->view.texture_revision == UINT64_MAX) {
        return message(slot->error, sizeof slot->error, "Preview texture revision space is exhausted");
    }
    if (!isfinite(yaw) || !isfinite(pitch) || !isfinite(zoom) ||
        fabsf(yaw) > RENDER_SHADER_PREVIEW_YAW_MAX ||
        fabsf(pitch) > RENDER_SHADER_PREVIEW_PITCH_MAX ||
        zoom < RENDER_SHADER_PREVIEW_ZOOM_MIN || zoom > RENDER_SHADER_PREVIEW_ZOOM_MAX) {
        return message(slot->error, sizeof slot->error, "Preview view settings are outside their supported ranges");
    }
    if (!isfinite(time)) {
        return message(slot->error, sizeof slot->error, "Preview time must be finite");
    }
    workspace_document_view document;
    workspace_document_status
        status = workspace_document_read(service->documents, slot->view.document, &document);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return message(slot->error, sizeof slot->error, workspace_document_status_text(status));
    }
    if (document.revision != revision) {
        return message(slot->error, sizeof slot->error, "Document revision is stale");
    }
    unsigned catalog = render_shader_revision();
    preview_definition
        *cached = slot->snapshot && service->world
                    ? preview_find_definition(service->world, slot->snapshot->name)
                    : NULL;
    if ((slot->renderer || slot->model) && cached && cached->applied &&
        !cached->dirty && cached->applied->identity == slot->applied_identity &&
        slot->view.revision == revision &&
        (unsigned)slot->view.catalog_revision == catalog && images_current(slot)) {
        bool drawn = draw_retained(slot, time, yaw, pitch, zoom);
        if (!drawn) {
            message(slot->error, sizeof slot->error, "Preview rendering or view settings failed");
        }
        return drawn;
    }
    visual_authoring_report report;
    visual_authoring_report_init(&report);
    visual_authoring_inspect(service->documents, slot->view.document, revision, &report);
    bool valid = report.source &&
                 (report.source->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                  report.source->domain == BEHAVIOR_DOMAIN_SHAPE ||
                  report.source->domain == BEHAVIOR_DOMAIN_MODEL);
    preview_definition
        *definition = report.source && service->world
                        ? preview_find_definition(service->world, report.source->name)
                        : NULL;
    valid = valid && definition && definition->applied && !definition->dirty &&
            preview_working_matches(definition, report.source);
    if (!valid) {
        message(
            slot->error,
            sizeof slot->error,
            report.error.message[0] ? report.error.message : "Document has no matching applied shader generation"
        );
        visual_authoring_report_free(&report);
        return false;
    }
    if (slot->view.catalog_revision != (int)catalog ||
        (slot->snapshot && strcmp(slot->snapshot->name, report.source->name))) {
        render_shader_preview_free(slot->renderer);
        slot->renderer = NULL;
        visual_model_preview_free(slot->model);
        slot->model = NULL;
    }
    shader_error error = { 0 };
    if (report.source->domain == BEHAVIOR_DOMAIN_MODEL && !slot->model) {
        slot->model = visual_model_preview_new(
            report.source->name,
            slot->view.width,
            slot->view.height,
            slot->error,
            sizeof slot->error
        );
    }
    if (report.source->domain != BEHAVIOR_DOMAIN_MODEL && !slot->renderer) {
        slot->renderer = render_shader_preview_find(
            report.source->name,
            slot->view.width,
            slot->view.height,
            &error
        );
    }
    if (report.source->domain == BEHAVIOR_DOMAIN_MODEL) {
        const model_reference *reference = &report.source->reference;
        visual_model_reference next = {
            .image = xstrdup(reference->image ? reference->image : ""),
            .center_x = reference->center[0],
            .center_y = reference->center[1],
            .height = reference->image ? reference->span : 2,
            .angle = reference->angle,
            .locked = reference->locked
        };
        visual_model_reference_free(&slot->reference);
        slot->reference = next;
    }
    bool drawn = draw_retained(slot, time, yaw, pitch, zoom) &&
                 (report.source->domain == BEHAVIOR_DOMAIN_MODEL ||
                  remember_images(slot, definition->applied));
    if (drawn) {
        behavior_asset_free(slot->snapshot);
        slot->snapshot = behavior_asset_clone(definition->applied);
        slot->applied_identity = definition->applied->identity;
        slot->view.revision = revision;
        slot->view.catalog_revision = (int)catalog;
    } else if (!slot->error[0]) {
        const char *renderer_error = slot->renderer
                                       ? render_shader_preview_error(slot->renderer)
                                       : NULL;
        message(
            slot->error,
            sizeof slot->error,
            error.message[0] ? error.message
            : renderer_error && renderer_error[0]
                ? renderer_error
                : "Preview rendering or view settings failed"
        );
    }
    if (!drawn) {
        slot->view.drawn = false;
        slot->view.texture = 0;
    }
    visual_authoring_report_free(&report);
    return drawn;
}
static bool evidence_destination(
    visual_previews *service,
    const char *relative,
    char path[PATH_MAX],
    char *error,
    size_t size
)
{
    if (!relative || !relative[0] || relative[0] == '/' ||
        strchr(relative, '\\') || strlen(relative) >= PATH_MAX) {
        return message(error, size, "Choose a new workspace-relative evidence directory");
    }
    char copy[PATH_MAX];
    snprintf(copy, sizeof copy, "%s", relative);
    for (const char *at = relative; *at;) {
        const char *end = strchr(at, '/');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (!length || (length == 1 && at[0] == '.') ||
            (length == 2 && at[0] == '.' && at[1] == '.')) {
            return message(error, size, "Evidence paths cannot contain empty, dot or parent segments");
        }
        if (!end) {
            break;
        }
        at = end + 1;
        if (!*at) {
            return message(error, size, "Evidence paths cannot end with a separator");
        }
    }
#if defined(O_NOFOLLOW) && !defined(_WIN32)
    int parent = open(service->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent < 0) {
        return message(error, size, "Evidence workspace root is unavailable");
    }
    char *at = copy, *slash;
    while ((slash = strchr(at, '/'))) {
        *slash = 0;
        int next = openat(parent, at, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(parent);
        if (next < 0) {
            return message(error, size, "Evidence parent must be an existing directory without symlinks");
        }
        parent = next;
        at = slash + 1;
    }
    struct stat existing;
    bool present = !fstatat(parent, at, &existing, AT_SYMLINK_NOFOLLOW);
    close(parent);
    if (present) {
        return message(error, size, "Evidence destination already exists");
    }
#else
    return message(error, size, "Safe evidence paths are unavailable on this platform");
#endif
    int written = snprintf(path, PATH_MAX, "%s/%s", service->root, relative);
    if (written < 0 || written >= PATH_MAX) {
        return message(error, size, "Evidence path is too long");
    }
    return true;
}
static bool model_export(
    visual_previews *service,
    surface *slot,
    const char *destination,
    const char *version,
    const char *build,
    const char *manifest,
    int *result,
    char *error,
    size_t size
)
{
    workspace_document_view document;
    if (!service->files) {
        return message(error, size, "Model evidence workspace files are unavailable");
    }
    if (!slot->view.drawn ||
        workspace_document_read(service->documents, slot->view.document, &document) !=
            WORKSPACE_DOCUMENT_OK ||
        document.revision != slot->view.revision ||
        render_shader_revision() != (unsigned)slot->view.catalog_revision ||
        !images_current(slot)) {
        return message(error, size, "Draw the current model generation before exporting evidence");
    }
    evidence_job *job = NULL;
    for (int i = 0; i < JOB_MAX; i++) {
        if (!service->jobs[i].handle) {
            job = &service->jobs[i];
            break;
        }
    }
    if (!job || service->sequence == INT_MAX) {
        return message(error, size, "Close an evidence job before starting another");
    }
    char resolved[PATH_MAX];
    if (!evidence_destination(service, destination, resolved, error, size)) {
        return false;
    }
    int count = 0;
    behavior_asset **catalog = visual_previews_catalog_snapshot(service, &count);
    float parameters[32][4];
    visual_model_preview_parameters(slot->model, parameters);
    model_evidence_input input = {
        .source = slot->snapshot,
        .catalog = (const behavior_asset *const *)catalog,
        .catalog_count = count,
        .width = slot->view.width,
        .height = slot->view.height,
        .revision = slot->view.revision,
        .part = slot->view.model_part,
        .section_axis = slot->view.section_axis,
        .time = slot->view.time,
        .yaw = slot->view.yaw,
        .pitch = slot->view.pitch,
        .zoom = slot->view.zoom,
        .section_offset = slot->view.section_offset,
        .pan_x = slot->view.pan_x,
        .pan_y = slot->view.pan_y,
        .reference_visible = slot->view.reference_visible,
        .reference_opacity = slot->view.reference_opacity,
        .parameters = parameters,
        .images = render_images_loader(),
        .resolve_image = render_image_resolve,
        .engine_version = version,
        .build_id = build,
        .manifest_sha256 = manifest,
        .backend = cf_backend_type_to_string(cf_query_backend()),
        .video_driver = SDL_GetCurrentVideoDriver(),
        .gpu_rendered = true
    };
    model_evidence_plan *plan = model_evidence_prepare(&input, error, size);
    for (int i = 0; i < count; i++) {
        behavior_asset_free(catalog[i]);
    }
    free(catalog);
    if (!plan) {
        return false;
    }
    CF_Canvas canvas = { visual_model_preview_take_canvas(slot->model) };
    slot->view.drawn = false;
    slot->view.texture = 0;
    CF_Readback readback = canvas.id ? cf_canvas_readback(canvas)
                                     : (CF_Readback){ 0 };
    if (!readback.id) {
        if (canvas.id) {
            cf_destroy_canvas(canvas);
        }
        model_evidence_free(plan);
        return message(error, size, "Model preview readback could not start");
    }
    *job = (evidence_job){ .handle = ++service->sequence,
                           .state = VISUAL_EVIDENCE_PENDING,
                           .model_plan = plan,
                           .canvas = canvas,
                           .readback = readback,
                           .started = cf_get_ticks(),
                           .width = slot->view.width,
                           .height = slot->view.height,
                           .destination = xstrdup(destination) };
    *result = job->handle;
    return true;
}
static bool visual_preview_export_inner(
    visual_previews *service,
    int handle,
    const char *destination,
    const char *version,
    const char *build,
    const char *manifest,
    int *result,
    char *error,
    size_t size
)
{
    if (!shader_authoring_evidence_identity_valid(version, build, manifest, error, size)) {
        return false;
    }
    surface *slot = find_surface(service, handle);
    if (slot && slot->model) {
        return model_export(service, slot, destination, version, build, manifest, result, error, size);
    }
    workspace_document_view document;
    if (!slot || !slot->view.drawn || !slot->canvas.id ||
        workspace_document_read(service->documents, slot->view.document, &document) !=
            WORKSPACE_DOCUMENT_OK ||
        document.revision != slot->view.revision ||
        render_shader_revision() != (unsigned)slot->view.catalog_revision ||
        !images_current(slot)) {
        return message(error, size, "Draw the current document generation before exporting evidence");
    }
    evidence_job *job = NULL;
    for (int i = 0; i < JOB_MAX; i++) {
        if (!service->jobs[i].handle) {
            job = &service->jobs[i];
            break;
        }
    }
    if (!job || service->sequence == INT_MAX) {
        return message(error, size, "Close an evidence job before starting another");
    }
    char resolved_destination[PATH_MAX];
    if (!evidence_destination(service, destination, resolved_destination, error, size)) {
        return false;
    }
    shader_authoring_evidence evidence = {
        .snapshot = slot->snapshot,
        .content_root = service->content,
        .destination = resolved_destination,
        .width = slot->view.width,
        .height = slot->view.height,
        .time = slot->view.time,
        .yaw = slot->view.yaw,
        .pitch = slot->view.pitch,
        .zoom = slot->view.zoom,
        .engine_version = version,
        .engine_build_id = build,
        .engine_manifest_hash = manifest,
        .backend = cf_backend_type_to_string(cf_query_backend()),
        .video_driver = SDL_GetCurrentVideoDriver(),
        .edit_revision = (unsigned)document.revision
    };
    evidence.cpu_context = shader_authoring_center_context(slot->snapshot);
    evidence.cpu_context.time = evidence.time;
    evidence.cpu_context.resolve_image = render_image_resolve;
    evidence.cpu_attempted = true;
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    evidence
        .cpu_valid = shader_authoring_inspect(slot->snapshot, &report, &evidence.cpu_error) &&
                     shader_authoring_sample(
                         &report,
                         &evidence.cpu_context,
                         NULL,
                         evidence.cpu_outputs,
                         &evidence.cpu_error
                     );
    shader_authoring_report_free(&report);
    shader_authoring_evidence_plan *plan = shader_authoring_evidence_prepare(&evidence, error, size);
    if (!plan) {
        return false;
    }
    CF_Readback readback = cf_canvas_readback(slot->canvas);
    if (!readback.id) {
        shader_authoring_evidence_free(plan);
        return message(error, size, "Preview readback could not start");
    }
    *job = (evidence_job){ .handle = ++service->sequence,
                           .state = VISUAL_EVIDENCE_PENDING,
                           .plan = plan,
                           .canvas = slot->canvas,
                           .readback = readback,
                           .started = cf_get_ticks(),
                           .width = slot->view.width,
                           .height = slot->view.height,
                           .destination = xstrdup(resolved_destination) };
    slot->canvas = (CF_Canvas){ 0 };
    slot->view.texture = 0;
    slot->view.drawn = false;
    *result = job->handle;
    return true;
}
static void visual_previews_poll_inner(visual_previews *service)
{
    for (int i = 0; i < JOB_MAX; i++) {
        evidence_job *job = &service->jobs[i];
        if (!job->handle || job->state != VISUAL_EVIDENCE_PENDING) {
            continue;
        }
        if (!cf_readback_ready(job->readback)) {
            if (cf_get_ticks() - job->started > cf_get_tick_frequency() * 10) {
                message(job->error, sizeof job->error, "Preview readback timed out");
                finish_job(job, VISUAL_EVIDENCE_FAILED);
            }
            continue;
        }
        int bytes = job->width * job->height * (int)sizeof(CF_Pixel);
        CF_Pixel *pixels = xmalloc((size_t)bytes);
        bool ok = cf_readback_size(job->readback) == bytes &&
                  cf_readback_data(job->readback, pixels, bytes) == bytes;
        CF_Image image = { job->width, job->height, pixels };
        void *png = NULL;
        int png_bytes = 0;
        ok = ok &&
             !cf_is_error(cf_image_save_png_to_memory(&image, &png, &png_bytes)) &&
             png_bytes > 0;
        message(job->error, sizeof job->error, "Could not encode the rendered preview");
        if (ok) {
            if (job->model_plan) {
                ok = model_evidence_finalize(
                    job->model_plan,
                    service->files,
                    job->destination,
                    png,
                    (size_t)png_bytes,
                    job->error,
                    sizeof job->error
                );
            } else {
                ok = shader_authoring_evidence_finalize(
                    job->plan,
                    png,
                    (size_t)png_bytes,
                    job->error,
                    sizeof job->error
                );
            }
        }
        cf_free(png);
        free(pixels);
        if (ok) {
            job->error[0] = 0;
        }
        finish_job(job, ok ? VISUAL_EVIDENCE_COMPLETE : VISUAL_EVIDENCE_FAILED);
    }
}
bool visual_evidence_read(visual_previews *service, int handle, visual_evidence_view *view)
{
    evidence_job *job = find_job(service, handle);
    if (!job) {
        return false;
    }
    *view = (visual_evidence_view){ .job = handle,
                                    .state = job->state,
                                    .destination = job->destination,
                                    .error = job->error };
    return true;
}
static bool visual_evidence_close_inner(visual_previews *service, int handle)
{
    evidence_job *job = find_job(service, handle);
    if (!job) {
        return false;
    }
    finish_job(job, VISUAL_EVIDENCE_CANCELLED);
    free(job->destination);
    *job = (evidence_job){ 0 };
    return true;
}

static bool enter_resources(visual_previews *service, render_resource_scope *scope, char *error, size_t size)
{
    service->used = true;
    return render_resource_scope_enter(service->resources, scope, error, size);
}
static void leave_resources(render_resource_scope *scope)
{
    char error[256];
    bool left = render_resource_scope_leave(scope, error, sizeof error);
    assert(left);
    (void)left;
}
void visual_previews_set_resources(visual_previews *service, render_resource_context *resources)
{
    assert(!service->used && resources);
    char error[256];
    bool freed = render_resource_context_free(service->resources, error, sizeof error);
    assert(freed);
    (void)freed;
    service->resources = resources;
    service->owns_resources = false;
}
render_resource_context *visual_previews_resources(visual_previews *service)
{
    return service->resources;
}
void visual_previews_free(visual_previews *service)
{
    if (!service) {
        return;
    }
    render_resource_context *resources = service->resources;
    bool owned = service->owns_resources;
    render_resource_scope scope = { 0 };
    char error[256];
    bool entered = enter_resources(service, &scope, error, sizeof error);
    assert(entered);
    if (!entered) {
        return;
    }
    visual_previews_free_inner(service);
    leave_resources(&scope);
    if (owned) {
        bool freed = render_resource_context_free(resources, error, sizeof error);
        assert(freed);
        (void)freed;
    }
}
bool visual_preview_publish(visual_previews *service, int document, int revision, char *error, size_t size)
{
    render_resource_scope scope = { 0 };
    if (!enter_resources(service, &scope, error, size)) {
        return false;
    }
    bool valid = visual_preview_publish_inner(service, document, revision, error, size);
    leave_resources(&scope);
    return valid;
}
bool visual_publish_documents(
    visual_previews *service,
    const int *documents,
    int count,
    visual_publication_result *results,
    char *error,
    size_t size
)
{
    render_resource_scope scope = { 0 };
    if (!enter_resources(service, &scope, error, size)) {
        return false;
    }
    bool valid = visual_publish_documents_inner(service, documents, count, results, error, size);
    leave_resources(&scope);
    return valid;
}
bool visual_preview_close(visual_previews *service, int handle)
{
    render_resource_scope scope = { 0 };
    char error[256];
    if (!enter_resources(service, &scope, error, sizeof error)) {
        return false;
    }
    bool valid = visual_preview_close_inner(service, handle);
    leave_resources(&scope);
    return valid;
}
bool visual_preview_read(visual_previews *service, int handle, visual_preview_view *view)
{
    render_resource_scope scope = { 0 };
    char error[256];
    if (!enter_resources(service, &scope, error, sizeof error)) {
        return false;
    }
    bool valid = visual_preview_read_inner(service, handle, view);
    leave_resources(&scope);
    return valid;
}
bool visual_preview_draw(
    visual_previews *service,
    int handle,
    int revision,
    float time,
    float yaw,
    float pitch,
    float zoom
)
{
    render_resource_scope scope = { 0 };
    char error[256];
    if (!enter_resources(service, &scope, error, sizeof error)) {
        return false;
    }
    bool valid = visual_preview_draw_inner(service, handle, revision, time, yaw, pitch, zoom);
    leave_resources(&scope);
    return valid;
}
bool visual_preview_export(
    visual_previews *service,
    int handle,
    const char *destination,
    const char *version,
    const char *build,
    const char *manifest,
    int *job,
    char *error,
    size_t size
)
{
    render_resource_scope scope = { 0 };
    if (!enter_resources(service, &scope, error, size)) {
        return false;
    }
    bool valid = visual_preview_export_inner(service, handle, destination, version, build, manifest, job, error, size);
    leave_resources(&scope);
    return valid;
}
void visual_previews_poll(visual_previews *service)
{
    render_resource_scope scope = { 0 };
    char error[256];
    bool entered = enter_resources(service, &scope, error, sizeof error);
    assert(entered);
    if (!entered) {
        return;
    }
    visual_previews_poll_inner(service);
    leave_resources(&scope);
}
bool visual_evidence_close(visual_previews *service, int handle)
{
    render_resource_scope scope = { 0 };
    char error[256];
    if (!enter_resources(service, &scope, error, sizeof error)) {
        return false;
    }
    bool valid = visual_evidence_close_inner(service, handle);
    leave_resources(&scope);
    return valid;
}

behavior_asset **visual_previews_catalog_snapshot(visual_previews *service, int *count)
{
    *count = 0;
    if (!service->world) {
        return NULL;
    }
    int total = preview_definition_count(service->world);
    if (total > VISUAL_CATALOG_SOURCES_MAX) {
        *count = -1;
        return NULL;
    }
    behavior_asset **sources = total ? xcalloc((size_t)total, sizeof *sources) : NULL;
    for (int i = 0; i < total; i++) {
        const preview_definition *definition = preview_definition_at(service->world, i);
        if (!definition->applied) {
            continue;
        }
        sources[(*count)++] = behavior_asset_clone(definition->applied);
    }
    return sources;
}

preview_world *visual_previews_world(visual_previews *service)
{
    return service->world;
}
bool visual_previews_refresh_catalog(
    visual_previews *service,
    workspace_resources *inventory,
    char *error,
    size_t size
)
{
    render_resource_scope previous = { 0 };
    if (!enter_resources(service, &previous, error, size)) {
        return false;
    }
    render_image_loader loader = render_images_loader();
    leave_resources(&previous);
    preview_world *next_world = NULL;
    render_resource_context *next_resources = NULL;
    if (!visual_catalog_build(
            service->documents,
            service->root,
            inventory,
            loader,
            &next_world,
            &next_resources,
            error,
            size
        )) {
        return false;
    }
    render_resource_context *old_resources = service->resources;
    bool owned_resources = service->owns_resources;
    if (!enter_resources(service, &previous, error, size)) {
        render_resource_scope candidate = { 0 };
        char cleanup[128];
        bool entered = render_resource_scope_enter(next_resources, &candidate, cleanup, sizeof cleanup);
        assert(entered);
        (void)entered;
        preview_world_free(next_world);
        leave_resources(&candidate);
        bool freed = render_resource_context_free(next_resources, cleanup, sizeof cleanup);
        assert(freed);
        (void)freed;
        return false;
    }
    for (int i = 0; i < PREVIEW_MAX; i++) {
        visual_preview_view view = service->surfaces[i].view;
        release_surface(&service->surfaces[i]);
        view.drawn = false;
        view.texture = 0;
        view.catalog_revision = 0;
        service->surfaces[i].view = view;
    }
    if (service->owns_world) {
        preview_world_free(service->world);
    }
    leave_resources(&previous);
    if (owned_resources) {
        char cleanup[128];
        bool freed = render_resource_context_free(old_resources, cleanup, sizeof cleanup);
        assert(freed);
        (void)freed;
    }
    service->world = next_world;
    service->resources = next_resources;
    service->owns_world = true;
    service->owns_resources = true;
    error[0] = 0;
    return true;
}

static bool model_inspection_inner(
    visual_previews *service,
    int handle,
    int revision,
    int part,
    int axis,
    float offset,
    char *error,
    size_t size
)
{
    surface *slot = find_surface(service, handle);
    if (!slot || part < -1 || axis < 0 || axis > 3 || !isfinite(offset) ||
        fabsf(offset) > 1e6f) {
        return message(error, size, "Invalid model inspection settings");
    }
    workspace_document_view view;
    workspace_document_status
        status = workspace_document_read(service->documents, slot->view.document, &view);
    if (status != WORKSPACE_DOCUMENT_OK || view.revision != revision) {
        return message(error, size, "Model document revision is stale or unavailable");
    }
    behavior_asset *source = behavior_asset_load(view.text);
    bool model = source && source->domain == BEHAVIOR_DOMAIN_MODEL;
    preview_definition
        *definition = model ? preview_find_definition(service->world, source->name)
                            : NULL;
    const model_inspection *info = model ? render_shader_model_inspection(
                                               render_shader_find(source->name)
                                           )
                                         : NULL;
    model = model && definition && !definition->dirty &&
            preview_working_matches(definition, source) && info &&
            part < acount(info->parts);
    behavior_asset_free(source);
    if (!model) {
        return message(error, size, "Model inspection requires a model document");
    }
    slot->view.model_part = part;
    slot->view.section_axis = axis;
    slot->view.section_offset = offset;
    slot->view.drawn = false;
    slot->view.texture = 0;
    error[0] = 0;
    return true;
}

bool visual_preview_model_inspection(
    visual_previews *service,
    int handle,
    int revision,
    int part,
    int axis,
    float offset,
    char *error,
    size_t size
)
{
    render_resource_scope scope = { 0 };
    if (!enter_resources(service, &scope, error, size)) {
        return false;
    }
    bool valid = model_inspection_inner(service, handle, revision, part, axis, offset, error, size);
    leave_resources(&scope);
    return valid;
}

bool visual_preview_model_pan(
    visual_previews *service,
    int handle,
    int revision,
    float x,
    float y,
    char *error,
    size_t size
)
{
    surface *slot = find_surface(service, handle);
    workspace_document_view view;
    if (!slot || !isfinite(x) || !isfinite(y) || fabsf(x) > 1000000 || fabsf(y) > 1000000 ||
        workspace_document_read(service->documents, slot->view.document, &view) !=
            WORKSPACE_DOCUMENT_OK ||
        view.revision != revision) {
        return message(error, size, "Model pan requires a current revision and finite bounded coordinates");
    }
    visual_model_reference reference = { 0 };
    shader_error problem = { 0 };
    bool model = visual_model_reference_read(
        service->documents,
        slot->view.document,
        revision,
        &reference,
        &problem
    );
    visual_model_reference_free(&reference);
    if (!model) {
        return message(error, size, problem.message);
    }
    slot->view.pan_x = x;
    slot->view.pan_y = y;
    slot->view.drawn = false;
    slot->view.texture = 0;
    if (size) {
        error[0] = 0;
    }
    return true;
}
bool visual_preview_model_reference(
    visual_previews *service,
    int handle,
    int revision,
    const visual_model_reference *reference,
    bool visible,
    float opacity,
    char *error,
    size_t size
)
{
    surface *slot = find_surface(service, handle);
    visual_model_reference current = { 0 };
    shader_error problem = { 0 };
    if (!slot || !reference || !reference->image || !isfinite(opacity) ||
        opacity < 0 || opacity > 1 ||
        !visual_model_reference_read(service->documents, slot->view.document, revision, &current, &problem)) {
        return message(error, size, problem.message[0] ? problem.message : "Model overlay requires current source alignment and opacity0..1");
    }
    bool same = !strcmp(reference->image, current.image) &&
                reference->center_x == current.center_x &&
                reference->center_y == current.center_y &&
                reference->height == current.height &&
                reference->angle == current.angle &&
                reference->locked == current.locked;
    if (!same) {
        visual_model_reference_free(&current);
        return message(error, size, "Reference alignment differs from its canonical source revision");
    }
    visual_model_reference_free(&slot->reference);
    slot->reference = current;
    slot->view.reference_visible = visible;
    slot->view.reference_opacity = opacity;
    slot->view.drawn = false;
    slot->view.texture = 0;
    if (size) {
        error[0] = 0;
    }
    return true;
}
