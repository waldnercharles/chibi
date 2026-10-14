#include "authoring/workspace_assets.h"

#include "graph/model/registry.h"
#include "language/semantic.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KIND_NAMES[NEW_ASSET_KIND_COUNT + 3] = {
    "scene",   "prototype", "behavior",    "event",     "system",
    "library", "material",  "shape",       "field",     "shader-library",
    "model",   "ui-widget", "ui-document", "stylesheet"
};
int workspace_asset_kind_count()
{
    return NEW_ASSET_KIND_COUNT + 3;
}
const char *workspace_asset_kind_folder(int kind)
{
    assert(kind >= 0 && kind < workspace_asset_kind_count());
    return kind < NEW_ASSET_KIND_COUNT ? new_asset_folder(kind) : "ui";
}
const char *workspace_asset_kind_extension(int kind)
{
    assert(kind >= 0 && kind < workspace_asset_kind_count());
    return kind < NEW_ASSET_KIND_COUNT      ? new_asset_extension(kind)
         : kind == NEW_ASSET_KIND_COUNT + 2 ? ".rcss"
                                            : ".rml";
}
const char *workspace_asset_kind_name(int kind)
{
    assert(kind >= 0 && kind < workspace_asset_kind_count());
    return KIND_NAMES[kind];
}
bool workspace_asset_template(
    const char *kind,
    const char *name,
    workspace_asset_template_result *out,
    char *error,
    size_t size
)
{
    *out = (workspace_asset_template_result){ 0 };
    int index = 0;
    for (; index < workspace_asset_kind_count(); index++) {
        if (!strcmp(kind, KIND_NAMES[index])) {
            break;
        }
    }
    if (index == workspace_asset_kind_count()) {
        snprintf(error, size, "Unknown asset kind '%s'", kind);
        return false;
    }
    if (!new_asset_name_valid(name, error, size)) {
        return false;
    }
    if (index == NEW_ASSET_FIELD && !registry_node_by_name("Field Output")) {
        snprintf(error, size, "Field authoring capabilities are not registered in this host");
        return false;
    }
    char *source;
    if (index < NEW_ASSET_KIND_COUNT) {
        source = new_asset_text(index, name);
    } else {
        size_t bytes = 2 * strlen(name) + 256;
        source = xmalloc(bytes);
        if (index == NEW_ASSET_KIND_COUNT) {
            snprintf(source, bytes, "<div class=\"%s\" attrs>\n  <slot/>\n</div>\n", name);
        } else if (index == NEW_ASSET_KIND_COUNT + 1) {
            snprintf(source, bytes, "<rml>\n<head><title>%s</title></head>\n<body>\n  <div id=\"%s\"></div>\n</body>\n</rml>\n", name, name);
        } else {
            snprintf(source, bytes, ".%s {\n  display: block;\n}\n", name);
        }
    }
    if (!source) {
        snprintf(error, size, "Could not serialize asset template");
        return false;
    }
    const char *folder = workspace_asset_kind_folder(index),
               *extension = workspace_asset_kind_extension(index);
    size_t bytes = strlen("content//") + strlen(folder) + strlen(name) +
                   strlen(extension) + 1;
    char *path = xmalloc(bytes);
    snprintf(path, bytes, "content/%s/%s%s", folder, name, extension);
    *out = (workspace_asset_template_result){ path, source };
    if (size) {
        *error = 0;
    }
    return true;
}
void workspace_asset_template_free(workspace_asset_template_result *out)
{
    free(out->path);
    free(out->text);
    *out = (workspace_asset_template_result){ 0 };
}

bool workspace_asset_identity(
    const workspace_documents *documents,
    int document,
    int revision,
    char **kind,
    char **name,
    char *error,
    size_t size
)
{
    *kind = NULL;
    *name = NULL;
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (status != WORKSPACE_DOCUMENT_OK || view.revision != revision) {
        if (status == WORKSPACE_DOCUMENT_OK) {
            status = WORKSPACE_DOCUMENT_STALE;
        }
        snprintf(error, size, "%s", workspace_document_status_text(status));
        return false;
    }
    const asset_schema *schema = asset_schema_for_path(view.path);
    const char *source_kind = schema ? schema->kind : NULL;
    const char *suffix = strrchr(view.path, '.');
    if (suffix && !strcmp(suffix, ".rcss")) {
        source_kind = "stylesheet";
    }
    if (suffix && !strcmp(suffix, ".rml")) {
        source_kind = "ui";
    }
    const char *base = strrchr(view.path, '/');
    base = base ? base + 1 : view.path;
    char base_name[1024];
    snprintf(base_name, sizeof base_name, "%s", base);
    char *extension = strrchr(base_name, '.');
    if (extension) {
        *extension = 0;
    }
    *name = xstrdup(base_name);
    if (schema && schema->executable) {
        script_syntax *syntax = script_parse(view.text);
        script_catalog catalog = { 0 };
        script_analysis *analysis = script_declarations(syntax, &catalog);
        if (analysis->name && *analysis->name) {
            free(*name);
            *name = xstrdup(analysis->name);
        }
        script_analysis_free(analysis);
        script_syntax_free(syntax);
    } else if (!schema || !strcmp(schema->extension, ".kdl")) {
        av_doc *doc = av_doc_read(view.text, NULL, 0);
        if (doc) {
            const av_value *root = av_doc_root(doc);
            const char *label = av_get_str(av_obj_get(root, "name"));
            if (label) {
                free(*name);
                *name = xstrdup(label);
            }
            if (!source_kind) {
                const char *domain = av_get_str(av_obj_get(root, "domain"));
                if (domain) {
                    source_kind = domain;
                } else {
                    source_kind = av_obj_get(root, "entities") ? "scene" : "prototype";
                }
                *kind = xstrdup(source_kind);
            }
            av_doc_free(doc);
        }
    }
    if (!*kind && source_kind) {
        *kind = xstrdup(source_kind);
    }
    if (!*kind) {
        free(*name);
        *name = NULL;
        snprintf(error, size, "Document kind is unknown");
        return false;
    }
    if (size) {
        *error = 0;
    }
    return true;
}
