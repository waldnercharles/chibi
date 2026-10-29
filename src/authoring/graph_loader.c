#include "authoring/behavior_loader.h"
#include "authoring/graph_loader_internal.h"
#include "authoring/graph_sources.h"
#include "authoring/shader_prepare.h"
#include "graph/document/serialize.h"
#include "render/shader_catalog.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void load_error(const char *path, const char *stage, const char *message)
{
    fprintf(stderr, "Could not load graph asset %s (%s): %s\n", path, stage, message);
}

int authoring_graphs_prepare(preview_world *world, prototype_preview *preview, const char *content_root)
{
    if (!world || !content_root || !content_root[0]) {
        return -1;
    }
    authoring_graph_source *sources = NULL;
    if (!authoring_graph_sources_load(content_root, &sources)) {
        return -1;
    }
    preview_definition **pending = NULL;
    int loaded = 0;
    bool failed = false;
    for (int i = 0; i < acount(sources); i++) {
        behavior_asset *asset = sources[i].asset;
        preview_definition *definition = preview_definition_new(asset->name, asset);
        if (definition) {
            sources[i].asset = NULL;
            preview_definition_set_path(definition, sources[i].path);
            apush(pending, definition);
        } else {
            load_error(sources[i].path, "create editable definition", graph_last_error());
            failed = true;
        }
    }
    authoring_graph_sources_free(sources);
    const behavior_asset **models = NULL;
    for (int i = 0; i < acount(pending); i++) {
        if (pending[i]->working->domain == BEHAVIOR_DOMAIN_MODEL) {
            apush(models, pending[i]->working);
        }
    }
    bool models_ready = false;
    for (int pass = 0; pass < 4; pass++) {
        if (pass == 2) {
            shader_error model_error = { 0 };
            models_ready = !acount(models) ||
                           render_shader_models_upsert(models, acount(models), &model_error);
            if (!models_ready) {
                load_error(content_root, "model dependencies", model_error.message);
                failed = true;
            }
        }
        for (int i = 0; i < acount(pending); i++) {
            preview_definition *definition = pending[i];
            if (!definition) {
                continue;
            }
            behavior_domain domain = definition->working->domain;
            if (domain == BEHAVIOR_DOMAIN_MODEL && !models_ready) {
                continue;
            }
            int stage = domain == BEHAVIOR_DOMAIN_EVENT ? 3
                      : domain == BEHAVIOR_DOMAIN_MODEL ? 2
                      : (domain == BEHAVIOR_DOMAIN_MATERIAL ||
                         domain == BEHAVIOR_DOMAIN_SHAPE ||
                         domain == BEHAVIOR_DOMAIN_FIELD)
                          ? 1
                          : 0;
            if (stage != pass) {
                continue;
            }
            if (preview_add_definition(world, definition) != 0) {
                const compile_diagnostic *error = compile_report_first_error(
                    &definition->diagnostics
                );
                load_error(definition->path, "register", error ? error->message : "definition rejected");
                failed = true;
                continue;
            }
            pending[i] = NULL;
            if (preview && prototype_preview_register_behavior(
                               preview,
                               definition->name,
                               definition->working
                           ) != 0) {
                load_error(definition->path, "preview", "registration rejected; see preview diagnostic above");
                preview_remove_definition(world, definition->name);
                failed = true;
                continue;
            }
            loaded++;
        }
    }
    afree(models);
    for (int i = 0; i < acount(pending); i++) {
        preview_definition_free(pending[i]);
    }
    afree(pending);
    if (failed) {
        return -1;
    }
    return loaded;
}
