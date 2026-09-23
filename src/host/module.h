#ifndef CHIBI_HOST_MODULE_H
#define CHIBI_HOST_MODULE_H

#include <chibi/application.h>

typedef struct
{
    void *library;
    const chibi_application_api *api;
    void *state;
    int lease;
} chibi_module;

/* Paths identify immutable, uniquely named completed libraries. Candidate
 * failure leaves current active, including its registrations and state. */
bool chibi_module_replace(
    chibi_module *current,
    const char *path,
    const chibi_host *host,
    const char *initial_checkpoint,
    char *error,
    size_t size
);
void chibi_module_close(chibi_module *module);

#endif
