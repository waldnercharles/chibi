#include "support/markup_expression.h"

#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
static bool decode_entity(const char *text, int remaining, unsigned *code, int *bytes)
{
    static const struct
    {
        const char *name;
        unsigned code;
    } named[] = { { "&amp;", '&' },
                  { "&lt;", '<' },
                  { "&gt;", '>' },
                  { "&quot;", '"' },
                  { "&apos;", '\'' } };
    for (int i = 0; i < 5; i++) {
        int n = (int)strlen(named[i].name);
        if (n <= remaining && !strncmp(text, named[i].name, (size_t)n)) {
            *code = named[i].code;
            *bytes = n;
            return true;
        }
    }
    if (remaining < 4 || text[0] != '&' || text[1] != '#') {
        return false;
    }
    int at = 2, base = 10;
    unsigned value = 0;
    if (at < remaining && (text[at] == 'x' || text[at] == 'X')) {
        base = 16;
        at++;
    }
    int first = at;
    while (at < remaining && text[at] != ';') {
        unsigned char c = (unsigned char)text[at++];
        int digit = isdigit(c)           ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                         : -1;
        if (digit < 0 || digit >= base ||
            value > (0x10ffffu - (unsigned)digit) / (unsigned)base) {
            return false;
        }
        value = value * (unsigned)base + (unsigned)digit;
    }
    if (at == first || at >= remaining || !value ||
        (value >= 0xd800 && value <= 0xdfff)) {
        return false;
    }
    *code = value;
    *bytes = at + 1;
    return true;
}
void markup_expression_decode(const char *text, int start, int end, markup_expression *p)
{
    assert(text && start >= 0 && end >= start && (size_t)end <= strlen(text));
    *p = (markup_expression){ 0 };
    int capacity = end - start + 1;
    p->text = xmalloc((size_t)capacity);
    p->starts = xmalloc((size_t)capacity * sizeof *p->starts);
    p->ends = xmalloc((size_t)capacity * sizeof *p->ends);
    for (int i = start; i < end;) {
        unsigned c = (unsigned char)text[i];
        int bytes = 1;
        bool entity = c == '&' && decode_entity(text + i, end - i, &c, &bytes);
        unsigned char utf8[4];
        int n = 1;
        utf8[0] = (unsigned char)c;
        if (entity && c >= 0x80) {
            if (c < 0x800) {
                n = 2;
                utf8[0] = 0xc0 | (c >> 6);
                utf8[1] = 0x80 | (c & 63);
            } else if (c < 0x10000) {
                n = 3;
                utf8[0] = 0xe0 | (c >> 12);
                utf8[1] = 0x80 | ((c >> 6) & 63);
                utf8[2] = 0x80 | (c & 63);
            } else {
                n = 4;
                utf8[0] = 0xf0 | (c >> 18);
                utf8[1] = 0x80 | ((c >> 12) & 63);
                utf8[2] = 0x80 | ((c >> 6) & 63);
                utf8[3] = 0x80 | (c & 63);
            }
        }
        for (int j = 0; j < n; j++) {
            p->text[p->length] = (char)utf8[j];
            p->starts[p->length] = i;
            p->ends[p->length++] = i + bytes;
        }
        i += bytes;
    }
    p->text[p->length] = 0;
}

void markup_expression_free(markup_expression *text)
{
    free(text->text);
    free(text->starts);
    free(text->ends);
    *text = (markup_expression){ 0 };
}
