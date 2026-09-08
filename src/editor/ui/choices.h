#ifndef CBLUEPRINT_EDITOR_CHOICES_H
#define CBLUEPRINT_EDITOR_CHOICES_H

#include "graph/model/node.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct editor_choice
{
    int value;
    const char *label;
} editor_choice;

typedef struct editor_choices
{
    const editor_choice *items;
    int count;
} editor_choices;

typedef const editor_choices
    *(*editor_choices_resolver)(const node *, int output, int pin, void *user);

/* Catalogs are borrowed; labels are nonempty and values unique. Unknown values
 * remain unchanged until the user selects an item. */
void editor_choices_format(int value, const editor_choices *choices, char *buf, size_t cap);
bool editor_choices_draw(const char *id, int *value, const editor_choices *choices);

#endif
