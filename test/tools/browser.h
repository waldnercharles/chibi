#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Serves one in-memory page and an explicit file allowlist to headless Chrome.
 * Receives one bounded /result POST. No GPU; one 55-second browser deadline. */
bool tool_browser(const char *root, const char *page, const char *const files[], char *result, size_t capacity);
