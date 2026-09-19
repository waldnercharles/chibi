#include "automation/gpu_capture.h"

#include <stdio.h>

bool chibi_gpu_capture_begin(const char *, char *error, size_t size)
{
    snprintf(error, size, "Metal trace capture requires macOS; use RenderDoc or a native GPU profiler on this platform");
    return false;
}

bool chibi_gpu_capture_end(const char *, char *, size_t)
{
    return false;
}
