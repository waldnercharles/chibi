/* ui_check: every RmlUi warning of a ui asset, with its block's line. */
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

static const char BROKEN[] = "rml: |\n"
                             "  <div id=\"a\">\n"
                             "    <p data-if=\"save.x +\">x</p>\n"
                             "    <span>open\n"
                             "  </div>\n"
                             "rcss: |\n"
                             "  .a { color: red; }\n"
                             "  .b { width: 10bogus; }\n";

/* Whether the messages are, in order, parts and lines with text starting
 * as given. */
typedef struct expected
{
    ui_check_part part;
    int line;
    const char *text;
} expected;

static bool messages_are(const ui_check_message *messages, const expected *list, int count)
{
    bool ok = acount(messages) == count;
    for (int i = 0; ok && i < count; i++) {
        ok = messages[i].part == list[i].part && messages[i].line == list[i].line &&
             !strncmp(messages[i].text, list[i].text, strlen(list[i].text));
    }
    for (int i = 0; !ok && i < acount(messages); i++) {
        printf(
            "  part %d line %d: %s\n",
            messages[i].part,
            messages[i].line,
            messages[i].text
        );
    }
    return ok;
}

void test_ui_check()
{
    char root[] = "/tmp/chibi-ui-check-XXXXXX", folder[64];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(!mkdir(folder, 0700));
    write_file(root, "Button.yaml", "rml: <button data-if=\"open +\"><slot/></button>\n");

    ui_check_message *messages = NULL;
    char error[200] = "";
    bool ok = ui_check(root, "Broken", BROKEN, &messages, error, sizeof error);
    static const expected BROKEN_MESSAGES[] = {
        { UI_CHECK_RML, 4, "Closing tag 'div' mismatched on line 4 was expecting 'span'." },
        { UI_CHECK_RML, 2, "Error in data expression at 8." },
        { UI_CHECK_RCSS, 2, "Syntax error parsing property declaration 'width: 10bogus;'" },
    };
    test_check(ok && messages_are(messages, BROKEN_MESSAGES, 3), "ui_check reports every RmlUi warning with its block's line");
    ui_check_messages_free(messages);

    messages = NULL;
    ok = ui_check(
        root,
        "Host",
        "rml: |\n  <div>\n    <use ui=\"Button\">Go</use>\n  </div>\n",
        &messages,
        error,
        sizeof error
    );
    static const expected HOST_MESSAGES[] = {
        { UI_CHECK_WIDGETS, 0, "Error in data expression at 6." },
    };
    test_check(
        ok && messages_are(messages, HOST_MESSAGES, 1),
        "ui_check reports what only the markup with its widgets expanded shows"
    );
    ui_check_messages_free(messages);

    messages = NULL;
    ok = ui_check(root, "Lost", "rml: <use ui=\"Missing\"/>\n", &messages, error, sizeof error);
    test_check(!ok && strstr(error, "Missing"), "ui_check refuses an asset whose widget is missing");
    ui_check_messages_free(messages);

    messages = NULL;
    ok = ui_check(
        "projects/studio/content",
        "Shell",
        "rml: |\n"
        "  <body data-class-dark=\"save.theme == 'dark'\"><slot/></body>\n"
        "rcss: '@import \"theme/tokens.rcss\";'\n",
        &messages,
        error,
        sizeof error
    );
    test_check(ok && messages_are(messages, NULL, 0), "ui_check checks a page with its body attributes and imports");
    ui_check_messages_free(messages);

    char command[96];
    snprintf(command, sizeof command, "rm -rf %s", root);
    assert(system(command) == 0);

    rml_backend *backend = rml_backend_create(1, 1, 1);
    const char *names[512];
    int count = rml_property_names(names, 512);
    bool color = false, margin = false;
    for (int i = 0; i < count && i < 512; i++) {
        color |= !strcmp(names[i], "color");
        margin |= !strcmp(names[i], "margin");
    }
    rml_backend_destroy(backend);
    test_check(count > 100 && count <= 512 && color && margin, "RmlUi lists the RCSS properties and shorthands it registers");
}
