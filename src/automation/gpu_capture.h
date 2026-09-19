#ifndef CHIBI_AUTOMATION_GPU_CAPTURE_H
#define CHIBI_AUTOMATION_GPU_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>

/* Tooling only. Begin before acquiring a frame command buffer, end after its
 * submission. Normal benchmarks never enable capture instrumentation. */
bool chibi_gpu_capture_begin(const char *path, char *error, size_t size);
bool chibi_gpu_capture_end(const char *path, char *error, size_t size);

#endif
