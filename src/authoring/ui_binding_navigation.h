#ifndef CHIBI_UI_BINDING_NAVIGATION_H
#define CHIBI_UI_BINDING_NAVIGATION_H
#include "assets/ui_source.h"
#include "language/semantic.h"

typedef struct
{
    const char *name; /* A data path under self, such as inventory.items. */
    script_type type;
    bool ambiguous;
} authoring_ui_binding;
typedef struct
{
    const char *name;
    uint64_t owner, field;
    script_range range;
    bool unresolved;
} authoring_ui_binding_reference;
/* Expression-aware markup references. Strings and unrelated attribute/text
 * values never become references. Environments borrow real semantic types;
 * unknown/conflicting receivers remain explicit, never guessed by spelling.
 * Output rows and names belong to storage. No execution or mutation occurs. */
bool authoring_ui_binding_references(
    const ui_source *,
    const script_catalog *,
    const authoring_ui_binding *,
    int count,
    arena *storage,
    authoring_ui_binding_reference **rows,
    int *row_count,
    char *error,
    size_t size
);
#endif
