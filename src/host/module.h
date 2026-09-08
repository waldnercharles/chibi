#ifndef BLUEPRINTS_HOST_MODULE_H
#define BLUEPRINTS_HOST_MODULE_H

#include <blueprints/application.h>

typedef struct
{
    void *library;
    const bp_application_api *api;
    void *state;
} bp_module;

/* Paths identify immutable, uniquely named completed libraries. Candidate
 * failure leaves current active, including its registrations and state. */
bool bp_module_replace(
    bp_module *current,
    const char *path,
    const bp_host *host,
    const char *initial_checkpoint,
    char *error,
    size_t size
);
void bp_module_close(bp_module *module);

#endif
