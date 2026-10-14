#include "host/package_identity.h"

#include "support/resources.h"
#include "support/sha256.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

void package_identity_init(package_identity *identity)
{
    *identity = (package_identity){ 0 };
}
void package_identity_free(package_identity *identity)
{
    free(identity->version);
    free(identity->build_id);
    package_identity_init(identity);
}
static bool label(yyjson_val *value, size_t max)
{
    if (!yyjson_is_str(value)) {
        return false;
    }
    size_t length = yyjson_get_len(value);
    const char *text = yyjson_get_str(value);
    if (!length || length > max || memchr(text, 0, length)) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char)text[i] <= 32 || (unsigned char)text[i] == 127) {
            return false;
        }
    }
    return true;
}
static bool unique_fields(yyjson_val *root)
{
    int kinds = 0, versions = 0, builds = 0;
    size_t i, count;
    yyjson_val *key, *value;
    yyjson_obj_foreach(root, i, count, key, value)
    {
        (void)value;
        kinds += yyjson_equals_str(key, "kind");
        versions += yyjson_equals_str(key, "package_version");
        builds += yyjson_equals_str(key, "build_id");
    }
    return kinds == 1 && versions == 1 && builds == 1;
}
package_identity_status package_identity_load(
    package_identity *identity,
    const char *directory,
    char *error,
    size_t error_size
)
{
    assert(identity && error && error_size);
    package_identity_free(identity);
    error[0] = 0;
    directory = directory ? directory : resource_root();
    if (!directory[0]) {
        snprintf(error, error_size, "SDK resource directory is empty");
        return PACKAGE_IDENTITY_INVALID;
    }
    char path[4096];
    int length = snprintf(path, sizeof path, "%s/package.json", directory);
    if (length < 0 || (size_t)length >= sizeof path) {
        snprintf(error, error_size, "SDK package manifest path exceeds4095bytes");
        return PACKAGE_IDENTITY_INVALID;
    }
    /* Nonblocking open also bounds invalid FIFO/device inputs. Only regular
     * manifest files are accepted, including ordinary SDK symlink installations. */
    int file = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (file < 0) {
        snprintf(error, error_size, "SDK package manifest unavailable: %s", strerror(errno));
        return PACKAGE_IDENTITY_UNAVAILABLE;
    }
    struct stat info;
    if (fstat(file, &info)) {
        int failure = errno;
        close(file);
        snprintf(error, error_size, "SDK package manifest unavailable: %s", strerror(failure));
        return PACKAGE_IDENTITY_UNAVAILABLE;
    }
    if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
        info.st_size > PACKAGE_IDENTITY_MANIFEST_MAX) {
        close(file);
        snprintf(error, error_size, "SDK package manifest must be a nonempty regular file at most8MiB");
        return PACKAGE_IDENTITY_INVALID;
    }
    size_t expected = (size_t)info.st_size, at = 0;
    char *bytes = xmalloc(expected + 1);
    bool readable = true;
    while (at <= expected) {
        ssize_t got = read(file, bytes + at, expected + 1 - at);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got < 0) {
            readable = false;
            break;
        }
        if (!got) {
            break;
        }
        at += (size_t)got;
    }
    close(file);
    if (!readable || at != expected) {
        free(bytes);
        snprintf(error, error_size, "SDK package manifest could not be read as one complete bounded file");
        return PACKAGE_IDENTITY_UNAVAILABLE;
    }
    yyjson_read_err parse = { 0 };
    yyjson_doc *doc = yyjson_read_opts(bytes, at, 0, NULL, &parse);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *version = yyjson_obj_get(root, "package_version");
    yyjson_val *build = yyjson_obj_get(root, "build_id");
    yyjson_val *kind = yyjson_obj_get(root, "kind");
    bool valid = yyjson_is_obj(root) && unique_fields(root) &&
                 (yyjson_equals_str(kind, "sdk") ||
                  yyjson_equals_str(kind, "web-author")) &&
                 label(version, 128) && label(build, 256);
    if (!valid) {
        snprintf(
            error,
            error_size,
            "Invalid SDK package manifest: %s",
            doc         ? "expected kind sdk/web-author and unique nonempty "
                          "package_version/build_id strings"
            : parse.msg ? parse.msg
                        : "malformed JSON"
        );
        yyjson_doc_free(doc);
        free(bytes);
        return PACKAGE_IDENTITY_INVALID;
    }
    identity->version = xstrdup(yyjson_get_str(version));
    identity->build_id = xstrdup(yyjson_get_str(build));
    sha256_hex(bytes, at, identity->manifest_sha256);
    yyjson_doc_free(doc);
    free(bytes);
    return PACKAGE_IDENTITY_OK;
}
