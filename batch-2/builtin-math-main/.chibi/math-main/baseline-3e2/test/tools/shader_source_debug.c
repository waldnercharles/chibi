#include "runtime/shaders/source_debug.h"
#include "runtime/shaders/volume.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char material_key[] = "content/materials/Debug.chibi";
static const char material_text[] = "Shade :: (uv: vec2, time: number, gain: "
                                    "number) -> Surface {\n"
                                    "    phase := uv.x * gain + time\n"
                                    "    first := Twice(uv.x)\n"
                                    "    second := Twice(time)\n"
                                    "    alias := first\n"
                                    "    color := image.Sample(uv)\n"
                                    "    normal := Bump(phase)\n"
                                    "    stars := StarField(uv)\n"
                                    "    activity := Influence(uv)\n"
                                    "    decal := Decal(uv)\n"
                                    "}\n";
static const char helper_key[] = "content/libraries/Math.chibi";
static const char
    helper_text[] = "Twice :: (x: number) -> number { return x * 2 }\n";
static shader_graph graph;
static shader_debug_snapshot *snapshot;
static shader_debug_document documents[2];
static shader_debug_origin origins[16];
static shader_debug_result results[16];
static shader_debug_source_map map;
static shader_error error;
static char *source_bytes, *identity_bytes;

static void setup()
{
    shader_graph_init(&graph);
    documents[0] = (shader_debug_document){ xstrdup(material_key),
                                            xstrdup(material_text) };
    documents[1] = (shader_debug_document){ xstrdup(helper_key), xstrdup(helper_text) };
    memset(origins, 0, sizeof origins);
    memset(results, 0, sizeof results);
    map = (shader_debug_source_map){ .documents = documents,
                                     .document_count = 2,
                                     .origins = origins,
                                     .results = results };
    error = (shader_error){ 0 };
}
static void teardown()
{
    shader_debug_snapshot_free(snapshot);
    snapshot = NULL;
    free(source_bytes);
    free(identity_bytes);
    source_bytes = identity_bytes = NULL;
    shader_graph_free(&graph);
    for (int i = 0; i < 2; i++) {
        free((char *)documents[i].identity);
        free((char *)documents[i].text);
        documents[i] = (shader_debug_document){ 0 };
    }
}
static int operation(shader_op op, int width, uint64_t source, int a, int b)
{
    int node = shader_graph_add(&graph, op, width);
    graph.nodes[node].source_id = source;
    graph.nodes[node].input[0] = a;
    graph.nodes[node].input[1] = b;
    graph.output_count = 1;
    graph.output[0] = node;
    return node;
}
static int constant(float value)
{
    int node = operation(SHADER_CONSTANT, 1, 0, -1, -1);
    graph.nodes[node].value[0] = value;
    return node;
}
static void origin(uint64_t id, int document, const char *text, uint64_t occurrence, uint64_t call)
{
    const char *start = strstr(documents[document].text, text);
    assert(start && map.origin_count < 16);
    origins[map.origin_count++] = (shader_debug_origin){
        .source_id = id,
        .occurrence = occurrence,
        .call_site = call,
        .document = document,
        .start = (int)(start - documents[document].text),
        .end = (int)(start - documents[document].text + strlen(text))
    };
}
static void result(uint64_t id, int node)
{
    assert(map.result_count < 16);
    results[map.result_count++] = (shader_debug_result){ id, node };
}
static bool near(float actual, float expected)
{
    return isfinite(actual) && fabsf(actual - expected) < .00001f;
}

TEST_CASE(uv_time_and_uniform_values_follow_one_sample)
{
    int uv = operation(SHADER_UV, 2, 10, -1, -1);
    int x = operation(SHADER_X, 1, 11, uv, -1);
    int gain = operation(SHADER_PARAMETER, 1, 12, -1, -1);
    graph.nodes[gain].value[0] = 2;
    int scaled = operation(SHADER_MULTIPLY, 1, 13, x, gain);
    int time = operation(SHADER_TIME, 1, 14, -1, -1);
    int phase = operation(SHADER_ADD, 1, 15, scaled, time);
    origin(10, 0, "uv", 100, 0);
    origin(11, 0, "uv.x", 101, 0);
    origin(12, 0, "gain", 102, 0);
    origin(13, 0, "uv.x * gain", 103, 0);
    origin(14, 0, "time", 104, 0);
    origin(15, 0, "uv.x * gain + time", 105, 0);
    result(15, phase);
    shader_sample_context sample = { .uv = { .25f, .75f }, .time = 1.5f };
    float parameters[32][4] = { { 4 } };
    REQUIRE(shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    REQUIRE(shader_debug_value_count(snapshot) == 6);
    const shader_debug_value *value = shader_debug_value_at(snapshot, uv, documents, 2);
    REQUIRE(value && value->op == SHADER_UV && value->width == 2 && value->finite);
    REQUIRE(near(value->value[0], .25f) && near(value->value[1], .75f));
    value = shader_debug_value_at(snapshot, scaled, documents, 2);
    REQUIRE(value && near(value->value[0], 1));
    value = shader_debug_source_value(snapshot, 15, documents, 2);
    REQUIRE(value && value->op == SHADER_ADD && near(value->value[0], 2.5f));
    parameters[0][0] = 6;
    sample.uv[0] = .5f;
    sample.time = 2;
    REQUIRE(near(value->value[0], 2.5f));
    REQUIRE(shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    value = shader_debug_source_value(snapshot, 15, documents, 2);
    REQUIRE(value && near(value->value[0], 5));
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    value = shader_debug_source_value(snapshot, 15, documents, 2);
    REQUIRE(value && near(value->value[0], 3));
    REQUIRE(shader_debug_limitations(snapshot) == SHADER_DEBUG_FLOAT_APPROXIMATION);
    return true;
}
TEST_CASE(source_results_select_principal_nodes_and_zero_node_aliases)
{
    int two = constant(2), three = constant(3), one = constant(1);
    int scaled = operation(SHADER_MULTIPLY, 1, 30, two, three);
    int phase = operation(SHADER_ADD, 1, 30, scaled, one);
    origin(30, 0, "uv.x * gain + time", 300, 0);
    origin(31, 0, "alias", 301, 0);
    result(30, phase);
    result(31, phase);
    shader_sample_context sample = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    REQUIRE(shader_debug_find(snapshot, 30, -1) == scaled);
    REQUIRE(shader_debug_find(snapshot, 30, scaled) == phase);
    REQUIRE(shader_debug_find(snapshot, 30, phase) == -1);
    REQUIRE(shader_debug_find(snapshot, 31, -1) == -1);
    const shader_debug_value *value = shader_debug_value_at(snapshot, scaled, documents, 2);
    REQUIRE(value && value->op == SHADER_MULTIPLY && near(value->value[0], 6));
    value = shader_debug_source_value(snapshot, 30, documents, 2);
    REQUIRE(value && value->op == SHADER_ADD && near(value->value[0], 7));
    value = shader_debug_source_value(snapshot, 31, documents, 2);
    REQUIRE(value && value->origin.source_id == 31 && near(value->value[0], 7));
    REQUIRE(value->origin.occurrence == 301 && value->origin.start == origins[1].start);
    REQUIRE(!shader_debug_source_value(snapshot, 999, documents, 2));
    REQUIRE(shader_debug_value_at(snapshot, two, documents, 2));
    REQUIRE(!shader_debug_value_at(snapshot, two, NULL, 0));
    return true;
}
TEST_CASE(helper_expansions_keep_values_and_call_chains_separate)
{
    int three = constant(3), five = constant(5), two = constant(2);
    int first = operation(SHADER_MULTIPLY, 1, 61, three, two);
    int second = operation(SHADER_MULTIPLY, 1, 62, five, two);
    origin(50, 0, "Shade", 500, 0);
    origin(51, 0, "Twice(uv.x)", 501, 50);
    origin(52, 0, "Twice(time)", 502, 50);
    origin(61, 1, "x * 2", 600, 51);
    origin(62, 1, "x * 2", 600, 52);
    result(61, first);
    result(62, second);
    result(51, first);
    result(52, second);
    shader_sample_context sample = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    const shader_debug_value *a = shader_debug_source_value(snapshot, 61, documents, 2);
    const shader_debug_value *b = shader_debug_source_value(snapshot, 62, documents, 2);
    REQUIRE(a && b && near(a->value[0], 6) && near(b->value[0], 10));
    REQUIRE(a->origin.occurrence == b->origin.occurrence);
    REQUIRE(a->origin.source_id != b->origin.source_id);
    REQUIRE(a->origin.call_site == 51 && b->origin.call_site == 52);
    REQUIRE(a->origin.document == 1 && b->origin.document == 1);
    a = shader_debug_source_value(snapshot, 51, documents, 2);
    b = shader_debug_source_value(snapshot, 52, documents, 2);
    REQUIRE(a && b && a->origin.call_site == 50 && b->origin.call_site == 50);
    REQUIRE(near(a->value[0], 6) && near(b->value[0], 10));
    const shader_debug_origin
        *call = shader_debug_origin_find(snapshot, a->origin.source_id);
    REQUIRE(call && call->call_site == 50);
    const shader_debug_origin *root = shader_debug_origin_find(snapshot, call->call_site);
    REQUIRE(root && root->source_id == 50 && root->occurrence == 500 && !root->call_site);
    REQUIRE(!shader_debug_source_value(snapshot, 50, documents, 2));
    REQUIRE(shader_debug_find(snapshot, 50, -1) == -1);
    const shader_debug_document *document = shader_debug_document_at(snapshot, root->document);
    REQUIRE(document && !strcmp(document->identity, material_key));
    REQUIRE(!strcmp(document->text, material_text));
    REQUIRE(!shader_debug_origin_find(snapshot, 999));
    shader_debug_document changed[] = { documents[0], documents[1] };
    changed[0].text = "Shade :: () {}\n";
    REQUIRE(!shader_debug_source_value(snapshot, 61, changed, 2));
    REQUIRE(!shader_debug_value_at(snapshot, first, changed, 2));
    REQUIRE(!shader_debug_source_value(snapshot, 61, &documents[1], 1));
    shader_debug_document reordered[] = { documents[1], documents[0] };
    REQUIRE(!shader_debug_source_value(snapshot, 61, reordered, 2));
    return true;
}
TEST_CASE(snapshot_owns_values_and_exact_source_revision)
{
    int node = constant(3.5f);
    graph.nodes[node].source_id = 10;
    origin(50, 0, "Shade", 500, 0);
    origin(10, 0, "gain", 100, 50);
    result(10, node);
    shader_sample_context sample = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    shader_debug_document current[] = { { material_key, material_text },
                                        { helper_key, helper_text } };
    shader_debug_document renamed[] = { { "content/materials/Renamed.chibi", material_text },
                                        { helper_key, helper_text } };
    shader_debug_document edited[] = { { material_key, "Shade :: () {}\n" },
                                       { helper_key, helper_text } };
    REQUIRE(!shader_debug_value_at(snapshot, node, renamed, 2));
    REQUIRE(!shader_debug_source_value(snapshot, 10, renamed, 2));
    REQUIRE(!shader_debug_value_at(snapshot, node, edited, 2));
    REQUIRE(!shader_debug_source_value(snapshot, 10, edited, 2));
    REQUIRE(!shader_debug_value_at(snapshot, node, NULL, 0));
    REQUIRE(!shader_debug_value_at(snapshot, -1, current, 2));
    REQUIRE(!shader_debug_value_at(snapshot, 1, current, 2));
    ((char *)documents[0].text)[0] = '?';
    ((char *)documents[0].identity)[0] = '?';
    for (int i = 0; i < 2; i++) {
        free((char *)documents[i].identity);
        free((char *)documents[i].text);
        documents[i] = (shader_debug_document){ 0 };
    }
    memset(origins, 0, sizeof origins);
    memset(results, 0, sizeof results);
    graph.nodes[node].value[0] = 0;
    shader_graph_free(&graph);
    const shader_debug_value *value = shader_debug_source_value(snapshot, 10, current, 2);
    REQUIRE(value && near(value->value[0], 3.5f));
    REQUIRE(value->origin.source_id == 10 && value->origin.occurrence == 100);
    REQUIRE(shader_debug_find(snapshot, 10, -1) == node);
    const shader_debug_origin *owned = shader_debug_origin_find(snapshot, 10);
    REQUIRE(owned && owned->occurrence == 100 && owned->call_site == 50);
    const shader_debug_origin *root = shader_debug_origin_find(snapshot, owned->call_site);
    REQUIRE(root && root->source_id == 50 && root->occurrence == 500 && !root->call_site);
    REQUIRE(!shader_debug_source_value(snapshot, 50, current, 2));
    const shader_debug_document *document = shader_debug_document_at(snapshot, root->document);
    REQUIRE(document && !strcmp(document->identity, material_key));
    REQUIRE(!strcmp(document->text, material_text));
    document = shader_debug_document_at(snapshot, 1);
    REQUIRE(document && !strcmp(document->identity, helper_key));
    REQUIRE(!strcmp(document->text, helper_text));
    REQUIRE(!shader_debug_document_at(snapshot, -1));
    REQUIRE(!shader_debug_document_at(snapshot, 2));
    return true;
}
TEST_CASE(unmapped_ir_needs_no_documents_only_when_capture_had_none)
{
    int node = constant(3);
    map.document_count = 0;
    shader_sample_context sample = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    const shader_debug_value *value = shader_debug_value_at(snapshot, node, NULL, 0);
    REQUIRE(value && value->finite && near(value->value[0], 3));
    REQUIRE(!shader_debug_value_at(snapshot, node, documents, 2));
    REQUIRE(!shader_debug_document_at(snapshot, 0));
    return true;
}
TEST_CASE(source_byte_limits_accept_boundaries_and_preserve_previous_capture)
{
    int node = constant(3);
    source_bytes = xmalloc((size_t)SHADER_DEBUG_SOURCE_BYTES_MAX + 2);
    memset(source_bytes, 'x', (size_t)SHADER_DEBUG_SOURCE_BYTES_MAX + 1);
    source_bytes[SHADER_DEBUG_SOURCE_BYTES_MAX] = 0;
    shader_debug_document document = { material_key, source_bytes };
    map.documents = &document;
    map.document_count = 1;
    shader_sample_context sample = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    const shader_debug_value *value = shader_debug_value_at(snapshot, node, &document, 1);
    REQUIRE(value && near(value->value[0], 3));
    const shader_debug_document *owned = shader_debug_document_at(snapshot, 0);
    REQUIRE(owned && strlen(owned->text) == SHADER_DEBUG_SOURCE_BYTES_MAX);
    shader_debug_snapshot *saved = snapshot;
    source_bytes[SHADER_DEBUG_SOURCE_BYTES_MAX] = 'x';
    source_bytes[SHADER_DEBUG_SOURCE_BYTES_MAX + 1] = 0;
    REQUIRE(!shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    REQUIRE(snapshot == saved && strstr(error.message, "byte limit"));
    REQUIRE(strlen(owned->text) == SHADER_DEBUG_SOURCE_BYTES_MAX);
    source_bytes[SHADER_DEBUG_SOURCE_BYTES_MAX] = 0;
    enum
    {
        CLOSURE_DOCUMENTS = SHADER_DEBUG_TOTAL_BYTES_MAX / SHADER_DEBUG_SOURCE_BYTES_MAX
    };
    shader_debug_document closure[CLOSURE_DOCUMENTS + 1];
    char keys[CLOSURE_DOCUMENTS + 1][32];
    for (int i = 0; i < CLOSURE_DOCUMENTS + 1; i++) {
        snprintf(keys[i], sizeof keys[i], "content/Library%d.chibi", i);
        closure[i] = (shader_debug_document){ keys[i], source_bytes };
    }
    map.documents = closure;
    map.document_count = CLOSURE_DOCUMENTS;
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    value = shader_debug_value_at(snapshot, node, closure, CLOSURE_DOCUMENTS);
    REQUIRE(value && near(value->value[0], 3));
    saved = snapshot;
    map.document_count++;
    REQUIRE(!shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    REQUIRE(snapshot == saved && strstr(error.message, "byte limit"));
    REQUIRE(shader_debug_value_at(snapshot, node, closure, CLOSURE_DOCUMENTS));
    return true;
}
TEST_CASE(document_identity_and_count_limits_reject_excess)
{
    int node = constant(3);
    identity_bytes = xmalloc((size_t)SHADER_DEBUG_IDENTITY_BYTES_MAX + 2);
    memset(identity_bytes, 'i', (size_t)SHADER_DEBUG_IDENTITY_BYTES_MAX + 1);
    identity_bytes[SHADER_DEBUG_IDENTITY_BYTES_MAX] = 0;
    shader_debug_document document = { identity_bytes, "3" };
    map.documents = &document;
    map.document_count = 1;
    shader_sample_context sample = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    REQUIRE(shader_debug_value_at(snapshot, node, &document, 1));
    shader_debug_snapshot *saved = snapshot;
    identity_bytes[SHADER_DEBUG_IDENTITY_BYTES_MAX] = 'i';
    identity_bytes[SHADER_DEBUG_IDENTITY_BYTES_MAX + 1] = 0;
    REQUIRE(!shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    REQUIRE(snapshot == saved && strstr(error.message, "byte limit"));
    identity_bytes[SHADER_DEBUG_IDENTITY_BYTES_MAX] = 0;
    map.document_count = SHADER_DEBUG_DOCUMENT_MAX + 1;
    REQUIRE(!shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    REQUIRE(snapshot == saved);
    REQUIRE(shader_debug_value_at(snapshot, node, &document, 1));
    return true;
}
static bool resolve_image(void *userdata, const shader_image_source *source, shader_image_view *view)
{
    if (!userdata || strcmp(source->key, "Mask.png")) {
        return false;
    }
    *view = *(const shader_image_view *)userdata;
    return true;
}
TEST_CASE(missing_image_preserves_previous_values_and_reports_source)
{
    int previous = constant(.5f);
    graph.nodes[previous].source_id = 10;
    origin(10, 0, "gain", 100, 0);
    result(10, previous);
    shader_sample_context sample = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    shader_debug_snapshot *saved = snapshot;
    shader_graph_free(&graph);
    shader_graph_init(&graph);
    map.origin_count = map.result_count = 0;
    int uv = operation(SHADER_UV, 2, 0, -1, -1);
    int image = operation(SHADER_IMAGE_TEXTURE, 3, 20, uv, -1);
    shader_image_source source = { .key = "Mask.png",
                                   .color_space = SHADER_IMAGE_LINEAR,
                                   .filter = SHADER_IMAGE_NEAREST,
                                   .wrap = SHADER_IMAGE_CLAMP };
    graph.nodes[image].image = shader_graph_add_image(&graph, &source);
    origin(20, 0, "image.Sample(uv)", 200, 0);
    result(20, image);
    sample.resolve_image = resolve_image;
    REQUIRE(!shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    REQUIRE(snapshot == saved && error.node == 20 && strstr(error.message, "Mask.png"));
    const shader_debug_value *value = shader_debug_source_value(snapshot, 10, documents, 2);
    REQUIRE(value && near(value->value[0], .5f));
    unsigned char pixels[] = { 255, 0, 0, 128 };
    shader_image_view view = { pixels, 1, 1 };
    sample.image_userdata = &view;
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    value = shader_debug_source_value(snapshot, 20, documents, 2);
    REQUIRE(value && value->width == 3 && near(value->value[0], 1));
    REQUIRE(value->value[1] == 0 && value->value[2] == 0);
    REQUIRE(shader_debug_limitations(snapshot) & SHADER_DEBUG_BASE_LEVEL_IMAGES);
    memset(pixels, 0, sizeof pixels);
    REQUIRE(near(value->value[0], 1));
    return true;
}
TEST_CASE(invalid_source_maps_cannot_replace_a_valid_snapshot)
{
    int node = constant(4);
    graph.nodes[node].source_id = 10;
    shader_sample_context sample = { 0 };
    origin(10, 0, "gain", 100, 0);
    result(10, node);
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    shader_debug_snapshot *saved = snapshot;
    shader_debug_origin valid = origins[0];
    for (int invalid = 0; invalid < 12; invalid++) {
        origins[0] = origins[1] = valid;
        results[0] = results[1] = (shader_debug_result){ 10, node };
        map.origin_count = map.result_count = 1;
        switch (invalid) {
            case 0: map.origin_count = 2; break;
            case 1: map.origin_count = map.result_count = 0; break;
            case 2: origins[0].end = (int)strlen(documents[0].text) + 1; break;
            case 3: origins[0].start = -1; break;
            case 4: origins[0].end = origins[0].start; break;
            case 5: origins[0].document = 2; break;
            case 6: origins[0].call_site = 99; break;
            case 7: origins[0].call_site = 10; break;
            case 8:
                map.origin_count = 2;
                origins[0].call_site = 11;
                origins[1].source_id = 11;
                origins[1].call_site = 10;
                break;
            case 9: results[0].source_id = 99; break;
            case 10: map.result_count = 2; break;
            case 11: results[0].node = node + 1; break;
        }
        error = (shader_error){ 0 };
        REQUIRE(!shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
        REQUIRE(snapshot == saved && error.message[0]);
        const shader_debug_value *value = shader_debug_source_value(snapshot, 10, documents, 2);
        REQUIRE(value && near(value->value[0], 4));
    }
    return true;
}
TEST_CASE(nonfinite_intermediates_remain_visible_when_output_is_finite)
{
    int large = constant(FLT_MAX), zero = constant(0);
    int gain = operation(SHADER_PARAMETER, 1, 10, -1, -1);
    graph.nodes[gain].value[0] = 2;
    int overflow = operation(SHADER_MULTIPLY, 1, 11, large, gain);
    int output = operation(SHADER_DIVIDE, 1, 0, overflow, zero);
    origin(10, 0, "gain", 100, 0);
    origin(11, 0, "uv.x * gain", 101, 0);
    result(11, overflow);
    shader_sample_context sample = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    const shader_debug_value *value = shader_debug_source_value(snapshot, 11, documents, 2);
    REQUIRE(value && !value->finite && isinf(value->value[0]));
    value = shader_debug_value_at(snapshot, output, documents, 2);
    REQUIRE(value && value->finite && value->value[0] == 0);
    REQUIRE(shader_debug_limitations(snapshot) & SHADER_DEBUG_NONFINITE_VALUE);
    float parameters[32][4] = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    value = shader_debug_source_value(snapshot, 11, documents, 2);
    REQUIRE(value && value->finite && value->value[0] == 0);
    REQUIRE(!(shader_debug_limitations(snapshot) & SHADER_DEBUG_NONFINITE_VALUE));
    return true;
}
typedef struct
{
    shader_image_view view;
    int calls;
    bool available;
} volume_resolver;
static bool resolve_volume(void *userdata, const shader_image_source *source, shader_image_view *view)
{
    volume_resolver *resolver = userdata;
    resolver->calls++;
    assert(!strcmp(source->key, "Mask.png"));
    *view = resolver->view;
    return resolver->available;
}
TEST_CASE(sampled_sdf_arithmetic_failure_is_not_a_missing_image)
{
    int position = operation(SHADER_PARAMETER, 3, 10, -1, -1);
    int two = constant(2);
    int scaled = operation(SHADER_MULTIPLY, 3, 11, position, two);
    int sdf = operation(SHADER_SAMPLED_SDF, 1, 20, scaled, -1);
    shader_node *volume = &graph.nodes[sdf];
    volume->parameter = 2;
    volume->volume_range = volume->volume_lipschitz = 1;
    volume->value[0] = volume->value[1] = volume->value[2] = 1;
    shader_image_source source = { .key = "Mask.png",
                                   .color_space = SHADER_IMAGE_LINEAR,
                                   .filter = SHADER_IMAGE_NEAREST,
                                   .wrap = SHADER_IMAGE_CLAMP };
    volume->image = shader_graph_add_image(&graph, &source);
    unsigned char pixels[8 * 4];
    for (int i = 0; i < 8; i++) {
        memcpy(pixels + i * 4, (unsigned char[]){ 255, 255, 0, 255 }, 4);
    }
    volume_resolver resolver = { .view = { pixels, 4, 2 }, .available = true };
    REQUIRE(shader_volume_validate(volume, &resolver.view, &error));
    origin(10, 0, "gain", 100, 0);
    origin(11, 0, "uv.x * gain", 101, 0);
    origin(20, 0, "image.Sample(uv)", 200, 0);
    result(20, sdf);
    shader_sample_context sample = { .resolve_image = resolve_volume,
                                     .image_userdata = &resolver };
    float parameters[32][4] = { 0 };
    REQUIRE(shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    const shader_debug_value *value = shader_debug_source_value(snapshot, 20, documents, 2);
    REQUIRE(value && value->finite && near(value->value[0], 1));
    shader_debug_snapshot *saved = snapshot;
    parameters[0][0] = FLT_MAX;
    REQUIRE(!shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    REQUIRE(snapshot == saved && resolver.calls == 2 && error.node == 20);
    if (!strstr(error.message, "nonfinite")) {
        fprintf(stderr, "Sampled SDF overflow diagnostic: %s\n", error.message);
    }
    REQUIRE(strstr(error.message, "nonfinite") && strstr(error.message, "coordinates"));
    REQUIRE(!strstr(error.message, "resolved"));
    REQUIRE(shader_debug_source_value(snapshot, 20, documents, 2) == value);
    parameters[0][0] = FLT_MAX / 2;
    REQUIRE(!shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    REQUIRE(snapshot == saved && resolver.calls == 3 && error.node == 20);
    REQUIRE(strstr(error.message, "nonfinite") && strstr(error.message, "distance"));
    parameters[0][0] = 0;
    resolver.available = false;
    REQUIRE(!shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    REQUIRE(snapshot == saved && resolver.calls == 4 && error.node == 20);
    REQUIRE(strstr(error.message, "image could not be resolved"));
    resolver.available = true;
    resolver.view.width = 3;
    REQUIRE(!shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    REQUIRE(snapshot == saved && resolver.calls == 5 && error.node == 20);
    REQUIRE(strstr(error.message, "atlas dimensions"));
    REQUIRE(shader_debug_source_value(snapshot, 20, documents, 2) == value);
    REQUIRE(value->finite && near(value->value[0], 1));
    resolver.view.width = 4;
    REQUIRE(shader_debug_capture(&graph, &sample, parameters, &map, &snapshot, &error));
    value = shader_debug_source_value(snapshot, 20, documents, 2);
    REQUIRE(value && value->finite && near(value->value[0], 1) && resolver.calls == 6);
    return true;
}
TEST_CASE(bump_and_context_effects_declare_their_cpu_limits)
{
    int position = operation(SHADER_PART_POSITION, 3, 0, -1, -1);
    int height = operation(SHADER_X, 1, 0, position, -1);
    int strength = constant(1), distance = constant(.001f);
    int normal = operation(SHADER_NORMAL, 3, 0, -1, -1);
    int bump = operation(SHADER_BUMP, 3, 20, height, strength);
    graph.nodes[bump].input[2] = distance;
    graph.nodes[bump].input[3] = normal;
    int density = constant(0), brightness = constant(1);
    int star = operation(SHADER_STAR_FIELD, 3, 21, position, density);
    graph.nodes[star].input[2] = brightness;
    int influence = operation(SHADER_SURFACE_INFLUENCE, 3, 22, position, -1);
    graph.output_count = 3;
    graph.output[0] = bump;
    graph.output[1] = star;
    graph.output[2] = influence;
    origin(20, 0, "Bump(phase)", 200, 0);
    origin(21, 0, "StarField(uv)", 201, 0);
    origin(22, 0, "Influence(uv)", 202, 0);
    result(20, bump);
    shader_sample_context sample = { .normal = { 0, 1, 0 } };
    for (int i = 0; i < 3; i++) {
        sample.part_from_world[i][i] = 1;
    }
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    const shader_debug_value *value = shader_debug_source_value(snapshot, 20, documents, 2);
    REQUIRE(value && near(value->value[0], -sqrtf(.5f)) && near(value->value[1], sqrtf(.5f)));
    unsigned flags = SHADER_DEBUG_FLOAT_APPROXIMATION |
                     SHADER_DEBUG_COORDINATE_GRADIENTS |
                     SHADER_DEBUG_REFERENCE_FOOTPRINT | SHADER_DEBUG_SCENE_INFLUENCE;
    REQUIRE(shader_debug_limitations(snapshot) == flags);
    return true;
}
TEST_CASE(decal_facing_declares_coordinate_gradient_dependency)
{
    int position = operation(SHADER_PART_POSITION, 3, 0, -1, -1);
    int half = operation(SHADER_CONSTANT, 3, 0, -1, -1);
    for (int i = 0; i < 3; i++) {
        graph.nodes[half].value[i] = 1;
    }
    int feather = constant(0), facing = constant(.5f);
    int decal = operation(SHADER_DECAL_MASK, 1, 20, position, half);
    graph.nodes[decal].input[2] = feather;
    graph.nodes[decal].input[3] = facing;
    origin(20, 0, "Decal(uv)", 200, 0);
    result(20, decal);
    shader_sample_context sample = { .normal = { 0, 0, 1 } };
    for (int i = 0; i < 3; i++) {
        sample.part_from_world[i][i] = 1;
    }
    REQUIRE(shader_debug_capture(&graph, &sample, NULL, &map, &snapshot, &error));
    const shader_debug_value *value = shader_debug_source_value(snapshot, 20, documents, 2);
    REQUIRE(value && value->value[0] == 1);
    REQUIRE(shader_debug_limitations(snapshot) & SHADER_DEBUG_COORDINATE_GRADIENTS);
    return true;
}
void test_tool_shader_source_debug()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(uv_time_and_uniform_values_follow_one_sample);
    RUN_TEST_CASE(source_results_select_principal_nodes_and_zero_node_aliases);
    RUN_TEST_CASE(helper_expansions_keep_values_and_call_chains_separate);
    RUN_TEST_CASE(snapshot_owns_values_and_exact_source_revision);
    RUN_TEST_CASE(unmapped_ir_needs_no_documents_only_when_capture_had_none);
    RUN_TEST_CASE(source_byte_limits_accept_boundaries_and_preserve_previous_capture);
    RUN_TEST_CASE(document_identity_and_count_limits_reject_excess);
    RUN_TEST_CASE(missing_image_preserves_previous_values_and_reports_source);
    RUN_TEST_CASE(invalid_source_maps_cannot_replace_a_valid_snapshot);
    RUN_TEST_CASE(nonfinite_intermediates_remain_visible_when_output_is_finite);
    RUN_TEST_CASE(sampled_sdf_arithmetic_failure_is_not_a_missing_image);
    RUN_TEST_CASE(bump_and_context_effects_declare_their_cpu_limits);
    RUN_TEST_CASE(decal_facing_declares_coordinate_gradient_dependency);
    pu_setup(NULL, NULL);
}
