#ifndef CHIBI_CODE_CATALOG_H
#define CHIBI_CODE_CATALOG_H

#include "assets/code_source.h"

typedef struct code_catalog code_catalog;
typedef struct
{
    const char *path, *message;
} code_catalog_error;

/* Owns event definitions and complete executable libraries. Library signatures
 * are declared before any body is lowered, so filename order is immaterial.
 * Invalid entries have path-qualified diagnostics; valid entries remain usable. */
code_catalog *code_catalog_load(const char *content_root);
code_catalog *code_catalog_load_project(const char *project_root);
/* Owns the filtered pointer arrays, while the caller keeps models alive. */
code_catalog *code_catalog_from_assets(const behavior_asset *const *assets, int count);
/* Owns declaration models parsed from the current in-memory project sources. */
code_catalog *code_catalog_from_texts(const char *const *texts, int count);
/* Copies a typed asset declaration supplied by an in-memory project snapshot. */
void code_catalog_add_reference(code_catalog *, code_reference_kind, const char *name);
/* Refresh snapshot library signatures to their restored graph/pin identities. */
bool code_catalog_restore_metadata(
    code_catalog *,
    const char *const *texts,
    const char *const *metadata,
    int count
);
void code_catalog_free(code_catalog *catalog);
const code_source_context *code_catalog_context(const code_catalog *catalog);
int code_catalog_error_count(const code_catalog *catalog);
const code_catalog_error *code_catalog_error_at(const code_catalog *catalog, int index);
/* The returned model is independent of the catalog. Declarative graph domains
 * retain their KDL loader; executable source uses the project signatures. */
behavior_asset *code_catalog_load_behavior(
    const code_catalog *catalog,
    const char *text,
    char *error,
    size_t size
);
/* Disk asset path supplies optional .code.meta presentation and identities. */
behavior_asset *code_catalog_load_path(
    const code_catalog *catalog,
    const char *text,
    const char *path,
    char *error,
    size_t size
);
behavior_asset *code_catalog_load_with_context(
    const code_source_context *context,
    const char *text,
    char *error,
    size_t size
);

#endif
