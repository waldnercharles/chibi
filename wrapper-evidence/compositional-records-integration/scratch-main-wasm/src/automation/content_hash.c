#include "automation/content_hash.h"

uint64_t automation_content_hash(const char *text, size_t bytes)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < bytes; i++) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
