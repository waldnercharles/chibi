#include "test/suite_internal.h"

void run_support_format_checks()
{
    /* Vendored fmt library (third_party/fmt): unit tests through its public
     * fmt_printd API into a fmt_stream, plus the two local fixes (an empty
     * result must still get a buffer; an out-of-range placeholder renders
     * empty when the error code is ignored, which is what Format Text
     * relies on for CAT-38). The lowercase "fmt:" names keep these out of
     * the requirement-ID scanner, where FMT-x means the serialization
     * family. */
    {
        fmt_stream ss = { 0 };
        fmt_printd(&ss, "One arg: {}", 42);
        fmt_check_stream(&ss, "One arg: 42", "fmt: one auto-numbered argument");
        fmt_printd(&ss, "{} {} {}", "a", "b", "c");
        fmt_check_stream(&ss, "a b c", "fmt: several auto-numbered arguments");
        fmt_printd(&ss, "{} {} {} {} {} {} {} {} {} {}", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
        fmt_check_stream(&ss, "1 2 3 4 5 6 7 8 9 10", "fmt: ten arguments substitute in order");
        fmt_printd(&ss, "{1} {0}", "first", "second");
        fmt_check_stream(&ss, "second first", "fmt: positional placeholders reorder");
        fmt_printd(&ss, "{0} {0} {0}", "repeat");
        fmt_check_stream(&ss, "repeat repeat repeat", "fmt: one argument is reused by position");
        fmt_printd(&ss, "{2} {1} {0}", "zero", "one", "two");
        fmt_check_stream(&ss, "two one zero", "fmt: positional placeholders reverse");
        fmt_printd(&ss, "{1:d} {0:.2f}", 3.14159, 42);
        fmt_check_stream(&ss, "42 3.14", "fmt: typed positional specifiers dispatch by C type");
        fmt_printd(&ss, "{:<10}|{:>10}", "left", "right");
        fmt_check_stream(&ss, "left      |     right", "fmt: alignment specifiers pad strings");
        fmt_printd(&ss, "a{{b}}c", 0);
        fmt_check_stream(&ss, "a{b}c", "fmt: {{ and }} escape literal braces");
        fmt_printd(&ss, "{0}", "");
        fmt_check_stream(&ss, "", "fmt: an empty result still gets a buffer (local fix)");
        /* The Format Text path: runtime specifier array of "s" codes with
         * stringified values; the out-of-range error code is ignored so a
         * missing placeholder renders empty (CAT-38). */
        char scratch[64];
        const char *specs[2] = { "s", "s" };
        (void)_fmt_parse(scratch, 2, "{1} and {0}", specs);
        _fmt_sprint(&ss, scratch, "X", "Y");
        fmt_check_stream(&ss, "Y and X", "fmt: parses with a runtime specifier array");
        (void)_fmt_parse(scratch, 1, "{2}", specs);
        _fmt_sprint(&ss, scratch, "X");
        fmt_check_stream(&ss, "", "fmt: an out-of-range placeholder renders empty");
    }
}
