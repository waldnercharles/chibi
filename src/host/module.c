#include "module.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}

static int generation_lease(const char *path)
{
    const char *marker = strstr(path, "/.chibi/generations/");
    if (!marker) {
        return -1;
    }
    const char *generation = marker + strlen("/.chibi/generations/");
    const char *filename = strchr(generation, '/');
    if (!filename || strchr(filename + 1, '/')) {
        errno = EINVAL;
        return -2;
    }
    size_t length = (size_t)(filename - path);
    char *lease_path = malloc(length + sizeof "/.lease");
    if (!lease_path) {
        abort();
    }
    snprintf(lease_path, length + sizeof "/.lease", "%.*s/.lease", (int)length, path);
    int lease = open(lease_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    free(lease_path);
    if (lease < 0) {
        return -2;
    }
    if (flock(lease, LOCK_SH) == 0) {
        return lease;
    }
    int error = errno;
    close(lease);
    errno = error;
    return -2;
}

bool chibi_module_replace(
    chibi_module *current,
    const char *path,
    const chibi_host *host,
    const char *initial_checkpoint,
    char *error,
    size_t size
)
{
    int lease = generation_lease(path);
    if (lease == -2) {
        return fail(error, size, strerror(errno));
    }
    chibi_module candidate = { .library = dlopen(path, RTLD_NOW | RTLD_LOCAL),
                               .lease = lease };
    if (!candidate.library) {
        if (lease >= 0) {
            close(lease);
        }
        return fail(error, size, dlerror());
    }
    chibi_application_entry entry = NULL;
    void *symbol = dlsym(candidate.library, CHIBI_APPLICATION_SYMBOL);
    _Static_assert(sizeof entry == sizeof symbol, "POSIX function pointer size");
    memcpy(&entry, &symbol, sizeof entry);
    candidate.api = entry ? entry() : NULL;
    const chibi_application_api *api = candidate.api;
    if (!api || api->abi != CHIBI_APPLICATION_ABI || !api->create || !api->checkpoint ||
        !api->activate || !api->deactivate || !api->frame || !api->destroy) {
        dlclose(candidate.library);
        if (lease >= 0) {
            close(lease);
        }
        return fail(error, size, "incompatible application lifecycle");
    }
    char *checkpoint = current->api ? current->api->checkpoint(current->state) : NULL;
    if (current->api && !checkpoint) {
        dlclose(candidate.library);
        if (lease >= 0) {
            close(lease);
        }
        return fail(error, size, "could not checkpoint active workspace");
    }
    candidate.state = api->create(host, checkpoint ? checkpoint : initial_checkpoint, error, size);
    host->release(checkpoint);
    if (!candidate.state) {
        dlclose(candidate.library);
        if (lease >= 0) {
            close(lease);
        }
        return false;
    }
    /* Creation and checkpoint import have succeeded. No fallible operation is
     * allowed after deactivating the old generation. Destruction uses old code. */
    if (current->api) {
        current->api->deactivate(current->state);
        current->api->destroy(current->state);
    }
    void *old_library = current->library;
    int old_lease = current->lease;
    *current = candidate;
    current->api->activate(current->state);
    if (old_library) {
        dlclose(old_library);
        if (old_lease >= 0) {
            close(old_lease);
        }
    }
    return true;
}

void chibi_module_close(chibi_module *module)
{
    if (!module->api) {
        return;
    }
    module->api->deactivate(module->state);
    module->api->destroy(module->state);
    if (module->library) {
        dlclose(module->library);
    }
    if (module->lease >= 0) {
        close(module->lease);
    }
    *module = (chibi_module){ 0 };
}
