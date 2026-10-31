#ifndef CHIBI_ASSET_COMPILER_HOST_INTERNAL_H
#define CHIBI_ASSET_COMPILER_HOST_INTERNAL_H

#include "authoring/asset_compiler_host.h"

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
/* Native prototype projection only: keep existing integral constructors when
 * reflected real payloads are exactly representable by their original kind. */
char *asset_source_reconcile_native_compiled(
    const char *source,
    const av_value *old_value,
    const av_value *new_value,
    const asset_compiler_options *,
    asset_compiler_diagnostic *
);
#endif
