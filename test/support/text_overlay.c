#include "support/text_overlay.h"

#include "support/file_watch.h"
#include "support/text_edit.h"
#include "support/text_file.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static bool reads(const char *path, const char *expected)
{
    char *text = text_file_read(path);
    bool same = text && !strcmp(text, expected);
    free(text);
    return same;
}

void test_text_overlay()
{
    char root[] = "/tmp/chibi-text-overlay-XXXXXX";
    assert(mkdtemp(root));
    char path[256], spelled[256];
    snprintf(path, sizeof path, "%s/ui/Panel.rml", root);
    snprintf(spelled, sizeof spelled, "%s/ui/../ui/./Panel.rml", root);
    char folder[256];
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(!mkdir(folder, 0755));
    write_file(path, "<p>saved</p>\n");

    file_watch w = { 0 };
    test_check(file_watch_changed(&w, path, 0) && reads(path, "<p>saved</p>\n"), "a file reads from disk without an overlay");
    text_overlay_set(spelled, "<p>unsaved</p>\n");
    test_check(reads(path, "<p>unsaved</p>\n"), "an overlay reads in place of its file, however the path is spelled");
    test_check(file_watch_changed(&w, path, 0.1), "a file watch sees an overlay at once");
    test_check(!file_watch_changed(&w, path, 0.2), "an unchanged overlay is not a change");
    text_overlay_set(path, "<p>again</p>\n");
    test_check(file_watch_changed(&w, path, 0.3) && reads(path, "<p>again</p>\n"), "a file watch sees an overlay edit at once");
    text_overlay_set(path, NULL);
    test_check(file_watch_changed(&w, path, 0.4) && reads(path, "<p>saved</p>\n"), "removing an overlay reads the file again");
    text_overlay_set(path, "x");
    text_overlay_clear();
    test_check(!text_overlay_text(path) && text_overlay_revision(path) == 0, "clearing removes every overlay");

    char cwd[1024];
    assert(getcwd(cwd, sizeof cwd));
    assert(!chdir(root));
    text_overlay_set("ui/Panel.rml", "relative");
    test_check(reads(path, "relative"), "a relative path matches its absolute spelling");
    text_overlay_clear();
    assert(!chdir(cwd));

    const char *source = "<div class=\"a\">x</div>";
    text_edit edits[] = {
        { 15, 16, "yz" },
        { 12, 13, "b" },
        { 0, 0, "<!-- c -->" },
    };
    char *edited = text_edits_apply(source, edits, 3);
    test_check(!strcmp(edited, "<!-- c --><div class=\"b\">yz</div>"), "text edits apply in any order against the original text");
    free(edited);

    unlink(path);
    rmdir(folder);
    rmdir(root);
}
