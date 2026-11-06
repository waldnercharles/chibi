#ifndef CHIBI_AUTOMATION_IMAGE_IMPORT_H
#define CHIBI_AUTOMATION_IMAGE_IMPORT_H

#include "authoring/project_prepare.h"

#include <yyjson.h>

/* Plan/apply adapter. Returns a CHIBI_AUTOMATION_* code; result holds reviewable
 * plan metadata even when apply refuses a stale hash or exceeded budget. */
int automation_image_import(
    const authoring_project_options *,
    yyjson_val *,
    bool apply,
    yyjson_mut_doc *,
    yyjson_mut_val *,
    char *,
    size_t
);

#endif
