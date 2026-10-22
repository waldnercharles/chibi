#include "authoring/asset_compiler_host.h"
#include "authoring/source_asset.h"

#include <stdio.h>

#define FACADE "project:asset_compiler:1:"

av_doc *authoring_prototype_source_parameters(const char *source, char *error, size_t size)
{
    av_doc *args = av_doc_new();
    av_value *values = av_arr(args);
    av_doc_set_root(args, values);
    av_arr_add_strcpy(args, values, source);
    asset_compiler_diagnostic diagnostic = { 0 };
    av_doc *result = asset_source_invoke_document(FACADE "Parameters:callable", values, NULL, &diagnostic);
    if (result && av_obj_size(av_doc_root(result)) > 0) {
        av_arr_add_strcpy(args, values, "Parameters");
        av_doc *metadata = asset_source_invoke(FACADE "Inspect:callable", values, NULL, &diagnostic);
        const av_value *returned = metadata
                                     ? av_obj_get(av_arr_get(av_doc_root(metadata), 0), "values")
                                     : NULL;
        if (!metadata || av_arr_size(returned) != 2) {
            av_doc_free(result);
            result = NULL;
            if (metadata) {
                snprintf(diagnostic.message, sizeof diagnostic.message, "%s", "Invalid parameter source metadata");
            }
        } else {
            const av_value *bindings = av_arr_get(returned, 0);
            av_value *root = av_doc_root(result);
            for (size_t i = 0; i < av_arr_size(bindings); i++) {
                const av_value *binding = av_arr_get(bindings, i);
                if (av_get_int(av_obj_get(binding, "parent")) != 0) {
                    continue;
                }
                const char *key = av_get_str(av_obj_get(binding, "key"));
                const av_value *value = av_obj_get(root, key);
                if (!value) {
                    continue; /* An unused metadata helper is not active. */
                }
                int kind = av_get_int(av_obj_get(binding, "kind"));
                if (kind == 2 && av_is_uint(value) &&
                    av_get_uint(value) <= (uint64_t)INT64_MAX) {
                    av_obj_set(result, root, key, av_sint(result, (int64_t)av_get_uint(value)));
                } else if (kind == 3 && av_is_sint(value) && av_get_sint(value) >= 0) {
                    av_obj_set(result, root, key, av_uint(result, (uint64_t)av_get_sint(value)));
                }
            }
        }
        av_doc_free(metadata);
    }
    if (size) {
        snprintf(error, size, "%s", result ? "" : diagnostic.message);
    }
    av_doc_free(args);
    return result;
}

bool authoring_prototype_source_field(
    const char *source,
    const char *component,
    const char *field_path,
    authoring_prototype_field_source *out
)
{
    *out = (authoring_prototype_field_source){
        .expression = { SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX }
    };
    av_doc *args = av_doc_new();
    av_value *values = av_arr(args);
    av_doc_set_root(args, values);
    av_arr_add_strcpy(args, values, source);
    av_arr_add_strcpy(args, values, component);
    av_arr_add_strcpy(args, values, field_path);
    asset_compiler_diagnostic diagnostic = { 0 };
    av_doc *result = asset_source_invoke(FACADE "Field:callable", values, NULL, &diagnostic);
    const av_value *returned = result
                                 ? av_obj_get(av_arr_get(av_doc_root(result), 0), "values")
                                 : NULL;
    bool found = av_arr_size(returned) == 4;
    if (found) {
        out->computed = av_get_bool(av_arr_get(returned, 0));
        out->overridden = av_get_bool(av_arr_get(returned, 1));
        int start = av_get_int(av_arr_get(returned, 2));
        int end = av_get_int(av_arr_get(returned, 3));
        out->expression.start = start < 0 ? SIZE_MAX : (size_t)start;
        out->expression.end = end < 0 ? SIZE_MAX : (size_t)end;
    }
    av_doc_free(result);
    av_doc_free(args);
    return found;
}
