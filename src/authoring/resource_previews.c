#include "authoring/resource_previews.h"

#include "authoring/document_semantics.h"
#include "authoring/image_prepare.h"
#include "authoring/resource_preview_data.h"
#include "authoring/ui_source_provider.h"
#include "authoring/visual_authoring_preview.h"
#include "authoring/visual_catalog.h"
#include "authoring/workspace_info.h"
#include "authoring/workspace_runtime.h"
#include "graph/authoring/layout.h"
#include "graph/document/serialize.h"
#include "platform/cute.h"
#include "render/field_image.h"
#include "render/shader_publish.h"
#include "render/ui.h"
#include "runtime/material_field.h"
#include "runtime/scenes/scene.h"
#include "runtime/ui/builtin_fonts.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/rooted_files.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum
{
    SLOT_MAX = 8,
    BYTES_MAX = 16 * 1024 * 1024
};
typedef struct
{
    int handle, document, visual, width, height, field_row;
    material_field_session *field_session;
    float *field_values;
    char *path, *kind;
    bool scene_pending, prototype;
    bool pending, ready;
    CF_Texture image_texture;
    CF_Canvas ui_canvas;
    uint64_t texture, texture_revision;
    char image[64], error[256];
} slot;
struct resource_previews
{
    rooted_files *root;
    int next;
    resource_preview_config config;
    char *root_path, *content;
    workspace_documents *documents;
    preview_world *world;
    struct render_resource_context *resources;
    visual_previews *visuals;
    workspace_runtime *runtime;
    bool seeded, destroying;
    slot slots[SLOT_MAX];
};
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static slot *find(resource_previews *p, int handle)
{
    for (int i = 0; i < SLOT_MAX; i++) {
        if (handle > 0 && p->slots[i].handle == handle) {
            return &p->slots[i];
        }
    }
    return NULL;
}
static bool upload(slot *s, void *pixels)
{
    CF_TextureParams params = cf_texture_defaults(s->width, s->height);
    params.pixel_format = CF_PIXEL_FORMAT_R8G8B8A8_UNORM_SRGB;
    params.filter = CF_FILTER_LINEAR;
    params.wrap_u = params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
    s->image_texture = cf_make_texture(params);
    if (!s->image_texture.id) {
        return fail(s->error, sizeof s->error, "Resource thumbnail texture allocation failed");
    }
    cf_texture_update(s->image_texture, pixels, s->width * s->height * 4);
    s->texture = s->image_texture.id;
    s->texture_revision = 1;
    return true;
}
static bool field_step(resource_previews *p, slot *s)
{
    material_fields *fields = materials_fields(p->world->runtime->materials);
    int rows = s->height - s->field_row;
    if (rows > 8) {
        rows = 8;
    }
    bool valid = material_field_session_bake_rows(
        s->field_session,
        fields,
        (field_grid){ .width = s->width, .height = s->height, .cell_size = 2 },
        s->field_row,
        rows,
        s->field_values,
        NULL,
        s->error,
        sizeof s->error
    );
    if (!valid) {
        return false;
    }
    s->field_row += rows;
    if (s->field_row < s->height) {
        return true;
    }
    CF_Pixel *pixels = xmalloc((size_t)s->width * s->height * sizeof *pixels);
    render_field_image(s->field_values, s->width, s->height, 2, pixels);
    valid = upload(s, pixels);
    free(pixels);
    material_field_session_free(s->field_session);
    s->field_session = NULL;
    free(s->field_values);
    s->field_values = NULL;
    return valid;
}
static bool raster(resource_previews *p, slot *s)
{
    unsigned char *source = NULL;
    size_t bytes = 0;
    if (!rooted_files_read(p->root, s->path, BYTES_MAX, &source, &bytes) || !bytes) {
        free(source);
        return fail(s->error, sizeof s->error, "Image must be a stable regular workspace file between1byte and16MiB");
    }
    shader_image_view decoded = { 0 };
    bool ok = image_prepare_decode(source, bytes, &decoded, s->error, sizeof s->error) !=
              NULL;
    free(source);
    if (!ok) {
        return false;
    }
    unsigned char *pixels = xcalloc((size_t)s->width * s->height, 4);
    /* Aspect-preserving fit with transparent letterboxing, deterministic nearest
     * sample centers. The source decoder owns its finite64MiB budget. */
    int width = s->width,
        height = (int)((int64_t)decoded.height * width / decoded.width);
    if (height > s->height) {
        height = s->height;
        width = (int)((int64_t)decoded.width * height / decoded.height);
    }
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    int left = (s->width - width) / 2, top = (s->height - height) / 2;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sx = (int)(((int64_t)x * 2 + 1) * decoded.width / (width * 2));
            int sy = (int)(((int64_t)y * 2 + 1) * decoded.height / (height * 2));
            memcpy(
                pixels + ((size_t)(top + y) * s->width + left + x) * 4,
                decoded.pixels + ((size_t)sy * decoded.width + sx) * 4,
                4
            );
        }
    }
    free((void *)decoded.pixels);
    ok = upload(s, pixels);
    free(pixels);
    return ok;
}
static bool stylesheet(resource_previews *p, slot *s)
{
    char *text = NULL;
    workspace_document_status status = workspace_document_read_path(
        p->config.overlays ? p->config.overlays : p->documents,
        s->path,
        &text
    );
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(s->error, sizeof s->error, workspace_document_status_text(status));
    }
    unsigned char colors[64][4];
    int count;
    bool valid = resource_preview_palette(
        text,
        colors,
        &count,
        s->error,
        sizeof s->error
    );
    free(text);
    if (!valid) {
        return false;
    }
    int columns = 1;
    while (columns * columns < count) {
        columns++;
    }
    int rows = (count + columns - 1) / columns;
    unsigned char *pixels = xcalloc((size_t)s->width * s->height, 4);
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            int index = y * rows / s->height * columns + x * columns / s->width;
            if (index < count) {
                memcpy(pixels + ((size_t)y * s->width + x) * 4, colors[index], 4);
            }
        }
    }
    valid = upload(s, pixels);
    free(pixels);
    return valid;
}
static const char *relative_content(const resource_previews *p)
{
    size_t root = strlen(p->root_path);
    return !strncmp(p->content, p->root_path, root) && p->content[root] == '/'
             ? p->content + root + 1
             : NULL;
}
static bool source_document(resource_previews *p, slot *s)
{
    char *text = NULL;
    workspace_document_status status = workspace_document_read_path(
        p->config.overlays ? p->config.overlays : p->documents,
        s->path,
        &text
    );
    if (!status) {
        status = workspace_document_open(p->documents, s->path, &s->document);
    }
    workspace_document_view view;
    if (!status) {
        status = workspace_document_read(p->documents, s->document, &view);
    }
    if (!status && strcmp(view.text, text)) {
        status = workspace_document_edit(
            p->documents,
            s->document,
            view.revision,
            0,
            (int)strlen(view.text),
            text
        );
    }
    free(text);
    return status
             ? fail(s->error, sizeof s->error, workspace_document_status_text(status))
             : true;
}
static bool source_graph(resource_previews *p, slot *s)
{
    if (!p->config.source_catalog || !p->config.source_catalog_release) {
        return fail(s->error, sizeof s->error, "Graph preview requires a retained source/type catalog");
    }
    if (!source_document(p, s)) {
        return false;
    }
    const script_catalog *catalog = NULL;
    void *owner = NULL;
    if (!p->config.source_catalog(
            p->config.source_catalog_user,
            &catalog,
            &owner,
            s->error,
            sizeof s->error
        )) {
        return false;
    }
    workspace_document_view view;
    workspace_document_read(p->documents, s->document, &view);
    document_semantics *semantics = document_semantics_new_content(
        p->documents,
        s->document,
        view.revision,
        catalog,
        relative_content(p),
        s->error,
        sizeof s->error
    );
    const behavior_asset *asset = semantics ? document_semantics_graph(semantics)
                                            : NULL;
    behavior_asset *owned = asset ? behavior_asset_clone(asset) : NULL;
    graph *selected = owned ? owned->graph : NULL;
    if (owned) {
        for (int i = 0; i < acount(owned->graphs); i++) {
            if (!selected || acount(owned->graphs[i]->nodes) > acount(selected->nodes)) {
                selected = owned->graphs[i];
            }
        }
    }
    if (selected && acount(selected->nodes) <= 512 && acount(selected->edges) <= 2048) {
        graph_place_unplaced(selected);
    }
    unsigned char *pixels = xmalloc((size_t)s->width * s->height * 4);
    bool valid = selected && resource_preview_graph(
                                 selected,
                                 s->width,
                                 s->height,
                                 pixels,
                                 s->error,
                                 sizeof s->error
                             );
    if (valid) {
        valid = upload(s, pixels);
    }
    free(pixels);
    behavior_asset_free(owned);
    document_semantics_free(semantics);
    p->config.source_catalog_release(p->config.source_catalog_user, owner);
    if (!valid && !s->error[0]) {
        fail(s->error, sizeof s->error, "Source has no graph presentation to preview");
    }
    return valid;
}
static bool ui_resources(const ui_document *document, slot *s)
{
    ui_source_resource *resources = NULL;
    bool valid = ui_source_resources(
        document->rml,
        document->rcss,
        &resources,
        s->error,
        sizeof s->error
    );
    size_t decoded = 0;
    int images = 0;
    for (int i = 0; valid && i < acount(resources); i++) {
        const ui_source_resource *resource = &resources[i];
        if (resource->kind == UI_SOURCE_RESOURCE_FONT_FAMILY) {
            bool found = false;
            for (int f = 0; f < ui_builtin_font_count(); f++) {
                if (!strcasecmp(resource->text, ui_builtin_font_at(f).family)) {
                    found = true;
                }
            }
            if (!found) {
                valid = fail(s->error, sizeof s->error, "UI preview font has no supplied builtin bytes");
            }
            continue;
        }
        if (++images > 64) {
            valid = fail(s->error, sizeof s->error, "UI thumbnail exceeds64static images");
            break;
        }
        char *key = xstrdup(resource->text);
        size_t length = strlen(key);
        if (length >= 8 && !strcmp(key + length - 8, "#nearest")) {
            key[length - 8] = 0;
        }
        if (strncmp(key, "content/", 8) || strstr(key, "..") || strchr(key, '\\')) {
            valid = fail(s->error, sizeof s->error, "UI thumbnail image must be a canonical content resource");
        }
        int resident_width = 0, resident_height = 0;
        bool resident = valid &&
                        render_image_info(key, &resident_width, &resident_height);
        if (resident) {
            free(key);
            continue;
        }
        if (valid) {
            valid = render_image_reload(key, s->error, sizeof s->error);
        }
        int width = 0, height = 0;
        if (valid) {
            valid = render_image_info(key, &width, &height);
        }
        size_t bytes = (size_t)width * height * 4;
        if (valid && bytes > 64 * 1024 * 1024 - decoded) {
            valid = fail(s->error, sizeof s->error, "UI thumbnail image closure exceeds64MiB");
        }
        if (valid) {
            decoded += bytes;
        }
        free(key);
    }
    ui_source_resources_free(resources);
    return valid;
}
static bool source_ui(resource_previews *p, slot *s)
{
    const char *content = relative_content(p);
    if (!content) {
        return fail(s->error, sizeof s->error, "UI thumbnail requires a contained content root");
    }
    size_t prefix = strlen(content);
    if (strncmp(s->path, content, prefix) || strncmp(s->path + prefix, "/ui/", 4)) {
        return fail(s->error, sizeof s->error, "UI thumbnail path is outside the authored UI folder");
    }
    const char *relative = s->path + prefix + 4;
    size_t length = strlen(relative);
    if (length <= 4 || strcmp(relative + length - 4, ".rml")) {
        return fail(s->error, sizeof s->error, "UI thumbnail requires an RML page or widget");
    }
    char *name = xstrdup(relative);
    name[length - 4] = 0;
    ui_source_provider *provider = ui_source_provider_new(
        p->root_path,
        content,
        p->config.overlays ? p->config.overlays : p->documents,
        s->error,
        sizeof s->error
    );
    render_ui_loader loader = provider ? ui_source_provider_loader(provider)
                                       : (render_ui_loader){ 0 };
    ui_document document = { 0 };
    int read = 0;
    if (provider) {
        void *state = loader.open_context(loader.context, "", name, true);
        read = loader.read(state, 0, &document, s->error, sizeof s->error);
        loader.close(state);
    }
    bool page = document.body != NULL;
    render_resource_context *resources = render_resource_context_new();
    render_resource_scope scope = { 0 };
    bool entered = read == 1 && render_resource_scope_enter(
                                    resources,
                                    &scope,
                                    s->error,
                                    sizeof s->error
                                );
    render_ui *renderer = NULL;
    ecs_world *empty = NULL;
    bool valid = entered;
    if (valid) {
        render_image_loader owned;
        valid = render_image_loader_copy(p->config.images, &owned);
        if (valid) {
            render_images_set_loader(owned);
            valid = ui_resources(&document, s);
        }
    }
    if (valid) {
        renderer = render_ui_new("", NULL);
        render_ui_set_loader(renderer, &loader);
        if (!page) {
            render_ui_set_overlay(renderer, name, "");
        }
        empty = ecs_world_new();
        ui_world model = { .ecs = empty, .page = page ? name : "", .read_only = true };
        render_ui_update(renderer, &model, s->width, s->height, 0);
        const char *failure = render_ui_error(renderer);
        valid = !*failure;
        if (!valid) {
            fail(s->error, sizeof s->error, failure);
        }
        if (valid) {
            s->ui_canvas = cf_make_canvas(cf_canvas_defaults(s->width, s->height));
            valid = s->ui_canvas.id != 0;
            if (valid) {
                cf_draw_push();
                cf_render_to(s->ui_canvas, true);
                render_ui_composite(renderer, s->ui_canvas);
                cf_draw_pop();
                s->texture = cf_canvas_get_target(s->ui_canvas).id;
                s->texture_revision = 1;
            }
        }
    }
    if (renderer) {
        render_ui_free(renderer);
    }
    if (empty) {
        ecs_world_free(empty);
    }
    if (entered) {
        char leave_error[256];
        bool left = render_resource_scope_leave(&scope, leave_error, sizeof leave_error);
        assert(left);
        (void)left;
    }
    char cleanup[256];
    bool freed = render_resource_context_free(resources, cleanup, sizeof cleanup);
    assert(freed);
    (void)freed;
    ui_document_free(&document);
    ui_source_provider_free(provider);
    free(name);
    return valid;
}
static char *resource_kind(resource_previews *p, const char *path)
{
    if (!p->config.inventory) {
        return xstrdup("");
    }
    arena storage = { 0 };
    pin_array *rows;
    char *kind = NULL;
    for (int first = 0; first < WORKSPACE_RESOURCE_ROWS_MAX && !kind; first += 256) {
        workspace_resource_result result = workspace_resources_query(
            p->config.inventory,
            first,
            256,
            "",
            path,
            &storage,
            &rows
        );
        for (int i = 0; i < rows->count; i++) {
            workspace_resource_row *row = rows->items[i].ptr;
            if (!strcmp(row->path, path) && !row->unresolved) {
                kind = xstrdup(row->kind);
                break;
            }
        }
        if (first + 256 >= result.total) {
            break;
        }
    }
    arena_free(&storage);
    return kind ? kind : xstrdup("");
}
static char *prototype_scene(resource_previews *p, const char *source, slot *s)
{
    av_doc *prototype = av_doc_read(source, s->error, sizeof s->error);
    const char *name = prototype
                         ? av_get_str(av_obj_get(av_doc_root(prototype), "name"))
                         : NULL;
    if (!name || !*name) {
        av_doc_free(prototype);
        fail(s->error, sizeof s->error, "Prototype has no declared name");
        return NULL;
    }
    workspace_info *info = workspace_info_new(p->root_path, s->error, sizeof s->error);
    char *base = NULL;
    if (info) {
        workspace_document_read_path(
            p->config.overlays ? p->config.overlays : p->documents,
            workspace_info_get(info)->start_scene,
            &base
        );
    }
    av_doc *scene = base ? av_doc_read(base, s->error, sizeof s->error)
                         : av_doc_new();
    free(base);
    workspace_info_free(info);
    if (!scene) {
        av_doc_free(prototype);
        return NULL;
    }
    if (!av_is_obj(av_doc_root(scene))) {
        av_doc_set_root(scene, av_obj(scene));
    }
    av_value *root = av_doc_root(scene), *entities = av_arr(scene),
             *entity = av_obj(scene);
    av_obj_set(scene, root, "name", av_str(scene, "Resource Preview"));
    av_obj_set(scene, root, "ui", av_str(scene, ""));
    av_obj_set(scene, entity, "name", av_str(scene, "Preview"));
    av_obj_set(scene, entity, "prototype", av_str(scene, name));
    av_arr_add_val(entities, entity);
    av_obj_set(scene, root, "entities", entities);
    char *text = av_doc_write(scene);
    av_doc_free(scene);
    av_doc_free(prototype);
    return text;
}
static void scene_retire(resource_previews *p)
{
    workspace_runtime_free(p->runtime);
    p->runtime = NULL;
}
static bool scene_copy(slot *s, uint64_t texture)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = (CF_Texture){ .id = texture };
    CF_Canvas source = cf_make_canvas(params);
    if (!source.id) {
        return fail(s->error, sizeof s->error, "Scene thumbnail source attachment failed");
    }
    s->ui_canvas = cf_make_canvas(cf_canvas_defaults(s->width, s->height));
    bool valid = s->ui_canvas.id != 0;
    if (valid) {
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(0, 0, (float)s->width, (float)s->height));
        cf_draw_canvas(source, cf_v2(0, 0), cf_v2((float)s->width, (float)s->height));
        cf_render_to(s->ui_canvas, true);
        cf_draw_pop();
        s->texture = cf_canvas_get_target(s->ui_canvas).id;
        s->texture_revision = 1;
    }
    cf_destroy_canvas(source);
    return valid;
}
static bool scene_step(resource_previews *p, slot *s)
{
    workspace_runtime_result result = workspace_runtime_poll(p->runtime);
    if (result.preparing) {
        return true;
    }
    s->scene_pending = false;
    bool valid = result.ok && result.ready;
    if (!valid) {
        fail(s->error, sizeof s->error, result.error);
    }
    if (valid && s->prototype) {
        CF_V3 low, high;
        valid = workspace_runtime_bounds(
            p->runtime,
            result.session,
            NULL,
            0,
            &low,
            &high,
            s->error,
            sizeof s->error
        );
        if (valid) {
            bool perspective = scene_perspective(
                scene_instance_scene(workspace_runtime_scene(p->runtime))
            );
            float x = high.x - low.x, y = high.y - low.y, z = high.z - low.z,
                  aspect = (float)s->width / s->height;
            float span = perspective
                           ? sqrtf(x * x + y * y + z * z) * 1.4f * fmaxf(1, aspect)
                           : fmaxf(x, z * aspect) * 1.2f;
            engine_viewport_view view = { .center_x = (low.x + high.x) * .5f,
                                          .center_y = -(low.z + high.z) * .5f,
                                          .target_elevation = (low.y + high.y) * .5f,
                                          .width = fmaxf(.1f, span),
                                          .height = fmaxf(.1f, span) / aspect,
                                          .pitch = .6f };
            valid = workspace_runtime_set_view(p->runtime, result.session, &view, perspective);
        }
    }
    if (valid) {
        result = workspace_runtime_draw(p->runtime, result.session, s->width, s->height, 0);
        valid = result.ok && result.texture;
        if (!valid) {
            fail(s->error, sizeof s->error, result.error);
        }
    }
    if (valid) {
        valid = scene_copy(s, result.texture);
    }
    scene_retire(p);
    return valid;
}
static bool source_scene(resource_previews *p, slot *s)
{
    if (!p->config.scene_runtime) {
        return fail(s->error, sizeof s->error, "Scene preview requires a private readonly runtime factory");
    }
    assert(!p->runtime);
    char *source = NULL;
    workspace_document_status status = workspace_document_read_path(
        p->config.overlays ? p->config.overlays : p->documents,
        s->path,
        &source
    );
    if (status) {
        return fail(s->error, sizeof s->error, workspace_document_status_text(status));
    }
    s->prototype = !strcmp(s->kind, "prototype");
    if (s->prototype) {
        char *wrapped = prototype_scene(p, source, s);
        free(source);
        source = wrapped;
    }
    if (!source) {
        return false;
    }
    p->runtime = p->config.scene_runtime(
        p->config.scene_runtime_user,
        p->config.overlays ? p->config.overlays : p->documents,
        p->root_path,
        s->error,
        sizeof s->error
    );
    if (!p->runtime) {
        free(source);
        return false;
    }
    workspace_runtime_result result = workspace_runtime_prepare_scene_source(
        p->runtime,
        ".chibi/resource-preview/scene.kdl",
        source
    );
    free(source);
    if (!result.ok) {
        fail(s->error, sizeof s->error, result.error);
        scene_retire(p);
        return false;
    }
    s->scene_pending = result.preparing;
    return scene_step(p, s);
}
static void retire_visuals(resource_previews *p)
{
    visual_previews_free(p->visuals);
    p->visuals = NULL;
    char error[256];
    render_resource_scope scope = { 0 };
    if (p->world) {
        bool entered = render_resource_scope_enter(p->resources, &scope, error, sizeof error);
        assert(entered);
        if (entered) {
            preview_world_free(p->world);
            bool left = render_resource_scope_leave(&scope, error, sizeof error);
            assert(left);
            (void)left;
        }
        p->world = NULL;
    }
    if (p->resources) {
        bool freed = render_resource_context_free(p->resources, error, sizeof error);
        assert(freed);
        (void)freed;
        p->resources = NULL;
    }
}
static bool seed(resource_previews *p, char *error, size_t size)
{
    if (p->seeded) {
        return true;
    }
    if (p->config.inventory) {
        preview_world *world = NULL;
        render_resource_context *resources = NULL;
        if (!visual_catalog_build(
                p->config.overlays ? p->config.overlays : p->documents,
                p->root_path,
                p->config.inventory,
                p->config.images,
                &world,
                &resources,
                error,
                size
            )) {
            return false;
        }
        retire_visuals(p);
        p->world = world;
        p->resources = resources;
        p->visuals = visual_previews_new(p->documents, world, p->content, p->root_path);
        visual_previews_set_resources(p->visuals, resources);
        p->seeded = true;
        return true;
    }
    int count = 0;
    behavior_asset **assets = p->config.catalog
                                ? p->config.catalog(p->config.catalog_user, &count)
                                : NULL;
    bool ok = count >= 0 && count <= WORKSPACE_DOCUMENT_COUNT_MAX &&
              (!count || assets);
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX] = { 0 };
    visual_publication_result results[WORKSPACE_DOCUMENT_COUNT_MAX];
    for (int i = 0; ok && i < count; i++) {
        char *text = behavior_asset_save(assets[i]);
        ok = text && workspace_document_memory(p->documents, text, &handles[i]) ==
                         WORKSPACE_DOCUMENT_OK;
        free(text);
    }
    if (ok && count) {
        ok = visual_publish_documents(p->visuals, handles, count, results, error, size);
    }
    for (int i = 0; i < count; i++) {
        if (i < WORKSPACE_DOCUMENT_COUNT_MAX && handles[i]) {
            workspace_document_close(p->documents, handles[i]);
        }
        if (assets) {
            behavior_asset_free(assets[i]);
        }
    }
    free(assets);
    if (!ok && size && !*error) {
        fail(error, size, "Background visual catalog exceeds64sources or cannot publish");
    }
    p->seeded = ok;
    return ok;
}
static bool visual(resource_previews *p, slot *s)
{
    if (!seed(p, s->error, sizeof s->error)) {
        return false;
    }
    char *source = NULL;
    workspace_document_status status = workspace_document_read_path(
        p->config.overlays ? p->config.overlays : p->documents,
        s->path,
        &source
    );
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(s->error, sizeof s->error, workspace_document_status_text(status));
    }
    status = workspace_document_memory(p->documents, source, &s->document);
    free(source);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(s->error, sizeof s->error, workspace_document_status_text(status));
    }
    workspace_document_view document;
    workspace_document_read(p->documents, s->document, &document);
    if (!visual_preview_publish(
            p->visuals,
            s->document,
            document.revision,
            s->error,
            sizeof s->error
        )) {
        return false;
    }
    behavior_asset *asset = behavior_asset_load(document.text);
    bool field = asset && asset->domain == BEHAVIOR_DOMAIN_FIELD;
    if (field) {
        material_fields *fields = materials_fields(p->world->runtime->materials);
        field_context context = { 0 };
        bool valid = material_field_context(
            fields,
            asset->name,
            "",
            0,
            &context,
            s->error,
            sizeof s->error
        );
        if (valid) {
            s->field_session = material_field_session_new(
                fields,
                asset->name,
                context,
                NULL,
                s->error,
                sizeof s->error
            );
        }
        behavior_asset_free(asset);
        if (!s->field_session) {
            return false;
        }
        s->field_values = xmalloc((size_t)s->width * s->height * sizeof *s->field_values);
        return field_step(p, s);
    }
    behavior_asset_free(asset);
    if (!visual_preview_open(
            p->visuals,
            s->document,
            s->width,
            s->height,
            &s->visual,
            s->error,
            sizeof s->error
        ) ||
        !visual_preview_draw(p->visuals, s->visual, document.revision, 0, 0, 0, 1)) {
        return false;
    }
    visual_preview_view view = { 0 };
    if (!visual_preview_read(p->visuals, s->visual, &view) || !view.drawn) {
        return fail(s->error, sizeof s->error, view.error ? view.error : "Resource preview did not render");
    }
    s->texture = view.texture;
    s->texture_revision = view.texture_revision;
    return true;
}
resource_previews *resource_previews_new(const resource_preview_config *config, char *error, size_t size)
{
    resource_previews *p = xcalloc(1, sizeof *p);
    p->next = 1;
    if (!config || !config->root || !config->content) {
        fail(error, size, "Resource previews require explicit roots");
        resource_previews_free(p);
        return NULL;
    }
    p->root_path = xstrdup(config->root);
    p->content = xstrdup(config->content);
    p->config = *config;
    p->config.images = (render_image_loader){ 0 };
    bool loader_ready = config->images.read
                          ? render_image_loader_copy(config->images, &p->config.images)
                          : image_prepare_loader_new(p->content, &p->config.images, error, size);
    if (!loader_ready) {
        fail(error, size, "Background preview image loader is unavailable or cannot be retained");
        resource_previews_free(p);
        return NULL;
    }
    p->root = rooted_files_open(config->root);
    p->documents = workspace_documents_new(config->root, NULL);
    p->resources = render_resource_context_new();
    render_resource_scope scope = { 0 };
    if (!p->root || !p->documents ||
        !render_resource_scope_enter(p->resources, &scope, error, size)) {
        resource_previews_free(p);
        return NULL;
    }
    p->world = preview_world_new(NULL);
    bool left = render_resource_scope_leave(&scope, error, size);
    if (!left || !p->world) {
        resource_previews_free(p);
        return NULL;
    }
    p->visuals = visual_previews_new(p->documents, p->world, p->content, p->root_path);
    visual_previews_set_resources(p->visuals, p->resources);
    return p;
}
static void reset_visuals(resource_previews *p)
{
    retire_visuals(p);
    p->resources = render_resource_context_new();
    char error[256];
    render_resource_scope scope = { 0 };
    bool entered = render_resource_scope_enter(p->resources, &scope, error, sizeof error);
    assert(entered);
    if (!entered) {
        return;
    }
    p->world = preview_world_new(NULL);
    bool left = render_resource_scope_leave(&scope, error, sizeof error);
    assert(left);
    (void)left;
    p->visuals = visual_previews_new(p->documents, p->world, p->content, p->root_path);
    visual_previews_set_resources(p->visuals, p->resources);
    p->seeded = false;
}
int resource_preview_open(resource_previews *p, const char *path, int width, int height, char *error, size_t size)
{
    if (!p || width < 64 || width > 256 || height < 64 || height > 256 || !path ||
        !*path || strlen(path) >= WORKSPACE_DOCUMENT_PATH_MAX || p->next == INT_MAX) {
        fail(error, size, "Resource preview requires a rooted path and64..256pixel sides");
        return 0;
    }
    for (int i = 0; i < SLOT_MAX; i++) {
        if (!p->slots[i].handle) {
            slot *s = &p->slots[i];
            *s = (slot){ .handle = p->next++,
                         .width = width,
                         .height = height,
                         .pending = true,
                         .path = xstrdup(path) };
            snprintf(s->image, sizeof s->image, "kit:resource/%d", s->handle);
            return s->handle;
        }
    }
    fail(error, size, "Close a resource preview before opening another; the service retains8surfaces");
    return 0;
}
bool resource_preview_read(resource_previews *p, int handle, resource_preview_view *out)
{
    slot *s = find(p, handle);
    if (!s) {
        return false;
    }
    *out = (resource_preview_view){ s->handle,
                                    s->width,
                                    s->height,
                                    s->ready,
                                    s->pending ? "pending"
                                    : s->ready ? "ready"
                                               : "failed",
                                    s->ready ? s->image : "",
                                    s->error,
                                    s->texture,
                                    s->texture_revision };
    return true;
}
int resource_preview_handles(resource_previews *p, int *out, int capacity)
{
    int count = 0;
    for (int i = 0; i < SLOT_MAX; i++) {
        if (p->slots[i].handle) {
            if (out && count < capacity) {
                out[count] = p->slots[i].handle;
            }
            count++;
        }
    }
    return count;
}
bool resource_preview_close(resource_previews *p, int handle)
{
    slot *s = find(p, handle);
    if (!s) {
        return false;
    }
    if (s->visual) {
        visual_preview_close(p->visuals, s->visual);
    }
    if (s->document) {
        workspace_document_close(p->documents, s->document);
    }
    if (s->image_texture.id) {
        cf_destroy_texture(s->image_texture);
    }
    if (s->ui_canvas.id) {
        cf_destroy_canvas(s->ui_canvas);
    }
    material_field_session_free(s->field_session);
    free(s->field_values);
    if (s->scene_pending) {
        scene_retire(p);
    }
    free(s->path);
    free(s->kind);
    *s = (slot){ 0 };
    if (!p->destroying && p->seeded && resource_preview_handles(p, NULL, 0) == 0) {
        reset_visuals(p);
    }
    return true;
}
void resource_previews_tick(resource_previews *p)
{
    for (int i = 0; i < SLOT_MAX; i++) {
        slot *s = &p->slots[i];
        if (!s->pending) {
            continue;
        }
        if (s->scene_pending) {
            s->ready = scene_step(p, s);
        } else if (s->field_session) {
            s->ready = field_step(p, s);
        } else {
            if (!s->kind) {
                s->kind = resource_kind(p, s->path);
            }
            switch (resource_preview_route(s->kind, s->path)) {
                case RESOURCE_PREVIEW_SCENE:
                    s->ready = source_scene(p, s);
                    break;
                case RESOURCE_PREVIEW_UI: s->ready = source_ui(p, s); break;
                case RESOURCE_PREVIEW_GRAPH:
                    s->ready = source_graph(p, s);
                    break;
                case RESOURCE_PREVIEW_STYLESHEET:
                    s->ready = stylesheet(p, s);
                    break;
                case RESOURCE_PREVIEW_RASTER: s->ready = raster(p, s); break;
                case RESOURCE_PREVIEW_VISUAL: s->ready = visual(p, s); break;
                case RESOURCE_PREVIEW_NONE: s->ready = false; break;
            }
        }
        s->pending = s->ready && (s->field_session || s->scene_pending);
        if (s->pending) {
            s->ready = false;
        }
        if (!s->pending && !s->ready && !*s->error) {
            fail(s->error, sizeof s->error, "This resource cannot be previewed");
        }
        break;
    }
}
void resource_previews_free(resource_previews *p)
{
    if (!p) {
        return;
    }
    p->destroying = true;
    for (int i = 0; i < SLOT_MAX; i++) {
        if (p->slots[i].handle) {
            resource_preview_close(p, p->slots[i].handle);
        }
    }
    retire_visuals(p);
    workspace_documents_free(p->documents);
    if (p->root) {
        rooted_files_close(p->root);
    }
    render_image_loader_release(&p->config.images);
    free(p->root_path);
    free(p->content);
    free(p);
}
