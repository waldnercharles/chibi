#include "automation/material_bake.h"

#include "graph/shader/shader_graph.h"
#include "runtime/images/catalog.h"
#include "runtime/shaders/bake.h"
#include "runtime/shaders/prepared.h"
#include "support/array.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAKE_SIDE_MAX 128
#define BAKE_WORK_MAX UINT64_C(16777216)
#define BAKE_IMAGE_KEYS_MAX 8
#define BAKE_IMAGE_BYTES_MAX ((size_t)64 * 1024 * 1024)

static bool vector(yyjson_val *request, const char *key, float *out, int width)
{
    yyjson_val *value = yyjson_obj_get(request, key);
    if (!value) {
        return true;
    }
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != (size_t)width) {
        return false;
    }
    for (int i = 0; i < width; i++) {
        yyjson_val *item = yyjson_arr_get(value, (size_t)i);
        double n = yyjson_get_num(item);
        if (!yyjson_is_num(item) || !isfinite(n) || fabs(n) > FLT_MAX) {
            return false;
        }
        out[i] = (float)n;
    }
    return true;
}

static bool parameters(
    const behavior_asset *asset,
    const shader_graph *graph,
    yyjson_val *request,
    shader_bake_parameter *out,
    uint64_t *source_ids,
    int *count
)
{
    uint64_t ids[SHADER_PARAMETER_MAX] = { 0 };
    bool seen[SHADER_PARAMETER_MAX] = { false };
    *count = 0;
    for (int i = 0; i < acount(graph->nodes); i++) {
        const shader_node *node = &graph->nodes[i];
        if (node->op != SHADER_PARAMETER) {
            continue;
        }
        if (node->parameter < 0 || node->parameter >= SHADER_PARAMETER_MAX) {
            return false;
        }
        int slot = node->parameter;
        if (seen[slot]) {
            if (ids[slot] != node->source_id) {
                return false;
            }
            continue;
        }
        seen[slot] = true;
        ids[slot] = node->source_id;
        source_ids[*count] = node->source_id;
        out[(*count)++] = (shader_bake_parameter){ .slot = slot, .width = node->width };
        memcpy(out[*count - 1].value, node->value, sizeof node->value);
    }
    yyjson_val *overrides = yyjson_obj_get(request, "overrides");
    if (!overrides) {
        return true;
    }
    if (!yyjson_is_arr(overrides) || yyjson_arr_size(overrides) > SHADER_PARAMETER_MAX) {
        return false;
    }
    bool changed[SHADER_PARAMETER_MAX] = { false };
    for (size_t i = 0; i < yyjson_arr_size(overrides); i++) {
        yyjson_val *entry = yyjson_arr_get(overrides, i);
        const char *name = yyjson_get_str(yyjson_obj_get(entry, "asset"));
        const char *scope = yyjson_get_str(yyjson_obj_get(entry, "scope"));
        yyjson_val *id = yyjson_obj_get(entry, "parameter_id");
        if (!yyjson_is_obj(entry) || !name || strcmp(name, asset->name) ||
            !scope || *scope || !yyjson_is_uint(id)) {
            return false;
        }
        int match = -1;
        for (int p = 0; p < *count; p++) {
            if (ids[out[p].slot] == yyjson_get_uint(id)) {
                match = p;
            }
        }
        if (match < 0 || changed[match] || !yyjson_obj_get(entry, "value") ||
            !vector(entry, "value", out[match].value, out[match].width)) {
            return false;
        }
        changed[match] = true;
    }
    return true;
}

static yyjson_mut_val *numbers(yyjson_mut_doc *doc, const float *values, int count)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < count; i++) {
        yyjson_mut_arr_add_real(doc, array, values[i]);
    }
    return array;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
static uint64_t hash_int(uint64_t hash, uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        unsigned char byte = (unsigned char)(value >> (8 * i));
        hash = hash_bytes(hash, &byte, 1);
    }
    return hash;
}
static void put_hash(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, uint64_t hash)
{
    char text[17];
    snprintf(text, sizeof text, "%016llx", (unsigned long long)hash);
    yyjson_mut_obj_add_strcpy(doc, object, key, text);
}
static bool prepare_images(const shader_graph *graph, shader_error *error)
{
    const shader_image_source *sources[BAKE_IMAGE_KEYS_MAX];
    int count = 0;
    /* Complete key admission before touching any image file. Sampler variants
     * sharing a source key count once toward decoded storage. */
    for (int i = 0; i < acount(graph->images); i++) {
        const shader_image_source *source = &graph->images[i];
        bool found = false;
        for (int j = 0; j < count; j++) {
            found |= !strcmp(source->key, sources[j]->key);
        }
        if (found) {
            continue;
        }
        if (count == BAKE_IMAGE_KEYS_MAX) {
            snprintf(error->message, sizeof error->message, "Material bake exceeds 8 distinct image dependency keys");
            return false;
        }
        sources[count++] = source;
    }
    size_t bytes = 0;
    for (int i = 0; i < count; i++) {
        shader_image_view image = { 0 };
        if (!render_image_variant(sources[i], error) ||
            !render_image_resolve(NULL, sources[i], &image)) {
            return false;
        }
        size_t decoded = (size_t)image.width * image.height * 4;
        if (decoded > BAKE_IMAGE_BYTES_MAX - bytes) {
            snprintf(error->message, sizeof error->message, "Material bake exceeds 64 MiB decoded image dependencies");
            return false;
        }
        bytes += decoded;
    }
    return true;
}

static bool provenance(const shader_graph *graph, yyjson_mut_doc *doc, yyjson_mut_val *result, shader_error *error)
{
    unsigned char *bytes = NULL;
    size_t size = 0;
    if (!shader_graph_prepared_write(graph, &bytes, &size, error)) {
        return false;
    }
    uint64_t hash = hash_bytes(UINT64_C(14695981039346656037), bytes, size);
    free(bytes);
    yyjson_mut_obj_add_str(doc, result, "input_hash_algorithm", "fnv1a64-bytes");
    yyjson_mut_obj_add_uint(doc, result, "linked_graph_format", SHADER_PREPARED_VERSION);
    put_hash(doc, result, "linked_graph_hash", hash);
    yyjson_mut_val *images = yyjson_mut_obj_add_arr(doc, result, "image_dependencies");
    const char *keys[BAKE_IMAGE_KEYS_MAX] = { 0 };
    uint64_t hashes[BAKE_IMAGE_KEYS_MAX] = { 0 };
    int count = 0;
    for (int i = 0; i < acount(graph->images); i++) {
        const shader_image_source *source = &graph->images[i];
        shader_image_view image = { 0 };
        if (!render_image_resolve(NULL, source, &image)) {
            snprintf(error->message, sizeof error->message, "Cannot snapshot image dependency");
            return false;
        }
        int cached = -1;
        for (int j = 0; j < count; j++) {
            if (!strcmp(source->key, keys[j])) {
                cached = j;
            }
        }
        if (cached >= 0) {
            hash = hashes[cached];
        } else {
            hash = hash_int(UINT64_C(14695981039346656037), (uint32_t)image.width);
            hash = hash_int(hash, (uint32_t)image.height);
            hash = hash_bytes(hash, image.pixels, (size_t)image.width * image.height * 4);
            keys[count] = source->key;
            hashes[count++] = hash;
        }
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, images);
        yyjson_mut_obj_add_strcpy(doc, entry, "key", source->key);
        yyjson_mut_obj_add_str(doc, entry, "format", "uint32-le-dimensions-rgba8");
        yyjson_mut_obj_add_int(doc, entry, "width", image.width);
        yyjson_mut_obj_add_int(doc, entry, "height", image.height);
        yyjson_mut_obj_add_int(doc, entry, "color_space", source->color_space);
        yyjson_mut_obj_add_int(doc, entry, "filter", source->filter);
        yyjson_mut_obj_add_int(doc, entry, "wrap", source->wrap);
        put_hash(doc, entry, "pixel_hash", hash);
    }
    return true;
}

bool automation_material_bake(
    const behavior_asset *asset,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
)
{
    shader_bake_desc desc = { .width = 32,
                              .height = 32,
                              .channels = SHADER_BAKE_CHANNELS_ALL,
                              .axis_u = { 1, 0, 0 },
                              .axis_v = { 0, 1, 0 },
                              .uv_extent = { 1, 1 },
                              .context = { .view_direction = { 0, 0, 1 },
                                           .resolve_image = render_image_resolve } };
    const char *dimensions[] = { "width", "height" };
    int *sides[] = { &desc.width, &desc.height };
    for (int i = 0; i < 2; i++) {
        yyjson_val *v = yyjson_obj_get(request, dimensions[i]);
        if (!v) {
            continue;
        }
        if (!yyjson_is_int(v) || yyjson_get_sint(v) < 1 ||
            yyjson_get_sint(v) > BAKE_SIDE_MAX) {
            goto invalid;
        }
        *sides[i] = (int)yyjson_get_sint(v);
    }
    float uv_max[2] = { 1, 1 };
    if (!vector(request, "uv_min", desc.uv_origin, 2) ||
        !vector(request, "uv_max", uv_max, 2) ||
        !vector(request, "origin", desc.origin, 3) ||
        !vector(request, "axis_u", desc.axis_u, 3) ||
        !vector(request, "axis_v", desc.axis_v, 3)) {
        goto invalid;
    }
    for (int i = 0; i < 2; i++) {
        desc.uv_extent[i] = uv_max[i] - desc.uv_origin[i];
        if (!isfinite(desc.uv_extent[i]) || desc.uv_extent[i] <= 0) {
            goto invalid;
        }
    }
    yyjson_val *time = yyjson_obj_get(request, "time");
    if (time) {
        double value = yyjson_get_num(time);
        if (!yyjson_is_num(time) || !isfinite(value) || fabs(value) > FLT_MAX) {
            goto invalid;
        }
        desc.context.time = (float)value;
    }
    yyjson_val *selected = yyjson_obj_get(request, "channel");
    if (selected) {
        const char *name = yyjson_get_str(selected);
        if (!name) {
            goto invalid;
        }
        desc.channels = 0;
        for (int i = 0; i < SHADER_BAKE_CHANNEL_COUNT; i++) {
            if (!strcmp(name, shader_bake_channel_name(i))) {
                desc.channels = 1u << i;
            }
        }
        if (!desc.channels) {
            goto invalid;
        }
    }
    shader_graph graph = { 0 };
    shader_error diagnostic = { 0 };
    shader_bake_parameter snapshot[SHADER_PARAMETER_MAX] = { 0 };
    int count = 0;
    uint64_t source_ids[SHADER_PARAMETER_MAX] = { 0 };
    if (!shader_graph_from_graph(asset->graph, &graph, &diagnostic)) {
        snprintf(error, size, "%s", diagnostic.message);
        return false;
    }
    uint64_t prefix[SHADER_NODE_MAX] = { 0 }, per_sample = 0;
    for (int i = 0; i < acount(graph.nodes); i++) {
        const shader_node *n = &graph.nodes[i];
        bool derivative = n->op >= SHADER_NORMAL_MAP && n->op <= SHADER_DECAL_MASK;
        uint64_t cost = 1;
        if (derivative) {
            int root = n->input[n->op == SHADER_NORMAL_MAP ? 1 : 0];
            /* Each derivative evaluates six upstream samples; include full
             * graph scans and recursively weighted prefix work. */
            cost += 6 * (prefix[root] + 2 * (uint64_t)acount(graph.nodes));
        }
        per_sample += cost;
        if (per_sample > BAKE_WORK_MAX) {
            per_sample = BAKE_WORK_MAX + 1;
        }
        prefix[i] = per_sample;
    }
    uint64_t work = (uint64_t)desc.width * desc.height * per_sample;
    bool valid = work <= BAKE_WORK_MAX &&
                 parameters(asset, &graph, request, snapshot, source_ids, &count);
    if (valid) {
        valid = prepare_images(&graph, &diagnostic);
    }
    if (valid) {
        valid = provenance(&graph, doc, result, &diagnostic);
    }
    desc.parameters = snapshot;
    desc.parameter_count = count;
    shader_bake_job *job = valid ? shader_bake_new(&graph, &desc, &diagnostic) : NULL;
    shader_graph_free(&graph);
    if (!job) {
        snprintf(error, size, "%s", diagnostic.message[0] ? diagnostic.message : "Invalid material parameter overrides or bake work budget exceeded");
        return false;
    }
    valid = shader_bake_step(job, desc.height, &diagnostic);
    shader_bake_result baked = { 0 };
    if (valid) {
        valid = shader_bake_take(job, &baked, &diagnostic);
    }
    shader_bake_free(job);
    if (!valid) {
        snprintf(error, size, "%s", diagnostic.message);
        shader_bake_result_free(&baked);
        return false;
    }
    yyjson_mut_obj_add_uint(doc, result, "work_bound", work);
    yyjson_mut_obj_add_int(doc, result, "width", baked.width);
    yyjson_mut_obj_add_int(doc, result, "height", baked.height);
    yyjson_mut_obj_add_str(doc, result, "sampling", "Pixel centers; row zero follows axis_v from origin; world, object and part coordinates coincide");
    yyjson_mut_obj_add_str(doc, result, "hash_algorithm", "fnv1a64-float32-le");
    yyjson_mut_obj_add_val(doc, result, "origin", numbers(doc, desc.origin, 3));
    yyjson_mut_obj_add_val(doc, result, "axis_u", numbers(doc, desc.axis_u, 3));
    yyjson_mut_obj_add_val(doc, result, "axis_v", numbers(doc, desc.axis_v, 3));
    yyjson_mut_obj_add_val(doc, result, "uv_min", numbers(doc, desc.uv_origin, 2));
    yyjson_mut_obj_add_val(doc, result, "uv_max", numbers(doc, uv_max, 2));
    yyjson_mut_obj_add_real(doc, result, "time", desc.context.time);
    yyjson_mut_val *parameter_values = yyjson_mut_obj_add_arr(doc, result, "parameters");
    for (int i = 0; i < count; i++) {
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, parameter_values);
        yyjson_mut_obj_add_strcpy(doc, entry, "asset", asset->name);
        yyjson_mut_obj_add_str(doc, entry, "scope", "");
        yyjson_mut_obj_add_uint(doc, entry, "parameter_id", source_ids[i]);
        const node *authored = NULL;
        for (int g = 0; g < acount(asset->graphs) && !authored; g++) {
            authored = graph_node_by_id(asset->graphs[g], source_ids[i]);
        }
        yyjson_mut_obj_add_strcpy(
            doc,
            entry,
            "name",
            authored && authored->reference_name ? authored->reference_name : ""
        );
        yyjson_mut_obj_add_val(
            doc,
            entry,
            "value",
            numbers(doc, snapshot[i].value, snapshot[i].width)
        );
    }
    yyjson_mut_val *channels = yyjson_mut_obj_add_arr(doc, result, "channels");
    int offset = 0;
    for (int channel = 0; channel < SHADER_BAKE_CHANNEL_COUNT; channel++) {
        if (!(desc.channels & (1u << channel))) {
            continue;
        }
        int width = shader_bake_channel_width(channel);
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, channels);
        yyjson_mut_obj_add_str(doc, entry, "name", shader_bake_channel_name(channel));
        yyjson_mut_obj_add_int(doc, entry, "components", width);
        yyjson_mut_obj_add_str(doc, entry, "encoding", "raw-linear-float32");
        const char *semantic = channel == SHADER_BAKE_BASE_COLOR || channel == SHADER_BAKE_EMISSION
                                 ? "linear-color"
                             : channel == SHADER_BAKE_NORMAL
                                 ? "raw-surface-direction; zero means geometry "
                                   "normal; not tangent-space texture encoding"
                                 : "scalar-data";
        yyjson_mut_obj_add_str(doc, entry, "semantic", semantic);
        yyjson_mut_val *data = yyjson_mut_obj_add_arr(doc, entry, "data");
        uint64_t hash = UINT64_C(14695981039346656037);
        for (int pixel = 0; pixel < baked.width * baked.height; pixel++) {
            for (int c = 0; c < width; c++) {
                float value = baked.pixels[pixel * baked.stride + offset + c];
                uint32_t bits;
                memcpy(&bits, &value, sizeof bits);
                for (int b = 0; b < 4; b++) {
                    hash = (hash ^ ((bits >> (8 * b)) & 255)) * UINT64_C(1099511628211);
                }
                yyjson_mut_arr_add_real(doc, data, value);
            }
        }
        char text[17];
        snprintf(text, sizeof text, "%016llx", (unsigned long long)hash);
        yyjson_mut_obj_add_strcpy(doc, entry, "hash", text);
        offset += width;
    }
    shader_bake_result_free(&baked);
    return true;
invalid:
    snprintf(error, size, "Material bake requires dimensions 1..128, finite plane/time, increasing UV bounds and a known channel");
    return false;
}
