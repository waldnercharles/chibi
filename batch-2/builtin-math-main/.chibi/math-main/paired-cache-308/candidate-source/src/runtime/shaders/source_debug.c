#include "runtime/shaders/source_debug.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct shader_debug_snapshot
{
    shader_debug_document *documents;
    shader_debug_origin *origins;
    shader_debug_value *values;
    shader_debug_value *results;
    int document_count, origin_count, value_count, result_count;
    unsigned limitations;
};

static bool fail(shader_error *error, uint64_t source_id, const char *message)
{
    if (error) {
        error->node = source_id;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return false;
}

static int origin_index(const shader_debug_origin *origins, int count, uint64_t id)
{
    for (int i = 0; i < count; i++) {
        if (origins[i].source_id == id) {
            return i;
        }
    }
    return -1;
}

static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0;
    while (length <= limit && text[length]) {
        length++;
    }
    return length;
}

static bool source_map_valid(const shader_debug_source_map *map, int node_count, shader_error *error)
{
    const shader_debug_document *documents = map->documents;
    const shader_debug_origin *origins = map->origins;
    int document_count = map->document_count, origin_count = map->origin_count;
    if (document_count < 0 || document_count > SHADER_DEBUG_DOCUMENT_MAX ||
        origin_count < 0 || origin_count > SHADER_NODE_MAX || map->result_count < 0 ||
        map->result_count > SHADER_NODE_MAX || (document_count && !documents) ||
        (origin_count && !origins) || (map->result_count && !map->results)) {
        return fail(error, 0, "Invalid shader source map size");
    }
    size_t lengths[SHADER_DEBUG_DOCUMENT_MAX], total = 0;
    for (int i = 0; i < document_count; i++) {
        if (!documents[i].identity || !*documents[i].identity || !documents[i].text) {
            return fail(error, 0, "Shader source needs document identity and text");
        }
        lengths[i] = bounded_length(documents[i].text, SHADER_DEBUG_SOURCE_BYTES_MAX);
        total += lengths[i];
        if (lengths[i] > SHADER_DEBUG_SOURCE_BYTES_MAX ||
            total > SHADER_DEBUG_TOTAL_BYTES_MAX ||
            bounded_length(documents[i].identity, SHADER_DEBUG_IDENTITY_BYTES_MAX) >
                SHADER_DEBUG_IDENTITY_BYTES_MAX) {
            return fail(error, 0, "Shader debug source byte limit exceeded");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(documents[i].identity, documents[j].identity)) {
                return fail(error, 0, "Shader source document identities must be unique");
            }
        }
    }
    for (int i = 0; i < origin_count; i++) {
        const shader_debug_origin *origin = &origins[i];
        if (!origin->source_id || origin->document < 0 ||
            origin->document >= document_count || origin->start < 0 ||
            origin->end <= origin->start ||
            (size_t)origin->end > lengths[origin->document]) {
            return fail(error, origin->source_id, "Invalid shader source byte range");
        }
        if (origin_index(origins, i, origin->source_id) >= 0) {
            return fail(error, origin->source_id, "Shader source IDs must have unique origins");
        }
    }
    for (int i = 0; i < origin_count; i++) {
        uint64_t call = origins[i].call_site;
        int depth = 0;
        while (call) {
            int index = origin_index(origins, origin_count, call);
            if (index < 0 || ++depth > origin_count) {
                return fail(error, origins[i].source_id, "Invalid shader source call chain");
            }
            call = origins[index].call_site;
        }
    }
    for (int i = 0; i < map->result_count; i++) {
        const shader_debug_result *result = &map->results[i];
        if (origin_index(origins, origin_count, result->source_id) < 0 ||
            result->node < 0 || result->node >= node_count) {
            return fail(error, result->source_id, "Invalid shader source result binding");
        }
        for (int j = 0; j < i; j++) {
            if (map->results[j].source_id == result->source_id) {
                return fail(error, result->source_id, "Shader source result bindings must be unique");
            }
        }
    }
    return true;
}

static unsigned limitations(shader_op op)
{
    switch (op) {
        case SHADER_IMAGE_TEXTURE:
        case SHADER_IMAGE_TEXTURE_ALPHA: return SHADER_DEBUG_BASE_LEVEL_IMAGES;
        case SHADER_NORMAL_MAP:
        case SHADER_BUMP:
        case SHADER_DECAL_MASK: return SHADER_DEBUG_COORDINATE_GRADIENTS;
        case SHADER_TRIPLANAR_TEXTURE:
        case SHADER_TRIPLANAR_TEXTURE_ALPHA:
            return SHADER_DEBUG_BASE_LEVEL_IMAGES | SHADER_DEBUG_COORDINATE_GRADIENTS;
        case SHADER_STAR_FIELD: return SHADER_DEBUG_REFERENCE_FOOTPRINT;
        case SHADER_SURFACE_INFLUENCE: return SHADER_DEBUG_SCENE_INFLUENCE;
        default: return 0;
    }
}

void shader_debug_snapshot_free(shader_debug_snapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    for (int i = 0; i < snapshot->document_count; i++) {
        free((char *)snapshot->documents[i].identity);
        free((char *)snapshot->documents[i].text);
    }
    free(snapshot->documents);
    free(snapshot->origins);
    free(snapshot->values);
    free(snapshot->results);
    free(snapshot);
}

bool shader_debug_capture(
    const shader_graph *graph,
    const shader_sample_context *sample,
    const float (*parameters)[4],
    const shader_debug_source_map *map,
    shader_debug_snapshot **result,
    shader_error *error
)
{
    assert(graph && sample && map && result);
    if (!shader_graph_validate(graph, error) ||
        !source_map_valid(map, acount(graph->nodes), error)) {
        return false;
    }
    int count = acount(graph->nodes);
    const shader_debug_document *documents = map->documents;
    const shader_debug_origin *origins = map->origins;
    int document_count = map->document_count, origin_count = map->origin_count;
    for (int i = 0; i < count; i++) {
        uint64_t id = graph->nodes[i].source_id;
        if (id && origin_index(origins, origin_count, id) < 0) {
            return fail(error, id, "Shader operation has no source origin");
        }
    }
    float (*values)[4] = xcalloc((size_t)count, sizeof *values);
    bool evaluated = shader_graph_eval_values_validated(graph, sample, parameters, values, error);
    if (!evaluated) {
        free(values);
        return false;
    }
    shader_debug_snapshot *snapshot = xcalloc(1, sizeof *snapshot);
    snapshot->document_count = document_count;
    snapshot->documents = xcalloc((size_t)document_count, sizeof *snapshot->documents);
    for (int i = 0; i < document_count; i++) {
        snapshot->documents[i].identity = xstrdup(documents[i].identity);
        snapshot->documents[i].text = xstrdup(documents[i].text);
    }
    snapshot->origin_count = origin_count;
    snapshot->origins = xcalloc((size_t)origin_count, sizeof *snapshot->origins);
    if (origin_count) {
        memcpy(snapshot->origins, origins, (size_t)origin_count * sizeof *origins);
    }
    snapshot->value_count = count;
    snapshot->values = xcalloc((size_t)count, sizeof *snapshot->values);
    snapshot->limitations = SHADER_DEBUG_FLOAT_APPROXIMATION;
    for (int i = 0; i < count; i++) {
        const shader_node *node = &graph->nodes[i];
        int origin = origin_index(origins, origin_count, node->source_id);
        shader_debug_value *value = &snapshot->values[i];
        value->origin = origin >= 0 ? origins[origin]
                                    : (shader_debug_origin){ .document = -1 };
        value->op = node->op;
        value->width = node->width;
        memcpy(value->value, values[i], sizeof value->value);
        value->finite = true;
        for (int j = 0; j < node->width; j++) {
            value->finite &= isfinite(value->value[j]);
        }
        if (!value->finite) {
            snapshot->limitations |= SHADER_DEBUG_NONFINITE_VALUE;
        }
        snapshot->limitations |= limitations(node->op);
    }
    snapshot->result_count = map->result_count;
    snapshot->results = xcalloc((size_t)map->result_count, sizeof *snapshot->results);
    for (int i = 0; i < map->result_count; i++) {
        const shader_debug_result *binding = &map->results[i];
        int origin = origin_index(origins, origin_count, binding->source_id);
        snapshot->results[i] = snapshot->values[binding->node];
        snapshot->results[i].origin = origins[origin];
    }
    free(values);
    shader_debug_snapshot_free(*result);
    *result = snapshot;
    return true;
}

int shader_debug_value_count(const shader_debug_snapshot *snapshot)
{
    return snapshot->value_count;
}

unsigned shader_debug_limitations(const shader_debug_snapshot *snapshot)
{
    return snapshot->limitations;
}

const shader_debug_origin *shader_debug_origin_find(const shader_debug_snapshot *snapshot, uint64_t source_id)
{
    int index = origin_index(snapshot->origins, snapshot->origin_count, source_id);
    return index >= 0 ? &snapshot->origins[index] : NULL;
}

const shader_debug_document *shader_debug_document_at(const shader_debug_snapshot *snapshot, int document)
{
    return document >= 0 && document < snapshot->document_count
             ? &snapshot->documents[document]
             : NULL;
}

static bool documents_match(
    const shader_debug_snapshot *snapshot,
    const shader_debug_document *current,
    int count
)
{
    if (count != snapshot->document_count || (count && !current)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!current[i].identity || !current[i].text ||
            strcmp(current[i].identity, snapshot->documents[i].identity) ||
            strcmp(current[i].text, snapshot->documents[i].text)) {
            return false;
        }
    }
    return true;
}

const shader_debug_value *shader_debug_value_at(
    const shader_debug_snapshot *snapshot,
    int node,
    const shader_debug_document *current,
    int document_count
)
{
    return node >= 0 && node < snapshot->value_count &&
                   documents_match(snapshot, current, document_count)
             ? &snapshot->values[node]
             : NULL;
}

const shader_debug_value *shader_debug_source_value(
    const shader_debug_snapshot *snapshot,
    uint64_t source_id,
    const shader_debug_document *current,
    int document_count
)
{
    if (!documents_match(snapshot, current, document_count)) {
        return NULL;
    }
    for (int i = 0; i < snapshot->result_count; i++) {
        if (snapshot->results[i].origin.source_id == source_id) {
            return &snapshot->results[i];
        }
    }
    return NULL;
}

int shader_debug_find(const shader_debug_snapshot *snapshot, uint64_t source_id, int after)
{
    if (!source_id || after < -1 || after >= snapshot->value_count) {
        return -1;
    }
    for (int i = after + 1; i < snapshot->value_count; i++) {
        if (snapshot->values[i].origin.source_id == source_id) {
            return i;
        }
    }
    return -1;
}
