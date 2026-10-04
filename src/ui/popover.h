#ifndef CHIBI_UI_POPOVER_H
#define CHIBI_UI_POPOVER_H

/* Popovers for every RmlUi document, the editor's and games', after the HTML
 * popover attribute and Base UI's Popover, Menu and Menubar. One stack per
 * context holds what is open, authored popovers and the kit's popups alike,
 * so Escape and an outside press close the top one first.
 *
 * - <div popover id="p">: hidden until shown. <button popovertarget="p">
 *   toggles it, hanging below the button; the id is looked up from the
 *   button outward, so a widget repeated by data-for finds its own. A
 *   popovertarget on a menu item opens its submenu beside the row.
 * - Shown, it is fixed and unclipped, below its anchor or flipped above it
 *   (beside a submenu's row, or at a point), 4 dp inside the window, with
 *   --available-height the window's height less those margins; its
 *   document stacks above the other areas, below the overlay layer, whichever
 *   has focus. It closes on a press outside it and its trigger, on Escape
 *   (focus inside returns to its trigger) and when one of its items
 *   activates. Showing one closes the others but those it opened from.
 * - role="menu": items ([role=menuitem], menuitemcheckbox, menuitemradio)
 *   highlight under the pointer, with Up, Down, Home and End, and by
 *   typeahead; Enter or Space clicks the highlighted one. An item with
 *   aria-haspopup opens its submenu after a 100 ms hover or with Right, and
 *   Left closes it. A menuitem's or an option's click closes its popover and
 *   the menus it opened from; checkbox and radio items stay open. A disabled
 *   item (data-disabled) never highlights and its click reaches nothing.
 *   Keys stay with a focused text field.
 * - Other popovers are Base UI's nonmodal Popover: opening focuses the first
 *   tabbable control, Tab moves within and closes past either end, and focus
 *   moving to an outside control closes it.
 * - <menubar>: while a menu one of its triggers opened is open, hovering
 *   another trigger opens that one's, and Left and Right step between them.
 *   A trigger in a menubar opens on press.
 * - States: data-open while shown, data-starting-style for its first frame,
 *   data-ending-style while it fades out, data-unplaced until placed,
 *   data-highlighted on a menu's highlighted item, and data-popup-open on
 *   the trigger of an open popover (Base UI's). Closed, it lays out and
 *   draws nothing ([popover]:not([data-open]) is display: none).
 * - data-template (Base UI's Popup, mounted only while open): a popover that
 *   carries it holds no elements and no data views while closed. Its content
 *   is built when it is shown, bound to the data of that moment, and removed
 *   with its views once it has faded out. Like data-for it cancels the
 *   popover's own other data views, and opening it again during its fade
 *   keeps the content. Menu and Submenu have it; a popover without it stays
 *   built.
 *
 * C owners, such as the kit, stack their own elements with popover_show and
 * see them close through popover_open; those are placed and dismissed here
 * but never shown or hidden. A key a role="combobox" field takes stays the
 * field's. */

#include "ui/rml.h"

#include <stdbool.h>

typedef enum popover_side
{
    POPOVER_BELOW,  /* below its anchor, or above when the window ends */
    POPOVER_BESIDE, /* beside a submenu's row, right or else left */
    POPOVER_POINT,  /* its top left at a point */
} popover_side;

typedef struct popover_anchor
{
    popover_side side;
    rml_element *element; /* the box it hangs from; NULL for `box` */
    float box[4];         /* x, y, width, height in context pixels */
    rml_element *trigger; /* gets focus back; a press on it is its own */
    bool keyboard; /* a key opened it: a menu highlights its first item */
} popover_anchor;

/* Shows `popover` above the stack, closing what it did not open from. */
void popover_show(rml_element *popover, const popover_anchor *at);
/* Closes `popover` and what opened from it; nothing when it is closed. */
void popover_hide(rml_element *popover);
bool popover_open(const rml_element *popover);
/* The context's topmost open popover, or NULL. */
rml_element *popover_top(rml_context *context);
void popover_hide_all(rml_context *context);
/* Listens in `context`, places what is open, opens hovered submenus and
 * ends fades; the backend runs it after each update. */
void popovers_settle(rml_context *context);

#endif
