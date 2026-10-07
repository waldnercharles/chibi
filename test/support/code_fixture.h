#ifndef CHIBI_TEST_CODE_FIXTURE_H
#define CHIBI_TEST_CODE_FIXTURE_H

#include "assets/code_catalog.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Authored source fixtures resolve signatures from their own project's
 * content tree, including forward library references and event payloads. */
static inline behavior_asset *test_project_asset(const char *path, const char *text)
{
    const char *content = strstr(path, "/content/");
    size_t length = content ? (size_t)(content - path) + strlen("/content")
                            : strlen("content");
    assert(content || !strncmp(path, "content/", strlen("content/")));
    char *root = xmalloc(length + 1);
    memcpy(root, path, length);
    root[length] = 0;
    code_catalog *catalog = code_catalog_load(root);
    char error[256];
    behavior_asset *asset = code_catalog_load_behavior(catalog, text, error, sizeof error);
    if (!asset) {
        fprintf(stderr, "%s: %s\n", path, error);
    }
    code_catalog_free(catalog);
    free(root);
    return asset;
}

#endif
