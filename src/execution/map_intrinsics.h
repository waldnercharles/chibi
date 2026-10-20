#ifndef CHIBI_EXECUTION_MAP_INTRINSICS_H
#define CHIBI_EXECUTION_MAP_INTRINSICS_H
#include "execution/host.h"

extern const node_host_call EXECUTION_map_make_call, EXECUTION_map_get_call,
    EXECUTION_map_has_call, EXECUTION_map_remove_call, EXECUTION_map_item_call;
/* In-place store and removal of a local's own map entry: (header address,
 * key, value) and (header address, key). Every store into a local snapshots,
 * so no other value shares the map these update. */
extern const node_host_call EXECUTION_map_store_call, EXECUTION_map_delete_call;
#endif
