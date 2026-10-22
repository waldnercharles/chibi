#ifndef CHIBI_ASSET_DOCUMENT_H
#define CHIBI_ASSET_DOCUMENT_H

#include "support/asset_format.h"
#include "support/asset_value.h"

/* Ordinary Chibi modules export Build() -> byte[]. The authored compiler and
 * packaged pure value library produce the native document transport. */
/* Syntax-only package classification also supports incomplete editor drafts. */
bool asset_document_source(const char *source);
av_doc *asset_document_read(const char *source, char *error, size_t error_size);
/* New static source is produced by the authored source writer. Existing source
 * edits use asset_source_reconcile so helper expressions and trivia survive. */
char *asset_document_write(const av_value *root);
/* Persistence preserves existing Chibi source. The owning source session
 * validates edits before committing them. */
char *asset_document_to_disk(const char *source, char *error, size_t error_size);

#endif
