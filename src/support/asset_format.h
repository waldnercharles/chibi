#ifndef CHIBI_ASSET_FORMAT_H
#define CHIBI_ASSET_FORMAT_H
#include <stdbool.h>
#include <string.h>

#define TEXTURE_ASSET_METADATA_EXTENSION ".texture.kdl"
/* Import configuration is not a scene, prototype or native graph document. */
static inline bool asset_texture_metadata_path(const char *path)
{
    size_t length = strlen(path),
           suffix = sizeof TEXTURE_ASSET_METADATA_EXTENSION - 1;
    return length >= suffix &&
           !strcmp(path + length - suffix, TEXTURE_ASSET_METADATA_EXTENSION);
}

#define DECLARATIVE_ASSET_EXTENSION ".kdl"
#define CODE_ASSET_EXTENSION ".chibi"
#define CODE_ASSET_METADATA_EXTENSION ".chibi.meta"
/* The most native graph documents one project's content can hold. */
#define NATIVE_GRAPH_DOCUMENTS_MAX 1024
#endif
