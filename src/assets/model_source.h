#ifndef CHIBI_MODEL_SOURCE_H
#define CHIBI_MODEL_SOURCE_H
#include "graph/model/behavior_asset.h"

#include <stddef.h>
typedef struct
{
    behavior_asset *asset;
    char *path, *text;
} model_saved_source;
typedef struct
{
    model_saved_source *saved;
    const behavior_asset **catalog;
    behavior_asset **linked;
    int model_count;
} model_source_catalog;
bool model_source_load(const char *content_root, model_source_catalog *result, char *error, size_t size);
void model_source_free(model_source_catalog *source);
#endif
