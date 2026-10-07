#ifndef CHIBI_AUTOMATION_CODE_EDIT_H
#define CHIBI_AUTOMATION_CODE_EDIT_H

#include "assets/code_source_context.h"

#include <stdbool.h>
#include <stddef.h>
#include <yyjson.h>

/* Analyze an unsaved source candidate using the same native frontend as Studio.
 * Syntax/compile failures are results with valid=false and precise diagnostics;
 * malformed protocol arguments return an automation error. */
int automation_code_analyze(
    yyjson_val *request,
    bool compile,
    const code_source_context *context,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
);

#endif
