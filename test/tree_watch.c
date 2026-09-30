#include "support/tree_watch.h"

#include "support/array.h"
#include "test/support/report.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static bool changed_only(char **changes, const char *path)
{
    bool only = acount(changes) == 1 && !strcmp(changes[0], path);
    tree_watch_changes_free(changes);
    return only;
}

void test_tree_watch()
{
    char root[] = "/tmp/chibi-tree-watch-XXXXXX";
    assert(mkdtemp(root));
    char nested[64], file[96], other[96];
    snprintf(nested, sizeof nested, "%s/nested", root);
    snprintf(file, sizeof file, "%s/A.yaml", nested);
    snprintf(other, sizeof other, "%s/notes.txt", root);
    assert(mkdir(nested, 0755) == 0);
    tree_watch w = { 0 };
    assert(!tree_watch_poll(&w, root, ".yaml", 0));
    write_text(file, "a: 1\n");
    write_text(other, "ignored");
    assert(!tree_watch_poll(&w, root, ".yaml", 0.5));
    assert(changed_only(tree_watch_poll(&w, root, ".yaml", 1), file));
    write_text(file, "a: 22\n");
    assert(changed_only(tree_watch_poll(&w, root, ".yaml", 2), file));
    write_text(file, "a: 333\n");
    tree_watch_note(&w, file);
    assert(!tree_watch_poll(&w, root, ".yaml", 3));
    unlink(file);
    assert(changed_only(tree_watch_poll(&w, root, ".yaml", 4), file));
    tree_watch_free(&w);
    unlink(other);
    rmdir(nested);
    rmdir(root);
    test_note("tree watch PASS: additions, edits, removals and recorded writes by extension, recursively and once a second\n");
}
