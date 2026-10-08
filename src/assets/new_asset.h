#ifndef CHIBI_ASSETS_NEW_ASSET_H
#define CHIBI_ASSETS_NEW_ASSET_H

#include "graph/model/behavior_asset.h"

#include <stdbool.h>
#include <stddef.h>

/* An empty asset of each kind a project's content holds, the form
 * Studio's New menu starts from. Each is built
 * as its in-memory model and written by that model's serializer. */
typedef enum
{
    NEW_ASSET_SCENE,
    NEW_ASSET_PROTOTYPE,
    NEW_ASSET_BEHAVIOR,
    NEW_ASSET_EVENT,
    NEW_ASSET_SYSTEM,
    NEW_ASSET_LIBRARY,
    NEW_ASSET_MATERIAL,
    NEW_ASSET_SHAPE,
    NEW_ASSET_FIELD,
    NEW_ASSET_SHADER_LIBRARY,
    NEW_ASSET_MODEL,
    NEW_ASSET_KIND_COUNT,
} new_asset_kind;

/* The folder under the content directory that holds the kind's files. */
const char *new_asset_folder(new_asset_kind kind);
/* Executable definitions use .chibi; declarative documents use .kdl. */
const char *new_asset_extension(new_asset_kind kind);
/* Whether `name` can name a new asset's file: a letter, then letters,
 * digits, - and _; false with why in `error`. */
bool new_asset_name_valid(const char *name, char *error, size_t size);
/* A graph asset of `domain` named `name`: its one event graph holds the
 * output node a material, shape or field needs, and a shape has 32-unit
 * half extents. */
behavior_asset *new_asset_graph(behavior_domain domain, const char *name);
/* The owned code source of an empty `kind` named `name`, as its loader reads it. */
char *new_asset_text(new_asset_kind kind, const char *name);

#endif
