#ifndef CHIBI_CODE_SOURCE_H
#define CHIBI_CODE_SOURCE_H

#include "assets/asset_source.h"
#include "assets/code_source_context.h"
#include "graph/model/behavior_asset.h"

/* Shared native source frontend. Every range is a byte range in exact text.
 * Candidate assets are independently owned; failed edits do not replace a
 * caller's previous generation. Executable semantics come from readable
 * declarations and statements; graph presentation metadata is optional. */
typedef struct
{
    int start, end, line, column;
    int severity, rule;
    uint64_t graph_id, node_id, pin_id;
    /* Owned origin strings for diagnostics from a linked project library.
     * Byte ranges still refer to the importing call in this source. */
    char *asset_name, *graph_name;
    char message[256];
} code_source_diagnostic;
typedef struct
{
    uint64_t current, previous;
} code_source_identity;
typedef struct code_source
{
    char *text;
    asset_source_span *spans;
    code_source_diagnostic *diagnostics;
    av_doc *document;
    /* Owned with program: AST source maps borrow native graph/node pointers.
     * Consumers may inspect this model; replace it only by reparsing the source.
     * build_behavior/compile_behavior return independently editable candidates. */
    behavior_asset *asset;
    void *program;
    void *intelligence;
    code_source_context context;
    code_source_identity *identity_matches;
    uint64_t id_floor;
    /* Borrowed only while optional presentation metadata is being restored. */
    const av_value *metadata_anchors;
} code_source;

code_source *code_source_parse(const char *text);
/* Signature-only catalog pass; returned asset is independently owned. */
behavior_asset *code_source_declarations(const char *text, char *error, size_t size);
code_source *code_source_parse_context(
    const char *text,
    const code_source *previous,
    const code_source_context *context
);
code_source *code_source_parse_previous(const char *text, const code_source *previous);
void code_source_free(code_source *source);
bool code_source_is_structured(const char *text);
bool code_source_is_behavior(const code_source *source);
const char *code_source_kind(const code_source *source);
uint64_t code_source_previous_identity(const code_source *source, uint64_t current);
char *code_source_reconcile_query(const code_source *source, const av_value *query, char *error, size_t size);
/* Raw native model for project linking; validation follows resolution. */
behavior_asset *code_source_build_behavior(code_source *source);
/* Returns a validated independent candidate, or NULL with source diagnostics.
 * Warnings do not prevent construction. Repeat calls return independent copies. */
behavior_asset *code_source_compile_behavior(code_source *source);
char *code_source_format(const behavior_asset *asset);
/* Native graph first-save counterpart: metadata preserves exact emitted IDs. */
char *code_source_format_with_metadata(const behavior_asset *asset, char **metadata);
/* Clean executable text, with generated identity/presentation comments removed. */
char *code_source_persist(const code_source *source);
/* Optional JSON presentation/identity data; executable source works without it.
 * Disk metadata is bound to exact executable bytes. Stale or malformed files
 * are ignored and can never turn valid executable source into an error. */
char *code_source_metadata_export(const behavior_asset *asset);
/* Includes exact source anchors for repeated statements and declarations. */
char *code_source_metadata_export_source(const code_source *source);
/* Reanchors optional identities after a known edit that preserves token order,
 * such as a resolved symbol rename. Returns owned JSON. */
char *code_source_metadata_reanchor(const char *json, const char *before, const char *after);
/* Binds optional metadata to exact persisted source bytes for disk publication. */
char *code_source_metadata_bind(const char *json, const char *text);
/* Checks a disk sidecar binding without parsing executable imports. */
bool code_source_metadata_matches(const char *json, const char *text);
bool code_source_metadata_restore(code_source *source, const char *json);
bool code_source_metadata_load(code_source *source, const char *path);
bool code_source_metadata_save(const code_source *source, const char *path);

/* Range of an obsolete generated footer, used only to remove it from source. */
bool code_source_metadata_range(const char *text, int *start, int *end);
char *code_source_reconcile(const code_source *source, const behavior_asset *asset, char *error, size_t size);
/* Carries exact graph-edit identities separately from the returned clean code. */
char *code_source_reconcile_with_metadata(
    const code_source *source,
    const behavior_asset *asset,
    char **metadata,
    char *error,
    size_t size
);
/* Independently owned editable clone with foreign callable signatures refreshed
 * from the current context and validated. Pass the prior source snapshot with
 * its context rebound to the new catalog: proven omitted arguments follow new
 * defaults, authored literals and pending overrides survive. Edits/IDs/layout survive;
 * failure returns NULL without mutating either input. Reconcile against this
 * exact model before adopting the refreshed generation. */
behavior_asset *code_source_refresh_imports(
    const code_source *source,
    const behavior_asset *working,
    char *error,
    size_t size
);
/* Readable graph expressions and their inverse; both return malloc-owned text.
 * Resolution maps existing reflected inputs without mutating graph history. */
/* Borrowed readable label derived from the capture's native binding or source.
 * Custom presentation stays on pin.display_name; this never changes the model. */
const char *code_source_expression_input_label(
    const behavior_asset *asset,
    const node *expression,
    int input
);
/* Prefer exact authored capture spelling from this source's parser facts.
 * The borrowed result lives with source (or asset for the native fallback). */
const char *code_source_expression_input_label_at(
    const code_source *source,
    const behavior_asset *asset,
    const node *expression,
    int input
);
/* Compact presentation names; full capture spelling remains available above.
 * These never rename executable pins or change the source. */
void code_source_expression_input_name(
    const code_source *source,
    const behavior_asset *asset,
    const node *expression,
    int input,
    char *out,
    size_t size
);
char *code_source_expression(const code_source *source, const behavior_asset *asset, const node *expression);
char *code_source_resolve_expression(
    const code_source *source,
    const behavior_asset *asset,
    const node *expression,
    const char *text,
    char *error,
    size_t size
);
void code_source_position(const code_source *source, int offset, int *line, int *column);
/* Source locations include authored nodes and their generated expression bodies. */
bool code_source_location(
    const code_source *source,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    int *start,
    int *end
);

#endif
