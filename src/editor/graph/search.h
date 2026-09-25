#ifndef CHIBI_EDITOR_GRAPH_SEARCH_H
#define CHIBI_EDITOR_GRAPH_SEARCH_H

#include "graph/model/behavior_asset.h"

typedef struct editor editor;
typedef struct ed_search_match
{
    const node_desc *desc;
    graph *function;
    const behavior_variable *variable;
    const char *prototype;
    const behavior_asset *event;
    int host_event; /* One-based index in the editor host event catalog. */
    /* Component node presets: a reflected component key, and for Set the one
     * field it writes. */
    const char *component;
    const char *field;
    const char *category;
    char name[384];
    int score;
} ed_search_match;

/* Caller owns the returned core/array. Entries borrow the current catalogs. */
ed_search_match *ed_search_matches(const editor *e, const char *filter);
/* Spawns at world (x, y) as one undo step: shader parameters, System compaction
 * and event naming apply, and a pending link-drag wires the new node. Comment
 * boxes are created and return NULL. */
node *ed_spawn_match_at(editor *e, const ed_search_match *match, float x, float y);

#endif
