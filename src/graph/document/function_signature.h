#ifndef CHIBI_FUNCTION_SIGNATURE_H
#define CHIBI_FUNCTION_SIGNATURE_H

#include "graph/model/behavior_asset.h"

/* A function's signature: its parameters are the data outputs of Function
 * Entry and its results the data inputs of Return, addressed by pin index
 * (`result` picks the side). Every edit updates the function's calls in its
 * asset with graph_library_refresh_call, so call pins follow their
 * declarations. A call wire that a removed or retyped pin can no longer carry
 * is removed, as Unreal's node refresh does. Edits return 0, or -1 when
 * refused; a refused edit changes nothing. */
bool function_signature_pure(const graph *function);
/* A pin that declares a parameter or result: data, not a field or split part. */
bool function_signature_declared(const pin_spec *pin);
/* A pure function has no execution pins. Making one pure removes them from
 * its calls and joins each call's incoming execution to its outgoing wire; it
 * is refused while the body has nodes with execution pins. */
int function_signature_set_pure(behavior_asset *asset, graph *function, bool pure);
/* `pin` gives the name, type, type_name and container. Names are unique
 * among the pins of a side. */
int function_signature_add(behavior_asset *asset, graph *function, bool result, const pin_spec *pin);
int function_signature_remove(behavior_asset *asset, graph *function, bool result, int index);
int function_signature_rename(
    behavior_asset *asset,
    graph *function,
    bool result,
    int index,
    const char *name
);
/* Takes the type, type_name and container of `type`. */
int function_signature_retype(
    behavior_asset *asset,
    graph *function,
    bool result,
    int index,
    const pin_spec *type
);

#endif
