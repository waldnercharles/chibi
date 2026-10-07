#ifndef CHIBI_CODE_SOURCE_INTERNAL_H
#define CHIBI_CODE_SOURCE_INTERNAL_H
#include "assets/code_source.h"
#include "support/code_lex.h"

void code_source_expression_capture_name(const char *text, char *out, size_t size);
void code_source_error(code_source *source, int start, int end, const char *message);
bool code_source_metadata_range(const char *text, int *start, int *end);
bool code_source_explicit_location(
    const code_source *source,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    int *start,
    int *end
);
void code_source_expression_error(
    code_source *source,
    int source_start,
    const char *original,
    const char *resolved,
    int offset,
    int length,
    const char *message
);
void code_source_load_error(code_source *source, const char *message);
#endif
