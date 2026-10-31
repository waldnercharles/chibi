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
    NEW_ASSET_RELATIONSHIP,
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
/* The longest name a new asset may have, in bytes. */
#define NEW_ASSET_NAME_MAX 63
/* Whether `name` can name a new asset's file: a letter, then letters,
 * digits, - and _, at most NEW_ASSET_NAME_MAX bytes; false with why in
 * `error`. */
bool new_asset_name_valid(const char *name, char *error, size_t size);
/* A graph asset of `domain` named `name`: its one event graph holds the
 * output node a material, shape or field needs, and a shape has 32-unit
 * half extents. */
behavior_asset *new_asset_graph(behavior_domain domain, const char *name);
/* The owned code source of an empty `kind` named `name`, as its loader reads it. */
char *new_asset_text(new_asset_kind kind, const char *name);
/* Whether `path` is a folder and not a symbolic link to one, so a file made
 * inside it stays in the project; false with why in `error`. */
bool new_asset_real_folder(const char *path, char *error, size_t size);
/* Writes the empty `kind` named `name` into its folder under `content`,
 * creating the folder when it is missing, and returns the owned file path.
 * NULL with why in `error`: errno is then EINVAL for an invalid name, EEXIST
 * for its file or for a model or shape of that name, since the two kinds
 * share names, and otherwise the storage error. */
char *new_asset_create(const char *content, new_asset_kind kind, const char *name, char *error, size_t size);

#endif
