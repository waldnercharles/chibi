#include "assets/model_validation.h"

#include "assets/model_source.h"
#include "authoring/image_prepare.h"
#include "authoring/mesh_prepare.h"
#include "authoring/shader_prepare.h"
#include "graph/document/problems.h"
#include "render/shader_catalog.h"
#include "runtime/images/catalog.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

bool model_content_validate(const char *root, char *error, size_t size)
{
    assert(root && error && size);
    error[0] = '\0';
    model_source_catalog source;
    bool valid = model_source_load(root, &source, error, size);
    if (valid) {
        valid = render_image_mount_content(root, error, size) &&
                authoring_mesh_mount_content(root, error, size);
    }
    model_saved_source *saved = source.saved;
    int models = source.model_count;
    const behavior_asset **catalog = source.catalog;
    for (int i = 0; valid && i < models; i++) {
        for (int j = i + 1; j < acount(saved); j++) {
            if (!strcmp(saved[i].asset->name, saved[j].asset->name) &&
                (saved[j].asset->domain == BEHAVIOR_DOMAIN_MODEL ||
                 saved[j].asset->domain == BEHAVIOR_DOMAIN_SHAPE)) {
                snprintf(
                    error,
                    size,
                    "%s: Ambiguous shape/model name '%s' also used by %s",
                    saved[i].path,
                    saved[i].asset->name,
                    saved[j].path
                );
                valid = false;
                break;
            }
        }
        if (!valid) {
            break;
        }
        compile_report report;
        compile_report_init(&report);
        behavior_asset *linked = behavior_asset_validate(saved[i].asset, catalog, acount(catalog), &report);
        const compile_diagnostic *diagnostic = compile_report_first_error(&report);
        if (diagnostic) {
            snprintf(
                error,
                size,
                "%s (node %llu): %s",
                saved[i].path,
                (unsigned long long)diagnostic->node_id,
                diagnostic->message
            );
            valid = false;
        }
        if (valid) {
            Shader binding = { .shape = saved[i].asset->name };
            shader_error material_error = { 0 };
            if (!render_shader_binding_sources_validate(&binding, NULL, catalog, acount(catalog), &material_error)) {
                snprintf(
                    error,
                    size,
                    "%s (node %llu): %s",
                    saved[i].path,
                    (unsigned long long)material_error.node,
                    material_error.message
                );
                valid = false;
            }
        }
        behavior_asset_free(linked);
        compile_report_free(&report);
    }
    model_source_free(&source);
    return valid;
}
