#ifndef CBLUEPRINT_EDITOR_FLAGS_H
#define CBLUEPRINT_EDITOR_FLAGS_H

#include "graph/model/node.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct editor_flag
{
    unsigned value;
    const char *label;
} editor_flag;

typedef struct editor_flags
{
    const editor_flag *items;
    int count;
} editor_flags;

/* Catalogs are borrowed. Values must be nonzero and labels nonempty. */
typedef const editor_flags *(*editor_flags_resolver)(const node *, int output, int pin, void *user);
void editor_flags_format(unsigned value, const editor_flags *flags, char *buf, size_t cap);
unsigned editor_flags_toggle(unsigned value, unsigned flag);
bool editor_flags_draw(const char *label, unsigned *value, const editor_flags *flags);

#endif
