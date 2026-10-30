#include "support/asset_text.h"

#include "third_party/pico_headers/pico_unit.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* KDL asset text: parsing, canonical writing and exact numeric round trips. */

static av_doc *doc;
static char *text;

static void cleanup()
{
    av_doc_free(doc);
    doc = NULL;
    free(text);
    text = NULL;
}

static bool source_equals(const char *source, av_value *value, const char *literal)
{
    av_source range = av_get_source(value);
    return range.end - range.start == strlen(literal) &&
           !memcmp(source + range.start, literal, strlen(literal));
}

/* A literal written for `value` reads back as an equal value. */
static bool writes_back(av_value *value, double *read)
{
    char error[256];
    char *literal = av_value_write(value);
    av_doc *loaded = av_value_read(literal, error, sizeof error);
    bool same = loaded && av_equals(value, av_doc_root(loaded));
    *read = loaded ? av_get_num(av_doc_root(loaded)) : 0;
    av_doc_free(loaded);
    free(literal);
    return same;
}

TEST_CASE(kdl_parser_owns_each_property_name)
{
    char error[256];
    doc = av_doc_read("node first=1 second=2 third=3\n", error, sizeof error);
    REQUIRE(doc);
    av_value *node = av_obj_get(av_doc_root(doc), "node");
    REQUIRE(av_obj_size(node) == 3 && av_get_uint(av_obj_get(node, "third")) == 3);
    cleanup();
    for (int i = 0; i < 16; i++) {
        doc = av_doc_read("n a=1 b=2\n", error, sizeof error);
        REQUIRE(doc);
        av_value *copy = av_value_copy(av_doc_root(doc));
        cleanup();
        av_value *n = av_obj_get(copy, "n");
        bool owned = av_get_int(av_obj_get(n, "a")) == 1 &&
                     av_get_int(av_obj_get(n, "b")) == 2;
        av_value_free(copy);
        REQUIRE(owned);
    }
    return true;
}

TEST_CASE(kdl_reads_typed_values_and_writes_them_canonically)
{
    const char
        *source = "// Authored comments are accepted without becoming values.\n"
                  "maximum 18_446_744_073_709_551_615\n"
                  "minimum -9223372036854775808\n"
                  "precise 0.1234567890123456789\n"
                  "hex 0xff\n"
                  "flag #true\n"
                  "missing #null\n"
                  "raw #\"a \\\" b\"#\n"
                  "/- discarded 10\n"
                  "object label=\"hello\" count=7 {\n"
                  "    (array)empty\n"
                  "    (array)one 2\n"
                  "    (array)children {\n"
                  "        - id=1 name=\"first\"\n"
                  "        - id=2 name=\"second\"\n"
                  "    }\n"
                  "}\n";
    char error[256];
    doc = av_doc_read(source, error, sizeof error);
    REQUIRE(doc);
    av_value *root = av_doc_root(doc), *object = av_obj_get(root, "object");
    REQUIRE(av_get_uint(av_obj_get(root, "maximum")) == UINT64_MAX);
    REQUIRE(av_get_sint(av_obj_get(root, "minimum")) == INT64_MIN);
    REQUIRE(av_get_uint(av_obj_get(root, "hex")) == 255);
    REQUIRE(av_get_bool(av_obj_get(root, "flag")));
    REQUIRE(av_is_null(av_obj_get(root, "missing")));
    REQUIRE(!av_obj_get(root, "discarded"));
    REQUIRE(av_arr_size(av_obj_get(object, "empty")) == 0);
    REQUIRE(av_arr_size(av_obj_get(object, "one")) == 1);
    REQUIRE(av_arr_size(av_obj_get(object, "children")) == 2);
    /* Source ranges include exact number and string delimiters. */
    REQUIRE(source_equals(source, av_obj_get(root, "maximum"), "18_446_744_073_709_551_615"));
    REQUIRE(source_equals(source, av_obj_get(root, "raw"), "#\"a \\\" b\"#"));
    REQUIRE(source_equals(source, av_obj_get(object, "label"), "\"hello\""));

    text = av_doc_write(doc);
    av_doc *loaded = av_doc_read(text, error, sizeof error);
    bool same = loaded && av_equals(root, av_doc_root(loaded));
    av_doc_free(loaded);
    REQUIRE(same);
    /* Source editing round-trips one array value without a document. */
    double unused;
    REQUIRE(writes_back(av_obj_get(object, "children"), &unused));
    cleanup();
    return true;
}

TEST_CASE(kdl_reals_round_trip_exactly_and_read_plainly)
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
    /* Ordinary quantities stay fixed decimal; extreme magnitudes go compact. */
    const struct
    {
        double value;
        const char *literal;
    } readable[] = { { 80, "80.0" },
                     { 320, "320.0" },
                     { 0.2, "0.2" },
                     { 0.0001, "0.0001" },
                     { 1e15, "1000000000000000.0" },
                     { 1e-5, "1e-05" },
                     { 1e16, "1e+16" } };
    for (size_t i = 0; i < sizeof readable / sizeof *readable; i++) {
        doc = av_doc_new();
        text = av_value_write(av_real(doc, readable[i].value));
        REQUIRE(!strcmp(text, readable[i].literal));
        double read;
        REQUIRE(writes_back(av_real(doc, readable[i].value), &read));
        REQUIRE(read == readable[i].value);
        cleanup();
    }
    return true;
}

TEST_CASE(kdl_floats_keep_their_bits_through_copies)
{
    const float floats[] = { 0.2f,       0.66f,        nextafterf(0.2f, 1),
                             1.2345678f, FLT_TRUE_MIN, FLT_MAX,
                             -0.0f };
    for (size_t i = 0; i < sizeof floats / sizeof *floats; i++) {
        doc = av_doc_new();
        av_value *copy = av_value_copy(av_float(doc, floats[i]));
        double read;
        bool same = writes_back(copy, &read);
        text = av_value_write(copy);
        av_value_free(copy);
        REQUIRE(same && (float)read == floats[i]);
        REQUIRE(signbit((float)read) == signbit(floats[i]));
        /* A native float saves without promotion noise. */
        REQUIRE(i || !strcmp(text, "0.2"));
        cleanup();
    }
    char error[256];
    doc = av_doc_read("bloom 0.20000000000000001\nfog 0.66000000000000003\n", error, sizeof error);
    REQUIRE(doc);
    text = av_doc_write(doc);
    REQUIRE(!strcmp(text, "bloom 0.2\nfog 0.66\n"));
    cleanup();
    return true;
}

TEST_CASE(kdl_strings_escape_separators_and_decode_scalars)
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
    cleanup();
    char error[256];
    doc = av_value_read("\"hello\\nworld\"", error, sizeof error);
    REQUIRE(doc && av_is_str(av_doc_root(doc)));
    REQUIRE(!strcmp(av_get_str(av_doc_root(doc)), "hello\nworld"));
    cleanup();
    return true;
}

TEST_CASE(kdl_rejects_invalid_shapes_with_a_location)
{
    const char *invalid[] = { "a 1\na 2\n",
                              "a x=1 x=2\n",
                              "a 1 x=2\n",
                              "a 1 { b 2; }\n",
                              "(array)a x=1\n",
                              "(array)a { child 1; }\n",
                              "a 1 2\n",
                              "a 18446744073709551616\n",
                              "a -9223372036854775809\n",
                              "a #inf\n",
                              "a #nan\n",
                              "a true\ninvalid [\n",
                              "a (unsupported)2\n" };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        char error[256] = "";
        doc = av_doc_read(invalid[i], error, sizeof error);
        REQUIRE(!doc && strstr(error, "line ") && strstr(error, "column "));
    }
    return true;
}

void test_asset_text()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(kdl_parser_owns_each_property_name);
    RUN_TEST_CASE(kdl_reads_typed_values_and_writes_them_canonically);
    RUN_TEST_CASE(kdl_reals_round_trip_exactly_and_read_plainly);
    RUN_TEST_CASE(kdl_floats_keep_their_bits_through_copies);
    RUN_TEST_CASE(kdl_strings_escape_separators_and_decode_scalars);
    RUN_TEST_CASE(kdl_rejects_invalid_shapes_with_a_location);
    pu_clear_setup();
}
