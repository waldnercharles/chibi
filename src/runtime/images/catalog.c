#include "runtime/images/catalog.h"

#include "runtime/images/table.h"
#include "runtime/shaders/volume.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    int w, h;
    unsigned char *pix;
} image_pixels;
typedef struct
{
    char *key;
    image_pixels image;
    char error[256];
    uint64_t revision;
} image_source;
typedef struct
{
    int source;
    shader_image_color_space color_space;
    shader_image_filter filter;
    shader_image_wrap wrap;
} image_variant;
static image_source *sources;
static image_variant *variants;
static uint64_t revision;
static render_image_loader loader;
static render_image_backend backend;
static uint64_t next_revision()
{
    assert(revision < UINT64_MAX);
    return ++revision;
}
void render_images_set_loader(render_image_loader value)
{
    loader = value;
}
render_image_loader render_images_loader()
{
    return loader;
}
void render_images_set_backend(render_image_backend value)
{
    backend = value;
}
static int find_source(const char *key)
{
    for (int i = 0; i < acount(sources); i++) {
        if (!strcmp(sources[i].key, key)) {
            return i;
        }
    }
    return -1;
}

static bool sampled_identity(const char *key, const image_pixels *image)
{
    size_t length = strlen(key);
    if (length < 8 || strcmp(key + length - 8, ".sdf.png")) {
        return true;
    }
    const char *name = strrchr(key, '/');
    name = name ? name + 1 : key;
    if (strlen(name) != 24) {
        return false;
    }
    uint64_t expected = 0;
    for (int i = 0; i < 16; i++) {
        char c = name[i];
        int digit = c >= '0' && c <= '9' ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                         : -1;
        if (digit < 0) {
            return false;
        }
        expected = (expected << 4) | (unsigned)digit;
    }
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *bytes = (const unsigned char *)image->pix;
    for (size_t i = 0; i < (size_t)image->w * image->h * 4; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash == expected;
}
bool render_image_validate_volumes(const shader_graph *g, shader_error *error)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i].op == SHADER_SAMPLED_SDF) {
            const shader_node *n = &g->nodes[i];
            shader_image_view view = { 0 };
            if (!render_image_variant(&g->images[n->image], error) ||
                !render_image_resolve(NULL, &g->images[n->image], &view) ||
                !shader_volume_validate(n, &view, error)) {
                return false;
            }
        }
    }
    return true;
}
bool render_image_reload(const char *key, char *message, size_t size)
{
    int index = find_source(key);
    if (index < 0) {
        index = acount(sources);
        apush(sources, ((image_source){ .key = xstrdup(key) }));
    }
    image_source *source = &sources[index];
    shader_image_view decoded = { 0 };
    char error[256] = { 0 };
    bool valid = loader.read &&
                 loader.read(loader.context, key, &decoded, error, sizeof error);
    image_pixels fresh = { .w = decoded.width,
                           .h = decoded.height,
                           .pix = (unsigned char *)decoded.pixels };
    if (valid && !fresh.pix) {
        snprintf(error, sizeof error, "Image loader returned no pixels");
        valid = false;
    }
    if (valid && (fresh.w < 1 || fresh.h < 1 || fresh.w > RENDER_IMAGE_SIDE_MAX ||
                  fresh.h > RENDER_IMAGE_SIDE_MAX)) {
        snprintf(error, sizeof error, "Image dimensions exceed the resident limit");
        valid = false;
    }
    if (valid && !sampled_identity(key, &fresh)) {
        snprintf(error, sizeof error, "Sampled SDF pixel digest differs from immutable filename");
        valid = false;
    }
    uint64_t fresh_revision = valid ? next_revision() : 0;
    if (valid && backend.replace) {
        valid = backend.replace(backend.context, key, &decoded, source->revision, fresh_revision, error, sizeof error);
    }
    if (!valid) {
        free(fresh.pix);
        snprintf(source->error, sizeof source->error, "%.80s: %.160s", key, error[0] ? error : "No image preparation loader supplied");
        if (size) {
            snprintf(message, size, "%s", source->error);
        }
        return false;
    }
    if (backend.retire && source->revision) {
        backend.retire(backend.context, source->revision);
    }
    free(source->image.pix);
    source->image = fresh;
    source->revision = fresh_revision;
    source->error[0] = 0;
    if (size) {
        message[0] = 0;
    }
    return true;
}
static bool render_image_load(const char *key, char *message, size_t size)
{
    int index = find_source(key);
    if (index < 0) {
        return render_image_reload(key, message, size);
    }
    if (size) {
        snprintf(message, size, "%s", sources[index].error);
    }
    return sources[index].image.pix != NULL;
}

bool render_image_info(const char *key, int *width, int *height)
{
    int index = find_source(key);
    if (index < 0 || !sources[index].image.pix) {
        return false;
    }
    *width = sources[index].image.w;
    *height = sources[index].image.h;
    return true;
}

int render_image_variant(const shader_image_source *source, shader_error *error)
{
    char message[256];
    if (!render_image_load(source->key, message, sizeof message)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "%s", message);
        }
        return 0;
    }
    return render_image_variant_resident(source, error);
}

int render_image_variant_resident(const shader_image_source *source, shader_error *error)
{
    int image = source->key ? find_source(source->key) : -1;
    if (image < 0 || !sources[image].image.pix || source->color_space < SHADER_IMAGE_SRGB ||
        source->color_space > SHADER_IMAGE_LINEAR ||
        source->filter < SHADER_IMAGE_NEAREST || source->filter > SHADER_IMAGE_BILINEAR ||
        source->wrap < SHADER_IMAGE_REPEAT || source->wrap > SHADER_IMAGE_CLAMP) {
        if (error) {
            error->node = 0;
            snprintf(
                error->message,
                sizeof error->message,
                "Image resource or sampler is not resident: %.80s",
                source->key ? source->key : ""
            );
        }
        return 0;
    }
    for (int i = 0; i < acount(variants); i++) {
        const image_variant *variant = &variants[i];
        if (variant->source == image && variant->color_space == source->color_space &&
            variant->filter == source->filter && variant->wrap == source->wrap) {
            return i + 1;
        }
    }
    apush(
        variants,
        ((image_variant){ image, source->color_space, source->filter, source->wrap })
    );
    return acount(variants);
}

bool render_image_resolve(void *, const shader_image_source *source, shader_image_view *view)
{
    int index = find_source(source->key);
    if (index < 0 || !sources[index].image.pix) {
        return false;
    }
    image_pixels *image = &sources[index].image;
    *view = (shader_image_view){ (const unsigned char *)image->pix,
                                 image->w,
                                 image->h };
    return true;
}

bool render_image_sampler(int id, shader_image_source *sampler, shader_image_view *view, uint64_t *stamp)
{
    if (id < 1 || id > acount(variants)) {
        return false;
    }
    const image_variant *v = &variants[id - 1];
    const image_source *r = &sources[v->source];
    *sampler = (shader_image_source){ .key = r->key,
                                      .color_space = v->color_space,
                                      .filter = v->filter,
                                      .wrap = v->wrap };
    *view = (shader_image_view){ r->image.pix, r->image.w, r->image.h };
    *stamp = r->revision;
    return r->image.pix != NULL;
}
void render_image_catalog_clear()
{
    afree(variants);
    for (int i = 0; i < acount(sources); i++) {
        if (backend.retire && sources[i].revision) {
            backend.retire(backend.context, sources[i].revision);
        }
        free(sources[i].key);
        free(sources[i].image.pix);
    }
    afree(sources);
    if (loader.clear) {
        loader.clear(loader.context);
    }
}
static size_t row_growth(int length, int capacity, size_t width)
{
    if (length < capacity) {
        return 0;
    }
    int next = capacity ? capacity * 2 : 16;
    return (size_t)(next - capacity) * width + (capacity ? 0 : sizeof(dyna_hdr));
}
struct render_image_table
{
    image_source *sources;
    image_variant *variants;
};
size_t render_image_table_header_bytes()
{
    return sizeof(render_image_table);
}
render_image_table *render_image_table_new()
{
    return xcalloc(1, sizeof(render_image_table));
}
static void image_header(execution_wire *w, int *width, int *height)
{
    uint64_t magic = UINT64_C(0x31474d4942484343),
             version = RENDER_IMAGE_PREPARED_VERSION;
    execution_wire_u64(w, &magic);
    execution_wire_u64(w, &version);
    execution_wire_int(w, width);
    execution_wire_int(w, height);
    if (!w->error && (magic != UINT64_C(0x31474d4942484343) ||
                      version != RENDER_IMAGE_PREPARED_VERSION || *width < 1 ||
                      *width > RENDER_IMAGE_SIDE_MAX || *height < 1 ||
                      *height > RENDER_IMAGE_SIDE_MAX)) {
        w->error = "Prepared image header or dimensions are invalid";
    }
}
static bool image_error(shader_error *error, const char *text)
{
    if (error) {
        *error = (shader_error){ 0 };
        snprintf(error->message, sizeof error->message, "%s", text);
    }
    return false;
}
int render_image_source_count()
{
    return acount(sources);
}
const char *render_image_source_key(int index)
{
    return sources[index].key;
}
bool render_image_pixels_prepared_write(
    const shader_image_view *view,
    unsigned char **data,
    size_t *length,
    shader_error *error
)
{
    assert(view && data && length);
    *data = NULL;
    *length = 0;
    if (!view->pixels) {
        return image_error(error, "Image pixels are absent");
    }
    int width = view->width, height = view->height;
    execution_wire w = { 0 };
    image_header(&w, &width, &height);
    if (w.error) {
        afree(w.data);
        return image_error(error, w.error);
    }
    size_t pixels = (size_t)width * height * 4;
    if (pixels > EXECUTION_WIRE_MAX_BYTES - w.size) {
        afree(w.data);
        return image_error(error, "Prepared image exceeds byte bounds");
    }

    asetlen(w.data, (int)(w.size + pixels));
    memcpy(w.data + w.size, view->pixels, pixels);
    w.size += pixels;
    *data = xmalloc(w.size);
    memcpy(*data, w.data, w.size);
    *length = w.size;
    afree(w.data);
    return true;
}
bool render_image_prepared_write(const char *key, unsigned char **data, size_t *length, shader_error *error)
{
    *data = NULL;
    *length = 0;
    int index = find_source(key);
    if (index < 0 || !sources[index].image.pix) {
        return image_error(error, "Image pixels are not resident");
    }
    image_pixels image = sources[index].image;
    shader_image_view view = { .pixels = (const unsigned char *)image.pix,
                               .width = image.w,
                               .height = image.h };
    return render_image_pixels_prepared_write(&view, data, length, error);
}
bool render_image_table_decode(
    render_image_table *table,
    const char *key,
    const void *data,
    size_t length,
    size_t *remaining,
    shader_error *error
)
{
    if (!key || !key[0] || strlen(key) > 1024 || strncmp(key, "content/", 8) ||
        strstr(key, "..") || strchr(key, '\\')) {
        return image_error(error, "Prepared image key is invalid");
    }
    for (int i = 0; i < acount(table->sources); i++) {
        if (!strcmp(key, table->sources[i].key)) {
            return image_error(error, "Prepared image keys are duplicated");
        }
    }
    execution_wire w = { .data = (unsigned char *)data, .size = length, .reading = true };
    int width = 0, height = 0;
    image_header(&w, &width, &height);
    if (w.error) {
        return image_error(error, w.error);
    }
    size_t pixels = (size_t)width * height * 4,
           retained = pixels + strlen(key) + 1 +
                      row_growth(acount(table->sources), acap(table->sources), sizeof(image_source));
    if (pixels != length - w.offset || retained > *remaining) {
        return image_error(error, "Prepared image byte count or ownership budget differs");
    }
    image_pixels image = { .w = width, .h = height, .pix = xmalloc(pixels) };
    assert(image.pix);
    memcpy(image.pix, w.data + w.offset, pixels);
    if (!sampled_identity(key, &image)) {
        free(image.pix);
        return image_error(error, "Sampled image identity differs");
    }
    asetcap(table->sources, acount(table->sources) + 1);
    apush(
        table->sources,
        ((image_source){ .key = xstrdup(key), .image = image, .revision = next_revision() })
    );
    *remaining -= retained;
    return true;
}
bool render_image_table_resolve(void *context, const shader_image_source *source, shader_image_view *view)
{
    const render_image_table *table = context;
    for (int i = 0; i < acount(table->sources); i++) {
        const image_source *r = &table->sources[i];
        if (!strcmp(source->key, r->key)) {
            *view = (shader_image_view){ (const unsigned char *)r->image.pix,
                                         r->image.w,
                                         r->image.h };
            return true;
        }
    }
    return false;
}
int render_image_table_variant(
    render_image_table *table,
    const shader_image_source *source,
    size_t *remaining,
    shader_error *error
)
{
    if (!source->key || source->color_space < SHADER_IMAGE_SRGB ||
        source->color_space > SHADER_IMAGE_LINEAR ||
        source->filter < SHADER_IMAGE_NEAREST || source->filter > SHADER_IMAGE_BILINEAR ||
        source->wrap < SHADER_IMAGE_REPEAT || source->wrap > SHADER_IMAGE_CLAMP) {
        return image_error(error, "Prepared image sampler is invalid");
    }
    int image = -1;
    for (int i = 0; i < acount(table->sources); i++) {
        if (!strcmp(source->key, table->sources[i].key)) {
            image = i;
        }
    }
    if (image < 0) {
        return image_error(error, "Prepared shader image is missing");
    }
    for (int i = 0; i < acount(table->variants); i++) {
        const image_variant *v = &table->variants[i];
        if (v->source == image && v->color_space == source->color_space &&
            v->filter == source->filter && v->wrap == source->wrap) {
            return i + 1;
        }
    }
    size_t added = row_growth(acount(table->variants), acap(table->variants), sizeof(image_variant));
    if (added > *remaining) {
        return image_error(error, "Prepared image variant ownership budget exceeded");
    }
    asetcap(table->variants, acount(table->variants) + 1);
    *remaining -= added;
    apush(
        table->variants,
        ((image_variant){ .source = image,
                          .color_space = source->color_space,
                          .filter = source->filter,
                          .wrap = source->wrap })
    );
    return acount(table->variants);
}
void render_image_table_exchange(render_image_table *table)
{
    image_source *old_sources = sources;
    image_variant *old_variants = variants;
    sources = table->sources;
    variants = table->variants;
    table->sources = old_sources;
    table->variants = old_variants;
}
void render_image_table_free(render_image_table *table)
{
    if (!table) {
        return;
    }
    afree(table->variants);
    for (int i = 0; i < acount(table->sources); i++) {
        if (backend.retire) {
            backend.retire(backend.context, table->sources[i].revision);
        }
        free(table->sources[i].key);
        free(table->sources[i].image.pix);
    }
    afree(table->sources);
    free(table);
}
