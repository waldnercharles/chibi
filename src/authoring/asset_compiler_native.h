#ifndef CHIBI_ASSET_COMPILER_NATIVE_H
#define CHIBI_ASSET_COMPILER_NATIVE_H
#include "authoring/asset_compiler_host.h"

/* Native service identity for persistent closure keys, false when unavailable.
 * Hash the bytes: supported filesystems can preserve timestamps on rewrites. */
bool asset_compiler_native_digest(const asset_compiler_options *, char digest[65]);
#endif
