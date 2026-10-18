#ifndef CHIBI_LANGUAGE_SYNTAX_H
#define CHIBI_LANGUAGE_SYNTAX_H

#include <stdbool.h>
#include <tree_sitter/api.h>

typedef struct
{
    int start, end;   /* Half-open UTF-8 byte offsets. */
    int line, column; /* One-based; column counts bytes. */
    int document; /* Package input index; zero for single-document syntax. */
} script_range;

typedef struct
{
    script_range range;
    char message[192];
} script_diagnostic;

/* Owned snapshot, including exact source and an error-tolerant concrete syntax
 * tree. Consumers may inspect it, but must not mutate its fields or tree. */
typedef struct
{
    char *text;
    TSTree *tree;
    script_diagnostic *diagnostics;
    int diagnostic_count;
} script_syntax;

script_syntax *script_parse(const char *text);
/* Creates a new snapshot through Tree-sitter's incremental parse. The previous
 * snapshot stays usable. Offsets must be valid UTF-8 boundaries in its source. */
script_syntax *script_edit(const script_syntax *previous, int start, int end, const char *replacement);
void script_syntax_free(script_syntax *syntax);
script_range script_node_range(TSNode node);

#endif
