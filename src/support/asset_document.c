#include "support/asset_document.h"

#include "authoring/asset_compiler_host.h"
#include "support/xalloc.h"

#include <stdio.h>

bool asset_document_source(const char *source)
{
    if (!source) {
        return false;
    }
    av_doc *arguments = av_doc_new();
    av_value *values = av_arr(arguments);
    av_doc_set_root(arguments, values);
    av_arr_append(values, av_str(arguments, source));
    asset_compiler_diagnostic diagnostic = { 0 };
    av_doc *result = asset_source_invoke(
        "project:asset_compiler:1:Classify:callable",
        values,
        NULL,
        &diagnostic
    );
    bool asset = av_get_bool(av_arr_get(av_doc_root(result), 0));
    av_doc_free(result);
    av_doc_free(arguments);
    return asset;
}

av_doc *asset_document_read(const char *source, char *error, size_t error_size)
{
    asset_compiler_diagnostic diagnostic = { 0 };
    av_doc *document = asset_source_read(source, NULL, NULL, &diagnostic);
    if (error && error_size) {
        if (document) {
            error[0] = 0;
        } else {
            snprintf(
                error,
                error_size,
                "line %d, column %d: %s",
                diagnostic.line,
                diagnostic.column,
                diagnostic.message
            );
        }
    }
    return document;
}

char *asset_document_write(const av_value *root)
{
    asset_compiler_diagnostic diagnostic = { 0 };
    return asset_source_write(root, NULL, &diagnostic);
}

char *asset_document_to_disk(const char *source, char *error, size_t error_size)
{
    if (error && error_size) {
        error[0] = 0;
    }
    return source ? xstrdup(source) : NULL;
}
