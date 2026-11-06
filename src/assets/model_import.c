#include "assets/model_import.h"

#include "assets/new_asset.h"
#include "assets/prototype.h"
#include "assets/standard_material.h"
#include "authoring/image_prepare.h"
#include "authoring/source_asset.h"
#include "graph/document/serialize.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/geometry/mesh_prepared.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "types/text.h"
#include "yyjson.h"

/* cute_model.h refuses to compile without ckit.h, though only its
 * implementation, which Cute's library builds, uses ckit. ckit's dynamic
 * array macros share support/array.h's names, so its guard stands in. */
#define CKIT_H
#include <cute/cute_model.h>
#undef CKIT_H

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
    GLB_MAGIC = 0x46546c67, /* "glTF" */
    GLB_JSON = 0x4e4f534a,  /* "JSON" */
    GLB_HEADER = 20,        /* the file header and the JSON chunk's */
    GL_NEAREST = 9728,
    GL_NEAREST_MIPMAP_NEAREST = 9984,
    GL_NEAREST_MIPMAP_LINEAR = 9986,
    GL_CLAMP_TO_EDGE = 33071,
    GL_REPEAT = 10497,
};

/* Extensions an import applies, so a model may require them. */
static const char *const APPLIED[] = { "KHR_materials_unlit",
                                       "KHR_materials_emissive_strength",
                                       "KHR_texture_transform" };

/* The glTF texture each Standard slot reads, as its key in a material. */
static const char *const TEXTURE_KEYS[STANDARD_TEXTURE_COUNT] = {
    [STANDARD_TEXTURE_BASE_COLOR] = "baseColorTexture",
    [STANDARD_TEXTURE_METALLIC_ROUGHNESS] = "metallicRoughnessTexture",
    [STANDARD_TEXTURE_NORMAL] = "normalTexture",
    [STANDARD_TEXTURE_OCCLUSION] = "occlusionTexture",
    [STANDARD_TEXTURE_EMISSION] = "emissiveTexture",
};

/* One import: what it read, and what it named and wrote so far. */
typedef struct
{
    const char *content, *name;
    CM_Model *model;
    yyjson_val *root;
    model_import_report *report;
    /* Each glTF material's asset name, and its group's parameter scope, with
     * the default material's last; NULL for one no drawn triangle uses. */
    char **materials, **scopes;
    char **images; /* each glTF image's content key, NULL for one no material uses */
    char **taken;   /* every name chosen so far */
    char **created; /* paths made, files and folders, to remove on failure */
    char *error;
    size_t size;
} import;

static bool fail(import *in, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(in->error, in->size, format, arguments);
    va_end(arguments);
    return false;
}

static void warn(import *in, const char *format, ...)
{
    char text[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    apush(in->report->warnings, xstrdup(text));
}

/* The whole file at `path` when it holds at most `limit` bytes, or NULL.
 * `length` is its size, read or not, or 0 when it cannot be measured. */
static void *read_whole(const char *path, size_t limit, size_t *length)
{
    *length = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    long end = fseek(file, 0, SEEK_END) ? -1 : ftell(file);
    *length = end > 0 ? (size_t)end : 0;
    void *bytes = NULL;
    if (end > 0 && (size_t)end <= limit && !fseek(file, 0, SEEK_SET)) {
        bytes = xmalloc((size_t)end);
        if (fread(bytes, 1, (size_t)end, file) != (size_t)end) {
            free(bytes);
            bytes = NULL;
        }
    }
    fclose(file);
    return bytes;
}

/* The files a .gltf names beside it, which read_external reads. */
typedef struct
{
    const char *folder; /* the model's real folder, which they stay inside */
    size_t budget;      /* the bytes left for them under the import's limit */
    bool over;          /* whether they would have taken more */
} sidecars;

/* Whether the real path `path` lies inside the real folder `folder`. */
static bool within(const char *folder, const char *path)
{
    size_t length = strlen(folder);
    return !strncmp(folder, path, length) &&
           (path[length] == '/' || folder[length - 1] == '/');
}

/* A .gltf's buffer or image file, which `uri` names relative to the model's
 * real folder. A model never reaches a file outside that folder: not by
 * absolute path or scheme, nor through .. or a link. Its files together stay
 * within the budget `beside` holds. */
static void *read_external(const char *uri, int *size, void *beside)
{
    sidecars *files = beside;
    *size = 0;
    if (uri[0] == '/' || uri[0] == '\\' || strchr(uri, ':')) {
        return NULL;
    }
    size_t bytes = strlen(files->folder) + strlen(uri) + 2;
    char *path = xmalloc(bytes);
    snprintf(path, bytes, "%s/%s", files->folder, uri);
    char *real = realpath(path, NULL);
    free(path);
    size_t length = 0;
    void *data = real && within(files->folder, real)
                   ? read_whole(real, files->budget, &length)
                   : NULL;
    free(real);
    files->over |= !data && length > files->budget;
    if (data) {
        files->budget -= length;
        *size = (int)length;
    }
    return data;
}

static void free_external(void *data, void *)
{
    free(data);
}

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

/* The glTF JSON of a .gltf file's bytes, or of a .glb's first chunk. */
static yyjson_doc *read_json(const unsigned char *bytes, size_t length)
{
    if (length < 4 || read_u32(bytes) != GLB_MAGIC) {
        return yyjson_read((const char *)bytes, length, 0);
    }
    if (length < GLB_HEADER || read_u32(bytes + 16) != GLB_JSON ||
        read_u32(bytes + 12) > length - GLB_HEADER) {
        return NULL;
    }
    return yyjson_read((const char *)bytes + GLB_HEADER, read_u32(bytes + 12), 0);
}

static yyjson_val *item(yyjson_val *array, int index)
{
    return index < 0 ? NULL : yyjson_arr_get(array, (size_t)index);
}

static yyjson_val *json_material(const import *in, int material)
{
    return item(yyjson_obj_get(in->root, "materials"), material);
}

static yyjson_val *json_primitive(const import *in, int mesh, int primitive)
{
    yyjson_val *meshes = yyjson_obj_get(in->root, "meshes");
    return item(yyjson_obj_get(item(meshes, mesh), "primitives"), primitive);
}

static bool has_attribute(yyjson_val *primitive, const char *name)
{
    return yyjson_obj_get(yyjson_obj_get(primitive, "attributes"), name) != NULL;
}

static bool applied(const char *extension)
{
    for (size_t i = 0; i < sizeof APPLIED / sizeof *APPLIED; i++) {
        if (!strcmp(APPLIED[i], extension)) {
            return true;
        }
    }
    return false;
}

/* Refuses a model that requires an extension the import doesn't apply, and
 * warns of the ones it uses without requiring. Lights get their own warning. */
static bool check_extensions(import *in)
{
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(yyjson_obj_get(in->root, "extensionsRequired"), i, count, value)
    {
        const char *extension = yyjson_get_str(value);
        if (extension && !applied(extension)) {
            return fail(in, "The model requires %s, which import does not support", extension);
        }
    }
    yyjson_arr_foreach(yyjson_obj_get(in->root, "extensionsUsed"), i, count, value)
    {
        const char *extension = yyjson_get_str(value);
        if (extension && !applied(extension) &&
            strcmp(extension, "KHR_lights_punctual")) {
            warn(in, "Ignored the %s extension.", extension);
        }
    }
    return true;
}

static int primitive_material(const CM_Model *model, const CM_Primitive *primitive)
{
    return primitive->material >= 0 && primitive->material < model->material_count
             ? primitive->material
             : -1;
}

/* Whether `name` matches one taken so far; names differing only in case
 * would share a file on case-insensitive file systems. */
static bool taken(const import *in, const char *name)
{
    for (int i = 0; i < acount(in->taken); i++) {
        if (!strcasecmp(in->taken[i], name)) {
            return true;
        }
    }
    return false;
}

/* Bytes a generated name keeps after its prefix, for its own label and any
 * unique suffix. */
#define NAME_LABEL_MIN 16

/* `prefix` and then `label`, each character an asset name refuses turned to
 * '_', at most NEW_ASSET_NAME_MAX bytes, and unlike every name taken so far.
 * The prefix leaves NAME_LABEL_MIN bytes, so the name always runs past it. */
static char *unique_name(import *in, const char *prefix, const char *label)
{
    assert(strlen(prefix) + NAME_LABEL_MIN <= NEW_ASSET_NAME_MAX + 1);
    char base[NEW_ASSET_NAME_MAX + 1];
    snprintf(base, sizeof base, "%s%s", prefix, label);
    for (char *c = base + strlen(prefix); *c; c++) {
        bool kept = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                    (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
        *c = kept ? *c : '_';
    }
    char name[NEW_ASSET_NAME_MAX + 1];
    snprintf(name, sizeof name, "%s", base);
    for (int n = 2; taken(in, name); n++) {
        char suffix[16];
        int length = snprintf(suffix, sizeof suffix, "_%d", n);
        snprintf(name, sizeof name, "%.*s%s", NEW_ASSET_NAME_MAX - length, base, suffix);
    }
    apush(in->taken, xstrdup(name));
    return xstrdup(name);
}

/* Names each glTF material a mesh node draws with, and the default
 * material when a primitive names none. */
static void name_materials(import *in)
{
    const CM_Model *model = in->model;
    in->materials = xcalloc((size_t)model->material_count + 1, sizeof *in->materials);
    in->scopes = xcalloc((size_t)model->material_count + 1, sizeof *in->scopes);
    /* As much of the model's name as leaves each material room for its own. */
    char prefix[NEW_ASSET_NAME_MAX + 1];
    snprintf(prefix, sizeof prefix, "%.*s_", NEW_ASSET_NAME_MAX - NAME_LABEL_MIN, in->name);
    for (int i = 0; i < model->node_count; i++) {
        int mesh = model->nodes[i].mesh;
        for (int j = 0; mesh >= 0 && j < model->meshes[mesh].primitive_count; j++) {
            int material = primitive_material(model, &model->meshes[mesh].primitives[j]);
            int slot = material >= 0 ? material : model->material_count;
            if (in->materials[slot]) {
                continue;
            }
            char label[32];
            const char *name = material >= 0 ? model->materials[material].name
                                             : "Default";
            if (!*name) {
                snprintf(label, sizeof label, "Material%d", material);
                name = label;
            }
            in->materials[slot] = unique_name(in, prefix, name);
            in->scopes[slot] = xstrdup(in->materials[slot] + strlen(prefix));
        }
    }
}

/* The image's name, else its file's name without folders or extension, else
 * Image<n>. */
static void image_label(const import *in, int image, char *label, size_t size)
{
    yyjson_val *json = item(yyjson_obj_get(in->root, "images"), image);
    const char *uri = yyjson_get_str(yyjson_obj_get(json, "uri"));
    snprintf(label, size, "%s", in->model->images[image].name);
    if (!*label && uri && strncmp(uri, "data:", 5)) {
        const char *slash = strrchr(uri, '/');
        const char *file = slash ? slash + 1 : uri;
        snprintf(label, size, "%.*s", (int)strcspn(file, "."), file);
    }
    if (!*label) {
        snprintf(label, size, "Image%d", image);
    }
}

/* The image's content key under textures/<model>/, after checking it decodes. */
static bool name_image(import *in, int image)
{
    if (in->images[image]) {
        return true;
    }
    const CM_Image *source = &in->model->images[image];
    if (!source->data) {
        return fail(in, "Cannot read image %d of the model", image);
    }
    char reason[160];
    const char *extension = image_prepare_check(source->data, (size_t)source->size, reason, sizeof reason);
    if (!extension) {
        return fail(in, "Image %d of the model: %s", image, reason);
    }
    char label[NEW_ASSET_NAME_MAX + 1];
    image_label(in, image, label, sizeof label);
    char *file = unique_name(in, "", label);
    size_t size = strlen(in->name) + strlen(file) + 32;
    in->images[image] = xmalloc(size);
    snprintf(in->images[image], size, "content/textures/%s/%s.%s", in->name, file, extension);
    free(file);
    return true;
}

static yyjson_val *texture_info(const import *in, int material, standard_texture_slot slot)
{
    yyjson_val *owner = json_material(in, material);
    if (slot == STANDARD_TEXTURE_BASE_COLOR ||
        slot == STANDARD_TEXTURE_METALLIC_ROUGHNESS) {
        owner = yyjson_obj_get(owner, "pbrMetallicRoughness");
    }
    return yyjson_obj_get(owner, TEXTURE_KEYS[slot]);
}

/* The sampler a material's texture uses, as an engine filter and wrap. */
static void sample(import *in, int material, standard_texture_slot slot, standard_texture *out)
{
    yyjson_val *info = texture_info(in, material, slot);
    yyjson_val *texture = item(
        yyjson_obj_get(in->root, "textures"),
        (int)yyjson_get_int(yyjson_obj_get(info, "index"))
    );
    yyjson_val *sampler = item(
        yyjson_obj_get(in->root, "samplers"),
        yyjson_obj_get(texture, "sampler")
            ? (int)yyjson_get_int(yyjson_obj_get(texture, "sampler"))
            : -1
    );
    yyjson_val *s = yyjson_obj_get(sampler, "wrapS"),
               *t = yyjson_obj_get(sampler, "wrapT");
    int wrap_s = s ? (int)yyjson_get_int(s) : GL_REPEAT,
        wrap_t = t ? (int)yyjson_get_int(t) : GL_REPEAT;
    out->filter = yyjson_get_int(yyjson_obj_get(sampler, "magFilter")) == GL_NEAREST
                    ? SHADER_IMAGE_NEAREST
                    : SHADER_IMAGE_BILINEAR;
    /* An image has one filter, so its magnification filter shrinks it too. */
    yyjson_val *min = yyjson_obj_get(sampler, "minFilter");
    int shrink = (int)yyjson_get_int(min);
    bool nearest = shrink == GL_NEAREST || shrink == GL_NEAREST_MIPMAP_NEAREST ||
                   shrink == GL_NEAREST_MIPMAP_LINEAR;
    if (min && nearest != (out->filter == SHADER_IMAGE_NEAREST)) {
        warn(
            in,
            "Material %s's %s shrinks with its magFilter, not the model's "
            "minFilter.",
            in->materials[material],
            TEXTURE_KEYS[slot]
        );
    }
    out->wrap = wrap_s == GL_CLAMP_TO_EDGE && wrap_t == GL_CLAMP_TO_EDGE
                  ? SHADER_IMAGE_CLAMP
                  : SHADER_IMAGE_REPEAT;
    if (out->wrap == SHADER_IMAGE_REPEAT &&
        (wrap_s != GL_REPEAT || wrap_t != GL_REPEAT)) {
        warn(
            in,
            "Material %s's %s repeats where the model mirrors or clamps it.",
            in->materials[material],
            TEXTURE_KEYS[slot]
        );
    }
}

/* The decoder supplies defaults, but malformed JSON fields must not quietly
 * become identity or zero. All persisted values must fit in a finite float. */
static bool texture_transform(import *in, int material, standard_texture_slot slot, standard_texture *out)
{
    yyjson_val *info = texture_info(in, material, slot);
    yyjson_val *extension = yyjson_obj_get(yyjson_obj_get(info, "extensions"), "KHR_texture_transform");
    yyjson_val *coordinate = yyjson_obj_get(extension, "texCoord");
    if (!coordinate) {
        coordinate = yyjson_obj_get(info, "texCoord");
    }
    if (coordinate && (!yyjson_is_int(coordinate) || yyjson_get_num(coordinate) < 0 ||
                       yyjson_get_num(coordinate) > INT_MAX)) {
        return fail(
            in,
            "Material %s's %s has an invalid texCoord",
            in->materials[material],
            TEXTURE_KEYS[slot]
        );
    }
    int uv_set = coordinate ? (int)yyjson_get_int(coordinate) : 0;
    if (uv_set != 0) {
        return fail(
            in,
            "Material %s's %s reads TEXCOORD_%d; import reads only TEXCOORD_0",
            in->materials[material],
            TEXTURE_KEYS[slot],
            uv_set
        );
    }
    out->scale[0] = out->scale[1] = 1;
    if (!extension) {
        return true;
    }
    const char *fields[] = { "offset", "scale", "rotation" };
    float *values[] = { out->offset, out->scale, &out->rotation };
    for (int field = 0; field < 3; field++) {
        yyjson_val *value = yyjson_obj_get(extension, fields[field]);
        int count = field == 2 ? 1 : 2;
        bool valid = yyjson_is_obj(extension) &&
                     (!value || count == 1 ||
                      (yyjson_is_arr(value) && yyjson_arr_size(value) == 2));
        for (int i = 0; valid && value && i < count; i++) {
            yyjson_val *number = count == 1 ? value : item(value, i);
            valid = yyjson_is_num(number) && isfinite((float)yyjson_get_num(number));
            if (valid) {
                values[field][i] = (float)yyjson_get_num(number);
            }
        }
        if (!valid) {
            return fail(
                in,
                "Material %s's %s has an invalid texture transform %s",
                in->materials[material],
                TEXTURE_KEYS[slot],
                fields[field]
            );
        }
    }
    return true;
}

static bool unlit(const import *in, int material)
{
    return yyjson_obj_get(yyjson_obj_get(json_material(in, material), "extensions"), "KHR_materials_unlit") !=
           NULL;
}

static const CM_TextureRef *texture_ref(const CM_Material *m, standard_texture_slot slot)
{
    switch (slot) {
        case STANDARD_TEXTURE_BASE_COLOR: return &m->base_color_texture;
        case STANDARD_TEXTURE_METALLIC_ROUGHNESS:
            return &m->metallic_roughness_texture;
        case STANDARD_TEXTURE_NORMAL: return &m->normal_texture;
        case STANDARD_TEXTURE_OCCLUSION: return &m->occlusion_texture;
        case STANDARD_TEXTURE_EMISSION: return &m->emissive_texture;
        default: assert(false); return NULL;
    }
}

/* Check actual used UVs as well as finite parameters: valid floats can
 * overflow during shader scale, rotation or translation. */
static bool part_texture_transforms(
    import *in,
    int material,
    const render_mesh_asset *part,
    standard_texture *normal
)
{
    if (material < 0 || !acount(part->vertices)) {
        return true;
    }
    for (int slot = 0; slot < STANDARD_TEXTURE_COUNT; slot++) {
        if ((unlit(in, material) && slot != STANDARD_TEXTURE_BASE_COLOR) ||
            texture_ref(&in->model->materials[material], slot)->image < 0) {
            continue;
        }
        standard_texture transform = { 0 };
        if (!texture_transform(in, material, slot, &transform)) {
            return false;
        }
        float c = cosf(transform.rotation), s = sinf(transform.rotation);
        for (int i = 0; i < acount(part->vertices); i++) {
            CF_V2 uv = part->vertices[i].uv;
            float u = uv.x * transform.scale[0], v = uv.y * transform.scale[1];
            float x = c * u - s * v, y = s * u + c * v;
            if (!isfinite(u) || !isfinite(v) || !isfinite(x) || !isfinite(y) ||
                !isfinite(x + transform.offset[0]) ||
                !isfinite(y + transform.offset[1])) {
                return fail(
                    in,
                    "Material %s's %s transforms UVs beyond float range",
                    in->materials[material],
                    TEXTURE_KEYS[slot]
                );
            }
        }
        if (slot == STANDARD_TEXTURE_NORMAL) {
            *normal = transform;
        }
    }
    return true;
}

/* The engine form of a glTF texture slot, after checking the import can
 * sample it as glTF does. */
static bool texture(
    import *in,
    int material,
    standard_texture_slot slot,
    const CM_TextureRef *ref,
    standard_texture *out
)
{
    *out = (standard_texture){ 0 };
    if (ref->image < 0) {
        return true;
    }
    const char *name = in->materials[material];
    if (ref->image >= in->model->image_count) {
        return fail(in, "Material %s's %s names a missing image", name, TEXTURE_KEYS[slot]);
    }
    if (!texture_transform(in, material, slot, out)) {
        return false;
    }
    if (!name_image(in, ref->image)) {
        return false;
    }
    out->image = in->images[ref->image];
    sample(in, material, slot, out);
    return true;
}

/* Whether every factor of `material` is finite, as its asset text needs: a
 * glTF number beyond float range reads as infinite. */
static bool finite_factors(const standard_material *material)
{
    const float *color = material->base_color, *emission = material->emission;
    const float factors[] = { color[0],
                              color[1],
                              color[2],
                              color[3],
                              material->metallic,
                              material->roughness,
                              emission[0],
                              emission[1],
                              emission[2],
                              material->normal_scale,
                              material->occlusion_strength,
                              material->alpha_cutoff };
    for (size_t i = 0; i < sizeof factors / sizeof *factors; i++) {
        if (!isfinite(factors[i])) {
            return false;
        }
    }
    return true;
}

/* The Standard form of glTF material `index`, or of the default material
 * when it is -1. */
static bool standard(import *in, int index, standard_material *out)
{
    *out = (standard_material){ .base_color = { 1, 1, 1, 1 },
                                .metallic = 1,
                                .roughness = 1,
                                .normal_scale = 1,
                                .occlusion_strength = 1,
                                .alpha_mode = SHADER_ALPHA_OPAQUE,
                                .alpha_cutoff = .5f };
    if (index < 0) {
        return true;
    }
    const CM_Material *m = &in->model->materials[index];
    yyjson_val *extensions = yyjson_obj_get(json_material(in, index), "extensions");
    yyjson_val *strength = yyjson_obj_get(
        yyjson_obj_get(extensions, "KHR_materials_emissive_strength"),
        "emissiveStrength"
    );
    float emissive = strength ? (float)yyjson_get_num(strength) : 1;
    out->unlit = unlit(in, index);
    memcpy(out->base_color, m->base_color, sizeof out->base_color);
    out->metallic = m->metallic;
    out->roughness = m->roughness;
    /* An unlit material's emission is the fallback glTF gives viewers that
     * light it, so it adds nothing here. */
    if (!out->unlit) {
        for (int i = 0; i < 3; i++) {
            out->emission[i] = m->emissive[i] * emissive;
        }
    }
    out->normal_scale = m->normal_scale;
    out->occlusion_strength = m->occlusion_strength;
    out->alpha_mode = m->alpha_mode == CM_ALPHA_BLEND ? SHADER_ALPHA_BLEND
                    : m->alpha_mode == CM_ALPHA_MASK  ? SHADER_ALPHA_MASK
                                                      : SHADER_ALPHA_OPAQUE;
    out->alpha_cutoff = m->alpha_cutoff;
    if (!finite_factors(out)) {
        return fail(in, "Material %s holds a number beyond float range", in->materials[index]);
    }
    if (m->double_sided) {
        warn(in, "Material %s draws single-sided.", in->materials[index]);
    }
    for (int slot = 0; slot < STANDARD_TEXTURE_COUNT; slot++) {
        /* Like its emission, an unlit material's other images are fallbacks. */
        bool fallback = out->unlit && slot != STANDARD_TEXTURE_BASE_COLOR;
        if (!fallback &&
            !texture(in, index, slot, texture_ref(m, slot), &out->textures[slot])) {
            return false;
        }
    }
    return true;
}

static CF_V3 v3(const float *values)
{
    return (CF_V3){ values[0], values[1], values[2] };
}

/* `v` at unit length, or `fallback` when it is zero or not finite. Its
 * length is taken in double, where no float's square overflows or
 * vanishes, so a vector a vast or tiny scale carried keeps its direction. */
static CF_V3 unit_of(const double v[3], CF_V3 fallback)
{
    double length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!isfinite(length) || length <= 0) {
        return fallback;
    }
    return (CF_V3){ (float)(v[0] / length), (float)(v[1] / length), (float)(v[2] / length) };
}

static CF_V3 unit(CF_V3 v, CF_V3 fallback)
{
    return unit_of((const double[3]){ v.x, v.y, v.z }, fallback);
}

/* The cross product of a triangle's edges, along its normal and twice its
 * area long, taken in double, where no float's product overflows. */
static void face_cross(CF_V3 a, CF_V3 b, CF_V3 c, double out[3])
{
    double x1 = (double)b.x - a.x, y1 = (double)b.y - a.y, z1 = (double)b.z - a.z;
    double x2 = (double)c.x - a.x, y2 = (double)c.y - a.y, z2 = (double)c.z - a.z;
    out[0] = y1 * z2 - z1 * y2;
    out[1] = z1 * x2 - x1 * z2;
    out[2] = x1 * y2 - y1 * x2;
}

/* Whether the primitive's indices make whole triangles of its vertices. */
static bool whole_triangles(import *in, const CM_Primitive *p)
{
    if (p->index_count % 3) {
        return fail(in, "A primitive's %d indices are not whole triangles", p->index_count);
    }
    for (int i = 0; i < p->index_count; i++) {
        if (p->indices[i] >= (uint32_t)p->vertex_count) {
            return fail(in, "A primitive's index %d names a missing vertex", i);
        }
    }
    return true;
}

/* What a node draws of a primitive: the triangles with area in its frame,
 * and the vertices they use. */
typedef struct
{
    bool flat;      /* without normals, so each corner is its own vertex */
    int *triangles; /* each kept triangle's first index */
    int *keys;      /* each corner used when flat, else vertex, in order */
    int *place;     /* by key: its index in keys, or -1 when unused */
} kept_part;

/* The key of the vertex at the primitive's index `corner`. */
static int key_of(const CM_Primitive *p, const kept_part *k, int corner)
{
    return k->flat ? corner : (int)p->indices[corner];
}

/* The kept part of `p` in its node's frame, with Chibi's tangent frame,
 * whose bitangent points toward +v, down the image: generated, or each
 * authored tangent with its w negated, since glTF's bitangent points up the
 * image. Without normals, each triangle's corners get its own flat normal, as
 * glTF requires. */
static bool primitive_part(
    import *in,
    const CM_Primitive *p,
    yyjson_val *json,
    const kept_part *k,
    render_mesh_asset *part
)
{
    for (int i = 0; i < acount(k->keys); i++) {
        int key = k->keys[i], source = k->flat ? (int)p->indices[key] : key;
        CF_V3 up = { 0, 1, 0 }, normal;
        if (k->flat) {
            const uint32_t *corner = p->indices + key / 3 * 3;
            double cross[3];
            face_cross(
                v3(p->positions + corner[0] * 3),
                v3(p->positions + corner[1] * 3),
                v3(p->positions + corner[2] * 3),
                cross
            );
            normal = unit_of(cross, up);
        } else {
            normal = unit(v3(p->normals + source * 3), up);
        }
        render_mesh_surface_vertex vertex = {
            .position = v3(p->positions + source * 3),
            .normal = normal,
            .uv = p->uvs ? (CF_V2){ p->uvs[source * 2], p->uvs[source * 2 + 1] }
                         : (CF_V2){ 0 },
        };
        apush(part->vertices, vertex);
    }
    for (int t = 0; t < acount(k->triangles); t++) {
        for (int j = 0; j < 3; j++) {
            apush(part->indices, k->place[key_of(p, k, k->triangles[t] + j)]);
        }
    }
    /* glTF ignores the tangents of a primitive without normals. */
    bool authored = !k->flat && has_attribute(json, "TANGENT") && p->tangents;
    int material = primitive_material(in->model, p);
    standard_texture transform = { .scale = { 1, 1 } };
    if (!part_texture_transforms(in, material, part, &transform)) {
        return false;
    }
    bool mapped = !authored && material >= 0 && !unlit(in, material) &&
                  in->model->materials[material].normal_texture.image >= 0;
    if (mapped) {
        float c = cosf(transform.rotation), s = sinf(transform.rotation);
        for (int i = 0; i < acount(part->vertices); i++) {
            CF_V2 uv = part->vertices[i].uv;
            float u = uv.x * transform.scale[0], v = uv.y * transform.scale[1];
            /* Translation cannot change the tangent frame; omitting it avoids
             * cancellation when a large offset swallows small UV differences. */
            CF_V2 turned = { c * u - s * v, s * u + c * v };
            part->vertices[i].uv = turned;
        }
    }
    render_mesh_asset_tangents(part);
    for (int i = 0; i < acount(part->vertices); i++) {
        render_mesh_surface_vertex *vertex = &part->vertices[i];
        int key = k->keys[i], source = k->flat ? (int)p->indices[key] : key;
        vertex->uv = p->uvs ? (CF_V2){ p->uvs[source * 2], p->uvs[source * 2 + 1] }
                            : (CF_V2){ 0 };
        const float *t = authored ? p->tangents + k->keys[i] * 4 : NULL;
        CF_V3 direction = t ? unit(v3(t), (CF_V3){ 0 }) : (CF_V3){ 0 };
        if (cf_len_v3(direction) > 0) {
            vertex->tangent = (CF_V4){ direction.x,
                                       direction.y,
                                       direction.z,
                                       t[3] < 0 ? 1.0f : -1.0f };
        }
    }
    return true;
}

static bool finite_frame(CF_M4x4 m)
{
    for (int i = 0; i < 16; i++) {
        if (!isfinite(m.elements[i])) {
            return false;
        }
    }
    return true;
}

/* The cofactors of `m`'s linear part, as columns: each the cross product of
 * the other two axes. They carry normals as the inverse transpose does,
 * scaled by the determinant, and stay defined where it is zero. */
static void cofactors(CF_M4x4 m, double out[3][3])
{
    const float *e = m.elements;
    for (int i = 0; i < 3; i++) {
        const float *u = e + (i + 1) % 3 * 4, *v = e + (i + 2) % 3 * 4;
        out[i][0] = (double)u[1] * v[2] - (double)u[2] * v[1];
        out[i][1] = (double)u[2] * v[0] - (double)u[0] * v[2];
        out[i][2] = (double)u[0] * v[1] - (double)u[1] * v[0];
    }
}

/* Whether a frame collapses every surface onto a line or a point, as a
 * zero scale on two axes does: no two of its axes span any area. */
static bool collapses(CF_M4x4 m)
{
    double cofactor[3][3];
    cofactors(m, cofactor);
    for (int i = 0; i < 9; i++) {
        if (cofactor[i / 3][i % 3]) {
            return false;
        }
    }
    return true;
}

/* `normal` carried by the cofactors, at unit length and flipped where the
 * frame mirrors, as winding is. Under one zero scale it turns to the normal
 * of the plane the surface flattens onto, on its own side. */
static CF_V3 carry_normal(const double cofactor[3][3], CF_V3 normal, bool mirrored)
{
    const double n[3] = { normal.x, normal.y, normal.z };
    double v[3];
    for (int k = 0; k < 3; k++) {
        v[k] = cofactor[0][k] * n[0] + cofactor[1][k] * n[1] + cofactor[2][k] * n[2];
    }
    double length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!isfinite(length) || length <= 0) {
        return (CF_V3){ 0, 1, 0 };
    }
    double scale = mirrored ? -1 / length : 1 / length;
    return (CF_V3){ (float)(v[0] * scale), (float)(v[1] * scale), (float)(v[2] * scale) };
}

/* `direction` carried by `m`'s linear part in double, where no sum of two
 * float products overflows, at unit length or else `fallback`. */
static CF_V3 carry_direction(CF_M4x4 m, CF_V3 direction, CF_V3 fallback)
{
    const float *e = m.elements;
    double v[3];
    for (int k = 0; k < 3; k++) {
        v[k] = (double)e[k] * direction.x + (double)e[4 + k] * direction.y +
               (double)e[8 + k] * direction.z;
    }
    return unit_of(v, fallback);
}

/* Whether the triangle has the area a mesh file requires. */
static bool has_area(CF_V3 a, CF_V3 b, CF_V3 c)
{
    double n[3];
    face_cross(a, b, c, n);
    double area = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    return isfinite(area) && area > RENDER_MESH_MIN_TRIANGLE_AREA;
}

/* Fills `k` with the triangles of `p` that have area in the frame `world`
 * and the vertices they use, and returns how many triangles have none
 * there. A vertex only those use, or that no index names, is left out. */
static int keep(const CM_Primitive *p, yyjson_val *json, CF_M4x4 world, kept_part *k)
{
    k->flat = !has_attribute(json, "NORMAL");
    int dropped = 0;
    for (int i = 0; i < p->index_count; i += 3) {
        CF_V3 at[3];
        for (int j = 0; j < 3; j++) {
            at[j] = cf_m4_transform_point(world, v3(p->positions + p->indices[i + j] * 3));
        }
        if (!has_area(at[0], at[1], at[2])) {
            dropped++;
            continue;
        }
        apush(k->triangles, i);
    }
    int count = k->flat ? p->index_count : p->vertex_count;
    k->place = xmalloc((size_t)count * sizeof *k->place);
    for (int i = 0; i < count; i++) {
        k->place[i] = -1;
    }
    for (int t = 0; t < acount(k->triangles); t++) {
        for (int j = 0; j < 3; j++) {
            k->place[key_of(p, k, k->triangles[t] + j)] = 0;
        }
    }
    for (int i = 0; i < count; i++) {
        if (k->place[i] < 0) {
            continue;
        }
        k->place[i] = acount(k->keys);
        apush(k->keys, i);
    }
    return dropped;
}

static void kept_part_free(kept_part *k)
{
    afree(k->triangles);
    afree(k->keys);
    free(k->place);
}

/* Appends `part` to `mesh` in the world frame `world`, turning tangent
 * handedness and winding where it is `mirrored`. */
static void bake(render_mesh_asset *mesh, const render_mesh_asset *part, CF_M4x4 world, bool mirrored)
{
    double cofactor[3][3];
    cofactors(world, cofactor);
    int first = acount(mesh->vertices);
    for (int i = 0; i < acount(part->vertices); i++) {
        render_mesh_surface_vertex vertex = part->vertices[i];
        CF_V4 t = vertex.tangent;
        vertex.position = cf_m4_transform_point(world, vertex.position);
        vertex.normal = carry_normal(cofactor, vertex.normal, mirrored);
        CF_V3 tangent = carry_direction(world, (CF_V3){ t.x, t.y, t.z }, (CF_V3){ 1, 0, 0 });
        vertex.tangent = (CF_V4){ tangent.x, tangent.y, tangent.z, mirrored ? -t.w : t.w };
        apush(mesh->vertices, vertex);
    }
    for (int i = 0; i < acount(part->indices); i += 3) {
        apush(mesh->indices, first + part->indices[i]);
        apush(mesh->indices, first + part->indices[i + (mirrored ? 2 : 1)]);
        apush(mesh->indices, first + part->indices[i + (mirrored ? 1 : 2)]);
    }
}

/* Whether `mesh` still fits a .mesh file with `k` added, checked before the
 * part is built, so that a vast primitive, or nodes copying one many times,
 * stop before they outgrow memory. */
static bool fits(import *in, const render_mesh_asset *mesh, const kept_part *k)
{
    return render_mesh_prepared_fits(
               acount(mesh->vertices) + acount(k->keys),
               acount(mesh->indices) + 3 * acount(k->triangles)
           ) ||
           fail(in, "The model's meshes, copied for each node that draws them, are larger than a .mesh file holds");
}

/* Each node's world frame, parents first. */
static CF_M4x4 *world_frames(const CM_Model *model)
{
    CF_M4x4 *frames = xmalloc(
        (size_t)(model->node_count ? model->node_count : 1) * sizeof *frames
    );
    for (int i = 0; i < model->node_count; i++) {
        int n = model->order[i];
        const CM_Node *node = &model->nodes[n];
        const CM_Transform *rest = &node->rest;
        CF_Quat rotation = { rest->rotation[0],
                             rest->rotation[1],
                             rest->rotation[2],
                             rest->rotation[3] };
        float length = sqrtf(
            rotation.x * rotation.x + rotation.y * rotation.y +
            rotation.z * rotation.z + rotation.w * rotation.w
        );
        rotation = length > 0 ? (CF_Quat){ rotation.x / length,
                                           rotation.y / length,
                                           rotation.z / length,
                                           rotation.w / length }
                              : (CF_Quat){ 0, 0, 0, 1 };
        CF_M4x4 local = cf_m4_from_trs(v3(rest->translation), rotation, v3(rest->scale));
        assert(node->parent < model->node_count);
        frames[n] = node->parent >= 0 ? cf_mul_m4(frames[node->parent], local) : local;
    }
    return frames;
}

/* Whether node `n`'s frame mirrors, by the signs of its scales and its
 * ancestors'; a decomposed matrix carries its mirroring in them too. Unlike
 * the determinant's sign, it holds where a zero scale flattens the frame,
 * counting that scale as positive, the limit of a shrinking one, so the
 * surface keeps the side its normals face. */
static bool mirrors(const CM_Model *model, int n)
{
    bool mirrored = false;
    for (int at = n; at >= 0; at = model->nodes[at].parent) {
        const float *scale = model->nodes[at].rest.scale;
        for (int i = 0; i < 3; i++) {
            mirrored ^= scale[i] < 0;
        }
    }
    return mirrored;
}

/* Every mesh node's triangles in the world frame, one group per material in
 * glTF order with the default material's last. */
static bool bake_mesh(import *in, render_mesh_asset *mesh)
{
    const CM_Model *model = in->model;
    CF_M4x4 *frames = world_frames(model);
    int dropped = 0;
    bool colors = false, morphs = false;
    for (int slot = 0; slot <= model->material_count; slot++) {
        int first = acount(mesh->indices);
        for (int i = 0; in->materials[slot] && i < model->node_count; i++) {
            int n = model->order[i], index = model->nodes[n].mesh;
            /* A node whose scale collapses its surfaces, or overflows
             * float, hides them; one that flattens them onto a plane keeps
             * what has area there. */
            if (index < 0 || !finite_frame(frames[n]) || collapses(frames[n])) {
                continue;
            }
            const CM_Mesh *source = &model->meshes[index];
            for (int j = 0; j < source->primitive_count; j++) {
                const CM_Primitive *p = &source->primitives[j];
                int material = primitive_material(model, p);
                if ((material >= 0 ? material : model->material_count) != slot) {
                    continue;
                }
                yyjson_val *json = json_primitive(in, index, j);
                kept_part kept = { 0 };
                bool baked = whole_triangles(in, p);
                if (baked) {
                    dropped += keep(p, json, frames[n], &kept);
                    baked = fits(in, mesh, &kept);
                }
                if (baked) {
                    render_mesh_asset part = { 0 };
                    baked = primitive_part(in, p, json, &kept, &part);
                    if (baked) {
                        bake(mesh, &part, frames[n], mirrors(model, n));
                    }
                    render_mesh_asset_free(&part);
                }
                kept_part_free(&kept);
                if (!baked) {
                    free(frames);
                    return false;
                }
                colors = colors || p->colors;
                morphs = morphs || p->target_count;
            }
        }
        int count = acount(mesh->indices) - first;
        if (!count) {
            /* A material that only hidden nodes or triangles with no area
             * draw with is neither checked nor written. */
            free(in->materials[slot]);
            free(in->scopes[slot]);
            in->materials[slot] = in->scopes[slot] = NULL;
            continue;
        }
        render_mesh_group group = { .first = first,
                                    .count = count,
                                    .part = -1,
                                    .asset = xstrdup(in->name),
                                    .name = xstrdup(in->scopes[slot]),
                                    .path = xstrdup(""),
                                    .material = xstrdup(in->materials[slot]),
                                    .material_scope = xstrdup(in->scopes[slot]) };
        apush(mesh->groups, group);
    }
    free(frames);
    if (colors) {
        warn(in, "Ignored vertex colors.");
    }
    if (morphs) {
        warn(in, "Ignored morph targets.");
    }
    if (dropped) {
        warn(in, "Left out %d triangle%s with no area.", dropped, dropped == 1 ? "" : "s");
    }
    if (!acount(mesh->indices)) {
        return fail(in, "The model has no triangles to import");
    }
    mesh->min = mesh->max = mesh->vertices[0].position;
    for (int i = 1; i < acount(mesh->vertices); i++) {
        mesh->min = cf_min_v3(mesh->min, mesh->vertices[i].position);
        mesh->max = cf_max_v3(mesh->max, mesh->vertices[i].position);
    }
    return true;
}

/* Creates the folders of `path` below the content folder, remembering the
 * ones it made. Each must be a folder, not a link that would carry the file
 * out of the project; "x" already refuses a link in the file's place. */
static bool make_folders(import *in, char *path)
{
    for (char *slash = strchr(path + strlen(in->content) + 1, '/'); slash;
         slash = strchr(slash + 1, '/')) {
        *slash = 0;
        bool made = !mkdir(path, 0777);
        int code = errno;
        if (made) {
            apush(in->created, xstrdup(path));
        }
        if (!made && code != EEXIST) {
            *slash = '/';
            return fail(in, "Cannot create the folders of %s: %s", path, strerror(code));
        }
        bool real = new_asset_real_folder(path, in->error, in->size);
        *slash = '/';
        if (!real) {
            return false;
        }
    }
    return true;
}

/* Writes `length` bytes as the new content file `key`, never replacing one. */
static bool write_new(import *in, const char *key, const void *bytes, size_t length)
{
    assert(!strncmp(key, "content/", 8));
    size_t size = strlen(in->content) + strlen(key);
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", in->content, key + 8);
    if (!make_folders(in, path)) {
        free(path);
        return false;
    }
    /* "x" refuses a file that is already there, so nothing is overwritten. */
    FILE *file = fopen(path, "wbx");
    if (!file) {
        int code = errno;
        free(path);
        return code == EEXIST
                 ? fail(in, "%s already exists", key)
                 : fail(in, "Cannot create %s: %s", key, strerror(code));
    }
    apush(in->created, path);
    bool written = fwrite(bytes, 1, length, file) == length;
    written = fclose(file) == 0 && written;
    apush(in->report->written, xstrdup(key));
    return written || fail(in, "Cannot write all of %s", key);
}

static bool write_text(import *in, const char *key, char *text)
{
    bool written = write_new(in, key, text, strlen(text));
    free(text);
    return written;
}

/* Writes the mesh after reading its bytes back as the engine loads them. */
static bool write_mesh(import *in, const render_mesh_asset *mesh, const char *key)
{
    unsigned char *bytes = NULL;
    size_t length = 0, budget = SIZE_MAX;
    render_mesh_asset check = { 0 };
    char reason[256] = "";
    bool valid = render_mesh_asset_prepared_write(mesh, &bytes, &length, reason, sizeof reason) &&
                 render_mesh_asset_prepared_read_budget(bytes, length, &budget, &check, reason, sizeof reason);
    render_mesh_asset_free(&check);
    valid = valid ? write_new(in, key, bytes, length)
                  : fail(in, "The baked mesh is invalid: %s", reason);
    free(bytes);
    return valid;
}

/* A prototype that draws the mesh at `source` in its own units. */
static char *prototype_text(const char *name, const char *source)
{
    prototype *prototype = prototype_new(name);
    const ecs_component_desc *components[] = { &COMPONENT_Transform3D,
                                               &COMPONENT_Geometry3D,
                                               &COMPONENT_MeshAsset };
    for (size_t i = 0; i < sizeof components / sizeof *components; i++) {
        int added = prototype_add_component(prototype, components[i]);
        assert(added == 0);
        (void)added;
    }
    CF_V3 scale = { 1, 1, 1 };
    CF_Quat rotation = { 0, 0, 0, 1 };
    const struct
    {
        const char *component, *field;
        pin_value value;
    } fields[] = {
        { "Transform3D", "rotation", { .type = PIN_STRUCT, .ptr = &rotation } },
        { "Transform3D", "scale", { .type = PIN_STRUCT, .ptr = &scale } },
        { "Geometry3D", "width", { .type = PIN_F32, .f = 1 } },
        { "Geometry3D", "height", { .type = PIN_F32, .f = 1 } },
        { "Geometry3D", "depth", { .type = PIN_F32, .f = 1 } },
        { "MeshAsset",
          "source",
          { .type = PIN_STR,
            .text = &(pin_string){ .bytes = source, .length = (uint32_t)strlen(source) } } },
    };
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++) {
        int written = prototype_write_field(
            prototype,
            fields[i].component,
            fields[i].field,
            fields[i].value
        );
        assert(written == 0);
        (void)written;
    }
    char *text = prototype_save(prototype);
    prototype_free(prototype);
    return text;
}

/* The Standard material's file text, as chibi_ctl --format writes it. */
static char *material_text(const char *name, const standard_material *material)
{
    behavior_asset *asset = standard_material_build(name, material);
    char *saved = behavior_asset_save(asset);
    behavior_asset_free(asset);
    char error[160];
    char *text = asset_text_to_disk(saved, error, sizeof error);
    assert(text);
    free(saved);
    return text;
}

/* Reads into `name` the name the asset file at `path` declares, owned by
 * the caller, or NULL when it declares none. Refuses a file it cannot read
 * or parse, since that could hide a clash. */
static bool declared_name(import *in, const char *path, char **name)
{
    const char *shown = path + strlen(in->content) + 1;
    char *text = file_read_text(path);
    if (!text) {
        return fail(in, "Cannot read content/%s", shown);
    }
    char reason[160];
    av_doc *document = authoring_prototype_document_read(text, NULL, reason, sizeof reason);
    av_value *value = document ? av_obj_get(av_doc_root(document), "name") : NULL;
    *name = value && av_is_str(value) ? xstrdup(av_get_str(value)) : NULL;
    bool parsed = document != NULL;
    av_doc_free(document);
    free(text);
    return parsed || fail(in, "Cannot read content/%s: %s", shown, reason);
}

/* Whether `file` is `name` with the declarative asset extension. */
static bool names_file(const char *file, const char *name)
{
    size_t length = strlen(name);
    return !strncmp(file, name, length) &&
           !strcmp(file + length, DECLARATIVE_ASSET_EXTENSION);
}

/* Whether `file` is a declarative asset, as the catalogs read them. */
static bool asset_file(const char *file, new_asset_kind kind)
{
    size_t length = strlen(file), extension = strlen(DECLARATIVE_ASSET_EXTENSION);
    return file[0] != '.' &&
           ((length > extension &&
             !strcmp(file + length - extension, DECLARATIVE_ASSET_EXTENSION)) ||
            (kind == NEW_ASSET_PROTOTYPE && length > 6 &&
             !strcmp(file + length - 6, ".chibi")));
}

/* Whether `file`, an asset of `kind` that declares `name`, declares one of
 * the import's names in a file other than the one the import writes for it;
 * write_new refuses to replace that one. */
static bool clashes(const import *in, new_asset_kind kind, const char *file, const char *name)
{
    bool prototype = kind == NEW_ASSET_PROTOTYPE;
    if (names_file(file, name) && (prototype || kind == NEW_ASSET_MATERIAL)) {
        return false;
    }
    if (prototype) {
        return !strcmp(name, in->name);
    }
    for (int slot = 0; slot <= in->model->material_count; slot++) {
        if (in->materials[slot] && !strcmp(in->materials[slot], name)) {
            return true;
        }
    }
    return false;
}

/* The number of `kind` files the content holds, as the catalogs count
 * them, or -1 when it refuses because one already declares a name the
 * import would declare too, since preparing the project refuses two
 * declarations of one prototype or native graph name. */
static int undeclared(import *in, new_asset_kind kind)
{
    const char *folder = new_asset_folder(kind);
    size_t bytes = strlen(in->content) + strlen(folder) + 2;
    char *path = xmalloc(bytes);
    snprintf(path, bytes, "%s/%s", in->content, folder);
    DIR *directory = opendir(path);
    int code = errno;
    free(path);
    /* A folder it cannot read could hide a clash; one that is missing holds
     * none. */
    if (!directory && code != ENOENT) {
        fail(in, "Cannot read content/%s: %s", folder, strerror(code));
        return -1;
    }
    int count = 0;
    for (struct dirent *entry;
         count >= 0 && directory && (entry = readdir(directory));) {
        const char *file = entry->d_name;
        if (!asset_file(file, kind)) {
            continue;
        }
        bytes = strlen(in->content) + strlen(folder) + strlen(file) + 3;
        path = xmalloc(bytes);
        snprintf(path, bytes, "%s/%s/%s", in->content, folder, file);
        struct stat info;
        /* The catalogs pass over what is not a file. */
        if (stat(path, &info) || !S_ISREG(info.st_mode)) {
            free(path);
            continue;
        }
        char *name = NULL;
        if (!declared_name(in, path, &name)) {
            count = -1;
        } else if (name && clashes(in, kind, file, name)) {
            count = -1;
            fail(in, "content/%s/%s already declares %s", folder, file, name);
        } else {
            count++;
        }
        free(path);
        free(name);
    }
    if (directory) {
        closedir(directory);
    }
    return count;
}

/* Whether every name the import declares is still free, and the native
 * graphs fit their catalog with its materials. Prototypes share one
 * namespace, and the native graphs, every other declarative kind but
 * scenes, share another with materials. */
static bool names_free(import *in)
{
    int graphs = 0;
    for (int kind = 0; kind < NEW_ASSET_KIND_COUNT; kind++) {
        bool declarative = !strcmp(new_asset_extension(kind), DECLARATIVE_ASSET_EXTENSION);
        if ((!declarative && kind != NEW_ASSET_PROTOTYPE) || kind == NEW_ASSET_SCENE) {
            continue;
        }
        int count = undeclared(in, kind);
        if (count < 0) {
            return false;
        }
        graphs += kind == NEW_ASSET_PROTOTYPE ? 0 : count;
    }
    int materials = 0;
    for (int slot = 0; slot <= in->model->material_count; slot++) {
        materials += in->materials[slot] != NULL;
    }
    return graphs + materials <= NATIVE_GRAPH_DOCUMENTS_MAX ||
           fail(
               in,
               "The content holds %d native graphs, and %d more material%s "
               "would pass the %d a project can prepare",
               graphs,
               materials,
               materials == 1 ? "" : "s",
               NATIVE_GRAPH_DOCUMENTS_MAX
           );
}

/* Every image, material, the mesh and the prototype, as new content files. */
static bool write_files(import *in, const render_mesh_asset *mesh, const standard_material *materials)
{
    if (!new_asset_real_folder(in->content, in->error, in->size)) {
        return false;
    }
    const CM_Model *model = in->model;
    for (int i = 0; i < model->image_count; i++) {
        if (in->images[i] && !write_new(
                                 in,
                                 in->images[i],
                                 model->images[i].data,
                                 (size_t)model->images[i].size
                             )) {
            return false;
        }
    }
    for (int slot = 0; slot <= model->material_count; slot++) {
        if (!in->materials[slot]) {
            continue;
        }
        char key[NEW_ASSET_NAME_MAX + 32];
        snprintf(key, sizeof key, "content/materials/%s.kdl", in->materials[slot]);
        if (!write_text(in, key, material_text(in->materials[slot], &materials[slot]))) {
            return false;
        }
    }
    char mesh_key[NEW_ASSET_NAME_MAX + 32], prototype_key[NEW_ASSET_NAME_MAX + 32];
    snprintf(mesh_key, sizeof mesh_key, "content/meshes/%s.mesh", in->name);
    snprintf(prototype_key, sizeof prototype_key, "content/prototypes/%s.kdl", in->name);
    return write_mesh(in, mesh, mesh_key) &&
           write_text(in, prototype_key, prototype_text(in->name, mesh_key));
}

static void warn_unused(import *in)
{
    const CM_Model *model = in->model;
    size_t cameras = yyjson_arr_size(yyjson_obj_get(in->root, "cameras"));
    const struct
    {
        size_t count;
        const char *what;
    } unused[] = { { (size_t)model->skin_count, "skin" },
                   { (size_t)model->animation_count, "animation" },
                   { cameras, "camera" },
                   { (size_t)model->light_count, "light" } };
    for (size_t i = 0; i < sizeof unused / sizeof *unused; i++) {
        if (unused[i].count) {
            warn(in, "Ignored %zu %s%s.", unused[i].count, unused[i].what, unused[i].count == 1 ? "" : "s");
        }
    }
}

/* Reads the model and writes its files; `in` holds what to undo. */
static bool run(import *in, const char *path)
{
    size_t length = 0;
    unsigned char *bytes = read_whole(path, MODEL_IMPORT_BYTES_MAX, &length);
    if (!bytes) {
        return fail(in, "Cannot read %s, or it is over 256 MiB", path);
    }
    size_t folder = 0;
    for (size_t i = 0; path[i]; i++) {
        folder = path[i] == '/' || path[i] == '\\' ? i + 1 : folder;
    }
    char *directory = xmalloc(folder + 2);
    snprintf(directory, folder + 2, "%.*s.", (int)folder, path);
    char *real = realpath(directory, NULL);
    free(directory);
    if (!real) {
        free(bytes);
        return fail(in, "Cannot find the folder of %s", path);
    }
    sidecars beside = { .folder = real, .budget = MODEL_IMPORT_BYTES_MAX - length };
    CM_LoadParams params = { .read_fn = read_external,
                             .free_fn = free_external,
                             .udata = &beside };
    in->model = cm_load_ex(bytes, (int)length, params);
    yyjson_doc *json = in->model && !beside.over ? read_json(bytes, length) : NULL;
    free(real);
    free(bytes);
    if (beside.over) {
        return fail(in, "%s and the files it names are over 256 MiB together", path);
    }
    if (!in->model) {
        return fail(in, "Cannot load %s: %s", path, cm_error());
    }
    if (!json) {
        return fail(in, "Cannot read the glTF JSON of %s", path);
    }
    in->root = yyjson_doc_get_root(json);
    name_materials(in);
    in->images = xcalloc((size_t)in->model->image_count + 1, sizeof *in->images);
    standard_material *materials = xcalloc((size_t)in->model->material_count + 1, sizeof *materials);
    render_mesh_asset mesh = { 0 };
    bool ok = check_extensions(in) && bake_mesh(in, &mesh);
    for (int slot = 0; ok && slot <= in->model->material_count; slot++) {
        ok = !in->materials[slot] ||
             standard(in, slot < in->model->material_count ? slot : -1, &materials[slot]);
    }
    if (ok) {
        warn_unused(in);
    }
    ok = ok && names_free(in) && write_files(in, &mesh, materials);
    render_mesh_asset_free(&mesh);
    free(materials);
    yyjson_doc_free(json);
    return ok;
}

static void free_names(char **names, int count)
{
    for (int i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

bool model_import(
    const char *content,
    const char *path,
    const char *name,
    model_import_report *report,
    char *error,
    size_t size
)
{
    assert(size);
    *report = (model_import_report){ 0 };
    if (!new_asset_name_valid(name, error, size)) {
        return false;
    }
    error[0] = 0;
import in = {.content = content, .name = name, .report = report, .error = error, .size = size };
    bool ok = run(&in, path);
    for (int i = acount(in.created) - 1; i >= 0; i--) {
        if (!ok) {
            remove(in.created[i]);
        }
        free(in.created[i]);
    }
    afree(in.created);
    if (!ok) {
        for (int i = 0; i < acount(report->written); i++) {
            free(report->written[i]);
        }
        afree(report->written);
    }
    int materials = in.model ? in.model->material_count + 1 : 0;
    free_names(in.materials, in.materials ? materials : 0);
    free_names(in.scopes, in.scopes ? materials : 0);
    free_names(in.images, in.images ? in.model->image_count : 0);
    for (int i = 0; i < acount(in.taken); i++) {
        free(in.taken[i]);
    }
    afree(in.taken);
    cm_free(in.model);
    return ok;
}

void model_import_report_free(model_import_report *report)
{
    for (int i = 0; i < acount(report->written); i++) {
        free(report->written[i]);
    }
    afree(report->written);
    for (int i = 0; i < acount(report->warnings); i++) {
        free(report->warnings[i]);
    }
    afree(report->warnings);
}
