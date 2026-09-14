#ifndef CHIBI_AUTOMATION_SCENE_EDIT_H
#define CHIBI_AUTOMATION_SCENE_EDIT_H

#include "automation/controller.h"

#include <stddef.h>

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;
typedef struct yyjson_val yyjson_val;

/* Apply a bounded semantic edit batch to a scene clone. The helper owns no IO:
 * on success it returns a newly allocated canonical scene document in
 * edited_json; on failure the source remains untouched. */
int automation_scene_edit(
    const char *source_json,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited_json,
    char *error,
    size_t error_size
);

#endif
