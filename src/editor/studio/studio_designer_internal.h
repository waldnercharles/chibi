#ifndef CHIBI_EDITOR_STUDIO_DESIGNER_INTERNAL_H
#define CHIBI_EDITOR_STUDIO_DESIGNER_INTERNAL_H

/* What the visual designer's files share: studio_designer.c keeps the
 * document, the selection, the Hierarchy and the Canvas;
 * studio_designer_details.c shows Details; studio_designer_library.c the
 * Library and the Add menus. */
#include "editor/documents/ui_design.h"
#include "editor/studio/studio_internal.h"

/* The drag type of Hierarchy rows ("#<element>") and Library items (their
 * markup). */
#define DESIGNER_DRAG "ui"

/* The edges a Canvas resize handle moves. */
enum
{
    DESIGNER_RESIZE_WIDTH = 1u << 0,
    DESIGNER_RESIZE_HEIGHT = 1u << 1,
};

/* An element's border box, width then height, and the padding and border
 * inside it, in document dp; and whether its width and height size that
 * box (box-sizing: border-box) rather than its content. */
typedef struct designer_box
{
    float size[2], extra[2];
    bool border_box;
} designer_box;

/* The width and height, in whole dp, a resize sets after the pointer moved
 * (dx, dy) document dp: the moved axes grow by it, never under the padding
 * and border; with `aspect` and both axes, the one moved further leads and
 * the other keeps the ratio. */
void designer_resize_css(
    const designer_box *box,
    unsigned axes,
    float dx,
    float dy,
    bool aspect,
    float css[2]
);

struct studio_designer
{
    char path[1024];       /* the UI document the rest describes */
    char asset[128];       /* its ui asset's name, as data-src names it */
    unsigned parsed;       /* its buffer's revision when parsed */
    ui_source *rml, *rcss; /* its parts */
    int selected;          /* an element of rml, or -1 for the document */
    int hovered; /* the element whose Hierarchy row the pointer is over, or -1 */
    int menu;    /* the element the Hierarchy's menu is for */
    bool reveal_row;      /* scroll the Hierarchy to the selection */
    int reveal_cursor;    /* where to put the Text Editor's cursor, or -1 */
    int cursor;           /* the Text Editor's cursor as last seen */
    unsigned cursor_seen; /* the buffer's revision then */
    bool closed;          /* the Hierarchy folds the document */
    char **folded;        /* places ("0.2.1") of the elements it folds */
    char filter[128];     /* the Hierarchy's */
    char library_filter[128];
    struct
    {
        bool active;  /* a scrub or picker session made the last undo step */
        char key[96]; /* the value it changes */
        unsigned revision; /* the buffer's after its last change */
    } gesture;
    struct
    {
        int toast;         /* offers Undo while the delete is the latest edit */
        unsigned revision; /* the buffer's right after it */
    } undo;
    /* A Canvas resize (Unity UI Builder's handles): the element, the edges
       its handle moves, where the press was, its border box then and the
       padding and border inside it in document dp, the buffer's revision,
       and the inline width and height it had ("" for none), which Escape
       puts back. Only the Canvas shows the new size until release writes it. */
    struct
    {
        bool active;
        int element;
        unsigned axes; /* DESIGNER_RESIZE_WIDTH | DESIGNER_RESIZE_HEIGHT */
        float x0, y0;
        designer_box box;
        bool moved;
        unsigned revision;
        char before[2][64];
        float css[2]; /* the width and height a release writes */
    } resize;
    struct
    {
        bool focus; /* focus the field on its first frame */
        char text[192];
    } add; /* the field of an Add popover (class, attribute) */
    /* Workstream Q: a Hierarchy row carried with the keyboard, as dnd-kit's
       keyboard sensor carries one: the element, the slot it would drop in
       (an index of ui_design_slots', and that slot), and the buffer's
       revision when it was picked up, since a change to the text puts it
       down. */
    struct
    {
        bool active;
        int element;
        int slot;
        ui_design_slot at;
        unsigned revision;
    } carry;
};

/* The active UI document with markup and its designer state, parsed as its
 * text is now; NULL while no such document is active. */
studio_designer *designer_state(studio *s, ui_buffer **b);
/* Selects element `element` of the current parse (-1 for the document) and
 * reveals it in the Hierarchy and the Text Editor. */
void designer_select(studio *s, int element);
/* Visible bounds of a source element, including expanded widget roots. */
kit_rect studio_designer_element_box(rml_element *element);
/* A change's warning toast. */
void designer_warn(studio *s, const char *title, const char *error);
/* An element's name as the Hierarchy shows it: its tag, or a <use>'s widget. */
void designer_element_name(const ui_source *rml, int element, char *out, size_t size);
uint32_t designer_element_icon(const ui_source *rml, int element);
/* Adds markup for element `target` (-1 the document): inside a container,
 * else after it; selects what it added. */
void designer_add(studio *s, int target, const char *markup, const char *label);

/* Library (studio_designer_library.c): the Add menu's items, the elements
 * then the project's widgets; true when one was chosen, with its markup in
 * `markup` and its label in `label`. */
bool designer_add_items(studio *s, char *markup, size_t size, char *label, size_t label_size);

#endif
