#include "assets/code_value.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

void run_code_value_checks()
{
    char error[256];
    av_doc *doc = code_value_read(
        "{position: {x: -0.0, y: 1.25}, active: true, labels: [\"tick\\n\", "
        "\"\\uD83D\\uDE80\"], tint: 0xff00ff00, minimum: -9223372036854775808}",
        error,
        sizeof error
    );
    test_check(doc != NULL, "Behavior struct and array literals parse independently of KDL documents");
    if (doc) {
        av_value *root = av_doc_root(doc);
        test_check(
            av_get_bool(av_obj_get(root, "active")) &&
                signbit(av_get_num(av_obj_get(av_obj_get(root, "position"), "x"))) &&
                av_get_uint(av_obj_get(root, "tint")) == 0xff00ff00 &&
                av_get_sint(av_obj_get(root, "minimum")) == INT64_MIN,
            "Literal decoding preserves boolean, signed zero, color and "
            "integer boundary values"
        );
        test_check(
            !strcmp(av_get_str(av_arr_get(av_obj_get(root, "labels"), 1)), "🚀"),
            "Behavior strings decode supplementary Unicode characters"
        );
        char *source = code_value_write(root);
        av_doc *reopened = code_value_read(source, error, sizeof error);
        test_check(
            reopened && av_equals_ordered(root, av_doc_root(reopened)),
            "Canonical literals retain nested values and ordered struct fields"
        );
        free(source);
        av_doc_free(reopened);
        av_doc_free(doc);
    }
    const char *invalid[] = { "asset {x = 1;}",
                              "{x = 1;}",
                              "{x: 1, x: 2}",
                              "#true",
                              "has(\"A\")",
                              "[1,,2]",
                              "\"\\uD800\"",
                              "\"\\uDFFF\"",
                              "\"\\q\"",
                              "18446744073709551616",
                              "-9223372036854775809",
                              "1e9999" };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        doc = code_value_read(invalid[i], error, sizeof error);
        test_check(!doc && error[0], "Malformed literals and document grammars fail instead of becoming executable values");
        av_doc_free(doc);
    }
}
