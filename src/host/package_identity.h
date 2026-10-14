#ifndef CHIBI_PACKAGE_IDENTITY_H
#define CHIBI_PACKAGE_IDENTITY_H
#include <stddef.h>

enum
{
    PACKAGE_IDENTITY_MANIFEST_MAX = 8 * 1024 * 1024
};
typedef enum
{
    PACKAGE_IDENTITY_OK,
    PACKAGE_IDENTITY_UNAVAILABLE,
    PACKAGE_IDENTITY_INVALID,
} package_identity_status;
typedef struct
{
    char *version, *build_id; /* Owned host package_version and build_id. */
    char manifest_sha256[65]; /* Exact package.json bytes, not canonical JSON. */
} package_identity;
void package_identity_init(package_identity *);
void package_identity_free(package_identity *);
/* NULL directory uses resource_root(). Reads a bounded regular package.json,
 * requiring sdk/web-author kind and nonempty package_version/build_id. Clears previous
 * identity before loading; failures retain no definitive identity. Development
 * builds without an installed SDK manifest return UNAVAILABLE, never placeholders. */
package_identity_status package_identity_load(
    package_identity *,
    const char *resource_directory,
    char *error,
    size_t error_size
);
#endif
