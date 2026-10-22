#ifndef CHIBI_ASSET_COMPILER_HOST_H
#define CHIBI_ASSET_COMPILER_HOST_H

#include "support/asset_value.h"

typedef struct
{
    const char *path, *source;
} asset_source_library;
typedef struct
{
    const char *name, *path;
} asset_source_collection;
typedef struct
{
    const void *compiler;
    size_t compiler_length;
    const asset_source_library *libraries;
    int library_count;
    const asset_source_collection *collections;
    int collection_count;
    uint64_t compile_budget, evaluation_budget;
    bool (*cancelled)(void *user);
    void *user;
} asset_compiler_options;
typedef struct
{
    int document, start, end, line, column;
    char message[256];
    char source[512]; /* Source identity; at most 511 bytes copied. */
    bool source_truncated;
} asset_compiler_diagnostic;
/* Register one immutable packaged artifact/source snapshot. Caller retains it
 * for subsequent default reads. Set before concurrent reads; NULL clears it. */
void asset_source_set_default(const asset_compiler_options *);
/* Releases this thread's compiler and bounded prepared-source cache. */
void asset_source_release_thread();
/* Generated SDK resource adapter, target-compatible and immutable. */
const asset_compiler_options *asset_compiler_packaged_options();
const asset_compiler_options *asset_source_default_options();
/* Attach authored Inspect ranges to an already evaluated value. */
void asset_source_metadata_attach(const char *source, av_value *root);
/* Mechanical preorder projection for authored source/editing facade inputs. */
av_doc *asset_source_value_columns(const av_value *, asset_compiler_diagnostic *);
/* Generic authored facade calls return the ordinary result-array document. */
av_doc *asset_source_invoke(
    const char *entry,
    const av_value *arguments,
    const asset_compiler_options *,
    asset_compiler_diagnostic *
);
/* A facade byte result containing the private JSON document transport. */
av_doc *asset_source_invoke_document(
    const char *entry,
    const av_value *arguments,
    const asset_compiler_options *,
    asset_compiler_diagnostic *
);
char *asset_source_invoke_bytes(
    const char *entry,
    const av_value *arguments,
    const asset_compiler_options *,
    asset_compiler_diagnostic *
);
char *asset_source_reconcile_compiled(
    const char *source,
    const av_value *old_value,
    const av_value *new_value,
    const asset_compiler_options *,
    asset_compiler_diagnostic *
);
char *asset_source_write(const av_value *, const asset_compiler_options *, asset_compiler_diagnostic *);
/* Ordinary package source with public Build() -> byte[]. The authored asset
 * compiler resolves its entry, then the pure runtime evaluates its JSON byte
 * transport. No source parser/type checker or filesystem resolution occurs in
 * this adapter. NULL path selects /project/asset/Asset.chibi. */
av_doc *asset_source_read(
    const char *source,
    const char *path,
    const asset_compiler_options *,
    asset_compiler_diagnostic *
);

#endif
