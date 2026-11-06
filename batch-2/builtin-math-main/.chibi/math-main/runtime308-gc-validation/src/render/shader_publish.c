#include "render/shader_publish.h"

#include "render/catalog_publication.h"
#include "render/scene3d_parts_table.h"
#include "render/shader_catalog_records.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_prepared.h"
#include "runtime/images/sky.h"
#include "runtime/images/table.h"
#include "runtime/models/prepared.h"
#include "runtime/shaders/prepared.h"
#include "runtime/shaders/volume.h"
#include "runtime/ui/documents.h"
#include "runtime/ui/font.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct resource_generation
{
    shader_asset *assets;
    shader_values *values;
    render_parts_table *parts;
    render_image_table *images;
    render_mesh_table *meshes;
    render_sky_table *skies;
    ui_documents *documents;
    render_parts_table *empty_parts;
    render_image_table *empty_images;
    render_mesh_table *empty_meshes;
    render_sky_table *empty_skies;
    ui_documents *empty_documents;
    ui_font *fonts;
    size_t font_storage;
    bool installed;
} resource_generation;
static resource_generation *current;
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static int asset_index(const resource_generation *g, const char *name)
{
    for (int i = 0; i < acount(g->assets); i++) {
        if (!strcmp(name, g->assets[i].name)) {
            return i;
        }
    }
    return -1;
}
static bool generation_image(void *context, const shader_image_source *source, shader_image_view *view)
{
    resource_generation *g = context;
    return g->installed ? render_image_resolve(NULL, source, view)
                        : render_image_table_resolve(g->images, source, view);
}
static void free_assets(shader_asset *assets)
{
    for (int i = 0; i < acount(assets); i++) {
        shader_catalog_free_asset(&assets[i]);
    }
    afree(assets);
}
static void discard(void *, void *generation)
{
    resource_generation *g = generation;
    assert(g && !g->installed);
    free_assets(g->assets);
    shader_values_release(g->values);
    render_parts_free(g->parts);
    render_image_table_free(g->images);
    render_mesh_table_free(g->meshes);
    render_sky_table_free(g->skies);
    ui_documents_free(g->documents);
    render_parts_free(g->empty_parts);
    render_image_table_free(g->empty_images);
    render_mesh_table_free(g->empty_meshes);
    render_sky_table_free(g->empty_skies);
    ui_documents_free(g->empty_documents);
    for (int i = 0; i < acount(g->fonts); i++) {
        ui_font_free(&g->fonts[i]);
    }
    afree(g->fonts);
    free(g);
}
static bool graph_equal(const shader_graph *a, const shader_graph *b, shader_error *error)
{
    unsigned char *left = NULL, *right = NULL;
    size_t ln = 0, rn = 0;
    bool ok = shader_graph_prepared_write(a, &left, &ln, error) &&
              shader_graph_prepared_write(b, &right, &rn, error) && ln == rn &&
              !memcmp(left, right, ln);
    free(left);
    free(right);
    return ok;
}
static bool prepare_shader(resource_generation *g, const player_asset *record, size_t *budget, shader_error *error)
{
    shader_prepared_asset value;
    shader_prepared_asset_init(&value);
    if (!shader_asset_prepared_read_budget(record->data, record->length, budget, &value, error)) {
        return false;
    }
    if (strcmp(record->name, value.name)) {
        shader_prepared_asset_free(&value);
        snprintf(error->message, sizeof error->message, "Shader envelope identity differs");
        return false;
    }
    shader_asset asset = { .name = value.name,
                           .graph = value.graph,
                           .shape = value.shape,
                           .alpha_mode = value.alpha_mode };
    memcpy(asset.names, value.names, sizeof asset.names);
    memcpy(asset.widths, value.widths, sizeof asset.widths);
    memcpy(asset.defaults, value.defaults, sizeof asset.defaults);
    memcpy(asset.bounds, value.bounds, sizeof asset.bounds);
    apush(g->assets, asset);
    return true;
}
static bool prepare_model(resource_generation *g, const player_asset *record, size_t *budget, shader_error *error)
{
    int index = asset_index(g, record->name);
    if (index < 0 || !g->assets[index].shape) {
        snprintf(error->message, sizeof error->message, "Model requires its matching shape shader envelope");
        return false;
    }
    shader_asset *asset = &g->assets[index];
    if (!model_program_prepared_read_budget(
            record->data,
            record->length,
            budget,
            &asset->model,
            error
        )) {
        return false;
    }
    if (!graph_equal(&asset->graph, &asset->model.shader, error)) {
        snprintf(error->message, sizeof error->message, "Model and shape shader snapshots differ");
        return false;
    }
    int count = acount(asset->model.parts);
    int capacity = count ? 16 : 0;
    while (capacity < count) {
        capacity *= 2;
    }
    size_t inspection_bytes = capacity
                                ? (size_t)capacity * sizeof(model_part_inspection) +
                                      sizeof(dyna_hdr)
                                : 0;
    for (int i = 0; i < count; i++) {
        const model_part *part = &asset->model.parts[i];
        inspection_bytes += strlen(part->asset ? part->asset : "") +
                            strlen(part->name ? part->name : "") +
                            strlen(part->path ? part->path : "") + 3;
    }
    if (inspection_bytes > *budget) {
        snprintf(error->message, sizeof error->message, "Prepared model inspection budget exceeded");
        return false;
    }
    *budget -= inspection_bytes;
    model_program_inspect(&asset->model, &asset->inspection);
    shader_graph_free(&asset->model.shader);
    shader_graph_init(&asset->model.shader);
    asset->model.resolve_image = generation_image;
    asset->model.image_userdata = g;
    asset->is_model = true;
    asset->inspection.image_limit = RENDER_SHADER_IMAGE_MAX;
    asset->inspection.ramp_limit = RENDER_SHADER_RAMP_VEC4_MAX;
    return true;
}
static bool materials_valid(const resource_generation *g, shader_error *error)
{
    const shader_asset **catalog = NULL;
    for (int i = 0; i < acount(g->assets); i++) {
        apush(catalog, &g->assets[i]);
    }
    bool ok = true;
    for (int i = 0; i < acount(g->assets) && ok; i++) {
        const shader_asset *a = &g->assets[i];
        if (!a->is_model) {
            continue;
        }
        for (int j = 0; j < acount(a->model.material_dependencies); j++) {
            int index = asset_index(g, a->model.material_dependencies[j]);
            if (index < 0 || g->assets[index].shape ||
                g->assets[index].graph.output_count != SHADER_OUTPUT_MAX) {
                ok = false;
                break;
            }
        }
        model_material_entry entries[MODEL_MATERIAL_SLOT_MAX + 1];
        int count;
        float values[32][4];
        if (ok) {
            ok = shader_catalog_model_material_entries(
                a,
                NULL,
                catalog,
                acount(catalog),
                entries,
                &count,
                values,
                error
            );
        }
    }
    for (int i = 0; i < render_parts_count(g->parts) && ok; i++) {
        const scene3d_parts *parts = render_parts_at(g->parts, i);
        for (int j = 0; j < parts->count; j++) {
            const char *name = parts->parts[j].material;
            int index = *name ? asset_index(g, name) : 0;
            if (*name && (index < 0 || g->assets[index].shape ||
                          g->assets[index].graph.output_count != SHADER_OUTPUT_MAX)) {
                ok = false;
                break;
            }
        }
    }
    for (int i = 0; i < render_mesh_table_source_count(g->meshes) && ok; i++) {
        const render_mesh_asset *mesh = render_mesh_table_asset_at(g->meshes, i);
        for (int j = 0; j < acount(mesh->groups); j++) {
            const char *name = mesh->groups[j].material;
            int index = name && *name ? asset_index(g, name) : 0;
            if (name && *name &&
                (index < 0 || g->assets[index].shape ||
                 g->assets[index].graph.output_count != SHADER_OUTPUT_MAX)) {
                ok = false;
                break;
            }
        }
    }
    afree(catalog);
    if (!ok && !error->message[0]) {
        snprintf(error->message, sizeof error->message, "Prepared material dependency is missing or incompatible");
    }
    return ok;
}
static bool prepare_graphs(resource_generation *g, size_t *budget, shader_error *error)
{
    for (int i = 0; i < acount(g->assets); i++) {
        shader_asset *a = &g->assets[i];
        a->material_inspection = shader_graph_inspect(&a->graph);
        if (acount(a->graph.images) > RENDER_SHADER_IMAGE_MAX) {
            return false;
        }
        int image_count = acount(a->graph.images);
        size_t image_rows = image_count
                              ? (size_t)(image_count < 16 ? 16 : image_count) *
                                        sizeof(int) +
                                    sizeof(dyna_hdr)
                              : 0;
        if (image_rows > *budget) {
            snprintf(error->message, sizeof error->message, "Prepared sampler ownership budget exceeded");
            return false;
        }
        *budget -= image_rows;
        if (image_count) {
            asetcap(a->images, image_count);
        }
        for (int j = 0; j < acount(a->graph.images); j++) {
            int variant = render_image_table_variant(g->images, &a->graph.images[j], budget, error);
            if (!variant) {
                return false;
            }
            apush(a->images, variant);
        }
        for (int j = 0; j < acount(a->graph.nodes); j++) {
            const shader_node *n = &a->graph.nodes[j];
            if (n->op == SHADER_SAMPLED_SDF) {
                shader_image_view view;
                if (!render_image_table_resolve(g->images, &a->graph.images[n->image], &view) ||
                    !shader_volume_validate(n, &view, error)) {
                    return false;
                }
            }
        }
        char prefix[64];
        snprintf(prefix, sizeof prefix, "sg_asset_%d", i + 1);
        a->glsl = shader_graph_glsl_named_ramps(&a->graph, prefix, 0, error);
        if (!a->glsl) {
            return false;
        }
        size_t bytes = strlen(a->glsl) + 1;
        if (a->is_model) {
            snprintf(prefix, sizeof prefix, "sg_asset_%d_hit", i + 1);
            char *hit = shader_graph_glsl_named(&a->model.hit_shader, prefix, error);
            if (!hit) {
                return false;
            }
            size_t hit_bytes = strlen(hit);
            if (hit_bytes > *budget || bytes > *budget - hit_bytes) {
                free(hit);
                snprintf(error->message, sizeof error->message, "Prepared renderer ownership budget exceeded");
                return false;
            }
            a->glsl = xrealloc(a->glsl, bytes + hit_bytes);
            memcpy(a->glsl + bytes - 1, hit, hit_bytes + 1);
            bytes += hit_bytes;
            free(hit);
        }
        if (bytes > *budget) {
            snprintf(error->message, sizeof error->message, "Prepared renderer ownership budget exceeded");
            return false;
        }
        *budget -= bytes;
    }
    return materials_valid(g, error);
}
static void *stage_decoded(const player_asset *records, int count, size_t *remaining, char *error, size_t size)
{
    if (count < 0 || count > PLAYER_MAX_ASSETS || (count && !records)) {
        fail(error, size, "Invalid prepared renderer collection");
        return NULL;
    }
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        const player_asset *r = &records[i];
        if (!r->name || !r->name[0] || strnlen(r->name, 1025) > 1024 ||
            r->kind < PLAYER_ASSET_SHADER || r->kind >= PLAYER_ASSET_COUNT ||
            !r->data || !r->length || r->length > PLAYER_MAX_ASSET_BYTES - total) {
            fail(error, size, "Invalid prepared renderer resource record");
            return NULL;
        }
        total += r->length;
    }
    int shaders = 0, fonts = 0;
    for (int i = 0; i < count; i++) {
        shaders += records[i].kind == PLAYER_ASSET_SHADER;
        fonts += records[i].kind == PLAYER_ASSET_FONT;
    }
    if (fonts > 128) {
        fail(error, size, "Prepared font face count exceeds host limit");
        return NULL;
    }
    size_t font_rows = fonts ? (size_t)(fonts < 16 ? 16 : fonts) * sizeof(ui_font) +
                                   sizeof(dyna_hdr)
                             : 0;
    size_t headers = font_rows + sizeof(resource_generation) +
                     2 * (render_image_table_header_bytes() +
                          render_parts_header_bytes() +
                          render_sky_table_header_bytes()) +
                     (shaders ? (size_t)(shaders < 16 ? 16 : shaders) *
                                        sizeof(shader_asset) +
                                    sizeof(dyna_hdr)
                              : 0);
    if (headers > *remaining) {
        fail(error, size, "Prepared renderer ownership budget exceeded");
        return NULL;
    }
    resource_generation *g = xcalloc(1, sizeof *g);
    if (shaders) {
        asetcap(g->assets, shaders);
    }
    if (fonts) {
        asetcap(g->fonts, fonts);
    }
    g->font_storage = font_rows;
    size_t budget = *remaining - headers;
    shader_error diagnostic = { 0 };
    g->images = render_image_table_new();
    g->parts = render_parts_new();
    g->empty_parts = render_parts_new();
    g->empty_images = render_image_table_new();
    g->meshes = render_mesh_table_new_budget(&budget);
    g->empty_meshes = render_mesh_table_new_budget(&budget);
    g->skies = render_sky_table_new();
    g->empty_skies = render_sky_table_new();
    g->documents = ui_documents_new_budget(&budget, true);
    g->empty_documents = ui_documents_new_budget(&budget, false);
    if (!g->meshes || !g->empty_meshes || !g->documents || !g->empty_documents) {
        snprintf(diagnostic.message, sizeof diagnostic.message, "Prepared resource table budget exceeded");
        goto reject;
    }
    for (int i = 0; i < count; i++) {
        const player_asset *r = &records[i];
        if (r->kind == PLAYER_ASSET_FONT) {
            ui_font font = { 0 };
            size_t before = budget;
            if (!ui_font_prepared_read_budget(r->data, r->length, &budget, &font, error, size)) {
                goto reject;
            }
            if (strcmp(r->name, font.key)) {
                ui_font_free(&font);
                snprintf(diagnostic.message, sizeof diagnostic.message, "Prepared font identity differs");
                goto reject;
            }
            g->font_storage += before - budget;
            apush(g->fonts, font);
        }
        if (r->kind == PLAYER_ASSET_UI) {
            prepared_ui_document document = { 0 };
            if (!ui_document_prepared_read_budget(r->data, r->length, &budget, &document, error, size)) {
                goto reject;
            }
            bool ok = !strcmp(r->name, document.name) &&
                      ui_documents_add_owned_budget(g->documents, &document, &budget);
            prepared_ui_document_free(&document);
            if (!ok) {
                snprintf(diagnostic.message, sizeof diagnostic.message, "Prepared UI identity is invalid");
                goto reject;
            }
        }
        if (r->kind == PLAYER_ASSET_SHADER &&
            !prepare_shader(g, r, &budget, &diagnostic)) {
            goto reject;
        }
        if (r->kind == PLAYER_ASSET_IMAGE &&
            !render_image_table_decode(g->images, r->name, r->data, r->length, &budget, &diagnostic)) {
            goto reject;
        }
        if (r->kind == PLAYER_ASSET_MESH) {
            render_mesh_asset mesh = { 0 };
            if (!render_mesh_asset_prepared_read_budget(r->data, r->length, &budget, &mesh, error, size)) {
                goto reject;
            }
            bool ok = render_mesh_table_add_owned_budget(g->meshes, r->name, &mesh, &budget);
            render_mesh_asset_free(&mesh);
            if (!ok) {
                snprintf(diagnostic.message, sizeof diagnostic.message, "Prepared mesh identity is invalid");
                goto reject;
            }
        }
        if (r->kind == PLAYER_ASSET_SKY &&
            !render_sky_table_decode(g->skies, r->name, r->data, r->length, &budget, error, size)) {
            goto reject;
        }
        if (r->kind == PLAYER_ASSET_PARTS) {
            render_parts_table *parts = render_parts_prepare(r->data, r->length, &budget, error, size);
            if (!parts) {
                goto reject;
            }
            render_parts_free(g->parts);
            g->parts = parts;
        }
    }
    for (int i = 0; i < count; i++) {
        if (records[i].kind == PLAYER_ASSET_MODEL &&
            !prepare_model(g, &records[i], &budget, &diagnostic)) {
            goto reject;
        }
    }
    if (!prepare_graphs(g, &budget, &diagnostic)) {
        goto reject;
    }
    for (int i = 0; i < count; i++) {
        const player_asset *r = &records[i];
        if (r->kind == PLAYER_ASSET_SHADER_VALUES) {
            if (!shader_values_prepare(
                    r->data,
                    r->length,
                    g->assets,
                    acount(g->assets),
                    &g->values,
                    &budget,
                    &diagnostic
                )) {
                goto reject;
            }
        }
    }
    for (int i = 0; i < ui_documents_count(g->documents); i++) {
        const prepared_ui_document *document = ui_documents_at(g->documents, i);
        for (int j = 0; j < acount(document->resources); j++) {
            const ui_resource *resource = &document->resources[j];
            bool found = false;
            for (int k = 0; k < count; k++) {
                if (!strcmp(resource->key, records[k].name) &&
                    records[k].kind == (resource->kind == UI_RESOURCE_IMAGE
                                            ? PLAYER_ASSET_IMAGE
                                            : PLAYER_ASSET_FONT)) {
                    found = true;
                }
            }
            if (!found) {
                snprintf(diagnostic.message, sizeof diagnostic.message, "Prepared UI resource dependency is missing");
                goto reject;
            }
        }
    }
    /* RmlUi retains accepted faces until shutdown. A later rejected face may
     * leave an unused registration, but resident documents never change here. */
    if (acount(g->fonts) && !rml_backend_prepare_fonts()) {
        snprintf(diagnostic.message, sizeof diagnostic.message, "Prepared font engine initialization failed");
        goto reject;
    }
    for (int i = 0; i < acount(g->fonts); i++) {
        if (!rml_backend_install_font_budget(&g->fonts[i], &budget, error, size)) {
            goto reject;
        }
    }
    for (int i = 0; i < acount(g->fonts); i++) {
        ui_font_free(&g->fonts[i]);
    }
    afree(g->fonts);
    budget += g->font_storage;
    g->font_storage = 0;
    *remaining = budget;
    if (size) {
        error[0] = 0;
    }
    return g;
reject:
    if (diagnostic.message[0]) {
        fail(error, size, diagnostic.message);
    } else if (size && !error[0]) {
        fail(error, size, "Prepared renderer resource validation failed");
    }
    discard(NULL, g);
    return NULL;
}
static int resource_order(const void *left, const void *right)
{
    const player_asset *a = *(const player_asset *const *)left;
    const player_asset *b = *(const player_asset *const *)right;
    if (a->kind != b->kind) {
        return a->kind < b->kind ? -1 : 1;
    }
    return strcmp(a->name, b->name);
}
/* External records contain typed artifacts, never authored source files. */
static void *stage(
    void *context,
    const player_asset *records,
    int count,
    size_t *remaining,
    char *error,
    size_t size
)
{
    if (size) {
        error[0] = 0;
    }
    if (!remaining || count < 0 || count > PLAYER_MAX_ASSETS || (count && !records)) {
        fail(error, size, "Invalid prepared renderer collection");
        return NULL;
    }
    player_asset *inputs = xcalloc(count ? count : 1, sizeof *inputs);
    void **owned = xcalloc(count ? count : 1, sizeof *owned);
    size_t total = 0;
    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        inputs[i] = records[i];
        const player_asset *record = &records[i];
        if (!player_asset_valid(record) ||
            record->length > PLAYER_MAX_ASSET_BYTES - total) {
            ok = fail(error, size, "Invalid prepared renderer resource record");
            break;
        }
        total += record->length;
        if (record->external) {
            const char *root = context;
            if (!root || !root[0]) {
                ok = fail(error, size, "External renderer artifact requires an explicit content root");
                break;
            }
            size_t length = strlen(root) + strlen(record->path) + 2;
            char *path = xmalloc(length);
            snprintf(path, length, "%s/%s", root, record->path);
            FILE *file = fopen(path, "rb");
            free(path);
            if (!file) {
                ok = fail(error, size, "External renderer artifact cannot be opened");
                break;
            }
            unsigned char *bytes = xmalloc(record->length);
            size_t read = fread(bytes, 1, record->length, file);
            int trailing = fgetc(file);
            bool valid = read == record->length && trailing == EOF && !ferror(file);
            if (fclose(file)) {
                valid = false;
            }
            if (!valid) {
                free(bytes);
                ok = fail(error, size, "External renderer artifact byte length differs");
                break;
            }
            owned[i] = bytes;
            inputs[i].data = bytes;
        }
        if (record->content_hash &&
            player_asset_fingerprint(inputs[i].data, record->length) !=
                record->content_hash) {
            ok = fail(error, size, "Renderer artifact fingerprint differs");
        }
    }
    if (ok && count) {
        const player_asset **ordered = xmalloc((size_t)count * sizeof *ordered);
        for (int i = 0; i < count; i++) {
            ordered[i] = &inputs[i];
        }
        qsort(ordered, (size_t)count, sizeof *ordered, resource_order);
        for (int i = 1; i < count; i++) {
            const player_asset *a = ordered[i - 1], *b = ordered[i];
            if (a->kind == b->kind && (a->kind == PLAYER_ASSET_PARTS ||
                                       a->kind == PLAYER_ASSET_SHADER_VALUES ||
                                       !strcmp(a->name, b->name))) {
                ok = fail(error, size, "Prepared renderer resource identities are duplicated");
                break;
            }
        }
        free(ordered);
    }
    void *generation = ok ? stage_decoded(inputs, count, remaining, error, size) : NULL;
    for (int i = 0; i < count; i++) {
        free(owned[i]);
    }
    free(owned);
    free(inputs);
    return generation;
}
static bool ui_image(void *, const char *key, const unsigned char **pixels, int *width, int *height)
{
    shader_image_source source = { .key = (char *)key,
                                   .color_space = SHADER_IMAGE_SRGB,
                                   .filter = SHADER_IMAGE_BILINEAR,
                                   .wrap = SHADER_IMAGE_CLAMP };
    shader_image_view view;
    if (!render_image_resolve(NULL, &source, &view)) {
        return false;
    }
    *pixels = view.pixels;
    *width = view.width;
    *height = view.height;
    return true;
}
static bool named(const char *name)
{
    return name && *name;
}
static bool prepared_scene_ui(const resource_generation *g, const char *name)
{
    return !named(name) || ui_documents_find(g->documents, name);
}
static bool component_schema(const ecs_component_desc *actual, const ecs_component_desc *expected)
{
    return actual->element_size == expected->element_size && actual->type &&
           actual->type->name && !strcmp(actual->type->name, expected->type->name) &&
           ecs_layout_hash(actual->type) == ecs_layout_hash(expected->type);
}
static bool validate_scene(void *, void *generation, const scene *value, char *error, size_t size)
{
    const resource_generation *g = generation;
    assert(g && !g->installed && value);
    if (!prepared_scene_ui(g, scene_ui(value))) {
        return fail(error, size, "Scene UI document is absent from prepared resources");
    }
    for (int object = 0; object < scene_object_count(value); object++) {
        for (int index = 0; index < scene_object_component_count(value, object);
             index++) {
            scene_component c = scene_object_component_at(value, object, index);
            const ecs_component_desc *expected = NULL;
            if (c.desc->id == COMPONENT_Shader.id) {
                expected = &COMPONENT_Shader;
            }
            if (c.desc->id == COMPONENT_MeshAsset.id) {
                expected = &COMPONENT_MeshAsset;
            }
            if (c.desc->id == COMPONENT_MeshParts.id) {
                expected = &COMPONENT_MeshParts;
            }
            if (c.desc->id == COMPONENT_Ui.id) {
                expected = &COMPONENT_Ui;
            }
            if (c.desc->id == COMPONENT_Environment.id) {
                expected = &COMPONENT_Environment;
            }
            if (!expected) {
                continue;
            }
            if (!component_schema(c.desc, expected)) {
                return fail(error, size, "Prepared resource component schema differs");
            }
            if (expected == &COMPONENT_Ui) {
                if (!prepared_scene_ui(g, ((const Ui *)c.data)->document)) {
                    return fail(error, size, "Component UI document is absent from prepared resources");
                }
            }
            if (expected == &COMPONENT_Shader) {
                const Shader *binding = c.data;
                int shape = named(binding->shape) ? asset_index(g, binding->shape)
                                                  : -1;
                int material = named(binding->material)
                                 ? asset_index(g, binding->material)
                                 : -1;
                if ((named(binding->shape) && (shape < 0 || !g->assets[shape].shape)) ||
                    (named(binding->material) &&
                     (material < 0 || g->assets[material].shape))) {
                    return fail(error, size, "Component shader identity or role is absent from prepared resources");
                }
                if (named(binding->parameters)) {
                    bool found = false;
                    for (int i = 0; i < acount(g->values); i++) {
                        const shader_values *v = &g->values[i];
                        if (!v->plain && !v->scoped && v->shape == shape + 1 &&
                            v->material == material + 1 &&
                            !strcmp(v->key, binding->parameters)) {
                            found = true;
                        }
                    }
                    if (!found) {
                        return fail(error, size, "Component shader parameters are absent from prepared resources");
                    }
                }
            }
            if (expected == &COMPONENT_MeshAsset) {
                const char *key = ((const MeshAsset *)c.data)->source;
                bool found = !named(key);
                for (int i = 0; i < render_mesh_table_source_count(g->meshes); i++) {
                    if (named(key) &&
                        !strcmp(key, render_mesh_table_source_at(g->meshes, i))) {
                        found = true;
                    }
                }
                if (!found) {
                    return fail(error, size, "Component mesh is absent from prepared resources");
                }
            }
            if (expected == &COMPONENT_Environment) {
                const char *key = ((const Environment *)c.data)->sky;
                if (named(key) && !render_sky_table_contains(g->skies, key)) {
                    return fail(error, size, "Environment sky is absent from prepared resources");
                }
            }
            if (expected == &COMPONENT_MeshParts) {
                const char *key = ((const MeshParts *)c.data)->parts;
                bool found = !named(key);
                for (int i = 0; i < render_parts_count(g->parts); i++) {
                    if (named(key) && !strcmp(key, render_parts_key(g->parts, i))) {
                        found = true;
                    }
                }
                if (!found) {
                    return fail(error, size, "Component mesh parts are absent from prepared resources");
                }
            }
        }
    }
    return true;
}
static void exchange(resource_generation *g)
{
    shader_asset *old = shader_catalog_assets;
    shader_catalog_assets = g->assets;
    g->assets = old;
    shader_values_exchange(&g->values);
    render_parts_exchange(g->parts);
    render_image_table_exchange(g->images);
    render_mesh_table_exchange(g->meshes);
    render_sky_table_exchange(g->skies);
    ui_documents_exchange(g->documents);
}
static void commit(void *, void *generation, world *)
{
    resource_generation *g = generation;
    assert(g && !g->installed);
    exchange(g);
    if (current) {
        assert(current->installed && !current->assets && !current->values);
        current->assets = g->assets;
        g->assets = NULL;
        current->values = g->values;
        g->values = NULL;
        render_parts_table *parts = current->parts;
        current->parts = g->parts;
        g->parts = parts;
        render_image_table *images = current->images;
        current->images = g->images;
        g->images = images;
        render_mesh_table *meshes = current->meshes;
        current->meshes = g->meshes;
        g->meshes = meshes;
        render_sky_table *skies = current->skies;
        current->skies = g->skies;
        g->skies = skies;
        ui_documents *documents = current->documents;
        current->documents = g->documents;
        g->documents = documents;
        current->installed = false;
    } else {
        free_assets(g->assets);
        g->assets = NULL;
        shader_values_release(g->values);
        g->values = NULL;
        render_parts_free(g->parts);
        g->parts = g->empty_parts;
        g->empty_parts = NULL;
        render_image_table_free(g->images);
        g->images = g->empty_images;
        g->empty_images = NULL;
        render_mesh_table_free(g->meshes);
        g->meshes = g->empty_meshes;
        g->empty_meshes = NULL;
        render_sky_table_free(g->skies);
        g->skies = g->empty_skies;
        g->empty_skies = NULL;
        ui_documents_free(g->documents);
        g->documents = g->empty_documents;
        g->empty_documents = NULL;
    }
    shader_catalog_publication_exchange(current, g);
    g->installed = true;
    current = g;
    rml_backend_set_image_provider(ui_image, NULL);
    shader_catalog_resources_changed();
}
static void retire(void *context, void *generation)
{
    resource_generation *g = generation;
    if (g->installed) {
        assert(current == g);
        exchange(g);
        shader_catalog_publication_exchange(g, NULL);
        g->installed = false;
        current = NULL;
        shader_catalog_resources_changed();
    }
    discard(context, g);
}
player_resource_host render_prepared_resources(const char *root)
{
    return (player_resource_host){ .context = (void *)root,
                                   .stage = stage,
                                   .scene = validate_scene,
                                   .commit = commit,
                                   .discard = discard,
                                   .retire = retire };
}
