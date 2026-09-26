#ifndef CHIBI_UI_SOURCE_INTERNAL_H
#define CHIBI_UI_SOURCE_INTERNAL_H

/* What ui_source.c, ui_source_edit.c and ui_source_complete.c share: each
 * block's value as YAML means it, where its bytes are in the file, and its
 * tree-sitter parse. Outlines are kept in the value's offsets too, which
 * edits and completion work in. */

#include "assets/ui_source.h"

#include <tree_sitter/api.h>

/* How a block's value is written in the file. */
typedef enum
{
    SCALAR_LITERAL, /* a | block, whose lines map in place */
    SCALAR_INLINE,  /* one line, plain or quoted */
    SCALAR_OTHER,   /* anything else: positions fall on the scalar's start */
} scalar_style;

typedef struct block_map
{
    ui_source_language language;
    scalar_style style;
    char *value;
    int length;
    int *offsets; /* the file offset of each value byte, and of its end */
    int indent;   /* a literal block's indentation */
    /* The scalar in the file, indicator included; an inline one's end is its
     * line's, past any comment. */
    int scalar_start, scalar_end;
    TSTree *tree;
    ui_source_element *elements; /* in the value's offsets */
} block_map;

struct ui_source_tree
{
    block_map *maps; /* parallel to ui_source.blocks */
    int *lines;      /* each line's first byte */
    bool stylesheet; /* the text is a whole .rcss file */
};

const block_map *ui_source_map(const ui_source *source, ui_source_language language);
/* A value offset's file offset. */
int ui_source_file_offset(const block_map *map, int at);

#endif
