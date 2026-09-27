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
        bool compared;
        unsigned revision;          /* the buffer's when last compared */
        unsigned long long overlay; /* its file's overlay's then */
        bool current;               /* the Canvas shows the text as it is */
    } canvas;
    struct
    {
        int toast;         /* offers Undo while the delete is the latest edit */
        unsigned revision; /* the buffer's right after it */
    } undo;
    struct
    {
        bool focus; /* focus the field on its first frame */
        char text[192];
    } add; /* the field of an Add popover (class, attribute) */
};

/* The active UI document with markup and its designer state, parsed as its
 * text is now; NULL while no such document is active. */
studio_designer *designer_state(studio *s, ui_buffer **b);
/* Selects element `element` of the current parse (-1 for the document) and
 * reveals it in the Hierarchy and the Text Editor. */
void designer_select(studio *s, int element);
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
