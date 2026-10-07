#include "assets/code_query.h"

#include "support/asset_json.h"

#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

void run_code_query_checks()
{
    const char
        *text = "all(has(\"GAME.GameTransform\"), "
                "any(has(\"GAME.GameVelocity\"), "
                "has(\"GAME.GameBody\")), not(has(\"GAME.GameCollider\")));";
    const char
        *expected = "{\"op\":\"all\",\"terms\":["
                    "{\"op\":\"has\",\"component\":\"GAME.GameTransform\"},"
                    "{\"op\":\"any\",\"terms\":["
                    "{\"op\":\"has\",\"component\":\"GAME.GameVelocity\"},"
                    "{\"op\":\"has\",\"component\":\"GAME.GameBody\"}]},"
                    "{\"op\":\"not\",\"terms\":["
                    "{\"op\":\"has\",\"component\":\"GAME.GameCollider\"}]}]}";
    av_doc *doc = av_doc_new(), *baseline = av_json_read(expected);
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token token = code_lex_next(&lexer);
    char error[256] = { 0 };
    av_value *query = code_query_parse(doc, &lexer, &token, error, sizeof error);
    test_check(query && av_equals(query, av_doc_root(baseline)), "Readable query preserves nested selection operators and exact component names");
    test_check(code_token_is(&lexer, token, ";"), "Query parser leaves the enclosing declaration terminator unconsumed");
    char *printed = query ? code_query_write(query) : NULL;
    test_check(
        printed && strlen(printed) == strlen(text) - 1 &&
            !strncmp(printed, text, strlen(text) - 1),
        "Query printer emits readable source with the original boolean grouping"
    );
    free(printed);
    av_doc_free(baseline);
    av_doc_free(doc);

    const char *invalid[] = { "not()",
                              "not(has(\"A\"), has(\"B\"))",
                              "has()",
                              "has(\"A\", \"B\")",
                              "all(has(\"A\") has(\"B\"))",
                              "any(has(\"A\"),)",
                              "unknown()",
                              "has(\"\")",
                              "has(\"A\\u0000B\")",
                              "has(\"A\\q\")" };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        doc = av_doc_new();
        code_lex_init(&lexer, invalid[i]);
        token = code_lex_next(&lexer);
        error[0] = 0;
        test_check(!code_query_parse(doc, &lexer, &token, error, sizeof error) && error[0], "Malformed query fails explicitly instead of changing entity selection");
        av_doc_free(doc);
    }
}
