#ifndef CHIBI_AUTHORING_SOURCE_CATALOG_H
#define CHIBI_AUTHORING_SOURCE_CATALOG_H
#include "authoring/source_declaration.h"
#include "language/semantic.h"
#include "runtime/input_action.h"

typedef struct source_catalog source_catalog;
typedef struct
{
    const char *path, *text, *identity;
    script_syntax *syntax;
    script_analysis *declarations;
} source_catalog_source;
typedef struct
{
    const char *path;
    script_range range;
    char message[192];
} source_catalog_diagnostic;
/* Reads direct authored declarations under a content root. Owns source, syntax,
 * project and native declarations and defaults.
 * Files are limited to 512 KiB, 256 executable sources and 8 MiB total text.
 * Diagnostics make an incomplete snapshot explicit; it must not be published. */
source_catalog *source_catalog_new(const char *content_root, const script_catalog *native);
/* One-time normalization may analyze retired generated accessors. Normal
 * catalog construction never admits those source spellings. */
source_catalog *source_catalog_new_for_migration(const char *content_root, const script_catalog *native);
typedef struct
{
    const char *path, *text; /* NULL text removes this source from the snapshot. */
    const char *identity; /* Optional prior document key for an explicit file move. */
} source_catalog_edit;
bool source_catalog_source_path(const char *content_root, const char *path);
bool source_catalog_edit_path(const char *content_root, const char *path);
bool source_catalog_identity_valid(const char *);
/* Applies exact-path source and declarative edits before declaration discovery.
 * Inputs are borrowed only for this call; disk files are never changed. */
source_catalog *source_catalog_edited(
    const char *content_root,
    const script_catalog *native,
    const source_catalog_edit *,
    int edit_count
);
/* Complete executable membership from memory; omitted disk sources are absent.
 * Declarative project declarations are still discovered under content_root,
 * with any supplied declarative overlays applied. */
source_catalog *source_catalog_replaced(
    const char *content_root,
    const script_catalog *native,
    const source_catalog_edit *,
    int edit_count
);
const script_catalog *source_catalog_view(const source_catalog *);
/* Owned by this snapshot, sorted by canonical specialization identity. The
 * requested-analysis API selects requests belonging to its document key. */
const script_specialization_request *source_catalog_requests(const source_catalog *, int *count);
/* The project's input map, content/input.kdl, as a support/array.h vector. */
const input_action *source_catalog_input(const source_catalog *);
int source_catalog_source_count(const source_catalog *);
const source_catalog_source *source_catalog_source_at(const source_catalog *, int index);
int source_catalog_definition_count(const source_catalog *);
const source_catalog_definition *source_catalog_definitions(const source_catalog *);
int source_catalog_reference_count(const source_catalog *);
const source_catalog_reference *source_catalog_references(const source_catalog *);
int source_catalog_diagnostic_count(const source_catalog *);
const source_catalog_diagnostic *source_catalog_diagnostic_at(const source_catalog *, int index);
void source_catalog_free(source_catalog *);
#endif
