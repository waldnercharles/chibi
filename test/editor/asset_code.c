#include "editor/documents/asset_code.h"

#include "support/asset_text.h"
#include "support/text_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_asset_code_checkpoint()
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    const char *path = "/tmp/chibi-source-checkpoint.kdl";
    const char *valid = "name \"Last good\"\n";
    av_obj_add_str(doc, root, "path", path);
    av_obj_add_str(doc, root, "disk", "name \"Saved\"\n");
    av_obj_add_str(doc, root, "text", "name \"unfinished");
    av_obj_add_str(doc, root, "valid", valid);
    av_obj_add_bool(doc, root, "overlay", true);
    av_obj_add_int(doc, root, "step", 1);
    av_obj_add_int(doc, root, "cursor", 3);
    av_obj_add_int(doc, root, "anchor", 12);
    av_value *history = av_obj_add_arr(doc, root, "history");
    av_arr_add_str(doc, history, "name \"Saved\"\n");
    av_arr_add_str(doc, history, "name \"unfinished");
    av_arr_add_str(doc, history, "name \"Finished\"\n");

    /* An invalid draft and the redo branch survive replacement independently
     * of the last valid preview and saved disk baseline. */
    asset_code *code = asset_code_restore(root, NULL);
    assert(code && asset_code_dirty(code) && asset_code_error(code)[0]);
    assert(!strcmp(text_overlay_text(path), valid));
    assert(kit_code_cursor(asset_code_editor(code)) == 3);
    int start, end;
    assert(kit_code_selection(asset_code_editor(code), &start, &end));
    assert(start == 3 && end == 12);
    av_value *snapshot = asset_code_capture(code);
    assert(av_equals(snapshot, root));
    assert(asset_code_has_state(code, snapshot));
    char error[256];
    assert(!asset_code_save(code, error, sizeof error));
    assert(error[0]);
    asset_code *replacement = asset_code_restore(snapshot, NULL);
    assert(replacement);
    asset_code_free(code);
    assert(!strcmp(text_overlay_text(path), valid));
    asset_code_free(replacement);
    assert(!text_overlay_text(path));

    code = asset_code_restore(snapshot, NULL);
    assert(code && !strcmp(kit_code_text(asset_code_editor(code), NULL), "name \"unfinished"));
    av_value *again = asset_code_capture(code);
    assert(av_equals(snapshot, again));
    asset_code_free(code);
    av_value_free(again);
    av_value_free(snapshot);
    av_doc_free(doc);

    /* Independent visual changes merge into an unsaved source draft without
     * canonicalizing its comments or overwriting the typed name. */
    char file_path[] = "/tmp/chibi-kdl-refresh-XXXXXX";
    int fd = mkstemp(file_path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs("name \"Saved\"\nvalue 2\n", file);
    assert(!fclose(file));
    doc = av_doc_new();
    root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_str(doc, root, "path", file_path);
    av_obj_add_str(doc, root, "disk", "name \"Saved\"\nvalue 1\n");
    av_obj_add_str(doc, root, "text", "// draft\nname  \"Draft\"\nvalue 1\n");
    av_obj_add_str(doc, root, "valid", "// draft\nname  \"Draft\"\nvalue 1\n");
    av_obj_add_bool(doc, root, "overlay", true);
    av_obj_add_int(doc, root, "step", 0);
    history = av_obj_add_arr(doc, root, "history");
    av_arr_add_str(doc, history, "// draft\nname  \"Draft\"\nvalue 1\n");
    code = asset_code_restore(root, NULL);
    assert(code);
    asset_code_refresh(code);
    assert(!strcmp(kit_code_text(asset_code_editor(code), NULL), "// draft\nname  \"Draft\"\nvalue 2\n"));
    assert(asset_code_dirty(code));
    assert(asset_code_save(code, error, sizeof error));
    assert(!asset_code_dirty(code));
    asset_code_free(code);
    av_doc_free(doc);
    assert(!unlink(file_path));
}
