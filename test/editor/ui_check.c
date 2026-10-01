/* ui_check: every RmlUi warning of a ui asset, at the line that wrote it. */
#include "render/ui_check.h"

#include "support/array.h"
#include "test/suite_internal.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *root, const char *name, const char *text)
{
    char path[256];
    snprintf(path, sizeof path, "%s/ui/%s", root, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static const char BROKEN_RML[] = "<div id=\"a\">\n"
                                 "  <p data-if=\"save.x +\">x</p>\n"
                                 "  <span>open\n"
                                 "</div>\n";
static const char BROKEN_RCSS[] = "@import \"theme/bad.rcss\";\n"
                                  ".a {\n"
                                  "  color: red;\n"
                                  "  .b { width: 10bogus; }\n"
                                  "}\n";

/* A warning expected in order: its part, the end of its source's name
 * ("" for the checked asset), its line, and how its text starts. */
typedef struct expected
{
    ui_check_part part;
    const char *source;
    int line;
    const char *text;
} expected;

static bool source_is(const char *source, const char *ending)
{
    size_t length = source ? strlen(source) : 0, end = strlen(ending);
    return ending[0] ? length >= end && !strcmp(source + length - end, ending) : !source;
}

static bool messages_are(const ui_check_message *messages, const expected *list, int count)
{
    bool ok = acount(messages) == count;
    for (int i = 0; ok && i < count; i++) {
        ok = messages[i].part == list[i].part &&
             source_is(messages[i].source, list[i].source) &&
             messages[i].line == list[i].line &&
             !strncmp(messages[i].text, list[i].text, strlen(list[i].text));
    }
    for (int i = 0; !ok && i < acount(messages); i++) {
        printf(
            "  part %d %s line %d: %s\n",
            messages[i].part,
            messages[i].source ? messages[i].source : "-",
            messages[i].line,
            messages[i].text
        );
    }
    return ok;
}

static void property_names()
{
    rml_backend *backend = rml_backend_create(1, 1, 1);
    char **names = NULL;
    ui_check_property_names(&names);
    bool color = false, margin = false;
    for (int i = 0; i < acount(names); i++) {
        color |= !strcmp(names[i], "color");
        margin |= !strcmp(names[i], "margin");
        free(names[i]);
    }
    int count = acount(names);
    afree(names);
    rml_backend_destroy(backend);
    test_check(count > 100 && color && margin, "RmlUi lists the RCSS properties and shorthands it registers");
}

void test_ui_check()
{
    char root[] = "/tmp/chibi-ui-check-XXXXXX", folder[64];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(!mkdir(folder, 0700));
    snprintf(folder, sizeof folder, "%s/ui/theme", root);
    assert(!mkdir(folder, 0700));
    write_file(root, "theme/bad.rcss", "body { color: red; }\n.c { height: 3bogus; }\n");
    write_file(root, "Button.rml", "<button>\n  <span data-if=\"open +\"><slot/></span>\n</button>\n");

    ui_check_message *messages = NULL;
    char error[200] = "";
    bool ok = ui_check(root, "Broken", BROKEN_RML, BROKEN_RCSS, &messages, error, sizeof error);
    static const expected BROKEN_MESSAGES[] = {
        { UI_CHECK_RML, "", 4, "Closing tag 'div' mismatched on line 4 was expecting 'span'." },
        { UI_CHECK_RML, "", 2, "Error in data expression at 8." },
        { UI_CHECK_RCSS, "theme/bad.rcss", 2, "Syntax error parsing property declaration 'height: 3bogus;'" },
        { UI_CHECK_RCSS, "", 4, "Syntax error parsing property declaration 'width: 10bogus;'" },
    };
    test_check(ok && messages_are(messages, BROKEN_MESSAGES, 4), "ui_check reports every RmlUi warning at its file's line, or its import's");
    ui_check_messages_free(messages);

    messages = NULL;
    ok = ui_check(root, "Host", "<div>\n  <use ui=\"Button\">Go</use>\n</div>\n", NULL, &messages, error, sizeof error);
    static const expected HOST_MESSAGES[] = {
        { UI_CHECK_WIDGETS, "Button", 2, "Error in data expression at 6." },
    };
    test_check(ok && messages_are(messages, HOST_MESSAGES, 1), "ui_check reports what only the markup with its widgets expanded shows, at the widget's line");
    ui_check_messages_free(messages);

    messages = NULL;
    ok = ui_check(root, "Lost", "<use ui=\"Missing\"/>\n", NULL, &messages, error, sizeof error);
    test_check(!ok && strstr(error, "Missing"), "ui_check refuses an asset whose widget is missing");
    ui_check_messages_free(messages);

    messages = NULL;
    ok = ui_check(
        "projects/studio/content",
        "Shell",
        "<body data-class-dark=\"save.theme == 'dark'\"><slot/></body>\n",
        "@import \"theme/tokens.rcss\";\n",
        &messages,
        error,
        sizeof error
    );
    test_check(ok && messages_are(messages, NULL, 0), "ui_check checks a page with its body attributes and imports");
    ui_check_messages_free(messages);

    messages = NULL;
    ok = ui_check(
        root,
        "Counters",
        "<div>\n"
        "  <b>{{ save.ekX > 0 ? save.ekX : 0 }}</b>\n"
        "  <b>{{ save.ekH > 0 ? save.ekH : 0 }}</b>\n"
        "</div>\n",
        NULL,
        &messages,
        error,
        sizeof error
    );
    test_check(ok && messages_are(messages, NULL, 0), "ui_check accepts valid ternary text bindings over dynamic save fields");
    ui_check_messages_free(messages);

    char command[96];
    snprintf(command, sizeof command, "rm -rf %s", root);
    assert(system(command) == 0);
    property_names();
}
