#include "support/asset_text.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *compiler;
static char root[PATH_MAX], log_path[PATH_MAX];

/* A file or folder of the repository, copied to the same place below root
 * once; copying a folder again would nest the second copy in the first. */
static bool copy(const char *relative)
{
    char from[PATH_MAX], to[PATH_MAX], folder[PATH_MAX];
    int length = snprintf(from, sizeof from, "%s/%s", tool_root(), relative);
    assert(length > 0 && length < (int)sizeof from);
    length = snprintf(to, sizeof to, "%s/%s", root, relative);
    assert(length > 0 && length < (int)sizeof to);
    if (!access(to, F_OK)) {
        return true;
    }
    strcpy(folder, to);
    *strrchr(folder, '/') = '\0';
    const char *parents[] = { "/bin/mkdir", "-p", folder, NULL };
    const char *files[] = { "/bin/cp", "-R", from, to, NULL };
    return !tool_run(parents, root, log_path) && !tool_run(files, root, log_path);
}

/* docs/site.kdl and every page, package folder and grammar it lists. A
 * manifest that does not read copies no pages, and an input that leaves the
 * root is not copied; the compiler says why it refuses either. */
static bool copy_site()
{
    char manifest[PATH_MAX], error[256];
    int length = snprintf(manifest, sizeof manifest, "%s/docs/site.kdl", root);
    assert(length > 0 && length < (int)sizeof manifest);
    bool copied = copy("docs/site.kdl");
    char *text = copied ? tool_read(manifest) : NULL;
    av_doc *doc = text ? av_doc_read(text, error, sizeof error) : NULL;
    const av_value *sections = av_obj_get(av_doc_root(doc), "sections");
    for (size_t i = 0; copied && i < av_arr_size(sections); i++) {
        const av_value *pages = av_obj_get(av_arr_get(sections, i), "pages");
        for (size_t j = 0; copied && j < av_arr_size(pages); j++) {
            const av_value *page = av_arr_get(pages, j);
            const char *inputs[] = { av_get_str(av_obj_get(page, "source")),
                                     av_get_str(av_obj_get(page, "packages")),
                                     av_get_str(av_obj_get(page, "grammar")) };
            for (int k = 0; copied && k < 3; k++) {
                copied = !inputs[k] || strstr(inputs[k], "..") || copy(inputs[k]);
            }
        }
    }
    av_doc_free(doc);
    free(text);
    return copied;
}

/* The real compiler's --docs builds the repository's published site, so every
 * ```chibi example on its pages still parses, and each whole package still
 * analyzes, before a change lands. It builds copies of the listed pages, which
 * keeps the written site out of the source tree. */
TEST_CASE(published_site_builds)
{
    REQUIRE(copy_site());
    const char *docs[] = { compiler, "--project-root", root, "--docs", NULL };
    REQUIRE(tool_run(docs, root, log_path) == 0);
    return true;
}

void test_tool_published_docs(const char *binary)
{
    compiler = binary;
    strcpy(root, "/tmp/chibi-published-docs-XXXXXX");
    assert(mkdtemp(root));
    int length = snprintf(log_path, sizeof log_path, "%s/docs.log", root);
    assert(length > 0 && length < (int)sizeof log_path);
    RUN_TEST_CASE(published_site_builds);
    tool_remove_tree(root);
}
