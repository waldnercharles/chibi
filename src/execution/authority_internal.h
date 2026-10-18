#ifndef CHIBI_EXECUTION_AUTHORITY_INTERNAL_H
#define CHIBI_EXECUTION_AUTHORITY_INTERNAL_H

#include "execution/authority.h"
#include "execution/request.h"

/* Scheduler/source entry boundaries revoke active proofs even when no matching
 * handler exists. This is private runtime machinery, not an authored service. */
void program_authority_invalidate();
execution_dispatch *program_dispatch_retain(execution_dispatch *);
execution_work *program_dispatch_work(execution_dispatch *);
void program_dispatch_free(execution_dispatch *);
void execution_request_set_dispatch(flow_host_request *, execution_dispatch *);

#endif
