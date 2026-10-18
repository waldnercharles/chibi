#ifndef CHIBI_AUTHORING_SOURCE_PACKAGE_SYNTAX_H
#define CHIBI_AUTHORING_SOURCE_PACKAGE_SYNTAX_H

#include "authoring/source_packages.h"

typedef struct
{
    script_range path;  /* Complete quoted string token; not decoded here. */
    script_range alias; /* Zero for an implicit alias; includes explicit _. */
} source_package_import_syntax;

/* Private extraction snapshot used by source_package_snapshot. Name and alias strings, descriptor arrays and
 * locations are owned. Path and directory strings are borrowed from the caller.
 * The syntax is read only during extraction; keep it when interpreting ranges.
 * Imports preserve source order. Their directory starts NULL: the caller must
 * resolve every path and assign a canonical borrowed directory before passing
 * the descriptors to source_packages_new. No implicit path resolution occurs.
 * Keep this owner, all borrowed paths/directories and the final descriptor arrays
 * alive until the graph is freed. The owning SDK snapshot handles resolution;
 * callers do not use this extraction record as an executable package. */
typedef struct
{
    source_package_file file;
    source_package_import *imports;
    source_package_import_syntax *locations;
    int import_count;
} source_package_syntax;

/* Uses the canonical, error-free source_file tree, never a second parser.
 * file is the caller's snapshot-local file index. Header range is the name
 * token; import range runs from import through the quoted path, excluding
 * terminators and trailing comments. Syntax errors, non-package roots and
 * the blank package name _ return NULL and one source diagnostic; no partial
 * descriptors escape. Explicit blank import aliases remain valid. */
source_package_syntax *source_package_syntax_new(
    const script_syntax *,
    const char *path,
    const char *directory,
    int file,
    script_diagnostic *
);
void source_package_syntax_free(source_package_syntax *);

#endif
