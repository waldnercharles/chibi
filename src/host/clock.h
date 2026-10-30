#ifndef CHIBI_HOST_CLOCK_H
#define CHIBI_HOST_CLOCK_H

#include <stdint.h>

typedef enum
{
    HOST_CLOCK_OK,
    HOST_CLOCK_UNAVAILABLE,
    HOST_CLOCK_FAILED,
} host_clock_status;

/* Samples the platform's monotonic clock without allocating. Both output
 * pointers are required. The epoch is unspecified; samples are not wall time.
 * Success writes nonnegative nanoseconds. Failure writes zero nanoseconds;
 * platform_error is errno for a failed sample or ERANGE for an invalid or
 * overflowing sample, and zero when the platform is unavailable. */
host_clock_status host_clock_read(int64_t *nanoseconds, int *platform_error);

#endif
