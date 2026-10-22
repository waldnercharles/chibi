#include "support/asset_document.h"

#include "third_party/pico_headers/pico_unit.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Compiled asset values retain ownership and exact scalar serialization. */
static av_doc *doc;
static char *text;

static void cleanup()
{
    av_doc_free(doc);
    doc = NULL;
    free(text);
    text = NULL;
}

static bool writes_back(av_value *value, double *read)
{
    char error[256];
    char *source = asset_document_write(value);
    av_doc *loaded = asset_document_read(source, error, sizeof error);
    bool same = loaded && av_equals(value, av_doc_root(loaded));
    *read = loaded ? av_get_num(av_doc_root(loaded)) : 0;
    av_doc_free(loaded);
    free(source);
    return same;
}

TEST_CASE(asset_document_owns_each_property_name)
{
    char error[256];
    for (int i = 0; i < 16; i++) {
        doc = av_doc_new();
        av_value *root = av_obj(doc), *node = av_obj_add_obj(doc, root, "node");
        av_doc_set_root(doc, root);
        av_obj_add_int(doc, node, "first", 1);
        av_obj_add_int(doc, node, "second", 2);
        av_obj_add_int(doc, node, "third", 3);
        text = asset_document_write(root);
        av_doc *loaded = asset_document_read(text, error, sizeof error);
        REQUIRE(loaded);
        av_value *copy = av_value_copy(av_doc_root(loaded));
        av_doc_free(loaded);
        cleanup();
        node = av_obj_get(copy, "node");
        bool owned = av_obj_size(node) == 3 &&
                     av_get_int(av_obj_get(node, "first")) == 1 &&
                     av_get_int(av_obj_get(node, "second")) == 2 &&
                     av_get_int(av_obj_get(node, "third")) == 3;
        av_value_free(copy);
        REQUIRE(owned);
    }
    return true;
}

TEST_CASE(asset_document_reads_typed_values_and_writes_them_canonically)
{
    doc = av_doc_new();
    av_value *root = av_obj(doc), *object = av_obj_add_obj(doc, root, "object");
    av_doc_set_root(doc, root);
    av_obj_set(doc, root, "maximum", av_uint(doc, UINT64_MAX));
    av_obj_set(doc, root, "minimum", av_sint(doc, INT64_MIN));
    av_obj_set(doc, root, "precise", av_real(doc, 0.1234567890123456789));
    av_obj_add_bool(doc, root, "flag", true);
    av_obj_set(doc, root, "missing", av_null(doc));
    av_obj_add_str(doc, object, "label", "hello");
    av_obj_add_arr(doc, object, "empty");
    av_value *children = av_obj_add_arr(doc, object, "children");
    av_obj_add_int(doc, av_arr_add_obj(doc, children), "id", 1);
    av_obj_add_int(doc, av_arr_add_obj(doc, children), "id", 2);
    char error[256];
    text = asset_document_write(root);
    REQUIRE(text && strstr(text, "package asset") && strstr(text, "pub Build"));
    av_doc *loaded = asset_document_read(text, error, sizeof error);
    REQUIRE(loaded && av_equals(root, av_doc_root(loaded)));
    char *again = asset_document_write(av_doc_root(loaded));
    REQUIRE(again && !strcmp(text, again));
    free(again);
    av_doc_free(loaded);
    double unused;
    REQUIRE(writes_back(children, &unused));
    cleanup();
    return true;
}

TEST_CASE(asset_document_reals_round_trip_exactly)
{
    const double reals[] = { -1.20935707681886e86,
                             0.1,
                             0.2,
                             0.66,
                             0.2000000000001,
                             nextafter(0.2, 1),
                             1.2345678901234567,
                             0x1p-1074,
                             DBL_MAX,
                             -0.0 };
    for (size_t i = 0; i < sizeof reals / sizeof *reals; i++) {
        doc = av_doc_new();
        double read;
        REQUIRE(writes_back(av_real(doc, reals[i]), &read));
        REQUIRE(read == reals[i] && signbit(read) == signbit(reals[i]));
        cleanup();
    }
    return true;
}

TEST_CASE(asset_document_floats_keep_their_bits_through_copies)
{
    const float floats[] = { 0.2f,       0.66f,        nextafterf(0.2f, 1),
                             1.2345678f, FLT_TRUE_MIN, FLT_MAX,
                             -0.0f };
    for (size_t i = 0; i < sizeof floats / sizeof *floats; i++) {
        doc = av_doc_new();
        av_value *copy = av_value_copy(av_float(doc, floats[i]));
        double read;
        bool same = writes_back(copy, &read);
        av_value_free(copy);
        REQUIRE(same && (float)read == floats[i]);
        REQUIRE(signbit((float)read) == signbit(floats[i]));
        cleanup();
    }
    return true;
}

TEST_CASE(asset_document_strings_escape_separators_and_decode_scalars)
{
    doc = av_doc_new();
    double unused;
    REQUIRE(writes_back(
        av_str(
            doc,
            "line\xe2\x80\xa8"
            "break\xe2\x80\xa9"
            "end"
        ),
        &unused
    ));
    REQUIRE(writes_back(av_str(doc, "hello\nworld\"\\"), &unused));
    cleanup();
    return true;
}

void test_asset_document()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(asset_document_owns_each_property_name);
    RUN_TEST_CASE(asset_document_reads_typed_values_and_writes_them_canonically);
    RUN_TEST_CASE(asset_document_reals_round_trip_exactly);
    RUN_TEST_CASE(asset_document_floats_keep_their_bits_through_copies);
    RUN_TEST_CASE(asset_document_strings_escape_separators_and_decode_scalars);
    pu_clear_setup();
}
