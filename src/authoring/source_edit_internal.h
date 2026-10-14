#ifndef CHIBI_AUTHORING_SOURCE_EDIT_INTERNAL_H
#define CHIBI_AUTHORING_SOURCE_EDIT_INTERNAL_H
#include "authoring/source_edit.h"
#include "support/text_edit.h"
typedef struct
{
    char *text;
    text_edit *patches;
    script_range moved;
    int destination;
} source_edit_changes;
typedef struct
{
    char *text;
    script_type type;
    uint64_t symbol;
    int prefix;
    bool inline_value;
} source_edit_connection;
/* A fused call keeps its expression origin for argument edits and a complete
 * statement origin for operations on the execution spine. Pins stay values. */
const source_presentation_origin *source_edit_statement_origin(
    const authoring_source_presentation *,
    const source_presentation_origin *
);
source_edit_connection source_edit_producer(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    uint64_t producer,
    int result,
    char *,
    size_t
);
/* Validate copied expression names at their final candidate byte position.
 * Returned candidate is owned; failure frees the supplied candidate. */
char *source_edit_check_connection(
    const script_analysis *,
    const script_catalog *,
    char *candidate,
    const source_edit_connection *,
    const source_presentation_origin *,
    int destination,
    char *,
    size_t
);
bool source_edit_single_expression(const char *);
bool source_edit_selection_region(
    const authoring_source_presentation *,
    const script_syntax *,
    const uint64_t *,
    int count,
    script_range *,
    TSNode *,
    char *,
    size_t
);
script_range source_edit_whole_statement(const script_syntax *, script_range);
bool source_edit_structural(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
bool source_edit_disconnect_patch(
    const script_analysis *,
    const script_catalog *,
    const script_syntax *,
    const authoring_source_presentation *,
    uint64_t,
    text_edit **,
    char *,
    size_t
);
bool source_edit_arguments(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
bool source_edit_declarations(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
bool source_edit_capability(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
void source_edit_changes_free(source_edit_changes *);
bool source_edit_refactor(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
bool source_edit_signature(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
bool source_edit_binding(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    source_edit_changes *,
    char *,
    size_t
);
#endif
