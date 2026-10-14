#ifndef CHIBI_UI_AUTHORING_H
#define CHIBI_UI_AUTHORING_H
#include "authoring/workspace_documents.h"
#include "support/arena.h"
#include "types/array.h"

typedef struct ui_authoring ui_authoring;
typedef struct
{
    int index, parent, depth, start, end, line;
    const char *name, *value, *extra;
    bool binding;
} ui_authoring_row;
typedef struct
{
    bool ok, valid;
    int code, document, revision, total;
    const char *error;
} ui_authoring_status;
/* Borrowed documents; cache holds immutable parsed snapshots only. Every query
 * and edit checks the current document revision. UI selection/filter/history
 * policy belongs to the caller. Pages <=256 rows, source <=1MiB, <=4096 elements
 * or rules and <=16384 total attributes/declarations, depth<=64. */
ui_authoring *ui_authoring_new(workspace_documents *);
void ui_authoring_free(ui_authoring *);
ui_authoring_status ui_authoring_query(
    ui_authoring *,
    int document,
    int revision,
    const char *language,
    const char *kind,
    int parent,
    int first,
    int count,
    arena *,
    pin_array **rows
);
/* One atomic document edit, joining an existing caller-owned transaction.
 * Operations attribute/class/style/insert/move/delete/text/duplicate; Remove
 * removes an attribute/class/style. RCSS operations declaration/selector/remove-rule
 * use Element=rule and Target=declaration (-1 appends). Place is inside/before/after. Each operation
 * names indexes in this exact revision. Invalid syntax/source leaves it intact. */
ui_authoring_status ui_authoring_edit(
    ui_authoring *,
    int document,
    int revision,
    const char *operation,
    int element,
    int target,
    const char *place,
    const char *name,
    const char *value,
    bool remove
);
#endif
