#include "support/asset_text.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

static bool source_equals(const char *text, av_value *value, const char *literal)
{
    av_source source = av_get_source(value);
    return source.end - source.start == strlen(literal) &&
           !memcmp(text + source.start, literal, strlen(literal));
}

void run_asset_text_checks()
{
    char error[256];
    av_doc *properties = av_doc_read("node first=1 second=2 third=3\n", error, sizeof error);
    test_check(
        properties && av_obj_size(av_obj_get(av_doc_root(properties), "node")) == 3 &&
            av_get_uint(av_obj_get(av_obj_get(av_doc_root(properties), "node"), "third")) == 3,
        "KDL parser owns each property name until the next event"
    );
    av_doc_free(properties);

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
    av_doc *doc = av_doc_read(source, error, sizeof error);
    test_check(doc != NULL, "KDL2 parses typed assets, raw strings, comments and child lists");
    if (doc) {
        av_value *root = av_doc_root(doc), *object = av_obj_get(root, "object");
        test_check(
            av_get_uint(av_obj_get(root, "maximum")) == UINT64_MAX &&
                av_get_sint(av_obj_get(root, "minimum")) == INT64_MIN,
            "KDL preserves the full signed and unsigned 64-bit ranges"
        );
        test_check(
            av_get_uint(av_obj_get(root, "hex")) == 255 &&
                av_get_bool(av_obj_get(root, "flag")) &&
                av_is_null(av_obj_get(root, "missing")) &&
                !av_obj_get(root, "discarded"),
            "KDL2 hex and keyword scalars retain types and slashdash omits "
            "nodes"
        );
        test_check(
            av_arr_size(av_obj_get(object, "empty")) == 0 &&
                av_arr_size(av_obj_get(object, "one")) == 1 &&
                av_arr_size(av_obj_get(object, "children")) == 2,
            "KDL annotations distinguish empty, singleton and object arrays"
        );
        test_check(
            source_equals(source, av_obj_get(root, "maximum"), "18_446_744_073_709_551_615") &&
                source_equals(source, av_obj_get(root, "raw"), "#\"a \\\" b\"#") &&
                source_equals(source, av_obj_get(object, "label"), "\"hello\""),
            "KDL source ranges include exact number and string delimiters"
        );
        char *written = av_doc_write(doc);
        av_doc *loaded = written ? av_doc_read(written, error, sizeof error) : NULL;
        test_check(loaded && av_equals(root, av_doc_root(loaded)), "Canonical KDL roundtrip preserves all asset values and collection shapes");
        av_doc_free(loaded);
        free(written);
        av_value *children = av_obj_get(object, "children");
        char *array_text = av_value_write(children);
        av_doc *array = array_text ? av_value_read(array_text, error, sizeof error)
                                   : NULL;
        test_check(array && av_equals(children, av_doc_root(array)), "Source editing roundtrips a native array value without JSON");
        av_doc_free(array);
        free(array_text);
        av_doc_free(doc);
    }
    const double real_values[] = { -1.20935707681886e86,
                                   0.1,
                                   0.2,
                                   0.66,
                                   0.2000000000001,
                                   nextafter(0.2, 1),
                                   1.2345678901234567,
                                   0x1p-1074,
                                   DBL_MAX,
                                   -0.0 };
    for (size_t i = 0; i < sizeof real_values / sizeof real_values[0]; i++) {
        av_doc *real_doc = av_doc_new();
        av_value *real = av_real(real_doc, real_values[i]);
        char *literal = av_value_write(real);
        av_doc *loaded = av_value_read(literal, error, sizeof error);
        test_check(
            loaded && av_get_num(av_doc_root(loaded)) == real_values[i] &&
                signbit(av_get_num(av_doc_root(loaded))) == signbit(real_values[i]),
            "KDL decimal emission roundtrips doubles exactly including "
            "adjacent, subnormal and signed-zero values"
        );
        av_doc_free(loaded);
        free(literal);
        av_doc_free(real_doc);
    }
    const struct
    {
        double value;
        const char *literal;
    } readable_reals[] = { { 80, "80.0" },
                           { 320, "320.0" },
                           { 0.2, "0.2" },
                           { 0.0001, "0.0001" },
                           { 1e15, "1000000000000000.0" },
                           { 1e-5, "1e-05" },
                           { 1e16, "1e+16" } };
    for (size_t i = 0; i < sizeof readable_reals / sizeof readable_reals[0]; i++) {
        av_doc *real_doc = av_doc_new();
        char *literal = av_value_write(av_real(real_doc, readable_reals[i].value));
        test_check(!strcmp(literal, readable_reals[i].literal), "KDL keeps ordinary quantities in fixed decimal and extreme magnitudes compact");
        av_doc *loaded = av_value_read(literal, error, sizeof error);
        test_check(
            loaded && av_get_num(av_doc_root(loaded)) == readable_reals[i].value,
            "KDL notation boundaries preserve exact numeric values"
        );
        av_doc_free(loaded);
        free(literal);
        av_doc_free(real_doc);
    }
    const float float_values[] = {
        0.2f,    0.66f, nextafterf(0.2f, 1), 1.2345678f, FLT_TRUE_MIN,
        FLT_MAX, -0.0f
    };
    for (size_t i = 0; i < sizeof float_values / sizeof float_values[0]; i++) {
        av_doc *float_doc = av_doc_new();
        av_value *value = av_float(float_doc, float_values[i]);
        av_value *copy = av_value_copy(value);
        char *literal = av_value_write(copy);
        av_doc *loaded = av_value_read(literal, error, sizeof error);
        float parsed = loaded ? (float)av_get_num(av_doc_root(loaded)) : 0;
        test_check(
            loaded && parsed == float_values[i] &&
                signbit(parsed) == signbit(float_values[i]) &&
                av_equals(value, av_doc_root(loaded)),
            "KDL float emission preserves native float bits and canonical "
            "value equality through cloning"
        );
        if (i == 0) {
            test_check(!strcmp(literal, "0.2"), "a native float saves as 0.2 without promotion noise");
        }
        av_doc_free(loaded);
        free(literal);
        av_value_free(copy);
        av_doc_free(float_doc);
    }
    doc = av_doc_read("bloom 0.20000000000000001\nfog 0.66000000000000003\n", error, sizeof error);
    char *clean = doc ? av_doc_write(doc) : NULL;
    test_check(clean && !strcmp(clean, "bloom 0.2\nfog 0.66\n"), "canonical KDL removes redundant digits from existing authored bloom and fog values");
    free(clean);
    av_doc_free(doc);
    av_doc *unicode_doc = av_doc_new();
    av_value *unicode = av_str(
        unicode_doc,
        "line\xe2\x80\xa8"
        "break\xe2\x80\xa9"
        "end"
    );
    char *unicode_literal = av_value_write(unicode);
    av_doc *unicode_loaded = av_value_read(unicode_literal, error, sizeof error);
    test_check(
        unicode_loaded && av_equals(unicode, av_doc_root(unicode_loaded)),
        "KDL emitter escapes Unicode line separators in single-line strings"
    );
    av_doc_free(unicode_loaded);
    free(unicode_literal);
    av_doc_free(unicode_doc);

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
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        doc = av_doc_read(invalid[i], error, sizeof error);
        test_check(
            !doc && strstr(error, "line ") && strstr(error, "column "),
            "Invalid KDL asset shape or numeric value reports a source location"
        );
        av_doc_free(doc);
    }
    bool owned_properties = true;
    for (int i = 0; i < 16; i++) {
        doc = av_doc_read("n a=1 b=2\n", error, sizeof error);
        if (!doc) {
            owned_properties = false;
            break;
        }
        av_value *copy = av_value_copy(av_doc_root(doc));
        av_doc_free(doc);
        av_value *node = av_obj_get(copy, "n");
        owned_properties &= av_get_int(av_obj_get(node, "a")) == 1 &&
                            av_get_int(av_obj_get(node, "b")) == 2;
        av_value_free(copy);
    }
    test_check(owned_properties, "Owned KDL parses release transferred property names and preserve copied values");
    doc = av_value_read("\"hello\\nworld\"", error, sizeof error);
    test_check(
        doc && av_is_str(av_doc_root(doc)) &&
            !strcmp(av_get_str(av_doc_root(doc)), "hello\nworld"),
        "Source editor decodes an isolated KDL scalar literal"
    );
    av_doc_free(doc);
}
