#include "authoring/ui_prepare.h"

#include "assets/ui_document.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool authoring_ui_prepare(
    const char *content,
    const char *name,
    prepared_ui_document *result,
    ui_source_resource **dependencies,
    char *error,
    size_t size
)
{
    assert(content && result && dependencies && (!size || error));
    if (!name || !*name || strlen(name) > 1024 || strstr(name, "..") ||
        strchr(name, '/') || strchr(name, '\\')) {
        if (size) {
            snprintf(error, size, "UI document name is invalid");
        }
        return false;
    }
    char directory[4096];
    if (snprintf(directory, sizeof directory, "%s/ui", content) >= (int)sizeof directory) {
        if (size) {
            snprintf(error, size, "UI document directory exceeds limit");
        }
        return false;
    }
    ui_document source = { 0 };
    char **imports = NULL, **uses = NULL;
    prepared_ui_document next = { 0 };
    ui_source_resource *resources = NULL;
    bool valid = ui_document_load(directory, name, &imports, &source, error, size);
    if (!valid) {
        goto done;
    }
    next.body = source.body ? xstrdup(source.body) : NULL;
    if (!ui_document_expand(&source, name, directory, false, &uses, error, size)) {
        valid = false;
        goto done;
    }
    char *page = NULL;
    if (next.body) {
        size_t bytes = strlen(next.body) + strlen(source.rml) + sizeof "<body ></body>";
        page = xmalloc(bytes);
        snprintf(page, bytes, "<body %s>%s</body>", next.body, source.rml);
    }
    valid = ui_source_resources(page ? page : source.rml, source.rcss, &resources, error, size);
    free(page);
    if (!valid) {
        goto done;
    }
    bool default_font = false;
    for (int i = 0; i < acount(resources); i++) {
        default_font |= resources[i].kind == UI_SOURCE_RESOURCE_FONT_FAMILY &&
                        !strcmp(resources[i].text, "Inter");
    }
    if (!default_font) {
        apush(
            resources,
            ((ui_source_resource){ .kind = UI_SOURCE_RESOURCE_FONT_FAMILY,
                                   .text = xstrdup("Inter") })
        );
    }
    next.name = xstrdup(name);
    next.markup = source.rml;
    next.style = source.rcss;
    source.rml = source.rcss = NULL;
    prepared_ui_document_free(result);
    *result = next;
    next = (prepared_ui_document){ 0 };
    ui_source_resources_free(*dependencies);
    *dependencies = resources;
    resources = NULL;
done:
    for (int i = 0; i < acount(imports); i++) {
        free(imports[i]);
    }
    for (int i = 0; i < acount(uses); i++) {
        free(uses[i]);
    }
    afree(imports);
    afree(uses);
    ui_document_free(&source);
    prepared_ui_document_free(&next);
    ui_source_resources_free(resources);
    return valid;
}
