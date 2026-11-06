#include "authoring/source_link.h"

#include <assert.h>
#include <string.h>

static bool library_host(const authoring_source_link *link, uint64_t capability, int entry, script_lower_host *out)
{
    const script_catalog_capability
        *signature = link->catalog
                       ? script_catalog_capability_by_id(link->catalog, capability)
                       : NULL;
    const char *name = NULL;
    if (entry >= 0) {
        if (!signature || entry >= signature->entry_count) {
            return false;
        }
        name = signature->entries[entry];
    }
    for (int i = 0; i < link->library_count; i++) {
        execution_module *module = link->libraries[i];
        assert(module);
        for (int j = 0; j < execution_module_function_count(module); j++) {
            execution_module_function_info function = execution_module_function_at(module, j);
            bool default_entry = !name || !strcmp(name, "entry");
            bool selected = default_entry
                              ? function.id == capability
                              : function.kind == EXECUTION_FUNCTION_MACRO_ENTRY &&
                                    function.parent == capability &&
                                    function.entry && !strcmp(function.entry, name);
            if (selected && function.body) {
                *out = (script_lower_host){ .function = function.body, .module = module };
                return true;
            }
        }
    }
    return false;
}
static bool host(void *user, uint64_t capability, script_lower_host *out)
{
    const authoring_source_link *link = user;
    return library_host(link, capability, -1, out) ||
           (link->native.host && link->native.host(link->native.user, capability, out));
}
static bool action(void *user, uint64_t capability, uint64_t instance, int entry, script_lower_host *out)
{
    const authoring_source_link *link = user;
    return library_host(link, capability, entry, out) ||
           (link->native.action &&
            link->native.action(link->native.user, capability, instance, entry, out));
}
static bool global(void *user, uint64_t capability, behavior_function_instruction *out)
{
    const authoring_source_link *link = user;
    return link->native.global &&
           link->native.global(link->native.user, capability, out);
}
script_lower_bindings authoring_source_bindings(const authoring_source_link *link)
{
    assert(link && link->library_count >= 0);
    return (
        script_lower_bindings
    ){ .user = (void *)link, .host = host, .action = action, .global = global };
}
