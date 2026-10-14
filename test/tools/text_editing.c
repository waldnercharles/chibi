#include "runtime/text_editing.h"

#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

TEST_CASE(text_cursor_moves_by_utf8_code_points_and_word_classes)
{
    const char *text = "aé game++ next";
    char error[256];
    int at;
    REQUIRE(
        text_editing_cursor(text, 1, "character", 1, 0, &at, error, sizeof error) && at == 3
    );
    REQUIRE(
        text_editing_cursor(text, 2, "character", -1, 0, &at, error, sizeof error) &&
        at == 0
    );
    REQUIRE(text_editing_cursor(text, 0, "word", 1, 0, &at, error, sizeof error) && at == 4);
    REQUIRE(text_editing_cursor(text, 4, "word", 1, 0, &at, error, sizeof error) && at == 8);
    REQUIRE(text_editing_cursor(text, 11, "word", -1, 0, &at, error, sizeof error) && at == 8);
    REQUIRE(text_editing_cursor(text, 4, "word-end", 1, 0, &at, error, sizeof error) && at == 7);
    REQUIRE(
        text_editing_cursor(text, 4, "space-word", 1, 0, &at, error, sizeof error) &&
        at == 11
    );
    REQUIRE(
        text_editing_cursor(text, 4, "space-word-end", 1, 0, &at, error, sizeof error) &&
        at == 9
    );
    REQUIRE(!text_editing_cursor(text, 0, "unknown", 1, 0, &at, error, sizeof error));
    return true;
}
TEST_CASE(text_line_motions_keep_requested_unicode_columns_and_crlf)
{
    const char *text = "aéz\r\nx\n界123";
    char error[256];
    text_line_view row;
    REQUIRE(text_editing_line(text, 3, &row, error, sizeof error));
    REQUIRE(row.start == 0 && row.end == 4 && row.next == 6 && row.line == 1 && row.column == 3);
    int at;
    REQUIRE(text_editing_cursor(text, 3, "line", 1, 3, &at, error, sizeof error) && at == 7);
    REQUIRE(text_editing_cursor(text, at, "line", 1, 3, &at, error, sizeof error) && at == 12);
    REQUIRE(text_editing_cursor(text, at, "line", -2, 3, &at, error, sizeof error) && at == 3);
    REQUIRE(
        text_editing_cursor(text, 6, "line-start", 1, 0, &at, error, sizeof error) &&
        at == 6
    );
    REQUIRE(text_editing_cursor(text, 6, "line-end", 1, 0, &at, error, sizeof error) && at == 7);
    return true;
}
TEST_CASE(text_replace_and_slice_preserve_exact_bytes_and_reject_invalid_utf8)
{
    const char *text = "aé\r\n界";
    char error[256], *owned = NULL;
    bool ok = text_editing_slice(text, 1, 3, &owned, error, sizeof error);
    bool same = owned && !strcmp(owned, "é");
    free(owned);
    REQUIRE(ok && same);
    ok = text_editing_replace(text, 2, 3, "🎮", &owned, error, sizeof error);
    same = owned && !strcmp(owned, "a🎮\r\n界");
    free(owned);
    REQUIRE(ok && same);
    REQUIRE(!text_editing_slice("\xc0\x80", 0, 1, &owned, error, sizeof error) && !owned);
    REQUIRE(
        !text_editing_slice("\x80\x80\x80\x80\x80", 0, 1, &owned, error, sizeof error) && !owned
    );
    return true;
}
TEST_CASE(text_difference_owns_a_minimal_unicode_safe_splice)
{
    char error[256];
    text_difference diff;
    REQUIRE(text_editing_difference("aéz", "aê🎮z", &diff, error, sizeof error));
    bool exact = diff.start == 1 && diff.end == 3 && diff.removed == 1 &&
                 diff.inserted == 2 && !strcmp(diff.replacement, "ê🎮");
    free(diff.replacement);
    REQUIRE(exact);
    REQUIRE(text_editing_difference("aéz", "aéz", &diff, error, sizeof error));
    exact = diff.start == 4 && diff.end == 4 && diff.removed == 0 &&
            diff.inserted == 0 && !strcmp(diff.replacement, "");
    free(diff.replacement);
    REQUIRE(exact);
    REQUIRE(!text_editing_difference("ok", "\xc0\x80", &diff, error, sizeof error));
    REQUIRE(!diff.replacement);
    return true;
}
TEST_CASE(text_prefixes_toggle_selected_lines_preserving_crlf_and_indentation)
{
    const char *source = "  aé\r\n\tx\nlast";
    char error[256], *owned = NULL;
    int start, end;
    REQUIRE(
        text_editing_prefix_lines(source, 1, 10, "//", "toggle", &owned, &start, &end, error, sizeof error)
    );
    bool exact = !strcmp(owned, "  //aé\r\n\t//x\nlast") && start == 0 && end == 14;
    REQUIRE(exact);
    char *restored = NULL;
    REQUIRE(
        text_editing_prefix_lines(owned, start, end, "//", "toggle", &restored, &start, &end, error, sizeof error)
    );
    exact = !strcmp(restored, source);
    free(owned);
    free(restored);
    REQUIRE(exact);
    REQUIRE(
        text_editing_prefix_lines("a\nb", 0, 2, "//", "add", &owned, &start, &end, error, sizeof error)
    );
    exact = !strcmp(owned, "//a\nb") && end == 4;
    free(owned);
    REQUIRE(exact);
    REQUIRE(
        text_editing_prefix_lines("", 0, 0, "//", "toggle", &owned, &start, &end, error, sizeof error)
    );
    exact = !strcmp(owned, "//") && start == 0 && end == 2;
    free(owned);
    REQUIRE(exact);
    REQUIRE(
        !text_editing_prefix_lines("a", 0, 1, "\n", "toggle", &owned, &start, &end, error, sizeof error) &&
        !owned
    );
    return true;
}
void test_text_editing()
{
    RUN_TEST_CASE(text_cursor_moves_by_utf8_code_points_and_word_classes);
    RUN_TEST_CASE(text_difference_owns_a_minimal_unicode_safe_splice);
    RUN_TEST_CASE(text_prefixes_toggle_selected_lines_preserving_crlf_and_indentation);
    RUN_TEST_CASE(text_line_motions_keep_requested_unicode_columns_and_crlf);
    RUN_TEST_CASE(text_replace_and_slice_preserve_exact_bytes_and_reject_invalid_utf8);
}
