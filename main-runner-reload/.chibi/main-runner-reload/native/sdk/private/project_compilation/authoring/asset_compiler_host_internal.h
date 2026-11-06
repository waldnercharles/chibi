#ifndef CHIBI_ASSET_COMPILER_HOST_INTERNAL_H
#define CHIBI_ASSET_COMPILER_HOST_INTERNAL_H

#include "authoring/asset_compiler_host.h"

/* Mechanical preorder projection for authored source/editing facade inputs. */
av_doc *asset_source_value_columns(const av_value *, asset_compiler_diagnostic *);
/* Generic authored facade calls return the ordinary result-array document. */
av_doc *asset_source_invoke(
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
#endif
