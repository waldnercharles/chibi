#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "host/clock.h"

#include <assert.h>
#include <errno.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) &&                            \
    (defined(__APPLE__) || defined(__linux__))
#include <time.h>

#define HOST_CLOCK_NANOSECONDS_PER_SECOND UINT64_C(1000000000)
#endif

host_clock_status host_clock_read(int64_t *nanoseconds, int *platform_error)
{
    assert(nanoseconds && platform_error);
    *nanoseconds = 0;
    *platform_error = 0;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) &&                            \
    (defined(__APPLE__) || defined(__linux__))
#ifdef __APPLE__
    const clockid_t clock = CLOCK_UPTIME_RAW;
#else
    const clockid_t clock = CLOCK_MONOTONIC;
#endif
    struct timespec now;
    if (clock_gettime(clock, &now)) {
        *platform_error = errno;
        return HOST_CLOCK_FAILED;
    }
    if (now.tv_sec < 0 || now.tv_nsec < 0 ||
        (uintmax_t)now.tv_nsec >= HOST_CLOCK_NANOSECONDS_PER_SECOND) {
        *platform_error = ERANGE;
        return HOST_CLOCK_FAILED;
    }
    uintmax_t seconds = (uintmax_t)now.tv_sec;
    if (seconds > ((uintmax_t)INT64_MAX - (uintmax_t)now.tv_nsec) /
                      HOST_CLOCK_NANOSECONDS_PER_SECOND) {
        *platform_error = ERANGE;
        return HOST_CLOCK_FAILED;
    }
    *nanoseconds = (int64_t)(seconds * HOST_CLOCK_NANOSECONDS_PER_SECOND +
                             (uintmax_t)now.tv_nsec);
    return HOST_CLOCK_OK;
#else
    return HOST_CLOCK_UNAVAILABLE;
#endif
}
