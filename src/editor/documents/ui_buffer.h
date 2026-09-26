#ifndef CHIBI_UI_BUFFER_H
#define CHIBI_UI_BUFFER_H

#include "support/text_edit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <yyjson.h>

/* The text of a UI document open for editing: a ui asset's RML and RCSS, or
 * a stylesheet's RCSS. Studio's Text Editor, the designer and the MCP change
 * it through text_edits; the buffer keeps the undo history and whether the
 * text reads as its file would. Published, the text stands in for the file
 * (support/text_overlay.h), so everything that reads the file sees it. */
typedef enum ui_part
{
    UI_PART_RML,
    UI_PART_RCSS,
    UI_PART_COUNT,
} ui_part;

/* The parts before an undo step, and the part it changed. */
typedef struct ui_buffer_step
{
    char *parts[UI_PART_COUNT];
    ui_part part;
} ui_buffer_step;

typedef struct ui_buffer
{
    char *path;                 /* absolute */
    bool stylesheet;            /* an .rcss file, whose text is its RCSS part */
    char *parts[UI_PART_COUNT]; /* a stylesheet's RML part is NULL */
    char *saved[UI_PART_COUNT]; /* as last loaded or saved */
    uint64_t disk;              /* the file's hash as last loaded or saved */
    uint64_t disk_seen;         /* its hash when last polled */
    double disk_due;            /* the next poll, in seconds */
    ui_part part;               /* the part the Text Editor shows */
    ui_buffer_step *undo, *redo;
    double typed; /* when the last typed change was made; -1 after other changes */
    unsigned revision; /* changes with each change to the parts */
    unsigned seen;     /* the revision ui_buffer_update last saw */
    double seen_time;  /* when it saw it */
    unsigned checked;  /* the revision `error` describes */
    bool published;    /* the text stands in for the file */
    char error[256];   /* why the text does not read; empty when it does */
} ui_buffer;

/* Reads the file at `path`, a ui asset (.yaml) or a stylesheet (.rcss);
 * NULL with why in `error`. */
ui_buffer *ui_buffer_open(const char *path, char *error, size_t size);
/* Frees the buffer; the file reads from disk again. */
void ui_buffer_free(ui_buffer *b);
/* A part's text; NULL for a stylesheet's RML. */
const char *ui_buffer_text(const ui_buffer *b, ui_part part);

/* Applies `edits` to a part as one undo step. */
void ui_buffer_edit(ui_buffer *b, ui_part part, const text_edit *edits, int count);
/* Replaces a part's text as typed `now`, in seconds: a change within a second
 * of the last one typed into the same part joins its undo step. */
void ui_buffer_type(ui_buffer *b, ui_part part, const char *text, double now);
bool ui_buffer_can_undo(const ui_buffer *b);
bool ui_buffer_can_redo(const ui_buffer *b);
/* Steps through the history; the Text Editor turns to the part changed. */
void ui_buffer_undo(ui_buffer *b);
void ui_buffer_redo(ui_buffer *b);
/* The text differs from the file as last loaded or saved. */
bool ui_buffer_dirty(const ui_buffer *b);

/* Owned file text: canonical YAML for a ui asset (as `chibi_ctl --format`
 * writes it), the RCSS of a stylesheet. */
char *ui_buffer_compose(const ui_buffer *b);
/* Whether the file text reads as a ui asset (ui_document_read) or a
 * stylesheet (rcss_expand), with why not in `error`. */
bool ui_buffer_check(ui_buffer *b);
/* Once a change has rested for UI_BUFFER_SETTLE seconds, checks the text and,
 * with `live`, publishes it when it reads, so an invalid edit keeps the last
 * valid text everywhere. Without `live` the file stands. True when what
 * stands in for the file changed. */
#define UI_BUFFER_SETTLE 0.15
bool ui_buffer_update(ui_buffer *b, double now, bool live);

/* The file on disk is the one loaded or last saved. */
bool ui_buffer_on_disk(const ui_buffer *b);
/* Another program changed the file since the last poll; polls at most once a
 * second. */
bool ui_buffer_poll_disk(ui_buffer *b, double now);
/* Records `text`, from ui_buffer_compose, as written to the file. */
void ui_buffer_saved(ui_buffer *b, const char *text);
/* Replaces the text with the file's, discarding edits and history; false
 * with why in `error`, keeping the text. */
bool ui_buffer_reload(ui_buffer *b, char *error, size_t size);

/* The buffer in workspace checkpoints (editor/workspace/checkpoint.h). */
yyjson_mut_val *ui_buffer_checkpoint(const ui_buffer *b, yyjson_mut_doc *doc);
ui_buffer *ui_buffer_restore(yyjson_val *value);

/* A new UI asset: a widget has a <slot/> for its use's content and RCSS
 * scoped to it; a document is a panel; a stylesheet is RCSS for others to
 * @import. */
typedef enum ui_new_kind
{
    UI_NEW_WIDGET,
    UI_NEW_DOCUMENT,
    UI_NEW_STYLESHEET,
    UI_NEW_KIND_COUNT,
} ui_new_kind;

/* The owned path of a new `kind` named `name` in `folder`, the project's ui
 * assets; NULL with why in `error` when the name is invalid or taken. */
char *ui_buffer_new_path(const char *folder, ui_new_kind kind, const char *name, char *error, size_t size);
/* The owned file text a new `kind` starts with. */
char *ui_buffer_template(ui_new_kind kind);

#endif
