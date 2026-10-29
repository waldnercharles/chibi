#ifndef CHIBI_SOURCE_EDIT_REFACTOR_INTERNAL_H
#define CHIBI_SOURCE_EDIT_REFACTOR_INTERNAL_H
#include "authoring/source_edit_internal.h"
#include "authoring/source_refactor_text.h"
bool source_edit_inline(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
#endif
