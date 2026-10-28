#include "compiler_fixture.h"
#include "docs_markdown_source.h"
#include "package_core_source.h"
#include "package_snapshot_chibi_source.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fourteen cases on bytecode and the compiled backend. One preparation of the
 * shipped site generator packages with core:strings per case; each call
 * renders, lays out, styles or reports on one small page with a
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
        { "/compiler/reference/Reference.chibi",
          DOCS_REFERENCE_SOURCE,
          strlen(DOCS_REFERENCE_SOURCE) },
        { "/compiler/grammar/Grammar.chibi", DOCS_GRAMMAR_SOURCE, strlen(DOCS_GRAMMAR_SOURCE) },
        { "/compiler/compiler_syntax/Syntax.chibi",
          COMPILER_SYNTAX_CHIBI_SOURCE,
          strlen(COMPILER_SYNTAX_CHIBI_SOURCE) },
        { "/compiler/syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          strlen(SYNTAX_CHIBI_SOURCE) },
    };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, files, 7, &COMPILER_SYNTAX_DEFINITION_PACK, 1));
    return true;
}
static pin_value text(const char *value)
{
    value = value ? value : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, value, strlen(value));
    if (error) {
        abort();
    }
    return result;
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
        size += strlen(pin_text(pieces->items[i]).bytes);
    }
    char *html = xmalloc(size);
    html[0] = '\0';
    for (int i = 0; i < pieces->count; i++) {
        strcat(html, pin_text(pieces->items[i]).bytes);
    }
    return html;
}
/* markdown's lines as a string[] argument, as chibi doc passes them. */
static pin_value lines(const char *markdown)
{
    int count = 0;
    size_t capacity = 1;
    for (const char *at = markdown; *at; at++) {
        capacity += *at == '\n';
    }
    const char **items = xmalloc(capacity * sizeof(*items));
    for (const char *line = markdown; *line; count++) {
        const char *end = strchr(line, '\n');
        size_t size = end ? (size_t)(end - line) : strlen(line);
        char *copy = arena_alloc(&input, size + 1);
        memcpy(copy, line, size);
        copy[size] = '\0';
        items[count] = copy;
        line = end ? end + 1 : line + size;
    }
    pin_value result = strings(items, count);
    free(items);
    return result;
}
/* What markdown's `name` makes of the page at `page` with the site map below,
 * as one string. */
static char *page_call(int backend, const char *name, const char *markdown, const char *page)
{
    static const char *const sources[] = { "README.md",
                                           "docs/code-language.md",
                                           "docs/examples.md" };
    static const char *const outputs[] = { "index.html", "code-language.html", "examples.html" };
    pin_value arguments[] = { lines(markdown),
                              text(page),
                              strings(sources, 3),
                              strings(outputs, 3) };
    return call(backend, "project:markdown", name, arguments, 4);
}
/* Whether a call's joined result is expected; frees it. */
static bool joined_is(char *joined, const char *expected)
{
    bool same = joined && !strcmp(joined, expected);
    if (joined && !same) {
        fprintf(stderr, "got:\n%s\nexpected:\n%s\n", joined, expected);
    }
    free(joined);
    return same;
}
static bool renders(int backend, const char *markdown, const char *page, const char *expected)
{
    return joined_is(page_call(backend, "Render", markdown, page), expected);
}
static bool reports(int backend, const char *markdown, const char *page, const char *expected)
{
    return joined_is(page_call(backend, "Unpublished", markdown, page), expected);
}

/* Headings carry GitHub's anchors; paragraphs join their lines; text is
 * escaped; fenced code keeps its lines and names its language. Lines lose
 * their spaces, tabs and carriage returns at either end. */
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
            "<pre><code class=\"language-chibi\">x <span "
            "class=\"chibi-operator\">:=</span> a <span "
            "class=\"chibi-operator\">&lt;</span> "
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
        REQUIRE(
            renders(b, "# Title\t\r\n\r\n\tOne \t\r\ntwo\r\n", "README.md", "<h1 id=\"title\">Title</h1>\n<p>One two</p>\n")
        );
    }
    return true;
}

/* Strip only the highlighter's spans and decode the four HTML escapes. Every
 * remaining byte must be the original fence, including whitespace and UTF-8. */
static bool preserves_source(const char *html, const char *source)
{
    const char *prefix = "<pre><code class=\"language-chibi\">";
    if (!html || strncmp(html, prefix, strlen(prefix))) {
        return false;
    }
    html += strlen(prefix);
    bool span = false;
    while (*source || !strncmp(html, "</span>", 7)) {
        if (!strncmp(html, "<span class=\"chibi-", 19)) {
            const char *end = strstr(html, "\">");
            if (span || !end || end == html + 19 || !strncmp(end + 2, "</span>", 7)) {
                return false;
            }
            for (const char *at = html + 19; at < end; at++) {
                if (*at < 'a' || *at > 'z') {
                    return false;
                }
            }
            span = true;
            html = end + 2;
            continue;
        }
        if (!strncmp(html, "</span>", 7)) {
            if (!span) {
                return false;
            }
            span = false;
            html += 7;
            continue;
        }
        const char *entity = NULL;
        switch (*source) {
            case '&': entity = "&amp;"; break;
            case '<': entity = "&lt;"; break;
            case '>': entity = "&gt;"; break;
            case '"': entity = "&quot;"; break;
            default: break;
        }
        if (entity) {
            if (strncmp(html, entity, strlen(entity))) {
                return false;
            }
            html += strlen(entity);
        } else if (*html++ != *source) {
            return false;
        }
        source++;
    }
    return !span && !strcmp(html, "</code></pre>\n");
}
static char *fence(int backend, const char *source)
{
    size_t size = strlen(source) + sizeof "```chibi\n```\n";
    char *markdown = xmalloc(size);
    snprintf(markdown, size, "```chibi\n%s```\n", source);
    char *html = page_call(backend, "Render", markdown, "README.md");
    free(markdown);
    return html;
}
static bool has_token(const char *html, const char *role, const char *escaped)
{
    char token[512];
    int count = snprintf(token, sizeof token, "<span class=\"chibi-%s\">%s</span>", role, escaped);
    return count > 0 && count < (int)sizeof token && strstr(html, token);
}

/* Canonical grammatical roles, not capitalization or neighboring words.
 * Unresolved names need not type-check; ordinary bindings stay neutral. */
TEST_CASE(docs_markdown_highlights_chibi_fences)
{
    REQUIRE(prepare());
    static const char library[] = "Example :: library {\n"
                                  "/* first <&>\n\tsecond café */\n"
                                  "Title :: \"café <&> 🧭\"\n"
                                  "Ratio :: 1.5\n"
                                  " \t\n"
                                  "Print :: (value: Position) -> int {\n"
                                  "\treturn value.x + 2  // kept <&>\n"
                                  "}\n}\n";
    static const char
        package[] = "package demo\n"
                    "import shapes \"core:shapes\"\n"
                    "Position :: struct { x: number; }\n"
                    "Result :: union { Ready | Missing }\n"
                    "Limit :: (1 + 2)\n"
                    "@test\n"
                    "Work :: (string: int, p: shapes.Position) -> bool {\n"
                    "    number := int(1)\n"
                    "    total: [string: int]\n"
                    "    copy := number + string\n"
                    "    object.int; object.number; object.int(2)\n"
                    "    answer := Work(value: string)\n"
                    "    letter := '<'\n"
                    "    for i in 0..<2 { number += i }\n"
                    "    return true\n"
                    "}\n";
    static const char
        comments[] = "package demo\n"
                     "@ /* café <&> */ test\n"
                     "Fn /* a */ :: /* b */ (arg /* c */ : int) {\n"
                     "    call /* a\n b */ (arg)\n"
                     "    reader; (arg)\n"
                     "}\n";
    for (int b = 0; b < 2; b++) {
        char *html = fence(b, library);
        REQUIRE(preserves_source(html, library));
        REQUIRE(has_token(html, "namespace", "Example"));
        REQUIRE(has_token(html, "keyword", "library"));
        REQUIRE(has_token(html, "constant", "Title"));
        REQUIRE(has_token(html, "constant", "Ratio"));
        REQUIRE(has_token(html, "string", "&quot;café &lt;&amp;&gt; 🧭&quot;"));
        REQUIRE(has_token(html, "number", "1.5"));
        REQUIRE(has_token(html, "comment", "/* first &lt;&amp;&gt;\n\tsecond café */"));
        REQUIRE(has_token(html, "function", "Print"));
        REQUIRE(has_token(html, "type", "Position"));
        REQUIRE(has_token(html, "builtin", "int"));
        REQUIRE(has_token(html, "property", "x"));
        REQUIRE(has_token(html, "operator", "+"));
        REQUIRE(strstr(html, "(value: "));
        free(html);

        html = fence(b, package);
        REQUIRE(preserves_source(html, package));
        REQUIRE(has_token(html, "namespace", "demo"));
        REQUIRE(has_token(html, "namespace", "shapes"));
        REQUIRE(has_token(html, "type", "Position"));
        REQUIRE(has_token(html, "type", "Result"));
        REQUIRE(has_token(html, "builtin", "number"));
        REQUIRE(has_token(html, "builtin", "bool"));
        REQUIRE(has_token(html, "constant", "Ready"));
        REQUIRE(has_token(html, "constant", "Limit"));
        REQUIRE(has_token(html, "constant", "true"));
        REQUIRE(!has_token(html, "function", "Limit"));
        REQUIRE(has_token(html, "attribute", "test"));
        REQUIRE(has_token(html, "function", "Work"));
        REQUIRE(has_token(html, "function", "int"));
        REQUIRE(has_token(html, "property", "int"));
        REQUIRE(has_token(html, "property", "number"));
        REQUIRE(has_token(html, "property", "value"));
        REQUIRE(has_token(html, "string", "'&lt;'"));
        REQUIRE(has_token(html, "operator", "..&lt;"));
        REQUIRE(strstr(html, "(string: <span class=\"chibi-builtin\">int</span>"));
        REQUIRE(strstr(html, "    number <span class=\"chibi-operator\">:=</span>"));
        REQUIRE(
            strstr(html, "copy <span class=\"chibi-operator\">:=</span> number <span class=\"chibi-operator\">+</span> string\n")
        );
        REQUIRE(!has_token(html, "namespace", "object"));
        /* The type named number is builtin; this loop uses the local value. */
        REQUIRE(strstr(html, " { number <span class=\"chibi-operator\">+=</span> i }"));
        free(html);

        html = fence(b, comments);
        REQUIRE(preserves_source(html, comments));
        REQUIRE(has_token(html, "attribute", "test"));
        REQUIRE(has_token(html, "function", "Fn"));
        REQUIRE(has_token(html, "function", "call"));
        REQUIRE(!has_token(html, "function", "reader"));
        REQUIRE(has_token(html, "comment", "/* a\n b */"));
        free(html);

        static const char crlf[] = "package demo\r\nValue :: 1  \r\n";
        html = fence(b, crlf);
        REQUIRE(preserves_source(html, crlf));
        REQUIRE(has_token(html, "constant", "Value"));
        REQUIRE(has_token(html, "number", "1"));
        free(html);
    }
    return true;
}

/* Recovery is presentation only. A broken token or incomplete program cannot
 * delete source, emit HTML, or discard highlighting of valid subtrees. The
 * publication integration separately rejects the original invalid snippets. */
TEST_CASE(docs_markdown_recovers_incomplete_fences)
{
    REQUIRE(prepare());
    static const char *const fragments[] = {
        "package demo\nValue :: 2\n/* <&> café\n",
        "package demo\nText :: \"first <&>\nsecond café\"\n",
        "package demo\nText :: \"\\q\"\n",
        "package demo\nPartial :: (arg: int) {\n\treturn arg + 2\n",
        "Undecided :: (\n",
        "Fn :: (arg: int) -> int\n",
        "Limit :: (1 + 2)\n",
    };
    for (int b = 0; b < 2; b++) {
        for (size_t i = 0; i < sizeof fragments / sizeof *fragments; i++) {
            char *html = fence(b, fragments[i]);
            REQUIRE(preserves_source(html, fragments[i]));
            if (i < 4) {
                REQUIRE(has_token(html, "keyword", "package"));
            }
            if (i == 0 || i == 3) {
                REQUIRE(has_token(html, "number", "2"));
            }
            /* Complete declaration/signature fragments borrow only grammar
             * context. No synthetic package or body may reach the output. */
            if (i == 5) {
                REQUIRE(has_token(html, "function", "Fn"));
                REQUIRE(has_token(html, "builtin", "int"));
                REQUIRE(!has_token(html, "constant", "Fn"));
            }
            if (i == 6) {
                REQUIRE(has_token(html, "constant", "Limit"));
                REQUIRE(!has_token(html, "function", "Limit"));
            }
            free(html);
        }
    }
    return true;
}

/* Other languages and unlabelled fences remain plain; empty Chibi fences
 * emit neither an empty span nor text for missing syntax nodes. */
TEST_CASE(docs_markdown_leaves_other_fences_plain)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        REQUIRE(renders(
            b,
            "```c\n"
            "return 2 < 3; // café\n"
            "```\n"
            "\n"
            "```\n"
            "\t<&> 🧭  \n"
            "```\n"
            "\n"
            "```chibi\n"
            "```\n",
            "README.md",
            "<pre><code class=\"language-c\">return 2 &lt; 3; // café\n"
            "</code></pre>\n"
            "<pre><code>\t&lt;&amp;&gt; 🧭  \n"
            "</code></pre>\n"
            "<pre><code class=\"language-chibi\"></code></pre>\n"
        ));
    }
    return true;
}

/* Contents preserve heading hierarchy and omit code. Long pages expose native
 * section disclosures, with each section closed before the next one. */
TEST_CASE(docs_markdown_lists_contents)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        REQUIRE(renders(
            b,
            "# Title\n\n## First `one`\n\n```sh\n## not a heading\n```\n\n"
            "### Second part\n\n#### Deep\n### Sibling\n## Last\n",
            "README.md",
            "<nav class=\"contents\" aria-label=\"On this page\">\n"
            "<details open>\n<summary>On this page</summary>\n<ul>\n"
            "<li class=\"section\"><a href=\"#first-one\">First "
            "<code>one</code></a>\n<ul>\n"
            "<li class=\"sub\"><a href=\"#second-part\">Second part</a>\n<ul>\n"
            "<li class=\"sub\"><a href=\"#deep\">Deep</a></li>\n</ul>\n</li>\n"
            "<li class=\"sub\"><a "
            "href=\"#sibling\">Sibling</a></li>\n</ul>\n</li>\n"
            "<li class=\"section\"><a href=\"#last\">Last</a></li>\n"
            "</ul>\n</details>\n</nav>\n"
            "<h1 id=\"title\">Title</h1>\n"
            "<h2 id=\"first-one\">First <code>one</code></h2>\n"
            "<pre><code class=\"language-sh\">## not a heading\n</code></pre>\n"
            "<h3 id=\"second-part\">Second part</h3>\n"
            "<h4 id=\"deep\">Deep</h4>\n"
            "<h3 id=\"sibling\">Sibling</h3>\n<h2 id=\"last\">Last</h2>\n"
        ));
        char *long_page = page_call(
            b,
            "Render",
            "# Title\nIntro.\n## One\nText.\n### Child\n- item\n## Two\n"
            "```chibi\n## fake\n```\n## Three\n## Four\n## Five\n## Six\n"
            "Last paragraph.\n# Appendix\nOutside.\n",
            "README.md"
        );
        REQUIRE(long_page);
        REQUIRE(strstr(
            long_page,
            "<h1 id=\"title\">Title</h1>\n<p>Intro.</p>\n"
            "<div class=\"section-controls\" hidden>"
        ));
        REQUIRE(strstr(long_page, "data-sections=\"open\">Expand all</button>"));
        REQUIRE(strstr(long_page, "data-sections=\"close\">Collapse all</button>"));
        REQUIRE(strstr(
            long_page,
            "<details class=\"doc-section\" open>\n<summary>"
            "<h2 id=\"one\">One</h2>\n</summary>\n<p>Text.</p>\n"
            "<h3 "
            "id=\"child\">Child</h3>\n<ul>\n<li>item</li>\n</ul>\n</details>\n"
            "<details class=\"doc-section\" open>\n<summary>"
            "<h2 id=\"two\">Two</h2>\n</summary>\n"
            "<pre><code class=\"language-chibi\">## "
            "fake\n</code></pre>\n</details>\n"
        ));
        REQUIRE(strstr(
            long_page,
            "<p>Last paragraph.</p>\n</details>\n"
            "<h1 id=\"appendix\">Appendix</h1>\n<p>Outside.</p>\n"
        ));
        REQUIRE(!strstr(long_page, "href=\"#fake\""));
        free(long_page);

        /* Five real sections and one fenced heading do not cross the cutoff. */
        char *short_page = page_call(
            b,
            "Render",
            "## One\n## Two\n## Three\n## Four\n## Five\n```\n## fake\n```\n",
            "README.md"
        );
        REQUIRE(short_page);
        REQUIRE(!strstr(short_page, "doc-section"));
        REQUIRE(!strstr(short_page, "section-controls"));
        free(short_page);

        /* Two sections become collapsible at the line-count cutoff. */
        char length_page[256] = "## First\n## Last\n";
        size_t used = strlen(length_page);
        for (int line = 2; line < 160; line++) {
            length_page[used++] = '\n';
        }
        length_page[used] = '\0';
        char *by_length = page_call(b, "Render", length_page, "README.md");
        REQUIRE(by_length);
        REQUIRE(strstr(by_length, "<h2 id=\"last\">Last</h2>\n</summary>\n</details>\n"));
        free(by_length);
        length_page[used - 1] = '\0';
        by_length = page_call(b, "Render", length_page, "README.md");
        REQUIRE(by_length);
        REQUIRE(!strstr(by_length, "doc-section"));
        free(by_length);
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
        REQUIRE(
            renders(b, "[Generated](code-language.html#types) [Unlisted](missing.html)\n", "docs/code-language.md", "<p><a href=\"code-language.html#types\">Generated</a> Unlisted</p>\n")
        );
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

/* Each link written as text is reported, in order, at the line that holds
 * it, in any block and inside emphasis; code and escaped brackets hold no
 * links, and links the site keeps are not reported. */
TEST_CASE(docs_markdown_reports_unpublished_links)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        REQUIRE(reports(
            b,
            "# Home and [plans](PLAN.md)\n\n"
            "See [the guide](examples.md), [notes](notes.md) and\n"
            "**[notes](notes.md)** twice: [notes](notes.md).\n\n"
            "```\n[code](code.md)\n```\n"
            "`[span](span.md)` \\[escaped](escaped.md) "
            "[web](https://a.example)\n\n"
            "| A | B |\n| --- | --- |\n| [cell](cell.md) | [out](../x.md) |\n\n"
            "- one\n  [item](item.md#a)\n\n"
            "> [ftp](ftp://a.example/x) [home](#top)\n",
            "docs/code-language.md",
            "docs/code-language.md:1: link to PLAN.md is not published; "
            "written as text\n"
            "docs/code-language.md:3: link to notes.md is not published; "
            "written as text\n"
            "docs/code-language.md:4: link to notes.md is not published; "
            "written as text\n"
            "docs/code-language.md:4: link to notes.md is not published; "
            "written as text\n"
            "docs/code-language.md:13: link to cell.md is not published; "
            "written as text\n"
            "docs/code-language.md:13: link to ../x.md is not published; "
            "written as text\n"
            "docs/code-language.md:16: link to item.md#a is not published; "
            "written as text\n"
            "docs/code-language.md:18: link to ftp://a.example/x is not "
            "published; written as text\n"
        ));
        REQUIRE(reports(b, "See [the guide](docs/code-language.md#tests).\n", "README.md", ""));
        /* A line break inside a link's target leaves it at the line before. */
        REQUIRE(reports(
            b,
            "See [x](\ny.md) and [z](z.md).\n",
            "README.md",
            "README.md:1: link to  y.md is not published; written as text\n"
            "README.md:2: link to z.md is not published; written as text\n"
        ));
    }
    return true;
}

/* The header names the site without repeating page navigation. A skip link
 * reaches the focusable content. Only sections with several pages get labels,
 * and both grouped and standalone pages can carry the current marker. */
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
                        strstr(head, "<a class=\"skip\" href=\"#main\">Skip to content</a>\n<header") &&
                        strstr(
                            head,
                            "<header class=\"bar\">\n<a class=\"site\" "
                            "href=\"index.html\">Chibi</a>\n"
                            "<span "
                            "class=\"context\">Documentation</span>\n</header>"
                        ) &&
                        !strstr(head, "aria-label=\"Sections\"") &&
                        !strstr(head, "<h2>Overview</h2>") &&
                        strstr(
                            head,
                            "<nav class=\"pages\" aria-label=\"Pages\">\n<ul>\n"
                            "<li><a href=\"index.html\">Chibi</a></li>\n"
                            "<li class=\"page-group\"><h2>Learn</h2>\n<ul>\n"
                            "<li><a href=\"language.html\">Language</a></li>\n"
                            "<li><a href=\"tests.html\" "
                            "aria-current=\"page\">Tests &amp; more</a></li>\n"
                            "</ul>\n</li>\n</ul>\n</nav>\n"
                        ) &&
                        strstr(head, "<main id=\"main\" tabindex=\"-1\">\n") &&
                        strstr(foot, "</main>\n</div>\n<script>\n") &&
                        strstr(foot, "querySelector('.section-controls')") &&
                        strstr(foot, "controls.hidden = false") &&
                        strstr(foot, "button.dataset.sections === 'open'") &&
                        strstr(foot, "matchMedia('(max-width: 64rem)')") &&
                        strstr(foot, "decodeURIComponent(hash.slice(1))") &&
                        strstr(foot, "parent.tagName === 'DETAILS') parent.open = true") &&
                        strstr(foot, "addEventListener('hashchange', visit)") &&
                        strstr(foot, "visit();") &&
                        strstr(foot, "</script>\n</body>\n</html>\n");
        free(head);
        free(foot);
        REQUIRE(laid_out);
        arguments[4].i = 0;
        head = call(b, "project:site", "Head", arguments, 5);
        REQUIRE(head);
        REQUIRE(strstr(head, "<li><a href=\"index.html\" aria-current=\"page\">Chibi</a></li>"));
        REQUIRE(!strstr(head, "href=\"tests.html\" aria-current="));
        free(head);
    }
    return true;
}

/* sRGB relative luminance for the six-digit colors in the shipped palette. */
static double color_channel(unsigned byte)
{
    double value = byte / 255.0;
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}
static double color_luminance(unsigned rgb)
{
    return 0.2126 * color_channel((rgb >> 16) & 255) +
           0.7152 * color_channel((rgb >> 8) & 255) +
           0.0722 * color_channel(rgb & 255);
}
static bool readable_color(const char *palette, const char *property)
{
    const char *foreground = strstr(palette, property);
    const char *background = strstr(palette, "--syntax-background:");
    unsigned fg, bg;
    if (!foreground || !background ||
        sscanf(foreground + strlen(property), " #%x", &fg) != 1 ||
        sscanf(background + strlen("--syntax-background:"), " #%x", &bg) != 1) {
        return false;
    }
    double a = color_luminance(fg), b = color_luminance(bg);
    return (fmax(a, b) + 0.05) / (fmin(a, b) + 0.05) >= 4.5;
}

/* Exact GitHub Light Default/Dark Default 6.3.5 syntax roles, including the
 * code backgrounds they were designed for. Neutral members are intentional.
 * Every foreground retains at least 4.5:1 contrast in both palettes. */
TEST_CASE(docs_site_follows_color_scheme)
{
    REQUIRE(prepare());
    static const char *const properties[] = { "--text:",  "--muted:", "--page:",
                                              "--panel:", "--line:",  "--link:",
                                              "--code:" };
    static const struct
    {
        const char *role;
        unsigned light, dark;
    } roles[] = {
        { "namespace", 0x953800, 0xffa657 }, { "type", 0x953800, 0xffa657 },
        { "builtin", 0x0550ae, 0x79c0ff },   { "function", 0x8250df, 0xd2a8ff },
        { "property", 0x1f2328, 0xe6edf3 },  { "constant", 0x0550ae, 0x79c0ff },
        { "attribute", 0x0550ae, 0x79c0ff }, { "keyword", 0xcf222e, 0xff7b72 },
        { "operator", 0xcf222e, 0xff7b72 },  { "number", 0x0550ae, 0x79c0ff },
        { "string", 0x0a3069, 0xa5d6ff },    { "comment", 0x6e7781, 0x8b949e },
    };
    for (int b = 0; b < 2; b++) {
        char *style = call(b, "project:site", "Style", NULL, 0);
        REQUIRE(style);
        char *light = strstr(style, ":root { color-scheme: light dark;");
        char *dark = strstr(style, "@media (prefers-color-scheme: dark) {\n  :root {");
        char *rules = dark ? strstr(dark, "\n}\n") : NULL;
        bool themed = light && dark && light < dark && rules && !strchr(rules, '#');
        for (size_t i = 0; themed && i < sizeof properties / sizeof *properties; i++) {
            char *defined = strstr(light, properties[i]);
            char *redefined = strstr(dark, properties[i]);
            themed = defined && defined < dark && redefined && redefined < rules;
        }
        for (size_t i = 0; themed && i < sizeof roles / sizeof *roles; i++) {
            char expected[80], property[64], selector[128];
            snprintf(property, sizeof property, "--syntax-%s:", roles[i].role);
            snprintf(expected, sizeof expected, "%s #%06x;", property, roles[i].light);
            char *defined = strstr(light, expected);
            snprintf(expected, sizeof expected, "%s #%06x;", property, roles[i].dark);
            char *redefined = strstr(dark, expected);
            snprintf(
                selector,
                sizeof selector,
                "pre .chibi-%s { color: var(--syntax-%s); }",
                roles[i].role,
                roles[i].role
            );
            themed = defined && defined < dark && redefined && redefined < rules &&
                     strstr(style, selector) && readable_color(light, property) &&
                     readable_color(dark, property);
        }
        const char *background = light ? strstr(light, "--syntax-background: #ffffff;")
                                       : NULL;
        const char *foreground = light ? strstr(light, "--syntax-foreground: #1f2328;")
                                       : NULL;
        themed = themed && background && background < dark && foreground &&
                 foreground < dark && strstr(dark, "--syntax-background: #0d1117;") &&
                 strstr(dark, "--syntax-foreground: #e6edf3;") &&
                 strstr(style, "background: var(--syntax-background)") &&
                 strstr(style, "color: var(--syntax-foreground)") &&
                 strstr(style, "Copyright (c) 2020 Primer") &&
                 strstr(style, "Copyright (c) 2018 GitHub Inc.") &&
                 strstr(style, "Permission is hereby granted, free of charge");
        free(style);
        REQUIRE(themed);
    }
    return true;
}

/* The bar keeps one row of a fixed height, which the anchor offset clears.
 * A narrow window keeps the page list, which holds the only links to a
 * section's later pages, and draws it where it is in the page, so focus
 * order and visual order agree. */
TEST_CASE(docs_site_keeps_navigation_in_reach)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        char *style = call(b, "project:site", "Style", NULL, 0);
        REQUIRE(style);
        char *narrow = strstr(style, "@media (max-width: 48rem) {");
        char *end = narrow ? strchr(narrow, '\n') : NULL;
        if (end) {
            *end = '\0';
        }
        bool reachable = end && strstr(style, "; height: 3.5rem;") &&
                         strstr(style, "scroll-margin-top: 4.5rem;") &&
                         !strstr(style, "flex-wrap: wrap") &&
                         strstr(narrow, ".pages li { display: inline-block;") &&
                         !strstr(narrow, ".pages { display: none") &&
                         !strstr(narrow, "order:");
        free(style);
        REQUIRE(reachable);
    }
    return true;
}

/* The package reference lists each package once, with its import path, and
 * each pub declaration's signature, up to its body, with the comment written
 * right above it. */
TEST_CASE(docs_reference_lists_declarations)
{
    REQUIRE(prepare());
    static const char *const paths[] = { "shapes/A.chibi", "shapes/B.chibi" };
    static const char *const files[] = { "package shapes",
                                         "",
                                         "// The area of a square.",
                                         "pub Area :: (side: int,",
                                         "             scale: int) -> int {",
                                         "    return side * side;",
                                         "}",
                                         "package shapes",
                                         "pub Sides: int : 4;",
                                         "// Not attached.",
                                         "",
                                         "pub Shape :: struct { sides: int; }",
                                         "pub Greeting :: \"Hello {}\";",
                                         "Hidden :: 3;",
                                         "@test",
                                         "Checks :: () {",
                                         "}",
                                         "@test Also :: () {}",
                                         "pub Vec :: struct {",
                                         "    // Horizontal.",
                                         "    x: int;",
                                         "}" };
    for (int b = 0; b < 2; b++) {
        pin_value starts = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY };
        pin_array *offsets = pin_array_snapshot_allocate(&input, PIN_I32, NULL, 2);
        offsets->items[0] = (pin_value){ .type = PIN_I32, .i = 0 };
        offsets->items[1] = (pin_value){ .type = PIN_I32, .i = 7 };
        starts.ptr = offsets;
        static const char *const none[] = { "README.md" };
        pin_value arguments[] = { text("core"),     strings(paths, 2),
                                  starts,           strings(files, 22),
                                  text("packages"), strings(none, 1),
                                  strings(none, 1) };
        char *html = call(b, "project:reference", "Render", arguments, 7);
        bool listed = html &&
                      strstr(html, "<h1 id=\"the-core-packages\">The core packages</h1>") &&
                      strstr(html, "<h2 id=\"shapes\">shapes</h2>\n<p>Import it as <code>core:shapes</code>.</p>") &&
                      strstr(
                          html,
                          "<h3 id=\"area\">Area</h3>\n<pre><code "
                          "class=\"language-chibi\">"
                          "<span class=\"chibi-keyword\">pub</span>"
                          " "
                          "<span class=\"chibi-function\">Area</span>"
                          " "
                          "<span class=\"chibi-operator\">::</span>"
                          " (side: "
                          "<span class=\"chibi-builtin\">int</span>"
                          ", scale: "
                          "<span class=\"chibi-builtin\">int</span>"
                          ") "
                          "<span class=\"chibi-operator\">-&gt;</span>"
                          " "
                          "<span class=\"chibi-builtin\">int</span>"
                          "\n</code></pre>\n<p>The area of a square.</p>"
                      ) &&
                      strstr(
                          html,
                          "<h3 id=\"sides\">Sides</h3>\n<pre><code "
                          "class=\"language-chibi\">"
                          "<span class=\"chibi-keyword\">pub</span>"
                          " "
                          "<span class=\"chibi-constant\">Sides</span>"
                          ": "
                          "<span class=\"chibi-builtin\">int</span>"
                          " : "
                          "<span class=\"chibi-number\">4</span>"
                          ";\n"
                      ) &&
                      strstr(
                          html,
                          "<span class=\"chibi-keyword\">pub</span>"
                          " "
                          "<span class=\"chibi-type\">Shape</span>"
                          " "
                          "<span class=\"chibi-operator\">::</span>"
                          " "
                          "<span class=\"chibi-keyword\">struct</span>"
                          " { "
                          "<span class=\"chibi-property\">sides</span>"
                          ": "
                          "<span class=\"chibi-builtin\">int</span>"
                          "; }\n"
                      ) &&
                      strstr(
                          html,
                          "<span class=\"chibi-keyword\">pub</span>"
                          " "
                          "<span class=\"chibi-constant\">Greeting</span>"
                          " "
                          "<span class=\"chibi-operator\">::</span>"
                          " "
                          "<span class=\"chibi-string\">&quot;Hello "
                          "{}&quot;</span>"
                          ";\n"
                      ) &&
                      !strstr(html, "Hidden") && !strstr(html, "Checks") &&
                      !strstr(html, "Also") &&
                      strstr(
                          html,
                          "<span class=\"chibi-keyword\">pub</span>"
                          " "
                          "<span class=\"chibi-type\">Vec</span>"
                          " "
                          "<span class=\"chibi-operator\">::</span>"
                          " "
                          "<span class=\"chibi-keyword\">struct</span>"
                          " {\n    "
                          "<span class=\"chibi-comment\">// Horizontal.</span>"
                          "\n    "
                          "<span class=\"chibi-property\">x</span>"
                          ": "
                          "<span class=\"chibi-builtin\">int</span>"
                          ";\n}\n"
                      ) &&
                      !strstr(html, "Not attached") &&
                      !strstr(strstr(html, "<h2") + 3, "<h2");
        if (!listed && html) {
            fprintf(stderr, "reference:\n%s\n", html);
        }
        free(html);
        REQUIRE(listed);
    }
    return true;
}

/* The grammar page shows each rule of the grammar's rules block with the
 * comments above it, and nothing outside the block. */
TEST_CASE(docs_grammar_lists_rules)
{
    REQUIRE(prepare());
    static const char *const grammar[] = { "  word: ($) => $.identifier,",
                                           "  rules: {",
                                           "    // A file is one item.",
                                           "    source_file: ($) =>",
                                           "      seq($.item),",
                                           "    _item: ($) => \"x\",",
                                           "  },",
                                           "  extras: ($) => []," };
    static const char *const none[] = { "README.md" };
    for (int b = 0; b < 2; b++) {
        pin_value arguments[] = { strings(grammar, 8),
                                  text("grammar.js"),
                                  strings(none, 1),
                                  strings(none, 1) };
        char *html = call(b, "project:grammar", "Render", arguments, 4);
        bool listed = html &&
                      strstr(
                          html,
                          "<h3 id=\"source_file\">source_file</h3>\n<p>A file "
                          "is one item.</p>\n"
                          "<pre><code class=\"language-js\">source_file: ($) "
                          "=&gt;\n  seq($.item),\n</code></pre>"
                      ) &&
                      strstr(html, "<h3 id=\"_item\">_item</h3>") &&
                      !strstr(html, "word") && !strstr(html, "extras");
        if (!listed && html) {
            fprintf(stderr, "grammar:\n%s\n", html);
        }
        free(html);
        REQUIRE(listed);
    }
    return true;
}

/* The generated pages report a link they write as text at the file and line
 * of the comment that holds it; a comment they do not publish reports none. */
TEST_CASE(docs_generated_pages_report_unpublished_links)
{
    REQUIRE(prepare());
    static const char *const paths[] = { "shapes/A.chibi", "shapes/B.chibi" };
    static const char *const files[] = {
        "package shapes",
        "// Not [attached](a.md).",
        "",
        "// The [area](area.md).",
        "pub Area :: () -> int {",
        "    return 1;",
        "}",
        "package shapes",
        "// Four [sides](PLAN.md#sides), as [Area](#area).",
        "pub Sides: int : 4;"
    };
    static const char *const grammar[] = {
        "  rules: {",
        "    // A [file](file.md) is one item.",
        "    source_file: ($) => $.item,",
        "  },"
    };
    static const char *const none[] = { "README.md" };
    for (int b = 0; b < 2; b++) {
        pin_value starts = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY };
        pin_array *offsets = pin_array_snapshot_allocate(&input, PIN_I32, NULL, 2);
        offsets->items[0] = (pin_value){ .type = PIN_I32, .i = 0 };
        offsets->items[1] = (pin_value){ .type = PIN_I32, .i = 7 };
        starts.ptr = offsets;
        pin_value reference[] = { text("core"),     strings(paths, 2),
                                  starts,           strings(files, 10),
                                  text("packages"), strings(none, 1),
                                  strings(none, 1) };
        REQUIRE(joined_is(
            call(b, "project:reference", "Unpublished", reference, 7),
            "packages/shapes/A.chibi:4: link to area.md is not published; "
            "written as text\n"
            "packages/shapes/B.chibi:2: link to PLAN.md#sides is not "
            "published; written as text\n"
        ));
        pin_value rules[] = { strings(grammar, 4),
                              text("grammar.js"),
                              strings(none, 1),
                              strings(none, 1) };
        REQUIRE(
            joined_is(call(b, "project:grammar", "Unpublished", rules, 4), "grammar.js:2: link to file.md is not published; written as text\n")
        );
    }
    return true;
}

void test_language_docs_markdown()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(docs_markdown_renders_blocks);
    RUN_TEST_CASE(docs_markdown_highlights_chibi_fences);
    RUN_TEST_CASE(docs_markdown_recovers_incomplete_fences);
    RUN_TEST_CASE(docs_markdown_leaves_other_fences_plain);
    RUN_TEST_CASE(docs_markdown_lists_contents);
    RUN_TEST_CASE(docs_markdown_renders_tables);
    RUN_TEST_CASE(docs_markdown_maps_links_to_pages);
    RUN_TEST_CASE(docs_markdown_reports_unpublished_links);
    RUN_TEST_CASE(docs_site_lays_out_navigation);
    RUN_TEST_CASE(docs_site_follows_color_scheme);
    RUN_TEST_CASE(docs_site_keeps_navigation_in_reach);
    RUN_TEST_CASE(docs_reference_lists_declarations);
    RUN_TEST_CASE(docs_grammar_lists_rules);
    RUN_TEST_CASE(docs_generated_pages_report_unpublished_links);
    pu_clear_setup();
}
