#ifndef CHIBI_SERVICE_H
#define CHIBI_SERVICE_H

#include <stdint.h>

typedef struct service_desc
{
    const char *name;
    uint64_t stable_id;
} service_desc;

/* Collection arguments are NULL or support/array.h dynamic vectors populated
 * with apush; APIs obtain their length with acount. A fixed C array or sentinel
 * array is not a valid collection. Program constructors copy binding rows and
 * type_name strings, allowing the input vector and names to be freed after
 * return. Host values are borrowed and must outlive programs using them. */
typedef struct service_binding
{
    const char *type_name;
    void *value; /* borrowed host pointer */
} service_binding;

#endif
