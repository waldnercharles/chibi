#ifndef CHIBI_AUTOMATION_PROTOTYPE_EDIT_H
#define CHIBI_AUTOMATION_PROTOTYPE_EDIT_H

#include "assets/prototype.h"
#include "automation/controller.h"

#include <stddef.h>

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;
typedef struct yyjson_val yyjson_val;

/* Apply a bounded semantic edit batch to a prototype clone. The helper owns no
 * IO: on success it returns a newly allocated canonical prototype document in
 * edited_text; on failure the source remains untouched. */
int automation_prototype_edit(
    const char *source_text,
    const game_prototype_registry *registry,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited_text,
    char *error,
    size_t error_size
);

#endif
