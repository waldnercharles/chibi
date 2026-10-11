#include "third_party/fmt/fmt.h"

#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

/* The vendored fmt library (third_party/fmt) through its public fmt_printd
 * API into a fmt_stream, plus its two local fixes: an empty result still gets
 * a buffer, and an out-of-range placeholder renders empty when its error code
 * is ignored, which Format Text relies on. */

static fmt_stream stream;

static void cleanup()
{
    fmt_close(&stream);
    stream = (fmt_stream){ 0 };
}

/* Whether the stream holds `expected`; empties it for the next print. */
static bool printed(const char *expected)
{
    bool same = stream.data && !strcmp(stream.data, expected);
    cleanup();
    return same;
}

TEST_CASE(fmt_substitutes_positional_and_typed_arguments)
{
    fmt_printd(&stream, "One arg: {}", 42);
    REQUIRE(printed("One arg: 42"));
    fmt_printd(&stream, "{} {} {}", "a", "b", "c");
    REQUIRE(printed("a b c"));
    fmt_printd(&stream, "{} {} {} {} {} {} {} {} {} {}", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    REQUIRE(printed("1 2 3 4 5 6 7 8 9 10"));
    fmt_printd(&stream, "{1} {0}", "first", "second");
    REQUIRE(printed("second first"));
    fmt_printd(&stream, "{0} {0} {0}", "repeat");
    REQUIRE(printed("repeat repeat repeat"));
    fmt_printd(&stream, "{2} {1} {0}", "zero", "one", "two");
    REQUIRE(printed("two one zero"));
    fmt_printd(&stream, "{1:d} {0:.2f}", 3.14159, 42);
    REQUIRE(printed("42 3.14"));
    fmt_printd(&stream, "{:<10}|{:>10}", "left", "right");
    REQUIRE(printed("left      |     right"));
    fmt_printd(&stream, "a{{b}}c", 0);
    REQUIRE(printed("a{b}c"));
    fmt_printd(&stream, "{0}", "");
    REQUIRE(printed(""));
    return true;
}

/* Format Text's path: a runtime array of "s" specifiers over stringified
 * values, ignoring the out-of-range error code. */
TEST_CASE(fmt_parses_runtime_specifiers_and_empties_missing_placeholders)
{
    char scratch[64];
    const char *specs[2] = { "s", "s" };
    (void)_fmt_parse(scratch, 2, "{1} and {0}", specs);
    _fmt_sprint(&stream, scratch, "X", "Y");
    REQUIRE(printed("Y and X"));
    (void)_fmt_parse(scratch, 1, "{2}", specs);
    _fmt_sprint(&stream, scratch, "X");
    REQUIRE(printed(""));
    return true;
}

void test_fmt()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(fmt_substitutes_positional_and_typed_arguments);
    RUN_TEST_CASE(fmt_parses_runtime_specifiers_and_empties_missing_placeholders);
    pu_clear_setup();
}
