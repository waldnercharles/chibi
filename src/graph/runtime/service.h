#ifndef CHIBI_SERVICE_H
#define CHIBI_SERVICE_H

#include <stdint.h>

typedef struct service_desc
{
    const char *name;
    uint64_t stable_id;
} service_desc;

typedef struct service_binding
{
    const char *type_name;
    void *value; /* borrowed host pointer */
} service_binding;

#endif
