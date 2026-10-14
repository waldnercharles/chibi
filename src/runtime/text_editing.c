#include "runtime/text_editing.h"

#include "support/xalloc.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static int next(const char *text, int at, int length)
{
    if (at >= length) {
        return length;
    }
    at++;
    while (at < length && ((unsigned char)text[at] & 0xc0) == 0x80) {
        at++;
    }
    return at;
}
static int previous(const char *text, int at)
{
    if (at <= 0) {
        return 0;
    }
    at--;
    while (at > 0 && ((unsigned char)text[at] & 0xc0) == 0x80) {
        at--;
    }
    return at;
}
static int boundary(const char *text, int at, int length)
{
    if (at <= 0) {
        return 0;
    }
    if (at >= length) {
        return length;
    }
    while (at > 0 && ((unsigned char)text[at] & 0xc0) == 0x80) {
        at--;
    }
    return at;
}
static uint32_t point(const char *text, int at, int end)
{
    unsigned char first = (unsigned char)text[at];
    uint32_t value = end - at == 1 ? first : first & (0x7f >> (end - at));
    for (int i = at + 1; i < end; i++) {
        value = (value << 6) | ((unsigned char)text[i] & 0x3f);
    }
    return value;
}
static bool input(const char *text, int *length, char *error, size_t size)
{
    if (size) {
        error[0] = 0;
    }
    size_t bytes = strlen(text);
    if (bytes > TEXT_EDITING_MAX_BYTES) {
        return fail(error, size, "Text exceeds the 1 MiB editing budget");
    }
    *length = (int)bytes;
    for (int at = 0; at < *length;) {
        int end = next(text, at, *length), width = end - at;
        unsigned char c = (unsigned char)text[at];
        if (width < 1 || width > 4) {
            return fail(error, size, "Text must be valid UTF-8");
        }
        uint32_t cp = point(text, at, end);
        bool valid = width == 1 ? c < 0x80
                   : width == 2 ? c >= 0xc2 && c <= 0xdf && cp >= 0x80
                   : width == 3 ? c >= 0xe0 && c <= 0xef && cp >= 0x800 &&
                                      !(cp >= 0xd800 && cp <= 0xdfff)
                                : width == 4 && c >= 0xf0 && c <= 0xf4 &&
                                      cp >= 0x10000 && cp <= 0x10ffff;
        if (!valid) {
            return fail(error, size, "Text must be valid UTF-8");
        }
        at = end;
    }
    return true;
}
static bool space(uint32_t c)
{
    return c <= 0x7f ? isspace((unsigned char)c) != 0
                     : c == 0x85 || c == 0xa0 || c == 0x1680 ||
                           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
                           c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
static int word_class(const char *text, int at, int length, bool big)
{
    uint32_t c = point(text, at, next(text, at, length));
    if (space(c)) {
        return 0;
    }
    return big || c >= 0x80 || isalnum((unsigned char)c) || c == '_' ? 1 : 2;
}
static text_line_view bounds(const char *text, int start, int length)
{
    text_line_view result = { .start = start, .end = start };
    while (result.end < length && text[result.end] != '\n') {
        result.end++;
    }
    result.next = result.end < length ? result.end + 1 : length;
    if (result.end > result.start && text[result.end - 1] == '\r') {
        result.end--;
    }
    return result;
}
static text_line_view line(const char *text, int offset, int length)
{
    text_line_view result = { .line = 1, .column = 1 };
    for (int at = 0; at < offset; at++) {
        if (text[at] == '\n') {
            result.start = at + 1;
            result.line++;
            result.column = 1;
        } else if (!(text[at] == '\r' && at + 1 < length && text[at + 1] == '\n') && ((unsigned char)text[at] & 0xc0) != 0x80) {
            result.column++;
        }
    }
    text_line_view content = bounds(text, result.start, length);
    result.end = content.end;
    result.next = content.next;
    return result;
}
bool text_editing_line(const char *text, int offset, text_line_view *out, char *error, size_t size)
{
    int length;
    if (!input(text, &length, error, size)) {
        return false;
    }
    *out = line(text, boundary(text, offset, length), length);
    return true;
}
static int word(const char *text, int at, int length, bool forward, bool end_word, bool big)
{
    if (!forward) {
        at = previous(text, at);
        while (at > 0 && word_class(text, at, length, big) == 0) {
            at = previous(text, at);
        }
        int cls = at < length ? word_class(text, at, length, big) : 0;
        while (at > 0 && word_class(text, previous(text, at), length, big) == cls) {
            at = previous(text, at);
        }
        return at;
    }
    if (at >= length) {
        return length;
    }
    int cls = word_class(text, at, length, big);
    if (end_word && cls != 0 && next(text, at, length) < length &&
        word_class(text, next(text, at, length), length, big) == cls) {
        while (next(text, at, length) < length &&
               word_class(text, next(text, at, length), length, big) == cls) {
            at = next(text, at, length);
        }
        return at;
    }
    while (at < length && word_class(text, at, length, big) == cls) {
        at = next(text, at, length);
    }
    while (at < length && word_class(text, at, length, big) == 0) {
        at = next(text, at, length);
    }
    if (!end_word || at >= length) {
        return at;
    }
    cls = word_class(text, at, length, big);
    while (next(text, at, length) < length &&
           word_class(text, next(text, at, length), length, big) == cls) {
        at = next(text, at, length);
    }
    return at;
}
bool text_editing_cursor(
    const char *text,
    int offset,
    const char *unit,
    int count,
    int goal_column,
    int *out,
    char *error,
    size_t size
)
{
    int length;
    if (!input(text, &length, error, size)) {
        return false;
    }
    if (count < -65536 || count > 65536) {
        return fail(error, size, "Text motion count exceeds 65536");
    }
    int at = boundary(text, offset, length);
    text_line_view current = line(text, at, length);
    if (!strcmp(unit, "line-start")) {
        *out = current.start;
        return true;
    }
    if (!strcmp(unit, "line-content")) {
        at = current.start;
        while (at < current.end && (text[at] == ' ' || text[at] == '\t')) {
            at++;
        }
        *out = at;
        return true;
    }
    if (!strcmp(unit, "line-end")) {
        *out = current.end;
        return true;
    }
    if (!strcmp(unit, "document")) {
        *out = count > 0 ? length : 0;
        return true;
    }
    bool forward = count >= 0;
    int steps = forward ? count : -count;
    if (!strcmp(unit, "line")) {
        int column = goal_column > 0 ? goal_column : current.column;
        for (int i = 0; i < steps; i++) {
            if (forward) {
                if (current.next == length && current.end == length) {
                    break;
                }
                current = bounds(text, current.next, length);
            } else {
                if (current.start == 0) {
                    break;
                }
                int start = current.start - 1;
                while (start > 0 && text[start - 1] != '\n') {
                    start--;
                }
                current = bounds(text, start, length);
            }
        }
        at = current.start;
        for (int i = 1; i < column && at < current.end; i++) {
            at = next(text, at, length);
        }
        *out = at;
        return true;
    }
    bool character = !strcmp(unit, "character"),
         word_start = !strcmp(unit, "word") || !strcmp(unit, "space-word"),
         word_end = !strcmp(unit, "word-end") || !strcmp(unit, "space-word-end"),
         big = !strcmp(unit, "space-word") || !strcmp(unit, "space-word-end");
    if (!character && !word_start && !word_end) {
        return fail(error, size, "Unknown text motion unit");
    }
    for (int i = 0; i < steps; i++) {
        int moved = character ? forward ? next(text, at, length) : previous(text, at)
                              : word(text, at, length, forward, word_end, big);
        if (moved == at) {
            break;
        }
        at = moved;
    }
    *out = at;
    return true;
}
static void range(const char *text, int length, int *start, int *end)
{
    *start = boundary(text, *start, length);
    *end = boundary(text, *end, length);
    if (*end < *start) {
        int swap = *start;
        *start = *end;
        *end = swap;
    }
}
bool text_editing_slice(const char *text, int start, int end, char **out, char *error, size_t size)
{
    *out = NULL;
    int length;
    if (!input(text, &length, error, size)) {
        return false;
    }
    range(text, length, &start, &end);
    *out = xmalloc((size_t)(end - start) + 1);
    memcpy(*out, text + start, (size_t)(end - start));
    (*out)[end - start] = 0;
    return true;
}
bool text_editing_replace(
    const char *text,
    int start,
    int end,
    const char *replacement,
    char **out,
    char *error,
    size_t size
)
{
    *out = NULL;
    int length, inserted;
    if (!input(text, &length, error, size) ||
        !input(replacement, &inserted, error, size)) {
        return false;
    }
    range(text, length, &start, &end);
    size_t bytes = (size_t)(length - (end - start)) + (size_t)inserted;
    if (bytes > TEXT_EDITING_MAX_BYTES) {
        return fail(error, size, "Replacement exceeds the 1 MiB editing budget");
    }
    *out = xmalloc(bytes + 1);
    memcpy(*out, text, (size_t)start);
    memcpy(*out + start, replacement, (size_t)inserted);
    memcpy(*out + start + inserted, text + end, (size_t)(length - end) + 1);
    return true;
}

bool text_editing_difference(
    const char *before,
    const char *after,
    text_difference *out,
    char *error,
    size_t size
)
{
    *out = (text_difference){ 0 };
    int a, b;
    if (!input(before, &a, error, size) || !input(after, &b, error, size)) {
        return false;
    }
    int start = 0;
    while (start < a && start < b && before[start] == after[start]) {
        start++;
    }
    start = boundary(before, start, a);
    start = boundary(after, start, b);
    int end_a = a, end_b = b;
    while (end_a > start && end_b > start) {
        int previous_a = previous(before, end_a), previous_b = previous(after, end_b);
        if (end_a - previous_a != end_b - previous_b ||
            memcmp(before + previous_a, after + previous_b, (size_t)(end_a - previous_a))) {
            break;
        }
        end_a = previous_a;
        end_b = previous_b;
    }
    out->start = start;
    out->end = end_a;
    for (int at = start; at < end_a; at = next(before, at, a)) {
        out->removed++;
    }
    for (int at = start; at < end_b; at = next(after, at, b)) {
        out->inserted++;
    }
    out->replacement = xmalloc((size_t)(end_b - start) + 1);
    memcpy(out->replacement, after + start, (size_t)(end_b - start));
    out->replacement[end_b - start] = 0;
    return true;
}

static int indentation(const char *text, int start, int end)
{
    while (start < end && (text[start] == ' ' || text[start] == '\t')) {
        start++;
    }
    return start;
}
bool text_editing_prefix_lines(
    const char *text,
    int start,
    int end,
    const char *prefix,
    const char *mode,
    char **out,
    int *out_start,
    int *out_end,
    char *error,
    size_t size
)
{
    *out = NULL;
    *out_start = *out_end = 0;
    int length, prefix_length;
    if (!input(text, &length, error, size) ||
        !input(prefix, &prefix_length, error, size)) {
        return false;
    }
    if (prefix_length == 0 || prefix_length > 256 || strchr(prefix, '\n') ||
        strchr(prefix, '\r')) {
        return fail(error, size, "Line prefix must contain 1 to 256 bytes without line breaks");
    }
    if (strcmp(mode, "add") && strcmp(mode, "remove") && strcmp(mode, "toggle")) {
        return fail(error, size, "Unknown line prefix mode");
    }
    range(text, length, &start, &end);
    while (start > 0 && text[start - 1] != '\n') {
        start--;
    }
    if (end > start && text[end - 1] == '\n') {
        end--;
    }
    while (end < length && text[end] != '\n') {
        end++;
    }
    if (end < length) {
        end++;
    }
    bool all = true;
    int lines = 0;
    for (int at = start; at < end || (at == start && start == end);) {
        text_line_view row = bounds(text, at, length);
        int content = indentation(text, row.start, row.end);
        bool has_prefix = row.end - content >= prefix_length &&
                          !memcmp(text + content, prefix, (size_t)prefix_length);
        all = all && has_prefix;
        lines++;
        if (row.next >= end) {
            break;
        }
        at = row.next;
    }
    bool remove = !strcmp(mode, "remove") || (!strcmp(mode, "toggle") && all);
    size_t capacity = (size_t)length +
                      (remove ? 0 : (size_t)lines * (size_t)prefix_length);
    if (capacity > TEXT_EDITING_MAX_BYTES) {
        return fail(error, size, "Line prefixes exceed the 1 MiB editing budget");
    }
    char *result = xmalloc(capacity + 1);
    memcpy(result, text, (size_t)start);
    int written = start;
    for (int at = start; at < end || (at == start && start == end);) {
        text_line_view row = bounds(text, at, length);
        int content = indentation(text, row.start, row.end);
        int indent = content - row.start;
        memcpy(result + written, text + row.start, (size_t)indent);
        written += indent;
        if (remove) {
            if (row.end - content >= prefix_length &&
                !memcmp(text + content, prefix, (size_t)prefix_length)) {
                content += prefix_length;
            }
        } else {
            memcpy(result + written, prefix, (size_t)prefix_length);
            written += prefix_length;
        }
        int bytes = row.next - content;
        memcpy(result + written, text + content, (size_t)bytes);
        written += bytes;
        if (row.next >= end) {
            break;
        }
        at = row.next;
    }
    *out_start = start;
    *out_end = written;
    memcpy(result + written, text + end, (size_t)(length - end) + 1);
    *out = result;
    return true;
}
