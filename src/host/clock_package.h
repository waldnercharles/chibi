#ifndef CHIBI_HOST_CLOCK_PACKAGE_H
#define CHIBI_HOST_CLOCK_PACKAGE_H

#include "host/clock.h"
#include "runtime/native_capabilities.h"

#define HOST_CLOCK_SERVICE "host.clock"
#define HOST_CLOCK_SAMPLE_ID UINT64_C(0xa591d29caadefff1)

enum
{
    HOST_CLOCK_SAMPLE_OK,
    HOST_CLOCK_SAMPLE_DENIED,
    HOST_CLOCK_SAMPLE_UNAVAILABLE,
    HOST_CLOCK_SAMPLE_FAILED,
};

/* A provider writes a nonnegative sample on success and a platform error on
 * failure. The state is borrowed only during synchronous calls. */
typedef host_clock_status (*host_clock_provider)(void *state, int64_t *, int *);
typedef struct
{
    uint64_t domain;
    host_clock_provider read;
    void *state;
} host_clock_grant;

/* Each grant has a distinct process-local clock domain. A NULL provider uses
 * the platform clock. Keep the grant and provider state alive until all calls
 * have returned. Neither owns an OS handle or retains authored arguments. */
void host_clock_grant_init(host_clock_grant *, host_clock_provider, void *state);

/* Private scalar transport for the authored host:clock package. Registration
 * makes compilation possible; only installing HOST_CLOCK_SERVICE grants access.
 * Missing service returns a denied status, not an activation failure. */
const native_capability_pack *host_clock_package();

#endif
