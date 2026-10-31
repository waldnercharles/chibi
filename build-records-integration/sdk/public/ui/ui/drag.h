#ifndef CHIBI_UI_DRAG_H
#define CHIBI_UI_DRAG_H

/* Drag and drop for every RmlUi context, the editor's and games', after
 * dnd-kit (docs/editor-ui-references.md) and HTML's drag and drop. One drag
 * runs at a time, in the context that took its press, across all of that
 * context's documents.
 *
 * - A source is an element with data-drag="type" and data-drag-payload; the
 *   overlay shows data-drag-label (the payload when absent) after the glyph
 *   in data-drag-icon. A press on it picks it up once the pointer moves
 *   FEEL_DRAG_DP with the button held, dnd-kit's distance constraint;
 *   holding still never does, so a slow click still clicks.
 * - A target is an element with data-drop="type ..." naming the types it
 *   takes; data-drop-hint says what a drop does there, under the overlay's
 *   label. data-drop-zones="tree" splits it as a sortable tree's row: its
 *   top and bottom quarters drop before and after it and its middle inside;
 *   "list" splits it in halves, before and after. Optional
 *   data-drop-zones-type="type" limits those zones to one drag type; other
 *   accepted types drop on the whole target with an empty zone.
 * - While a drag runs the source has data-dragging, each target that takes
 *   its type data-drop-accept, and the nearest one under the pointer
 *   data-drop-over and, with zones, data-drop-zone="before|inside|after".
 *   An overlay follows the pointer in the topmost document: a <drag-overlay
 *   class="drag-overlay"> with .icon, .text, .label and .hint parts and
 *   data-over and data-hint, which enters with data-starting-style and
 *   leaves with data-ending-style, as presence does.
 * - Releasing over a target dispatches `drop` from it, bubbling, with
 *   ev.type, ev.payload and ev.zone, and keeps the drop for
 *   ui_drag_dropped until the next press. Releasing elsewhere or Escape
 *   cancels.
 * - The release's click is swallowed, so a drop never also activates what
 *   it lands on, and a press after a drag is a click, not a double click:
 *   RmlUi counts any second press on an element, browsers only one after a
 *   click. */

#include "ui/rml.h"

#include <stdbool.h>

/* The drag, as dnd-kit's `active` and `over` name it. The strings are the
 * drag's and live until the next input event or call here; "" for none. */
typedef struct ui_drag_item
{
    const char *type;
    const char *payload;
    const char *zone; /* "before", "inside" or "after", with zones */
} ui_drag_item;

/* Listens in every context RmlUi creates from now on; the backend calls it
 * once RmlUi starts. */
void ui_drag_register();

/* Whether a drag runs in `context`; its item when it does. `item` may be
 * NULL. */
bool ui_drag_active(rml_context *context, ui_drag_item *item);
/* Whether the running drag is over `target`, an element with data-drop
 * that takes its type, with the zone the pointer is in. */
bool ui_drag_over(const rml_element *target, ui_drag_item *item);
/* Takes the drop that landed on `target` since the last press, once, when
 * its type is `type`: how C surfaces such as a viewport take drops. */
bool ui_drag_dropped(const rml_element *target, const char *type, ui_drag_item *item);
/* What a drop does where the pointer is, shown under the overlay's label;
 * the target's data-drop-hint replaces it when the pointer reaches another
 * target. */
void ui_drag_hint(rml_context *context, const char *text);
/* Ends the drag in `context`, or a press waiting to become one, without a
 * drop, as Escape does; for a host that saw the button released where the
 * context did not. */
void ui_drag_cancel(rml_context *context);

#endif
