#ifndef CHIBI_FIELD_NATIVE_H
#define CHIBI_FIELD_NATIVE_H

#include "execution/host.h"

/* Static scalar F32 capabilities. Callers supply typed result headers; these
 * kernels write payloads only and retain no arguments. */
extern const node_access FIELD_NATIVE_ACCESS;
extern const node_host_call FIELD_ADD_CALL;
extern const node_host_call FIELD_SUBTRACT_CALL;
extern const node_host_call FIELD_MIN_CALL;
extern const node_host_call FIELD_CIRCLE_CALL;
extern const node_host_call FIELD_BOX_CALL;
extern const node_host_call FIELD_CAPSULE_CALL;
extern const node_host_call FIELD_SMOOTH_UNION_CALL;

#endif
