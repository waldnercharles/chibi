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
    const char *category;
    char name[384];
    int score;
} ed_search_match;

typedef struct ed_search_output
{
    pin_spec pin;
    int node_pin; /* Index after reflected fields have been exposed. */
} ed_search_output;

/* Caller owns the returned core/array. Entries borrow the current catalogs. */
ed_search_match *ed_search_matches(const editor *e, const char *filter);
node *ed_spawn_match(editor *e, const ed_search_match *match);
/* Spawns at world (x, y) as one undo step: shader parameters, System compaction
 * and event naming apply, and a pending link-drag wires the new node. Comment
 * boxes are created and return NULL. */
node *ed_spawn_match_at(editor *e, const ed_search_match *match, float x, float y);
/* Includes one level of reflected fields for each struct output. */
int ed_search_match_output_count(const editor *e, const ed_search_match *match);
bool ed_search_match_output(const editor *e, const ed_search_match *match, int index, ed_search_output *output);
/* Adds the reflected field projections used by an autowired source node. */
int ed_expose_spawned_output_fields(editor *e, node *n);

#endif
