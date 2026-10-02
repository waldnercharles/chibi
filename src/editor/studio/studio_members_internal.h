#ifndef CHIBI_EDITOR_STUDIO_MEMBERS_INTERNAL_H
#define CHIBI_EDITOR_STUDIO_MEMBERS_INTERNAL_H

/* Shared by the Members editor and its Properties tab. */
#include "editor/studio/studio_graph_internal.h"
#include "graph/document/graph_signature.h"
#include "graph/model/local.h"

/* The selected member of each kind, or NULL. */
behavior_variable *sm_variable(const studio *s);
graph *sm_graph(const studio *s);
const graph_local *sm_local(const studio *s);
/* A variable as a pin, for its type label, swatch and value editor. */
pin_spec sm_variable_pin(const behavior_variable *v);
/* A type as the type picker names it: "Float", "Vector Array". */
const char *sm_type_label(const pin_spec *pin, char *out, size_t size);
/* A type swatch colored as the canvas colors its sockets. */
void sm_swatch(studio *s, const char *key, const pin_spec *pin);
/* Records `before`, a snapshot taken before an edit, as one undo step when
   the edit succeeded; frees it otherwise. Returns `ok`. */
bool sm_record(editor *e, behavior_asset *before, bool ok);
/* Moves the input or output whose pin id is `dragged` to index `to`, as one
   undo step; a pin dropped on itself changes nothing. Returns true when the
   signature changed. */
bool sm_move_signature_pin(editor *e, graph *g, bool result, uint64_t dragged, int to);
void sm_refused(studio *s, const char *title);

#endif
