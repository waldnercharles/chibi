#pragma once
#include "assets/ui_source.h"
#include "runtime/player.h"
#include "runtime/ui/documents.h"

/* Offline expanded markup/styles and exact static resource dependencies.
 * Initialized outputs are replaced only on success. Resource byte publication
 * is supplied by the application preparation host. */
bool authoring_ui_prepare(
    const char *content,
    const char *name,
    prepared_ui_document *,
    ui_source_resource **,
    char *error,
    size_t size
);
bool authoring_ui_assets_prepare(
    const char *content,
    const scene *const *,
    int scene_count,
    player_asset **,
    char *error,
    size_t size
);
