#ifndef CHIBI_EDITOR_CHOICES_H
#define CHIBI_EDITOR_CHOICES_H

#include "graph/model/node.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct editor_choice
{
    int value;
    const char *label;
    const char *name; /* Stable value for PIN_NAME catalogs; NULL for integers. */
} editor_choice;

typedef struct editor_choices
{
    const editor_choice *items;
    int count;
    const enum_desc *enumeration;
} editor_choices;

typedef const editor_choices
    *(*editor_choices_resolver)(const node *, int output, int pin, void *user);

/* Catalogs are borrowed; labels are nonempty and values unique. Unknown values
 * remain unchanged until the user selects an item. */
int editor_choices_count(const editor_choices *choices);
editor_choice editor_choice_at(const editor_choices *choices, int index);
void editor_choices_format(int value, const editor_choices *choices, char *buf, size_t cap);
bool editor_choices_draw(const char *id, int *value, const editor_choices *choices);
bool editor_choices_draw_name(const char *id, const char **value, const editor_choices *choices);
const editor_choices *editor_choices_for_enum(const char *type_name);

#endif
