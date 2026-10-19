#include "compiler_fixture.h"
#include "docs_markdown_source.h"
#include "package_core_source.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Four cases on bytecode and the compiled backend. One preparation of the
 * shipped markdown and site packages with core:strings; each call renders or
 * lays out one page of at most a few hundred bytes with a
 * 2,000,000-instruction budget. No filesystem, processes or frames. */
enum
{
    BUDGET = 2000000
};
static compiler_fixture fixture;
static arena input, detached;

static void cleanup()
{
    compiler_fixture_free(&fixture);
    fixture = (compiler_fixture){ 0 };
    arena_free(&input);
    arena_free(&detached);
}
static bool prepare()
{
    const source_package_input files[] = {
        { "/sdk/core/strings/Strings.chibi", CORE_STRINGS_SOURCE, strlen(CORE_STRINGS_SOURCE) },
        { "/compiler/markdown/Markdown.chibi",
          DOCS_MARKDOWN_SOURCE,
          strlen(DOCS_MARKDOWN_SOURCE) },
        { "/compiler/site/Site.chibi", DOCS_SITE_SOURCE, strlen(DOCS_SITE_SOURCE) },
    };
    REQUIRE(compiler_fixture_prepare(&fixture, files, 3));
    return true;
}
static pin_value text(const char *value)
{
    size_t bytes = strlen(value) + 1;
    char *copy = arena_alloc(&input, bytes);
    memcpy(copy, value, bytes);
    return (pin_value){ .type = PIN_STR, .str = copy };
}
static pin_value strings(const char *const *items, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(items[i]);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
/* The pieces a generator function returns, joined; NULL when it fails. */
static char *call(int backend, const char *identity, const char *name, const pin_value *arguments, int count)
{
    pin_value out = { 0 };
    if (!compiler_fixture_call(&fixture, backend, identity, name, arguments, count, BUDGET, &detached, &out)) {
        return NULL;
    }
    const pin_array *pieces = out.ptr;
    size_t size = 1;
    for (int i = 0; i < pieces->count; i++) {
        size += strlen(pieces->items[i].str);
    }
    char *html = xmalloc(size);
    html[0] = '\0';
    for (int i = 0; i < pieces->count; i++) {
        strcat(html, pieces->items[i].str);
    }
    return html;
}
/* markdown's lines as a string[] argument, as chibi doc passes them. */
static pin_value lines(const char *markdown)
{
    int count = 0;
    const char *items[64];
    for (const char *line = markdown; *line; count++) {
        const char *end = strchr(line, '\n');
        size_t size = end ? (size_t)(end - line) : strlen(line);
        char *copy = arena_alloc(&input, size + 1);
        memcpy(copy, line, size);
        copy[size] = '\0';
        items[count] = copy;
        line = end ? end + 1 : line + size;
    }
    return strings(items, count);
}
/* The page at `page` rendered with the site map below, as one string. */
static char *render(int backend, const char *markdown, const char *page)
{
    static const char *const sources[] = { "README.md",
                                           "docs/code-language.md",
                                           "docs/examples.md" };
    static const char *const outputs[] = { "index.html", "code-language.html", "examples.html" };
    pin_value arguments[] = { lines(markdown),
                              text(page),
                              strings(sources, 3),
                              strings(outputs, 3) };
    return call(backend, "project:markdown", "Render", arguments, 4);
}
static bool renders(int backend, const char *markdown, const char *page, const char *expected)
{
    char *html = render(backend, markdown, page);
    bool same = html && !strcmp(html, expected);
    if (html && !same) {
        fprintf(stderr, "rendered:\n%s\nexpected:\n%s\n", html, expected);
    }
    free(html);
    return same;
}

/* Headings carry GitHub's anchors; paragraphs join their lines; text is
 * escaped; fenced code keeps its lines and names its language. */
TEST_CASE(docs_markdown_renders_blocks)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        REQUIRE(renders(
            b,
            "# Tests and `@test`\n\nA <b> & \"c\"\nsecond line.\n\n```chibi\nx "
            ":= a < b;\n```\n",
            "docs/code-language.md",
            "<h1 id=\"tests-and-test\">Tests and <code>@test</code></h1>\n"
            "<p>A &lt;b&gt; &amp; &quot;c&quot; second line.</p>\n"
            "<pre><code class=\"language-chibi\">x := a &lt; "
            "b;\n</code></pre>\n"
        ));
        REQUIRE(renders(
            b,
            "- one\n  continued\n- **two**\n\n1. first\n2. *second*\n\n> "
            "quoted\n> text\n",
            "README.md",
            "<ul>\n<li>one "
            "continued</li>\n<li><strong>two</strong></li>\n</ul>\n"
            "<ol>\n<li>first</li>\n<li><em>second</em></li>\n</ol>\n"
            "<blockquote><p>quoted text</p></blockquote>\n"
        ));
    }
    return true;
}

/* Pipe tables split at pipes outside code spans and escaped pipes. */
TEST_CASE(docs_markdown_renders_tables)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        REQUIRE(renders(
            b,
            "| Command | Contract |\n| --- | --- |\n| `a | b` | one \\| two "
            "|\n",
            "README.md",
            "<table>\n<thead>\n<tr><th>Command</th><th>Contract</th></tr>\n</"
            "thead>\n<tbody>\n"
            "<tr><td><code>a | b</code></td><td>one | "
            "two</td></tr>\n</tbody>\n</table>\n"
        ));
    }
    return true;
}

/* Links between published sources point at their pages, relative to the
 * linking page's folder and keeping anchors and queries; external and
 * same-page links stay; a link to a file the site does not publish, or with
 * any other scheme, keeps only its text, and so does a stray bracket. */
TEST_CASE(docs_markdown_maps_links_to_pages)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        REQUIRE(renders(
            b,
            "See [the guide](docs/code-language.md#tests), "
            "[examples](./docs/examples.md), "
            "[web](https://example.com/a?b=1&c), [here](#top) and "
            "[plans](PLAN.md).\n",
            "README.md",
            "<p>See <a href=\"code-language.html#tests\">the guide</a>, "
            "<a href=\"examples.html\">examples</a>, "
            "<a href=\"https://example.com/a?b=1&amp;c\">web</a>, <a "
            "href=\"#top\">here</a> "
            "and plans.</p>\n"
        ));
        REQUIRE(renders(
            b,
            "[home](../README.md) [`code`](examples.md) [out](../../x.md)\n",
            "docs/code-language.md",
            "<p><a href=\"index.html\">home</a> <a "
            "href=\"examples.html\"><code>code</code></a> "
            "out</p>\n"
        ));
        REQUIRE(renders(
            b,
            "[run](javascript:alert(1)) [quoted](https://a.example/\"x\") "
            "[q](docs/examples.md?x=1#t) "
            "xs[i] (see [guide](docs/examples.md))\n",
            "README.md",
            "<p>run) <a href=\"https://a.example/&quot;x&quot;\">quoted</a> "
            "<a href=\"examples.html?x=1#t\">q</a> xs[i] (see <a "
            "href=\"examples.html\">guide</a>)</p>\n"
        ));
    }
    return true;
}

/* A page's head names it and the site, links the stylesheet and lists every
 * page under its section, marking this one. */
TEST_CASE(docs_site_lays_out_navigation)
{
    REQUIRE(prepare());
    static const char *const sections[] = { "Overview", "Learn", "Learn" };
    static const char *const titles[] = { "Chibi", "Language", "Tests & more" };
    static const char *const outputs[] = { "index.html", "language.html", "tests.html" };
    for (int b = 0; b < 2; b++) {
        pin_value arguments[] = { text("Chibi"),
                                  strings(sections, 3),
                                  strings(titles, 3),
                                  strings(outputs, 3),
                                  { .type = PIN_I32, .i = 2 } };
        char *head = call(b, "project:site", "Head", arguments, 5);
        char *foot = call(b, "project:site", "Foot", NULL, 0);
        bool laid_out = head && foot &&
                        strstr(head, "<title>Tests &amp; more · Chibi</title>") &&
                        strstr(head, "<link rel=\"stylesheet\" href=\"style.css\">") &&
                        strstr(
                            head,
                            "<h2>Overview</h2>\n<ul>\n<li><a "
                            "href=\"index.html\">Chibi</a></li>\n</ul>\n"
                            "<h2>Learn</h2>\n<ul>\n<li><a "
                            "href=\"language.html\">Language</a></li>\n"
                            "<li><a href=\"tests.html\" "
                            "aria-current=\"page\">Tests &amp; more</a></li>\n"
                            "</ul>\n</nav>\n<main>\n"
                        ) &&
                        !strcmp(foot, "</main>\n</body>\n</html>\n");
        free(head);
        free(foot);
        REQUIRE(laid_out);
    }
    return true;
}

void test_language_docs_markdown()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(docs_markdown_renders_blocks);
    RUN_TEST_CASE(docs_markdown_renders_tables);
    RUN_TEST_CASE(docs_markdown_maps_links_to_pages);
    RUN_TEST_CASE(docs_site_lays_out_navigation);
    pu_clear_setup();
}
