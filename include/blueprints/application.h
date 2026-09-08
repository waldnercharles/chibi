#ifndef BLUEPRINTS_APPLICATION_H
#define BLUEPRINTS_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLUEPRINTS_APPLICATION_ABI 1
#define BLUEPRINTS_APPLICATION_SYMBOL "blueprints_application"

/* Only this contract crosses the native code-generation boundary. Checkpoints
 * contain UTF-8 JSON, never native pointers. Allocate returned checkpoint bytes
 * with host->allocate; the host owns and releases them. */
typedef struct
{
    int argc;
    char **argv;
    void *(*allocate)(size_t);
    void (*release)(void *);
} bp_host;

typedef struct
{
    uint32_t abi;
    /* Creation/import prepares an inactive candidate. It must not replace
     * process registrations owned by the running application. */
    void *(*create)(const bp_host *, const char *checkpoint, char *error, size_t);
    char *(*checkpoint)(void *);
    void (*activate)(void *);
    void (*deactivate)(void *);
    /* Zero continues, positive exits normally, negative reports failure. */
    int (*frame)(void *);
    void (*destroy)(void *);
} bp_application_api;

typedef const bp_application_api *(*bp_application_entry)();

#if defined(_WIN32)
#define BP_APPLICATION_EXPORT __declspec(dllexport)
#else
#define BP_APPLICATION_EXPORT __attribute__((visibility("default")))
#endif
BP_APPLICATION_EXPORT const bp_application_api *blueprints_application();

#endif
