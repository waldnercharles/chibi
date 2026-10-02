#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_text.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void integer_spelling(char *json, const char *decimal)
{
    size_t length = strlen(decimal);
    char *value = strstr(json, decimal);
    assert(value);
    while (value) {
        memmove(value + length - 2, value + length, strlen(value + length) + 1);
        value = strstr(value, decimal);
    }
}

/* Exercise both precision contracts and the canonical parser/writer, rather
 * than comparing formatted text with a copy of the formatting algorithm. */
static void numeric_roundtrip(double value, bool single)
{
    av_doc *doc = av_doc_new();
    av_value *original = single ? av_float(doc, (float)value) : av_real(doc, value);
    char *text = av_value_write(original);
    char error[128];
    av_doc *loaded = av_value_read(text, error, sizeof error);
    assert(loaded);
    double parsed = av_get_num(av_doc_root(loaded));
    if (single) {
        float source = (float)value, result = (float)parsed;
        assert(!memcmp(&source, &result, sizeof source));
    } else {
        assert(!memcmp(&value, &parsed, sizeof value));
    }
    char *again = av_value_write(av_doc_root(loaded));
    assert(!strcmp(text, again));
    free(again);
    av_doc_free(loaded);
    free(text);
    av_doc_free(doc);
}

static void numeric_presentation()
{
    const float singles[] = { 0,
                              -0.0f,
                              0.1f,
                              0.035f,
                              FLT_MIN,
                              FLT_MAX,
                              0x1p-149f,
                              0x1.fffffep-127f,
                              1.00000011920928955078125f,
                              16777216 };
    const double doubles[] = { 0,
                               -0.0,
                               0.1,
                               DBL_MIN,
                               DBL_MAX,
                               0x1p-1074,
                               0x1.fffffffffffffp-1023,
                               1.0000000000000002,
                               9007199254740992 };
    for (size_t i = 0; i < sizeof singles / sizeof *singles; i++) {
        numeric_roundtrip(singles[i], true);
    }
    for (size_t i = 0; i < sizeof doubles / sizeof *doubles; i++) {
        numeric_roundtrip(doubles[i], false);
    }
    uint64_t state = UINT64_C(0x243f6a8885a308d3);
    for (int i = 0; i < 10000; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        uint32_t bits = (uint32_t)state;
        float single;
        double real;
        memcpy(&single, &bits, sizeof single);
        memcpy(&real, &state, sizeof real);
        if (isfinite(single)) {
            numeric_roundtrip(single, true);
        }
        if (isfinite(real)) {
            numeric_roundtrip(real, false);
        }
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_set(doc, root, "single", av_float(doc, 0.035f));
    av_obj_set(doc, root, "real", av_real(doc, 0.1));
    av_obj_set(doc, root, "color", av_color(doc, 0x0039e8ff));
    av_obj_set(doc, root, "maximum", av_uint(doc, UINT64_MAX));
    av_obj_set(doc, root, "heximum", av_hex(doc, UINT64_MAX));
    char *text = av_doc_write(doc);
    assert(strstr(text, "single 0.035\n"));
    assert(strstr(text, "real 0.1\n"));
    assert(strstr(text, "color 0x0039e8ff\n"));
    assert(strstr(text, "maximum 18446744073709551615\n"));
    assert(strstr(text, "heximum 0xffffffffffffffff\n"));
    char error[128];
    av_doc *loaded = av_doc_read(text, error, sizeof error);
    assert(loaded);
    assert(av_get_uint(av_obj_get(av_doc_root(loaded), "maximum")) == UINT64_MAX);
    assert(av_get_uint(av_obj_get(av_doc_root(loaded), "heximum")) == UINT64_MAX);
    char *again = asset_text_to_disk(text, error, sizeof error);
    assert(again && !strcmp(text, again));
    free(again);
    free(text);
    av_doc_free(loaded);
    av_doc_free(doc);
}

void test_number_serialization()
{
    numeric_presentation();
    graph original;
    graph_init(&original);
    node *single = graph_add_node(&original, registry_node_by_name("Add"));
    node *real = graph_add_node(&original, registry_node_by_name("Real"));
    single->x = 123;
    single->y = -456;
    pin_value f = { .type = PIN_F32, .f = 123 };
    pin_value d = { .type = PIN_F64, .d = -456 };
    assert(graph_set_pin_default(&original, single, 0, 0, &f) == 0);
    assert(graph_set_pin_default(&original, real, 0, 0, &d) == 0);
    char *json = graph_save(&original);
    assert(json);
    integer_spelling(json, "123.0");
    integer_spelling(json, "-456.0");
    graph *loaded = graph_load(json);
    assert(loaded && acount(loaded->nodes) == 2);
    assert(loaded->nodes[0]->x == 123 && loaded->nodes[0]->y == -456);
    arena memory;
    arena_init(&memory, 256);
    eval_context context;
    eval_context_init(&context);
    pin_value result;
    assert(graph_eval_output(loaded, loaded->nodes[0], 0, &memory, &result, &context) == 0);
    assert(result.type == PIN_F32 && result.f == 123);
    assert(graph_eval_output(loaded, loaded->nodes[1], 0, &memory, &result, &context) == 0);
    assert(result.type == PIN_F64 && result.d == -456);
    eval_context_free(&context);
    arena_free(&memory);
    graph_free(loaded);
    free(loaded);
    graph_free(&original);
    free(json);
    puts("PASS: integer KDL numbers retain float defaults and graph coordinates");
}
