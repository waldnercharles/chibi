#ifndef CHIBI_UI_SOURCE_INTERNAL_H
#define CHIBI_UI_SOURCE_INTERNAL_H

/* What ui_source.c, ui_source_edit.c and ui_source_complete.c share: the
 * source's tree-sitter parse and where its lines start. */

#include "assets/ui_source.h"

#include <tree_sitter/api.h>

struct ui_source_tree
{
    TSTree *tree;
    int *lines; /* each line's first byte */
};

#endif
