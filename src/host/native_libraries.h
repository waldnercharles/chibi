#ifndef CHIBI_HOST_NATIVE_LIBRARIES_H
#define CHIBI_HOST_NATIVE_LIBRARIES_H

#include "execution/native.h"

/* The native libraries a game ships in its natives folder: each
 * <Library>.so (.dylib on macOS, .dll on Windows) exports its manifest as
 * chibi_natives_<Library>. Other files are ignored. */
typedef struct native_libraries native_libraries;
/* Loads every library in `folder`; a missing folder holds none. NULL with why
 * when a library cannot be loaded or has no manifest. */
native_libraries *native_libraries_open(const char *folder, char *error, size_t size);
/* The libraries' manifests, which live until the libraries close. */
const execution_native_manifest *const *native_libraries_manifests(
    const native_libraries *,
    int *count
);
void native_libraries_close(native_libraries *);

#endif
