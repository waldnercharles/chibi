#include "support/text_edit.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int by_start(const void *a, const void *b)
{
    const text_edit *x = *(const text_edit *const *)a,
                    *y = *(const text_edit *const *)b;
    return (x->start > y->start) - (x->start < y->start);
}

char *text_edits_apply(const char *text, const text_edit *edits, int count)
{
    size_t length = strlen(text), size = length + 1;
    const text_edit **order = xmalloc(sizeof *order * (size_t)(count ? count : 1));
    for (int i = 0; i < count; i++) {
        assert(
            edits[i].start >= 0 && edits[i].start <= edits[i].end &&
            (size_t)edits[i].end <= length
        );
        order[i] = &edits[i];
        size += strlen(edits[i].text) - (size_t)(edits[i].end - edits[i].start);
    }
    qsort(order, (size_t)count, sizeof *order, by_start);
    char *out = xmalloc(size), *write = out;
    int at = 0;
    for (int i = 0; i < count; i++) {
        const text_edit *e = order[i];
        assert(e->start >= at);
        size_t added = strlen(e->text);
        memcpy(write, text + at, (size_t)(e->start - at));
        write += e->start - at;
        memcpy(write, e->text, added);
        write += added;
        at = e->end;
    }
    memcpy(write, text + at, length - (size_t)at + 1);
    free(order);
    return out;
}
