#ifndef CBLUEPRINT_EDITOR_GRAPH_SEARCH_H
#define CBLUEPRINT_EDITOR_GRAPH_SEARCH_H

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
    const char *category;
    char name[384];
    int score;
} ed_search_match;

/* Caller owns the returned core/array. Entries borrow the current catalogs. */
ed_search_match *ed_search_matches(const editor *e, const char *filter);
node *ed_spawn_match(editor *e, const ed_search_match *match);

#endif
