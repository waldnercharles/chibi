#include "module.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
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
    chibi_module candidate = { .library = dlopen(path, RTLD_NOW | RTLD_LOCAL) };
    if (!candidate.library) {
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
        return fail(error, size, "incompatible application lifecycle");
    }
    char *checkpoint = current->api ? current->api->checkpoint(current->state) : NULL;
    if (current->api && !checkpoint) {
        dlclose(candidate.library);
        return fail(error, size, "could not checkpoint active workspace");
    }
    candidate.state = api->create(host, checkpoint ? checkpoint : initial_checkpoint, error, size);
    host->release(checkpoint);
    if (!candidate.state) {
        dlclose(candidate.library);
        return false;
    }
    /* Creation and checkpoint import have succeeded. No fallible operation is
     * allowed after deactivating the old generation. Destruction uses old code. */
    if (current->api) {
        current->api->deactivate(current->state);
        current->api->destroy(current->state);
    }
    void *old_library = current->library;
    *current = candidate;
    current->api->activate(current->state);
    if (old_library) {
        dlclose(old_library);
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
    *module = (chibi_module){ 0 };
}
