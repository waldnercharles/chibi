#ifndef CHIBI_APPLICATION_H
#define CHIBI_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHIBI_APPLICATION_ABI 1
#define CHIBI_APPLICATION_SYMBOL "chibi_application"

/* Only this contract crosses the native code-generation boundary. Checkpoints
 * contain UTF-8 JSON, never native pointers. Allocate returned checkpoint bytes
 * with host->allocate; the host owns and releases them. */
typedef struct
{
    int argc;
    char **argv;
    void *(*allocate)(size_t);
    void (*release)(void *);
} chibi_host;

typedef struct
{
    uint32_t abi;
    /* Creation/import prepares an inactive candidate. It must not replace
     * process registrations owned by the running application. */
    void *(*create)(const chibi_host *, const char *checkpoint, char *error, size_t);
    char *(*checkpoint)(void *);
    void (*activate)(void *);
    void (*deactivate)(void *);
    /* Zero continues, positive exits normally, negative reports failure. */
    int (*frame)(void *);
    void (*destroy)(void *);
} chibi_application_api;

typedef const chibi_application_api *(*chibi_application_entry)();

#if defined(_WIN32)
#define CHIBI_APPLICATION_EXPORT __declspec(dllexport)
#else
#define CHIBI_APPLICATION_EXPORT __attribute__((visibility("default")))
#endif
CHIBI_APPLICATION_EXPORT const chibi_application_api *chibi_application();

#endif
