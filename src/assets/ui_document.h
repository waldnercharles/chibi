#ifndef CHIBI_UI_DOCUMENT_H
#define CHIBI_UI_DOCUMENT_H

#include "support/rcss.h"

#include <stdbool.h>
#include <stddef.h>

/* A ui asset: RML body markup and the RCSS that styles it. A scene's root
 * document may wrap its markup in <body ...>, as a web page's layout does,
 * to set the page body's attributes, such as a data-class-dark binding. */
typedef struct ui_document
{
    char *rml;
    char *rcss;   /* empty when the asset has none */
    char *body;   /* the <body> attributes as written, or NULL */
    int rml_line; /* the line of the rml block `rml` starts on */
    rcss_map map; /* where each rcss line came from; the block's own are -1 */
} ui_document;

/* Parses a ui asset into `out`, which is empty or holds an earlier read.
 * RCSS @imports resolve in `directory`, appending each file read to
 * `imports` (support/array.h) when it is not NULL; a NULL directory leaves
 * them as written. `map` traces each line of the expanded RCSS to an import
 * or to its line in the `rcss` block. Returns false with a message in
 * `error` and leaves `out` unchanged when the asset is invalid, so a caller
 * keeps the last valid document. */
bool ui_document_read(
    const char *text,
    const char *directory,
    char ***imports,
    ui_document *out,
    char *error,
    size_t error_size
);
/* Replaces each <use ui="Name" .../> in the markup with the widget asset
 * `ui_root`/Name.yaml: a div with the use element's attributes, `ui` renamed
 * data-ui, around the widget's markup. A widget element with an attrs
 * attribute takes the use element's other attributes in its place instead,
 * as Vue's v-bind="$attrs" does, so a binding reaches the widget's input.
 * The use element's children fill the widget's <slot/>. The RCSS becomes
 * each widget's, then the document's, each scoped to its data-ui name
 * (ui_document_scope), the document's to `name`; the map names each asset's
 * own lines `ui_root`/Name.yaml. With `sources`, each element is marked with
 * where it was written (ui_document_sources): a widget's elements with the
 * widget's asset, and the div in place of a <use> with the use element's.
 * Appends every widget name it looks up to `uses` (support/array.h), even on
 * failure. Returns false with a message in `error`, leaving `document`
 * unchanged, when a widget is missing, invalid, or uses itself. */
bool ui_document_expand(
    ui_document *document,
    const char *name,
    const char *ui_root,
    bool sources,
    char ***uses,
    char *error,
    size_t error_size
);
/* Owned markup with data-src="<asset>:<line>" first in each element's start
 * tag but <slot>'s, where `line` is the 1-based line in the asset's rml block
 * the tag starts on, and `rml` starts on line `first`. Studio's Inspector
 * reads it to show where an element was written; data-src is reserved. */
char *ui_document_sources(const char *rml, const char *asset, int first);
/* Owned RCSS whose rules only match inside elements with data-ui="name":
 * `body` in a selector names that element, and other selectors match within
 * it. @media blocks are scoped too; other at-rules are kept as written. Each
 * line of expanded RCSS (rcss_expand) keeps its line, so its map holds. */
char *ui_document_scope(const char *rcss, const char *name);
/* Appends the name of each widget the markup uses directly to `names`. */
void ui_document_use_names(const char *rml, char ***names);

/* The ui asset the player shell shows while paused. The shell takes its
 * events, so they never reach behaviors. */
#define UI_DOCUMENT_PAUSE_MENU "PauseMenu"

/* An event markup sends the entity showing it: an emit('Name') in a
 * data-event-* handler when that event happens (data-event-change when the
 * player changes an input), and a data-keys binding's when the player
 * presses its key with focus inside. */
typedef struct ui_document_event
{
    char *name;
    char *element; /* "#id", "tag.class", "tag", or a <use>'s widget name */
    char *key;     /* the data-keys key; NULL for a handler's emit */
    bool input;    /* emitted by data-event-change, when an input changes */
} ui_document_event;

/* Appends each event the markup names, in document order, to `events`. */
void ui_document_events(const char *rml, ui_document_event **events);
void ui_document_events_free(ui_document_event *events);

/* Owned markup with each name in it equal to `from` renamed `to`: widget
 * names (<use ui>), or with `event`, the events emit('Name') and data-keys
 * send. Sets *count to the names renamed. */
char *ui_document_rename(const char *rml, bool event, const char *from, const char *to, int *count);

/* Owned RML document text: `base` then the asset's RCSS in the head, and its
 * markup in a body with its body attributes. */
char *ui_document_compose(const ui_document *document, const char *base);
void ui_document_free(ui_document *document);

#endif
