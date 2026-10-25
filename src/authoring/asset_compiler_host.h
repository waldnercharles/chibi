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
    /* NULL selects CHIBI_ASSET_CACHE_DIR or the platform user cache. Empty
     * disables persistent storage. The caller retains explicit paths. */
    const char *cache_directory;
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
typedef struct
{
    uint64_t memory_hits, persistent_hits, compile_attempts;
    uint64_t preparation_attempts, evaluations;
    size_t resident_bytes; /* Includes decoded leases detached during an invocation. */
} asset_compiler_cache_stats;
/* Hits count validated prepared-code reuse; attempts count CompileBytes calls.
 * Preparation counts decoded asset loads; every Build attempt increments evaluations.
 * Reading statistics does not change execution or enable runtime observers. */
asset_compiler_cache_stats asset_source_cache_stats();
/* Releases this thread's compiler/memory cache and resets its statistics.
 * Active invocations retain leased hosts until they return. Their decoded
 * storage remains charged; options and immutable source snapshots are borrowed
 * for the whole invocation and must remain valid across cancellation callbacks.
 * Persistent storage remains available to subsequent calls and processes. */
void asset_source_release_thread();
/* Generated SDK resource adapter, target-compatible and immutable. */
const asset_compiler_options *asset_compiler_packaged_options();
const asset_compiler_options *asset_source_default_options();
/* Each call owns bounded compilation/evaluation scopes and restores any caller
 * scope on return; it does not debit a caller's outer quota.
 * Returns a caller-owned document (av_doc_free), or NULL with a copied diagnostic.
 * Options and snapshot pointers are borrowed through all cancellation callbacks.
 * Ordinary package source with public Build() -> byte[]. The authored asset
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
