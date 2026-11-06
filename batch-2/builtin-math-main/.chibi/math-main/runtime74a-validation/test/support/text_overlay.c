#include "support/text_overlay.h"

#include "support/file_watch.h"
#include "support/text_edit.h"
#include "support/text_file.h"
#include "third_party/pico_headers/pico_unit.h"

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

TEST_CASE(text_overlay_reads_in_place_of_files)
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
    /* A file reads from disk without an overlay. */
    REQUIRE(file_watch_changed(&w, path, 0) && reads(path, "<p>saved</p>\n"));
    text_overlay_set(spelled, "<p>unsaved</p>\n");
    /* An overlay reads in place of its file, however the path is spelled. */
    REQUIRE(reads(path, "<p>unsaved</p>\n"));
    /* A file watch sees an overlay at once. */
    REQUIRE(file_watch_changed(&w, path, 0.1));
    /* An unchanged overlay is not a change. */
    REQUIRE(!file_watch_changed(&w, path, 0.2));
    text_overlay_set(path, "<p>again</p>\n");
    /* A file watch sees an overlay edit at once. */
    REQUIRE(file_watch_changed(&w, path, 0.3) && reads(path, "<p>again</p>\n"));
    text_overlay_set(path, NULL);
    /* Removing an overlay reads the file again. */
    REQUIRE(file_watch_changed(&w, path, 0.4) && reads(path, "<p>saved</p>\n"));
    text_overlay_set(path, "x");
    text_overlay_clear();
    /* Clearing removes every overlay. */
    REQUIRE(!text_overlay_text(path) && text_overlay_revision(path) == 0);

    char cwd[1024];
    assert(getcwd(cwd, sizeof cwd));
    assert(!chdir(root));
    text_overlay_set("ui/Panel.rml", "relative");
    bool relative = reads(path, "relative");
    text_overlay_clear();
    assert(!chdir(cwd));
    /* A relative path matches its absolute spelling. */
    REQUIRE(relative);
    unlink(path);
    rmdir(folder);
    rmdir(root);
    return true;
}

TEST_CASE(text_edits_apply_against_the_original_text)
{
    const char *source = "<div class=\"a\">x</div>";
    text_edit edits[] = {
        { 15, 16, "yz" },
        { 12, 13, "b" },
        { 0, 0, "<!-- c -->" },
    };
    char *edited = text_edits_apply(source, edits, 3);
    /* Text edits apply in any order against the original text. */
    bool applied = !strcmp(edited, "<!-- c --><div class=\"b\">yz</div>");
    free(edited);
    REQUIRE(applied);
    return true;
}

void test_text_overlay()
{
    RUN_TEST_CASE(text_overlay_reads_in_place_of_files);
    RUN_TEST_CASE(text_edits_apply_against_the_original_text);
}
